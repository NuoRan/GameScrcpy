#pragma once

#include "CompanionInput.h"
#include "CompanionLan.h"
#include "CompanionLink.h"
#include "CompanionSync.h"
#include "ControlChannel.h"
#include "DeviceBatch.h"
#include "DeviceCommands.h"
#include "FailureAdvice.h"
#include "FileTransfers.h"
#include "FilterBudget.h"
#include "LatencyMeter.h"
#include "PictureTouch.h"
#include "ProvidedDevice.h"
#include "ScriptWorkbenchConsole.h"
#include "StatusProblem.h"
#include "SyncGroups.h"
#include "TransportPhones.h"
#include "TrayNotices.h"

#include <gs/adb/AdbClient.h>
#include <gs/adb/DeviceIdentity.h>
#include <gs/adb/DeviceList.h>
#include <gs/adb/DeviceTracker.h>
#include <gs/input/DeviceEvents.h>
#include <gs/platform/Usb.h>
#include <gs/script/Services.h>
#include <gs/session/AdbServerHost.h>
#include <gs/session/SessionState.h>

#include <QAbstractListModel>
#include <QDateTime>
#include <QSortFilterProxyModel>
#include <QImage>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

class QJSEngine;
class QKeyEvent;
class QQmlEngine;
class QWindow;

namespace gs::render {
class FrameSlot;
class GraphicsDevice;
}

namespace gs::media {
class FrameSnapshots;
}

namespace gs::input {
struct Profile;
}

namespace gs::platform {
class ForegroundWatcher;
class TrayNotice;
}

namespace gs::app {

class AppActions;
enum class PackageAction : std::uint8_t;
class AppCatalog;
class AppSettings;
class AoaSession;
class ControlChannel;
struct AppAvailability;
class DemoFeed;
class DeviceIdentifier;
class ExtensionHost;
class ExtensionLogic;
class ExtensionTouch;
class GridWindow;
class InputController;
class MirrorWindow;
class NativeJobs;
class PhoneFiles;
class ProfileEditor;
class ProfileLibrary;
class RecordingTap;
class Recordings;
class ScriptRuntime;
class ScriptWorkbench;
struct ScriptJob;
struct TaskEntry;
class TaskLibrary;
class ComponentLibrary;
class SessionController;
class SystemPanelService;
enum class PanelReach : std::uint8_t;
class UsbPhoneWatcher;
class VisionWorkbench;
class CaptureCardWatcher;
class CaptureFeed;
class CaptureMatcher;
struct PhoneSource;
struct CaptureRow;
enum class CaptureEvent : std::uint8_t;
enum class AoaState : std::uint8_t;
class TouchBoxSession;
class KeymapBackgrounds;
struct StoredBackground;
struct DevicePresentation;
struct MediaSourceDevice;

class DeviceManager : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString setupProblem READ setupProblem NOTIFY setupChanged FINAL)
    Q_PROPERTY(QString setupAction READ setupAction NOTIFY setupChanged FINAL)
    Q_PROPERTY(bool adbReady READ adbReady NOTIFY setupChanged FINAL)
    Q_PROPERTY(bool canRetryAdb READ canRetryAdb NOTIFY setupChanged FINAL)
    Q_PROPERTY(bool adbStarting READ adbStarting NOTIFY setupChanged FINAL)
    Q_PROPERTY(QString renderNotice READ renderNotice NOTIFY setupChanged FINAL)
    Q_PROPERTY(QString renderDetail READ renderDetail NOTIFY setupChanged FINAL)
    Q_PROPERTY(QAbstractItemModel* sorted READ sorted CONSTANT FINAL)
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY countsChanged FINAL)
    Q_PROPERTY(int mirroringCount READ mirroringCount NOTIFY countsChanged FINAL)
    Q_PROPERTY(QStringList onlineSerials READ onlineSerials NOTIFY countsChanged FINAL)
    Q_PROPERTY(QVariantMap leadDevice READ leadDevice NOTIFY countsChanged FINAL)
    Q_PROPERTY(QVariantList mirrors READ mirrors NOTIFY mirrorsChanged FINAL)
    Q_PROPERTY(QVariantList taskRuns READ taskRuns NOTIFY taskRunsChanged FINAL)
    Q_PROPERTY(QVariantList taskHistory READ taskHistory NOTIFY taskRunsChanged FINAL)
    Q_PROPERTY(gs::app::DeviceBatch* batch READ batch CONSTANT FINAL)
    Q_PROPERTY(QVariantList otherVideoDevices READ otherVideoDevices NOTIFY capturesChanged FINAL)
    Q_PROPERTY(int backgroundRevision READ backgroundRevision NOTIFY backgroundsChanged FINAL)
    Q_PROPERTY(int linkRevision READ linkRevision NOTIFY linkRevisionChanged FINAL)

public:
    enum Role {
        SerialRole = Qt::UserRole + 1,
        NameRole,
        DeviceStateRole,
        StatusRole,
        ProblemRole,
        ActionRole,
        CanConnectRole,
        ConnectedRole,
        BusyRole,
        AudioRole,
        MutedRole,
        VolumeRole,
        DiagnosticsRole,
        ScreenControlRole,
        ScreenOffRole,
        RotationControlRole,
        OrientationRole,
        TransportRole,
        PillRole,
        ToneRole,
        StageRole,
        FailedRole,
        MetaRole,
        RankRole,
        StatsRole,
        WirelessRole,
        TunnelOfferRole,
        CardRole,
        LinksRole,
        ThumbnailRole,
        DeviceIdRole,
        GuideRole,
        HostRole,
        FactsRole,
        HelpRole,
        CaptureRole,
        PictureRole,
        ProvidedRole,
        TransportedRole,
    };
    Q_ENUM(Role)

    DeviceManager(adb::AdbClient adb, std::filesystem::path adbExecutable, std::filesystem::path serverPackage,
                  const render::GraphicsDevice* graphics, AppSettings& settings, QObject* parent = nullptr);
    ~DeviceManager() override;

    static DeviceManager* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString setupProblem() const;
    [[nodiscard]] QString setupAction() const;
    [[nodiscard]] bool adbReady() const { return m_adbReady; }
    [[nodiscard]] bool canRetryAdb() const;
    [[nodiscard]] bool adbStarting() const { return m_adbStarting; }
    [[nodiscard]] QString renderNotice() const { return m_renderNotice; }
    [[nodiscard]] QString renderDetail() const { return m_renderDetail; }
    [[nodiscard]] QAbstractItemModel* sorted() { return &m_sorted; }
    [[nodiscard]] int deviceCount() const;
    [[nodiscard]] int mirroringCount() const;
    [[nodiscard]] QStringList onlineSerials() const;
    [[nodiscard]] QVariantMap leadDevice() const;
    [[nodiscard]] QVariantList mirrors() const;
    [[nodiscard]] std::vector<DeviceFacts> facts() const;
    [[nodiscard]] std::optional<DeviceFacts> facts(const QString& serial) const;
    void retranslate();
    void setExtensions(ExtensionHost* extensions);
    void setExtensionLogic(ExtensionLogic* logic);
    void setNativeJobs(NativeJobs* jobs);
    void setProfileLibrary(ProfileLibrary* library);
    void setQmlEngine(QQmlEngine* engine);
    void setCompanionPackage(const std::filesystem::path& apk, const std::filesystem::path& metadata);
    void closeWindows();
    bool addDemoDevice(const QString& recording, const QString& game = {});
    void setFullScreen(const QString& serial, bool on);
    void setMini(const QString& serial, bool on);
    void setEditing(const QString& serial, bool on);
    void editorAction(const QString& serial, const QString& action);
    void setMeasuringLatency(const QString& serial, bool on);
    void setRecording(const QString& serial, bool on);
    void showSystemPanel(const QString& serial, protocol::SystemPanelAction action);
    void toggleToolbar(const QString& serial);
    void setKeymap(const QString& serial, bool on);
    void setCameraLocked(const QString& serial, bool locked);
    void setKeyHints(bool shown);
    void stepProfile(const QString& serial, int step);
    [[nodiscard]] bool cameraLockedFor(const QString& serial) const;
    [[nodiscard]] bool keymapBinds(const QString& serial, const QKeyEvent& key) const;
    void setInputProfile(std::shared_ptr<const input::Profile> profile, std::filesystem::path folder = {});
    void setScriptStore(std::filesystem::path folder);
    void forceTouchBackend(TouchBackend backend);
    void setAppCatalog(AppCatalog* catalog);
    [[nodiscard]] AppAvailability appAvailability(const QString& serial) const;
    bool requestApps(const QString& serial, std::uint32_t requestId);
    bool requestAppIcon(const QString& serial, std::uint32_t requestId, const QString& package, int sizePx);
    void launchApp(const QString& serial, const QString& package, const QString& label, bool independent);
    Q_INVOKABLE void pushFiles(const QStringList& serials, const QStringList& localPaths,
                               const QString& remoteDir = QStringLiteral("/sdcard/Download"));
    Q_INVOKABLE void dropOnMirror(const QString& serial, const QList<QUrl>& urls);
    Q_INVOKABLE void installApk(const QStringList& serials, const QString& apkPath);

    Q_INVOKABLE void retryAdb();
    Q_INVOKABLE void connectDevice(const QString& serial);
    Q_INVOKABLE void disconnectDevice(const QString& serial);
    Q_INVOKABLE void pressKey(const QString& serial, int keycode);
    Q_INVOKABLE void setMuted(const QString& serial, bool muted);
    Q_INVOKABLE void setVolume(const QString& serial, double volume);
    Q_INVOKABLE void setDiagnostics(const QString& serial, bool shown);
    Q_INVOKABLE void setScreenOff(const QString& serial, bool off);
    Q_INVOKABLE void setOrientation(const QString& serial, int orientation);
    Q_INVOKABLE void setTransport(const QString& serial, int transport);
    Q_INVOKABLE void showWindow(const QString& serial);
    Q_INVOKABLE void notifyMain(const QString& title, const QString& text);
    Q_INVOKABLE void showGrid();
    Q_INVOKABLE void tileMirrors();
    Q_INVOKABLE void dismissLatencyTest(const QString& serial);
    Q_INVOKABLE bool saveEditing(const QString& serial);
    Q_INVOKABLE void finishEditing(const QString& serial, bool save);
    Q_INVOKABLE bool editProfile(const QString& serial, const QString& id);
    Q_INVOKABLE bool openDraftPreview(const QString& target);
    Q_INVOKABLE void applyDraftPreview(const QString& serial);
    Q_INVOKABLE void discardDraftPreview(const QString& serial);
    Q_INVOKABLE void closeDraftPreview(const QString& serial);
    bool openDraftPreviewAt(const QString& target, const QString& serial, QString& problem);
    Q_INVOKABLE void showAssistant(const QString& serial);
    Q_INVOKABLE bool saveEditingAs(const QString& serial, const QString& name);
    Q_INVOKABLE bool restoreEditing(const QString& serial);
    Q_INVOKABLE bool restoreProfile(const QString& id);
    Q_INVOKABLE void setPictureFrozen(const QString& serial, bool frozen);
    Q_INVOKABLE void zoomPicture(const QString& serial, double zoom, double anchorX, double anchorY);
    Q_INVOKABLE void scrollPicture(const QString& serial, double dx, double dy);
    Q_INVOKABLE void focusPicture(const QString& serial);
    Q_INVOKABLE void useProfile(const QString& serial, const QString& id);
    Q_INVOKABLE void closeStart(const QString& serial, const QString& package);
    Q_INVOKABLE void answerPrompt(const QString& serial, quint64 request, const QString& json);
    Q_INVOKABLE void setPrompting(const QString& serial, bool on);
    Q_INVOKABLE void toastAction(const QString& serial, int which = 0);
    Q_INVOKABLE void chooseLink(const QString& serial);
    Q_INVOKABLE void setControlOrientation(const QString& serial, int orientation);
    Q_INVOKABLE void setPhoneStatus(const QString& serial, bool on);
    Q_INVOKABLE QStringList noPictureLimits(const QString& serial = {}) const;
    Q_INVOKABLE QVariantMap phoneDetails(const QString& deviceId) const;
    Q_INVOKABLE QString openVisionWorkbench(const QString& serial, const QString& profileId);
    QString requestMaterials(const QString& client, const QString& serial, const QString& profileId,
                             const QString& profileName, const QString& folder, const QVariantList& items,
                             std::function<void(const QVariantList&)> done);
    void endMaterials(const QString& client);
    QString pickOnPicture(const QString& serial, const QString& folder, const QString& kind, const QString& prompt,
                          std::function<void(const QString&)> done);

    void feedInput(const QString& serial, const input::DeviceEvent& event);
    [[nodiscard]] QString focusedPhone() const;
    [[nodiscard]] std::vector<LogicPhone> logicPhones() const;
    [[nodiscard]] QImage thumbnailOf(const QString& serial) const;
    [[nodiscard]] LogicConsole& logicConsole() noexcept { return m_logicConsole; }
    bool restartLogic(const QString& serial);
    bool debugLogic(const QString& serial, bool on);
    void setLogicBreakpoints(const QString& serial, const QStringList& places);
    void resumeLogic(const QString& serial, quint64 pause, int mode);
    [[nodiscard]] QVariantMap logicSnapshot(const QString& serial) const;
    Q_INVOKABLE QString openLogic(const QString& id);
    Q_INVOKABLE QString openLogicFor(const QString& serial);
    Q_INVOKABLE QString testOnRecording(const QString& serial, const QString& profileId, const QString& recording);
    Q_INVOKABLE int listRecordings(const QString& serial);

    void setTaskLibrary(TaskLibrary* tasks);
    void setComponentLibrary(ComponentLibrary* components);
    [[nodiscard]] QVariantList taskRuns() const;
    [[nodiscard]] QVariantList taskHistory() const { return m_taskHistory; }
    [[nodiscard]] bool taskRunning(const QString& id, const QString& serial) const;
    Q_INVOKABLE QString startTask(const QString& id, const QString& serial);
    Q_INVOKABLE void stopTask(const QString& id, const QString& serial);
    Q_INVOKABLE QString openTaskLogic(const QString& id);
    Q_INVOKABLE QString openComponent(const QString& ref);
    Q_INVOKABLE bool hideToTrayOnClose();

    [[nodiscard]] DeviceBatch* batch() const noexcept { return m_batch; }
    Q_INVOKABLE QVariantList syncCandidates(const QString& master) const;
    Q_INVOKABLE QString setSyncFollowers(const QString& master, const QStringList& followers);
    Q_INVOKABLE void stopSync(const QString& master);

    bool addDemoCapture(const QString& recording);
    [[nodiscard]] QVariantList otherVideoDevices() const { return m_otherVideoDevices; }
    Q_INVOKABLE void setCaptureOption(const QString& serial, const QString& key, const QVariant& value);
    Q_INVOKABLE void hideCaptureCard(const QString& serial);
    Q_INVOKABLE void showCaptureCard(const QString& serial);
    Q_INVOKABLE void beginCaptureCrop(const QString& serial);
    Q_INVOKABLE void finishCaptureCrop(const QString& serial, bool apply, double x, double y, double width,
                                       double height);
    Q_INVOKABLE void autoCaptureCrop(const QString& serial);

    [[nodiscard]] int linkRevision() const noexcept { return m_linkRevision; }
    Q_INVOKABLE QVariantMap pictureAndTouch(const QString& deviceId) const;
    Q_INVOKABLE QVariantList phonesForCapture(const QString& captureSerial) const;
    Q_INVOKABLE void assignCapture(const QString& captureSerial, const QString& phoneDeviceId);
    Q_INVOKABLE int suggestPhoneForCapture(const QString& captureSerial);

    [[nodiscard]] int backgroundRevision() const noexcept { return m_backgroundRevision; }
    Q_INVOKABLE QVariantMap keymapBackground(const QString& serial) const;
    Q_INVOKABLE bool importBackground(const QString& serial, const QUrl& file);
    Q_INVOKABLE bool shootBackground(const QString& serial);
    Q_INVOKABLE void clearBackground(const QString& serial);
    Q_INVOKABLE void runDeviceAction(const QString& serial, const QString& action);

    [[nodiscard]] std::optional<script::DeviceInfo> deviceInfo(const QString& serial) const;
    [[nodiscard]] std::shared_ptr<media::FrameSnapshots> snapshotsOf(const QString& serial) const;
    bool setAudioTap(const QString& serial,
                     std::function<void(std::span<const float>, int, int, std::int64_t)> tap);
    bool requestScreenshot(const QString& serial, std::function<void(const QImage&)> done);
    [[nodiscard]] QString startScriptJob(const QString& serial, ScriptJob job, quint64& number);
    void stopScriptJob(quint64 number);
    [[nodiscard]] ProfileEditor* editorOf(const QString& serial) const;
    QVariantMap endRecording(const QString& serial);

signals:
    void detailsChanged();
    void setupChanged();
    void countsChanged();
    void devicePanelWanted(const QString& extension, const QString& panel, const QString& device, const QString& name);
    void mirrorsChanged();
    void filterBudgetExceeded(const QString& serial, const QString& filter, double frameMs);
    void appCommandRequested(const QString& command, const QString& serial);
    void installUpdated(const QVariantMap& job);
    void logicReported(const QString& serial, const gs::app::LogicReport& report);
    void machineStateChanged(const QString& serial, const QString& folder, const QVariantMap& state);
    void logicDebugHits(const QString& serial, const QString& folder, const QVariantList& hits);
    void logicPaused(const QString& serial, const QString& folder, const QVariantMap& pause);
    void logicResumed(const QString& serial, const QString& folder, quint64 pause);
    void logicChanged(const QString& serial);
    void taskRunsChanged();
    void mainWindowWanted();
    void tasksPageWanted(const QString& task);
    void assistantWanted(const QString& serial);
    void usbDriverReady(const QString& name);
    void recordingFinished(const QVariantMap& info);
    void recordingNotice(const QString& text, const QString& tone);
    void transferUpdated(const QVariantMap& job);
    void capturesChanged();
    void backgroundsChanged();
    void extensionStopped(const QString& name);
    void linkRevisionChanged();
    void phoneSuggested(int ticket, const QString& captureSerial, const QString& deviceId, double score);

private:
    struct TaskRun {
        quint64 number = 0;
        QString task;
        QString serial;
        QString name;
        QString phone;
        std::unique_ptr<ScriptRuntime> runtime;
        QString progress;
        QDateTime started;
        int attempt = 0;
        std::set<quint64> drawings;
        std::function<void(const LogicReport&)> reported;
        std::function<void(const QString&, const QString&)> ended;
        bool scheduled = false;
        bool woke = false;
    };

    struct ScheduledStart {
        QString task;
        QString serial;
        std::chrono::steady_clock::time_point deadline;
        int tries = 0;
    };

    QString launchTask(const QString& id, const QString& serial, int attempt, bool scheduled = false,
                       bool woke = false);
    void wireTask(TaskRun& run);
    [[nodiscard]] TaskRun* taskRun(quint64 number);
    void endTask(quint64 number, const QString& result, const QString& message);
    void stopTasksOn(const QString& serial, const QString& result);
    void checkSchedules();
    void queueScheduledStart(const TaskEntry& entry, const QString& serial);
    void advanceScheduledStarts();
    void startScheduled(const QString& task, const QString& serial, bool connected);
    void scheduledUnreachable(const ScheduledStart& start);
    void connectWireless(const QString& address, bool afterRestart = false);
    void recordTaskRun(QVariantMap entry);
    void noticeTaskEnd(const QString& task, const QString& name, const QString& phone, const QString& result,
                       const QString& message);
    void showTaskNotice(const QString& task, const QString& title, const QString& text);
    void showNotice(const QString& title, const QString& text, NoticeTarget target);
    void openNotice();
    [[nodiscard]] int runsOn(const QString& serial) const;
    platform::TrayNotice& tray();
    void setTrayMenu();

    struct PushedSync {
        std::shared_ptr<ControlChannel> channel;
        std::vector<std::pair<std::shared_ptr<ControlChannel>, std::shared_ptr<const input::Profile>>> followers;

        friend bool operator==(const PushedSync&, const PushedSync&) = default;
    };

    void refreshSync();
    void pushFollowers();
    void refreshBatchPhones();
    void forwardScroll(const QString& serial, input::Vec2 position, int horizontal, int vertical);

    struct Row {
        QString serial;
        QString model;
        adb::DeviceState state = adb::DeviceState::Unknown;
        bool listed = true;
        session::SessionState session;
        QString manufacturer;
        QString refusalProblem;
        QString refusalAction;
        protocol::ErrorCode refusalCode = protocol::ErrorCode::None;
        std::optional<protocol::AudioStatus> silenceStatus;
        bool silent = false;
        bool muted = false;
        double volume = 1.0;
        bool screenOff = false;
        int orientation = 0;
        session::TransportChoice transport = session::TransportChoice::AdbTunnel;
        QString deviceId;
        QString androidRelease;
        QString marketName;
        bool identifying = false;
        bool transportChosen = false;
        bool wasOnline = false;
        std::chrono::steady_clock::time_point graceUntil{};
        std::optional<session::TransportKind> link;
        bool diagnostics = false;
        QVariantMap stats;
        QImage thumbnail;
        std::chrono::steady_clock::time_point sampledAt;
        std::uint64_t framesTaken = 0;
        std::uint64_t framesReplaced = 0;
        ProblemFilter problems;
        SessionController* controller = nullptr;
        MirrorWindow* mirror = nullptr;
        std::unique_ptr<DemoFeed> demo;
        std::shared_ptr<ControlChannel> demoChannel;
        std::shared_ptr<ControlSink> demoSink;
        int demoNumber = 0;
        std::unique_ptr<CompanionLink> companion;
        quint64 companionTicket = 0;
        bool companionChosen = false;
        bool companionHeld = false;
        bool companionRetried = false;
        bool companionOverlay = true;
        std::optional<protocol::CursorState> loggedCursor;
        bool unsavedPhoneStatus = false;
        QString phoneStatusText;
        PhoneInputBox inputBox;
        bool enterToCompanion = false;
        bool companionCursor = false;
        std::uint32_t companionApplied = 0;
        std::string lanToken;
        LanKey lanKey{};
        std::optional<LanPairing> lanPairing;
        FilterBudget budget;
        std::vector<render::VideoFilter> filters;
        QString profileId;
        std::shared_ptr<const input::Profile> profile;
        QString foregroundApp;
        bool foregroundGame = false;
        QStringList closedStarts;
        QSize screen;
        double refreshHz = 0;
        int rotation = 0;
        QStringList codecs;
        bool latencyCard = false;
        std::optional<LatencyResult> latencyResult;
        QString editingId;
        QString host;
        QString appPackage;
        QString appLabel;
        std::unique_ptr<ScriptRuntime> logic;
        std::vector<std::pair<QPointer<ScriptRuntime>, quint64>> clipboardReads;
        struct PendingSave {
            quint64 request = 0;
            std::shared_ptr<const input::Profile> profile;
        };
        std::optional<PendingSave> pendingSave;
        bool prompting = false;
        bool focused = false;
        std::function<void()> toastAction;
        std::function<void()> toastSecondAction;
        bool reconnectWhenStopped = false;
        bool aoaOnly = false;
        std::optional<platform::UsbPhone> usb;
        AoaSession* aoa = nullptr;
        std::shared_ptr<RecordingTap> recordingTap;
        std::unique_ptr<CaptureRow> capture;
        std::optional<settings::PictureSource> picture;
        QString pictureCard;
        std::shared_ptr<render::FrameSlot> pictureFrames;
        std::shared_ptr<ControlChannel> pictureChannel;
        QString pictureNotice;
        bool skipCapture = false;
        TouchBoxSession* touchBox = nullptr;
        std::optional<ProvidedDevice> provided;
        QVariantMap providedJob;
        std::optional<TransportPhone> transported;
        std::uint32_t shotPending = 0;
        quint64 shotTicket = 0;
        quint64 shotAskedTicket = 0;

        [[nodiscard]] QString name() const;
    };

    [[nodiscard]] bool canConnect(const Row& row) const;
    [[nodiscard]] static bool syncable(const Row& row);
    [[nodiscard]] static bool streaming(const Row& row);
    [[nodiscard]] static bool connecting(const Row& row);
    [[nodiscard]] static QString vendorOf(const Row& row);
    [[nodiscard]] static QString guideOf(const Row& row);
    [[nodiscard]] static std::optional<Advice> adviceOf(const Row& row);
    [[nodiscard]] static bool hasCapability(const Row& row, std::uint32_t capability);
    [[nodiscard]] DeviceFacts factsOf(const Row& row) const;
    [[nodiscard]] static PanelReach panelReachOf(const Row& row);
    void syncPanelReach();
    [[nodiscard]] std::shared_ptr<const input::Profile> effectiveProfile(const Row& row) const;
    [[nodiscard]] bool keymapOf(const Row& row) const;
    [[nodiscard]] QString rememberedProfile(const Row& row) const;
    [[nodiscard]] QString profileName(const QString& id) const;
    void restoreProfile(Row& row);
    void applyProfile(Row& row);
    [[nodiscard]] std::pair<QString, QString> profileInfoOf(const Row& row) const;
    void tellTasksProfile(const Row& row);
    void selectProfile(Row& row, const QString& id, bool chosenByUser);
    void syncInputProfile();
    void syncSessionScripts(Row& row);
    void forSessionScripts(const QString& serial, const std::function<void(ScriptRuntime& runtime)>& apply) const;
    void syncLogic(Row& row);
    [[nodiscard]] static script::DeviceInfo deviceInfoOf(const Row& row);
    [[nodiscard]] static QString logicKeyOf(const Row& row);
    void logicFailed(const QString& serial, const QString& text);
    void logicTooFast(const QString& serial);
    void logicRequest(const QString& serial, const QString& source, const QString& request, const QString& json);
    void showActionWheel(const QString& control, bool open, int cell);
    void showNotification(const QString& serial, const QString& title, const QString& text);
    void readClipboardFor(const QString& serial, ScriptRuntime& runtime, quint64 request);
    void askToSave(const QString& serial, ScriptRuntime& runtime, quint64 request, const QVariantList& changes,
                   std::shared_ptr<const input::Profile> profile);
    void finishSave(Row& row, bool confirmed);
    void askToRead(const QString& serial, ScriptRuntime& runtime, quint64 request);
    [[nodiscard]] bool answerRead(const QString& serial, quint64 request, const QString& json);
    void dropReadAsk(const ScriptRuntime* runtime);
    void writeClipboardFor(const QString& serial, ScriptRuntime& runtime, quint64 request, const QString& text);
    void phoneClipboardRead(SessionController* controller, bool ok, const QString& text);
    [[nodiscard]] QString logicFolderOf(const Row& row) const;
    ScriptWorkbench* openWorkbench(const QString& folder, const QString& name,
                                   const std::function<void(ScriptWorkbench&)>& prepare = {});
    bool showLogicIn(const QString& serial, const QString& id);
    void attachLogic(const QString& serial, ProfileEditor& editor, const QString& folder, const QString& name);
    [[nodiscard]] QString busyText() const;
    void refreshProfiles(const QString& changed);
    void foregroundApp(const QString& serial, const QString& package, bool game);
    void followApp(Row& row);
    void offerStart(Row& row);
    void ensureAdb();
    void serverReady(bool ready, const QString& detail);
    void setAdbFailure(std::optional<session::SessionFailure> failure);
    void describeRendering();
    void devicesChanged(const adb::TrackerUpdate& update);
    void sessionChanged(const QString& serial, const session::SessionState& state);
    void identify(Row& row);
    void identified(const QString& serial, const std::optional<adb::DeviceInfo>& info);
    void openMirror(Row& row);
    void showLatencyTest(Row& row);
    void stepAside(MirrorWindow& window) const;
    [[nodiscard]] static bool gridEligible(const Row& row);
    bool placeInGrid(Row& row);
    void syncGrid();
    void refreshGrid();
    void closeGrid();
    void setGridSeparate(const QString& serial, bool separate, bool activate);
    bool showFromGrid(const QString& serial, bool activate);
    [[nodiscard]] QString gridFront() const;
    bool aimAtGrid(const QString& serial);
    void sendScroll(const QString& serial, input::Vec2 position, int horizontal, int vertical);
    void sendKey(const QString& serial, int keycode, int metaState, bool down, int repeat);
    void sendText(const QString& serial, const QString& text);
    void mirrorClosed(const QString& serial);
    void foregroundChanged();
    void followFocus(Row& row, bool focused);
    void applyKeymap();
    void followTrial(const QString& serial, bool switched);
    [[nodiscard]] MirrorWindow* mirrorOf(const QString& serial) const;
    void releaseInput(const QString& serial);
    void syncCompanion(Row& row);
    void stopCompanion(Row& row);
    void companionNotice(const QString& serial, quint64 ticket, const CompanionNotice& notice);
    void phoneCursorChanged(Row& row, const protocol::CursorState& cursor);
    [[nodiscard]] bool phoneStatusOn(const Row& row) const;
    void syncPhoneStatus(Row& row);
    void phoneStatusArrived(const QString& serial, const protocol::DeviceStatus& status);
    [[nodiscard]] CompanionLink::Listener companionListener(const QString& serial, quint64 ticket);
    void startCompanionLink(Row& row, quint64 ticket, std::unique_ptr<CompanionLink> link);
    void syncCompanionLan(Row& row);
    void syncLanService();
    void lanArrived(LanArrival arrival);
    void companionInputBox(Row& row, const protocol::InputBoxChanged& box);
    void dropInputBox(Row& row);
    void pushPhoneHints(Row& row);
    void pushCompanionSettings(Row& row);
    void companionSettingChanged(const QString& serial, const protocol::CompanionSettingChange& change);
    [[nodiscard]] const CompanionComputer& thisComputer();
    [[nodiscard]] QVariantMap companionQrOf(const Row& row) const;
    void aimAt(const QString& serial);
    [[nodiscard]] static std::shared_ptr<ControlChannel> channelOf(const Row& row);
    void updateStatsTimer();
    [[nodiscard]] static QString linkName(const Row& row);
    [[nodiscard]] static std::shared_ptr<render::FrameSlot> framesOf(const Row& row);
    void nameConnections();
    void applyLiveSettings();
    void applyFilters(Row& row);
    void applyAllFilters();
    void applyModules();
    void showModuleNotices();
    void showModuleNotice(Row& row);
    [[nodiscard]] QString moduleNotice(const Row& row) const;
    void noteModuleTrouble(const QString& module, const QString& control, int kind);
    void checkBudget(const QString& serial);
    [[nodiscard]] int indexOf(const QString& serial) const;
    void rowChanged(int index);
    [[nodiscard]] static bool isTwin(const Row& row, const Row& other) noexcept;
    [[nodiscard]] bool carded(const Row& row) const;
    [[nodiscard]] QVariantList linksOf(const Row& row) const;
    void removeRow(int index);
    void refreshStats();
    void connectApps(const QString& serial, SessionController* controller);
    [[nodiscard]] AppActions& appActions();
    [[nodiscard]] QString adbRefusal(const QString& serial) const;
    quint64 installOn(const QString& serial, const QString& apkPath);
    quint64 startAppAction(const QString& serial, PackageAction action, const QString& package);
    void finishScreenshot(std::uint32_t requestId, const QImage& image);
    void openAppDisplay(const Row& phone, const QString& package, const QString& label);
    void appDisplayChanged(const QString& key, const session::SessionState& state);
    void appDisplayRefused(const QString& key, protocol::ErrorCode code);
    void endAppDisplay(const QString& key);
    struct Measures;
    [[nodiscard]] Measures measure(Row& row);
    [[nodiscard]] static QStringList describe(const Measures& measures);
    static void restartMeasures(Row& row);
    void closeVisionWorkbench(bool now);
    [[nodiscard]] QString showVisionWorkbench(const QString& serial, const QString& profileId, const QString& name,
                                              const QString& folder, bool draft);
    [[nodiscard]] QString visionPictureProblem(const QString& serial) const;
    void releaseMemoryAfter(QWindow& window);
    void releaseWindowMemory();
    void warnInputRefused(Row& row);
    void useUhidTouch(const QString& serial);
    void usbChanged();
    void connectAoa(Row& row);
    void warnTouchUnavailable(const QString& serial);
    [[nodiscard]] bool wantsAoa(const QString& deviceId) const;
    [[nodiscard]] std::optional<Advice> aoaAdviceOf(const Row& row) const;
    [[nodiscard]] QString driverHelpOf(const Row& row) const;
    [[nodiscard]] int controlTurnOf(const Row& row) const;
    [[nodiscard]] QSize controlCanvasOf(const Row& row, int turn) const;
    void rememberScreen(const Row& row);
    [[nodiscard]] static std::shared_ptr<RecordingTap> tapOf(Row& row);
    [[nodiscard]] QString startRecording(Row& row);
    QVariantMap stopRecording(Row& row, bool inMirror);
    [[nodiscard]] QImage recordingThumbnail(const QString& serial) const;
    void recordingExported(const QString& serial, bool ok, const QString& text, const QString& shown);
    [[nodiscard]] bool filesOnline(const Row& row) const;
    void showDropNotice(const QString& serial, const DropNotice& notice);
    void dropTransferUpdated(const QVariantMap& job);
    void dropInstallUpdated(const QVariantMap& job);
    void captureDevicesChanged();
    void addCaptureRow(const QString& serial, const QString& name, const std::wstring& link,
                       const std::wstring& container);
    void startCapture(Row& row);
    void stopCapture(Row& row);
    void captureEvent(const QString& serial, CaptureEvent event);
    void captureTouchChanged(const QString& serial, std::uint32_t generation, AoaState state);
    void attachCaptureTouch(Row& row);
    void detachCaptureTouch(Row& row);
    void applyCaptureGeometry(Row& row);
    void cropGridCell(const Row& row);
    void restartCaptureFeed(Row& row);
    [[nodiscard]] std::shared_ptr<ExtensionTouch> extensionTouchSink(const QString& method, Row& row);
    void captureTouchFell(const QString& serial, std::uint32_t generation);
    [[nodiscard]] QString captureTouchName(const QString& method) const;
    [[nodiscard]] std::string touchDeviceOptions(std::string_view key, std::string_view device) const;
    [[nodiscard]] QVariantMap captureState(const Row& row) const;
    [[nodiscard]] std::optional<Advice> captureAdviceOf(const Row& row) const;
    [[nodiscard]] int phoneIndexOf(const QString& deviceId) const;
    [[nodiscard]] int cardIndexOf(const QString& deviceId) const;
    [[nodiscard]] PhoneLinks phoneLinksOf(const QString& deviceId, const settings::Touch& touch) const;
    [[nodiscard]] std::vector<ExtensionTouchOffer> extensionTouchOffers(const QString& chosen) const;
    [[nodiscard]] CaptureListing captureListingOf(const Row& card) const;
    [[nodiscard]] CaptureFeed* pictureFeedOf(const Row& row) const;
    [[nodiscard]] std::shared_ptr<media::FrameSnapshots> shotsOf(const Row& row) const;
    [[nodiscard]] QVariantMap pictureStateOf(const Row& row) const;
    [[nodiscard]] Row* lentCardOf(const Row& phone);
    [[nodiscard]] std::pair<QString, std::string> captureTouchWanted(const Row& card) const;
    settings::PictureSource usePicture(Row& phone);
    bool borrowCapture(Row& phone, Row& card);
    void returnCapture(Row& phone);
    void lentCaptureEvent(Row& card, CaptureEvent event);
    void lentCaptureLeft(Row& card);
    void lentCaptureBack(Row& card);
    void applyPictureGeometry(Row& phone);
    void showNoPicture(Row& row);
    void openDemoMirror(Row& row);
    void bumpLinks();
    void refreshCaptureRows();
    [[nodiscard]] std::vector<PhoneSource> phoneSources() const;
    int compareCapture(int ticket, const Row& card);
    void probeEvent(const QString& captureSerial, CaptureEvent event);
    void finishProbe(const QString& captureSerial, bool compare);
    void matcherSuggested(int matcherTicket, const QString& captureSerial, const QString& deviceId, double score);
    [[nodiscard]] static bool isBoxRow(const Row& row) noexcept;
    [[nodiscard]] static bool noPictureOf(const Row& row) noexcept;
    void syncProvided();
    void syncSources();
    void addSourceRow(const QString& serial, const MediaSourceDevice& source);
    void connectProvided(Row& row);
    void startProvidedJob(Row& row, const QString& job, const QString& title);
    void showProvidedJob(const QString& serial, QVariantMap shown);
    [[nodiscard]] QVariantMap providedState(const Row& row) const;
    [[nodiscard]] std::optional<Advice> providedAdviceOf(const Row& row) const;
    [[nodiscard]] DevicePresentation providedPresentation(const Row& row) const;
    void syncTransportPhones();
    [[nodiscard]] std::function<std::unique_ptr<session::ServerHost>()> transportHost(const TransportPhone& phone) const;
    [[nodiscard]] static QVariantMap transportedState(const Row& row);
    [[nodiscard]] static std::optional<Advice> transportedAdviceOf(const Row& row);
    [[nodiscard]] QString backgroundKeyOf(const Row& row) const;
    void bumpBackgrounds();
    void backgroundStored(Row& row, const StoredBackground& stored, bool fromPhone);
    void shotArrived(Row& row, const CompanionNotice& notice);
    void forgetShots(Row& row);

    static inline DeviceManager* s_instance = nullptr;

    const adb::AdbClient m_adb;
    const std::filesystem::path m_adbExecutable;
    const render::GraphicsDevice* const m_graphics;
    AppSettings& m_settings;
    ExtensionHost* m_extensions = nullptr;
    NativeJobs* m_jobs = nullptr;
    QPointer<ExtensionLogic> m_extensionLogic;
    InputController* m_input = nullptr;
    QString m_inputProfileKey;
    ProfileLibrary* m_library = nullptr;
    std::shared_ptr<const input::Profile> m_override;
    std::filesystem::path m_overrideFolder;
    std::filesystem::path m_scriptStore;
    std::optional<TouchBackend> m_touchBackend;
    std::map<QString, std::chrono::steady_clock::time_point> m_logicNotices;
    std::set<QString> m_fastNotices;
    struct ReadAsk {
        ScriptRuntime* runtime = nullptr;
        QString serial;
        quint64 request = 0;
        QPointer<MirrorWindow> mirror;
    };
    std::vector<ReadAsk> m_readAsks;
    bool m_keyHints = true;
    std::unique_ptr<platform::ForegroundWatcher> m_foreground;
    std::unique_ptr<platform::TrayNotice> m_tray;
    TrayNotices m_notices;
    QPointer<QQmlEngine> m_engine;
    std::unique_ptr<DeviceIdentifier> m_identifier;
    const std::shared_ptr<const session::ServerPackage> m_package;
    struct EndingCompanion {
        quint64 ticket = 0;
        QString deviceId;
        std::unique_ptr<CompanionLink> link;
    };
    std::shared_ptr<const CompanionApk> m_companionApk;
    std::vector<EndingCompanion> m_endingCompanions;
    quint64 m_lastCompanionTicket = 0;
    std::unique_ptr<CompanionLan> m_companionLan;
    std::optional<CompanionComputer> m_thisComputer;
    std::vector<Row> m_rows;
    std::optional<session::SessionFailure> m_adbFailure;
    QString m_adbProblem;
    QString m_adbAction;
    bool m_adbReady = false;
    bool m_adbStarting = false;
    QString m_renderNotice;
    QString m_renderDetail;
    QTimer m_statsTimer;
    QTimer m_offlineGrace;
    adb::TrackerUpdate m_lastDevices;
    std::map<QString, std::array<QString, 3>> m_knownNames;
    QSortFilterProxyModel m_sorted;
    std::map<QString, QString> m_chosenLink;
    std::unique_ptr<adb::DeviceTracker> m_tracker;
    AppCatalog* m_catalog = nullptr;
    int m_appDisplays = 0;
    AppActions* m_appActions = nullptr;
    QPointer<QWindow> m_visionWindow;
    QPointer<VisionWorkbench> m_vision;
    QString m_materialsClient;
    QPointer<GridWindow> m_grid;
    LogicConsole m_logicConsole;
    std::map<QString, QPointer<QObject>> m_workbenches;
    QPointer<TaskLibrary> m_tasks;
    QPointer<ComponentLibrary> m_components;
    std::vector<std::unique_ptr<TaskRun>> m_taskRuns;
    QVariantList m_taskHistory;
    QTimer m_taskClock;
    quint64 m_lastTaskRun = 0;
    QDateTime m_lastScheduleCheck;
    std::vector<ScheduledStart> m_scheduledStarts;
    QTimer m_scheduleWait;
    bool m_trayNoticeShown = false;
    std::unique_ptr<UsbPhoneWatcher> m_usb;
    std::map<std::string, platform::UsbDriver> m_usbDrivers;
    Recordings* m_recordings = nullptr;
    std::map<std::uint32_t, std::function<void(const QImage&)>> m_screenshots;
    std::uint32_t m_lastScreenshot = 0;
    std::unique_ptr<SystemPanelService> m_panels;
    DeviceBatch* m_batch = nullptr;
    SyncGroups m_sync;
    std::map<QString, PushedSync> m_pushedSync;
    std::unique_ptr<CaptureCardWatcher> m_captureWatcher;
    QVariantList m_otherVideoDevices;
    std::unique_ptr<KeymapBackgrounds> m_backgrounds;
    int m_backgroundRevision = 0;
    std::uint32_t m_lastShot = 0;
    std::jthread m_starter;
    std::unique_ptr<FileTransfers> m_transfers;
    std::unique_ptr<PhoneFiles> m_files;
    std::map<QString, DropFeedback> m_drops;
    std::map<QString, std::jthread> m_wirelessCalls;
    std::map<std::string, QString, std::less<>> m_moduleTroubles;
    bool m_adbRestarted = false;
    QStringList m_wirelessBeforeOutage;
    CaptureMatcher* m_matcher = nullptr;
    int m_linkRevision = 0;
    bool m_linksPending = false;
    QStringList m_listedCaptures;
    std::map<QString, quint64> m_assignOrder;
    quint64 m_lastAssign = 0;
    struct CaptureProbe {
        int ticket = 0;
        int cropped = 0;
        bool ready = false;
    };
    std::vector<platform::UsbPhone> m_usbAlone;
    std::map<QString, CaptureProbe> m_probes;
    std::map<int, int> m_suggestTickets;
    int m_lastSuggest = 0;
};

}
