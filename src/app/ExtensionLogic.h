#pragma once

#include "ExtensionContent.h"
#include "ScriptRuntime.h"

#include <gs/base/Clock.h>
#include <gs/input/Profile.h>
#include <gs/media/FrameSnapshot.h>
#include <gs/script/Services.h>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace gs::app {

class ExtensionLogic final : public QObject
{
    Q_OBJECT

public:
    struct Phone {
        QString serial;
        QString name;
        QString deviceKey;
        const ControlChannel* channel = nullptr;
        std::function<std::shared_ptr<LaneAccess>()> lane;
        std::shared_ptr<media::FrameSnapshots> snapshots;
        std::shared_ptr<const input::Profile> profile;
        script::DeviceInfo device;
        double frameRate = 60.0;
    };

    using FrontPhone = std::function<QString()>;
    using ProfileSaver = std::function<bool(const QString& serial, const input::Profile& profile)>;

    explicit ExtensionLogic(std::filesystem::path storeFolder, QObject* parent = nullptr);
    ~ExtensionLogic() override;
    ExtensionLogic(const ExtensionLogic&) = delete;
    ExtensionLogic& operator=(const ExtensionLogic&) = delete;

    void setScripts(std::vector<ExtensionScript> scripts);
    void startSession(Phone phone);
    void updateSession(const QString& serial, const script::DeviceInfo& device, const QString& deviceKey);
    void endSession(const QString& serial);
    void endSessions();
    void setClock(const Clock* clock) noexcept { m_clock = clock; }
    void setFrontPhone(FrontPhone front) { m_front = std::move(front); }
    void setProfileSaver(ProfileSaver saver) { m_saver = std::move(saver); }
    bool answerPrompt(quint64 id, const QString& json);
    bool cancelPrompt(quint64 id);

    [[nodiscard]] const std::vector<ExtensionScript>& scripts() const noexcept { return m_scripts; }
    [[nodiscard]] bool runs(const QString& serial, const ControlChannel* channel) const;
    [[nodiscard]] std::vector<ScriptRuntime*> appRuntimes() const;
    [[nodiscard]] std::vector<ScriptRuntime*> sessionRuntimes(const QString& serial) const;

signals:
    void sessionStarted(const QString& serial, gs::app::ScriptRuntime* runtime);
    void toast(const QString& serial, const QString& text);
    void requested(const QString& serial, const QString& source, const QString& request, const QString& json);
    void failed(const QString& serial, const QString& message);
    void reported(const QString& serial, const gs::app::LogicReport& report);
    void prompted(const QString& serial, quint64 id, const QVariantMap& prompt);
    void promptWithdrawn(const QString& serial, quint64 id);
    void saveWanted(const QString& serial, quint64 id, const QVariantMap& prompt);
    void drawn(const QString& serial, quint64 id, const QVariantMap& drawing);
    void erased(const QString& serial, quint64 id);
    void clipboardWanted(const QString& serial, gs::app::ScriptRuntime* runtime, quint64 request);
    void clipboardGiven(const QString& serial, gs::app::ScriptRuntime* runtime, quint64 request, const QString& text);
    void stoppedWorking(const QString& extensionId, const QString& extensionName);

private:
    struct Running {
        ExtensionScript script;
        std::unique_ptr<ScriptRuntime> runtime;
    };

    struct Session {
        Phone phone;
        std::vector<Running> running;
    };

    struct PendingPrompt {
        quint64 run = 0;
        QString serial;
        QPointer<ScriptRuntime> runtime;
        quint64 request = 0;
        QString kind;
        std::shared_ptr<const input::Profile> save;
    };

    struct ShownDrawing {
        quint64 run = 0;
        quint64 own = 0;
        QString serial;
    };

    void syncApps();
    void syncSession(Session& session);
    [[nodiscard]] std::unique_ptr<ScriptRuntime> startScript(const ExtensionScript& script, const Phone* phone);
    void wireAsking(ScriptRuntime& runtime, quint64 run, const QString& serial, const QString& extension);
    void wireDrawing(ScriptRuntime& runtime, quint64 run, const QString& serial, const QString& extension);
    void cancelPromptsOn(const QString& serial);
    void eraseDrawings(quint64 run);
    void dropRun(quint64 run);
    void appStopped(const ExtensionScript& script, ScriptRuntime& runtime);
    void restartApp(const ExtensionScript& script, const QPointer<ScriptRuntime>& crashed);
    void stopExtension(const QString& id, const QString& name);
    [[nodiscard]] std::uint64_t nextGestureOffset();
    [[nodiscard]] quint64 nextShown();

    std::filesystem::path m_storeFolder;
    std::vector<ExtensionScript> m_scripts;
    std::vector<Running> m_apps;
    std::map<QString, Session> m_sessions;
    std::uint64_t m_lastSlot = 0;
    quint64 m_lastRun = 0;
    quint64 m_lastShown = 0;
    std::map<quint64, PendingPrompt> m_prompts;
    std::map<quint64, ShownDrawing> m_drawings;
    std::map<QString, std::vector<Microseconds>> m_crashes;
    std::set<QString> m_stopped;
    const Clock* m_clock = nullptr;
    FrontPhone m_front;
    ProfileSaver m_saver;
};

}
