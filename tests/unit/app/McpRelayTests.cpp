#include "AppSettings.h"
#include "McpDrafts.h"
#include "McpServer.h"
#include "McpTestSupport.h"

#include <doctest/doctest.h>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>

using gs::app::McpCall;
using gs::app::McpOutcome;
using gs::test::hello;
using gs::test::rpc;
using gs::test::waitUntil;

namespace {

class EchoTools final : public gs::app::McpToolHost
{
public:
    [[nodiscard]] QJsonArray tools() const override
    {
        return {QJsonObject{{QStringLiteral("name"), QStringLiteral("echo")},
                            {QStringLiteral("description"), QStringLiteral("Echoes")},
                            {QStringLiteral("inputSchema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}}}};
    }

    void call(const McpCall& call, Reply reply) override
    {
        reply(McpOutcome{.content = QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                           {QStringLiteral("text"), call.client}}}});
    }

    void cancel(quint64) override {}
};

void startRelay(QProcess& relay, const QString& pipe)
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("GS_MCP_PIPE"), pipe);
    relay.setProcessEnvironment(environment);
    relay.setProgram(QString::fromUtf8(GS_CLI_PROGRAM));
    relay.setArguments({QStringLiteral("mcp")});
    relay.start();
    REQUIRE(relay.waitForStarted(20000));
}

QByteArray lineOf(const QJsonObject& message)
{
    return QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
}

QJsonObject nextReply(QProcess& relay, QByteArray& buffer)
{
    const bool arrived = waitUntil([&relay, &buffer] {
        buffer += relay.readAllStandardOutput();
        return buffer.contains('\n');
    });
    if (!arrived) {
        return {};
    }
    const qsizetype newline = buffer.indexOf('\n');
    const QJsonObject reply = QJsonDocument::fromJson(buffer.left(newline)).object();
    buffer.remove(0, newline + 1);
    return reply;
}

bool ended(QProcess& relay)
{
    return waitUntil([&relay] { return relay.state() == QProcess::NotRunning; });
}

}

TEST_CASE("gs mcp answers initialize with an error asking to turn the setting on when GameScrcpy does not listen")
{
    const gs::test::McpApplication application;
    QProcess relay;
    startRelay(relay, gs::test::uniquePipe());
    relay.write(lineOf(hello(7, QStringLiteral("Test client"))));
    REQUIRE(ended(relay));
    CHECK(relay.exitCode() == 1);
    const QJsonObject reply = QJsonDocument::fromJson(relay.readAllStandardOutput().trimmed()).object();
    CHECK(reply.value(QStringLiteral("id")).toInt() == 7);
    const QJsonObject error = reply.value(QStringLiteral("error")).toObject();
    CHECK(error.value(QStringLiteral("code")).toInt() == gs::app::McpServer::kRefused);
    CHECK(error.value(QStringLiteral("message")).toString().contains(QStringLiteral("Allow AI to connect through MCP")));
    CHECK(relay.readAllStandardError().contains("not accepting AI connections"));
}

TEST_CASE("gs mcp carries lines both ways unchanged and ends with its input once every request is answered")
{
    const gs::test::McpApplication application;
    const QString pipe = gs::test::uniquePipe();
    QLocalServer program;
    program.setSocketOptions(QLocalServer::UserAccessOption);
    REQUIRE(program.listen(pipe));
    QProcess relay;
    startRelay(relay, pipe);
    REQUIRE(waitUntil([&program] { return program.hasPendingConnections(); }));
    QLocalSocket* side = program.nextPendingConnection();
    REQUIRE(side != nullptr);

    const QByteArray request = lineOf(hello(1, QStringLiteral("Test client")));
    relay.write(request);
    QByteArray received;
    REQUIRE(waitUntil([&side, &received] {
        received += side->readAll();
        return received.endsWith('\n');
    }));
    CHECK(received == request);

    const QByteArray answer = "{\"id\":1,\"jsonrpc\":\"2.0\",\"result\":{\"tools\":[]}}\n";
    side->write(answer);
    QByteArray shown;
    REQUIRE(waitUntil([&relay, &shown] {
        shown += relay.readAllStandardOutput();
        return shown.endsWith('\n');
    }));
    CHECK(shown == answer);

    const QByteArray last = lineOf(rpc(2, QStringLiteral("ping")));
    relay.write(last);
    relay.closeWriteChannel();
    received.clear();
    REQUIRE(waitUntil([&side, &received] {
        received += side->readAll();
        return received.endsWith('\n');
    }));
    CHECK(received == last);
    CHECK(relay.state() == QProcess::Running);
    side->write("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/tools/list_changed\"}\n");
    shown.clear();
    REQUIRE(waitUntil([&relay, &shown] {
        shown += relay.readAllStandardOutput();
        return shown.endsWith('\n');
    }));
    CHECK(shown.contains("notifications/tools/list_changed"));
    CHECK(relay.state() == QProcess::Running);
    side->write("{\"id\":2,\"jsonrpc\":\"2.0\",\"result\":{}}\n");
    REQUIRE(ended(relay));
    CHECK(relay.exitCode() == 0);
    CHECK(relay.readAllStandardOutput().contains("\"id\":2"));
}

TEST_CASE("gs mcp ends with an error when GameScrcpy closes the connection")
{
    const gs::test::McpApplication application;
    const QString pipe = gs::test::uniquePipe();
    QLocalServer program;
    program.setSocketOptions(QLocalServer::UserAccessOption);
    REQUIRE(program.listen(pipe));
    QProcess relay;
    startRelay(relay, pipe);
    REQUIRE(waitUntil([&program] { return program.hasPendingConnections(); }));
    QLocalSocket* side = program.nextPendingConnection();
    REQUIRE(side != nullptr);
    side->abort();
    REQUIRE(ended(relay));
    CHECK(relay.exitCode() == 1);
    CHECK(relay.readAllStandardError().contains("GameScrcpy closed the connection"));
}

TEST_CASE("an AI client reaches GameScrcpy's tools through gs mcp once the user allows it")
{
    const gs::test::McpApplication application;
    const gs::test::McpFolder folder;
    gs::app::AppSettings settings(folder.path());
    EchoTools tools;
    const gs::ManualClock clock;
    gs::app::McpDrafts drafts({.root = folder.path() / "drafts"});
    const QString pipe = gs::test::uniquePipe();
    gs::app::McpServer server(settings, tools, drafts, clock, pipe);
    REQUIRE(settings.setValue(QStringLiteral("mcp.enabled"), true));

    QProcess relay;
    startRelay(relay, pipe);
    QByteArray buffer;
    relay.write(lineOf(hello(1, QStringLiteral("Relay client"))));
    const QJsonObject started = nextReply(relay, buffer);
    CHECK(started.value(QStringLiteral("result")).toObject().value(QStringLiteral("serverInfo")).toObject()
              .value(QStringLiteral("name")).toString() == u"GameScrcpy");
    CHECK(server.approval() == u"Relay client");

    relay.write(lineOf(rpc(2, QStringLiteral("tools/list"))));
    CHECK(nextReply(relay, buffer).value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString()
              .startsWith(QStringLiteral("GS-MCP-001")));

    server.approve(QStringLiteral("Relay client"), true, false);
    CHECK(nextReply(relay, buffer).value(QStringLiteral("method")).toString() == u"notifications/tools/list_changed");
    relay.write(lineOf(rpc(3, QStringLiteral("tools/call"), {{QStringLiteral("name"), QStringLiteral("echo")}})));
    const QJsonObject called = nextReply(relay, buffer);
    CHECK(called.value(QStringLiteral("id")).toInt() == 3);
    CHECK(called.value(QStringLiteral("result")).toObject().value(QStringLiteral("content")).toArray().at(0).toObject()
              .value(QStringLiteral("text")).toString() == u"Relay client");

    relay.closeWriteChannel();
    REQUIRE(ended(relay));
    CHECK(relay.exitCode() == 0);
    CHECK(waitUntil([&server] { return server.connected().isEmpty(); }));
}
