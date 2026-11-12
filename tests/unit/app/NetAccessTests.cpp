#include "NetAccess.h"
#include "WebSocketClient.h"

#include <doctest/doctest.h>

#include <QAbstractSocket>
#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using gs::app::LogicCaller;
using gs::app::NetAccess;
using gs::app::NetReplies;
using gs::app::WebSocketClient;

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
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

QByteArray response(int status, const QByteArray& reason, const QByteArray& body, const QByteArray& headers = {})
{
    QByteArray made = "HTTP/1.1 ";
    made.append(QByteArray::number(status));
    made.append(' ');
    made.append(reason);
    made.append("\r\nContent-Length: ");
    made.append(QByteArray::number(body.size()));
    made.append("\r\nConnection: close\r\n");
    made.append(headers);
    made.append("\r\n");
    made.append(body);
    return made;
}

class HttpServer
{
public:
    using Handler = std::function<QByteArray(const QByteArray& head, const QByteArray& body)>;

    explicit HttpServer(Handler handler)
        : m_handler(std::move(handler))
    {
        REQUIRE(m_server.listen(QHostAddress::LocalHost, 0));
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this] { accept(); });
    }

    [[nodiscard]] quint16 port() const { return m_server.serverPort(); }

    [[nodiscard]] QString url(const char* path) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(port()).arg(QString::fromLatin1(path));
    }

    [[nodiscard]] std::string listed() const { return "127.0.0.1:" + std::to_string(port()); }

    std::vector<QByteArray> heads;
    std::vector<QByteArray> bodies;

private:
    void accept()
    {
        while (QTcpSocket* socket = m_server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] { read(socket); });
        }
    }

    void read(QTcpSocket* socket)
    {
        QByteArray& buffer = m_buffers[socket];
        buffer.append(socket->readAll());
        const qsizetype end = buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            return;
        }
        const QByteArray head = buffer.left(end);
        qsizetype length = 0;
        for (const QByteArray& line : head.split('\n')) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.toLower().startsWith("content-length:")) {
                length = trimmed.mid(15).trimmed().toLongLong();
            }
        }
        if (buffer.size() - end - 4 < length) {
            return;
        }
        heads.push_back(head);
        bodies.push_back(buffer.mid(end + 4, length));
        buffer.clear();
        const QByteArray reply = m_handler(heads.back(), bodies.back());
        if (!reply.isEmpty()) {
            socket->write(reply);
            socket->disconnectFromHost();
        }
    }

    QTcpServer m_server;
    Handler m_handler;
    std::map<QTcpSocket*, QByteArray> m_buffers;
};

class SocketServer
{
public:
    SocketServer()
    {
        REQUIRE(m_server.listen(QHostAddress::LocalHost, 0));
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this] {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                m_peer = socket;
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] { read(socket); });
            }
        });
    }

    [[nodiscard]] quint16 port() const { return m_server.serverPort(); }

    [[nodiscard]] static QByteArray frameOf(int opcode, const QByteArray& payload, bool last = true)
    {
        QByteArray frame;
        frame.append(static_cast<char>((last ? 0x80 : 0) | opcode));
        const auto size = static_cast<quint64>(payload.size());
        if (size < 126) {
            frame.append(static_cast<char>(size));
        } else if (size <= 0xFFFF) {
            frame.append(static_cast<char>(126));
            frame.append(static_cast<char>((size >> 8) & 0xFF));
            frame.append(static_cast<char>(size & 0xFF));
        } else {
            frame.append(static_cast<char>(127));
            for (int shift = 56; shift >= 0; shift -= 8) {
                frame.append(static_cast<char>((size >> shift) & 0xFF));
            }
        }
        frame.append(payload);
        return frame;
    }

    void send(int opcode, const QByteArray& payload, bool last = true)
    {
        m_closing = m_closing || opcode == 8;
        sendRaw(frameOf(opcode, payload, last));
    }

    void sendRaw(const QByteArray& bytes)
    {
        if (m_peer != nullptr) {
            m_peer->write(bytes);
        }
    }

    void pause()
    {
        m_paused = true;
        if (m_peer != nullptr) {
            m_peer->setReadBufferSize(1);
        }
    }

    void resume()
    {
        m_paused = false;
        if (m_peer != nullptr) {
            m_peer->setReadBufferSize(0);
            read(m_peer);
        }
    }

    QByteArray head;
    std::vector<std::pair<int, QByteArray>> frames;
    bool masked = true;
    bool wrongAccept = false;

private:
    void read(QTcpSocket* socket)
    {
        if (m_paused) {
            return;
        }
        m_buffer.append(socket->readAll());
        if (!m_upgraded) {
            const qsizetype end = m_buffer.indexOf("\r\n\r\n");
            if (end < 0) {
                return;
            }
            head = m_buffer.left(end);
            m_buffer.remove(0, end + 4);
            QByteArray key;
            QByteArray offered;
            for (const QByteArray& line : head.split('\n')) {
                const QByteArray trimmed = line.trimmed();
                const QByteArray lower = trimmed.toLower();
                if (lower.startsWith("sec-websocket-key:")) {
                    key = trimmed.mid(18).trimmed();
                } else if (lower.startsWith("sec-websocket-protocol:")) {
                    offered = trimmed.mid(23).trimmed();
                }
            }
            QByteArray keyed = key;
            keyed.append("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
            const QByteArray accept = wrongAccept ? QByteArray("bm9wZQ==")
                                                  : QCryptographicHash::hash(keyed, QCryptographicHash::Sha1).toBase64();
            QByteArray reply = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                               "Sec-WebSocket-Accept: ";
            reply.append(accept);
            reply.append("\r\n");
            if (!offered.isEmpty()) {
                reply.append("Sec-WebSocket-Protocol: ");
                reply.append(offered.split(',').front().trimmed());
                reply.append("\r\n");
            }
            reply.append("\r\n");
            socket->write(reply);
            m_upgraded = true;
        }
        while (m_buffer.size() >= 2) {
            const auto first = static_cast<quint8>(m_buffer.at(0));
            const auto second = static_cast<quint8>(m_buffer.at(1));
            qsizetype at = 2;
            auto length = static_cast<qsizetype>(second & 0x7F);
            if (length == 126) {
                if (m_buffer.size() < 4) {
                    return;
                }
                length = (static_cast<quint8>(m_buffer.at(2)) << 8) | static_cast<quint8>(m_buffer.at(3));
                at = 4;
            } else if (length == 127) {
                if (m_buffer.size() < 10) {
                    return;
                }
                length = 0;
                for (qsizetype index = 2; index < 10; ++index) {
                    length = (length << 8) | static_cast<quint8>(m_buffer.at(index));
                }
                at = 10;
            }
            const bool hasMask = (second & 0x80) != 0;
            masked = masked && hasMask;
            const qsizetype keyAt = at;
            if (hasMask) {
                at += 4;
            }
            if (m_buffer.size() < at + length) {
                return;
            }
            QByteArray payload = m_buffer.mid(at, length);
            if (hasMask) {
                for (qsizetype index = 0; index < payload.size(); ++index) {
                    payload[index] = static_cast<char>(payload.at(index) ^ m_buffer.at(keyAt + index % 4));
                }
            }
            m_buffer.remove(0, at + length);
            const int opcode = first & 0x0F;
            frames.emplace_back(opcode, payload);
            if (opcode == 8) {
                if (!m_closing) {
                    send(8, payload.left(2));
                }
                socket->disconnectFromHost();
                return;
            }
        }
    }

    QTcpServer m_server;
    QPointer<QTcpSocket> m_peer;
    QByteArray m_buffer;
    bool m_upgraded = false;
    bool m_closing = false;
    bool m_paused = false;
};

struct Fixture {
    Application app;
    NetAccess net{nullptr};
    std::vector<std::pair<quint64, QJsonObject>> answers;
    std::vector<std::pair<quint64, QString>> refusals;
    std::vector<Told> told;

    Fixture()
    {
        net.setReplies(NetReplies{
            .answer = [this](std::uint64_t, quint64 request,
                             const QString& json) { answers.emplace_back(request, QJsonDocument::fromJson(json.toUtf8()).object()); },
            .refuse = [this](std::uint64_t, quint64 request,
                             const QString& message) { refusals.emplace_back(request, message); },
            .tell = [this](std::uint64_t, const QString& kind, const QString& id, const QString& json) {
                told.push_back({kind, id, QJsonDocument::fromJson(json.toUtf8()).object()});
            },
        });
        net.allowPlainLocal(true);
    }

    void fetch(const LogicCaller& caller, quint64 id, const QString& json) { net.request(caller, "net.fetch", id, json); }
    void open(const LogicCaller& caller, quint64 id, const QString& json) { net.request(caller, "net.socket", id, json); }

    [[nodiscard]] bool settled(quint64 id) const
    {
        return std::ranges::any_of(answers, [id](const auto& entry) { return entry.first == id; }) ||
               std::ranges::any_of(refusals, [id](const auto& entry) { return entry.first == id; });
    }

    [[nodiscard]] QJsonObject answerOf(quint64 id) const
    {
        const auto found = std::ranges::find(answers, id, &std::pair<quint64, QJsonObject>::first);
        return found == answers.end() ? QJsonObject() : found->second;
    }

    [[nodiscard]] QString refusalOf(quint64 id) const
    {
        const auto found = std::ranges::find(refusals, id, &std::pair<quint64, QString>::first);
        return found == refusals.end() ? QString() : found->second;
    }

    [[nodiscard]] bool waitFor(quint64 id)
    {
        return pumpUntil([this, id] { return settled(id); });
    }
};

QString fetchJson(const QString& url, const QString& more = {})
{
    return QStringLiteral(R"({"url":"%1"%2})").arg(url, more);
}

}

TEST_CASE("ADR-0112: net.fetch asks a server for the logic with its method, headers and body and gives back the answer")
{
    Fixture fixture;
    HttpServer server([](const QByteArray& head, const QByteArray&) {
        if (head.startsWith("GET /bytes")) {
            return response(200, "OK", QByteArray("\x00\xff", 2), "Content-Type: application/octet-stream\r\n");
        }
        return response(201, "Created", "{\"ok\":true}", "Content-Type: application/json\r\nX-Two: a\r\nX-Two: b\r\n");
    });
    CHECK(fixture.net.call(kOwn, "net.fetch.part", R"({"stage":1,"data":"aGVs"})").code.empty());
    fixture.fetch(kOwn, 1,
                  fetchJson(server.url("/post?x=1#part"),
                            QStringLiteral(R"(,"method":"POST","headers":{"X-Token":"abc","content-type":"text/plain"},)"
                                           R"("stage":1,"text":"lo")")));
    REQUIRE(fixture.waitFor(1));
    INFO(fixture.refusalOf(1).toStdString());
    const QJsonObject made = fixture.answerOf(1);
    CHECK(made.value(u"status").toInt() == 201);
    CHECK(made.value(u"text").toString() == QStringLiteral("{\"ok\":true}"));
    CHECK(made.value(u"headers").toObject().value(u"x-two").toString() == QStringLiteral("a, b"));
    CHECK(made.value(u"headers").toObject().value(u"content-type").toString() == QStringLiteral("application/json"));
    CHECK(made.value(u"url").toString() == server.url("/post?x=1"));
    REQUIRE(server.heads.size() == 1);
    CHECK(server.heads[0].startsWith("POST /post?x=1 HTTP/1.1\r\n"));
    CHECK(server.heads[0].contains("X-Token: abc"));
    CHECK(server.bodies[0] == "hello");

    fixture.fetch(kOwn, 2, fetchJson(server.url("/bytes")));
    REQUIRE(fixture.waitFor(2));
    const QJsonObject bytes = fixture.answerOf(2);
    CHECK_FALSE(bytes.contains(u"text"));
    CHECK(QByteArray::fromBase64(bytes.value(u"body").toString().toLatin1()) == QByteArray("\x00\xff", 2));
    CHECK(fixture.net.fetching() == 0);
}

TEST_CASE("ADR-0112: an extension reaches only the hosts and ports it lists, and is sent on only to them")
{
    Fixture fixture;
    HttpServer target([](const QByteArray&, const QByteArray&) { return response(200, "OK", "there"); });
    HttpServer other([](const QByteArray&, const QByteArray&) { return response(200, "OK", "other"); });
    HttpServer hop([&target, &other](const QByteArray& head, const QByteArray&) {
        const quint16 port = head.startsWith("GET /far") ? other.port() : target.port();
        return response(302, "Found", {}, "Location: http://127.0.0.1:" + QByteArray::number(port) + "/end\r\n");
    });

    fixture.fetch(kDeck, 1, fetchJson(target.url("/")));
    REQUIRE(fixture.waitFor(1));
    CHECK(fixture.refusalOf(1).startsWith(QStringLiteral("GS-API-155 ")));

    fixture.net.setHosts({{QStringLiteral("com.example.deck"), {target.listed(), hop.listed()}}});
    fixture.fetch(kDeck, 2, fetchJson(target.url("/")));
    REQUIRE(fixture.waitFor(2));
    CHECK(fixture.answerOf(2).value(u"text").toString() == QStringLiteral("there"));
    fixture.fetch(kDeck, 3, fetchJson(other.url("/")));
    REQUIRE(fixture.waitFor(3));
    CHECK(fixture.refusalOf(3).startsWith(QStringLiteral("GS-API-155 ")));
    fixture.fetch(kDeck, 4, fetchJson(hop.url("/near")));
    REQUIRE(fixture.waitFor(4));
    INFO(fixture.refusalOf(4).toStdString());
    CHECK(fixture.answerOf(4).value(u"text").toString() == QStringLiteral("there"));
    CHECK(fixture.answerOf(4).value(u"url").toString() == target.url("/end"));
    fixture.fetch(kDeck, 5, fetchJson(hop.url("/far")));
    REQUIRE(fixture.waitFor(5));
    CHECK(fixture.refusalOf(5).startsWith(QStringLiteral("GS-API-155 the server sent the request on")));
    fixture.fetch(kOwn, 6, fetchJson(hop.url("/far")));
    REQUIRE(fixture.waitFor(6));
    CHECK(fixture.answerOf(6).value(u"text").toString() == QStringLiteral("other"));

    fixture.fetch(kOwn, 7, fetchJson(QStringLiteral("ftp://127.0.0.1/")));
    fixture.fetch(kOwn, 8, fetchJson(QStringLiteral("https://user:secret@example.com/")));
    fixture.net.allowPlainLocal(false);
    fixture.fetch(kOwn, 9, fetchJson(target.url("/")));
    for (const quint64 id : std::array<quint64, 3>{7, 8, 9}) {
        CAPTURE(id);
        CHECK(fixture.refusalOf(id).startsWith(QStringLiteral("GS-API-156 ")));
    }
}

TEST_CASE("ADR-0112: net.fetch keeps to its limits on the body, the answer, the requests at once and the time")
{
    Fixture fixture;
    HttpServer silent([](const QByteArray&, const QByteArray&) { return QByteArray(); });
    HttpServer declared([](const QByteArray&, const QByteArray&) {
        return QByteArray("HTTP/1.1 200 OK\r\nContent-Length: 9000000\r\nConnection: close\r\n\r\n");
    });
    HttpServer endless([](const QByteArray&, const QByteArray&) {
        QByteArray made = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n";
        made.append(QByteArray(9 * 1024 * 1024, 'x'));
        return made;
    });

    const std::string tooMuch = R"({"stage":2,"data":")" +
                                QByteArray((1 << 20) + 1, '\0').toBase64().toStdString() + R"("})";
    CHECK(fixture.net.call(kOwn, "net.fetch.part", tooMuch).code == "GS-API-157");
    const std::string whole = R"({"stage":3,"data":")" + QByteArray(1 << 20, '\0').toBase64().toStdString() + R"("})";
    CHECK(fixture.net.call(kOwn, "net.fetch.part", whole).code.empty());
    fixture.fetch(kOwn, 1, fetchJson(silent.url("/"), QStringLiteral(R"(,"method":"POST","stage":3,"data":"AA==")")));
    CHECK(fixture.refusalOf(1).startsWith(QStringLiteral("GS-API-157 ")));

    fixture.fetch(kOwn, 2, fetchJson(declared.url("/")));
    fixture.fetch(kOwn, 3, fetchJson(endless.url("/")));
    REQUIRE(fixture.waitFor(2));
    REQUIRE(fixture.waitFor(3));
    CHECK(fixture.refusalOf(2).startsWith(QStringLiteral("GS-API-157 ")));
    CHECK(fixture.refusalOf(3).startsWith(QStringLiteral("GS-API-157 ")));

    fixture.fetch(kOwn, 4, fetchJson(silent.url("/"), QStringLiteral(R"(,"method":"TRACE")")));
    fixture.fetch(kOwn, 5, fetchJson(silent.url("/"), QStringLiteral(R"(,"headers":{"Host":"elsewhere"})")));
    fixture.fetch(kOwn, 6, fetchJson(silent.url("/"), QStringLiteral(R"(,"timeoutMs":500)")));
    fixture.fetch(kOwn, 7, fetchJson(silent.url("/"), QStringLiteral(R"(,"text":"x")")));
    fixture.fetch(kOwn, 8, fetchJson(silent.url("/"), QStringLiteral(R"(,"method":"POST","data":"%%%")")));
    for (const quint64 id : std::array<quint64, 5>{4, 5, 6, 7, 8}) {
        CAPTURE(id);
        CHECK(fixture.refusalOf(id).startsWith(QStringLiteral("GS-API-156 ")));
    }

    for (quint64 id = 10; id < 18; ++id) {
        fixture.fetch(kOwn, id, fetchJson(silent.url("/")));
    }
    CHECK(fixture.net.fetching() == 8);
    fixture.fetch(kOwn, 18, fetchJson(silent.url("/")));
    CHECK(fixture.refusalOf(18).startsWith(QStringLiteral("GS-API-157 ")));
    fixture.net.drop(kOwn, 10);
    CHECK(fixture.net.fetching() == 7);
    fixture.net.endLogic(kOwn.logic);
    CHECK(fixture.net.fetching() == 0);
    for (quint64 id = 10; id < 18; ++id) {
        CHECK_FALSE(fixture.settled(id));
    }

    fixture.fetch(kOwn, 20, fetchJson(silent.url("/"), QStringLiteral(R"(,"timeoutMs":1000)")));
    REQUIRE(fixture.waitFor(20));
    CHECK(fixture.refusalOf(20).startsWith(QStringLiteral("GS-API-158 ")));
}

TEST_CASE("ADR-0112: net.socket speaks WebSocket to a server: masked frames, fragments, ping, pong and its close")
{
    Fixture fixture;
    SocketServer server;
    fixture.open(kOwn, 1,
                 QStringLiteral(R"({"url":"ws://127.0.0.1:%1/feed?room=1","protocols":["chat","v2"]})").arg(server.port()));
    REQUIRE(fixture.waitFor(1));
    INFO(fixture.refusalOf(1).toStdString());
    const QJsonObject made = fixture.answerOf(1);
    const QString handle = made.value(u"handle").toString();
    REQUIRE_FALSE(handle.isEmpty());
    CHECK(made.value(u"protocol").toString() == QStringLiteral("chat"));
    CHECK(server.head.startsWith("GET /feed?room=1 HTTP/1.1\r\n"));
    CHECK(server.head.contains("Sec-WebSocket-Protocol: chat, v2"));
    CHECK(server.head.contains("Sec-WebSocket-Version: 13"));
    CHECK(server.head.contains("Host: 127.0.0.1:" + QByteArray::number(server.port())));

    const auto send = [&fixture, &handle](const char* rest) {
        return fixture.net.call(
            kOwn, "net.socket.send",
            QStringLiteral(R"({"handle":"%1",%2})").arg(handle, QString::fromLatin1(rest)).toStdString());
    };
    CHECK(send(R"("binary":false,"text":"hi")").code.empty());
    CHECK(send(R"("binary":true,"data":"AQID")").code.empty());
    REQUIRE(pumpUntil([&server] { return server.frames.size() >= 2; }));
    CHECK(server.masked);
    CHECK(server.frames[0].first == 1);
    CHECK(server.frames[0].second == "hi");
    CHECK(server.frames[1].first == 2);
    CHECK(server.frames[1].second == QByteArray("\x01\x02\x03"));

    server.send(1, "Hel", false);
    server.send(9, "p1");
    server.send(0, "lo");
    server.send(2, QByteArray("\x04\x05"));
    REQUIRE(pumpUntil([&fixture, &server] { return fixture.told.size() >= 2 && server.frames.size() >= 3; }));
    CHECK(fixture.told[0].kind == QStringLiteral("netSocket"));
    CHECK(fixture.told[0].id == handle);
    CHECK(fixture.told[0].value.value(u"type").toString() == QStringLiteral("message"));
    CHECK_FALSE(fixture.told[0].value.value(u"binary").toBool());
    CHECK(fixture.told[0].value.value(u"text").toString() == QStringLiteral("Hello"));
    CHECK(fixture.told[1].value.value(u"binary").toBool());
    CHECK(fixture.told[1].value.value(u"data").toString() == QStringLiteral("BAU="));
    CHECK(server.frames[2].first == 10);
    CHECK(server.frames[2].second == "p1");

    server.send(8, QByteArray("\x03\xe8" "bye"));
    REQUIRE(pumpUntil([&fixture] { return fixture.told.size() >= 3; }));
    const QJsonObject closed = fixture.told[2].value;
    CHECK(closed.value(u"type").toString() == QStringLiteral("closed"));
    CHECK(closed.value(u"code").toInt() == 1000);
    CHECK(closed.value(u"reason").toString() == QStringLiteral("bye"));
    CHECK(closed.value(u"clean").toBool());
    REQUIRE(server.frames.size() >= 4);
    CHECK(server.frames[3].first == 8);
    CHECK(server.frames[3].second == QByteArray("\x03\xe8"));
    CHECK(send(R"("binary":false,"text":"late")").code == "GS-API-159");
    CHECK(pumpUntil([&fixture] { return fixture.net.sockets() == 0; }));
}

TEST_CASE("ADR-0112: a socket closes from the logic with its code and reason, and one over 1 MiB closes it with 1009")
{
    Fixture fixture;
    SocketServer server;
    fixture.open(kOwn, 1, QStringLiteral(R"({"url":"ws://127.0.0.1:%1/"})").arg(server.port()));
    REQUIRE(fixture.waitFor(1));
    const QString handle = fixture.answerOf(1).value(u"handle").toString();
    REQUIRE_FALSE(handle.isEmpty());
    CHECK(fixture.net
              .call(kOwn, "net.socket.close",
                    QStringLiteral(R"({"handle":"%1","code":4000,"reason":"done"})").arg(handle).toStdString())
              .code.empty());
    REQUIRE(pumpUntil([&server] { return !server.frames.empty(); }));
    CHECK(server.frames[0].first == 8);
    CHECK(server.frames[0].second == QByteArray("\x0f\xa0" "done"));
    CHECK(pumpUntil([&fixture] { return fixture.net.sockets() == 0; }));
    CHECK(fixture.told.empty());
    CHECK(fixture.net.call(kOwn, "net.socket.close", R"({"handle":"w1","code":1001})").code == "GS-API-156");

    SocketServer flood;
    fixture.open(kOwn, 2, QStringLiteral(R"({"url":"ws://127.0.0.1:%1/"})").arg(flood.port()));
    REQUIRE(fixture.waitFor(2));
    REQUIRE_FALSE(fixture.answerOf(2).isEmpty());
    flood.send(2, QByteArray((1 << 20) + 1, 'x'));
    REQUIRE(pumpUntil([&fixture] { return !fixture.told.empty(); }));
    CHECK(fixture.told[0].value.value(u"type").toString() == QStringLiteral("closed"));
    CHECK(fixture.told[0].value.value(u"code").toInt() == 1009);
    CHECK_FALSE(fixture.told[0].value.value(u"clean").toBool());
    REQUIRE(pumpUntil([&flood] { return !flood.frames.empty(); }));
    CHECK(flood.frames[0].first == 8);
    CHECK(flood.frames[0].second.left(2) == QByteArray("\x03\xf1"));

    SocketServer wrong;
    wrong.wrongAccept = true;
    fixture.open(kOwn, 3, QStringLiteral(R"({"url":"ws://127.0.0.1:%1/"})").arg(wrong.port()));
    REQUIRE(fixture.waitFor(3));
    CHECK(fixture.refusalOf(3).startsWith(QStringLiteral("GS-API-158 ")));
}

TEST_CASE("ADR-0112: sockets keep to the host rules, to four at a time and to the time for the handshake")
{
    Fixture fixture;
    QTcpServer silent;
    REQUIRE(silent.listen(QHostAddress::LocalHost, 0));
    const QString url = QStringLiteral(R"({"url":"ws://127.0.0.1:%1/"})").arg(silent.serverPort());
    fixture.open(kDeck, 1, url);
    CHECK(fixture.refusalOf(1).startsWith(QStringLiteral("GS-API-155 ")));

    fixture.net.setHosts({{QStringLiteral("com.example.deck"), {"127.0.0.1:" + std::to_string(silent.serverPort())}}});
    for (quint64 id = 2; id < 6; ++id) {
        fixture.open(kDeck, id, url);
    }
    CHECK(fixture.net.sockets() == 4);
    fixture.open(kDeck, 6, url);
    CHECK(fixture.refusalOf(6).startsWith(QStringLiteral("GS-API-157 ")));
    fixture.open(kDeck, 7, QStringLiteral(R"({"url":"ws://127.0.0.1:1/"})"));
    CHECK(fixture.refusalOf(7).startsWith(QStringLiteral("GS-API-155 ")));
    fixture.open(kOwn, 8, QStringLiteral(R"({"url":"ws://example.com/"})"));
    CHECK(fixture.refusalOf(8).startsWith(QStringLiteral("GS-API-156 ")));
    fixture.open(kOwn, 9, QStringLiteral(R"({"url":"wss://example.com/","protocols":["a b"]})"));
    CHECK(fixture.refusalOf(9).startsWith(QStringLiteral("GS-API-156 ")));
    fixture.net.endLogic(kDeck.logic);
    CHECK(fixture.net.sockets() == 0);
    for (quint64 id = 2; id < 6; ++id) {
        CHECK_FALSE(fixture.settled(id));
    }

    fixture.net.setHandshakeLimit(std::chrono::milliseconds(300));
    fixture.open(kOwn, 10, url);
    REQUIRE(fixture.waitFor(10));
    CHECK(fixture.refusalOf(10).startsWith(QStringLiteral("GS-API-158 ")));
}

TEST_CASE("ADR-0112: a logic has at most 64 bodies and messages waiting in parts, however small they are")
{
    Fixture fixture;
    const auto part = [](std::uint64_t stage) {
        return R"({"stage":)" + std::to_string(stage) + R"(,"text":"a"})";
    };
    for (std::uint64_t stage = 1; stage <= 64; ++stage) {
        CAPTURE(stage);
        CHECK(fixture.net.call(kOwn, "net.fetch.part", part(stage)).code.empty());
    }
    CHECK(fixture.net.call(kOwn, "net.fetch.part", part(64)).code.empty());
    const auto refused = fixture.net.call(kOwn, "net.fetch.part", part(65));
    CHECK(refused.code == "GS-API-157");
    CHECK(refused.problem.find("64") != std::string::npos);
    CHECK(fixture.net.call(kOwn, "net.socket.part", part(66)).code == "GS-API-157");
    CHECK(fixture.net.call(LogicCaller{.logic = 3}, "net.fetch.part", part(65)).code.empty());
    fixture.net.endLogic(kOwn.logic);
    CHECK(fixture.net.call(kOwn, "net.fetch.part", part(65)).code.empty());
}

TEST_CASE("ADR-0112: a server that pings without reading gets a pong for its last ping, not one for each")
{
    Application app;
    SocketServer server;
    WebSocketClient client(QUrl(QStringLiteral("ws://127.0.0.1:%1/").arg(server.port())), {}, qsizetype{1} << 20);
    bool opened = false;
    QByteArray message;
    QObject::connect(&client, &WebSocketClient::opened, &client, [&opened] { opened = true; });
    QObject::connect(&client, &WebSocketClient::received, &client,
                     [&message](const QByteArray& data, bool) { message = data; });
    client.open(std::chrono::steady_clock::now() + std::chrono::seconds(10));
    REQUIRE(pumpUntil([&opened] { return opened; }));

    server.pause();
    constexpr int kPings = 400000;
    const auto ping = [](int index) { return QByteArray::number(index).leftJustified(125, '.'); };
    QByteArray flood;
    flood.reserve(qsizetype{kPings} * 127 + 16);
    for (int index = 0; index < kPings; ++index) {
        flood.append(SocketServer::frameOf(9, ping(index)));
    }
    flood.append(SocketServer::frameOf(1, "end"));
    server.sendRaw(flood);
    REQUIRE(pumpUntil([&message] { return message == "end"; }));
    CHECK(client.queued() <= (qint64{1} << 20) + 256);

    server.resume();
    const QByteArray last = ping(kPings - 1);
    REQUIRE(pumpUntil([&server, &last] { return !server.frames.empty() && server.frames.back().second == last; }));
    CHECK(server.frames.back().first == 10);
    CHECK(std::ranges::count(server.frames, 10, &std::pair<int, QByteArray>::first) < kPings / 2);
}
