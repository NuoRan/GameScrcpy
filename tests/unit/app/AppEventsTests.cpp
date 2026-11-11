#include "AppEvents.h"
#include "AppSettings.h"
#include "ExtensionHost.h"
#include "Recordings.h"

#include "unit/ext/TestFiles.h"

#include <gs/base/Clock.h>
#include <gs/exthost/Hosted.h>
#include <gs/exthost/InProcess.h>
#include <gs/exthost/Module.h>

#include <gamescrcpy_ext.h>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <windows.h>

#include <QCoreApplication>
#include <QDir>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantMap>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace gs;
using namespace gs::ext::test;
using namespace std::chrono_literals;
using gs::app::AppEvents;
using gs::app::AppTopic;
using gs::app::LogicCaller;
namespace fs = std::filesystem;

namespace {

const fs::path kEventDll(GS_TEST_NATIVE_DLL);
const fs::path kEventHost(GS_EXTHOST_PROGRAM);

class QtScope
{
public:
    QtScope()
    {
        if (QCoreApplication::instance() == nullptr) {
            m_application = std::make_unique<QCoreApplication>(m_argc, m_argv.data());
        }
    }

    QtScope(const QtScope&) = delete;
    QtScope& operator=(const QtScope&) = delete;

private:
    int m_argc = 1;
    std::string m_name = "gs_app_tests";
    std::array<char*, 2> m_argv{m_name.data(), nullptr};
    std::unique_ptr<QCoreApplication> m_application;
};

struct Told {
    std::uint64_t logic = 0;
    QString kind;
    QString topic;
    nlohmann::json value;
};

std::vector<LogicCaller> everyone()
{
    return {
        {.logic = 1, .device = QStringLiteral("A")},
        {.logic = 2, .device = QStringLiteral("B")},
        {.logic = 3, .extension = QStringLiteral("x.deck"), .device = QStringLiteral("A")},
        {.logic = 4, .extension = QStringLiteral("x.deck"), .device = QStringLiteral("B")},
        {.logic = 5, .extension = QStringLiteral("x.deck")},
        {.logic = 6, .extension = QStringLiteral("x.deck"), .device = QStringLiteral("A"), .task = true},
        {.logic = 7, .extension = QStringLiteral("y.watch")},
        {.logic = 8, .extension = QStringLiteral("y.watch"), .device = QStringLiteral("B")},
    };
}

struct Fixture {
    ManualClock clock{Microseconds{5'000'000}};
    AppEvents events{&clock};
    std::vector<LogicCaller> callers = everyone();
    std::vector<app::NativeEventTarget> natives;
    std::vector<Told> told;

    Fixture()
    {
        events.setRoutes({.callers = [this] { return callers; },
                          .tell =
                              [this](std::uint64_t logic, const QString& kind, const QString& id, const QString& json) {
                                  told.push_back({logic, kind, id, nlohmann::json::parse(json.toStdString())});
                              },
                          .everything = [](const QString& extension) { return extension == QStringLiteral("y.watch"); },
                          .natives = [this] { return natives; }});
    }

    [[nodiscard]] std::vector<std::uint64_t> logics(const QString& topic) const
    {
        std::vector<std::uint64_t> found;
        for (const Told& each : told) {
            if (each.topic == topic) {
                found.push_back(each.logic);
            }
        }
        return found;
    }
};

class Listening final : public exthost::Module
{
public:
    explicit Listening(std::set<std::string, std::less<>> topics)
        : m_topics(std::move(topics))
    {
    }

    const exthost::ModuleInfo& info() const noexcept override { return m_info; }
    exthost::CallResult call(std::size_t, std::span<const exthost::Value>) override { return {}; }
    std::uint64_t openTouch(std::size_t, std::string_view, std::string_view) override { return 0; }
    bool touch(std::uint64_t, const exthost::TouchFrame&) override { return false; }
    bool key(std::uint64_t, const exthost::KeyEvent&) override { return false; }
    void closeTouch(std::uint64_t) override {}
    bool hears(std::string_view topic) const override { return m_topics.contains(topic); }
    void appEvent(std::string_view topic, std::string_view json) override
    {
        m_heard.emplace_back(std::string(topic), nlohmann::json::parse(json));
    }

    [[nodiscard]] const std::vector<std::pair<std::string, nlohmann::json>>& heard() const { return m_heard; }

private:
    std::set<std::string, std::less<>> m_topics;
    exthost::ModuleInfo m_info;
    std::vector<std::pair<std::string, nlohmann::json>> m_heard;
};

exthost::Value stringOf(std::string text)
{
    exthost::Value made;
    made.type = exthost::ValueType::String;
    made.text = std::move(text);
    return made;
}

exthost::Value boolOf(bool on)
{
    exthost::Value made;
    made.type = exthost::ValueType::Boolean;
    made.flag = on;
    return made;
}

exthost::CallResult callTestNode(exthost::Module& module, std::string_view name, std::vector<exthost::Value> values = {})
{
    const std::vector<exthost::NodeInfo>& nodes = module.info().nodes;
    const auto found = std::ranges::find(nodes, name, &exthost::NodeInfo::name);
    REQUIRE(found != nodes.end());
    return module.call(static_cast<std::size_t>(found - nodes.begin()), values);
}

bool soon(const std::function<bool()>& done)
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

double subscribe(exthost::Module& module, const char* topic, bool on)
{
    return callTestNode(module, "subscribe", {stringOf(topic), boolOf(on)}).value.number;
}

nlohmann::json collect(exthost::Module& module, std::size_t count)
{
    nlohmann::json all = nlohmann::json::array();
    const bool arrived = soon([&] {
        const exthost::CallResult seen = callTestNode(module, "heard");
        REQUIRE_MESSAGE(seen.ok(), seen.problem);
        for (const nlohmann::json& each : nlohmann::json::parse(seen.value.text)) {
            all.push_back(each);
        }
        return all.size() >= count;
    });
    CHECK(arrived);
    return all;
}

constexpr std::string_view kGlowShader = R"(//! PARAM strength float 0.5 0 1 0.05
//! PASS glow
float4 glow(float2 uv)
{
    return Source.Sample(Linear, uv) * strength;
}
)";

void writeGlow(const fs::path& user)
{
    writeFile(user / "alice.glow/1.0.0/manifest.json",
              R"({"schemaVersion": 1, "id": "alice.glow", "version": "1.0.0", "name": "Glow", "license": "MIT",
                  "requires": {"filter": "^1.0"},
                  "contributes": {"filters": [{"name": "Glow", "file": "shaders/glow.hlsl"}]},
                  "settings": [
                    {"key": "strength", "type": "number", "title": "Strength", "min": 0, "max": 1, "default": 0.5,
                     "scope": "device"},
                    {"key": "steps", "type": "integer", "title": "Steps", "min": 1, "max": 4, "default": 3}]})");
    writeFile(user / "alice.glow/1.0.0/shaders/glow.hlsl", kGlowShader);
}

struct Published {
    QString topic;
    QString phone;
    nlohmann::json value;
};

std::vector<Published> published(const std::vector<Published>& all, const QString& topic)
{
    std::vector<Published> found;
    for (const Published& each : all) {
        if (each.topic == topic) {
            found.push_back(each);
        }
    }
    return found;
}

}

TEST_CASE("ADR-0112: an event about a phone reaches your own logic, the session scripts of that phone and the logic "
          "allowed events.app, and not the other phones, app scripts or tasks of an extension")
{
    Fixture fixture;
    REQUIRE(AppEvents::instance() == &fixture.events);
    app::publishPhoneEvent(AppTopic::AppForeground, QStringLiteral("A"), QStringLiteral("Pixel 8"),
                           {{QStringLiteral("package"), QStringLiteral("com.example.game")},
                            {QStringLiteral("game"), true}});
    CHECK(fixture.logics(QStringLiteral("app.foreground")) == std::vector<std::uint64_t>{1, 2, 3, 7, 8});
    REQUIRE_FALSE(fixture.told.empty());
    CHECK(fixture.told[0].kind == QStringLiteral("extEvent"));
    CHECK(fixture.told[0].value ==
          nlohmann::json::parse(R"({"phone":"A","name":"Pixel 8","package":"com.example.game","game":true})"));
    fixture.told.clear();

    app::publishPhoneEvent(AppTopic::DeviceConnected, QStringLiteral("B"), QStringLiteral("Galaxy"));
    CHECK(fixture.logics(QStringLiteral("device.connected")) == std::vector<std::uint64_t>{1, 2, 4, 7, 8});
    CHECK(fixture.told[0].value == nlohmann::json::parse(R"({"phone":"B","name":"Galaxy"})"));
    fixture.told.clear();

    app::publishPhoneEvent(AppTopic::ProfileApplied, QStringLiteral("A"), QStringLiteral("Pixel 8"),
                           {{QStringLiteral("profile"), QStringLiteral("shooter")},
                            {QStringLiteral("profileName"), QStringLiteral("Shooter")}});
    CHECK(fixture.logics(QStringLiteral("profile.applied")) == std::vector<std::uint64_t>{1, 2, 3, 7, 8});
    CHECK(fixture.told[0].value ==
          nlohmann::json::parse(R"({"phone":"A","name":"Pixel 8","profile":"shooter","profileName":"Shooter"})"));

    CHECK(AppEvents::shaped({.extension = QStringLiteral("x.deck"), .phone = QStringLiteral("A"), .everything = false},
                            AppTopic::WindowFocused, QStringLiteral("A"), {})
              .has_value());
    CHECK_FALSE(AppEvents::shaped({.extension = QStringLiteral("x.deck"), .phone = {}, .everything = false},
                                  AppTopic::WindowFocused, QStringLiteral("A"), {})
                    .has_value());
    CHECK(AppEvents::shaped({.extension = QStringLiteral("x.deck"), .phone = {}, .everything = true},
                            AppTopic::RecordingEnded, QStringLiteral("A"), {})
              .has_value());
}

TEST_CASE("ADR-0112: extension events reach every logic, and settings.changed tells an extension only of its own "
          "settings and of the language and look settings")
{
    Fixture fixture;
    app::publishExtensionTurned(true, QStringLiteral("z.tool"), QStringLiteral("Tool"), QStringLiteral("1.2.0"));
    CHECK(fixture.logics(QStringLiteral("extension.enabled")) == std::vector<std::uint64_t>{1, 2, 3, 4, 5, 6, 7, 8});
    CHECK(fixture.told.back().value == nlohmann::json::parse(R"({"id":"z.tool","name":"Tool","version":"1.2.0"})"));
    fixture.told.clear();

    app::publishSettingsChanged({QStringLiteral("general.language"), QStringLiteral("video.maxSize")},
                                {{QStringLiteral("general.language"), QStringLiteral("zh_CN")}});
    CHECK(fixture.logics(QStringLiteral("settings.changed")) == std::vector<std::uint64_t>{1, 2, 3, 4, 5, 6, 7, 8});
    const nlohmann::json whole = nlohmann::json::parse(
        R"({"keys":["general.language","video.maxSize"],"values":{"general.language":"zh_CN"},"extension":null,"device":null})");
    const nlohmann::json shown = nlohmann::json::parse(
        R"({"keys":["general.language"],"values":{"general.language":"zh_CN"},"extension":null,"device":null})");
    for (const Told& each : fixture.told) {
        CAPTURE(each.logic);
        CHECK(each.value == (each.logic <= 2 ? whole : shown));
    }
    fixture.told.clear();

    app::publishSettingsChanged({QStringLiteral("video.maxSize")}, {});
    CHECK(fixture.logics(QStringLiteral("settings.changed")) == std::vector<std::uint64_t>{1, 2});
    fixture.told.clear();

    app::publishSettingsChanged({QStringLiteral("port")}, {}, QStringLiteral("x.deck"), QStringLiteral("phone-1"));
    CHECK(fixture.logics(QStringLiteral("settings.changed")) == std::vector<std::uint64_t>{1, 2, 3, 4, 5, 6});
    CHECK(fixture.told.back().value ==
          nlohmann::json::parse(R"({"keys":["port"],"values":{},"extension":"x.deck","device":"phone-1"})"));
    fixture.told.clear();

    app::publishSettingsChanged({}, {});
    CHECK(fixture.told.empty());
}

TEST_CASE("ADR-0112: a DLL hears only the topics it asked for, and the events of phones and windows only with "
          "events.app")
{
    Fixture fixture;
    fixture.callers.clear();
    const auto deck = std::make_shared<Listening>(
        std::set<std::string, std::less<>>{"app.foreground", "settings.changed", "extension.disabled"});
    const auto watch =
        std::make_shared<Listening>(std::set<std::string, std::less<>>{"app.foreground", "settings.changed"});
    const auto quiet = std::make_shared<Listening>(std::set<std::string, std::less<>>{});
    fixture.natives = {{.extension = "x.deck", .everything = false, .module = deck},
                       {.extension = "y.watch", .everything = true, .module = watch},
                       {.extension = "z.quiet", .everything = true, .module = quiet}};
    app::publishPhoneEvent(AppTopic::AppForeground, QStringLiteral("A"), QStringLiteral("Pixel 8"),
                           {{QStringLiteral("package"), QStringLiteral("com.example.game")}});
    app::publishSettingsChanged({QStringLiteral("appearance.theme"), QStringLiteral("video.maxSize")},
                                {{QStringLiteral("appearance.theme"), QStringLiteral("light")}});
    app::publishSettingsChanged({QStringLiteral("port")}, {}, QStringLiteral("y.watch"));
    app::publishExtensionTurned(false, QStringLiteral("z.tool"), QStringLiteral("Tool"), QStringLiteral("1.2.0"));

    using Heard = std::vector<std::pair<std::string, nlohmann::json>>;
    const nlohmann::json theme = nlohmann::json::parse(
        R"({"keys":["appearance.theme"],"values":{"appearance.theme":"light"},"extension":null,"device":null})");
    CHECK(deck->heard() ==
          Heard{{"settings.changed", theme},
                {"extension.disabled", nlohmann::json::parse(R"({"id":"z.tool","name":"Tool","version":"1.2.0"})")}});
    CHECK(watch->heard() ==
          Heard{{"app.foreground",
                 nlohmann::json::parse(R"({"phone":"A","name":"Pixel 8","package":"com.example.game"})")},
                {"settings.changed", theme},
                {"settings.changed",
                 nlohmann::json::parse(R"({"keys":["port"],"values":{},"extension":"y.watch","device":null})")}});
    CHECK(quiet->heard().empty());
    CHECK(fixture.told.empty());
}

TEST_CASE("ADR-0112: of each topic a listener gets the first 100 events in a second at once and the rest of that "
          "second merged into its last one when the second ends")
{
    QtScope qt;
    Fixture fixture;
    fixture.callers = {{.logic = 1, .device = QStringLiteral("A")},
                       {.logic = 4, .extension = QStringLiteral("x.deck"), .device = QStringLiteral("B")}};
    const auto foreground = [](const char* phone, const QString& package) {
        app::publishPhoneEvent(AppTopic::AppForeground, QString::fromLatin1(phone), QStringLiteral("Phone"),
                               {{QStringLiteral("package"), package}});
    };
    for (int count = 1; count <= 150; ++count) {
        foreground("A", QString::number(count));
    }
    REQUIRE(fixture.logics(QStringLiteral("app.foreground")).size() == 100);
    CHECK(fixture.told.back().value["package"] == "100");
    app::publishPhoneEvent(AppTopic::WindowFocused, QStringLiteral("A"), QStringLiteral("Phone"),
                           {{QStringLiteral("window"), QStringLiteral("mirror")}, {QStringLiteral("focused"), true}});
    CHECK(fixture.logics(QStringLiteral("window.focused")) == std::vector<std::uint64_t>{1});
    foreground("B", QStringLiteral("b1"));
    REQUIRE(fixture.told.back().logic == 4);
    CHECK(fixture.told.back().value["package"] == "b1");
    fixture.told.clear();

    fixture.clock.advance(999ms);
    fixture.events.flush();
    CHECK(fixture.told.empty());
    fixture.clock.advance(1ms);
    fixture.events.flush();
    REQUIRE(fixture.told.size() == 1);
    CHECK(fixture.told[0].logic == 1);
    CHECK(fixture.told[0].value["package"] == "b1");
    fixture.told.clear();

    for (int count = 1; count <= 100; ++count) {
        foreground("A", QStringLiteral("again %1").arg(count));
    }
    REQUIRE(fixture.told.size() == 99);
    CHECK(fixture.told.back().value["package"] == "again 99");
    fixture.clock.advance(1s);
    foreground("A", QStringLiteral("late"));
    REQUIRE(fixture.told.size() == 101);
    CHECK(fixture.told[99].value["package"] == "again 100");
    CHECK(fixture.told[100].value["package"] == "late");

    fixture.events.forget(1);
    fixture.told.clear();
    for (int count = 1; count <= 100; ++count) {
        foreground("A", QStringLiteral("fresh %1").arg(count));
    }
    CHECK(fixture.told.size() == 100);
}

TEST_CASE("ADR-0112: GameScrcpy's own topics and their families are reserved, and a DLL cannot send events under "
          "them, in GameScrcpy and in gs-exthost.exe")
{
    CHECK(exthost::appTopic("device.connected"));
    CHECK(exthost::appTopic("settings.changed"));
    CHECK_FALSE(exthost::appTopic("device.renamed"));
    CHECK(exthost::reservedTopic("device.renamed"));
    CHECK(exthost::reservedTopic("app.started"));
    CHECK(exthost::reservedTopic("window.focused"));
    CHECK_FALSE(exthost::reservedTopic("devices.found"));
    CHECK_FALSE(exthost::reservedTopic("board.added"));
    CHECK(exthost::phoneTopic("app.foreground"));
    CHECK(exthost::phoneTopic("recording.ended"));
    CHECK_FALSE(exthost::phoneTopic("extension.enabled"));
    CHECK_FALSE(exthost::phoneTopic("settings.changed"));
    CHECK_FALSE(exthost::phoneTopic("board.added"));

    struct Sent final : exthost::HostServices {
        std::mutex mutex;
        std::vector<std::string> topics;

        void log(int, std::string_view) override {}
        bool notify(std::string_view, std::string_view) override { return false; }
        void emitted(std::string_view topic, std::string_view) override
        {
            const std::scoped_lock lock(mutex);
            topics.emplace_back(topic);
        }
        std::vector<std::string> seen()
        {
            const std::scoped_lock lock(mutex);
            return topics;
        }
    };
    Scratch scratch;
    std::string problem;
    Sent inside;
    const std::unique_ptr<exthost::InProcessModule> loaded = exthost::InProcessModule::load(
        {.dll = kEventDll, .store = scratch / "inside.json", .granted = {}, .services = &inside}, problem);
    REQUIRE_MESSAGE(loaded != nullptr, problem);
    Sent outside;
    exthost::HostedOptions options;
    options.program = kEventHost;
    options.dll = kEventDll;
    options.store = scratch / "outside.json";
    options.event = [&outside](const std::string& topic, const std::string& json) {
        outside.emitted(topic, json);
    };
    const std::unique_ptr<exthost::HostedModule> hosted = exthost::HostedModule::start(std::move(options), problem);
    REQUIRE_MESSAGE(hosted != nullptr, problem);
    const std::array<std::pair<exthost::Module*, Sent*>, 2> modules{
        {{loaded.get(), &inside}, {hosted.get(), &outside}}};
    for (const auto& [module, sent] : modules) {
        CAPTURE(module == loaded.get());
        for (const char* topic : {"device.connected", "settings.changed", "app.started", "window.moved"}) {
            CAPTURE(topic);
            CHECK(callTestNode(*module, "say", {stringOf(topic), stringOf("{}")}).value.number ==
                  GS_EXT_BAD_ARGUMENT);
        }
        CHECK(callTestNode(*module, "say", {stringOf("board.added"), stringOf("{}")}).value.number == GS_EXT_OK);
        REQUIRE(soon([&] { return !sent->seen().empty(); }));
        CHECK(sent->seen() == std::vector<std::string>{"board.added"});
    }
}

TEST_CASE("ADR-0112: a DLL asks for GameScrcpy's own events and hears them one at a time on a thread of GameScrcpy "
          "that does not draw the interface, until it stops, in GameScrcpy and in gs-exthost.exe")
{
    Scratch scratch;
    std::string problem;
    {
        const std::unique_ptr<exthost::InProcessModule> plain = exthost::InProcessModule::load(
            {.dll = kEventDll, .store = scratch / "plain.json", .granted = {}, .services = nullptr}, problem);
        REQUIRE_MESSAGE(plain != nullptr, problem);
        CHECK(subscribe(*plain, "app.foreground", true) == GS_EXT_DENIED);
        CHECK_FALSE(plain->hears("app.foreground"));
    }
    const std::unique_ptr<exthost::InProcessModule> inside = exthost::InProcessModule::load(
        {.dll = kEventDll, .store = scratch / "inside.json", .granted = {"events.app"}, .services = nullptr}, problem);
    REQUIRE_MESSAGE(inside != nullptr, problem);
    exthost::HostedOptions options;
    options.program = kEventHost;
    options.dll = kEventDll;
    options.store = scratch / "outside.json";
    options.granted = {"notify"};
    const std::unique_ptr<exthost::HostedModule> hosted = exthost::HostedModule::start(std::move(options), problem);
    REQUIRE_MESSAGE(hosted != nullptr, problem);

    CHECK(subscribe(*hosted, "app.foreground", true) == GS_EXT_DENIED);
    CHECK(subscribe(*inside, "app.foreground", true) == GS_EXT_OK);
    CHECK(inside->hears("app.foreground"));
    inside->appEvent("app.foreground", R"({"phone":"A","package":"com.example.game"})");
    const nlohmann::json phoneEvent = collect(*inside, 1);
    REQUIRE(phoneEvent.size() == 1);
    CHECK(phoneEvent[0][0] == "app.foreground");
    CHECK(phoneEvent[0][1] == nlohmann::json::parse(R"({"phone":"A","package":"com.example.game"})"));
    CHECK(phoneEvent[0][2].get<std::uint64_t>() != static_cast<std::uint64_t>(GetCurrentThreadId()));
    CHECK(subscribe(*inside, "app.foreground", false) == GS_EXT_OK);

    for (exthost::Module* module :
         {static_cast<exthost::Module*>(inside.get()), static_cast<exthost::Module*>(hosted.get())}) {
        CAPTURE(module == inside.get());
        CHECK(callTestNode(*module, "supports", {stringOf("app-events")}).value.flag);
        CHECK(subscribe(*module, "Board", true) == GS_EXT_BAD_ARGUMENT);
        CHECK(subscribe(*module, "board.added", true) == GS_EXT_UNSUPPORTED);
        CHECK(subscribe(*module, "settings.changed", true) == GS_EXT_OK);
        CHECK(subscribe(*module, "extension.enabled", true) == GS_EXT_OK);
        REQUIRE(soon([&] { return module->hears("settings.changed") && module->hears("extension.enabled"); }));
        CHECK_FALSE(module->hears("extension.disabled"));
        module->appEvent("extension.disabled", R"({"id":"a"})");
        module->appEvent("settings.changed", R"({"keys":["port"]})");
        module->appEvent("extension.enabled", R"({"id":"b"})");
        const nlohmann::json first = collect(*module, 2);
        REQUIRE(first.size() == 2);
        CHECK(first[0][0] == "settings.changed");
        CHECK(first[0][1] == nlohmann::json::parse(R"({"keys":["port"]})"));
        CHECK(first[1][0] == "extension.enabled");
        CHECK(first[1][1] == nlohmann::json::parse(R"({"id":"b"})"));

        CHECK(subscribe(*module, "settings.changed", false) == GS_EXT_OK);
        REQUIRE(soon([&] { return !module->hears("settings.changed"); }));
        module->appEvent("settings.changed", R"({"keys":["speed"]})");
        module->appEvent("extension.enabled", R"({"id":"c"})");
        const nlohmann::json second = collect(*module, 1);
        REQUIRE(second.size() == 1);
        CHECK(second[0][0] == "extension.enabled");
        CHECK(second[0][1] == nlohmann::json::parse(R"({"id":"c"})"));
        CHECK(subscribe(*module, "extension.enabled", false) == GS_EXT_OK);
        CHECK(callTestNode(*module, "heard").value.text == "[]");
    }
}

TEST_CASE("ADR-0112: the extension host publishes extension.enabled and extension.disabled when an extension turns "
          "on and off, and not again when it reloads")
{
    QtScope qt;
    Scratch scratch;
    writeGlow(scratch / "user");
    app::AppSettings settings(scratch / "data");
    AppEvents events;
    events.setRoutes({});
    std::vector<Published> seen;
    QObject::connect(&events, &AppEvents::published,
                     [&seen](const QString& topic, const QString& phone, const QString& json) {
                         seen.push_back({topic, phone, nlohmann::json::parse(json.toStdString())});
                     });
    app::ExtensionHost host(settings, {.user = scratch / "user", .builtin = {}});
    host.start(false);
    CHECK(published(seen, QStringLiteral("extension.enabled")).empty());

    REQUIRE(host.setEnabled("alice.glow", true));
    const std::vector<Published> enabled = published(seen, QStringLiteral("extension.enabled"));
    REQUIRE(enabled.size() == 1);
    CHECK(enabled[0].phone.isEmpty());
    CHECK(enabled[0].value == nlohmann::json::parse(R"({"id":"alice.glow","name":"Glow","version":"1.0.0"})"));

    host.start(false);
    CHECK(published(seen, QStringLiteral("extension.enabled")).size() == 1);
    CHECK(published(seen, QStringLiteral("extension.disabled")).empty());

    REQUIRE(host.setEnabled("alice.glow", false));
    const std::vector<Published> disabled = published(seen, QStringLiteral("extension.disabled"));
    REQUIRE(disabled.size() == 1);
    CHECK(disabled[0].value == nlohmann::json::parse(R"({"id":"alice.glow","name":"Glow","version":"1.0.0"})"));
}

TEST_CASE("ADR-0112: changing a setting of GameScrcpy, of one phone or of an extension publishes settings.changed "
          "with the changed keys, and the values of the language and look settings")
{
    QtScope qt;
    Scratch scratch;
    writeGlow(scratch / "user");
    app::AppSettings settings(scratch / "data");
    AppEvents events;
    events.setRoutes({});
    std::vector<Published> seen;
    QObject::connect(&events, &AppEvents::published,
                     [&seen](const QString& topic, const QString& phone, const QString& json) {
                         seen.push_back({topic, phone, nlohmann::json::parse(json.toStdString())});
                     });
    app::ExtensionHost host(settings, {.user = scratch / "user", .builtin = {}});
    host.start(false);
    REQUIRE(host.setEnabled("alice.glow", true));
    seen.clear();

    REQUIRE(settings.setValue(QStringLiteral("appearance.theme"), QStringLiteral("light")));
    REQUIRE(settings.setValue(QStringLiteral("video.maxFps"), 30));
    REQUIRE(settings.setValue(QStringLiteral("video.maxFps"), 30));
    REQUIRE(settings.setDeviceValue(QStringLiteral("phone-1"), QStringLiteral("video.maxFps"), 60));
    settings.resetDeviceValue(QStringLiteral("phone-1"), QStringLiteral("video.maxFps"));
    REQUIRE(host.setValue("alice.glow", "steps", app::settings::Scalar{std::int64_t{2}}));
    REQUIRE(host.setDeviceValue("phone-1", "alice.glow", "strength", app::settings::Scalar{0.75}));
    REQUIRE(host.resetDeviceValue("phone-1", "alice.glow", "strength"));

    const std::vector<Published> changed = published(seen, QStringLiteral("settings.changed"));
    std::vector<nlohmann::json> values;
    for (const Published& each : changed) {
        CHECK(each.phone.isEmpty());
        values.push_back(each.value);
    }
    const auto parsed = [](const char* text) { return nlohmann::json::parse(text); };
    CHECK(values ==
          std::vector<nlohmann::json>{
              parsed(R"({"keys":["appearance.theme"],"values":{"appearance.theme":"light"},"extension":null,
                         "device":null})"),
              parsed(R"({"keys":["video.maxFps"],"values":{},"extension":null,"device":null})"),
              parsed(R"({"keys":["video.maxFps"],"values":{},"extension":null,"device":"phone-1"})"),
              parsed(R"({"keys":["video.maxFps"],"values":{},"extension":null,"device":"phone-1"})"),
              parsed(R"({"keys":["steps"],"values":{},"extension":"alice.glow","device":null})"),
              parsed(R"({"keys":["strength"],"values":{},"extension":"alice.glow","device":"phone-1"})"),
              parsed(R"({"keys":["strength"],"values":{},"extension":"alice.glow","device":"phone-1"})")});
}

TEST_CASE("ADR-0112: a recording publishes recording.started with its folder when it starts and recording.ended "
          "with its folder and length when it stops")
{
    QtScope qt;
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QByteArray before = qgetenv("GS_DATA_DIR");
    const bool had = qEnvironmentVariableIsSet("GS_DATA_DIR");
    qputenv("GS_DATA_DIR", root.path().toUtf8());
    {
        app::AppSettings settings(fs::path(root.path().toStdWString()) / "settings");
        AppEvents events;
        events.setRoutes({});
        std::vector<Published> seen;
        QObject::connect(&events, &AppEvents::published,
                         [&seen](const QString& topic, const QString& phone, const QString& json) {
                             seen.push_back({topic, phone, nlohmann::json::parse(json.toStdString())});
                         });
        app::Recordings recordings(settings);
        app::RecordingStart start;
        start.serial = QStringLiteral("S1");
        start.phone = QStringLiteral("Pixel 8");
        start.device = script::DeviceInfo{.model = "Pixel 8", .android = 15, .width = 1080, .height = 2400};
        const auto recorder = recordings.start(std::move(start));
        REQUIRE(recorder.has_value());
        const std::vector<Published> started = published(seen, QStringLiteral("recording.started"));
        REQUIRE(started.size() == 1);
        CHECK(started[0].phone == QStringLiteral("S1"));
        CHECK(started[0].value["phone"] == "S1");
        CHECK(started[0].value["name"] == "Pixel 8");
        const QString folder = QString::fromStdString(started[0].value["path"].get<std::string>());
        CHECK(QDir(folder).exists());

        const QVariantMap info = recordings.stop(QStringLiteral("S1"));
        const std::vector<Published> ended = published(seen, QStringLiteral("recording.ended"));
        REQUIRE(ended.size() == 1);
        CHECK(ended[0].value["path"] == folder.toStdString());
        CHECK(info.value(QStringLiteral("folder")).toString() == folder);
        CHECK(ended[0].value["durationMs"].is_number_integer());
    }
    if (had) {
        qputenv("GS_DATA_DIR", before);
    } else {
        qunsetenv("GS_DATA_DIR");
    }
}
