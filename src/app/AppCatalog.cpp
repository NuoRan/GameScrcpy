#include "AppCatalog.h"

#include <QClipboard>
#include <QCollator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJSEngine>
#include <QSaveFile>
#include <QUrl>
#include <QtLogging>

#include <algorithm>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

constexpr std::string_view kPngSignature = "\x89PNG\r\n\x1a\n";

QString workingText(PackageAction action, int count)
{
    switch (action) {
    case PackageAction::ForceStop:
        return AppCatalog::tr("Force stopping %n app(s)…", "", count);
    case PackageAction::Disable:
        return AppCatalog::tr("Disabling %n app(s)…", "", count);
    case PackageAction::Enable:
        return AppCatalog::tr("Enabling %n app(s)…", "", count);
    case PackageAction::ClearData:
        return AppCatalog::tr("Clearing the data of %n app(s)…", "", count);
    case PackageAction::Uninstall:
        return AppCatalog::tr("Uninstalling %n app(s)…", "", count);
    }
    return {};
}

QString confirmTitle(PackageAction action, int count)
{
    switch (action) {
    case PackageAction::ForceStop:
        return AppCatalog::tr("Force stop %n app(s)?", "", count);
    case PackageAction::Disable:
        return AppCatalog::tr("Disable %n app(s)?", "", count);
    case PackageAction::Enable:
        return AppCatalog::tr("Enable %n app(s)?", "", count);
    case PackageAction::ClearData:
        return AppCatalog::tr("Clear the data of %n app(s)?", "", count);
    case PackageAction::Uninstall:
        return AppCatalog::tr("Uninstall %n app(s)?", "", count);
    }
    return {};
}

QString confirmText(PackageAction action)
{
    switch (action) {
    case PackageAction::ForceStop:
        return AppCatalog::tr("They close at once. Anything unsaved in them is lost.");
    case PackageAction::Disable:
        return AppCatalog::tr("They stop running and leave the phone's home screen until they are enabled again.");
    case PackageAction::Enable:
        return AppCatalog::tr("They can run again and come back to the phone's home screen.");
    case PackageAction::ClearData:
        return AppCatalog::tr(
            "Their accounts, settings and saved files on the phone are deleted. The apps stay installed.");
    case PackageAction::Uninstall:
        return AppCatalog::tr("The apps and their data are removed from the phone.");
    }
    return {};
}

QString skippedText(PackageAction action, int skipped)
{
    switch (action) {
    case PackageAction::Disable:
        return AppCatalog::tr("%n selected app(s) are already disabled.", "", skipped);
    case PackageAction::Enable:
        return AppCatalog::tr("%n selected app(s) are already enabled.", "", skipped);
    case PackageAction::ForceStop:
        return AppCatalog::tr("%n selected app(s) are skipped: system apps and disabled apps cannot be force stopped.",
                              "", skipped);
    case PackageAction::ClearData:
    case PackageAction::Uninstall:
        break;
    }
    return AppCatalog::tr("%n selected system app(s) are skipped: system apps can only be disabled or enabled.", "",
                          skipped);
}

QString acceptText(PackageAction action)
{
    switch (action) {
    case PackageAction::ForceStop:
        return AppCatalog::tr("Force stop");
    case PackageAction::Disable:
        return AppCatalog::tr("Disable");
    case PackageAction::Enable:
        return AppCatalog::tr("Enable");
    case PackageAction::ClearData:
        return AppCatalog::tr("Clear data");
    case PackageAction::Uninstall:
        return AppCatalog::tr("Uninstall");
    }
    return {};
}

bool isPng(const QByteArray& data)
{
    return data.size() > static_cast<qsizetype>(kPngSignature.size()) &&
           std::string_view(data.constData(), kPngSignature.size()) == kPngSignature;
}

QString fileUrl(const std::filesystem::path& path)
{
    return QUrl::fromLocalFile(QString::fromStdWString(path.wstring())).toString();
}

}

AppListModel::AppListModel(QObject* parent) : QAbstractListModel(parent) {}

int AppListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_shown.size());
}

QVariant AppListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_shown.size())) {
        return {};
    }
    const AppItem& app = m_apps[m_shown[static_cast<std::size_t>(index.row())]];
    switch (role) {
    case PackageRole:
        return app.package;
    case LabelRole:
    case Qt::DisplayRole:
        return app.label;
    case IconRole:
        return app.icon;
    case SystemRole:
        return app.system;
    case GameRole:
        return app.game;
    case EnabledRole:
        return app.enabled;
    case CheckedRole:
        return m_checked.contains(app.package);
    default:
        return {};
    }
}

QHash<int, QByteArray> AppListModel::roleNames() const
{
    return {{PackageRole, "packageName"},
            {LabelRole, "label"},
            {IconRole, "iconUrl"},
            {SystemRole, "systemApp"},
            {GameRole, "game"},
            {EnabledRole, "appEnabled"},
            {CheckedRole, "checked"}};
}

void AppListModel::reset(std::vector<AppItem> apps)
{
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::ranges::stable_sort(apps, [&collator](const AppItem& a, const AppItem& b) {
        const int order = collator.compare(a.label, b.label);
        return order != 0 ? order < 0 : a.package < b.package;
    });
    beginResetModel();
    m_apps = std::move(apps);
    std::erase_if(m_checked, [this](const QString& package) {
        return std::ranges::find(m_apps, package, &AppItem::package) == m_apps.end();
    });
    m_shown.clear();
    for (std::size_t i = 0; i < m_apps.size(); ++i) {
        if (accepts(m_apps[i])) {
            m_shown.push_back(i);
        }
    }
    endResetModel();
}

void AppListModel::setIcon(const QString& package, const QString& icon)
{
    const auto found = std::ranges::find(m_apps, package, &AppItem::package);
    if (found == m_apps.end() || found->icon == icon) {
        return;
    }
    found->icon = icon;
    const auto position = static_cast<std::size_t>(found - m_apps.begin());
    const auto shown = std::ranges::find(m_shown, position);
    if (shown != m_shown.end()) {
        const QModelIndex changed = index(static_cast<int>(shown - m_shown.begin()));
        emit dataChanged(changed, changed, {IconRole});
    }
}

void AppListModel::setFilter(Filter filter)
{
    if (filter != m_filter) {
        m_filter = filter;
        refilter();
    }
}

void AppListModel::setQuery(const QString& query)
{
    const QString trimmed = query.trimmed();
    if (trimmed != m_query) {
        m_query = trimmed;
        refilter();
    }
}

const AppItem* AppListModel::find(const QString& package) const
{
    const auto found = std::ranges::find(m_apps, package, &AppItem::package);
    return found == m_apps.end() ? nullptr : &*found;
}

QString AppListModel::packageAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_shown.size())) {
        return {};
    }
    return m_apps[m_shown[static_cast<std::size_t>(row)]].package;
}

void AppListModel::setChecked(const QString& package, bool checked)
{
    const auto found = std::ranges::find(m_apps, package, &AppItem::package);
    if (found == m_apps.end() || m_checked.contains(package) == checked) {
        return;
    }
    if (checked) {
        m_checked.insert(package);
    } else {
        m_checked.erase(package);
    }
    const auto position = static_cast<std::size_t>(found - m_apps.begin());
    const auto shown = std::ranges::find(m_shown, position);
    if (shown != m_shown.end()) {
        const QModelIndex changed = index(static_cast<int>(shown - m_shown.begin()));
        emit dataChanged(changed, changed, {CheckedRole});
    }
}

void AppListModel::checkShown()
{
    for (const std::size_t position : m_shown) {
        m_checked.insert(m_apps[position].package);
    }
    checksChanged();
}

void AppListModel::clearChecked()
{
    if (m_checked.empty()) {
        return;
    }
    m_checked.clear();
    checksChanged();
}

QStringList AppListModel::checkedPackages() const
{
    QStringList packages;
    for (const AppItem& app : m_apps) {
        if (m_checked.contains(app.package)) {
            packages.append(app.package);
        }
    }
    return packages;
}

void AppListModel::checksChanged()
{
    if (!m_shown.empty()) {
        emit dataChanged(index(0), index(static_cast<int>(m_shown.size()) - 1), {CheckedRole});
    }
}

bool AppListModel::accepts(const AppItem& app) const
{
    if ((m_filter == Filter::Games && !app.game) || (m_filter == Filter::System && !app.system)) {
        return false;
    }
    return m_query.isEmpty() || app.label.contains(m_query, Qt::CaseInsensitive) ||
           app.package.contains(m_query, Qt::CaseInsensitive);
}

void AppListModel::refilter()
{
    beginResetModel();
    m_shown.clear();
    for (std::size_t i = 0; i < m_apps.size(); ++i) {
        if (accepts(m_apps[i])) {
            m_shown.push_back(i);
        }
    }
    endResetModel();
}

AppCatalog::AppCatalog(std::filesystem::path iconDirectory, Source source, QObject* parent)
    : QObject(parent), m_iconDirectory(std::move(iconDirectory)), m_source(std::move(source))
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
}

AppCatalog::~AppCatalog()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

AppCatalog* AppCatalog::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

bool AppCatalog::loading() const
{
    return m_listRequest != 0 || (m_wantList && m_availability.connected && m_availability.apps && !m_listFailed);
}

bool AppCatalog::ready() const
{
    return !m_serial.isEmpty() && m_availability.connected && m_availability.apps && m_availability.bulk;
}

QString AppCatalog::problem() const
{
    if (m_serial.isEmpty()) {
        return {};
    }
    if (!m_availability.connected) {
        return tr("Mirror this phone to see and start its apps");
    }
    if (!m_availability.apps) {
        return tr("This phone cannot list its apps");
    }
    if (m_listFailed) {
        return tr("The phone could not list its apps");
    }
    return {};
}

int AppCatalog::total() const
{
    return static_cast<int>(m_model.all().size());
}

int AppCatalog::games() const
{
    return static_cast<int>(std::ranges::count_if(m_model.all(), &AppItem::game));
}

int AppCatalog::systemApps() const
{
    return static_cast<int>(std::ranges::count_if(m_model.all(), &AppItem::system));
}

void AppCatalog::setQuery(const QString& query)
{
    if (query == m_query) {
        return;
    }
    m_query = query;
    m_model.setQuery(query);
    emit queryChanged();
}

void AppCatalog::setFilter(int filter)
{
    const int clamped = std::clamp(filter, 0, 2);
    if (clamped == m_filter) {
        return;
    }
    m_filter = clamped;
    m_model.setFilter(static_cast<AppListModel::Filter>(clamped));
    emit filterChanged();
}

void AppCatalog::open(const QString& serial)
{
    if (serial != m_serial) {
        m_serial = serial;
        m_model.reset({});
        m_iconQueue.clear();
        m_iconRequests.clear();
        m_retries.clear();
        m_listRequest = 0;
        m_current.clear();
        m_selecting = false;
        m_running.clear();
        m_batch.reset();
        m_installs.clear();
        m_installing.clear();
        emit openedChanged();
        emit countsChanged();
        emit currentChanged();
        emit selectionChanged();
        emit workChanged();
    }
    m_wantList = !serial.isEmpty();
    m_listFailed = false;
    refresh();
}

void AppCatalog::close()
{
    open({});
}

void AppCatalog::reload()
{
    if (m_serial.isEmpty() || m_listRequest != 0) {
        return;
    }
    m_wantList = true;
    m_listFailed = false;
    refresh();
}

void AppCatalog::launch(const QString& package, bool independent)
{
    const AppItem* app = m_model.find(package);
    if (app == nullptr || !app->enabled || !ready() || (independent && !m_availability.independent)) {
        return;
    }
    qInfo("starting %s (%s)%s", qUtf8Printable(app->label), qUtf8Printable(package),
          independent ? " on an independent display" : "");
    m_source.launch(m_serial, package, app->label, independent);
}

void AppCatalog::copyPackage(const QString& package) const
{
    if (QClipboard* clipboard = QGuiApplication::clipboard(); clipboard != nullptr && !package.isEmpty()) {
        clipboard->setText(package);
    }
}

void AppCatalog::listed(const QString& serial, std::uint32_t requestId, const std::vector<protocol::AppEntry>& apps)
{
    if (serial != m_serial || requestId != m_listRequest || requestId == 0) {
        return;
    }
    m_listRequest = 0;
    m_listFailed = false;
    std::vector<AppItem> items;
    items.reserve(apps.size());
    for (const protocol::AppEntry& entry : apps) {
        AppItem item{.package = QString::fromStdString(entry.package),
                     .label = QString::fromStdString(entry.label),
                     .versionCode = entry.versionCode,
                     .system = entry.system,
                     .game = entry.game,
                     .enabled = entry.enabled};
        if (item.package.isEmpty()) {
            continue;
        }
        if (item.label.trimmed().isEmpty()) {
            item.label = item.package;
        }
        if (const auto cached = cachedIcon(item)) {
            item.icon = *cached;
        }
        items.push_back(std::move(item));
    }
    if (const auto kept = m_turnedOff.find(serial); kept != m_turnedOff.end()) {
        for (auto app = kept->second.begin(); app != kept->second.end();) {
            if (std::ranges::find(items, app->first, &AppItem::package) != items.end()) {
                app = kept->second.erase(app);
            } else {
                items.push_back(app->second);
                ++app;
            }
        }
    }
    m_model.reset(std::move(items));
    if (!m_current.isEmpty() && m_model.find(m_current) == nullptr) {
        m_current.clear();
    }
    m_iconQueue.clear();
    for (const AppItem& app : m_model.all()) {
        if (app.icon.isEmpty() && validPackageName(app.package)) {
            m_iconQueue.push_back(app.package);
        }
    }
    qInfo("%zu apps on %s, %zu icons to fetch", m_model.all().size(), qUtf8Printable(serial), m_iconQueue.size());
    pumpIcons();
    emit countsChanged();
    emit stateChanged();
    emit currentChanged();
    emit selectionChanged();
}

void AppCatalog::iconArrived(const QString& serial, std::uint32_t requestId, const QString& package,
                             const QByteArray& png)
{
    if (serial != m_serial) {
        return;
    }
    const auto found = m_iconRequests.find(requestId);
    if (found == m_iconRequests.end()) {
        return;
    }
    const QString requested = found->second;
    m_iconRequests.erase(found);
    const AppItem* app = m_model.find(requested);
    if (app != nullptr && package == requested && isPng(png)) {
        if (const auto stored = store(*app, png)) {
            m_model.setIcon(requested, *stored);
        }
    }
    pumpIcons();
}

void AppCatalog::requestFailed(const QString& serial, std::uint32_t requestId, protocol::ErrorCode code,
                               const QString& message)
{
    if (serial != m_serial) {
        return;
    }
    if (requestId != 0 && requestId == m_listRequest) {
        m_listRequest = 0;
        m_listFailed = true;
        qWarning("the apps of %s could not be listed: %s", qUtf8Printable(serial), qUtf8Printable(message));
        emit stateChanged();
        return;
    }
    const auto found = m_iconRequests.find(requestId);
    if (found == m_iconRequests.end()) {
        return;
    }
    const QString package = found->second;
    m_iconRequests.erase(found);
    int& retries = m_retries[package];
    if (code == protocol::ErrorCode::ResourceBusy && retries < kBusyRetries) {
        ++retries;
        m_iconQueue.push_back(package);
    } else {
        retries = kBusyRetries + 1;
    }
    pumpIcons();
}

void AppCatalog::availabilityChanged(const QString& serial)
{
    if (serial == m_serial && !m_serial.isEmpty()) {
        refresh();
    }
}

void AppCatalog::setActions(Actions actions)
{
    m_actions = std::move(actions);
}

void AppCatalog::setCurrent(const QString& package)
{
    if (package != m_current) {
        m_current = package;
        emit currentChanged();
    }
}

QVariantMap AppCatalog::currentApp() const
{
    const AppItem* app = m_current.isEmpty() ? nullptr : m_model.find(m_current);
    if (app == nullptr) {
        return {};
    }
    QVariantMap info{{QStringLiteral("package"), app->package},
                     {QStringLiteral("label"), app->label},
                     {QStringLiteral("system"), app->system},
                     {QStringLiteral("enabled"), app->enabled}};
    for (const PackageAction action : kPackageActions) {
        info.insert(packageActionName(action), actionAllowed(action, app->system, app->enabled));
    }
    return info;
}

void AppCatalog::setSelecting(bool selecting)
{
    if (selecting == m_selecting) {
        return;
    }
    m_selecting = selecting;
    if (!selecting) {
        m_model.clearChecked();
    }
    emit selectionChanged();
}

QVariantMap AppCatalog::eligible() const
{
    const QStringList checked = m_model.checkedPackages();
    QVariantMap counts;
    for (const PackageAction action : kPackageActions) {
        const auto count = std::ranges::count_if(checked, [this, action](const QString& package) {
            const AppItem* app = m_model.find(package);
            return app != nullptr && actionAllowed(action, app->system, app->enabled);
        });
        counts.insert(packageActionName(action), static_cast<int>(count));
    }
    return counts;
}

QString AppCatalog::activity() const
{
    if (m_batch) {
        return workingText(m_batch->action, m_batch->total - m_batch->succeeded - m_batch->failed);
    }
    if (m_installing.isEmpty()) {
        return {};
    }
    const QString name = m_installing.value(QStringLiteral("name")).toString();
    if (m_installing.value(QStringLiteral("state")).toString() != u"copying") {
        return tr("Installing %1…").arg(name);
    }
    const double total = m_installing.value(QStringLiteral("total")).toDouble();
    const double done = m_installing.value(QStringLiteral("done")).toDouble();
    const int percent = total > 0 ? static_cast<int>(std::clamp(done * 100.0 / total, 0.0, 100.0)) : 0;
    return tr("Copying %1 to the phone… %2%").arg(name).arg(percent);
}

void AppCatalog::toggleChecked(const QString& package)
{
    if (m_model.find(package) == nullptr) {
        return;
    }
    m_model.setChecked(package, !m_model.isChecked(package));
    emit selectionChanged();
}

void AppCatalog::checkAll()
{
    m_model.checkShown();
    emit selectionChanged();
}

void AppCatalog::clearChecked()
{
    m_model.clearChecked();
    emit selectionChanged();
}

QVariantMap AppCatalog::plan(const QString& action, bool batch) const
{
    const std::optional<PackageAction> chosen = packageActionNamed(action);
    if (!chosen) {
        return {};
    }
    QStringList candidates;
    if (batch) {
        candidates = m_model.checkedPackages();
    } else if (!m_current.isEmpty()) {
        candidates.append(m_current);
    }
    QStringList packages;
    int skipped = 0;
    for (const QString& package : candidates) {
        const AppItem* app = m_model.find(package);
        if (app != nullptr && actionAllowed(*chosen, app->system, app->enabled)) {
            packages.append(package);
        } else {
            ++skipped;
        }
    }
    const bool destructive = *chosen == PackageAction::Uninstall || *chosen == PackageAction::ClearData;
    const auto count = static_cast<int>(packages.size());
    QVariantMap result{{QStringLiteral("action"), action},
                       {QStringLiteral("packages"), packages},
                       {QStringLiteral("count"), count},
                       {QStringLiteral("skipped"), skipped},
                       {QStringLiteral("confirm"), batch || destructive},
                       {QStringLiteral("danger"), destructive},
                       {QStringLiteral("accept"), acceptText(*chosen)}};
    if (count == 0) {
        return result;
    }
    if (batch) {
        QString text = confirmText(*chosen);
        if (skipped > 0) {
            text.append(u' ').append(skippedText(*chosen, skipped));
        }
        result.insert(QStringLiteral("title"), confirmTitle(*chosen, count));
        result.insert(QStringLiteral("text"), text);
    } else if (destructive) {
        const QString label = m_model.find(packages.front())->label;
        const bool uninstall = *chosen == PackageAction::Uninstall;
        result.insert(QStringLiteral("title"),
                      uninstall ? tr("Uninstall %1?").arg(label) : tr("Clear the data of %1?").arg(label));
        result.insert(QStringLiteral("text"),
                      uninstall ? tr("%1 and its data are removed from the phone.").arg(label)
                                : tr("Its accounts, settings and saved files on the phone are deleted. The app stays "
                                     "installed."));
    }
    return result;
}

int AppCatalog::run(const QString& action, const QStringList& packages)
{
    const std::optional<PackageAction> chosen = packageActionNamed(action);
    if (!chosen || !ready() || busy() || !m_actions.act) {
        return 0;
    }
    Batch batch{.action = *chosen};
    for (const QString& package : packages) {
        const AppItem* app = m_model.find(package);
        if (app == nullptr || !actionAllowed(*chosen, app->system, app->enabled)) {
            qWarning("%s is not offered for %s", qUtf8Printable(action), qUtf8Printable(package));
            continue;
        }
        const quint64 id = m_actions.act(m_serial, *chosen, package);
        if (id == 0) {
            continue;
        }
        m_running.emplace(id, Running{.action = *chosen, .package = package, .label = app->label});
        batch.label = app->label;
        ++batch.total;
    }
    if (batch.total == 0) {
        return 0;
    }
    m_batch = batch;
    emit workChanged();
    return batch.total;
}

int AppCatalog::install(const QList<QUrl>& files)
{
    if (!ready() || !m_actions.install) {
        return 0;
    }
    int started = 0;
    int skipped = 0;
    for (const QUrl& file : files) {
        const QString path = file.isLocalFile() ? file.toLocalFile() : QString();
        if (path.isEmpty() || !path.endsWith(u".apk", Qt::CaseInsensitive)) {
            ++skipped;
            continue;
        }
        const quint64 id = m_actions.install(m_serial, path);
        if (id == 0) {
            ++skipped;
            continue;
        }
        m_installs.emplace(id, QFileInfo(path).fileName());
        ++started;
    }
    if (skipped > 0) {
        emit reported(tr("Only APK files on this computer can be installed"), QStringLiteral("warn"));
    }
    return started;
}

void AppCatalog::actionFinished(quint64 id, const QString& serial, const QString& package, bool ok,
                                const QString& reason)
{
    if (serial != m_serial || !m_batch) {
        return;
    }
    const auto found = m_running.find(id);
    if (found == m_running.end() || found->second.package != package) {
        return;
    }
    const Running running = found->second;
    m_running.erase(found);
    if (ok) {
        ++m_batch->succeeded;
        remember(running);
    } else {
        ++m_batch->failed;
        if (m_batch->failedLabel.isEmpty()) {
            m_batch->failedLabel = running.label;
            m_batch->reason = reason;
        }
    }
    if (m_running.empty()) {
        const auto [text, tone] = summary(*m_batch);
        m_batch.reset();
        emit reported(text, tone);
        relist();
    }
    emit workChanged();
}

void AppCatalog::installUpdated(const QVariantMap& job)
{
    if (m_serial.isEmpty() || job.value(QStringLiteral("serial")).toString() != m_serial) {
        return;
    }
    const qint64 id = job.value(QStringLiteral("id")).toLongLong();
    const QString state = job.value(QStringLiteral("state")).toString();
    if (state != u"done" && state != u"failed") {
        m_installing = job;
        emit workChanged();
        return;
    }
    if (m_installing.value(QStringLiteral("id")).toLongLong() == id) {
        m_installing.clear();
    }
    if (const auto mine = m_installs.find(static_cast<quint64>(id)); mine != m_installs.end()) {
        const QString name = mine->second;
        m_installs.erase(mine);
        if (state == u"done") {
            emit reported(tr("Installed %1").arg(name), QStringLiteral("ok"));
        } else {
            emit reported(tr("Could not install %1: %2").arg(name, job.value(QStringLiteral("message")).toString()),
                          QStringLiteral("bad"));
        }
    }
    if (state == u"done") {
        relist();
    }
    emit workChanged();
}

std::optional<std::filesystem::path> AppCatalog::iconFile(const std::filesystem::path& directory,
                                                          const QString& package, std::uint64_t versionCode)
{
    if (!validPackageName(package)) {
        return std::nullopt;
    }
    return directory / (package + u'@' + QString::number(versionCode) + u".png").toStdWString();
}

void AppCatalog::refresh()
{
    m_availability = m_serial.isEmpty() || !m_source.availability ? AppAvailability{} : m_source.availability(m_serial);
    if (!ready()) {
        if (m_listRequest != 0) {
            m_listRequest = 0;
            m_wantList = true;
        }
        for (const auto& request : m_iconRequests) {
            m_iconQueue.push_front(request.second);
        }
        m_iconRequests.clear();
    }
    if (ready() && m_wantList && m_listRequest == 0) {
        const std::uint32_t requestId = nextRequest();
        if (m_source.list && m_source.list(m_serial, requestId)) {
            m_listRequest = requestId;
            m_wantList = false;
        }
    }
    if (ready() && m_iconQueue.empty() && m_iconRequests.empty()) {
        for (const AppItem& app : m_model.all()) {
            const auto retried = m_retries.find(app.package);
            if (app.icon.isEmpty() && validPackageName(app.package) &&
                (retried == m_retries.end() || retried->second <= kBusyRetries)) {
                m_iconQueue.push_back(app.package);
            }
        }
    }
    pumpIcons();
    emit stateChanged();
}

void AppCatalog::relist()
{
    if (m_serial.isEmpty()) {
        return;
    }
    m_listRequest = 0;
    m_wantList = true;
    m_listFailed = false;
    refresh();
}

void AppCatalog::remember(const Running& running)
{
    if (running.action == PackageAction::Disable) {
        if (const AppItem* app = m_model.find(running.package)) {
            AppItem off = *app;
            off.enabled = false;
            m_turnedOff[m_serial].insert_or_assign(running.package, std::move(off));
        }
    } else if (running.action == PackageAction::Enable || running.action == PackageAction::Uninstall) {
        if (const auto kept = m_turnedOff.find(m_serial); kept != m_turnedOff.end()) {
            kept->second.erase(running.package);
        }
    }
}

std::pair<QString, QString> AppCatalog::summary(const Batch& batch)
{
    if (batch.failed == 0) {
        const QString ok = QStringLiteral("ok");
        if (batch.total == 1) {
            switch (batch.action) {
            case PackageAction::ForceStop:
                return {tr("Force stopped %1").arg(batch.label), ok};
            case PackageAction::Disable:
                return {tr("Disabled %1").arg(batch.label), ok};
            case PackageAction::Enable:
                return {tr("Enabled %1").arg(batch.label), ok};
            case PackageAction::ClearData:
                return {tr("Cleared the data of %1").arg(batch.label), ok};
            case PackageAction::Uninstall:
                return {tr("Uninstalled %1").arg(batch.label), ok};
            }
        }
        switch (batch.action) {
        case PackageAction::ForceStop:
            return {tr("Force stopped %n app(s)", "", batch.total), ok};
        case PackageAction::Disable:
            return {tr("Disabled %n app(s)", "", batch.total), ok};
        case PackageAction::Enable:
            return {tr("Enabled %n app(s)", "", batch.total), ok};
        case PackageAction::ClearData:
            return {tr("Cleared the data of %n app(s)", "", batch.total), ok};
        case PackageAction::Uninstall:
            return {tr("Uninstalled %n app(s)", "", batch.total), ok};
        }
        return {};
    }
    if (batch.total == 1) {
        const QString bad = QStringLiteral("bad");
        switch (batch.action) {
        case PackageAction::ForceStop:
            return {tr("Could not force stop %1: %2").arg(batch.failedLabel, batch.reason), bad};
        case PackageAction::Disable:
            return {tr("Could not disable %1: %2").arg(batch.failedLabel, batch.reason), bad};
        case PackageAction::Enable:
            return {tr("Could not enable %1: %2").arg(batch.failedLabel, batch.reason), bad};
        case PackageAction::ClearData:
            return {tr("Could not clear the data of %1: %2").arg(batch.failedLabel, batch.reason), bad};
        case PackageAction::Uninstall:
            return {tr("Could not uninstall %1: %2").arg(batch.failedLabel, batch.reason), bad};
        }
    }
    return {tr("%n app(s) failed. %1: %2", "", batch.failed).arg(batch.failedLabel, batch.reason),
            batch.succeeded > 0 ? QStringLiteral("warn") : QStringLiteral("bad")};
}

void AppCatalog::pumpIcons()
{
    if (!ready() || !m_source.icon) {
        return;
    }
    while (m_iconRequests.size() < kIconsInFlight && !m_iconQueue.empty()) {
        const QString package = m_iconQueue.front();
        m_iconQueue.pop_front();
        const AppItem* app = m_model.find(package);
        if (app == nullptr || !app->icon.isEmpty() ||
            std::ranges::any_of(m_iconRequests, [&package](const auto& entry) { return entry.second == package; })) {
            continue;
        }
        const std::uint32_t requestId = nextRequest();
        if (!m_source.icon(m_serial, requestId, package, kIconSize)) {
            m_iconQueue.push_front(package);
            return;
        }
        m_iconRequests.emplace(requestId, package);
    }
}

std::uint32_t AppCatalog::nextRequest() noexcept
{
    ++m_lastRequest;
    if (m_lastRequest == 0) {
        ++m_lastRequest;
    }
    return m_lastRequest;
}

std::optional<QString> AppCatalog::cachedIcon(const AppItem& app) const
{
    const auto path = iconFile(m_iconDirectory, app.package, app.versionCode);
    std::error_code error;
    if (!path || !std::filesystem::is_regular_file(*path, error)) {
        return std::nullopt;
    }
    return fileUrl(*path);
}

std::optional<QString> AppCatalog::store(const AppItem& app, const QByteArray& png) const
{
    const auto path = iconFile(m_iconDirectory, app.package, app.versionCode);
    if (!path) {
        return std::nullopt;
    }
    std::error_code error;
    std::filesystem::create_directories(m_iconDirectory, error);
    QSaveFile file(QString::fromStdWString(path->wstring()));
    if (!file.open(QIODevice::WriteOnly) || file.write(png) != png.size() || !file.commit()) {
        qWarning("the icon of %s could not be saved to %s", qUtf8Printable(app.package),
                 qUtf8Printable(QString::fromStdWString(path->wstring())));
        return std::nullopt;
    }
    const std::wstring prefix = (app.package + u'@').toStdWString();
    for (const auto& entry : std::filesystem::directory_iterator(m_iconDirectory, error)) {
        const std::wstring name = entry.path().filename().wstring();
        if (entry.path() != *path && name.starts_with(prefix) && entry.path().extension() == L".png") {
            std::error_code removed;
            std::filesystem::remove(entry.path(), removed);
        }
    }
    return fileUrl(*path);
}

}
