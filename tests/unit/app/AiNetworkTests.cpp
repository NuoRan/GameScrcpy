#include "AiNetwork.h"
#include "McpTestSupport.h"

#include <doctest/doctest.h>

#include <QByteArray>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <utility>

using gs::app::AiConnection;
using gs::app::AiKind;
using gs::app::AiProvider;
using gs::app::AiReply;
using gs::app::AiRequest;
using gs::app::AiTurn;
using namespace Qt::StringLiterals;

namespace {

class FakeService final : public QObject
{
public:
    using Answer = std::function<void(QTcpSocket& socket)>;

    FakeService()
    {
        listening = m_server.listen(QHostAddress::LocalHost, 0);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read(*socket); });
            }
        });
    }

    [[nodiscard]] QUrl url(const QString& path) const
    {
        return QUrl(u"http://127.0.0.1:%1%2"_s.arg(m_server.serverPort()).arg(path));
    }

    bool listening = false;
    Answer answer;
    QByteArray head;
    QByteArray body;
    int requests = 0;

private:
    void read(QTcpSocket& socket)
    {
        QByteArray& buffer = m_buffers[&socket];
        buffer += socket.readAll();
        const qsizetype end = buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            return;
        }
        const QByteArray header = buffer.left(end);
        qsizetype length = 0;
        for (const QByteArray& line : header.split('\n')) {
            const QByteArray field = line.trimmed();
            if (field.toLower().startsWith("content-length:")) {
                length = field.mid(15).trimmed().toLongLong();
            }
        }
        if (buffer.size() < end + 4 + length) {
            return;
        }
        head = header;
        body = buffer.mid(end + 4, length);
        buffer.clear();
        ++requests;
        if (answer) {
            answer(socket);
        }
    }

    QTcpServer m_server;
    std::map<QTcpSocket*, QByteArray> m_buffers;
};

void respond(QTcpSocket& socket, const QByteArray& status, const QByteArray& type, const QByteArray& content)
{
    socket.write("HTTP/1.1 " + status + "\r\nContent-Type: " + type +
                 "\r\nContent-Length: " + QByteArray::number(content.size()) + "\r\nConnection: close\r\n\r\n" +
                 content);
    socket.flush();
    socket.disconnectFromHost();
}

struct Outcome {
    QStringList deltas;
    std::optional<AiReply> reply;
};

AiProvider::Handlers collect(Outcome& outcome)
{
    return AiProvider::Handlers{.text = [&outcome](const QString& delta) { outcome.deltas.append(delta); },
                                .done = [&outcome](AiReply reply) { outcome.reply = std::move(reply); }};
}

AiRequest hello(bool stream)
{
    AiRequest request;
    request.stream = stream;
    request.turns.push_back(AiTurn{.role = AiTurn::Role::User, .text = u"Hi"_s});
    return request;
}

struct Network {
    gs::test::McpApplication application;
    QNetworkAccessManager manager;

    Network() { manager.setProxy(QNetworkProxy(QNetworkProxy::NoProxy)); }
};

}

TEST_CASE("the provider posts to the service with the key and streams the answer back")
{
    Network network;
    FakeService service;
    REQUIRE(service.listening);
    service.answer = [](QTcpSocket& socket) {
        respond(socket, "200 OK", "text/event-stream",
                "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Hel\"}}]}\n\n"
                "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"lo\"}}]}\n\n"
                "data: {\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
                "data: [DONE]\n\n");
    };
    const auto provider = gs::app::makeAiProvider(network.manager,
                                                  AiConnection{.kind = AiKind::OpenAi,
                                                               .url = service.url(u"/v1/chat/completions"_s),
                                                               .model = u"test-model"_s,
                                                               .key = u"sk-test"_s});
    Outcome outcome;
    provider->send(hello(true), collect(outcome));
    REQUIRE(gs::test::waitUntil([&outcome] { return outcome.reply.has_value(); }));
    CHECK(outcome.reply->problem.isEmpty());
    CHECK(outcome.reply->status == 200);
    CHECK(outcome.reply->text == u"Hello");
    CHECK(outcome.deltas.join(QString()) == u"Hello");
    CHECK(service.head.startsWith("POST /v1/chat/completions "));
    CHECK(service.head.contains("Bearer sk-test"));
    const QJsonObject sent = QJsonDocument::fromJson(service.body).object();
    CHECK(sent.value(u"model"_s).toString() == u"test-model");
    CHECK(sent.value(u"stream"_s).toBool());
}

TEST_CASE("an HTTP error from the service becomes its own sentence")
{
    Network network;
    FakeService service;
    REQUIRE(service.listening);
    service.answer = [](QTcpSocket& socket) {
        respond(socket, "401 Unauthorized", "application/json",
                R"({"error": {"message": "Incorrect API key provided", "code": "invalid_api_key"}})");
    };
    const auto provider = gs::app::makeAiProvider(
        network.manager,
        AiConnection{.kind = AiKind::OpenAi, .url = service.url(u"/v1/chat/completions"_s), .model = u"m"_s});
    Outcome outcome;
    provider->send(hello(true), collect(outcome));
    REQUIRE(gs::test::waitUntil([&outcome] { return outcome.reply.has_value(); }));
    CHECK(outcome.reply->status == 401);
    CHECK(outcome.reply->problem.contains(u"HTTP 401"));
    CHECK(outcome.reply->problem.contains(u"Incorrect API key provided"));
    CHECK(outcome.reply->code == u"invalid_api_key");
    CHECK_FALSE(service.head.toLower().contains("authorization"));
}

TEST_CASE("a whole answer is read when the service does not stream, with the Anthropic headers")
{
    Network network;
    FakeService service;
    REQUIRE(service.listening);
    service.answer = [](QTcpSocket& socket) {
        respond(socket, "200 OK", "application/json",
                R"({"type": "message", "content": [{"type": "text", "text": "OK"}], "stop_reason": "end_turn",
                    "usage": {"input_tokens": 5, "output_tokens": 1}})");
    };
    const auto provider = gs::app::makeAiProvider(network.manager,
                                                  AiConnection{.kind = AiKind::Anthropic,
                                                               .url = service.url(u"/v1/messages"_s),
                                                               .model = u"claude-test"_s,
                                                               .key = u"sk-ant"_s});
    Outcome outcome;
    provider->send(hello(false), collect(outcome));
    REQUIRE(gs::test::waitUntil([&outcome] { return outcome.reply.has_value(); }));
    CHECK(outcome.reply->problem.isEmpty());
    CHECK(outcome.reply->text == u"OK");
    CHECK(outcome.reply->usage.input == 5);
    CHECK(outcome.deltas.isEmpty());
    CHECK(service.head.toLower().contains("x-api-key: sk-ant"));
    CHECK(service.head.toLower().contains("anthropic-version: 2023-06-01"));
    const QJsonObject sent = QJsonDocument::fromJson(service.body).object();
    CHECK(sent.value(u"max_tokens"_s).toInt() > 0);
    CHECK_FALSE(sent.value(u"stream"_s).toBool());
}

TEST_CASE("a refused connection names the address, and a cancelled request never calls back")
{
    Network network;
    quint16 port = 0;
    {
        QTcpServer closed;
        REQUIRE(closed.listen(QHostAddress::LocalHost, 0));
        port = closed.serverPort();
    }
    const auto refused = gs::app::makeAiProvider(
        network.manager,
        AiConnection{.kind = AiKind::OpenAi,
                     .url = QUrl(u"http://127.0.0.1:%1/v1/chat/completions"_s.arg(port)),
                     .model = u"m"_s});
    Outcome outcome;
    refused->send(hello(true), collect(outcome));
    REQUIRE(gs::test::waitUntil([&outcome] { return outcome.reply.has_value(); }));
    CHECK(outcome.reply->status == 0);
    CHECK(outcome.reply->problem.contains(u"127.0.0.1"));

    FakeService silent;
    REQUIRE(silent.listening);
    const auto waiting = gs::app::makeAiProvider(
        network.manager,
        AiConnection{.kind = AiKind::OpenAi, .url = silent.url(u"/v1/chat/completions"_s), .model = u"m"_s});
    Outcome none;
    waiting->send(hello(true), collect(none));
    REQUIRE(gs::test::waitUntil([&silent] { return silent.requests == 1; }));
    waiting->cancel();
    static_cast<void>(gs::test::waitUntil([] { return false; }, std::chrono::milliseconds(200)));
    CHECK_FALSE(none.reply.has_value());
}
