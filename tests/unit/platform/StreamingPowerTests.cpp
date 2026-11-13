#include <gs/base/Log.h>
#include <gs/platform/StreamingPower.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;
using gs::platform::StreamingLink;
using gs::platform::StreamingPower;
using gs::platform::StreamingPowerTarget;
using gs::platform::SystemStreamingPower;

class FakeTarget final : public StreamingPowerTarget
{
public:
    std::uint32_t mediaResult = 0;
    std::uint32_t speedResult = 0;
    std::vector<bool> media;
    std::vector<bool> speed;

    [[nodiscard]] std::uint32_t setMediaStreaming(bool on) noexcept override
    {
        media.push_back(on);
        return mediaResult;
    }

    [[nodiscard]] std::uint32_t setExecutionSpeed(bool on) noexcept override
    {
        speed.push_back(on);
        return speedResult;
    }
};

class RecordingSink final : public gs::log::Sink
{
public:
    void write(const gs::log::Record& record) noexcept override
    {
        m_messages.emplace_back(record.message);
    }

    [[nodiscard]] std::size_t failures() const
    {
        return static_cast<std::size_t>(
            std::count_if(m_messages.begin(), m_messages.end(),
                          [](const std::string& text) { return text.find("failed") != std::string::npos; }));
    }

private:
    std::vector<std::string> m_messages;
};

class LogScope
{
public:
    explicit LogScope(gs::log::Sink& sink)
    {
        gs::log::setSink(&sink);
        gs::log::setMinimumLevel(gs::log::Level::Trace);
    }

    ~LogScope()
    {
        gs::log::setSink(nullptr);
        gs::log::setMinimumLevel(gs::log::Level::Info);
    }

    LogScope(const LogScope&) = delete;
    LogScope& operator=(const LogScope&) = delete;
};

void checkCalls(const std::vector<bool>& calls, std::initializer_list<bool> expected)
{
    REQUIRE(calls.size() == expected.size());
    std::size_t index = 0;
    for (const bool each : expected) {
        CHECK(calls[index] == each);
        ++index;
    }
}

}

TEST_CASE("ADR-0114: media streaming mode stays on while any wireless session streams")
{
    FakeTarget target;
    StreamingPower power(target);

    power.begin("phone-a:5555", StreamingLink::Wireless);
    CHECK(power.streamingSessions() == 1U);
    CHECK(power.wirelessSessions() == 1U);
    checkCalls(target.media, {true});
    checkCalls(target.speed, {true});

    power.begin("usb-phone", StreamingLink::Wired);
    CHECK(power.streamingSessions() == 2U);
    CHECK(power.wirelessSessions() == 1U);
    checkCalls(target.media, {true});
    checkCalls(target.speed, {true});

    power.end("phone-a:5555");
    CHECK(power.streamingSessions() == 1U);
    CHECK(power.wirelessSessions() == 0U);
    checkCalls(target.media, {true, false});
    checkCalls(target.speed, {true});

    power.end("usb-phone");
    CHECK(power.streamingSessions() == 0U);
    checkCalls(target.speed, {true, false});
}

TEST_CASE("ADR-0114: a wired session takes the process out of EcoQoS without touching media streaming mode")
{
    FakeTarget target;
    StreamingPower power(target);

    power.begin("usb-phone", StreamingLink::Wired);
    CHECK(power.wirelessSessions() == 0U);
    CHECK(target.media.empty());
    checkCalls(target.speed, {true});

    power.begin("other-usb", StreamingLink::Wired);
    checkCalls(target.speed, {true});

    power.end("usb-phone");
    checkCalls(target.speed, {true});

    power.end("other-usb");
    checkCalls(target.speed, {true, false});
    CHECK(target.media.empty());
}

TEST_CASE("ADR-0114: a repeated start for the same session counts once")
{
    FakeTarget target;
    StreamingPower power(target);

    power.begin("phone", StreamingLink::Wireless);
    power.begin("phone", StreamingLink::Wireless);
    CHECK(power.streamingSessions() == 1U);
    checkCalls(target.media, {true});
    checkCalls(target.speed, {true});

    power.end("phone");
    checkCalls(target.media, {true, false});

    power.end("phone");
    checkCalls(target.media, {true, false});
    CHECK(power.streamingSessions() == 0U);
}

TEST_CASE("ADR-0114: a refused power setting is logged once and changes nothing else")
{
    FakeTarget target;
    target.mediaResult = 5;
    RecordingSink sink;
    const LogScope scope(sink);
    StreamingPower power(target);

    power.begin("phone-a", StreamingLink::Wireless);
    CHECK(sink.failures() == 1U);
    CHECK(power.streamingSessions() == 1U);
    CHECK(power.wirelessSessions() == 1U);

    power.begin("phone-b", StreamingLink::Wireless);
    CHECK(sink.failures() == 1U);

    power.end("phone-a");
    power.end("phone-b");
    CHECK(sink.failures() == 1U);
    CHECK(power.streamingSessions() == 0U);
}

TEST_CASE("ADR-0114: a session that changes its link while streaming moves media streaming mode with it")
{
    FakeTarget target;
    StreamingPower power(target);

    power.begin("phone", StreamingLink::Wired);
    CHECK(target.media.empty());
    checkCalls(target.speed, {true});

    power.begin("phone", StreamingLink::Wireless);
    CHECK(power.streamingSessions() == 1U);
    CHECK(power.wirelessSessions() == 1U);
    checkCalls(target.media, {true});
    checkCalls(target.speed, {true});

    power.begin("phone", StreamingLink::Wired);
    CHECK(power.wirelessSessions() == 0U);
    checkCalls(target.media, {true, false});
    checkCalls(target.speed, {true});

    power.end("phone");
    checkCalls(target.media, {true, false});
    checkCalls(target.speed, {true, false});
}

TEST_CASE("ADR-0114: the system target applies media streaming mode on its own thread and stops cleanly")
{
    const auto appliedAtLeast = [](const SystemStreamingPower& system, std::uint64_t count) {
        const auto deadline = std::chrono::steady_clock::now() + 30s;
        while (system.mediaStreamingApplied() < count && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(5ms);
        }
        return system.mediaStreamingApplied() >= count;
    };

    {
        SystemStreamingPower system;
        CHECK(system.setMediaStreaming(false) == 0U);
        CHECK(system.mediaStreamingApplied() == 0U);
        CHECK(system.setMediaStreaming(true) == 0U);
        CHECK(appliedAtLeast(system, 1));
        CHECK(system.setMediaStreaming(false) == 0U);
        CHECK(system.setMediaStreaming(true) == 0U);
        CHECK(appliedAtLeast(system, 2));
    }
    {
        SystemStreamingPower system;
        CHECK(system.setMediaStreaming(true) == 0U);
    }
    {
        SystemStreamingPower system;
        CHECK(system.setExecutionSpeed(true) == 0U);
        CHECK(system.setExecutionSpeed(false) == 0U);
    }
}
