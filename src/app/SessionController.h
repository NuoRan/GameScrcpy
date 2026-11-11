#pragma once

#include "AoaTouchSink.h"
#include "ControlChannel.h"
#include "LatencyMeter.h"
#include "SessionMetrics.h"
#include "Settings.h"
#include "TouchMethods.h"
#include "UhidTouchSink.h"

#include <gs/adb/AdbClient.h>
#include <gs/media/AudioPlayer.h>
#include <gs/media/FrameSnapshot.h>
#include <gs/platform/Usb.h>
#include <gs/protocol/Enums.h>
#include <gs/render/FrameSlot.h>
#include <gs/session/AdbServerHost.h>
#include <gs/session/Session.h>

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace gs::render {
class GraphicsDevice;
}

namespace gs::record {
class Recorder;
}

namespace gs::app {

class SessionController : public QObject
{
    Q_OBJECT

public:
    using HostFactory = std::function<std::unique_ptr<session::ServerHost>()>;

    SessionController(adb::AdbClient adb, QString serial, std::shared_ptr<const session::ServerPackage> package,
                      const render::GraphicsDevice* graphics, QObject* parent = nullptr);
    ~SessionController() override;

    void setHostFactory(HostFactory factory) { m_hostFactory = std::move(factory); }
    void start(session::TransportChoice transport, const settings::Settings& settings);
    void stop();
    void showAppDisplay(const QString& package);
    [[nodiscard]] bool showsAppDisplay() const noexcept { return !m_appPackage.isEmpty(); }
    void forceTouchBackend(std::optional<TouchBackend> backend) noexcept { m_forcedTouch = backend; }
    void setPictureSource(settings::PictureSource source) noexcept { m_pictureSource = source; }
    [[nodiscard]] settings::PictureSource pictureSource() const noexcept { return m_pictureSource; }
    void setTouchMethods(TouchMethodSource* methods) noexcept { m_touchMethods = methods; }
    [[nodiscard]] const QString& extensionTouchProblem() const noexcept { return m_extensionTouchProblem; }
    void setUsbPhone(std::optional<platform::UsbPhone> phone) { m_usbPhone = std::move(phone); }
    [[nodiscard]] TouchBackend touchBackend() const noexcept { return m_touchBackend; }
    [[nodiscard]] bool touchesOverAoa() const noexcept { return m_aoaSink != nullptr && !m_aoaLost; }
    [[nodiscard]] platform::UsbFailure aoaFailure() const noexcept { return m_aoaFailure; }
    [[nodiscard]] bool bulkReady() const noexcept { return m_bulkReady; }
    bool requestAppList(std::uint32_t requestId);
    bool requestAppIcon(std::uint32_t requestId, const QString& package, int sizePx);
    bool requestScreenshot(std::uint32_t requestId, std::uint32_t displayId = 0);
    bool startApp(const QString& package, bool forceStop = false);
    bool readPhoneClipboard();
    bool writePhoneClipboard(const QString& text);
    void pressKey(std::uint32_t keycode);
    void moveMouse(int dx, int dy);
    void setMouseButtons(int buttons);
    void setAudioMuted(bool muted);
    void setAudioVolume(float volume);
    void setAudioTap(media::AudioTap tap);
    void setScreenOff(bool off);
    void setOrientation(protocol::OrientationMode mode);
    void setStatusReports(bool on);
    bool showSystemPanel(protocol::SystemPanelAction action);
    [[nodiscard]] std::uint32_t capabilities() const noexcept { return m_capabilities; }
    [[nodiscard]] bool canSwitchToUhid() const noexcept
    {
        return (m_capabilities & protocol::Welcome::kUhid) != 0 && m_touchBackend != TouchBackend::Uhid &&
               !showsAppDisplay() && !m_forcedTouch.has_value();
    }

    [[nodiscard]] session::SessionPhase phase() const noexcept { return m_phase; }
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] const std::shared_ptr<render::FrameSlot>& frames() const noexcept { return m_frames; }
    [[nodiscard]] const std::shared_ptr<render::FrameSlot>& benchFrames() const noexcept { return m_benchFrames; }
    void setBenchFed(bool fed) noexcept { m_benchFed.store(fed, std::memory_order_relaxed); }
    [[nodiscard]] const std::shared_ptr<media::FrameSnapshots>& snapshots() const noexcept { return m_snapshots; }
    [[nodiscard]] const std::shared_ptr<ControlChannel>& channel() const noexcept { return m_channel; }
    [[nodiscard]] SessionMetrics& metrics() noexcept { return m_metrics; }
    [[nodiscard]] media::AudioStats audioStats() const;

    [[nodiscard]] bool canMeasureLatency() const noexcept;
    bool startLatencyTest();
    void stopLatencyTest();
    [[nodiscard]] const std::optional<LatencyProgress>& latencyTest() const noexcept { return m_latency; }
    void setRecorder(std::shared_ptr<record::Recorder> recorder);
    [[nodiscard]] std::shared_ptr<record::Recorder> recorder() const;

signals:
    void stateChanged(const gs::session::SessionState& state);
    void transportUsed(gs::session::TransportKind kind);
    void welcomed(const QString& manufacturer, const QString& model);
    void screenDescribed(const QSize& size, double refreshHz, int rotation);
    void codecsDescribed(const QStringList& codecs);
    void refused(gs::protocol::ErrorCode code, const QString& message);
    void audioUnavailable(gs::protocol::AudioStatus status);
    void thumbnailed(const QImage& image);
    void foregroundAppChanged(const QString& package, const QString& activity, bool game);
    void latencyTestChanged();
    void bulkChanged(bool ready);
    void appsListed(std::uint32_t requestId, const std::vector<gs::protocol::AppEntry>& apps);
    void appIconArrived(std::uint32_t requestId, const QString& package, const QByteArray& png);
    void screenshotArrived(std::uint32_t requestId, const QImage& image);
    void bulkRequestFailed(std::uint32_t requestId, gs::protocol::ErrorCode code, const QString& message);
    void touchUnavailable();
    void phoneClipboardRead(bool ok, const QString& text);
    void deviceStatusArrived(const gs::protocol::DeviceStatus& status);
    void padLinkChanged();
    void uhidOutput(quint16 id, const QByteArray& data);
    void uhidStatus(quint16 id, bool recognized, bool vibration);
    void systemPanelFailed(bool unsupported);

private:
    class Bridge;

    static constexpr qsizetype kClipboardLimit = 262'000;
    static constexpr std::chrono::seconds kLatencyTestLimit{60};
    static constexpr std::chrono::milliseconds kLatencyPollInterval{100};
    static constexpr std::uint16_t kStatusPeriodMs = 2000;
    static constexpr std::uint16_t kControlOnlyMaxSize = 320;
    static constexpr std::uint16_t kControlOnlyMaxFps = 1;
    static constexpr std::uint32_t kControlOnlyBitrate = 200'000;

    [[nodiscard]] std::shared_ptr<LatencyMeter> meter() const;
    void pollLatency();
    void endLatencyTest();
    void refuseLatencyTest();

    void computerClipboardChanged();
    void deviceClipboardChanged(const QString& text);
    [[nodiscard]] bool clipboardOpen() const noexcept;
    bool shareClipboard(const QString& text);
    void endClipboardReads();
    void applyScreen();
    void applyOrientation();
    void applyLaunch();
    void applyMouse();
    void sendScreen();
    void sendOrientation();
    void applyStatus();
    void sendStatus();

    const adb::AdbClient m_adb;
    const QString m_serial;
    const std::shared_ptr<const session::ServerPackage> m_package;
    const render::GraphicsDevice* const m_graphics;
    const std::shared_ptr<render::FrameSlot> m_frames = std::make_shared<render::FrameSlot>();
    const std::shared_ptr<render::FrameSlot> m_benchFrames = std::make_shared<render::FrameSlot>();
    std::atomic<bool> m_benchFed{false};
    const std::shared_ptr<media::FrameSnapshots> m_snapshots;
    SessionMetrics m_metrics;
    session::SessionPhase m_phase = session::SessionPhase::Idle;
    std::uint32_t m_capabilities = 0;
    bool m_clipboardShared = false;
    QString m_sharedClipboard;
    std::uint32_t m_clipboardSequence = 0;
    bool m_clipboardAsked = false;
    int m_clipboardReads = 0;
    bool m_screenOff = false;
    bool m_mouseReady = false;
    std::uint8_t m_mouseButtons = 0;
    protocol::OrientationMode m_orientation = protocol::OrientationMode::Follow;
    bool m_statusReports = false;
    QString m_appPackage;
    bool m_launchPending = false;
    bool m_bulkReady = false;
    void releaseSession();

    const std::shared_ptr<ControlChannel> m_channel = std::make_shared<ControlChannel>();
    mutable std::mutex m_meterMutex;
    std::shared_ptr<LatencyMeter> m_meter;
    mutable std::mutex m_recorderMutex;
    std::shared_ptr<record::Recorder> m_recorder;
    std::optional<LatencyProgress> m_latency;
    QTimer m_latencyPoll;
    std::chrono::steady_clock::time_point m_latencyDeadline;
    std::unique_ptr<Bridge> m_bridge;
    std::unique_ptr<session::Session> m_session;
    std::unique_ptr<SessionControlSink> m_controlSink;
    ControlSink& touchSink();
    ControlSink& aoaSink();
    ControlSink* extensionSink(const std::string& key);
    void extensionTouchFailed();
    void noteTouch(std::uint32_t phone);
    void aoaChanged(AoaState state);
    void updatePadLink();
    std::uint32_t m_padEpoch = 0;

    std::optional<TouchBackend> m_forcedTouch;
    TouchBackend m_touchBackend = TouchBackend::Injection;
    settings::PictureSource m_pictureSource = settings::PictureSource::Phone;
    std::unique_ptr<NoTouchSink> m_noTouch;
    bool m_touchNoted = false;
    bool m_touchMissing = false;
    std::unique_ptr<SessionUhidWire> m_uhidWire;
    std::unique_ptr<UhidTouchSink> m_uhidSink;
    std::optional<platform::UsbPhone> m_usbPhone;
    platform::UsbFailure m_aoaFailure = platform::UsbFailure::None;
    bool m_aoaLost = false;
    std::unique_ptr<AoaPort> m_aoaPort;
    std::unique_ptr<AoaTouchSink> m_aoaSink;
    TouchMethodSource* m_touchMethods = nullptr;
    std::unique_ptr<ExtensionTouch> m_extensionTouch;
    QString m_extensionTouchTitle;
    QString m_extensionTouchProblem;
    HostFactory m_hostFactory;
};

}
