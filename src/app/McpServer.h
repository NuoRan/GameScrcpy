#pragma once

#include <gs/base/Clock.h>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalServer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

class QJSEngine;
class QLocalSocket;
class QQmlEngine;

namespace gs::app {

class AppSettings;
class McpDrafts;

[[nodiscard]] QString mcpPipeName();

enum class McpAnswer : std::uint8_t { Allowed, Refused, Unanswered, AllowedForAWhile };

struct McpConfirmation {
    QString kind;
    QVariantMap details;
};

struct McpOutcome {
    QJsonArray content;
    bool failed = false;
    QString code;
};

struct McpCall {
    quint64 id = 0;
    QString client;
    QString tool;
    QJsonObject arguments;
    std::function<void(McpConfirmation confirmation, std::function<void(McpAnswer)> done)> confirm;
};

class McpToolHost
{
public:
    using Reply = std::function<void(McpOutcome outcome)>;

    virtual ~McpToolHost() = default;

    [[nodiscard]] virtual QJsonArray tools() const = 0;
    virtual void call(const McpCall& call, Reply reply) = 0;
    virtual void cancel(quint64 call) = 0;
    virtual void forget(const QString& client) { static_cast<void>(client); }
    [[nodiscard]] virtual QVariantList allowedNow() const { return {}; }
    virtual void endAllowed(const QString& client, const QString& key)
    {
        static_cast<void>(client);
        static_cast<void>(key);
    }
};

class McpServer final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool listening READ listening NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString problem READ problem NOTIFY stateChanged FINAL)
    Q_PROPERTY(QStringList connected READ connected NOTIFY stateChanged FINAL)
    Q_PROPERTY(QStringList clients READ clients NOTIFY clientsChanged FINAL)
    Q_PROPERTY(QString approval READ approval NOTIFY approvalChanged FINAL)
    Q_PROPERTY(QVariantMap confirmation READ confirmation NOTIFY confirmationChanged FINAL)
    Q_PROPERTY(QVariantList allowedNow READ allowedNow NOTIFY allowedChanged FINAL)
    Q_PROPERTY(QVariantMap draftProfiles READ draftProfiles NOTIFY draftsChanged FINAL)

public:
    static constexpr auto kAnswerLimit = std::chrono::minutes(2);
    static constexpr qsizetype kMostMessageBytes = qsizetype{32} << 20;
    static constexpr qsizetype kMostClientChars = 64;
    static constexpr int kRefused = -32001;
    static constexpr int kNotInitialized = -32002;

    McpServer(AppSettings& settings, McpToolHost& tools, McpDrafts& drafts, const Clock& clock,
              QString pipe = mcpPipeName(), QObject* parent = nullptr);
    ~McpServer() override;
    McpServer(const McpServer&) = delete;
    McpServer& operator=(const McpServer&) = delete;

    static McpServer* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    [[nodiscard]] static McpServer* instance() noexcept { return s_instance; }

    [[nodiscard]] bool listening() const;
    [[nodiscard]] QString problem() const { return m_problem; }
    [[nodiscard]] QStringList connected() const;
    [[nodiscard]] QStringList clients() const;
    [[nodiscard]] QString approval() const;
    [[nodiscard]] QVariantMap confirmation() const;
    [[nodiscard]] QVariantList allowedNow() const;
    [[nodiscard]] QVariantMap draftProfiles() const;
    [[nodiscard]] const QString& pipe() const noexcept { return m_pipe; }
    [[nodiscard]] McpDrafts& drafts() noexcept { return m_drafts; }
    [[nodiscard]] QJsonArray localTools() const;
    void runLocal(const QString& client, const QString& tool, const QJsonObject& arguments, McpToolHost::Reply reply);
    void announceTools();

    Q_INVOKABLE QString installSkill() const;
    Q_INVOKABLE bool copySkill() const;

    Q_INVOKABLE void approve(const QString& client, bool allowed, bool remember);
    Q_INVOKABLE void answer(quint64 id, bool allowed, bool quiet = false);
    Q_INVOKABLE void revoke(const QString& client);
    Q_INVOKABLE void endAllowed(const QString& client, const QString& key);
    Q_INVOKABLE QString configuration() const;
    Q_INVOKABLE bool copyConfiguration() const;
    void expire();

signals:
    void stateChanged();
    void clientsChanged();
    void approvalChanged();
    void confirmationChanged();
    void allowedChanged();
    void draftsChanged();
    void attentionWanted();

private:
    enum class Trust : std::uint8_t { Unknown, Waiting, Allowed, Refused };

    struct Session {
        quint64 number = 0;
        QPointer<QLocalSocket> socket;
        QByteArray buffer;
        QString client;
        bool initialized = false;
        Trust trust = Trust::Unknown;
    };

    struct Running {
        quint64 session = 0;
        QJsonValue request;
        QString client;
        QString tool;
        McpToolHost::Reply local;
    };

    struct Pending {
        quint64 call = 0;
        QString client;
        McpConfirmation shown;
        Microseconds deadline{0};
        std::function<void(McpAnswer)> done;
    };

    void apply();
    void start();
    void stop();
    void accept();
    void readFrom(quint64 number);
    void handle(Session& session, const QByteArray& line);
    void initialize(Session& session, const QJsonValue& id, const QJsonObject& params);
    void callTool(Session& session, const QJsonValue& id, const QJsonObject& params);
    void finish(quint64 call, McpOutcome outcome);
    void cancelRequest(Session& session, const QJsonValue& request);
    void confirm(quint64 call, McpConfirmation shown, std::function<void(McpAnswer)> done);
    void dropCall(quint64 call);
    void close(quint64 number);
    void send(Session& session, const QJsonObject& message);
    void reply(Session& session, const QJsonValue& id, const QJsonValue& result);
    void fail(Session& session, const QJsonValue& id, int code, const QString& message);
    void schedule();
    void forgetWaiting(const QString& client);
    [[nodiscard]] Session* session(quint64 number);
    [[nodiscard]] bool remembered(const QString& client) const;

    static inline McpServer* s_instance = nullptr;

    AppSettings& m_settings;
    McpToolHost& m_tools;
    McpDrafts& m_drafts;
    const Clock& m_clock;
    const QString m_pipe;
    QLocalServer m_server;
    QTimer m_deadline;
    QString m_problem;
    std::vector<std::unique_ptr<Session>> m_sessions;
    std::map<quint64, Running> m_calls;
    std::vector<Pending> m_pending;
    QStringList m_waiting;
    quint64 m_lastSession = 0;
    quint64 m_lastCall = 0;
    bool m_on = false;
};

}
