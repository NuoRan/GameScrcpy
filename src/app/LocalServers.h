#pragma once

#include "LogicServices.h"

#include <gs/script/Services.h>

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

class QJSEngine;
class QQmlEngine;
class QTcpServer;
class QTcpSocket;

namespace gs::app {

class ExtensionHost;

struct LocalReplies {
    std::function<void(std::uint64_t logic, quint64 request, const QString& json)> answer;
    std::function<void(std::uint64_t logic, quint64 request, const QString& message)> refuse;
    std::function<void(std::uint64_t logic, const QString& kind, const QString& id, const QString& json)> tell;
};

class LocalServers final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantMap ports READ ports NOTIFY portsChanged FINAL)

public:
    static constexpr std::string_view kPermission = "network.listen";
    static constexpr std::string_view kEventKind = "net";
    static constexpr std::size_t kMostServers = 4;
    static constexpr std::size_t kMostConnections = 16;
    static constexpr std::size_t kMostOrigins = 16;
    static constexpr qsizetype kMostHeadBytes = 16 * 1024;
    static constexpr qsizetype kMostBodyBytes = 1024 * 1024;

    explicit LocalServers(QObject* parent);
    ~LocalServers() override;
    LocalServers(const LocalServers&) = delete;
    LocalServers& operator=(const LocalServers&) = delete;

    static LocalServers* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    [[nodiscard]] static LocalServers* instance() noexcept { return s_instance; }

    void offerServices();
    void setReplies(LocalReplies replies);
    void setPermitted(const QStringList& extensions);
    void setLimits(std::chrono::milliseconds read, std::chrono::milliseconds answer);
    void follow(ExtensionHost& extensions);

    void request(const LogicCaller& caller, std::string_view service, quint64 id, const QString& json);
    [[nodiscard]] script::ServiceAnswer call(const LogicCaller& caller, std::string_view service,
                                             std::string_view json);
    void endLogic(std::uint64_t logic);

    [[nodiscard]] QVariantMap ports() const;

signals:
    void portsChanged();

private:
    struct Shared;
    struct Server;
    struct Connection;

    [[nodiscard]] static script::ServiceAnswer callWith(const std::shared_ptr<Shared>& shared,
                                                        const LogicCaller& caller, std::string_view service,
                                                        std::string_view json);

    void listen(const LogicCaller& caller, quint64 id, const QString& json);
    void accept(const QString& handle);
    void readFrom(QTcpSocket* socket);
    void respond(std::uint64_t logic, const QString& handle, quint64 request, int status,
                 const std::vector<std::pair<QByteArray, QByteArray>>& headers, const QByteArray& body);
    void close(std::uint64_t logic, const QString& handle);
    void drop(const QString& handle, const QString& reason);
    void finish(QTcpSocket* socket, int status, const QByteArray& headers, const QByteArray& body);
    void forget(QTcpSocket* socket);
    void sweep();

    static inline LocalServers* s_instance = nullptr;

    std::shared_ptr<Shared> m_shared;
    LocalReplies m_replies;
    std::map<QString, Server> m_servers;
    std::map<QTcpSocket*, Connection> m_connections;
    QTimer m_sweep;
    std::chrono::milliseconds m_readLimit{10'000};
    std::chrono::milliseconds m_answerLimit{10'000};
    std::uint64_t m_lastHandle = 0;
    bool m_offered = false;
};

}
