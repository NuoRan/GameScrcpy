#pragma once

#include <gs/base/Expected.h>
#include <gs/script/Session.h>
#include <gs/script/TestRun.h>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <optional>
#include <vector>

namespace gs::input {
struct Profile;
}

namespace gs::app {

class TaskLibrary;

struct MadeTask {
    QString id;
    QString name;
};

[[nodiscard]] QString recordingTitle(const QString& recording);
[[nodiscard]] std::optional<QString> taskCodeFor(const script::Session& session, const input::Profile* profile);
[[nodiscard]] Expected<MadeTask, QString> turnRecordingIntoTask(TaskLibrary& tasks, const QString& recording,
                                                                const input::Profile* profile);
[[nodiscard]] script::ReplayProfile replayProfileOf(const input::Profile& profile);
[[nodiscard]] script::ReplayKit replayKit();
[[nodiscard]] Expected<QString, QString> saveRecordingTest(const QString& profileFolder, const QString& recording);
[[nodiscard]] std::vector<script::TestOutcome> runLogicTests(const QString& folder);
[[nodiscard]] QVariantMap shownTestResult(const script::TestOutcome& outcome);

class LogicTests : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Tests are run from the logic view")
    Q_PROPERTY(bool running READ running NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList results READ results NOTIFY changed FINAL)
    Q_PROPERTY(QString note READ note NOTIFY changed FINAL)

public:
    explicit LogicTests(QObject* parent = nullptr);
    ~LogicTests() override;
    LogicTests(const LogicTests&) = delete;
    LogicTests& operator=(const LogicTests&) = delete;

    [[nodiscard]] bool running() const noexcept { return m_running; }
    [[nodiscard]] QVariantList results() const { return m_results; }
    [[nodiscard]] QString note() const { return m_note; }
    void run(const QString& folder);
    void show(const std::vector<script::TestOutcome>& outcomes);

signals:
    void changed();

private:
    bool m_running = false;
    QVariantList m_results;
    QString m_note;
};

}
