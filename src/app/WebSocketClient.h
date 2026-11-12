#pragma once

#include <QByteArray>
#include <QNetworkProxy>
#include <QObject>
#include <QSslSocket>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <chrono>
#include <cstdint>

namespace gs::app {

class WebSocketClient final : public QObject
{
    Q_OBJECT

public:
    using Clock = std::chrono::steady_clock;

    static constexpr int kNormalClose = 1000;
    static constexpr int kNoStatus = 1005;
    static constexpr int kAbnormal = 1006;
    static constexpr int kProtocolError = 1002;
    static constexpr int kInvalidData = 1007;
    static constexpr int kTooBig = 1009;

    WebSocketClient(QUrl url, QStringList protocols, qsizetype mostMessageBytes, QObject* parent = nullptr);
    ~WebSocketClient() override;
    WebSocketClient(const WebSocketClient&) = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;

    void setProxy(const QNetworkProxy& proxy);
    void open(Clock::time_point deadline);
    void send(const QByteArray& data, bool binary);
    void close(int code, const QByteArray& reason);
    void expire(Clock::time_point now);

    [[nodiscard]] bool isOpen() const noexcept { return m_state == State::Open; }
    [[nodiscard]] bool waiting() const noexcept { return m_state == State::Connecting || m_state == State::Closing; }
    [[nodiscard]] const QString& protocol() const noexcept { return m_protocol; }
    [[nodiscard]] qint64 queued() const;

signals:
    void opened();
    void received(const QByteArray& data, bool binary);
    void drained(qint64 queued);
    void closing();
    void ended(int code, const QString& reason, bool clean);

private:
    enum class State : std::uint8_t { Connecting, Open, Closing, Closed };

    void handshake();
    void readable();
    [[nodiscard]] bool readHead();
    void readFrames();
    void control(int opcode, const QByteArray& payload);
    void answerPing();
    void write(int opcode, const QByteArray& payload);
    void fail(int code, const QString& reason);
    void lost();
    void finish(int code, const QString& reason, bool clean);

    QUrl m_url;
    QStringList m_protocols;
    qsizetype m_most;
    QSslSocket m_socket;
    State m_state = State::Connecting;
    QByteArray m_key;
    QByteArray m_buffer;
    QByteArray m_message;
    QByteArray m_pong;
    bool m_pongDue = false;
    int m_messageKind = 0;
    QString m_protocol;
    Clock::time_point m_deadline;
    int m_closeCode = kAbnormal;
    QString m_closeReason;
    bool m_closeSent = false;
    bool m_closeReceived = false;
    bool m_failed = false;
};

}
