#include "WebSocketClient.h"

#include <QAbstractSocket>
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QIODevice>
#include <QList>
#include <QRandomGenerator>

#include <array>
#include <cstddef>
#include <utility>

namespace gs::app {

namespace {

constexpr qsizetype kMostHeadBytes = 16 * 1024;
constexpr char kAcceptGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr int kContinuation = 0x0;
constexpr int kText = 0x1;
constexpr int kBinary = 0x2;
constexpr int kClose = 0x8;
constexpr int kPing = 0x9;
constexpr int kPong = 0xA;
constexpr quint64 kMostControlBytes = 125;
constexpr std::chrono::seconds kCloseWait{5};

bool hasToken(const QByteArray& value, QByteArrayView token)
{
    const QList<QByteArray> parts = value.split(',');
    for (const QByteArray& part : parts) {
        if (part.trimmed().compare(token, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

bool sendableCode(int code)
{
    return (code >= 1000 && code <= 1003) || (code >= 1007 && code <= 1014) || (code >= 3000 && code <= 4999);
}

QByteArray closePayload(int code, const QByteArray& reason)
{
    QByteArray payload;
    payload.append(static_cast<char>((code >> 8) & 0xFF));
    payload.append(static_cast<char>(code & 0xFF));
    payload.append(reason);
    return payload;
}

}

WebSocketClient::WebSocketClient(QUrl url, QStringList protocols, qsizetype mostMessageBytes, QObject* parent)
    : QObject(parent)
    , m_url(std::move(url))
    , m_protocols(std::move(protocols))
    , m_most(mostMessageBytes)
{
    m_socket.setReadBufferSize(qint64{1} << 20);
    connect(&m_socket, &QSslSocket::encrypted, this, &WebSocketClient::handshake);
    connect(&m_socket, &QAbstractSocket::connected, this, [this] {
        if (m_url.scheme() == u"ws") {
            handshake();
        }
    });
    connect(&m_socket, &QIODevice::readyRead, this, &WebSocketClient::readable);
    connect(&m_socket, &QIODevice::bytesWritten, this, [this] {
        answerPing();
        emit drained(queued());
    });
    connect(&m_socket, &QSslSocket::encryptedBytesWritten, this, [this] {
        answerPing();
        emit drained(queued());
    });
    connect(&m_socket, &QAbstractSocket::disconnected, this, &WebSocketClient::lost);
    connect(&m_socket, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_failed || (m_closeReceived && m_closeSent)) {
            lost();
            return;
        }
        finish(kAbnormal, m_socket.errorString(), false);
    });
}

WebSocketClient::~WebSocketClient()
{
    disconnect(&m_socket, nullptr, this, nullptr);
    m_socket.abort();
}

void WebSocketClient::setProxy(const QNetworkProxy& proxy)
{
    m_socket.setProxy(proxy);
}

void WebSocketClient::open(Clock::time_point deadline)
{
    m_deadline = deadline;
    const bool plain = m_url.scheme() == u"ws";
    const QString host = m_url.host(QUrl::EncodeUnicode);
    const auto port = static_cast<quint16>(m_url.port(plain ? 80 : 443));
    if (plain) {
        m_socket.connectToHost(host, port);
    } else {
        m_socket.connectToHostEncrypted(host, port);
    }
}

void WebSocketClient::send(const QByteArray& data, bool binary)
{
    if (m_state == State::Open) {
        write(binary ? kBinary : kText, data);
    }
}

void WebSocketClient::close(int code, const QByteArray& reason)
{
    if (m_state == State::Connecting) {
        finish(kAbnormal, QString(), false);
        return;
    }
    if (m_state != State::Open) {
        return;
    }
    m_closeSent = true;
    write(kClose, closePayload(code, reason));
    m_state = State::Closing;
    m_deadline = Clock::now() + kCloseWait;
}

void WebSocketClient::expire(Clock::time_point now)
{
    if (!waiting() || now < m_deadline) {
        return;
    }
    if (m_state == State::Connecting) {
        finish(kAbnormal, QStringLiteral("the server did not finish the WebSocket handshake in time"), false);
        return;
    }
    if (m_failed || m_closeReceived) {
        lost();
        return;
    }
    finish(kAbnormal, QString(), false);
}

qint64 WebSocketClient::queued() const
{
    return m_socket.bytesToWrite() + m_socket.encryptedBytesToWrite();
}

void WebSocketClient::handshake()
{
    if (m_state != State::Connecting || !m_key.isEmpty()) {
        return;
    }
    std::array<quint32, 4> entropy{};
    QRandomGenerator::system()->fillRange(entropy.data(), static_cast<qsizetype>(entropy.size()));
    m_key = QByteArray(reinterpret_cast<const char*>(entropy.data()), static_cast<qsizetype>(sizeof(entropy))).toBase64();
    const bool plain = m_url.scheme() == u"ws";
    QByteArray target = m_url.path(QUrl::FullyEncoded).toLatin1();
    if (target.isEmpty()) {
        target = "/";
    }
    if (m_url.hasQuery()) {
        target.append('?');
        target.append(m_url.query(QUrl::FullyEncoded).toLatin1());
    }
    QByteArray host = m_url.host(QUrl::EncodeUnicode).toLatin1();
    if (host.contains(':')) {
        host.prepend('[');
        host.append(']');
    }
    const int port = m_url.port(plain ? 80 : 443);
    if (port != (plain ? 80 : 443)) {
        host.append(':');
        host.append(QByteArray::number(port));
    }
    QByteArray request = "GET ";
    request.append(target);
    request.append(" HTTP/1.1\r\nHost: ");
    request.append(host);
    request.append("\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ");
    request.append(m_key);
    request.append("\r\nSec-WebSocket-Version: 13\r\n");
    if (!m_protocols.isEmpty()) {
        request.append("Sec-WebSocket-Protocol: ");
        request.append(m_protocols.join(QStringLiteral(", ")).toLatin1());
        request.append("\r\n");
    }
    request.append("\r\n");
    m_socket.write(request);
}

void WebSocketClient::readable()
{
    if (m_state == State::Closed || m_failed) {
        m_socket.readAll();
        return;
    }
    m_buffer.append(m_socket.readAll());
    if (m_state == State::Connecting && !readHead()) {
        return;
    }
    readFrames();
}

bool WebSocketClient::readHead()
{
    const qsizetype end = m_buffer.indexOf("\r\n\r\n");
    if (end < 0) {
        if (m_buffer.size() > kMostHeadBytes) {
            finish(kAbnormal, QStringLiteral("the server answered the handshake with too long a head"), false);
        }
        return false;
    }
    const QList<QByteArray> lines = m_buffer.left(end).split('\n');
    m_buffer.remove(0, end + 4);
    const QByteArray status = lines.front().trimmed();
    const QList<QByteArray> parts = status.split(' ');
    if (parts.size() < 2 || !parts[0].startsWith("HTTP/1.") || parts[1] != "101") {
        finish(kAbnormal,
               QStringLiteral("the server answered \"%1\" instead of switching to WebSocket")
                   .arg(QString::fromLatin1(status.left(80))),
               false);
        return false;
    }
    QByteArray upgrade;
    QByteArray connection;
    QByteArray accept;
    QByteArray chosen;
    bool extensions = false;
    for (qsizetype index = 1; index < lines.size(); ++index) {
        const QByteArray line = lines[index].trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0) {
            continue;
        }
        const QByteArray name = line.left(colon).trimmed().toLower();
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (name == "upgrade") {
            upgrade = value;
        } else if (name == "connection") {
            connection = value;
        } else if (name == "sec-websocket-accept") {
            accept = value;
        } else if (name == "sec-websocket-protocol") {
            chosen = value;
        } else if (name == "sec-websocket-extensions") {
            extensions = true;
        }
    }
    QByteArray keyed = m_key;
    keyed.append(kAcceptGuid);
    const QByteArray expected = QCryptographicHash::hash(keyed, QCryptographicHash::Sha1).toBase64();
    if (upgrade.compare("websocket", Qt::CaseInsensitive) != 0 || !hasToken(connection, "upgrade") ||
        accept != expected) {
        finish(kAbnormal, QStringLiteral("the server did not accept the WebSocket handshake"), false);
        return false;
    }
    if (extensions || (!chosen.isEmpty() && !m_protocols.contains(QString::fromLatin1(chosen)))) {
        finish(kAbnormal, QStringLiteral("the server chose a protocol or an extension that was not asked for"), false);
        return false;
    }
    m_protocol = QString::fromLatin1(chosen);
    m_state = State::Open;
    emit opened();
    return m_state == State::Open;
}

void WebSocketClient::readFrames()
{
    while (m_state == State::Open || m_state == State::Closing) {
        if (m_buffer.size() < 2) {
            return;
        }
        const auto first = static_cast<quint8>(m_buffer.at(0));
        const auto second = static_cast<quint8>(m_buffer.at(1));
        const bool last = (first & 0x80) != 0;
        const int opcode = first & 0x0F;
        if ((first & 0x70) != 0 || (second & 0x80) != 0) {
            fail(kProtocolError, QStringLiteral("the server sent a frame that WebSocket does not allow"));
            return;
        }
        qsizetype head = 2;
        auto length = static_cast<quint64>(second & 0x7F);
        if (length == 126) {
            if (m_buffer.size() < 4) {
                return;
            }
            length = (quint64{static_cast<quint8>(m_buffer.at(2))} << 8) | static_cast<quint8>(m_buffer.at(3));
            head = 4;
        } else if (length == 127) {
            if (m_buffer.size() < 10) {
                return;
            }
            length = 0;
            for (qsizetype index = 2; index < 10; ++index) {
                length = (length << 8) | static_cast<quint8>(m_buffer.at(index));
            }
            head = 10;
        }
        const bool controlFrame = opcode >= kClose;
        if (controlFrame && (!last || length > kMostControlBytes)) {
            fail(kProtocolError, QStringLiteral("the server sent a control frame that WebSocket does not allow"));
            return;
        }
        if (!controlFrame && length > static_cast<quint64>(m_most - m_message.size())) {
            fail(kTooBig, QStringLiteral("the server sent a message larger than 1 MiB"));
            return;
        }
        if (static_cast<quint64>(m_buffer.size() - head) < length) {
            return;
        }
        const QByteArray payload = m_buffer.mid(head, static_cast<qsizetype>(length));
        m_buffer.remove(0, head + static_cast<qsizetype>(length));
        if (controlFrame) {
            control(opcode, payload);
            continue;
        }
        const bool valid = opcode == kContinuation ? m_messageKind != 0
                                                   : (opcode == kText || opcode == kBinary) && m_messageKind == 0;
        if (!valid) {
            fail(kProtocolError, QStringLiteral("the server sent the parts of a message out of order"));
            return;
        }
        if (opcode != kContinuation) {
            m_messageKind = opcode;
        }
        m_message.append(payload);
        if (!last) {
            continue;
        }
        const int kind = std::exchange(m_messageKind, 0);
        const QByteArray message = std::exchange(m_message, QByteArray());
        if (kind == kText && !message.isValidUtf8()) {
            fail(kInvalidData, QStringLiteral("the server sent text that is not UTF-8"));
            return;
        }
        if (m_state == State::Open) {
            emit received(message, kind == kBinary);
        }
    }
}

void WebSocketClient::control(int opcode, const QByteArray& payload)
{
    if (opcode == kPing) {
        if (m_state == State::Open) {
            m_pong = payload;
            m_pongDue = true;
            answerPing();
        }
        return;
    }
    if (opcode == kPong) {
        return;
    }
    if (opcode != kClose || payload.size() == 1) {
        fail(kProtocolError, QStringLiteral("the server sent a control frame that WebSocket does not allow"));
        return;
    }
    int code = kNoStatus;
    QString reason;
    if (payload.size() >= 2) {
        code = (static_cast<quint8>(payload.at(0)) << 8) | static_cast<quint8>(payload.at(1));
        const QByteArray text = payload.mid(2);
        if (!sendableCode(code) || !text.isValidUtf8()) {
            fail(kProtocolError, QStringLiteral("the server closed with a code or reason that WebSocket does not allow"));
            return;
        }
        reason = QString::fromUtf8(text);
    }
    m_closeReceived = true;
    m_closeCode = code;
    m_closeReason = reason;
    if (!m_closeSent) {
        m_closeSent = true;
        write(kClose, payload.left(2));
    }
    m_state = State::Closing;
    m_deadline = Clock::now() + kCloseWait;
    m_socket.disconnectFromHost();
}

void WebSocketClient::answerPing()
{
    if (!m_pongDue || m_state != State::Open || queued() > m_most) {
        return;
    }
    m_pongDue = false;
    write(kPong, std::exchange(m_pong, QByteArray()));
}

void WebSocketClient::write(int opcode, const QByteArray& payload)
{
    QByteArray frame;
    frame.reserve(payload.size() + 14);
    frame.append(static_cast<char>(0x80 | opcode));
    const auto size = static_cast<quint64>(payload.size());
    if (size < 126) {
        frame.append(static_cast<char>(0x80 | size));
    } else if (size <= 0xFFFF) {
        frame.append(static_cast<char>(0x80 | 126));
        frame.append(static_cast<char>((size >> 8) & 0xFF));
        frame.append(static_cast<char>(size & 0xFF));
    } else {
        frame.append(static_cast<char>(0x80 | 127));
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.append(static_cast<char>((size >> shift) & 0xFF));
        }
    }
    const quint32 mask = QRandomGenerator::system()->generate();
    const std::array<char, 4> key{static_cast<char>((mask >> 24) & 0xFF), static_cast<char>((mask >> 16) & 0xFF),
                                  static_cast<char>((mask >> 8) & 0xFF), static_cast<char>(mask & 0xFF)};
    frame.append(key.data(), static_cast<qsizetype>(key.size()));
    const qsizetype start = frame.size();
    frame.append(payload);
    char* data = frame.data() + start;
    for (qsizetype index = 0; index < payload.size(); ++index) {
        data[index] = static_cast<char>(data[index] ^ key[static_cast<std::size_t>(index % 4)]);
    }
    m_socket.write(frame);
}

void WebSocketClient::fail(int code, const QString& reason)
{
    if (m_state != State::Open || m_closeSent) {
        finish(code, reason, false);
        return;
    }
    m_closeSent = true;
    write(kClose, closePayload(code, {}));
    m_failed = true;
    m_closeCode = code;
    m_closeReason = reason;
    m_state = State::Closing;
    m_buffer.clear();
    m_message.clear();
    m_messageKind = 0;
    m_deadline = Clock::now() + kCloseWait;
    emit closing();
}

void WebSocketClient::lost()
{
    if (m_failed || m_closeReceived) {
        finish(m_closeCode, m_closeReason, !m_failed && m_closeSent);
        return;
    }
    finish(kAbnormal, QStringLiteral("the connection closed without a close frame"), false);
}

void WebSocketClient::finish(int code, const QString& reason, bool clean)
{
    if (m_state == State::Closed) {
        return;
    }
    m_state = State::Closed;
    m_buffer.clear();
    m_message.clear();
    m_messageKind = 0;
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.abort();
    }
    emit ended(code, reason, clean);
}

}
