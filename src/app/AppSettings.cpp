#include "AppSettings.h"

#include "AppEvents.h"
#include "QtConversions.h"
#include "SettingVariant.h"
#include "SponsorPrompt.h"
#include "WindowFit.h"

#include <gs/platform/Accessibility.h>

#include <QGuiApplication>
#include <QFileInfo>
#include <QJSEngine>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QLocale>
#include <QMetaObject>
#include <QRect>
#include <QScreen>
#include <QWindow>

#include <algorithm>
#include <chrono>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace gs::app {

namespace {

constexpr std::chrono::milliseconds kSettle{300};

template <typename E, std::size_t N>
QString nameOf(E value, const std::array<std::string_view, N>& names)
{
    const std::string_view name = names[static_cast<std::size_t>(value)];
    return QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()));
}

template <typename E, std::size_t N>
std::optional<E> valueOf(const QString& name, const std::array<std::string_view, N>& names)
{
    for (std::size_t i = 0; i < N; ++i) {
        if (name == QLatin1StringView(names[i].data(), static_cast<qsizetype>(names[i].size()))) {
            return static_cast<E>(i);
        }
    }
    return std::nullopt;
}

bool usableTourKey(const QString& key)
{
    const std::string text = key.toStdString();
    return !text.empty() && text.size() <= settings::Hints::kMaxTourNameLength && key.trimmed() == key;
}

QJsonValue settingJson(const settings::Value& value)
{
    if (const auto* flag = std::get_if<bool>(&value)) {
        return *flag;
    }
    if (const auto* whole = std::get_if<std::int64_t>(&value)) {
        return static_cast<qint64>(*whole);
    }
    return QString::fromStdString(std::get<std::string>(value));
}

std::pair<QStringList, QJsonObject> changesOf(const settings::Settings& before, const settings::Settings& after)
{
    std::pair<QStringList, QJsonObject> changes;
    if (AppEvents::instance() == nullptr) {
        return changes;
    }
    for (const settings::Entry& entry : settings::catalog()) {
        const std::optional<settings::Value> was = settings::valueOf(before, entry.path);
        const std::optional<settings::Value> now = settings::valueOf(after, entry.path);
        if (was == now) {
            continue;
        }
        const QString key = QString::fromStdString(entry.path);
        changes.first.append(key);
        if (now && publicSetting(key)) {
            changes.second.insert(key, settingJson(*now));
        }
    }
    return changes;
}

}

std::filesystem::path dataDirectory()
{
    const QString overridden = qEnvironmentVariable("GS_DATA_DIR");
    if (!overridden.isEmpty()) {
        return overridden.toStdWString();
    }
    const QString programDirectory = QCoreApplication::applicationDirPath();
    if (QFileInfo::exists(programDirectory + QStringLiteral("/portable.flag"))) {
        return (programDirectory + QStringLiteral("/data")).toStdWString();
    }
    return (qEnvironmentVariable("APPDATA") + QStringLiteral("/GameScrcpy")).toStdWString();
}

AppSettings::AppSettings(const std::filesystem::path& directory, QObject* parent)
    : QObject(parent),
      m_file(directory / "settings.json", std::chrono::milliseconds(500), [this] { reportBusy(); }),
      m_devices(directory / "devices.json", std::chrono::milliseconds(500), [this] { reportBusy(); })
{
    Q_ASSERT(s_instance == nullptr);
    s_instance = this;
    m_systemReducedMotion = !platform::systemAnimationsOn();
    m_systemReducedTransparency = !platform::systemTransparencyOn();
    if (auto* application = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        connect(application, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive) {
                refreshSystem();
            }
        });
    }
}

void AppSettings::refreshSystem()
{
    const bool motion = !platform::systemAnimationsOn();
    const bool transparency = !platform::systemTransparencyOn();
    if (motion == m_systemReducedMotion && transparency == m_systemReducedTransparency) {
        return;
    }
    m_systemReducedMotion = motion;
    m_systemReducedTransparency = transparency;
    emit appearanceChanged();
}

AppSettings::~AppSettings()
{
    s_instance = nullptr;
}

void AppSettings::reportBusy()
{
    QMetaObject::invokeMethod(
        this,
        [this] {
            const auto now = std::chrono::steady_clock::now();
            if (m_busyShown && now - *m_busyShown < std::chrono::minutes(1)) {
                return;
            }
            m_busyShown = now;
            emit saveBusy();
        },
        Qt::QueuedConnection);
}

AppSettings* AppSettings::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

QString AppSettings::theme() const
{
    return nameOf(m_file.document().appearance.theme, settings::kThemeModeNames);
}

void AppSettings::setTheme(const QString& theme)
{
    if (const auto mode = valueOf<settings::ThemeMode>(theme, settings::kThemeModeNames)) {
        settings::Appearance appearance = m_file.document().appearance;
        appearance.theme = *mode;
        changeAppearance(appearance);
    }
}

QString AppSettings::accent() const
{
    return nameOf(m_file.document().appearance.accent, settings::kAccentNames);
}

void AppSettings::setAccent(const QString& accent)
{
    if (const auto value = valueOf<settings::Accent>(accent, settings::kAccentNames)) {
        settings::Appearance appearance = m_file.document().appearance;
        appearance.accent = *value;
        changeAppearance(appearance);
    }
}

QString AppSettings::palette() const
{
    return QString::fromStdString(m_file.document().appearance.palette);
}

void AppSettings::setPalette(const QString& palette)
{
    std::string text = palette.toStdString();
    if (text.size() > settings::Appearance::kMaxPaletteLength) {
        return;
    }
    settings::Appearance appearance = m_file.document().appearance;
    appearance.palette = std::move(text);
    changeAppearance(appearance);
}

bool AppSettings::reducedMotion() const
{
    switch (m_file.document().appearance.reducedMotion) {
    case settings::SystemToggle::On:
        return true;
    case settings::SystemToggle::Off:
        return false;
    case settings::SystemToggle::System:
        break;
    }
    return m_systemReducedMotion;
}

bool AppSettings::reducedTransparency() const
{
    switch (m_file.document().appearance.reducedTransparency) {
    case settings::SystemToggle::On:
        return true;
    case settings::SystemToggle::Off:
        return false;
    case settings::SystemToggle::System:
        break;
    }
    return m_systemReducedTransparency;
}

void AppSettings::changeAppearance(const settings::Appearance& appearance)
{
    settings::Settings next = m_file.document();
    next.appearance = appearance;
    change(next);
}

QVariantMap AppSettings::values() const
{
    QVariantMap values;
    for (const settings::Entry& entry : settings::catalog()) {
        if (const auto value = settings::valueOf(m_file.document(), entry.path)) {
            values.insert(QString::fromStdString(entry.path), toVariant(*value));
        }
    }
    return values;
}

bool AppSettings::setValue(const QString& path, const QVariant& value)
{
    const settings::Entry* entry = settings::findSetting(path.toStdString());
    const auto converted = entry != nullptr ? fromVariant(*entry, value) : std::nullopt;
    settings::Settings next = m_file.document();
    if (!converted || !settings::assign(next, entry->path, *converted)) {
        return false;
    }
    change(next);
    return true;
}

QVariantMap AppSettings::overrides() const
{
    QVariantMap byId;
    for (const auto& [id, device] : m_devices.document().byId) {
        if (device.overrides.empty()) {
            continue;
        }
        QVariantMap values;
        for (const auto& [path, value] : device.overrides) {
            values.insert(QString::fromStdString(path), toVariant(value));
        }
        byId.insert(QString::fromStdString(id), values);
    }
    return byId;
}

bool AppSettings::setDeviceValue(const QString& id, const QString& path, const QVariant& value)
{
    const settings::Entry* entry = settings::findSetting(path.toStdString());
    const auto converted = entry != nullptr ? fromVariant(*entry, value) : std::nullopt;
    settings::DeviceSettings next = device(id.toStdString());
    if (!converted || !settings::setOverride(next, entry->path, *converted)) {
        return false;
    }
    const bool changed = next != device(id.toStdString());
    if (!setDevice(id.toStdString(), next)) {
        return false;
    }
    if (changed) {
        publishSettingsChanged({QString::fromStdString(entry->path)}, {}, {}, id);
    }
    return true;
}

void AppSettings::resetDeviceValue(const QString& id, const QString& path)
{
    settings::DeviceSettings next = device(id.toStdString());
    if (settings::clearOverride(next, path.toStdString())) {
        setDevice(id.toStdString(), next);
        publishSettingsChanged({path}, {}, {}, id);
    }
}

void AppSettings::change(const settings::Settings& next)
{
    if (next == m_file.document()) {
        return;
    }
    const bool appearance = next.appearance != m_file.document().appearance;
    const bool language = next.general.language != m_file.document().general.language;
    const std::pair<QStringList, QJsonObject> changes = changesOf(m_file.document(), next);
    m_file.update(next);
    if (appearance) {
        emit appearanceChanged();
    }
    if (language) {
        emit languageChanged();
    }
    emit valuesChanged();
    publishSettingsChanged(changes.first, changes.second);
}

QString AppSettings::language() const
{
    return toQString(settings::kLanguageNames[static_cast<std::size_t>(m_file.document().general.language)]);
}

QString AppSettings::interfaceLanguage() const
{
    if (m_file.document().general.language != settings::Language::System) {
        return language();
    }
    for (const QString& name : QLocale::system().uiLanguages()) {
        const QLocale::Language spoken = QLocale(name).language();
        if (spoken == QLocale::Chinese) {
            return QStringLiteral("zh_CN");
        }
        if (spoken == QLocale::English) {
            return QStringLiteral("en");
        }
    }
    return QStringLiteral("en");
}

bool AppSettings::placeWindow(QWindow* window, const QString& which)
{
    if (window == nullptr) {
        return false;
    }
    Placed& placed = m_placed[which];
    if (placed.window != nullptr) {
        disconnect(placed.window, nullptr, this, nullptr);
    }
    placed.window = window;
    if (placed.settle == nullptr) {
        placed.settle = new QTimer(this);
        placed.settle->setInterval(kSettle);
        placed.settle->setSingleShot(true); // source-rule-allow R9: a debounce; the window's state settles after a maximize
        connect(placed.settle, &QTimer::timeout, this, [this, which] { capture(which); });
    }
    QTimer* settleTimer = placed.settle;

    QScreen* primary = QGuiApplication::primaryScreen();
    QList<QRect> screens;
    if (primary != nullptr) {
        screens.append(primary->availableGeometry());
    }
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen != primary) {
            screens.append(screen->availableGeometry());
        }
    }
    const settings::WindowPlacement saved =
        which == u"assistant" ? m_file.document().assistantWindow : m_file.document().hubWindow;
    const QSize designed = window->size();
    window->create();
    window->setGeometry(fitWindow(saved, screens, designed, window->minimumSize()));

    const auto settle = [settleTimer] { settleTimer->start(); };
    connect(window, &QWindow::xChanged, this, settle);
    connect(window, &QWindow::yChanged, this, settle);
    connect(window, &QWindow::widthChanged, this, settle);
    connect(window, &QWindow::heightChanged, this, settle);
    connect(window, &QWindow::windowStateChanged, this, settle);
    connect(window, &QWindow::visibleChanged, this, [this, which, settleTimer](bool visible) {
        if (!visible) {
            settleTimer->stop();
            capture(which);
            m_file.flush();
        }
    });
    return saved.width > 0 && saved.maximized;
}

settings::DeviceSettings AppSettings::device(const std::string& id) const
{
    const auto& byId = m_devices.document().byId;
    const auto found = byId.find(id);
    return found == byId.end() ? settings::DeviceSettings{} : found->second;
}

const std::map<std::string, settings::DeviceSettings, std::less<>>& AppSettings::devices() const
{
    return m_devices.document().byId;
}

settings::Settings AppSettings::effective(const std::string& id) const
{
    return id.empty() ? m_file.document() : settings::effectiveSettings(m_file.document(), device(id));
}

bool AppSettings::setDevice(const std::string& id, const settings::DeviceSettings& device)
{
    if (id.empty() || id.size() > settings::Devices::kMaxIdLength) {
        return false;
    }
    settings::Devices next = m_devices.document();
    if (device == settings::DeviceSettings{}) {
        next.byId.erase(id);
    } else {
        next.byId.insert_or_assign(id, device);
    }
    if (next == m_devices.document()) {
        return true;
    }
    m_devices.update(next);
    emit devicesChanged();
    return true;
}

bool AppSettings::wirelessCollapsed() const
{
    return m_file.document().wireless.collapsed;
}

void AppSettings::setWirelessCollapsed(bool collapsed)
{
    if (collapsed == wirelessCollapsed()) {
        return;
    }
    settings::Settings next = m_file.document();
    next.wireless.collapsed = collapsed;
    m_file.update(next);
    emit wirelessChanged();
}

QVariantList AppSettings::recentConnections() const
{
    const auto& recent = m_file.document().wireless.recent;
    std::vector<std::pair<std::string, settings::RecentConnection>> newestFirst(recent.begin(), recent.end());
    std::ranges::sort(newestFirst, std::greater{}, [](const auto& entry) { return entry.second.lastUsed; });
    QVariantList list;
    for (const auto& [address, connection] : newestFirst) {
        list.append(QVariantMap{{QStringLiteral("address"), QString::fromStdString(address)},
                                {QStringLiteral("name"), QString::fromStdString(connection.name)},
                                {QStringLiteral("lastUsed"), static_cast<qint64>(connection.lastUsed)}});
    }
    return list;
}

const std::map<std::string, std::string, std::less<>>& AppSettings::shortcuts() const
{
    return m_file.document().shortcuts;
}

void AppSettings::setShortcuts(const std::map<std::string, std::string, std::less<>>& shortcuts)
{
    if (shortcuts == m_file.document().shortcuts) {
        return;
    }
    settings::Settings next = m_file.document();
    next.shortcuts = shortcuts;
    m_file.update(next);
}

const std::vector<std::string>& AppSettings::recentCommands() const
{
    return m_file.document().palette.recent;
}

void AppSettings::setRecentCommands(const std::vector<std::string>& ids)
{
    if (ids == m_file.document().palette.recent) {
        return;
    }
    settings::Settings next = m_file.document();
    next.palette.recent = ids;
    m_file.update(next);
}

const settings::ExtensionStates& AppSettings::extensions() const
{
    return m_file.document().extensions;
}

void AppSettings::setExtensions(const settings::ExtensionStates& extensions)
{
    if (extensions == m_file.document().extensions) {
        return;
    }
    settings::Settings next = m_file.document();
    next.extensions = extensions;
    m_file.update(next);
}

bool AppSettings::mcpEnabled() const
{
    return m_file.document().mcp.enabled;
}

const std::vector<std::string>& AppSettings::mcpClients() const
{
    return m_file.document().mcp.clients;
}

void AppSettings::setMcpClients(const std::vector<std::string>& clients)
{
    if (clients == m_file.document().mcp.clients) {
        return;
    }
    settings::Settings next = m_file.document();
    next.mcp.clients = clients;
    m_file.update(next);
    emit mcpClientsChanged();
}

bool AppSettings::mirrorToolbarHintShown() const
{
    return m_file.document().hints.mirrorToolbar;
}

void AppSettings::markMirrorToolbarHintShown()
{
    if (mirrorToolbarHintShown()) {
        return;
    }
    settings::Settings next = m_file.document();
    next.hints.mirrorToolbar = true;
    m_file.update(next);
    emit hintsChanged();
}

bool AppSettings::setupGuideShown() const
{
    return m_file.document().hints.setupGuide;
}

void AppSettings::markSetupGuideShown()
{
    if (setupGuideShown()) {
        return;
    }
    settings::Settings next = m_file.document();
    next.hints.setupGuide = true;
    m_file.update(next);
    emit hintsChanged();
}

QStringList AppSettings::toursSeen() const
{
    QStringList keys;
    for (const std::string& key : m_file.document().hints.tours) {
        keys.append(QString::fromStdString(key));
    }
    return keys;
}

bool AppSettings::tourSeen(const QString& key) const
{
    if (!usableTourKey(key)) {
        return false;
    }
    const std::vector<std::string>& tours = m_file.document().hints.tours;
    const std::string text = key.toStdString();
    return std::ranges::find(tours, text) != tours.end();
}

void AppSettings::markTourSeen(const QString& key)
{
    if (!usableTourKey(key) || tourSeen(key)) {
        return;
    }
    settings::Settings next = m_file.document();
    if (next.hints.tours.size() >= settings::Hints::kMaxTours) {
        return;
    }
    next.hints.tours.push_back(key.toStdString());
    m_file.update(next);
    emit toursChanged();
}

void AppSettings::resetTour(const QString& key)
{
    if (!usableTourKey(key)) {
        return;
    }
    const std::string text = key.toStdString();
    std::vector<std::string> kept;
    for (const std::string& entry : m_file.document().hints.tours) {
        if (entry != text) {
            kept.push_back(entry);
        }
    }
    if (kept.size() == m_file.document().hints.tours.size()) {
        return;
    }
    settings::Settings next = m_file.document();
    next.hints.tours = std::move(kept);
    m_file.update(next);
    emit toursChanged();
}

void AppSettings::resetTours()
{
    if (m_file.document().hints.tours.empty()) {
        return;
    }
    settings::Settings next = m_file.document();
    next.hints.tours.clear();
    m_file.update(next);
    emit toursChanged();
}

void AppSettings::setToursQuiet(bool quiet)
{
    if (quiet == m_toursQuiet) {
        return;
    }
    m_toursQuiet = quiet;
    emit toursQuietChanged();
}

QString AppSettings::acknowledgedCrash() const
{
    return QString::fromStdString(m_file.document().hints.crashNotice);
}

void AppSettings::acknowledgeCrash(const QString& name)
{
    const std::string text = name.toStdString();
    if (text.size() > settings::Hints::kMaxCrashLength || text == m_file.document().hints.crashNotice) {
        return;
    }
    settings::Settings next = m_file.document();
    next.hints.crashNotice = text;
    m_file.update(next);
    emit hintsChanged();
}

int AppSettings::launches() const
{
    return m_file.document().hints.launches;
}

void AppSettings::countLaunch()
{
    if (launches() >= settings::Hints::kMaxLaunches) {
        return;
    }
    settings::Settings next = m_file.document();
    ++next.hints.launches;
    m_file.update(next);
    emit hintsChanged();
}

int AppSettings::sponsorMilestone() const
{
    const settings::Hints& hints = m_file.document().hints;
    return sponsorMilestoneDue(hints.launches, hints.sponsorAsked);
}

void AppSettings::markSponsorAsked(int milestone)
{
    if (milestone <= m_file.document().hints.sponsorAsked || milestone > settings::Hints::kMaxLaunches) {
        return;
    }
    settings::Settings next = m_file.document();
    next.hints.sponsorAsked = milestone;
    m_file.update(next);
    emit hintsChanged();
}

void AppSettings::flush()
{
    m_file.flush();
    m_devices.flush();
}

void AppSettings::forgetConnection(const QString& address)
{
    settings::Settings next = m_file.document();
    if (next.wireless.recent.erase(address.toStdString()) == 0) {
        return;
    }
    m_file.update(next);
    emit wirelessChanged();
}

void AppSettings::rememberConnection(const QString& address)
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    settings::Settings next = m_file.document();
    settings::rememberConnection(next.wireless, address.toStdString(),
                                 std::chrono::duration_cast<std::chrono::seconds>(now).count());
    m_file.update(next);
    emit wirelessChanged();
}

void AppSettings::nameConnection(const QString& address, const QString& name)
{
    settings::Settings next = m_file.document();
    const auto found = next.wireless.recent.find(address.toStdString());
    std::string text = name.left(settings::RecentConnection::kMaxNameLength / 4).toStdString();
    if (found == next.wireless.recent.end() || found->second.name == text) {
        return;
    }
    found->second.name = std::move(text);
    m_file.update(next);
    emit wirelessChanged();
}

bool AppSettings::otherVideoExpanded() const
{
    return m_file.document().otherVideo.expanded;
}

void AppSettings::setOtherVideoExpanded(bool expanded)
{
    if (expanded == otherVideoExpanded()) {
        return;
    }
    settings::Settings next = m_file.document();
    next.otherVideo.expanded = expanded;
    m_file.update(next);
    emit otherVideoChanged();
}

void AppSettings::capture(const QString& which)
{
    const auto found = m_placed.find(which);
    if (found == m_placed.end() || found->second.window == nullptr) {
        return;
    }
    QWindow* window = found->second.window;
    const Qt::WindowStates states = window->windowStates();
    if (states & (Qt::WindowMinimized | Qt::WindowFullScreen)) {
        return;
    }
    settings::Settings next = m_file.document();
    settings::WindowPlacement& placement = which == u"assistant" ? next.assistantWindow : next.hubWindow;
    if (states & Qt::WindowMaximized) {
        placement.maximized = true;
    } else {
        const QRect geometry = window->geometry();
        placement = {geometry.x(), geometry.y(), geometry.width(), geometry.height(), false};
    }
    m_file.update(next);
}

}
