#include "LanguagePacks.h"

#include "AppSettings.h"
#include "ExtensionHost.h"
#include "QtConversions.h"
#include "Settings.h"

#include <gs/ext/Manifest.h>
#include <gs/ext/Package.h>

#include <QCoreApplication>
#include <QFile>
#include <QJSEngine>
#include <QStringList>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace gs::app {

using namespace Qt::StringLiterals;

namespace {

namespace fs = std::filesystem;

QString packagePath(const fs::path& directory, const std::string& relative)
{
    const fs::path inside(std::u8string(reinterpret_cast<const char8_t*>(relative.data()), relative.size()));
    return QString::fromStdWString((directory / inside).wstring());
}

}

std::vector<LanguagePack> extensionLanguagePacks(const ExtensionHost& host, const QString& language)
{
    std::vector<LanguagePack> packs;
    for (const ExtensionHost::Extension& extension : host.extensions()) {
        if (!extension.active || !extension.manifest) {
            continue;
        }
        const ext::Manifest& manifest = *extension.manifest;
        for (const ext::LocaleContribution& locale : manifest.contributes.locales) {
            const QString tag = toQString(locale.locale);
            if (std::ranges::any_of(packs, [&tag](const LanguagePack& pack) { return pack.locale == tag; })) {
                continue;
            }
            packs.push_back({.locale = tag,
                             .name = toQString(locale.name),
                             .extension = toQString(manifest.id),
                             .extensionName = extensionText(manifest, "name", manifest.name, language),
                             .translations = packagePath(extension.directory, locale.translations),
                             .help = locale.help.empty() ? QString() : packagePath(extension.directory, locale.help)});
        }
    }
    std::ranges::stable_sort(packs, [](const LanguagePack& left, const LanguagePack& right) {
        return QString::localeAwareCompare(left.name, right.name) < 0;
    });
    return packs;
}

bool loadTranslationFile(QTranslator& translator, QByteArray& data, const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    constexpr auto kMostBytes = static_cast<qsizetype>(ext::kMaxTranslationBytes);
    QByteArray read = file.read(kMostBytes + 1);
    if (read.size() > kMostBytes ||
        !ext::compiledTranslation(std::string_view(read.constData(), static_cast<std::size_t>(read.size()))) ||
        !translator.load(reinterpret_cast<const uchar*>(read.constData()), static_cast<int>(read.size()))) {
        return false;
    }
    data = std::move(read);
    return true;
}

LanguagePacks::LanguagePacks(AppSettings& settings, QObject* parent)
    : QObject(parent), m_settings(settings)
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
    connect(&m_settings, &AppSettings::languageChanged, this, [this] {
        apply();
        settle();
    });
    apply();
    settle();
}

LanguagePacks::~LanguagePacks()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

LanguagePacks* LanguagePacks::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void LanguagePacks::follow(ExtensionHost& extensions)
{
    m_host = &extensions;
    const auto update = [this, &extensions] {
        setPacks(extensionLanguagePacks(extensions, m_settings.interfaceLanguage()));
    };
    update();
    connect(&extensions, &ExtensionHost::extensionsChanged, this, update);
}

void LanguagePacks::setPacks(std::vector<LanguagePack> packs)
{
    if (packs != m_packs) {
        m_packs = std::move(packs);
        emit packsChanged();
    }
    QStringList tags;
    for (const LanguagePack& pack : m_packs) {
        tags.append(pack.locale);
    }
    m_settings.setLanguagePacks(tags);
    apply();
    settle();
}

const LanguagePack* LanguagePacks::find(const QString& locale) const
{
    const auto found = std::ranges::find(m_packs, locale, &LanguagePack::locale);
    return found != m_packs.end() ? &*found : nullptr;
}

std::vector<HelpLanguage> LanguagePacks::helpLanguages() const
{
    std::vector<HelpLanguage> languages;
    for (const LanguagePack& pack : m_packs) {
        languages.push_back({.code = pack.locale, .translations = pack.translations, .folder = pack.help});
    }
    return languages;
}

QVariantList LanguagePacks::packList() const
{
    QVariantList list;
    for (const LanguagePack& pack : m_packs) {
        list.append(QVariantMap{{u"locale"_s, pack.locale},
                                {u"name"_s, pack.name},
                                {u"extension"_s, pack.extension},
                                {u"extensionName"_s, pack.extensionName}});
    }
    return list;
}

void LanguagePacks::apply()
{
    const QString language = m_settings.interfaceLanguage();
    const LanguagePack* pack = find(language);
    const QString source = pack != nullptr ? pack->translations : QString();
    if (m_applied && language == m_language && source == m_source) {
        return;
    }
    m_applied = true;
    m_language = language;
    m_source = source;
    m_loaded.clear();
    QCoreApplication::removeTranslator(&m_pack);
    QCoreApplication::removeTranslator(&m_builtin);
    if (m_builtin.load(u"gamescrcpy_"_s + (pack != nullptr ? u"en"_s : language), u":/i18n"_s)) {
        QCoreApplication::installTranslator(&m_builtin);
    }
    if (pack != nullptr) {
        if (loadTranslationFile(m_pack, m_packData, pack->translations)) {
            QCoreApplication::installTranslator(&m_pack);
            m_loaded = pack->locale;
        } else {
            qWarning("language pack %s: %s cannot be read as a compiled translation", qUtf8Printable(pack->locale),
                     qUtf8Printable(pack->translations));
        }
    }
    emit translationsChanged();
}

void LanguagePacks::settle()
{
    QVariantMap now = missingNow();
    if (now != m_missing) {
        m_missing = std::move(now);
        emit missingChanged();
    }
}

QVariantMap LanguagePacks::missingNow() const
{
    const QString chosen = m_settings.language();
    const std::string tag = chosen.toStdString();
    if (std::ranges::find(settings::kLanguageNames, tag) != settings::kLanguageNames.end() || find(chosen) != nullptr) {
        return {};
    }
    QVariantMap gone{{u"locale"_s, chosen}, {u"name"_s, chosen}, {u"extensionName"_s, QString()}, {u"installed"_s, false}};
    if (m_host == nullptr) {
        return gone;
    }
    const QString language = m_settings.interfaceLanguage();
    for (const ExtensionHost::Extension& extension : m_host->extensions()) {
        if (!extension.usable()) {
            continue;
        }
        const ext::Manifest& manifest = *extension.manifest;
        const auto found = std::ranges::find(manifest.contributes.locales, tag, &ext::LocaleContribution::locale);
        if (found == manifest.contributes.locales.end()) {
            continue;
        }
        gone.insert(u"name"_s, toQString(found->name));
        gone.insert(u"extensionName"_s, extensionText(manifest, "name", manifest.name, language));
        gone.insert(u"installed"_s, true);
        break;
    }
    return gone;
}

void watchLanguageHelp(HelpLibrary& help, LanguagePacks& languages)
{
    const auto refresh = [&help, &languages] { help.setLanguagePacks(languages.helpLanguages()); };
    QObject::connect(&languages, &LanguagePacks::packsChanged, &help, refresh);
    refresh();
}

}
