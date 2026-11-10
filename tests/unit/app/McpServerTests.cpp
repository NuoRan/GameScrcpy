#include "AppSettings.h"
#include "McpDrafts.h"
#include "McpServer.h"
#include "McpTestSupport.h"

#include <doctest/doctest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QStringList>
#include <QtGlobal>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

using gs::app::AppSettings;
using gs::app::McpAnswer;
using gs::app::McpCall;
using gs::app::McpConfirmation;
using gs::app::McpDrafts;
using gs::app::McpOutcome;
using gs::app::McpServer;
using gs::test::hello;
using gs::test::McpClient;
using gs::test::rpc;

namespace {

class FakeTools final : public gs::app::McpToolHost
{
public:
    [[nodiscard]] QJsonArray tools() const override
    {
        const QJsonObject schema{{QStringLiteral("type"), QStringLiteral("object")},
                                 {QStringLiteral("properties"), QJsonObject{}}};
        return {QJsonObject{{QStringLiteral("name"), QStringLiteral("echo")},
                            {QStringLiteral("description"), QStringLiteral("Echoes the text")},
                            {QStringLiteral("inputSchema"), schema}},
                QJsonObject{{QStringLiteral("name"), QStringLiteral("ask")},
                            {QStringLiteral("description"), QStringLiteral("Asks the user")},
                            {QStringLiteral("inputSchema"), schema}}};
    }

    void call(const McpCall& call, Reply reply) override
    {
        calls.push_back(call.tool);
        if (call.tool == u"echo") {
            const QString text = call.client + u':' + call.arguments.value(QStringLiteral("text")).toString();
            reply(McpOutcome{.content = QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                               {QStringLiteral("text"), text}}}});
            return;
        }
        call.confirm(McpConfirmation{.kind = QStringLiteral("write"),
                                     .details = {{QStringLiteral("profile"), QStringLiteral("Shooter")}}},
                     [this, reply](McpAnswer answer) {
                         answers.push_back(answer);
                         const bool allowed = answer == McpAnswer::Allowed || answer == McpAnswer::AllowedForAWhile;
                         reply(McpOutcome{.content = QJsonArray{QJsonObject{
                                              {QStringLiteral("type"), QStringLiteral("text")},
                                              {QStringLiteral("text"), allowed ? QStringLiteral("done")
                                                                               : QStringLiteral("GS-MCP-008 no")}}},
                                          .failed = !allowed,
                                          .code = allowed ? QString() : QStringLiteral("GS-MCP-008")});
                     });
    }

    void cancel(quint64 call) override { cancelled.push_back(call); }
    void forget(const QString& client) override { forgotten.push_back(client); }

    std::vector<QString> calls;
    std::vector<McpAnswer> answers;
    std::vector<quint64> cancelled;
    std::vector<QString> forgotten;
};

class LogCapture
{
public:
    LogCapture()
    {
        s_lines.clear();
        s_previous = qInstallMessageHandler(&LogCapture::handle);
    }
    ~LogCapture() { qInstallMessageHandler(s_previous); }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    [[nodiscard]] static QStringList lines() { return s_lines; }

private:
    static void handle(QtMsgType, const QMessageLogContext&, const QString& message) { s_lines.append(message); }

    static inline QStringList s_lines;
    static inline QtMessageHandler s_previous = nullptr;
};

struct Setup {
    gs::test::McpApplication application;
    gs::test::McpFolder folder;
    AppSettings settings{folder.path()};
    FakeTools tools;
    gs::ManualClock clock;
    McpDrafts drafts{{.root = folder.path() / "drafts"}};
    QString pipe = gs::test::uniquePipe();
    McpServer server{settings, tools, drafts, clock, pipe};

    void turn(bool on) { REQUIRE(settings.setValue(QStringLiteral("mcp.enabled"), on)); }

    QJsonObject approved(McpClient& client, const QString& name)
    {
        REQUIRE(client.connected());
        const QJsonObject started = client.ask(hello(1, name));
        REQUIRE(started.contains(QStringLiteral("result")));
        if (server.approval() == name) {
            server.approve(name, true, false);
            REQUIRE(client.receive().value(QStringLiteral("method")).toString() == u"notifications/tools/list_changed");
        }
        return started;
    }
};

QString errorText(const QJsonObject& reply)
{
    return reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
}

int errorCode(const QJsonObject& reply)
{
    return reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt();
}

QString firstText(const QJsonObject& reply)
{
    return reply.value(QStringLiteral("result"))
        .toObject()
        .value(QStringLiteral("content"))
        .toArray()
        .at(0)
        .toObject()
        .value(QStringLiteral("text"))
        .toString();
}

}

TEST_CASE("the MCP pipe is open only while the setting is on, and turning it off drops every connection")
{
    Setup setup;
    CHECK_FALSE(setup.server.listening());
    {
        McpClient early(setup.pipe);
        CHECK_FALSE(early.connected());
    }
    setup.turn(true);
    REQUIRE(setup.server.listening());
    McpClient client(setup.pipe);
    setup.approved(client, QStringLiteral("Test client"));
    CHECK(client.ask(rpc(2, QStringLiteral("ping"))).contains(QStringLiteral("result")));
    setup.turn(false);
    CHECK_FALSE(setup.server.listening());
    CHECK(client.dropped());
    McpClient late(setup.pipe);
    CHECK_FALSE(late.connected());
}

TEST_CASE("initialize names the server and its tools capability; broken lines get JSON-RPC errors")
{
    Setup setup;
    setup.turn(true);
    McpClient client(setup.pipe);
    REQUIRE(client.connected());

    client.sendRaw("{not json\n");
    const QJsonObject parse = client.receive();
    CHECK(errorCode(parse) == -32700);
    CHECK(parse.value(QStringLiteral("id")).isNull());

    client.sendRaw("[1, 2]\n");
    CHECK(errorCode(client.receive()) == -32600);
    client.sendRaw("{\"jsonrpc\": \"2.0\", \"id\": 7}\n");
    CHECK(errorCode(client.receive()) == -32600);

    CHECK(client.ask(rpc(2, QStringLiteral("ping"))).value(QStringLiteral("result")).toObject().isEmpty());
    CHECK(errorCode(client.ask(rpc(3, QStringLiteral("tools/list")))) == McpServer::kNotInitialized);

    QJsonObject asked = hello(4, QStringLiteral("Test client"));
    QJsonObject params = asked.value(QStringLiteral("params")).toObject();
    params.insert(QStringLiteral("protocolVersion"), QStringLiteral("2024-11-05"));
    asked.insert(QStringLiteral("params"), params);
    const QJsonObject started = client.ask(asked);
    CHECK(started.value(QStringLiteral("id")).toInt() == 4);
    const QJsonObject result = started.value(QStringLiteral("result")).toObject();
    CHECK(result.value(QStringLiteral("protocolVersion")).toString() == u"2024-11-05");
    CHECK(result.value(QStringLiteral("capabilities")).toObject().contains(QStringLiteral("tools")));
    CHECK(result.value(QStringLiteral("serverInfo")).toObject().value(QStringLiteral("name")).toString() == u"GameScrcpy");
    CHECK_FALSE(result.value(QStringLiteral("instructions")).toString().isEmpty());
    CHECK(errorCode(client.ask(hello(5, QStringLiteral("Test client")))) == -32600);

    McpClient other(setup.pipe);
    REQUIRE(other.connected());
    QJsonObject newer = hello(1, QStringLiteral("Other"));
    QJsonObject newerParams = newer.value(QStringLiteral("params")).toObject();
    newerParams.insert(QStringLiteral("protocolVersion"), QStringLiteral("2099-01-01"));
    newer.insert(QStringLiteral("params"), newerParams);
    CHECK(other.ask(newer).value(QStringLiteral("result")).toObject().value(QStringLiteral("protocolVersion")).toString() ==
          u"2025-06-18");

    McpClient nameless(setup.pipe);
    REQUIRE(nameless.connected());
    CHECK(errorCode(nameless.ask(hello(1, QStringLiteral("  ")))) == -32602);
}

TEST_CASE("a new client waits for the user: only initialize and ping work until it is allowed")
{
    Setup setup;
    setup.turn(true);
    int attention = 0;
    QObject::connect(&setup.server, &McpServer::attentionWanted, [&attention] { ++attention; });
    McpClient client(setup.pipe);
    REQUIRE(client.connected());
    CHECK(client.ask(hello(1, QStringLiteral("Test client"))).contains(QStringLiteral("result")));
    CHECK(setup.server.approval() == u"Test client");
    CHECK(attention == 1);

    const QJsonObject waiting = client.ask(rpc(2, QStringLiteral("tools/list")));
    CHECK(errorCode(waiting) == McpServer::kRefused);
    CHECK(errorText(waiting).startsWith(QStringLiteral("GS-MCP-001 ")));
    CHECK(client.ask(rpc(3, QStringLiteral("ping"))).contains(QStringLiteral("result")));
    CHECK(setup.tools.calls.empty());

    setup.server.approve(QStringLiteral("Test client"), true, false);
    CHECK(setup.server.approval().isEmpty());
    const QJsonObject changed = client.receive();
    CHECK(changed.value(QStringLiteral("method")).toString() == u"notifications/tools/list_changed");
    CHECK_FALSE(changed.contains(QStringLiteral("id")));
    const QJsonArray tools =
        client.ask(rpc(4, QStringLiteral("tools/list"))).value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray();
    REQUIRE(tools.size() == 2);
    CHECK(tools.at(0).toObject().value(QStringLiteral("name")).toString() == u"echo");
    CHECK(setup.server.clients().isEmpty());
    CHECK(setup.server.connected() == QStringList{QStringLiteral("Test client")});

    McpClient again(setup.pipe);
    REQUIRE(again.connected());
    CHECK(again.ask(hello(1, QStringLiteral("Test client"))).contains(QStringLiteral("result")));
    CHECK(setup.server.approval() == u"Test client");
    CHECK(errorCode(again.ask(rpc(2, QStringLiteral("tools/list")))) == McpServer::kRefused);
}

TEST_CASE("a refused client gets GS-MCP-002, and a client that leaves while waiting withdraws the question")
{
    Setup setup;
    setup.turn(true);
    McpClient refused(setup.pipe);
    REQUIRE(refused.connected());
    CHECK(refused.ask(hello(1, QStringLiteral("Refused"))).contains(QStringLiteral("result")));
    setup.server.approve(QStringLiteral("Refused"), false, true);
    const QJsonObject answer = refused.ask(rpc(2, QStringLiteral("tools/call"),
                                               {{QStringLiteral("name"), QStringLiteral("echo")}}));
    CHECK(errorText(answer).startsWith(QStringLiteral("GS-MCP-002 ")));
    CHECK(setup.server.clients().isEmpty());
    CHECK(setup.tools.calls.empty());

    {
        McpClient leaving(setup.pipe);
        REQUIRE(leaving.connected());
        CHECK(leaving.ask(hello(1, QStringLiteral("Leaving"))).contains(QStringLiteral("result")));
        CHECK(setup.server.approval() == u"Leaving");
    }
    CHECK(gs::test::waitUntil([&setup] { return setup.server.approval().isEmpty(); }));
}

TEST_CASE("a remembered client connects without asking, and revoking it drops its connections at once")
{
    Setup setup;
    setup.turn(true);
    McpClient first(setup.pipe);
    REQUIRE(first.connected());
    CHECK(first.ask(hello(1, QStringLiteral("Remembered"))).contains(QStringLiteral("result")));
    setup.server.approve(QStringLiteral("Remembered"), true, true);
    CHECK(first.receive().value(QStringLiteral("method")).toString() == u"notifications/tools/list_changed");
    CHECK(setup.server.clients() == QStringList{QStringLiteral("Remembered")});
    CHECK(setup.settings.mcpClients() == std::vector<std::string>{"Remembered"});

    McpClient second(setup.pipe);
    REQUIRE(second.connected());
    CHECK(second.ask(hello(1, QStringLiteral("Remembered"))).contains(QStringLiteral("result")));
    CHECK(setup.server.approval().isEmpty());
    CHECK(second.ask(rpc(2, QStringLiteral("tools/list"))).contains(QStringLiteral("result")));

    setup.server.revoke(QStringLiteral("Remembered"));
    CHECK(setup.server.clients().isEmpty());
    CHECK(setup.settings.mcpClients().empty());
    CHECK(first.dropped());
    CHECK(second.dropped());
}

TEST_CASE("tools/call reaches the tool host and leaves one log line; an unknown tool is refused")
{
    Setup setup;
    setup.turn(true);
    McpClient client(setup.pipe);
    setup.approved(client, QStringLiteral("Logger"));
    const LogCapture capture;
    const QJsonObject echoed =
        client.ask(rpc(2, QStringLiteral("tools/call"),
                       {{QStringLiteral("name"), QStringLiteral("echo")},
                        {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("text"), QStringLiteral("hi")}}}}));
    CHECK(echoed.value(QStringLiteral("id")).toInt() == 2);
    CHECK(firstText(echoed) == u"Logger:hi");
    CHECK_FALSE(echoed.value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool());
    const QStringList lines = LogCapture::lines().filter(QStringLiteral("mcp Logger: echo"));
    REQUIRE(lines.size() == 1);
    CHECK(lines.front().endsWith(QStringLiteral(" bytes")));

    const QJsonObject unknown =
        client.ask(rpc(3, QStringLiteral("tools/call"), {{QStringLiteral("name"), QStringLiteral("nope")}}));
    CHECK(errorCode(unknown) == -32602);
    CHECK(errorText(unknown).startsWith(QStringLiteral("GS-MCP-003 ")));
    CHECK(errorCode(client.ask(rpc(4, QStringLiteral("sampling/createMessage")))) == -32601);
}

TEST_CASE("a question about a change is refused when nobody answers within two minutes")
{
    Setup setup;
    setup.turn(true);
    McpClient client(setup.pipe);
    setup.approved(client, QStringLiteral("Asker"));

    client.send(rpc(2, QStringLiteral("tools/call"), {{QStringLiteral("name"), QStringLiteral("ask")}}));
    REQUIRE(gs::test::waitUntil([&setup] { return !setup.server.confirmation().isEmpty(); }));
    const QVariantMap shown = setup.server.confirmation();
    CHECK(shown.value(QStringLiteral("kind")).toString() == u"write");
    CHECK(shown.value(QStringLiteral("client")).toString() == u"Asker");
    CHECK(shown.value(QStringLiteral("profile")).toString() == u"Shooter");

    setup.clock.advance(std::chrono::seconds(119));
    setup.server.expire();
    CHECK_FALSE(setup.server.confirmation().isEmpty());
    CHECK(setup.tools.answers.empty());
    setup.clock.advance(std::chrono::seconds(1));
    setup.server.expire();
    CHECK(setup.server.confirmation().isEmpty());
    REQUIRE(setup.tools.answers.size() == 1);
    CHECK(setup.tools.answers.front() == McpAnswer::Unanswered);
    const QJsonObject refused = client.receive();
    CHECK(refused.value(QStringLiteral("id")).toInt() == 2);
    CHECK(refused.value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool());

    client.send(rpc(3, QStringLiteral("tools/call"), {{QStringLiteral("name"), QStringLiteral("ask")}}));
    REQUIRE(gs::test::waitUntil([&setup] { return !setup.server.confirmation().isEmpty(); }));
    setup.server.answer(setup.server.confirmation().value(QStringLiteral("id")).toULongLong(), true);
    CHECK(firstText(client.receive()) == u"done");
    CHECK(setup.tools.answers.back() == McpAnswer::Allowed);
}

TEST_CASE("a client that leaves or cancels takes its question and its call with it")
{
    Setup setup;
    setup.turn(true);
    {
        McpClient client(setup.pipe);
        setup.approved(client, QStringLiteral("Leaver"));
        client.send(rpc(2, QStringLiteral("tools/call"), {{QStringLiteral("name"), QStringLiteral("ask")}}));
        REQUIRE(gs::test::waitUntil([&setup] { return !setup.server.confirmation().isEmpty(); }));
    }
    CHECK(gs::test::waitUntil([&setup] { return setup.server.confirmation().isEmpty(); }));
    CHECK(setup.tools.cancelled.size() == 1);
    CHECK(setup.tools.answers.empty());

    McpClient canceller(setup.pipe);
    setup.approved(canceller, QStringLiteral("Canceller"));
    canceller.send(rpc(5, QStringLiteral("tools/call"), {{QStringLiteral("name"), QStringLiteral("ask")}}));
    REQUIRE(gs::test::waitUntil([&setup] { return !setup.server.confirmation().isEmpty(); }));
    canceller.send(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                               {QStringLiteral("method"), QStringLiteral("notifications/cancelled")},
                               {QStringLiteral("params"), QJsonObject{{QStringLiteral("requestId"), 5}}}});
    CHECK(gs::test::waitUntil([&setup] { return setup.server.confirmation().isEmpty(); }));
    CHECK(setup.tools.cancelled.size() == 2);
    CHECK(canceller.ask(rpc(6, QStringLiteral("ping"))).value(QStringLiteral("id")).toInt() == 6);
}

TEST_CASE("runLocal runs a tool as a named client without the pipe or approval, and its questions still ask the user")
{
    Setup setup;
    CHECK_FALSE(setup.server.listening());
    CHECK(setup.server.localTools() == setup.tools.tools());

    std::optional<McpOutcome> echoed;
    setup.server.runLocal(QStringLiteral("Assistant"), QStringLiteral("echo"), {{QStringLiteral("text"), QStringLiteral("hi")}},
                          [&echoed](McpOutcome outcome) { echoed = std::move(outcome); });
    REQUIRE(echoed);
    CHECK_FALSE(echoed->failed);
    CHECK(echoed->content.at(0).toObject().value(QStringLiteral("text")).toString() == u"Assistant:hi");
    CHECK(setup.server.approval().isEmpty());

    std::optional<McpOutcome> unknown;
    setup.server.runLocal(QStringLiteral("Assistant"), QStringLiteral("nope"), {},
                          [&unknown](McpOutcome outcome) { unknown = std::move(outcome); });
    REQUIRE(unknown);
    CHECK(unknown->failed);
    CHECK(unknown->code == u"GS-MCP-003");

    std::optional<McpOutcome> asked;
    setup.server.runLocal(QStringLiteral("Assistant"), QStringLiteral("ask"), {},
                          [&asked](McpOutcome outcome) { asked = std::move(outcome); });
    const QVariantMap shown = setup.server.confirmation();
    REQUIRE_FALSE(shown.isEmpty());
    CHECK(shown.value(QStringLiteral("client")).toString() == u"Assistant");
    CHECK_FALSE(asked);
    setup.server.answer(shown.value(QStringLiteral("id")).toULongLong(), true, true);
    REQUIRE(asked);
    CHECK_FALSE(asked->failed);
    REQUIRE(setup.tools.answers.size() == 1);
    CHECK(setup.tools.answers.front() == McpAnswer::AllowedForAWhile);
}

TEST_CASE("revoking a client throws away its drafts and what the tools keep about it")
{
    Setup setup;
    const std::filesystem::path kept = setup.drafts.clientFolder(QStringLiteral("Remembered"));
    std::filesystem::create_directories(kept / "profile-shooter");
    std::ofstream(kept / "profile-shooter" / "logic.ts") << "log.info(1);\n";
    const std::filesystem::path other = setup.drafts.clientFolder(QStringLiteral("Other"));
    std::filesystem::create_directories(other);
    int changed = 0;
    QObject::connect(&setup.drafts, &McpDrafts::draftsChanged, [&changed](const QString& client) {
        changed += client == u"Remembered" ? 1 : 0;
    });
    setup.server.revoke(QStringLiteral("Remembered"));
    CHECK_FALSE(std::filesystem::exists(kept));
    CHECK(std::filesystem::exists(other));
    CHECK(changed == 1);
    CHECK(setup.tools.forgotten == std::vector<QString>{QStringLiteral("Remembered")});
}

TEST_CASE("the configuration for AI clients starts gs.exe mcp")
{
    Setup setup;
    const QJsonObject root = QJsonDocument::fromJson(setup.server.configuration().toUtf8()).object();
    const QJsonObject server =
        root.value(QStringLiteral("mcpServers")).toObject().value(QStringLiteral("gamescrcpy")).toObject();
    CHECK(server.value(QStringLiteral("command")).toString().endsWith(QStringLiteral("\\gs.exe")));
    CHECK(server.value(QStringLiteral("args")).toArray() == QJsonArray{QStringLiteral("mcp")});
    CHECK(server.value(QStringLiteral("env")).toObject().value(QStringLiteral("GS_MCP_PIPE")).toString() == setup.pipe);
}

TEST_CASE("prompts and recipes: the prompts list the jobs, a prompt carries the goal, and recipes are resources")
{
    Setup setup;
    setup.turn(true);
    McpClient client(setup.pipe);
    const QJsonObject started = setup.approved(client, QStringLiteral("Guide reader"));
    const QJsonObject capabilities =
        started.value(QStringLiteral("result")).toObject().value(QStringLiteral("capabilities")).toObject();
    CHECK(capabilities.contains(QStringLiteral("prompts")));
    CHECK(capabilities.contains(QStringLiteral("resources")));

    const QJsonArray prompts = client.ask(rpc(2, QStringLiteral("prompts/list")))
                                   .value(QStringLiteral("result"))
                                   .toObject()
                                   .value(QStringLiteral("prompts"))
                                   .toArray();
    REQUIRE(prompts.size() == 5);
    CHECK(prompts.at(0).toObject().value(QStringLiteral("name")).toString() == u"make_keymap");

    const QJsonObject prompt =
        client.ask(rpc(3, QStringLiteral("prompts/get"),
                       {{QStringLiteral("name"), QStringLiteral("vision_logic")},
                        {QStringLiteral("arguments"),
                         QJsonObject{{QStringLiteral("goal"), QStringLiteral("tap the chest")}}}}));
    const QString text = prompt.value(QStringLiteral("result"))
                             .toObject()
                             .value(QStringLiteral("messages"))
                             .toArray()
                             .at(0)
                             .toObject()
                             .value(QStringLiteral("content"))
                             .toObject()
                             .value(QStringLiteral("text"))
                             .toString();
    CHECK(text.contains(QStringLiteral("tap the chest")));
    CHECK(text.contains(QStringLiteral("api_reference")));
    const QJsonObject missing =
        client.ask(rpc(4, QStringLiteral("prompts/get"), {{QStringLiteral("name"), QStringLiteral("nope")}}));
    CHECK(errorCode(missing) == -32602);

    const QJsonArray resources = client.ask(rpc(5, QStringLiteral("resources/list")))
                                     .value(QStringLiteral("result"))
                                     .toObject()
                                     .value(QStringLiteral("resources"))
                                     .toArray();
    REQUIRE(resources.size() == 8);
    const QJsonObject read =
        client.ask(rpc(6, QStringLiteral("resources/read"),
                       {{QStringLiteral("uri"), QStringLiteral("gamescrcpy://recipes/toggle-loop")}}));
    CHECK(read.value(QStringLiteral("result"))
              .toObject()
              .value(QStringLiteral("contents"))
              .toArray()
              .at(0)
              .toObject()
              .value(QStringLiteral("text"))
              .toString()
              .contains(QStringLiteral("again: \"parallel\"")));
    const QJsonObject unknown =
        client.ask(rpc(7, QStringLiteral("resources/read"), {{QStringLiteral("uri"), QStringLiteral("gamescrcpy://nope")}}));
    CHECK(errorCode(unknown) == -32602);
}
