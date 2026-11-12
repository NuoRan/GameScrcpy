#include "AiAssistant.h"
#include "AiChats.h"
#include "AiProviders.h"
#include "AiScripts.h"
#include "AppCatalog.h"
#include "AppCommandHost.h"
#include "AppEvents.h"
#include "AppSettings.h"
#include "Commands.h"
#include "ComponentDefaults.h"
#include "ComponentLibrary.h"
#include "Converters.h"
#include "CrashReport.h"
#include "DemoFeed.h"
#include "DeviceManager.h"
#include "DiagnosticsBundle.h"
#include "ExtensionHelp.h"
#include "ExtensionHost.h"
#include "ExtensionLinks.h"
#include "ExtensionLogic.h"
#include "ExtensionPanels.h"
#include "ExtensionServices.h"
#include "GridWindow.h"
#include "HardwareAccess.h"
#include "HardwareClaims.h"
#include "HelpLibrary.h"
#include "LocalServers.h"
#include "InputReadGrants.h"
#include "JsSequence.h"
#include "LanguagePacks.h"
#include "LogFile.h"
#include "McpDevices.h"
#include "McpDrafts.h"
#include "McpServer.h"
#include "McpTools.h"
#include "MediaFrames.h"
#include "MediaSinks.h"
#include "MirrorWindow.h"
#include "NativeEvents.h"
#include "NativeExtensions.h"
#include "NativeJobs.h"
#include "Recordings.h"
#include "OfflineNetwork.h"
#include "ProfileLibrary.h"
#include "TaskLibrary.h"
#include "QtConversions.h"
#include "SettingsCatalog.h"
#include "ShortcutDispatcher.h"
#include "SpeechEngines.h"
#include "VisionEngines.h"
#include "WirelessController.h"

#include <gs/adb/AdbClient.h>
#include <gs/base/BuildInfo.h>
#include <gs/base/Log.h>
#include <gs/hardware/FakeBackend.h>
#include <gs/input/Profile.h>
#include <gs/media/FfmpegLog.h>
#include <gs/media/FrameSnapshot.h>
#include <gs/platform/CrashHandler.h>
#include <gs/platform/RunningMarker.h>
#include <gs/platform/Speech.h>
#include <gs/platform/Thread.h>
#include <gs/render/GraphicsDevice.h>

#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QStringList>
#include <QtQml/qqmlextensionplugin.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

Q_IMPORT_QML_PLUGIN(GameScrcpy_ThemePlugin)
Q_IMPORT_QML_PLUGIN(GameScrcpy_IconsPlugin)
Q_IMPORT_QML_PLUGIN(GameScrcpy_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(GameScrcpy_BlueprintPlugin)

using namespace Qt::StringLiterals;

static_assert(gs::app::Commands::kMaxRecent == gs::app::settings::Palette::kMaxRecent);
static_assert(static_cast<std::size_t>(gs::app::CommandTable::kMaxIdLength) == gs::app::settings::kMaxCommandIdLength);

namespace {

class QtLogSink final : public gs::log::Sink
{
public:
    void write(const gs::log::Record& record) noexcept override
    {
        try {
            const QString text = u"[%1] %2"_s.arg(QString::fromUtf8(record.category.data(), record.category.size()),
                                                  QString::fromUtf8(record.message.data(), record.message.size()));
            switch (record.level) {
            case gs::log::Level::Trace:
            case gs::log::Level::Debug:
                qDebug().noquote() << text;
                break;
            case gs::log::Level::Info:
                qInfo().noquote() << text;
                break;
            case gs::log::Level::Warning:
                qWarning().noquote() << text;
                break;
            case gs::log::Level::Error:
                qCritical().noquote() << text;
                break;
            }
        } catch (...) {
        }
    }
};

class FileLogging
{
public:
    explicit FileLogging(gs::app::LogFile& file)
    {
        s_file = &file;
        s_previous = qInstallMessageHandler(&FileLogging::handle);
    }
    ~FileLogging()
    {
        qInstallMessageHandler(s_previous);
        s_file = nullptr;
    }
    FileLogging(const FileLogging&) = delete;
    FileLogging& operator=(const FileLogging&) = delete;

private:
    static void handle(QtMsgType type, const QMessageLogContext& context, const QString& message)
    {
        static constexpr std::array<std::string_view, 5> kLevels{"debug", "warning", "critical", "fatal", "info"};
        const auto level = static_cast<std::size_t>(type) < kLevels.size() ? kLevels[static_cast<std::size_t>(type)]
                                                                           : std::string_view("other");
        s_file->append(std::format("{} {:8} {}",
                                   QDateTime::currentDateTime().toString(u"yyyy-MM-dd HH:mm:ss.zzz"_s).toStdString(),
                                   level, message.toStdString()));
        if (s_previous != nullptr) {
            s_previous(type, context, message);
        }
    }

    static inline gs::app::LogFile* s_file = nullptr;
    static inline QtMessageHandler s_previous = nullptr;
};

class SmokeWatch
{
public:
    SmokeWatch() { s_previous = qInstallMessageHandler(&SmokeWatch::handle); }
    ~SmokeWatch() { qInstallMessageHandler(s_previous); }
    SmokeWatch(const SmokeWatch&) = delete;
    SmokeWatch& operator=(const SmokeWatch&) = delete;

    [[nodiscard]] static int problems() noexcept { return s_problems.load(); }

private:
    static void handle(QtMsgType type, const QMessageLogContext& context, const QString& message)
    {
        if (type != QtDebugMsg && type != QtInfoMsg &&
            (message.contains(u"qrc:/"_s) || message.contains(u"QML"_s) || message.contains(u" not created"_s))) {
            s_problems.fetch_add(1);
        }
        if (s_previous != nullptr) {
            s_previous(type, context, message);
        }
    }

    static inline QtMessageHandler s_previous = nullptr;
    static inline std::atomic<int> s_problems{0};
};

void applyDefaultFont()
{
    QFont font = QGuiApplication::font();
    font.setFamilies({u"Segoe UI Variable Text"_s, u"Segoe UI"_s, u"Microsoft YaHei UI"_s});
    font.setPixelSize(14);
    font.setHintingPreference(QFont::PreferNoHinting);
    QGuiApplication::setFont(font);
    const QStringList chinese{u"Microsoft YaHei UI"_s, u"Microsoft YaHei"_s};
    QFontDatabase::setApplicationFallbackFontFamilies(QChar::Script_Han, chinese);
    QFontDatabase::setApplicationFallbackFontFamilies(QChar::Script_Common, chinese);
}

void logExtensions(const gs::app::ExtensionHost& host)
{
    using Mode = gs::app::ExtensionHost::Mode;
    if (host.mode() != Mode::Normal) {
        qWarning("safe mode: no extension is loaded (%s)",
                 host.mode() == Mode::SafeRequested ? "--safe-mode was given"
                                                    : "the last run ended while extensions were loaded");
    }
    if (const auto& bisect = host.bisect()) {
        qInfo("finding the extension behind a problem: step %d of about %d, %zu of %zu extensions in this round",
              bisect->step() + 1, bisect->steps(), bisect->trial().size(), bisect->original().size());
    }
    for (const auto& extension : host.extensions()) {
        const std::string name = extension.manifest
                                     ? extension.manifest->id + " " + extension.manifest->version.text()
                                     : QString::fromStdWString(extension.directory.wstring()).toStdString();
        qInfo("extension %s: %s%s", name.c_str(), extension.builtin ? "built in, " : "",
              !extension.usable() ? "unusable" : extension.active ? "loaded" : "not loaded");
        for (const gs::ext::Diagnostic& diagnostic : extension.diagnostics) {
            qWarning("  %s at %s %s", std::string(gs::ext::codeName(diagnostic.code)).c_str(), diagnostic.path.c_str(),
                     diagnostic.detail.c_str());
        }
    }
    for (const gs::ext::Diagnostic& problem : host.problems()) {
        qWarning("extension problem %s at %s %s", std::string(gs::ext::codeName(problem.code)).c_str(),
                 problem.path.c_str(), problem.detail.c_str());
    }
}

void loadInputProfile(gs::app::DeviceManager& devices, const QString& path)
{
    constexpr qint64 kMostProfileBytes = 1024 * 1024;
    const QFileInfo given(path);
    QFile file(given.isDir() ? QDir(path).filePath(u"profile.json"_s) : path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("profile %s: cannot be read", qUtf8Printable(path));
        return;
    }
    const QByteArray bytes = file.read(kMostProfileBytes + 1);
    const gs::input::LoadResult loaded =
        gs::input::loadProfile(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    for (const gs::input::Diagnostic& diagnostic : loaded.diagnostics) {
        if (diagnostic.severity != gs::input::Severity::Info) {
            qWarning("profile %s: %s %s %s", qUtf8Printable(path),
                     diagnostic.severity == gs::input::Severity::Error ? "error at" : "warning at",
                     diagnostic.path.c_str(), diagnostic.detail.c_str());
        }
    }
    if (!loaded.ok()) {
        qWarning("profile %s: not used", qUtf8Printable(path));
        return;
    }
    qInfo("profile %s: %zu controls for game mode", qUtf8Printable(path), loaded.profile.controls.size());
    devices.setInputProfile(std::make_shared<const gs::input::Profile>(loaded.profile),
                            given.isDir() ? std::filesystem::path(given.absoluteFilePath().toStdWString())
                                          : std::filesystem::path());
}

std::filesystem::path findAdb()
{
    const QString bundled = QCoreApplication::applicationDirPath() + u"/adb/adb.exe"_s;
    if (QFileInfo::exists(bundled)) {
        return bundled.toStdWString();
    }
    if (const QString sdk = qEnvironmentVariable("ANDROID_HOME"); !sdk.isEmpty()) {
        const QString fromSdk = QDir(sdk).filePath(u"platform-tools/adb.exe"_s);
        if (QFileInfo::exists(fromSdk)) {
            return fromSdk.toStdWString();
        }
    }
    return QStandardPaths::findExecutable(u"adb"_s).toStdWString();
}

std::unique_ptr<gs::hardware::Backend> demoHardware()
{
    auto backend = std::make_unique<gs::hardware::FakeBackend>();
    gs::hardware::DeviceInfo board;
    board.id = "serial:DEMO\\VID_1A86&PID_7523\\1";
    board.kind = gs::hardware::Kind::Serial;
    board.name = "Demo serial board (COM9)";
    board.path = "\\\\.\\COM9";
    board.vendorId = 0x1A86;
    board.productId = 0x7523;
    backend->add(board);
    gs::hardware::DeviceInfo deck;
    deck.id = "hid:demo#vid_0fd9&pid_0080#1";
    deck.kind = gs::hardware::Kind::Hid;
    deck.name = "Demo button deck";
    deck.path = "\\\\?\\demo#vid_0fd9&pid_0080#1";
    deck.vendorId = 0x0FD9;
    deck.productId = 0x0080;
    deck.usagePage = 0x000C;
    deck.usage = 0x0001;
    backend->add(deck);
    gs::hardware::DeviceInfo pedal;
    pedal.id = gs::hardware::bleId(0xC0FFEE000001ull);
    pedal.kind = gs::hardware::Kind::Ble;
    pedal.name = "Demo foot pedal";
    pedal.path = gs::hardware::addressText(0xC0FFEE000001ull);
    pedal.services = {*gs::hardware::canonicalUuid("1812")};
    backend->add(pedal);
    return backend;
}

}

int main(int argc, char* argv[])
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    qputenv("QT_QPA_DISABLE_REDIRECTION_SURFACE", "1");
    if (!qEnvironmentVariableIsSet("QT_D3D_NO_VBLANK_THREAD")) {
        qputenv("QT_D3D_NO_VBLANK_THREAD", "1");
    }
    if (!qEnvironmentVariableIsSet("QT_QPA_UPDATE_IDLE_TIME")) {
        qputenv("QT_QPA_UPDATE_IDLE_TIME", "0");
    }

    QGuiApplication app(argc, argv);
    gs::app::installJsSequence();
    if (!gs::platform::keepTimerResolution()) {
        qWarning("timer resolution may be throttled while the window is hidden");
    }
    QGuiApplication::setApplicationName(u"GameScrcpy"_s);
    QGuiApplication::setOrganizationName(u"GameScrcpy"_s);
    QGuiApplication::setApplicationVersion(gs::app::toQString(gs::buildInfo().version));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption smokeTest(u"smoke-test"_s,
                                       u"Exit once the first frame is on screen: status 0, or 1 after QML warnings."_s);
    parser.addOption(smokeTest);
    const QCommandLineOption gallery(u"gallery"_s, u"Show the component gallery instead of the main window."_s);
    parser.addOption(gallery);
    const QCommandLineOption safeMode(u"safe-mode"_s, u"Start without loading any extension."_s);
    parser.addOption(safeMode);
    const QCommandLineOption demoDevice(u"demo-device"_s,
                                        u"Add a demo device that plays the given recording in a loop; give it again "
                                        u"for each further demo device."_s,
                                        u"recording"_s);
    parser.addOption(demoDevice);
    const QCommandLineOption demoGame(u"demo-game"_s,
                                      u"Let the demo device report the given app in front, marked as a game; given "
                                      u"again, each goes with the demo device in the same place, and the last one "
                                      u"with the rest."_s,
                                      u"package"_s);
    parser.addOption(demoGame);
    const QCommandLineOption demoCapture(u"demo-capture"_s,
                                         u"Add a demo capture card that shows the given recording with black bars."_s,
                                         u"recording"_s);
    parser.addOption(demoCapture);
    const QCommandLineOption demoHardwareOption(
        u"demo-hardware"_s,
        u"Offer made-up serial, HID and Bluetooth devices instead of the real ones, to try the device picker."_s);
    parser.addOption(demoHardwareOption);
    const QCommandLineOption touch(u"touch"_s,
                                   u"For testing: touch every phone by injection (inject), through a UHID touch "
                                   u"screen made on the phone (uhid) or through an AOA touch screen on the USB cable "
                                   u"(aoa), whatever the touch method setting says."_s,
                                   u"backend"_s);
    parser.addOption(touch);
    const QCommandLineOption profile(u"profile"_s, u"Use the given key mapping profile (a .json file or a profile folder) on every device for this run."_s,
                                     u"file"_s);
    parser.addOption(profile);
    QCommandLineOption testCrash(u"test-crash"_s, u"Crash on purpose once started, to try the crash report."_s);
    testCrash.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(testCrash);
    parser.addPositionalArgument(u"link"_s, u"A gamescrcpy:// link: install an extension from the web or show one."_s,
                                 u"[link]"_s);
    parser.process(app);
    const QString link = gs::app::linkArgument(parser.positionalArguments());
    if (!link.isEmpty() &&
        gs::app::LinkHandover::send(gs::app::linkPipeName(gs::app::dataDirectory()), link, std::chrono::seconds(5))) {
        return EXIT_SUCCESS;
    }
    const gs::platform::RunningMarker running(L"GameScrcpyRunning");

    applyDefaultFont();

    const std::filesystem::path data = gs::app::dataDirectory();
    gs::app::LogFile logFile(data / "logs", "gamescrcpy");
    const FileLogging fileLogging(logFile);
    if (!gs::platform::installCrashHandler(data / "crashes", logFile.path())) {
        qWarning("crash reports are off: %s cannot hold them",
                 qUtf8Printable(QString::fromStdWString((data / "crashes").wstring())));
    }
    std::optional<SmokeWatch> smokeWatch;
    if (parser.isSet(smokeTest)) {
        smokeWatch.emplace();
    }
    qInfo().noquote() << "GameScrcpy" << gs::app::toQString(gs::buildInfo().version) << "started";
    QtLogSink logSink;
    gs::log::setSink(&logSink);
    gs::media::routeFfmpegLog();

    gs::app::AppSettings settings(data);
    if (!parser.isSet(smokeTest) && !parser.isSet(gallery)) {
        settings.countLaunch();
    }
    gs::app::CrashReport crashes(settings, data / "crashes");
    gs::app::HardwareClaims hardwareClaims;
    gs::app::ComponentDefaults componentDefaults(data / "component-defaults.json");
    gs::app::LanguagePacks languagePacks(settings);
    gs::app::HelpLibrary help(settings);
    gs::app::ProfileLibrary profiles(data / "profiles");
    gs::app::TaskLibrary tasks(data / "tasks");
    tasks.setExamplesFolder((QCoreApplication::applicationDirPath() + u"/tasks"_s).toStdWString());
    gs::app::InputReadGrants inputGrants(data / "input-read.json");
    inputGrants.follow(profiles, tasks);
    gs::app::ComponentLibrary components((QCoreApplication::applicationDirPath() + u"/components"_s).toStdWString(),
                                         data / "components");

    gs::app::ExtensionHost extensions(
        settings, {.user = data / "extensions",
                   .builtin = (QCoreApplication::applicationDirPath() + u"/extensions"_s).toStdWString(),
                   .hostProgram = {},
                   .data = data,
                   .log = logFile.path()});
    extensions.start(parser.isSet(safeMode), data / "extensions.running", data / "extensions-bisect.json");
    logExtensions(extensions);
    gs::app::watchLanguageHelp(help, languagePacks);
    languagePacks.follow(extensions);
    profiles.setExtensionProfiles(extensions.profiles());
    QObject::connect(&extensions, &gs::app::ExtensionHost::profilesChanged, &profiles,
                     [&profiles, &extensions] { profiles.setExtensionProfiles(extensions.profiles()); });
    components.setExtensionComponents(extensions.components());
    QObject::connect(&extensions, &gs::app::ExtensionHost::componentsChanged, &components,
                     [&components, &extensions] { components.setExtensionComponents(extensions.components()); });
    tasks.setExtensionTasks(extensions.tasks());
    QObject::connect(&extensions, &gs::app::ExtensionHost::tasksChanged, &tasks,
                     [&tasks, &extensions] { tasks.setExtensionTasks(extensions.tasks()); });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &extensions, &gs::app::ExtensionHost::finish);
    gs::app::ExtensionLinks extensionLinks(data / "extensions" / ".staging");
    gs::app::LinkHandover linkHandover(gs::app::linkPipeName(data));
    QObject::connect(&linkHandover, &gs::app::LinkHandover::received, &extensionLinks, &gs::app::ExtensionLinks::open);
    if (!parser.isSet(smokeTest)) {
        linkHandover.listen();
    }
    gs::app::ExtensionPanels extensionPanels(nullptr);
    extensionPanels.watch(extensions);
    gs::app::watchExtensionHelp(help, extensions);
    gs::app::HardwareAccess hardware(parser.isSet(demoHardwareOption) ? demoHardware()
                                     : parser.isSet(smokeTest)        ? nullptr
                                                                      : gs::hardware::makeWindowsBackend(),
                                     data / "hardware-grants.json");
    hardware.follow(extensions);
    hardware.offerServices();
    gs::app::LocalServers localServers(nullptr);
    localServers.follow(extensions);
    localServers.offerServices();
    gs::app::MediaFrames mediaFrames(nullptr);
    mediaFrames.setSettings(&settings);
    mediaFrames.follow(extensions);
    mediaFrames.offerServices();
    gs::app::NativeJobs nativeJobs(*extensions.native());
    nativeJobs.offerServices();
    gs::app::SpeechEngines speechEngines(nullptr);
    speechEngines.setDefaults([&settings] {
        const gs::app::settings::Speech speech = settings.effective({}).speech;
        return gs::app::SpeechDefaults{.engine = QString::fromStdString(speech.engine),
                                       .voice = QString::fromStdString(speech.voice),
                                       .rate = speech.rate / 100.0,
                                       .volume = speech.volume / 100.0,
                                       .listenEngine = QString::fromStdString(speech.listenEngine),
                                       .language = QString::fromStdString(speech.language),
                                       .interfaceLanguage = settings.interfaceLanguage().replace(u'_', u'-')};
    });
    speechEngines.addEngine(std::make_unique<gs::app::SystemSpeechEngine>(
        parser.isSet(smokeTest) ? gs::platform::SpeechOutput::Nowhere : gs::platform::SpeechOutput::Speakers));
    speechEngines.follow(extensions, nativeJobs);
    speechEngines.offerServices();
    gs::app::AiProviders aiProviders(data / "ai-requests");
    aiProviders.use(*extensions.native(), nativeJobs);
    aiProviders.follow(extensions);
    gs::app::VisionEngines visionEngines(nullptr);
    visionEngines.addEngine(std::make_shared<gs::app::SystemVisionEngine>());
    visionEngines.follow(extensions, *extensions.native());
    visionEngines.offerServices();
    gs::app::NativeEvents nativeEvents(*extensions.native());
    gs::app::AppEvents appEvents;
    gs::app::ExtensionServices extensionServices(nullptr);
    extensionServices.follow(extensions);
    extensionServices.offerServices();
    gs::app::Converters converters(nullptr);
    converters.follow(extensions);

    const auto graphics =
        gs::render::GraphicsDevice::create(false, qEnvironmentVariableIntValue("GS_FORCE_WARP") == 1);
    if (graphics && graphics->warp()) {
        qWarning("drawing video without a GPU: %s", graphics->description().c_str());
    }
    const gs::adb::AdbClient adb(
        gs::adb::AdbClient::portFromSetting(qEnvironmentVariable("ANDROID_ADB_SERVER_PORT").toStdString()));
    gs::app::DeviceManager devices(adb, parser.isSet(smokeTest) ? std::filesystem::path() : findAdb(),
                                   (QCoreApplication::applicationDirPath() + u"/gs-server.apk"_s).toStdWString(),
                                   graphics.get(), settings);
    devices.setCompanionPackage((QCoreApplication::applicationDirPath() + u"/gs-companion.apk"_s).toStdWString(),
                                (QCoreApplication::applicationDirPath() + u"/gs-companion.json"_s).toStdWString());
    devices.setExtensions(&extensions);
    devices.setNativeJobs(&nativeJobs);
    gs::app::MediaSinks mediaSinks(
        *extensions.native(),
        {.windows = [&devices] { return devices.mirrors(); },
         .camera = [&devices](const QString& serial) -> gs::app::MediaSinks::Camera {
             std::shared_ptr<gs::media::FrameSnapshots> shots = devices.snapshotsOf(serial);
             if (shots == nullptr) {
                 return {};
             }
             return [shots] { return shots->capture(true); };
         },
         .listen = [&devices](const QString& serial, gs::media::AudioTap tap) {
             return devices.setAudioTap(serial, std::move(tap));
         }});
    QObject::connect(&devices, &gs::app::DeviceManager::mirrorsChanged, &mediaSinks,
                     &gs::app::MediaSinks::windowsChanged);
    if (gs::app::Recordings* recordings = gs::app::Recordings::instance()) {
        recordings->watchExtensions(*extensions.native());
    }
    devices.setScriptStore(data / "script-store");
    devices.setProfileLibrary(&profiles);
    devices.setTaskLibrary(&tasks);
    devices.setComponentLibrary(&components);
    gs::app::ExtensionLogic extensionLogic(data / "script-store");
    extensionLogic.setScripts(extensions.scripts());
    QObject::connect(&extensions, &gs::app::ExtensionHost::scriptsChanged, &extensionLogic,
                     [&extensionLogic, &extensions] { extensionLogic.setScripts(extensions.scripts()); });
    devices.setExtensionLogic(&extensionLogic);
    mediaFrames.setSources(
        {.phones =
             [&devices] {
                 std::vector<gs::app::FramePhone> phones;
                 for (const QVariant& item : devices.mirrors()) {
                     const QString serial = item.toMap().value(u"serial"_s).toString();
                     std::shared_ptr<gs::media::FrameSnapshots> snapshots = devices.snapshotsOf(serial);
                     if (snapshots == nullptr) {
                         continue;
                     }
                     const std::optional<gs::app::DeviceFacts> facts = devices.facts(serial);
                     const std::optional<gs::script::DeviceInfo> info = devices.deviceInfo(serial);
                     phones.push_back({.serial = serial,
                                       .deviceKey = facts && !facts->deviceId.isEmpty() ? facts->deviceId : serial,
                                       .snapshots = std::move(snapshots),
                                       .rotation = info ? info->rotation : 0});
                 }
                 return phones;
             },
         .taps = [&extensions] { return extensions.native()->frameTaps(); },
         .overlay =
             [](const QString& serial, const QString& group, const QVariantList& shapes) {
                 for (QWindow* window : QGuiApplication::topLevelWindows()) {
                     if (auto* mirror = qobject_cast<gs::app::MirrorWindow*>(window);
                         mirror != nullptr && mirror->serial() == serial) {
                         mirror->setOverlay(group, shapes);
                     }
                 }
             }});
    QObject::connect(&devices, &gs::app::DeviceManager::mirrorsChanged, &mediaFrames, &gs::app::MediaFrames::sync);
    QObject::connect(&devices, &QAbstractItemModel::dataChanged, &mediaFrames,
                     [&mediaFrames](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
                         if (roles.isEmpty() || std::ranges::any_of(roles, [](int role) {
                                 return role != gs::app::DeviceManager::StatsRole &&
                                        role != gs::app::DeviceManager::ThumbnailRole;
                             })) {
                             mediaFrames.sync();
                         }
                     });
    QObject::connect(extensions.native(), &gs::app::NativeExtensions::changed, &mediaFrames,
                     &gs::app::MediaFrames::sync);
    hardware.setFeed([&devices](const QString& serial,
                                const gs::input::DeviceEvent& event) { devices.feedInput(serial, event); },
                     [&devices] { return devices.focusedPhone(); });
    const auto detachHardware = qScopeGuard([&hardware] { hardware.setFeed({}, {}); });
    QObject::connect(extensions.native(), &gs::app::NativeExtensions::inputEvent, &hardware,
                     &gs::app::HardwareAccess::provideInput);
    QObject::connect(extensions.native(), &gs::app::NativeExtensions::inputsEnded, &hardware,
                     &gs::app::HardwareAccess::endProvidedInput);
    if (parser.isSet(touch)) {
        if (const QString backend = parser.value(touch); backend == u"uhid"_s) {
            devices.forceTouchBackend(gs::app::TouchBackend::Uhid);
            qInfo("touches go through a UHID touch screen on phones that allow it (--touch uhid)");
        } else if (backend == u"aoa"_s) {
            devices.forceTouchBackend(gs::app::TouchBackend::Aoa);
            qInfo("touches go through an AOA touch screen on the USB cable where the phone has one (--touch aoa)");
        } else if (backend == u"inject"_s) {
            devices.forceTouchBackend(gs::app::TouchBackend::Injection);
            qInfo("touches go through injection (--touch inject)");
        } else {
            qWarning("--touch %s is not known: touches follow the touch method setting", qUtf8Printable(backend));
        }
    }
    gs::app::DiagnosticsBundle diagnostics(settings, data);
    diagnostics.setDevices(&devices);
    diagnostics.setCrashReport(&crashes);
    diagnostics.setGraphics(graphics ? QString::fromStdString(graphics->description()) : QString());
    gs::app::AppCatalog appCatalog(
        data / "app-icons",
        {.availability = [&devices](const QString& serial) { return devices.appAvailability(serial); },
         .list = [&devices](const QString& serial, std::uint32_t requestId) {
             return devices.requestApps(serial, requestId);
         },
         .icon = [&devices](const QString& serial, std::uint32_t requestId, const QString& package,
                            int sizePx) { return devices.requestAppIcon(serial, requestId, package, sizePx); },
         .launch = [&devices](const QString& serial, const QString& package, const QString& label,
                              bool independent) { devices.launchApp(serial, package, label, independent); }});
    devices.setAppCatalog(&appCatalog);
    const auto detachCatalog = qScopeGuard([&devices] { devices.setAppCatalog(nullptr); });
    gs::app::WirelessController wireless(adb, settings);
    gs::app::AppCommandHost commandHost(devices, wireless);
    gs::app::Commands commands(commandHost);
    commands.applyUserShortcuts(settings.shortcuts());
    commands.setShortcutStore([&settings](const gs::app::ShortcutMap& shortcuts) { settings.setShortcuts(shortcuts); });
    commands.applyRecentCommands(settings.recentCommands());
    commands.setRecentStore([&settings](const std::vector<std::string>& ids) { settings.setRecentCommands(ids); });
    gs::app::ShortcutDispatcher shortcutDispatcher(commands, [](const QQuickWindow& window) -> std::optional<QString> {
        if (const auto* mirror = qobject_cast<const gs::app::MirrorWindow*>(&window); mirror != nullptr) {
            return mirror->serial();
        }
        if (const auto* grid = qobject_cast<const gs::app::GridWindow*>(&window);
            grid != nullptr && !grid->target().isEmpty()) {
            return grid->target();
        }
        return std::nullopt;
    });
    shortcutDispatcher.setBlocked(
        [&devices](const QString& serial, const QKeyEvent& key) { return devices.keymapBinds(serial, key); });
    shortcutDispatcher.setHelpTopic([](const QQuickWindow& window) {
        if (const auto* mirror = qobject_cast<const gs::app::MirrorWindow*>(&window); mirror != nullptr) {
            if (mirror->editor() != nullptr && mirror->editor()->logicShown()) {
                return QStringLiteral("scripts");
            }
            return mirror->editing() ? QStringLiteral("keymap-editor") : QStringLiteral("mirror-window");
        }
        return window.property("helpTopic").toString();
    });
    app.installEventFilter(&shortcutDispatcher);
    QObject::connect(&devices, &QAbstractItemModel::dataChanged, &commands,
                     [&commands](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
                         const bool pictureOnly = !roles.isEmpty() && std::ranges::all_of(roles, [](int role) {
                             return role == gs::app::DeviceManager::StatsRole ||
                                    role == gs::app::DeviceManager::ThumbnailRole;
                         });
                         if (!pictureOnly) {
                             commands.scheduleRefresh();
                         }
                     });
    QObject::connect(&devices, &QAbstractItemModel::rowsInserted, &commands, &gs::app::Commands::scheduleRefresh);
    QObject::connect(&devices, &QAbstractItemModel::rowsRemoved, &commands, &gs::app::Commands::scheduleRefresh);
    QObject::connect(&devices, &QAbstractItemModel::modelReset, &commands, &gs::app::Commands::scheduleRefresh);
    QObject::connect(&devices, &gs::app::DeviceManager::setupChanged, &commands, &gs::app::Commands::scheduleRefresh);
    QObject::connect(&settings, &gs::app::AppSettings::valuesChanged, &commands, &gs::app::Commands::scheduleRefresh);
    QObject::connect(&settings, &gs::app::AppSettings::devicesChanged, &commands, &gs::app::Commands::scheduleRefresh);
    QObject::connect(&profiles, &gs::app::ProfileLibrary::entriesChanged, &commands,
                     &gs::app::Commands::scheduleRefresh);
    QObject::connect(&devices, &gs::app::DeviceManager::appCommandRequested, &commands,
                     [&commands](const QString& command, const QString& serial) {
                         if (!commands.trigger(command, serial)) {
                             qInfo("profile command %s did not run", qUtf8Printable(command));
                         }
                     });

    gs::SteadyClock mcpClock;
    gs::app::McpDrafts mcpDrafts(
        {.root = data / "mcp-drafts", .profiles = &profiles, .components = &components, .tasks = &tasks});
    gs::app::McpTools mcpTools({.profiles = &profiles,
                                .logFile = logFile.path(),
                                .devices = gs::app::mcpDevicesOf(devices),
                                .components = &components,
                                .tasks = &tasks,
                                .drafts = &mcpDrafts},
                               mcpClock);
    gs::app::McpServer mcp(settings, mcpTools, mcpDrafts, mcpClock);
    QObject::connect(&mcpTools, &gs::app::McpTools::toolsChanged, &mcp, &gs::app::McpServer::announceTools);
    gs::app::AiAssistant assistant(settings);
    assistant.setProviders(&aiProviders);
    gs::app::AiChats chats(
        [&assistant](const gs::app::AiChoice& choice) { return assistant.provider(true, choice); },
        &assistant.tools(), &help, data / "ai-chats");
    gs::app::AiScripts aiScripts(
        gs::app::aiScriptHooks(settings, assistant, extensions, gs::app::mcpDevicesOf(devices).frames), mcpClock);
    aiScripts.offerService();

    gs::app::OfflineNetworkFactory offlineNetwork;
    QQmlApplicationEngine engine;
    devices.setQmlEngine(&engine);
    const auto closeMirrors = qScopeGuard([&devices] { devices.closeWindows(); });
    engine.setNetworkAccessManagerFactory(&offlineNetwork);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    if (graphics) {
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
                         [device = graphics.get()](QObject* created, const QUrl&) {
                             if (auto* window = qobject_cast<QQuickWindow*>(created)) {
                                 device->share(*window);
                             }
                         });
    }
    engine.loadFromModule(u"GameScrcpy.App"_s, parser.isSet(gallery) ? u"Gallery"_s : u"Main"_s);
    if (auto* hub = engine.rootObjects().isEmpty() ? nullptr
                                                   : qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst())) {
        auto first = std::make_shared<QMetaObject::Connection>();
        *first = QObject::connect(hub, &QQuickWindow::frameSwapped, &app, [first] {
            QObject::disconnect(*first);
            qInfo("hub window shown %lld ms after the process started",
                  static_cast<long long>(gs::platform::millisecondsSinceProcessStart()));
        }, Qt::QueuedConnection);
    }
    QObject::connect(&languagePacks, &gs::app::LanguagePacks::translationsChanged, &app, [&] {
        engine.retranslate();
        commands.retranslate();
        devices.retranslate();
        wireless.retranslate();
        if (auto* catalog = engine.singletonInstance<gs::app::SettingsCatalog*>("GameScrcpy.App", "SettingsCatalog")) {
            catalog->retranslate();
        }
    });
    if (!link.isEmpty()) {
        QMetaObject::invokeMethod(&extensionLinks, [&extensionLinks, link] { extensionLinks.open(link); },
                                  Qt::QueuedConnection);
    }
    if (parser.isSet(profile)) {
        loadInputProfile(devices, parser.value(profile));
    }
    for (const gs::app::DemoDevice& demo : gs::app::demoDevicesOf(parser.values(demoDevice), parser.values(demoGame))) {
        devices.addDemoDevice(demo.recording, demo.game);
    }
    if (parser.isSet(demoCapture)) {
        devices.addDemoCapture(parser.value(demoCapture));
    }
    if (parser.isSet(testCrash)) {
        QMetaObject::invokeMethod(&app, [] { gs::platform::crashOnPurpose(); }, Qt::QueuedConnection);
    }

    if (parser.isSet(smokeTest)) {
        for (const QString& name : {u"LogicWindow"_s, u"VisionWorkbenchWindow"_s, u"GridOverlay"_s}) {
            const QQmlComponent component(&engine, u"GameScrcpy.App"_s, name);
            if (component.isError()) {
                qWarning("%s not created: %s", qUtf8Printable(name), qUtf8Printable(component.errorString()));
            }
        }
        auto* window = engine.rootObjects().isEmpty()
                           ? nullptr
                           : qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
        if (window == nullptr) {
            return EXIT_FAILURE;
        }
        QObject::connect(window, &QQuickWindow::frameSwapped, &app,
                         [] { QCoreApplication::exit(SmokeWatch::problems() == 0 ? EXIT_SUCCESS : EXIT_FAILURE); },
                         Qt::QueuedConnection);
    }

    const int code = QGuiApplication::exec();
    qInfo("GameScrcpy exits with code %d", code);
    return code;
}
