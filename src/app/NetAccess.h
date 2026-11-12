#pragma once

#include "LogicServices.h"

#include <gs/script/Services.h>

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class QNetworkReply;

namespace gs::app {

class ExtensionHost;
class WebSocketClient;

struct NetReplies {
    std::function<void(std::uint64_t logic, quint64 request, const QString& json)> answer;
    std::function<void(std::uint64_t logic, quint64 request, const QString& message)> refuse;
    std::function<void(std::uint64_t logic, const QString& kind, const QString& id, const QString& json)> tell;
};

class NetAccess final : public QObject
{
    Q_OBJECT

public:
    static constexpr std::string_view kEventKind = "netSocket";
    static constexpr std::size_t kMostFetches = 8;
    static constexpr std::size_t kMostSockets = 4;
    static constexpr qsizetype kMostBodyBytes = qsizetype{1} << 20;
    static constexpr qsizetype kMostAnswerBytes = qsizetype{8} << 20;
    static constexpr qsizetype kMostMessageBytes = qsizetype{1} << 20;
    static constexpr qint64 kMostWaitingBytes = qint64{16} << 20;

    explicit NetAccess(QObject* parent = nullptr);
    ~NetAccess() override;
    NetAccess(const NetAccess&) = delete;
    NetAccess& operator=(const NetAccess&) = delete;

    void offerServices();
    void setReplies(NetReplies replies);
    void setHosts(std::map<QString, std::vector<std::string>> hosts);
    void follow(ExtensionHost& extensions);
    void useSystemProxy();
    void allowPlainLocal(bool allowed);
    void setHandshakeLimit(std::chrono::milliseconds limit);

    void request(const LogicCaller& caller, std::string_view service, quint64 id, const QString& json);
    [[nodiscard]] script::ServiceAnswer call(const LogicCaller& caller, std::string_view service,
                                             std::string_view json);
    void drop(const LogicCaller& caller, quint64 id);
    void endLogic(std::uint64_t logic);

    [[nodiscard]] std::size_t fetching() const noexcept { return m_fetches.size(); }
    [[nodiscard]] std::size_t sockets() const noexcept { return m_sockets.size(); }

private:
    struct Shared;

    struct Fetch {
        std::uint64_t logic = 0;
        quint64 request = 0;
        QString owner;
        QNetworkReply* reply = nullptr;
        QUrl url;
        QByteArray body;
        std::chrono::steady_clock::time_point deadline;
        int timeoutMs = 0;
    };

    struct Socket {
        std::uint64_t logic = 0;
        quint64 request = 0;
        QString owner;
        QUrl url;
        std::unique_ptr<WebSocketClient> client;
        bool open = false;
        bool closing = false;
    };

    [[nodiscard]] static script::ServiceAnswer callWith(const std::shared_ptr<Shared>& shared,
                                                        const LogicCaller& caller, std::string_view service,
                                                        std::string_view json);

    [[nodiscard]] QString urlProblem(const QString& owner, const QString& text, bool socket, QUrl& url) const;
    [[nodiscard]] bool reachable(const QString& owner, const QUrl& url) const;
    void fetch(const LogicCaller& caller, quint64 id, const QString& json);
    void openSocket(const LogicCaller& caller, quint64 id, const QString& json);
    void readSome(std::uint64_t serial);
    void measured(std::uint64_t serial);
    void redirected(std::uint64_t serial, const QUrl& next);
    void fetched(std::uint64_t serial);
    void failFetch(std::uint64_t serial, const QString& message);
    void forgetFetch(std::uint64_t serial);
    void socketOpened(const QString& handle);
    void socketMessage(const QString& handle, const QByteArray& data, bool binary);
    void socketEnded(const QString& handle, int code, const QString& reason, bool clean);
    void sendOn(const QString& handle, const QByteArray& data, bool binary);
    void closeSocket(const QString& handle, int code, const QByteArray& reason);
    void forgetSocket(const QString& handle, const QString& reason);
    void startSweep();
    void sweep();
    void answer(std::uint64_t logic, quint64 request, const QString& json) const;
    void refuse(std::uint64_t logic, quint64 request, const QString& message) const;
    void tell(std::uint64_t logic, const QString& handle, const QString& json) const;

    std::shared_ptr<Shared> m_shared;
    NetReplies m_replies;
    QNetworkAccessManager m_network;
    std::map<std::uint64_t, Fetch> m_fetches;
    std::map<QString, Socket> m_sockets;
    QTimer m_sweep;
    std::chrono::milliseconds m_handshakeLimit{15'000};
    std::uint64_t m_lastFetch = 0;
    std::uint64_t m_lastSocket = 0;
    bool m_systemProxy = false;
    bool m_plainLocal = false;
    bool m_offered = false;
};

}
