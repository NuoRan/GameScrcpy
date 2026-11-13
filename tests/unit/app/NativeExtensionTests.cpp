#include "AppSettings.h"
#include "ExtensionHost.h"
#include "ExtensionModules.h"
#include "ExtensionTouchSink.h"
#include "HardwareAccess.h"
#include "HardwareClaims.h"
#include "KeyLabels.h"
#include "NativeEvents.h"
#include "NativeExtensions.h"
#include "NativeJobs.h"
#include "ProfileEditor.h"
#include "SpeechEngines.h"
#include "TouchSinkFakes.h"

#include "unit/ext/TestFiles.h"

#include <gs/base/Clock.h>
#include <gs/ext/Manifest.h>
#include <gs/exthost/Hosted.h>
#include <gs/exthost/InProcess.h>
#include <gs/hardware/FakeBackend.h>
#include <gs/input/DeviceEvents.h>
#include <gs/input/InputSource.h>
#include <gs/input/MappingEngine.h>
#include <gs/input/Modules.h>
#include <gs/input/Profile.h>
#include <gs/platform/Crypto.h>
#include <gs/script/Services.h>
#include <gs/session/StreamHost.h>

#include <gamescrcpy_ext.h>

#include <windows.h>
#include <userenv.h>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <QCoreApplication>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

using namespace gs;
using namespace gs::ext::test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

class ApplicationScope
{
public:
    ApplicationScope()
    {
        if (QCoreApplication::instance() == nullptr) {
            m_application = std::make_unique<QCoreApplication>(m_argc, m_argv.data());
        }
    }

    ApplicationScope(const ApplicationScope&) = delete;
    ApplicationScope& operator=(const ApplicationScope&) = delete;

private:
    int m_argc = 1;
    std::string m_name = "gs_app_tests";
    std::array<char*, 2> m_argv{m_name.data(), nullptr};
    std::unique_ptr<QCoreApplication> m_application;
};

const fs::path kSampleFolder(GS_PC_CLOCK_DIR);
const fs::path kTestDll(GS_TEST_NATIVE_DLL);
const fs::path kProgram(GS_EXTHOST_PROGRAM);

exthost::Value numberValue(double value)
{
    exthost::Value made;
    made.type = exthost::ValueType::Number;
    made.number = value;
    return made;
}

exthost::Value textValue(exthost::ValueType type, std::string text)
{
    exthost::Value made;
    made.type = type;
    made.text = std::move(text);
    return made;
}

std::optional<std::size_t> nodeIndex(const exthost::Module& module, std::string_view name)
{
    const std::vector<exthost::NodeInfo>& nodes = module.info().nodes;
    const auto found = std::ranges::find(nodes, name, &exthost::NodeInfo::name);
    return found == nodes.end() ? std::nullopt
                                : std::optional<std::size_t>(static_cast<std::size_t>(found - nodes.begin()));
}

exthost::CallResult callNode(exthost::Module& module, std::string_view name, std::vector<exthost::Value> values = {})
{
    const std::optional<std::size_t> index = nodeIndex(module, name);
    REQUIRE(index.has_value());
    return module.call(*index, values);
}

fs::path containedProgram(const Scratch& scratch)
{
    const fs::path copy = scratch / "program" / kProgram.filename();
    fs::create_directories(copy.parent_path());
    fs::copy_file(kProgram, copy, fs::copy_options::overwrite_existing);
    return copy;
}

exthost::HostedOptions hostedOptions(const Scratch& scratch)
{
    exthost::HostedOptions options;
    options.program = kProgram;
    options.dll = kTestDll;
    options.store = scratch / "store/gstest.native.json";
    options.granted = {"notify"};
    return options;
}

bool eventually(const std::function<bool()>& done)
{
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(10ms);
    }
    return true;
}

void crashOnce(exthost::HostedModule& module, int expected)
{
    const exthost::CallResult crashed = callNode(module, "crash");
    CHECK(crashed.status != exthost::CallStatus::Ok);
    REQUIRE(eventually([&] { return module.crashes() == expected; }));
}

ext::NativeContribution testContribution()
{
    ext::NativeContribution native;
    native.dll = "native.dll";
    native.touchMethods = {{"recorder", "Recorder", "Writes down every finger."}};
    native.nodes = {{"add", {{"a", "number"}, {"b", "number"}}, "number", "Adds two numbers.", {}},
                    {"allowed", {{"permission", "string"}}, "boolean", "", {"notify"}},
                    {"touches", {}, "json", "", {}},
                    {"breakTouch", {}, "", "", {}},
                    {"echo", {{"text", "number"}}, "string", "", {}}};
    return native;
}

app::NativeWanted testWanted(const fs::path& folder)
{
    app::NativeWanted wanted;
    wanted.id = "gstest.native";
    wanted.version = "1.0.0";
    wanted.name = QStringLiteral("Test native");
    wanted.directory = folder;
    wanted.official = true;
    wanted.native = testContribution();
    return wanted;
}

}

TEST_CASE("the official sample loads inside GameScrcpy and its node gives the computer's time as text (ADR-0097)")
{
    std::string problem;
    const std::unique_ptr<exthost::InProcessModule> module =
        exthost::InProcessModule::load({.dll = kSampleFolder / "pc-clock.dll"}, problem);
    REQUIRE_MESSAGE(module != nullptr, problem);
    REQUIRE(module->info().nodes.size() == 1);
    CHECK(module->info().nodes[0].name == "now");
    CHECK(module->info().nodes[0].params.empty());
    CHECK(module->info().nodes[0].returns == exthost::ValueType::String);
    CHECK(module->info().touchMethods.empty());

    const exthost::CallResult result = callNode(*module, "now");
    REQUIRE_MESSAGE(result.ok(), result.problem);
    CHECK(result.value.type == exthost::ValueType::String);
    CHECK(std::regex_match(result.value.text, std::regex(R"(\d\d:\d\d:\d\d)")));
    CHECK(callNode(*module, "now", {numberValue(1)}).status == exthost::CallStatus::BadArgument);

    CHECK(exthost::InProcessModule::load({.dll = kSampleFolder / "missing.dll"}, problem) == nullptr);
    CHECK(problem.find("could not load") != std::string::npos);
}

TEST_CASE("ADR-0112: a DLL built with smaller or larger structs is read field by field, and a kind GameScrcpy does "
          "not know is left out")
{
    struct Logged final : exthost::HostServices {
        void log(int level, std::string_view text) override
        {
            lines.emplace_back(std::to_string(level) + " " + std::string(text));
        }
        bool notify(std::string_view, std::string_view) override { return false; }
        std::vector<std::string> lines;
    } logged;
    std::string problem;
    const std::unique_ptr<exthost::InProcessModule> module =
        exthost::InProcessModule::load({.dll = fs::path(GS_TEST_NATIVE_SIZES_DLL), .services = &logged}, problem);
    REQUIRE_MESSAGE(module != nullptr, problem);
    REQUIRE(module->info().nodes.size() == 1);
    CHECK(callNode(*module, "later").value.number == 42.0);
    REQUIRE(module->info().touchMethods.size() == 1);
    const exthost::TouchMethodInfo& older = module->info().touchMethods[0];
    CHECK(older.name == "older");
    CHECK(older.maxContacts == 2);
    CHECK(older.keys);
    CHECK_FALSE(older.text);
    CHECK_FALSE(older.mouse);
    CHECK_FALSE(older.gamepad);
    REQUIRE(logged.lines.size() == 1);
    CHECK(logged.lines[0].find("telepathy") != std::string::npos);
}

namespace {

struct JobWatch {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<double> fractions;
    std::vector<std::string> lines;
    std::vector<std::string> logs;
    std::vector<std::string> outputs;
    std::optional<std::pair<bool, std::string>> done;

    exthost::JobEvents events()
    {
        return {.progress =
                    [this](double fraction, std::string text) {
                        const std::scoped_lock lock(mutex);
                        fractions.push_back(fraction);
                        lines.push_back(std::move(text));
                    },
                .log =
                    [this](int, std::string text) {
                        const std::scoped_lock lock(mutex);
                        logs.push_back(std::move(text));
                    },
                .done =
                    [this](bool worked, std::string text) {
                        {
                            const std::scoped_lock lock(mutex);
                            done.emplace(worked, std::move(text));
                        }
                        changed.notify_all();
                    },
                .output =
                    [this](std::string json) {
                        const std::scoped_lock lock(mutex);
                        outputs.push_back(std::move(json));
                    }};
    }

    std::pair<bool, std::string> wait()
    {
        std::unique_lock lock(mutex);
        REQUIRE(changed.wait_for(lock, 20s, [this] { return done.has_value(); }));
        return *done;
    }
};

std::size_t jobIndex(const exthost::Module& module, std::string_view name)
{
    const std::vector<exthost::JobInfo>& jobs = module.info().jobs;
    const auto found = std::ranges::find(jobs, name, &exthost::JobInfo::name);
    REQUIRE(found != jobs.end());
    return static_cast<std::size_t>(found - jobs.begin());
}

void checkJobs(exthost::Module& module)
{
    REQUIRE(module.info().jobs.size() == 5);
    const std::size_t count = jobIndex(module, "count");

    JobWatch counted;
    CHECK(module.startJob(count, R"({"steps":3,"pause":60})", counted.events()) != 0);
    const auto [worked, result] = counted.wait();
    CHECK(worked);
    CHECK(result == R"({"counted":3})");
    {
        const std::scoped_lock lock(counted.mutex);
        CHECK(counted.fractions.size() == 3);
        CHECK(counted.fractions.back() == doctest::Approx(1.0));
        CHECK(counted.lines.front() == "step 1");
        CHECK(counted.logs == std::vector<std::string>{"counting"});
        CHECK(counted.outputs.empty());
    }

    JobWatch handed;
    CHECK(module.startJob(count, R"({"steps":150,"output":1})", handed.events()) != 0);
    CHECK(handed.wait() == std::pair<bool, std::string>{true, R"({"counted":150,"busy":50})"});
    {
        const std::scoped_lock lock(handed.mutex);
        REQUIRE(handed.outputs.size() == 100);
        CHECK(handed.outputs.front() == R"({"step":1})");
        CHECK(handed.outputs.back() == R"({"step":100})");
    }

    JobWatch failing;
    CHECK(module.startJob(count, R"({"steps":1,"fail":1})", failing.events()) != 0);
    CHECK(failing.wait() == std::pair<bool, std::string>{false, "asked to fail"});

    JobWatch refused;
    CHECK(module.startJob(jobIndex(module, "refuse"), "{}", refused.events()) != 0);
    CHECK(refused.wait() == std::pair<bool, std::string>{false, "the job did not start"});

    JobWatch cancelled;
    const std::uint64_t running = module.startJob(count, R"({"steps":1000,"pause":20})", cancelled.events());
    REQUIRE(running != 0);
    module.cancelJob(running);
    CHECK(cancelled.wait() == std::pair<bool, std::string>{false, "cancelled"});

    std::array<JobWatch, 5> many;
    std::vector<std::uint64_t> started;
    for (JobWatch& watch : many) {
        started.push_back(module.startJob(count, R"({"steps":1000,"pause":20})", watch.events()));
    }
    CHECK(std::ranges::count(started, std::uint64_t{0}) == 1);
    CHECK(started.back() == 0);
    for (std::size_t index = 0; index + 1 < started.size(); ++index) {
        module.cancelJob(started[index]);
        CHECK(many[index].wait().second == "cancelled");
    }
}

}

TEST_CASE("ADR-0112: a job runs on the DLL's own thread, tells how far it got and ends with its result, also when "
          "it fails, refuses or is cancelled, and at most 4 run at once")
{
    Scratch scratch;
    std::string problem;
    const std::unique_ptr<exthost::InProcessModule> module =
        exthost::InProcessModule::load({.dll = kTestDll, .store = scratch / "store.json"}, problem);
    REQUIRE_MESSAGE(module != nullptr, problem);
    checkJobs(*module);
}

TEST_CASE("ADR-0112: jobs of someone else's DLL run through gs-exthost.exe the same way, and end when its process "
          "stops")
{
    Scratch scratch;
    std::string problem;
    const std::unique_ptr<exthost::HostedModule> module = exthost::HostedModule::start(hostedOptions(scratch), problem);
    REQUIRE_MESSAGE(module != nullptr, problem);
    REQUIRE(module->waitReady(20s));
    checkJobs(*module);

    JobWatch stranded;
    REQUIRE(module->startJob(jobIndex(*module, "count"), R"({"steps":1000,"pause":20})", stranded.events()) != 0);
    crashOnce(*module, 1);
    CHECK(stranded.wait() == std::pair<bool, std::string>{false, "the extension's process stopped before the job ended"});
}

TEST_CASE("ADR-0112: a text a DLL writes into the ring itself as a job's progress is left out unless it is UTF-8, so "
          "no strict JSON write of GameScrcpy throws")
{
    Scratch scratch;
    std::string problem;
    const std::unique_ptr<exthost::HostedModule> module =
        exthost::HostedModule::start(hostedOptions(scratch), problem);
    REQUIRE_MESSAGE(module != nullptr, problem);
    REQUIRE(module->waitReady(20s));
    JobWatch watched;
    const std::uint64_t running = module->startJob(jobIndex(*module, "count"), R"({"steps":200,"pause":20})",
                                                   watched.events());
    REQUIRE(running != 0);
    const exthost::CallResult forged =
        callNode(*module, "forge", {textValue(exthost::ValueType::String, "progress"),
                                    numberValue(static_cast<double>(running)), numberValue(1)});
    CHECK(forged.value.number == 1);
    REQUIRE(eventually([&] {
        const std::scoped_lock lock(watched.mutex);
        return watched.fractions.size() >= 2;
    }));
    const std::vector<std::string> lines = [&watched] {
        const std::scoped_lock lock(watched.mutex);
        return watched.lines;
    }();
    CHECK(std::ranges::all_of(lines, [](const std::string& line) { return exthost::validUtf8(line); }));
    module->cancelJob(running);
    CHECK(watched.wait() == std::pair<bool, std::string>{false, "cancelled"});
}

TEST_CASE("ADR-0112: a transport or a source a DLL never declared is left out when the child names it, and the ones "
          "it declared still reach GameScrcpy")
{
    struct Watcher {
        std::mutex mutex;
        std::vector<std::string> transports;
        std::vector<std::string> sources;

        std::vector<std::string> seen()
        {
            const std::scoped_lock lock(mutex);
            return transports;
        }

        std::vector<std::string> named()
        {
            const std::scoped_lock lock(mutex);
            return sources;
        }
    } watcher;
    Scratch scratch;
    std::string problem;
    exthost::HostedOptions options = hostedOptions(scratch);
    options.transportChanged = [&watcher](const std::string& name) {
        const std::scoped_lock lock(watcher.mutex);
        watcher.transports.push_back(name);
    };
    options.sourcesChanged = [&watcher](const std::string& name) {
        const std::scoped_lock lock(watcher.mutex);
        watcher.sources.push_back(name);
    };
    const std::unique_ptr<exthost::HostedModule> module = exthost::HostedModule::start(std::move(options), problem);
    REQUIRE_MESSAGE(module != nullptr, problem);
    REQUIRE(module->waitReady(20s));
    CHECK(callNode(*module, "forge", {textValue(exthost::ValueType::String, "transport"), numberValue(0.0),
                                      numberValue(1)})
              .value.number == 1);
    CHECK(callNode(*module, "forge", {textValue(exthost::ValueType::String, "source"), numberValue(0.0),
                                      numberValue(1)})
              .value.number == 1);
    CHECK(callNode(*module, "setPhones", {textValue(exthost::ValueType::Json, R"([{"id":"echo","name":"Echo"}])")})
              .value.number == GS_EXT_OK);
    CHECK(callNode(*module, "changeSources", {}).value.number == GS_EXT_OK);
    REQUIRE(eventually([&] { return !watcher.seen().empty() && !watcher.named().empty(); }));
    std::this_thread::sleep_for(200ms);
    CHECK(watcher.seen() == std::vector<std::string>{"loop"});
    CHECK(watcher.named() == std::vector<std::string>{"bars"});
}

TEST_CASE("ADR-0112: a job's end still reaches GameScrcpy when its output filled the channel while GameScrcpy was "
          "busy, instead of leaving the job running for ever")
{
    struct Blocking {
        std::mutex mutex;
        std::condition_variable changed;
        std::condition_variable gate;
        bool opened = false;
        bool released = false;
        std::size_t outputs = 0;
        std::optional<std::pair<bool, std::string>> done;

        exthost::JobEvents events()
        {
            return {.progress =
                        [this](double, std::string) {
                            std::unique_lock lock(mutex);
                            opened = true;
                            changed.notify_all();
                            gate.wait(lock, [this] { return released; });
                        },
                    .log = [](int, std::string) {},
                    .done =
                        [this](bool worked, std::string text) {
                            {
                                const std::scoped_lock lock(mutex);
                                done.emplace(worked, std::move(text));
                            }
                            changed.notify_all();
                        },
                    .output =
                        [this](std::string) {
                            const std::scoped_lock lock(mutex);
                            ++outputs;
                        }};
        }

        void open()
        {
            std::unique_lock lock(mutex);
            changed.wait(lock, [this] { return opened; });
        }

        void release()
        {
            const std::scoped_lock lock(mutex);
            released = true;
            gate.notify_all();
        }

        std::pair<bool, std::string> wait()
        {
            std::unique_lock lock(mutex);
            REQUIRE(changed.wait_for(lock, 20s, [this] { return done.has_value(); }));
            return *done;
        }
    };
    Scratch scratch;
    std::string problem;
    const std::unique_ptr<exthost::HostedModule> module =
        exthost::HostedModule::start(hostedOptions(scratch), problem);
    REQUIRE_MESSAGE(module != nullptr, problem);
    REQUIRE(module->waitReady(20s));
    Blocking blocking;
    REQUIRE(module->startJob(jobIndex(*module, "count"), R"({"steps":20,"output":1,"big":1})", blocking.events()) != 0);
    blocking.open();
    std::this_thread::sleep_for(400ms);
    blocking.release();
    CHECK(blocking.wait() == std::pair<bool, std::string>{true, R"({"counted":20,"busy":0})"});
}

TEST_CASE("ADR-0112: turning off an extension whose child keeps filling the channel ends within a few seconds")
{
    Scratch scratch;
    std::string problem;
    std::unique_ptr<exthost::HostedModule> module = exthost::HostedModule::start(hostedOptions(scratch), problem);
    REQUIRE_MESSAGE(module != nullptr, problem);
    REQUIRE(module->waitReady(20s));
    CHECK(callNode(*module, "forge", {textValue(exthost::ValueType::String, "overlay"), numberValue(0.0),
                                      numberValue(1000000.0)})
              .status != exthost::CallStatus::Ok);
    std::this_thread::sleep_for(300ms);
    std::atomic<bool> gone{false};
    std::thread([held = std::move(module), &gone]() mutable {
        held.reset();
        gone.store(true);
    }).detach();
    CHECK(eventually([&] { return gone.load(); }));
}

namespace {

struct TapWatch {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::vector<exthost::Shape>> overlays;
    std::vector<std::string> failures;

    exthost::TapEvents events()
    {
        return {.overlay =
                    [this](std::vector<exthost::Shape> shapes) {
                        {
                            const std::scoped_lock lock(mutex);
                            overlays.push_back(std::move(shapes));
                        }
                        changed.notify_all();
                    },
                .failed =
                    [this](std::string problem) {
                        {
                            const std::scoped_lock lock(mutex);
                            failures.push_back(std::move(problem));
                        }
                        changed.notify_all();
                    }};
    }

    bool waitFor(const std::function<bool()>& done)
    {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 20s, done);
    }
};

nlohmann::json tapStats(exthost::Module& module)
{
    const exthost::CallResult stats = callNode(module, "tapStats");
    REQUIRE_MESSAGE(stats.ok(), stats.problem);
    return nlohmann::json::parse(stats.value.text);
}

std::vector<std::uint8_t> tapPicture(std::uint32_t width, std::uint32_t height, std::uint32_t stride)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(stride) * height, 0);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint8_t* pixel = pixels.data() + static_cast<std::size_t>(y) * stride + std::size_t{4} * x;
            pixel[0] = 10;
            pixel[1] = 20;
            pixel[2] = 30;
            pixel[3] = 255;
        }
    }
    return pixels;
}

void checkTaps(exthost::Module& module)
{
    REQUIRE(module.info().frameTaps.size() == 1);
    CHECK(module.info().frameTaps[0].name == "counter");
    REQUIRE(callNode(module, "tapReset").ok());
    constexpr std::uint32_t kStride = 64 * 4 + 16;
    const std::vector<std::uint8_t> pixels = tapPicture(64, 36, kStride);
    const auto frameAt = [&pixels](std::uint64_t timeUs) {
        return exthost::TapFrame{.pixels = pixels.data(), .stride = kStride, .width = 64, .height = 36, .timeUs = timeUs};
    };
    const auto framesSeen = [&module](int count) {
        return eventually([&module, count] { return tapStats(module)["frames"].get<int>() >= count; });
    };

    TapWatch watch;
    const std::uint64_t tap = module.openTap(0, {.device = "phone-1", .width = 64, .height = 36, .rotation = 1}, watch.events());
    REQUIRE(tap != 0);
    CHECK(module.offerFrame(tap, frameAt(1000)));
    REQUIRE(watch.waitFor([&watch] { return !watch.overlays.empty(); }));
    {
        const std::scoped_lock lock(watch.mutex);
        REQUIRE(watch.overlays[0].size() == 1);
        const exthost::Shape& box = watch.overlays[0][0];
        CHECK(box.kind == exthost::ShapeKind::Rect);
        CHECK(box.color == GS_EXT_COLOR_GOOD);
        CHECK(box.id == 1);
        CHECK(box.x == doctest::Approx(0.1));
        CHECK(box.y == doctest::Approx(0.2));
        CHECK(box.x2 == doctest::Approx(0.3));
        CHECK(box.y2 == doctest::Approx(0.4));
        CHECK(box.text == "box");
    }
    nlohmann::json stats = tapStats(module);
    CHECK(stats["opened"] == 1);
    CHECK(stats["device"] == "phone-1");
    CHECK(stats["width"] == 64);
    CHECK(stats["height"] == 36);
    CHECK(stats["rotation"] == 1);
    CHECK(stats["frameWidth"] == 64);
    CHECK(stats["frameHeight"] == 36);
    CHECK(stats["stride"] == 64 * 4);
    CHECK(stats["first"] == nlohmann::json::array({10, 20, 30, 255}));
    CHECK(stats["times"] == nlohmann::json::array({1000}));

    CHECK(module.offerFrame(tap, frameAt(2000)));
    REQUIRE(framesSeen(2));
    {
        const std::scoped_lock lock(watch.mutex);
        CHECK(watch.overlays.size() == 1);
    }

    REQUIRE(callNode(module, "tapPause", {numberValue(400)}).ok());
    CHECK(module.offerFrame(tap, frameAt(3000)));
    REQUIRE(framesSeen(3));
    for (const std::uint64_t timeUs : {std::uint64_t{4000}, std::uint64_t{5000}, std::uint64_t{6000}}) {
        CHECK(module.offerFrame(tap, frameAt(timeUs)));
    }
    REQUIRE(framesSeen(4));
    REQUIRE(callNode(module, "tapPause", {numberValue(0)}).ok());
    CHECK(tapStats(module)["times"] == nlohmann::json::array({1000, 2000, 3000, 6000}));

    module.closeTap(tap);
    REQUIRE(eventually([&module] { return tapStats(module)["closed"].get<int>() == 1; }));
    CHECK_FALSE(module.offerFrame(tap, frameAt(7000)));
    CHECK(tapStats(module)["frames"] == 4);

    CHECK_FALSE(module.offerFrame(tap + 1000, frameAt(8000)));
    CHECK(module.openTap(0, {.device = "phone-1", .width = 0, .height = 36, .rotation = 0}, {}) == 0);
    CHECK(module.openTap(1, {.device = "phone-1", .width = 64, .height = 36, .rotation = 0}, {}) == 0);

    REQUIRE(callNode(module, "tapRefuse", {numberValue(1)}).ok());
    TapWatch refused;
    const std::uint64_t again =
        module.openTap(0, {.device = "phone-2", .width = 64, .height = 36, .rotation = 0}, refused.events());
    if (again != 0) {
        REQUIRE(refused.waitFor([&refused] { return !refused.failures.empty(); }));
        CHECK_FALSE(module.offerFrame(again, frameAt(9000)));
        module.closeTap(again);
    }
    REQUIRE(callNode(module, "tapRefuse", {numberValue(0)}).ok());
    CHECK(tapStats(module)["opened"] == 1);
}

}

TEST_CASE("ADR-0112: a frame tap in GameScrcpy gets frames of the right size and stride, gives its overlay back once "
          "it changes, gets only the latest frame while it is busy and stops when it is closed")
{
    Scratch scratch;
    std::string problem;
    const std::unique_ptr<exthost::InProcessModule> module =
        exthost::InProcessModule::load({.dll = kTestDll, .store = scratch / "store.json"}, problem);
    REQUIRE_MESSAGE(module != nullptr, problem);
    checkTaps(*module);
    CHECK(callNode(*module, "supports", {textValue(exthost::ValueType::String, "frame-taps")}).value.flag);
}

TEST_CASE("ADR-0112: a frame tap in gs-exthost.exe gets its frames through a shared section of its own the same way, "
          "a refused tap says so, and a crash takes its overlay away")
{
    Scratch scratch;
    std::string problem;
    const std::unique_ptr<exthost::HostedModule> module = exthost::HostedModule::start(hostedOptions(scratch), problem);
    REQUIRE_MESSAGE(module != nullptr, problem);
    REQUIRE(module->waitReady(20s));
    checkTaps(*module);

    TapWatch watch;
    const std::vector<std::uint8_t> pixels = tapPicture(64, 36, 64 * 4);
    const exthost::TapFrame frame{.pixels = pixels.data(), .stride = 64 * 4, .width = 64, .height = 36, .timeUs = 1};
    const std::uint64_t tap =
        module->openTap(0, {.device = "phone-3", .width = 64, .height = 36, .rotation = 0}, watch.events());
    REQUIRE(tap != 0);
    CHECK(module->offerFrame(tap, frame));
    REQUIRE(watch.waitFor([&watch] { return !watch.overlays.empty(); }));
    crashOnce(*module, 1);
    REQUIRE(watch.waitFor([&watch] { return watch.overlays.size() >= 2; }));
    {
        const std::scoped_lock lock(watch.mutex);
        CHECK(watch.overlays[1].empty());
    }
    REQUIRE(module->waitReady(20s));
    REQUIRE(callNode(*module, "tapReset").ok());
    CHECK(module->offerFrame(tap, frame));
    REQUIRE(eventually([&module] { return tapStats(*module)["frames"].get<int>() >= 1; }));
    CHECK(tapStats(*module)["opened"] == 1);
    module->closeTap(tap);
}

TEST_CASE("ADR-0112: a script starts a job by name with checked arguments, hears its progress and gets its result; "
          "a cancelled or failing job says so, and a job ends with its script")
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path folder = scratch / "gstest.native/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    app::NativeWanted wanted = testWanted(folder);
    wanted.native.nodes.clear();
    wanted.native.jobs = {{.name = "count",
                           .title = "Count",
                           .params = {{"steps", "number"}, {"pause", "number"}, {"fail", "number"}},
                           .returns = "json",
                           .description = "Counts.",
                           .permissions = {},
                           .most = 2}};
    registry.apply({wanted});
    REQUIRE(registry.problems().empty());
    const std::vector<script::NodeInfo> listed = registry.nodes();
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].job);
    CHECK(listed[0].name == "count");
    CHECK(script::nodeSignature(listed[0]) ==
          "count(args: { steps: number; pause: number; fail: number }): ExtensionJob<StoreValue>");

    struct Heard {
        std::vector<std::pair<quint64, QString>> answers;
        std::vector<std::pair<quint64, QString>> refusals;
        std::vector<QString> told;
    } heard;
    app::NativeJobs jobs(registry);
    jobs.setReplies({.answer = [&heard](std::uint64_t, quint64 request,
                                        const QString& json) { heard.answers.emplace_back(request, json); },
                     .refuse = [&heard](std::uint64_t, quint64 request,
                                        const QString& message) { heard.refusals.emplace_back(request, message); },
                     .tell = [&heard](std::uint64_t, const QString& kind, const QString&,
                                      const QString& json) { heard.told.push_back(kind + u' ' + json); }});
    const auto settled = [](const std::function<bool()>& done) {
        const auto deadline = std::chrono::steady_clock::now() + 20s;
        while (!done()) {
            if (std::chrono::steady_clock::now() > deadline) {
                return false;
            }
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(5ms);
        }
        return true;
    };
    const auto idOf = [](const script::ServiceAnswer& answer) {
        return QString::fromStdString(nlohmann::json::parse(answer.json)["id"].get<std::string>());
    };
    const app::LogicCaller caller{.logic = 7};
    const auto startWith = [&](std::string_view args) {
        return jobs.start(caller, R"({"extension":"gstest.native","job":"count","args":)" + std::string(args) + "}");
    };

    const script::ServiceAnswer counted = startWith(R"({"steps":3,"pause":60,"fail":0})");
    REQUIRE_MESSAGE(counted.code.empty(), counted.problem);
    jobs.result(caller, 11, QStringLiteral(R"({"id":"%1"})").arg(idOf(counted)));
    REQUIRE(settled([&] { return !heard.answers.empty(); }));
    CHECK(heard.answers[0] == std::pair<quint64, QString>{11, QStringLiteral(R"({"counted":3})")});
    CHECK(heard.told.size() == 4);
    CHECK(heard.told.front().startsWith(u"job {\"fraction\":0.333"));
    CHECK(jobs.running() == 0);

    CHECK(startWith(R"({"steps":"three","pause":0,"fail":0})").code == "GS-API-061");
    CHECK(startWith(R"({"steps":3,"fail":0})").code == "GS-API-061");
    CHECK(startWith(R"({"steps":3,"pause":0,"fail":0,"extra":1})").code == "GS-API-061");
    CHECK(jobs.start(caller, R"({"extension":"gstest.native","job":"refuse","args":{}})").code == "GS-API-060");
    CHECK(jobs.start(caller, R"({"extension":"someone.else","job":"count","args":{}})").code == "GS-API-060");

    const script::ServiceAnswer long1 = startWith(R"({"steps":1000,"pause":20,"fail":0})");
    const script::ServiceAnswer long2 = startWith(R"({"steps":1000,"pause":20,"fail":0})");
    REQUIRE(long1.code.empty());
    REQUIRE(long2.code.empty());
    CHECK(startWith(R"({"steps":1000,"pause":20,"fail":0})").code == "GS-API-067");
    jobs.result(caller, 12, QStringLiteral(R"({"id":"%1"})").arg(idOf(long1)));
    CHECK(jobs.cancel(caller, R"({"id":")" + idOf(long1).toStdString() + "\"}").code.empty());
    REQUIRE(settled([&] { return !heard.refusals.empty(); }));
    CHECK(heard.refusals[0] == std::pair<quint64, QString>{12, QStringLiteral("GS-API-066 cancelled")});

    jobs.endLogic(caller.logic);
    CHECK(jobs.running() == 0);

    const script::ServiceAnswer failing = startWith(R"({"steps":1,"pause":0,"fail":1})");
    REQUIRE(failing.code.empty());
    jobs.result(caller, 13, QStringLiteral(R"({"id":"%1"})").arg(idOf(failing)));
    REQUIRE(settled([&] { return heard.refusals.size() == 2; }));
    CHECK(heard.refusals[1] == std::pair<quint64, QString>{13, QStringLiteral("GS-API-065 asked to fail")});
    jobs.result(caller, 14, QStringLiteral(R"({"id":"999"})"));
    CHECK(heard.refusals.back().second.startsWith(u"GS-API-060"));

    const auto declareReturns = [&](const char* returns) {
        wanted.native.jobs[0].returns = returns;
        registry.apply({wanted});
        REQUIRE(registry.problems().empty());
    };
    const auto askResult = [&](quint64 request, const script::ServiceAnswer& started) {
        REQUIRE_MESSAGE(started.code.empty(), started.problem);
        jobs.result(caller, request, QStringLiteral(R"({"id":"%1"})").arg(idOf(started)));
    };

    declareReturns("number");
    askResult(15, startWith(R"({"steps":1,"pause":0,"fail":0})"));
    REQUIRE(settled([&] { return heard.refusals.size() == 4; }));
    CHECK(heard.refusals[3] ==
          std::pair<quint64, QString>{15, QStringLiteral("GS-API-065 the job gave a result that is not a number")});

    declareReturns("");
    askResult(16, startWith(R"({"steps":1,"pause":0,"fail":0})"));
    REQUIRE(settled([&] { return heard.answers.size() == 2; }));
    CHECK(heard.answers[1] == std::pair<quint64, QString>{16, QStringLiteral("null")});

    askResult(17, startWith(R"({"steps":1000,"pause":20,"fail":0})"));
    registry.apply({});
    REQUIRE(settled([&] { return heard.refusals.size() == 5; }));
    CHECK(heard.refusals[4] ==
          std::pair<quint64, QString>{17, QStringLiteral("GS-API-065 the extension stopped before the job ended")});
    CHECK(jobs.running() == 0);
}

TEST_CASE("ADR-0112: an extension's speech engine reads and listens through its jobs, and a job hands over parts of "
          "its result to the script that started it")
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path folder = scratch / "gstest.native/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    app::NativeWanted wanted = testWanted(folder);
    wanted.native.nodes = {{.name = "voices", .params = {}, .returns = "json", .description = {}, .permissions = {}},
                           {.name = "lastSpoken", .params = {}, .returns = "json", .description = {}, .permissions = {}}};
    wanted.native.jobs = {
        {.name = "speak",
         .title = "Speak",
         .params = {{"text", "string"}, {"voice", "string"}, {"rate", "number"}, {"volume", "number"}},
         .returns = {},
         .description = {},
         .permissions = {},
         .most = std::nullopt},
        {.name = "hear",
         .title = "Hear",
         .params = {{"phrases", "json"}, {"language", "string"}, {"confidence", "number"}},
         .returns = {},
         .description = {},
         .permissions = {},
         .most = std::nullopt},
        {.name = "count",
         .title = "Count",
         .params = {{"steps", "number"}, {"pause", "number"}, {"fail", "number"}, {"output", "number"}},
         .returns = "json",
         .description = {},
         .permissions = {},
         .most = std::nullopt}};
    registry.apply({wanted});
    REQUIRE(registry.problems().empty());
    std::vector<QString> told;
    app::NativeJobs jobs(registry);
    jobs.setReplies({.answer = {},
                     .refuse = {},
                     .tell = [&told](std::uint64_t, const QString& kind, const QString&,
                                     const QString& json) { told.push_back(kind + u' ' + json); }});
    const auto settled = [](const std::function<bool()>& done) {
        const auto deadline = std::chrono::steady_clock::now() + 20s;
        while (!done()) {
            if (std::chrono::steady_clock::now() > deadline) {
                return false;
            }
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(5ms);
        }
        return true;
    };

    app::ExtensionSpeechEngine engine({.extension = QStringLiteral("gstest.native"),
                                       .name = QStringLiteral("robot"),
                                       .title = QStringLiteral("Robot"),
                                       .say = QStringLiteral("speak"),
                                       .voices = QStringLiteral("voices"),
                                       .listen = QStringLiteral("hear"),
                                       .dictation = true,
                                       .languages = {QStringLiteral("en-US")}},
                                      jobs);
    const app::SpeechEngineInfo info = engine.info();
    CHECK(info.id == u"gstest.native/robot");
    CHECK(info.say);
    CHECK(info.listen);
    CHECK(info.dictation);

    std::optional<std::vector<app::SpeechVoiceInfo>> voices;
    engine.voices([&voices](std::vector<app::SpeechVoiceInfo> found, QString problem) {
        CHECK(problem.isEmpty());
        voices = std::move(found);
    });
    REQUIRE(voices.has_value());
    REQUIRE(voices->size() == 2);
    CHECK((*voices)[0] == app::SpeechVoiceInfo{.id = QStringLiteral("robot"),
                                               .name = QStringLiteral("Test robot"),
                                               .language = QStringLiteral("en-US")});

    std::optional<std::pair<bool, QString>> spoken;
    const auto note = [&spoken](bool finished, QString problem) { spoken.emplace(finished, std::move(problem)); };
    CHECK(engine.say({.text = QStringLiteral("hello"), .voice = QStringLiteral("robot"), .rate = 1.25, .volume = 0.5},
                     note) != 0);
    REQUIRE(settled([&spoken] { return spoken.has_value(); }));
    CHECK(*spoken == std::pair<bool, QString>{true, QString()});
    CHECK(registry.call("gstest.native", "lastSpoken", {}).value.text ==
          R"({"rate":1.25,"text":"hello","voice":"robot","volume":0.5})");
    spoken.reset();
    const std::uint64_t waiting = engine.say({.text = QStringLiteral("wait")}, note);
    REQUIRE(waiting != 0);
    engine.stop(waiting);
    REQUIRE(settled([&spoken] { return spoken.has_value(); }));
    CHECK(*spoken == std::pair<bool, QString>{false, QStringLiteral("stopped")});

    std::vector<app::SpeechHeardInfo> heard;
    std::optional<QString> ended;
    const auto hear = [&heard](app::SpeechHeardInfo got) { heard.push_back(std::move(got)); };
    const auto end = [&ended](QString problem) { ended = std::move(problem); };
    const std::uint64_t listening = engine.listen(
        {.phrases = QStringList{QStringLiteral("open map"), QStringLiteral("quiet please")},
         .language = QStringLiteral("en-US"),
         .confidence = 0.6},
        hear, end);
    REQUIRE(listening != 0);
    REQUIRE(settled([&heard] { return heard.size() == 2; }));
    CHECK(heard[0].phrase == QStringLiteral("open map"));
    CHECK(heard[0].text == u"open map");
    CHECK(heard[0].confidence == doctest::Approx(0.9));
    CHECK(heard[1].confidence == doctest::Approx(0.3));
    engine.unlisten(listening);
    REQUIRE(settled([&ended] { return ended.has_value(); }));
    CHECK(*ended == u"stopped");

    heard.clear();
    ended.reset();
    const std::uint64_t dictating =
        engine.listen({.phrases = std::nullopt, .language = QStringLiteral("en-US"), .confidence = 0.6}, hear, end);
    REQUIRE(settled([&heard] { return !heard.empty(); }));
    CHECK_FALSE(heard[0].phrase.has_value());
    CHECK(heard[0].text == u"free words");
    engine.unlisten(dictating);
    REQUIRE(settled([&ended] { return ended.has_value(); }));

    const app::LogicCaller caller{.logic = 9};
    const script::ServiceAnswer counted = jobs.start(
        caller, R"({"extension":"gstest.native","job":"count","args":{"steps":3,"pause":0,"fail":0,"output":1}})");
    REQUIRE_MESSAGE(counted.code.empty(), counted.problem);
    const auto outputs = [&told] {
        return std::ranges::count_if(told, [](const QString& line) { return line.contains(u"\"kind\":\"output\""); });
    };
    REQUIRE(settled([&outputs] { return outputs() == 3; }));
    CHECK(std::ranges::find(told, QStringLiteral(R"(job {"kind":"output","value":{"step":2}})")) != told.end());
}

TEST_CASE("ADR-0112: a part of a job's result nested 30000 deep is left out for the script and the host while the "
          "job's other parts still arrive, without GameScrcpy running out of stack")
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path folder = scratch / "gstest.native/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    app::NativeWanted wanted = testWanted(folder);
    wanted.native.nodes.clear();
    wanted.native.jobs = {{.name = "count",
                           .title = "Count",
                           .params = {{"steps", "number"}, {"pause", "number"}, {"fail", "number"}, {"output", "number"}},
                           .returns = "json",
                           .description = {},
                           .permissions = {},
                           .most = std::nullopt}};
    registry.apply({wanted});
    REQUIRE(registry.problems().empty());
    std::vector<QString> told;
    app::NativeJobs jobs(registry);
    jobs.setReplies({.answer = {},
                     .refuse = {},
                     .tell = [&told](std::uint64_t, const QString& kind, const QString&,
                                     const QString& json) { told.push_back(kind + u' ' + json); }});
    const auto settled = [](const std::function<bool()>& done) {
        const auto deadline = std::chrono::steady_clock::now() + 20s;
        while (!done()) {
            if (std::chrono::steady_clock::now() > deadline) {
                return false;
            }
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(5ms);
        }
        return true;
    };
    const std::string asked =
        R"({"extension":"gstest.native","job":"count","args":{"steps":1,"pause":0,"fail":0,"output":2}})";

    const script::ServiceAnswer counted = jobs.start(app::LogicCaller{.logic = 9}, asked);
    REQUIRE_MESSAGE(counted.code.empty(), counted.problem);
    const QString line = QStringLiteral(R"(job {"kind":"output","value":{"step":1}})");
    REQUIRE(settled([&told, &line] { return std::ranges::find(told, line) != told.end(); }));
    CHECK(std::ranges::none_of(told, [](const QString& heard) {
        return heard.startsWith(QStringLiteral(R"(job {"kind":"output","value":[)"));
    }));

    std::optional<QString> handed;
    std::optional<bool> finished;
    const script::ServiceAnswer started = jobs.startForHost(
        asked, app::HostJobEvents{.progress = {},
                                  .done = [&finished](bool worked, const QString&) { finished = worked; },
                                  .output =
                                      [&handed](const QString& json) {
                                          if (!handed) {
                                              handed = json;
                                          }
                                      }});
    REQUIRE_MESSAGE(started.code.empty(), started.problem);
    REQUIRE(settled([&finished] { return finished.has_value(); }));
    CHECK(*finished);
    REQUIRE(handed.has_value());
    CHECK(*handed == QStringLiteral(R"({"step":1})"));
}

TEST_CASE("ADR-0112: the devices page starts a job with the same checks and limit as a script and hears its progress "
          "and end itself; no script can cancel it, ask for its result or end it")
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path folder = scratch / "gstest.native/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    app::NativeWanted wanted = testWanted(folder);
    wanted.native.nodes.clear();
    wanted.native.jobs = {{.name = "count",
                           .title = "Count",
                           .params = {{"steps", "number"}, {"pause", "number"}, {"fail", "number"}},
                           .returns = "json",
                           .description = "Counts.",
                           .permissions = {},
                           .most = 3}};
    registry.apply({wanted});
    REQUIRE(registry.problems().empty());

    std::vector<QString> told;
    std::vector<QString> refused;
    app::NativeJobs jobs(registry);
    jobs.setReplies({.answer = {},
                     .refuse = [&refused](std::uint64_t, quint64,
                                          const QString& message) { refused.push_back(message); },
                     .tell = [&told](std::uint64_t, const QString& kind, const QString&,
                                     const QString& json) { told.push_back(kind + u' ' + json); }});
    struct Heard {
        std::vector<std::pair<double, QString>> progress;
        std::optional<std::pair<bool, QString>> done;
    };
    const auto settled = [](const std::function<bool()>& done) {
        const auto deadline = std::chrono::steady_clock::now() + 20s;
        while (!done()) {
            if (std::chrono::steady_clock::now() > deadline) {
                return false;
            }
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(5ms);
        }
        return true;
    };
    const auto request = [](std::string_view args) {
        return R"({"extension":"gstest.native","job":"count","args":)" + std::string(args) + "}";
    };
    const auto hear = [](Heard& heard) {
        return app::HostJobEvents{
            .progress = [&heard](double fraction, const QString& text) { heard.progress.emplace_back(fraction, text); },
            .done = [&heard](bool worked, const QString& text) { heard.done.emplace(worked, text); }};
    };

    Heard counted;
    const script::ServiceAnswer started = jobs.startForHost(request(R"({"steps":2,"pause":60,"fail":0})"), hear(counted));
    REQUIRE_MESSAGE(started.code.empty(), started.problem);
    REQUIRE(settled([&] { return counted.done.has_value(); }));
    CHECK(counted.progress ==
          std::vector<std::pair<double, QString>>{{0.5, QStringLiteral("step 1")}, {1.0, QStringLiteral("step 2")}});
    CHECK(*counted.done == std::pair<bool, QString>{true, QStringLiteral(R"({"counted":2})")});
    CHECK(told.empty());
    CHECK(jobs.running() == 0);

    Heard failing;
    REQUIRE(jobs.startForHost(request(R"({"steps":1,"pause":0,"fail":1})"), hear(failing)).code.empty());
    REQUIRE(settled([&] { return failing.done.has_value(); }));
    CHECK(*failing.done == std::pair<bool, QString>{false, QStringLiteral("asked to fail")});

    Heard unchecked;
    CHECK(jobs.startForHost(request(R"({"steps":1})"), hear(unchecked)).code == "GS-API-061");
    CHECK(jobs.startForHost(R"({"extension":"someone.else","job":"count","args":{}})", hear(unchecked)).code ==
          "GS-API-060");
    CHECK_FALSE(unchecked.done.has_value());

    std::vector<Heard> running(3);
    std::vector<std::string> ids;
    for (Heard& each : running) {
        const script::ServiceAnswer longJob =
            jobs.startForHost(request(R"({"steps":1000,"pause":20,"fail":0})"), hear(each));
        REQUIRE_MESSAGE(longJob.code.empty(), longJob.problem);
        ids.push_back(nlohmann::json::parse(longJob.json)["id"].get<std::string>());
    }
    const app::LogicCaller script{.logic = 7};
    CHECK(jobs.start(script, request(R"({"steps":1,"pause":0,"fail":0})")).code == "GS-API-067");
    Heard fourth;
    CHECK(jobs.startForHost(request(R"({"steps":1,"pause":0,"fail":0})"), hear(fourth)).code == "GS-API-067");
    jobs.endLogic(script.logic);

    const auto growing = [&](const Heard& heard) {
        const std::size_t from = heard.progress.size();
        return settled([&heard, from] { return heard.progress.size() > from + 2; });
    };
    const app::LogicCaller nobody{.logic = 0};
    CHECK(jobs.cancel(nobody, R"({"id":")" + ids[0] + "\"}").code.empty());
    CHECK(growing(running[0]));
    CHECK_FALSE(running[0].done.has_value());

    jobs.result(nobody, 21, QString::fromStdString(R"({"id":")" + ids[1] + "\"}"));
    CHECK(refused == std::vector<QString>{QStringLiteral("GS-API-060 there is no such job")});

    const std::size_t before = jobs.running();
    jobs.endLogic(0);
    CHECK(jobs.running() == before);
    CHECK(growing(running[2]));
    CHECK(told.empty());

    registry.apply({});
    REQUIRE(settled([&] {
        return std::ranges::all_of(running, [](const Heard& heard) { return heard.done.has_value(); });
    }));
    for (const Heard& heard : running) {
        CHECK(*heard.done ==
              std::pair<bool, QString>{false, QStringLiteral("the extension stopped before the job ended")});
    }
    CHECK(jobs.running() == 0);
}

TEST_CASE("ADR-0112: a DLL sends events with checked topics and JSON; of a topic the first 100 in a second go out at "
          "once and the rest of that second arrive as its last, in GameScrcpy and in gs-exthost.exe")
{
    struct Heard final : exthost::HostServices {
        std::mutex mutex;
        std::vector<std::pair<std::string, std::string>> events;

        void log(int, std::string_view) override {}
        bool notify(std::string_view, std::string_view) override { return false; }
        void emitted(std::string_view topic, std::string_view json) override
        {
            const std::scoped_lock lock(mutex);
            events.emplace_back(topic, json);
        }
        void add(const std::string& topic, const std::string& json) { emitted(topic, json); }
        std::vector<std::pair<std::string, std::string>> seen()
        {
            const std::scoped_lock lock(mutex);
            return events;
        }
    };
    const auto text = [](std::string value) { return textValue(exthost::ValueType::String, std::move(value)); };
    const auto counted = [](const std::vector<std::pair<std::string, std::string>>& events, std::size_t from) {
        std::vector<std::string> values;
        for (std::size_t index = from; index < events.size(); ++index) {
            values.push_back(events[index].second);
        }
        return values;
    };
    std::vector<std::string> first100;
    for (int count = 1; count <= 100; ++count) {
        first100.push_back(std::to_string(count));
    }

    Scratch scratch;
    Heard inside;
    std::string problem;
    const std::unique_ptr<exthost::InProcessModule> module = exthost::InProcessModule::load(
        {.dll = kTestDll, .store = scratch / "inside.json", .granted = {}, .services = &inside}, problem);
    REQUIRE_MESSAGE(module != nullptr, problem);
    CHECK(callNode(*module, "say", {text("board.added"), text(R"({"id":"b1"})")}).value.number == GS_EXT_OK);
    CHECK(callNode(*module, "say", {text("board.empty"), text("")}).value.number == GS_EXT_OK);
    CHECK(callNode(*module, "say", {text("Board"), text("1")}).value.number == GS_EXT_BAD_ARGUMENT);
    CHECK(callNode(*module, "say", {text(std::string(65, 'a')), text("1")}).value.number == GS_EXT_BAD_ARGUMENT);
    CHECK(callNode(*module, "say", {text(""), text("1")}).value.number == GS_EXT_BAD_ARGUMENT);
    CHECK(callNode(*module, "say", {text("board.added"), text("{oops")}).value.number == GS_EXT_BAD_ARGUMENT);
    CHECK(callNode(*module, "sayMany", {text("count"), numberValue(150)}).value.number == GS_EXT_OK);
    std::vector<std::pair<std::string, std::string>> seen = inside.seen();
    REQUIRE(seen.size() == 102);
    CHECK(seen[0] == std::pair<std::string, std::string>{"board.added", R"({"id":"b1"})"});
    CHECK(seen[1] == std::pair<std::string, std::string>{"board.empty", "null"});
    CHECK(counted(seen, 2) == first100);
    REQUIRE(eventually([&] { return inside.seen().size() == 103; }));
    CHECK(inside.seen().back() == std::pair<std::string, std::string>{"count", "150"});

    Heard outside;
    exthost::HostedOptions options = hostedOptions(scratch);
    options.event = [&outside](const std::string& topic, const std::string& json) { outside.add(topic, json); };
    const std::unique_ptr<exthost::HostedModule> hosted = exthost::HostedModule::start(std::move(options), problem);
    REQUIRE_MESSAGE(hosted != nullptr, problem);
    CHECK(callNode(*hosted, "say", {text("Board"), text("1")}).value.number == GS_EXT_BAD_ARGUMENT);
    CHECK(callNode(*hosted, "sayMany", {text("count"), numberValue(120)}).value.number == GS_EXT_OK);
    REQUIRE(eventually([&] { return outside.seen().size() == 100; }));
    CHECK(counted(outside.seen(), 0) == first100);
    REQUIRE(eventually([&] { return outside.seen().size() == 101; }));
    CHECK(outside.seen().back() == std::pair<std::string, std::string>{"count", "120"});
}

TEST_CASE("ADR-0112: a DLL reads its settings for all phones or for one phone and hears which keys changed, in "
          "GameScrcpy and in gs-exthost.exe")
{
    const auto text = [](std::string value) { return textValue(exthost::ValueType::String, std::move(value)); };
    const std::string first =
        R"({"global":{"port":"COM3","speed":2,"fast":true},"devices":{"phone-1":{"port":"COM9"}}})";
    const std::string second = R"({"global":{"port":"COM4","speed":2,"fast":true},"devices":{}})";
    Scratch scratch;
    std::string problem;
    const std::unique_ptr<exthost::InProcessModule> inside = exthost::InProcessModule::load(
        {.dll = kTestDll, .store = scratch / "inside.json", .granted = {}, .services = nullptr, .settings = first},
        problem);
    REQUIRE_MESSAGE(inside != nullptr, problem);
    exthost::HostedOptions options = hostedOptions(scratch);
    options.settings = first;
    const std::unique_ptr<exthost::HostedModule> hosted = exthost::HostedModule::start(std::move(options), problem);
    REQUIRE_MESSAGE(hosted != nullptr, problem);
    for (exthost::Module* module :
         {static_cast<exthost::Module*>(inside.get()), static_cast<exthost::Module*>(hosted.get())}) {
        CHECK(callNode(*module, "setting", {text("port"), text("")}).value.text == "\"COM3\"");
        CHECK(callNode(*module, "setting", {text("port"), text("phone-1")}).value.text == "\"COM9\"");
        CHECK(callNode(*module, "setting", {text("speed"), text("phone-1")}).value.text == "2");
        CHECK(callNode(*module, "setting", {text("fast"), text("phone-2")}).value.text == "true");
        CHECK(callNode(*module, "setting", {text("nope"), text("")}).value.text ==
              "error " + std::to_string(GS_EXT_BAD_ARGUMENT));
        CHECK(callNode(*module, "changedKeys").value.text == "[]");
        module->setSettings(second);
        std::string keys;
        REQUIRE(eventually([&] {
            keys = callNode(*module, "changedKeys").value.text;
            return keys != "[]";
        }));
        CHECK(keys == R"(["port"])");
        CHECK(callNode(*module, "setting", {text("port"), text("phone-1")}).value.text == "\"COM4\"");
        module->setSettings(second);
        CHECK(callNode(*module, "changedKeys").value.text == "[]");
    }
}

TEST_CASE("ADR-0112: a touch method's options hold the extension's settings, this phone's own values, the phone and "
          "the device the user chose")
{
    const std::string snapshot = R"({"global":{"port":"COM3","speed":2},"devices":{"phone-1":{"port":"COM9"}}})";
    const nlohmann::json all = {{"port", "COM3"}, {"speed", 2}};
    CHECK(nlohmann::json::parse(app::nativeTouchOptions(snapshot, "phone-1",
                                                        R"({"kind":"serial","path":"COM9","name":"Board"})")) ==
          nlohmann::json{{"settings", all},
                         {"deviceSettings", {{"port", "COM9"}}},
                         {"phone", "phone-1"},
                         {"device", {{"kind", "serial"}, {"path", "COM9"}, {"name", "Board"}}},
                         {"touchBox", nullptr}});
    CHECK(nlohmann::json::parse(app::nativeTouchOptions(snapshot, "phone-2")) ==
          nlohmann::json{{"settings", all},
                         {"deviceSettings", nlohmann::json::object()},
                         {"phone", "phone-2"},
                         {"device", nullptr},
                         {"touchBox", nullptr}});
    CHECK(nlohmann::json::parse(app::nativeTouchOptions("", "phone-2", "{oops")) ==
          nlohmann::json{{"settings", nlohmann::json::object()},
                         {"deviceSettings", nlohmann::json::object()},
                         {"phone", "phone-2"},
                         {"device", nullptr},
                         {"touchBox", nullptr}});
    CHECK(nlohmann::json::parse(app::nativeTouchOptions(snapshot, "box:a/boxes/box-1", "", "box-1"))["touchBox"] ==
          "box-1");
}

TEST_CASE("ADR-0112: the registry hands a DLL its settings when it loads and again when they change, without "
          "loading it again")
{
    Scratch scratch;
    const fs::path folder = scratch / "gstest.native/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    app::NativeWanted wanted = testWanted(folder);
    wanted.native.nodes = {{.name = "setting",
                            .params = {{"key", "string"}, {"phone", "string"}},
                            .returns = "string",
                            .description = {},
                            .permissions = {}},
                           {.name = "changedKeys", .params = {}, .returns = "json", .description = {}, .permissions = {}}};
    wanted.settings = R"({"global":{"port":"COM3"},"devices":{}})";
    registry.apply({wanted});
    REQUIRE(registry.loaded("gstest.native"));
    const auto text = [](const char* value) {
        return script::NodeValue{.type = script::NodeType::String, .text = value};
    };
    const auto read = [&](const char* key, const char* phone) {
        return registry.call("gstest.native", "setting", {text(key), text(phone)}).value.text;
    };
    const auto changed = [&] { return registry.call("gstest.native", "changedKeys", {}).value.text; };
    CHECK(read("port", "") == "\"COM3\"");
    registry.setSettings("gstest.native", R"({"global":{"port":"COM4"},"devices":{}})");
    CHECK(read("port", "") == "\"COM4\"");
    CHECK(changed() == R"(["port"])");
    wanted.settings = R"({"global":{"port":"COM4"},"devices":{"phone-1":{"port":"COM6"}}})";
    registry.apply({wanted});
    CHECK(read("port", "phone-1") == "\"COM6\"");
    CHECK(changed() == R"(["port"])");
    registry.apply({wanted});
    CHECK(changed() == "[]");
}

TEST_CASE("ADR-0112: the extension host gives a native extension its settings and tells it when the user changes one")
{
    Scratch scratch;
    const fs::path builtin = scratch / "builtin";
    const fs::path folder = builtin / "gamescrcpy.settings-probe/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    writeFile(folder / "manifest.json", R"({
  "schemaVersion": 1,
  "id": "gamescrcpy.settings-probe",
  "version": "1.0.0",
  "name": "Settings probe",
  "description": "Reads its settings.",
  "authors": ["GameScrcpy"],
  "license": "Apache-2.0",
  "requires": {"native": "^2.0"},
  "settings": [
    {"key": "port", "type": "text", "title": "Port", "default": "COM3", "scope": "device"},
    {"key": "speed", "type": "integer", "title": "Speed", "default": 2, "min": 1, "max": 9}
  ],
  "contributes": {"native": {"dll": "native.dll", "nodes": [
    {"name": "setting", "params": [{"name": "key", "type": "string"}, {"name": "phone", "type": "string"}],
     "returns": "string"},
    {"name": "changedKeys", "returns": "json"}
  ]}}
})");
    app::AppSettings settings(scratch / "data");
    app::ExtensionHost host(settings, {.user = scratch / "user", .builtin = builtin, .hostProgram = kProgram});
    host.start(false);
    REQUIRE(host.extensions().size() == 1);
    INFO(listed(host.extensions()[0].diagnostics));
    REQUIRE(host.setEnabled("gamescrcpy.settings-probe", true));
    const auto text = [](const char* value) {
        return script::NodeValue{.type = script::NodeType::String, .text = value};
    };
    const auto read = [&](const char* key, const char* phone) {
        return host.native()->call("gamescrcpy.settings-probe", "setting", {text(key), text(phone)}).value.text;
    };
    const auto changed = [&] { return host.native()->call("gamescrcpy.settings-probe", "changedKeys", {}).value.text; };
    CHECK(read("port", "") == "\"COM3\"");
    CHECK(read("speed", "") == "2");

    REQUIRE(host.setValue("gamescrcpy.settings-probe", "speed", app::settings::Scalar{std::int64_t{5}}));
    CHECK(read("speed", "") == "5");
    CHECK(changed() == R"(["speed"])");
    REQUIRE(host.setDeviceValue("phone-1", "gamescrcpy.settings-probe", "port",
                                app::settings::Scalar{std::string("COM9")}));
    CHECK(read("port", "phone-1") == "\"COM9\"");
    CHECK(read("port", "") == "\"COM3\"");
    CHECK(changed() == R"(["port"])");
    CHECK(nlohmann::json::parse(host.nativeSettings("gamescrcpy.settings-probe")) ==
          nlohmann::json{{"global", {{"port", "COM3"}, {"speed", 5}}}, {"devices", {{"phone-1", {{"port", "COM9"}}}}}});
}

TEST_CASE("ADR-0112: add-on parameters in the settings nested more than 64 levels deep are shown and changed as if "
          "none were chosen, so that they cannot overflow the stack of GameScrcpy")
{
    Scratch scratch;
    const fs::path builtin = scratch / "builtin";
    const fs::path folder = builtin / "gamescrcpy.addon-probe/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    writeFile(folder / "manifest.json", R"({
  "schemaVersion": 1,
  "id": "gamescrcpy.addon-probe",
  "version": "1.0.0",
  "name": "Add-on probe",
  "description": "Shifts every key.",
  "authors": ["GameScrcpy"],
  "license": "Apache-2.0",
  "requires": {"native": "^2.0"},
  "contributes": {"native": {"dll": "native.dll", "attachments": [
    {"name": "shift", "title": "Shift", "appliesTo": ["*"], "params": [
      {"key": "dx", "type": "number", "title": "Distance", "default": 0.1, "min": -1, "max": 1},
      {"key": "sleepMs", "type": "integer", "title": "Sleep", "default": 0, "min": 0, "max": 1000}
    ]}
  ]}}
})");
    app::AppSettings settings(scratch / "data");
    app::ExtensionHost host(settings, {.user = scratch / "user", .builtin = builtin, .hostProgram = kProgram});
    host.start(false);
    REQUIRE(host.extensions().size() == 1);
    INFO(listed(host.extensions()[0].diagnostics));
    REQUIRE(host.extensions()[0].usable());
    const std::string deep = R"({"dx":)" + std::string(30000, '[') + std::string(30000, ']') + "}";

    app::DeclaredModule declared;
    declared.params = host.extensions()[0].manifest->contributes.native->attachments.at(0).params;
    const QVariantList shown = app::declaredParamMaps(declared, deep);
    REQUIRE(shown.size() == 2);
    CHECK(shown[0].toMap().value(QStringLiteral("json")).toString() == QStringLiteral("0.1"));

    app::settings::ExtensionStates states = settings.extensions();
    states["gamescrcpy.addon-probe"].addOns["shift"].params = deep;
    settings.setExtensions(states);
    CHECK(host.setGlobalAddOnParam(QStringLiteral("gamescrcpy.addon-probe"), QStringLiteral("shift"),
                                   QStringLiteral("sleepMs"), QStringLiteral("5")));
    CHECK(settings.extensions().at("gamescrcpy.addon-probe").addOns.at("shift").params == R"({"sleepMs":5})");
}

TEST_CASE("ADR-0112: a DLL's device provider lists its devices, says when they change and does their actions, in "
          "GameScrcpy and in gs-exthost.exe")
{
    struct Changes final : exthost::HostServices {
        std::mutex mutex;
        std::vector<std::string> providers;

        void log(int, std::string_view) override {}
        bool notify(std::string_view, std::string_view) override { return false; }
        void devicesChanged(std::string_view provider) override
        {
            const std::scoped_lock lock(mutex);
            providers.emplace_back(provider);
        }
        std::vector<std::string> seen()
        {
            const std::scoped_lock lock(mutex);
            return providers;
        }
    };
    const auto json = [](std::string value) { return textValue(exthost::ValueType::Json, std::move(value)); };
    const std::string boxes =
        R"([{"id":"box-1","name":"Touch box","kind":"touchBox","state":"ready","touchMethod":"recorder"}])";
    Scratch scratch;
    std::string problem;
    Changes inside;
    const std::unique_ptr<exthost::InProcessModule> loaded = exthost::InProcessModule::load(
        {.dll = kTestDll, .store = scratch / "inside.json", .granted = {}, .services = &inside}, problem);
    REQUIRE_MESSAGE(loaded != nullptr, problem);
    Changes outside;
    exthost::HostedOptions options = hostedOptions(scratch);
    options.devicesChanged = [&outside](const std::string& provider) { outside.devicesChanged(provider); };
    const std::unique_ptr<exthost::HostedModule> hosted = exthost::HostedModule::start(std::move(options), problem);
    REQUIRE_MESSAGE(hosted != nullptr, problem);
    const std::array<std::pair<exthost::Module*, Changes*>, 2> modules{
        {{loaded.get(), &inside}, {hosted.get(), &outside}}};
    for (const auto& [module, changes] : modules) {
        REQUIRE(module->info().providers == std::vector<exthost::ProviderInfo>{{.name = "boxes", .acts = true}});
        const exthost::CallResult empty = module->listDevices(0);
        REQUIRE_MESSAGE(empty.ok(), empty.problem);
        CHECK(empty.value.text == "[]");
        CHECK(callNode(*module, "setBoxes", {json(boxes)}).value.number == GS_EXT_OK);
        REQUIRE(eventually([&] { return changes->seen() == std::vector<std::string>{"boxes"}; }));
        CHECK(module->listDevices(0).value.text == boxes);
        CHECK(module->listDevices(1).status == exthost::CallStatus::Unavailable);
        CHECK(module->actOnDevice(0, "box-1", "disconnect").ok());
        CHECK(callNode(*module, "lastAction").value.text == "box-1:disconnect");
        CHECK(module->actOnDevice(0, "", "disconnect").status == exthost::CallStatus::BadArgument);
        REQUIRE(callNode(*module, "setBoxes", {json("{}")}).ok());
        CHECK(module->listDevices(0).status == exthost::CallStatus::Failed);
        REQUIRE(callNode(*module, "setBoxes", {json("[]")}).ok());
    }
}

namespace {

struct StreamWatch {
    std::mutex mutex;
    bool opened = false;
    std::string data;
    int writable = 0;
    std::optional<std::string> closed;

    exthost::StreamEvents events()
    {
        return {.opened =
                    [this] {
                        const std::scoped_lock lock(mutex);
                        opened = true;
                    },
                .data =
                    [this](std::string_view bytes) {
                        const std::scoped_lock lock(mutex);
                        data.append(bytes);
                    },
                .writable =
                    [this] {
                        const std::scoped_lock lock(mutex);
                        ++writable;
                    },
                .closed =
                    [this](std::string why) {
                        const std::scoped_lock lock(mutex);
                        closed = std::move(why);
                    }};
    }

    bool isOpened()
    {
        const std::scoped_lock lock(mutex);
        return opened;
    }

    std::string got()
    {
        const std::scoped_lock lock(mutex);
        return data;
    }

    int writables()
    {
        const std::scoped_lock lock(mutex);
        return writable;
    }

    std::optional<std::string> reason()
    {
        const std::scoped_lock lock(mutex);
        return closed;
    }
};

}

TEST_CASE("ADR-0112: a DLL's transport lists phones and opens streams that carry bytes both ways, hold back while "
          "the DLL is busy and close from either side, in GameScrcpy and in gs-exthost.exe")
{
    struct Changes final : exthost::HostServices {
        std::mutex mutex;
        std::vector<std::string> transports;

        void log(int, std::string_view) override {}
        bool notify(std::string_view, std::string_view) override { return false; }
        void transportChanged(std::string_view transport) override
        {
            const std::scoped_lock lock(mutex);
            transports.emplace_back(transport);
        }
        std::vector<std::string> seen()
        {
            const std::scoped_lock lock(mutex);
            return transports;
        }
    };
    const auto json = [](std::string value) { return textValue(exthost::ValueType::Json, std::move(value)); };
    const auto text = [](std::string value) { return textValue(exthost::ValueType::String, std::move(value)); };
    const auto flag = [](bool on) {
        exthost::Value made;
        made.type = exthost::ValueType::Boolean;
        made.flag = on;
        return made;
    };
    Scratch scratch;
    std::string problem;
    std::deque<StreamWatch> watches;
    Changes inside;
    const std::unique_ptr<exthost::InProcessModule> loaded = exthost::InProcessModule::load(
        {.dll = kTestDll, .store = scratch / "inside.json", .granted = {}, .services = &inside}, problem);
    REQUIRE_MESSAGE(loaded != nullptr, problem);
    Changes outside;
    exthost::HostedOptions options = hostedOptions(scratch);
    options.transportChanged = [&outside](const std::string& transport) { outside.transportChanged(transport); };
    const std::unique_ptr<exthost::HostedModule> hosted = exthost::HostedModule::start(std::move(options), problem);
    REQUIRE_MESSAGE(hosted != nullptr, problem);
    const std::array<std::pair<exthost::Module*, Changes*>, 2> modules{
        {{loaded.get(), &inside}, {hosted.get(), &outside}}};
    for (const auto& [module, changes] : modules) {
        CAPTURE(module == loaded.get());
        REQUIRE(module->info().transports == std::vector<exthost::TransportInfo>{{"loop"}});
        CHECK(callNode(*module, "supports", {text("transports")}).value.flag);
        const exthost::CallResult listed = module->discover(0);
        REQUIRE_MESSAGE(listed.ok(), listed.problem);
        const nlohmann::json phones = nlohmann::json::parse(listed.value.text);
        REQUIRE(phones.size() == 3);
        CHECK(phones[0]["id"] == "echo");
        CHECK(phones[0]["model"] == "Loop");
        CHECK(module->discover(1).status == exthost::CallStatus::Unavailable);
        CHECK(callNode(*module, "setPhones", {json(R"([{"id":"echo","name":"Echo"}])")}).value.number == GS_EXT_OK);
        REQUIRE(eventually([&] { return changes->seen() == std::vector<std::string>{"loop"}; }));
        CHECK(nlohmann::json::parse(module->discover(0).value.text).size() == 1);

        StreamWatch& echo = watches.emplace_back();
        const std::string session = R"({"session":"gamescrcpy_1","token":"00"})";
        REQUIRE(module->openStream(0, 1, {.phone = "echo", .channel = "control", .session = session}, echo.events()));
        REQUIRE(eventually([&] { return echo.isOpened(); }));
        CHECK(nlohmann::json::parse(callNode(*module, "lastOpen").value.text) ==
              nlohmann::json{{"phone", "echo"}, {"channel", "control"}, {"session", nlohmann::json::parse(session)}});
        CHECK(module->sendStream(1, "hello") == exthost::StreamSent::Taken);
        REQUIRE(eventually([&] { return echo.got() == "hello"; }));
        const std::string big(exthost::kMostStreamBytes, 'b');
        CHECK(module->sendStream(1, big) == exthost::StreamSent::Taken);
        REQUIRE(eventually([&] { return echo.got().size() == 5 + big.size(); }));
        CHECK(module->sendStream(1, big + "!") == exthost::StreamSent::Failed);

        CHECK(callNode(*module, "holdSends", {flag(true)}).value.flag);
        std::size_t held = 0;
        exthost::StreamSent sent = exthost::StreamSent::Taken;
        for (int round = 0; round < 8 && sent == exthost::StreamSent::Taken; ++round) {
            sent = module->sendStream(1, big);
            if (sent == exthost::StreamSent::Taken) {
                held += big.size();
            }
        }
        CHECK(sent == exthost::StreamSent::Busy);
        CHECK(callNode(*module, "holdSends", {flag(false)}).value.flag);
        REQUIRE(eventually([&] { return echo.writables() >= 1; }));
        CHECK(module->sendStream(1, "x") == exthost::StreamSent::Taken);
        REQUIRE(eventually([&] { return echo.got().size() == 5 + big.size() + held + 1; }));
        CHECK(echo.got().back() == 'x');

        CHECK(callNode(*module, "dropStreams", {text("gone")}).value.number == 1);
        REQUIRE(eventually([&] { return echo.reason() == std::optional<std::string>("gone"); }));
        CHECK(module->sendStream(1, "late") == exthost::StreamSent::Failed);

        StreamWatch& refused = watches.emplace_back();
        REQUIRE(module->openStream(0, 2, {.phone = "refuse", .channel = "video", .session = "{}"}, refused.events()));
        REQUIRE(eventually([&] { return refused.reason() == std::optional<std::string>("nobody answers"); }));
        CHECK_FALSE(refused.isOpened());

        StreamWatch& nowhere = watches.emplace_back();
        if (module->openStream(0, 3, {.phone = "nowhere", .channel = "control", .session = "{}"}, nowhere.events())) {
            REQUIRE(eventually([&] { return nowhere.reason().has_value(); }));
        }
        CHECK_FALSE(nowhere.isOpened());

        StreamWatch& first = watches.emplace_back();
        StreamWatch& second = watches.emplace_back();
        REQUIRE(module->openStream(0, 4, {.phone = "pair", .channel = "control", .session = "{}"}, first.events()));
        REQUIRE(module->openStream(0, 5, {.phone = "pair", .channel = "control", .session = "{}"}, second.events()));
        REQUIRE(eventually([&] { return first.isOpened() && second.isOpened(); }));
        CHECK(module->sendStream(4, "ping") == exthost::StreamSent::Taken);
        REQUIRE(eventually([&] { return second.got() == "ping"; }));
        CHECK(module->sendStream(5, "pong") == exthost::StreamSent::Taken);
        REQUIRE(eventually([&] { return first.got() == "pong"; }));
        module->closeStream(4);
        REQUIRE(eventually([&] { return second.reason() == std::optional<std::string>("the other end left"); }));
        CHECK_FALSE(first.reason().has_value());
        REQUIRE(eventually([&] { return callNode(*module, "closedStreams").value.text == "[4]"; }));
    }
}

TEST_CASE("ADR-0112: the registry lists the phones of an extension allowed to provide a way to reach them, with the "
          "transport's title and icon, follows changes and carries a session's streams through the transport")
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path folder = scratch / "gstest.native/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    writeFile(folder / "icons/loop.svg", R"(<svg viewBox="0 0 24 24"><path d="M4 4 L20 20"/></svg>)");
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    app::NativeWanted wanted = testWanted(folder);
    wanted.native.nodes = {
        {.name = "setPhones", .params = {{"phones", "json"}}, .returns = "number", .description = {}, .permissions = {}},
        {.name = "lastOpen", .params = {}, .returns = "json", .description = {}, .permissions = {}}};
    wanted.native.transports = {{.name = "loop", .title = "Loopback", .icon = "svg:icons/loop.svg"}};
    wanted.transportTitles = {QStringLiteral("Loop line")};
    int changes = 0;
    QObject::connect(&registry, &app::NativeExtensions::transportPhonesChanged, [&changes] { ++changes; });
    const auto setPhones = [&](const std::string& json) {
        REQUIRE(registry.call("gstest.native", "setPhones", {script::NodeValue{.type = script::NodeType::Json, .text = json}})
                    .problem.empty());
        for (int round = 0; round < 20; ++round) {
            QCoreApplication::processEvents();
        }
    };

    registry.apply({wanted});
    CHECK(registry.transportPhones().empty());

    wanted.granted = {"transport"};
    registry.apply({wanted});
    const std::vector<app::TransportPhone> listed = registry.transportPhones();
    REQUIRE(listed.size() == 3);
    CHECK(listed[0].extension == "gstest.native");
    CHECK(listed[0].extensionName == "Test native");
    CHECK(listed[0].transport == "loop");
    CHECK(listed[0].transportTitle == "Loop line");
    CHECK(listed[0].id == "echo");
    CHECK(listed[0].name == "Echo phone");
    CHECK(listed[0].model == "Loop");
    CHECK(listed[0].detail == "answers what it gets");
    CHECK(listed[0].iconPath == "M4 4 L20 20");
    CHECK(listed[0].icon.isEmpty());
    CHECK(listed[2].id == "refuse");
    CHECK(changes >= 1);

    setPhones(R"([{"id":"echo","name":"Echo phone"},{"id":"","name":"No id"},{"id":"twin","name":"Twin"},)"
              R"({"id":"twin","name":"Twin again"},{"name":"Nameless"}])");
    const std::vector<app::TransportPhone> changed = registry.transportPhones();
    REQUIRE(changed.size() == 2);
    CHECK(changed[0].id == "echo");
    CHECK(changed[0].model.isEmpty());
    CHECK(changed[1].id == "twin");

    session::LaunchParams params;
    params.token.fill(std::uint8_t{0xAB});
    params.socketName = "gamescrcpy_0123456789abcdef";
    auto opened = registry.phoneStreams(changed[0], nullptr)->open(session::StreamChannel::Control, params, 5s, {});
    REQUIRE_MESSAGE(opened.has_value(), (opened.has_value() ? std::string() : opened.error().detail));
    const nlohmann::json open = nlohmann::json::parse(registry.call("gstest.native", "lastOpen", {}).value.text);
    CHECK(open["phone"] == "echo");
    CHECK(open["channel"] == "control");
    CHECK(open["session"]["token"] == platform::toHex(params.token));
    CHECK(open["session"]["socket"] == "gamescrcpy_0123456789abcdef");
    CHECK(open["session"]["server"].is_null());
    std::unique_ptr<session::ByteStream>& stream = *opened;
    stream->setReadTimeout(5s);
    stream->setWriteTimeout(5s);
    const std::string ping = "ping";
    REQUIRE(stream->writeAll(std::span(reinterpret_cast<const std::uint8_t*>(ping.data()), ping.size())));
    std::array<std::uint8_t, 4> back{};
    REQUIRE(stream->readExact(back));
    CHECK(std::string(back.begin(), back.end()) == "ping");
    stream.reset();

    app::TransportPhone absent = changed[0];
    absent.id = "refuse";
    const std::shared_ptr<session::PhoneStreams> retrying = registry.phoneStreams(absent, nullptr);
    const auto refused = retrying->open(session::StreamChannel::Video, params, 5s, {});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().kind == session::FailureKind::ConnectFailed);
    CHECK(refused.error().detail.find("Loop line") != std::string::npos);
    CHECK(refused.error().detail.find("nobody answers") != std::string::npos);
    CHECK_FALSE(retrying->awaitChange(std::chrono::steady_clock::now() + 50ms, {}).has_value());
    setPhones(R"([{"id":"echo","name":"Echo phone"}])");
    CHECK(retrying->awaitChange(std::chrono::steady_clock::now() + 5s, {}).has_value());

    app::TransportPhone elsewhere = changed[0];
    elsewhere.extension = "gstest.missing";
    const auto off = registry.phoneStreams(elsewhere, nullptr)->open(session::StreamChannel::Control, params, 1s, {});
    REQUIRE_FALSE(off.has_value());
    CHECK(off.error().kind == session::FailureKind::ServerEnded);

    const session::ServerPackage package({1, 2, 3}, fs::path("C:/gs/server.apk"));
    const nlohmann::json described = nlohmann::json::parse(app::streamSession(params, &package));
    CHECK(described["session"] == params.socketName);
    CHECK(described["protocol"] == 1);
    CHECK(described["server"]["file"] == "C:/gs/server.apk");
    CHECK(described["server"]["path"] == package.remotePath());
    CHECK(described["server"]["command"].get<std::string>().find("socket=gamescrcpy_0123456789abcdef") !=
          std::string::npos);
    CHECK(described["server"]["stdin"] == "GS_TOKEN=" + platform::toHex(params.token) + "\n");

    wanted.granted = {};
    registry.apply({wanted});
    CHECK(registry.transportPhones().empty());
}

namespace {

exthost::VisionImage paintedPicture(std::uint32_t width, std::uint32_t height, std::uint32_t stride,
                                    std::array<std::uint8_t, 3> bgr)
{
    const std::size_t row = std::max(std::size_t{stride}, std::size_t{4} * width);
    auto pixels = std::make_shared<std::vector<std::uint8_t>>(row * height, std::uint8_t{255});
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint8_t* pixel = pixels->data() + std::size_t{y} * stride + std::size_t{4} * x;
            pixel[0] = bgr[0];
            pixel[1] = bgr[1];
            pixel[2] = bgr[2];
            pixel[3] = 255;
        }
    }
    return {.pixels = std::shared_ptr<const std::uint8_t>(pixels, pixels->data()),
            .width = width,
            .height = height,
            .stride = stride};
}

}

TEST_CASE("ADR-0112: a DLL's vision engine gets the picture with its size and stride and gives its result, keeps to "
          "its kinds and its time limit and says why it failed, in GameScrcpy and in gs-exthost.exe")
{
    Scratch scratch;
    std::string problem;
    const std::unique_ptr<exthost::InProcessModule> loaded = exthost::InProcessModule::load(
        {.dll = kTestDll, .store = scratch / "inside.json", .granted = {}, .services = nullptr, .visionLimit = 300ms},
        problem);
    REQUIRE_MESSAGE(loaded != nullptr, problem);
    exthost::HostedOptions options = hostedOptions(scratch);
    options.visionLimit = 300ms;
    const std::unique_ptr<exthost::HostedModule> hosted = exthost::HostedModule::start(std::move(options), problem);
    REQUIRE_MESSAGE(hosted != nullptr, problem);
    const exthost::VisionImage padded = paintedPicture(8, 4, 40, {10, 20, 30});
    const std::uint32_t kinds =
        exthost::visionBit(exthost::VisionKind::Ocr) | exthost::visionBit(exthost::VisionKind::Classify);
    const std::array<std::pair<exthost::Module*, std::string>, 2> modules{
        {{loaded.get(), "8x4/40"}, {hosted.get(), "8x4/32"}}};
    for (const auto& [module, size] : modules) {
        CAPTURE(size);
        REQUIRE(module->info().visionEngines ==
                std::vector<exthost::VisionEngineInfo>{{.name = "probe", .kinds = kinds}});
        const exthost::CallResult classified =
            module->recognize(0, exthost::VisionKind::Classify, padded, R"({"mode":"mean"})");
        REQUIRE_MESSAGE(classified.ok(), classified.problem);
        CHECK(classified.value.type == exthost::ValueType::Json);
        CHECK(nlohmann::json::parse(classified.value.text) ==
              nlohmann::json::array({{{"label", "#1E140A"}, {"confidence", 1}},
                                     {{"label", size}, {"confidence", 0.5}},
                                     {{"label", R"({"mode":"mean"})"}, {"confidence", 0.25}}}));
        const exthost::CallResult read = module->recognize(0, exthost::VisionKind::Ocr, padded, {});
        REQUIRE_MESSAGE(read.ok(), read.problem);
        CHECK(nlohmann::json::parse(read.value.text) ==
              nlohmann::json::parse(R"([{"text":"OK","x":0.5,"y":0.25,"width":0.5,"height":0.25,"confidence":0.75}])"));

        CHECK(module->recognize(0, exthost::VisionKind::Detect, padded, {}).status == exthost::CallStatus::BadArgument);
        CHECK(module->recognize(1, exthost::VisionKind::Ocr, padded, {}).status == exthost::CallStatus::Unavailable);
        CHECK(module->recognize(0, exthost::VisionKind::Ocr, exthost::VisionImage{}, {}).status ==
              exthost::CallStatus::BadArgument);
        CHECK(module->recognize(0, exthost::VisionKind::Ocr, paintedPicture(8, 4, 16, {0, 0, 0}), {}).status ==
              exthost::CallStatus::BadArgument);
        const exthost::CallResult failed = module->recognize(0, exthost::VisionKind::Classify, padded, R"({"fail":1})");
        CHECK(failed.status == exthost::CallStatus::Failed);
        CHECK(failed.problem == "the test engine fails on purpose");
        const exthost::CallResult refused =
            module->recognize(0, exthost::VisionKind::Classify, padded, R"({"refuse":1})");
        CHECK(refused.status == exthost::CallStatus::BadArgument);
        CHECK(refused.problem == "the test engine does not take refuse");
        const exthost::CallResult broken =
            module->recognize(0, exthost::VisionKind::Classify, padded, R"({"broken":1})");
        CHECK(broken.status == exthost::CallStatus::Failed);
        CHECK(broken.problem == "the engine did not give a JSON array");

        const exthost::CallResult slow = module->recognize(0, exthost::VisionKind::Ocr, padded, R"({"sleepMs":900})");
        CHECK(slow.status == exthost::CallStatus::TooSlow);
        CHECK(slow.problem == "the engine took longer than 300 ms");
        const exthost::CallResult busy = module->recognize(0, exthost::VisionKind::Ocr, padded, {});
        CHECK(busy.status == exthost::CallStatus::TooSlow);
        CHECK(busy.problem == "the engine is still working on an earlier picture");
        REQUIRE(eventually([&] { return module->recognize(0, exthost::VisionKind::Ocr, padded, {}).ok(); }));
    }

    const exthost::VisionImage large = paintedPicture(1200, 1000, 4800, {200, 100, 50});
    const exthost::CallResult grown = hosted->recognize(0, exthost::VisionKind::Classify, large, {});
    REQUIRE_MESSAGE(grown.ok(), grown.problem);
    const nlohmann::json seen = nlohmann::json::parse(grown.value.text);
    CHECK(seen[0]["label"] == "#3264C8");
    CHECK(seen[1]["label"] == "1200x1000/4800");
    CHECK(seen[2]["label"] == "{}");

    const exthost::CallResult crashed = hosted->recognize(0, exthost::VisionKind::Classify, padded, R"({"crash":1})");
    CHECK_FALSE(crashed.ok());
    REQUIRE(eventually([&] { return hosted->crashes() == 1 && hosted->ready(); }));
    const exthost::CallResult again = hosted->recognize(0, exthost::VisionKind::Classify, padded, {});
    REQUIRE_MESSAGE(again.ok(), again.problem);
    CHECK(nlohmann::json::parse(again.value.text)[0]["label"] == "#1E140A");
    CHECK(callNode(*hosted, "supports", {textValue(exthost::ValueType::String, "vision-engines")}).value.flag);
    CHECK(callNode(*loaded, "supports", {textValue(exthost::ValueType::String, "vision-engines")}).value.flag);
}

TEST_CASE("ADR-0112: the registry reaches a vision engine the manifest declares with kinds its DLL recognizes, and "
          "names one whose kinds do not match")
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path folder = scratch / "gstest.native/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    app::NativeWanted wanted = testWanted(folder);
    wanted.native.visionEngines = {{.name = "probe", .title = "Probe", .kinds = {"classify"}, .languages = {}}};
    registry.apply({wanted});
    const exthost::VisionImage padded = paintedPicture(8, 4, 40, {10, 20, 30});
    const exthost::CallResult classified =
        registry.recognize("gstest.native", "probe", exthost::VisionKind::Classify, padded, "{}");
    REQUIRE_MESSAGE(classified.ok(), classified.problem);
    CHECK(nlohmann::json::parse(classified.value.text)[0]["label"] == "#1E140A");
    CHECK(registry.recognize("gstest.native", "probe", exthost::VisionKind::Detect, padded, "{}").status ==
          exthost::CallStatus::BadArgument);
    CHECK(registry.recognize("gstest.native", "nope", exthost::VisionKind::Classify, padded, "{}").status ==
          exthost::CallStatus::Unavailable);
    CHECK(registry.recognize("gstest.other", "probe", exthost::VisionKind::Classify, padded, "{}").status ==
          exthost::CallStatus::Unavailable);
    CHECK(std::ranges::none_of(registry.problems(), [](const ext::Diagnostic& problem) {
        return problem.path.find("visionEngines") != std::string::npos;
    }));

    wanted.native.visionEngines = {{.name = "probe", .title = "Probe", .kinds = {"detect"}, .languages = {}},
                                   {.name = "absent", .title = "Absent", .kinds = {"ocr"}, .languages = {}}};
    registry.apply({wanted});
    const std::vector<ext::Diagnostic> problems = registry.problems();
    CHECK(std::ranges::any_of(problems, [](const ext::Diagnostic& problem) {
        return problem.code == ext::Code::NativeMismatch && problem.detail == "probe";
    }));
    CHECK(std::ranges::any_of(problems, [](const ext::Diagnostic& problem) {
        return problem.code == ext::Code::NativeMismatch && problem.detail == "absent";
    }));
    CHECK(registry.recognize("gstest.native", "probe", exthost::VisionKind::Detect, padded, "{}").status ==
          exthost::CallStatus::Unavailable);
}

TEST_CASE("ADR-0112: the registry lists the devices of an extension allowed to add them, leaves out bad ones, "
          "follows changes and passes on actions")
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path folder = scratch / "gstest.native/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    app::NativeWanted wanted = testWanted(folder);
    wanted.native.nodes = {{.name = "setBoxes",
                            .params = {{"boxes", "json"}},
                            .returns = "number",
                            .description = {},
                            .permissions = {}},
                           {.name = "lastAction", .params = {}, .returns = "string", .description = {}, .permissions = {}}};
    wanted.native.deviceProviders = {{.name = "boxes", .title = "Boxes", .kinds = {"touchBox"}}};
    wanted.native.jobs = {{.name = "count",
                           .title = "Count",
                           .params = {},
                           .returns = "json",
                           .description = {},
                           .permissions = {},
                           .most = std::nullopt}};
    int changes = 0;
    QObject::connect(&registry, &app::NativeExtensions::providedChanged, [&changes] { ++changes; });
    writeFile(folder / "icons/box.svg", R"(<svg viewBox="0 0 24 24"><path d="M2 2 L22 22"/></svg>)");
    const std::string boxes =
        R"([{"id":"box-1","name":"Box","kind":"touchBox","summary":"USB","touchMethod":"recorder",)"
        R"("icon":"svg:icons/box.svg",)"
        R"("actions":[{"id":"flash","title":"Flash","job":"count"}]},)"
        R"({"id":"","name":"No id","kind":"touchBox"},)"
        R"({"id":"box-3","name":"Phone","kind":"phone"},)"
        R"({"id":"box-4","name":"Bad job","kind":"touchBox","actions":[{"id":"x","title":"X","job":"nope"}]},)"
        R"({"id":"box-5","name":"Bad method","kind":"touchBox","touchMethod":"nope"}])";
    const auto setBoxes = [&](const std::string& json) {
        REQUIRE(registry.call("gstest.native", "setBoxes", {script::NodeValue{.type = script::NodeType::Json, .text = json}})
                    .problem.empty());
        for (int round = 0; round < 20; ++round) {
            QCoreApplication::processEvents();
        }
    };

    registry.apply({wanted});
    setBoxes(boxes);
    CHECK(registry.providedDevices().empty());

    wanted.granted = {"devices.provide"};
    registry.apply({wanted});
    setBoxes(boxes);
    const std::vector<app::ProvidedDevice> listed = registry.providedDevices();
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].extension == "gstest.native");
    CHECK(listed[0].provider == "boxes");
    CHECK(listed[0].id == "box-1");
    CHECK(listed[0].name == "Box");
    CHECK(listed[0].summary == "USB");
    CHECK(listed[0].state == "ready");
    CHECK(listed[0].touchMethod == "gstest.native/recorder");
    CHECK(listed[0].iconPath == "M2 2 L22 22");
    CHECK(listed[0].icon.isEmpty());
    REQUIRE(listed[0].actions.size() == 1);
    CHECK(listed[0].actions[0].toMap().value(QStringLiteral("job")).toString() == "count");
    CHECK(changes >= 1);

    CHECK(registry.actOnDevice("gstest.native", "boxes", "box-1", "disconnect").ok());
    CHECK(registry.call("gstest.native", "lastAction", {}).value.text == "box-1:disconnect");
    CHECK(registry.actOnDevice("gstest.native", "nope", "box-1", "disconnect").status ==
          exthost::CallStatus::Unavailable);

    setBoxes("[]");
    CHECK(registry.providedDevices().empty());
}

TEST_CASE("ADR-0112: a device list, a job result or a job output of a DLL nested more than 64 levels deep is left "
          "out instead of overflowing the stack of GameScrcpy")
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path folder = scratch / "gstest.native/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    app::NativeWanted wanted = testWanted(folder);
    wanted.native.nodes = {{.name = "setBoxes",
                            .params = {{"boxes", "json"}},
                            .returns = "number",
                            .description = {},
                            .permissions = {}}};
    wanted.native.deviceProviders = {{.name = "boxes", .title = "Boxes", .kinds = {"touchBox"}}};
    wanted.native.jobs = {{.name = "count",
                           .title = "Count",
                           .params = {{"deep", "number"}},
                           .returns = "json",
                           .description = {},
                           .permissions = {},
                           .most = std::nullopt}};
    wanted.granted = {"devices.provide"};
    registry.apply({wanted});
    REQUIRE(registry.problems().empty());
    const auto setBoxes = [&](const std::string& json) {
        REQUIRE(registry.call("gstest.native", "setBoxes", {script::NodeValue{.type = script::NodeType::Json, .text = json}})
                    .problem.empty());
        registry.refreshProviders("gstest.native");
    };
    const std::string box = R"({"id":"box-1","name":"Box","kind":"touchBox")";
    setBoxes("[" + box + R"(,"capture":)" + std::string(100'000, '[') + std::string(100'000, ']') + "}]");
    CHECK(registry.providedDevices().empty());
    setBoxes("[" + box + "}]");
    REQUIRE(registry.providedDevices().size() == 1);
    CHECK(registry.providedDevices()[0].id == "box-1");

    struct Heard {
        std::vector<std::pair<quint64, QString>> answers;
        std::vector<std::pair<quint64, QString>> refusals;
        std::vector<QString> told;
    } heard;
    app::NativeJobs jobs(registry);
    jobs.setReplies({.answer = [&heard](std::uint64_t, quint64 request,
                                        const QString& json) { heard.answers.emplace_back(request, json); },
                     .refuse = [&heard](std::uint64_t, quint64 request,
                                        const QString& message) { heard.refusals.emplace_back(request, message); },
                     .tell = [&heard](std::uint64_t, const QString& kind, const QString&,
                                      const QString& json) { heard.told.push_back(kind + u' ' + json); }});
    const app::LogicCaller caller{.logic = 7};
    const auto runNested = [&](quint64 request, std::size_t levels) {
        const script::ServiceAnswer started =
            jobs.start(caller, R"({"extension":"gstest.native","job":"count","args":{"deep":)" +
                                   std::to_string(levels) + "}}");
        REQUIRE_MESSAGE(started.code.empty(), started.problem);
        const QString id = QString::fromStdString(nlohmann::json::parse(started.json)["id"].get<std::string>());
        const std::size_t before = heard.answers.size() + heard.refusals.size();
        jobs.result(caller, request, QStringLiteral(R"({"id":"%1"})").arg(id));
        const auto deadline = std::chrono::steady_clock::now() + 20s;
        while (heard.answers.size() + heard.refusals.size() == before && std::chrono::steady_clock::now() < deadline) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(5ms);
        }
        for (int round = 0; round < 20; ++round) {
            QCoreApplication::processEvents();
        }
    };

    runNested(21, 30'000);
    CHECK(heard.answers.empty());
    REQUIRE(heard.refusals.size() == 1);
    CHECK(heard.refusals[0] ==
          std::pair<quint64, QString>{21, QStringLiteral("GS-API-065 the job gave a result nested more than 64 levels deep")});
    CHECK(heard.told.empty());

    const QString fits = QString(64, u'[') + QString(64, u']');
    runNested(22, 64);
    REQUIRE(heard.answers.size() == 1);
    CHECK(heard.answers[0] == std::pair<quint64, QString>{22, fits});
    REQUIRE(heard.told.size() == 1);
    CHECK(heard.told[0] == QStringLiteral("job {\"kind\":\"output\",\"value\":") + fits + u'}');
}

TEST_CASE("ADR-0112: a DLL claims hardware, lets it go and is asked for it, in GameScrcpy and in gs-exthost.exe")
{
    struct Claims final : exthost::HostServices {
        std::mutex mutex;
        std::vector<std::string> said;
        exthost::ClaimResult answer = exthost::ClaimResult::Claimed;

        void log(int, std::string_view) override {}
        bool notify(std::string_view, std::string_view) override { return false; }
        exthost::ClaimResult claim(std::string_view resource, bool wait) override
        {
            const std::scoped_lock lock(mutex);
            said.push_back((wait ? "wait " : "claim ") + std::string(resource));
            return answer;
        }
        void release(std::string_view resource) override
        {
            const std::scoped_lock lock(mutex);
            said.push_back("release " + std::string(resource));
        }
        void answerWith(exthost::ClaimResult next)
        {
            const std::scoped_lock lock(mutex);
            answer = next;
        }
        std::vector<std::string> seen()
        {
            const std::scoped_lock lock(mutex);
            return said;
        }
    };
    const auto text = [](std::string value) { return textValue(exthost::ValueType::String, std::move(value)); };
    const auto flag = [](bool on) {
        exthost::Value made;
        made.type = exthost::ValueType::Boolean;
        made.flag = on;
        return made;
    };
    const auto touches = [](exthost::Module& module) {
        const exthost::CallResult seen = callNode(module, "touches");
        REQUIRE_MESSAGE(seen.ok(), seen.problem);
        return nlohmann::json::parse(seen.value.text)["seen"];
    };
    Scratch scratch;
    std::string problem;
    Claims inside;
    const std::unique_ptr<exthost::InProcessModule> loaded = exthost::InProcessModule::load(
        {.dll = kTestDll, .store = scratch / "inside.json", .granted = {}, .services = &inside}, problem);
    REQUIRE_MESSAGE(loaded != nullptr, problem);
    Claims outside;
    std::atomic<int> lost{0};
    exthost::HostedOptions options = hostedOptions(scratch);
    options.claim = [&outside](const std::string& resource, bool wait,
                               std::function<void(exthost::ClaimResult)> answer) {
        answer(outside.claim(resource, wait));
    };
    options.release = [&outside](const std::string& resource) { outside.release(resource); };
    options.touchLost = [&lost](std::uint64_t) { ++lost; };
    const std::unique_ptr<exthost::HostedModule> hosted = exthost::HostedModule::start(std::move(options), problem);
    REQUIRE_MESSAGE(hosted != nullptr, problem);
    const std::array<std::pair<exthost::Module*, Claims*>, 2> modules{
        {{loaded.get(), &inside}, {hosted.get(), &outside}}};
    for (const auto& [module, claims] : modules) {
        CAPTURE(module == loaded.get());
        CHECK(callNode(*module, "supports", {text("claims")}).value.flag);
        CHECK(callNode(*module, "supports", {text("job-output")}).value.flag);
        CHECK_FALSE(callNode(*module, "supports", {text("telepathy")}).value.flag);
        CHECK(callNode(*module, "claim", {text("serial:COM9"), flag(false)}).value.number == GS_EXT_OK);
        claims->answerWith(exthost::ClaimResult::Busy);
        CHECK(callNode(*module, "claim", {text("ble:AA:BB:CC:DD:EE:FF"), flag(true)}).value.number == GS_EXT_BUSY);
        CHECK(callNode(*module, "claim", {text("COM9"), flag(false)}).value.number == GS_EXT_BAD_ARGUMENT);
        CHECK(callNode(*module, "claim", {text("serial:COM\n9"), flag(false)}).value.number == GS_EXT_BAD_ARGUMENT);
        CHECK(callNode(*module, "release", {text("serial:")}).value.number == GS_EXT_BAD_ARGUMENT);
        CHECK(callNode(*module, "release", {text("serial:COM9")}).value.number == GS_EXT_OK);
        REQUIRE(eventually([&] {
            return claims->seen() ==
                   std::vector<std::string>{"claim serial:COM9", "wait ble:AA:BB:CC:DD:EE:FF", "release serial:COM9"};
        }));

        static_cast<void>(touches(*module));
        const std::uint64_t refused = module->openTouch(0, "phone-1", R"({"claim":"serial:COM7"})");
        if (module == loaded.get()) {
            CHECK(refused == 0);
        } else {
            REQUIRE(eventually([&] { return lost.load() == 1; }));
        }
        CHECK(touches(*module).empty());
        claims->answerWith(exthost::ClaimResult::Claimed);
        const std::uint64_t session = module->openTouch(0, "phone-1", R"({"claim":"serial:COM7"})");
        REQUIRE(session != 0);
        const nlohmann::json opened = touches(*module);
        REQUIRE(opened.size() == 1);
        CHECK(opened[0] == "open");
        CHECK(claims->seen() == std::vector<std::string>{"claim serial:COM9", "wait ble:AA:BB:CC:DD:EE:FF",
                                                          "release serial:COM9", "claim serial:COM7",
                                                          "claim serial:COM7"});
        module->closeTouch(session);

        CHECK_FALSE(module->claimWanted("serial:COM7", "Someone"));
        CHECK(callNode(*module, "lastWanted").value.text == "serial:COM7 by Someone");
        REQUIRE(callNode(*module, "yieldClaims", {flag(true)}).ok());
        CHECK(module->claimWanted("serial:COM7", "Someone else"));
        REQUIRE(eventually([&] { return claims->seen().back() == "release serial:COM7"; }));
        CHECK(callNode(*module, "lastWanted").value.text == "serial:COM7 by Someone else");
        CHECK_FALSE(module->claimWanted("COM7", "Someone"));
        REQUIRE(callNode(*module, "yieldClaims", {flag(false)}).ok());
    }
}

TEST_CASE("ADR-0112: when gs-exthost.exe stops, GameScrcpy lets go of what its DLL claimed")
{
    Scratch scratch;
    std::mutex mutex;
    std::vector<std::string> released;
    exthost::HostedOptions options = hostedOptions(scratch);
    options.claim = [](const std::string&, bool, std::function<void(exthost::ClaimResult)> answer) {
        answer(exthost::ClaimResult::Claimed);
    };
    options.release = [&mutex, &released](const std::string& resource) {
        const std::scoped_lock lock(mutex);
        released.push_back(resource);
    };
    const auto releasedSoFar = [&mutex, &released] {
        const std::scoped_lock lock(mutex);
        return released;
    };
    std::string problem;
    const std::unique_ptr<exthost::HostedModule> hosted = exthost::HostedModule::start(std::move(options), problem);
    REQUIRE_MESSAGE(hosted != nullptr, problem);
    const auto text = [](std::string value) { return textValue(exthost::ValueType::String, std::move(value)); };
    exthost::Value no;
    no.type = exthost::ValueType::Boolean;
    REQUIRE(callNode(*hosted, "claim", {text("serial:COM9"), no}).value.number == GS_EXT_OK);
    REQUIRE(callNode(*hosted, "claim", {text("hid:pad"), no}).value.number == GS_EXT_OK);
    REQUIRE(callNode(*hosted, "release", {text("hid:pad")}).value.number == GS_EXT_OK);
    REQUIRE(eventually([&] { return releasedSoFar() == std::vector<std::string>{"hid:pad"}; }));
    crashOnce(*hosted, 1);
    REQUIRE(eventually([&] { return releasedSoFar() == std::vector<std::string>{"hid:pad", "serial:COM9"}; }));
}

TEST_CASE("ADR-0112: a crash of gs-exthost.exe leaves a crash report in the extension's folder, and GameScrcpy hears the "
          "exit code")
{
    Scratch scratch;
    writeFile(scratch / "gamescrcpy.log", "a line of the program's log\n");
    std::mutex mutex;
    std::vector<std::uint32_t> codes;
    exthost::HostedOptions options = hostedOptions(scratch);
    options.crashes = scratch / "crashes with spaces" / "gstest.native";
    options.logFile = scratch / "gamescrcpy.log";
    options.crashed = [&mutex, &codes](std::uint32_t code) {
        const std::scoped_lock lock(mutex);
        codes.push_back(code);
    };
    std::string problem;
    const std::unique_ptr<exthost::HostedModule> hosted = exthost::HostedModule::start(std::move(options), problem);
    REQUIRE_MESSAGE(hosted != nullptr, problem);
    crashOnce(*hosted, 1);
    REQUIRE(eventually([&] {
        const std::scoped_lock lock(mutex);
        return codes.size() == 1;
    }));
    CHECK(codes[0] == 0xC0000005U);
    std::vector<fs::path> folders;
    for (const fs::directory_entry& entry : fs::directory_iterator(scratch / "crashes with spaces" / "gstest.native")) {
        folders.push_back(entry.path());
    }
    REQUIRE(folders.size() == 1);
    std::vector<std::string> files;
    for (const fs::directory_entry& entry : fs::directory_iterator(folders[0])) {
        files.push_back(entry.path().extension() == ".dmp" ? std::string(".dmp") : entry.path().filename().string());
    }
    std::ranges::sort(files);
    CHECK(files == std::vector<std::string>{".dmp", "crash.txt", "gamescrcpy.log"});
    const std::string summary = readFile(folders[0] / "crash.txt");
    CHECK(summary.find("exception 0xC0000005") != std::string::npos);
    CHECK(readFile(folders[0] / "gamescrcpy.log") == "a line of the program's log\n");
}

TEST_CASE("ADR-0112: two extensions share hardware through GameScrcpy: one waits for it and the other lets it go")
{
    ApplicationScope application;
    app::HardwareClaims claims;
    Scratch scratch;
    const fs::path insideFolder = scratch / "gstest.inside/1.0.0";
    const fs::path outsideFolder = scratch / "gstest.outside/1.0.0";
    for (const fs::path& folder : {insideFolder, outsideFolder}) {
        fs::create_directories(folder);
        fs::copy_file(kTestDll, folder / "native.dll");
    }
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    const auto declared = [](app::NativeWanted wanted, const char* id, const char* name, bool official) {
        wanted.id = id;
        wanted.name = QString::fromUtf8(name);
        wanted.official = official;
        wanted.native.nodes = {{.name = "claim",
                                .params = {{"resource", "string"}, {"wait", "boolean"}},
                                .returns = "number",
                                .description = {},
                                .permissions = {}},
                               {.name = "yieldClaims",
                                .params = {{"on", "boolean"}},
                                .returns = "boolean",
                                .description = {},
                                .permissions = {}},
                               {.name = "lastWanted", .params = {}, .returns = "string", .description = {},
                                .permissions = {}}};
        return wanted;
    };
    const app::NativeWanted inside = declared(testWanted(insideFolder), "gstest.inside", "Inside", true);
    const app::NativeWanted outside = declared(testWanted(outsideFolder), "gstest.outside", "Outside", false);
    registry.apply({inside, outside});
    REQUIRE(eventually([&] {
        QCoreApplication::processEvents();
        return registry.loaded("gstest.inside") && registry.loaded("gstest.outside");
    }));
    const auto text = [](const char* value) { return script::NodeValue{.type = script::NodeType::String, .text = value}; };
    const auto flag = [](bool on) { return script::NodeValue{.type = script::NodeType::Boolean, .flag = on}; };
    const auto claim = [&](const char* id, const char* resource, bool wait) {
        double status = -1;
        std::thread([&] { status = registry.call(id, "claim", {text(resource), flag(wait)}).value.number; }).join();
        return status;
    };

    CHECK(claim("gstest.outside", "serial:COM9", false) == GS_EXT_OK);
    CHECK(claims.holderName("serial:COM9") == "Outside");
    CHECK(claim("gstest.inside", "serial:com9", false) == GS_EXT_BUSY);
    CHECK(claim("gstest.inside", "serial:com9", true) == GS_EXT_BUSY);
    CHECK(registry.call("gstest.outside", "lastWanted", {}).value.text == "serial:COM9 by Inside");
    REQUIRE(registry.call("gstest.outside", "yieldClaims", {flag(true)}).problem.empty());
    CHECK(claim("gstest.inside", "serial:com9", true) == GS_EXT_OK);
    CHECK(claims.holderName("serial:COM9") == "Inside");
    CHECK(claim("gstest.outside", "serial:COM9", false) == GS_EXT_BUSY);

    registry.apply({outside});
    CHECK(claims.holderName("serial:COM9").isEmpty());
    CHECK(claim("gstest.outside", "serial:COM9", false) == GS_EXT_OK);
    registry.apply({});
    CHECK(claims.holderName("serial:COM9").isEmpty());
}

TEST_CASE("ADR-0112: an extension's events reach that extension's own logic and no other")
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path folder = scratch / "gstest.native/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    app::NativeWanted wanted = testWanted(folder);
    wanted.native.nodes = {{.name = "say",
                            .params = {{"topic", "string"}, {"json", "string"}},
                            .returns = "number",
                            .description = {},
                            .permissions = {}}};
    registry.apply({wanted});
    REQUIRE(registry.loaded("gstest.native"));

    std::vector<std::tuple<std::uint64_t, QString, QString, QString>> told;
    app::NativeEvents events(registry);
    events.setRoutes({.callers =
                          [] {
                              return std::vector<app::LogicCaller>{{.logic = 3, .extension = "gstest.native"},
                                                                   {.logic = 4, .extension = "someone.else"},
                                                                   {.logic = 5, .extension = {}},
                                                                   {.logic = 6, .extension = "gstest.native"}};
                          },
                      .tell = [&told](std::uint64_t logic, const QString& kind, const QString& id,
                                      const QString& json) { told.emplace_back(logic, kind, id, json); }});
    REQUIRE(registry.call("gstest.native", "say",
                          {script::NodeValue{.type = script::NodeType::String, .text = "board.added"},
                           script::NodeValue{.type = script::NodeType::String, .text = R"({"id":"b1"})"}})
                .problem.empty());
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (told.size() < 2 && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(5ms);
    }
    using Told = std::tuple<std::uint64_t, QString, QString, QString>;
    CHECK(told == std::vector<Told>{{3, "extEvent", "board.added", R"({"id":"b1"})"},
                                    {6, "extEvent", "board.added", R"({"id":"b1"})"}});
}

namespace {

struct FedInput {
    std::string source;
    std::string slot;
    double x = 0.0;
    double y = 0.0;
    std::uint64_t at = 0;

    friend bool operator==(const FedInput&, const FedInput&) = default;
};

class InputLog final : public exthost::HostServices
{
public:
    void log(int level, std::string_view text) override
    {
        const std::scoped_lock lock(m_mutex);
        m_lines.push_back(std::to_string(level) + " " + std::string(text));
    }

    bool notify(std::string_view, std::string_view) override { return false; }

    void inputEvent(std::string_view source, std::string_view slot, double x, double y, std::uint64_t at) override
    {
        add({std::string(source), std::string(slot), x, y, at});
    }

    void add(FedInput heard)
    {
        const std::scoped_lock lock(m_mutex);
        m_fed.push_back(std::move(heard));
    }

    [[nodiscard]] std::vector<FedInput> fed()
    {
        const std::scoped_lock lock(m_mutex);
        return m_fed;
    }

    [[nodiscard]] std::vector<std::string> lines()
    {
        const std::scoped_lock lock(m_mutex);
        return m_lines;
    }

private:
    std::mutex m_mutex;
    std::vector<FedInput> m_fed;
    std::vector<std::string> m_lines;
};

std::vector<exthost::DeclaredInput> padInputs()
{
    return {{.source = "pad",
             .slotList = {{.name = "a", .kind = exthost::InputSlotKind::Button},
                          {.name = "knob", .kind = exthost::InputSlotKind::Axis},
                          {.name = "stick", .kind = exthost::InputSlotKind::Stick}}},
            {.source = "other", .slotList = {{.name = "b", .kind = exthost::InputSlotKind::Button}}}};
}

class Presses final : public input::TouchFrameSink, public input::EngineListener
{
public:
    bool trySend(const protocol::TouchFrame&) override { return true; }
    void pointerState(input::PointerState) override {}
    void notice(input::EngineNotice) override {}
    void liveChanged(const std::vector<bool>&) override {}
    void controlPressed(std::string_view id, bool down) override { presses.emplace_back(id, down); }

    std::vector<std::pair<std::string, bool>> presses;
};

void checkProvidedInput(bool official)
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path folder = scratch / "gstest.native/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    const input::LoadResult profile = input::loadProfile(
        R"({"format":"gamescrcpy.profile","version":3,"controls":[)"
        R"({"id":"jump","type":"hold","offset":[0.3,0.2],"inputs":{"press":"Ext.gstest.native.pad.a"}}]})");
    REQUIRE(profile.ok());
    Presses presses;
    input::MappingEngine engine(profile.profile,
                                input::EngineConfig{.screen = {.width = 2400, .height = 1080},
                                                    .refreshHz = 60.0,
                                                    .pacingTick = Microseconds{0}},
                                presses, presses);
    Microseconds now{0};
    QStringList phones;
    QStringList seen;
    app::HardwareAccess access(nullptr, {});
    access.setFeed(
        [&](const QString& serial, const input::DeviceEvent& event) {
            phones.append(serial);
            now += Microseconds{1000};
            input::deliver(event, engine, now);
        },
        [] { return QStringLiteral("PHONE"); });
    QObject::connect(&access, &app::HardwareAccess::inputSeen, [&seen](const QString& name) { seen.append(name); });
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    QObject::connect(&registry, &app::NativeExtensions::inputEvent, &access, &app::HardwareAccess::provideInput);
    QObject::connect(&registry, &app::NativeExtensions::inputsEnded, &access, &app::HardwareAccess::endProvidedInput);
    app::NativeWanted wanted = testWanted(folder);
    wanted.official = official;
    wanted.native.nodes = {{.name = "sendInput",
                            .params = {{"source", "string"}, {"slot", "string"}, {"x", "number"}, {"y", "number"},
                                       {"at", "number"}},
                            .returns = "number",
                            .description = {},
                            .permissions = {}},
                           {.name = "crash", .params = {}, .returns = {}, .description = {}, .permissions = {}}};
    wanted.native.inputSources = {{.name = "pad",
                                   .title = "Test pad",
                                   .slotList = {{.name = "a", .kind = ext::InputSlotKind::Button, .title = "Button A"},
                                                {.name = "stick", .kind = ext::InputSlotKind::Stick, .title = {}}}}};
    const auto text = [](const char* value) { return script::NodeValue{.type = script::NodeType::String, .text = value}; };
    const auto number = [](double value) {
        return script::NodeValue{.type = script::NodeType::Number, .number = value};
    };
    const auto send = [&](const char* slot, double x) {
        script::NodeAnswer answer;
        REQUIRE(eventually([&] {
            QCoreApplication::processEvents();
            answer = registry.call("gstest.native", "sendInput",
                                   {text("pad"), text(slot), number(x), number(0), number(0)});
            return answer.code.empty();
        }));
        return answer.value.number;
    };
    const auto pressed = [&](std::size_t count) {
        return eventually([&] {
            QCoreApplication::processEvents();
            return presses.presses.size() == count;
        });
    };
    using Press = std::pair<std::string, bool>;

    registry.apply({wanted});
    CHECK(send("a", 1) == GS_EXT_DENIED);
    for (int round = 0; round < 20; ++round) {
        QCoreApplication::processEvents();
    }
    CHECK(presses.presses.empty());
    CHECK(seen.isEmpty());

    wanted.granted = {"input.provide"};
    registry.apply({wanted});
    REQUIRE(eventually([&] {
        QCoreApplication::processEvents();
        return registry.inputTitles().contains("Ext.gstest.native.pad.a");
    }));
    CHECK(registry.inputTitles().at("Ext.gstest.native.pad.a") ==
          app::ExtensionInputTitle{.source = QStringLiteral("Test pad"), .slot = QStringLiteral("Button A")});
    CHECK(registry.inputTitles().at("Ext.gstest.native.pad.stick").slot == QStringLiteral("stick"));
    const std::optional<input::Chord> chord = input::parseChord("Ext.gstest.native.pad.a");
    REQUIRE(chord.has_value());
    CHECK(app::keyLabel(*chord).text == QStringLiteral("Test pad \u00B7 Button A"));

    CHECK(send("a", 1) == GS_EXT_OK);
    REQUIRE(pressed(1));
    CHECK(presses.presses[0] == Press{"jump", true});
    CHECK(seen == QStringList{QStringLiteral("Ext.gstest.native.pad.a")});
    CHECK(phones == QStringList{QStringLiteral("PHONE")});
    CHECK(send("a", 0) == GS_EXT_OK);
    REQUIRE(pressed(2));
    CHECK(presses.presses[1] == Press{"jump", false});

    CHECK(send("a", 1) == GS_EXT_OK);
    REQUIRE(pressed(3));
    if (!official) {
        static_cast<void>(registry.call("gstest.native", "crash", {}));
        REQUIRE(pressed(4));
        CHECK(presses.presses[3] == Press{"jump", false});
        CHECK(send("a", 1) == GS_EXT_OK);
        REQUIRE(pressed(5));
    }
    registry.apply({});
    REQUIRE(presses.presses.size() == (official ? 4U : 6U));
    CHECK(presses.presses.back() == Press{"jump", false});
    CHECK(registry.inputTitles().empty());
}

}

TEST_CASE("ADR-0112: a DLL feeds the buttons, axes and sticks of its input sources in GameScrcpy and in "
          "gs-exthost.exe; slots the manifest does not declare and values out of range are refused, and a source "
          "passes 1000 events a second")
{
    const auto text = [](std::string value) { return textValue(exthost::ValueType::String, std::move(value)); };
    const auto send = [&text](exthost::Module& module, const char* source, const char* slot, double x, double y,
                              double at) {
        const exthost::CallResult sent = callNode(
            module, "sendInput", {text(source), text(slot), numberValue(x), numberValue(y), numberValue(at)});
        REQUIRE_MESSAGE(sent.ok(), sent.problem);
        return sent.value.number;
    };
    Scratch scratch;
    std::string problem;
    InputLog inside;
    const std::unique_ptr<exthost::InProcessModule> loaded =
        exthost::InProcessModule::load({.dll = kTestDll,
                                        .store = scratch / "inside.json",
                                        .granted = {"input.provide"},
                                        .services = &inside,
                                        .inputs = padInputs()},
                                       problem);
    REQUIRE_MESSAGE(loaded != nullptr, problem);
    InputLog outside;
    exthost::HostedOptions options = hostedOptions(scratch);
    options.granted = {"input.provide"};
    options.log = [&outside](int level, const std::string& line) { outside.log(level, line); };
    options.inputs = padInputs();
    options.input = [&outside](const std::string& source, const std::string& slot, double x, double y,
                               std::uint64_t at) { outside.add({source, slot, x, y, at}); };
    const std::unique_ptr<exthost::HostedModule> hosted = exthost::HostedModule::start(std::move(options), problem);
    REQUIRE_MESSAGE(hosted != nullptr, problem);
    const std::array<std::pair<exthost::Module*, InputLog*>, 2> modules{
        {{loaded.get(), &inside}, {hosted.get(), &outside}}};
    for (const auto& [module, heard] : modules) {
        CAPTURE(module == loaded.get());
        CHECK(module->info().inputSources == std::vector<exthost::InputSourceInfo>{{.name = "pad"}});
        CHECK(callNode(*module, "supports", {text("input-sources")}).value.flag);
        CHECK(send(*module, "pad", "a", 1, 0, 1234) == GS_EXT_OK);
        CHECK(send(*module, "pad", "a", 1, 0, 1235) == GS_EXT_OK);
        CHECK(send(*module, "pad", "knob", -0.25, 0.75, 1236) == GS_EXT_OK);
        CHECK(send(*module, "pad", "stick", 0.5, -1, 1237) == GS_EXT_OK);
        CHECK(send(*module, "pad", "a", 0, 0, 0) == GS_EXT_OK);
        CHECK(send(*module, "pad", "nope", 1, 0, 0) == GS_EXT_BAD_ARGUMENT);
        CHECK(send(*module, "other", "b", 1, 0, 0) == GS_EXT_BAD_ARGUMENT);
        CHECK(send(*module, "missing", "a", 1, 0, 0) == GS_EXT_BAD_ARGUMENT);
        CHECK(send(*module, "pad", "a", 0.5, 0, 0) == GS_EXT_BAD_ARGUMENT);
        CHECK(send(*module, "pad", "knob", 1.5, 0, 0) == GS_EXT_BAD_ARGUMENT);
        CHECK(send(*module, "pad", "stick", 0, -2, 0) == GS_EXT_BAD_ARGUMENT);
        REQUIRE(eventually([&] { return heard->fed().size() == 4; }));
        std::vector<FedInput> fed = heard->fed();
        CHECK(fed[0] == FedInput{"pad", "a", 1.0, 0.0, 1234});
        CHECK(fed[1] == FedInput{"pad", "knob", -0.25, 0.0, 1236});
        CHECK(fed[2] == FedInput{"pad", "stick", 0.5, -1.0, 1237});
        CHECK(fed[3].slot == "a");
        CHECK(fed[3].x == 0.0);
        CHECK(fed[3].at > 0);

        const exthost::CallResult burst =
            callNode(*module, "sendInputs", {text("pad"), text("knob"), numberValue(1500)});
        REQUIRE_MESSAGE(burst.ok(), burst.problem);
        const auto passed = static_cast<std::size_t>(burst.value.number);
        CHECK(passed >= 996);
        CHECK(passed < 1500);
        CHECK(send(*module, "pad", "a", 1, 0, 0) == GS_EXT_FAILED);
        CHECK(send(*module, "pad", "knob", 0, 0, 0) == GS_EXT_OK);
        REQUIRE(eventually([&] {
            const std::vector<FedInput> now = heard->fed();
            return now.size() >= 5 + 996 && now.back().slot == "knob" && now.back().x == 0.0;
        }));
        fed = heard->fed();
        const std::size_t through = fed.size() - 5;
        CHECK(through >= 996);
        CHECK(through <= passed);
        const std::vector<std::string> lines = heard->lines();
        CHECK(std::ranges::count_if(lines, [](const std::string& line) {
                  return line.starts_with("1 ") && line.find("pad sent more than 1000 events") != std::string::npos;
              }) == 1);
    }
}

TEST_CASE("ADR-0112: a binding Ext.gstest.native.pad.a holds its control while the DLL's button is down and lets go "
          "when it comes up, when gs-exthost.exe stops or when the extension stops")
{
    SUBCASE("inside GameScrcpy")
    {
        checkProvidedInput(true);
    }
    SUBCASE("in gs-exthost.exe")
    {
        checkProvidedInput(false);
    }
}

TEST_CASE("ADR-0112: the extension host lists an extension's input devices and names their slots on key caps by "
          "their titles in the language of the interface")
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path builtin = scratch / "builtin";
    const fs::path folder = builtin / "gamescrcpy.pedal-probe/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    writeFile(folder / "manifest.json", R"({
  "schemaVersion": 1,
  "id": "gamescrcpy.pedal-probe",
  "version": "1.0.0",
  "name": "Pedal probe",
  "license": "Apache-2.0",
  "requires": {"native": "^2.0"},
  "contributes": {"native": {"dll": "native.dll", "inputSources": [
    {"name": "pad", "title": "Test pad", "slots": [{"name": "a", "kind": "button", "title": "Button A"},
                                                   {"name": "stick", "kind": "stick"}]}
  ]}},
  "permissions": [{"name": "input.provide", "reason": "Sends the pad to the key mapping."}],
  "l10n": {"zh_CN": {"inputSources.pad.title": "测试手柄", "inputSources.pad.slots.a.title": "A 键"}}
})");
    writeFile(scratch / "data/settings.json",
              R"({"format": "gamescrcpy.settings", "version": 1, "general": {"language": "zh_CN"}})");
    app::AppSettings settings(scratch / "data");
    app::ExtensionHost host(settings, {.user = scratch / "user", .builtin = builtin, .hostProgram = kProgram});
    host.start(false);
    REQUIRE(host.extensions().size() == 1);
    INFO(listed(host.extensions()[0].diagnostics));
    REQUIRE(host.setEnabled("gamescrcpy.pedal-probe", true));
    CHECK(host.problems().empty());
    const QVariantMap entry = host.entries()[0].toMap();
    CHECK(entry.value(QStringLiteral("native")).toMap().value(QStringLiteral("inputSources")).toStringList() ==
          QStringList{QStringLiteral("测试手柄")});
    const QVariantList permissions = entry.value(QStringLiteral("permissions")).toList();
    REQUIRE(permissions.size() == 1);
    CHECK(permissions[0].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("input.provide"));
    const app::ExtensionInputTitles titles = host.native()->inputTitles();
    REQUIRE(titles.size() == 2);
    CHECK(titles.at("Ext.gamescrcpy.pedal-probe.pad.a") ==
          app::ExtensionInputTitle{.source = QStringLiteral("测试手柄"), .slot = QStringLiteral("A 键")});
    CHECK(titles.at("Ext.gamescrcpy.pedal-probe.pad.stick") ==
          app::ExtensionInputTitle{.source = QStringLiteral("测试手柄"), .slot = QStringLiteral("stick")});
    const std::optional<input::Chord> chord = input::parseChord("Ext.gamescrcpy.pedal-probe.pad.a");
    REQUIRE(chord.has_value());
    CHECK(app::keyLabel(*chord).text == QStringLiteral("测试手柄 \u00B7 A 键"));
    REQUIRE(host.setEnabled("gamescrcpy.pedal-probe", false));
    CHECK(host.native()->inputTitles().empty());
    CHECK(app::keyLabel(*chord).text == QStringLiteral("pad \u00B7 a"));
}

TEST_CASE("ADR-0112: a DLL made for interface version 1 is not loaded and the message says to build it again")
{
    std::string problem;
    CHECK(exthost::InProcessModule::load({.dll = fs::path(GS_TEST_NATIVE_OLD_DLL)}, problem) == nullptr);
    CHECK(problem.find("older GameScrcpy") != std::string::npos);
    CHECK(problem.find("build it again") != std::string::npos);
}

TEST_CASE("a DLL inside GameScrcpy checks the values going in and out and reports a node that takes too long "
          "(ADR-0097)")
{
    Scratch scratch;
    std::string problem;
    const std::unique_ptr<exthost::InProcessModule> module = exthost::InProcessModule::load(
        {.dll = kTestDll, .store = scratch / "store.json", .granted = {}, .services = nullptr}, problem);
    REQUIRE_MESSAGE(module != nullptr, problem);
    CHECK(callNode(*module, "add", {numberValue(2), numberValue(40)}).value.number == 42.0);
    CHECK(callNode(*module, "add", {numberValue(2), textValue(exthost::ValueType::String, "x")}).status ==
          exthost::CallStatus::BadArgument);
    CHECK(callNode(*module, "echo", {textValue(exthost::ValueType::String, std::string("\xFF\xFE"))}).status ==
          exthost::CallStatus::BadArgument);

    const exthost::CallResult wrong = callNode(*module, "wrongType");
    CHECK(wrong.status == exthost::CallStatus::Failed);
    CHECK(wrong.problem.find("string") != std::string::npos);
    const exthost::CallResult failed = callNode(*module, "fail");
    CHECK(failed.status == exthost::CallStatus::Failed);
    CHECK(failed.problem == "the test node fails on purpose");

    exthost::Value wait;
    wait.type = exthost::ValueType::Duration;
    wait.number = 150;
    CHECK(callNode(*module, "slow", {wait}).status == exthost::CallStatus::TooSlow);

    CHECK(callNode(*module, "allowed", {textValue(exthost::ValueType::String, "notify")}).value.flag == false);
    CHECK(callNode(*module, "remember",
                   {textValue(exthost::ValueType::String, "best"), textValue(exthost::ValueType::Json, "[1,2]")})
              .value.flag);
    CHECK(callNode(*module, "recall", {textValue(exthost::ValueType::String, "best")}).value.text == "[1,2]");
    CHECK(callNode(*module, "recall", {textValue(exthost::ValueType::String, "none")}).value.text == "null");
    CHECK(fs::exists(scratch / "store.json"));
}

TEST_CASE("a node runs through gs-exthost.exe with values of every type going there and back (ADR-0097)")
{
    Scratch scratch;
    std::string problem;
    std::unique_ptr<exthost::HostedModule> module = exthost::HostedModule::start(hostedOptions(scratch), problem);
    REQUIRE_MESSAGE(module != nullptr, problem);
    REQUIRE(module->info().touchMethods.size() == 1);
    CHECK(module->info().touchMethods[0].name == "recorder");
    CHECK(module->info().touchMethods[0].maxContacts == 4);
    CHECK(module->info().touchMethods[0].canCancel);
    CHECK(module->info().touchMethods[0].keys);
    CHECK(module->working());

    const exthost::CallResult sum = callNode(*module, "add", {numberValue(2), numberValue(3.5)});
    REQUIRE_MESSAGE(sum.ok(), sum.problem);
    CHECK(sum.value.number == 5.5);
    CHECK(callNode(*module, "echo", {textValue(exthost::ValueType::String, "h\xC3\xA9llo")}).value.text ==
          "h\xC3\xA9llo");

    exthost::Value at;
    at.type = exthost::ValueType::Point;
    at.x = 0.25;
    at.y = 0.75;
    exthost::Value area;
    area.type = exthost::ValueType::Region;
    area.x = 0.1;
    area.y = 0.2;
    area.width = 0.3;
    area.height = 0.4;
    exthost::Value wait;
    wait.type = exthost::ValueType::Duration;
    wait.number = 250;
    exthost::Value on;
    on.type = exthost::ValueType::Boolean;
    on.flag = true;
    const exthost::CallResult mirrored =
        callNode(*module, "mirror",
                 {at, area, textValue(exthost::ValueType::Color, "#FF8800"), wait,
                  textValue(exthost::ValueType::Json, R"({"a":[1,2]})"), on});
    REQUIRE_MESSAGE(mirrored.ok(), mirrored.problem);
    const nlohmann::json echoed = nlohmann::json::parse(mirrored.value.text);
    CHECK(echoed["at"] == nlohmann::json::array({0.25, 0.75}));
    CHECK(echoed["area"] == nlohmann::json::array({0.1, 0.2, 0.3, 0.4}));
    CHECK(echoed["tint"] == "#FF8800");
    CHECK(echoed["wait"] == 250);
    CHECK(echoed["data"] == nlohmann::json{{"a", {1, 2}}});
    CHECK(echoed["on"] == true);

    const exthost::CallResult failed = callNode(*module, "fail");
    CHECK(failed.status == exthost::CallStatus::Failed);
    CHECK(failed.problem == "the test node fails on purpose");
    CHECK(callNode(*module, "add", {numberValue(1)}).status == exthost::CallStatus::BadArgument);
    CHECK(callNode(*module, "allowed", {textValue(exthost::ValueType::String, "notify")}).value.flag);
    CHECK_FALSE(callNode(*module, "allowed", {textValue(exthost::ValueType::String, "network")}).value.flag);
    CHECK(callNode(*module, "remember",
                   {textValue(exthost::ValueType::String, "best"), textValue(exthost::ValueType::Json, "42")})
              .value.flag);
    CHECK(callNode(*module, "recall", {textValue(exthost::ValueType::String, "best")}).value.text == "42");
    CHECK(fs::exists(scratch / "store/gstest.native.json"));
    CHECK(module->crashes() == 0);
}

TEST_CASE("a touch method in gs-exthost.exe gets each finger, and a failing one loses its session (ADR-0097)")
{
    Scratch scratch;
    std::mutex mutex;
    std::vector<std::uint64_t> lost;
    exthost::HostedOptions options = hostedOptions(scratch);
    options.touchLost = [&](std::uint64_t session) {
        const std::scoped_lock lock(mutex);
        lost.push_back(session);
    };
    std::string problem;
    std::unique_ptr<exthost::HostedModule> module = exthost::HostedModule::start(std::move(options), problem);
    REQUIRE_MESSAGE(module != nullptr, problem);

    const std::uint64_t session = module->openTouch(0, "emulator-5554");
    REQUIRE(session != 0);
    exthost::TouchFrame frame;
    frame.rotation = 1;
    frame.contacts.push_back({.id = 0,
                              .action = exthost::TouchAction::Down,
                              .x = 0.25,
                              .y = 0.75,
                              .naturalX = 0.25,
                              .naturalY = 0.25,
                              .pressure = 1.0});
    CHECK(module->touch(session, frame));
    CHECK(module->key(session, {.keycode = 4, .down = true, .repeat = 0, .meta = 0}));
    const exthost::CallResult seen = callNode(*module, "touches");
    REQUIRE_MESSAGE(seen.ok(), seen.problem);
    const nlohmann::json touches = nlohmann::json::parse(seen.value.text);
    CHECK(touches["device"] == "emulator-5554");
    REQUIRE(touches["seen"].size() == 3);
    CHECK(touches["seen"][0] == "open");
    CHECK(touches["seen"][1] == nlohmann::json::array({0, 0, 0.25, 0.75, 0.25, 0.25, 1}));
    CHECK(touches["seen"][2] == nlohmann::json::array({"key", 4, 1}));

    REQUIRE(callNode(*module, "breakTouch").ok());
    CHECK(module->touch(session, frame));
    REQUIRE(eventually([&] {
        const std::scoped_lock lock(mutex);
        return !lost.empty();
    }));
    {
        const std::scoped_lock lock(mutex);
        CHECK(lost == std::vector<std::uint64_t>{session});
    }
    CHECK_FALSE(module->touch(session, frame));
    CHECK(module->working());
}

TEST_CASE("a touch method gets its options, typed text, the phone's mouse and a gamepad, in GameScrcpy and in "
          "gs-exthost.exe (ADR-0102)")
{
    Scratch scratch;
    std::string problem;
    const std::unique_ptr<exthost::InProcessModule> inside = exthost::InProcessModule::load(
        {.dll = kTestDll, .store = scratch / "inside.json", .granted = {}, .services = nullptr}, problem);
    REQUIRE_MESSAGE(inside != nullptr, problem);
    const std::unique_ptr<exthost::HostedModule> hosted =
        exthost::HostedModule::start(hostedOptions(scratch), problem);
    REQUIRE_MESSAGE(hosted != nullptr, problem);
    for (exthost::Module* module :
         {static_cast<exthost::Module*>(inside.get()), static_cast<exthost::Module*>(hosted.get())}) {
        CAPTURE(module == inside.get());
        const exthost::TouchMethodInfo& method = module->info().touchMethods.at(0);
        CHECK(method.text);
        CHECK(method.mouse);
        CHECK(method.gamepad);
        REQUIRE(callNode(*module, "touches").ok());
        const std::uint64_t session = module->openTouch(0, "phone", R"({"port":"COM7"})");
        REQUIRE(session != 0);
        CHECK(module->text(session, "gg wp"));
        CHECK_FALSE(module->text(session, std::string("\xff", 1)));
        CHECK(module->mouse(session, {.buttons = 1, .dx = -3, .dy = 4, .wheel = 1}));
        CHECK(module->gamepad(session, {.buttons = 0x11,
                                        .hat = 2,
                                        .leftX = -32768,
                                        .leftY = 100,
                                        .rightX = 0,
                                        .rightY = 32767,
                                        .leftTrigger = 255,
                                        .rightTrigger = 7}));
        const exthost::CallResult seen = callNode(*module, "touches");
        REQUIRE_MESSAGE(seen.ok(), seen.problem);
        const nlohmann::json touches = nlohmann::json::parse(seen.value.text);
        CHECK(touches["options"] == nlohmann::json{{"port", "COM7"}});
        REQUIRE(touches["seen"].size() == 4);
        CHECK(touches["seen"][0] == "open");
        CHECK(touches["seen"][1] == nlohmann::json::array({"text", "gg wp"}));
        CHECK(touches["seen"][2] == nlohmann::json::array({"mouse", 1, -3, 4, 1}));
        CHECK(touches["seen"][3] == nlohmann::json::array({"pad", 17, 2, -32768, 100, 0, 32767, 255, 7}));
        module->closeTouch(session);
    }
}

TEST_CASE("gs-exthost.exe starts again after a crash, and three crashes within a minute stop the extension "
          "(ADR-0097)")
{
    Scratch scratch;
    ManualClock clock;
    std::atomic<int> restarts{0};
    std::atomic<int> stops{0};
    exthost::HostedOptions options = hostedOptions(scratch);
    options.clock = &clock;
    options.restarted = [&restarts] { ++restarts; };
    options.stopped = [&stops] { ++stops; };
    std::string problem;
    std::unique_ptr<exthost::HostedModule> module = exthost::HostedModule::start(std::move(options), problem);
    REQUIRE_MESSAGE(module != nullptr, problem);

    crashOnce(*module, 1);
    REQUIRE(module->waitReady(20s));
    CHECK(restarts == 1);
    CHECK(callNode(*module, "add", {numberValue(1), numberValue(1)}).value.number == 2.0);

    clock.advance(30s);
    crashOnce(*module, 2);
    REQUIRE(module->waitReady(20s));

    clock.advance(31s);
    crashOnce(*module, 3);
    REQUIRE(module->waitReady(20s));
    CHECK(module->working());
    CHECK(restarts == 3);
    CHECK(stops == 0);

    clock.advance(1s);
    crashOnce(*module, 4);
    CHECK_FALSE(module->waitReady(20s));
    CHECK_FALSE(module->working());
    REQUIRE(eventually([&stops] { return stops != 0; }));
    CHECK(stops == 1);
    CHECK(restarts == 3);
    const exthost::CallResult after = callNode(*module, "add", {numberValue(1), numberValue(1)});
    CHECK(after.status == exthost::CallStatus::Unavailable);
    CHECK(module->openTouch(0, "phone") == 0);
}

TEST_CASE("ADR-0112: the touch method of the ESP32 touch box built into GameScrcpy 2.0 is unknown now, so a phone "
          "that kept it touches the normal way")
{
    Scratch scratch;
    const fs::path folder = scratch / "gstest.native/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    registry.apply({testWanted(folder)});
    REQUIRE(registry.loaded("gstest.native"));
    test::SinkLog injection;
    CHECK(registry.openTouch("gamescrcpy.esp32/esp32", "R58M123ABC", injection, {}) == nullptr);
    CHECK(injection.frames.empty());
}

TEST_CASE("the registry offers the nodes and touch methods of a native extension, names a missing permission and "
          "falls back to normal touch when the method fails (ADR-0097)")
{
    Scratch scratch;
    const fs::path folder = scratch / "gstest.native/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    int changes = 0;
    QObject::connect(&registry, &app::NativeExtensions::changed, [&changes] { ++changes; });
    registry.apply({testWanted(folder)});
    CHECK(changes == 1);
    CHECK(registry.loaded("gstest.native"));

    const std::vector<script::NodeInfo> nodes = registry.nodes();
    std::vector<std::string> names;
    for (const script::NodeInfo& node : nodes) {
        names.push_back(node.name);
    }
    CHECK(names == std::vector<std::string>{"add", "allowed", "touches", "breakTouch"});
    CHECK(nodes[0].extensionName == "Test native");
    CHECK(nodes[0].params == std::vector<script::NodeParam>{{"a", script::NodeType::Number},
                                                             {"b", script::NodeType::Number}});
    CHECK(nodes[0].returns == script::NodeType::Number);
    CHECK(nodes[0].description == "Adds two numbers.");
    const std::vector<ext::Diagnostic> problems = registry.problems();
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == ext::Code::NativeMismatch);
    CHECK(problems[0].path == R"($["gstest.native"].contributes.native.nodes[4])");

    script::NodeValue one;
    one.type = script::NodeType::Number;
    one.number = 1;
    const script::NodeAnswer sum = registry.call("gstest.native", "add", {one, one});
    CHECK(sum.code.empty());
    CHECK(sum.value.number == 2.0);
    script::NodeValue permission;
    permission.type = script::NodeType::String;
    permission.text = "notify";
    const script::NodeAnswer refused = registry.call("gstest.native", "allowed", {permission});
    CHECK(refused.code == "GS-API-063");
    CHECK(refused.problem.find("\"notify\"") != std::string::npos);
    CHECK(registry.call("gstest.native", "echo", {one}).code == "GS-API-060");
    CHECK(registry.call("someone.else", "add", {one, one}).code == "GS-API-060");

    const QVariantList methods = registry.touchMethods();
    REQUIRE(methods.size() == 1);
    CHECK(methods[0].toMap().value(QStringLiteral("key")).toString() == QStringLiteral("gstest.native/recorder"));
    CHECK(methods[0].toMap().value(QStringLiteral("title")).toString() == QStringLiteral("Recorder"));
    CHECK(registry.touchTitle("gstest.native/recorder") == QStringLiteral("Recorder"));
    test::SinkLog unused;
    CHECK(registry.openTouch("gstest.native/missing", "phone", unused, {}) == nullptr);

    test::SinkLog injection;
    int fellBack = 0;
    registry.setTouchOptions([](std::string_view key, std::string_view device) {
        return nlohmann::json{{"key", std::string(key)}, {"device", std::string(device)}}.dump();
    });
    std::unique_ptr<app::ExtensionTouch> touch =
        registry.openTouch("gstest.native/recorder", "phone", injection, [&fellBack] { ++fellBack; });
    REQUIRE(touch != nullptr);
    touch->setVideo(3, 0);
    CHECK(touch->capabilities().maxContacts == 4);
    CHECK(touch->capabilities().gamepad);
    protocol::TouchFrame frame;
    frame.videoSession = 3;
    frame.points.push_back(test::pressAt(2, 0.5, 0.5));
    CHECK(touch->send(frame));
    const script::NodeAnswer seen = registry.call("gstest.native", "touches", {});
    REQUIRE(seen.code.empty());
    const nlohmann::json touches = nlohmann::json::parse(seen.value.text);
    CHECK(touches["device"] == "phone");
    CHECK(touches["options"] == nlohmann::json{{"key", "gstest.native/recorder"}, {"device", "phone"}});
    REQUIRE(touches["seen"].size() == 2);
    CHECK(touches["seen"][1][0] == 0);
    CHECK(touches["seen"][1][1] == 0);
    CHECK(injection.frames.empty());

    protocol::Text typed;
    typed.text = "hi";
    CHECK(touch->send(typed));
    input::PadPassState pad;
    pad.buttons = input::pad_button::kSouth;
    pad.hat = 2;
    CHECK(touch->padExternal(pad));
    const script::NodeAnswer more = registry.call("gstest.native", "touches", {});
    REQUIRE(more.code.empty());
    const nlohmann::json typing = nlohmann::json::parse(more.value.text);
    REQUIRE(typing["seen"].size() == 2);
    CHECK(typing["seen"][0] == nlohmann::json::array({"text", "hi"}));
    CHECK(typing["seen"][1] == nlohmann::json::array({"pad", 1, 2, 0, 0, 0, 0, 0, 0}));

    REQUIRE(registry.call("gstest.native", "breakTouch", {}).code.empty());
    protocol::TouchFrame next;
    next.videoSession = 3;
    next.points.push_back(test::pressAt(3, 0.25, 0.25));
    CHECK(touch->send(next));
    CHECK(touch->fellBack());
    CHECK(fellBack == 1);
    REQUIRE(injection.frames.size() == 1);
    CHECK(injection.frames[0].points.size() == 1);
    CHECK(injection.frames[0].points[0].pointerId == 3);
    protocol::TouchFrame stray;
    stray.videoSession = 3;
    stray.points.push_back(test::liftAt(2, 0.5, 0.5));
    CHECK(touch->send(stray));
    CHECK(injection.frames.size() == 1);
    CHECK(touch->capabilities() == app::SinkCapabilities{});
    touch.reset();

    registry.apply({});
    CHECK(registry.nodes().empty());
    CHECK(registry.touchMethods().isEmpty());
    CHECK(changes == 2);
}

TEST_CASE("a turned-on built-in native extension is loaded by the extension host and its node can be called "
          "(ADR-0097)")
{
    Scratch scratch;
    const fs::path builtin = scratch / "builtin";
    const fs::path folder = builtin / "gamescrcpy.pc-clock/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kSampleFolder / "manifest.json", folder / "manifest.json");
    fs::copy_file(kSampleFolder / "pc-clock.dll", folder / "pc-clock.dll");
    app::AppSettings settings(scratch / "data");
    app::ExtensionHost host(settings, {.user = scratch / "user", .builtin = builtin, .hostProgram = kProgram});
    host.start(false);
    REQUIRE(host.native() != nullptr);
    REQUIRE(host.extensions().size() == 1);
    INFO(listed(host.extensions()[0].diagnostics));
    REQUIRE(host.extensions()[0].usable());
    CHECK(host.native()->nodes().empty());

    REQUIRE(host.setEnabled("gamescrcpy.pc-clock", true));
    const std::vector<script::NodeInfo> nodes = host.native()->nodes();
    REQUIRE(nodes.size() == 1);
    CHECK(nodes[0].extension == "gamescrcpy.pc-clock");
    CHECK(nodes[0].name == "now");
    CHECK(nodes[0].returns == script::NodeType::String);
    CHECK(script::nodePath(nodes[0].extension, nodes[0].name) == "ext.gamescrcpy.pcClock.now");
    const script::NodeAnswer answer = host.native()->call("gamescrcpy.pc-clock", "now", {});
    CHECK(answer.code.empty());
    CHECK(std::regex_match(answer.value.text, std::regex(R"(\d\d:\d\d:\d\d)")));
    CHECK(host.problems().empty());

    const QVariantList entries = host.entries();
    REQUIRE(entries.size() == 1);
    const QVariantMap native = entries[0].toMap().value(QStringLiteral("native")).toMap();
    CHECK(native.value(QStringLiteral("nodes")).toStringList() ==
          QStringList{QStringLiteral("ext.gamescrcpy.pcClock.now")});
    CHECK(native.value(QStringLiteral("touchMethods")).toStringList().isEmpty());
    CHECK_FALSE(native.value(QStringLiteral("ownProcess")).toBool());

    REQUIRE(host.setEnabled("gamescrcpy.pc-clock", false));
    CHECK(host.native()->nodes().empty());
    CHECK(host.native()->call("gamescrcpy.pc-clock", "now", {}).code == "GS-API-060");
}

namespace {

exthost::StepInput outputStep(exthost::CommandKind kind, double x, double y, std::uint64_t at)
{
    return {.call = exthost::StepCall::Output,
            .event = {},
            .command = {.kind = kind, .finger = 0, .x = x, .y = y, .at = at},
            .now = at,
            .wakeAt = 0};
}

exthost::StepInput inputStep(exthost::InputKind kind, std::uint32_t slot, double x, double y, std::uint64_t at)
{
    return {.call = exthost::StepCall::Input,
            .event = {.kind = kind, .slot = slot, .x = x, .y = y, .at = at},
            .command = {},
            .now = at,
            .wakeAt = 0};
}

exthost::StepInput timeStep(exthost::StepCall call, std::uint64_t now)
{
    return {.call = call, .event = {}, .command = {}, .now = now, .wakeAt = 0};
}

bool sameOutput(const exthost::StepOutput& a, const exthost::StepOutput& b)
{
    return a.status == b.status && a.commands == b.commands && a.events == b.events && a.wakeAt == b.wakeAt;
}

std::vector<exthost::StepOutput> shiftRun(exthost::Module& module, std::uint64_t seed)
{
    const std::uint64_t id = module.openInstance(
        exthost::InstanceKind::Attachment, 0, R"({"dx":0.1,"sleepMs":0,"@position":[0.5,0.5],"@unit":[0.1,0.2]})",
        seed);
    REQUIRE(id != 0);
    std::vector<exthost::StepOutput> outputs;
    outputs.push_back(module.step(id, outputStep(exthost::CommandKind::Down, 0.4, 0.5, 1000)));
    outputs.push_back(module.step(id, inputStep(exthost::InputKind::Axis2, 0, 0.25, -0.5, 2000)));
    outputs.push_back(module.step(id, inputStep(exthost::InputKind::KeyDown, 1, 0.0, 0.0, 3000)));
    outputs.push_back(module.step(id, timeStep(exthost::StepCall::Wake, 5000)));
    outputs.push_back(module.step(id, outputStep(exthost::CommandKind::Up, 0.4, 0.5, 6000)));
    module.closeInstance(id);
    return outputs;
}

ext::DeclaredParam declaredNumber(std::string key, std::string title, ext::DeclaredParamType type, double low, double high)
{
    ext::DeclaredParam param;
    param.key = std::move(key);
    param.type = type;
    param.title = std::move(title);
    param.min = low;
    param.max = high;
    return param;
}

app::NativeWanted feelWanted(const fs::path& folder, ext::NativeSlotKind tapSlot)
{
    ext::NativeAttachment shift;
    shift.name = "shift";
    shift.title = "Shift";
    shift.appliesTo = {"tap", "hold"};
    shift.params = {declaredNumber("dx", "Shift", ext::DeclaredParamType::Number, -1.0, 1.0),
                    declaredNumber("sleepMs", "Sleep", ext::DeclaredParamType::Integer, 0.0, 1000.0)};
    shift.params[0].defaultValue = "0.1";
    ext::NativeAttachment missing;
    missing.name = "missing";
    missing.title = "Missing";
    ext::NativeBehavior tap;
    tap.name = "tap";
    tap.title = "Tap";
    tap.slotList = {{"press", tapSlot, "Press"}};
    ext::NativeBehavior spin;
    spin.name = "spin";
    spin.title = "Spin";
    app::NativeWanted wanted;
    wanted.id = "gstest.feel";
    wanted.version = "1.0.0";
    wanted.name = QStringLiteral("Feel test");
    wanted.directory = folder;
    wanted.official = true;
    wanted.native.dll = "native.dll";
    wanted.native.attachments = {shift, missing};
    wanted.native.behaviors = {tap, spin};
    wanted.attachmentTexts = {app::NativeTexts{.title = QStringLiteral("Shift it"),
                                               .paramTitles = {QStringLiteral("Distance")}}};
    return wanted;
}

}

TEST_CASE("a hosted DLL that is not ready yet still starts, and says when it gets ready (issue 20)")
{
    Scratch scratch;
    std::atomic<int> readies{0};
    exthost::HostedOptions options = hostedOptions(scratch);
    options.readyWait = 0ms;
    options.ready = [&readies] { ++readies; };
    std::string problem;
    const std::unique_ptr<exthost::HostedModule> module = exthost::HostedModule::start(std::move(options), problem);
    REQUIRE_MESSAGE(module != nullptr, problem);
    const bool waiting = !module->ready();
    REQUIRE(module->waitReady(20s));
    CHECK(module->ready());
    if (waiting) {
        REQUIRE(eventually([&readies] { return readies.load() == 1; }));
    }
    CHECK(readies.load() <= 1);
    CHECK(callNode(*module, "add", {numberValue(1), numberValue(2)}).value.number == 3.0);
}

TEST_CASE("the registry matches a hosted DLL that gets ready late, and reports one that never does (issue 20)")
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path folder = scratch / "gstest.native/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    app::NativeExtensions registry(
        {.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr, .readyWait = 0ms});
    const auto settled = [](const std::function<bool()>& done) {
        const auto deadline = std::chrono::steady_clock::now() + 20s;
        while (!done()) {
            if (std::chrono::steady_clock::now() > deadline) {
                return false;
            }
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(10ms);
        }
        return true;
    };
    app::NativeWanted wanted = testWanted(folder);
    wanted.official = false;
    registry.apply({wanted});
    REQUIRE(settled([&registry] { return !registry.nodes().empty(); }));
    std::vector<std::string> names;
    for (const script::NodeInfo& node : registry.nodes()) {
        names.push_back(node.name);
    }
    CHECK(names == std::vector<std::string>{"add", "allowed", "touches", "breakTouch"});
    CHECK(registry.loaded("gstest.native"));

    const fs::path empty = scratch / "gstest.empty/1.0.0";
    fs::create_directories(empty);
    app::NativeWanted missing = testWanted(empty);
    missing.id = "gstest.empty";
    missing.official = false;
    registry.apply({wanted, missing});
    REQUIRE(settled([&registry] {
        return reports(registry.problems(), ext::Code::NativeLoadFailed, R"($["gstest.empty"].contributes.native.dll)");
    }));
    CHECK_FALSE(registry.loaded("gstest.empty"));
    CHECK(registry.loaded("gstest.native"));
}

TEST_CASE("an attachment and a behavior of a DLL give the same output in GameScrcpy and in gs-exthost.exe, and the "
          "same seed gives the same output again (ADR-0105)")
{
    Scratch scratch;
    std::string problem;
    const std::unique_ptr<exthost::InProcessModule> inside = exthost::InProcessModule::load(
        {.dll = kTestDll, .store = scratch / "inside.json", .granted = {}, .services = nullptr}, problem);
    REQUIRE_MESSAGE(inside != nullptr, problem);
    exthost::HostedOptions options = hostedOptions(scratch);
    options.stepLimit = 500ms;
    const std::unique_ptr<exthost::HostedModule> hosted = exthost::HostedModule::start(std::move(options), problem);
    REQUIRE_MESSAGE(hosted != nullptr, problem);
    for (exthost::Module* module :
         {static_cast<exthost::Module*>(inside.get()), static_cast<exthost::Module*>(hosted.get())}) {
        CAPTURE(module == inside.get());
        CHECK(module->info().attachments == std::vector<exthost::AttachmentInfo>{{"shift"}});
        REQUIRE(module->info().behaviors.size() == 1);
        const exthost::BehaviorInfo& tap = module->info().behaviors[0];
        CHECK(tap.name == "tap");
        CHECK(tap.slotKinds == std::vector<exthost::SlotKind>{exthost::SlotKind::Key});
        CHECK(tap.fingers == 1);
        CHECK_FALSE(tap.frames);
    }

    const std::vector<exthost::StepOutput> local = shiftRun(*inside, 7);
    REQUIRE(local.size() == 5);
    for (const exthost::StepOutput& output : local) {
        CHECK(output.status == exthost::StepStatus::Ok);
    }
    REQUIRE(local[0].commands.size() == 2);
    CHECK(local[0].commands[0].kind == exthost::CommandKind::Down);
    CHECK(local[0].commands[0].x == doctest::Approx(0.5));
    CHECK(local[0].commands[0].y == doctest::Approx(0.5007));
    CHECK(local[0].commands[1].kind == exthost::CommandKind::Move);
    CHECK(local[0].commands[1].x == doctest::Approx(0.6));
    CHECK(local[0].commands[1].at == 6000);
    CHECK(local[0].events.empty());
    REQUIRE(local[1].events.size() == 1);
    CHECK(local[1].events[0].kind == exthost::InputKind::Axis2);
    CHECK(local[1].events[0].x == doctest::Approx(0.5));
    CHECK(local[1].events[0].y == doctest::Approx(-0.5));
    CHECK(local[1].commands.empty());
    CHECK(local[2].events.empty());
    CHECK(local[2].wakeAt == 5000);
    REQUIRE(local[3].events.size() == 1);
    CHECK(local[3].events[0].kind == exthost::InputKind::KeyDown);
    CHECK(local[3].events[0].slot == 1);
    CHECK(local[3].events[0].at == 5000);
    CHECK(local[3].wakeAt == 0);
    REQUIRE(local[4].commands.size() == 1);
    CHECK(local[4].commands[0].kind == exthost::CommandKind::Up);

    const std::vector<exthost::StepOutput> remote = shiftRun(*hosted, 7);
    const std::vector<exthost::StepOutput> again = shiftRun(*inside, 7);
    REQUIRE(remote.size() == local.size());
    REQUIRE(again.size() == local.size());
    for (std::size_t index = 0; index < local.size(); ++index) {
        CAPTURE(index);
        CHECK(sameOutput(local[index], remote[index]));
        CHECK(sameOutput(local[index], again[index]));
    }
    CHECK(shiftRun(*inside, 8)[0].commands[0].y == doctest::Approx(0.5008));

    for (exthost::Module* module :
         {static_cast<exthost::Module*>(inside.get()), static_cast<exthost::Module*>(hosted.get())}) {
        CAPTURE(module == inside.get());
        const std::uint64_t id =
            module->openInstance(exthost::InstanceKind::Behavior, 0, R"({"@position":[0.3,0.6],"@unit":[0.1,0.2]})", 1);
        REQUIRE(id != 0);
        const exthost::StepOutput down = module->step(id, inputStep(exthost::InputKind::KeyDown, 0, 0.0, 0.0, 1000));
        REQUIRE(down.status == exthost::StepStatus::Ok);
        REQUIRE(down.commands.size() == 1);
        CHECK(down.commands[0] ==
              exthost::TouchCommand{.kind = exthost::CommandKind::Down, .finger = 0, .x = 0.3, .y = 0.6, .at = 1000});
        const exthost::StepOutput up = module->step(id, inputStep(exthost::InputKind::KeyUp, 0, 0.0, 0.0, 2000));
        REQUIRE(up.commands.size() == 1);
        CHECK(up.commands[0].kind == exthost::CommandKind::Up);
        module->closeInstance(id);
        const std::uint64_t failing = module->openInstance(exthost::InstanceKind::Behavior, 0, R"({"fail":1})", 1);
        REQUIRE(failing != 0);
        CHECK(module->step(failing, inputStep(exthost::InputKind::KeyDown, 0, 0.0, 0.0, 3000)).status ==
              exthost::StepStatus::Failed);
        module->closeInstance(failing);
        CHECK(module->openInstance(exthost::InstanceKind::Attachment, 5, "{}", 1) == 0);
    }
    CHECK(hosted->skippedSteps() == 0);
}

TEST_CASE("a step in gs-exthost.exe that takes longer than 2 ms is skipped and counted (ADR-0105)")
{
    Scratch scratch;
    std::string problem;
    const std::unique_ptr<exthost::HostedModule> module =
        exthost::HostedModule::start(hostedOptions(scratch), problem);
    REQUIRE_MESSAGE(module != nullptr, problem);
    CHECK(module->skippedSteps() == 0);
    const std::uint64_t slow =
        module->openInstance(exthost::InstanceKind::Attachment, 0, R"({"dx":0.1,"sleepMs":60})", 1);
    REQUIRE(slow != 0);
    const exthost::StepOutput skipped = module->step(slow, outputStep(exthost::CommandKind::Down, 0.4, 0.5, 1000));
    CHECK(skipped.status == exthost::StepStatus::Skipped);
    CHECK(skipped.commands.empty());
    CHECK(module->skippedSteps() == 1);
    CHECK(module->working());
    module->closeInstance(slow);
    const std::uint64_t quick =
        module->openInstance(exthost::InstanceKind::Attachment, 0, R"({"dx":0.1,"sleepMs":0})", 1);
    REQUIRE(quick != 0);
    CHECK(eventually([&] {
        return module->step(quick, outputStep(exthost::CommandKind::Move, 0.4, 0.5, 2000)).status ==
               exthost::StepStatus::Ok;
    }));
    CHECK(module->crashes() == 0);
}

TEST_CASE("add-ons turned on for every profile reach the module factory in the extension's order, only those the DLL "
          "has (ADR-0073 revision 2026-11-08)")
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path folder = scratch / "gstest.feel/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    ManualClock clock;
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = &clock});
    registry.apply({feelWanted(folder, ext::NativeSlotKind::Key)});
    REQUIRE(registry.moduleFactory() != nullptr);
    CHECK(registry.moduleFactory()->globals().empty());
    int changes = 0;
    QObject::connect(&registry, &app::NativeExtensions::globalsChanged, [&changes] { ++changes; });

    const app::NativeExtensions::GlobalChoices chosen{
        {"gstest.feel", {{"shift", R"({"dx":0.25})"}, {"missing", "{}"}}}, {"gone.ext", {{"x", "{}"}}}};
    registry.setGlobals(chosen);
    CHECK(changes == 1);
    const std::shared_ptr<app::ExtensionModules> factory = registry.moduleFactory();
    REQUIRE(factory->globals().size() == 1);
    CHECK(factory->globals()[0].module == "gstest.feel/shift");
    CHECK(factory->globals()[0].params == R"({"dx":0.25})");
    CHECK(factory->globals()[0].appliesTo == std::vector<std::string>{"tap", "hold"});

    registry.setGlobals(chosen);
    CHECK(changes == 1);
    CHECK(registry.moduleFactory() == factory);
    registry.setGlobals({});
    CHECK(changes == 2);
    CHECK(registry.moduleFactory()->globals().empty());
}

TEST_CASE("the registry lists the attachments and behaviors a DLL offers, names the ones it lacks, makes their "
          "instances through the module factory and turns the extension off after three failures in 10 seconds "
          "(ADR-0105)")
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path folder = scratch / "gstest.feel/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    ManualClock clock;
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = &clock});
    int changes = 0;
    QObject::connect(&registry, &app::NativeExtensions::changed, [&changes] { ++changes; });
    registry.apply({feelWanted(folder, ext::NativeSlotKind::Key)});
    CHECK(changes == 1);
    const std::vector<ext::Diagnostic> problems = registry.problems();
    INFO(listed(problems));
    REQUIRE(problems.size() == 2);
    CHECK(problems[0].code == ext::Code::NativeMismatch);
    CHECK(problems[0].path == R"($["gstest.feel"].contributes.native.attachments[1])");
    CHECK(problems[1].code == ext::Code::NativeMismatch);
    CHECK(problems[1].path == R"($["gstest.feel"].contributes.native.behaviors[1])");

    const QVariantList attachments = registry.attachmentList();
    REQUIRE(attachments.size() == 1);
    const QVariantMap shift = attachments[0].toMap();
    CHECK(shift.value(QStringLiteral("module")).toString() == QStringLiteral("gstest.feel/shift"));
    CHECK(shift.value(QStringLiteral("title")).toString() == QStringLiteral("Shift it"));
    CHECK(shift.value(QStringLiteral("extension")).toString() == QStringLiteral("Feel test"));
    CHECK(shift.value(QStringLiteral("appliesTo")).toStringList() ==
          QStringList{QStringLiteral("tap"), QStringLiteral("hold")});
    const QVariantList behaviors = registry.behaviorList();
    REQUIRE(behaviors.size() == 1);
    const QVariantMap tap = behaviors[0].toMap();
    CHECK(tap.value(QStringLiteral("module")).toString() == QStringLiteral("gstest.feel/tap"));
    CHECK(tap.value(QStringLiteral("fingers")).toInt() == 1);
    const QVariantList inputs = tap.value(QStringLiteral("inputs")).toList();
    REQUIRE(inputs.size() == 1);
    CHECK(inputs[0].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("press"));
    CHECK(inputs[0].toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("key"));
    CHECK(inputs[0].toMap().value(QStringLiteral("title")).toString() == QStringLiteral("Press"));
    const std::optional<app::DeclaredModule> declared = registry.declaredModule("gstest.feel/shift");
    REQUIRE(declared.has_value());
    CHECK(declared->texts.paramTitles == std::vector<QString>{QStringLiteral("Distance"), QStringLiteral("Sleep")});
    CHECK_FALSE(registry.declaredModule("gstest.feel/missing").has_value());

    const input::ModuleCatalog catalog = registry.moduleCatalog();
    REQUIRE(catalog.attachments.size() == 1);
    CHECK(catalog.attachments[0].module == "gstest.feel/shift");
    CHECK(catalog.attachments[0].appliesTo == std::vector<std::string>{"tap", "hold"});
    REQUIRE(catalog.behaviors.size() == 1);
    CHECK(catalog.behaviors[0].shape.slotList ==
          std::vector<input::BehaviorSlot>{{"press", input::BehaviorSlotKind::Key}});

    const std::shared_ptr<app::ExtensionModules> factory = registry.moduleFactory();
    REQUIRE(factory != nullptr);
    input::ModuleRequest request{.module = "gstest.feel/shift",
                                 .control = "fire",
                                 .type = "tap",
                                 .params = R"({"dx":0.25,"sleepMs":"x"})",
                                 .position = {0.5, 0.5},
                                 .unit = {0.1, 0.2},
                                 .seed = 3};
    std::unique_ptr<input::AttachmentInstance> attachment = factory->attachment(request);
    REQUIRE(attachment != nullptr);
    const input::FingerCommand press{
        .action = input::FingerAction::Down, .finger = 0, .position = {0.4, 0.5}, .at = Microseconds{1000}};
    input::ModuleOutput out;
    out.commands.push_back(press);
    CHECK(attachment->output(press, out) == input::ModuleStatus::Ok);
    REQUIRE(out.commands.size() == 2);
    CHECK(out.commands[0].position.x == doctest::Approx(0.65));
    CHECK(out.commands[0].position.y == doctest::Approx(0.5003));
    CHECK(out.commands[1].action == input::FingerAction::Move);
    CHECK(out.commands[1].at == Microseconds{6000});
    CHECK_FALSE(out.wakeAt.has_value());
    attachment.reset();

    request.type = "camera";
    CHECK(factory->attachment(request) == nullptr);
    request.module = "gstest.feel/missing";
    request.type = "tap";
    CHECK(factory->attachment(request) == nullptr);

    input::BehaviorShape shape;
    request.module = "gstest.feel/tap";
    request.params = "";
    request.position = {0.3, 0.6};
    std::unique_ptr<input::BehaviorInstance> behavior = factory->behavior(request, shape);
    REQUIRE(behavior != nullptr);
    CHECK(shape.slotList.size() == 1);
    CHECK(shape.fingers == 1);
    input::ModuleOutput tapped;
    CHECK(behavior->input({.kind = input::ModuleInput::KeyDown, .slot = 0, .x = 0.0, .y = 0.0, .at = Microseconds{2000}},
                          tapped) == input::ModuleStatus::Ok);
    REQUIRE(tapped.commands.size() == 1);
    CHECK(tapped.commands[0].position == input::Vec2{0.3, 0.6});
    behavior.reset();

    std::vector<std::pair<QString, int>> troubles;
    QStringList failed;
    QObject::connect(&registry, &app::NativeExtensions::moduleTrouble,
                     [&troubles](const QString& module, const QString&, int kind) { troubles.emplace_back(module, kind); });
    QObject::connect(&registry, &app::NativeExtensions::modulesFailed,
                     [&failed](const QString& id, const QString&) { failed.append(id); });
    const auto fail = [&](input::ModuleTroubleKind kind, std::string_view module) {
        factory->trouble({.kind = kind, .module = module, .control = "fire"});
        QCoreApplication::processEvents();
    };
    fail(input::ModuleTroubleKind::Failed, "gstest.feel/shift");
    clock.advance(4s);
    fail(input::ModuleTroubleKind::Failed, "gstest.feel/shift");
    fail(input::ModuleTroubleKind::Missing, "gstest.feel/shift");
    CHECK(troubles.size() == 2);
    clock.advance(7s);
    fail(input::ModuleTroubleKind::Stopped, "gstest.feel/tap");
    CHECK(failed.isEmpty());
    clock.advance(1s);
    fail(input::ModuleTroubleKind::Failed, "gstest.feel/shift");
    CHECK(failed == QStringList{QStringLiteral("gstest.feel")});
    REQUIRE(troubles.size() == 4);
    CHECK(troubles[2] == std::pair<QString, int>(QStringLiteral("gstest.feel/tap"),
                                                 static_cast<int>(input::ModuleTroubleKind::Stopped)));

    registry.apply({feelWanted(folder, ext::NativeSlotKind::Axis2)});
    CHECK(reports(registry.problems(), ext::Code::NativeMismatch, R"($["gstest.feel"].contributes.native.behaviors[0])"));
    CHECK(registry.behaviorList().isEmpty());
    CHECK(registry.moduleCatalog().behaviors.empty());
    CHECK(registry.moduleFactory() != factory);
}

TEST_CASE("the profile editor adds the add-ons that fit, edits their parameters, switches and order, and places "
          "extension components with their slots (ADR-0105)")
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path folder = scratch / "gstest.feel/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    registry.apply({feelWanted(folder, ext::NativeSlotKind::Key)});
    REQUIRE(app::NativeExtensions::instance() == &registry);

    input::LoadResult loaded = input::loadProfile(R"({"format":"gamescrcpy.profile","version":3,"controls":[
        {"id":"fire","type":"tap","inputs":{"press":"KeyF"}},{"id":"view","type":"camera"}]})");
    REQUIRE(loaded.ok());
    app::ProfileEditor editor(loaded.profile, {.width = 2400, .height = 1080});
    const QString fire = QStringLiteral("fire");
    const auto attachmentsNow = [&editor] { return editor.profile().controls.at(0).attachments; };

    const QVariantList offers = editor.attachable(fire);
    REQUIRE(offers.size() == 1);
    CHECK(offers[0].toMap().value(QStringLiteral("module")).toString() == QStringLiteral("gstest.feel/shift"));
    CHECK(editor.attachable(QStringLiteral("view")).isEmpty());
    CHECK_FALSE(editor.addAttachment(QStringLiteral("view"), QStringLiteral("gstest.feel/shift")));
    CHECK_FALSE(editor.addAttachment(fire, QStringLiteral("gstest.feel/missing")));
    REQUIRE(editor.addAttachment(fire, QStringLiteral("gstest.feel/shift")));
    REQUIRE(editor.addAttachment(fire, QStringLiteral("gstest.feel/shift")));
    CHECK(attachmentsNow().size() == 2);

    const QVariantList attached = editor.attachmentsOf(fire);
    REQUIRE(attached.size() == 2);
    const QVariantMap first = attached[0].toMap();
    CHECK(first.value(QStringLiteral("title")).toString() == QStringLiteral("Shift it"));
    CHECK(first.value(QStringLiteral("available")).toBool());
    CHECK(first.value(QStringLiteral("fits")).toBool());
    const QVariantList params = first.value(QStringLiteral("params")).toList();
    REQUIRE(params.size() == 2);
    const QVariantMap dx = params[0].toMap();
    CHECK(dx.value(QStringLiteral("key")).toString() == QStringLiteral("dx"));
    CHECK(dx.value(QStringLiteral("type")).toString() == QStringLiteral("number"));
    CHECK(dx.value(QStringLiteral("title")).toString() == QStringLiteral("Distance"));
    CHECK(dx.value(QStringLiteral("json")).toString() == QStringLiteral("0.1"));
    CHECK(dx.value(QStringLiteral("bounded")).toBool());
    CHECK(dx.value(QStringLiteral("minimum")).toDouble() == -1.0);
    CHECK(params[1].toMap().value(QStringLiteral("type")).toString() == QStringLiteral("integer"));
    CHECK(params[1].toMap().value(QStringLiteral("decimals")).toInt() == 0);

    CHECK(editor.setModuleParam(fire, 0, QStringLiteral("dx"), QStringLiteral("0.4")));
    CHECK(attachmentsNow()[0].params == R"({"dx":0.4})");
    CHECK_FALSE(editor.setModuleParam(fire, 0, QStringLiteral("dx"), QStringLiteral("3")));
    CHECK_FALSE(editor.setModuleParam(fire, 0, QStringLiteral("nope"), QStringLiteral("1")));
    CHECK_FALSE(editor.setModuleParam(fire, 5, QStringLiteral("dx"), QStringLiteral("0.1")));
    const std::string deep = std::string(30000, '[') + std::string(30000, ']');
    CHECK_FALSE(editor.setModuleParam(fire, 0, QStringLiteral("dx"), QString::fromStdString(deep)));
    CHECK(editor.attachmentsOf(fire)[0].toMap().value(QStringLiteral("params")).toList()[0].toMap().value(
              QStringLiteral("json")) == QStringLiteral("0.4"));

    CHECK(editor.enableAttachment(fire, 1, false));
    CHECK_FALSE(attachmentsNow()[1].enabled);
    CHECK(editor.moveAttachment(fire, 1, -1));
    CHECK_FALSE(attachmentsNow()[0].enabled);
    CHECK(attachmentsNow()[1].params == R"({"dx":0.4})");
    CHECK_FALSE(editor.moveAttachment(fire, 0, -1));
    CHECK(editor.removeAttachment(fire, 0));
    REQUIRE(attachmentsNow().size() == 1);
    CHECK(attachmentsNow()[0].enabled);
    editor.undo();
    CHECK(attachmentsNow().size() == 2);
    editor.redo();
    CHECK(attachmentsNow().size() == 1);

    const QVariantMap added = editor.addComponent(QStringLiteral("behavior:gstest.feel/tap"), 0.25, 0.5);
    const QString tap = added.value(QStringLiteral("id")).toString();
    REQUIRE_FALSE(tap.isEmpty());
    CHECK(added.value(QStringLiteral("slot")).toString() == QStringLiteral("inputs.press"));
    CHECK(editor.bindInput(tap, QStringLiteral("inputs.press"), QStringLiteral("KeyQ")));
    const auto* behavior = std::get_if<input::BehaviorControl>(&editor.profile().controls.back().kind);
    REQUIRE(behavior != nullptr);
    CHECK(behavior->behavior == "gstest.feel/tap");
    REQUIRE(behavior->inputs.size() == 1);
    CHECK(behavior->inputs[0].name == "press");
    REQUIRE(behavior->inputs[0].binding.has_value());
    CHECK(input::chordName(*behavior->inputs[0].binding->input) == "KeyQ");
    CHECK(editor.profile().controls.back().name == "Tap");
    CHECK(editor.keyList().last().toMap().value(QStringLiteral("behavior")).toString() ==
          QStringLiteral("gstest.feel/tap"));

    const QVariantList groups = editor.form(tap);
    REQUIRE_FALSE(groups.isEmpty());
    const QVariantList fields = groups[0].toMap().value(QStringLiteral("fields")).toList();
    REQUIRE(fields.size() == 1);
    CHECK(fields[0].toMap().value(QStringLiteral("key")).toString() == QStringLiteral("slot:press"));
    CHECK(fields[0].toMap().value(QStringLiteral("widget")).toString() == QStringLiteral("key"));
    CHECK(fields[0].toMap().value(QStringLiteral("title")).toString() == QStringLiteral("Press"));
    CHECK(fields[0].toMap().value(QStringLiteral("bindPath")).toString() == QStringLiteral("inputs.press"));
    CHECK(editor.behaviorParams(tap).isEmpty());
    CHECK(editor.setField(tap, QStringLiteral("slot:press"), QString()));
    CHECK(std::get<input::BehaviorControl>(editor.profile().controls.back().kind).inputs.empty());
    CHECK_FALSE(editor.addComponent(QStringLiteral("behavior:gstest.feel/missing"), 0.5, 0.5)
                    .contains(QStringLiteral("id")));
}

TEST_CASE("module parameters take the profile's valid values, the declared defaults for the rest and the "
          "component's place (ADR-0105)")
{
    ext::DeclaredParam size = declaredNumber("size", "Size", ext::DeclaredParamType::Number, 0.0, 0.5);
    size.defaultValue = "0.2";
    ext::DeclaredParam mode;
    mode.key = "mode";
    mode.type = ext::DeclaredParamType::Choice;
    mode.title = "Mode";
    mode.options = {"soft", "hard"};
    ext::DeclaredParam spot;
    spot.key = "spot";
    spot.type = ext::DeclaredParamType::Point;
    spot.title = "Spot";
    const nlohmann::json made = nlohmann::json::parse(app::moduleParams(
        {size, mode, spot}, R"({"size":0.9,"mode":"hard","spot":[1,-1],"other":true})", {0.25, 0.75}, {0.1, 0.2}));
    CHECK(made == nlohmann::json{{"size", 0.2},
                                 {"mode", "hard"},
                                 {"spot", {1, -1}},
                                 {"@position", {0.25, 0.75}},
                                 {"@unit", {0.1, 0.2}}});
    CHECK(nlohmann::json::parse(app::moduleParams({size, mode}, "", {}, {}))["mode"] == "soft");
    CHECK(nlohmann::json::parse(app::moduleParams({size}, "not json", {}, {}))["size"] == 0.2);
    const auto deepParams = [](std::size_t levels) {
        std::string text;
        for (std::size_t level = 0; level < levels; ++level) {
            text += R"({"size":)";
        }
        text += '1';
        return text + std::string(levels, '}');
    };
    CHECK(nlohmann::json::parse(app::moduleParams({size}, deepParams(30000), {}, {}))["size"] == 0.2);
    CHECK(app::moduleFits({}, "camera"));
    CHECK(app::moduleFits({"*"}, "camera"));
    CHECK(app::moduleFits({"tap", "hold"}, "hold"));
    CHECK_FALSE(app::moduleFits({"tap"}, "camera"));
}

namespace {

std::string utf8Of(const fs::path& path)
{
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

exthost::Value stringValue(std::string text)
{
    return textValue(exthost::ValueType::String, std::move(text));
}

nlohmann::json jsonOf(const std::string& text)
{
    nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
    return parsed.is_discarded() ? nlohmann::json() : parsed;
}

nlohmann::json deviceRun(exthost::Module& module, const std::string& call, const std::string& first = {},
                         const std::string& second = {})
{
    const std::size_t before = jsonOf(callNode(module, "deviceResults").value.text).size();
    const exthost::CallResult started =
        callNode(module, "deviceStart", {stringValue(call), stringValue(first), stringValue(second)});
    REQUIRE_MESSAGE(started.ok(), started.problem);
    nlohmann::json all;
    REQUIRE(eventually([&] {
        all = jsonOf(callNode(module, "deviceResults").value.text);
        return all.is_array() && all.size() > before;
    }));
    return all.back();
}

nlohmann::json deviceHeard(exthost::Module& module, std::size_t count)
{
    nlohmann::json heard = nlohmann::json::array();
    REQUIRE(eventually([&] {
        const nlohmann::json more = jsonOf(callNode(module, "deviceEvents").value.text);
        for (const nlohmann::json& event : more.is_array() ? more : nlohmann::json::array()) {
            heard.push_back(event);
        }
        return heard.size() >= count;
    }));
    return heard;
}

struct DeviceCalls {
    std::mutex mutex;
    std::vector<std::pair<std::string, nlohmann::json>> asked;
    std::vector<std::uint64_t> closed;

    exthost::ServiceResult answer(std::string_view request, std::string_view json)
    {
        const nlohmann::json body = nlohmann::json::parse(json, nullptr, false);
        {
            const std::scoped_lock lock(mutex);
            if (request == "hardware.close") {
                closed.push_back(body.is_object() && body.contains("device") ? body.at("device").get<std::uint64_t>()
                                                                             : 0);
                return {exthost::ServiceStatus::Ok, "null"};
            }
            asked.emplace_back(std::string(request), body);
        }
        if (request == "hardware.list") {
            return {exthost::ServiceStatus::Ok, R"([{"id":"serial:board","kind":"serial","name":"Board","present":true}])"};
        }
        if (request == "hardware.open") {
            return {exthost::ServiceStatus::Ok, R"({"device":7,"id":"serial:board","kind":"serial","name":"Board"})"};
        }
        if (request == "hardware.control") {
            return {exthost::ServiceStatus::Denied, "GS-API-081 the device has not been allowed"};
        }
        return {exthost::ServiceStatus::Ok, "null"};
    }

    std::vector<std::pair<std::string, nlohmann::json>> seen()
    {
        const std::scoped_lock lock(mutex);
        return asked;
    }

    std::vector<std::uint64_t> shut()
    {
        const std::scoped_lock lock(mutex);
        return closed;
    }
};

class DeviceServices final : public exthost::HostServices
{
public:
    explicit DeviceServices(DeviceCalls& calls)
        : m_calls(calls)
    {
    }

    void log(int, std::string_view) override {}
    bool notify(std::string_view, std::string_view) override { return false; }

    exthost::ServiceResult device(std::string_view request, std::string_view json) override
    {
        return m_calls.answer(request, json);
    }

    void closeDevice(std::uint64_t device) override
    {
        static_cast<void>(m_calls.answer("hardware.close", "{\"device\":" + std::to_string(device) + "}"));
    }

private:
    DeviceCalls& m_calls;
};

}

TEST_CASE("ADR-0112: a DLL's device functions reach GameScrcpy as requests with base64 bytes, need the permission "
          "hardware, and what its devices send reaches device_event, in GameScrcpy and in gs-exthost.exe")
{
    Scratch scratch;
    std::string problem;
    DeviceCalls insideCalls;
    DeviceServices inside(insideCalls);
    const std::unique_ptr<exthost::InProcessModule> loaded = exthost::InProcessModule::load(
        {.dll = kTestDll, .store = scratch / "inside.json", .granted = {"hardware"}, .services = &inside}, problem);
    REQUIRE_MESSAGE(loaded != nullptr, problem);
    DeviceCalls outsideCalls;
    exthost::HostedOptions options = hostedOptions(scratch);
    options.granted = {"hardware"};
    options.device = [&outsideCalls](const std::string& request, const std::string& json,
                                     std::function<void(exthost::ServiceResult)> answer) {
        answer(outsideCalls.answer(request, json));
    };
    const std::unique_ptr<exthost::HostedModule> hosted = exthost::HostedModule::start(std::move(options), problem);
    REQUIRE_MESSAGE(hosted != nullptr, problem);
    REQUIRE(hosted->waitReady(30s));
    const std::array<std::pair<exthost::Module*, DeviceCalls*>, 2> modules{
        {{loaded.get(), &insideCalls}, {hosted.get(), &outsideCalls}}};
    for (const auto& [module, calls] : modules) {
        CAPTURE(module == loaded.get());
        CHECK(callNode(*module, "supports", {stringValue("devices")}).value.flag);
        const nlohmann::json listed = deviceRun(*module, "list");
        CHECK(listed.at("status") == static_cast<int>(GS_EXT_OK));
        CHECK(listed.at("type") == static_cast<int>(GS_EXT_TYPE_JSON));
        CHECK(jsonOf(listed.at("answer").get<std::string>()).at(0).at("id") == "serial:board");
        const nlohmann::json opened = deviceRun(*module, "open", "serial:board", R"({"baudRate":9600})");
        CHECK(opened.at("status") == static_cast<int>(GS_EXT_OK));
        CHECK(opened.at("device") == 7);
        CHECK(jsonOf(opened.at("answer").get<std::string>()).at("name") == "Board");
        CHECK(deviceRun(*module, "write", "0102ff").at("status") == static_cast<int>(GS_EXT_OK));
        const nlohmann::json refused = deviceRun(*module, "control", R"({"op":"signals","dtr":true})");
        CHECK(refused.at("status") == static_cast<int>(GS_EXT_DENIED));
        CHECK(refused.at("type") == static_cast<int>(GS_EXT_TYPE_STRING));
        CHECK(refused.at("answer").get<std::string>().starts_with("GS-API-081"));
        CHECK(deviceRun(*module, "control", "not json").at("status") == static_cast<int>(GS_EXT_BAD_ARGUMENT));
        CHECK(deviceRun(*module, "write", "00", "[1]").at("status") == static_cast<int>(GS_EXT_BAD_ARGUMENT));
        CHECK(deviceRun(*module, "open", "", "").at("status") == static_cast<int>(GS_EXT_BAD_ARGUMENT));
        CHECK(deviceRun(*module, "close").at("status") == static_cast<int>(GS_EXT_OK));
        REQUIRE(eventually([&] { return calls->shut() == std::vector<std::uint64_t>{7}; }));
        const std::vector<std::pair<std::string, nlohmann::json>> seen = calls->seen();
        REQUIRE(seen.size() == 4);
        CHECK(seen[0].first == "hardware.list");
        CHECK(seen[1].first == "hardware.open");
        CHECK(seen[1].second == nlohmann::json{{"id", "serial:board"}, {"options", {{"baudRate", 9600}}}});
        CHECK(seen[2].first == "hardware.write");
        CHECK(seen[2].second == nlohmann::json{{"device", 7}, {"data", "AQL/"}});
        CHECK(seen[3].first == "hardware.control");
        CHECK(seen[3].second == nlohmann::json{{"op", "signals"}, {"dtr", true}, {"device", 7}});

        module->deviceEvent(7, R"({"type":"data","data":"AQL/"})");
        module->deviceEvent(7, R"({"type":"data","data":"not base64!"})");
        module->deviceEvent(7, R"({"type":"report","report":4,"data":"BQ=="})");
        module->deviceEvent(7, R"({"type":"notify","service":"0000180d-0000-1000-8000-00805f9b34fb",)"
                               R"("characteristic":"00002a37-0000-1000-8000-00805f9b34fb","data":"AA=="})");
        module->deviceEvent(7, R"({"type":"closed","reason":"removed","message":"GS-API-082 Board is not connected"})");
        const nlohmann::json heard = deviceHeard(*module, 4);
        REQUIRE(heard.size() == 4);
        CHECK(heard[0].at("type") == static_cast<int>(GS_EXT_DEVICE_DATA));
        CHECK(heard[0].at("device") == 7);
        CHECK(heard[0].at("data") == "0102ff");
        CHECK(heard[1].at("type") == static_cast<int>(GS_EXT_DEVICE_REPORT));
        CHECK(heard[1].at("report") == 4);
        CHECK(heard[1].at("data") == "05");
        CHECK(heard[2].at("type") == static_cast<int>(GS_EXT_DEVICE_NOTIFY));
        CHECK(heard[2].at("service") == "0000180d-0000-1000-8000-00805f9b34fb");
        CHECK(heard[2].at("characteristic") == "00002a37-0000-1000-8000-00805f9b34fb");
        CHECK(heard[2].at("data") == "00");
        CHECK(heard[3].at("type") == static_cast<int>(GS_EXT_DEVICE_CLOSED));
        CHECK(heard[3].at("reason") == "removed");
        CHECK(heard[3].at("text") == "GS-API-082 Board is not connected");
        CHECK(heard[3].at("data") == "");
    }

    fs::create_directories(scratch / "bare");
    fs::copy_file(kTestDll, scratch / "bare/native.dll");
    DeviceCalls bareCalls;
    DeviceServices bare(bareCalls);
    const std::unique_ptr<exthost::InProcessModule> denied = exthost::InProcessModule::load(
        {.dll = scratch / "bare/native.dll", .store = scratch / "bare.json", .granted = {}, .services = &bare}, problem);
    REQUIRE_MESSAGE(denied != nullptr, problem);
    const nlohmann::json refusedList = deviceRun(*denied, "list");
    CHECK(refusedList.at("status") == static_cast<int>(GS_EXT_DENIED));
    CHECK(refusedList.at("answer").get<std::string>().starts_with("GS-API-080"));
    CHECK(deviceRun(*denied, "use", "7").at("status") == static_cast<int>(GS_EXT_OK));
    CHECK(deviceRun(*denied, "close").at("status") == static_cast<int>(GS_EXT_DENIED));
    CHECK(bareCalls.seen().empty());
    CHECK(bareCalls.shut().empty());
}

TEST_CASE("ADR-0112: device options, a write target and a control request nested more than 64 levels deep from a DLL "
          "are refused with GS_EXT_BAD_ARGUMENT, so that they cannot overflow the stack of GameScrcpy")
{
    Scratch scratch;
    std::string problem;
    DeviceCalls calls;
    DeviceServices services(calls);
    const std::unique_ptr<exthost::InProcessModule> module = exthost::InProcessModule::load(
        {.dll = kTestDll, .store = scratch / "inside.json", .granted = {"hardware"}, .services = &services}, problem);
    REQUIRE_MESSAGE(module != nullptr, problem);
    const auto nestedObject = [](std::size_t levels, const std::string& before = {}, const std::string& after = {}) {
        std::string text = before;
        for (std::size_t level = 0; level < levels; ++level) {
            text += R"({"a":)";
        }
        text += '1';
        text += std::string(levels, '}');
        return text + after;
    };
    CHECK(deviceRun(*module, "open", "serial:board", nestedObject(65)).at("status") ==
          static_cast<int>(GS_EXT_BAD_ARGUMENT));
    CHECK(deviceRun(*module, "open", "serial:board", nestedObject(30000)).at("status") ==
          static_cast<int>(GS_EXT_BAD_ARGUMENT));
    CHECK(deviceRun(*module, "use", "7").at("status") == static_cast<int>(GS_EXT_OK));
    CHECK(deviceRun(*module, "write", "00", nestedObject(30000)).at("status") ==
          static_cast<int>(GS_EXT_BAD_ARGUMENT));
    CHECK(deviceRun(*module, "control", nestedObject(30000, R"({"op":"signals","rest":)", "}")).at("status") ==
          static_cast<int>(GS_EXT_BAD_ARGUMENT));
    CHECK(calls.seen().empty());
}

TEST_CASE("ADR-0112: a restricted DLL runs in gs-exthost.exe inside an AppContainer of its own: it loads, its nodes "
          "work, its store goes through GameScrcpy, and it cannot open the user's files")
{
    Scratch scratch;
    const fs::path folder = scratch / "gstest.restricted/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    writeFile(scratch / "private/notes.txt", "not for the extension");
    exthost::HostedOptions options = hostedOptions(scratch);
    options.program = containedProgram(scratch);
    options.dll = folder / "native.dll";
    options.store = scratch / "store/gstest.restricted.json";
    options.container = "gstest.restricted";
    options.readable = folder;
    std::string problem;
    std::unique_ptr<exthost::HostedModule> hosted = exthost::HostedModule::start(std::move(options), problem);
    REQUIRE_MESSAGE(hosted != nullptr, problem);
    REQUIRE(hosted->waitReady(30s));

    const exthost::CallResult added = callNode(*hosted, "add", {numberValue(2), numberValue(3)});
    REQUIRE_MESSAGE(added.ok(), added.problem);
    CHECK(added.value.number == 5.0);
    CHECK(callNode(*hosted, "supports", {stringValue("devices")}).value.flag);

    const exthost::CallResult outside = callNode(*hosted, "openFile", {stringValue(utf8Of(scratch / "private/notes.txt"))});
    REQUIRE_MESSAGE(outside.ok(), outside.problem);
    CHECK(outside.value.number != 0.0);
    const exthost::CallResult own = callNode(*hosted, "openFile", {stringValue(utf8Of(folder / "native.dll"))});
    REQUIRE_MESSAGE(own.ok(), own.problem);
    CHECK(own.value.number == 0.0);

    const exthost::CallResult kept = callNode(
        *hosted, "remember", {stringValue("board"), textValue(exthost::ValueType::Json, R"({"port":"COM9"})")});
    REQUIRE_MESSAGE(kept.ok(), kept.problem);
    CHECK(kept.value.flag);
    const exthost::CallResult recalled = callNode(*hosted, "recall", {stringValue("board")});
    REQUIRE_MESSAGE(recalled.ok(), recalled.problem);
    CHECK(jsonOf(recalled.value.text) == nlohmann::json{{"port", "COM9"}});
    CHECK(jsonOf(readFile(scratch / "store/gstest.restricted.json")).at("board").at("port") == "COM9");
    CHECK(callNode(*hosted, "recall", {stringValue("missing")}).value.text == "null");

    exthost::HostedOptions trusted = hostedOptions(scratch);
    const std::unique_ptr<exthost::HostedModule> full = exthost::HostedModule::start(std::move(trusted), problem);
    REQUIRE_MESSAGE(full != nullptr, problem);
    REQUIRE(full->waitReady(30s));
    CHECK(callNode(*full, "openFile", {stringValue(utf8Of(scratch / "private/notes.txt"))}).value.number == 0.0);

    hosted.reset();
    CHECK(exthost::forgetContainer("gstest.restricted"));
    static_cast<void>(exthost::forgetContainer("gstest.restricted"));
    static_cast<void>(exthost::forgetContainer("gstest.never-ran"));
}

TEST_CASE("ADR-0112: a restricted DLL gets an AppContainer named after a SHA-256 of its id and a bare environment, "
          "and GameScrcpy paces its logs, notices and device changes, while a DLL with full rights is not paced")
{
    Scratch scratch;
    const fs::path folder = scratch / "gstest.paced/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    REQUIRE(SetEnvironmentVariableW(L"GS_TEST_SECRET", L"leak"));
    struct Heard {
        std::mutex mutex;
        std::vector<std::string> lines;
        std::vector<std::string> others;
        int notices = 0;
        int changes = 0;
    };
    const auto listen = [](const std::shared_ptr<Heard>& heard, exthost::HostedOptions& options) {
        options.log = [heard](int, const std::string& text) {
            const std::scoped_lock lock(heard->mutex);
            (text.starts_with("line ") ? heard->lines : heard->others).push_back(text);
        };
        options.notify = [heard](const std::string&, const std::string&) {
            const std::scoped_lock lock(heard->mutex);
            ++heard->notices;
        };
        options.devicesChanged = [heard](const std::string&) {
            const std::scoped_lock lock(heard->mutex);
            ++heard->changes;
        };
    };
    const auto flood = [](exthost::HostedModule& module) {
        REQUIRE(callNode(module, "logMany", {numberValue(1000)}).ok());
        for (int count = 0; count < 20; ++count) {
            REQUIRE(callNode(module, "notify", {stringValue("Title"), stringValue("Text")}).ok());
            REQUIRE(callNode(module, "setBoxes", {textValue(exthost::ValueType::Json, "[]")}).ok());
        }
        REQUIRE(callNode(module, "add", {numberValue(1), numberValue(2)}).ok());
    };

    const auto boxed = std::make_shared<Heard>();
    exthost::HostedOptions options = hostedOptions(scratch);
    options.program = containedProgram(scratch);
    options.dll = folder / "native.dll";
    options.store = scratch / "store/gstest.paced.json";
    options.container = "gstest.paced";
    options.readable = folder;
    listen(boxed, options);
    std::string problem;
    std::unique_ptr<exthost::HostedModule> hosted = exthost::HostedModule::start(std::move(options), problem);
    REQUIRE_MESSAGE(hosted != nullptr, problem);
    REQUIRE(hosted->waitReady(30s));

    const std::string_view id = "gstest.paced";
    const gs::platform::Sha256Digest digest =
        gs::platform::sha256(std::span(reinterpret_cast<const std::uint8_t*>(id.data()), id.size()));
    std::string hex = gs::platform::toHex(std::span(digest).first(16));
    std::ranges::transform(hex, hex.begin(),
                           [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
    const std::wstring name = L"GameScrcpy.Extension." + std::wstring(hex.begin(), hex.end());
    PSID sid = nullptr;
    const HRESULT made = CreateAppContainerProfile(name.c_str(), name.c_str(), name.c_str(), nullptr, 0, &sid);
    if (SUCCEEDED(made)) {
        FreeSid(sid);
        static_cast<void>(DeleteAppContainerProfile(name.c_str()));
    }
    CHECK(made == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));

    CHECK(callNode(*hosted, "environment", {stringValue("GS_TEST_SECRET")}).value.text.empty());
    CHECK_FALSE(callNode(*hosted, "environment", {stringValue("SystemRoot")}).value.text.empty());
    const auto contained = [&hosted](const char* variable) {
        return callNode(*hosted, "environment", {stringValue(variable)}).value.text;
    };
    const std::string local = contained("LOCALAPPDATA");
    CHECK(local.find("\\Packages\\") != std::string::npos);
    CHECK(contained("TEMP").starts_with(local + "\\"));
    CHECK(contained("TMP").starts_with(local + "\\"));
    CHECK(contained("USERPROFILE").empty());
    CHECK(contained("APPDATA").empty());
    CHECK(contained("USERNAME").empty());

    flood(*hosted);
    {
        const std::scoped_lock lock(boxed->mutex);
        CHECK(boxed->lines.size() >= 100);
        CHECK(boxed->lines.size() <= 400);
        CHECK(std::ranges::count_if(boxed->others, [](const std::string& line) {
                  return line.find("dropped") != std::string::npos;
              }) >= 3);
        CHECK(boxed->notices >= 1);
        CHECK(boxed->notices <= 6);
        CHECK(boxed->changes >= 1);
        CHECK(boxed->changes <= 8);
    }

    const auto trusted = std::make_shared<Heard>();
    exthost::HostedOptions full = hostedOptions(scratch);
    listen(trusted, full);
    const std::unique_ptr<exthost::HostedModule> fullRights = exthost::HostedModule::start(std::move(full), problem);
    REQUIRE_MESSAGE(fullRights != nullptr, problem);
    REQUIRE(fullRights->waitReady(30s));
    CHECK(callNode(*fullRights, "environment", {stringValue("GS_TEST_SECRET")}).value.text == "leak");
    flood(*fullRights);
    {
        const std::scoped_lock lock(trusted->mutex);
        CHECK(trusted->lines.size() == 1000);
        CHECK(trusted->notices == 20);
        CHECK(trusted->changes == 20);
    }

    hosted.reset();
    CHECK(exthost::forgetContainer("gstest.paced"));
    SetEnvironmentVariableW(L"GS_TEST_SECRET", nullptr);
}

namespace {

bool pumpEvents(const std::function<bool()>& done)
{
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(5ms);
    }
    return true;
}

hardware::DeviceInfo fakeDevice(const char* id, hardware::Kind kind, const char* name, const char* path)
{
    hardware::DeviceInfo device;
    device.id = id;
    device.kind = kind;
    device.name = name;
    device.path = path;
    return device;
}

class DeviceDriver
{
public:
    DeviceDriver(const app::NativeExtensions& registry, std::string extension)
        : m_registry(registry)
        , m_extension(std::move(extension))
    {
    }

    nlohmann::json run(const std::string& call, const std::string& first = {}, const std::string& second = {})
    {
        const std::size_t before = results().size();
        const script::NodeAnswer started =
            m_registry.call(m_extension, "deviceStart", {text(call), text(first), text(second)});
        REQUIRE_MESSAGE(started.problem.empty(), started.problem);
        nlohmann::json all;
        REQUIRE(pumpEvents([&] {
            all = results();
            return all.size() > before;
        }));
        return all.back();
    }

    nlohmann::json now(const std::string& call)
    {
        return jsonOf(m_registry.call(m_extension, "deviceNow", {text(call), text({}), text({})}).value.text);
    }

    nlohmann::json heard(std::size_t count)
    {
        nlohmann::json events = nlohmann::json::array();
        REQUIRE(pumpEvents([&] {
            const nlohmann::json more = jsonOf(m_registry.call(m_extension, "deviceEvents", {}).value.text);
            for (const nlohmann::json& event : more.is_array() ? more : nlohmann::json::array()) {
                events.push_back(event);
            }
            return events.size() >= count;
        }));
        return events;
    }

    double openFile(const fs::path& path)
    {
        return m_registry.call(m_extension, "openFile", {text(utf8Of(path))}).value.number;
    }

private:
    static script::NodeValue text(const std::string& value)
    {
        return script::NodeValue{.type = script::NodeType::String, .text = value};
    }

    nlohmann::json results()
    {
        const nlohmann::json all = jsonOf(m_registry.call(m_extension, "deviceResults", {}).value.text);
        return all.is_array() ? all : nlohmann::json::array();
    }

    const app::NativeExtensions& m_registry;
    std::string m_extension;
};

}

TEST_CASE("ADR-0112: a DLL opens a device the user allowed through GameScrcpy, writes to it, controls it, hears it and "
          "closes it, under the extension's grants and claims, in GameScrcpy, in gs-exthost.exe and restricted")
{
    ApplicationScope application;
    app::HardwareClaims claims;
    Scratch scratch;
    const hardware::DeviceInfo padInfo = fakeDevice("hid:pad", hardware::Kind::Hid, "Pad", "\\\\?\\hid#pad");
    auto backend = std::make_unique<hardware::FakeBackend>();
    hardware::FakeBackend& fake = *backend;
    fake.add(fakeDevice("serial:board", hardware::Kind::Serial, "Board", "\\\\.\\COM7"));
    fake.add(padInfo);
    app::HardwareAccess access(std::move(backend), scratch / "hardware-grants.json");
    access.setReplies({.answer = [](std::uint64_t, quint64, const QString&) {},
                       .refuse = [](std::uint64_t, quint64, const QString&) {},
                       .tell = [](std::uint64_t, const QString&, const QString&, const QString&) {}});
    struct Declared {
        const char* id;
        const char* name;
        bool official;
        bool restricted;
    };
    const std::array<Declared, 3> declared{{{"gstest.inside", "Inside", true, false},
                                            {"gstest.outside", "Outside", false, false},
                                            {"gstest.boxed", "Boxed", true, true}}};
    QStringList permitted;
    for (const Declared& each : declared) {
        permitted.append(QString::fromUtf8(each.id));
    }
    access.setPermitted(permitted);
    std::vector<app::NativeWanted> wanted;
    for (const Declared& each : declared) {
        const app::LogicCaller asking{.logic = 1, .extension = QString::fromUtf8(each.id)};
        access.request(asking, "hardware.request", 1, QString::fromUtf8(R"({"kind":"serial"})"));
        REQUIRE(access.allow({QStringLiteral("serial:board")}));
        access.request(asking, "hardware.request", 2, QString::fromUtf8(R"({"kind":"hid"})"));
        REQUIRE(access.allow({QStringLiteral("hid:pad")}));
        const fs::path folder = scratch / (std::string(each.id) + "/1.0.0");
        fs::create_directories(folder);
        fs::copy_file(kTestDll, folder / "native.dll");
        app::NativeWanted made = testWanted(folder);
        made.id = each.id;
        made.name = QString::fromUtf8(each.name);
        made.official = each.official;
        made.granted = {"hardware"};
        made.native.sandbox = each.restricted ? ext::NativeSandbox::Restricted : ext::NativeSandbox::Full;
        const std::vector<ext::NativeParam> three{{"call", "string"}, {"first", "string"}, {"second", "string"}};
        made.native.nodes = {
            {.name = "deviceStart", .params = three, .returns = "number", .description = {}, .permissions = {}},
            {.name = "deviceResults", .params = {}, .returns = "json", .description = {}, .permissions = {}},
            {.name = "deviceEvents", .params = {}, .returns = "json", .description = {}, .permissions = {}},
            {.name = "deviceNow", .params = three, .returns = "json", .description = {}, .permissions = {}},
            {.name = "openFile", .params = {{"path", "string"}}, .returns = "number", .description = {}, .permissions = {}}};
        wanted.push_back(std::move(made));
    }
    app::NativeExtensions registry(
        {.program = containedProgram(scratch), .storeRoot = scratch / ".store", .clock = nullptr});
    registry.apply(wanted);
    writeFile(scratch / "secret.txt", "not for extensions");

    for (const Declared& each : declared) {
        CAPTURE(each.id);
        REQUIRE(pumpEvents([&] { return registry.call(each.id, "deviceResults", {}).problem.empty(); }));
        DeviceDriver driver(registry, each.id);
        const QString name = QString::fromUtf8(each.name);
        if (each.restricted) {
            CHECK(driver.openFile(scratch / "secret.txt") != 0.0);
        } else {
            CHECK(driver.openFile(scratch / "secret.txt") == 0.0);
        }

        const nlohmann::json listed = driver.run("list");
        REQUIRE(listed.at("status") == static_cast<int>(GS_EXT_OK));
        CHECK(jsonOf(listed.at("answer").get<std::string>()).size() == 2);

        const nlohmann::json opened = driver.run("open", "serial:board", R"({"baudRate":9600})");
        REQUIRE_MESSAGE(opened.at("status") == static_cast<int>(GS_EXT_OK), opened.dump());
        const std::uint64_t board = opened.at("device").get<std::uint64_t>();
        CHECK(board != 0);
        const nlohmann::json description = jsonOf(opened.at("answer").get<std::string>());
        CHECK(description.at("id") == "serial:board");
        CHECK(description.at("device") == board);
        REQUIRE(fake.link("serial:board") != nullptr);
        CHECK(fake.link("serial:board")->serial().baudRate == 9600);
        CHECK(claims.holderName("serial:COM7") == name);

        CHECK(driver.run("write", "0102ff").at("status") == static_cast<int>(GS_EXT_OK));
        REQUIRE_FALSE(fake.link("serial:board")->written().empty());
        CHECK(fake.link("serial:board")->written().back() == hardware::Bytes{1, 2, 255});
        CHECK(driver.run("control", R"({"op":"signals","dtr":false})").at("status") == static_cast<int>(GS_EXT_OK));
        CHECK(fake.link("serial:board")->dtr() == std::optional<bool>(false));
        fake.link("serial:board")->sendData({7, 8});
        const nlohmann::json data = driver.heard(1);
        CHECK(data[0].at("type") == static_cast<int>(GS_EXT_DEVICE_DATA));
        CHECK(data[0].at("device") == board);
        CHECK(data[0].at("data") == "0708");

        CHECK(driver.run("close").at("status") == static_cast<int>(GS_EXT_OK));
        REQUIRE(pumpEvents([&] { return fake.link("serial:board")->closed(); }));
        REQUIRE(pumpEvents([&] { return claims.holderName("serial:COM7").isEmpty(); }));

        REQUIRE(claims.claim("serial:COM7", app::ClaimHolder{"app:tester", QStringLiteral("Tester"), {}}, false) ==
                exthost::ClaimResult::Claimed);
        const nlohmann::json busy = driver.run("open", "serial:board");
        CHECK(busy.at("status") == static_cast<int>(GS_EXT_FAILED));
        CHECK(busy.at("answer").get<std::string>().find("Tester") != std::string::npos);
        claims.release("serial:COM7", "app:tester");

        const nlohmann::json pad = driver.run("open", "hid:pad");
        REQUIRE_MESSAGE(pad.at("status") == static_cast<int>(GS_EXT_OK), pad.dump());
        REQUIRE(fake.link("hid:pad") != nullptr);
        fake.link("hid:pad")->setFeature(3, {9, 8});
        const nlohmann::json feature = driver.run("control", R"({"op":"getFeature","report":3})");
        CHECK(feature.at("status") == static_cast<int>(GS_EXT_OK));
        CHECK(jsonOf(feature.at("answer").get<std::string>()) == nlohmann::json{{"data", "CQg="}});
        CHECK(driver.run("write", "aa", R"({"report":2})").at("status") == static_cast<int>(GS_EXT_OK));
        REQUIRE_FALSE(fake.link("hid:pad")->reports().empty());
        CHECK(fake.link("hid:pad")->reports().back() == hardware::FakeReport{2, {0xAA}});
        fake.link("hid:pad")->sendReport(1, {5});
        fake.remove("hid:pad");
        const nlohmann::json padEvents = driver.heard(2);
        CHECK(padEvents[0].at("type") == static_cast<int>(GS_EXT_DEVICE_REPORT));
        CHECK(padEvents[0].at("report") == 1);
        CHECK(padEvents[0].at("data") == "05");
        CHECK(padEvents[1].at("type") == static_cast<int>(GS_EXT_DEVICE_CLOSED));
        CHECK(padEvents[1].at("reason") == "removed");
        CHECK(claims.holderName("hid:pad").isEmpty());
        fake.add(padInfo);

        const nlohmann::json elsewhere = driver.run("open", "serial:elsewhere");
        CHECK(elsewhere.at("status") == static_cast<int>(GS_EXT_DENIED));
        CHECK(elsewhere.at("answer").get<std::string>().starts_with("GS-API-081"));
        CHECK(driver.now("list").at("status") == static_cast<int>(GS_EXT_UNSUPPORTED));
    }

    const nlohmann::json kept = DeviceDriver(registry, "gstest.boxed").run("open", "serial:board");
    REQUIRE(kept.at("status") == static_cast<int>(GS_EXT_OK));
    CHECK(claims.holderName("serial:COM7") == QStringLiteral("Boxed"));
    registry.apply({});
    REQUIRE(pumpEvents([&] { return fake.link("serial:board")->closed() && claims.holderName("serial:COM7").isEmpty(); }));
    CHECK(exthost::forgetContainer("gstest.boxed"));
}

TEST_CASE("ADR-0112: turning off an extension whose DLL inside GameScrcpy waits for a device ends the wait at once "
          "instead of holding GameScrcpy until it times out")
{
    ApplicationScope application;
    app::HardwareClaims claims;
    Scratch scratch;
    auto backend = std::make_unique<hardware::FakeBackend>();
    backend->add(fakeDevice("serial:board", hardware::Kind::Serial, "Board", "\\\\.\\COM7"));
    app::HardwareAccess access(std::move(backend), scratch / "hardware-grants.json");
    access.setReplies({.answer = [](std::uint64_t, quint64, const QString&) {},
                       .refuse = [](std::uint64_t, quint64, const QString&) {},
                       .tell = [](std::uint64_t, const QString&, const QString&, const QString&) {}});
    access.setPermitted({QStringLiteral("gstest.inside")});
    access.request(app::LogicCaller{.logic = 1, .extension = QStringLiteral("gstest.inside")}, "hardware.request", 1,
                   QString::fromUtf8(R"({"kind":"serial"})"));
    REQUIRE(access.allow({QStringLiteral("serial:board")}));
    const fs::path folder = scratch / "gstest.inside/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    app::NativeWanted made = testWanted(folder);
    made.id = "gstest.inside";
    made.name = QStringLiteral("Inside");
    made.official = true;
    made.granted = {"hardware"};
    const std::vector<ext::NativeParam> three{{"call", "string"}, {"first", "string"}, {"second", "string"}};
    made.native.nodes = {
        {.name = "deviceStart", .params = three, .returns = "number", .description = {}, .permissions = {}},
        {.name = "deviceCalls", .params = {}, .returns = "number", .description = {}, .permissions = {}}};
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    registry.apply({made});
    const auto text = [](const std::string& value) {
        return script::NodeValue{.type = script::NodeType::String, .text = value};
    };
    const auto calls = [&registry] { return registry.call("gstest.inside", "deviceCalls", {}); };
    REQUIRE(pumpEvents([&] { return calls().problem.empty(); }));
    REQUIRE(registry.call("gstest.inside", "deviceStart", {text("open"), text("serial:board"), text({})})
                .problem.empty());
    const auto started = std::chrono::steady_clock::now() + 10s;
    while (calls().value.number < 1.0 && std::chrono::steady_clock::now() < started) {
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE(calls().value.number >= 1.0);
    const auto before = std::chrono::steady_clock::now();
    registry.apply({});
    CHECK(std::chrono::steady_clock::now() - before < 10s);
}

TEST_CASE("ADR-0112: a job is not started with arguments nested more than 64 levels deep, without GameScrcpy running "
          "out of stack")
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path folder = scratch / "gstest.native/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    app::NativeWanted wanted = testWanted(folder);
    wanted.native.nodes.clear();
    wanted.native.jobs = {{.name = "speak",
                           .title = "Speak",
                           .params = {{"text", "json"}},
                           .returns = "json",
                           .description = {},
                           .permissions = {},
                           .most = std::nullopt}};
    registry.apply({wanted});
    REQUIRE(registry.problems().empty());
    app::NativeJobs jobs(registry);
    jobs.setReplies({.answer = {}, .refuse = {}, .tell = {}});
    const auto nested = [](std::size_t levels) { return std::string(levels, '[') + std::string(levels, ']'); };
    const auto withText = [](const std::string& text) {
        return R"({"extension":"gstest.native","job":"speak","args":{"text":)" + text + "}}";
    };
    const app::LogicCaller caller{.logic = 9};

    const script::ServiceAnswer fits = jobs.start(caller, withText(nested(63)));
    CHECK_MESSAGE(fits.code.empty(), fits.problem);
    for (const std::size_t levels : {std::size_t{64}, std::size_t{30000}}) {
        const script::ServiceAnswer refused = jobs.start(caller, withText(nested(levels)));
        CHECK(refused.code == "GS-API-061");
        CHECK(refused.problem == "the arguments are nested more than 64 levels deep");
    }
    const script::ServiceAnswer refusedForHost = jobs.startForHost(withText(nested(30000)), app::HostJobEvents{});
    CHECK(refusedForHost.code == "GS-API-061");
    jobs.endLogic(caller.logic);
}

TEST_CASE("ADR-0112: a DLL cannot keep a value nested more than 64 levels deep in its store, and a store file nested "
          "deeper is not read, so that neither overflows the stack of GameScrcpy")
{
    Scratch scratch;
    const auto nested = [](std::size_t levels) { return std::string(levels, '[') + std::string(levels, ']'); };
    const auto key = [](std::string text) { return textValue(exthost::ValueType::String, std::move(text)); };
    const auto json = [](std::string text) { return textValue(exthost::ValueType::Json, std::move(text)); };
    std::string problem;
    {
        std::unique_ptr<exthost::HostedModule> module = exthost::HostedModule::start(hostedOptions(scratch), problem);
        REQUIRE_MESSAGE(module != nullptr, problem);
        CHECK(callNode(*module, "remember", {key("fits"), json(nested(64))}).value.flag);
        CHECK_FALSE(callNode(*module, "remember", {key("deep"), json(nested(65))}).value.flag);
        CHECK_FALSE(callNode(*module, "remember", {key("deeper"), json(nested(30000))}).value.flag);
        CHECK(callNode(*module, "recall", {key("fits")}).value.text == nested(64));
        CHECK(callNode(*module, "recall", {key("deeper")}).value.text == "null");
        CHECK(module->crashes() == 0);
    }

    const fs::path file = scratch / "store.json";
    writeFile(file, R"({"kept":1,"deep":)" + nested(30000) + "}");
    const std::unique_ptr<exthost::InProcessModule> inside = exthost::InProcessModule::load(
        {.dll = kTestDll, .store = file, .granted = {}, .services = nullptr}, problem);
    REQUIRE_MESSAGE(inside != nullptr, problem);
    CHECK(callNode(*inside, "recall", {key("kept")}).value.text == "null");
    CHECK_FALSE(callNode(*inside, "remember", {key("deep"), json(nested(30000))}).value.flag);
    CHECK(callNode(*inside, "remember", {key("kept"), json("2")}).value.flag);
    CHECK(callNode(*inside, "recall", {key("kept")}).value.text == "2");
}

TEST_CASE("ADR-0112: a list of devices or phones from a DLL nested more than 64 levels deep is left out, so that it "
          "cannot overflow the stack of GameScrcpy")
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path folder = scratch / "gstest.native/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    app::NativeWanted wanted = testWanted(folder);
    wanted.native.nodes = {
        {.name = "setBoxes", .params = {{"boxes", "json"}}, .returns = "number", .description = {}, .permissions = {}},
        {.name = "setPhones", .params = {{"phones", "json"}}, .returns = "number", .description = {}, .permissions = {}}};
    wanted.native.deviceProviders = {{.name = "boxes", .title = "Boxes", .kinds = {"touchBox"}}};
    wanted.native.transports = {{.name = "loop", .title = "Loopback", .icon = {}}};
    wanted.granted = {"devices.provide", "transport"};
    registry.apply({wanted});
    const auto setList = [&registry](std::string_view node, const std::string& json) {
        REQUIRE(registry.call("gstest.native", node, {script::NodeValue{.type = script::NodeType::Json, .text = json}})
                    .problem.empty());
        for (int round = 0; round < 20; ++round) {
            QCoreApplication::processEvents();
        }
    };
    const std::string deep = std::string(30000, '[') + std::string(30000, ']');
    const std::string box = R"({"id":"box-1","name":"Box","kind":"touchBox")";

    setList("setBoxes", "[" + box + R"(,"capture":)" + deep + "}]");
    CHECK(registry.providedDevices().empty());
    setList("setBoxes", "[" + box + "}]");
    REQUIRE(registry.providedDevices().size() == 1);
    CHECK(registry.providedDevices()[0].id == "box-1");

    setList("setPhones", "[" + deep + "]");
    CHECK(registry.transportPhones().empty());
    setList("setPhones", R"([{"id":"echo","name":"Echo"}])");
    REQUIRE(registry.transportPhones().size() == 1);
    CHECK(registry.transportPhones()[0].id == "echo");
}
