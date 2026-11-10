#include <winsock2.h>
#include <ws2tcpip.h>

#include "unit/script/ScriptFixture.h"

#include <gs/script/Permissions.h>
#include <gs/script/Services.h>
#include <gs/scripthost/RemoteLogicHost.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using gs::script::ControlInfo;
using gs::script::DeviceInfo;
using gs::script::Point;
using gs::script::Report;
using gs::script::ReportKind;
using gs::script::ScriptId;
using gs::script::ScriptSource;
using gs::script::ServiceAnswer;
using gs::script::StateInfo;
using gs::script::TaskId;
using gs::script::VarValue;
using gs::script::test::logged;
using gs::script::test::loadedOrFailed;
using gs::script::test::Recorder;
using gs::script::test::ScriptFolder;
using gs::script::test::taskEnded;
using gs::scripthost::RemoteLogicHost;
using gs::scripthost::RemoteOptions;
using gs::scripthost::SandboxMode;

namespace {

using Clock = std::chrono::steady_clock;

class PhoneServices final : public gs::script::ScriptServices
{
public:
    DeviceInfo device() override { return {"Pixel", 15, 1080, 2400, 0}; }

    void tap(std::uint64_t, Point at, double durationMs) override
    {
        {
            const std::scoped_lock lock(m_mutex);
            ++m_taps;
        }
        record(std::format("tap {} {} {}", at.x, at.y, durationMs));
    }

    void swipe(std::uint64_t, Point from, Point to, double durationMs) override
    {
        record(std::format("swipe {} {} {} {} {}", from.x, from.y, to.x, to.y, durationMs));
    }

    void key(std::string_view name, bool down) override { record(std::format("key {} {}", name, down ? "down" : "up")); }

    std::vector<ControlInfo> controls() override
    {
        return {{"jump", "Jump", "tap", true, R"({"inputs":{"press":"Space"},"tapMs":40})"}};
    }

    bool pressControl(std::string_view, bool) override { return false; }
    bool enableControl(std::string_view, bool) override { return false; }
    std::vector<StateInfo> states() override { return {}; }
    std::optional<VarValue> stateValue(std::string_view) override { return std::nullopt; }
    bool setState(std::string_view, const VarValue&) override { return false; }
    std::optional<std::string> loadStore(std::string_view) override { return std::nullopt; }
    void saveStore(std::string_view, std::string_view contents) override { record(std::format("store {}", contents)); }

    void readClipboard(std::uint64_t request) override
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_requests.push_back(request);
        }
        record("clipboard read");
    }

    bool offersService(std::string_view name) override { return name.starts_with("mcp."); }

    ServiceAnswer callService(std::string_view name, std::string_view json) override
    {
        record(std::format("service {} {}", name, json));
        return {"true", {}, {}};
    }

    [[nodiscard]] bool waitFor(std::string_view text, std::chrono::milliseconds timeout = std::chrono::seconds(30))
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, timeout, [&] { return std::ranges::find(m_events, text) != m_events.end(); });
    }

    [[nodiscard]] bool waitForTaps(int count, std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, timeout, [&] { return m_taps >= count; });
    }

    [[nodiscard]] std::optional<std::uint64_t> waitForRequest(std::size_t index)
    {
        std::unique_lock lock(m_mutex);
        if (!m_changed.wait_for(lock, std::chrono::seconds(30), [&] { return m_requests.size() > index; })) {
            return std::nullopt;
        }
        return m_requests[index];
    }

    [[nodiscard]] std::vector<std::string> events() const
    {
        const std::scoped_lock lock(m_mutex);
        return m_events;
    }

private:
    void record(std::string event)
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_events.push_back(std::move(event));
        }
        m_changed.notify_all();
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<std::string> m_events;
    std::vector<std::uint64_t> m_requests;
    int m_taps = 0;
};

RemoteOptions options()
{
    return RemoteOptions{.program = std::filesystem::path(GS_SCRIPTHOST_PROGRAM), .label = "test logic"};
}

ScriptSource extensionSource(const ScriptFolder& folder, std::vector<std::string> permissions)
{
    ScriptSource source = folder.source();
    source.permissions = std::move(permissions);
    return source;
}

class Loopback
{
public:
    Loopback()
    {
        WSADATA data{};
        m_started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        m_socket = m_started ? socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) : INVALID_SOCKET;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        int size = static_cast<int>(sizeof(address));
        if (m_socket != INVALID_SOCKET &&
            bind(m_socket, reinterpret_cast<const sockaddr*>(&address), static_cast<int>(sizeof(address))) == 0 &&
            listen(m_socket, 4) == 0 && getsockname(m_socket, reinterpret_cast<sockaddr*>(&address), &size) == 0) {
            m_port = ntohs(address.sin_port);
        }
    }

    ~Loopback()
    {
        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket);
        }
        if (m_started) {
            WSACleanup();
        }
    }

    Loopback(const Loopback&) = delete;
    Loopback& operator=(const Loopback&) = delete;

    [[nodiscard]] std::uint16_t port() const noexcept { return m_port; }

private:
    bool m_started = false;
    SOCKET m_socket = INVALID_SOCKET;
    std::uint16_t m_port = 0;
};

std::filesystem::path profileFolder()
{
    wchar_t* value = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&value, &length, L"USERPROFILE") != 0 || value == nullptr) {
        return std::filesystem::temp_directory_path();
    }
    std::filesystem::path folder(value);
    std::free(value);
    return folder;
}

class ProbeFile
{
public:
    ProbeFile()
        : m_path(profileFolder() / ("gs-sandbox-probe-" + std::to_string(std::random_device{}()) + ".txt"))
    {
        std::ofstream out(m_path, std::ios::binary);
        out << "only the user may read this";
    }

    ~ProbeFile()
    {
        std::error_code ignored;
        std::filesystem::remove(m_path, ignored);
    }

    ProbeFile(const ProbeFile&) = delete;
    ProbeFile& operator=(const ProbeFile&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

}

TEST_CASE("logic in the sandbox taps, swipes, reads the phone, calls a service, hears events and gets answers, and "
          "its reports come back")
{
    Recorder recorder;
    const auto services = std::make_shared<PhoneServices>();
    ScriptFolder folder;
    folder.write("main.js", R"js(
mcp.tool("echo", { description: "Echoes the text" }, async (input) => input.text);
export async function play() {
    await touch.tap(0.5, 0.25);
    await touch.swipe(0.1, 0.2, 0.3, 0.4, 50);
    store.set("played", true);
    log.info("played", device.info().model);
}
export async function paste() {
    log.info("clipboard", await clipboard.read());
}
)js");
    const auto started = Clock::now();
    RemoteLogicHost host(recorder.sink(), gs::script::Limits{}, services, options());
    const ScriptId script = host.load(extensionSource(folder, {"device.input", "device.clipboard"}));
    host.event("custom", "early", "{}");
    const auto returned = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started);
    MESSAGE("constructing the remote host and queueing a load took ", returned.count(), " ms");
    CHECK(returned < std::chrono::milliseconds(250));

    const auto loaded = recorder.waitFor(loadedOrFailed(script));
    REQUIRE(loaded.has_value());
    INFO(gs::script::describe(loaded->report));
    REQUIRE(loaded->report.kind == ReportKind::ScriptLoaded);
    CHECK(host.alive());
    CHECK(host.mode() != SandboxMode::None);
    MESSAGE("the logic sandbox runs in ", std::string(gs::scripthost::modeName(host.mode())));
    CHECK(services->waitFor(R"(service mcp.register {"name":"echo","description":"Echoes the text","input":{"type":"object"}})"));

    const TaskId play = host.start(script, "play");
    const auto played = recorder.waitFor(taskEnded(play));
    REQUIRE(played.has_value());
    INFO(gs::script::describe(played->report));
    CHECK(played->report.kind == ReportKind::TaskFinished);
    CHECK(recorder.count(logged("played Pixel")) == 1);
    CHECK(services->waitFor("tap 0.5 0.25 60"));
    CHECK(services->waitFor("swipe 0.1 0.2 0.3 0.4 50"));
    CHECK(services->waitFor(R"(store {"played":true})"));

    host.event("mcp", "3", R"({"tool":"echo","arguments":{"text":"hi"}})");
    CHECK(services->waitFor(R"(service mcp.result {"call":"3","ok":true,"value":"hi"})"));

    const TaskId paste = host.start(script, "paste");
    const std::optional<std::uint64_t> request = services->waitForRequest(0);
    REQUIRE(request.has_value());
    host.answer(*request, R"("from the phone")");
    const auto pasted = recorder.waitFor(taskEnded(paste));
    REQUIRE(pasted.has_value());
    CHECK(pasted->report.kind == ReportKind::TaskFinished);
    CHECK(recorder.count(logged("clipboard from the phone")) == 1);
}

TEST_CASE("the sandbox raises GS-API-072 itself for what the extension may not do")
{
    Recorder recorder;
    const auto services = std::make_shared<PhoneServices>();
    ScriptFolder folder;
    folder.write("main.js", R"js(
try {
    touch.tap(0.5, 0.5);
} catch (error) {
    log.info(error.message);
}
)js");
    RemoteLogicHost host(recorder.sink(), gs::script::Limits{}, services, options());
    const ScriptId script = host.load(extensionSource(folder, {}));
    const auto loaded = recorder.waitFor(loadedOrFailed(script));
    REQUIRE(loaded.has_value());
    CHECK(loaded->report.kind == ReportKind::ScriptLoaded);
    CHECK(recorder.waitFor(logged(gs::script::permissionProblem("device.input"))).has_value());
    for (const std::string& event : services->events()) {
        CHECK_FALSE(event.starts_with("tap"));
    }
}

TEST_CASE("when the sandbox is killed the logic reports that it stopped, and the next load starts a new one")
{
    Recorder recorder;
    const auto services = std::make_shared<PhoneServices>();
    ScriptFolder folder;
    folder.write("main.js", R"js(
log.info("up");
export async function forever() {
    await sleep(600000);
}
)js");
    RemoteLogicHost host(recorder.sink(), gs::script::Limits{}, services, options());
    const ScriptId first = host.load(extensionSource(folder, {}));
    const auto loaded = recorder.waitFor(loadedOrFailed(first));
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->report.kind == ReportKind::ScriptLoaded);
    static_cast<void>(host.start(first, "forever"));
    host.kill();
    const auto stopped = recorder.waitFor([first](const Report& report) {
        return report.script == first && report.kind == ReportKind::ScriptFailed;
    });
    REQUIRE(stopped.has_value());
    CHECK(stopped->report.message == std::string(gs::scripthost::kStoppedText));

    const ScriptId second = host.load(extensionSource(folder, {}));
    const auto again = recorder.waitFor(loadedOrFailed(second));
    REQUIRE(again.has_value());
    CHECK(again->report.kind == ReportKind::ScriptLoaded);
    CHECK(host.alive());
    CHECK(recorder.count(logged("up")) == 2);
}

TEST_CASE("the sandbox cannot open the user's files, connect to this computer or start other programs")
{
    const ProbeFile probe;
    const Loopback listener;
    REQUIRE(listener.port() != 0);
    const std::filesystem::path program(GS_SCRIPTHOST_PROGRAM);

    const gs::scripthost::SelfTestResult result = gs::scripthost::runSelfTest(program, probe.path(), listener.port());
    INFO(result.text);
    INFO(result.problem);
    REQUIRE(result.mode != SandboxMode::None);
    MESSAGE("self-test in ", std::string(gs::scripthost::modeName(result.mode)), ":\n", result.text);
    CHECK(result.file);
    CHECK(result.process);
    if (result.mode == SandboxMode::AppContainer) {
        CHECK(result.socket);
    } else {
        WARN(result.socket);
    }

    const gs::scripthost::SelfTestResult restricted =
        gs::scripthost::runSelfTest(program, probe.path(), listener.port(), true);
    INFO(restricted.text);
    INFO(restricted.problem);
    REQUIRE(restricted.mode == SandboxMode::Restricted);
    MESSAGE("self-test with a restricted token:\n", restricted.text);
    CHECK(restricted.file);
    CHECK(restricted.process);
    WARN(restricted.socket);
}

TEST_CASE("200 key presses become taps through the sandbox fast enough")
{
    Recorder recorder;
    const auto services = std::make_shared<PhoneServices>();
    ScriptFolder folder;
    folder.write("main.js", R"js(
controls.jump.onPress(() => {
    touch.tap(0.5, 0.5, 10);
});
log.info("listening");
)js");
    RemoteLogicHost host(recorder.sink(), gs::script::Limits{}, services, options());
    const ScriptId script = host.load(extensionSource(folder, {"device.input"}));
    const auto loaded = recorder.waitFor(loadedOrFailed(script));
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->report.kind == ReportKind::ScriptLoaded);
    REQUIRE(recorder.waitFor(logged("listening")).has_value());

    std::vector<double> micros;
    micros.reserve(200);
    for (int press = 1; press <= 200; ++press) {
        const auto at = Clock::now();
        host.controlEvent("jump", true);
        REQUIRE(services->waitForTaps(press, std::chrono::seconds(5)));
        micros.push_back(std::chrono::duration<double, std::micro>(Clock::now() - at).count());
        host.controlEvent("jump", false);
    }
    const double p99 = gs::script::test::percentile(micros, 0.99);
    MESSAGE("key to tap through the sandbox: ", gs::script::test::summary(micros, "us"));
    CHECK(p99 < 20000.0);
}
