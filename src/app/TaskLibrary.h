#pragma once

#include "Declarations.h"
#include "ExtensionContent.h"

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

class QJSEngine;
class QQmlEngine;

namespace gs::app {

using TaskParamSpec = DeclaredSpec;

[[nodiscard]] std::vector<TaskParamSpec> taskParamsIn(const QString& code);

struct TaskEntry {
    QString id;
    QString name;
    QString folder;
    QStringList phones;
    QString schedule = QStringLiteral("manual");
    QString time = QStringLiteral("08:00");
    QString onError = QStringLiteral("stop");
    bool wake = false;
    bool sleepAfter = false;
    QVariantMap values;
    std::vector<TaskParamSpec> params;
    QString lastScheduled;
    QList<int> days;
    bool readOnly = false;
    QString extensionId;
    QString extensionName;
    QString example;
};

struct TaskExample {
    QString id;
    QString name;
    QString description;
    QString icon;
    QString folder;
    QString schedule = QStringLiteral("manual");
    QString time = QStringLiteral("08:00");
    QList<int> days;
    std::vector<TaskParamSpec> params;
};

inline constexpr qint64 kScheduleWindowMs = 300'000;
inline constexpr int kScheduledConnectSeconds = 60;

enum class ScheduleVerdict : std::uint8_t { Idle, Due, Missed };

struct ScheduleCheck {
    ScheduleVerdict verdict = ScheduleVerdict::Idle;
    QDateTime at;
};

struct ScreenPlan {
    bool wake = false;
    bool sleepAfter = false;

    friend bool operator==(const ScreenPlan&, const ScreenPlan&) = default;
};

[[nodiscard]] QDateTime lastDueTime(const TaskEntry& entry, const QDateTime& now);
[[nodiscard]] ScheduleCheck checkSchedule(const TaskEntry& entry, const QDateTime& previous, const QDateTime& now);
[[nodiscard]] bool hasSchedule(const TaskEntry& entry) noexcept;
[[nodiscard]] bool closesToTray(bool settingOn, const std::vector<TaskEntry>& entries);
[[nodiscard]] ScreenPlan screenPlanFor(bool scheduledRun, bool connectedForRun) noexcept;
[[nodiscard]] bool sleepsWhenDone(bool woke, int otherRunsOnPhone) noexcept;

class TaskLibrary final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY tasksChanged FINAL)
    Q_PROPERTY(QVariantList examples READ examples NOTIFY examplesChanged FINAL)

public:
    explicit TaskLibrary(std::filesystem::path directory, QObject* parent = nullptr);
    ~TaskLibrary() override;
    TaskLibrary(const TaskLibrary&) = delete;
    TaskLibrary& operator=(const TaskLibrary&) = delete;

    static TaskLibrary* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    [[nodiscard]] static TaskLibrary* instance() noexcept;

    [[nodiscard]] QVariantList tasks() const { return m_list; }
    [[nodiscard]] const std::vector<TaskEntry>& entries() const noexcept { return m_entries; }
    [[nodiscard]] const TaskEntry* find(const QString& id) const;
    [[nodiscard]] QString paramsJson(const QString& id) const;
    [[nodiscard]] static QString entryName() { return QStringLiteral("task.ts"); }

    Q_INVOKABLE QString add(const QString& name);
    QString addWithCode(const QString& name, const QString& code, const QString& declarations);
    Q_INVOKABLE bool remove(const QString& id);
    Q_INVOKABLE bool rename(const QString& id, const QString& name);
    Q_INVOKABLE bool setPhones(const QString& id, const QStringList& serials);
    Q_INVOKABLE bool setSchedule(const QString& id, const QString& mode, const QString& time);
    Q_INVOKABLE bool setDays(const QString& id, const QList<int>& days);
    Q_INVOKABLE bool setValue(const QString& id, const QString& name, const QVariant& value);
    Q_INVOKABLE bool setOption(const QString& id, const QString& key, const QVariant& value);
    Q_INVOKABLE void reveal(const QString& id) const;
    Q_INVOKABLE void reload();
    bool markScheduled(const QString& id, const QString& day);
    [[nodiscard]] QVariantList history() const;
    void saveHistory(const QVariantList& history) const;
    void setExtensionTasks(std::vector<ExtensionTask> tasks);
    Q_INVOKABLE QString copyToMine(const QString& id);
    Q_INVOKABLE bool exportPackage(const QString& id, const QString& fileOrUrl) const;

    [[nodiscard]] QVariantList examples() const { return m_exampleList; }
    [[nodiscard]] const std::vector<TaskExample>& exampleTasks() const noexcept { return m_examples; }
    [[nodiscard]] const TaskExample* findExample(const QString& id) const;
    void setExamplesFolder(std::filesystem::path folder);
    Q_INVOKABLE QString copyExample(const QString& id);

signals:
    void tasksChanged();
    void examplesChanged();
    void taskCreated(const QString& id);
    void taskRemoved(const QString& id);

private:
    TaskEntry* entryOf(const QString& id);
    bool write(const TaskEntry& entry);
    void refreshList();
    void watch();

    std::filesystem::path m_directory;
    std::vector<ExtensionTask> m_extensionTasks;
    std::vector<TaskEntry> m_entries;
    QVariantList m_list;
    QFileSystemWatcher m_watcher;
    QTimer m_settle;
    std::filesystem::path m_examplesDirectory;
    std::vector<TaskExample> m_examples;
    QVariantList m_exampleList;
};

}
