#pragma once

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gs::exthost::detail {

inline constexpr std::size_t kRingBytes = std::size_t{1} << 20;
inline constexpr std::size_t kMostPayloadBytes = 256 * 1024;

enum class Kind : std::uint32_t {
    Start = 1,
    Ready,
    Failed,
    Call,
    Answer,
    TouchOpen,
    TouchFrame,
    TouchKey,
    TouchClose,
    TouchLost,
    Log,
    Notify,
    Stop,
    TouchText,
    TouchMouse,
    TouchPad,
    InstanceOpen,
    InstanceCall,
    InstanceAnswer,
    InstanceReset,
    InstanceClose,
    JobStart,
    JobCancel,
    JobUpdate,
    JobDone,
};

struct Message {
    Kind kind = Kind::Stop;
    std::uint64_t id = 0;
    std::string payload;
};

enum class ReadResult : std::uint8_t { Read, Empty, Broken };

inline constexpr std::size_t kRingLine = 64;

struct RingHeader {
    alignas(kRingLine) std::atomic<std::uint64_t> head{0};
    std::byte headGap[kRingLine - sizeof(std::atomic<std::uint64_t>)]{};
    alignas(kRingLine) std::atomic<std::uint64_t> tail{0};
    std::byte tailGap[kRingLine - sizeof(std::atomic<std::uint64_t>)]{};
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

class Ring
{
public:
    Ring() = default;
    Ring(RingHeader* header, std::byte* data) noexcept
        : m_header(header)
        , m_data(data)
    {
    }

    [[nodiscard]] bool write(Kind kind, std::uint64_t id, std::string_view payload);
    [[nodiscard]] ReadResult read(Message& out);

private:
    void copyIn(std::uint64_t position, const void* source, std::size_t size);
    void copyOut(std::uint64_t position, void* target, std::size_t size) const;

    RingHeader* m_header = nullptr;
    std::byte* m_data = nullptr;
};

class Channel
{
public:
    Channel() = default;
    ~Channel();
    Channel(Channel&& other) noexcept;
    Channel& operator=(Channel&& other) noexcept;
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    [[nodiscard]] static std::optional<Channel> create();
    [[nodiscard]] static std::optional<Channel> open(std::wstring_view handles);

    [[nodiscard]] std::array<HANDLE, 3> handles() const noexcept { return {m_mapping, m_toHostEvent, m_toAppEvent}; }
    [[nodiscard]] Ring& toHost() noexcept { return m_toHost; }
    [[nodiscard]] Ring& toApp() noexcept { return m_toApp; }
    [[nodiscard]] HANDLE toHostEvent() const noexcept { return m_toHostEvent; }
    [[nodiscard]] HANDLE toAppEvent() const noexcept { return m_toAppEvent; }

private:
    void close() noexcept;
    [[nodiscard]] bool map(bool fresh);

    HANDLE m_mapping = nullptr;
    HANDLE m_toHostEvent = nullptr;
    HANDLE m_toAppEvent = nullptr;
    void* m_view = nullptr;
    Ring m_toHost;
    Ring m_toApp;
};

[[nodiscard]] std::wstring handleText(const std::array<HANDLE, 3>& handles);

}
