#pragma once

#include "AiService.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace gs::app {

struct AiScope {
    QString serial;
    QString profile;
};

struct AiGuarded {
    QJsonObject arguments;
    QString refusal;
};

struct AiContext {
    QString serial;
    QString phone;
    QString app;
    QString profile;
    QString profileName;
    QString window;
    QString page;
    QString selection;

    friend bool operator==(const AiContext&, const AiContext&) = default;
};

inline constexpr int kAiFrameSide = 1280;
inline constexpr qsizetype kAiMostSavedResultChars = 20'000;

[[nodiscard]] QJsonArray aiOfferedTools(const QJsonArray& tools, const AiScope& scope);
[[nodiscard]] AiGuarded aiGuard(const QString& tool, const QJsonObject& arguments, const QJsonArray& offered,
                                const AiScope& scope);
[[nodiscard]] QString aiToolTitle(const QString& tool, const QJsonObject& arguments);
[[nodiscard]] QString aiToolRunningTitle(const QString& tool, const QJsonObject& arguments);
[[nodiscard]] QString aiChangeSummary(const QString& result);
[[nodiscard]] QString aiSystemPrompt(const AiContext& context, const QString& snapshot, const QString& problem);

class AiConversation : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString serial READ serial WRITE setSerial NOTIFY contextChanged FINAL)
    Q_PROPERTY(QString phone READ phone WRITE setPhone NOTIFY contextChanged FINAL)
    Q_PROPERTY(QString app READ app WRITE setApp NOTIFY contextChanged FINAL)
    Q_PROPERTY(QString profile READ profile WRITE setProfile NOTIFY contextChanged FINAL)
    Q_PROPERTY(QString profileName READ profileName WRITE setProfileName NOTIFY contextChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(bool waiting READ waiting NOTIFY busyChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    static constexpr int kMostRounds = 24;

    enum Role {
        KindRole = Qt::UserRole + 1,
        TextRole,
        BlocksRole,
        TitleRole,
        InputRole,
        OutputRole,
        RunningRole,
        FailedRole,
        SecondsRole,
        ChangesRole,
        RunRole,
    };
    Q_ENUM(Role)

    static constexpr int kGroupedCalls = 3;

    using ProviderFactory = std::function<std::unique_ptr<AiProvider>()>;

    explicit AiConversation(QObject* parent = nullptr);
    AiConversation(ProviderFactory provider, AiToolRunner* tools, QObject* parent = nullptr);
    ~AiConversation() override;
    AiConversation(const AiConversation&) = delete;
    AiConversation& operator=(const AiConversation&) = delete;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString serial() const { return m_context.serial; }
    void setSerial(const QString& serial);
    [[nodiscard]] QString phone() const { return m_context.phone; }
    void setPhone(const QString& phone);
    [[nodiscard]] QString app() const { return m_context.app; }
    void setApp(const QString& app);
    [[nodiscard]] QString profile() const { return m_context.profile; }
    void setProfile(const QString& profile);
    [[nodiscard]] QString profileName() const { return m_context.profileName; }
    void setProfileName(const QString& profileName);
    [[nodiscard]] const AiContext& context() const noexcept { return m_context; }
    void setContext(const AiContext& context);
    [[nodiscard]] bool busy() const noexcept { return m_phase != Phase::Idle; }
    [[nodiscard]] bool waiting() const noexcept { return m_waiting; }
    [[nodiscard]] int count() const noexcept { return static_cast<int>(m_items.size()); }
    [[nodiscard]] const std::vector<AiTurn>& history() const noexcept { return m_history; }
    [[nodiscard]] QJsonObject saved() const;
    void restore(const QJsonObject& saved);

    Q_INVOKABLE bool send(const QString& text);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void note(const QString& text);

signals:
    void contextChanged();
    void busyChanged();
    void countChanged();
    void turnFinished();

private:
    enum class Phase : std::uint8_t { Idle, Context, Asking, Tools };

    struct Item {
        enum class Kind : std::uint8_t { User, Assistant, Tool, Notice, Problem, Thinking };

        Kind kind = Kind::User;
        QString text;
        QString title;
        QString input;
        QString output;
        bool running = false;
        bool failed = false;
        qint64 started = 0;
        int seconds = 0;
        QString changes;
    };

    void readContext();
    void ask();
    void thought(const QString& delta);
    void endThinking();
    void calling(const QString& tool);
    void dropCalls(const QString& why);
    void streamed(const QString& delta);
    void answered(AiReply reply);
    void runNext();
    void ranTool(int row, const AiToolCall& call, McpOutcome outcome);
    void finishTurn();
    int append(Item item);
    void changed(int row, const QList<int>& roles);
    [[nodiscard]] std::pair<int, int> runAround(int row) const;
    void touchRun(int row);
    [[nodiscard]] QVariantMap runOf(int row) const;
    void setPhase(Phase phase);
    void setWaiting(bool waiting);
    [[nodiscard]] Item& at(int row) { return m_items[static_cast<std::size_t>(row)]; }

    ProviderFactory m_makeProvider;
    AiToolRunner* m_tools = nullptr;
    std::unique_ptr<AiProvider> m_provider;
    std::vector<Item> m_items;
    std::vector<AiTurn> m_history;
    AiContext m_context;
    QString m_snapshot;
    QString m_snapshotProblem;
    QJsonArray m_offered;
    std::vector<AiToolCall> m_queue;
    std::size_t m_next = 0;
    AiTurn m_round;
    int m_rounds = 0;
    int m_streamRow = -1;
    int m_thinkRow = -1;
    int m_toolRow = -1;
    std::vector<int> m_callRows;
    quint64 m_generation = 0;
    quint64 m_calls = 0;
    Phase m_phase = Phase::Idle;
    bool m_waiting = false;
    bool m_thought = false;
    bool m_pictures = true;
    bool m_sentPictures = false;
    bool m_retried = false;
};

}
