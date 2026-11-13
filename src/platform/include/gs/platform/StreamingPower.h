#pragma once

#include "gs/base/ThreadChecker.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace gs::platform {

enum class StreamingLink : std::uint8_t { Wired, Wireless };

class StreamingPowerTarget
{
public:
    virtual ~StreamingPowerTarget() = default;

    [[nodiscard]] virtual std::uint32_t setMediaStreaming(bool on) noexcept = 0;
    [[nodiscard]] virtual std::uint32_t setExecutionSpeed(bool on) noexcept = 0;
};

class StreamingPower
{
public:
    explicit StreamingPower(StreamingPowerTarget& target) noexcept : m_target(target) {}
    ~StreamingPower() = default;
    StreamingPower(const StreamingPower&) = delete;
    StreamingPower& operator=(const StreamingPower&) = delete;

    void begin(std::string_view session, StreamingLink link) noexcept;
    void end(std::string_view session) noexcept;

    [[nodiscard]] std::uint32_t streamingSessions() const noexcept;
    [[nodiscard]] std::uint32_t wirelessSessions() const noexcept;

private:
    static constexpr std::size_t kMaxSessions = 256;

    void refresh() noexcept;

    StreamingPowerTarget& m_target;
    std::map<std::string, StreamingLink, std::less<>> m_sessions;
    bool m_mediaStreaming = false;
    bool m_executionSpeed = false;
    bool m_mediaReported = false;
    bool m_speedReported = false;
    ThreadChecker m_thread;
};

class SystemStreamingPower final : public StreamingPowerTarget
{
public:
    SystemStreamingPower() noexcept;
    ~SystemStreamingPower() override;
    SystemStreamingPower(const SystemStreamingPower&) = delete;
    SystemStreamingPower& operator=(const SystemStreamingPower&) = delete;

    [[nodiscard]] std::uint32_t setMediaStreaming(bool on) noexcept override;
    [[nodiscard]] std::uint32_t setExecutionSpeed(bool on) noexcept override;

    [[nodiscard]] std::uint64_t mediaStreamingApplied() const noexcept;

private:
    struct Worker;

    std::unique_ptr<Worker> m_worker;
};

}
