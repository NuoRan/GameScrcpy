#include "LocalServers.h"

#include <doctest/doctest.h>

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVariantList>

#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using gs::app::LocalReplies;
using gs::app::LocalServers;
using gs::app::LogicCaller;

namespace {

class Application
{
public:
    Application()
    {
        if (QCoreApplication::instance() == nullptr) {
            m_app = std::make_unique<QCoreApplication>(m_argc, m_argv.data());
        }
    }

private:
    int m_argc = 1;
    char m_name[5] = "test";
    std::array<char*, 2> m_argv{m_name, nullptr};
    std::unique_ptr<QCoreApplication> m_app;
};

struct Told {
    QString kind;
    QString id;
    QJsonObject value;
};

const LogicCaller kOwn{.logic = 1};
const LogicCaller kDeck{.logic = 2, .extension = QStringLiteral("com.example.deck")};

bool pumpUntil(const std::function<bool()>& done)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

struct Fixture {
    Application app;
    LocalServers servers{nullptr};
    std::vector<std::pair<quint64, QJsonObject>> answers;
    std::vector<std::pair<quint64, QString>> refusals;
    std::vector<Told> told;

    Fixture()
    {
        servers.setReplies(LocalReplies{
            .answer = [this](std::uint64_t, quint64 request,
                             const QString& json) { answers.emplace_back(request, QJsonDocument::fromJson(json.toUtf8()).object()); },
            .refuse = [this](std::uint64_t, quint64 request,
                             const QString& message) { refusals.emplace_back(request, message); },
            .tell = [this](std::uint64_t, const QString& kind, const QString& id, const QString& json) {
                told.push_back({kind, id, QJsonDocument::fromJson(json.toUtf8()).object()});
            },
        });
        servers.setLimits(std::chrono::milliseconds(600), std::chrono::milliseconds(600));
    }

    static quint16 freePort()
    {
        QTcpServer probe;
        REQUIRE(probe.listen(QHostAddress::LocalHost, 0));
        return probe.serverPort();
    }

    QString listen(const LogicCaller& caller, quint64 id, quint16 port, const QString& origins = QStringLiteral("[]"))
    {
        servers.request(caller, "net.listen", id, QStringLiteral(R"({"port":%1,"origins":%2})").arg(port).arg(origins));
        REQUIRE_FALSE(answers.empty());
        REQUIRE(answers.back().first == id);
        return answers.back().second.value(QStringLiteral("handle")).toString();
    }

    void respond(const LogicCaller& caller, const QString& handle, const QJsonObject& event, int status,
                 const QByteArray& body, const QString& headers = QStringLiteral("{}"))
    {
        const gs::script::ServiceAnswer said = servers.call(
            caller, "net.respond",
            QStringLiteral(R"({"handle":"%1","request":%2,"status":%3,"headers":%4,"body":"%5"})")
                .arg(handle)
                .arg(event.value(QStringLiteral("request")).toInt())
                .arg(status)
                .arg(headers)
                .arg(QString::fromLatin1(body.toBase64()))
                .toStdString());
        CHECK(said.code.empty());
    }
};

QByteArray exchange(quint16 port, const QByteArray& request, const std::function<void()>& meanwhile = {})
{
    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, port);
    REQUIRE(pumpUntil([&client] { return client.state() == QAbstractSocket::ConnectedState; }));
    client.write(request);
    QByteArray reply;
    const bool closed = pumpUntil([&] {
        if (meanwhile) {
            meanwhile();
        }
        reply += client.readAll();
        return client.state() == QAbstractSocket::UnconnectedState;
    });
    CHECK(closed);
    reply += client.readAll();
    return reply;
}

QByteArray hostOf(quint16 port)
{
    return "Host: 127.0.0.1:" + QByteArray::number(port) + "\r\n";
}

}

TEST_CASE("ADR-0112: a logic answers HTTP requests on 127.0.0.1, and its answer goes back to the program that asked")
{
    Fixture fixture;
    const quint16 port = Fixture::freePort();
    const QString handle = fixture.listen(kOwn, 1, port);
    CHECK(fixture.servers.ports().value(QStringLiteral("mine")).toList() == QVariantList{static_cast<int>(port)});
    bool answered = false;
    const QByteArray reply = exchange(
        port,
        "POST /hello%20there?name=Ann&x=1 HTTP/1.1\r\n" + hostOf(port) + "X-Thing: a\r\nContent-Length: 5\r\n\r\nhowdy",
        [&] {
            if (answered || fixture.told.empty()) {
                return;
            }
            answered = true;
            const Told& event = fixture.told.back();
            CHECK(event.kind == "net");
            CHECK(event.id == handle);
            CHECK(event.value.value(QStringLiteral("type")).toString() == "request");
            CHECK(event.value.value(QStringLiteral("method")).toString() == "POST");
            CHECK(event.value.value(QStringLiteral("path")).toString() == "/hello there");
            CHECK(event.value.value(QStringLiteral("query")).toObject().value(QStringLiteral("name")).toString() == "Ann");
            CHECK(event.value.value(QStringLiteral("headers")).toObject().value(QStringLiteral("x-thing")).toString() ==
                  "a");
            CHECK(QByteArray::fromBase64(event.value.value(QStringLiteral("body")).toString().toLatin1()) == "howdy");
            fixture.respond(kOwn, handle, event.value, 201, "made", QStringLiteral(R"({"X-Answer":"yes"})"));
        });
    CHECK(reply.startsWith("HTTP/1.1 201 Created\r\n"));
    CHECK(reply.contains("X-Answer: yes\r\n"));
    CHECK(reply.contains("Content-Length: 4\r\n"));
    CHECK(reply.endsWith("\r\n\r\nmade"));
}

TEST_CASE("ADR-0112: a local server refuses other hosts and other web pages, and answers the preflight of the pages it "
          "allows")
{
    Fixture fixture;
    const quint16 port = Fixture::freePort();
    const QString handle = fixture.listen(kOwn, 1, port, QStringLiteral(R"(["https://Deck.Example/"])"));
    CHECK(exchange(port, "GET / HTTP/1.1\r\nHost: evil.example\r\n\r\n").startsWith("HTTP/1.1 403 "));
    CHECK(exchange(port, "GET / HTTP/1.1\r\n" + hostOf(port) + "Origin: https://evil.example\r\n\r\n")
              .startsWith("HTTP/1.1 403 "));
    CHECK(fixture.told.empty());

    const QByteArray preflight = exchange(port, "OPTIONS /press HTTP/1.1\r\n" + hostOf(port) +
                                                    "Origin: https://deck.example\r\n"
                                                    "Access-Control-Request-Headers: content-type\r\n\r\n");
    CHECK(preflight.startsWith("HTTP/1.1 204 No Content\r\n"));
    CHECK(preflight.contains("Access-Control-Allow-Origin: https://deck.example\r\n"));
    CHECK(preflight.contains("Access-Control-Allow-Headers: content-type\r\n"));
    CHECK(fixture.told.empty());

    bool answered = false;
    const QByteArray allowed =
        exchange(port, "GET /state HTTP/1.1\r\nhost: LOCALHOST:" + QByteArray::number(port) +
                           "\r\nOrigin: https://deck.example\r\n\r\n",
                 [&] {
                     if (!answered && !fixture.told.empty()) {
                         answered = true;
                         fixture.respond(kOwn, handle, fixture.told.back().value, 200, "{}");
                     }
                 });
    CHECK(allowed.startsWith("HTTP/1.1 200 OK\r\n"));
    CHECK(allowed.contains("Access-Control-Allow-Origin: https://deck.example\r\n"));

    answered = false;
    const QByteArray same = exchange(port,
                                     "HEAD / HTTP/1.1\r\n" + hostOf(port) + "Origin: http://127.0.0.1:" +
                                         QByteArray::number(port) + "\r\n\r\n",
                                     [&] {
                                         if (!answered && fixture.told.size() == 2) {
                                             answered = true;
                                             fixture.respond(kOwn, handle, fixture.told.back().value, 200, "body");
                                         }
                                     });
    CHECK(same.startsWith("HTTP/1.1 200 OK\r\n"));
    CHECK_FALSE(same.contains("Access-Control-Allow-Origin"));
    CHECK(same.contains("Content-Length: 4\r\n"));
    CHECK(same.endsWith("\r\n\r\n"));
}

TEST_CASE("ADR-0112: a local server refuses pages whose origin is null, which any site can give a sandboxed frame")
{
    Fixture fixture;
    const quint16 port = Fixture::freePort();
    static_cast<void>(fixture.listen(kOwn, 1, port));
    const QByteArray pressed =
        exchange(port, "POST /tap/k1 HTTP/1.1\r\n" + hostOf(port) + "Origin: null\r\nContent-Length: 0\r\n\r\n");
    CHECK(pressed.startsWith("HTTP/1.1 403 "));
    CHECK_FALSE(pressed.contains("Access-Control-Allow-Origin"));
    const QByteArray preflight = exchange(port, "OPTIONS /tap/k1 HTTP/1.1\r\n" + hostOf(port) +
                                                    "Origin: null\r\n"
                                                    "Access-Control-Request-Headers: content-type\r\n\r\n");
    CHECK(preflight.startsWith("HTTP/1.1 403 "));
    CHECK(fixture.told.empty());
}

TEST_CASE("ADR-0112: a local server limits what it reads and how long it waits for the logic")
{
    Fixture fixture;
    const quint16 port = Fixture::freePort();
    static_cast<void>(fixture.listen(kOwn, 1, port));
    CHECK(exchange(port, "GET / HTTP/1.1\r\n" + hostOf(port) + "X-Long: " + QByteArray(17 * 1024, 'a') + "\r\n\r\n")
              .startsWith("HTTP/1.1 431 "));
    CHECK(exchange(port, "POST / HTTP/1.1\r\n" + hostOf(port) + "Content-Length: 2000000\r\n\r\n")
              .startsWith("HTTP/1.1 413 "));
    CHECK(exchange(port, "POST / HTTP/1.1\r\n" + hostOf(port) + "Transfer-Encoding: chunked\r\n\r\n")
              .startsWith("HTTP/1.1 501 "));
    CHECK(exchange(port, "GET http://elsewhere/ HTTP/1.1\r\n" + hostOf(port) + "\r\n").startsWith("HTTP/1.1 400 "));
    CHECK(fixture.told.empty());
    CHECK(exchange(port, "GET / HTTP/1.1\r\n" + hostOf(port)).startsWith("HTTP/1.1 408 "));
    CHECK(exchange(port, "GET /slow HTTP/1.1\r\n" + hostOf(port) + "\r\n").startsWith("HTTP/1.1 504 "));
    CHECK(fixture.told.size() == 1);
}

TEST_CASE("ADR-0112: listening needs the permission and a free port, at most four for an extension, and stops with the "
          "permission or the logic")
{
    Fixture fixture;
    const quint16 port = Fixture::freePort();
    fixture.servers.request(kDeck, "net.listen", 1, QStringLiteral(R"({"port":%1})").arg(port));
    REQUIRE(fixture.refusals.size() == 1);
    CHECK(fixture.refusals.back().second.startsWith(QStringLiteral("GS-API-100 ")));
    CHECK(fixture.servers.call(kDeck, "net.respond", R"({"handle":"n1","request":1})").code == "GS-API-100");

    fixture.servers.setPermitted({QStringLiteral("com.example.deck")});
    const QString handle = fixture.listen(kDeck, 2, port);
    QTcpServer taken;
    REQUIRE(taken.listen(QHostAddress::LocalHost, 0));
    fixture.servers.request(kDeck, "net.listen", 3, QStringLiteral(R"({"port":%1})").arg(taken.serverPort()));
    CHECK(fixture.refusals.back().second.startsWith(QStringLiteral("GS-API-102 ")));
    fixture.servers.request(kDeck, "net.listen", 4, QStringLiteral(R"({"port":80})"));
    CHECK(fixture.refusals.back().second.startsWith(QStringLiteral("GS-API-101 ")));
    fixture.servers.request(kDeck, "net.listen", 5,
                            QStringLiteral(R"({"port":%1,"origins":["ftp://deck"]})").arg(Fixture::freePort()));
    CHECK(fixture.refusals.back().second.startsWith(QStringLiteral("GS-API-101 ")));
    for (quint64 id = 6; id < 9; ++id) {
        static_cast<void>(fixture.listen(kDeck, id, Fixture::freePort()));
    }
    fixture.servers.request(kDeck, "net.listen", 9, QStringLiteral(R"({"port":%1})").arg(Fixture::freePort()));
    CHECK(fixture.refusals.back().second.startsWith(QStringLiteral("GS-API-102 ")));
    CHECK(fixture.servers.ports().value(QStringLiteral("com.example.deck")).toList().size() == 4);
    CHECK(fixture.servers.call(kDeck, "net.respond", R"({"handle":"n1","request":1,"status":999})").code ==
          "GS-API-101");
    CHECK(fixture.servers.call(kDeck, "net.respond", R"({"handle":"n1","request":1,"headers":{"Bad Name":"x"}})")
              .code == "GS-API-101");

    fixture.servers.setPermitted({});
    CHECK(fixture.servers.ports().isEmpty());
    REQUIRE(fixture.told.size() == 4);
    CHECK(fixture.told.front().id == handle);
    CHECK(fixture.told.front().value.value(QStringLiteral("type")).toString() == "closed");
    CHECK(fixture.told.front().value.value(QStringLiteral("reason")).toString() == "forbidden");
    QTcpSocket refused;
    refused.connectToHost(QHostAddress::LocalHost, port);
    CHECK(pumpUntil([&refused] { return refused.state() == QAbstractSocket::UnconnectedState; }));

    static_cast<void>(fixture.listen(kOwn, 10, port));
    CHECK(fixture.servers.ports().value(QStringLiteral("mine")).toList().size() == 1);
    fixture.servers.endLogic(kOwn.logic);
    CHECK(fixture.servers.ports().isEmpty());
    CHECK(fixture.told.size() == 4);
}
