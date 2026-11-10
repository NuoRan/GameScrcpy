#include "gs/platform/Foreground.h"
#include "gs/platform/InputCapture.h"

#include <doctest/doctest.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

using namespace gs;
using namespace gs::platform;
using namespace std::chrono_literals;

namespace {

constexpr USHORT kUsageMouse = 0x02;
constexpr USHORT kUsageGamepad = 0x05;
constexpr USHORT kUsageKeyboard = 0x06;

class Recorder final : public InputHandler
{
public:
    void key(const RawKey&, Microseconds) override { keys.fetch_add(1); }
    void mouse(const RawMouse&, Microseconds) override { mice.fetch_add(1); }
    void gamepad(const PadState&, Microseconds) override { pads.fetch_add(1); }

    void wake(Microseconds now) override
    {
        const std::scoped_lock lock(mutex);
        wakes.push_back(now);
        due.reset();
        changed.notify_all();
    }

    [[nodiscard]] std::optional<Microseconds> nextWakeup() const override
    {
        const std::scoped_lock lock(mutex);
        return due;
    }

    void wakeAt(Microseconds when)
    {
        const std::scoped_lock lock(mutex);
        due = when;
    }

    template <typename Predicate>
    bool waitFor(std::chrono::milliseconds limit, Predicate&& predicate)
    {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, limit, std::forward<Predicate>(predicate));
    }

    std::atomic<int> keys{0};
    std::atomic<int> mice{0};
    std::atomic<int> pads{0};
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::optional<Microseconds> due;
    std::vector<Microseconds> wakes;
};

std::optional<HWND> registeredTarget(USHORT usage)
{
    UINT count = 0;
    GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE));
    if (count == 0) {
        return std::nullopt;
    }
    std::vector<RAWINPUTDEVICE> devices(count);
    if (GetRegisteredRawInputDevices(devices.data(), &count, sizeof(RAWINPUTDEVICE)) == static_cast<UINT>(-1)) {
        return std::nullopt;
    }
    for (const RAWINPUTDEVICE& device : devices) {
        if (device.usUsagePage == 0x01 && device.usUsage == usage) {
            return device.hwndTarget;
        }
    }
    return std::nullopt;
}

bool registered(USHORT usage)
{
    return registeredTarget(usage).has_value();
}

template <typename Predicate>
bool eventually(Predicate&& predicate, std::chrono::milliseconds limit = 2000ms)
{
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(2ms);
    }
    return true;
}

}

TEST_CASE("the capture thread runs posted tasks in order on its own thread and stops cleanly (ADR-0069)")
{
    Recorder recorder;
    std::vector<int> order;
    std::mutex mutex;
    std::thread::id worker;
    {
        InputCapture capture(recorder);
        REQUIRE(capture.started());
        CHECK(capture.problem().empty());
        for (int i = 0; i < 50; ++i) {
            capture.post([&, i] {
                const std::scoped_lock lock(mutex);
                order.push_back(i);
                worker = std::this_thread::get_id();
            });
        }
        CHECK(eventually([&] {
            const std::scoped_lock lock(mutex);
            return order.size() == 50;
        }));
    }
    CHECK(std::is_sorted(order.begin(), order.end()));
    CHECK(worker != std::this_thread::get_id());
    CHECK(worker != std::thread::id{});
}

TEST_CASE("the capture thread wakes the handler at its next wakeup and stays idle without one (ADR-0069)")
{
    Recorder recorder;
    InputCapture capture(recorder);
    REQUIRE(capture.started());
    const Microseconds start = InputCapture::now();
    capture.post([&] { recorder.wakeAt(start + Microseconds{20'000}); });
    CHECK(recorder.waitFor(2000ms, [&] { return !recorder.wakes.empty(); }));
    {
        const std::scoped_lock lock(recorder.mutex);
        REQUIRE(recorder.wakes.size() == 1);
        CHECK(recorder.wakes.front() >= start + Microseconds{20'000});
    }
    std::this_thread::sleep_for(100ms);
    const std::scoped_lock lock(recorder.mutex);
    CHECK(recorder.wakes.size() == 1);
}

TEST_CASE("keyboard and mouse are captured only while there is a target, gamepad arrival always (ADR-0069)")
{
    const HWND hidden = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
    REQUIRE(hidden != nullptr);
    const auto window = reinterpret_cast<std::uintptr_t>(hidden);
    Recorder recorder;
    {
        InputCapture capture(recorder);
        REQUIRE(capture.started());
        CHECK(registered(kUsageGamepad));
        CHECK_FALSE(registered(kUsageKeyboard));
        CHECK_FALSE(registered(kUsageMouse));

        capture.setCaptureTarget(window);
        CHECK(eventually([] { return registered(kUsageKeyboard) && registered(kUsageMouse); }));
        const std::optional<HWND> target = registeredTarget(kUsageKeyboard);
        REQUIRE(target.has_value());
        CHECK(*target != nullptr);
        CHECK(*target != hidden);

        capture.setCaptureTarget(0);
        CHECK(eventually([] { return !registered(kUsageKeyboard) && !registered(kUsageMouse); }));
        capture.setCaptureTarget(window);
        CHECK(eventually([] { return registered(kUsageKeyboard); }));
        CHECK(capture.problem().empty());
    }
    CHECK_FALSE(registered(kUsageKeyboard));
    CHECK_FALSE(registered(kUsageMouse));
    CHECK_FALSE(registered(kUsageGamepad));
    CHECK(recorder.keys.load() == 0);
    CHECK(recorder.mice.load() == 0);
    DestroyWindow(hidden);
}

TEST_CASE("only a window that is, or owns, the system foreground window counts as in front (ADR-0069)")
{
    CHECK_FALSE(isInFront(0));
    const HWND hidden = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
    REQUIRE(hidden != nullptr);
    CHECK_FALSE(isInFront(reinterpret_cast<std::uintptr_t>(hidden)));
    DestroyWindow(hidden);
}

TEST_CASE("the foreground watcher hooks foreground changes and unhooks on destruction (ADR-0069)")
{
    int calls = 0;
    {
        const ForegroundWatcher first([&calls] { ++calls; });
        const ForegroundWatcher second([&calls] { ++calls; });
        CHECK(first.started());
        CHECK(second.started());
    }
    const ForegroundWatcher again([&calls] { ++calls; });
    CHECK(again.started());
    CHECK(calls == 0);
}
