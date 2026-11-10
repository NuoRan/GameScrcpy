#include "AppSettings.h"
#include "ExtensionHost.h"
#include "ExtensionModules.h"
#include "ExtensionTouchSink.h"
#include "NativeExtensions.h"
#include "ProfileEditor.h"
#include "TouchSinkFakes.h"

#include "unit/ext/TestFiles.h"

#include <gs/base/Clock.h>
#include <gs/ext/Manifest.h>
#include <gs/exthost/Hosted.h>
#include <gs/exthost/InProcess.h>
#include <gs/input/Modules.h>
#include <gs/input/Profile.h>
#include <gs/script/Services.h>

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
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace gs;
using namespace gs::ext::test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

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
    CHECK(stops == 1);
    CHECK(restarts == 3);
    const exthost::CallResult after = callNode(*module, "add", {numberValue(1), numberValue(1)});
    CHECK(after.status == exthost::CallStatus::Unavailable);
    CHECK(module->openTouch(0, "phone") == 0);
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
    CHECK(app::moduleFits({}, "camera"));
    CHECK(app::moduleFits({"*"}, "camera"));
    CHECK(app::moduleFits({"tap", "hold"}, "hold"));
    CHECK_FALSE(app::moduleFits({"tap"}, "camera"));
}
