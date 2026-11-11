#pragma once

#include "gs/base/Expected.h"
#include "gs/platform/TcpStream.h"
#include "gs/session/ServerHost.h"
#include "gs/session/SessionState.h"
#include "gs/session/Transport.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace gs::session {

class ByteStream
{
public:
    virtual ~ByteStream() = default;

    virtual void setReadTimeout(std::chrono::milliseconds timeout) noexcept = 0;
    virtual void setWriteTimeout(std::chrono::milliseconds timeout) noexcept = 0;
    [[nodiscard]] virtual bool writeAll(std::span<const std::uint8_t> bytes) = 0;
    [[nodiscard]] virtual bool readExact(std::span<std::uint8_t> bytes) = 0;
    [[nodiscard]] virtual std::size_t readSome(std::span<std::uint8_t> bytes) = 0;
    [[nodiscard]] virtual bool readTimedOut() const noexcept = 0;
    [[nodiscard]] virtual std::string closedReason() const = 0;
    virtual void interrupt() noexcept = 0;
};

enum class StreamChannel : std::uint8_t { Control, Video, Audio, Bulk };

[[nodiscard]] std::string_view streamChannelName(StreamChannel channel) noexcept;

enum class StreamSend : std::uint8_t { Taken, Busy, Failed };

class TransportStream final
{
public:
    using Sender = std::function<StreamSend(std::span<const std::uint8_t> bytes)>;
    using Closer = std::function<void()>;

    enum class Opening : std::uint8_t { Opened, Refused, TimedOut, Stopped };

    static constexpr std::size_t kChunkBytes = 64 * 1024;
    static constexpr std::size_t kMostHeldBytes = 16 * 1024 * 1024;

    TransportStream(Sender sender, Closer closer);
    TransportStream(const TransportStream&) = delete;
    TransportStream& operator=(const TransportStream&) = delete;

    void opened();
    void deliver(std::span<const std::uint8_t> bytes);
    void writable();
    void closed(std::string reason);
    void shut();

    [[nodiscard]] Opening awaitOpen(std::chrono::steady_clock::time_point deadline, const std::stop_token& stop);
    [[nodiscard]] std::string reason() const;

    void setReadTimeout(std::chrono::milliseconds timeout) noexcept;
    void setWriteTimeout(std::chrono::milliseconds timeout) noexcept;
    [[nodiscard]] bool writeAll(std::span<const std::uint8_t> bytes);
    [[nodiscard]] bool readExact(std::span<std::uint8_t> bytes);
    [[nodiscard]] std::size_t readSome(std::span<std::uint8_t> bytes);
    [[nodiscard]] bool readTimedOut() const noexcept;
    void interrupt() noexcept;

private:
    enum class State : std::uint8_t { Opening, Open, Ended, Failed, Shut };

    [[nodiscard]] std::size_t heldLocked() const noexcept { return m_held.size() - m_taken; }
    void takeLocked(std::span<std::uint8_t> out);
    void failLocked(std::string reason);

    const Sender m_sender;
    const Closer m_closer;
    mutable std::mutex m_mutex;
    std::condition_variable_any m_changed;
    std::mutex m_writeMutex;
    State m_state = State::Opening;
    std::string m_reason;
    std::vector<std::uint8_t> m_held;
    std::size_t m_taken = 0;
    std::uint64_t m_writable = 0;
    bool m_interrupted = false;
    std::atomic<std::int64_t> m_readTimeoutMs{0};
    std::atomic<std::int64_t> m_writeTimeoutMs{0};
    std::atomic<bool> m_readTimedOut{false};
};

[[nodiscard]] std::unique_ptr<ByteStream> sessionEndOf(std::shared_ptr<TransportStream> stream);
[[nodiscard]] Expected<std::unique_ptr<ByteStream>, SessionFailure> awaitStream(std::shared_ptr<TransportStream> stream,
                                                                              std::string_view through,
                                                                              std::chrono::milliseconds timeout,
                                                                              const std::stop_token& stop);

class PhoneStreams
{
public:
    virtual ~PhoneStreams() = default;

    [[nodiscard]] virtual Expected<std::unique_ptr<ByteStream>, SessionFailure> open(StreamChannel channel,
                                                                                    const LaunchParams& params,
                                                                                    std::chrono::milliseconds timeout,
                                                                                    std::stop_token stop) = 0;
    [[nodiscard]] virtual Expected<void, SessionFailure> awaitChange(std::chrono::steady_clock::time_point deadline,
                                                                     std::stop_token stop) = 0;
};

class StreamServerHost final : public ServerHost
{
public:
    explicit StreamServerHost(std::shared_ptr<PhoneStreams> streams) noexcept;

    [[nodiscard]] Expected<void, SessionFailure> start(const LaunchParams& params, Events& events,
                                                       std::stop_token stop) override;
    [[nodiscard]] Expected<platform::TcpStream, SessionFailure> connect() override;
    [[nodiscard]] std::unique_ptr<Transport> transport(std::chrono::milliseconds handshakeTimeout) override;
    [[nodiscard]] Expected<void, SessionFailure> restore(bool afterFailure, std::chrono::steady_clock::time_point deadline,
                                                         std::stop_token stop) override;
    void stop(std::chrono::milliseconds grace) override;

private:
    std::shared_ptr<PhoneStreams> m_streams;
    LaunchParams m_params;
};

}
