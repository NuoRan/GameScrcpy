#include "DeviceManager.h"

#include "AoaSession.h"
#include "AppActions.h"
#include "AppCatalog.h"
#include "AppEvents.h"
#include "AppSettings.h"
#include "CaptureCards.h"
#include "CaptureDemo.h"
#include "CaptureFeed.h"
#include "CaptureMatch.h"
#include "CaptureRow.h"
#include "ComponentLibrary.h"
#include "ControlChannel.h"
#include "DemoFeed.h"
#include "DeviceIdentifier.h"
#include "DevicePresentation.h"
#include "Esp32Boards.h"
#include "Esp32Panel.h"
#include "Esp32Session.h"
#include "ExtensionHost.h"
#include "ExtensionLogic.h"
#include "ExtensionMedia.h"
#include "ExtensionPanels.h"
#include "FailureAdvice.h"
#include "GridLayout.h"
#include "GridWindow.h"
#include "HardwareAccess.h"
#include "HardwareClaims.h"
#include "InputController.h"
#include "KeyHintModel.h"
#include "KeymapBackgrounds.h"
#include "McpDrafts.h"
#include "McpServer.h"
#include "MirrorWindow.h"
#include "NativeExtensions.h"
#include "NativeJobs.h"
#include "PhoneFiles.h"
#include "PhoneStatus.h"
#include "PhoneVendors.h"
#include "ProfileEditor.h"
#include "ProfileLibrary.h"
#include "QrPicture.h"
#include "Recordings.h"
#include "ScriptRuntime.h"
#include "ScriptWorkbench.h"
#include "SessionController.h"
#include "SetupGuide.h"
#include "SystemPanelService.h"
#include "TaskLibrary.h"
#include "UsbPhones.h"
#include "VisionWorkbench.h"

#include <gs/adb/ServerLauncher.h>
#include <gs/capture/MediaFoundationSource.h>
#include <gs/esp32/Boards.h>
#include <gs/exthost/Module.h>
#include <gs/input/MappingEngine.h>
#include <gs/input/Profile.h>
#include <gs/media/FrameSnapshot.h>
#include <gs/platform/Foreground.h>
#include <gs/platform/Thread.h>
#include <gs/platform/TrayNotice.h>
#include <gs/render/GraphicsDevice.h>

#include <nlohmann/json.hpp>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <set>
#include <optional>
#include <tuple>
#include <utility>

namespace gs::app {

namespace {

constexpr int kStepAside = 32;
constexpr int kMostSteps = 8;
constexpr int kRecordingThumbnail = 160;
constexpr std::string_view kEsp32TouchKey = "gamescrcpy.esp32/esp32";
constexpr std::chrono::milliseconds kOfflineGrace{3000};
constexpr int kTaskClockMs = 20000;
constexpr int kTaskIdShift = 40;
constexpr qsizetype kMostTaskHistory = 50;
constexpr int kScheduleWaitMs = 1000;
constexpr int kScheduledConnectTries = 2;
constexpr int kKeyWakeup = 224;
constexpr int kKeySleep = 223;
constexpr quint64 kTaskGestureSlots = 255;
constexpr int kTaskGestureShift = 56;
constexpr std::uint32_t kScreenshotRequests = 0x80000000U;
constexpr std::size_t kMostScreenshotWaits = 16;
constexpr int kTileGap = 8;

bool active(session::SessionPhase phase)
{
    return phase != session::SessionPhase::Idle && phase != session::SessionPhase::Stopped &&
           phase != session::SessionPhase::Failed;
}

settings::Companion companionSettingsOf(const AppSettings& settings, const QString& deviceId)
{
    if (deviceId.isEmpty()) {
        return settings::Companion{.cursor = false, .ime = false, .hints = false};
    }
    return settings.effective(deviceId.toStdString()).companion;
}

protocol::CursorState phoneCursorFor(bool aoaOnly, bool cursorOn, const protocol::CursorState& cursor)
{
    return aoaOnly || !cursorOn ? protocol::CursorState{} : cursor;
}

protocol::OrientationMode orientationMode(int orientation)
{
    switch (orientation) {
    case 1:
        return protocol::OrientationMode::Lock0;
    case 2:
        return protocol::OrientationMode::Lock90;
    default:
        return protocol::OrientationMode::Follow;
    }
}

QString transportName(session::TransportKind kind)
{
    return kind == session::TransportKind::WifiDirect ? DeviceManager::tr("WiFi direct")
                                                      : DeviceManager::tr("adb tunnel");
}

QString problemText(StatusProblem problem)
{
    switch (problem) {
    case StatusProblem::SlowDelay:
        return DeviceManager::tr("Delay is high");
    case StatusProblem::DroppingFrames:
        return DeviceManager::tr("Dropping frames");
    case StatusProblem::Reconnecting:
        return DeviceManager::tr("Reconnecting");
    case StatusProblem::None:
        break;
    }
    return {};
}

bool isWireless(const QString& serial)
{
    return !serial.startsWith(u"emulator-") && !serial.startsWith(u"box:") && !serial.startsWith(u"via:") &&
           (serial.contains(u':') || serial.contains(u"._adb-tls-connect._tcp"));
}

session::TransportChoice defaultTransportFor(const QString& serial)
{
    return isWireless(serial) ? session::TransportChoice::WifiDirect : session::TransportChoice::AdbTunnel;
}

bool offersTunnel(session::SessionPhase phase, const std::optional<session::SessionFailure>& failure)
{
    return phase == session::SessionPhase::Failed && failure &&
           (failure->kind == session::FailureKind::DirectUnreachable ||
            failure->kind == session::FailureKind::DirectNoAddress);
}

QString attachmentName(const QString& serial)
{
    if (serial.startsWith(u"emulator-")) {
        return DeviceManager::tr("Emulator");
    }
    return isWireless(serial) ? DeviceManager::tr("Wireless adb") : DeviceManager::tr("USB");
}

QString aoaLinkName(bool streaming)
{
    return streaming ? DeviceManager::tr("No picture · USB · AOA") : DeviceManager::tr("USB · AOA");
}

QString aoaTouchLinkName(const QString& serial)
{
    return isWireless(serial) ? DeviceManager::tr("Wireless · picture + USB · AOA touch")
                              : DeviceManager::tr("USB · picture + AOA touch");
}

QString aoaHelpSlug()
{
    return QStringLiteral("aoa-driver");
}

QString esp32LinkName(bool wifi, bool streaming)
{
    if (streaming) {
        return wifi ? DeviceManager::tr("No picture · ESP32 over Wi-Fi")
                    : DeviceManager::tr("No picture · ESP32 over USB serial");
    }
    return wifi ? DeviceManager::tr("ESP32 over Wi-Fi") : DeviceManager::tr("ESP32 over USB serial");
}

esp32::Esp32Settings esp32SettingsOf(const settings::Esp32& board)
{
    esp32::Esp32Settings chosen;
    chosen.port = board.port;
    chosen.link = board.link == settings::Esp32Connection::Wifi ? esp32::LinkMode::Wifi : esp32::LinkMode::Serial;
    chosen.address = board.address;
    chosen.boardsFile = dataDirectory() / std::string(esp32::kBoardsFileName);
    return chosen;
}

QString shotFailureText(ShotFailure failure)
{
    switch (failure) {
    case ShotFailure::Refused:
        return DeviceManager::tr("The phone refused the screen recording");
    case ShotFailure::Unsupported:
        return DeviceManager::tr("This phone cannot take screenshots for GameScrcpy");
    case ShotFailure::Busy:
        return DeviceManager::tr("The phone is still taking the last screenshot");
    case ShotFailure::TimedOut:
        return DeviceManager::tr("The screenshot did not arrive in time");
    case ShotFailure::Error:
    case ShotFailure::TooBig:
    case ShotFailure::Garbled:
    case ShotFailure::Lost:
        break;
    }
    return DeviceManager::tr("The screenshot from the phone did not arrive. Try again, or import a screenshot file");
}

QString backgroundProblemText(BackgroundProblem problem)
{
    switch (problem) {
    case BackgroundProblem::Missing:
        return DeviceManager::tr("That picture could not be read");
    case BackgroundProblem::TooLarge:
        return DeviceManager::tr("That picture is larger than 8 MB");
    case BackgroundProblem::NotPicture:
        return DeviceManager::tr("Only PNG and JPG pictures can be the background");
    case BackgroundProblem::Unwritable:
        break;
    }
    return DeviceManager::tr("The background could not be saved");
}

QByteArray demoLogName(int number)
{
    return number > 0 ? QByteArrayLiteral("demo device ") + QByteArray::number(number)
                      : QByteArrayLiteral("demo device");
}

QString jsonText(const QString& text)
{
    const QByteArray wrapped = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(wrapped.sliced(1, wrapped.size() - 2));
}

const char* touchActionName(protocol::TouchAction action)
{
    switch (action) {
    case protocol::TouchAction::Down:
        return "down";
    case protocol::TouchAction::Up:
        return "up";
    case protocol::TouchAction::Move:
        return "move";
    case protocol::TouchAction::Cancel:
        return "cancel";
    }
    return "?";
}

class LoggingControlSink final : public ControlSink
{
public:
    explicit LoggingControlSink(QByteArray name) : m_name(std::move(name)) {}

    bool send(const protocol::TouchFrame& frame) override
    {
        QString points;
        for (const protocol::TouchPoint& point : frame.points) {
            points += QStringLiteral(" %1 #%2 (%3, %4)")
                          .arg(QString::fromLatin1(touchActionName(point.action)))
                          .arg(point.pointerId)
                          .arg(point.x / 65535.0, 0, 'f', 3)
                          .arg(point.y / 65535.0, 0, 'f', 3);
        }
        qInfo("%s: touch frame for video session %u:%s, not sent", m_name.constData(),
              static_cast<unsigned>(frame.videoSession), qUtf8Printable(points));
        return true;
    }

    bool send(const protocol::Key& key) override
    {
        qInfo("%s: key %u %s, not sent", m_name.constData(), static_cast<unsigned>(key.keycode),
              key.action == protocol::KeyAction::Down ? "down" : "up");
        return true;
    }

    bool send(const protocol::Scroll& scroll) override
    {
        qInfo("%s: scroll for video session %u at (%.3f, %.3f) by %d, %d, not sent", m_name.constData(),
              static_cast<unsigned>(scroll.videoSession), scroll.x / 65535.0, scroll.y / 65535.0,
              static_cast<int>(scroll.hScroll), static_cast<int>(scroll.vScroll));
        return true;
    }

    bool send(const protocol::Text& text) override
    {
        qInfo("%s: text of %lld characters, not sent", m_name.constData(),
              static_cast<long long>(QString::fromStdString(text.text).size()));
        return true;
    }

    bool padFeature(const protocol::UhidFeature& feature) override
    {
        qInfo("%s: feature report %u of gamepad %u, not sent", m_name.constData(),
              static_cast<unsigned>(feature.reportNumber), static_cast<unsigned>(feature.id));
        return true;
    }

    bool padCreate(const protocol::UhidCreate& create) override
    {
        qInfo("%s: gamepad %u \"%s\" %04x:%04x on bus %u, not made", m_name.constData(),
              static_cast<unsigned>(create.id), create.name.c_str(), static_cast<unsigned>(create.vendorId),
              static_cast<unsigned>(create.productId), static_cast<unsigned>(create.bus));
        return true;
    }

    bool padInput(const protocol::UhidInput&) override { return true; }

    bool padDestroy(const protocol::UhidDestroy& destroy) override
    {
        qInfo("%s: gamepad %u taken off, not sent", m_name.constData(), static_cast<unsigned>(destroy.id));
        return true;
    }

private:
    QByteArray m_name;
};

void logDemoAs(ControlChannel& channel, std::shared_ptr<ControlSink>& kept, const DemoFeed& feed, int number)
{
    auto sink = std::make_shared<LoggingControlSink>(demoLogName(number));
    channel.attach(*sink);
    channel.setVideo(feed.videoSession(), feed.frameWidth(), feed.frameHeight());
    kept = std::move(sink);
}

constexpr std::chrono::seconds kCaptureProbeLimit{8};

QImage lumaPicture(const media::LumaSnapshot& shot)
{
    const auto width = static_cast<std::size_t>(std::max(shot.width, 0));
    const auto height = static_cast<std::size_t>(std::max(shot.height, 0));
    if (width == 0 || height == 0 || shot.luma.size() < width * height) {
        return {};
    }
    QImage picture(shot.width, shot.height, QImage::Format_Grayscale8);
    for (int y = 0; y < shot.height; ++y) {
        std::memcpy(picture.scanLine(y), shot.luma.data() + static_cast<std::size_t>(y) * width, width);
    }
    return picture;
}

QImage thumbnailImage(const media::Thumbnail& made)
{
    return QImage(reinterpret_cast<const uchar*>(made.pixels.data()), made.width, made.height,
                  static_cast<qsizetype>(made.width) * 4, QImage::Format_RGB32)
        .copy();
}

ControlTurn turnOfRotation(int rotation) noexcept
{
    switch (rotation & 3) {
    case 1:
        return ControlTurn::LandscapeLeft;
    case 3:
        return ControlTurn::LandscapeRight;
    default:
        return ControlTurn::Portrait;
    }
}

static_assert(static_cast<int>(settings::Transport::AdbTunnel) ==
              static_cast<int>(session::TransportChoice::AdbTunnel));
static_assert(static_cast<int>(settings::Transport::WifiDirect) ==
              static_cast<int>(session::TransportChoice::WifiDirect));
static_assert(static_cast<int>(settings::Transport::Auto) == static_cast<int>(session::TransportChoice::Auto));

}

DeviceManager::DeviceManager(adb::AdbClient adb, std::filesystem::path adbExecutable,
                             std::filesystem::path serverPackage, const render::GraphicsDevice* graphics,
                             AppSettings& settings, QObject* parent)
    : QAbstractListModel(parent), m_adb(adb), m_adbExecutable(std::move(adbExecutable)), m_graphics(graphics),
      m_settings(settings), m_package(session::ServerPackage::load(serverPackage))
{
    Q_ASSERT(s_instance == nullptr);
    s_instance = this;
    m_identifier = std::make_unique<DeviceIdentifier>(
        m_adb, [this](const std::string& serial, const adb::AdbResult<adb::DeviceInfo>& info) {
            QMetaObject::invokeMethod(
                this,
                [this, serial = QString::fromStdString(serial),
                 info = info ? std::optional<adb::DeviceInfo>(*info) : std::nullopt] { identified(serial, info); },
                Qt::QueuedConnection);
        });
    describeRendering();
    m_statsTimer.setInterval(500);
    connect(&m_statsTimer, &QTimer::timeout, this, &DeviceManager::refreshStats);
    m_offlineGrace.setSingleShot(true); // source-rule-allow R9: a grace period; wireless adb drops for under a second and comes back
    m_offlineGrace.setInterval(kOfflineGrace);
    connect(&m_offlineGrace, &QTimer::timeout, this, [this] { devicesChanged(m_lastDevices); });
    connect(&m_settings, &AppSettings::wirelessChanged, this, &DeviceManager::nameConnections);
    connect(&m_settings, &AppSettings::valuesChanged, this, &DeviceManager::applyLiveSettings);
    connect(&m_settings, &AppSettings::devicesChanged, this, &DeviceManager::applyLiveSettings);
    if (HardwareClaims* claims = HardwareClaims::instance()) {
        connect(
            claims, &HardwareClaims::changed, this,
            [this] {
                for (std::size_t index = 0; index < m_rows.size(); ++index) {
                    if (m_rows[index].provided && !m_rows[index].provided->resource.empty()) {
                        rowChanged(static_cast<int>(index));
                    }
                }
            },
            Qt::QueuedConnection);
    }
    m_input = new InputController(this);
    connect(m_input, &InputController::keymapChanged, this, [this](bool on) {
        qInfo("key mapping %s", on ? "on" : "off");
        applyKeymap();
    });
    connect(m_input, &InputController::cameraLockChanged, this, [this](bool locked) {
        const int index = indexOf(m_input->target());
        const QString name = index >= 0 ? m_rows[static_cast<std::size_t>(index)].name() : QString();
        qInfo("camera %s for %s", locked ? "locked" : "released", qUtf8Printable(name));
        applyKeymap();
    });
    connect(m_input, &InputController::cursorPeekChanged, this, [this] { applyKeymap(); });
    connect(m_input, &InputController::actionWheelChanged, this,
            [this](const QString& control, bool open, int cell) { showActionWheel(control, open, cell); });
    connect(m_input, &InputController::noCamera, this, [this] {
        const int index = indexOf(m_input->target());
        if (m_grid != nullptr && m_grid->holds(m_input->target())) {
            m_grid->showToast(tr("This profile has no camera control"), QStringLiteral("info"));
        } else if (index >= 0 && m_rows[static_cast<std::size_t>(index)].mirror != nullptr) {
            m_rows[static_cast<std::size_t>(index)].mirror->showToast(tr("This profile has no camera control"),
                                                                     QStringLiteral("info"));
        }
    });
    connect(m_input, &InputController::textInputChanged, this, [this](bool on) {
        const int index = indexOf(m_input->target());
        if (index >= 0 && m_rows[static_cast<std::size_t>(index)].mirror != nullptr) {
            m_rows[static_cast<std::size_t>(index)].mirror->setTextInput(on);
        }
        if (m_grid != nullptr) {
            m_grid->setTextInput(on && m_grid->holds(m_input->target()));
        }
    });
    connect(m_input, &InputController::controlsLiveChanged, this,
            [this](const QString& serial, const QList<bool>& live, quintptr profile) {
                const int index = indexOf(serial);
                if (index >= 0 && m_rows[static_cast<std::size_t>(index)].mirror != nullptr) {
                    m_rows[static_cast<std::size_t>(index)].mirror->setLiveControls(live, profile);
                }
                if (m_grid != nullptr) {
                    m_grid->setLiveControls(serial, live, profile);
                }
            });
    connect(m_input, &InputController::controlsAimingChanged, this,
            [this](const QString& serial, const QList<bool>& aiming, quintptr profile) {
                const int index = indexOf(serial);
                if (index >= 0 && m_rows[static_cast<std::size_t>(index)].mirror != nullptr) {
                    m_rows[static_cast<std::size_t>(index)].mirror->setAimingControls(aiming, profile);
                }
                if (m_grid != nullptr) {
                    m_grid->setAimingControls(serial, aiming, profile);
                }
            });
    m_batch = new DeviceBatch(
        {.startTask = [this](const QString& id, const QString& serial) { return startTask(id, serial); },
         .launchApp = [this](const QString& serial, const QString& package,
                             const QString& label) { launchApp(serial, package, label, false); },
         .installApk = [this](const QStringList& serials, const QString& apkPath) { installApk(serials, apkPath); },
         .pushFiles = [this](const QStringList& serials,
                             const QStringList& localPaths) { pushFiles(serials, localPaths); }},
        this);
    connect(this, &DeviceManager::installUpdated, m_batch, &DeviceBatch::installUpdated);
    connect(this, &DeviceManager::transferUpdated, m_batch, &DeviceBatch::transferUpdated);
    connect(this, &DeviceManager::countsChanged, this, &DeviceManager::refreshSync);
    connect(this, &DeviceManager::mirrorsChanged, this, &DeviceManager::refreshSync);
    connect(this, &DeviceManager::mirrorsChanged, this, &DeviceManager::syncGrid);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &DeviceManager::syncGrid);
    connect(m_input, &InputController::gamepadNotice, this, [this](const QString& serial, const QString& text) {
        const int index = indexOf(serial);
        if (index >= 0 && m_rows[static_cast<std::size_t>(index)].mirror != nullptr) {
            qInfo("gamepad on %s: %s", qUtf8Printable(serial), qUtf8Printable(text));
            m_rows[static_cast<std::size_t>(index)].mirror->showToast(text, QStringLiteral("gamepad-2"));
        }
    });
    m_keyHints = m_settings.effective({}).keymap.hints;
    m_recordings = new Recordings(m_settings, this);
    connect(m_recordings, &Recordings::failed, this, [this](const QString& serial, const QString& problem) {
        const int at = indexOf(serial);
        if (at < 0) {
            return;
        }
        qWarning("recording of %s stopped: %s", qUtf8Printable(serial), qUtf8Printable(problem));
        Row& row = m_rows[static_cast<std::size_t>(at)];
        stopRecording(row, true);
        if (row.mirror != nullptr) {
            row.mirror->showToast(tr("The recording stopped: %1").arg(problem), QStringLiteral("triangle-alert"));
        }
    });
    connect(m_recordings, &Recordings::exported, this, &DeviceManager::recordingExported);
    m_panels = std::make_unique<SystemPanelService>(
        *this, [this](const QString& serial, protocol::SystemPanelAction action) { showSystemPanel(serial, action); });
    connect(this, &DeviceManager::countsChanged, this, &DeviceManager::syncPanelReach);
    connect(this, &QAbstractItemModel::rowsInserted, this, &DeviceManager::syncPanelReach);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &DeviceManager::syncPanelReach);
    connect(&LogicServices::instance(), &LogicServices::logicStarted, this, &DeviceManager::syncPanelReach);
    m_foreground = std::make_unique<platform::ForegroundWatcher>([this] { foregroundChanged(); });
    m_sorted.setSourceModel(this);
    m_sorted.setSortRole(RankRole);
    m_sorted.setFilterRole(CardRole);
    m_sorted.setFilterRegularExpression(QRegularExpression(QStringLiteral("^true$")));
    m_sorted.setDynamicSortFilter(true);
    m_sorted.sort(0);
    ensureAdb();
    m_usb = std::make_unique<UsbPhoneWatcher>();
    connect(m_usb.get(), &UsbPhoneWatcher::phonesChanged, this, &DeviceManager::usbChanged);
    m_usb->start();
    m_transfers = std::make_unique<FileTransfers>(m_adb);
    connect(m_transfers.get(), &FileTransfers::updated, this, &DeviceManager::transferUpdated);
    connect(m_transfers.get(), &FileTransfers::updated, this, &DeviceManager::dropTransferUpdated);
    connect(this, &DeviceManager::installUpdated, this, &DeviceManager::dropInstallUpdated);
    m_files = std::make_unique<PhoneFiles>(
        m_adb, *m_transfers,
        PhoneFiles::Source{.availability =
                               [this](const QString& serial) {
                                   const int index = indexOf(serial);
                                   if (index < 0) {
                                       return FilesAvailability{};
                                   }
                                   const Row& row = m_rows[static_cast<std::size_t>(index)];
                                   return FilesAvailability{.name = row.name(), .online = filesOnline(row)};
                               }},
        std::filesystem::path(QStandardPaths::writableLocation(QStandardPaths::TempLocation).toStdWString()) /
            L"GameScrcpy" / L"files-drag");
    const auto followFiles = [this] { m_files->refreshAvailability(); };
    connect(this, &QAbstractItemModel::dataChanged, m_files.get(), followFiles);
    connect(this, &QAbstractItemModel::rowsInserted, m_files.get(), followFiles);
    connect(this, &QAbstractItemModel::rowsRemoved, m_files.get(), followFiles);
    connect(this, &QAbstractItemModel::modelReset, m_files.get(), followFiles);
    connect(this, &DeviceManager::setupChanged, m_files.get(), followFiles);
    m_captureWatcher = std::make_unique<CaptureCardWatcher>();
    connect(m_captureWatcher.get(), &CaptureCardWatcher::devicesChanged, this, &DeviceManager::captureDevicesChanged);
    m_captureWatcher->start();
    m_backgrounds = std::make_unique<KeymapBackgrounds>(dataDirectory() / "backgrounds");
    m_esp32 = std::make_unique<Esp32Boards>();
    connect(m_esp32.get(), &Esp32Boards::boardsChanged, this, &DeviceManager::esp32BoardsChanged,
            Qt::QueuedConnection);
    m_esp32->start();
    syncEsp32();
    m_matcher = new CaptureMatcher(m_adb.port(), this);
    connect(m_matcher, &CaptureMatcher::suggested, this, &DeviceManager::matcherSuggested);
    connect(this, &DeviceManager::countsChanged, this, &DeviceManager::bumpLinks);
    connect(this, &QAbstractItemModel::rowsInserted, this, &DeviceManager::bumpLinks);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &DeviceManager::bumpLinks);
    connect(&m_settings, &AppSettings::valuesChanged, this, &DeviceManager::bumpLinks);
    connect(&m_settings, &AppSettings::devicesChanged, this, &DeviceManager::bumpLinks);
}

DeviceManager::~DeviceManager()
{
    m_captureWatcher.reset();
    if (m_recordings != nullptr) {
        m_recordings->stopAll();
    }
    m_usb.reset();
    m_companionLan.reset();
    m_endingCompanions.clear();
    for (Row& row : m_rows) {
        row.companion.reset();
    }
    closeVisionWorkbench(true);
    m_identifier.reset();
    if (m_extensionLogic != nullptr) {
        m_extensionLogic->endSessions();
    }
    m_taskRuns.clear();
    for (Row& row : m_rows) {
        row.logic.reset();
    }
    delete m_grid.data();
    for (Row& row : m_rows) {
        delete row.mirror;
        row.mirror = nullptr;
        delete row.controller;
        row.controller = nullptr;
        delete row.aoa;
        row.aoa = nullptr;
        delete row.esp32;
        row.esp32 = nullptr;
        row.demo.reset();
        row.capture.reset();
    }
    m_esp32.reset();
    m_tracker.reset();
    s_instance = nullptr;
}

DeviceManager* DeviceManager::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

QString DeviceManager::setupProblem() const
{
    return m_package ? m_adbProblem : FailureAdvice::forMissingPackage().problem;
}

QString DeviceManager::setupAction() const
{
    return m_package ? m_adbAction : FailureAdvice::forMissingPackage().action;
}

bool DeviceManager::canRetryAdb() const
{
    return m_package && !m_adbReady && !m_adbProblem.isEmpty();
}

int DeviceManager::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QString DeviceManager::Row::name() const
{
    if (demo != nullptr) {
        return demoNumber > 0 ? DeviceManager::tr("Demo device %1").arg(demoNumber) : DeviceManager::tr("Demo device");
    }
    if (capture != nullptr) {
        return captureCardName(capture->name);
    }
    if (!esp32Where.isEmpty()) {
        return DeviceManager::tr("ESP32 touch box");
    }
    if (provided) {
        return provided->name;
    }
    if (transported) {
        return transported->name;
    }
    const QString name = DevicePresentation::nameOf(marketName, manufacturer, model);
    if (!appLabel.isEmpty()) {
        return DeviceManager::tr("%1 — %2").arg(name.isEmpty() ? host : name, appLabel);
    }
    if (name.isEmpty() && aoaOnly && usb) {
        return QString::fromStdString(usb->serial);
    }
    return name.isEmpty() ? serial : name;
}

QVariant DeviceManager::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_rows.size())) {
        return {};
    }
    const Row& row = m_rows[static_cast<std::size_t>(index.row())];
    const session::SessionPhase phase = row.session.phase;
    std::optional<Advice> advice = row.aoaOnly ? aoaAdviceOf(row) : adviceOf(row);
    if (!advice && !row.aoaOnly) {
        advice = aoaAdviceOf(row);
    }
    if (row.capture != nullptr) {
        advice = captureAdviceOf(row);
    }
    if (isEsp32Row(row)) {
        advice = esp32AdviceOf(row);
    }
    if (row.provided) {
        advice = providedAdviceOf(row);
    }
    if (row.transported) {
        advice = transportedAdviceOf(row);
    }

    switch (role) {
    case SerialRole:
        return row.serial;
    case NameRole:
        return row.name();
    case DeviceStateRole:
        if (!row.listed) {
            return tr("Not detected");
        }
        if (row.aoaOnly) {
            return row.usb && row.usb->driver == platform::UsbDriver::Ready ? tr("Online") : tr("Needs a driver");
        }
        if (isEsp32Row(row)) {
            return esp32Usable(row) ? tr("Online") : tr("In use");
        }
        if (row.provided) {
            return row.provided->state == u"busy"      ? tr("In use")
                   : row.provided->state == u"problem" ? tr("Needs attention")
                                                       : tr("Online");
        }
        if (row.transported) {
            return tr("Online");
        }
        switch (row.state) {
        case adb::DeviceState::Device:
            return tr("Online");
        case adb::DeviceState::Unauthorized:
            return tr("Waiting for authorization");
        case adb::DeviceState::Offline:
            return tr("Offline");
        default:
            return tr("Unavailable");
        }
    case StatusRole:
        if (phase == session::SessionPhase::Streaming && row.link) {
            return tr("%1 (%2)").arg(FailureAdvice::phase(phase),
                                     row.transported ? row.transported->transportTitle : transportName(*row.link));
        }
        return FailureAdvice::phase(phase);
    case ProblemRole:
        return advice ? advice->problem : QString();
    case ActionRole:
        return advice ? advice->action : QString();
    case CanConnectRole:
        return canConnect(row);
    case ConnectedRole:
        return streaming(row);
    case BusyRole:
        return connecting(row);
    case AudioRole:
        return streaming(row) && !row.silent && !isBoxRow(row);
    case MutedRole:
        return row.muted;
    case VolumeRole:
        return row.volume;
    case DiagnosticsRole:
        return row.diagnostics;
    case ScreenControlRole:
        return hasCapability(row, protocol::Welcome::kDisplayPower);
    case ScreenOffRole:
        return row.screenOff;
    case RotationControlRole:
        return hasCapability(row, protocol::Welcome::kRotation);
    case OrientationRole:
        return row.orientation;
    case TransportRole:
        return static_cast<int>(row.transport);
    case PillRole:
    case ToneRole:
    case StageRole:
    case FailedRole:
    case RankRole: {
        const DevicePresentation shown =
            row.provided    ? providedPresentation(row)
            : isEsp32Row(row) ? esp32Presentation(row)
            : row.aoaOnly   ? DevicePresentation::ofAoa(phase, row.session.failure, row.listed,
                                                        row.usb && row.usb->driver == platform::UsbDriver::Ready)
            : row.transported ? DevicePresentation::of(phase, row.session.failure, row.listed, adb::DeviceState::Device)
                            : DevicePresentation::of(phase, row.session.failure, row.listed, row.state);
        switch (role) {
        case PillRole:
            return shown.pill;
        case ToneRole:
            return DevicePresentation::toneName(shown.tone);
        case StageRole:
            return shown.stage;
        case FailedRole:
            return shown.failed;
        default:
            return shown.rank;
        }
    }
    case MetaRole: {
        QString meta;
        if (row.demo != nullptr) {
            meta = tr("%1 / %2").arg(linkName(row), row.serial);
        } else if (row.capture != nullptr) {
            const QString format = row.capture->feed != nullptr && streaming(row) ? row.capture->feed->formatText()
                                                                                  : QString();
            meta = tr("%1 / %2").arg(linkName(row), format.isEmpty() ? row.serial : format);
        } else if (row.aoaOnly) {
            meta = tr("%1 / %2").arg(aoaLinkName(streaming(row) && noPictureOf(row)),
                                     row.usb ? QString::fromStdString(row.usb->serial) : row.serial);
        } else if (isEsp32Row(row)) {
            meta = tr("%1 / %2").arg(linkName(row), row.esp32Where);
        } else if (row.provided) {
            meta = tr("%1 / %2").arg(row.provided->extensionName, QString::fromStdString(row.provided->id));
        } else if (row.transported) {
            const TransportPhone& phone = *row.transported;
            meta = tr("%1 · %2 / %3")
                       .arg(phone.transportTitle, phone.extensionName,
                            phone.model.isEmpty() ? QString::fromStdString(phone.id) : phone.model);
            if (!phone.detail.isEmpty()) {
                meta = tr("%1 / %2").arg(meta, phone.detail);
            }
        } else {
            QStringList attachments;
            for (const QVariant& link : linksOf(row)) {
                attachments.append(link.toMap().value(QStringLiteral("name")).toString());
            }
            const bool aoaTouch = phase == session::SessionPhase::Streaming && row.controller != nullptr &&
                                  row.controller->touchesOverAoa();
            const QString attachment = aoaTouch ? aoaTouchLinkName(row.serial)
                                       : phase == session::SessionPhase::Streaming && row.link
                                           ? transportName(*row.link)
                                       : attachments.size() > 1 ? attachments.join(QStringLiteral(" · "))
                                                                : attachmentName(row.serial);
            meta = row.androidRelease.isEmpty()
                       ? tr("%1 / %2").arg(attachment, row.serial)
                       : tr("Android %1 / %2 / %3").arg(row.androidRelease, attachment, row.serial);
        }
        const QString profile = profileName(row.profileId.isEmpty() ? rememberedProfile(row) : row.profileId);
        return profile.isEmpty() ? meta : tr("%1 / Profile %2").arg(meta, profile);
    }
    case StatsRole:
        return row.stats;
    case WirelessRole:
        return isWireless(row.serial);
    case CardRole:
        return carded(row);
    case LinksRole:
        return linksOf(row);
    case TunnelOfferRole:
        return row.demo == nullptr && offersTunnel(row.session.phase, row.session.failure);
    case ThumbnailRole:
        return row.thumbnail;
    case DeviceIdRole:
        return row.deviceId;
    case GuideRole:
        return guideOf(row);
    case HostRole:
        return row.host;
    case FactsRole: {
        const QString attachment = row.demo != nullptr                  ? QStringLiteral("demo")
                                   : row.capture != nullptr             ? QStringLiteral("capture")
                                   : isEsp32Row(row)                    ? QStringLiteral("esp32")
                                   : row.provided.has_value()           ? QStringLiteral("touch-box")
                                   : row.transported.has_value()        ? QStringLiteral("transport")
                                   : row.aoaOnly                        ? QStringLiteral("aoa")
                                   : row.serial.startsWith(u"emulator-") ? QStringLiteral("emulator")
                                   : isWireless(row.serial)             ? QStringLiteral("wireless-adb")
                                                                        : QStringLiteral("usb");
        QVariantMap facts{{QStringLiteral("android"), row.androidRelease}, {QStringLiteral("attachment"), attachment}};
        if (phase == session::SessionPhase::Streaming && row.link) {
            const bool direct = *row.link == session::TransportKind::WifiDirect;
            const bool carried = *row.link == session::TransportKind::Extension;
            facts.insert(QStringLiteral("link"), direct    ? QStringLiteral("wifi-direct")
                                                 : carried ? QStringLiteral("extension")
                                                           : QStringLiteral("adb-tunnel"));
        }
        if (row.aoaOnly || (row.controller != nullptr && row.controller->touchesOverAoa())) {
            facts.insert(QStringLiteral("touch"), QStringLiteral("aoa"));
        }
        return facts;
    }
    case HelpRole:
        return driverHelpOf(row);
    case CaptureRole:
        return captureState(row);
    case Esp32Role:
        return esp32State(row);
    case ProvidedRole:
        return providedState(row);
    case PictureRole:
        return pictureStateOf(row);
    case TransportedRole:
        return transportedState(row);
    default:
        return {};
    }
}

std::optional<Advice> DeviceManager::adviceOf(const Row& row)
{
    const session::SessionPhase phase = row.session.phase;
    if (phase == session::SessionPhase::Failed && row.session.failure) {
        return FailureAdvice::forFailure(*row.session.failure, isWireless(row.serial));
    }
    if (phase == session::SessionPhase::Reconnecting && row.session.failure) {
        return FailureAdvice::forReconnecting(*row.session.failure);
    }
    if (!row.refusalProblem.isEmpty() && phase == session::SessionPhase::Streaming) {
        return Advice{row.refusalProblem, row.refusalAction};
    }
    if (row.listed && row.state == adb::DeviceState::Unauthorized) {
        session::SessionFailure failure;
        failure.kind = session::FailureKind::DeviceUnauthorized;
        return FailureAdvice::forFailure(failure);
    }
    return std::nullopt;
}

QString DeviceManager::vendorOf(const Row& row)
{
    if (row.manufacturer.isEmpty()) {
        return {};
    }
    const std::string_view id = PhoneVendors::forManufacturer(row.manufacturer.toStdString()).id;
    return QString::fromLatin1(id.data(), static_cast<qsizetype>(id.size()));
}

QString DeviceManager::guideOf(const Row& row)
{
    const session::SessionPhase phase = row.session.phase;
    if (phase == session::SessionPhase::Streaming && row.refusalCode == protocol::ErrorCode::InjectionDenied) {
        return vendorOf(row) + QStringLiteral("/input");
    }
    if (row.listed && row.state == adb::DeviceState::Unauthorized) {
        return vendorOf(row) + QStringLiteral("/allow");
    }
    if (phase == session::SessionPhase::Failed && row.session.failure) {
        switch (row.session.failure->kind) {
        case session::FailureKind::DeviceUnauthorized:
            return vendorOf(row) + QStringLiteral("/allow");
        case session::FailureKind::DeviceNotFound:
        case session::FailureKind::DeviceOffline:
            return vendorOf(row) + QStringLiteral("/");
        default:
            break;
        }
    }
    return {};
}

QVariantMap DeviceManager::leadDevice() const
{
    if (m_sorted.rowCount() == 0) {
        return {};
    }
    const int first = m_sorted.mapToSource(m_sorted.index(0, 0)).row();
    if (first < 0 || first >= static_cast<int>(m_rows.size())) {
        return {};
    }
    const Row* lead = &m_rows[static_cast<std::size_t>(first)];
    QString state;
    if (streaming(*lead)) {
        state = QStringLiteral("streaming");
    } else if (connecting(*lead)) {
        state = QStringLiteral("busy");
    } else if (lead->session.phase == session::SessionPhase::Failed) {
        state = QStringLiteral("failed");
    } else if (canConnect(*lead)) {
        state = QStringLiteral("ready");
    } else if (lead->listed && lead->state == adb::DeviceState::Unauthorized) {
        state = QStringLiteral("unauthorized");
    } else {
        state = QStringLiteral("offline");
    }
    const std::optional<Advice> advice = adviceOf(*lead);
    return {{QStringLiteral("serial"), lead->serial},
            {QStringLiteral("name"), lead->name()},
            {QStringLiteral("state"), state},
            {QStringLiteral("canConnect"), canConnect(*lead)},
            {QStringLiteral("vendor"), vendorOf(*lead)},
            {QStringLiteral("guide"), guideOf(*lead)},
            {QStringLiteral("problem"), advice ? advice->problem : QString()},
            {QStringLiteral("action"), advice ? advice->action : QString()}};
}

bool DeviceManager::canConnect(const Row& row) const
{
    if (row.demo != nullptr) {
        return !active(row.session.phase);
    }
    if (row.capture != nullptr) {
        return row.listed && row.capture->feed != nullptr && row.capture->drivenBy.isEmpty() &&
               !active(row.session.phase);
    }
    if (row.aoaOnly) {
        const bool driver = row.usb.has_value() && row.usb->driver == platform::UsbDriver::Ready;
        const bool card = m_settings.effective(row.deviceId.toStdString()).picture.source ==
                              settings::PictureSource::Capture &&
                          cardIndexOf(row.deviceId) >= 0;
        return row.listed && (driver || card) && !active(row.session.phase);
    }
    if (isEsp32Row(row)) {
        return row.listed && esp32Usable(row) && !active(row.session.phase);
    }
    if (row.provided) {
        return row.listed && row.provided->state == u"ready" && !row.provided->touchMethod.empty() &&
               !active(row.session.phase);
    }
    if (row.transported) {
        return row.listed && !active(row.session.phase);
    }
    return m_package && m_adbReady && row.listed && row.state == adb::DeviceState::Device && !active(row.session.phase);
}

bool DeviceManager::streaming(const Row& row)
{
    return row.session.phase == session::SessionPhase::Streaming;
}

bool DeviceManager::connecting(const Row& row)
{
    return active(row.session.phase) && !streaming(row);
}

bool DeviceManager::hasCapability(const Row& row, std::uint32_t capability)
{
    if (row.demo != nullptr) {
        return streaming(row);
    }
    if (row.capture != nullptr) {
        return false;
    }
    return streaming(row) && row.controller != nullptr && (row.controller->capabilities() & capability) != 0;
}

DeviceFacts DeviceManager::factsOf(const Row& row) const
{
    DeviceFacts facts;
    facts.serial = row.serial;
    facts.name = row.name();
    facts.deviceId = row.deviceId;
    facts.canConnect = canConnect(row);
    facts.connected = streaming(row);
    facts.busy = connecting(row);
    facts.audio = streaming(row) && !row.silent && !isBoxRow(row);
    facts.muted = row.muted;
    facts.volume = row.volume;
    facts.diagnostics = row.diagnostics;
    facts.screenControl = hasCapability(row, protocol::Welcome::kDisplayPower);
    facts.screenOff = row.screenOff;
    facts.rotationControl = hasCapability(row, protocol::Welcome::kRotation);
    facts.orientation = row.orientation;
    facts.transport = static_cast<int>(row.transport);
    facts.transportChoice = row.capture == nullptr && !isBoxRow(row) && !row.transported;
    facts.wireless = isWireless(row.serial);
    facts.fullScreen = row.mirror != nullptr && row.mirror->fullScreen();
    facts.mini = row.mirror != nullptr && row.mirror->mini();
    facts.editing = row.mirror != nullptr && row.mirror->editing();
    facts.trying = row.mirror != nullptr && row.mirror->trying();
    facts.logic = row.mirror != nullptr && row.mirror->editor() != nullptr && row.mirror->editor()->logicShown();
    facts.latencyTest = row.controller != nullptr && row.controller->canMeasureLatency();
    facts.measuringLatency =
        row.controller != nullptr && row.controller->latencyTest() && row.controller->latencyTest()->running;
    facts.keymap = keymapOf(row);
    facts.hasProfile = effectiveProfile(row) != nullptr;
    facts.cameraLocked = cameraLockedFor(row.serial);
    facts.keyHints = m_keyHints;
    facts.profiles = m_library != nullptr ? m_library->usableCount() : 0;
    facts.apps = row.host.isEmpty() && hasCapability(row, protocol::Welcome::kApps) && row.demo == nullptr;
    facts.files = filesOnline(row);
    facts.noPicture = noPictureOf(row);
    facts.recording = m_recordings != nullptr && m_recordings->running(row.serial);
    facts.systemPanel = panelReachOf(row) == PanelReach::Ready;
    if (const CaptureFeed* feed = pictureFeedOf(row); feed != nullptr) {
        facts.audio = streaming(row) && feed->sound();
    }
    if (!row.host.isEmpty()) {
        facts.audio = false;
        facts.screenControl = false;
        facts.rotationControl = false;
        facts.wireless = false;
    }
    return facts;
}

std::vector<DeviceFacts> DeviceManager::facts() const
{
    std::vector<DeviceFacts> all;
    all.reserve(m_rows.size());
    for (const Row& row : m_rows) {
        if (row.host.isEmpty() && (row.capture == nullptr || carded(row))) {
            all.push_back(factsOf(row));
        }
    }
    return all;
}

std::optional<DeviceFacts> DeviceManager::facts(const QString& serial) const
{
    const int index = indexOf(serial);
    return index < 0 ? std::nullopt : std::optional(factsOf(m_rows[static_cast<std::size_t>(index)]));
}

int DeviceManager::deviceCount() const
{
    return static_cast<int>(std::ranges::count_if(m_rows, [this](const Row& row) { return carded(row); }));
}

int DeviceManager::mirroringCount() const
{
    return static_cast<int>(std::ranges::count_if(m_rows, [](const Row& row) {
        return row.host.isEmpty() && row.session.phase == session::SessionPhase::Streaming;
    }));
}

QStringList DeviceManager::onlineSerials() const
{
    QStringList serials;
    for (const Row& row : m_rows) {
        if (row.listed && row.state == adb::DeviceState::Device && row.demo == nullptr && row.capture == nullptr) {
            serials.append(row.serial);
        }
    }
    return serials;
}

void DeviceManager::showWindow(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0 || showFromGrid(serial, true)) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.demo != nullptr && row.mirror == nullptr) {
        openDemoMirror(row);
        updateStatsTimer();
        rowChanged(index);
    }
    if (MirrorWindow* window = row.mirror; window != nullptr) {
        window->setWindowStates(window->windowStates() & ~Qt::WindowMinimized);
        window->show();
        window->raise();
        window->requestActivate();
    }
}

void DeviceManager::setFullScreen(const QString& serial, bool on)
{
    if (on) {
        showFromGrid(serial, true);
    }
    const int index = indexOf(serial);
    if (index >= 0 && m_rows[static_cast<std::size_t>(index)].mirror != nullptr) {
        m_rows[static_cast<std::size_t>(index)].mirror->setFullScreen(on);
    }
}

void DeviceManager::setMini(const QString& serial, bool on)
{
    const int index = indexOf(serial);
    if (index < 0 || m_rows[static_cast<std::size_t>(index)].mirror == nullptr) {
        return;
    }
    if (on) {
        showFromGrid(serial, true);
    }
    m_rows[static_cast<std::size_t>(index)].mirror->setMini(on);
    foregroundChanged();
    rowChanged(index);
}

void DeviceManager::setEditing(const QString& serial, bool on)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.mirror == nullptr) {
        return;
    }
    if (!on) {
        finishEditing(serial, true);
        return;
    }
    if (row.mirror->editing() || row.mirror->mini()) {
        return;
    }
    showFromGrid(serial, true);
    const ProfileEntry* entry = m_library != nullptr ? m_library->find(row.profileId) : nullptr;
    if (entry == nullptr || entry->profile == nullptr) {
        row.mirror->showToast(tr("Choose a profile first, then edit it"), QStringLiteral("info"));
        return;
    }
    const std::shared_ptr<ControlChannel> channel = channelOf(row);
    const std::optional<InputScreen> screen = channel != nullptr ? channel->screen() : std::nullopt;
    input::ScreenGeometry geometry{.width = 2400, .height = 1080};
    if (screen && screen->geometry.width > 0 && screen->geometry.height > 0) {
        geometry = screen->geometry;
    }
    auto* editor = new ProfileEditor(*entry->profile, geometry, row.mirror);
    row.editingId = entry->id;
    row.mirror->setEditor(editor);
    const QString profileId = entry->id;
    const QString profileName = entry->name;
    editor->setLogic(entry->folder.empty() ? QString() : QString::fromStdWString(entry->folder.wstring()),
                     entry->readOnly() ? tr("%1 comes with an extension and cannot have its own logic. Copy it to your "
                                            "profiles first")
                                             .arg(profileName)
                                       : QString(),
                     [this, profileId, profileName](QString& problem) -> QString {
                         const QString folder = m_library != nullptr ? m_library->ensureFolder(profileId) : QString();
                         if (folder.isEmpty()) {
                             problem = m_library != nullptr && m_library->lastWriteBusy()
                                           ? busyText()
                                           : tr("The logic of %1 cannot be opened").arg(profileName);
                         }
                         return folder;
                     });
    editor->setHandedOver(row.logic != nullptr ? row.logic->handedOver() : QStringList());
    connect(editor, &ProfileEditor::logicWanted, this, [this, serial, profileName, editor](const QString& folder) {
        attachLogic(serial, *editor, folder, profileName);
    });
    connect(editor, &ProfileEditor::logicShownChanged, this, [this, serial] {
        if (const int at = indexOf(serial); at >= 0) {
            rowChanged(at);
        }
    });
    connect(editor, &ProfileEditor::logicProblem, row.mirror,
            [mirror = row.mirror](const QString& text) { mirror->showToast(text, QStringLiteral("triangle-alert")); });
    connect(editor, &ProfileEditor::tryingChanged, this, [this, serial] { followTrial(serial, true); });
    connect(editor, &ProfileEditor::changed, this, [this, serial] { followTrial(serial, false); });
    connect(editor, &ProfileEditor::defaultsBusy, row.mirror,
            [this, mirror = row.mirror] { mirror->showToast(busyText(), QStringLiteral("triangle-alert")); });
    foregroundChanged();
    rowChanged(index);
    emit mirrorsChanged();
}

void DeviceManager::editorAction(const QString& serial, const QString& action)
{
    const int index = indexOf(serial);
    if (index < 0 || m_rows[static_cast<std::size_t>(index)].mirror == nullptr) {
        return;
    }
    ProfileEditor* editor = m_rows[static_cast<std::size_t>(index)].mirror->editor();
    if (editor == nullptr) {
        return;
    }
    if ((action == u"undo" || action == u"redo") && editor->logicShown()) {
        emit editor->logicActionWanted(action);
        return;
    }
    if (action == u"undo") {
        editor->undo();
    } else if (action == u"redo") {
        editor->redo();
    } else if (action == u"delete") {
        editor->removeSelection();
    } else if (action == u"duplicate") {
        editor->duplicateSelection();
    } else if (action == u"selectAll") {
        editor->selectAll();
    } else if (action == u"save") {
        saveEditing(serial);
    } else if (action == u"try" || action == u"edit") {
        editor->setTrying(action == u"try");
    }
}

bool DeviceManager::saveEditing(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0 || m_rows[static_cast<std::size_t>(index)].mirror == nullptr) {
        return false;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    ProfileEditor* editor = row.mirror->editor();
    if (editor == nullptr) {
        return false;
    }
    if (editor->preview()) {
        return true;
    }
    if (auto* bench = qobject_cast<ScriptWorkbench*>(editor->logic());
        bench != nullptr && bench->dirtyCount() > 0 && !bench->saveAll()) {
        return false;
    }
    if (!editor->dirty()) {
        return true;
    }
    if (m_library == nullptr || !m_library->save(row.editingId, editor->profile())) {
        const ProfileEntry* entry = m_library != nullptr ? m_library->find(row.editingId) : nullptr;
        row.mirror->showToast(m_library != nullptr && m_library->lastWriteBusy() ? busyText()
                              : entry != nullptr && entry->readOnly()
                                  ? tr("These changes cannot be kept on %1. Use Save as…").arg(entry->name)
                                  : tr("The profile could not be saved"),
                              QStringLiteral("triangle-alert"));
        return false;
    }
    editor->markSaved();
    return true;
}

void DeviceManager::finishEditing(const QString& serial, bool save)
{
    const int index = indexOf(serial);
    if (index < 0 || m_rows[static_cast<std::size_t>(index)].mirror == nullptr) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    ProfileEditor* editor = row.mirror->editor();
    if (editor == nullptr) {
        return;
    }
    if (save && !saveEditing(serial)) {
        return;
    }
    disconnect(editor, nullptr, this, nullptr);
    row.mirror->setEditor(nullptr);
    row.editingId.clear();
    editor->deleteLater();
    applyProfile(row);
    syncInputProfile();
    foregroundChanged();
    rowChanged(index);
    emit mirrorsChanged();
}

bool DeviceManager::editProfile(const QString& serial, const QString& id)
{
    const int index = indexOf(serial);
    if (index < 0 || m_library == nullptr || m_library->profile(id) == nullptr) {
        return false;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.mirror == nullptr) {
        return false;
    }
    if (row.mirror->editing()) {
        showWindow(serial);
        if (row.editingId == id) {
            return true;
        }
        row.mirror->showToast(tr("Finish editing %1 first").arg(profileName(row.editingId)), QStringLiteral("info"));
        return false;
    }
    if (row.mirror->mini()) {
        setMini(serial, false);
    }
    selectProfile(row, id, true);
    setEditing(serial, true);
    showWindow(serial);
    return row.mirror != nullptr && row.mirror->editing();
}

void DeviceManager::showAssistant(const QString& serial)
{
    emit assistantWanted(serial);
}

bool DeviceManager::openDraftPreviewAt(const QString& target, const QString& serial, QString& problem)
{
    McpServer* server = McpServer::instance();
    if (server == nullptr || target.section(u':', 0, 0) != u"profile") {
        problem = tr("Only a draft of a profile can be shown");
        return false;
    }
    QString client;
    for (const McpDraftFacts& each : server->drafts().pending()) {
        if (each.target == target) {
            client = each.client;
            break;
        }
    }
    if (client.isEmpty()) {
        problem = tr("There is no draft of %1 to show").arg(target.section(u':', 1));
        return false;
    }
    const std::optional<McpDraftPreview> plan = server->drafts().preview(client, target);
    if (!plan) {
        problem = tr("The draft of %1 cannot be read").arg(target.section(u':', 1));
        return false;
    }
    const input::LoadResult drafted = input::loadProfile(plan->profileText.toStdString());
    if (!drafted.ok()) {
        problem = tr("The draft's profile.json has problems, so it cannot be shown; ask the AI to fix it");
        return false;
    }
    input::Profile live;
    if (!plan->liveText.isEmpty()) {
        const input::LoadResult base = input::loadProfile(plan->liveText.toStdString());
        if (base.ok()) {
            live = base.profile;
        }
    }
    const QString id = target.section(u':', 1);
    int index = serial.isEmpty() ? -1 : indexOf(serial);
    if (index < 0 || m_rows[static_cast<std::size_t>(index)].mirror == nullptr) {
        index = -1;
        for (int at = 0; at < static_cast<int>(m_rows.size()); ++at) {
            const Row& row = m_rows[static_cast<std::size_t>(at)];
            if (row.mirror != nullptr && row.profileId == id) {
                index = at;
                break;
            }
        }
    }
    if (index < 0) {
        for (int at = 0; at < static_cast<int>(m_rows.size()); ++at) {
            if (m_rows[static_cast<std::size_t>(at)].mirror != nullptr) {
                index = at;
                break;
            }
        }
    }
    if (index < 0) {
        problem = tr("Start mirroring a phone first; the key mapping editor draws the keys on its picture");
        return false;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.mirror == nullptr) {
        problem = tr("Start mirroring a phone first; the key mapping editor draws the keys on its picture");
        return false;
    }
    if (row.mirror->editing()) {
        ProfileEditor* open = row.mirror->editor();
        if (open != nullptr && open->preview()) {
            if (open->previewTarget() == target) {
                showWindow(row.serial);
                return true;
            }
            problem = tr("Close the draft preview that is open on %1 first").arg(row.name());
            return false;
        }
        problem = tr("Finish editing %1 first").arg(profileName(row.editingId));
        return false;
    }
    if (row.mirror->mini()) {
        setMini(row.serial, false);
    }
    const std::shared_ptr<ControlChannel> channel = channelOf(row);
    const std::optional<InputScreen> screen = channel != nullptr ? channel->screen() : std::nullopt;
    input::ScreenGeometry geometry{.width = 2400, .height = 1080};
    if (screen && screen->geometry.width > 0 && screen->geometry.height > 0) {
        geometry = screen->geometry;
    }
    auto* editor = new ProfileEditor(drafted.profile, geometry, row.mirror);
    editor->setPreview({.client = plan->client,
                        .target = target,
                        .live = live,
                        .liveFolder = plan->liveFolder,
                        .draftFolder = plan->draftFolder});
    row.mirror->setEditor(editor);
    editor->setLogic(plan->draftFolder, QString(), ProfileEditor::LogicFolder{});
    const QString name = plan->name;
    connect(editor, &ProfileEditor::logicWanted, this,
            [this, serial = row.serial, name, editor](const QString& folder) {
                attachLogic(serial, *editor, folder, name);
                if (auto* bench = qobject_cast<ScriptWorkbench*>(editor->logic()); bench != nullptr) {
                    bench->setReadOnly(true);
                }
            });
    connect(editor, &ProfileEditor::logicShownChanged, this, [this, serial = row.serial] {
        if (const int at = indexOf(serial); at >= 0) {
            rowChanged(at);
        }
    });
    connect(editor, &ProfileEditor::logicProblem, row.mirror,
            [mirror = row.mirror](const QString& text) { mirror->showToast(text, QStringLiteral("triangle-alert")); });
    connect(editor, &ProfileEditor::defaultsBusy, row.mirror,
            [this, mirror = row.mirror] { mirror->showToast(busyText(), QStringLiteral("triangle-alert")); });
    foregroundChanged();
    rowChanged(index);
    emit mirrorsChanged();
    showWindow(row.serial);
    qInfo("draft preview of %s opened on %s for %s", qUtf8Printable(target), qUtf8Printable(row.name()),
          qUtf8Printable(plan->client));
    return true;
}

bool DeviceManager::openDraftPreview(const QString& target)
{
    QString problem;
    if (openDraftPreviewAt(target, QString(), problem)) {
        return true;
    }
    for (const Row& row : m_rows) {
        if (row.mirror != nullptr) {
            row.mirror->showToast(problem, QStringLiteral("triangle-alert"));
            return false;
        }
    }
    notifyMain(tr("The draft cannot be shown"), problem);
    return false;
}

void DeviceManager::applyDraftPreview(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0 || m_rows[static_cast<std::size_t>(index)].mirror == nullptr) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    ProfileEditor* editor = row.mirror->editor();
    McpServer* server = McpServer::instance();
    if (editor == nullptr || !editor->preview() || server == nullptr) {
        return;
    }
    const QString name = editor->profileName();
    const QString client = editor->previewClient();
    const QString target = editor->previewTarget();
    const McpDraftResult result = server->drafts().apply(client, target);
    if (!result.ok()) {
        row.mirror->showToast(result.problem.isEmpty() ? result.code : result.code + u' ' + result.problem,
                              QStringLiteral("triangle-alert"));
        return;
    }
    finishEditing(serial, false);
    if (row.mirror != nullptr) {
        row.mirror->showToast(tr("Applied the draft of %1").arg(name), QStringLiteral("check"));
    }
    qInfo("mcp %s: the user applied the draft of %s from its preview", qUtf8Printable(client), qUtf8Printable(target));
}

void DeviceManager::discardDraftPreview(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0 || m_rows[static_cast<std::size_t>(index)].mirror == nullptr) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    ProfileEditor* editor = row.mirror->editor();
    McpServer* server = McpServer::instance();
    if (editor == nullptr || !editor->preview() || server == nullptr) {
        return;
    }
    const QString name = editor->profileName();
    const QString client = editor->previewClient();
    const QString target = editor->previewTarget();
    server->drafts().discard(client, target);
    finishEditing(serial, false);
    if (row.mirror != nullptr) {
        row.mirror->showToast(tr("Threw away the draft of %1").arg(name), QStringLiteral("trash-2"));
    }
    qInfo("mcp %s: the user threw away the draft of %s from its preview", qUtf8Printable(client),
          qUtf8Printable(target));
}

void DeviceManager::closeDraftPreview(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0 || m_rows[static_cast<std::size_t>(index)].mirror == nullptr) {
        return;
    }
    ProfileEditor* editor = m_rows[static_cast<std::size_t>(index)].mirror->editor();
    if (editor == nullptr || !editor->preview()) {
        return;
    }
    finishEditing(serial, false);
}

bool DeviceManager::saveEditingAs(const QString& serial, const QString& name)
{
    const int index = indexOf(serial);
    if (index < 0 || m_library == nullptr || m_rows[static_cast<std::size_t>(index)].mirror == nullptr) {
        return false;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    ProfileEditor* editor = row.mirror->editor();
    const QString title = name.trimmed();
    if (editor == nullptr || title.isEmpty()) {
        return false;
    }
    input::Profile copy = editor->profile();
    copy.meta.name = title.toStdString();
    const QString id = m_library->add(copy);
    if (id.isEmpty()) {
        row.mirror->showToast(m_library->lastWriteBusy() ? busyText() : tr("The profile could not be saved"),
                              QStringLiteral("triangle-alert"));
        return false;
    }
    editor->markSavedAs(title);
    row.editingId = id;
    selectProfile(row, id, true);
    row.mirror->showToast(tr("Saved as %1").arg(title), QStringLiteral("check"));
    rowChanged(index);
    return true;
}

bool DeviceManager::restoreEditing(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return false;
    }
    const Row& row = m_rows[static_cast<std::size_t>(index)];
    MirrorWindow* mirror = row.mirror;
    if (mirror == nullptr || mirror->editor() == nullptr) {
        return false;
    }
    const QString id = row.editingId;
    if (!restoreProfile(id)) {
        mirror->showToast(m_library != nullptr && m_library->lastWriteBusy()
                              ? busyText()
                              : tr("The original profile could not be restored"),
                          QStringLiteral("triangle-alert"));
        return false;
    }
    mirror->showToast(tr("Restored the original %1").arg(profileName(id)), QStringLiteral("rotate-ccw"));
    return true;
}

bool DeviceManager::restoreProfile(const QString& id)
{
    if (m_library == nullptr || !m_library->restore(id)) {
        return false;
    }
    const std::shared_ptr<const input::Profile> original = m_library->profile(id);
    if (original == nullptr) {
        return false;
    }
    for (Row& row : m_rows) {
        ProfileEditor* editor = row.mirror != nullptr && row.editingId == id ? row.mirror->editor() : nullptr;
        if (editor != nullptr) {
            editor->replaceSaved(*original);
        }
    }
    return true;
}

void DeviceManager::setPictureFrozen(const QString& serial, bool frozen)
{
    if (MirrorWindow* mirror = mirrorOf(serial); mirror != nullptr) {
        mirror->setPictureFrozen(frozen);
    }
}

void DeviceManager::zoomPicture(const QString& serial, double zoom, double anchorX, double anchorY)
{
    if (MirrorWindow* mirror = mirrorOf(serial); mirror != nullptr) {
        mirror->zoomPicture(zoom, {anchorX, anchorY});
    }
}

void DeviceManager::scrollPicture(const QString& serial, double dx, double dy)
{
    if (MirrorWindow* mirror = mirrorOf(serial); mirror != nullptr) {
        mirror->scrollPicture({dx, dy});
    }
}

void DeviceManager::focusPicture(const QString& serial)
{
    if (MirrorWindow* mirror = mirrorOf(serial); mirror != nullptr) {
        mirror->focusPicture();
    }
}

MirrorWindow* DeviceManager::mirrorOf(const QString& serial) const
{
    const int index = indexOf(serial);
    return index >= 0 ? m_rows[static_cast<std::size_t>(index)].mirror : nullptr;
}

void DeviceManager::followTrial(const QString& serial, bool switched)
{
    const int index = indexOf(serial);
    if (index < 0 || m_rows[static_cast<std::size_t>(index)].mirror == nullptr) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    const bool trying = row.mirror->trying();
    if (!switched && !trying) {
        return;
    }
    applyProfile(row);
    syncInputProfile();
    if (!switched) {
        return;
    }
    qInfo("%s the key mapping being edited on %s", trying ? "trying" : "stopped trying", qUtf8Printable(row.name()));
    if (trying && m_input != nullptr && !m_input->keymap()) {
        m_input->setKeymap(true);
    }
    foregroundChanged();
    rowChanged(index);
}

void DeviceManager::toggleToolbar(const QString& serial)
{
    const int index = indexOf(serial);
    if (index >= 0 && m_rows[static_cast<std::size_t>(index)].mirror != nullptr) {
        m_rows[static_cast<std::size_t>(index)].mirror->toggleToolbar();
    }
}

void DeviceManager::setQmlEngine(QQmlEngine* engine)
{
    m_engine = engine;
}

void DeviceManager::setCompanionPackage(const std::filesystem::path& apk, const std::filesystem::path& metadata)
{
    m_companionApk = CompanionApk::load(apk, metadata);
    if (m_companionApk == nullptr) {
        qInfo("companion app: %s is missing; the cursor on the phone works only where the app is installed already",
              qUtf8Printable(QString::fromStdWString(apk.wstring())));
    } else if (!m_companionApk->versionCode) {
        qInfo("companion app: %s gives no version; an installed app is not updated",
              qUtf8Printable(QString::fromStdWString(metadata.wstring())));
    }
}

void DeviceManager::closeWindows()
{
    if (m_recordings != nullptr) {
        for (Row& row : m_rows) {
            if (row.controller != nullptr) {
                row.controller->setRecorder(nullptr);
            }
            if (row.demo != nullptr) {
                row.demo->setRecorder(nullptr);
            }
            if (row.capture != nullptr && row.capture->feed != nullptr) {
                row.capture->feed->setRecorder(nullptr);
            }
        }
        m_recordings->stopAll();
    }
    closeVisionWorkbench(true);
    const std::map<QString, QPointer<QObject>> workbenches = std::exchange(m_workbenches, {});
    for (const auto& workbench : workbenches) {
        delete workbench.second.data();
    }
    if (m_input != nullptr) {
        m_input->setTarget({}, nullptr, 0);
    }
    if (m_extensionLogic != nullptr) {
        m_extensionLogic->endSessions();
    }
    m_taskRuns.clear();
    if (GridWindow* grid = m_grid.data(); grid != nullptr) {
        disconnect(grid, nullptr, this, nullptr);
        delete grid;
    }
    for (Row& row : m_rows) {
        stopCompanion(row);
        row.logic.reset();
        if (row.mirror != nullptr) {
            disconnect(row.mirror, nullptr, this, nullptr);
            delete row.mirror;
            row.mirror = nullptr;
        }
    }
    updateStatsTimer();
    emit mirrorsChanged();
}

bool DeviceManager::addDemoDevice(const QString& recording, const QString& game)
{
    const int number =
        1 + static_cast<int>(std::ranges::count_if(m_rows, [](const Row& each) { return each.demo != nullptr; }));
    const QString serial = demoSerialOf(number);
    if (indexOf(serial) >= 0) {
        qWarning("demo device: %s is already added", qUtf8Printable(serial));
        return false;
    }
    if (m_graphics == nullptr) {
        qWarning("demo device: not added, there is no graphics device to decode and draw video");
        return false;
    }
    auto demo = std::make_unique<DemoFeed>(*m_graphics, std::filesystem::path(recording.toStdWString()));
    if (!demo->start()) {
        qWarning("demo device: not added, %s", qUtf8Printable(demo->problem()));
        return false;
    }
    if (number == 2) {
        for (std::size_t i = 0; i < m_rows.size(); ++i) {
            Row& first = m_rows[i];
            if (first.demo == nullptr) {
                continue;
            }
            first.demoNumber = 1;
            logDemoAs(*first.demoChannel, first.demoSink, *first.demo, first.demoNumber);
            if (first.mirror != nullptr) {
                first.mirror->setTitle(first.name());
            }
            rowChanged(static_cast<int>(i));
        }
    }
    const int at = static_cast<int>(m_rows.size());
    beginInsertRows({}, at, at);
    Row row;
    row.serial = serial;
    row.listed = true;
    row.state = adb::DeviceState::Device;
    row.deviceId = serial;
    row.demoNumber = number > 1 ? number : 0;
    row.session = {session::SessionPhase::Streaming, std::nullopt};
    row.demo = std::move(demo);
    row.demoChannel = std::make_shared<ControlChannel>();
    logDemoAs(*row.demoChannel, row.demoSink, *row.demo, row.demoNumber);
    static_cast<void>(row.demoChannel->setPadLink({.route = PadRoute::Uhid, .epoch = 1}));
    m_rows.push_back(std::move(row));
    endInsertRows();
    Row& added = m_rows.back();
    openDemoMirror(added);
    updateStatsTimer();
    emit countsChanged();
    publishPhoneEvent(AppTopic::DeviceConnected, added.serial, added.name());
    publishPhoneEvent(AppTopic::SessionStarted, added.serial, added.name());
    qInfo("%s: playing %s", demoLogName(added.demoNumber).constData(), qUtf8Printable(recording));
    if (!game.isEmpty()) {
        foregroundApp(serial, game, true);
    }
    return true;
}

QHash<int, QByteArray> DeviceManager::roleNames() const
{
    return {{SerialRole, "serial"},         {NameRole, "name"},           {DeviceStateRole, "deviceState"},
            {StatusRole, "status"},         {ProblemRole, "problem"},     {ActionRole, "action"},
            {CanConnectRole, "canConnect"}, {ConnectedRole, "connected"}, {BusyRole, "busy"},
            {AudioRole, "audio"},           {MutedRole, "muted"},         {VolumeRole, "volume"},
            {DiagnosticsRole, "diagnostics"},   {ScreenControlRole, "screenControl"},
            {ScreenOffRole, "screenOff"},       {RotationControlRole, "rotationControl"},
            {OrientationRole, "orientation"},   {TransportRole, "transport"},
            {PillRole, "pill"},                 {ToneRole, "tone"},
            {StageRole, "stage"},               {FailedRole, "failed"},
            {MetaRole, "meta"},                 {RankRole, "rank"},
            {StatsRole, "stats"},               {WirelessRole, "wireless"},
            {ThumbnailRole, "thumbnail"},       {DeviceIdRole, "deviceId"},
            {GuideRole, "guide"},               {FactsRole, "facts"},
            {TunnelOfferRole, "tunnelOffer"},   {CardRole, "card"},
            {LinksRole, "links"},               {HelpRole, "help"},
            {CaptureRole, "capture"},           {Esp32Role, "esp32"},
            {PictureRole, "picture"},           {ProvidedRole, "provided"},
            {TransportedRole, "transported"}};
}

void DeviceManager::connectDevice(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.demo != nullptr) {
        if (active(row.session.phase)) {
            return;
        }
        if (!row.demo->start()) {
            qWarning("demo device: cannot play again, %s", qUtf8Printable(row.demo->problem()));
            return;
        }
        row.demoChannel->setVideo(row.demo->videoSession(), row.demo->frameWidth(), row.demo->frameHeight());
        row.session = {session::SessionPhase::Streaming, std::nullopt};
        openDemoMirror(row);
        updateStatsTimer();
        rowChanged(index);
        publishPhoneEvent(AppTopic::SessionStarted, row.serial, row.name());
        qInfo("demo device: playing again");
        return;
    }
    if (row.capture != nullptr) {
        startCapture(row);
        return;
    }
    if (row.aoaOnly) {
        connectAoa(row);
        return;
    }
    if (isEsp32Row(row)) {
        connectEsp32(row);
        return;
    }
    if (row.provided) {
        connectProvided(row);
        return;
    }
    identify(row);
    if (row.controller == nullptr) {
        row.controller = new SessionController(m_adb, serial, m_package, m_graphics, this);
        row.controller->forceTouchBackend(m_touchBackend);
        row.controller->setTouchMethods(m_extensions != nullptr ? m_extensions->touchSource() : nullptr);
        connect(row.controller, &SessionController::touchUnavailable, this,
                [this, serial] { warnTouchUnavailable(serial); });
        connect(row.controller, &SessionController::padLinkChanged, this,
                [this, channel = row.controller->channel()] { m_input->padLinkChanged(channel); });
        connect(row.controller, &SessionController::uhidOutput, this,
                [this, channel = row.controller->channel()](quint16 id, const QByteArray& data) {
                    m_input->padOutput(channel, id, data);
                });
        connect(row.controller, &SessionController::uhidStatus, this,
                [this, channel = row.controller->channel()](quint16 id, bool recognized, bool vibration) {
                    m_input->padStatus(channel, id, recognized, vibration);
                });
        connect(row.controller, &SessionController::stateChanged, this,
                [this, serial](const session::SessionState& state) { sessionChanged(serial, state); });
        connect(row.controller, &SessionController::transportUsed, this, [this, serial](session::TransportKind kind) {
            const int at = indexOf(serial);
            if (at >= 0) {
                m_rows[static_cast<std::size_t>(at)].link = kind;
                rowChanged(at);
            }
        });
        connect(row.controller, &SessionController::foregroundAppChanged, this,
                [this, serial](const QString& package, const QString&, bool game) {
                    foregroundApp(serial, package, game);
                });
        connect(row.controller, &SessionController::latencyTestChanged, this, [this, serial] {
            const int at = indexOf(serial);
            if (at < 0) {
                return;
            }
            Row& measured = m_rows[static_cast<std::size_t>(at)];
            const std::optional<LatencyProgress>& progress = measured.controller->latencyTest();
            if (progress && !progress->running && progress->result) {
                measured.latencyResult = progress->result;
            }
            showLatencyTest(measured);
            rowChanged(at);
        });
        connect(row.controller, &SessionController::thumbnailed, this, [this, serial](const QImage& image) {
            const int at = indexOf(serial);
            if (at < 0 || m_rows[static_cast<std::size_t>(at)].session.phase != session::SessionPhase::Streaming ||
                m_rows[static_cast<std::size_t>(at)].picture.value_or(settings::PictureSource::Phone) !=
                    settings::PictureSource::Phone) {
                return;
            }
            m_rows[static_cast<std::size_t>(at)].thumbnail = image;
            const QModelIndex changed = this->index(at);
            emit dataChanged(changed, changed, {ThumbnailRole});
        });
        connect(row.controller, &SessionController::welcomed, this,
                [this, serial](const QString& manufacturer, const QString& model) {
                    const int at = indexOf(serial);
                    if (at >= 0) {
                        m_rows[static_cast<std::size_t>(at)].manufacturer = manufacturer;
                        if (m_rows[static_cast<std::size_t>(at)].model.isEmpty()) {
                            m_rows[static_cast<std::size_t>(at)].model = model;
                        }
                        rowChanged(at);
                    }
                });
        connect(row.controller, &SessionController::screenDescribed, this,
                [this, serial](const QSize& size, double refreshHz, int rotation) {
                    const int at = indexOf(serial);
                    if (at >= 0) {
                        Row& described = m_rows[static_cast<std::size_t>(at)];
                        const bool turned = described.rotation != rotation;
                        described.screen = size;
                        described.refreshHz = refreshHz;
                        described.rotation = rotation;
                        if (turned) {
                            publishPhoneEvent(AppTopic::OrientationChanged, described.serial, described.name(),
                                              {{QStringLiteral("rotation"), rotation}});
                        }
                        rememberScreen(described);
                        if (described.picture == settings::PictureSource::None) {
                            showNoPicture(described);
                        }
                        if (described.logic != nullptr) {
                            described.logic->setDevice(deviceInfoOf(described));
                            described.logic->setFrameRate(refreshHz > 0.0 ? refreshHz : 60.0);
                        }
                        for (const std::unique_ptr<TaskRun>& run : m_taskRuns) {
                            if (run->serial == serial && run->runtime != nullptr) {
                                run->runtime->setDevice(deviceInfoOf(described));
                            }
                        }
                        emit detailsChanged();
                    }
                });
        connect(row.controller, &SessionController::codecsDescribed, this, [this, serial](const QStringList& codecs) {
            const int at = indexOf(serial);
            if (at >= 0) {
                m_rows[static_cast<std::size_t>(at)].codecs = codecs;
                emit detailsChanged();
            }
        });
        connect(row.controller, &SessionController::refused, this,
                [this, serial](protocol::ErrorCode code, const QString&) {
                    const int at = indexOf(serial);
                    if (at < 0) {
                        return;
                    }
                    Row& refused = m_rows[static_cast<std::size_t>(at)];
                    if (code == protocol::ErrorCode::VideoRestored) {
                        if (refused.refusalCode == protocol::ErrorCode::VideoReduced) {
                            refused.refusalProblem.clear();
                            refused.refusalAction.clear();
                            refused.refusalCode = protocol::ErrorCode::None;
                            rowChanged(at);
                        }
                        return;
                    }
                    if (const auto advice = FailureAdvice::forRefusal(code, refused.manufacturer.toStdString())) {
                        const bool fresh = refused.refusalCode != code;
                        refused.refusalProblem = advice->problem;
                        refused.refusalAction = advice->action;
                        refused.refusalCode = code;
                        rowChanged(at);
                        if (fresh && code == protocol::ErrorCode::InjectionDenied) {
                            warnInputRefused(refused);
                        }
                    }
                });
        connect(row.controller, &SessionController::refused, m_batch,
                [batch = m_batch, serial](protocol::ErrorCode code, const QString&) {
                    if (code == protocol::ErrorCode::NotFound) {
                        batch->appRefused(serial);
                    }
                });
        connect(row.controller, &SessionController::systemPanelFailed, this, [this, serial](bool unsupported) {
            if (MirrorWindow* mirror = mirrorOf(serial)) {
                mirror->showToast(unsupported ? tr("This phone's Android version cannot open or close the "
                                                   "notification shade from the computer")
                                              : tr("The phone did not let GameScrcpy open or close the notification "
                                                   "shade"),
                                  QStringLiteral("triangle-alert"));
            }
        });
        connect(row.controller, &SessionController::foregroundAppChanged, m_batch,
                [batch = m_batch, serial](const QString& package, const QString&, bool) {
                    batch->appShown(serial, package);
                });
        connect(row.controller, &SessionController::audioUnavailable, this,
                [this, serial](protocol::AudioStatus status) {
                    const int at = indexOf(serial);
                    const auto advice = FailureAdvice::forAudioStatus(status);
                    if (at < 0 || !advice) {
                        return;
                    }
                    Row& silent = m_rows[static_cast<std::size_t>(at)];
                    silent.silent = true;
                    silent.silenceStatus = status;
                    silent.refusalProblem = advice->problem;
                    silent.refusalAction = advice->action;
                    silent.refusalCode = protocol::ErrorCode::None;
                    rowChanged(at);
                });
        connect(row.controller, &SessionController::phoneClipboardRead, this,
                [this, controller = row.controller](bool ok, const QString& text) {
                    phoneClipboardRead(controller, ok, text);
                });
        row.controller->setAudioMuted(row.muted);
        row.controller->setAudioVolume(static_cast<float>(row.volume));
        row.controller->setScreenOff(row.screenOff);
        row.controller->setOrientation(orientationMode(row.orientation));
        connectApps(serial, row.controller);
        connect(row.controller, &SessionController::deviceStatusArrived, this,
                [this, serial](const protocol::DeviceStatus& status) { phoneStatusArrived(serial, status); });
    }
    if (!row.controller->active()) {
        row.screenOff = false;
        row.orientation = 0;
        row.controller->setPictureSource(usePicture(row));
    }
    row.refusalProblem.clear();
    row.refusalAction.clear();
    row.refusalCode = protocol::ErrorCode::None;
    row.silent = false;
    row.link.reset();
    qInfo("connecting to %s (%s)", qUtf8Printable(row.name()), qUtf8Printable(serial));
    row.controller->setUsbPhone(row.usb);
    row.controller->setHostFactory(row.transported ? transportHost(*row.transported) : SessionController::HostFactory{});
    row.controller->start(row.transport, m_settings.effective(row.deviceId.toStdString()));
    row.session = {session::SessionPhase::Deploying, std::nullopt};
    rowChanged(index);
}

void DeviceManager::disconnectDevice(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    stopRecording(row, false);
    if (row.capture != nullptr) {
        if (const QString phone = row.capture->drivenBy; !phone.isEmpty()) {
            disconnectDevice(phone);
            return;
        }
        stopCapture(row);
        return;
    }
    if (row.demo != nullptr) {
        releaseInput(serial);
        if (row.mirror != nullptr) {
            row.mirror->deleteLater();
            row.mirror = nullptr;
            emit mirrorsChanged();
        }
        returnCapture(row);
        row.picture.reset();
        row.demo->stop();
        const bool wasPlaying = active(row.session.phase);
        row.session = {session::SessionPhase::Stopped, std::nullopt};
        updateStatsTimer();
        rowChanged(index);
        if (wasPlaying) {
            publishPhoneEvent(AppTopic::SessionEnded, row.serial, row.name(), {{QStringLiteral("failed"), false}});
        }
        qInfo("demo device: stopped");
    } else if (row.aoaOnly) {
        if (!row.pictureCard.isEmpty()) {
            sessionChanged(serial, {session::SessionPhase::Stopped, std::nullopt});
        } else if (row.aoa != nullptr) {
            row.aoa->stop();
        }
    } else if (isBoxRow(row)) {
        if (row.esp32 != nullptr) {
            row.esp32->stop();
        }
    } else if (row.controller != nullptr) {
        row.controller->stop();
    }
    std::vector<QPointer<SessionController>> displays;
    for (const Row& shown : m_rows) {
        if (shown.host == serial && shown.controller != nullptr && active(shown.session.phase)) {
            displays.emplace_back(shown.controller);
        }
    }
    for (const QPointer<SessionController>& display : displays) {
        if (display != nullptr) {
            display->stop();
        }
    }
}

void DeviceManager::setMuted(const QString& serial, bool muted)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    row.muted = muted;
    if (CaptureFeed* feed = pictureFeedOf(row); feed != nullptr) {
        feed->setMuted(muted);
    }
    if (row.demo != nullptr) {
        qInfo("demo device: %s, not sent", muted ? "mute" : "unmute");
    } else if (row.controller != nullptr) {
        row.controller->setAudioMuted(muted);
    }
    rowChanged(index);
}

void DeviceManager::setVolume(const QString& serial, double volume)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    row.volume = std::clamp(volume, 0.0, 1.0);
    if (row.mirror != nullptr) {
        row.mirror->setVolume(row.volume);
    }
    if (CaptureFeed* feed = pictureFeedOf(row); feed != nullptr) {
        feed->setVolume(row.volume);
    }
    if (row.demo != nullptr) {
        qInfo("demo device: volume %.2f, not sent", row.volume);
    } else if (row.controller != nullptr) {
        row.controller->setAudioVolume(static_cast<float>(row.volume));
    }
    rowChanged(index);
}

void DeviceManager::setScreenOff(const QString& serial, bool off)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    row.screenOff = off;
    if (row.demo != nullptr) {
        qInfo("demo device: screen %s, not sent", off ? "off" : "on");
    } else if (row.controller != nullptr) {
        row.controller->setScreenOff(off);
    }
    rowChanged(index);
}

void DeviceManager::setOrientation(const QString& serial, int orientation)
{
    const int index = indexOf(serial);
    if (index < 0 || orientation < 0 || orientation > 2) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    row.orientation = orientation;
    if (row.demo != nullptr) {
        qInfo("demo device: orientation %d, not sent", orientation);
    } else if (row.controller != nullptr) {
        row.controller->setOrientation(orientationMode(orientation));
    }
    rowChanged(index);
}

void DeviceManager::showSystemPanel(const QString& serial, protocol::SystemPanelAction action)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    const std::string kind(panelKindOf(action));
    if (panelReachOf(row) != PanelReach::Ready) {
        qInfo("%s: the notification shade cannot be reached now, %s not sent", qUtf8Printable(row.name()),
              kind.c_str());
        return;
    }
    if (row.demo != nullptr) {
        qInfo("%s: system panel %s, not sent", demoLogName(row.demoNumber).constData(), kind.c_str());
    } else if (row.controller != nullptr && !row.controller->showSystemPanel(action)) {
        qInfo("%s: system panel %s could not be sent", qUtf8Printable(row.name()), kind.c_str());
    }
}

PanelReach DeviceManager::panelReachOf(const Row& row)
{
    if (!row.host.isEmpty() || row.aoaOnly || row.capture != nullptr ||
        (row.controller == nullptr && row.demo == nullptr)) {
        return PanelReach::NoServer;
    }
    return streaming(row) ? PanelReach::Ready : PanelReach::Waiting;
}

void DeviceManager::syncPanelReach()
{
    if (m_panels == nullptr) {
        return;
    }
    std::map<QString, PanelReach> reach;
    for (const Row& row : m_rows) {
        reach.insert_or_assign(row.serial, panelReachOf(row));
    }
    m_panels->setReach(std::move(reach));
}

void DeviceManager::setTransport(const QString& serial, int transport)
{
    const int index = indexOf(serial);
    if (index < 0 || transport < 0 || transport > static_cast<int>(session::TransportChoice::Auto)) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    row.transport = static_cast<session::TransportChoice>(transport);
    row.transportChosen = true;
    if (row.demo != nullptr) {
        qInfo("demo device: transport %d, not saved", transport);
    } else if (!row.deviceId.isEmpty()) {
        const std::string id = row.deviceId.toStdString();
        settings::DeviceSettings device = m_settings.device(id);
        device.transport = static_cast<settings::Transport>(row.transport);
        m_settings.setDevice(id, device);
    }
    rowChanged(index);
}

void DeviceManager::setDiagnostics(const QString& serial, bool shown)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    row.diagnostics = shown;
    if (!shown && row.mirror != nullptr) {
        row.mirror->setDiagnostics({});
    }
    rowChanged(index);
}

struct DeviceManager::Measures {
    SessionMetrics::Sample sample;
    double shownPerSecond = 0;
    double replacedPerSecond = 0;
    std::optional<double> arrivalToRender;
    std::optional<double> captureToRender;
    std::optional<double> inputToRender;
    std::optional<LatencyResult> touchDots;
    media::AudioStats audio;
};

void DeviceManager::refreshStats()
{
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        Row& row = m_rows[i];
        if (!streaming(row) || (row.controller == nullptr && row.demo == nullptr && row.capture == nullptr &&
                                row.pictureCard.isEmpty())) {
            (void)row.problems.update(StatusProblem::None);
            if (row.mirror != nullptr) {
                const bool reconnecting = row.session.phase == session::SessionPhase::Reconnecting;
                row.mirror->setStatus(-1.0, -1.0, linkName(row),
                                      problemText(reconnecting ? StatusProblem::Reconnecting : StatusProblem::None));
            }
            continue;
        }
        const Measures measures = measure(row);
        QVariantMap stats{{QStringLiteral("fps"), measures.sample.framesPerSecond},
                          {QStringLiteral("megabits"), measures.sample.megabitsPerSecond}};
        if (measures.inputToRender) {
            stats.insert(QStringLiteral("latencyMs"), *measures.inputToRender);
        }
        row.stats = std::move(stats);
        if (row.mirror != nullptr) {
            if (row.diagnostics) {
                row.mirror->setDiagnostics(describe(measures));
            }
            const double shown = row.demo != nullptr ? measures.sample.framesPerSecond : measures.shownPerSecond;
            const double refreshRate = row.mirror->screen() != nullptr ? row.mirror->screen()->refreshRate() : 0;
            const StatusProblem problem =
                row.demo != nullptr || pictureFeedOf(row) != nullptr
                    ? StatusProblem::None
                    : row.problems.update(measuredProblem(measures.inputToRender, measures.sample.framesPerSecond,
                                                          measures.shownPerSecond, refreshRate));
            row.mirror->setStatus(shown, measures.inputToRender.value_or(-1.0), linkName(row), problemText(problem));
            if (row.demo != nullptr) {
                syncPhoneStatus(row);
            }
        }
        const QModelIndex changed = index(static_cast<int>(i));
        emit dataChanged(changed, changed, {StatsRole});
    }
    refreshGrid();
}

void DeviceManager::restartMeasures(Row& row)
{
    row.sampledAt = std::chrono::steady_clock::now();
    if (row.controller != nullptr) {
        (void)row.controller->metrics().sample(std::chrono::microseconds::zero());
    }
    if (const std::shared_ptr<render::FrameSlot> frames = framesOf(row); frames != nullptr) {
        row.framesTaken = frames->taken();
        row.framesReplaced = frames->replaced();
        (void)frames->takeArrivalToRenderMs();
    }
}

DeviceManager::Measures DeviceManager::measure(Row& row)
{
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - row.sampledAt);
    row.sampledAt = now;
    Measures measures;
    measures.touchDots = row.latencyResult;
    if (row.controller != nullptr) {
        measures.sample = row.controller->metrics().sample(elapsed);
        measures.audio = row.controller->audioStats();
    } else if (row.demo != nullptr) {
        measures.sample.framesPerSecond = row.demo->framesPerSecond();
    }
    const CaptureFeed* feed = pictureFeedOf(row);
    if (feed != nullptr) {
        measures.sample.framesPerSecond = feed->framesPerSecond();
        measures.audio = feed->audioStats();
    }
    const std::shared_ptr<render::FrameSlot> slot = framesOf(row);
    if (slot == nullptr) {
        return measures;
    }
    render::FrameSlot& frames = *slot;
    const std::uint64_t taken = frames.taken();
    const std::uint64_t replaced = frames.replaced();
    const double seconds = std::chrono::duration<double>(elapsed).count();
    measures.shownPerSecond = seconds > 0 ? static_cast<double>(taken - row.framesTaken) / seconds : 0;
    measures.replacedPerSecond = seconds > 0 ? static_cast<double>(replaced - row.framesReplaced) / seconds : 0;
    row.framesTaken = taken;
    row.framesReplaced = replaced;
    measures.arrivalToRender = frames.takeArrivalToRenderMs();
    if (measures.sample.captureToReceiptMs && measures.arrivalToRender) {
        measures.captureToRender = *measures.sample.captureToReceiptMs + *measures.arrivalToRender;
    }
    if (measures.captureToRender && measures.sample.inputMs) {
        measures.inputToRender =
            *measures.sample.inputMs + measures.sample.frameWaitMs.value_or(0.0) + *measures.captureToRender;
    }
    if (feed != nullptr && measures.arrivalToRender) {
        measures.captureToRender = measures.arrivalToRender;
        measures.inputToRender = measures.arrivalToRender;
    }
    return measures;
}

QStringList DeviceManager::describe(const Measures& measures)
{
    const SessionMetrics::Sample& sample = measures.sample;
    const media::AudioStats& audio = measures.audio;
    const double shownPerSecond = measures.shownPerSecond;
    const double replacedPerSecond = measures.replacedPerSecond;
    const std::optional<double>& arrivalToRender = measures.arrivalToRender;
    const std::optional<double>& captureToRender = measures.captureToRender;

    const auto number = [](double value, int decimals) { return QString::number(value, 'f', decimals); };
    const auto maybe = [&](const std::optional<double>& value) {
        return value ? number(*value, 1) : QStringLiteral("–");
    };
    QStringList lines;
    lines << tr("Video: %1 fps received, %2 shown, %3 replaced before shown")
                 .arg(number(sample.framesPerSecond, 0), number(shownPerSecond, 0), number(replacedPerSecond, 0));
    lines << tr("Bit rate: %1 Mbit/s").arg(number(sample.megabitsPerSecond, 1));
    lines << tr("Decoding: %1 ms per frame").arg(maybe(sample.decodeMs));
    lines << tr("Input to render: %1 ms, the game's own reaction not included").arg(maybe(measures.inputToRender));
    lines << tr("Input reaching the phone: %1 ms; waiting for the next frame: %2 ms")
                 .arg(maybe(sample.inputMs), maybe(sample.frameWaitMs));
    if (measures.touchDots) {
        lines << tr("Measured with touch dots: %1 ms, the median of %2 presses (%3 to %4 ms)")
                     .arg(number(measures.touchDots->medianMs, 0))
                     .arg(measures.touchDots->samples)
                     .arg(number(measures.touchDots->lowMs, 0), number(measures.touchDots->highMs, 0));
    }
    lines << tr("Capture to render: %1 ms").arg(maybe(captureToRender));
    lines << tr("Capture to encoder output: %1 ms").arg(maybe(sample.encodeMs));
    lines << tr("Network: %1 ms (including the phone's send queue)").arg(maybe(sample.networkMs));
    lines << tr("Arrival to render: %1 ms").arg(maybe(arrivalToRender));
    lines << tr("Round trip: %1 ms; input waited at most %2 ms").arg(maybe(sample.roundTripMs), maybe(sample.sendDelayMs));
    if (audio.sampleRate > 0) {
        const auto ms = [&](double frameCount) { return number(frameCount * 1000.0 / audio.sampleRate, 1); };
        lines << tr("Audio: %1 ms buffered, target %2 ms; %3 ms of silence so far")
                     .arg(ms(static_cast<double>(audio.bufferedFrames)), ms(static_cast<double>(audio.targetFrames)),
                          ms(static_cast<double>(audio.underflowFrames)));
        lines << tr("Audio output: %1 period of %2 ms")
                     .arg(audio.lowLatency ? tr("low-latency") : tr("shared"), ms(static_cast<double>(audio.periodFrames)));
    } else {
        lines << tr("Audio: none");
    }
    return lines;
}

void DeviceManager::pressKey(const QString& serial, int keycode)
{
    const int index = indexOf(serial);
    if (index < 0 || keycode < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    const Row* lent = row.aoaOnly ? lentCardOf(row) : nullptr;
    CaptureRow* card = row.capture != nullptr ? row.capture.get() : lent != nullptr ? lent->capture.get() : nullptr;
    if (card != nullptr) {
        if (card->aoa == nullptr || !card->aoa->pressKey(static_cast<std::uint32_t>(keycode))) {
            qInfo("capture card: key %d has no way to the phone", keycode);
            return;
        }
    } else if (row.demo != nullptr) {
        qInfo("demo device: key %d, not sent", keycode);
    } else if (row.aoaOnly) {
        if (row.aoa != nullptr) {
            row.aoa->pressKey(static_cast<std::uint32_t>(keycode));
        }
    } else if (isBoxRow(row)) {
        if (row.esp32 != nullptr) {
            row.esp32->pressKey(static_cast<std::uint32_t>(keycode));
        }
    } else if (row.controller != nullptr) {
        row.controller->pressKey(static_cast<std::uint32_t>(keycode));
    } else {
        return;
    }
    if (row.recordingTap != nullptr) {
        protocol::Key key;
        key.keycode = static_cast<std::uint32_t>(keycode);
        key.action = protocol::KeyAction::Down;
        row.recordingTap->keyed(key);
        key.action = protocol::KeyAction::Up;
        row.recordingTap->keyed(key);
    }
}

void DeviceManager::retryAdb()
{
    if (canRetryAdb() && !m_adbStarting) {
        ensureAdb();
    }
}

void DeviceManager::ensureAdb()
{
    m_adbStarting = true;
    emit setupChanged();
    m_starter = std::jthread([this] {
        const auto version = adb::ensureServer(m_adb, m_adbExecutable);
        const QString detail = version ? QString() : QString::fromStdString(version.error().message);
        QMetaObject::invokeMethod(this, [this, ready = version.has_value(), detail] { serverReady(ready, detail); },
                                  Qt::QueuedConnection);
    });
}

void DeviceManager::retranslate()
{
    setAdbFailure(m_adbFailure);
    describeRendering();
    for (Row& row : m_rows) {
        if (row.refusalProblem.isEmpty()) {
            continue;
        }
        std::optional<Advice> advice;
        if (row.refusalCode != protocol::ErrorCode::None) {
            advice = FailureAdvice::forRefusal(row.refusalCode, row.manufacturer.toStdString());
        } else if (row.silenceStatus) {
            advice = FailureAdvice::forAudioStatus(*row.silenceStatus);
        }
        if (advice) {
            row.refusalProblem = advice->problem;
            row.refusalAction = advice->action;
        }
    }
    for (const auto& [key, window] : m_workbenches) {
        if (window == nullptr) {
            continue;
        }
        if (auto* bench = qobject_cast<ScriptWorkbench*>(window->property("bench").value<QObject*>())) {
            bench->retranslate();
        }
    }
    for (const Row& row : m_rows) {
        if (row.mirror != nullptr && row.mirror->editor() != nullptr) {
            row.mirror->editor()->retranslate();
            if (auto* bench = qobject_cast<ScriptWorkbench*>(row.mirror->editor()->logic())) {
                bench->retranslate();
            }
        }
    }
    if (m_grid != nullptr) {
        m_grid->retranslate();
        refreshGrid();
    }
    emit setupChanged();
    if (!m_rows.empty()) {
        emit dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1));
    }
    emit countsChanged();
    setTrayMenu();
}

void DeviceManager::setAdbFailure(std::optional<session::SessionFailure> failure)
{
    m_adbFailure = std::move(failure);
    if (m_adbFailure) {
        const Advice advice = FailureAdvice::forFailure(*m_adbFailure);
        m_adbProblem = advice.problem;
        m_adbAction = advice.action;
    } else {
        m_adbProblem.clear();
        m_adbAction.clear();
    }
}

void DeviceManager::describeRendering()
{
    if (m_graphics != nullptr && m_graphics->warp()) {
        const Advice advice = FailureAdvice::forSoftwareRendering(QString::fromStdString(m_graphics->description()));
        m_renderNotice = advice.problem;
        m_renderDetail = advice.action;
    }
}

void DeviceManager::serverReady(bool ready, const QString& detail)
{
    m_adbStarting = false;
    m_adbReady = ready;
    if (ready) {
        setAdbFailure(std::nullopt);
        if (!m_tracker) {
            m_tracker = std::make_unique<adb::DeviceTracker>(m_adb.port(), [this](const adb::TrackerUpdate& update) {
                QMetaObject::invokeMethod(this, [this, update] { devicesChanged(update); }, Qt::QueuedConnection);
            });
        }
        const QStringList again = std::exchange(m_wirelessBeforeOutage, {});
        for (const QString& address : again) {
            connectWireless(address, true);
        }
    } else {
        session::SessionFailure failure;
        failure.kind = session::FailureKind::AdbUnavailable;
        failure.detail = detail.toStdString();
        setAdbFailure(failure);
    }
    emit setupChanged();
    if (!m_rows.empty()) {
        emit dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1));
    }
}

void DeviceManager::devicesChanged(const adb::TrackerUpdate& update)
{
    m_lastDevices = update;
    if (!update.serverAvailable && m_adbReady && !m_adbRestarted && !m_adbStarting) {
        m_adbRestarted = true;
        m_wirelessBeforeOutage.clear();
        for (const Row& row : m_rows) {
            if (row.listed && row.state == adb::DeviceState::Device && isWireless(row.serial) &&
                !row.serial.contains(u"._adb-tls-connect._tcp")) {
                m_wirelessBeforeOutage.append(row.serial);
            }
        }
        qWarning("the adb server went away; starting it again and reconnecting %lld wireless phone(s)",
                 static_cast<long long>(m_wirelessBeforeOutage.size()));
        ensureAdb();
    } else if (update.serverAvailable) {
        m_adbRestarted = false;
    }
    if (!update.serverAvailable) {
        m_adbReady = false;
        session::SessionFailure failure;
        failure.kind = session::FailureKind::AdbUnavailable;
        setAdbFailure(failure);
        emit setupChanged();
    } else if (!m_adbReady) {
        m_adbReady = true;
        setAdbFailure(std::nullopt);
        emit setupChanged();
    }

    for (Row& row : m_rows) {
        if (row.demo == nullptr && !row.aoaOnly && row.capture == nullptr && !isBoxRow(row) && !row.transported) {
            row.listed = false;
        }
    }
    for (const adb::DeviceEntry& entry : update.devices) {
        const QString serial = QString::fromStdString(entry.serial);
        QString model = QString::fromStdString(entry.model).replace(u'_', u' ');
        const int index = indexOf(serial);
        if (index >= 0) {
            Row& row = m_rows[static_cast<std::size_t>(index)];
            if (row.demo != nullptr) {
                continue;
            }
            row.listed = true;
            row.state = entry.state;
            if (row.manufacturer.isEmpty() && !model.isEmpty()) {
                row.model = std::move(model);
            }
            continue;
        }
        const int at = static_cast<int>(m_rows.size());
        beginInsertRows({}, at, at);
        Row row;
        row.serial = serial;
        row.model = std::move(model);
        row.state = entry.state;
        row.transport = defaultTransportFor(serial);
        if (const auto known = m_knownNames.find(serial); known != m_knownNames.end()) {
            row.marketName = known->second[0];
            row.manufacturer = known->second[1];
            row.model = known->second[2];
        }
        m_rows.push_back(std::move(row));
        endInsertRows();
    }
    const auto now = std::chrono::steady_clock::now();
    bool graced = false;
    for (Row& row : m_rows) {
        if (row.demo != nullptr || row.aoaOnly || row.capture != nullptr || isBoxRow(row) || row.transported) {
            continue;
        }
        if (row.listed && row.state == adb::DeviceState::Device) {
            if (!row.wasOnline) {
                publishPhoneEvent(AppTopic::DeviceConnected, row.serial, row.name());
            }
            row.wasOnline = true;
            row.graceUntil = {};
            continue;
        }
        if (row.wasOnline && isWireless(row.serial) && update.serverAvailable) {
            if (row.graceUntil == std::chrono::steady_clock::time_point{}) {
                row.graceUntil = now + kOfflineGrace;
            }
            if (now < row.graceUntil) {
                row.listed = true;
                row.state = adb::DeviceState::Device;
                graced = true;
                continue;
            }
        }
        if (row.wasOnline) {
            publishPhoneEvent(AppTopic::DeviceDisconnected, row.serial, row.name());
        }
        row.wasOnline = false;
        row.graceUntil = {};
    }
    if (graced && !m_offlineGrace.isActive()) {
        m_offlineGrace.start();
    }
    for (int i = static_cast<int>(m_rows.size()) - 1; i >= 0; --i) {
        const Row& row = m_rows[static_cast<std::size_t>(i)];
        if (!row.listed && !active(row.session.phase) && row.demo == nullptr && !row.aoaOnly &&
            row.capture == nullptr && !isBoxRow(row) && !row.transported) {
            removeRow(i);
        }
    }
    for (Row& row : m_rows) {
        identify(row);
    }
    nameConnections();
    usbChanged();
    if (!m_rows.empty()) {
        emit dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1));
    }
    emit countsChanged();
}

void DeviceManager::nameConnections()
{
    for (const Row& row : m_rows) {
        if (row.listed && isWireless(row.serial)) {
            m_settings.nameConnection(row.serial, row.name());
        }
    }
}

void DeviceManager::identified(const QString& serial, const std::optional<adb::DeviceInfo>& info)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    row.identifying = false;
    if (!info) {
        return;
    }
    row.androidRelease = QString::fromStdString(info->androidRelease);
    row.marketName = QString::fromStdString(info->marketName);
    if (!info->model.empty()) {
        row.manufacturer = QString::fromStdString(info->manufacturer);
        row.model = QString::fromStdString(info->model);
    }
    m_knownNames[serial] = {row.marketName, row.manufacturer, row.model};
    if (!info->id.empty()) {
        row.deviceId = QString::fromStdString(info->id);
        const std::string& key = info->id;
        settings::DeviceSettings device = m_settings.device(key);
        if (row.transportChosen) {
            device.transport = static_cast<settings::Transport>(row.transport);
            m_settings.setDevice(key, device);
        } else if (device.transport != settings::Transport::Default) {
            row.transport = static_cast<session::TransportChoice>(device.transport);
        }
        if (row.mirror != nullptr) {
            row.mirror->setSharpness(m_settings.effective(key).display.sharpness / 100.0);
            applyFilters(row);
            if (row.profileId.isEmpty()) {
                restoreProfile(row);
                applyProfile(row);
                syncInputProfile();
                applyKeymap();
            }
            followApp(row);
            offerStart(row);
        }
    }
    if (row.logic != nullptr) {
        row.logic->setDevice(deviceInfoOf(row));
        row.logic->setDeviceKey(logicKeyOf(row));
    }
    if (m_extensionLogic != nullptr) {
        m_extensionLogic->updateSession(row.serial, deviceInfoOf(row), logicKeyOf(row));
    }
    syncCompanion(row);
    nameConnections();
    rowChanged(index);
    if (row.mirror != nullptr) {
        emit mirrorsChanged();
    }
    usbChanged();
}

void DeviceManager::identify(Row& row)
{
    if (row.demo == nullptr && row.listed && row.state == adb::DeviceState::Device && row.deviceId.isEmpty() &&
        !row.identifying) {
        row.identifying = true;
        m_identifier->identify(row.serial.toStdString());
    }
}

void DeviceManager::sessionChanged(const QString& serial, const session::SessionState& state)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    const bool wasStreaming = row.session.phase == session::SessionPhase::Streaming;
    const bool wasReconnecting = row.session.phase == session::SessionPhase::Reconnecting;
    row.session = state;
    const bool streaming = state.phase == session::SessionPhase::Streaming;
    const bool reconnecting = state.phase == session::SessionPhase::Reconnecting;
    if ((wasStreaming && reconnecting) || (wasReconnecting && streaming)) {
        if (row.logic != nullptr) {
            row.logic->setConnected(streaming);
        }
        for (const std::unique_ptr<TaskRun>& run : m_taskRuns) {
            if (run->serial == row.serial && run->runtime != nullptr) {
                run->runtime->setConnected(streaming);
            }
        }
        forSessionScripts(row.serial, [streaming](ScriptRuntime& runtime) { runtime.setConnected(streaming); });
    }
    if ((wasStreaming || wasReconnecting) && !active(state.phase)) {
        publishPhoneEvent(AppTopic::SessionEnded, row.serial, row.name(),
                          {{QStringLiteral("failed"), state.phase == session::SessionPhase::Failed}});
    }
    if (streaming && !wasStreaming && row.controller != nullptr) {
        restartMeasures(row);
        identify(row);
        row.companionRetried = false;
    } else if (!streaming) {
        row.stats.clear();
        row.thumbnail = {};
    }
    if (!active(state.phase)) {
        row.companionHeld = false;
        row.companionRetried = false;
    }
    if (!active(state.phase) && m_esp32 != nullptr) {
        m_esp32->release(serial);
    }
    if (state.phase != session::SessionPhase::Streaming) {
        row.refusalProblem.clear();
        row.refusalAction.clear();
        row.refusalCode = protocol::ErrorCode::None;
    }
    if (!active(state.phase)) {
        stopRecording(row, false);
    }
    if (state.phase == session::SessionPhase::Streaming && row.mirror == nullptr) {
        openMirror(row);
    } else if (!active(state.phase) && row.mirror != nullptr) {
        releaseInput(serial);
        row.mirror->deleteLater();
        row.mirror = nullptr;
        emit mirrorsChanged();
    }
    if (!active(state.phase)) {
        returnCapture(row);
        row.picture.reset();
    }
    if (state.phase == session::SessionPhase::Failed) {
        row.pictureNotice.clear();
    }
    updateStatsTimer();
    syncCompanion(row);
    syncPhoneStatus(row);
    if (streaming && !wasStreaming && !wasReconnecting) {
        publishPhoneEvent(AppTopic::SessionStarted, row.serial, row.name());
    }
    if (state.phase == session::SessionPhase::Stopped && !row.listed) {
        removeRow(index);
        emit countsChanged();
        return;
    }
    rowChanged(index);
    if (!active(state.phase) && std::exchange(row.reconnectWhenStopped, false)) {
        QMetaObject::invokeMethod(this, [this, serial] { connectDevice(serial); }, Qt::QueuedConnection);
    }
}

void DeviceManager::openMirror(Row& row)
{
    std::shared_ptr<render::FrameSlot> frames = framesOf(row);
    if (row.mirror != nullptr || m_graphics == nullptr || frames == nullptr) {
        return;
    }
    const settings::Display display = m_settings.effective(row.deviceId.toStdString()).display;
    qInfo("mirror window for %s: low-latency mode %s, sharpening %d%%", qUtf8Printable(row.name()),
          display.lowLatency ? "on, presenting without vertical sync" : "off", static_cast<int>(display.sharpness));
    const QString serial = row.serial;
    row.mirror = new MirrorWindow(*m_graphics, std::move(frames), serial, row.name(), display.lowLatency);
    releaseMemoryAfter(*row.mirror);
    row.mirror->setSharpness(display.sharpness / 100.0);
    row.mirror->setTitleBarFixed(display.titleBar == settings::TitleBarMode::Fixed);
    row.mirror->setToolbarPinned(display.toolbar == settings::ToolbarMode::Pinned);
    row.filters.clear();
    applyFilters(row);
    if (QQmlEngine* engine = m_engine.data(); engine != nullptr) {
        row.mirror->attachOverlay(*engine);
    }
    connect(row.mirror, &MirrorWindow::closedByUser, this, [this, serial] { mirrorClosed(serial); });
    connect(row.mirror, &MirrorWindow::filterStatusChanged, this, [this, serial] { checkBudget(serial); });
    connect(row.mirror, &MirrorWindow::fullScreenChanged, this, [this, serial] {
        const int at = indexOf(serial);
        if (at >= 0) {
            rowChanged(at);
        }
    });
    connect(row.mirror, &MirrorWindow::pictureAreaChanged, this, [this, serial](const QRectF& share) {
        if (m_grid == nullptr || !m_grid->holds(serial)) {
            m_input->setPictureArea(serial, share);
        }
    });
    connect(row.mirror, &MirrorWindow::cursorTouched, this,
            [this, serial](input::CursorPhase phase, input::Vec2 position) {
                const int at = indexOf(serial);
                if (at >= 0 && m_rows[static_cast<std::size_t>(at)].mirror != nullptr) {
                    const Row& target = m_rows[static_cast<std::size_t>(at)];
                    m_input->cursor(serial, channelOf(target), static_cast<std::uintptr_t>(target.mirror->winId()),
                                    phase, position);
                }
            });
    connect(row.mirror, &MirrorWindow::cursorScrolled, this,
            [this, serial](input::Vec2 position, int horizontal, int vertical) {
                sendScroll(serial, position, horizontal, vertical);
            });
    connect(row.mirror, &MirrorWindow::cursorKey, this,
            [this, serial](int keycode, int metaState, bool down, int repeat) {
                const int at = indexOf(serial);
                if (at >= 0 && isPlainEnter(keycode, static_cast<std::uint32_t>(metaState))) {
                    Row& target = m_rows[static_cast<std::size_t>(at)];
                    if (down && target.companion != nullptr && target.inputBox.commits()) {
                        if (repeat == 0) {
                            target.companion->editorAction(protocol::ImeAction::Unspecified);
                        }
                        target.enterToCompanion = true;
                        return;
                    }
                    if (!down && std::exchange(target.enterToCompanion, false)) {
                        return;
                    }
                }
                sendKey(serial, keycode, metaState, down, repeat);
            });
    connect(row.mirror, &MirrorWindow::textInputFinished, this, [this, serial] {
        if (const int at = indexOf(serial); at >= 0) {
            m_rows[static_cast<std::size_t>(at)].inputBox.left();
        }
        m_input->setTextInput(false);
    });
    connect(row.mirror, &MirrorWindow::playingChanged, this, &DeviceManager::foregroundChanged);
    connect(row.mirror, &MirrorWindow::phoneCursorChanged, this, [this, serial](const protocol::CursorState& cursor) {
        if (const int at = indexOf(serial); at >= 0) {
            phoneCursorChanged(m_rows[static_cast<std::size_t>(at)], cursor);
        }
    });
    connect(row.mirror, &MirrorWindow::phoneHintsChanged, this, [this, serial] {
        if (const int at = indexOf(serial); at >= 0) {
            pushPhoneHints(m_rows[static_cast<std::size_t>(at)]);
        }
    });
    connect(row.mirror, &MirrorWindow::phoneHintStateChanged, this, [this, serial] {
        if (const int at = indexOf(serial); at >= 0) {
            pushPhoneHints(m_rows[static_cast<std::size_t>(at)]);
        }
    });
    connect(row.mirror, &MirrorWindow::cursorTyped, this, [this, serial](const QString& text) {
        const int at = indexOf(serial);
        if (at < 0) {
            return;
        }
        Row& target = m_rows[static_cast<std::size_t>(at)];
        if (target.companion != nullptr && target.inputBox.commits()) {
            target.companion->commitText(text.toStdString());
            return;
        }
        sendText(serial, text);
    });
    if (row.aoaOnly && row.aoa != nullptr && noPictureOf(row)) {
        row.mirror->setNoPicture(row.aoa->canvas(), controlTurnOf(row));
        connect(row.mirror, &MirrorWindow::mouseMoved, this, [this, serial](int dx, int dy) {
            const int at = indexOf(serial);
            if (at >= 0 && m_rows[static_cast<std::size_t>(at)].aoa != nullptr) {
                m_rows[static_cast<std::size_t>(at)].aoa->moveMouse(dx, dy);
            }
        });
        connect(row.mirror, &MirrorWindow::mouseButtonsChanged, this, [this, serial](int buttons) {
            const int at = indexOf(serial);
            if (at >= 0 && m_rows[static_cast<std::size_t>(at)].aoa != nullptr) {
                m_rows[static_cast<std::size_t>(at)].aoa->setMouseButtons(buttons);
            }
        });
    }
    if (row.esp32 != nullptr) {
        row.mirror->setNoPicture(row.esp32->canvas(), controlTurnOf(row));
        connect(row.mirror, &MirrorWindow::mouseMoved, this, [this, serial](int dx, int dy) {
            const int at = indexOf(serial);
            if (at >= 0 && m_rows[static_cast<std::size_t>(at)].esp32 != nullptr) {
                m_rows[static_cast<std::size_t>(at)].esp32->moveMouse(dx, dy);
            }
        });
        connect(row.mirror, &MirrorWindow::mouseButtonsChanged, this, [this, serial](int buttons) {
            const int at = indexOf(serial);
            if (at >= 0 && m_rows[static_cast<std::size_t>(at)].esp32 != nullptr) {
                m_rows[static_cast<std::size_t>(at)].esp32->setMouseButtons(buttons);
            }
        });
    }
    restoreProfile(row);
    row.mirror->setForegroundApp(row.foregroundApp);
    applyProfile(row);
    followApp(row);
    offerStart(row);
    row.mirror->setKeymap(keymapOf(row));
    row.mirror->setCameraLocked(cameraLockedFor(serial));
    row.mirror->setStatus(-1.0, -1.0, linkName(row), {});
    row.mirror->setVolume(row.volume);
    showLatencyTest(row);
    if (m_recordings != nullptr && m_recordings->running(serial)) {
        row.mirror->setRecording(m_recordings->state(serial));
    }
    if (row.capture != nullptr) {
        applyCaptureGeometry(row);
    }
    if (!row.pictureCard.isEmpty()) {
        applyPictureGeometry(row);
    }
    if (row.picture == settings::PictureSource::None && (row.controller != nullptr || row.demo != nullptr)) {
        showNoPicture(row);
    }
    if (row.picture == settings::PictureSource::None && row.controller != nullptr) {
        connect(row.mirror, &MirrorWindow::mouseMoved, this, [this, serial](int dx, int dy) {
            const int at = indexOf(serial);
            if (at >= 0 && m_rows[static_cast<std::size_t>(at)].controller != nullptr) {
                m_rows[static_cast<std::size_t>(at)].controller->moveMouse(dx, dy);
            }
        });
        connect(row.mirror, &MirrorWindow::mouseButtonsChanged, this, [this, serial](int buttons) {
            const int at = indexOf(serial);
            if (at >= 0 && m_rows[static_cast<std::size_t>(at)].controller != nullptr) {
                m_rows[static_cast<std::size_t>(at)].controller->setMouseButtons(buttons);
            }
        });
    }
    if (!row.pictureNotice.isEmpty()) {
        row.mirror->showToast(std::exchange(row.pictureNotice, {}), QStringLiteral("info"));
    }
    if (!placeInGrid(row)) {
        row.mirror->show();
        stepAside(*row.mirror);
    }
    foregroundChanged();
    syncCompanion(row);
    syncPhoneStatus(row);
    emit mirrorsChanged();
}

void DeviceManager::stepAside(MirrorWindow& window) const
{
    const auto taken = [this, &window](QPoint corner) {
        return std::ranges::any_of(m_rows, [&window, corner](const Row& other) {
            return other.mirror != nullptr && other.mirror != &window && other.mirror->isVisible() &&
                   other.mirror->framePosition() == corner;
        });
    };
    QPoint corner = window.framePosition();
    const QRect area = window.screen() != nullptr ? window.screen()->availableGeometry() : QRect();
    const QSize frame = window.frameGeometry().size();
    for (int step = 0; step < kMostSteps && taken(corner); ++step) {
        const QPoint next = corner + QPoint(kStepAside, kStepAside);
        if (!area.isEmpty() &&
            (next.x() + frame.width() > area.right() + 1 || next.y() + frame.height() > area.bottom() + 1)) {
            break;
        }
        corner = next;
    }
    if (corner != window.framePosition()) {
        window.setFramePosition(corner);
    }
}

void DeviceManager::showGrid()
{
    if (m_grid != nullptr) {
        m_grid->setWindowStates(m_grid->windowStates() & ~Qt::WindowMinimized);
        m_grid->show();
        m_grid->raise();
        m_grid->requestActivate();
        return;
    }
    if (m_graphics == nullptr) {
        return;
    }
    auto* grid = new GridWindow(*m_graphics);
    m_grid = grid;
    releaseMemoryAfter(*grid);
    if (QQmlEngine* engine = m_engine.data(); engine != nullptr) {
        grid->attachOverlay(*engine);
    }
    grid->setHintItems([](const input::Profile& profile, QSizeF picture) {
        return keyHintItems(profile, picture, defaultLockKey());
    });
    connect(grid, &GridWindow::closedByUser, this, &DeviceManager::closeGrid);
    connect(grid, &GridWindow::targetChosen, this, [this] { foregroundChanged(); });
    connect(grid, &GridWindow::ownWindowWanted, this,
            [this](const QString& serial) { setGridSeparate(serial, true, true); });
    connect(grid, &GridWindow::backToGridWanted, this,
            [this](const QString& serial) { setGridSeparate(serial, false, true); });
    connect(grid, &GridWindow::picturesChanged, this, [this] {
        if (m_grid == nullptr) {
            return;
        }
        for (const QString& serial : m_grid->serials()) {
            if (m_grid->holds(serial)) {
                m_input->setPictureArea(serial, m_grid->pictureShare(serial));
            }
        }
    });
    connect(grid, &GridWindow::cursorTouched, this,
            [this](const QString& serial, input::CursorPhase phase, input::Vec2 position) {
                const int at = indexOf(serial);
                if (at >= 0 && m_grid != nullptr) {
                    m_input->cursor(serial, channelOf(m_rows[static_cast<std::size_t>(at)]),
                                    static_cast<std::uintptr_t>(m_grid->winId()), phase, position);
                }
            });
    connect(grid, &GridWindow::cursorScrolled, this, &DeviceManager::sendScroll);
    connect(grid, &GridWindow::cursorKey, this, &DeviceManager::sendKey);
    connect(grid, &GridWindow::cursorTyped, this, &DeviceManager::sendText);
    connect(grid, &GridWindow::textInputFinished, this, [this] { m_input->setTextInput(false); });
    for (Row& row : m_rows) {
        placeInGrid(row);
    }
    refreshGrid();
    qInfo("grid window: %lld phone(s)", static_cast<long long>(grid->serials().size()));
    grid->show();
    grid->raise();
    grid->requestActivate();
    foregroundChanged();
}

void DeviceManager::tileMirrors()
{
    std::vector<MirrorWindow*> windows;
    std::map<QScreen*, int> screens;
    for (const Row& row : m_rows) {
        MirrorWindow* mirror = row.mirror;
        if (mirror != nullptr && mirror->isVisible() && !mirror->mini() && !mirror->editing() &&
            !mirror->fullScreen()) {
            windows.push_back(mirror);
            ++screens[mirror->screen()];
        }
    }
    if (windows.empty()) {
        return;
    }
    QScreen* screen = std::ranges::max_element(screens, {}, [](const auto& entry) { return entry.second; })->first;
    if (screen == nullptr) {
        return;
    }
    std::vector<GridItem> items;
    items.reserve(windows.size());
    for (const MirrorWindow* window : windows) {
        const QSize picture = window->pictureSize();
        const QSize chrome = window->pictureChrome();
        const QMargins frame = window->frameMargins();
        double aspect = 0.0;
        if (!picture.isEmpty()) {
            aspect = static_cast<double>(picture.width()) / picture.height();
        } else if (window->width() > chrome.width() && window->height() > chrome.height()) {
            aspect = static_cast<double>(window->width() - chrome.width()) / (window->height() - chrome.height());
        }
        items.push_back({.aspect = aspect,
                         .chrome = QSizeF(chrome.width() + frame.left() + frame.right(),
                                          chrome.height() + frame.top() + frame.bottom())});
    }
    const std::vector<QRect> rects = tileWindows(screen->availableGeometry(), items, kTileGap);
    for (std::size_t i = 0; i < windows.size() && i < rects.size(); ++i) {
        if (rects[i].isEmpty()) {
            continue;
        }
        MirrorWindow* window = windows[i];
        if (window->windowStates().testAnyFlags(Qt::WindowMinimized | Qt::WindowMaximized)) {
            window->showNormal();
        }
        window->setGeometry(rects[i].marginsRemoved(window->frameMargins()));
        window->raise();
    }
    qInfo("tiled %zu mirror window(s)", windows.size());
}

bool DeviceManager::gridEligible(const Row& row)
{
    return row.mirror != nullptr && row.host.isEmpty() && !noPictureOf(row) && framesOf(row) != nullptr;
}

bool DeviceManager::placeInGrid(Row& row)
{
    if (m_grid == nullptr || !gridEligible(row) || m_grid->contains(row.serial)) {
        return false;
    }
    const QSize picture = row.mirror->pictureSize();
    m_grid->addCell(row.serial, row.name(),
                    picture.isEmpty() ? 0.0 : static_cast<double>(picture.width()) / picture.height());
    cropGridCell(row);
    row.mirror->setKeptOnClose(true);
    if (row.mirror->editing() || row.mirror->mini()) {
        return false;
    }
    row.mirror->hide();
    row.mirror->lendVideo(true);
    m_grid->setFrames(row.serial, framesOf(row));
    m_input->setPictureArea(row.serial, m_grid->pictureShare(row.serial));
    return true;
}

void DeviceManager::syncGrid()
{
    if (m_grid == nullptr) {
        return;
    }
    for (const QString& serial : m_grid->serials()) {
        const int index = indexOf(serial);
        if (index >= 0 && gridEligible(m_rows[static_cast<std::size_t>(index)])) {
            continue;
        }
        const bool held = m_grid->holds(serial);
        m_grid->removeCell(serial);
        if (MirrorWindow* mirror = index >= 0 ? m_rows[static_cast<std::size_t>(index)].mirror : nullptr;
            mirror != nullptr) {
            mirror->setKeptOnClose(false);
            if (held) {
                mirror->lendVideo(false);
                mirror->show();
                stepAside(*mirror);
            }
        }
    }
    for (Row& row : m_rows) {
        placeInGrid(row);
    }
    refreshGrid();
}

void DeviceManager::refreshGrid()
{
    if (m_grid == nullptr) {
        return;
    }
    for (const QString& serial : m_grid->serials()) {
        const int index = indexOf(serial);
        if (index < 0) {
            continue;
        }
        const Row& row = m_rows[static_cast<std::size_t>(index)];
        const StatusProblem problem = row.session.phase == session::SessionPhase::Reconnecting
                                          ? StatusProblem::Reconnecting
                                          : row.problems.shown();
        m_grid->setLabel(serial, row.name(),
                         problem == StatusProblem::None ? QStringLiteral("ok") : QStringLiteral("warn"),
                         problemText(problem));
        m_grid->setRate(serial, streaming(row) ? row.stats.value(QStringLiteral("fps"), -1.0).toDouble() : -1.0);
        GridCells::Sync sync;
        if (const SyncGroup* group = m_sync.groupOf(serial); group != nullptr) {
            const bool leads = group->master == serial;
            const int leader = leads ? -1 : indexOf(group->master);
            const QString leaderName = leader >= 0 ? m_rows[static_cast<std::size_t>(leader)].name() : QString();
            sync = GridCells::Sync{.role = leads ? QStringLiteral("master") : QStringLiteral("follower"),
                                   .tone = group->tone,
                                   .followers = static_cast<int>(group->followers.size()),
                                   .leader = leaderName};
        }
        m_grid->setSync(serial, sync);
    }
}

void DeviceManager::closeGrid()
{
    GridWindow* grid = m_grid.data();
    if (grid == nullptr) {
        return;
    }
    m_grid = nullptr;
    disconnect(grid, nullptr, this, nullptr);
    const bool aimed = !grid->target().isEmpty() && m_input->target() == grid->target();
    for (const QString& serial : grid->serials()) {
        const bool held = grid->holds(serial);
        grid->removeCell(serial);
        const int index = indexOf(serial);
        MirrorWindow* mirror = index >= 0 ? m_rows[static_cast<std::size_t>(index)].mirror : nullptr;
        if (mirror == nullptr) {
            continue;
        }
        mirror->setKeptOnClose(false);
        if (held) {
            mirror->lendVideo(false);
            m_input->setPictureArea(serial, mirror->pictureShare());
            mirror->show();
            stepAside(*mirror);
        }
    }
    if (aimed) {
        m_input->setTarget({}, nullptr, 0);
    }
    grid->deleteLater();
    qInfo("grid window closed");
    foregroundChanged();
}

void DeviceManager::setGridSeparate(const QString& serial, bool separate, bool activate)
{
    const int index = indexOf(serial);
    if (m_grid == nullptr || index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    MirrorWindow* mirror = row.mirror;
    if (mirror == nullptr) {
        return;
    }
    if (separate) {
        if (!m_grid->holds(serial)) {
            return;
        }
        m_grid->setFrames(serial, nullptr);
        mirror->lendVideo(false);
        m_input->setPictureArea(serial, mirror->pictureShare());
        mirror->setWindowStates(mirror->windowStates() & ~Qt::WindowMinimized);
        mirror->show();
        stepAside(*mirror);
        mirror->raise();
        if (activate) {
            mirror->requestActivate();
        }
    } else {
        if (!m_grid->separate(serial)) {
            return;
        }
        if (mirror->editing()) {
            mirror->raise();
            if (activate) {
                mirror->requestActivate();
            }
            return;
        }
        mirror->hide();
        mirror->lendVideo(true);
        m_grid->setFrames(serial, framesOf(row));
        m_input->setPictureArea(serial, m_grid->pictureShare(serial));
        if (activate) {
            m_grid->setTarget(serial);
            m_grid->raise();
            m_grid->requestActivate();
        }
    }
    qInfo("%s %s", qUtf8Printable(row.name()), separate ? "left the grid for its own window" : "is back in the grid");
    foregroundChanged();
}

bool DeviceManager::showFromGrid(const QString& serial, bool activate)
{
    if (m_grid == nullptr || !m_grid->holds(serial)) {
        return false;
    }
    setGridSeparate(serial, true, activate);
    return true;
}

QString DeviceManager::gridFront() const
{
    if (m_grid == nullptr || !m_grid->holds(m_grid->target()) ||
        !platform::isInFront(static_cast<std::uintptr_t>(m_grid->winId()))) {
        return {};
    }
    return m_grid->target();
}

bool DeviceManager::aimAtGrid(const QString& serial)
{
    const int index = indexOf(serial);
    if (m_grid == nullptr || serial.isEmpty() || index < 0) {
        return false;
    }
    const Row& row = m_rows[static_cast<std::size_t>(index)];
    m_input->setPictureArea(serial, m_grid->pictureShare(serial));
    m_input->setTarget(serial, channelOf(row), static_cast<std::uintptr_t>(m_grid->winId()));
    syncInputProfile();
    m_input->setFocused(!row.prompting);
    applyKeymap();
    return true;
}

void DeviceManager::sendScroll(const QString& serial, input::Vec2 position, int horizontal, int vertical)
{
    const int at = indexOf(serial);
    const std::shared_ptr<ControlChannel> channel = at >= 0 ? channelOf(m_rows[static_cast<std::size_t>(at)]) : nullptr;
    if (channel != nullptr) {
        static_cast<void>(
            channel->scroll(position, static_cast<std::int16_t>(horizontal), static_cast<std::int16_t>(vertical)));
        forwardScroll(serial, position, horizontal, vertical);
    }
}

void DeviceManager::sendKey(const QString& serial, int keycode, int metaState, bool down, int repeat)
{
    const int at = indexOf(serial);
    const std::shared_ptr<ControlChannel> channel = at >= 0 ? channelOf(m_rows[static_cast<std::size_t>(at)]) : nullptr;
    if (channel != nullptr) {
        protocol::Key key;
        key.action = down ? protocol::KeyAction::Down : protocol::KeyAction::Up;
        key.keycode = static_cast<std::uint32_t>(keycode);
        key.metaState = static_cast<std::uint32_t>(metaState);
        key.repeat = static_cast<std::uint16_t>(std::clamp(repeat, 0, 0xFFFF));
        static_cast<void>(channel->send(key));
    }
}

void DeviceManager::sendText(const QString& serial, const QString& text)
{
    const int at = indexOf(serial);
    const std::shared_ptr<ControlChannel> channel = at >= 0 ? channelOf(m_rows[static_cast<std::size_t>(at)]) : nullptr;
    if (channel != nullptr) {
        static_cast<void>(channel->type(text.toStdString()));
    }
}

void DeviceManager::showLatencyTest(Row& row)
{
    QVariantMap state;
    const std::optional<LatencyProgress> progress =
        row.controller != nullptr ? row.controller->latencyTest() : std::nullopt;
    if (row.latencyCard && progress) {
        state.insert(QStringLiteral("running"), progress->running);
        state.insert(QStringLiteral("measured"), progress->measured);
        state.insert(QStringLiteral("target"), progress->target);
        state.insert(QStringLiteral("missed"), progress->missed);
        state.insert(QStringLiteral("refused"), progress->touchesRefused);
        if (progress->result) {
            state.insert(QStringLiteral("samples"), progress->result->samples);
            state.insert(QStringLiteral("medianMs"), progress->result->medianMs);
            state.insert(QStringLiteral("lowMs"), progress->result->lowMs);
            state.insert(QStringLiteral("highMs"), progress->result->highMs);
        }
    }
    if (row.mirror != nullptr) {
        row.mirror->setLatencyTest(state);
    }
}

void DeviceManager::setMeasuringLatency(const QString& serial, bool on)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.controller == nullptr) {
        return;
    }
    if (on) {
        row.latencyCard = row.controller->startLatencyTest() || row.latencyCard;
    } else {
        row.controller->stopLatencyTest();
    }
    showLatencyTest(row);
    rowChanged(index);
}

void DeviceManager::dismissLatencyTest(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    row.latencyCard = false;
    if (row.controller != nullptr) {
        row.controller->stopLatencyTest();
    }
    showLatencyTest(row);
    rowChanged(index);
}

void DeviceManager::setRecording(const QString& serial, bool on)
{
    const int index = indexOf(serial);
    if (index < 0 || m_recordings == nullptr) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (!on) {
        stopRecording(row, true);
        return;
    }
    if (m_recordings->running(serial) || !streaming(row) || noPictureOf(row)) {
        return;
    }
    if (const QString problem = startRecording(row); !problem.isEmpty()) {
        qWarning("recording of %s did not start: %s", qUtf8Printable(serial), qUtf8Printable(problem));
        if (row.mirror != nullptr) {
            row.mirror->showToast(problem, QStringLiteral("triangle-alert"));
        }
    }
}

std::shared_ptr<RecordingTap> DeviceManager::tapOf(Row& row)
{
    if (row.recordingTap == nullptr) {
        row.recordingTap = std::make_shared<RecordingTap>();
    }
    return row.recordingTap;
}

QString DeviceManager::startRecording(Row& row)
{
    const QString serial = row.serial;
    RecordingStart start;
    start.serial = serial;
    start.phone = row.name();
    start.profileId = m_override == nullptr ? row.profileId : QString();
    start.profileName = start.profileId.isEmpty() ? QString() : profileName(start.profileId);
    start.sound = row.controller != nullptr && !row.silent && !row.controller->showsAppDisplay() &&
                  m_settings.effective(row.deviceId.toStdString()).audio.enabled;
    CaptureFeed* feed = pictureFeedOf(row);
    if (row.capture != nullptr || feed != nullptr) {
        start.sound = feed != nullptr && feed->sound();
    }
    start.device = deviceInfoOf(row);
    start.frameRate = row.refreshHz > 0.0 ? row.refreshHz : 60.0;
    start.thumbnail = [this, serial] { return recordingThumbnail(serial); };
    auto recorder = m_recordings->start(std::move(start));
    if (!recorder) {
        return recorder.error();
    }
    const std::shared_ptr<RecordingTap> tap = tapOf(row);
    tap->setRecorder(*recorder);
    if (const std::shared_ptr<ControlChannel> channel = channelOf(row)) {
        channel->setOperationObserver(tap);
    }
    if (feed != nullptr) {
        feed->setRecorder(*recorder);
    } else if (row.controller != nullptr) {
        row.controller->setRecorder(*recorder);
    } else if (row.demo != nullptr) {
        row.demo->setRecorder(*recorder);
    }
    if (row.mirror != nullptr) {
        row.mirror->setRecording(m_recordings->state(serial));
    }
    rowChanged(indexOf(serial));
    return {};
}

QVariantMap DeviceManager::stopRecording(Row& row, bool inMirror)
{
    if (m_recordings == nullptr || !m_recordings->running(row.serial)) {
        return {};
    }
    if (row.controller != nullptr) {
        row.controller->setRecorder(nullptr);
    }
    if (row.demo != nullptr) {
        row.demo->setRecorder(nullptr);
    }
    if (CaptureFeed* feed = pictureFeedOf(row); feed != nullptr) {
        feed->setRecorder(nullptr);
    }
    if (const std::shared_ptr<ControlChannel> channel = channelOf(row)) {
        channel->setOperationObserver(nullptr);
    }
    if (row.recordingTap != nullptr) {
        row.recordingTap->setRecorder(nullptr);
    }
    const QVariantMap info = m_recordings->stop(row.serial);
    if (row.mirror != nullptr) {
        row.mirror->setRecording({});
    }
    if (inMirror && row.mirror != nullptr) {
        row.mirror->showRecording(info);
    } else {
        emit recordingFinished(info);
    }
    rowChanged(indexOf(row.serial));
    return info;
}

ProfileEditor* DeviceManager::editorOf(const QString& serial) const
{
    const MirrorWindow* mirror = mirrorOf(serial);
    return mirror != nullptr ? mirror->editor() : nullptr;
}

QVariantMap DeviceManager::endRecording(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return {};
    }
    return stopRecording(m_rows[static_cast<std::size_t>(index)], true);
}

QImage DeviceManager::recordingThumbnail(const QString& serial) const
{
    const int index = indexOf(serial);
    if (index < 0) {
        return {};
    }
    const Row& row = m_rows[static_cast<std::size_t>(index)];
    if (const CaptureFeed* feed = pictureFeedOf(row); feed != nullptr) {
        const std::shared_ptr<const media::LumaSnapshot> shot = feed->snapshots()->capture(true);
        return shot != nullptr ? snapshotThumbnail(*shot, kRecordingThumbnail) : QImage();
    }
    if (row.demo != nullptr) {
        const std::shared_ptr<const media::LumaSnapshot> shot = row.demo->snapshots()->capture(true);
        return shot != nullptr ? snapshotThumbnail(*shot, kRecordingThumbnail) : QImage();
    }
    return row.thumbnail;
}

void DeviceManager::recordingExported(const QString& serial, bool ok, const QString& text, const QString& shown)
{
    const int index = indexOf(serial);
    MirrorWindow* mirror = index >= 0 ? m_rows[static_cast<std::size_t>(index)].mirror : nullptr;
    if (mirror == nullptr) {
        emit recordingNotice(text, ok ? QStringLiteral("ok") : QStringLiteral("bad"));
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (ok && !shown.isEmpty()) {
        row.toastAction = [shown] {
            if (const Recordings* recordings = Recordings::instance()) {
                recordings->showInFolder(shown);
            }
        };
        row.toastSecondAction = nullptr;
        mirror->showToast(text, QStringLiteral("check"), tr("Show in folder"));
        return;
    }
    mirror->showToast(text, ok ? QStringLiteral("check") : QStringLiteral("triangle-alert"));
}

int DeviceManager::listRecordings(const QString& serial)
{
    if (m_recordings == nullptr) {
        return 0;
    }
    const int index = indexOf(serial);
    const QString phone = index >= 0 ? m_rows[static_cast<std::size_t>(index)].name() : QString();
    return m_recordings->listRecordings(serial, phone);
}

QString DeviceManager::testOnRecording(const QString& serial, const QString& profileId, const QString& recording)
{
    ScriptWorkbench* bench = nullptr;
    const int index = indexOf(serial);
    const Row* row = index >= 0 ? &m_rows[static_cast<std::size_t>(index)] : nullptr;
    if (profileId.isEmpty()) {
        if (m_override == nullptr || m_overrideFolder.empty()) {
            return tr("No profile was in use while recording, so there is no logic to run on it");
        }
        const QString name = QString::fromStdString(m_override->meta.name);
        bench = openWorkbench(QString::fromStdWString(m_overrideFolder.wstring()),
                              name.isEmpty() ? QString::fromStdWString(m_overrideFolder.filename().wstring()) : name);
    } else if (row != nullptr && row->mirror != nullptr && row->profileId == profileId && m_override == nullptr &&
               showLogicIn(serial, profileId)) {
        showWindow(serial);
        const Row& shown = m_rows[static_cast<std::size_t>(indexOf(serial))];
        bench = shown.mirror != nullptr && shown.mirror->editor() != nullptr
                    ? qobject_cast<ScriptWorkbench*>(shown.mirror->editor()->logic())
                    : nullptr;
    } else {
        const ProfileEntry* entry = m_library != nullptr ? m_library->find(profileId) : nullptr;
        if (entry == nullptr) {
            return tr("This profile is no longer there");
        }
        const QString name = entry->name;
        if (entry->readOnly()) {
            return tr("%1 comes with an extension and cannot have its own logic. Copy it to your profiles first")
                .arg(name);
        }
        const QString folder = m_library->ensureFolder(profileId);
        if (folder.isEmpty()) {
            return m_library->lastWriteBusy() ? busyText() : tr("The logic of %1 cannot be opened").arg(name);
        }
        bench = openWorkbench(folder, name);
    }
    if (bench == nullptr) {
        return tr("The logic could not be opened");
    }
    bench->runOnRecording(QUrl::fromLocalFile(QDir::fromNativeSeparators(recording)));
    return {};
}

void DeviceManager::aimAt(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    const Row& row = m_rows[static_cast<std::size_t>(index)];
    std::uintptr_t window = row.mirror != nullptr ? static_cast<std::uintptr_t>(row.mirror->winId()) : 0;
    if (m_grid != nullptr && m_grid->holds(serial)) {
        window = static_cast<std::uintptr_t>(m_grid->winId());
        m_input->setPictureArea(serial, m_grid->pictureShare(serial));
    }
    m_input->setTarget(serial, channelOf(row), window);
    syncInputProfile();
    m_input->setFocused(platform::isInFront(window));
}

void DeviceManager::setKeymap(const QString& serial, bool on)
{
    aimAt(serial);
    m_input->setKeymap(on);
}

void DeviceManager::setCameraLocked(const QString& serial, bool locked)
{
    aimAt(serial);
    m_input->setCameraLock(locked ? input::Switch::On : input::Switch::Off);
}

bool DeviceManager::keymapOf(const Row& row) const
{
    return m_input != nullptr && m_input->keymap() && effectiveProfile(row) != nullptr;
}

bool DeviceManager::cameraLockedFor(const QString& serial) const
{
    return m_input != nullptr && m_input->cameraLocked() && !serial.isEmpty() && m_input->target() == serial;
}

bool DeviceManager::keymapBinds(const QString& serial, const QKeyEvent& key) const
{
    if (m_input == nullptr || !m_input->engaged() || m_input->target() != serial) {
        return false;
    }
    constexpr quint32 kExtended = 0xE000;
    const quint32 scanCode = key.nativeScanCode();
    input::KeyboardDecoder decoder;
    const std::optional<input::DeviceEvent> event =
        decoder.decode(static_cast<std::uint16_t>(scanCode & 0xFF), (scanCode & kExtended) != 0, false, false);
    return event && m_input->binds(event->source);
}

void DeviceManager::setInputProfile(std::shared_ptr<const input::Profile> profile, std::filesystem::path folder)
{
    m_override = std::move(profile);
    m_overrideFolder = m_override != nullptr ? std::move(folder) : std::filesystem::path();
    for (Row& row : m_rows) {
        applyProfile(row);
        offerStart(row);
    }
    syncInputProfile();
    applyKeymap();
    emit mirrorsChanged();
}

void DeviceManager::setProfileLibrary(ProfileLibrary* library)
{
    if (m_library != nullptr) {
        disconnect(m_library, nullptr, this, nullptr);
    }
    m_library = library;
    if (m_library != nullptr) {
        connect(m_library, &ProfileLibrary::entriesChanged, this, [this] { refreshProfiles({}); });
        connect(m_library, &ProfileLibrary::profileChanged, this, &DeviceManager::refreshProfiles);
    }
    refreshProfiles({});
}

QVariantList DeviceManager::mirrors() const
{
    QVariantList list;
    for (const Row& row : m_rows) {
        if (row.mirror == nullptr) {
            continue;
        }
        list.append(QVariantMap{{QStringLiteral("serial"), row.serial},
                                {QStringLiteral("name"), row.name()},
                                {QStringLiteral("app"), row.foregroundApp},
                                {QStringLiteral("profileId"), row.profileId},
                                {QStringLiteral("profileName"), profileName(row.profileId)},
                                {QStringLiteral("editingId"), row.editingId}});
    }
    return list;
}

void DeviceManager::useProfile(const QString& serial, const QString& id)
{
    const int index = indexOf(serial);
    if (index >= 0) {
        selectProfile(m_rows[static_cast<std::size_t>(index)], id, true);
    }
}

QVariantMap DeviceManager::phoneDetails(const QString& deviceId) const
{
    if (deviceId.isEmpty()) {
        return {};
    }
    for (const Row& row : m_rows) {
        if (row.deviceId != deviceId || (row.screen.isEmpty() && row.codecs.isEmpty())) {
            continue;
        }
        QVariantMap details{{QStringLiteral("codecs"), row.codecs}};
        if (!row.screen.isEmpty()) {
            details.insert(QStringLiteral("longSide"), std::max(row.screen.width(), row.screen.height()));
            details.insert(QStringLiteral("shortSide"), std::min(row.screen.width(), row.screen.height()));
        }
        if (row.refreshHz > 0) {
            details.insert(QStringLiteral("refreshHz"), row.refreshHz);
        }
        return details;
    }
    return {};
}

QString DeviceManager::visionPictureProblem(const QString& serial) const
{
    if (const int index = indexOf(serial); index >= 0 && m_rows[static_cast<std::size_t>(index)].aoaOnly &&
                                           noPictureOf(m_rows[static_cast<std::size_t>(index)])) {
        return tr("There is no picture over AOA without adb, so the vision workbench has nothing to look at");
    }
    if (const int index = indexOf(serial); index >= 0 && isEsp32Row(m_rows[static_cast<std::size_t>(index)])) {
        return tr("There is no picture with the ESP32 touch box, so the vision workbench has nothing to look at");
    }
    if (const int index = indexOf(serial); index >= 0 && m_rows[static_cast<std::size_t>(index)].provided) {
        return tr("There is no picture with %1, so the vision workbench has nothing to look at")
            .arg(m_rows[static_cast<std::size_t>(index)].name());
    }
    return {};
}

QString DeviceManager::openVisionWorkbench(const QString& serial, const QString& profileId)
{
    if (m_library == nullptr || m_engine.isNull() || m_graphics == nullptr) {
        return tr("The vision workbench cannot open now");
    }
    if (const QString problem = visionPictureProblem(serial); !problem.isEmpty()) {
        return problem;
    }
    const ProfileEntry* entry = m_library->find(profileId);
    if (entry == nullptr) {
        return tr("Choose a profile first");
    }
    if (entry->readOnly()) {
        return tr("Profiles from extensions cannot hold vision resources. Duplicate it to make one of your own");
    }
    const QString name = entry->name;
    const QString folder = m_library->ensureFolder(profileId);
    if (folder.isEmpty()) {
        return m_library->lastWriteBusy() ? busyText() : tr("The profile could not be turned into a folder");
    }
    return showVisionWorkbench(serial, profileId, name, folder, false);
}

QString DeviceManager::showVisionWorkbench(const QString& serial, const QString& profileId, const QString& name,
                                           const QString& folder, bool draft)
{
    if (m_vision != nullptr && m_visionWindow != nullptr && m_vision->profileId() == profileId
        && m_vision->draft() == draft) {
        if (!serial.isEmpty()) {
            m_vision->setPhone(serial);
        }
        if (m_visionWindow->visibility() == QWindow::Minimized) {
            m_visionWindow->showNormal();
        }
        m_visionWindow->raise();
        m_visionWindow->requestActivate();
        return {};
    }
    closeVisionWorkbench(false);
    QQmlEngine* engine = m_engine.data();
    if (engine->imageProvider(QStringLiteral("vision")) == nullptr) {
        engine->addImageProvider(QStringLiteral("vision"), new VisionImageProvider);
    }
    QString phone = serial;
    for (const Row& row : m_rows) {
        if (phone.isEmpty() && row.mirror != nullptr && row.profileId == profileId) {
            phone = row.serial;
        }
    }
    VisionSetup setup{.profileId = profileId, .profileName = name, .folder = folder.toStdWString(), .phone = phone};
    setup.draft = draft;
    setup.phones = [this] {
        std::vector<VisionPhone> phones;
        for (const Row& row : m_rows) {
            if (row.mirror != nullptr && framesOf(row) != nullptr) {
                phones.push_back({row.serial, row.name()});
            }
        }
        return phones;
    };
    setup.feedOf = [this](const QString& wanted) {
        const int index = indexOf(wanted);
        if (index < 0) {
            return VisionFeed{};
        }
        const Row& row = m_rows[static_cast<std::size_t>(index)];
        VisionFeed feed;
        if (CaptureFeed* captured = pictureFeedOf(row); captured != nullptr) {
            feed.frames = captured->benchFrames();
            feed.snapshots = captured->snapshots();
        } else if (row.controller != nullptr) {
            feed.frames = row.controller->benchFrames();
            feed.snapshots = row.controller->snapshots();
        } else if (row.demo != nullptr) {
            feed.frames = row.demo->benchFrames();
            feed.snapshots = row.demo->snapshots();
        } else {
            return VisionFeed{};
        }
        feed.feed = [this, wanted](bool on) {
            const int at = indexOf(wanted);
            if (at < 0) {
                return;
            }
            const Row& target = m_rows[static_cast<std::size_t>(at)];
            if (CaptureFeed* captured = pictureFeedOf(target); captured != nullptr) {
                captured->setBenchFed(on);
            } else if (target.controller != nullptr) {
                target.controller->setBenchFed(on);
            } else if (target.demo != nullptr) {
                target.demo->setBenchFed(on);
            }
        };
        return feed;
    };
    setup.unsaved = [this, folder] {
        QStringList files;
        const auto add = [&files, &folder](QObject* logic) {
            if (const auto* open = qobject_cast<ScriptWorkbench*>(logic);
                open != nullptr && sameFolder(open->folder(), folder)) {
                files += open->unsavedFiles();
            }
        };
        for (const Row& row : m_rows) {
            if (row.mirror != nullptr && row.mirror->editor() != nullptr) {
                add(row.mirror->editor()->logic());
            }
        }
        for (const auto& [key, window] : m_workbenches) {
            if (window != nullptr) {
                add(window->property("bench").value<QObject*>());
            }
        }
        return files;
    };
    auto* bench = new VisionWorkbench(std::move(setup));
    QQmlEngine::setObjectOwnership(bench, QQmlEngine::CppOwnership);
    QQmlComponent component(engine, QStringLiteral("GameScrcpy.App"), QStringLiteral("VisionWorkbenchWindow"));
    QObject* created = component.beginCreate(engine->rootContext());
    auto* window = qobject_cast<QQuickWindow*>(created);
    if (window == nullptr) {
        qWarning("vision workbench not created: %s", qUtf8Printable(component.errorString()));
        if (created != nullptr) {
            component.completeCreate();
        }
        delete created;
        delete bench;
        return tr("The vision workbench could not open");
    }
    m_graphics->share(*window);
    releaseMemoryAfter(*window);
    component.setInitialProperties(window, {{QStringLiteral("bench"), QVariant::fromValue(bench)}});
    component.completeCreate();
    bench->setParent(window);
    m_vision = bench;
    m_visionWindow = window;
    connect(this, &DeviceManager::mirrorsChanged, bench, &VisionWorkbench::refreshPhones);
    connect(window, &QWindow::visibleChanged, this, [this, window](bool visible) {
        if (!visible && m_visionWindow == window) {
            closeVisionWorkbench(false);
        }
    });
    window->show();
    qInfo("vision workbench for %s: %s", qUtf8Printable(name), qUtf8Printable(folder));
    return {};
}

QString DeviceManager::pickOnPicture(const QString& serial, const QString& folder, const QString& kind,
                                     const QString& prompt, std::function<void(const QString&)> done)
{
    QString profileId;
    if (m_library != nullptr) {
        for (const ProfileEntry& entry : m_library->all()) {
            if (!entry.folder.empty() && sameFolder(QString::fromStdWString(entry.folder.wstring()), folder)) {
                profileId = entry.id;
                break;
            }
        }
    }
    if (profileId.isEmpty()) {
        return tr("This logic does not belong to a profile, so it has no vision resources");
    }
    if (const QString problem = openVisionWorkbench(serial, profileId); !problem.isEmpty()) {
        return problem;
    }
    if (m_vision == nullptr) {
        return tr("The vision workbench could not open");
    }
    m_vision->startPick(kind, prompt, std::move(done));
    return {};
}

QString DeviceManager::requestMaterials(const QString& client, const QString& serial, const QString& profileId,
                                        const QString& profileName, const QString& folder, const QVariantList& items,
                                        std::function<void(const QVariantList&)> done)
{
    if (m_engine.isNull() || m_graphics == nullptr) {
        return tr("The vision workbench cannot open now");
    }
    if (m_vision != nullptr && m_visionWindow != nullptr && m_vision->requesting()) {
        return tr("The vision workbench is already asking the user for materials");
    }
    std::vector<VisionRequestItem> asked;
    for (const QVariant& item : items) {
        const QVariantMap entry = item.toMap();
        asked.push_back({.name = entry.value(QStringLiteral("name")).toString(),
                         .kind = entry.value(QStringLiteral("kind")).toString(),
                         .note = entry.value(QStringLiteral("note")).toString()});
    }
    if (asked.empty()) {
        return tr("There is nothing to ask the user for");
    }
    if (const QString problem = visionPictureProblem(serial); !problem.isEmpty()) {
        return problem;
    }
    if (const QString problem = showVisionWorkbench(serial, profileId, profileName, folder, true);
        !problem.isEmpty()) {
        return problem;
    }
    if (m_vision == nullptr || m_visionWindow == nullptr) {
        return tr("The vision workbench could not open");
    }
    m_materialsClient = client;
    QStringList names;
    for (const VisionRequestItem& item : asked) {
        names.append(item.name);
    }
    const int shown = std::min<int>(3, static_cast<int>(names.size()));
    QString text = names.mid(0, shown).join(QStringLiteral(", "));
    if (names.size() > shown) {
        text += tr(", and %n more", "", static_cast<int>(names.size()) - shown);
    }
    const bool inFront = QGuiApplication::focusWindow() != nullptr;
    if (!inFront) {
        notifyMain(tr("An AI is asking you for materials"), text);
    }
    m_visionWindow->raise();
    m_visionWindow->requestActivate();
    if (!inFront) {
        m_visionWindow->alert(0);
    }
    m_vision->beginRequest(std::move(asked),
                           [this, done = std::move(done)](const std::vector<VisionRequestResult>& results) {
        QVariantList made;
        for (const VisionRequestResult& result : results) {
            QVariantMap entry{{QStringLiteral("name"), result.name},
                              {QStringLiteral("kind"), result.kind},
                              {QStringLiteral("state"), result.state},
                              {QStringLiteral("resource"), result.resource}};
            if (!result.value.isEmpty()) {
                entry.insert(QStringLiteral("value"), QJsonObject::fromVariantMap(result.value));
            }
            made.append(entry);
        }
        m_materialsClient.clear();
        if (done) {
            done(made);
        }
    });
    return {};
}

void DeviceManager::endMaterials(const QString& client)
{
    if (client != m_materialsClient) {
        return;
    }
    m_materialsClient.clear();
    if (m_vision != nullptr) {
        m_vision->abandonRequest();
    }
}

void DeviceManager::warnInputRefused(Row& row)
{
    if (row.mirror == nullptr) {
        return;
    }
    const QStringList guide = guideOf(row).split(u'/');
    std::function<void()> showGuide = [this, vendor = guide.value(0), step = guide.value(1)] {
        if (QQmlEngine* engine = m_engine.data(); engine != nullptr) {
            if (auto* setup = engine->singletonInstance<SetupGuide*>("GameScrcpy.App", "SetupGuide")) {
                setup->open(1, vendor, step);
            }
        }
    };
    const bool uhid = row.controller != nullptr && row.controller->canSwitchToUhid() && !row.deviceId.isEmpty();
    if (uhid) {
        row.toastAction = [this, serial = row.serial] { useUhidTouch(serial); };
        row.toastSecondAction = std::move(showGuide);
        row.mirror->showToast(tr("The phone refuses simulated taps and keys"), QStringLiteral("triangle-alert"),
                              tr("Use a UHID touch screen"), tr("View guide"));
        return;
    }
    row.toastAction = std::move(showGuide);
    row.toastSecondAction = {};
    row.mirror->showToast(tr("The phone refuses simulated taps and keys"), QStringLiteral("triangle-alert"),
                          tr("View guide"));
}

void DeviceManager::useUhidTouch(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.controller == nullptr || row.deviceId.isEmpty()) {
        return;
    }
    const std::string key = row.deviceId.toStdString();
    settings::DeviceSettings device = m_settings.device(key);
    if (!settings::setOverride(device, "touch.method", settings::Value(std::string("uhid")))) {
        qWarning("cannot remember the UHID touch screen for %s", qUtf8Printable(row.name()));
        return;
    }
    m_settings.setDevice(key, device);
    qInfo("%s refuses simulated taps: switching it to a UHID touch screen and reconnecting", qUtf8Printable(row.name()));
    if (!active(row.session.phase)) {
        connectDevice(serial);
        return;
    }
    row.reconnectWhenStopped = true;
    row.controller->stop();
}

void DeviceManager::releaseMemoryAfter(QWindow& window)
{
    connect(&window, &QObject::destroyed, this, [this] {
        QMetaObject::invokeMethod(this, &DeviceManager::releaseWindowMemory, Qt::QueuedConnection);
    });
}

void DeviceManager::releaseWindowMemory()
{
    if (QQmlEngine* engine = m_engine.data(); engine != nullptr) {
        engine->collectGarbage();
    }
    platform::returnFreeHeapMemory();
}

void DeviceManager::closeVisionWorkbench(bool now)
{
    QWindow* window = m_visionWindow.data();
    if (m_vision != nullptr) {
        m_vision->abandonRequest();
    }
    m_materialsClient.clear();
    m_visionWindow = nullptr;
    m_vision = nullptr;
    if (window == nullptr) {
        return;
    }
    disconnect(window, nullptr, this, nullptr);
    if (now) {
        delete window;
    } else {
        window->deleteLater();
    }
}

void DeviceManager::selectProfile(Row& row, const QString& id, bool chosenByUser)
{
    std::shared_ptr<const input::Profile> profile;
    if (!id.isEmpty()) {
        profile = m_library != nullptr ? m_library->profile(id) : nullptr;
        if (profile == nullptr) {
            return;
        }
    }
    row.profileId = id;
    row.profile = std::move(profile);
    if (chosenByUser && !row.deviceId.isEmpty()) {
        const std::string key = row.deviceId.toStdString();
        settings::DeviceSettings device = m_settings.device(key);
        device.profile = id.toStdString();
        if (!row.foregroundApp.isEmpty()) {
            device.apps[row.foregroundApp.toStdString()] = id.toStdString();
        }
        m_settings.setDevice(key, device);
    }
    applyProfile(row);
    offerStart(row);
    syncInputProfile();
    applyKeymap();
    emit mirrorsChanged();
    if (row.mirror != nullptr && (chosenByUser || !id.isEmpty())) {
        const QString message =
            id.isEmpty() ? tr("No key mapping profile in use") : tr("Profile: %1").arg(profileName(id));
        row.mirror->showToast(message, QStringLiteral("keyboard"));
    }
}

void DeviceManager::foregroundApp(const QString& serial, const QString& package, bool game)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.foregroundApp == package && row.foregroundGame == game) {
        return;
    }
    row.foregroundApp = package;
    row.foregroundGame = game;
    qInfo("foreground app on %s: %s%s", qUtf8Printable(row.name()), qUtf8Printable(package), game ? " (game)" : "");
    if (row.mirror != nullptr) {
        row.mirror->setForegroundApp(package);
    }
    if (row.logic != nullptr) {
        row.logic->setForeground(package, game);
    }
    forSessionScripts(row.serial, [&package, game](ScriptRuntime& runtime) { runtime.setForeground(package, game); });
    publishPhoneEvent(AppTopic::AppForeground, row.serial, row.name(),
                      {{QStringLiteral("package"), package.isEmpty() ? QJsonValue() : QJsonValue(package)},
                       {QStringLiteral("game"), game}});
    followApp(row);
    offerStart(row);
    emit mirrorsChanged();
}

void DeviceManager::offerStart(Row& row)
{
    if (row.mirror == nullptr) {
        return;
    }
    QString offer;
    if (m_override == nullptr && m_library != nullptr) {
        const settings::DeviceSettings device =
            row.deviceId.isEmpty() ? settings::DeviceSettings() : m_settings.device(row.deviceId.toStdString());
        std::vector<std::string> closed = device.closedStarts;
        for (const QString& package : row.closedStarts) {
            closed.push_back(package.toStdString());
        }
        if (offersStart(*m_library, device.apps, closed, row.foregroundApp, row.foregroundGame)) {
            offer = row.foregroundApp;
        }
    }
    row.mirror->setStartOffer(offer);
}

void DeviceManager::closeStart(const QString& serial, const QString& package)
{
    const int index = indexOf(serial);
    if (index < 0 || package.isEmpty()) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.deviceId.isEmpty()) {
        if (!row.closedStarts.contains(package)) {
            row.closedStarts.append(package);
        }
    } else {
        const std::string key = row.deviceId.toStdString();
        settings::DeviceSettings device = m_settings.device(key);
        if (settings::rememberClosedStart(device, package.toStdString())) {
            m_settings.setDevice(key, device);
        }
    }
    qInfo("start panel closed for %s on %s", qUtf8Printable(package), qUtf8Printable(row.name()));
    offerStart(row);
}

void DeviceManager::followApp(Row& row)
{
    if (m_override != nullptr || m_library == nullptr || row.foregroundApp.isEmpty() ||
        !m_settings.effective(row.deviceId.toStdString()).keymap.autoProfile) {
        return;
    }
    const std::map<std::string, std::string, std::less<>> remembered =
        row.deviceId.isEmpty() ? std::map<std::string, std::string, std::less<>>()
                               : m_settings.device(row.deviceId.toStdString()).apps;
    const ProfileChoice choice = chooseForApp(*m_library, remembered, row.foregroundApp, row.profileId);
    if (choice.change) {
        selectProfile(row, choice.id, false);
    }
}

void DeviceManager::stepProfile(const QString& serial, int step)
{
    const int index = indexOf(serial);
    if (index < 0 || m_library == nullptr) {
        return;
    }
    const Row& row = m_rows[static_cast<std::size_t>(index)];
    const QString next = m_library->neighbour(row.profileId, step);
    if (next.isEmpty()) {
        return;
    }
    if (next == row.profileId) {
        if (row.mirror != nullptr) {
            row.mirror->showToast(tr("No other profile for this game"), QStringLiteral("info"));
        }
        return;
    }
    useProfile(serial, next);
}

std::shared_ptr<const input::Profile> DeviceManager::effectiveProfile(const Row& row) const
{
    return profileInUse(row.mirror != nullptr ? row.mirror->editor() : nullptr,
                        m_override != nullptr ? m_override : row.profile);
}

QString DeviceManager::rememberedProfile(const Row& row) const
{
    if (row.deviceId.isEmpty()) {
        return {};
    }
    return QString::fromStdString(m_settings.device(row.deviceId.toStdString()).profile);
}

QString DeviceManager::profileName(const QString& id) const
{
    if (id.isEmpty() || m_library == nullptr) {
        return {};
    }
    const ProfileEntry* entry = m_library->find(id);
    return entry != nullptr ? entry->name : QString();
}

void DeviceManager::restoreProfile(Row& row)
{
    if (!row.profileId.isEmpty() || m_library == nullptr) {
        return;
    }
    const QString remembered = rememberedProfile(row);
    if (std::shared_ptr<const input::Profile> profile = m_library->profile(remembered)) {
        row.profileId = remembered;
        row.profile = std::move(profile);
    }
}

std::pair<QString, QString> DeviceManager::profileInfoOf(const Row& row) const
{
    if (m_override != nullptr) {
        return {m_overrideFolder.empty() ? QString() : QString::fromStdWString(m_overrideFolder.filename().wstring()),
                QString::fromStdString(m_override->meta.name)};
    }
    return {row.profileId, profileName(row.profileId)};
}

void DeviceManager::tellTasksProfile(const Row& row)
{
    const auto [id, name] = profileInfoOf(row);
    for (const std::unique_ptr<TaskRun>& run : m_taskRuns) {
        if (run->serial == row.serial && run->runtime != nullptr) {
            run->runtime->setProfileFact(id, name);
        }
    }
    const std::pair<QString, QString> fact = profileInfoOf(row);
    forSessionScripts(row.serial,
                      [&fact](ScriptRuntime& runtime) { runtime.setProfileFact(fact.first, fact.second); });
}

void DeviceManager::applyProfile(Row& row)
{
    if (row.mirror == nullptr) {
        return;
    }
    tellTasksProfile(row);
    if (m_override != nullptr && !row.mirror->trying()) {
        row.mirror->setProfile(m_override, defaultLockKey(), {}, QString::fromStdString(m_override->meta.name));
    } else {
        row.mirror->setProfile(effectiveProfile(row), defaultLockKey(), row.profileId, profileName(row.profileId));
    }
    m_moduleTroubles.clear();
    showModuleNotice(row);
    syncLogic(row);
    const auto [appliedId, appliedName] = profileInfoOf(row);
    publishPhoneEvent(AppTopic::ProfileApplied, row.serial, row.name(),
                      {{QStringLiteral("profile"), appliedId.isEmpty() ? QJsonValue() : QJsonValue(appliedId)},
                       {QStringLiteral("profileName"), appliedName.isEmpty() ? QJsonValue() : QJsonValue(appliedName)}});
}

void DeviceManager::syncLogic(Row& row)
{
    syncSessionScripts(row);
    LogicSource wanted;
    if (row.mirror != nullptr && m_input != nullptr) {
        if (m_override != nullptr) {
            if (!m_overrideFolder.empty()) {
                wanted.folder = m_overrideFolder;
                wanted.profile = effectiveProfile(row);
                wanted.id = QString::fromStdWString(m_overrideFolder.filename().wstring());
                wanted.name = QString::fromStdString(m_override->meta.name);
            }
        } else if (m_library != nullptr && !row.profileId.isEmpty() && row.profile != nullptr) {
            if (const ProfileEntry* entry = m_library->find(row.profileId); entry != nullptr && !entry->folder.empty()) {
                wanted.folder = entry->folder;
                wanted.profile = effectiveProfile(row);
                wanted.id = entry->id;
                wanted.name = entry->name;
            } else if (entry != nullptr && entry->readOnly() && !entry->extensionFolder.empty()) {
                wanted.folder = entry->extensionFolder;
                wanted.profile = effectiveProfile(row);
                wanted.id = entry->id;
                wanted.name = entry->name;
                wanted.extension = entry->extensionId;
                if (m_extensions != nullptr) {
                    wanted.permissions = m_extensions->grantedPermissions(entry->extensionId.toStdString());
                }
            }
        }
        if (!wanted.folder.empty()) {
            wanted.entry = logicEntryOf(wanted.folder);
            if (wanted.entry.empty() && wanted.profile != nullptr && input::usesComponents(*wanted.profile)) {
                wanted.entry = "logic.ts";
            }
        }
    }
    if (wanted.entry.empty()) {
        row.logic.reset();
        return;
    }
    if (row.logic != nullptr && row.logic->source().folder == wanted.folder &&
        row.logic->source().entry == wanted.entry && row.logic->source().profile == wanted.profile &&
        row.logic->source().permissions == wanted.permissions) {
        return;
    }
    row.logic.reset();
    const std::shared_ptr<ControlChannel> channel = channelOf(row);
    if (channel == nullptr) {
        return;
    }
    if (wanted.name.isEmpty()) {
        wanted.name = wanted.id;
    }
    wanted.device = row.name();
    wanted.deviceKey = logicKeyOf(row);
    wanted.serial = row.serial;
    if (m_override == nullptr && wanted.extension.isEmpty()) {
        wanted.inputGrant = {.reader = InputReader::Profile, .id = wanted.id};
    }
    wanted.snapshots = shotsOf(row);
    qInfo("logic %s on %s: starting %s", qUtf8Printable(wanted.name), qUtf8Printable(wanted.device),
          qUtf8Printable(QString::fromStdWString((wanted.folder / wanted.entry).wstring())));
    row.logic = std::make_unique<ScriptRuntime>(std::move(wanted),
                                                std::make_shared<RecordingLane>(
                                                    std::make_shared<ControllerLane>(*m_input, row.serial, channel),
                                                    tapOf(row)),
                                                m_scriptStore);
    row.logic->setDevice(deviceInfoOf(row));
    row.logic->setFrameRate(row.refreshHz > 0.0 ? row.refreshHz : 60.0);
    const QString serial = row.serial;
    connect(row.logic.get(), &ScriptRuntime::failed, this,
            [this, serial](const QString& text) { logicFailed(serial, text); });
    connect(row.logic.get(), &ScriptRuntime::tooFast, this, [this, serial] { logicTooFast(serial); });
    connect(row.logic.get(), &ScriptRuntime::storeBusy, this, [this, serial] { logicFailed(serial, busyText()); });
    connect(row.logic.get(), &ScriptRuntime::toast, this, [this, serial](const QString& text) {
        if (MirrorWindow* mirror = mirrorOf(serial)) {
            mirror->showToast(text, QStringLiteral("info"));
        }
    });
    connect(row.logic.get(), &ScriptRuntime::prompt, this, [this, serial](quint64 request, const QVariantMap& shown) {
        if (MirrorWindow* mirror = mirrorOf(serial)) {
            mirror->showPrompt(request, shown);
        }
    });
    connect(row.logic.get(), &ScriptRuntime::promptWithdrawn, this, [this, serial](quint64 request) {
        if (MirrorWindow* mirror = mirrorOf(serial)) {
            mirror->withdrawPrompt(request);
        }
    });
    connect(row.logic.get(), &ScriptRuntime::drawn, this, [this, serial](quint64 id, const QVariantMap& drawing) {
        if (MirrorWindow* mirror = mirrorOf(serial)) {
            mirror->draw(id, drawing);
        }
    });
    connect(row.logic.get(), &ScriptRuntime::erased, this, [this, serial](quint64 id) {
        if (MirrorWindow* mirror = mirrorOf(serial)) {
            mirror->erase(id);
        }
    });
    connect(row.logic.get(), &ScriptRuntime::drawingsCleared, this, [this, serial] {
        if (MirrorWindow* mirror = mirrorOf(serial)) {
            mirror->clearDrawings();
            mirror->clearHintChanges();
        }
    });
    connect(row.logic.get(), &ScriptRuntime::appRequested, this, [this, serial](const QString& request, const QString& json) {
        logicRequest(serial, QStringLiteral("logic"), request, json);
    });
    connect(row.logic.get(), &QObject::destroyed, this, [this, serial] {
        if (MirrorWindow* mirror = mirrorOf(serial)) {
            mirror->clearScriptProgress(QStringLiteral("logic"));
        }
        if (const int index = indexOf(serial); index >= 0) {
            Row& gone = m_rows[static_cast<std::size_t>(index)];
            if (gone.pendingSave) {
                if (gone.mirror != nullptr) {
                    gone.mirror->withdrawPrompt(gone.pendingSave->request);
                }
                gone.pendingSave.reset();
            }
        }
    });
    connect(row.logic.get(), &ScriptRuntime::saveWanted, this,
            [this, serial, runtime = row.logic.get()](quint64 request, const QVariantList& changes,
                                                      std::shared_ptr<const input::Profile> profile) {
                askToSave(serial, *runtime, request, changes, std::move(profile));
            });
    connect(row.logic.get(), &ScriptRuntime::clipboardWanted, this,
            [this, serial, runtime = row.logic.get()](quint64 request) { readClipboardFor(serial, *runtime, request); });
    connect(row.logic.get(), &ScriptRuntime::clipboardGiven, this,
            [this, serial, runtime = row.logic.get()](quint64 request, const QString& text) {
                writeClipboardFor(serial, *runtime, request, text);
            });
    connect(row.logic.get(), &ScriptRuntime::inputWanted, this,
            [this, serial, runtime = row.logic.get()] { askToRead(serial, *runtime, 0); });
    connect(row.logic.get(), &QObject::destroyed, this, [this, runtime = row.logic.get()] { dropReadAsk(runtime); });
    row.logic->setKeymapOn(keymapOf(row));
    row.logic->setForeground(row.foregroundApp, row.foregroundGame);
    row.logic->setFocused(row.focused);
    connect(row.logic.get(), &ScriptRuntime::reported, this, [this, serial](const LogicReport& report) {
        m_logicConsole.append(serial, report);
        emit logicReported(serial, report);
    });
    const QString logicFolder = QString::fromStdWString(row.logic->source().folder.wstring());
    connect(row.logic.get(), &ScriptRuntime::debugHits, this, [this, serial, logicFolder](const QVariantList& hits) {
        emit logicDebugHits(serial, logicFolder, hits);
    });
    connect(row.logic.get(), &ScriptRuntime::debugPaused, this, [this, serial, logicFolder](const QVariantMap& pause) {
        emit logicPaused(serial, logicFolder, pause);
    });
    connect(row.logic.get(), &ScriptRuntime::debugResumed, this, [this, serial, logicFolder](quint64 pause) {
        emit logicResumed(serial, logicFolder, pause);
    });
    connect(row.logic.get(), &ScriptRuntime::handedOverChanged, this, [this, serial](const QStringList& controls) {
        if (ProfileEditor* editor = editorOf(serial)) {
            editor->setHandedOver(controls);
        }
    });
    connect(
        row.logic.get(), &QObject::destroyed, this,
        [this, serial] {
            if (ProfileEditor* editor = editorOf(serial)) {
                editor->setHandedOver({});
            }
        },
        Qt::QueuedConnection);
    connect(row.logic.get(), &QObject::destroyed, this, [this, serial] { emit logicChanged(serial); },
            Qt::QueuedConnection);
    emit logicChanged(serial);
}

void DeviceManager::logicRequest(const QString& serial, const QString& source, const QString& request,
                                 const QString& json)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    const QJsonArray wrapped = QJsonDocument::fromJson(QStringLiteral("[%1]").arg(json).toUtf8()).array();
    const QJsonValue value = wrapped.isEmpty() ? QJsonValue() : wrapped.first();
    if (request == u"progress") {
        if (row.mirror != nullptr) {
            if (value.isObject()) {
                const QJsonObject shown = value.toObject();
                row.mirror->setScriptProgress(source, shown.value(QStringLiteral("title")).toString(),
                                              shown.value(QStringLiteral("value")).toDouble());
            } else {
                row.mirror->clearScriptProgress(source);
            }
        }
        return;
    }
    if (request == u"notify") {
        const QJsonObject shown = value.toObject();
        showNotification(serial, shown.value(QStringLiteral("title")).toString(),
                         shown.value(QStringLiteral("text")).toString());
        return;
    }
    if (request == u"keymap") {
        if (m_input != nullptr && m_input->target() == serial) {
            m_input->setKeymap(value.isString() ? !m_input->keymap() : value.toBool());
        }
    } else if (request == u"hints") {
        if (row.mirror != nullptr) {
            row.mirror->forceHints(source, value.toObject().toVariantMap());
        }
    } else if (request == u"hint") {
        const QVariantMap change = value.toObject().toVariantMap();
        if (row.mirror != nullptr) {
            row.mirror->changeHint(change.value(QStringLiteral("id")).toString(), change);
        }
    } else if (request == u"profile") {
        const QJsonObject asked = value.toObject();
        if (asked.contains(QStringLiteral("step"))) {
            stepProfile(serial, asked.value(QStringLiteral("step")).toInt());
        } else if (m_library != nullptr) {
            const QString wanted = asked.value(QStringLiteral("to")).toString();
            for (const ProfileEntry& entry : m_library->all()) {
                if (entry.id == wanted || entry.name == wanted) {
                    useProfile(serial, entry.id);
                    break;
                }
            }
        }
    } else if (request == u"launch") {
        const QJsonObject asked = value.toObject();
        const QString package = asked.value(QStringLiteral("package")).toString();
        if (row.controller != nullptr) {
            row.controller->startApp(package, asked.value(QStringLiteral("restart")).toBool());
        } else if (row.aoaOnly) {
            qInfo("%s is controlled over AOA without adb: %s cannot be launched", qUtf8Printable(row.name()),
                  qUtf8Printable(package));
        } else {
            qInfo("demo device: launch %s, not sent", qUtf8Printable(package));
        }
    } else if (request == u"screen") {
        setScreenOff(serial, !value.toBool());
    } else if (request == u"machineState") {
        const QJsonObject state = value.toObject();
        const QString folder = row.logic != nullptr ? QString::fromStdWString(row.logic->source().folder.wstring())
                                                    : QString();
        emit machineStateChanged(serial, folder,
                                 {{QStringLiteral("file"), state.value(QStringLiteral("file")).toString()},
                                  {QStringLiteral("line"), state.value(QStringLiteral("line")).toInt()},
                                  {QStringLiteral("name"), state.value(QStringLiteral("name")).toString()},
                                  {QStringLiteral("state"), state.value(QStringLiteral("state")).toString()},
                                  {QStringLiteral("reason"), state.value(QStringLiteral("reason")).toString()},
                                  {QStringLiteral("since"), static_cast<double>(QDateTime::currentMSecsSinceEpoch())}});
    } else if (request == u"rotate") {
        setOrientation(serial, value.toInt());
    }
}

void DeviceManager::showNotification(const QString& serial, const QString& title, const QString& text)
{
    showNotice(title, text, NoticeTarget{.kind = NoticeTarget::Kind::Phone, .serial = serial});
}

void DeviceManager::showTaskNotice(const QString& task, const QString& title, const QString& text)
{
    showNotice(title, text, NoticeTarget{.kind = NoticeTarget::Kind::Task, .task = task});
}

void DeviceManager::notifyMain(const QString& title, const QString& text)
{
    showNotice(title, text, NoticeTarget{.kind = NoticeTarget::Kind::Main});
}

void DeviceManager::showNotice(const QString& title, const QString& text, NoticeTarget target)
{
    const bool shown = m_notices.show(title, text, std::move(target),
                                      [this](const QString& balloonTitle, const QString& balloonText) {
                                          return tray().show(balloonTitle.toStdWString(), balloonText.toStdWString());
                                      });
    if (!shown) {
        qWarning("could not show the notification \"%s\"", qUtf8Printable(title));
    }
}

void DeviceManager::openNotice()
{
    const NoticeTarget target = m_notices.target();
    switch (target.kind) {
    case NoticeTarget::Kind::Task:
        emit tasksPageWanted(target.task);
        return;
    case NoticeTarget::Kind::Phone:
        if (const int index = indexOf(target.serial);
            index >= 0 && (m_rows[static_cast<std::size_t>(index)].mirror != nullptr ||
                           m_rows[static_cast<std::size_t>(index)].demo != nullptr)) {
            showWindow(target.serial);
            return;
        }
        break;
    case NoticeTarget::Kind::Main:
        break;
    }
    emit mainWindowWanted();
}

platform::TrayNotice& DeviceManager::tray()
{
    if (m_tray == nullptr) {
        m_tray = std::make_unique<platform::TrayNotice>([this] { openNotice(); });
        m_tray->setActivated([this] { emit mainWindowWanted(); });
        setTrayMenu();
    }
    return *m_tray;
}

void DeviceManager::setTrayMenu()
{
    if (m_tray == nullptr) {
        return;
    }
    std::vector<platform::TrayMenuItem> items{{tr("Open GameScrcpy").toStdWString(), [this] { emit mainWindowWanted(); }}};
    bool extensions = false;
    for (const QVariant& each : ExtensionPanels::instance() != nullptr ? ExtensionPanels::instance()->menus()
                                                                       : QVariantList()) {
        const QVariantMap group = each.toMap();
        if (group.value(QStringLiteral("where")).toString() != u"tray") {
            continue;
        }
        if (!std::exchange(extensions, true)) {
            items.push_back({.separator = true});
        }
        const quint64 logic = group.value(QStringLiteral("logic")).toULongLong();
        const QString name = group.value(QStringLiteral("name")).toString();
        for (const QVariant& entry : group.value(QStringLiteral("items")).toList()) {
            const QVariantMap item = entry.toMap();
            const QString id = item.value(QStringLiteral("id")).toString();
            const bool checkable = item.value(QStringLiteral("checkable")).toBool();
            const bool checked = item.value(QStringLiteral("checked")).toBool();
            items.push_back({.text = item.value(QStringLiteral("title")).toString().toStdWString(),
                             .chosen =
                                 [logic, name, id, checkable, checked] {
                                     if (const ExtensionPanels* panels = ExtensionPanels::instance()) {
                                         panels->choose(logic, name, id, checkable ? QVariant(!checked) : QVariant(),
                                                        {});
                                     }
                                 },
                             .checked = checked,
                             .enabled = item.value(QStringLiteral("enabled")).toBool()});
        }
    }
    if (extensions) {
        items.push_back({.separator = true});
    }
    items.push_back({tr("Quit").toStdWString(), [] { QCoreApplication::quit(); }});
    m_tray->setMenu(std::move(items));
}

bool DeviceManager::hideToTrayOnClose()
{
    const bool keep = m_tasks != nullptr && closesToTray(m_settings.effective({}).tray.closeToTray, m_tasks->entries());
    QGuiApplication::setQuitOnLastWindowClosed(!keep);
    if (!keep) {
        return false;
    }
    if (!tray().showIcon()) {
        qWarning("the notification area did not take the GameScrcpy icon: closing instead of hiding");
        QGuiApplication::setQuitOnLastWindowClosed(true);
        return false;
    }
    if (!std::exchange(m_trayNoticeShown, true)) {
        showNotice(QStringLiteral("GameScrcpy"),
                   tr("GameScrcpy is still running in the tray; scheduled tasks run as usual"), NoticeTarget{});
    }
    qInfo("main window hidden to the tray: a task has a schedule");
    return true;
}

void DeviceManager::readClipboardFor(const QString& serial, ScriptRuntime& runtime, quint64 request)
{
    const int index = indexOf(serial);
    if (index >= 0) {
        Row& row = m_rows[static_cast<std::size_t>(index)];
        if (row.controller != nullptr && row.controller->readPhoneClipboard()) {
            row.clipboardReads.emplace_back(&runtime, request);
            return;
        }
        if (row.controller == nullptr && row.demo != nullptr) {
            qInfo("demo device: clipboard read, not sent");
        }
    }
    runtime.answer(request, QStringLiteral("null"));
}

void DeviceManager::writeClipboardFor(const QString& serial, ScriptRuntime& runtime, quint64 request,
                                      const QString& text)
{
    bool sent = false;
    if (const int index = indexOf(serial); index >= 0) {
        Row& row = m_rows[static_cast<std::size_t>(index)];
        if (row.controller != nullptr) {
            sent = row.controller->writePhoneClipboard(text);
        } else if (row.demo != nullptr) {
            qInfo("demo device: clipboard text of %lld characters, not sent", static_cast<long long>(text.size()));
        }
    }
    runtime.answer(request, sent ? QStringLiteral("true") : QStringLiteral("false"));
}

void DeviceManager::phoneClipboardRead(SessionController* controller, bool ok, const QString& text)
{
    const auto found = std::ranges::find(m_rows, controller, &Row::controller);
    if (found == m_rows.end()) {
        return;
    }
    const QString json = ok ? jsonText(text) : QStringLiteral("null");
    for (const auto& [runtime, request] : std::exchange(found->clipboardReads, {})) {
        if (runtime != nullptr) {
            runtime->answer(request, json);
        }
    }
}

void DeviceManager::answerPrompt(const QString& serial, quint64 request, const QString& json)
{
    if (m_extensionLogic != nullptr && m_extensionLogic->answerPrompt(request, json)) {
        return;
    }
    if (answerRead(serial, request, json)) {
        return;
    }
    if (const quint64 number = request >> kTaskIdShift; number != 0) {
        if (TaskRun* run = taskRun(number); run != nullptr && run->runtime != nullptr) {
            run->runtime->answer(request - (number << kTaskIdShift), json);
        }
        return;
    }
    const int index = indexOf(serial);
    if (index >= 0 && m_rows[static_cast<std::size_t>(index)].logic != nullptr) {
        Row& row = m_rows[static_cast<std::size_t>(index)];
        if (row.pendingSave && row.pendingSave->request == request) {
            finishSave(row, json == u"true");
            return;
        }
        if (row.recordingTap != nullptr) {
            row.recordingTap->answered(json);
        }
        row.logic->answer(request, json);
    }
}

void DeviceManager::askToSave(const QString& serial, ScriptRuntime& runtime, quint64 request,
                              const QVariantList& changes, std::shared_ptr<const input::Profile> profile)
{
    const int index = indexOf(serial);
    Row* row = index >= 0 ? &m_rows[static_cast<std::size_t>(index)] : nullptr;
    const bool usable = row != nullptr && row->logic.get() == &runtime && m_override == nullptr && m_library != nullptr &&
                        !row->profileId.isEmpty() && row->mirror != nullptr && row->mirror->editor() == nullptr &&
                        runtime.source().profile == row->profile && profile != nullptr && !row->pendingSave &&
                        !changes.isEmpty();
    if (!usable) {
        runtime.answer(request, QStringLiteral("false"));
        return;
    }
    row->pendingSave = Row::PendingSave{.request = request, .profile = std::move(profile)};
    row->mirror->showPrompt(request, {{QStringLiteral("kind"), QStringLiteral("save")},
                                      {QStringLiteral("profile"), profileName(row->profileId)},
                                      {QStringLiteral("changes"), changes}});
}

void DeviceManager::finishSave(Row& row, bool confirmed)
{
    const Row::PendingSave pending = *row.pendingSave;
    row.pendingSave.reset();
    const QPointer<ScriptRuntime> runtime = row.logic.get();
    const QPointer<MirrorWindow> mirror = row.mirror;
    const QString id = row.profileId;
    bool saved = false;
    if (confirmed && m_library != nullptr && pending.profile != nullptr) {
        saved = m_library->save(id, *pending.profile);
        if (!saved && mirror != nullptr) {
            mirror->showToast(m_library->lastWriteBusy() ? busyText() : tr("The changes could not be saved to the profile"),
                              QStringLiteral("bad"));
        }
    }
    if (runtime != nullptr) {
        runtime->answer(pending.request, saved ? QStringLiteral("true") : QStringLiteral("false"));
    }
}

void DeviceManager::askToRead(const QString& serial, ScriptRuntime& runtime, quint64 request)
{
    if (std::ranges::any_of(m_readAsks, [&runtime](const ReadAsk& ask) { return ask.runtime == &runtime; })) {
        return;
    }
    MirrorWindow* mirror = mirrorOf(serial);
    if (mirror == nullptr) {
        return;
    }
    m_readAsks.push_back(ReadAsk{.runtime = &runtime, .serial = serial, .request = request, .mirror = mirror});
    const LogicSource& source = runtime.source();
    qInfo("logic %s on %s: asks to read the keys and the cursor", qUtf8Printable(source.name),
          qUtf8Printable(source.device));
    mirror->showPrompt(request, {{QStringLiteral("kind"), QStringLiteral("confirm")},
                                 {QStringLiteral("question"),
                                  tr("Allow the logic of %1 to read the keys you press and the cursor position?")
                                      .arg(source.name)},
                                 {QStringLiteral("ok"), tr("Allow")},
                                 {QStringLiteral("cancel"), tr("Don't allow")}});
}

bool DeviceManager::answerRead(const QString& serial, quint64 request, const QString& json)
{
    if ((request & ((quint64{1} << kTaskIdShift) - 1)) != 0) {
        return false;
    }
    const auto found = std::ranges::find_if(
        m_readAsks, [&](const ReadAsk& ask) { return ask.serial == serial && ask.request == request; });
    if (found == m_readAsks.end()) {
        return true;
    }
    ScriptRuntime* runtime = found->runtime;
    m_readAsks.erase(found);
    const LogicSource& source = runtime->source();
    if (json != u"true") {
        qInfo("logic %s on %s: was not allowed to read the keys and the cursor", qUtf8Printable(source.name),
              qUtf8Printable(source.device));
        return true;
    }
    qInfo("logic %s on %s: may read the keys and the cursor from now on", qUtf8Printable(source.name),
          qUtf8Printable(source.device));
    runtime->allowInput();
    if (source.inputGrant.reader == InputReader::Run) {
        return true;
    }
    std::vector<ReadAsk> settled;
    for (auto it = m_readAsks.begin(); it != m_readAsks.end();) {
        if (it->runtime->source().inputGrant == source.inputGrant) {
            settled.push_back(std::move(*it));
            it = m_readAsks.erase(it);
        } else {
            ++it;
        }
    }
    for (const ReadAsk& ask : settled) {
        if (ask.mirror != nullptr) {
            ask.mirror->withdrawPrompt(ask.request);
        }
    }
    return true;
}

void DeviceManager::dropReadAsk(const ScriptRuntime* runtime)
{
    const auto found =
        std::ranges::find_if(m_readAsks, [runtime](const ReadAsk& ask) { return ask.runtime == runtime; });
    if (found == m_readAsks.end()) {
        return;
    }
    MirrorWindow* mirror = found->mirror.data();
    const quint64 request = found->request;
    m_readAsks.erase(found);
    if (mirror != nullptr) {
        QMetaObject::invokeMethod(
            mirror, [mirror, request] { mirror->withdrawPrompt(request); }, Qt::QueuedConnection);
    }
}

void DeviceManager::setPrompting(const QString& serial, bool on)
{
    const int index = indexOf(serial);
    if (index < 0 || m_rows[static_cast<std::size_t>(index)].prompting == on) {
        return;
    }
    m_rows[static_cast<std::size_t>(index)].prompting = on;
    if (on) {
        showFromGrid(serial, false);
    }
    foregroundChanged();
}

void DeviceManager::toastAction(const QString& serial, int which)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    const std::function<void()> action = std::exchange(which == 1 ? row.toastSecondAction : row.toastAction, {});
    row.toastAction = {};
    row.toastSecondAction = {};
    if (action) {
        action();
    }
}

QString DeviceManager::logicKeyOf(const Row& row)
{
    return row.deviceId.isEmpty() ? row.serial : row.deviceId;
}

script::DeviceInfo DeviceManager::deviceInfoOf(const Row& row)
{
    script::DeviceInfo info;
    info.model = (row.model.isEmpty() ? row.name() : row.model).toStdString();
    const QString release = row.androidRelease.section(u'.', 0, 0);
    info.android = release.toInt();
    QSize size = row.screen;
    if (size.isEmpty()) {
        if (const std::shared_ptr<ControlChannel> channel = channelOf(row)) {
            if (const std::optional<InputScreen> screen = channel->screen()) {
                size = QSize(static_cast<int>(screen->geometry.width), static_cast<int>(screen->geometry.height));
            }
        }
    }
    info.width = size.width();
    info.height = size.height();
    info.rotation = row.rotation;
    return info;
}

void DeviceManager::logicFailed(const QString& serial, const QString& text)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    const auto now = std::chrono::steady_clock::now();
    std::erase_if(m_logicNotices, [now](const auto& notice) { return now - notice.second >= std::chrono::minutes(1); });
    const QString key = (row.logic != nullptr ? row.logic->source().id : row.profileId) + u'\n' + text;
    if (!m_logicNotices.try_emplace(key, now).second) {
        return;
    }
    if (row.mirror != nullptr) {
        row.mirror->showToast(text, QStringLiteral("triangle-alert"));
    }
}

void DeviceManager::logicTooFast(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    const QString key = row.logic != nullptr ? row.logic->source().id : row.profileId;
    const QString name = row.logic != nullptr ? row.logic->source().name : key;
    qInfo("logic %s on %s: more than %zu actions in a second", qUtf8Printable(name), qUtf8Printable(row.name()),
          ScriptRuntime::kFastActions);
    if (!m_fastNotices.insert(key).second) {
        return;
    }
    if (row.mirror != nullptr) {
        row.mirror->showToast(
            tr("Logic is acting more than %1 times a second").arg(QString::number(ScriptRuntime::kFastActions)),
            QStringLiteral("info"));
    }
}

QString DeviceManager::logicFolderOf(const Row& row) const
{
    if (m_override != nullptr) {
        return m_overrideFolder.empty() ? QString() : QString::fromStdWString(m_overrideFolder.wstring());
    }
    const ProfileEntry* entry =
        m_library != nullptr && !row.profileId.isEmpty() ? m_library->find(row.profileId) : nullptr;
    return entry != nullptr && !entry->folder.empty() ? QString::fromStdWString(entry->folder.wstring()) : QString();
}

std::vector<LogicPhone> DeviceManager::logicPhones() const
{
    std::vector<LogicPhone> phones;
    for (const Row& row : m_rows) {
        if (row.mirror == nullptr) {
            continue;
        }
        LogicPhone phone{.serial = row.serial, .name = row.name(), .folder = logicFolderOf(row)};
        if (row.logic != nullptr) {
            phone.started = true;
            phone.halted = row.logic->halted();
            phone.running = row.logic->running() && !phone.halted;
            phone.error = row.logic->lastError();
        }
        phones.push_back(std::move(phone));
    }
    return phones;
}

QImage DeviceManager::thumbnailOf(const QString& serial) const
{
    const int index = indexOf(serial);
    return index >= 0 ? m_rows[static_cast<std::size_t>(index)].thumbnail : QImage();
}

QVariantMap DeviceManager::logicSnapshot(const QString& serial) const
{
    const int index = indexOf(serial);
    const ScriptRuntime* logic = index >= 0 ? m_rows[static_cast<std::size_t>(index)].logic.get() : nullptr;
    return logic != nullptr ? logic->snapshot() : QVariantMap{};
}

bool DeviceManager::restartLogic(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return false;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.logic != nullptr) {
        qInfo("logic %s on %s: restarted from the script workbench", qUtf8Printable(row.logic->source().name),
              qUtf8Printable(row.name()));
        row.logic->reload();
        return true;
    }
    syncLogic(row);
    return row.logic != nullptr;
}

bool DeviceManager::debugLogic(const QString& serial, bool on)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return false;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.logic == nullptr) {
        syncLogic(row);
    }
    if (row.logic == nullptr) {
        return false;
    }
    qInfo("logic %s on %s: debugging %s", qUtf8Printable(row.logic->source().name), qUtf8Printable(row.name()),
          on ? "on" : "off");
    row.logic->setDebugging(on);
    return true;
}

void DeviceManager::setLogicBreakpoints(const QString& serial, const QStringList& places)
{
    const int index = indexOf(serial);
    if (index >= 0 && m_rows[static_cast<std::size_t>(index)].logic != nullptr) {
        m_rows[static_cast<std::size_t>(index)].logic->setBreakpoints(places);
    }
}

void DeviceManager::resumeLogic(const QString& serial, quint64 pause, int mode)
{
    const int index = indexOf(serial);
    if (index >= 0 && m_rows[static_cast<std::size_t>(index)].logic != nullptr) {
        m_rows[static_cast<std::size_t>(index)].logic->resumeDebug(pause, mode);
    }
}

QString DeviceManager::openLogic(const QString& id)
{
    const ProfileEntry* entry = m_library != nullptr ? m_library->find(id) : nullptr;
    if (entry == nullptr) {
        return tr("This profile is no longer there");
    }
    const QString name = entry->name;
    if (entry->readOnly()) {
        return tr("%1 comes with an extension and cannot have its own logic. Copy it to your profiles first").arg(name);
    }
    if (m_override == nullptr) {
        QString serial;
        for (const Row& row : m_rows) {
            if (row.mirror == nullptr || row.profileId != id) {
                continue;
            }
            if (row.mirror->editing() || serial.isEmpty()) {
                serial = row.serial;
            }
            if (row.mirror->editing()) {
                break;
            }
        }
        if (!serial.isEmpty()) {
            showLogicIn(serial, id);
            return {};
        }
    }
    const QString folder = m_library->ensureFolder(id);
    if (folder.isEmpty()) {
        return m_library->lastWriteBusy() ? busyText() : tr("The logic of %1 cannot be opened").arg(name);
    }
    openWorkbench(folder, name);
    return {};
}

QString DeviceManager::openLogicFor(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return {};
    }
    const Row& row = m_rows[static_cast<std::size_t>(index)];
    if (m_override != nullptr) {
        if (m_overrideFolder.empty()) {
            return tr("This run uses a profile file. Start with --profile and the profile's folder to edit its logic");
        }
        const QString folder = QString::fromStdWString(m_overrideFolder.wstring());
        const QString name = QString::fromStdString(m_override->meta.name);
        openWorkbench(folder, name.isEmpty() ? QString::fromStdWString(m_overrideFolder.filename().wstring()) : name);
        return {};
    }
    if (row.profileId.isEmpty()) {
        return tr("Choose a profile first, then edit its logic");
    }
    const QString id = row.profileId;
    showLogicIn(serial, id);
    return {};
}

bool DeviceManager::showLogicIn(const QString& serial, const QString& id)
{
    if (!editProfile(serial, id)) {
        return false;
    }
    const int index = indexOf(serial);
    MirrorWindow* mirror = index >= 0 ? m_rows[static_cast<std::size_t>(index)].mirror : nullptr;
    ProfileEditor* editor = mirror != nullptr ? mirror->editor() : nullptr;
    return editor != nullptr && editor->showLogic(true);
}

void DeviceManager::attachLogic(const QString& serial, ProfileEditor& editor, const QString& folder, const QString& name)
{
    const int index = indexOf(serial);
    MirrorWindow* mirror = index >= 0 ? m_rows[static_cast<std::size_t>(index)].mirror : nullptr;
    if (mirror == nullptr) {
        return;
    }
    for (const Row& other : m_rows) {
        if (other.serial == serial || other.mirror == nullptr || other.mirror->editor() == nullptr) {
            continue;
        }
        if (const auto* bench = qobject_cast<ScriptWorkbench*>(other.mirror->editor()->logic());
            bench != nullptr && sameFolder(bench->folder(), folder)) {
            mirror->showToast(tr("The logic of %1 is open in the editor of %2").arg(name, other.name()),
                              QStringLiteral("info"));
            return;
        }
    }
    if (const auto found = m_workbenches.find(folderKey(folder)); found != m_workbenches.end()) {
        auto* window = qobject_cast<QQuickWindow*>(found->second.data());
        const auto* open =
            window != nullptr ? qobject_cast<ScriptWorkbench*>(window->property("bench").value<QObject*>()) : nullptr;
        if (open != nullptr && open->dirtyCount() > 0) {
            window->setWindowStates(window->windowStates() & ~Qt::WindowMinimized);
            window->show();
            window->raise();
            window->requestActivate();
            mirror->showToast(tr("The logic of %1 has changes that are not saved in another window. Save or close "
                                 "them there first")
                                  .arg(name),
                              QStringLiteral("triangle-alert"));
            return;
        }
        QObject* closing = found->second.data();
        m_workbenches.erase(found);
        delete closing;
    }
    auto* bench = new ScriptWorkbench(*this, folder, name);
    bench->pinPhone(serial);
    editor.attachLogic(bench);
    qInfo("logic view opened for %s on %s", qUtf8Printable(folder),
          qUtf8Printable(m_rows[static_cast<std::size_t>(index)].name()));
}

ScriptWorkbench* DeviceManager::openWorkbench(const QString& folder, const QString& name,
                                             const std::function<void(ScriptWorkbench&)>& prepare)
{
    for (const Row& row : m_rows) {
        if (row.mirror == nullptr || row.mirror->editor() == nullptr) {
            continue;
        }
        if (auto* bench = qobject_cast<ScriptWorkbench*>(row.mirror->editor()->logic());
            bench != nullptr && sameFolder(bench->folder(), folder)) {
            const QString serial = row.serial;
            ProfileEditor* editor = row.mirror->editor();
            showWindow(serial);
            editor->showLogic(true);
            return bench;
        }
    }
    const QString key = folderKey(folder);
    if (const auto found = m_workbenches.find(key); found != m_workbenches.end()) {
        if (auto* window = qobject_cast<QQuickWindow*>(found->second.data())) {
            window->setWindowStates(window->windowStates() & ~Qt::WindowMinimized);
            window->show();
            window->raise();
            window->requestActivate();
            return qobject_cast<ScriptWorkbench*>(window->property("bench").value<QObject*>());
        }
        m_workbenches.erase(found);
    }
    QQmlEngine* engine = m_engine.data();
    if (engine == nullptr) {
        return nullptr;
    }
    auto* bench = new ScriptWorkbench(*this, folder, name);
    if (prepare) {
        prepare(*bench);
    }
    QQuickWindow* window = bench->openWindow(*engine);
    if (window == nullptr) {
        delete bench;
        return nullptr;
    }
    if (m_graphics != nullptr) {
        m_graphics->share(*window);
    }
    releaseMemoryAfter(*window);
    qInfo("script workbench opened for %s", qUtf8Printable(folder));
    m_workbenches.emplace(key, window);
    connect(window, &QWindow::visibleChanged, this, [this, key](bool shown) {
        const auto found = m_workbenches.find(key);
        if (shown || found == m_workbenches.end()) {
            return;
        }
        if (QObject* closed = found->second.data()) {
            closed->deleteLater();
        }
        m_workbenches.erase(found);
    });
    window->raise();
    window->requestActivate();
    return bench;
}

void DeviceManager::setComponentLibrary(ComponentLibrary* components)
{
    m_components = components;
    if (m_components == nullptr) {
        return;
    }
    connect(m_components, &ComponentLibrary::componentSaved, this, [this](const QString& ref) {
        for (Row& row : m_rows) {
            if (row.logic == nullptr || row.logic->source().profile == nullptr) {
                continue;
            }
            const auto& controls = row.logic->source().profile->controls;
            if (std::ranges::any_of(controls, [&](const input::Control& control) {
                    return m_components->extends(QString::fromStdString(input::componentRef(control)), ref);
                })) {
                row.logic->reload();
            }
        }
    });
}

void DeviceManager::setTaskLibrary(TaskLibrary* tasks)
{
    m_tasks = tasks;
    if (m_tasks != nullptr) {
        m_taskHistory = m_tasks->history();
        while (m_taskHistory.size() > kMostTaskHistory) {
            m_taskHistory.removeLast();
        }
    }
    m_taskClock.setInterval(kTaskClockMs);
    connect(&m_taskClock, &QTimer::timeout, this, &DeviceManager::checkSchedules, Qt::UniqueConnection);
    m_taskClock.start();
    m_scheduleWait.setInterval(kScheduleWaitMs);
    connect(&m_scheduleWait, &QTimer::timeout, this, &DeviceManager::advanceScheduledStarts, Qt::UniqueConnection);
}

QVariantList DeviceManager::taskRuns() const
{
    QVariantList runs;
    for (const auto& run : m_taskRuns) {
        if (run->ended) {
            continue;
        }
        runs.append(QVariantMap{{QStringLiteral("number"), run->number},
                                {QStringLiteral("task"), run->task},
                                {QStringLiteral("serial"), run->serial},
                                {QStringLiteral("name"), run->name},
                                {QStringLiteral("phone"), run->phone},
                                {QStringLiteral("progress"), run->progress},
                                {QStringLiteral("startedMs"), run->started.toMSecsSinceEpoch()}});
    }
    return runs;
}

bool DeviceManager::taskRunning(const QString& id, const QString& serial) const
{
    return std::ranges::any_of(m_taskRuns, [&](const auto& run) { return run->task == id && run->serial == serial; });
}

QString DeviceManager::startTask(const QString& id, const QString& serial)
{
    return launchTask(id, serial, 0);
}

QString DeviceManager::launchTask(const QString& id, const QString& serial, int attempt, bool scheduled, bool woke)
{
    const TaskEntry* entry = m_tasks != nullptr ? m_tasks->find(id) : nullptr;
    if (entry == nullptr) {
        return tr("This task is no longer there");
    }
    if (taskRunning(id, serial)) {
        return {};
    }
    const int index = indexOf(serial);
    const std::shared_ptr<ControlChannel> channel =
        index >= 0 && m_rows[static_cast<std::size_t>(index)].mirror != nullptr
            ? channelOf(m_rows[static_cast<std::size_t>(index)])
            : nullptr;
    if (channel == nullptr || m_input == nullptr) {
        return tr("%1 is not mirroring. Mirror it to run the task")
            .arg(index >= 0 ? m_rows[static_cast<std::size_t>(index)].name() : serial);
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (entry->wake && row.screenOff) {
        setScreenOff(serial, false);
    }
    LogicSource source;
    source.folder = std::filesystem::path(entry->folder.toStdWString());
    source.entry = TaskLibrary::entryName().toStdString();
    source.profile = effectiveProfile(row);
    source.id = QStringLiteral("task-") + id;
    source.name = entry->name;
    source.device = row.name();
    source.deviceKey = logicKeyOf(row);
    source.serial = row.serial;
    source.snapshots = shotsOf(row);
    source.task = true;
    source.taskParams = m_tasks->paramsJson(id).toStdString();
    if (!entry->extensionId.isEmpty()) {
        source.extension = entry->extensionId;
        if (m_extensions != nullptr) {
            source.permissions = m_extensions->grantedPermissions(entry->extensionId.toStdString());
        }
    }
    if (entry->extensionId.isEmpty()) {
        source.inputGrant = {.reader = InputReader::Task, .id = id};
    }
    const quint64 number = ++m_lastTaskRun;
    source.gestureOffset = (number % kTaskGestureSlots + 1) << kTaskGestureShift;
    auto run = std::make_unique<TaskRun>();
    run->number = number;
    run->task = id;
    run->serial = serial;
    run->name = entry->name;
    run->phone = row.name();
    run->started = QDateTime::currentDateTime();
    run->attempt = attempt;
    run->scheduled = scheduled;
    run->woke = woke;
    run->runtime = std::make_unique<ScriptRuntime>(
        std::move(source),
        std::make_shared<TaskLane>(std::make_shared<ControllerLane>(*m_input, row.serial, channel)), m_scriptStore);
    run->runtime->setDevice(deviceInfoOf(row));
    run->runtime->setFrameRate(row.refreshHz > 0.0 ? row.refreshHz : 60.0);
    run->runtime->setKeymapOn(keymapOf(row));
    run->runtime->setForeground(row.foregroundApp, row.foregroundGame);
    run->runtime->setFocused(row.focused);
    const auto [profileId, profileShown] = profileInfoOf(row);
    run->runtime->setProfileFact(profileId, profileShown);
    wireTask(*run);
    qInfo("task %s on %s: started", qUtf8Printable(run->name), qUtf8Printable(run->phone));
    m_taskRuns.push_back(std::move(run));
    emit taskRunsChanged();
    return {};
}

void DeviceManager::wireTask(TaskRun& run)
{
    const quint64 number = run.number;
    const quint64 offset = number << kTaskIdShift;
    const QString serial = run.serial;
    ScriptRuntime* runtime = run.runtime.get();
    connect(runtime, &ScriptRuntime::ended, this,
            [this, number](const QString& result, const QString& message) { endTask(number, result, message); },
            Qt::QueuedConnection);
    connect(runtime, &ScriptRuntime::progressed, this, [this, number](const QString& text) {
        if (TaskRun* found = taskRun(number); found != nullptr && found->progress != text) {
            found->progress = text;
            emit taskRunsChanged();
        }
    });
    connect(runtime, &ScriptRuntime::toast, this, [this, serial](const QString& text) {
        if (MirrorWindow* mirror = mirrorOf(serial)) {
            mirror->showToast(text, QStringLiteral("info"));
        }
    });
    connect(runtime, &ScriptRuntime::prompt, this, [this, serial, offset](quint64 request, const QVariantMap& shown) {
        if (MirrorWindow* mirror = mirrorOf(serial)) {
            mirror->showPrompt(request + offset, shown);
        }
    });
    connect(runtime, &ScriptRuntime::promptWithdrawn, this, [this, serial, offset](quint64 request) {
        if (MirrorWindow* mirror = mirrorOf(serial)) {
            mirror->withdrawPrompt(request + offset);
        }
    });
    connect(runtime, &ScriptRuntime::drawn, this,
            [this, serial, number, offset](quint64 id, const QVariantMap& drawing) {
                if (TaskRun* found = taskRun(number)) {
                    found->drawings.insert(id + offset);
                }
                if (MirrorWindow* mirror = mirrorOf(serial)) {
                    mirror->draw(id + offset, drawing);
                }
            });
    connect(runtime, &ScriptRuntime::erased, this, [this, serial, number, offset](quint64 id) {
        if (TaskRun* found = taskRun(number)) {
            found->drawings.erase(id + offset);
        }
        if (MirrorWindow* mirror = mirrorOf(serial)) {
            mirror->erase(id + offset);
        }
    });
    connect(runtime, &ScriptRuntime::drawingsCleared, this, [this, serial, number] {
        TaskRun* found = taskRun(number);
        if (found == nullptr) {
            return;
        }
        const std::set<quint64> drawings = std::exchange(found->drawings, {});
        if (MirrorWindow* mirror = mirrorOf(serial)) {
            for (const quint64 id : drawings) {
                mirror->erase(id);
            }
        }
    });
    connect(runtime, &ScriptRuntime::appRequested, this, [this, serial, number](const QString& request, const QString& json) {
        logicRequest(serial, QStringLiteral("task:%1").arg(number), request, json);
    });
    connect(runtime, &QObject::destroyed, this, [this, serial, number] {
        if (MirrorWindow* mirror = mirrorOf(serial)) {
            mirror->clearScriptProgress(QStringLiteral("task:%1").arg(number));
        }
    });
    connect(runtime, &ScriptRuntime::saveWanted, this,
            [runtime](quint64 request, const QVariantList&, const std::shared_ptr<const input::Profile>&) {
                runtime->answer(request, QStringLiteral("false"));
            });
    connect(runtime, &ScriptRuntime::clipboardWanted, this,
            [this, serial, runtime](quint64 request) { readClipboardFor(serial, *runtime, request); });
    connect(runtime, &ScriptRuntime::clipboardGiven, this,
            [this, serial, runtime](quint64 request, const QString& text) {
                writeClipboardFor(serial, *runtime, request, text);
            });
    connect(runtime, &ScriptRuntime::inputWanted, this,
            [this, serial, runtime, offset] { askToRead(serial, *runtime, offset); });
    connect(runtime, &QObject::destroyed, this, [this, runtime] { dropReadAsk(runtime); });
    connect(runtime, &ScriptRuntime::reported, this, [this, serial, number](const LogicReport& report) {
        m_logicConsole.append(serial, report);
        emit logicReported(serial, report);
        if (TaskRun* found = taskRun(number); found != nullptr && found->reported) {
            found->reported(report);
        }
    });
}

DeviceManager::TaskRun* DeviceManager::taskRun(quint64 number)
{
    const auto found = std::ranges::find(m_taskRuns, number, [](const auto& run) { return run->number; });
    return found != m_taskRuns.end() ? found->get() : nullptr;
}

void DeviceManager::endTask(quint64 number, const QString& result, const QString& message)
{
    const auto found = std::ranges::find(m_taskRuns, number, [](const auto& run) { return run->number; });
    if (found == m_taskRuns.end()) {
        return;
    }
    std::unique_ptr<TaskRun> run = std::move(*found);
    m_taskRuns.erase(found);
    if (run->ended) {
        qInfo("script %s on %s: %s%s%s", qUtf8Printable(run->name), qUtf8Printable(run->phone), qUtf8Printable(result),
              message.isEmpty() ? "" : ": ", qUtf8Printable(message));
        const std::function<void(const QString&, const QString&)> ended = std::move(run->ended);
        run.reset();
        ended(result, message);
        return;
    }
    const TaskEntry* entry = m_tasks != nullptr ? m_tasks->find(run->task) : nullptr;
    const bool retry = result == u"failed" && entry != nullptr && entry->onError == u"retry" && run->attempt == 0;
    qInfo("task %s on %s: %s%s%s", qUtf8Printable(run->name), qUtf8Printable(run->phone), qUtf8Printable(result),
          message.isEmpty() ? "" : ": ", qUtf8Printable(message));
    recordTaskRun(QVariantMap{{QStringLiteral("task"), run->task},
                              {QStringLiteral("serial"), run->serial},
                              {QStringLiteral("name"), run->name},
                              {QStringLiteral("phone"), run->phone},
                              {QStringLiteral("result"), result},
                              {QStringLiteral("message"), message},
                              {QStringLiteral("retry"), retry},
                              {QStringLiteral("whenMs"), run->started.toMSecsSinceEpoch()},
                              {QStringLiteral("durationMs"), run->started.msecsTo(QDateTime::currentDateTime())}});
    if (result == u"failed") {
        if (MirrorWindow* mirror = mirrorOf(run->serial)) {
            mirror->showToast(tr("Task %1 failed: %2").arg(run->name, message), QStringLiteral("triangle-alert"));
        }
    }
    const QString task = run->task;
    const QString serial = run->serial;
    const QString name = run->name;
    const QString phone = run->phone;
    const int attempt = run->attempt;
    const bool scheduled = run->scheduled;
    const bool woke = run->woke;
    const bool sleepAfter = entry != nullptr && entry->sleepAfter;
    run.reset();
    emit taskRunsChanged();
    QString reason = message;
    if (retry) {
        const QString problem = launchTask(task, serial, attempt + 1, scheduled, woke);
        if (problem.isEmpty()) {
            return;
        }
        reason = problem;
    } else if (result == u"done" && sleepAfter) {
        setScreenOff(serial, true);
    }
    if (scheduled) {
        noticeTaskEnd(task, name, phone, result, reason);
    }
    if (sleepsWhenDone(woke, runsOn(serial))) {
        qInfo("task %s on %s: putting the phone back to sleep and closing the connection it opened",
              qUtf8Printable(name), qUtf8Printable(phone));
        pressKey(serial, kKeySleep);
        disconnectDevice(serial);
    }
}

void DeviceManager::stopTask(const QString& id, const QString& serial)
{
    std::vector<quint64> stopping;
    for (const auto& run : m_taskRuns) {
        if (run->task == id && (serial.isEmpty() || run->serial == serial)) {
            stopping.push_back(run->number);
        }
    }
    for (const quint64 number : stopping) {
        endTask(number, QStringLiteral("stopped"), {});
    }
}

void DeviceManager::stopTasksOn(const QString& serial, const QString& result)
{
    std::vector<quint64> stopping;
    for (const auto& run : m_taskRuns) {
        if (run->serial == serial) {
            stopping.push_back(run->number);
        }
    }
    for (const quint64 number : stopping) {
        endTask(number, result, {});
    }
}

void DeviceManager::checkSchedules()
{
    if (m_tasks == nullptr) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime previous = m_lastScheduleCheck.isValid() ? m_lastScheduleCheck : now;
    m_lastScheduleCheck = now;
    std::vector<std::pair<TaskEntry, QDateTime>> due;
    std::vector<std::pair<TaskEntry, QDateTime>> missed;
    for (const TaskEntry& entry : m_tasks->entries()) {
        const ScheduleCheck check = checkSchedule(entry, previous, now);
        if (check.verdict == ScheduleVerdict::Due) {
            due.emplace_back(entry, check.at);
        } else if (check.verdict == ScheduleVerdict::Missed) {
            missed.emplace_back(entry, check.at);
        }
    }
    QStringList missedNames;
    for (const auto& [entry, at] : missed) {
        m_tasks->markScheduled(entry.id, at.date().toString(Qt::ISODate));
        QStringList phones;
        for (const QString& serial : entry.phones) {
            const int index = indexOf(serial);
            phones.append(index >= 0 ? m_rows[static_cast<std::size_t>(index)].name() : serial);
        }
        const QString startText = at.time().toString(QStringLiteral("HH:mm"));
        qInfo("task %s: its start at %s was missed while the computer was asleep", qUtf8Printable(entry.name),
              qUtf8Printable(startText));
        recordTaskRun(QVariantMap{{QStringLiteral("task"), entry.id},
                                  {QStringLiteral("serial"), QString()},
                                  {QStringLiteral("name"), entry.name},
                                  {QStringLiteral("phone"), phones.join(QStringLiteral(", "))},
                                  {QStringLiteral("result"), QStringLiteral("missed")},
                                  {QStringLiteral("message"), QString()},
                                  {QStringLiteral("whenMs"), at.toMSecsSinceEpoch()},
                                  {QStringLiteral("durationMs"), 0}});
        missedNames.append(tr("%1 (%2)").arg(entry.name, startText));
    }
    if (!missedNames.isEmpty()) {
        showTaskNotice(missed.front().first.id, tr("Missed while the computer was asleep"),
                       missedNames.join(tr(", ")));
    }
    for (const auto& [entry, at] : due) {
        m_tasks->markScheduled(entry.id, at.date().toString(Qt::ISODate));
        if (entry.phones.isEmpty()) {
            const QString problem = tr("No phone is ticked for this task");
            recordTaskRun(QVariantMap{{QStringLiteral("task"), entry.id},
                                      {QStringLiteral("serial"), QString()},
                                      {QStringLiteral("name"), entry.name},
                                      {QStringLiteral("phone"), QString()},
                                      {QStringLiteral("result"), QStringLiteral("failed")},
                                      {QStringLiteral("message"), problem},
                                      {QStringLiteral("whenMs"), now.toMSecsSinceEpoch()},
                                      {QStringLiteral("durationMs"), 0}});
            showTaskNotice(entry.id, tr("Task failed"), tr("%1 did not start: %2").arg(entry.name, problem));
            continue;
        }
        for (const QString& serial : entry.phones) {
            queueScheduledStart(entry, serial);
        }
    }
    advanceScheduledStarts();
}

void DeviceManager::queueScheduledStart(const TaskEntry& entry, const QString& serial)
{
    const bool waiting = std::ranges::any_of(m_scheduledStarts, [&](const ScheduledStart& start) {
        return start.task == entry.id && start.serial == serial;
    });
    if (waiting || taskRunning(entry.id, serial)) {
        return;
    }
    const int index = indexOf(serial);
    if (index >= 0 && m_rows[static_cast<std::size_t>(index)].mirror != nullptr) {
        startScheduled(entry.id, serial, false);
        return;
    }
    qInfo("task %s: connecting %s for its scheduled start", qUtf8Printable(entry.name), qUtf8Printable(serial));
    m_scheduledStarts.push_back(ScheduledStart{
        .task = entry.id,
        .serial = serial,
        .deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kScheduledConnectSeconds)});
    const bool present = index >= 0 && m_rows[static_cast<std::size_t>(index)].listed &&
                         m_rows[static_cast<std::size_t>(index)].state == adb::DeviceState::Device;
    if (!present && isWireless(serial) && !serial.contains(u"._adb-tls-connect._tcp")) {
        connectWireless(serial);
    }
    if (!m_scheduleWait.isActive()) {
        m_scheduleWait.start();
    }
}

void DeviceManager::advanceScheduledStarts()
{
    const auto now = std::chrono::steady_clock::now();
    std::vector<ScheduledStart> waiting;
    std::vector<ScheduledStart> ready;
    std::vector<ScheduledStart> lost;
    std::vector<QString> toConnect;
    for (ScheduledStart start : m_scheduledStarts) {
        if (m_tasks == nullptr || m_tasks->find(start.task) == nullptr) {
            continue;
        }
        const int index = indexOf(start.serial);
        if (index >= 0 && m_rows[static_cast<std::size_t>(index)].mirror != nullptr &&
            channelOf(m_rows[static_cast<std::size_t>(index)]) != nullptr) {
            ready.push_back(start);
            continue;
        }
        if (now >= start.deadline) {
            lost.push_back(start);
            continue;
        }
        if (index >= 0 && canConnect(m_rows[static_cast<std::size_t>(index)]) &&
            start.tries < kScheduledConnectTries) {
            ++start.tries;
            toConnect.push_back(start.serial);
        }
        waiting.push_back(start);
    }
    m_scheduledStarts = std::move(waiting);
    if (m_scheduledStarts.empty()) {
        m_scheduleWait.stop();
    }
    for (const QString& serial : toConnect) {
        connectDevice(serial);
    }
    for (const ScheduledStart& start : ready) {
        startScheduled(start.task, start.serial, start.tries > 0);
    }
    for (const ScheduledStart& start : lost) {
        scheduledUnreachable(start);
    }
}

void DeviceManager::startScheduled(const QString& task, const QString& serial, bool connected)
{
    const TaskEntry* entry = m_tasks != nullptr ? m_tasks->find(task) : nullptr;
    if (entry == nullptr || taskRunning(task, serial)) {
        return;
    }
    const QString name = entry->name;
    const ScreenPlan plan = screenPlanFor(true, connected);
    if (plan.wake) {
        pressKey(serial, kKeyWakeup);
    }
    const QString problem = launchTask(task, serial, 0, true, plan.sleepAfter);
    if (problem.isEmpty()) {
        return;
    }
    const int index = indexOf(serial);
    const QString phone = index >= 0 ? m_rows[static_cast<std::size_t>(index)].name() : serial;
    recordTaskRun(QVariantMap{{QStringLiteral("task"), task},
                              {QStringLiteral("serial"), serial},
                              {QStringLiteral("name"), name},
                              {QStringLiteral("phone"), phone},
                              {QStringLiteral("result"), QStringLiteral("failed")},
                              {QStringLiteral("message"), problem},
                              {QStringLiteral("whenMs"), QDateTime::currentMSecsSinceEpoch()},
                              {QStringLiteral("durationMs"), 0}});
    noticeTaskEnd(task, name, phone, QStringLiteral("failed"), problem);
    if (sleepsWhenDone(plan.sleepAfter, runsOn(serial))) {
        pressKey(serial, kKeySleep);
        disconnectDevice(serial);
    }
}

void DeviceManager::scheduledUnreachable(const ScheduledStart& start)
{
    const TaskEntry* entry = m_tasks != nullptr ? m_tasks->find(start.task) : nullptr;
    if (entry == nullptr) {
        return;
    }
    const int index = indexOf(start.serial);
    const QString phone = index >= 0 ? m_rows[static_cast<std::size_t>(index)].name() : start.serial;
    qInfo("task %s: %s did not connect within %d seconds", qUtf8Printable(entry->name), qUtf8Printable(phone),
          kScheduledConnectSeconds);
    const QString reason = tr("It did not connect within %1 seconds").arg(kScheduledConnectSeconds);
    recordTaskRun(QVariantMap{{QStringLiteral("task"), entry->id},
                              {QStringLiteral("serial"), start.serial},
                              {QStringLiteral("name"), entry->name},
                              {QStringLiteral("phone"), phone},
                              {QStringLiteral("result"), QStringLiteral("unreachable")},
                              {QStringLiteral("message"), reason},
                              {QStringLiteral("whenMs"), QDateTime::currentMSecsSinceEpoch()},
                              {QStringLiteral("durationMs"), 0}});
    showTaskNotice(entry->id, tr("Task did not start"), tr("%1 could not connect to %2").arg(entry->name, phone));
}

void DeviceManager::connectWireless(const QString& address, bool afterRestart)
{
    if (m_wirelessCalls.contains(address)) {
        return;
    }
    m_wirelessCalls.emplace(address, std::jthread([this, address, afterRestart, client = m_adb](
                                                      const std::stop_token& stop) {
        const adb::AdbResult<std::string> serial = client.connect(address.toStdString(), stop);
        if (stop.stop_requested()) {
            return;
        }
        QMetaObject::invokeMethod(
            this,
            [this, address, afterRestart, connected = serial.has_value()] {
                m_wirelessCalls.erase(address);
                if (!afterRestart) {
                    qInfo("adb connect %s for a scheduled task: %s", qUtf8Printable(address),
                          connected ? "connected" : "did not connect");
                    return;
                }
                qInfo("adb connect %s after the adb server restarted: %s", qUtf8Printable(address),
                      connected ? "connected" : "did not connect");
                if (MirrorWindow* mirror = mirrorOf(address); mirror != nullptr && connected) {
                    mirror->showToast(tr("The adb server restarted; reconnected"), QStringLiteral("plug"));
                }
            },
            Qt::QueuedConnection);
    }));
}

void DeviceManager::recordTaskRun(QVariantMap entry)
{
    m_taskHistory.prepend(std::move(entry));
    while (m_taskHistory.size() > kMostTaskHistory) {
        m_taskHistory.removeLast();
    }
    if (m_tasks != nullptr) {
        m_tasks->saveHistory(m_taskHistory);
    }
    emit taskRunsChanged();
}

void DeviceManager::noticeTaskEnd(const QString& task, const QString& name, const QString& phone,
                                  const QString& result, const QString& message)
{
    if (result == u"done") {
        showTaskNotice(task, tr("Task done"), tr("%1 finished on %2").arg(name, phone));
        return;
    }
    if (result != u"failed" && result != u"disconnected") {
        return;
    }
    const QString reason = result == u"disconnected" ? tr("The phone disconnected") : message;
    showTaskNotice(task, tr("Task failed"),
                   reason.isEmpty() ? tr("%1 failed on %2").arg(name, phone)
                                    : tr("%1 failed on %2: %3").arg(name, phone, reason));
}

int DeviceManager::runsOn(const QString& serial) const
{
    return static_cast<int>(
        std::ranges::count_if(m_taskRuns, [&serial](const auto& run) { return run->serial == serial; }));
}

QString DeviceManager::openComponent(const QString& ref)
{
    const ComponentEntry* entry = m_components != nullptr ? m_components->find(ref) : nullptr;
    if (entry == nullptr) {
        return tr("This component is no longer there");
    }
    const bool locked = entry->example;
    ScriptWorkbench* bench = openWorkbench(entry->folder, entry->name,
                                           [&ref, locked](ScriptWorkbench& fresh) { fresh.setComponent(ref, locked); });
    if (bench == nullptr) {
        return tr("The logic of %1 cannot be opened").arg(entry->name);
    }
    return {};
}

QString DeviceManager::openTaskLogic(const QString& id)
{
    const TaskEntry* entry = m_tasks != nullptr ? m_tasks->find(id) : nullptr;
    if (entry == nullptr) {
        return tr("This task is no longer there");
    }
    if (entry->readOnly) {
        return tr("%1 comes with an extension: copy it to your tasks to change its logic").arg(entry->name);
    }
    ScriptWorkbench* bench = openWorkbench(entry->folder, entry->name);
    if (bench == nullptr) {
        return tr("The logic of %1 cannot be opened").arg(entry->name);
    }
    bench->setTask(id);
    return {};
}

bool DeviceManager::syncable(const Row& row)
{
    return row.mirror != nullptr && row.host.isEmpty() && channelOf(row) != nullptr;
}

QVariantList DeviceManager::syncCandidates(const QString& master) const
{
    QVariantList candidates;
    for (const Row& row : m_rows) {
        if (row.serial == master || !syncable(row)) {
            continue;
        }
        const QString leader = m_sync.masterOf(row.serial);
        QString note;
        if (!leader.isEmpty() && leader != master) {
            const int at = indexOf(leader);
            note = tr("Following %1").arg(at >= 0 ? m_rows[static_cast<std::size_t>(at)].name() : leader);
        } else if (m_sync.leads(row.serial)) {
            note = tr("Leads its own sync");
        }
        candidates.append(QVariantMap{{QStringLiteral("serial"), row.serial},
                                      {QStringLiteral("name"), row.name()},
                                      {QStringLiteral("checked"), leader == master},
                                      {QStringLiteral("enabled"), note.isEmpty()},
                                      {QStringLiteral("note"), note}});
    }
    return candidates;
}

QString DeviceManager::setSyncFollowers(const QString& master, const QStringList& followers)
{
    const int index = indexOf(master);
    if (index < 0 || !syncable(m_rows[static_cast<std::size_t>(index)])) {
        return tr("This phone is not mirroring");
    }
    if (const QString leader = m_sync.masterOf(master); !leader.isEmpty()) {
        const int at = indexOf(leader);
        return tr("This phone follows %1, so other phones cannot follow it")
            .arg(at >= 0 ? m_rows[static_cast<std::size_t>(at)].name() : leader);
    }
    QStringList usable;
    for (const QString& serial : followers) {
        const int at = indexOf(serial);
        if (at >= 0 && syncable(m_rows[static_cast<std::size_t>(at)])) {
            usable.append(serial);
        }
    }
    if (!m_sync.setFollowers(master, usable)) {
        return {};
    }
    QStringList names;
    for (const QString& serial : m_sync.followersOf(master)) {
        const int at = indexOf(serial);
        names.append(at >= 0 ? m_rows[static_cast<std::size_t>(at)].name() : serial);
    }
    const QString name = m_rows[static_cast<std::size_t>(index)].name();
    if (names.isEmpty()) {
        qInfo("sync: %s stopped leading", qUtf8Printable(name));
    } else {
        qInfo("sync: %s leads %s", qUtf8Printable(name), qUtf8Printable(names.join(QStringLiteral(", "))));
    }
    refreshSync();
    return {};
}

void DeviceManager::stopSync(const QString& master)
{
    if (!m_sync.setFollowers(master, {})) {
        return;
    }
    const int index = indexOf(master);
    qInfo("sync: %s stopped leading",
          qUtf8Printable(index >= 0 ? m_rows[static_cast<std::size_t>(index)].name() : master));
    refreshSync();
}

void DeviceManager::refreshSync()
{
    if (m_input == nullptr || m_batch == nullptr) {
        return;
    }
    QStringList present;
    for (const Row& row : m_rows) {
        if (syncable(row)) {
            present.append(row.serial);
        }
    }
    if (m_sync.keepOnly(present)) {
        qInfo("sync: phones that stopped mirroring left their sync");
    }
    pushFollowers();
    for (Row& row : m_rows) {
        if (row.mirror == nullptr) {
            continue;
        }
        const SyncGroup* group = m_sync.groupOf(row.serial);
        if (group == nullptr) {
            row.mirror->setSync({}, -1, 0, {});
            continue;
        }
        const bool leads = group->master == row.serial;
        const int leader = leads ? -1 : indexOf(group->master);
        row.mirror->setSync(leads ? QStringLiteral("master") : QStringLiteral("follower"), group->tone,
                            static_cast<int>(group->followers.size()),
                            leader >= 0 ? m_rows[static_cast<std::size_t>(leader)].name() : QString());
    }
    refreshBatchPhones();
    refreshGrid();
}

void DeviceManager::pushFollowers()
{
    std::map<QString, PushedSync> next;
    std::map<QString, std::vector<SyncFollower>> lists;
    for (const SyncGroup& group : m_sync.groups()) {
        const int at = indexOf(group.master);
        if (at < 0) {
            continue;
        }
        PushedSync pushed{.channel = channelOf(m_rows[static_cast<std::size_t>(at)])};
        std::vector<SyncFollower>& list = lists[group.master];
        for (const QString& serial : group.followers) {
            const int follower = indexOf(serial);
            if (follower < 0) {
                continue;
            }
            const Row& row = m_rows[static_cast<std::size_t>(follower)];
            SyncFollower each{.serial = serial, .channel = channelOf(row), .profile = effectiveProfile(row)};
            pushed.followers.emplace_back(each.channel, each.profile);
            list.push_back(std::move(each));
        }
        next.emplace(group.master, std::move(pushed));
    }
    for (const auto& [serial, before] : m_pushedSync) {
        const auto after = next.find(serial);
        if (after == next.end() || after->second.channel != before.channel) {
            m_input->setFollowers(serial, before.channel, {});
        }
    }
    for (const auto& [serial, pushed] : next) {
        const auto before = m_pushedSync.find(serial);
        if (before == m_pushedSync.end() || before->second != pushed) {
            m_input->setFollowers(serial, pushed.channel, lists[serial]);
        }
    }
    m_pushedSync = std::move(next);
}

void DeviceManager::refreshBatchPhones()
{
    std::vector<BatchPhone> phones;
    for (const Row& row : m_rows) {
        if (!carded(row)) {
            continue;
        }
        phones.push_back({.serial = row.serial,
                          .name = row.name(),
                          .online = row.listed && row.state == adb::DeviceState::Device && row.demo == nullptr &&
                                    !row.aoaOnly,
                          .mirroring = row.mirror != nullptr && channelOf(row) != nullptr,
                          .apps = row.controller != nullptr && hasCapability(row, protocol::Welcome::kApps)});
    }
    m_batch->setPhones(std::move(phones));
}

void DeviceManager::forwardScroll(const QString& serial, input::Vec2 position, int horizontal, int vertical)
{
    for (const QString& follower : m_sync.followersOf(serial)) {
        const int at = indexOf(follower);
        const std::shared_ptr<ControlChannel> channel =
            at >= 0 ? channelOf(m_rows[static_cast<std::size_t>(at)]) : nullptr;
        if (channel != nullptr) {
            static_cast<void>(channel->scroll(position, static_cast<std::int16_t>(horizontal),
                                              static_cast<std::int16_t>(vertical)));
        }
    }
}

std::optional<script::DeviceInfo> DeviceManager::deviceInfo(const QString& serial) const
{
    const int index = indexOf(serial);
    if (index < 0) {
        return std::nullopt;
    }
    return deviceInfoOf(m_rows[static_cast<std::size_t>(index)]);
}

std::shared_ptr<media::FrameSnapshots> DeviceManager::snapshotsOf(const QString& serial) const
{
    const int index = indexOf(serial);
    if (index < 0) {
        return nullptr;
    }
    const Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.mirror == nullptr) {
        return nullptr;
    }
    return shotsOf(row);
}

bool DeviceManager::requestScreenshot(const QString& serial, std::function<void(const QImage&)> done)
{
    const int index = indexOf(serial);
    SessionController* controller = index >= 0 ? m_rows[static_cast<std::size_t>(index)].controller : nullptr;
    if (controller == nullptr || controller->showsAppDisplay() || !controller->bulkReady() ||
        (controller->capabilities() & protocol::Welcome::kScreenshot) == 0) {
        return false;
    }
    m_lastScreenshot = m_lastScreenshot % 0xFFFFU + 1;
    const std::uint32_t id = kScreenshotRequests | m_lastScreenshot;
    if (!controller->requestScreenshot(id)) {
        return false;
    }
    while (m_screenshots.size() >= kMostScreenshotWaits) {
        m_screenshots.erase(m_screenshots.begin());
    }
    m_screenshots.insert_or_assign(id, std::move(done));
    return true;
}

void DeviceManager::finishScreenshot(std::uint32_t requestId, const QImage& image)
{
    const auto found = m_screenshots.find(requestId);
    if (found == m_screenshots.end()) {
        return;
    }
    const std::function<void(const QImage&)> done = std::move(found->second);
    m_screenshots.erase(found);
    if (done) {
        done(image);
    }
}

QString DeviceManager::startScriptJob(const QString& serial, ScriptJob job, quint64& number)
{
    const int index = indexOf(serial);
    const std::shared_ptr<ControlChannel> channel =
        index >= 0 && m_rows[static_cast<std::size_t>(index)].mirror != nullptr
            ? channelOf(m_rows[static_cast<std::size_t>(index)])
            : nullptr;
    if (channel == nullptr || m_input == nullptr) {
        return tr("%1 is not mirroring. Mirror it to run the script")
            .arg(index >= 0 ? m_rows[static_cast<std::size_t>(index)].name() : serial);
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    LogicSource source;
    source.folder = job.folder;
    source.entry = job.entry;
    source.profile = effectiveProfile(row);
    source.id = QStringLiteral("mcp");
    source.name = job.name;
    source.device = row.name();
    source.deviceKey = logicKeyOf(row);
    source.serial = row.serial;
    const ProfileEntry* jobProfile =
        m_override == nullptr && m_library != nullptr ? m_library->find(row.profileId) : nullptr;
    if (jobProfile != nullptr && !jobProfile->readOnly() && !jobProfile->folder.empty() &&
        jobProfile->folder == job.folder) {
        source.inputGrant = {.reader = InputReader::Profile, .id = jobProfile->id};
    }
    source.snapshots = shotsOf(row);
    source.task = true;
    source.taskHandler = job.handler;
    number = ++m_lastTaskRun;
    source.gestureOffset = (number % kTaskGestureSlots + 1) << kTaskGestureShift;
    auto run = std::make_unique<TaskRun>();
    run->number = number;
    run->serial = serial;
    run->name = job.name;
    run->phone = row.name();
    run->started = QDateTime::currentDateTime();
    run->reported = std::move(job.reported);
    run->ended = job.ended ? std::move(job.ended) : [](const QString&, const QString&) {};
    run->runtime = std::make_unique<ScriptRuntime>(
        std::move(source),
        std::make_shared<TaskLane>(std::make_shared<ControllerLane>(*m_input, row.serial, channel)), m_scriptStore);
    run->runtime->setDevice(deviceInfoOf(row));
    run->runtime->setFrameRate(row.refreshHz > 0.0 ? row.refreshHz : 60.0);
    run->runtime->setKeymapOn(keymapOf(row));
    run->runtime->setForeground(row.foregroundApp, row.foregroundGame);
    run->runtime->setFocused(row.focused);
    const auto [profileId, profileShown] = profileInfoOf(row);
    run->runtime->setProfileFact(profileId, profileShown);
    wireTask(*run);
    qInfo("script %s on %s: started", qUtf8Printable(run->name), qUtf8Printable(run->phone));
    m_taskRuns.push_back(std::move(run));
    return {};
}

void DeviceManager::stopScriptJob(quint64 number)
{
    if (const TaskRun* run = taskRun(number); run != nullptr && run->ended) {
        endTask(number, QStringLiteral("stopped"), {});
    }
}

void DeviceManager::setScriptStore(std::filesystem::path folder)
{
    m_scriptStore = std::move(folder);
}

void DeviceManager::forceTouchBackend(TouchBackend backend)
{
    m_touchBackend = backend;
}

bool DeviceManager::wantsAoa(const QString& deviceId) const
{
    if (m_touchBackend) {
        return *m_touchBackend == TouchBackend::Aoa;
    }
    return m_settings.effective(deviceId.toStdString()).touch.method == settings::TouchMethod::Aoa;
}

std::optional<Advice> DeviceManager::aoaAdviceOf(const Row& row) const
{
    const bool missing = row.usb && row.usb->driver == platform::UsbDriver::Missing;
    const Advice needsDriver{tr("This phone needs the WinUSB driver before it can be controlled over AOA"),
                             tr("Install it with Zadig as the illustrated guide shows. GameScrcpy notices by itself "
                                "once it is installed")};
    if (!row.aoaOnly) {
        if (missing && wantsAoa(row.deviceId) && !streaming(row)) {
            return Advice{tr("AOA needs the WinUSB driver for this phone"),
                          tr("Until it is installed, touches go the normal way. The illustrated guide shows how to "
                             "install it with Zadig")};
        }
        return std::nullopt;
    }
    if (row.aoa != nullptr && row.session.phase == session::SessionPhase::Failed) {
        switch (row.aoa->failure()) {
        case platform::UsbFailure::NoDriver:
            return needsDriver;
        case platform::UsbFailure::Busy:
            return Advice{tr("Another program is using this phone's USB"),
                          tr("Close programs that use the phone, such as adb or a phone assistant, then try again")};
        case platform::UsbFailure::Gone:
            return Advice{tr("The phone was unplugged"), tr("Plug it in again to control it over AOA")};
        case platform::UsbFailure::NotFound:
            return Advice{tr("The phone is no longer on USB"), tr("Check the cable and plug the phone in again")};
        case platform::UsbFailure::Refused:
        case platform::UsbFailure::None:
            return Advice{tr("The phone did not accept the AOA touch screen"),
                          tr("AOA needs Android 4.1 or later. Unlock the phone and try again")};
        }
    }
    if (missing) {
        return needsDriver;
    }
    return std::nullopt;
}

QString DeviceManager::driverHelpOf(const Row& row) const
{
    const bool missing = row.usb && row.usb->driver == platform::UsbDriver::Missing;
    if (row.aoaOnly) {
        const bool refused = row.aoa != nullptr && row.session.phase == session::SessionPhase::Failed &&
                             row.aoa->failure() == platform::UsbFailure::NoDriver;
        return missing || refused ? aoaHelpSlug() : QString();
    }
    return missing && wantsAoa(row.deviceId) ? aoaHelpSlug() : QString();
}

int DeviceManager::controlTurnOf(const Row& row) const
{
    switch (m_settings.device(row.deviceId.toStdString()).controlOrientation) {
    case settings::ControlOrientation::Portrait:
        return static_cast<int>(ControlTurn::Portrait);
    case settings::ControlOrientation::LandscapeLeft:
        return static_cast<int>(ControlTurn::LandscapeLeft);
    case settings::ControlOrientation::LandscapeRight:
        return static_cast<int>(ControlTurn::LandscapeRight);
    case settings::ControlOrientation::Profile:
        break;
    }
    const std::shared_ptr<const input::Profile> profile = effectiveProfile(row);
    return static_cast<int>(profile != nullptr && profile->meta.orientation == input::Orientation::Landscape
                                ? ControlTurn::LandscapeLeft
                                : ControlTurn::Portrait);
}

QSize DeviceManager::controlCanvasOf(const Row& row, int turn) const
{
    const settings::DeviceSettings device = m_settings.device(row.deviceId.toStdString());
    return controlCanvas(QSize(device.screenWidth, device.screenHeight), static_cast<ControlTurn>(turn));
}

void DeviceManager::rememberScreen(const Row& row)
{
    if (row.deviceId.isEmpty() || row.demo != nullptr || !row.host.isEmpty() || row.screen.isEmpty()) {
        return;
    }
    const QSize natural = naturalScreenOf(row.screen, row.rotation);
    QMetaObject::invokeMethod(this, [this, id = row.deviceId.toStdString(), natural] {
        settings::DeviceSettings device = m_settings.device(id);
        if (device.screenWidth == natural.width() && device.screenHeight == natural.height()) {
            return;
        }
        device.screenWidth = natural.width();
        device.screenHeight = natural.height();
        m_settings.setDevice(id, device);
    }, Qt::QueuedConnection);
}

void DeviceManager::setControlOrientation(const QString& serial, int orientation)
{
    const int index = indexOf(serial);
    if (index < 0 || orientation < 0 || orientation > static_cast<int>(ControlTurn::LandscapeRight)) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (const Row* card = row.capture != nullptr ? &row : lentCardOf(row); card != nullptr) {
        if (orientation == static_cast<int>(ControlTurn::Portrait)) {
            return;
        }
        const QString cardSerial = card->serial;
        const QString cardName = card->name();
        const std::string id = card->deviceId.toStdString();
        settings::DeviceSettings device = m_settings.device(id);
        device.capture.landscape = orientation == static_cast<int>(ControlTurn::LandscapeRight)
                                       ? settings::CaptureLandscape::Right
                                       : settings::CaptureLandscape::Left;
        m_settings.setDevice(id, device);
        const int at = indexOf(serial);
        const int cardAt = indexOf(cardSerial);
        if (at < 0 || cardAt < 0) {
            return;
        }
        if (cardAt == at) {
            applyCaptureGeometry(m_rows[static_cast<std::size_t>(at)]);
        } else {
            applyPictureGeometry(m_rows[static_cast<std::size_t>(at)]);
        }
        qInfo("%s: a landscape picture means the phone is turned to the %s", qUtf8Printable(cardName),
              device.capture.landscape == settings::CaptureLandscape::Right ? "right" : "left");
        rowChanged(at);
        emit mirrorsChanged();
        return;
    }
    const auto turn = static_cast<ControlTurn>(orientation);
    const QSize canvas = controlCanvasOf(row, orientation);
    const int heldBefore = row.rotation;
    if (row.aoa != nullptr && active(row.session.phase)) {
        row.aoa->setOrientation(rotationOf(turn), canvas);
        row.rotation = rotationOf(turn);
        if (row.mirror != nullptr) {
            row.mirror->setNoPicture(canvas, orientation);
        }
        if (row.logic != nullptr) {
            row.logic->setDevice(deviceInfoOf(row));
        }
        if (m_extensionLogic != nullptr) {
            m_extensionLogic->updateSession(row.serial, deviceInfoOf(row), logicKeyOf(row));
        }
    }
    if (row.esp32 != nullptr && active(row.session.phase)) {
        row.esp32->setOrientation(rotationOf(turn), canvas);
        row.rotation = rotationOf(turn);
        if (row.mirror != nullptr) {
            row.mirror->setNoPicture(canvas, orientation);
        }
        if (row.logic != nullptr) {
            row.logic->setDevice(deviceInfoOf(row));
        }
    }
    if (row.rotation != heldBefore) {
        publishPhoneEvent(AppTopic::OrientationChanged, row.serial, row.name(),
                          {{QStringLiteral("rotation"), row.rotation}});
    }
    qInfo("%s is held %s", qUtf8Printable(row.name()),
          turn == ControlTurn::Portrait        ? "upright"
          : turn == ControlTurn::LandscapeLeft ? "turned to the left"
                                               : "turned to the right");
    const std::string id = row.deviceId.toStdString();
    if (!id.empty()) {
        settings::DeviceSettings device = m_settings.device(id);
        device.controlOrientation = turn == ControlTurn::Portrait        ? settings::ControlOrientation::Portrait
                                    : turn == ControlTurn::LandscapeLeft ? settings::ControlOrientation::LandscapeLeft
                                                                         : settings::ControlOrientation::LandscapeRight;
        m_settings.setDevice(id, device);
    }
    if (const int at = indexOf(serial); at >= 0) {
        rowChanged(at);
    }
    emit mirrorsChanged();
    bumpBackgrounds();
}

QStringList DeviceManager::noPictureLimits(const QString& serial) const
{
    const int index = serial.isEmpty() ? -1 : indexOf(serial);
    const bool adb = index >= 0 && (m_rows[static_cast<std::size_t>(index)].controller != nullptr ||
                                    m_rows[static_cast<std::size_t>(index)].demo != nullptr);
    return featuresWithoutPicture(adb);
}

void DeviceManager::connectAoa(Row& row)
{
    if (active(row.session.phase)) {
        return;
    }
    const QString serial = row.serial;
    if (usePicture(row) == settings::PictureSource::Capture) {
        qInfo("controlling %s with the picture of its capture card", qUtf8Printable(row.name()));
        sessionChanged(serial, {session::SessionPhase::Connecting, std::nullopt});
        return;
    }
    if (!row.usb) {
        row.picture.reset();
        return;
    }
    if (row.aoa == nullptr) {
        row.aoa = new AoaSession(row.name(), this);
        connect(row.aoa, &AoaSession::stateChanged, this,
                [this, serial](const session::SessionState& state) { sessionChanged(serial, state); });
        connect(row.aoa, &AoaSession::padLinkChanged, this,
                [this, channel = row.aoa->channel()] { m_input->padLinkChanged(channel); });
    }
    const int turn = controlTurnOf(row);
    qInfo("controlling %s over AOA, without a picture", qUtf8Printable(row.name()));
    const platform::UsbPhone phone = *row.usb;
    row.rotation = rotationOf(static_cast<ControlTurn>(turn));
    row.aoa->start(phone, row.rotation, controlCanvasOf(row, turn));
}

void DeviceManager::warnTouchUnavailable(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.mirror == nullptr || row.controller == nullptr) {
        return;
    }
    if (const QString method = row.controller->extensionTouchProblem(); !method.isEmpty()) {
        row.mirror->showToast(tr("The touch method %1 is not working; touches go the normal way.").arg(method),
                              QStringLiteral("info"));
        return;
    }
    if (row.controller->touchBackend() != TouchBackend::Aoa) {
        row.mirror->showToast(tr("This phone cannot use a UHID touch screen; touches go the normal way."),
                              QStringLiteral("info"));
        return;
    }
    QString text;
    switch (row.controller->aoaFailure()) {
    case platform::UsbFailure::NoDriver:
        text = tr("AOA needs the WinUSB driver for this phone; touches go the normal way.");
        break;
    case platform::UsbFailure::Busy:
        text = tr("Another program is using this phone's USB, so AOA cannot start; touches go the normal way.");
        break;
    case platform::UsbFailure::Gone:
        text = tr("The USB cable for AOA was unplugged; touches go the normal way.");
        break;
    case platform::UsbFailure::Refused:
        text = tr("The phone did not accept the AOA touch screen; touches go the normal way.");
        break;
    case platform::UsbFailure::NotFound:
    case platform::UsbFailure::None:
        text = tr("This phone is not on a USB cable, so AOA cannot be used; touches go the normal way.");
        break;
    }
    if (row.controller->aoaFailure() == platform::UsbFailure::NoDriver) {
        row.toastAction = [this] { emit appCommandRequested(QStringLiteral("app.help"), aoaHelpSlug()); };
        row.toastSecondAction = {};
        row.mirror->showToast(text, QStringLiteral("info"), tr("View the driver guide"));
        return;
    }
    row.mirror->showToast(text, QStringLiteral("info"));
}

void DeviceManager::usbChanged()
{
    const std::vector<platform::UsbPhone> phones =
        m_usb != nullptr ? m_usb->phones() : std::vector<platform::UsbPhone>();
    std::vector<AdbPhone> adb;
    std::vector<std::size_t> adbRows;
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        Row& row = m_rows[i];
        if (row.demo != nullptr || row.aoaOnly || !row.host.isEmpty()) {
            continue;
        }
        row.usb.reset();
        adb.push_back({.serial = row.serial, .deviceId = row.deviceId});
        adbRows.push_back(i);
    }
    const UsbPlan plan = planUsbPhones(phones, adb);
    for (const auto& [phone, at] : plan.attached) {
        m_rows[adbRows[at]].usb = phones[phone];
    }
    std::set<QString> alone;
    m_usbAlone.clear();
    for (const std::size_t phone : plan.alone) {
        const platform::UsbPhone& usb = phones[phone];
        m_usbAlone.push_back(usb);
        const QString serial = QString::fromStdString(usb.serial);
        const QString key = aoaSerialOf(serial);
        alone.insert(key);
        if (const int at = indexOf(key); at >= 0) {
            Row& row = m_rows[static_cast<std::size_t>(at)];
            row.usb = usb;
            row.listed = true;
            continue;
        }
        if (!wantsAoa(serial) && cardIndexOf(serial) < 0) {
            continue;
        }
        const int at = static_cast<int>(m_rows.size());
        beginInsertRows({}, at, at);
        Row row;
        row.serial = key;
        row.aoaOnly = true;
        row.usb = usb;
        row.listed = true;
        row.state = adb::DeviceState::Unknown;
        row.deviceId = serial;
        row.model = QString::fromStdString(usb.name);
        if (const auto known = m_knownNames.find(serial); known != m_knownNames.end()) {
            row.marketName = known->second[0];
            row.manufacturer = known->second[1];
            row.model = known->second[2];
        }
        qInfo("%s is on USB without adb: it can be controlled over AOA (%s)", qUtf8Printable(row.name()),
              usb.driver == platform::UsbDriver::Ready ? "the WinUSB driver is installed" : "it needs the WinUSB driver");
        m_rows.push_back(std::move(row));
        endInsertRows();
    }
    for (int i = static_cast<int>(m_rows.size()) - 1; i >= 0; --i) {
        Row& row = m_rows[static_cast<std::size_t>(i)];
        if (!row.aoaOnly) {
            continue;
        }
        const bool present = alone.contains(row.serial);
        row.listed = present;
        if ((!present || (!wantsAoa(row.deviceId) && cardIndexOf(row.deviceId) < 0)) && !active(row.session.phase)) {
            removeRow(i);
        }
    }
    for (const platform::UsbPhone& phone : phones) {
        const auto known = m_usbDrivers.find(phone.serial);
        const bool installed = known != m_usbDrivers.end() && known->second == platform::UsbDriver::Missing &&
                               phone.driver == platform::UsbDriver::Ready;
        m_usbDrivers[phone.serial] = phone.driver;
        if (!installed) {
            continue;
        }
        QString name = QString::fromStdString(phone.name);
        for (const Row& row : m_rows) {
            if (row.usb && row.usb->serial == phone.serial && carded(row)) {
                name = row.name();
            }
        }
        qInfo("the WinUSB driver for %s is installed: it can be controlled over AOA now", qUtf8Printable(name));
        emit usbDriverReady(name.isEmpty() ? QString::fromStdString(phone.serial) : name);
    }
    if (!m_rows.empty()) {
        emit dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1));
    }
    emit countsChanged();
}

QString DeviceManager::busyText() const
{
    return tr("Not saved: another program is using the file");
}

void DeviceManager::syncInputProfile()
{
    if (m_input == nullptr) {
        return;
    }
    const int index = indexOf(m_input->target());
    if (index < 0) {
        return;
    }
    const Row& row = m_rows[static_cast<std::size_t>(index)];
    const QString key = row.serial + QLatin1Char('\n') + row.profileId + QLatin1Char('\n')
                        + (m_override != nullptr ? QString::fromStdString(m_override->meta.name) : QString());
    const bool revision = key == m_inputProfileKey;
    m_inputProfileKey = key;
    m_input->setProfile(effectiveProfile(row), revision);
}

void DeviceManager::refreshProfiles(const QString& changed)
{
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        Row& row = m_rows[i];
        if (!row.profileId.isEmpty() && m_library != nullptr && (changed.isEmpty() || row.profileId == changed)) {
            std::shared_ptr<const input::Profile> profile = m_library->profile(row.profileId);
            if (profile != row.profile) {
                if (profile == nullptr) {
                    row.profileId.clear();
                }
                row.profile = std::move(profile);
                applyProfile(row);
            }
        }
        if (ProfileEditor* editor = row.mirror != nullptr ? row.mirror->editor() : nullptr;
            editor != nullptr && !changed.isEmpty() && row.editingId == changed && m_library != nullptr &&
            !editor->dirty()) {
            if (const std::shared_ptr<const input::Profile> latest = m_library->profile(changed); latest != nullptr) {
                editor->replaceSaved(*latest);
            }
        }
        if (row.mirror != nullptr) {
            syncLogic(row);
            followApp(row);
            offerStart(row);
        }
        rowChanged(static_cast<int>(i));
    }
    syncInputProfile();
    applyKeymap();
    emit mirrorsChanged();
}

void DeviceManager::setKeyHints(bool shown)
{
    m_settings.setValue(QStringLiteral("keymap.hints"), shown);
}

void DeviceManager::foregroundChanged()
{
    int front = -1;
    const QString gridTarget = gridFront();
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        MirrorWindow* mirror = m_rows[i].mirror;
        const bool active =
            mirror != nullptr && platform::isInFront(static_cast<std::uintptr_t>(mirror->winId()));
        followFocus(m_rows[i], active || (!gridTarget.isEmpty() && m_rows[i].serial == gridTarget));
        if (mirror == nullptr) {
            continue;
        }
        const bool inFront = !mirror->mini() && (!mirror->editing() || mirror->playing()) && active;
        mirror->setForeground(inFront);
        if (inFront) {
            front = static_cast<int>(i);
        }
    }
    if (m_grid != nullptr) {
        m_grid->setForeground(!gridTarget.isEmpty());
    }
    if (front < 0) {
        if (aimAtGrid(gridTarget)) {
            return;
        }
        m_input->setFocused(false);
        return;
    }
    const Row& row = m_rows[static_cast<std::size_t>(front)];
    m_input->setTarget(row.serial, channelOf(row), static_cast<std::uintptr_t>(row.mirror->winId()));
    syncInputProfile();
    m_input->setFocused(!row.prompting);
    if (row.inputBox.typingWanted()) {
        m_input->setTextInput(true);
    }
    applyKeymap();
}

void DeviceManager::followFocus(Row& row, bool focused)
{
    if (row.focused == focused) {
        return;
    }
    row.focused = focused;
    if (row.logic != nullptr) {
        row.logic->setFocused(focused);
    }
    for (const std::unique_ptr<TaskRun>& run : m_taskRuns) {
        if (run->serial == row.serial && run->runtime != nullptr) {
            run->runtime->setFocused(focused);
        }
    }
    forSessionScripts(row.serial, [focused](ScriptRuntime& runtime) { runtime.setFocused(focused); });
    const bool inGrid = m_grid != nullptr && m_grid->holds(row.serial);
    publishPhoneEvent(AppTopic::WindowFocused, row.serial, row.name(),
                      {{QStringLiteral("window"), inGrid ? QStringLiteral("grid") : QStringLiteral("mirror")},
                       {QStringLiteral("focused"), focused}});
}

void DeviceManager::showActionWheel(const QString& control, bool open, int cell)
{
    const int index = indexOf(m_input->target());
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.mirror == nullptr) {
        return;
    }
    QVariantMap wheel{{QStringLiteral("control"), control},
                      {QStringLiteral("open"), open},
                      {QStringLiteral("cell"), cell}};
    if (open) {
        QVariantList cells;
        if (const std::shared_ptr<const input::Profile> profile = effectiveProfile(row)) {
            const auto found = std::ranges::find(profile->controls, control.toStdString(), &input::Control::id);
            const auto* kind = found != profile->controls.end() ? std::get_if<input::ActionWheelControl>(&found->kind)
                                                                : nullptr;
            if (kind != nullptr) {
                for (const input::WheelCell& each : kind->cells) {
                    const std::string_view action = input::kWheelActionNames[static_cast<std::size_t>(each.action)];
                    cells.append(QVariantMap{{QStringLiteral("name"), QString::fromStdString(each.name)},
                                             {QStringLiteral("action"), QString::fromUtf8(action.data(),
                                                                                          static_cast<qsizetype>(action.size()))}});
                }
            }
        }
        wheel.insert(QStringLiteral("cells"), cells);
    }
    row.mirror->setActionWheel(wheel);
    if (m_grid != nullptr && m_grid->holds(row.serial)) {
        m_grid->setActionWheel(row.serial, wheel);
    }
}

void DeviceManager::applyKeymap()
{
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        Row& row = m_rows[i];
        if (row.mirror != nullptr) {
            row.mirror->setKeymap(keymapOf(row));
            row.mirror->setCameraLocked(cameraLockedFor(row.serial));
            row.mirror->setCursorPeek(cameraLockedFor(row.serial) && m_input->cursorPeeking());
        }
        if (row.logic != nullptr) {
            row.logic->setKeymapOn(keymapOf(row));
        }
        const bool keymapOn = keymapOf(row);
        forSessionScripts(row.serial, [keymapOn](ScriptRuntime& runtime) { runtime.setKeymapOn(keymapOn); });
        rowChanged(static_cast<int>(i));
    }
    if (m_grid != nullptr) {
        const QString target = m_grid->target();
        const int index = indexOf(target);
        const Row* aimed = index >= 0 ? &m_rows[static_cast<std::size_t>(index)] : nullptr;
        m_grid->setProfile(aimed != nullptr ? effectiveProfile(*aimed) : nullptr);
        m_grid->setKeymap(aimed != nullptr && keymapOf(*aimed));
        m_grid->setCameraLocked(cameraLockedFor(target));
        m_grid->setCursorPeek(cameraLockedFor(target) && m_input->cursorPeeking());
    }
}

void DeviceManager::releaseInput(const QString& serial)
{
    stopTasksOn(serial, QStringLiteral("disconnected"));
    if (m_extensionLogic != nullptr) {
        m_extensionLogic->endSession(serial);
    }
    if (const int index = indexOf(serial); index >= 0) {
        Row& row = m_rows[static_cast<std::size_t>(index)];
        row.logic.reset();
        if (m_input != nullptr) {
            m_input->forget(channelOf(row));
        }
    }
    if (m_input == nullptr || serial != m_input->target()) {
        return;
    }
    m_input->setTarget({}, nullptr, 0);
}

void DeviceManager::syncCompanion(Row& row)
{
    if (row.controller == nullptr && (noPictureOf(row) || row.aoaOnly)) {
        syncCompanionLan(row);
        pushCompanionSettings(row);
        return;
    }
    const settings::Companion companion = companionSettingsOf(m_settings, row.deviceId);
    const bool chosen = companion.cursor || companion.ime || companion.hints;
    row.companionCursor = companion.cursor;
    if (chosen != row.companionChosen) {
        row.companionChosen = chosen;
        row.companionHeld = false;
        row.companionRetried = false;
        row.loggedCursor.reset();
    }
    const bool wanted = chosen && !row.companionHeld && row.demo == nullptr && row.host.isEmpty() &&
                        row.capture == nullptr && row.mirror != nullptr && !row.transported &&
                        row.session.phase == session::SessionPhase::Streaming;
    if (!wanted) {
        stopCompanion(row);
        return;
    }
    if (row.companion != nullptr) {
        row.companion->setIme(companion.ime);
        row.companion->setCursor(phoneCursorFor(row.aoaOnly, companion.cursor, row.mirror->phoneCursor()));
        pushPhoneHints(row);
        pushCompanionSettings(row);
        return;
    }
    if (std::ranges::any_of(m_endingCompanions,
                            [&row](const EndingCompanion& ending) { return ending.deviceId == row.deviceId; })) {
        return;
    }
    const QString serial = row.serial;
    const quint64 ticket = ++m_lastCompanionTicket;
    auto link =
        std::make_unique<CompanionLink>(m_adb, serial.toStdString(), m_companionApk, companionListener(serial, ticket));
    link->setIme(companion.ime);
    startCompanionLink(row, ticket, std::move(link));
    qInfo("companion on %s: starting the app for%s%s%s", qUtf8Printable(row.name()), companion.ime ? " typing" : "",
          companion.cursor ? " the cursor" : "", companion.hints ? " key hints" : "");
}

CompanionLink::Listener DeviceManager::companionListener(const QString& serial, quint64 ticket)
{
    return [this, serial, ticket](const CompanionNotice& notice) {
        QMetaObject::invokeMethod(
            this, [this, serial, ticket, notice] { companionNotice(serial, ticket, notice); }, Qt::QueuedConnection);
    };
}

void DeviceManager::startCompanionLink(Row& row, quint64 ticket, std::unique_ptr<CompanionLink> link)
{
    row.companionTicket = ticket;
    row.companionOverlay = true;
    row.companionApplied = 0;
    row.companion = std::move(link);
    row.companionCursor = companionSettingsOf(m_settings, row.deviceId).cursor;
    if (row.mirror != nullptr) {
        row.companion->setCursor(phoneCursorFor(noPictureOf(row), row.companionCursor, row.mirror->phoneCursor()));
    }
    pushPhoneHints(row);
    pushCompanionSettings(row);
}

void DeviceManager::stopCompanion(Row& row)
{
    dropInputBox(row);
    if (row.companion == nullptr) {
        return;
    }
    forgetShots(row);
    row.companion->stop();
    m_endingCompanions.push_back(
        {.ticket = row.companionTicket, .deviceId = row.deviceId, .link = std::move(row.companion)});
}

void DeviceManager::syncCompanionLan(Row& row)
{
    const bool wanted = row.mirror != nullptr && noPictureOf(row) && (row.aoa != nullptr || row.esp32 != nullptr) &&
                        !row.deviceId.isEmpty() && row.session.phase == session::SessionPhase::Streaming;
    if (!wanted) {
        stopCompanion(row);
        row.lanToken.clear();
        row.lanPairing.reset();
        if (row.mirror != nullptr) {
            row.mirror->setCompanionQr({});
        }
    } else if (row.companion == nullptr && row.lanToken.empty()) {
        row.lanToken = newLanToken();
        row.lanKey = newLanKey();
    }
    syncLanService();
}

void DeviceManager::syncLanService()
{
    std::vector<LanExpectation> expected;
    for (const Row& row : m_rows) {
        if (!noPictureOf(row) || row.mirror == nullptr || (row.aoa == nullptr && row.esp32 == nullptr) ||
            row.deviceId.isEmpty() || row.session.phase != session::SessionPhase::Streaming) {
            continue;
        }
        const bool connected = row.companion != nullptr;
        const settings::DeviceSettings device = m_settings.device(row.deviceId.toStdString());
        LanExpectation expectation;
        expectation.serial = row.serial.toStdString();
        if (!connected) {
            expectation.token = row.lanToken;
            expectation.qrKey = row.lanKey;
        }
        expectation.paired =
            pairingOf({.phone = device.companionPhone, .key = device.companionKey, .pc = device.companionPc});
        if (expectation.paired) {
            if (std::optional<std::string> sealed = reprotectedLanKey(device.companionKey)) {
                QMetaObject::invokeMethod(
                    this,
                    [this, id = row.deviceId.toStdString(), name = row.name(), plain = device.companionKey,
                     key = std::move(*sealed)] {
                        settings::DeviceSettings stored = m_settings.device(id);
                        if (stored.companionKey != plain) {
                            return;
                        }
                        stored.companionKey = key;
                        m_settings.setDevice(id, stored);
                        qInfo("companion on %s: the pairing key is protected by Windows now", qUtf8Printable(name));
                    },
                    Qt::QueuedConnection);
            }
        }
        expectation.announce = expectation.paired.has_value() && !connected;
        if (!expectation.token.empty() || expectation.paired) {
            expected.push_back(std::move(expectation));
        }
    }
    const bool listen = std::ranges::any_of(
        expected, [](const LanExpectation& expectation) { return !expectation.token.empty() || expectation.announce; });
    if (!listen) {
        m_companionLan.reset();
    } else {
        if (m_companionLan == nullptr || !m_companionLan->listening()) {
            m_companionLan = std::make_unique<CompanionLan>(m_companionApk, [this](LanArrival arrival) {
                auto shared = std::make_shared<LanArrival>(std::move(arrival));
                QMetaObject::invokeMethod(this, [this, shared] { lanArrived(std::move(*shared)); }, Qt::QueuedConnection);
            });
        }
        m_companionLan->expect(std::move(expected));
    }
    for (Row& row : m_rows) {
        if (noPictureOf(row) && row.mirror != nullptr) {
            row.mirror->setCompanionQr(companionQrOf(row));
        }
    }
}

QVariantMap DeviceManager::companionQrOf(const Row& row) const
{
    if (!noPictureOf(row) || row.companion != nullptr || row.lanToken.empty()) {
        return {};
    }
    const std::string address = lanAddress();
    if (address.empty() || m_companionLan == nullptr || !m_companionLan->listening()) {
        return {{QStringLiteral("offline"), true}};
    }
    const QrPicture picture =
        qrPicture(companionQrText(address, m_companionLan->port(), row.lanToken, row.lanKey));
    return {{QStringLiteral("modules"), picture.size},
            {QStringLiteral("path"), QString::fromStdString(picture.path)},
            {QStringLiteral("address"),
             QStringLiteral("%1:%2").arg(QString::fromStdString(address)).arg(m_companionLan->port())}};
}

void DeviceManager::lanArrived(LanArrival arrival)
{
    const int index = indexOf(QString::fromStdString(arrival.serial));
    if (index < 0 || arrival.channel == nullptr) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (!noPictureOf(row) || row.mirror == nullptr) {
        return;
    }
    stopCompanion(row);
    row.lanPairing = arrival.paired;
    const QString serial = row.serial;
    const quint64 ticket = ++m_lastCompanionTicket;
    startCompanionLink(row, ticket,
                       std::make_unique<CompanionLink>(std::move(arrival.channel), arrival.welcome,
                                                       serial.toStdString(), companionListener(serial, ticket)));
    qInfo("companion on %s: %s over the local network", qUtf8Printable(row.name()),
          arrival.paired ? "paired" : "connected again");
    syncLanService();
}

void DeviceManager::companionInputBox(Row& row, const protocol::InputBoxChanged& box)
{
    const bool same = row.inputBox.box() && *row.inputBox.box() == box;
    const TypingChange change = row.inputBox.changed(box);
    if (row.mirror != nullptr) {
        row.mirror->setTypingFollowsPhone(box.shown);
    }
    if (m_input != nullptr) {
        applyTyping(*m_input, row.serial, change);
    }
    if (change != TypingChange::None) {
        qInfo("%s: an input box %s on the phone", qUtf8Printable(row.name()), box.shown ? "opened" : "closed");
    }
    if (same) {
        return;
    }
    const std::string json = inputBoxJson(box);
    if (row.logic != nullptr) {
        row.logic->setInputBox(json);
    }
    for (const std::unique_ptr<TaskRun>& run : m_taskRuns) {
        if (run->serial == row.serial && run->runtime != nullptr) {
            run->runtime->setInputBox(json);
        }
    }
    forSessionScripts(row.serial, [&json](ScriptRuntime& runtime) { runtime.setInputBox(json); });
}

void DeviceManager::dropInputBox(Row& row)
{
    row.enterToCompanion = false;
    if (row.inputBox.shown()) {
        companionInputBox(row, protocol::InputBoxChanged{});
    }
    static_cast<void>(row.inputBox.lost());
}

void DeviceManager::pushPhoneHints(Row& row)
{
    if (row.companion == nullptr || row.mirror == nullptr) {
        return;
    }
    const settings::Companion companion = companionSettingsOf(m_settings, row.deviceId);
    protocol::KeyHintState state = row.mirror->phoneHintState();
    state.shown = state.shown && (row.aoaOnly || row.mirror->noPicture() || companion.hints);
    if (state.faded) {
        state.opacity = static_cast<std::uint8_t>(std::clamp<std::int32_t>(
            companion.hintOpacity, protocol::kMinPhoneHintOpacity, protocol::kMaxPhoneHintOpacity));
    }
    row.companion->setKeyHints(row.mirror->phoneHints());
    row.companion->setKeyHintState(state);
}

void DeviceManager::pushCompanionSettings(Row& row)
{
    if (row.companion == nullptr) {
        return;
    }
    const protocol::ComputerLink link = row.companion->overLan() ? protocol::ComputerLink::Lan
                                        : isWireless(row.serial) ? protocol::ComputerLink::Wireless
                                                                 : protocol::ComputerLink::Usb;
    row.companion->setSettings(companionSnapshot(companionSettingsOf(m_settings, row.deviceId), thisComputer(), link,
                                                 accentColor(m_settings.effective({}).appearance.accent),
                                                 row.companionApplied));
}

void DeviceManager::companionSettingChanged(const QString& serial, const protocol::CompanionSettingChange& change)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    m_rows[static_cast<std::size_t>(index)].companionApplied = change.change;
    const QString deviceId = m_rows[static_cast<std::size_t>(index)].deviceId;
    const QString name = m_rows[static_cast<std::size_t>(index)].name();
    const std::string_view path = companionSettingPath(change.item);
    const std::optional<settings::Value> value = companionSettingValue(change);
    if (!deviceId.isEmpty() && !path.empty() && value) {
        const QVariant stored = std::holds_alternative<bool>(*value)
                                    ? QVariant(std::get<bool>(*value))
                                    : QVariant(static_cast<int>(std::get<std::int64_t>(*value)));
        const QString key = QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size()));
        if (m_settings.setDeviceValue(deviceId, key, stored)) {
            qInfo("companion on %s: %s set to %d on the phone", qUtf8Printable(name), qUtf8Printable(key),
                  static_cast<int>(change.value));
        } else {
            qWarning("companion on %s: %s from the phone could not be stored", qUtf8Printable(name),
                     qUtf8Printable(key));
        }
    }
    if (const int again = indexOf(serial); again >= 0) {
        pushCompanionSettings(m_rows[static_cast<std::size_t>(again)]);
    }
}

const CompanionComputer& DeviceManager::thisComputer()
{
    if (!m_thisComputer) {
        const QByteArray machine = QSysInfo::machineUniqueId();
        const QByteArray host = QSysInfo::machineHostName().toUtf8();
        const std::string_view machineId(machine.constData(), static_cast<std::size_t>(machine.size()));
        const std::string_view hostName(host.constData(), static_cast<std::size_t>(host.size()));
        m_thisComputer = companionComputer(machineId, hostName);
    }
    return *m_thisComputer;
}

void DeviceManager::companionNotice(const QString& serial, quint64 ticket, const CompanionNotice& notice)
{
    if (const auto ending = std::ranges::find(m_endingCompanions, ticket, &EndingCompanion::ticket);
        ending != m_endingCompanions.end()) {
        if (!isFinal(notice.event)) {
            return;
        }
        const QString deviceId = ending->deviceId;
        m_endingCompanions.erase(ending);
        for (Row& twin : m_rows) {
            if (!deviceId.isEmpty() && twin.deviceId == deviceId) {
                syncCompanion(twin);
            }
        }
        return;
    }
    const int index = indexOf(serial);
    if (index < 0 || m_rows[static_cast<std::size_t>(index)].companion == nullptr ||
        m_rows[static_cast<std::size_t>(index)].companionTicket != ticket) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    MirrorWindow* mirror = row.mirror;
    switch (notice.event) {
    case CompanionEvent::InstallNeeded:
        if (mirror != nullptr) {
            const settings::Companion companion = companionSettingsOf(m_settings, row.deviceId);
            row.toastAction = [this, serial, ticket] {
                const int at = indexOf(serial);
                if (at >= 0 && m_rows[static_cast<std::size_t>(at)].companion != nullptr &&
                    m_rows[static_cast<std::size_t>(at)].companionTicket == ticket) {
                    m_rows[static_cast<std::size_t>(at)].companion->allowInstall();
                }
            };
            row.toastSecondAction = [this, serial, ticket] {
                const int at = indexOf(serial);
                if (at >= 0 && m_rows[static_cast<std::size_t>(at)].companion != nullptr &&
                    m_rows[static_cast<std::size_t>(at)].companionTicket == ticket) {
                    m_rows[static_cast<std::size_t>(at)].companionHeld = true;
                    stopCompanion(m_rows[static_cast<std::size_t>(at)]);
                }
            };
            mirror->showToast(companion.ime ? tr("Install GameScrcpy Companion on the phone to type there with this "
                                                 "computer's keyboard")
                              : companion.cursor ? tr("Install GameScrcpy Companion on the phone to show the cursor there")
                                                 : tr("Install GameScrcpy Companion on the phone to show the key hints "
                                                      "there"),
                              QStringLiteral("smartphone"), tr("Install"), tr("Not now"));
        }
        return;
    case CompanionEvent::InputBox:
        if (notice.box) {
            companionInputBox(row, *notice.box);
        }
        return;
    case CompanionEvent::ImeSwitched:
        qInfo("companion on %s: typing goes through its input method", qUtf8Printable(row.name()));
        return;
    case CompanionEvent::Screenshot:
    case CompanionEvent::ScreenshotFailed:
        shotArrived(row, notice);
        return;
    case CompanionEvent::SettingChanged:
        if (notice.change) {
            companionSettingChanged(serial, *notice.change);
        }
        return;
    case CompanionEvent::ImeRefused:
        if (mirror != nullptr) {
            row.toastAction = [this, serial, ticket] {
                const int at = indexOf(serial);
                if (at >= 0 && m_rows[static_cast<std::size_t>(at)].companion != nullptr &&
                    m_rows[static_cast<std::size_t>(at)].companionTicket == ticket) {
                    m_rows[static_cast<std::size_t>(at)].companion->retryIme();
                }
            };
            row.toastSecondAction = {};
            mirror->showToast(tr("On the phone, turn on GameScrcpy Companion in the list of keyboards, then choose "
                                 "Try again"),
                              QStringLiteral("keyboard"), tr("Try again"));
        }
        return;
    case CompanionEvent::Installing:
        if (mirror != nullptr) {
            mirror->showToast(tr("Installing GameScrcpy Companion on the phone; allow it there if the phone asks"),
                              QStringLiteral("smartphone"));
        }
        return;
    case CompanionEvent::PermissionNeeded:
        row.companionOverlay = false;
        if (mirror != nullptr) {
            mirror->showToast(tr("On the phone, allow GameScrcpy Companion to display over other apps"),
                              QStringLiteral("smartphone"));
        }
        return;
    case CompanionEvent::Connected:
        row.companionOverlay = notice.overlay;
        qInfo("companion on %s: the app is connected%s", qUtf8Printable(row.name()),
              notice.overlay ? "" : ", waiting for the overlay permission");
        row.shotTicket = ticket;
        row.shotPending = 0;
        bumpBackgrounds();
        if (noPictureOf(row)) {
            const std::optional<LanPairing> paired = std::exchange(row.lanPairing, std::nullopt);
            row.lanToken.clear();
            if (paired && !row.deviceId.isEmpty()) {
                QMetaObject::invokeMethod(
                    this,
                    [this, id = row.deviceId.toStdString(), text = pairingText(*paired)] {
                        if (text.key.empty()) {
                            qWarning("companion pairing of %s is not kept: Windows could not protect its key",
                                     id.c_str());
                            return;
                        }
                        settings::DeviceSettings device = m_settings.device(id);
                        device.companionPhone = text.phone;
                        device.companionKey = text.key;
                        device.companionPc = text.pc;
                        m_settings.setDevice(id, device);
                    },
                    Qt::QueuedConnection);
            }
            if (mirror != nullptr) {
                mirror->showToast(tr("GameScrcpy Companion is connected over Wi-Fi"), QStringLiteral("wifi"));
            }
            syncLanService();
        }
        return;
    case CompanionEvent::OverlayChanged:
        if (notice.overlay && !row.companionOverlay && mirror != nullptr && row.companionCursor) {
            mirror->showToast(tr("The cursor now shows on the phone"), QStringLiteral("mouse-pointer-click"));
        }
        row.companionOverlay = notice.overlay;
        return;
    case CompanionEvent::InstallFailed:
    case CompanionEvent::Failed:
    case CompanionEvent::Lost:
    case CompanionEvent::StoppedOnPhone:
    case CompanionEvent::Ended:
        break;
    }
    dropInputBox(row);
    row.companion.reset();
    forgetShots(row);
    if (noPictureOf(row)) {
        row.lanPairing.reset();
        if (mirror != nullptr && notice.event != CompanionEvent::Ended) {
            mirror->showToast(tr("GameScrcpy Companion disconnected; it connects again by itself on the same Wi-Fi"),
                              QStringLiteral("wifi"));
        }
        syncCompanionLan(row);
        return;
    }
    switch (notice.event) {
    case CompanionEvent::Lost:
        if (!std::exchange(row.companionRetried, true)) {
            syncCompanion(row);
            return;
        }
        row.companionHeld = true;
        break;
    case CompanionEvent::InstallFailed:
        row.companionHeld = true;
        if (mirror != nullptr) {
            mirror->showToast(m_companionApk == nullptr
                                  ? tr("GameScrcpy Companion is missing from this installation; reinstall GameScrcpy")
                                  : tr("GameScrcpy Companion could not be installed on the phone: %1")
                                        .arg(QString::fromStdString(notice.detail)),
                              QStringLiteral("triangle-alert"));
        }
        break;
    case CompanionEvent::Failed:
        row.companionHeld = true;
        if (mirror != nullptr) {
            mirror->showToast(tr("GameScrcpy Companion could not be started on the phone"),
                              QStringLiteral("triangle-alert"));
        }
        break;
    case CompanionEvent::StoppedOnPhone:
        row.companionHeld = true;
        if (mirror != nullptr) {
            mirror->showToast(tr("GameScrcpy Companion was stopped from the phone"), QStringLiteral("smartphone"));
        }
        break;
    default:
        break;
    }
}

void DeviceManager::phoneCursorChanged(Row& row, const protocol::CursorState& cursor)
{
    if (row.companion != nullptr) {
        row.companion->setCursor(phoneCursorFor(noPictureOf(row), row.companionCursor, cursor));
        return;
    }
    if (row.demo == nullptr || !row.companionChosen || !row.companionCursor) {
        return;
    }
    if (!row.loggedCursor || row.loggedCursor->visible != cursor.visible ||
        row.loggedCursor->pressed != cursor.pressed) {
        qInfo("demo device: cursor on the phone %s%s, not sent", cursor.visible ? "shown" : "hidden",
              cursor.pressed ? " and pressed" : "");
    }
    row.loggedCursor = cursor;
}

void DeviceManager::setPhoneStatus(const QString& serial, bool on)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.demo != nullptr || row.deviceId.isEmpty()) {
        row.unsavedPhoneStatus = on;
        if (row.demo != nullptr) {
            qInfo("demo device: phone status %s, not saved", on ? "shown" : "hidden");
        }
    } else {
        const QString deviceId = row.deviceId;
        m_settings.setDeviceValue(deviceId, QStringLiteral("display.phoneStatus"), on);
    }
    if (const int at = indexOf(serial); at >= 0) {
        syncPhoneStatus(m_rows[static_cast<std::size_t>(at)]);
    }
}

bool DeviceManager::phoneStatusOn(const Row& row) const
{
    if (row.demo != nullptr || row.deviceId.isEmpty()) {
        return row.unsavedPhoneStatus;
    }
    return m_settings.effective(row.deviceId.toStdString()).display.phoneStatus;
}

void DeviceManager::syncPhoneStatus(Row& row)
{
    const bool on = phoneStatusOn(row);
    const bool available = row.host.isEmpty() && hasCapability(row, protocol::Welcome::kDeviceStatus);
    const bool shown = on && row.host.isEmpty() && row.mirror != nullptr;
    if (row.controller != nullptr) {
        row.controller->setStatusReports(shown);
    }
    if (!shown || !available) {
        row.phoneStatusText.clear();
    } else if (row.demo != nullptr) {
        row.phoneStatusText = PhoneStatus::of(row.demo->status()).text();
    }
    if (row.mirror != nullptr) {
        row.mirror->setPhoneStatus(QVariantMap{{QStringLiteral("available"), available},
                                               {QStringLiteral("on"), on},
                                               {QStringLiteral("text"), row.phoneStatusText}});
    }
}

void DeviceManager::phoneStatusArrived(const QString& serial, const protocol::DeviceStatus& status)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.mirror == nullptr || !phoneStatusOn(row)) {
        return;
    }
    row.phoneStatusText = PhoneStatus::of(status).text();
    syncPhoneStatus(row);
}

std::shared_ptr<ControlChannel> DeviceManager::channelOf(const Row& row)
{
    if (row.pictureChannel != nullptr) {
        return row.pictureChannel;
    }
    if (row.controller != nullptr) {
        return row.controller->channel();
    }
    if (row.aoa != nullptr) {
        return row.aoa->channel();
    }
    if (row.esp32 != nullptr) {
        return row.esp32->channel();
    }
    if (row.capture != nullptr) {
        return row.capture->channel;
    }
    return row.demoChannel;
}

void DeviceManager::mirrorClosed(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    if (m_grid != nullptr && m_grid->separate(serial)) {
        if (const MirrorWindow* mirror = m_rows[static_cast<std::size_t>(index)].mirror;
            mirror != nullptr && mirror->editing()) {
            finishEditing(serial, false);
        }
        setGridSeparate(serial, false, false);
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    stopRecording(row, false);
    if (row.demo == nullptr) {
        disconnectDevice(serial);
        return;
    }
    releaseInput(serial);
    if (row.mirror != nullptr) {
        row.mirror->deleteLater();
        row.mirror = nullptr;
        emit mirrorsChanged();
    }
    returnCapture(row);
    row.picture.reset();
    updateStatsTimer();
    rowChanged(index);
}

void DeviceManager::updateStatsTimer()
{
    const bool needed =
        std::ranges::any_of(m_rows, [](const Row& each) { return streaming(each) || each.mirror != nullptr; });
    if (needed && !m_statsTimer.isActive()) {
        m_statsTimer.start();
    } else if (!needed) {
        m_statsTimer.stop();
    }
}

QString DeviceManager::linkName(const Row& row)
{
    if (row.demo != nullptr) {
        return tr("Demo");
    }
    if (row.capture != nullptr) {
        return tr("Capture card");
    }
    if (row.aoaOnly) {
        return aoaLinkName(streaming(row) && noPictureOf(row));
    }
    if (isEsp32Row(row)) {
        return esp32LinkName(row.esp32Wifi, streaming(row));
    }
    if (row.provided) {
        return row.provided->extensionName;
    }
    if (row.transported) {
        return row.transported->transportTitle;
    }
    if (row.controller != nullptr && row.controller->touchesOverAoa() && streaming(row)) {
        return aoaTouchLinkName(row.serial);
    }
    if (row.link == session::TransportKind::WifiDirect) {
        return tr("WiFi");
    }
    return attachmentName(row.serial);
}

std::shared_ptr<render::FrameSlot> DeviceManager::framesOf(const Row& row)
{
    if (row.pictureFrames != nullptr) {
        return row.pictureFrames;
    }
    if (row.controller != nullptr) {
        return row.controller->frames();
    }
    if (row.aoa != nullptr) {
        return row.aoa->frames();
    }
    if (row.esp32 != nullptr) {
        return row.esp32->frames();
    }
    if (row.demo != nullptr) {
        return row.demo->frames();
    }
    if (row.capture != nullptr && row.capture->feed != nullptr) {
        return row.capture->feed->frames();
    }
    return nullptr;
}

void DeviceManager::applyLiveSettings()
{
    m_keyHints = m_settings.effective({}).keymap.hints;
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        Row& row = m_rows[i];
        if (row.mirror != nullptr) {
            const settings::Settings effective = m_settings.effective(row.deviceId.toStdString());
            row.mirror->setSharpness(effective.display.sharpness / 100.0);
            row.mirror->setTitleBarFixed(effective.display.titleBar == settings::TitleBarMode::Fixed);
            row.mirror->setToolbarPinned(effective.display.toolbar == settings::ToolbarMode::Pinned);
            followApp(row);
        }
        syncCompanion(row);
        syncPhoneStatus(row);
        rowChanged(static_cast<int>(i));
    }
    usbChanged();
    syncEsp32();
}

void DeviceManager::setExtensions(ExtensionHost* extensions)
{
    if (m_extensions != nullptr) {
        disconnect(m_extensions, nullptr, this, nullptr);
        if (NativeExtensions* native = m_extensions->native()) {
            disconnect(native, &NativeExtensions::moduleTrouble, this, nullptr);
        }
    }
    m_extensions = extensions;
    if (m_extensions != nullptr) {
        connect(m_extensions, &ExtensionHost::filtersChanged, this, &DeviceManager::applyAllFilters);
        connect(m_extensions, &ExtensionHost::nativeChanged, this, [this] {
            for (std::size_t index = 0; index < m_rows.size(); ++index) {
                if (m_rows[index].capture != nullptr) {
                    rowChanged(static_cast<int>(index));
                }
            }
        });
        connect(m_extensions, &ExtensionHost::nativeChanged, this, &DeviceManager::syncEsp32);
        connect(m_extensions, &ExtensionHost::nativeChanged, this, &DeviceManager::bumpLinks);
        connect(m_extensions, &ExtensionHost::extensionsChanged, this, &DeviceManager::bumpLinks);
        if (NativeExtensions* native = m_extensions->native()) {
            native->setTouchOptions(
                [this](std::string_view key, std::string_view device) { return touchOptionsFor(key, device); });
            connect(native, &NativeExtensions::moduleTrouble, this, &DeviceManager::noteModuleTrouble);
            connect(native, &NativeExtensions::providedChanged, this, &DeviceManager::syncProvided);
            connect(native, &NativeExtensions::transportPhonesChanged, this, &DeviceManager::syncTransportPhones);
            connect(native, &NativeExtensions::sourcesChanged, this, &DeviceManager::syncSources);
        }
        connect(m_extensions, &ExtensionHost::nativeChanged, this, &DeviceManager::applyModules);
        connect(m_extensions, &ExtensionHost::addOnsChanged, this, &DeviceManager::applyModules);
    }
    applyAllFilters();
    applyModules();
    syncEsp32();
    syncProvided();
    syncTransportPhones();
    syncSources();
    if (ExtensionPanels* panels = ExtensionPanels::instance()) {
        connect(panels, &ExtensionPanels::menusChanged, this, &DeviceManager::setTrayMenu, Qt::UniqueConnection);
    }
}

void DeviceManager::setNativeJobs(NativeJobs* jobs)
{
    m_jobs = jobs;
}

void DeviceManager::setExtensionLogic(ExtensionLogic* logic)
{
    if (m_extensionLogic != nullptr) {
        disconnect(m_extensionLogic, nullptr, this, nullptr);
        m_extensionLogic->setFrontPhone({});
        m_extensionLogic->setProfileSaver({});
        m_extensionLogic->endSessions();
    }
    m_extensionLogic = logic;
    if (logic == nullptr) {
        return;
    }
    connect(logic, &ExtensionLogic::sessionStarted, this, [this](const QString& serial, ScriptRuntime* runtime) {
        const int index = indexOf(serial);
        if (index < 0 || runtime == nullptr) {
            return;
        }
        const Row& row = m_rows[static_cast<std::size_t>(index)];
        runtime->setKeymapOn(keymapOf(row));
        runtime->setForeground(row.foregroundApp, row.foregroundGame);
        runtime->setFocused(row.focused);
        const auto [profileId, profileShown] = profileInfoOf(row);
        runtime->setProfileFact(profileId, profileShown);
    });
    connect(logic, &ExtensionLogic::toast, this, [this](const QString& serial, const QString& text) {
        if (MirrorWindow* mirror = mirrorOf(serial)) {
            mirror->showToast(text, QStringLiteral("info"));
        }
    });
    connect(logic, &ExtensionLogic::requested, this,
            [this](const QString& serial, const QString& source, const QString& request, const QString& json) {
                logicRequest(serial, source, request, json);
            });
    connect(logic, &ExtensionLogic::failed, this,
            [this](const QString& serial, const QString& text) { logicFailed(serial, text); });
    connect(logic, &ExtensionLogic::reported, this, [this](const QString& serial, const LogicReport& report) {
        m_logicConsole.append(serial, report);
        emit logicReported(serial, report);
    });
    connect(logic, &ExtensionLogic::prompted, this,
            [this, logic](const QString& serial, quint64 id, const QVariantMap& shown) {
                if (MirrorWindow* mirror = mirrorOf(serial)) {
                    mirror->showPrompt(id, shown);
                } else {
                    logic->cancelPrompt(id);
                }
            });
    connect(logic, &ExtensionLogic::promptWithdrawn, this, [this](const QString& serial, quint64 id) {
        if (MirrorWindow* mirror = mirrorOf(serial)) {
            mirror->withdrawPrompt(id);
        }
    });
    connect(logic, &ExtensionLogic::saveWanted, this,
            [this, logic](const QString& serial, quint64 id, const QVariantMap& shown) {
                const int index = indexOf(serial);
                Row* row = index >= 0 ? &m_rows[static_cast<std::size_t>(index)] : nullptr;
                if (row == nullptr || m_override != nullptr || m_library == nullptr || row->profileId.isEmpty() ||
                    row->mirror == nullptr || row->mirror->editor() != nullptr || row->pendingSave ||
                    shown.value(QStringLiteral("changes")).toList().isEmpty()) {
                    logic->cancelPrompt(id);
                    return;
                }
                QVariantMap card = shown;
                card.insert(QStringLiteral("profile"), profileName(row->profileId));
                row->mirror->showPrompt(id, card);
            });
    connect(logic, &ExtensionLogic::drawn, this, [this](const QString& serial, quint64 id, const QVariantMap& drawing) {
        if (MirrorWindow* mirror = mirrorOf(serial)) {
            mirror->draw(id, drawing);
        }
    });
    connect(logic, &ExtensionLogic::erased, this, [this](const QString& serial, quint64 id) {
        if (MirrorWindow* mirror = mirrorOf(serial)) {
            mirror->erase(id);
        }
    });
    connect(logic, &ExtensionLogic::clipboardWanted, this,
            [this](const QString& serial, ScriptRuntime* runtime, quint64 request) {
                readClipboardFor(serial, *runtime, request);
            });
    connect(logic, &ExtensionLogic::clipboardGiven, this,
            [this](const QString& serial, ScriptRuntime* runtime, quint64 request, const QString& text) {
                writeClipboardFor(serial, *runtime, request, text);
            });
    connect(logic, &ExtensionLogic::stoppedWorking, this, [this](const QString& id, const QString& name) {
        if (m_extensions != nullptr) {
            m_extensions->setEnabled(id.toStdString(), false);
        }
        emit extensionStopped(name);
    });
    logic->setFrontPhone([self = QPointer<DeviceManager>(this)]() -> QString {
        if (self == nullptr || self->m_input == nullptr || self->mirrorOf(self->m_input->target()) == nullptr) {
            return {};
        }
        return self->m_input->target();
    });
    logic->setProfileSaver([self = QPointer<DeviceManager>(this)](const QString& serial, const input::Profile& profile) {
        const int index = self != nullptr ? self->indexOf(serial) : -1;
        if (index < 0 || self->m_library == nullptr) {
            return false;
        }
        const QString id = self->m_rows[static_cast<std::size_t>(index)].profileId;
        if (id.isEmpty()) {
            return false;
        }
        if (self->m_library->save(id, profile)) {
            return true;
        }
        if (MirrorWindow* mirror = self->mirrorOf(serial)) {
            mirror->showToast(self->m_library->lastWriteBusy() ? self->busyText()
                                                               : tr("The changes could not be saved to the profile"),
                              QStringLiteral("bad"));
        }
        return false;
    });
    if (m_extensions != nullptr) {
        connect(m_extensions, &ExtensionHost::extensionsChanged, this, [this] {
            for (Row& row : m_rows) {
                syncLogic(row);
            }
        });
    }
    for (Row& row : m_rows) {
        syncSessionScripts(row);
    }
}

void DeviceManager::syncSessionScripts(Row& row)
{
    if (m_extensionLogic == nullptr) {
        return;
    }
    const std::shared_ptr<ControlChannel> channel =
        row.mirror != nullptr && m_input != nullptr ? channelOf(row) : std::shared_ptr<ControlChannel>();
    if (channel == nullptr) {
        m_extensionLogic->endSession(row.serial);
        return;
    }
    if (m_extensionLogic->runs(row.serial, channel.get())) {
        return;
    }
    InputController* input = m_input;
    const QString serial = row.serial;
    m_extensionLogic->startSession(ExtensionLogic::Phone{
        .serial = serial,
        .name = row.name(),
        .deviceKey = logicKeyOf(row),
        .channel = channel.get(),
        .lane =
            [input, serial, channel] {
                return std::make_shared<TaskLane>(std::make_shared<ControllerLane>(*input, serial, channel));
            },
        .snapshots = shotsOf(row),
        .profile = effectiveProfile(row),
        .device = deviceInfoOf(row),
        .frameRate = row.refreshHz > 0.0 ? row.refreshHz : 60.0});
}

void DeviceManager::forSessionScripts(const QString& serial,
                                      const std::function<void(ScriptRuntime& runtime)>& apply) const
{
    if (m_extensionLogic == nullptr) {
        return;
    }
    for (ScriptRuntime* runtime : m_extensionLogic->sessionRuntimes(serial)) {
        apply(*runtime);
    }
}

void DeviceManager::applyFilters(Row& row)
{
    if (row.mirror == nullptr) {
        return;
    }
    std::vector<render::VideoFilter> filters = m_extensions != nullptr
                                                   ? m_extensions->filters(row.deviceId.toStdString())
                                                   : std::vector<render::VideoFilter>{};
    if (filters != row.filters) {
        row.budget.reset();
        row.filters = filters;
    }
    row.mirror->setFilters(std::move(filters));
}

void DeviceManager::applyAllFilters()
{
    for (Row& row : m_rows) {
        applyFilters(row);
    }
}

namespace {

bool usesExtension(const input::Profile& profile, std::string_view extension)
{
    return std::ranges::any_of(profile.controls, [extension](const input::Control& control) {
        const auto* behavior = std::get_if<input::BehaviorControl>(&control.kind);
        return (behavior != nullptr && input::moduleExtension(behavior->behavior) == extension) ||
               std::ranges::any_of(control.attachments, [extension](const input::Attachment& attachment) {
                   return attachment.enabled && input::moduleExtension(attachment.module) == extension;
               });
    });
}

}

void DeviceManager::applyModules()
{
    NativeExtensions* native = m_extensions != nullptr ? m_extensions->native() : nullptr;
    std::shared_ptr<input::ModuleFactory> modules;
    if (native != nullptr) {
        modules = native->moduleFactory();
    }
    if (m_input != nullptr) {
        m_input->post([modules](InputHost& host, Microseconds now) { host.setModules(modules, now); });
    }
    m_moduleTroubles.clear();
    showModuleNotices();
}

void DeviceManager::showModuleNotices()
{
    for (Row& row : m_rows) {
        showModuleNotice(row);
    }
}

void DeviceManager::showModuleNotice(Row& row)
{
    if (row.mirror != nullptr) {
        row.mirror->setExtensionNotice(moduleNotice(row));
    }
}

QString DeviceManager::moduleNotice(const Row& row) const
{
    const NativeExtensions* native = m_extensions != nullptr ? m_extensions->native() : nullptr;
    const std::shared_ptr<const input::Profile> profile = effectiveProfile(row);
    if (native == nullptr || profile == nullptr) {
        return {};
    }
    const std::vector<std::string> missing = input::missingExtensions(*profile, native->moduleCatalog());
    if (!missing.empty()) {
        const QString language = m_settings.interfaceLanguage();
        QStringList names;
        for (const std::string& id : missing) {
            QString name = QString::fromStdString(id);
            for (const ExtensionHost::Extension& extension : m_extensions->extensions()) {
                if (extension.manifest && extension.manifest->id == id) {
                    name = extensionText(*extension.manifest, "name", extension.manifest->name, language);
                    break;
                }
            }
            names.append(name);
        }
        return tr("Extension missing or off: %1").arg(names.join(QStringLiteral(", ")));
    }
    for (const auto& [extension, text] : m_moduleTroubles) {
        const std::shared_ptr<ExtensionModules> modules = native->moduleFactory();
        const bool global = modules != nullptr &&
                            std::ranges::any_of(modules->globals(), [&extension](const input::GlobalAttachment& each) {
                                return input::moduleExtension(each.module) == extension;
                            });
        if (global || usesExtension(*profile, extension)) {
            return text;
        }
    }
    return {};
}

void DeviceManager::noteModuleTrouble(const QString& module, const QString&, int kind)
{
    const NativeExtensions* native = m_extensions != nullptr ? m_extensions->native() : nullptr;
    const std::string ref = module.toStdString();
    QString title = module;
    if (native != nullptr) {
        if (const std::optional<DeclaredModule> declared = native->declaredModule(ref)) {
            title = declared->texts.title;
        }
    }
    QString text;
    switch (static_cast<input::ModuleTroubleKind>(kind)) {
    case input::ModuleTroubleKind::Missing:
        text = tr("%1 could not start").arg(title);
        break;
    case input::ModuleTroubleKind::Failed:
        text = tr("%1 had an error and was skipped").arg(title);
        break;
    case input::ModuleTroubleKind::Stopped:
        text = tr("%1 stopped after an error").arg(title);
        break;
    }
    if (text.isEmpty()) {
        return;
    }
    m_moduleTroubles.insert_or_assign(std::string(input::moduleExtension(ref)), text);
    showModuleNotices();
}

void DeviceManager::checkBudget(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.mirror == nullptr) {
        return;
    }
    if (const auto notice = row.budget.observe(row.mirror->filterStatus(), FilterBudget::Clock::now())) {
        qWarning("filters on %s take %.2f ms a frame, over the budget of %.1f ms; the heaviest is %s at %.2f ms",
                 qUtf8Printable(row.name()), notice->totalMs, FilterBudget::kFrameBudgetMs, notice->heaviest.c_str(),
                 notice->heaviestMs);
        emit filterBudgetExceeded(serial, QString::fromStdString(notice->heaviest), notice->totalMs);
    }
}

int DeviceManager::indexOf(const QString& serial) const
{
    const auto found = std::ranges::find(m_rows, serial, &Row::serial);
    return found == m_rows.end() ? -1 : static_cast<int>(found - m_rows.begin());
}

void DeviceManager::removeRow(int index)
{
    stopTasksOn(m_rows[static_cast<std::size_t>(index)].serial, QStringLiteral("disconnected"));
    stopRecording(m_rows[static_cast<std::size_t>(index)], false);
    Row& row = m_rows[static_cast<std::size_t>(index)];
    const QString deviceId = row.demo == nullptr ? row.deviceId : QString();
    stopCompanion(row);
    if (isEsp32Row(row) && m_esp32 != nullptr) {
        m_esp32->release(row.serial);
    }
    beginRemoveRows({}, index, index);
    if (row.mirror != nullptr) {
        row.mirror->deleteLater();
    }
    if (row.controller != nullptr) {
        row.controller->deleteLater();
    }
    if (row.aoa != nullptr) {
        row.aoa->deleteLater();
    }
    if (row.esp32 != nullptr) {
        row.esp32->deleteLater();
    }
    m_rows.erase(m_rows.begin() + index);
    endRemoveRows();
    if (!deviceId.isEmpty()) {
        for (std::size_t i = 0; i < m_rows.size(); ++i) {
            if (m_rows[i].deviceId == deviceId) {
                const QModelIndex twin = this->index(static_cast<int>(i));
                emit dataChanged(twin, twin);
            }
        }
    }
}

void DeviceManager::rowChanged(int index)
{
    const QModelIndex changed = this->index(index);
    emit dataChanged(changed, changed);
    if (index >= 0 && index < static_cast<int>(m_rows.size())) {
        const Row& row = m_rows[static_cast<std::size_t>(index)];
        if (!row.deviceId.isEmpty() && row.demo == nullptr) {
            for (std::size_t i = 0; i < m_rows.size(); ++i) {
                if (static_cast<int>(i) != index && m_rows[i].deviceId == row.deviceId) {
                    const QModelIndex twin = this->index(static_cast<int>(i));
                    emit dataChanged(twin, twin);
                }
            }
        }
    }
    emit countsChanged();
}

bool DeviceManager::isTwin(const Row& row, const Row& other) noexcept
{
    return !row.deviceId.isEmpty() && row.demo == nullptr && other.demo == nullptr && row.host.isEmpty() &&
           other.host.isEmpty() && other.deviceId == row.deviceId;
}

bool DeviceManager::carded(const Row& row) const
{
    if (!row.host.isEmpty()) {
        return false;
    }
    if (row.capture != nullptr) {
        return captureListingOf(row).listed || active(row.session.phase) || row.mirror != nullptr;
    }
    if (row.deviceId.isEmpty() || row.demo != nullptr) {
        return true;
    }
    const auto chosen = m_chosenLink.find(row.deviceId);
    const auto score = [&](const Row& candidate) {
        const bool online = candidate.listed && candidate.state == adb::DeviceState::Device;
        return std::tuple(active(candidate.session.phase),
                          online && chosen != m_chosenLink.end() && chosen->second == candidate.serial, online,
                          !candidate.aoaOnly, !isWireless(candidate.serial));
    };
    const Row* best = nullptr;
    for (const Row& candidate : m_rows) {
        if (isTwin(row, candidate) && (best == nullptr || score(candidate) > score(*best))) {
            best = &candidate;
        }
    }
    return best == &row;
}

QVariantList DeviceManager::linksOf(const Row& row) const
{
    QVariantList links;
    if (row.deviceId.isEmpty() || row.demo != nullptr || row.aoaOnly) {
        return links;
    }
    for (const bool wireless : {false, true}) {
        for (const Row& candidate : m_rows) {
            if (isTwin(row, candidate) && !candidate.aoaOnly && isWireless(candidate.serial) == wireless) {
                links.append(QVariantMap{{QStringLiteral("serial"), candidate.serial},
                                         {QStringLiteral("name"), attachmentName(candidate.serial)},
                                         {QStringLiteral("wireless"), wireless},
                                         {QStringLiteral("online"), candidate.listed &&
                                                                        candidate.state == adb::DeviceState::Device},
                                         {QStringLiteral("chosen"), &candidate == &row}});
            }
        }
    }
    return links;
}

void DeviceManager::chooseLink(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    const Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.deviceId.isEmpty() || row.demo != nullptr) {
        return;
    }
    m_chosenLink[row.deviceId] = serial;
    qInfo("%s will connect over %s", qUtf8Printable(row.name()), qUtf8Printable(attachmentName(serial)));
    rowChanged(index);
}

void DeviceManager::setAppCatalog(AppCatalog* catalog)
{
    if (m_catalog != nullptr && m_catalog != catalog) {
        m_catalog->setActions({});
    }
    m_catalog = catalog;
    if (m_catalog != nullptr) {
        m_catalog->setActions(
            {.act = [this](const QString& serial, PackageAction action,
                           const QString& package) { return startAppAction(serial, action, package); },
             .install = [this](const QString& serial, const QString& apkPath) { return installOn(serial, apkPath); }});
    }
}

AppAvailability DeviceManager::appAvailability(const QString& serial) const
{
    const int index = indexOf(serial);
    if (index < 0) {
        return {};
    }
    const Row& row = m_rows[static_cast<std::size_t>(index)];
    AppAvailability availability;
    availability.name = row.name();
    if (row.controller == nullptr || !row.host.isEmpty() || !streaming(row)) {
        return availability;
    }
    const std::uint32_t capabilities = row.controller->capabilities();
    availability.connected = true;
    availability.apps = (capabilities & protocol::Welcome::kApps) != 0;
    availability.bulk = row.controller->bulkReady();
    availability.independent = (capabilities & protocol::Welcome::kIndependentDisplay) != 0;
    return availability;
}

bool DeviceManager::requestApps(const QString& serial, std::uint32_t requestId)
{
    const int index = indexOf(serial);
    SessionController* controller = index >= 0 ? m_rows[static_cast<std::size_t>(index)].controller : nullptr;
    return controller != nullptr && !controller->showsAppDisplay() && controller->requestAppList(requestId);
}

bool DeviceManager::requestAppIcon(const QString& serial, std::uint32_t requestId, const QString& package, int sizePx)
{
    const int index = indexOf(serial);
    SessionController* controller = index >= 0 ? m_rows[static_cast<std::size_t>(index)].controller : nullptr;
    return controller != nullptr && !controller->showsAppDisplay() &&
           controller->requestAppIcon(requestId, package, sizePx);
}

void DeviceManager::launchApp(const QString& serial, const QString& package, const QString& label, bool independent)
{
    const int index = indexOf(serial);
    if (index < 0 || package.isEmpty()) {
        return;
    }
    const Row& phone = m_rows[static_cast<std::size_t>(index)];
    if (phone.controller == nullptr || !phone.host.isEmpty() || !streaming(phone)) {
        return;
    }
    if (!independent) {
        if (phone.controller->startApp(package)) {
            showWindow(serial);
        }
        return;
    }
    if (!hasCapability(phone, protocol::Welcome::kIndependentDisplay)) {
        return;
    }
    const auto running = std::ranges::find_if(m_rows, [&serial, &package](const Row& row) {
        return row.host == serial && row.appPackage == package && active(row.session.phase) &&
               row.session.phase != session::SessionPhase::Stopping;
    });
    if (running != m_rows.end()) {
        const QString key = running->serial;
        showWindow(key);
        return;
    }
    openAppDisplay(phone, package, label);
}

void DeviceManager::installApk(const QStringList& serials, const QString& apkPath)
{
    QStringList started;
    for (const QString& serial : serials) {
        if (serial.isEmpty() || started.contains(serial)) {
            continue;
        }
        started.append(serial);
        installOn(serial, apkPath);
    }
}

AppActions& DeviceManager::appActions()
{
    if (m_appActions == nullptr) {
        m_appActions = new AppActions(phoneAccess(m_adb), this);
        connect(m_appActions, &AppActions::installUpdated, this, [this](const QVariantMap& job) {
            emit installUpdated(job);
            if (m_catalog != nullptr) {
                m_catalog->installUpdated(job);
            }
        });
        connect(m_appActions, &AppActions::actionFinished, this,
                [this](quint64 id, const QString& serial, const QString& package, bool ok, const QString& reason) {
                    if (m_catalog != nullptr) {
                        m_catalog->actionFinished(id, serial, package, ok, reason);
                    }
                });
    }
    return *m_appActions;
}

QString DeviceManager::adbRefusal(const QString& serial) const
{
    const int index = indexOf(serial);
    if (index < 0) {
        return tr("The phone is not connected");
    }
    const Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.demo != nullptr || row.aoaOnly || !row.host.isEmpty()) {
        return tr("This connection cannot install apps");
    }
    if (!row.listed || row.state != adb::DeviceState::Device) {
        return tr("The phone is not connected");
    }
    return {};
}

quint64 DeviceManager::installOn(const QString& serial, const QString& apkPath)
{
    const QString local =
        apkPath.startsWith(u"file:", Qt::CaseInsensitive) ? QUrl(apkPath).toLocalFile() : apkPath;
    if (const QString refusal = adbRefusal(serial); !refusal.isEmpty()) {
        return appActions().refuse(serial, local, refusal);
    }
    qInfo("installing %s on %s", qUtf8Printable(local), qUtf8Printable(serial));
    return appActions().install(serial, local);
}

quint64 DeviceManager::startAppAction(const QString& serial, PackageAction action, const QString& package)
{
    return adbRefusal(serial).isEmpty() ? appActions().act(serial, action, package) : 0;
}

void DeviceManager::connectApps(const QString& serial, SessionController* controller)
{
    const auto changed = [this, serial] {
        if (m_catalog != nullptr) {
            m_catalog->availabilityChanged(serial);
        }
    };
    connect(controller, &SessionController::stateChanged, this, changed);
    connect(controller, &SessionController::bulkChanged, this, changed);
    connect(controller, &SessionController::appsListed, this,
            [this, serial](std::uint32_t requestId, const std::vector<protocol::AppEntry>& apps) {
                if (m_catalog != nullptr) {
                    m_catalog->listed(serial, requestId, apps);
                }
            });
    connect(controller, &SessionController::appIconArrived, this,
            [this, serial](std::uint32_t requestId, const QString& package, const QByteArray& png) {
                if (m_catalog != nullptr) {
                    m_catalog->iconArrived(serial, requestId, package, png);
                }
            });
    connect(controller, &SessionController::bulkRequestFailed, this,
            [this, serial](std::uint32_t requestId, protocol::ErrorCode code, const QString& message) {
                if ((requestId & kScreenshotRequests) != 0) {
                    finishScreenshot(requestId, {});
                    return;
                }
                if (m_catalog != nullptr) {
                    m_catalog->requestFailed(serial, requestId, code, message);
                }
            });
    connect(controller, &SessionController::screenshotArrived, this,
            [this](std::uint32_t requestId, const QImage& image) { finishScreenshot(requestId, image); });
}

void DeviceManager::openAppDisplay(const Row& phone, const QString& package, const QString& label)
{
    Row row;
    row.serial = QStringLiteral("%1#%2").arg(phone.serial).arg(++m_appDisplays);
    row.host = phone.serial;
    row.appPackage = package;
    row.appLabel = label.isEmpty() ? package : label;
    row.listed = false;
    row.state = adb::DeviceState::Device;
    row.model = phone.model;
    row.manufacturer = phone.manufacturer;
    row.marketName = phone.marketName;
    row.androidRelease = phone.androidRelease;
    row.deviceId = phone.deviceId;
    row.transport = phone.transport;
    row.silent = true;
    row.foregroundApp = package;
    row.session = {session::SessionPhase::Deploying, std::nullopt};
    const QString key = row.serial;
    const session::TransportChoice transport = row.transport;
    const std::string deviceId = row.deviceId.toStdString();
    auto* controller = new SessionController(m_adb, row.host, m_package, m_graphics, this);
    controller->showAppDisplay(package);
    controller->forceTouchBackend(m_touchBackend);
    if (phone.transported) {
        controller->setHostFactory(transportHost(*phone.transported));
    }
    row.controller = controller;
    qInfo("opening %s of %s on an independent display", qUtf8Printable(package), qUtf8Printable(row.host));
    const int at = static_cast<int>(m_rows.size());
    beginInsertRows({}, at, at);
    m_rows.push_back(std::move(row));
    endInsertRows();

    connect(controller, &SessionController::stateChanged, this,
            [this, key](const session::SessionState& state) { appDisplayChanged(key, state); });
    connect(controller, &SessionController::transportUsed, this, [this, key](session::TransportKind kind) {
        const int index = indexOf(key);
        if (index >= 0) {
            m_rows[static_cast<std::size_t>(index)].link = kind;
            rowChanged(index);
        }
    });
    connect(controller, &SessionController::foregroundAppChanged, this,
            [this, key](const QString& foreground, const QString&, bool game) { foregroundApp(key, foreground, game); });
    connect(controller, &SessionController::refused, this,
            [this, key](protocol::ErrorCode code, const QString&) { appDisplayRefused(key, code); });
    connect(controller, &SessionController::latencyTestChanged, this, [this, key] {
        const int index = indexOf(key);
        if (index < 0) {
            return;
        }
        Row& measured = m_rows[static_cast<std::size_t>(index)];
        const std::optional<LatencyProgress>& progress = measured.controller->latencyTest();
        if (progress && !progress->running && progress->result) {
            measured.latencyResult = progress->result;
        }
        showLatencyTest(measured);
        rowChanged(index);
    });
    controller->start(transport, m_settings.effective(deviceId));
}

void DeviceManager::appDisplayChanged(const QString& key, const session::SessionState& state)
{
    sessionChanged(key, state);
    if (state.phase == session::SessionPhase::Failed) {
        if (state.failure) {
            qWarning("the independent display %s failed: %s", qUtf8Printable(key), state.failure->detail.c_str());
        }
        endAppDisplay(key);
    }
}

void DeviceManager::appDisplayRefused(const QString& key, protocol::ErrorCode code)
{
    if (code == protocol::ErrorCode::ChannelUnavailable || code == protocol::ErrorCode::NotFound) {
        endAppDisplay(key);
    }
}

void DeviceManager::endAppDisplay(const QString& key)
{
    const int index = indexOf(key);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    const int host = indexOf(row.host);
    if (host >= 0 && m_rows[static_cast<std::size_t>(host)].mirror != nullptr) {
        m_rows[static_cast<std::size_t>(host)].mirror->showToast(
            tr("%1 could not open on an independent display").arg(row.appLabel), QStringLiteral("triangle-alert"));
    }
    if (row.controller != nullptr && active(row.session.phase)) {
        row.controller->stop();
        return;
    }
    releaseInput(key);
    if (row.mirror != nullptr) {
        row.mirror->deleteLater();
        row.mirror = nullptr;
        emit mirrorsChanged();
    }
    removeRow(index);
    updateStatsTimer();
}

bool DeviceManager::filesOnline(const Row& row) const
{
    return m_adbReady && row.host.isEmpty() && row.demo == nullptr && !row.aoaOnly && row.listed &&
           row.state == adb::DeviceState::Device;
}

void DeviceManager::pushFiles(const QStringList& serials, const QStringList& localPaths, const QString& remoteDir)
{
    for (const QString& serial : serials) {
        for (const QString& path : localPaths) {
            m_transfers->push(serial, path, remoteDir);
        }
    }
}

void DeviceManager::dropOnMirror(const QString& serial, const QList<QUrl>& urls)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    const QString host = m_rows[static_cast<std::size_t>(index)].host;
    const QString phone = host.isEmpty() ? serial : host;
    const int phoneIndex = indexOf(phone);
    const DropPlan plan = planDrop(urls);
    if (plan.apks.isEmpty() && plan.files.isEmpty()) {
        showDropNotice(serial, DropNotice{.toast = tr("Only files on this computer can be sent to the phone"),
                                          .icon = QStringLiteral("triangle-alert")});
        return;
    }
    if (phoneIndex < 0 || !filesOnline(m_rows[static_cast<std::size_t>(phoneIndex)])) {
        showDropNotice(serial, DropNotice{.toast = tr("Files can be sent only to a phone connected with adb"),
                                          .icon = QStringLiteral("triangle-alert")});
        return;
    }
    qInfo("%lld APK(s) and %lld other item(s) dropped on %s", static_cast<long long>(plan.apks.size()),
          static_cast<long long>(plan.files.size()), qUtf8Printable(phone));
    for (const QString& apk : plan.apks) {
        showDropNotice(serial, m_drops[serial].startInstall(QFileInfo(apk).fileName()));
        installApk(QStringList{phone}, apk);
    }
    if (plan.files.isEmpty()) {
        return;
    }
    QList<qint64> ids;
    QStringList names;
    for (const QString& path : plan.files) {
        ids.append(m_transfers->push(phone, path, QStringLiteral("/sdcard/Download"), QStringLiteral("mirror")));
        names.append(QFileInfo(path).fileName());
    }
    showDropNotice(serial, m_drops[serial].startPush(ids, names));
    for (const qint64 id : ids) {
        if (const std::optional<TransferJob> job = m_transfers->job(id); job && job->finished()) {
            dropTransferUpdated(transferVariant(*job));
        }
    }
}

void DeviceManager::showDropNotice(const QString& serial, const DropNotice& notice)
{
    MirrorWindow* window = mirrorOf(serial);
    if (window == nullptr) {
        return;
    }
    if (notice.clear) {
        window->clearScriptProgress(notice.source);
    } else if (!notice.title.isEmpty()) {
        window->setScriptProgress(notice.source, notice.title, notice.progress);
    }
    if (!notice.toast.isEmpty()) {
        window->showToast(notice.toast, notice.icon);
    }
}

void DeviceManager::dropTransferUpdated(const QVariantMap& job)
{
    for (auto it = m_drops.begin(); it != m_drops.end();) {
        if (const std::optional<DropNotice> notice = it->second.pushUpdated(job)) {
            showDropNotice(it->first, *notice);
        }
        it = it->second.idle() ? m_drops.erase(it) : std::next(it);
    }
}

void DeviceManager::dropInstallUpdated(const QVariantMap& job)
{
    const QString phone = job.value(QStringLiteral("serial")).toString();
    for (auto it = m_drops.begin(); it != m_drops.end();) {
        const int index = indexOf(it->first);
        const QString host = index >= 0 ? m_rows[static_cast<std::size_t>(index)].host : QString();
        if ((host.isEmpty() ? it->first : host) == phone) {
            if (const std::optional<DropNotice> notice = it->second.installUpdated(job)) {
                showDropNotice(it->first, *notice);
            }
        }
        it = it->second.idle() ? m_drops.erase(it) : std::next(it);
    }
}

namespace {

CaptureOptions captureOptionsOf(const settings::CaptureSettings& wanted)
{
    return {.format = wanted.format, .sound = wanted.sound, .manualCrop = manualCropOf(wanted)};
}

session::SessionState captureFailure(const QString& problem)
{
    session::SessionFailure failure;
    failure.kind = session::FailureKind::ConnectFailed;
    failure.phase = session::SessionPhase::Connecting;
    failure.detail = problem.toStdString();
    return {session::SessionPhase::Failed, failure};
}

QRectF rectOf(const capture::CropBox& box)
{
    return {box.x, box.y, box.width, box.height};
}

QImage croppedTo(const QImage& image, const capture::CropBox& box)
{
    if (image.isNull() || box.whole()) {
        return image;
    }
    const QRect area = QRectF(box.x * image.width(), box.y * image.height(), box.width * image.width(),
                              box.height * image.height())
                           .toAlignedRect()
                           .intersected(image.rect());
    return area.isEmpty() ? image : image.copy(area);
}

}

bool DeviceManager::addDemoCapture(const QString& recording)
{
    const QString serial = demoCaptureSerial();
    if (indexOf(serial) >= 0) {
        qWarning("demo capture card: already added");
        return false;
    }
    if (m_graphics == nullptr) {
        qWarning("demo capture card: not added, there is no graphics device to draw video");
        return false;
    }
    addCaptureRow(serial, tr("Demo capture card"), {}, {});
    const int index = indexOf(serial);
    if (index < 0) {
        return false;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    row.capture->demo = true;
    row.capture->logging = std::make_shared<LoggingControlSink>(QByteArrayLiteral("demo capture card"));
    row.capture->feed = std::make_unique<CaptureFeed>(
        m_graphics->device(),
        std::make_unique<CaptureDemoSource>(m_graphics->device(), std::filesystem::path(recording.toStdWString())));
    qInfo("demo capture card: %s with black bars, as a capture card would give it", qUtf8Printable(recording));
    rowChanged(index);
    emit dataChanged(this->index(0), this->index(static_cast<int>(m_rows.size()) - 1));
    return true;
}

void DeviceManager::captureDevicesChanged()
{
    if (m_captureWatcher == nullptr) {
        return;
    }
    std::vector<QString> shown;
    for (const Row& row : m_rows) {
        if (row.capture != nullptr && !row.capture->demo && row.capture->sourceExtension.empty()) {
            shown.push_back(row.serial);
        }
    }
    const CapturePlan plan =
        planCaptureCards(m_captureWatcher->devices(), shown, [this](const QString& serial) {
            return m_settings.device(serial.toStdString()).capture.kind;
        });
    QVariantList others;
    for (const OtherVideoDevice& other : plan.others) {
        others.append(QVariantMap{{QStringLiteral("serial"), other.serial},
                                  {QStringLiteral("name"), other.name},
                                  {QStringLiteral("hidden"), other.hidden}});
    }
    const bool othersChanged = others != m_otherVideoDevices;
    m_otherVideoDevices = others;
    for (const CaptureCard& card : plan.added) {
        addCaptureRow(card.serial, card.name, card.link, card.container);
    }
    for (int i = static_cast<int>(m_rows.size()) - 1; i >= 0; --i) {
        Row& row = m_rows[static_cast<std::size_t>(i)];
        if (row.capture == nullptr || row.capture->demo || !row.capture->sourceExtension.empty()) {
            continue;
        }
        const bool present = std::ranges::find(plan.removed, row.serial) == plan.removed.end();
        const bool was = row.listed;
        row.listed = present;
        if (present) {
            row.capture->goneShown = false;
            if (!was && !row.capture->drivenBy.isEmpty()) {
                lentCaptureBack(row);
            } else if (!was && row.session.phase == session::SessionPhase::Reconnecting) {
                startCapture(row);
            }
            continue;
        }
        if (!row.capture->drivenBy.isEmpty()) {
            if (was) {
                qInfo("capture card unplugged while a phone showed its picture: %s", qUtf8Printable(row.name()));
                lentCaptureLeft(row);
            }
            rowChanged(i);
            continue;
        }
        if (!active(row.session.phase) && row.mirror == nullptr) {
            qInfo("capture card unplugged: %s", qUtf8Printable(row.name()));
            removeRow(i);
            continue;
        }
        if (was) {
            qInfo("capture card unplugged while it was in use: %s", qUtf8Printable(row.name()));
            if (row.mirror != nullptr && !row.capture->goneShown) {
                row.mirror->showToast(tr("The capture card was disconnected"), QStringLiteral("unlink"));
                row.capture->goneShown = true;
            }
            if (row.capture->feed != nullptr) {
                row.capture->feed->stop();
            }
            detachCaptureTouch(row);
            row.session = {session::SessionPhase::Reconnecting, std::nullopt};
        }
        rowChanged(i);
    }
    if (othersChanged) {
        emit capturesChanged();
    }
    updateStatsTimer();
    usbChanged();
    emit countsChanged();
}

void DeviceManager::addCaptureRow(const QString& serial, const QString& name, const std::wstring& link,
                                  const std::wstring& container)
{
    if (indexOf(serial) >= 0 || m_graphics == nullptr) {
        return;
    }
    auto state = std::make_unique<CaptureRow>();
    state->name = name;
    state->link = link;
    state->container = container;
    if (!link.empty()) {
        state->feed = std::make_unique<CaptureFeed>(
            m_graphics->device(),
            std::make_unique<capture::MediaFoundationSource>(link, container, m_graphics->device()));
    }
    state->channel->attach(state->dropping);
    const int at = static_cast<int>(m_rows.size());
    beginInsertRows({}, at, at);
    Row row;
    row.serial = serial;
    row.listed = true;
    row.state = adb::DeviceState::Device;
    row.deviceId = serial;
    row.capture = std::move(state);
    m_rows.push_back(std::move(row));
    endInsertRows();
    qInfo("capture card plugged in: %s (%s)", qUtf8Printable(m_rows.back().name()), qUtf8Printable(serial));
    emit countsChanged();
}

void DeviceManager::startCapture(Row& row)
{
    CaptureRow& state = *row.capture;
    if (state.feed == nullptr || !state.drivenBy.isEmpty() ||
        (active(row.session.phase) && row.session.phase != session::SessionPhase::Reconnecting)) {
        return;
    }
    const QString serial = row.serial;
    const settings::CaptureSettings wanted = m_settings.device(row.deviceId.toStdString()).capture;
    state.feed->setListener([this, serial](CaptureEvent event) {
        QMetaObject::invokeMethod(this, [this, serial, event] { captureEvent(serial, event); }, Qt::QueuedConnection);
    });
    state.feed->setMuted(row.muted);
    state.feed->setVolume(row.volume);
    const bool reopening = row.session.phase == session::SessionPhase::Reconnecting;
    row.session = {reopening ? session::SessionPhase::Reconnecting : session::SessionPhase::Connecting, std::nullopt};
    qInfo("opening the capture card %s", qUtf8Printable(row.name()));
    if (!state.feed->start(captureOptionsOf(wanted))) {
        qWarning("capture card %s: %s", qUtf8Printable(row.name()), qUtf8Printable(state.feed->problem()));
        row.session = captureFailure(state.feed->problem());
        rowChanged(indexOf(serial));
        return;
    }
    attachCaptureTouch(row);
    updateStatsTimer();
    rowChanged(indexOf(serial));
}

void DeviceManager::stopCapture(Row& row)
{
    const QString serial = row.serial;
    releaseInput(serial);
    if (row.mirror != nullptr) {
        row.mirror->deleteLater();
        row.mirror = nullptr;
        emit mirrorsChanged();
    }
    detachCaptureTouch(row);
    if (row.capture->feed != nullptr) {
        row.capture->feed->stop();
    }
    row.capture->cropEditing = false;
    row.session = {session::SessionPhase::Stopped, std::nullopt};
    row.stats.clear();
    row.thumbnail = {};
    updateStatsTimer();
    const int index = indexOf(serial);
    if (!row.listed && index >= 0) {
        removeRow(index);
        emit countsChanged();
        return;
    }
    qInfo("capture card %s: stopped", qUtf8Printable(row.name()));
    rowChanged(index);
}

void DeviceManager::captureEvent(const QString& serial, CaptureEvent event)
{
    if (m_probes.contains(serial)) {
        QMetaObject::invokeMethod(this, [this, serial, event] { probeEvent(serial, event); }, Qt::QueuedConnection);
    }
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.capture == nullptr || row.capture->feed == nullptr) {
        return;
    }
    if (!row.capture->drivenBy.isEmpty()) {
        lentCaptureEvent(row, event);
        return;
    }
    CaptureRow& state = *row.capture;
    switch (event) {
    case CaptureEvent::Opened:
        state.formats = state.feed->formats();
        if (row.session.phase == session::SessionPhase::Connecting ||
            row.session.phase == session::SessionPhase::Reconnecting) {
            row.session = {session::SessionPhase::Streaming, std::nullopt};
            restartMeasures(row);
            if (row.mirror == nullptr) {
                openMirror(row);
            }
            if (row.mirror != nullptr && !state.unavailable.isEmpty()) {
                row.mirror->showToast(tr("%1 is not available, touch is off").arg(state.unavailable),
                                      QStringLiteral("info"));
                state.unavailable.clear();
            }
            updateStatsTimer();
            qInfo("capture card %s: %s", qUtf8Printable(row.name()), qUtf8Printable(state.feed->formatText()));
        }
        applyCaptureGeometry(row);
        break;
    case CaptureEvent::Cropped:
        applyCaptureGeometry(row);
        break;
    case CaptureEvent::Thumbnailed:
        if (std::optional<media::Thumbnail> made = state.feed->takeThumbnail();
            made && made->width > 0 && made->height > 0 && row.session.phase == session::SessionPhase::Streaming) {
            row.thumbnail = thumbnailImage(*made);
            const QModelIndex changed = this->index(index);
            emit dataChanged(changed, changed, {ThumbnailRole});
        }
        return;
    case CaptureEvent::Failed: {
        const QString problem = state.feed->problem();
        if (row.session.phase == session::SessionPhase::Streaming) {
            stopRecording(row, true);
            state.feed->stop();
            detachCaptureTouch(row);
            row.session = {session::SessionPhase::Reconnecting, std::nullopt};
            if (row.mirror != nullptr && !state.goneShown) {
                row.mirror->showToast(tr("The capture card was disconnected"), QStringLiteral("unlink"));
                state.goneShown = true;
            }
        } else if (active(row.session.phase)) {
            state.feed->stop();
            detachCaptureTouch(row);
            row.session = captureFailure(problem);
        }
        updateStatsTimer();
        break;
    }
    }
    rowChanged(index);
}

void DeviceManager::detachCaptureTouch(Row& row)
{
    CaptureRow& state = *row.capture;
    ++state.touchGeneration;
    state.channel->detach();
    state.aoa.reset();
    state.port.reset();
    state.extension.reset();
    state.touch.clear();
    if (m_esp32 != nullptr) {
        m_esp32->release(row.serial);
    }
}

void DeviceManager::attachCaptureTouch(Row& row)
{
    detachCaptureTouch(row);
    CaptureRow& state = *row.capture;
    const auto [method, aoaPhone] = captureTouchWanted(row);
    const std::vector<platform::UsbPhone> phones =
        m_usb != nullptr ? m_usb->phones() : std::vector<platform::UsbPhone>();
    const bool aoa = method == u"aoa";
    if (!aoa && !method.isEmpty() && method != u"none") {
        state.extension = extensionTouchSink(method, row);
    }
    const TouchChoice choice = chooseCaptureTouch(method, aoaPhone, phones, state.extension != nullptr);
    QString unavailable = choice.unavailable.isEmpty() ? QString() : captureTouchName(method);
    if (choice.method == u"aoa" && choice.phone) {
        platform::UsbFailure failure = platform::UsbFailure::NotFound;
        if (std::unique_ptr<AoaLink> link = openAoaLink(*choice.phone, failure); link != nullptr) {
            const QString serial = row.serial;
            const std::uint32_t generation = state.touchGeneration;
            state.port = std::make_unique<AoaPort>(std::move(link), [this, serial, generation](AoaState portState) {
                QMetaObject::invokeMethod(
                    this, [this, serial, generation, portState] { captureTouchChanged(serial, generation, portState); },
                    Qt::QueuedConnection);
            });
            state.aoa = std::make_unique<AoaTouchSink>(nullptr, *state.port, AoaDevices{.mouse = false, .keyboard = true});
            state.channel->attach(*state.aoa);
            state.touch = choice.method;
            qInfo("capture card %s: touches go to %s over AOA", qUtf8Printable(row.name()),
                  choice.phone->serial.c_str());
        } else {
            unavailable = captureTouchName(method);
        }
    } else if (state.extension != nullptr && choice.method == method) {
        state.channel->attach(*state.extension);
        state.touch = method;
    } else {
        state.extension.reset();
    }
    if (state.touch.isEmpty()) {
        ControlSink& idle = state.logging != nullptr ? *state.logging : static_cast<ControlSink&>(state.dropping);
        state.channel->attach(idle);
        state.touch = QStringLiteral("none");
    }
    applyCaptureGeometry(row);
    if (unavailable.isEmpty()) {
        return;
    }
    qWarning("capture card %s: %s is not available, touch is off", qUtf8Printable(row.name()),
             qUtf8Printable(unavailable));
    if (row.mirror != nullptr) {
        row.mirror->showToast(tr("%1 is not available, touch is off").arg(unavailable), QStringLiteral("info"));
    } else {
        state.unavailable = unavailable;
    }
}

void DeviceManager::captureTouchChanged(const QString& serial, std::uint32_t generation, AoaState portState)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.capture == nullptr || row.capture->touchGeneration != generation || row.capture->port == nullptr ||
        portState == AoaState::Starting) {
        return;
    }
    if (portState == AoaState::Ready) {
        qInfo("capture card %s: the AOA touch screen is ready", qUtf8Printable(row.name()));
        return;
    }
    detachCaptureTouch(row);
    CaptureRow& state = *row.capture;
    ControlSink& idle = state.logging != nullptr ? *state.logging : static_cast<ControlSink&>(state.dropping);
    state.channel->attach(idle);
    state.touch = QStringLiteral("none");
    applyCaptureGeometry(row);
    qWarning("capture card %s: AOA stopped, touch is off", qUtf8Printable(row.name()));
    if (MirrorWindow* shown = row.mirror != nullptr ? row.mirror : mirrorOf(state.drivenBy); shown != nullptr) {
        shown->showToast(tr("%1 is not available, touch is off").arg(QStringLiteral("AOA")), QStringLiteral("info"));
    }
    rowChanged(index);
}

std::shared_ptr<ExtensionTouch> DeviceManager::extensionTouchSink(const QString& method, Row& row)
{
    TouchMethodSource* source = m_extensions != nullptr ? m_extensions->touchSource() : nullptr;
    if (source == nullptr || row.capture == nullptr) {
        return nullptr;
    }
    const QString serial = row.serial;
    const std::uint32_t generation = row.capture->touchGeneration;
    return source->openTouch(method.toStdString(), serial.toStdString(), row.capture->dropping, [this, serial, generation] {
        QMetaObject::invokeMethod(
            this, [this, serial, generation] { captureTouchFell(serial, generation); }, Qt::QueuedConnection);
    });
}

void DeviceManager::captureTouchFell(const QString& serial, std::uint32_t generation)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.capture == nullptr || row.capture->touchGeneration != generation || row.capture->extension == nullptr) {
        return;
    }
    const QString name = captureTouchName(row.capture->touch);
    detachCaptureTouch(row);
    CaptureRow& state = *row.capture;
    ControlSink& idle = state.logging != nullptr ? *state.logging : static_cast<ControlSink&>(state.dropping);
    state.channel->attach(idle);
    state.touch = QStringLiteral("none");
    applyCaptureGeometry(row);
    qWarning("capture card %s: the touch method %s stopped working, touch is off", qUtf8Printable(row.name()),
             qUtf8Printable(name));
    if (MirrorWindow* shown = row.mirror != nullptr ? row.mirror : mirrorOf(state.drivenBy); shown != nullptr) {
        shown->showToast(tr("%1 is not available, touch is off").arg(name), QStringLiteral("info"));
    }
    rowChanged(index);
}

QString DeviceManager::captureTouchName(const QString& method) const
{
    if (method.contains(u'/') && m_extensions != nullptr && m_extensions->touchSource() != nullptr) {
        return m_extensions->touchSource()->touchTitle(method.toStdString());
    }
    return touchMethodName(method);
}

std::string DeviceManager::touchOptionsFor(std::string_view key, std::string_view device) const
{
    if (key != kEsp32TouchKey) {
        return touchDeviceOptions(key, device);
    }
    const QString serial = QString::fromUtf8(device.data(), static_cast<qsizetype>(device.size()));
    const int index = indexOf(serial);
    if (index >= 0 && isEsp32Row(m_rows[static_cast<std::size_t>(index)])) {
        const Row& row = m_rows[static_cast<std::size_t>(index)];
        return esp32TouchOptions(row.esp32Where.toStdString(), row.esp32Wifi,
                                 dataDirectory() / std::string(esp32::kBoardsFileName));
    }
    const QString deviceId = index >= 0 ? m_rows[static_cast<std::size_t>(index)].deviceId : serial;
    const settings::Esp32 board = m_settings.effective(deviceId.toStdString()).esp32;
    if (m_esp32 != nullptr) {
        m_esp32->claim(serial, esp32ClaimOf(esp32SettingsOf(board)));
    }
    const QJsonObject options{
        {QStringLiteral("port"), QString::fromStdString(board.port)},
        {QStringLiteral("link"), board.link == settings::Esp32Connection::Wifi ? QStringLiteral("wifi")
                                                                               : QStringLiteral("serial")},
        {QStringLiteral("address"), QString::fromStdString(board.address)},
        {QStringLiteral("boardsFile"),
         QString::fromStdWString((dataDirectory() / std::string(esp32::kBoardsFileName)).wstring())}};
    return QJsonDocument(options).toJson(QJsonDocument::Compact).toStdString();
}

std::string DeviceManager::touchDeviceOptions(std::string_view key, std::string_view device) const
{
    const NativeExtensions* native = m_extensions != nullptr ? m_extensions->native() : nullptr;
    if (native == nullptr) {
        return {};
    }
    const QString method = QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size()));
    const QString serial = QString::fromUtf8(device.data(), static_cast<qsizetype>(device.size()));
    const int index = indexOf(serial);
    const QString deviceId = index >= 0 ? m_rows[static_cast<std::size_t>(index)].deviceId : serial;
    const std::string_view extension = key.substr(0, key.rfind('/'));
    std::string chosenDevice;
    const QVariantList methods = native->touchMethods();
    const bool wanted = std::ranges::any_of(methods, [&method](const QVariant& entry) {
        const QVariantMap fields = entry.toMap();
        return fields.value(QStringLiteral("key")).toString() == method &&
               fields.value(QStringLiteral("wantsDevice")).toBool();
    });
    const HardwareAccess* hardware = HardwareAccess::instance();
    const QString chosen = QString::fromStdString(m_settings.effective(deviceId.toStdString()).touch.device);
    if (wanted && hardware != nullptr && chosen.isEmpty()) {
        qInfo("touch method %s on %s: no device is chosen for it", qUtf8Printable(method), qUtf8Printable(serial));
    } else if (wanted && hardware != nullptr) {
        const std::optional<gs::hardware::DeviceInfo> found = hardware->device(chosen);
        if (found) {
            const std::string_view kind = gs::hardware::kindName(found->kind);
            const QJsonObject made{
                {QStringLiteral("kind"), QString::fromUtf8(kind.data(), static_cast<qsizetype>(kind.size()))},
                {QStringLiteral("path"), QString::fromStdString(found->path)},
                {QStringLiteral("name"), QString::fromStdString(found->name)}};
            chosenDevice = QJsonDocument(made).toJson(QJsonDocument::Compact).toStdString();
        } else {
            qWarning("touch method %s on %s: the chosen device %s is not connected", qUtf8Printable(method),
                     qUtf8Printable(serial), qUtf8Printable(hardware->deviceTitle(chosen)));
        }
    }
    const std::string touchBox =
        index >= 0 && m_rows[static_cast<std::size_t>(index)].provided ? m_rows[static_cast<std::size_t>(index)].provided->id
                                                                       : std::string();
    return nativeTouchOptions(m_extensions->nativeSettings(extension), deviceId.toStdString(), chosenDevice,
                              touchBox);
}

void DeviceManager::feedInput(const QString& serial, const input::DeviceEvent& event)
{
    const int index = indexOf(serial);
    if (index < 0 || m_input == nullptr) {
        return;
    }
    const Row& row = m_rows[static_cast<std::size_t>(index)];
    const std::shared_ptr<ControlChannel> channel = channelOf(row);
    if (channel == nullptr || !keymapOf(row)) {
        return;
    }
    m_input->post([channel, event](InputHost& host, Microseconds now) {
        static_cast<void>(host.feed(channel.get(), event, now));
    });
}

QString DeviceManager::focusedPhone() const
{
    const auto found = std::ranges::find_if(m_rows, [](const Row& row) { return row.focused; });
    return found != m_rows.end() ? found->serial : QString();
}

void DeviceManager::applyCaptureGeometry(Row& row)
{
    CaptureRow& state = *row.capture;
    if (state.feed == nullptr) {
        return;
    }
    const settings::CaptureSettings wanted = m_settings.device(row.deviceId.toStdString()).capture;
    const capture::CropBox crop = state.feed->crop();
    const QSize frame = state.feed->frameSize();
    const QSize shown = state.feed->cropSize();
    const bool right = wanted.landscape == settings::CaptureLandscape::Right;
    row.rotation = frame.width() > 0 && frame.height() > 0 ? capture::rotationOf(crop, frame.width(), frame.height(), right)
                                                           : 0;
    const std::uint16_t touchSession = state.feed->touchSession();
    if (state.aoa != nullptr) {
        state.aoa->setVideo(touchSession, row.rotation);
    }
    if (state.extension != nullptr) {
        state.extension->setVideo(touchSession, row.rotation);
    }
    if (shown.width() > 0 && shown.height() > 0) {
        state.channel->setVideo(touchSession, static_cast<std::uint16_t>(shown.width()),
                                static_cast<std::uint16_t>(shown.height()));
    }
    if (row.mirror != nullptr) {
        row.mirror->setCaptureCard(static_cast<int>(right ? ControlTurn::LandscapeRight : ControlTurn::LandscapeLeft),
                                   state.feed->manualCrop());
        if (!state.cropEditing) {
            row.mirror->setCrop(rectOf(crop));
        }
    }
    cropGridCell(row);
    if (row.logic != nullptr) {
        row.logic->setDevice(deviceInfoOf(row));
    }
    for (const std::unique_ptr<TaskRun>& run : m_taskRuns) {
        if (run->serial == row.serial && run->runtime != nullptr) {
            run->runtime->setDevice(deviceInfoOf(row));
        }
    }
    if (m_extensionLogic != nullptr) {
        m_extensionLogic->updateSession(row.serial, deviceInfoOf(row), logicKeyOf(row));
    }
}

void DeviceManager::cropGridCell(const Row& row)
{
    const CaptureFeed* feed = pictureFeedOf(row);
    if (m_grid != nullptr && feed != nullptr && m_grid->contains(row.serial)) {
        m_grid->setCrop(row.serial, rectOf(feed->crop()));
    }
}

void DeviceManager::restartCaptureFeed(Row& row)
{
    CaptureRow& state = *row.capture;
    if (state.feed == nullptr) {
        return;
    }
    if (!state.drivenBy.isEmpty()) {
        state.feed->stop();
        if (!state.feed->start(captureOptionsOf(m_settings.device(row.deviceId.toStdString()).capture))) {
            qWarning("capture card %s: %s", qUtf8Printable(row.name()), qUtf8Printable(state.feed->problem()));
            lentCaptureLeft(row);
        }
        return;
    }
    if (!active(row.session.phase)) {
        return;
    }
    const QString serial = row.serial;
    state.feed->stop();
    row.session = {session::SessionPhase::Connecting, std::nullopt};
    if (!state.feed->start(captureOptionsOf(m_settings.device(row.deviceId.toStdString()).capture))) {
        detachCaptureTouch(row);
        row.session = captureFailure(state.feed->problem());
    }
    rowChanged(indexOf(serial));
}

QVariantMap DeviceManager::captureState(const Row& row) const
{
    if (row.capture == nullptr) {
        return {};
    }
    const CaptureRow& state = *row.capture;
    const settings::CaptureSettings wanted = m_settings.device(row.deviceId.toStdString()).capture;
    QVariantList phones;
    if (m_usb != nullptr) {
        for (const platform::UsbPhone& phone : m_usb->phones()) {
            phones.append(QVariantMap{
                {QStringLiteral("serial"), QString::fromStdString(phone.serial)},
                {QStringLiteral("name"), QString::fromStdString(phone.name.empty() ? phone.serial : phone.name)},
                {QStringLiteral("ready"), phone.driver == platform::UsbDriver::Ready}});
        }
    }
    QVariantList methods{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("none")}, {QStringLiteral("name"), tr("No touch")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("aoa")}, {QStringLiteral("name"), tr("AOA over USB")}},
    };
    const NativeExtensions* native = m_extensions != nullptr ? m_extensions->native() : nullptr;
    if (native != nullptr) {
        for (const QVariant& method : native->touchMethods()) {
            const QVariantMap entry = method.toMap();
            methods.append(QVariantMap{{QStringLiteral("id"), entry.value(QStringLiteral("key"))},
                                       {QStringLiteral("name"), entry.value(QStringLiteral("title"))}});
        }
    }
    const QString chosenTouch = QString::fromStdString(wanted.touch);
    if (chosenTouch.contains(u'/') && std::ranges::none_of(methods, [&chosenTouch](const QVariant& method) {
            return method.toMap().value(QStringLiteral("id")).toString() == chosenTouch;
        })) {
        methods.append(QVariantMap{{QStringLiteral("id"), chosenTouch}, {QStringLiteral("name"), captureTouchName(chosenTouch)}});
    }
    const QString touchShown = streaming(row) && !state.touch.isEmpty() ? state.touch : chosenTouch;
    QString touchTitle = captureTouchName(touchShown);
    for (const QVariant& method : methods) {
        const QVariantMap entry = method.toMap();
        if (entry.value(QStringLiteral("id")).toString() == touchShown) {
            touchTitle = entry.value(QStringLiteral("name")).toString();
            break;
        }
    }
    const QSize live = state.feed != nullptr && streaming(row) ? state.feed->frameSize() : QSize();
    const QSize resolution = live.width() > 0 && live.height() > 0 ? live : captureResolution(state.formats, wanted.format);
    const QString profile = row.profileId.isEmpty() ? rememberedProfile(row) : row.profileId;
    const CaptureAssignment assignment = captureListing(wanted.phone, false).assignment;
    const QString phone =
        assignment == CaptureAssignment::Assigned ? QString::fromStdString(wanted.phone) : QString();
    const int owner = phoneIndexOf(phone);
    return {{QStringLiteral("isCapture"), true},
            {QStringLiteral("assignment"), assignmentName(assignment)},
            {QStringLiteral("phone"), phone},
            {QStringLiteral("phoneName"), owner >= 0 ? m_rows[static_cast<std::size_t>(owner)].name() : QString()},
            {QStringLiteral("demo"), state.demo},
            {QStringLiteral("format"), state.feed != nullptr && streaming(row) ? state.feed->formatText() : QString()},
            {QStringLiteral("formats"), state.formats},
            {QStringLiteral("chosenFormat"), QString::fromStdString(wanted.format)},
            {QStringLiteral("touch"), QString::fromStdString(wanted.touch)},
            {QStringLiteral("touchInUse"), state.touch},
            {QStringLiteral("aoaPhone"), QString::fromStdString(wanted.aoaPhone)},
            {QStringLiteral("phones"), phones},
            {QStringLiteral("sound"), wanted.sound},
            {QStringLiteral("soundFound"), state.feed != nullptr && state.feed->sound()},
            {QStringLiteral("manualCrop"), manualCropOf(wanted).has_value()},
            {QStringLiteral("touchMethods"), methods},
            {QStringLiteral("profileId"), profile},
            {QStringLiteral("summary"), captureSummary(resolution, touchTitle, profileName(profile))},
            {QStringLiteral("extension"), state.sourceExtensionName}};
}

std::optional<Advice> DeviceManager::captureAdviceOf(const Row& row) const
{
    if (row.capture == nullptr) {
        return std::nullopt;
    }
    if (row.session.phase == session::SessionPhase::Failed) {
        const QString detail = row.capture->feed != nullptr ? row.capture->feed->problem() : QString();
        return Advice{tr("The capture card could not be opened"),
                      detail.isEmpty()
                          ? tr("Close other programs that use it, such as OBS or a camera app, then try again")
                          : tr("%1. Close other programs that use it, such as OBS or a camera app, then try again")
                                .arg(detail)};
    }
    if (row.session.phase == session::SessionPhase::Reconnecting) {
        return Advice{tr("The capture card was disconnected"),
                      tr("Plug it in again; the picture comes back by itself")};
    }
    return std::nullopt;
}

void DeviceManager::setCaptureOption(const QString& serial, const QString& key, const QVariant& value)
{
    constexpr qsizetype kMostOptionLength = 64;
    const int index = indexOf(serial);
    if (index < 0 || m_rows[static_cast<std::size_t>(index)].capture == nullptr) {
        return;
    }
    const std::string id = serial.toStdString();
    settings::DeviceSettings device = m_settings.device(id);
    settings::CaptureSettings& wanted = device.capture;
    const std::string text = value.toString().left(kMostOptionLength).toStdString();
    if (key == u"touch") {
        wanted.touch = text.empty() ? std::string("none") : text;
    } else if (key == u"aoaPhone") {
        wanted.aoaPhone = text;
    } else if (key == u"format") {
        wanted.format = text;
    } else if (key == u"sound") {
        wanted.sound = value.toBool();
    } else {
        return;
    }
    m_settings.setDevice(id, device);
    Row& row = m_rows[static_cast<std::size_t>(index)];
    qInfo("capture card %s: %s is now %s", qUtf8Printable(row.name()), qUtf8Printable(key),
          qUtf8Printable(value.toString()));
    if (key == u"touch" || key == u"aoaPhone") {
        if (streaming(row)) {
            attachCaptureTouch(row);
        }
    } else {
        restartCaptureFeed(row);
    }
    rowChanged(indexOf(serial));
}

void DeviceManager::hideCaptureCard(const QString& serial)
{
    if (!isCaptureSerial(serial) || serial == demoCaptureSerial()) {
        return;
    }
    const std::string id = serial.toStdString();
    settings::DeviceSettings device = m_settings.device(id);
    device.capture.kind = settings::CaptureKind::NotCaptureCard;
    m_settings.setDevice(id, device);
    if (const int index = indexOf(serial); index >= 0 && m_rows[static_cast<std::size_t>(index)].capture != nullptr) {
        if (const QString phone = m_rows[static_cast<std::size_t>(index)].capture->drivenBy; !phone.isEmpty()) {
            disconnectDevice(phone);
        }
    }
    if (const int index = indexOf(serial); index >= 0) {
        Row& row = m_rows[static_cast<std::size_t>(index)];
        stopRecording(row, false);
        if (active(row.session.phase) || row.mirror != nullptr) {
            stopCapture(row);
        }
        if (const int at = indexOf(serial); at >= 0) {
            removeRow(at);
        }
    }
    qInfo("capture card %s is hidden: it is not a capture card", qUtf8Printable(serial));
    captureDevicesChanged();
}

void DeviceManager::showCaptureCard(const QString& serial)
{
    if (!isCaptureSerial(serial) || serial == demoCaptureSerial()) {
        return;
    }
    const std::string id = serial.toStdString();
    settings::DeviceSettings device = m_settings.device(id);
    if (device.capture.kind == settings::CaptureKind::CaptureCard) {
        return;
    }
    device.capture.kind = settings::CaptureKind::CaptureCard;
    m_settings.setDevice(id, device);
    qInfo("capture device %s is treated as a capture card", qUtf8Printable(serial));
    captureDevicesChanged();
}

void DeviceManager::beginCaptureCrop(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    Row* card = row.capture != nullptr ? &row : lentCardOf(row);
    if (card == nullptr || card->capture->feed == nullptr || row.mirror == nullptr) {
        return;
    }
    card->capture->cropEditing = true;
    row.mirror->setCropEditing(true, rectOf(card->capture->feed->crop()));
}

void DeviceManager::finishCaptureCrop(const QString& serial, bool apply, double x, double y, double width,
                                      double height)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    Row* card = row.capture != nullptr ? &row : lentCardOf(row);
    if (card == nullptr || card->capture->feed == nullptr) {
        return;
    }
    card->capture->cropEditing = false;
    const QString cardSerial = card->serial;
    if (apply) {
        capture::CropBox box{.x = std::clamp(x, 0.0, 1.0), .y = std::clamp(y, 0.0, 1.0)};
        box.width = std::clamp(width, 0.0, 1.0 - box.x);
        box.height = std::clamp(height, 0.0, 1.0 - box.y);
        if (box.width >= 0.05 && box.height >= 0.05) {
            const std::string id = card->deviceId.toStdString();
            settings::DeviceSettings device = m_settings.device(id);
            storeManualCrop(device.capture, box);
            m_settings.setDevice(id, device);
            if (const int cardAt = indexOf(cardSerial);
                cardAt >= 0 && m_rows[static_cast<std::size_t>(cardAt)].capture->feed != nullptr) {
                const Row& kept = m_rows[static_cast<std::size_t>(cardAt)];
                kept.capture->feed->setManualCrop(manualCropOf(device.capture));
                qInfo("capture card %s: the picture area is set by hand", qUtf8Printable(kept.name()));
            }
        }
    }
    const int at = indexOf(serial);
    if (at < 0) {
        return;
    }
    Row& shown = m_rows[static_cast<std::size_t>(at)];
    if (shown.mirror != nullptr) {
        shown.mirror->setCropEditing(false, {});
    }
    if (shown.capture != nullptr) {
        applyCaptureGeometry(shown);
    } else {
        applyPictureGeometry(shown);
    }
    rowChanged(at);
}

void DeviceManager::autoCaptureCrop(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    const Row* card = row.capture != nullptr ? &row : lentCardOf(row);
    if (card == nullptr || card->capture->feed == nullptr) {
        return;
    }
    const QString cardSerial = card->serial;
    const std::string id = card->deviceId.toStdString();
    settings::DeviceSettings device = m_settings.device(id);
    storeManualCrop(device.capture, std::nullopt);
    m_settings.setDevice(id, device);
    const int at = indexOf(serial);
    const int cardAt = indexOf(cardSerial);
    if (at < 0 || cardAt < 0 || m_rows[static_cast<std::size_t>(cardAt)].capture->feed == nullptr) {
        return;
    }
    Row& shown = m_rows[static_cast<std::size_t>(at)];
    CaptureRow& state = *m_rows[static_cast<std::size_t>(cardAt)].capture;
    state.feed->setManualCrop(std::nullopt);
    if (state.cropEditing) {
        state.cropEditing = false;
        if (shown.mirror != nullptr) {
            shown.mirror->setCropEditing(false, {});
        }
    }
    qInfo("capture card %s: the picture area is found by itself again",
          qUtf8Printable(m_rows[static_cast<std::size_t>(cardAt)].name()));
    if (shown.capture != nullptr) {
        applyCaptureGeometry(shown);
    } else {
        applyPictureGeometry(shown);
    }
    rowChanged(at);
}

int DeviceManager::phoneIndexOf(const QString& deviceId) const
{
    if (deviceId.isEmpty()) {
        return -1;
    }
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        const Row& row = m_rows[i];
        if (row.deviceId == deviceId && row.host.isEmpty() && row.capture == nullptr && !isBoxRow(row) &&
            carded(row)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int DeviceManager::cardIndexOf(const QString& deviceId) const
{
    if (deviceId.isEmpty()) {
        return -1;
    }
    const std::string wanted = deviceId.toStdString();
    int best = -1;
    quint64 bestOrder = 0;
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        const Row& row = m_rows[i];
        if (row.capture == nullptr || row.capture->feed == nullptr || !row.listed ||
            m_settings.device(row.deviceId.toStdString()).capture.phone != wanted) {
            continue;
        }
        const auto order = m_assignOrder.find(row.serial);
        const quint64 rank = order != m_assignOrder.end() ? order->second : 0;
        if (best < 0 || rank >= bestOrder) {
            best = static_cast<int>(i);
            bestOrder = rank;
        }
    }
    return best;
}

PhoneLinks DeviceManager::phoneLinksOf(const QString& deviceId, const settings::Touch& touch) const
{
    PhoneLinks links;
    links.extensions = extensionTouchOffers(QString::fromStdString(touch.extension));
    if (deviceId.isEmpty()) {
        return links;
    }
    for (const Row& row : m_rows) {
        if (row.deviceId != deviceId || !row.host.isEmpty() || row.capture != nullptr || isBoxRow(row)) {
            continue;
        }
        if (row.demo != nullptr) {
            links.online = true;
            links.adb = true;
            links.demo = true;
        } else if (row.aoaOnly) {
            links.online = links.online || row.listed;
        } else if (row.listed && row.state == adb::DeviceState::Device) {
            links.online = true;
            links.adb = true;
        }
        if (row.listed && row.usb && row.usb->driver == platform::UsbDriver::Ready) {
            links.aoa = true;
        }
    }
    if (const int card = cardIndexOf(deviceId); card >= 0) {
        links.captureCard = true;
        links.captureName = m_rows[static_cast<std::size_t>(card)].name();
    }
    return links;
}

std::vector<ExtensionTouchOffer> DeviceManager::extensionTouchOffers(const QString& chosen) const
{
    std::vector<ExtensionTouchOffer> offers;
    const auto offered = [&offers](const QString& key) {
        return std::ranges::any_of(offers, [&key](const ExtensionTouchOffer& offer) { return offer.key == key; });
    };
    if (m_extensions != nullptr) {
        const NativeExtensions* native = m_extensions->native();
        for (const ExtensionHost::Extension& extension : m_extensions->extensions()) {
            if (!extension.usable() || !extension.manifest->contributes.native) {
                continue;
            }
            const ext::Manifest& manifest = *extension.manifest;
            const bool on = native != nullptr && native->loaded(manifest.id);
            for (const ext::NativeTouchMethod& method : manifest.contributes.native->touchMethods) {
                const QString key = QString::fromStdString(manifest.id + "/" + method.name);
                if (!offered(key)) {
                    offers.push_back({.key = key, .on = on});
                }
            }
        }
    }
    if (!chosen.isEmpty() && !offered(chosen)) {
        offers.push_back({.key = chosen, .on = false});
    }
    return offers;
}

CaptureListing DeviceManager::captureListingOf(const Row& card) const
{
    const std::string phone = m_settings.device(card.deviceId.toStdString()).capture.phone;
    return captureListing(phone, assignedToPhone(phone) && phoneIndexOf(QString::fromStdString(phone)) >= 0);
}

CaptureFeed* DeviceManager::pictureFeedOf(const Row& row) const
{
    if (row.capture != nullptr) {
        return row.capture->feed.get();
    }
    if (row.pictureCard.isEmpty()) {
        return nullptr;
    }
    const int card = indexOf(row.pictureCard);
    return card >= 0 && m_rows[static_cast<std::size_t>(card)].capture != nullptr
               ? m_rows[static_cast<std::size_t>(card)].capture->feed.get()
               : nullptr;
}

std::shared_ptr<media::FrameSnapshots> DeviceManager::shotsOf(const Row& row) const
{
    if (const CaptureFeed* feed = pictureFeedOf(row); feed != nullptr) {
        return feed->snapshots();
    }
    if (row.controller != nullptr) {
        return row.controller->snapshots();
    }
    return row.demo != nullptr ? row.demo->snapshots() : nullptr;
}

QVariantMap DeviceManager::pictureStateOf(const Row& row) const
{
    if (row.capture != nullptr || isBoxRow(row) || !row.host.isEmpty()) {
        return {};
    }
    const settings::PictureSource source = m_settings.effective(row.deviceId.toStdString()).picture.source;
    const int lent = indexOf(row.pictureCard);
    const int card = !row.pictureCard.isEmpty() && lent >= 0 ? lent : cardIndexOf(row.deviceId);
    const Row* shown = card >= 0 ? &m_rows[static_cast<std::size_t>(card)] : nullptr;
    return {{QStringLiteral("source"), pictureSourceName(source)},
            {QStringLiteral("inUse"),
             row.mirror != nullptr && row.picture ? pictureSourceName(*row.picture) : QString()},
            {QStringLiteral("captureSerial"), shown != nullptr ? shown->serial : QString()},
            {QStringLiteral("captureName"), shown != nullptr ? shown->name() : QString()}};
}

DeviceManager::Row* DeviceManager::lentCardOf(const Row& phone)
{
    if (phone.pictureCard.isEmpty()) {
        return nullptr;
    }
    const int card = indexOf(phone.pictureCard);
    if (card < 0 || m_rows[static_cast<std::size_t>(card)].capture == nullptr) {
        return nullptr;
    }
    return &m_rows[static_cast<std::size_t>(card)];
}

std::pair<QString, std::string> DeviceManager::captureTouchWanted(const Row& card) const
{
    const settings::CaptureSettings wanted = m_settings.device(card.deviceId.toStdString()).capture;
    if (card.capture == nullptr || card.capture->drivenBy.isEmpty()) {
        return {QString::fromStdString(wanted.touch), wanted.aoaPhone};
    }
    const int at = indexOf(card.capture->drivenBy);
    if (at < 0 || !m_rows[static_cast<std::size_t>(at)].aoaOnly) {
        return {QStringLiteral("none"), {}};
    }
    const Row& phone = m_rows[static_cast<std::size_t>(at)];
    const settings::Touch touch = m_settings.effective(phone.deviceId.toStdString()).touch;
    if (!touch.extension.empty()) {
        return {QString::fromStdString(touch.extension), {}};
    }
    if (touch.method == settings::TouchMethod::Aoa && phone.usb) {
        return {QStringLiteral("aoa"), phone.usb->serial};
    }
    return {QStringLiteral("none"), {}};
}

settings::PictureSource DeviceManager::usePicture(Row& phone)
{
    returnCapture(phone);
    const settings::Settings effective = m_settings.effective(phone.deviceId.toStdString());
    const bool wantsCapture = effective.picture.source == settings::PictureSource::Capture;
    const bool adb = phone.demo != nullptr || !phone.aoaOnly;
    PhoneLinks links = phoneLinksOf(phone.deviceId, effective.touch);
    links.adb = links.adb || adb;
    PictureInUse chosen = effectivePicture(effective.picture.source, links);
    if (chosen.source == settings::PictureSource::Capture && std::exchange(phone.skipCapture, false)) {
        chosen = {.source = adb ? settings::PictureSource::Phone : settings::PictureSource::None};
    }
    phone.skipCapture = false;
    if (chosen.source == settings::PictureSource::Capture) {
        if (const int card = cardIndexOf(phone.deviceId);
            card >= 0 && borrowCapture(phone, m_rows[static_cast<std::size_t>(card)])) {
            phone.picture = chosen.source;
            return chosen.source;
        }
        chosen = {.source = adb ? settings::PictureSource::Phone : settings::PictureSource::None,
                  .note = adb ? tr("The capture card could not be opened, so the picture comes from the phone")
                              : tr("The capture card could not be opened")};
    }
    if (wantsCapture && phone.pictureNotice.isEmpty()) {
        phone.pictureNotice = chosen.note;
    }
    phone.picture = chosen.source;
    qInfo("the picture of %s comes from %s", qUtf8Printable(phone.name()),
          chosen.source == settings::PictureSource::Phone ? "the phone" : "nowhere");
    return chosen.source;
}

bool DeviceManager::borrowCapture(Row& phone, Row& card)
{
    CaptureRow& state = *card.capture;
    if (state.feed == nullptr || !card.listed || (!state.drivenBy.isEmpty() && state.drivenBy != phone.serial)) {
        return false;
    }
    if (active(card.session.phase) || card.mirror != nullptr) {
        qInfo("capture card %s stops showing on its own: %s shows its picture now", qUtf8Printable(card.name()),
              qUtf8Printable(phone.name()));
        stopRecording(card, false);
        stopCapture(card);
    }
    const QString serial = card.serial;
    state.drivenBy = phone.serial;
    state.feed->setListener([this, serial](CaptureEvent event) {
        QMetaObject::invokeMethod(this, [this, serial, event] { captureEvent(serial, event); }, Qt::QueuedConnection);
    });
    state.feed->setMuted(phone.muted);
    state.feed->setVolume(phone.volume);
    if (!state.feed->start(captureOptionsOf(m_settings.device(card.deviceId.toStdString()).capture))) {
        qWarning("capture card %s: %s", qUtf8Printable(card.name()), qUtf8Printable(state.feed->problem()));
        state.drivenBy.clear();
        return false;
    }
    phone.pictureCard = serial;
    phone.pictureFrames = state.feed->frames();
    if (phone.controller == nullptr && phone.demo == nullptr) {
        phone.pictureChannel = state.channel;
    }
    attachCaptureTouch(card);
    qInfo("the picture of %s comes from the capture card %s", qUtf8Printable(phone.name()),
          qUtf8Printable(card.name()));
    return true;
}

void DeviceManager::returnCapture(Row& phone)
{
    const QString cardSerial = std::exchange(phone.pictureCard, {});
    phone.pictureFrames.reset();
    phone.pictureChannel.reset();
    if (cardSerial.isEmpty()) {
        return;
    }
    phone.thumbnail = {};
    if (const int self = indexOf(phone.serial); self >= 0) {
        const QModelIndex changed = this->index(self);
        emit dataChanged(changed, changed, {ThumbnailRole});
    }
    if (phone.demo != nullptr && phone.demoChannel != nullptr) {
        phone.demoChannel->setVideo(phone.demo->videoSession(), phone.demo->frameWidth(), phone.demo->frameHeight());
    }
    const int index = indexOf(cardSerial);
    if (index < 0) {
        return;
    }
    Row& card = m_rows[static_cast<std::size_t>(index)];
    if (card.capture == nullptr || card.capture->drivenBy != phone.serial) {
        return;
    }
    CaptureRow& state = *card.capture;
    state.drivenBy.clear();
    detachCaptureTouch(card);
    if (state.feed != nullptr) {
        state.feed->stop();
    }
    state.cropEditing = false;
    qInfo("capture card %s no longer shows %s", qUtf8Printable(card.name()), qUtf8Printable(phone.name()));
    if (!card.listed) {
        QMetaObject::invokeMethod(this, [this] { captureDevicesChanged(); }, Qt::QueuedConnection);
    }
    rowChanged(index);
}

void DeviceManager::lentCaptureEvent(Row& card, CaptureEvent event)
{
    CaptureRow& state = *card.capture;
    const int at = indexOf(state.drivenBy);
    if (at < 0) {
        return;
    }
    switch (event) {
    case CaptureEvent::Opened: {
        state.formats = state.feed->formats();
        const QString serial = m_rows[static_cast<std::size_t>(at)].serial;
        qInfo("capture card %s for %s: %s", qUtf8Printable(card.name()),
              qUtf8Printable(m_rows[static_cast<std::size_t>(at)].name()), qUtf8Printable(state.feed->formatText()));
        m_rows[static_cast<std::size_t>(at)].picture = settings::PictureSource::Capture;
        if (const Row& starting = m_rows[static_cast<std::size_t>(at)];
            starting.aoaOnly && (starting.session.phase == session::SessionPhase::Connecting ||
                                 starting.session.phase == session::SessionPhase::Reconnecting)) {
            restartMeasures(m_rows[static_cast<std::size_t>(at)]);
            sessionChanged(serial, {session::SessionPhase::Streaming, std::nullopt});
        }
        const int shown = indexOf(serial);
        if (shown < 0) {
            return;
        }
        Row& phone = m_rows[static_cast<std::size_t>(shown)];
        applyPictureGeometry(phone);
        if (phone.mirror != nullptr && !state.unavailable.isEmpty()) {
            phone.mirror->showToast(tr("%1 is not available, touch is off").arg(state.unavailable),
                                    QStringLiteral("info"));
            state.unavailable.clear();
        }
        updateStatsTimer();
        rowChanged(shown);
        return;
    }
    case CaptureEvent::Cropped:
        applyPictureGeometry(m_rows[static_cast<std::size_t>(at)]);
        break;
    case CaptureEvent::Thumbnailed: {
        Row& phone = m_rows[static_cast<std::size_t>(at)];
        if (std::optional<media::Thumbnail> made = state.feed->takeThumbnail();
            made && made->width > 0 && made->height > 0 && phone.session.phase == session::SessionPhase::Streaming) {
            phone.thumbnail = croppedTo(thumbnailImage(*made), state.feed->crop());
            const QModelIndex changed = this->index(at);
            emit dataChanged(changed, changed, {ThumbnailRole});
        }
        return;
    }
    case CaptureEvent::Failed:
        qWarning("capture card %s stopped while %s showed its picture: %s", qUtf8Printable(card.name()),
                 qUtf8Printable(m_rows[static_cast<std::size_t>(at)].name()), qUtf8Printable(state.feed->problem()));
        lentCaptureLeft(card);
        return;
    }
    rowChanged(at);
}

void DeviceManager::lentCaptureLeft(Row& card)
{
    CaptureRow& state = *card.capture;
    if (state.feed != nullptr) {
        state.feed->stop();
    }
    const int at = indexOf(state.drivenBy);
    if (at < 0) {
        return;
    }
    Row& phone = m_rows[static_cast<std::size_t>(at)];
    const QString serial = phone.serial;
    stopRecording(phone, phone.aoaOnly);
    if (phone.controller != nullptr) {
        if (!phone.controller->active()) {
            returnCapture(phone);
            return;
        }
        qInfo("%s starts again with the picture from the phone: its capture card is gone",
              qUtf8Printable(phone.name()));
        phone.pictureNotice = tr("The capture card was disconnected, so the picture comes from the phone");
        phone.skipCapture = true;
        phone.reconnectWhenStopped = true;
        phone.controller->stop();
        return;
    }
    if (phone.demo != nullptr) {
        releaseInput(serial);
        if (phone.mirror != nullptr) {
            phone.mirror->deleteLater();
            phone.mirror = nullptr;
            emit mirrorsChanged();
        }
        returnCapture(phone);
        phone.pictureNotice = tr("The capture card was disconnected, so the picture comes from the phone");
        phone.skipCapture = true;
        openDemoMirror(phone);
        rowChanged(at);
        return;
    }
    qInfo("%s goes on without a picture: its capture card is gone", qUtf8Printable(phone.name()));
    phone.pictureNotice = tr("The capture card was disconnected, so there is no picture");
    phone.skipCapture = true;
    phone.reconnectWhenStopped = true;
    sessionChanged(serial, {session::SessionPhase::Stopped, std::nullopt});
}

void DeviceManager::lentCaptureBack(Row& card)
{
    CaptureRow& state = *card.capture;
    const int at = indexOf(state.drivenBy);
    if (at < 0 || state.feed == nullptr) {
        return;
    }
    if (!state.feed->start(captureOptionsOf(m_settings.device(card.deviceId.toStdString()).capture))) {
        qWarning("capture card %s: %s", qUtf8Printable(card.name()), qUtf8Printable(state.feed->problem()));
        return;
    }
    qInfo("capture card %s is back: %s shows its picture again", qUtf8Printable(card.name()),
          qUtf8Printable(m_rows[static_cast<std::size_t>(at)].name()));
}

void DeviceManager::applyPictureGeometry(Row& phone)
{
    Row* card = lentCardOf(phone);
    if (card == nullptr || card->capture->feed == nullptr) {
        return;
    }
    applyCaptureGeometry(*card);
    const CaptureFeed& feed = *card->capture->feed;
    if (phone.controller == nullptr) {
        phone.rotation = card->rotation;
    }
    if (phone.demo != nullptr && phone.demoChannel != nullptr) {
        const QSize shown = feed.cropSize();
        if (shown.width() > 0 && shown.height() > 0) {
            phone.demoChannel->setVideo(feed.touchSession(), static_cast<std::uint16_t>(shown.width()),
                                        static_cast<std::uint16_t>(shown.height()));
        }
    }
    if (phone.mirror != nullptr) {
        const bool right =
            m_settings.device(card->deviceId.toStdString()).capture.landscape == settings::CaptureLandscape::Right;
        phone.mirror->setCaptureCard(static_cast<int>(right ? ControlTurn::LandscapeRight : ControlTurn::LandscapeLeft),
                                     feed.manualCrop());
        if (!card->capture->cropEditing) {
            phone.mirror->setCrop(rectOf(feed.crop()));
        }
    }
    cropGridCell(phone);
    if (phone.logic != nullptr) {
        phone.logic->setDevice(deviceInfoOf(phone));
    }
    for (const std::unique_ptr<TaskRun>& run : m_taskRuns) {
        if (run->serial == phone.serial && run->runtime != nullptr) {
            run->runtime->setDevice(deviceInfoOf(phone));
        }
    }
    if (m_extensionLogic != nullptr) {
        m_extensionLogic->updateSession(phone.serial, deviceInfoOf(phone), logicKeyOf(phone));
    }
}

void DeviceManager::showNoPicture(Row& row)
{
    if (row.mirror == nullptr) {
        return;
    }
    const auto turn = static_cast<int>(turnOfRotation(row.rotation));
    row.mirror->setNoPicture(row.screen.isEmpty() ? controlCanvasOf(row, turn) : row.screen, turn);
}

void DeviceManager::openDemoMirror(Row& row)
{
    usePicture(row);
    restartMeasures(row);
    openMirror(row);
}

void DeviceManager::bumpLinks()
{
    if (std::exchange(m_linksPending, true)) {
        return;
    }
    QMetaObject::invokeMethod(this, [this] {
        m_linksPending = false;
        ++m_linkRevision;
        emit linkRevisionChanged();
        QStringList listed;
        for (const Row& row : m_rows) {
            if (row.capture != nullptr && carded(row)) {
                listed.append(row.serial);
            }
        }
        if (listed != m_listedCaptures) {
            m_listedCaptures = std::move(listed);
            refreshCaptureRows();
            emit countsChanged();
        }
    }, Qt::QueuedConnection);
}

void DeviceManager::refreshCaptureRows()
{
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].capture != nullptr) {
            const QModelIndex changed = index(static_cast<int>(i));
            emit dataChanged(changed, changed);
        }
    }
}

QVariantMap DeviceManager::pictureAndTouch(const QString& deviceId) const
{
    const settings::Settings effective = m_settings.effective(deviceId.toStdString());
    const settings::PictureSource source = effective.picture.source;
    const PhoneLinks links = phoneLinksOf(deviceId, effective.touch);
    const int phone = phoneIndexOf(deviceId);
    const std::optional<settings::PictureSource> inUse =
        phone >= 0 && m_rows[static_cast<std::size_t>(phone)].mirror != nullptr
            ? m_rows[static_cast<std::size_t>(phone)].picture
            : std::nullopt;
    QString note;
    if (inUse && *inUse != source) {
        const PictureInUse now = effectivePicture(source, links);
        note = now.source == *inUse ? now.note : tr("Changes take effect the next time this phone is mirrored");
    }
    QVariantMap capture;
    if (const int card = cardIndexOf(deviceId); card >= 0) {
        capture = {{QStringLiteral("serial"), m_rows[static_cast<std::size_t>(card)].serial},
                   {QStringLiteral("name"), m_rows[static_cast<std::size_t>(card)].name()}};
    }
    const LinkChoices choices = linkChoices(links);
    return {{QStringLiteral("source"), pictureSourceName(source)},
            {QStringLiteral("inUse"), inUse ? pictureSourceName(*inUse) : QString()},
            {QStringLiteral("note"), note},
            {QStringLiteral("capture"), capture},
            {QStringLiteral("pictureChoices"), pictureChoiceList(choices.picture)},
            {QStringLiteral("touchChoices"), touchChoiceList(choices.touch)}};
}

QVariantList DeviceManager::phonesForCapture(const QString& captureSerial) const
{
    QVariantList phones;
    const int index = indexOf(captureSerial);
    if (index < 0 || m_rows[static_cast<std::size_t>(index)].capture == nullptr) {
        return phones;
    }
    const auto add = [&phones](const QString& deviceId, const QString& serial, const QString& name, bool online,
                               const QString& kind) {
        phones.append(QVariantMap{{QStringLiteral("deviceId"), deviceId},
                                  {QStringLiteral("serial"), serial},
                                  {QStringLiteral("name"), name},
                                  {QStringLiteral("online"), online},
                                  {QStringLiteral("kind"), kind}});
    };
    std::set<QString> listed;
    for (const Row& row : m_rows) {
        if (row.deviceId.isEmpty() || !row.host.isEmpty() || row.capture != nullptr || isBoxRow(row) ||
            !carded(row)) {
            continue;
        }
        if (row.demo != nullptr) {
            add(row.deviceId, row.serial, row.name(), true, QStringLiteral("demo"));
        } else if (row.aoaOnly) {
            add(row.deviceId, row.serial, row.name(), row.listed, QStringLiteral("aoa"));
        } else if (row.listed && row.state == adb::DeviceState::Device) {
            add(row.deviceId, row.serial, row.name(), true, QStringLiteral("adb"));
        } else {
            continue;
        }
        listed.insert(row.deviceId);
    }
    for (const platform::UsbPhone& usb : m_usbAlone) {
        const QString deviceId = QString::fromStdString(usb.serial);
        if (deviceId.isEmpty() || listed.contains(deviceId)) {
            continue;
        }
        listed.insert(deviceId);
        QString name = QString::fromStdString(usb.name);
        if (const auto known = m_knownNames.find(deviceId); known != m_knownNames.end()) {
            name = DevicePresentation::nameOf(known->second[0], known->second[1], known->second[2]);
        }
        add(deviceId, aoaSerialOf(deviceId), name.isEmpty() ? deviceId : name, true, QStringLiteral("aoa"));
    }
    return phones;
}

void DeviceManager::assignCapture(const QString& captureSerial, const QString& phoneDeviceId)
{
    const int index = indexOf(captureSerial);
    if (index < 0 || m_rows[static_cast<std::size_t>(index)].capture == nullptr) {
        return;
    }
    const std::string phone = phoneDeviceId.toStdString();
    if (phone.size() > settings::CaptureSettings::kMaxPhoneLength) {
        qWarning("capture card %s: the id of the phone is too long to remember", qUtf8Printable(captureSerial));
        return;
    }
    const std::string id = captureSerial.toStdString();
    settings::DeviceSettings card = m_settings.device(id);
    card.capture.phone = phone;
    m_settings.setDevice(id, card);
    if (assignedToPhone(phone)) {
        m_assignOrder[captureSerial] = ++m_lastAssign;
        settings::DeviceSettings shown = m_settings.device(phone);
        if (settings::setOverride(shown, "picture.source", settings::Value(std::string("capture")))) {
            m_settings.setDevice(phone, shown);
        }
        qInfo("capture card %s shows the phone %s; that phone's picture comes from it from its next mirroring on",
              qUtf8Printable(captureSerial), qUtf8Printable(phoneDeviceId));
    } else {
        m_assignOrder.erase(captureSerial);
        qInfo("capture card %s is %s", qUtf8Printable(captureSerial),
              phone.empty() ? "not given to a phone yet" : "used on its own");
    }
    usbChanged();
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        const Row& row = m_rows[i];
        if (row.serial == captureSerial || (!phoneDeviceId.isEmpty() && row.deviceId == phoneDeviceId)) {
            rowChanged(static_cast<int>(i));
        }
    }
    bumpLinks();
}

std::vector<PhoneSource> DeviceManager::phoneSources() const
{
    std::vector<PhoneSource> phones;
    for (const Row& row : m_rows) {
        if (row.deviceId.isEmpty() || !row.host.isEmpty() || row.capture != nullptr || isBoxRow(row) ||
            row.aoaOnly || !carded(row)) {
            continue;
        }
        QImage current;
        if (row.mirror != nullptr && row.picture == settings::PictureSource::Phone) {
            if (const std::shared_ptr<media::FrameSnapshots> shots = shotsOf(row); shots != nullptr) {
                if (const std::shared_ptr<const media::LumaSnapshot> frame = shots->capture(false); frame != nullptr) {
                    current = lumaPicture(*frame);
                }
            }
        }
        const bool reachable = row.demo == nullptr && row.listed && row.state == adb::DeviceState::Device;
        if (current.isNull() && !reachable) {
            continue;
        }
        phones.push_back({.deviceId = row.deviceId, .serial = row.serial, .current = std::move(current)});
    }
    return phones;
}

int DeviceManager::compareCapture(int ticket, const Row& card)
{
    const std::shared_ptr<const media::LumaSnapshot> shot = card.capture->feed->snapshots()->capture(false);
    const QImage picture = shot != nullptr ? lumaPicture(*shot) : QImage();
    if (picture.isNull()) {
        qInfo("capture card %s shows no picture yet, so there is nothing to compare with the phones",
              qUtf8Printable(card.name()));
        return 0;
    }
    std::vector<PhoneSource> phones = phoneSources();
    if (phones.empty()) {
        return 0;
    }
    const int matcherTicket = m_matcher->suggest(card.serial, picture, std::move(phones));
    if (matcherTicket != 0) {
        m_suggestTickets[matcherTicket] = ticket;
    }
    return matcherTicket;
}

int DeviceManager::suggestPhoneForCapture(const QString& captureSerial)
{
    const int index = indexOf(captureSerial);
    if (index < 0 || m_matcher == nullptr) {
        return 0;
    }
    Row& card = m_rows[static_cast<std::size_t>(index)];
    if (card.capture == nullptr || card.capture->feed == nullptr || phoneSources().empty()) {
        return 0;
    }
    const int ticket = ++m_lastSuggest;
    if (card.capture->feed->running()) {
        if (compareCapture(ticket, card) == 0) {
            QMetaObject::invokeMethod(
                this, [this, ticket, captureSerial] { emit phoneSuggested(ticket, captureSerial, {}, 0.0); },
                Qt::QueuedConnection);
        }
        return ticket;
    }
    if (const auto waiting = m_probes.find(captureSerial); waiting != m_probes.end()) {
        const int replaced = std::exchange(waiting->second.ticket, ticket);
        QMetaObject::invokeMethod(
            this, [this, replaced, captureSerial] { emit phoneSuggested(replaced, captureSerial, {}, 0.0); },
            Qt::QueuedConnection);
        return ticket;
    }
    const settings::CaptureSettings wanted = m_settings.device(card.deviceId.toStdString()).capture;
    card.capture->feed->setListener([this, captureSerial](CaptureEvent event) {
        QMetaObject::invokeMethod(
            this, [this, captureSerial, event] { captureEvent(captureSerial, event); }, Qt::QueuedConnection);
    });
    if (!card.capture->feed->start(captureOptionsOf(wanted))) {
        qWarning("capture card %s cannot open to compare its picture: %s", qUtf8Printable(card.name()),
                 qUtf8Printable(card.capture->feed->problem()));
        QMetaObject::invokeMethod(
            this, [this, ticket, captureSerial] { emit phoneSuggested(ticket, captureSerial, {}, 0.0); },
            Qt::QueuedConnection);
        return ticket;
    }
    qInfo("opening the capture card %s to compare its picture with the phones", qUtf8Printable(card.name()));
    m_probes[captureSerial] = CaptureProbe{.ticket = ticket, .cropped = 0, .ready = manualCropOf(wanted).has_value()};
    QTimer::singleShot(kCaptureProbeLimit, this, [this, captureSerial, ticket] { // source-rule-allow R9: a capture card that never settles is compared as it is after a while
        const auto waiting = m_probes.find(captureSerial);
        if (waiting != m_probes.end() && waiting->second.ticket == ticket) {
            finishProbe(captureSerial, true);
        }
    });
    return ticket;
}

void DeviceManager::probeEvent(const QString& captureSerial, CaptureEvent event)
{
    const auto probe = m_probes.find(captureSerial);
    if (probe == m_probes.end()) {
        return;
    }
    switch (event) {
    case CaptureEvent::Failed:
        finishProbe(captureSerial, false);
        return;
    case CaptureEvent::Cropped:
        if (++probe->second.cropped >= 2) {
            probe->second.ready = true;
        }
        return;
    case CaptureEvent::Thumbnailed:
        if (probe->second.ready) {
            finishProbe(captureSerial, true);
        }
        return;
    case CaptureEvent::Opened:
        return;
    }
}

void DeviceManager::finishProbe(const QString& captureSerial, bool compare)
{
    const auto probe = m_probes.find(captureSerial);
    if (probe == m_probes.end()) {
        return;
    }
    const int ticket = probe->second.ticket;
    m_probes.erase(probe);
    bool asked = false;
    const int index = indexOf(captureSerial);
    if (index >= 0 && m_rows[static_cast<std::size_t>(index)].capture != nullptr &&
        m_rows[static_cast<std::size_t>(index)].capture->feed != nullptr) {
        Row& card = m_rows[static_cast<std::size_t>(index)];
        asked = compare && compareCapture(ticket, card) != 0;
        if (!active(card.session.phase) && card.mirror == nullptr && card.capture->drivenBy.isEmpty()) {
            card.capture->feed->stop();
            qInfo("capture card %s is closed again after comparing its picture", qUtf8Printable(card.name()));
        }
    }
    if (!asked) {
        emit phoneSuggested(ticket, captureSerial, {}, 0.0);
    }
}

void DeviceManager::matcherSuggested(int matcherTicket, const QString& captureSerial, const QString& deviceId,
                                     double score)
{
    const auto found = m_suggestTickets.find(matcherTicket);
    if (found == m_suggestTickets.end()) {
        return;
    }
    const int ticket = found->second;
    m_suggestTickets.erase(found);
    emit phoneSuggested(ticket, captureSerial, deviceId, score);
}

bool DeviceManager::isEsp32Row(const Row& row) noexcept
{
    return !row.esp32Where.isEmpty();
}

bool DeviceManager::isBoxRow(const Row& row) noexcept
{
    return isEsp32Row(row) || row.provided.has_value();
}

bool DeviceManager::noPictureOf(const Row& row) noexcept
{
    if (row.picture) {
        return *row.picture == settings::PictureSource::None;
    }
    return row.aoaOnly || isBoxRow(row);
}

void DeviceManager::syncEsp32()
{
    if (m_esp32 == nullptr) {
        return;
    }
    m_esp32->setSettings(esp32SettingsOf(m_settings.effective({}).esp32));
    const NativeExtensions* native = m_extensions != nullptr ? m_extensions->native() : nullptr;
    m_esp32->setActive(native != nullptr && native->loaded(kEsp32Extension));
}

void DeviceManager::esp32BoardsChanged()
{
    if (m_esp32 == nullptr) {
        return;
    }
    std::set<QString> present;
    for (const Esp32Found& found : m_esp32->boards()) {
        const QString where = QString::fromStdString(found.where);
        const QString serial = esp32SerialOf(where);
        present.insert(serial);
        if (indexOf(serial) >= 0) {
            continue;
        }
        const int at = static_cast<int>(m_rows.size());
        beginInsertRows({}, at, at);
        Row row;
        row.serial = serial;
        row.esp32Where = where;
        row.esp32Wifi = found.wifi;
        row.listed = true;
        row.state = adb::DeviceState::Unknown;
        row.deviceId = serial;
        qInfo("ESP32 touch box on %s: it gets a card on the devices page", qUtf8Printable(where));
        m_rows.push_back(std::move(row));
        endInsertRows();
    }
    for (int i = static_cast<int>(m_rows.size()) - 1; i >= 0; --i) {
        Row& row = m_rows[static_cast<std::size_t>(i)];
        if (!isEsp32Row(row)) {
            continue;
        }
        row.listed = present.contains(row.serial);
        if (!row.listed && !active(row.session.phase)) {
            removeRow(i);
        }
    }
    if (!m_rows.empty()) {
        emit dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1));
    }
    emit countsChanged();
}

void DeviceManager::connectEsp32(Row& row)
{
    if (active(row.session.phase) || m_esp32 == nullptr) {
        return;
    }
    const QString serial = row.serial;
    if (row.esp32 == nullptr) {
        row.esp32 = new Esp32Session(row.name(), this);
        connect(row.esp32, &Esp32Session::stateChanged, this,
                [this, serial](const session::SessionState& state) { sessionChanged(serial, state); });
        connect(row.esp32, &Esp32Session::padLinkChanged, this,
                [this, channel = row.esp32->channel()] { m_input->padLinkChanged(channel); });
    }
    const int turn = controlTurnOf(row);
    row.rotation = rotationOf(static_cast<ControlTurn>(turn));
    m_esp32->claim(serial, Esp32Claim{.where = row.esp32Where.toStdString(), .wifi = row.esp32Wifi});
    qInfo("controlling a phone through the ESP32 touch box on %s, without a picture",
          qUtf8Printable(row.esp32Where));
    TouchMethodSource* source = m_extensions != nullptr ? m_extensions->touchSource() : nullptr;
    row.esp32->start(source, serial.toStdString(), row.rotation, controlCanvasOf(row, turn));
}

std::optional<Esp32Found> DeviceManager::esp32BoardOf(const Row& row) const
{
    if (m_esp32 == nullptr || !isEsp32Row(row)) {
        return std::nullopt;
    }
    return m_esp32->board(row.esp32Where.toStdString(), row.esp32Wifi);
}

bool DeviceManager::esp32Usable(const Row& row) const
{
    const std::optional<Esp32Found> found = esp32BoardOf(row);
    if (!found) {
        return false;
    }
    return found->held ? found->heldBy == row.serial : found->connected;
}

QVariantMap DeviceManager::esp32State(const Row& row) const
{
    if (!isEsp32Row(row)) {
        return {};
    }
    const std::optional<Esp32Found> found = esp32BoardOf(row);
    QString chip;
    QString firmware;
    QString boardName;
    QString phone;
    const bool legacy = found && found->legacy;
    if (found && found->info) {
        chip = QString::fromStdString(esp32::chipName(found->info->chipId));
        firmware = QString::fromStdString(found->info->firmware);
        boardName = QString::fromStdString(found->info->name);
    }
    if (found && !legacy) {
        phone = Esp32Panel::phoneText(found->status);
    }
    QString inUseBy;
    if (found && found->held && found->heldBy != row.serial) {
        const int at = indexOf(found->heldBy);
        inUseBy = found->flashing ? tr("the firmware flasher")
                  : at >= 0       ? m_rows[static_cast<std::size_t>(at)].name()
                                  : found->heldBy;
    }
    QString summary;
    if (!inUseBy.isEmpty()) {
        summary = tr("In use by %1").arg(inUseBy);
    } else {
        QStringList parts;
        parts.append(legacy ? tr("ESP32 with the old firmware, flashing the new firmware is recommended")
                     : chip.isEmpty() ? tr("ESP32")
                                      : chip);
        if (!firmware.isEmpty()) {
            parts.append(tr("firmware %1").arg(firmware));
        }
        parts.append(row.esp32Wifi ? tr("Wi-Fi %1").arg(row.esp32Where) : tr("serial port %1").arg(row.esp32Where));
        if (!phone.isEmpty()) {
            parts.append(phone);
        }
        summary = parts.join(QStringLiteral(" · "));
    }
    return {{QStringLiteral("isEsp32"), true},
            {QStringLiteral("where"), row.esp32Where},
            {QStringLiteral("wifi"), row.esp32Wifi},
            {QStringLiteral("chip"), chip},
            {QStringLiteral("firmware"), firmware},
            {QStringLiteral("boardName"), boardName},
            {QStringLiteral("legacy"), legacy},
            {QStringLiteral("phone"), phone},
            {QStringLiteral("inUse"), !inUseBy.isEmpty()},
            {QStringLiteral("connected"), found && found->connected},
            {QStringLiteral("summary"), summary}};
}

std::optional<Advice> DeviceManager::esp32AdviceOf(const Row& row) const
{
    if (row.session.phase != session::SessionPhase::Failed || !row.session.failure) {
        return std::nullopt;
    }
    const bool lost = row.session.failure->kind == session::FailureKind::ConnectionLost;
    return Advice{lost ? tr("The ESP32 touch box stopped answering") : tr("The ESP32 touch box did not open"),
                  tr("Check the board's cables and that the ESP32 extension is on, then choose Control again")};
}

DevicePresentation DeviceManager::esp32Presentation(const Row& row) const
{
    const session::SessionPhase phase = row.session.phase;
    if (phase == session::SessionPhase::Streaming && noPictureOf(row)) {
        return {tr("Controlling"), DevicePresentation::Tone::Live, -1, false, 0};
    }
    if (phase != session::SessionPhase::Idle && phase != session::SessionPhase::Stopped) {
        return DevicePresentation::of(phase, row.session.failure, row.listed, adb::DeviceState::Device);
    }
    if (!row.listed) {
        return DevicePresentation::of(phase, row.session.failure, false, adb::DeviceState::Unknown);
    }
    if (!esp32Usable(row)) {
        return {tr("In use"), DevicePresentation::Tone::Warn, -1, false, 2};
    }
    return DevicePresentation::of(phase, row.session.failure, true, adb::DeviceState::Device);
}

void DeviceManager::disconnectBoard(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0 || !isEsp32Row(m_rows[static_cast<std::size_t>(index)])) {
        return;
    }
    const std::string where = m_rows[static_cast<std::size_t>(index)].esp32Where.toStdString();
    const bool wifi = m_rows[static_cast<std::size_t>(index)].esp32Wifi;
    disconnectDevice(serial);
    if (m_esp32 != nullptr) {
        m_esp32->dismiss(where, wifi);
    }
}

QVariantList DeviceManager::esp32Ports() const
{
    return Esp32Panel::portsOf(esp32::listSerialPorts());
}

void DeviceManager::syncProvided()
{
    const NativeExtensions* native = m_extensions != nullptr ? m_extensions->native() : nullptr;
    const std::vector<ProvidedDevice> devices =
        native != nullptr ? native->providedDevices() : std::vector<ProvidedDevice>{};
    std::set<QString> present;
    for (const ProvidedDevice& device : devices) {
        const QString serial = QStringLiteral("box:%1/%2/%3")
                                   .arg(QString::fromStdString(device.extension), QString::fromStdString(device.provider),
                                        QString::fromStdString(device.id));
        present.insert(serial);
        if (const int found = indexOf(serial); found >= 0) {
            Row& row = m_rows[static_cast<std::size_t>(found)];
            row.provided = device;
            row.listed = device.state != u"offline";
            rowChanged(found);
            continue;
        }
        const int at = static_cast<int>(m_rows.size());
        beginInsertRows({}, at, at);
        Row row;
        row.serial = serial;
        row.deviceId = serial;
        row.provided = device;
        row.listed = device.state != u"offline";
        row.state = adb::DeviceState::Unknown;
        qInfo("%s adds %s to the devices page", qUtf8Printable(device.extensionName), qUtf8Printable(device.name));
        m_rows.push_back(std::move(row));
        endInsertRows();
    }
    for (int i = static_cast<int>(m_rows.size()) - 1; i >= 0; --i) {
        Row& row = m_rows[static_cast<std::size_t>(i)];
        if (!row.provided || present.contains(row.serial)) {
            continue;
        }
        row.listed = false;
        if (!active(row.session.phase)) {
            removeRow(i);
        } else {
            rowChanged(i);
        }
    }
    emit countsChanged();
}

void DeviceManager::syncSources()
{
    const NativeExtensions* native = m_extensions != nullptr ? m_extensions->native() : nullptr;
    const std::vector<MediaSourceDevice> sources =
        native != nullptr ? native->mediaSources() : std::vector<MediaSourceDevice>{};
    std::vector<QString> shown;
    for (const Row& row : m_rows) {
        if (row.capture != nullptr && !row.capture->sourceExtension.empty()) {
            shown.push_back(row.serial);
        }
    }
    const SourcePlan plan = planExtensionSources(sources, shown);
    for (const MediaSourceDevice& source : plan.added) {
        addSourceRow(extensionSourceSerial(source), source);
    }
    for (int i = static_cast<int>(m_rows.size()) - 1; i >= 0; --i) {
        Row& row = m_rows[static_cast<std::size_t>(i)];
        if (row.capture == nullptr || row.capture->sourceExtension.empty()) {
            continue;
        }
        const bool present = std::ranges::find(plan.removed, row.serial) == plan.removed.end();
        const bool was = row.listed;
        row.listed = present;
        if (present) {
            const auto listed = std::ranges::find_if(sources, [&row](const MediaSourceDevice& source) {
                return source.kind == exthost::MediaKind::Video && extensionSourceSerial(source) == row.serial;
            });
            if (listed != sources.end() && listed->name != row.capture->name) {
                row.capture->name = listed->name;
                rowChanged(i);
            }
            row.capture->goneShown = false;
            if (!was && !row.capture->drivenBy.isEmpty()) {
                lentCaptureBack(row);
            } else if (!was && row.session.phase == session::SessionPhase::Reconnecting) {
                startCapture(row);
            }
            continue;
        }
        if (!row.capture->drivenBy.isEmpty()) {
            if (was) {
                qInfo("the picture source %s is gone while a phone showed it", qUtf8Printable(row.name()));
                lentCaptureLeft(row);
            }
            rowChanged(i);
            continue;
        }
        if (!active(row.session.phase) && row.mirror == nullptr) {
            qInfo("%s no longer offers the picture %s", qUtf8Printable(row.capture->sourceExtensionName),
                  qUtf8Printable(row.name()));
            removeRow(i);
            continue;
        }
        if (was) {
            qInfo("the picture source %s is gone while it was in use", qUtf8Printable(row.name()));
            if (row.mirror != nullptr && !row.capture->goneShown) {
                row.mirror->showToast(tr("The picture source was disconnected"), QStringLiteral("unlink"));
                row.capture->goneShown = true;
            }
            if (row.capture->feed != nullptr) {
                row.capture->feed->stop();
            }
            detachCaptureTouch(row);
            row.session = {session::SessionPhase::Reconnecting, std::nullopt};
        }
        rowChanged(i);
    }
    updateStatsTimer();
    usbChanged();
    emit countsChanged();
}

void DeviceManager::addSourceRow(const QString& serial, const MediaSourceDevice& source)
{
    if (indexOf(serial) >= 0 || m_graphics == nullptr) {
        return;
    }
    auto state = std::make_unique<CaptureRow>();
    state->name = source.name;
    state->sourceExtension = source.extension;
    state->sourceExtensionName = source.extensionName;
    ExtensionVideoSource::Opener open = [this, video = source](exthost::MediaKind kind,
                                                              const std::string& params) -> std::unique_ptr<SourceLease> {
        const NativeExtensions* native = m_extensions != nullptr ? m_extensions->native() : nullptr;
        if (native == nullptr) {
            return nullptr;
        }
        if (kind == exthost::MediaKind::Video) {
            return native->openSource(video, params);
        }
        const std::optional<MediaSourceDevice> sound = pairedSound(native->mediaSources(), video);
        return sound ? native->openSource(*sound, params) : nullptr;
    };
    ExtensionVideoSource::Phone phone = [this, serial]() -> std::string {
        const int index = indexOf(serial);
        if (index < 0 || m_rows[static_cast<std::size_t>(index)].capture == nullptr) {
            return {};
        }
        const int shown = indexOf(m_rows[static_cast<std::size_t>(index)].capture->drivenBy);
        return shown >= 0 ? m_rows[static_cast<std::size_t>(shown)].deviceId.toStdString() : std::string();
    };
    state->feed = std::make_unique<CaptureFeed>(
        m_graphics->device(), std::make_unique<ExtensionVideoSource>(std::move(open), std::move(phone)));
    state->channel->attach(state->dropping);
    const int at = static_cast<int>(m_rows.size());
    beginInsertRows({}, at, at);
    Row row;
    row.serial = serial;
    row.listed = true;
    row.state = adb::DeviceState::Device;
    row.deviceId = serial;
    row.capture = std::move(state);
    m_rows.push_back(std::move(row));
    endInsertRows();
    qInfo("%s adds the picture %s to the devices page (%s)", qUtf8Printable(source.extensionName),
          qUtf8Printable(source.name), qUtf8Printable(serial));
    emit countsChanged();
}

bool DeviceManager::setAudioTap(const QString& serial,
                                std::function<void(std::span<const float>, int, int, std::int64_t)> tap)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return false;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (CaptureFeed* feed = pictureFeedOf(row); feed != nullptr) {
        feed->setAudioTap(std::move(tap));
        return true;
    }
    if (row.controller != nullptr) {
        row.controller->setAudioTap(std::move(tap));
        return true;
    }
    return false;
}

void DeviceManager::connectProvided(Row& row)
{
    if (active(row.session.phase) || !row.provided || row.provided->touchMethod.empty()) {
        return;
    }
    const QString serial = row.serial;
    if (row.esp32 == nullptr) {
        row.esp32 = new Esp32Session(row.name(), this, row.provided->touchMethod);
        connect(row.esp32, &Esp32Session::stateChanged, this,
                [this, serial](const session::SessionState& state) { sessionChanged(serial, state); });
        connect(row.esp32, &Esp32Session::padLinkChanged, this,
                [this, channel = row.esp32->channel()] { m_input->padLinkChanged(channel); });
    }
    const int turn = controlTurnOf(row);
    row.rotation = rotationOf(static_cast<ControlTurn>(turn));
    qInfo("controlling a phone through %s from %s, without a picture", qUtf8Printable(row.provided->name),
          qUtf8Printable(row.provided->extensionName));
    TouchMethodSource* source = m_extensions != nullptr ? m_extensions->touchSource() : nullptr;
    row.esp32->start(source, serial.toStdString(), row.rotation, controlCanvasOf(row, turn));
}

void DeviceManager::startProvidedJob(Row& row, const QString& job, const QString& title)
{
    const NativeExtensions* native = m_extensions != nullptr ? m_extensions->native() : nullptr;
    if (native == nullptr || m_jobs == nullptr || !row.provided) {
        return;
    }
    const ProvidedDevice device = *row.provided;
    const QString serial = row.serial;
    const auto shown = [title](bool running, bool failed, double fraction, const QString& text) {
        return QVariantMap{{QStringLiteral("title"), title},
                           {QStringLiteral("running"), running},
                           {QStringLiteral("failed"), failed},
                           {QStringLiteral("finished"), !running && !failed},
                           {QStringLiteral("fraction"), fraction},
                           {QStringLiteral("text"), text}};
    };
    const auto problemOf = [&device](const std::string& code, const std::string& problem) {
        if (code == "GS-API-067") {
            return tr("%1 is already doing this as often as it may; wait for it to end").arg(device.extensionName);
        }
        if (code == "GS-API-063") {
            return tr("%1 has not been allowed everything this needs; turn it off and on again on the Extensions page "
                      "to allow it")
                .arg(device.extensionName);
        }
        return QString::fromStdString(problem);
    };
    const NativeJobTarget target = native->jobTarget(device.extension, job.toStdString());
    if (!target.code.empty()) {
        showProvidedJob(serial, shown(false, true, -1.0, problemOf(target.code, target.problem)));
        return;
    }
    const bool wantsDevice = std::ranges::any_of(target.declared.params, [](const ext::NativeParam& param) {
        return param.name == "device" && param.type == "string";
    });
    const nlohmann::json request{
        {"extension", device.extension},
        {"job", job.toStdString()},
        {"args", wantsDevice ? nlohmann::json{{"device", device.id}} : nlohmann::json::object()}};
    const QPointer<DeviceManager> self(this);
    HostJobEvents events{
        .progress =
            [self, serial, shown](double fraction, const QString& text) {
                if (self) {
                    self->showProvidedJob(serial, shown(true, false, fraction, text));
                }
            },
        .done =
            [self, serial, shown](bool worked, const QString& text) {
                if (self) {
                    self->showProvidedJob(serial, shown(false, !worked, -1.0, worked ? QString() : text));
                }
            }};
    showProvidedJob(serial, shown(true, false, -1.0, QString()));
    const script::ServiceAnswer started = m_jobs->startForHost(request.dump(), std::move(events));
    if (!started.code.empty()) {
        showProvidedJob(serial, shown(false, true, -1.0, problemOf(started.code, started.problem)));
    }
}

void DeviceManager::showProvidedJob(const QString& serial, QVariantMap shown)
{
    if (const int at = indexOf(serial); at >= 0) {
        m_rows[static_cast<std::size_t>(at)].providedJob = std::move(shown);
        rowChanged(at);
    }
}

void DeviceManager::runDeviceAction(const QString& serial, const QString& action)
{
    const int index = indexOf(serial);
    if (index < 0 || !m_rows[static_cast<std::size_t>(index)].provided) {
        return;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    const ProvidedDevice device = *row.provided;
    for (const QVariant& entry : device.actions) {
        const QVariantMap fields = entry.toMap();
        if (fields.value(QStringLiteral("id")).toString() != action) {
            continue;
        }
        const QString panel = fields.value(QStringLiteral("panel")).toString();
        const QString job = fields.value(QStringLiteral("job")).toString();
        if (!panel.isEmpty()) {
            emit devicePanelWanted(QString::fromStdString(device.extension), panel, QString::fromStdString(device.id),
                                   device.name);
        } else if (!job.isEmpty()) {
            startProvidedJob(row, job, fields.value(QStringLiteral("title")).toString());
        } else if (NativeExtensions* native = m_extensions != nullptr ? m_extensions->native() : nullptr) {
            const exthost::CallResult done =
                native->actOnDevice(device.extension, device.provider, device.id, action.toStdString());
            if (!done.ok()) {
                qWarning("%s: the action %s of %s did not work: %s", qUtf8Printable(device.extensionName),
                         qUtf8Printable(action), qUtf8Printable(device.name), done.problem.c_str());
            }
            showProvidedJob(serial, {{QStringLiteral("title"), fields.value(QStringLiteral("title")).toString()},
                                     {QStringLiteral("running"), false},
                                     {QStringLiteral("failed"), !done.ok()},
                                     {QStringLiteral("finished"), done.ok()},
                                     {QStringLiteral("text"), QString::fromStdString(done.problem)}});
        }
        return;
    }
}

namespace {

QString claimedBy(const ProvidedDevice& device)
{
    const HardwareClaims* claims = HardwareClaims::instance();
    return claims != nullptr && !device.resource.empty() ? claims->holderName(device.resource) : QString();
}

}

QVariantMap DeviceManager::providedState(const Row& row) const
{
    if (!row.provided) {
        return {};
    }
    const ProvidedDevice& device = *row.provided;
    return {{QStringLiteral("isProvided"), true},
            {QStringLiteral("claimedBy"), claimedBy(device)},
            {QStringLiteral("extension"), QString::fromStdString(device.extension)},
            {QStringLiteral("extensionName"), device.extensionName},
            {QStringLiteral("provider"), QString::fromStdString(device.provider)},
            {QStringLiteral("id"), QString::fromStdString(device.id)},
            {QStringLiteral("name"), device.name},
            {QStringLiteral("kind"), device.kind},
            {QStringLiteral("summary"), device.summary},
            {QStringLiteral("state"), device.state},
            {QStringLiteral("stateText"), device.stateText},
            {QStringLiteral("icon"), device.icon},
            {QStringLiteral("iconPath"), device.iconPath},
            {QStringLiteral("capture"), device.capture},
            {QStringLiteral("canControl"), !device.touchMethod.empty()},
            {QStringLiteral("actions"), device.actions},
            {QStringLiteral("job"), row.providedJob}};
}

std::optional<Advice> DeviceManager::providedAdviceOf(const Row& row) const
{
    if (!row.provided || row.session.phase != session::SessionPhase::Failed || !row.session.failure) {
        return std::nullopt;
    }
    const bool lost = row.session.failure->kind == session::FailureKind::ConnectionLost;
    return Advice{lost ? tr("%1 stopped answering").arg(row.provided->name) : tr("%1 did not open").arg(row.provided->name),
                  tr("Check that %1 is on and the device is connected, then choose Control again")
                      .arg(row.provided->extensionName)};
}

DevicePresentation DeviceManager::providedPresentation(const Row& row) const
{
    const session::SessionPhase phase = row.session.phase;
    if (phase == session::SessionPhase::Streaming && noPictureOf(row)) {
        return {tr("Controlling"), DevicePresentation::Tone::Live, -1, false, 0};
    }
    if (phase != session::SessionPhase::Idle && phase != session::SessionPhase::Stopped) {
        return DevicePresentation::of(phase, row.session.failure, row.listed, adb::DeviceState::Device);
    }
    if (!row.listed) {
        return DevicePresentation::of(phase, row.session.failure, false, adb::DeviceState::Unknown);
    }
    if (const QString holder = claimedBy(*row.provided); !holder.isEmpty()) {
        return {tr("In use by %1").arg(holder), DevicePresentation::Tone::Warn, -1, false, 2};
    }
    if (row.provided->state == u"busy") {
        return {tr("In use"), DevicePresentation::Tone::Warn, -1, false, 2};
    }
    if (row.provided->state == u"problem") {
        return {row.provided->stateText.isEmpty() ? tr("Needs attention") : row.provided->stateText,
                DevicePresentation::Tone::Warn, -1, false, 2};
    }
    return DevicePresentation::of(phase, row.session.failure, true, adb::DeviceState::Device);
}

void DeviceManager::syncTransportPhones()
{
    const NativeExtensions* native = m_extensions != nullptr ? m_extensions->native() : nullptr;
    const std::vector<TransportPhone> phones =
        native != nullptr ? native->transportPhones() : std::vector<TransportPhone>{};
    std::set<QString> present;
    for (const TransportPhone& phone : phones) {
        const QString serial = QStringLiteral("via:%1/%2/%3")
                                   .arg(QString::fromStdString(phone.extension), QString::fromStdString(phone.transport),
                                        QString::fromStdString(phone.id));
        present.insert(serial);
        if (const int found = indexOf(serial); found >= 0) {
            Row& row = m_rows[static_cast<std::size_t>(found)];
            row.transported = phone;
            row.listed = true;
            rowChanged(found);
            continue;
        }
        const int at = static_cast<int>(m_rows.size());
        beginInsertRows({}, at, at);
        Row row;
        row.serial = serial;
        row.deviceId = serial;
        row.transported = phone;
        row.listed = true;
        row.state = adb::DeviceState::Unknown;
        qInfo("%s reaches %s through %s", qUtf8Printable(phone.extensionName), qUtf8Printable(phone.name),
              qUtf8Printable(phone.transportTitle));
        m_rows.push_back(std::move(row));
        endInsertRows();
    }
    for (int i = static_cast<int>(m_rows.size()) - 1; i >= 0; --i) {
        Row& row = m_rows[static_cast<std::size_t>(i)];
        if (!row.transported || present.contains(row.serial)) {
            continue;
        }
        row.listed = false;
        if (!active(row.session.phase)) {
            removeRow(i);
        } else {
            rowChanged(i);
        }
    }
    emit countsChanged();
}

std::function<std::unique_ptr<session::ServerHost>()> DeviceManager::transportHost(const TransportPhone& phone) const
{
    const NativeExtensions* native = m_extensions != nullptr ? m_extensions->native() : nullptr;
    PhoneRoute unreachable;
    unreachable.key = transportKey(phone.extension, phone.transport);
    unreachable.phone = phone.id;
    unreachable.through = phone.transportTitle.toStdString();
    std::shared_ptr<session::PhoneStreams> streams =
        native != nullptr ? native->phoneStreams(phone, m_package)
                          : makePhoneStreams(std::move(unreachable), std::make_shared<TransportChanges>(), m_package);
    return [streams = std::move(streams)] { return std::make_unique<session::StreamServerHost>(streams); };
}

QVariantMap DeviceManager::transportedState(const Row& row)
{
    if (!row.transported) {
        return {};
    }
    const TransportPhone& phone = *row.transported;
    return {{QStringLiteral("isTransported"), true},
            {QStringLiteral("extension"), QString::fromStdString(phone.extension)},
            {QStringLiteral("extensionName"), phone.extensionName},
            {QStringLiteral("transport"), QString::fromStdString(phone.transport)},
            {QStringLiteral("title"), phone.transportTitle},
            {QStringLiteral("id"), QString::fromStdString(phone.id)},
            {QStringLiteral("name"), phone.name},
            {QStringLiteral("model"), phone.model},
            {QStringLiteral("detail"), phone.detail},
            {QStringLiteral("icon"), phone.icon},
            {QStringLiteral("iconPath"), phone.iconPath}};
}

std::optional<Advice> DeviceManager::transportedAdviceOf(const Row& row)
{
    if (!row.transported || !row.session.failure) {
        return std::nullopt;
    }
    const TransportPhone& phone = *row.transported;
    const QString reason = QString::fromStdString(row.session.failure->detail);
    if (row.session.phase == session::SessionPhase::Reconnecting) {
        return Advice{tr("Reconnecting: %1").arg(reason),
                      tr("GameScrcpy connects again by itself as soon as %1 reaches the phone again")
                          .arg(phone.transportTitle)};
    }
    if (row.session.phase != session::SessionPhase::Failed) {
        return std::nullopt;
    }
    return Advice{tr("Not connected: %1").arg(reason),
                  tr("Check that %1 is on and can reach the phone, then connect again").arg(phone.extensionName)};
}

QString DeviceManager::backgroundKeyOf(const Row& row) const
{
    return row.deviceId.isEmpty() ? row.serial : row.deviceId;
}

void DeviceManager::bumpBackgrounds()
{
    ++m_backgroundRevision;
    emit backgroundsChanged();
}

void DeviceManager::forgetShots(Row& row)
{
    row.shotPending = 0;
    bumpBackgrounds();
}

QVariantMap DeviceManager::keymapBackground(const QString& serial) const
{
    const int index = indexOf(serial);
    if (index < 0 || m_backgrounds == nullptr) {
        return {};
    }
    const Row& row = m_rows[static_cast<std::size_t>(index)];
    const bool landscape = controlTurnOf(row) != static_cast<int>(ControlTurn::Portrait);
    QVariantMap shown{{QStringLiteral("landscape"), landscape},
                      {QStringLiteral("canShoot"), row.companion != nullptr && row.shotTicket == row.companionTicket},
                      {QStringLiteral("shooting"), row.companion != nullptr && row.shotPending != 0}};
    if (const std::optional<std::filesystem::path> file = m_backgrounds->find(backgroundKeyOf(row), landscape)) {
        QUrl url = QUrl::fromLocalFile(QString::fromStdWString(file->wstring()));
        url.setQuery(QStringLiteral("v=%1").arg(m_backgroundRevision));
        shown.insert(QStringLiteral("url"), url);
    }
    return shown;
}

bool DeviceManager::importBackground(const QString& serial, const QUrl& file)
{
    const int index = indexOf(serial);
    if (index < 0 || m_backgrounds == nullptr) {
        return false;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    const QString local = file.isLocalFile() ? file.toLocalFile() : file.toString();
    const Expected<StoredBackground, BackgroundProblem> stored =
        m_backgrounds->importFile(backgroundKeyOf(row), std::filesystem::path(local.toStdWString()));
    if (!stored) {
        qWarning("%s: the background picture was not taken", qUtf8Printable(row.name()));
        if (row.mirror != nullptr) {
            row.mirror->showToast(backgroundProblemText(stored.error()), QStringLiteral("triangle-alert"));
        }
        return false;
    }
    backgroundStored(row, *stored, false);
    return true;
}

bool DeviceManager::shootBackground(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0) {
        return false;
    }
    Row& row = m_rows[static_cast<std::size_t>(index)];
    if (row.companion == nullptr || row.shotTicket != row.companionTicket) {
        if (row.mirror != nullptr) {
            row.mirror->showToast(tr("Connect GameScrcpy Companion first: scan the code in this window with the phone"),
                                  QStringLiteral("smartphone"));
        }
        return false;
    }
    if (row.shotPending != 0) {
        return false;
    }
    row.shotPending = ++m_lastShot;
    if (row.shotPending == 0) {
        row.shotPending = ++m_lastShot;
    }
    if (row.shotAskedTicket != row.companionTicket) {
        row.shotAskedTicket = row.companionTicket;
        if (row.mirror != nullptr) {
            row.mirror->showToast(tr("Allow the screen recording on the phone when it asks"),
                                  QStringLiteral("smartphone"));
        }
    }
    qInfo("companion on %s: asking for a screenshot", qUtf8Printable(row.name()));
    row.companion->requestScreenshot(row.shotPending, 0, protocol::kDefaultScreenshotQuality);
    bumpBackgrounds();
    return true;
}

void DeviceManager::clearBackground(const QString& serial)
{
    const int index = indexOf(serial);
    if (index < 0 || m_backgrounds == nullptr) {
        return;
    }
    const Row& row = m_rows[static_cast<std::size_t>(index)];
    const bool landscape = controlTurnOf(row) != static_cast<int>(ControlTurn::Portrait);
    if (m_backgrounds->clear(backgroundKeyOf(row), landscape)) {
        qInfo("%s: the %s background is cleared", qUtf8Printable(row.name()), landscape ? "landscape" : "portrait");
    }
    bumpBackgrounds();
}

void DeviceManager::backgroundStored(Row& row, const StoredBackground& stored, bool fromPhone)
{
    bumpBackgrounds();
    qInfo("%s: a %s background of %d x %d is kept", qUtf8Printable(row.name()),
          stored.landscape ? "landscape" : "portrait", stored.width, stored.height);
    if (row.mirror == nullptr) {
        return;
    }
    const bool sideways = controlTurnOf(row) != static_cast<int>(ControlTurn::Portrait);
    if (stored.landscape != sideways) {
        row.mirror->showToast(stored.landscape ? tr("The picture is landscape: it shows when the phone is held sideways")
                                               : tr("The picture is portrait: it shows when the phone is held upright"),
                              QStringLiteral("info"));
        return;
    }
    if (fromPhone) {
        row.mirror->showToast(tr("The phone's screenshot is the background now"), QStringLiteral("image"));
    }
}

void DeviceManager::shotArrived(Row& row, const CompanionNotice& notice)
{
    if (notice.shot == 0 || notice.shot != row.shotPending) {
        return;
    }
    row.shotPending = 0;
    if (notice.event == CompanionEvent::Screenshot && notice.picture != nullptr && m_backgrounds != nullptr) {
        const Expected<StoredBackground, BackgroundProblem> stored =
            m_backgrounds->store(backgroundKeyOf(row), *notice.picture);
        if (stored) {
            backgroundStored(row, *stored, true);
            return;
        }
        qWarning("companion on %s: the screenshot could not be kept as the background", qUtf8Printable(row.name()));
        if (row.mirror != nullptr) {
            row.mirror->showToast(backgroundProblemText(stored.error()), QStringLiteral("triangle-alert"));
        }
        bumpBackgrounds();
        return;
    }
    qInfo("companion on %s: no screenshot, %s", qUtf8Printable(row.name()), notice.detail.c_str());
    if (row.mirror != nullptr) {
        row.mirror->showToast(shotFailureText(notice.shotFailure.value_or(ShotFailure::Error)),
                              QStringLiteral("triangle-alert"));
    }
    bumpBackgrounds();
}

}
