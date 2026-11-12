#include "SettingsCatalog.h"

#include "AppSettings.h"
#include "Commands.h"
#include "ExtensionHost.h"
#include "QtConversions.h"
#include "SettingVariant.h"
#include "Settings.h"
#include "SettingsSearch.h"

#include <gs/ext/Manifest.h>

#include <QCoreApplication>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace gs::app {

namespace {

constexpr std::array<std::string_view, 9> kPageNames{"",           "general",  "video", "audio",    "input",
                                                     "appearance", "advanced", "ai",    "assistant"};
constexpr std::array<std::string_view, 4> kKindNames{"choice", "integer", "boolean", "text"};
static_assert(kPageNames.size() == static_cast<std::size_t>(settings::Page::Assistant) + 1);
static_assert(kKindNames.size() == static_cast<std::size_t>(settings::Kind::Text) + 1);

QString translated(std::string_view text)
{
    return text.empty() ? QString() : QCoreApplication::translate("Settings", std::string(text).c_str());
}

constexpr int kMostDecimals = 4;
constexpr double kMostSliderStops = 200.0;

QString controlOf(const ext::SettingDeclaration& setting, double step)
{
    switch (setting.type) {
    case ext::SettingType::Boolean:
        return QStringLiteral("switch");
    case ext::SettingType::Choice:
        return QStringLiteral("select");
    case ext::SettingType::Integer:
    case ext::SettingType::Number:
        return setting.min && setting.max && (*setting.max - *setting.min) / step <= kMostSliderStops
                   ? QStringLiteral("slider")
                   : QStringLiteral("number");
    case ext::SettingType::Text:
        break;
    }
    return QStringLiteral("text");
}

int placesOf(double value)
{
    for (int decimals = 0; decimals < kMostDecimals; ++decimals) {
        const double scaled = value * std::pow(10.0, decimals);
        if (std::abs(scaled - std::round(scaled)) < 1e-9) {
            return decimals;
        }
    }
    return kMostDecimals;
}

int decimalsOf(const ext::SettingDeclaration& setting)
{
    if (setting.type != ext::SettingType::Number) {
        return 0;
    }
    int decimals = setting.step ? placesOf(*setting.step) : 2;
    if (setting.min) {
        decimals = std::max(decimals, placesOf(*setting.min));
    }
    if (setting.max) {
        decimals = std::max(decimals, placesOf(*setting.max));
    }
    return decimals;
}

}

std::vector<SettingInfo> settingInfos()
{
    std::vector<SettingInfo> infos;
    for (const settings::Entry& entry : settings::catalog()) {
        if (entry.page == settings::Page::None) {
            continue;
        }
        QStringList choices;
        for (const std::string_view choice : entry.choices) {
            choices.append(toQString(choice));
        }
        const settings::Page page = entry.page == settings::Page::Assistant ? settings::Page::Ai : entry.page;
        infos.push_back(SettingInfo{.path = toQString(entry.path),
                                    .section = toQString(kPageNames[static_cast<std::size_t>(page)]),
                                    .title = translated(entry.facets.title),
                                    .description = translated(entry.facets.description),
                                    .kind = toQString(kKindNames[static_cast<std::size_t>(entry.kind)]),
                                    .choices = choices,
                                    .defaultValue = toVariant(entry.defaultValue),
                                    .minimum = static_cast<qint64>(entry.min),
                                    .maximum = static_cast<qint64>(entry.max),
                                    .perDevice = entry.facets.scope == settings::Scope::Device});
    }
    return infos;
}

SettingsCatalog::SettingsCatalog(QObject* parent)
    : QObject(parent)
{
    if (const ExtensionHost* host = ExtensionHost::instance()) {
        connect(host, &ExtensionHost::extensionsChanged, this, [this] {
            rebuild();
            emit entriesChanged();
            refilter();
            recount();
        });
        connect(host, &ExtensionHost::valuesChanged, this, &SettingsCatalog::recount);
    }
    if (const AppSettings* settings = AppSettings::instance()) {
        connect(settings, &AppSettings::devicesChanged, this, &SettingsCatalog::recount);
    }
    rebuild();
    recount();
}

void SettingsCatalog::retranslate()
{
    rebuild();
    emit entriesChanged();
    refilter();
}

QVariantMap SettingsCatalog::pageOverrides() const
{
    return m_pageOverrides;
}

void SettingsCatalog::recount()
{
    QVariantMap byPage;
    const QString id = device();
    if (!id.isEmpty()) {
        QVariantMap values;
        if (const AppSettings* settings = AppSettings::instance()) {
            values.insert(settings->overrides().value(id).toMap());
        }
        if (const ExtensionHost* host = ExtensionHost::instance()) {
            values.insert(host->overrides().value(id).toMap());
        }
        for (auto value = values.cbegin(); value != values.cend(); ++value) {
            const QString page = m_entries.value(value.key()).toMap().value(QStringLiteral("page")).toString();
            if (page.isEmpty()) {
                continue;
            }
            QStringList paths = byPage.value(page).toStringList();
            paths.append(value.key());
            byPage.insert(page, paths);
        }
    }
    if (byPage != m_pageOverrides) {
        m_pageOverrides = byPage;
        emit overridesChanged();
    }
}

void SettingsCatalog::rebuild()
{
    m_entries.clear();
    m_searchable.clear();
    m_perDevicePages.clear();
    for (const settings::Entry& entry : settings::catalog()) {
        const QString path = toQString(entry.path);
        const QString page = toQString(kPageNames[static_cast<std::size_t>(entry.page)]);
        const QString title = translated(entry.facets.title);
        const QString description = translated(entry.facets.description);
        const bool perDevice = entry.facets.scope == settings::Scope::Device;
        QStringList choices;
        for (const std::string_view choice : entry.choices) {
            choices.append(toQString(choice));
        }
        m_entries.insert(path, QVariantMap{
                                   {QStringLiteral("title"), title},
                                   {QStringLiteral("description"), description},
                                   {QStringLiteral("page"), page},
                                   {QStringLiteral("kind"), toQString(kKindNames[static_cast<std::size_t>(entry.kind)])},
                                   {QStringLiteral("choices"), choices},
                                   {QStringLiteral("minimum"), static_cast<qint64>(entry.min)},
                                   {QStringLiteral("maximum"), static_cast<qint64>(entry.max)},
                                   {QStringLiteral("maxLength"), static_cast<qint64>(entry.maxLength)},
                                   {QStringLiteral("live"), entry.facets.live},
                                   {QStringLiteral("perDevice"), perDevice},
                                   {QStringLiteral("defaultValue"), toVariant(entry.defaultValue)},
                               });
        m_searchable.push_back({path,
                                page,
                                perDevice,
                                {title, description, translated(entry.facets.keywords), toQString(entry.facets.title),
                                 toQString(entry.facets.description), toQString(entry.facets.keywords)}});
        if (perDevice && !m_perDevicePages.contains(page)) {
            m_perDevicePages.append(page);
        }
    }
    if (const Commands* commands = Commands::instance()) {
        const QString page = QStringLiteral("shortcuts");
        const char* sectionTitle = QT_TRANSLATE_NOOP("SettingsCatalog", "Keyboard shortcuts");
        const char* sectionKeywords = QT_TRANSLATE_NOOP("SettingsCatalog", "shortcut hotkey keys key binding");
        m_searchable.push_back({page,
                                page,
                                false,
                                {QCoreApplication::translate("SettingsCatalog", sectionTitle),
                                 QCoreApplication::translate("SettingsCatalog", sectionKeywords),
                                 QString::fromUtf8(sectionTitle), QString::fromUtf8(sectionKeywords)}});
        for (const Command& command : commands->table().all()) {
            m_searchable.push_back({page + u'.' + command.id,
                                    page,
                                    false,
                                    {QCoreApplication::translate("Commands", command.title),
                                     QCoreApplication::translate("Commands", command.keywords),
                                     QString::fromUtf8(command.title), QString::fromUtf8(command.keywords)}});
        }
    }
    addExtensionSettings();
}

void SettingsCatalog::addExtensionSettings()
{
    m_extensionGroups.clear();
    const ExtensionHost* host = ExtensionHost::instance();
    if (host == nullptr) {
        return;
    }
    const AppSettings* appSettings = AppSettings::instance();
    const QString language = appSettings != nullptr ? appSettings->interfaceLanguage() : QStringLiteral("en");
    const QString page = QStringLiteral("extensions");
    std::vector<QVariantMap> groups;
    for (const ext::Manifest* manifest : host->settingsManifests()) {
        const QString id = QString::fromStdString(manifest->id);
        const QString name = extensionText(*manifest, "name", manifest->name, language);
        QVariantList rows;
        for (const ext::SettingDeclaration& setting : manifest->settings) {
            const QString path = extensionSettingPath(manifest->id, setting.key);
            const std::string prefix = "settings." + setting.key + ".";
            const QString title = extensionText(*manifest, prefix + "title", setting.title, language);
            const QString description = extensionText(*manifest, prefix + "description", setting.description, language);
            const QString keywords = extensionText(*manifest, prefix + "keywords", setting.keywords, language);
            const bool perDevice = setting.scope == ext::SettingScope::Device;
            const int decimals = decimalsOf(setting);
            const double step =
                setting.type == ext::SettingType::Integer
                    ? std::max(std::round(setting.step.value_or(1.0)), 1.0)
                    : std::max(setting.step.value_or(std::pow(10.0, -decimals)), std::pow(10.0, -kMostDecimals));
            const QString control = controlOf(setting, step);
            QStringList choices;
            QVariantMap labels;
            for (const std::string& choice : setting.choices) {
                const QString value = QString::fromStdString(choice);
                choices.append(value);
                labels.insert(value, extensionText(*manifest, prefix + "choices." + choice, choice, language));
            }
            QVariantMap entry{
                {QStringLiteral("title"), title},
                {QStringLiteral("description"), description},
                {QStringLiteral("page"), page},
                {QStringLiteral("kind"), toQString(ext::kSettingTypeNames[static_cast<std::size_t>(setting.type)])},
                {QStringLiteral("control"), control},
                {QStringLiteral("choices"), choices},
                {QStringLiteral("choiceLabels"), labels},
                {QStringLiteral("step"), step},
                {QStringLiteral("decimals"), decimals},
                {QStringLiteral("maxLength"), static_cast<qint64>(setting.maxLength.value_or(0))},
                {QStringLiteral("live"), setting.live},
                {QStringLiteral("perDevice"), perDevice},
                {QStringLiteral("defaultValue"), extensionVariant(setting, setting.defaultValue)},
                {QStringLiteral("extension"), id},
                {QStringLiteral("extensionName"), name},
            };
            if (setting.min) {
                entry.insert(QStringLiteral("minimum"), *setting.min);
            }
            if (setting.max) {
                entry.insert(QStringLiteral("maximum"), *setting.max);
            }
            m_entries.insert(path, entry);
            m_searchable.push_back({path,
                                    page,
                                    perDevice,
                                    {title, description, keywords, QString::fromStdString(setting.title),
                                     QString::fromStdString(setting.description),
                                     QString::fromStdString(setting.keywords), name}});
            if (perDevice && !m_perDevicePages.contains(page)) {
                m_perDevicePages.append(page);
            }
            rows.append(QVariantMap{{QStringLiteral("path"), path}, {QStringLiteral("control"), control}});
        }
        groups.push_back(
            QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), name}, {QStringLiteral("settings"), rows}});
    }
    std::ranges::sort(groups, [](const QVariantMap& left, const QVariantMap& right) {
        return QString::localeAwareCompare(left.value(QStringLiteral("name")).toString(),
                                           right.value(QStringLiteral("name")).toString()) < 0;
    });
    for (QVariantMap& group : groups) {
        m_extensionGroups.append(std::move(group));
    }
}

QVariantMap SettingsCatalog::entries() const
{
    return m_entries;
}

QVariantList SettingsCatalog::extensionGroups() const
{
    return m_extensionGroups;
}

QStringList SettingsCatalog::perDevicePages() const
{
    return m_perDevicePages;
}

QString SettingsCatalog::query() const
{
    return m_query;
}

void SettingsCatalog::setQuery(const QString& query)
{
    if (query == m_query) {
        return;
    }
    m_query = query;
    emit queryChanged();
    refilter();
}

bool SettingsCatalog::searching() const
{
    return !m_query.trimmed().isEmpty();
}

QString SettingsCatalog::scopeDevice() const
{
    return m_scopeDevice;
}

QString SettingsCatalog::scopeDeviceName() const
{
    return m_scopeDeviceName;
}

bool SettingsCatalog::scoped() const
{
    return m_scoped;
}

void SettingsCatalog::setScoped(bool scoped)
{
    scoped = scoped && !m_scopeDevice.isEmpty();
    if (scoped == m_scoped) {
        return;
    }
    m_scoped = scoped;
    emit scopeChanged();
    refilter();
    recount();
}

QString SettingsCatalog::device() const
{
    return m_scoped ? m_scopeDevice : QString();
}

QVariantMap SettingsCatalog::hits() const
{
    return m_hits;
}

QVariantMap SettingsCatalog::pageHits() const
{
    return m_pageHits;
}

int SettingsCatalog::hitCount() const
{
    return static_cast<int>(m_hits.size());
}

void SettingsCatalog::openForDevice(const QString& id, const QString& name)
{
    m_scopeDevice = id;
    m_scopeDeviceName = name;
    m_scoped = !id.isEmpty();
    emit scopeChanged();
    refilter();
    recount();
    emit openRequested();
}

void SettingsCatalog::refilter()
{
    m_hits.clear();
    m_pageHits.clear();
    if (searching()) {
        for (const Searchable& item : m_searchable) {
            if ((!m_scoped || item.perDevice) && matchesQuery(item.texts, m_query)) {
                m_hits.insert(item.path, true);
                m_pageHits.insert(item.page, m_pageHits.value(item.page).toInt() + 1);
            }
        }
    }
    emit filterChanged();
}

}
