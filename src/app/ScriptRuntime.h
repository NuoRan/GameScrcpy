#pragma once

#include "ControlChannel.h"
#include "InputHost.h"
#include "InputReadGrants.h"
#include "LogicServices.h"
#include "ScriptWorkbenchConsole.h"

#include <gs/base/Clock.h>
#include <gs/input/Profile.h>
#include <gs/media/FrameSnapshot.h>
#include <gs/script/Report.h>
#include <gs/script/Services.h>

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QTimer>

#include <filesystem>
#include <map>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class QImage;

namespace gs::script {
class LogicHost;
}

namespace gs::app {

class InputController;

class LaneAccess
{
public:
    virtual ~LaneAccess() = default;

    virtual void attach(std::shared_ptr<const input::Profile> profile, std::shared_ptr<LaneObserver> observer) = 0;
    virtual void detach(std::shared_ptr<LaneObserver> observer) = 0;
    virtual void post(std::function<void(InputHost& host, Microseconds now)> task) = 0;
    virtual void claimKeys(std::vector<input::InputSource>) {}
    virtual void handOver(std::vector<std::string>) {}
    virtual void watch(std::shared_ptr<const input::Profile>, std::shared_ptr<LaneObserver>) {}
    virtual void unwatch(std::shared_ptr<LaneObserver>) {}
    virtual void watchKeys(std::shared_ptr<LaneObserver>, std::vector<input::InputSource>) {}
    [[nodiscard]] virtual const ControlChannel* channel() const noexcept = 0;
    [[nodiscard]] virtual std::optional<input::Vec2> cursor() const { return std::nullopt; }
    [[nodiscard]] virtual bool held(std::string_view) const { return false; }
};

class ControllerLane final : public LaneAccess
{
public:
    ControllerLane(InputController& input, QString serial, std::shared_ptr<ControlChannel> channel);

    void attach(std::shared_ptr<const input::Profile> profile, std::shared_ptr<LaneObserver> observer) override;
    void detach(std::shared_ptr<LaneObserver> observer) override;
    void post(std::function<void(InputHost& host, Microseconds now)> task) override;
    void claimKeys(std::vector<input::InputSource> keys) override;
    void handOver(std::vector<std::string> ids) override;
    void watch(std::shared_ptr<const input::Profile> profile, std::shared_ptr<LaneObserver> observer) override;
    void unwatch(std::shared_ptr<LaneObserver> observer) override;
    void watchKeys(std::shared_ptr<LaneObserver> observer, std::vector<input::InputSource> keys) override;
    [[nodiscard]] const ControlChannel* channel() const noexcept override { return m_channel.get(); }
    [[nodiscard]] std::optional<input::Vec2> cursor() const override;
    [[nodiscard]] bool held(std::string_view name) const override;

private:
    InputController& m_input;
    QString m_serial;
    std::shared_ptr<ControlChannel> m_channel;
};

struct LogicSource {
    std::filesystem::path folder;
    std::string entry;
    std::shared_ptr<const input::Profile> profile;
    QString id;
    QString name;
    QString device;
    QString deviceKey;
    QString serial;
    std::shared_ptr<media::FrameSnapshots> snapshots;
    bool task = false;
    std::string taskParams = "null";
    std::string taskHandler = "default";
    std::uint64_t gestureOffset = 0;
    QString extension;
    std::vector<std::string> permissions;
    bool followsProfile = false;
    InputGrant inputGrant;
};

class NoPhoneLane final : public LaneAccess
{
public:
    void attach(std::shared_ptr<const input::Profile> profile, std::shared_ptr<LaneObserver> observer) override;
    void detach(std::shared_ptr<LaneObserver> observer) override;
    void post(std::function<void(InputHost& host, Microseconds now)> task) override;
    [[nodiscard]] const ControlChannel* channel() const noexcept override { return nullptr; }
};

struct ScriptJob {
    std::filesystem::path folder;
    std::string entry;
    std::string handler = "default";
    QString name;
    std::function<void(const LogicReport& report)> reported;
    std::function<void(const QString& result, const QString& message)> ended;
};

class TaskLane final : public LaneAccess
{
public:
    explicit TaskLane(std::shared_ptr<LaneAccess> lane);

    void attach(std::shared_ptr<const input::Profile> profile, std::shared_ptr<LaneObserver> observer) override;
    void detach(std::shared_ptr<LaneObserver> observer) override;
    void post(std::function<void(InputHost& host, Microseconds now)> task) override;
    void claimKeys(std::vector<input::InputSource> keys) override;
    [[nodiscard]] const ControlChannel* channel() const noexcept override { return m_lane->channel(); }
    [[nodiscard]] std::optional<input::Vec2> cursor() const override { return m_lane->cursor(); }
    [[nodiscard]] bool held(std::string_view name) const override { return m_lane->held(name); }

private:
    std::shared_ptr<LaneAccess> m_lane;
    std::weak_ptr<LaneObserver> m_observer;
};

class ScriptRuntime final : public QObject
{
    Q_OBJECT

public:
    static constexpr std::size_t kFastActions = 100;

    ScriptRuntime(LogicSource source, std::shared_ptr<LaneAccess> lane, std::filesystem::path storeFolder,
                  QObject* parent = nullptr);
    ~ScriptRuntime() override;
    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;

    [[nodiscard]] const LogicSource& source() const noexcept { return m_source; }
    [[nodiscard]] bool running() const noexcept { return m_script != 0; }
    [[nodiscard]] bool halted() const noexcept { return m_halted; }
    [[nodiscard]] const QString& lastError() const noexcept { return m_lastError; }
    [[nodiscard]] std::filesystem::path storePath() const;
    void setDevice(const script::DeviceInfo& device);
    void setDeviceKey(const QString& key);
    void setFrameRate(double hz);
    void setKeymapOn(bool on);
    void setForeground(const QString& package, bool game);
    void setFocused(bool focused);
    void setProfileFact(const QString& id, const QString& name);
    void setConnected(bool connected);
    void setInputBox(const std::string& json);
    void reload();
    void setDebugging(bool on);
    [[nodiscard]] bool debugging() const noexcept { return m_debugging; }
    void setBreakpoints(const QStringList& places);
    void resumeDebug(quint64 pause, int mode);
    void answer(quint64 request, const QString& json);
    void refuse(quint64 request, const QString& message);
    void tell(const QString& kind, const QString& id, const QString& json);
    [[nodiscard]] std::uint64_t keepFrame(const QImage& picture, script::PictureSize source, quint64 frames);
    void forgetFrames(quint64 frames);
    [[nodiscard]] bool grab(quint64 request, bool color, LogicServices::FrameTaken taken);
    void ungrab(quint64 request);
    void allowInput();
    [[nodiscard]] const LogicCaller& caller() const noexcept { return m_caller; }
    [[nodiscard]] bool task() const noexcept { return m_source.task; }
    [[nodiscard]] bool sandboxed() const noexcept { return m_sandboxed; }
    [[nodiscard]] QVariantMap snapshot() const;
    [[nodiscard]] QStringList handedOver() const;

signals:
    void loaded();
    void failed(const QString& message);
    void tooFast();
    void storeBusy();
    void toast(const QString& text);
    void prompt(quint64 request, const QVariantMap& prompt);
    void promptWithdrawn(quint64 request);
    void drawn(quint64 id, const QVariantMap& drawing);
    void erased(quint64 id);
    void drawingsCleared();
    void appRequested(const QString& request, const QString& json);
    void clipboardWanted(quint64 request);
    void clipboardGiven(quint64 request, const QString& text);
    void inputWanted();
    void saveWanted(quint64 request, const QVariantList& changes, std::shared_ptr<const gs::input::Profile> profile);
    void reported(const gs::app::LogicReport& report);
    void progressed(const QString& text);
    void ended(const QString& result, const QString& message);
    void debugHits(const QVariantList& hits);
    void debugPaused(const QVariantMap& pause);
    void debugResumed(quint64 pause);
    void handedOverChanged(const QStringList& controls);

private:
    class Link;
    class StoreWriter;
    class Seer;

    void load();
    void handle(const script::Report& report);
    void debugReport(const script::Report& report);
    void announce(const script::Report& report);
    void watch();
    void restore();
    [[nodiscard]] QString problemOf(const script::Report& report) const;
    void serve(quint64 request, const std::string& service, const QString& json);
    void unserve(quint64 request, const std::string& service);

    LogicSource m_source;
    LogicCaller m_caller;
    std::shared_ptr<LaneAccess> m_lane;
    std::filesystem::path m_storeFolder;
    std::unique_ptr<StoreWriter> m_writer;
    std::unique_ptr<Seer> m_seer;
    std::shared_ptr<Link> m_link;
    std::unique_ptr<script::LogicHost> m_host;
    script::ScriptId m_script = 0;
    script::TaskId m_task = 0;
    bool m_ended = false;
    QFileSystemWatcher m_watcher;
    QTimer m_settle;
    bool m_halted = false;
    bool m_debugging = false;
    QStringList m_breakpoints;
    std::map<std::filesystem::path, std::filesystem::file_time_type> m_watchedScripts;
    QString m_lastError;
    bool m_sandboxed = false;
};

}
