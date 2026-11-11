#include "ExtensionServices.h"
#include "NativeEvents.h"
#include "NativeExtensions.h"

#include "unit/ext/TestFiles.h"

#include <gs/script/Services.h>

#include <gamescrcpy_ext.h>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <QCoreApplication>
#include <QString>

#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
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

const fs::path kTestDll(GS_TEST_NATIVE_DLL);
const fs::path kProgram(GS_EXTHOST_PROGRAM);

using Reply = std::tuple<std::uint64_t, quint64, QString>;

struct Told {
    std::uint64_t logic = 0;
    QString id;
    nlohmann::json value;
};

struct Replies {
    std::vector<Reply> answered;
    std::vector<Reply> refused;
    std::vector<Told> told;

    app::ServiceReplies make(std::function<script::NodeAnswer(std::string_view, std::string_view,
                                                              const std::vector<script::NodeValue>&)>
                                 node = {})
    {
        return {.answer = [this](std::uint64_t logic, quint64 request,
                                 const QString& json) { answered.emplace_back(logic, request, json); },
                .refuse = [this](std::uint64_t logic, quint64 request,
                                 const QString& message) { refused.emplace_back(logic, request, message); },
                .tell =
                    [this](std::uint64_t logic, const QString& kind, const QString& id, const QString& json) {
                        CHECK(kind == "extService");
                        told.push_back({logic, id, nlohmann::json::parse(json.toStdString())});
                    },
                .node = std::move(node)};
    }
};

app::LogicCaller logicOf(std::uint64_t logic, const char* extension, const char* script = "app",
                         const char* device = "")
{
    return {.logic = logic,
            .extension = QString::fromUtf8(extension),
            .script = QString::fromUtf8(script),
            .device = QString::fromUtf8(device),
            .task = false};
}

QString callJson(const char* service, const char* args, const char* limit = "null")
{
    return QString::fromStdString(R"({"service":")" + std::string(service) + R"(","args":)" +
                                  nlohmann::json(std::string(args)).dump() + R"(,"limit":)" + limit + "}");
}

std::string answerJson(const QString& id, const char* result)
{
    return R"({"call":")" + id.toStdString() + R"(","result":)" + nlohmann::json(std::string(result)).dump() + "}";
}

std::string failJson(const QString& id, const char* message, bool output)
{
    return R"({"call":")" + id.toStdString() + R"(","message":)" + nlohmann::json(std::string(message)).dump() +
           R"(,"output":)" + (output ? "true" : "false") + "}";
}

std::vector<app::ServiceExtension> sample()
{
    return {
        {.id = "alice.tools",
         .name = "Tools",
         .dependencies = {},
         .granted = {"notify", "ai"},
         .services = {{.name = "add",
                       .script = "app",
                       .node = {},
                       .input = R"({"type":"object","required":["a","b"]})",
                       .output = R"({"type":"number"})"},
                      {.name = "clock", .script = {}, .node = "now", .input = {}, .output = {}}}},
        {.id = "bob.app", .name = "Bob", .dependencies = {"alice.tools"}, .granted = {"notify", "hardware"}, .services = {}},
        {.id = "carol.x", .name = "Carol", .dependencies = {}, .granted = {"notify"}, .services = {}},
    };
}

void settle()
{
    QCoreApplication::processEvents();
}

bool eventually(const std::function<bool()>& done)
{
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(5ms);
    }
    return true;
}

}

TEST_CASE("ADR-0112: a call from logic reaches the app script that provides the service, with the caller and the "
          "permissions both extensions have, and its result comes back checked against the output schema")
{
    ApplicationScope application;
    app::ExtensionServices services;
    Replies replies;
    services.setReplies(replies.make());
    services.setExtensions(sample());
    const app::LogicCaller provider = logicOf(1, "alice.tools");
    const app::LogicCaller caller = logicOf(2, "bob.app", "main");
    CHECK(services.call(provider, "ext.provide", R"({"name":"add"})").code.empty());
    settle();

    services.request(caller, "ext.call", 11, callJson("alice.tools/add", R"({"a":1,"b":2})"));
    REQUIRE(replies.told.size() == 1);
    CHECK(replies.told[0].logic == 1);
    CHECK(replies.told[0].value == nlohmann::json::parse(R"({"type":"call","name":"add","caller":"bob.app",
                                                             "permissions":["notify"],"args":{"a":1,"b":2}})"));
    CHECK(services.call(provider, "ext.answer", answerJson(replies.told[0].id, "3")).code.empty());
    CHECK(services.call(logicOf(9, "carol.x"), "ext.answer", answerJson(replies.told[0].id, "4")).code.empty());
    settle();
    CHECK(replies.answered == std::vector<Reply>{{2, 11, "3"}});

    services.request(caller, "ext.call", 12, callJson("alice.tools/add", R"({"a":1,"b":2})"));
    REQUIRE(replies.told.size() == 2);
    CHECK(services.call(logicOf(9, "carol.x"), "ext.answer", answerJson(replies.told[1].id, "4")).code.empty());
    CHECK(services.call(provider, "ext.answer", answerJson(replies.told[1].id, R"("three")")).code.empty());
    settle();
    REQUIRE(replies.refused.size() == 1);
    CHECK(replies.refused[0] ==
          Reply{2, 12, "GS-API-148 the result of alice.tools/add does not match its output: $ is not a number"});

    services.request(caller, "ext.call", 13, callJson("alice.tools/add", R"({"a":1})"));
    REQUIRE(replies.refused.size() == 2);
    CHECK(replies.refused[1] ==
          Reply{2, 13, "GS-API-148 the arguments do not match the input of alice.tools/add: $.b is missing"});
    services.request(caller, "ext.call", 14, callJson("alice.tools/add", "nope"));
    REQUIRE(replies.refused.size() == 3);
    CHECK(std::get<2>(replies.refused[2]).startsWith(QStringLiteral("GS-API-148 the arguments do not match")));
    CHECK(replies.told.size() == 2);

    services.request(caller, "ext.call", 15, callJson("alice.tools/add", R"({"a":1,"b":2})"));
    REQUIRE(replies.told.size() == 3);
    CHECK(services.call(provider, "ext.fail", failJson(replies.told[2].id, "no luck", false)).code.empty());
    services.request(caller, "ext.call", 16, callJson("alice.tools/add", R"({"a":1,"b":2})"));
    REQUIRE(replies.told.size() == 4);
    CHECK(services.call(provider, "ext.fail", failJson(replies.told[3].id, "GS-API-148 the result must be JSON", true))
              .code.empty());
    settle();
    REQUIRE(replies.refused.size() == 5);
    CHECK(replies.refused[3] == Reply{2, 15, "GS-API-149 alice.tools/add failed: no luck"});
    CHECK(std::get<2>(replies.refused[4]).startsWith(QStringLiteral("GS-API-148 the result of alice.tools/add is not JSON: ")));

    services.request(caller, "ext.call", 17, callJson("alice.tools/add", R"({"a":1,"b":2})", R"(["hardware"])"));
    services.request(logicOf(3, "", "logic"), "ext.call", 18, callJson("alice.tools/add", R"({"a":1,"b":2})"));
    REQUIRE(replies.told.size() == 6);
    CHECK(replies.told[4].value["permissions"] == nlohmann::json::array());
    CHECK(replies.told[5].value["caller"] == "");
    CHECK(replies.told[5].value["permissions"] == nlohmann::json::array({"ai", "notify"}));
    CHECK(services.call(caller, "ext.withdraw", R"({"name":"add"})").code.empty());
    services.request(caller, "ext.call", 19, callJson("alice.tools/add", R"({"a":1,"b":2})"));
    REQUIRE(replies.told.size() == 7);
    CHECK(replies.told[6].logic == 1);
}

TEST_CASE("ADR-0112: only an extension that depends on another, the same extension or the user's own logic calls its "
          "services, and only the app script the manifest names provides them")
{
    ApplicationScope application;
    app::ExtensionServices services;
    Replies replies;
    services.setReplies(replies.make());
    services.setExtensions(sample());
    const app::LogicCaller provider = logicOf(1, "alice.tools");
    REQUIRE(services.call(provider, "ext.provide", R"({"name":"add"})").code.empty());

    services.request(logicOf(4, "carol.x", "main"), "ext.call", 21, callJson("alice.tools/add", R"({"a":1,"b":2})"));
    REQUIRE(replies.refused.size() == 1);
    CHECK(replies.refused[0] == Reply{4, 21,
                                      "GS-API-147 carol.x may call alice.tools/add only when it lists alice.tools in "
                                      "its dependencies"});
    services.request(logicOf(5, "alice.tools", "phone", "emulator-5554"), "ext.call", 22,
                     callJson("alice.tools/add", R"({"a":1,"b":2})"));
    REQUIRE(replies.told.size() == 1);
    CHECK(replies.told[0].value["caller"] == "alice.tools");
    CHECK(replies.told[0].value["permissions"] == nlohmann::json::array({"ai", "notify"}));

    services.request(logicOf(2, "bob.app", "main"), "ext.call", 23, callJson("alice.tools/subtract", "{}"));
    services.request(logicOf(2, "bob.app", "main"), "ext.call", 24, callJson("dave.none/add", "{}"));
    services.request(logicOf(2, "bob.app", "main"), "ext.call", 25, callJson("add", "{}"));
    REQUIRE(replies.refused.size() == 4);
    CHECK(std::get<2>(replies.refused[1]) == "GS-API-146 Tools offers no service named subtract");
    CHECK(std::get<2>(replies.refused[2]) ==
          "GS-API-146 no extension that is on offers dave.none/add; turn on the extension that adds it");
    CHECK(std::get<2>(replies.refused[3]).startsWith(QStringLiteral("GS-API-146 no extension that is on offers add")));
    services.request(logicOf(2, "bob.app", "main"), "ext.listen", 26, "{}");
    REQUIRE(replies.refused.size() == 5);
    CHECK(std::get<2>(replies.refused[4]).startsWith(QStringLiteral("GS-API-070")));

    const auto provided = [&services](const app::LogicCaller& who, const char* name) {
        return services.call(who, "ext.provide", R"({"name":")" + std::string(name) + R"("})");
    };
    const script::ServiceAnswer otherScript = provided(logicOf(6, "alice.tools", "panel"), "add");
    CHECK(otherScript.code == "GS-API-145");
    CHECK(otherScript.problem == "add is answered by the app script app, not by panel");
    CHECK(provided(logicOf(7, "", "logic"), "add").code == "GS-API-145");
    CHECK(provided(logicOf(8, "alice.tools", "app", "emulator-5554"), "add").code == "GS-API-145");
    CHECK(provided(provider, "subtract").problem == "subtract is not a service of alice.tools: declare it in "
                                                    "contributes.services");
    CHECK(provided(provider, "clock").problem == "clock is answered by the native node now, not by logic");
    CHECK(services.call(provider, "ext.unknown", "{}").code == "GS-API-070");

    CHECK(services.depends("bob.app", "alice.tools"));
    CHECK_FALSE(services.depends("carol.x", "alice.tools"));
    CHECK_FALSE(services.depends("alice.tools", "bob.app"));
    CHECK_FALSE(services.depends("bob.app", "dave.none"));
}

TEST_CASE("ADR-0112: a call waits for the app script to provide the service, times out, is cancelled with the caller's "
          "task and fails when the logic that answers it stops or its extension is turned off")
{
    ApplicationScope application;
    app::ExtensionServices services;
    Replies replies;
    services.setReplies(replies.make());
    services.setExtensions(sample());
    services.setTimeout(200ms);
    const app::LogicCaller provider = logicOf(1, "alice.tools");
    const app::LogicCaller caller = logicOf(2, "bob.app", "main");
    const QString args = callJson("alice.tools/add", R"({"a":1,"b":2})");

    services.request(caller, "ext.call", 31, args);
    CHECK(replies.told.empty());
    REQUIRE(services.call(provider, "ext.provide", R"({"name":"add"})").code.empty());
    settle();
    REQUIRE(replies.told.size() == 1);
    REQUIRE(eventually([&] { return replies.refused.size() == 1; }));
    CHECK(replies.refused[0] == Reply{2, 31, "GS-API-149 alice.tools/add did not answer within 200 ms"});
    REQUIRE(replies.told.size() == 2);
    CHECK(replies.told[1].id == replies.told[0].id);
    CHECK(replies.told[1].value == nlohmann::json::parse(R"({"type":"cancel"})"));

    services.request(caller, "ext.call", 32, args);
    REQUIRE(replies.told.size() == 3);
    services.drop(caller, 32);
    REQUIRE(replies.told.size() == 4);
    CHECK(replies.told[3].value["type"] == "cancel");
    CHECK(services.call(provider, "ext.answer", answerJson(replies.told[2].id, "3")).code.empty());
    settle();
    CHECK(replies.answered.empty());

    services.request(caller, "ext.call", 33, args);
    REQUIRE(replies.told.size() == 5);
    services.endLogic(1);
    REQUIRE(replies.refused.size() == 2);
    CHECK(replies.refused[1] ==
          Reply{2, 33, "GS-API-149 the logic that answers alice.tools/add stopped before it answered"});

    services.request(caller, "ext.call", 34, args);
    REQUIRE(eventually([&] { return replies.refused.size() == 3; }));
    CHECK(replies.refused[2] ==
          Reply{2, 34,
                "GS-API-146 alice.tools/add was not provided within 200 ms: the app script of alice.tools is not running "
                "or does not call ext.provide(\"add\")"});

    services.request(caller, "ext.call", 35, args);
    services.endLogic(2);
    REQUIRE(services.call(provider, "ext.provide", R"({"name":"add"})").code.empty());
    settle();
    CHECK(replies.told.size() == 5);

    services.request(caller, "ext.call", 36, args);
    REQUIRE(replies.told.size() == 6);
    std::vector<app::ServiceExtension> without = sample();
    without.erase(without.begin());
    services.setExtensions(without);
    REQUIRE(replies.refused.size() == 4);
    CHECK(replies.refused[3] == Reply{2, 36,
                                      "GS-API-146 alice.tools/add is no longer offered: the extension that offers it "
                                      "was turned off"});
    REQUIRE(replies.told.size() == 7);
    CHECK(replies.told[6].value["type"] == "cancel");
    const auto quiet = std::chrono::steady_clock::now() + 400ms;
    while (std::chrono::steady_clock::now() < quiet) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(5ms);
    }
    CHECK(replies.refused.size() == 4);
    CHECK(replies.told.size() == 7);
}

TEST_CASE("ADR-0112: a service answered by a node of a DLL gets the caller and the permissions inside GameScrcpy and "
          "through gs-exthost, and a DLL's own thread calls services with call_service")
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path insideFolder = scratch / "gstest.inside/1.0.0";
    const fs::path outsideFolder = scratch / "gstest.outside/1.0.0";
    for (const fs::path& folder : {insideFolder, outsideFolder}) {
        fs::create_directories(folder);
        fs::copy_file(kTestDll, folder / "native.dll");
    }
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    const auto wanted = [](const fs::path& folder, const char* id, bool official) {
        app::NativeWanted made;
        made.id = id;
        made.version = "1.0.0";
        made.name = QString::fromUtf8(id);
        made.directory = folder;
        made.official = official;
        made.native.dll = "native.dll";
        made.native.nodes = {
            {.name = "serve", .params = {{"input", "json"}}, .returns = "json", .description = {}, .permissions = {}},
            {.name = "callService",
             .params = {{"service", "string"}, {"json", "json"}},
             .returns = "number",
             .description = {},
             .permissions = {}},
            {.name = "callServiceNow",
             .params = {{"service", "string"}, {"json", "json"}},
             .returns = "number",
             .description = {},
             .permissions = {}},
            {.name = "serviceResult", .params = {}, .returns = "json", .description = {}, .permissions = {}},
            {.name = "supports", .params = {{"feature", "string"}}, .returns = "boolean", .description = {},
             .permissions = {}}};
        made.granted = {"notify"};
        return made;
    };
    registry.apply({wanted(insideFolder, "gstest.inside", true), wanted(outsideFolder, "gstest.outside", false)});
    REQUIRE(eventually([&] {
        return registry.call("gstest.inside", "serviceResult", {}).code.empty() &&
               registry.call("gstest.outside", "serviceResult", {}).code.empty();
    }));

    app::ExtensionServices services;
    Replies replies;
    services.setReplies(replies.make([&registry](std::string_view extension, std::string_view node,
                                                 const std::vector<script::NodeValue>& values) {
        return registry.call(extension, node, values);
    }));
    const std::string echoOutput = R"({"type":"object","required":["seen"]})";
    services.setExtensions({
        {.id = "gstest.inside",
         .name = "Inside",
         .dependencies = {"gstest.outside"},
         .granted = {"notify", "hardware"},
         .services = {{.name = "echo", .script = {}, .node = "serve", .input = {}, .output = echoOutput}}},
        {.id = "gstest.outside",
         .name = "Outside",
         .dependencies = {"gstest.inside"},
         .granted = {"notify"},
         .services = {{.name = "echo", .script = {}, .node = "serve", .input = {}, .output = echoOutput}}},
        {.id = "bob.app",
         .name = "Bob",
         .dependencies = {"gstest.inside", "gstest.outside"},
         .granted = {"notify", "hardware"},
         .services = {}},
        {.id = "carol.x",
         .name = "Carol",
         .dependencies = {},
         .granted = {},
         .services = {{.name = "ping", .script = "app", .node = {}, .input = {}, .output = {}}}},
    });

    services.request(logicOf(2, "bob.app", "main"), "ext.call", 41, callJson("gstest.inside/echo", R"({"x":1})"));
    services.request(logicOf(2, "bob.app", "main"), "ext.call", 42, callJson("gstest.outside/echo", R"({"x":2})"));
    CHECK(replies.refused.empty());
    CHECK(replies.answered ==
          std::vector<Reply>{
              {2, 41, R"({"seen":{"caller":"bob.app","permissions":["hardware","notify"],"input":{"x":1}}})"},
              {2, 42, R"({"seen":{"caller":"bob.app","permissions":["notify"],"input":{"x":2}}})"}});

    const auto text = [](const char* value) { return script::NodeValue{.type = script::NodeType::String, .text = value}; };
    const auto json = [](const char* value) { return script::NodeValue{.type = script::NodeType::Json, .text = value}; };
    const auto answerOf = [&registry](const char* id, const char* service, const char* args) {
        REQUIRE(registry.call(id, "callService", {script::NodeValue{.type = script::NodeType::String, .text = service},
                                                   script::NodeValue{.type = script::NodeType::Json, .text = args}})
                    .code.empty());
        nlohmann::json result;
        REQUIRE(eventually([&] {
            const script::NodeAnswer answer = registry.call(id, "serviceResult", {});
            result = answer.code.empty() ? nlohmann::json::parse(answer.value.text) : nlohmann::json();
            return !result.is_null();
        }));
        return result;
    };

    const nlohmann::json outside = answerOf("gstest.outside", "gstest.inside/echo", R"({"y":2})");
    CHECK(outside.at("status").get<int>() == GS_EXT_OK);
    CHECK(outside.at("text") == R"({"seen":{"caller":"gstest.outside","permissions":["notify"],"input":{"y":2}}})");
    const nlohmann::json inside = answerOf("gstest.inside", "gstest.outside/echo", "[3]");
    CHECK(inside.at("status").get<int>() == GS_EXT_OK);
    CHECK(inside.at("text") == R"({"seen":{"caller":"gstest.inside","permissions":["notify"],"input":[3]}})");
    const nlohmann::json denied = answerOf("gstest.inside", "carol.x/ping", "{}");
    CHECK(denied.at("status").get<int>() == GS_EXT_DENIED);
    CHECK(denied.at("text").get<std::string>().starts_with("GS-API-147 gstest.inside may call carol.x/ping"));
    const nlohmann::json missing = answerOf("gstest.outside", "gstest.inside/nothing", "{}");
    CHECK(missing.at("status").get<int>() == GS_EXT_UNSUPPORTED);
    CHECK(missing.at("text").get<std::string>().starts_with("GS-API-146"));

    const auto nowStatus = [&](const char* id, const char* service) {
        return static_cast<int>(registry.call(id, "callServiceNow", {text(service), json("{}")}).value.number);
    };
    CHECK(nowStatus("gstest.outside", "gstest.inside/echo") == GS_EXT_UNSUPPORTED);
    CHECK(nowStatus("gstest.inside", "gstest.outside/echo") == GS_EXT_UNSUPPORTED);
    CHECK(registry.call("gstest.inside", "supports", {text("services")}).value.flag);
    CHECK(registry.call("gstest.outside", "supports", {text("services")}).value.flag);
    registry.apply({});
}

TEST_CASE("ADR-0112: the events of a DLL reach the logic of the extensions that depend on its extension as "
          "\"<extension id>:<topic>\"")
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path folder = scratch / "gstest.native/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    app::NativeWanted wanted;
    wanted.id = "gstest.native";
    wanted.version = "1.0.0";
    wanted.name = QStringLiteral("Test native");
    wanted.directory = folder;
    wanted.official = true;
    wanted.native.dll = "native.dll";
    wanted.native.nodes = {{.name = "say",
                            .params = {{"topic", "string"}, {"json", "string"}},
                            .returns = "number",
                            .description = {},
                            .permissions = {}}};
    registry.apply({wanted});
    REQUIRE(registry.loaded("gstest.native"));

    app::ExtensionServices services;
    services.setExtensions({{.id = "gstest.native", .name = "Native", .dependencies = {}, .granted = {}, .services = {}},
                            {.id = "bob.app", .name = "Bob", .dependencies = {"gstest.native"}, .granted = {}, .services = {}},
                            {.id = "carol.x", .name = "Carol", .dependencies = {}, .granted = {}, .services = {}}});
    std::vector<std::tuple<std::uint64_t, QString, QString, QString>> told;
    app::NativeEvents events(registry);
    events.setRoutes({.callers =
                          [] {
                              return std::vector<app::LogicCaller>{{.logic = 3, .extension = "gstest.native"},
                                                                   {.logic = 4, .extension = "bob.app"},
                                                                   {.logic = 5, .extension = "carol.x"},
                                                                   {.logic = 6, .extension = {}}};
                          },
                      .tell = [&told](std::uint64_t logic, const QString& kind, const QString& id,
                                      const QString& json) { told.emplace_back(logic, kind, id, json); },
                      .hears = [&services](const QString& listener, const QString& emitter) {
                          return services.depends(listener, emitter);
                      }});
    REQUIRE(registry.call("gstest.native", "say",
                          {script::NodeValue{.type = script::NodeType::String, .text = "board.added"},
                           script::NodeValue{.type = script::NodeType::String, .text = R"({"id":"b1"})"}})
                .problem.empty());
    REQUIRE(eventually([&] { return told.size() >= 2; }));
    using Event = std::tuple<std::uint64_t, QString, QString, QString>;
    CHECK(told == std::vector<Event>{{3, "extEvent", "board.added", R"({"id":"b1"})"},
                                     {4, "extEvent", "gstest.native:board.added", R"({"id":"b1"})"}});
    registry.apply({});
}
