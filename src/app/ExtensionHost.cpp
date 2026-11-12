#include "ExtensionHost.h"

#include "AmsiScan.h"
#include "AppEvents.h"
#include "AppSettings.h"
#include "ExtensionHealth.h"
#include "ExtensionIcons.h"
#include "NativeExtensions.h"
#include "ProfileEditor.h"
#include "QtConversions.h"
#include "ThemeTokens.h"

#include <gs/base/Log.h>
#include <gs/ext/Archive.h>
#include <gs/ext/Bundle.h>
#include <gs/ext/Resolution.h>
#include <gs/ext/Signing.h>
#include <gs/input/Profile.h>
#include <gs/input/ProfilePatch.h>
#include <gs/platform/AtomicFile.h>
#include <gs/script/Services.h>

#include <nlohmann/json.hpp>

#include <QCoreApplication>
#include <QImageReader>
#include <QJSEngine>
#include <QMetaType>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

namespace gs::app {

namespace {

namespace fs = std::filesystem;

constexpr int kMaxTextureSide = 8192;
constexpr std::size_t kMaxBisectBytes = 256 * 1024;
constexpr std::size_t kMaxProfileBytes = 1024 * 1024;

const ext::HostApi& hostApi()
{
    static const ext::HostApi api = [] {
        ext::HostApi offered;
        offered.versions[ext::ExtensionPoint::Filter] = {ext::Version{1, 0, 0}};
        offered.versions[ext::ExtensionPoint::Theme] = {ext::Version{1, 0, 0}};
        offered.versions[ext::ExtensionPoint::Profile] = {ext::Version{1, 0, 0}};
        offered.versions[ext::ExtensionPoint::Native] = {ext::Version{1, 0, 0}};
        offered.versions[ext::ExtensionPoint::Component] = {ext::Version{1, 0, 0}};
        offered.versions[ext::ExtensionPoint::Task] = {ext::Version{1, 0, 0}};
        offered.versions[ext::ExtensionPoint::Script] = {ext::Version{1, 0, 0}};
        offered.versions[ext::ExtensionPoint::Panel] = {ext::Version{1, 0, 0}};
        offered.versions[ext::ExtensionPoint::Help] = {ext::Version{1, 0, 0}};
        offered.versions[ext::ExtensionPoint::Tour] = {ext::Version{1, 0, 0}};
        offered.versions[ext::ExtensionPoint::Locale] = {ext::Version{1, 0, 0}};
        return offered;
    }();
    return api;
}

std::string memberPath(std::string_view id)
{
    return "$[\"" + std::string(id) + "\"]";
}

fs::path pathOf(std::string_view text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::optional<std::string> readText(const fs::path& file, std::size_t maxBytes)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string text(maxBytes + 1, '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (in.bad()) {
        return std::nullopt;
    }
    text.resize(static_cast<std::size_t>(in.gcount()));
    if (text.size() > maxBytes) {
        return std::nullopt;
    }
    return text;
}

ext::SettingValue settingValueOf(const settings::Scalar& scalar)
{
    return std::visit([](const auto& value) { return ext::SettingValue(value); }, scalar);
}

nlohmann::json settingJson(const ext::SettingValue& value)
{
    return std::visit(
        [](const auto& held) -> nlohmann::json {
            if constexpr (std::is_same_v<std::decay_t<decltype(held)>, std::monostate>) {
                return nullptr;
            } else {
                return held;
            }
        },
        value);
}

bool storable(const ext::SettingDeclaration& setting, const settings::Scalar& value)
{
    const auto* text = std::get_if<std::string>(&value);
    const auto characters = [](const std::string& bytes) {
        return static_cast<std::size_t>(
            std::ranges::count_if(bytes, [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
    };
    return (text == nullptr || (text->size() <= settings::kMaxExtensionTextLength &&
                                characters(*text) <= settings::kMaxExtensionTextCharacters)) &&
           ext::settingAccepts(setting, settingValueOf(value));
}

std::optional<double> numberOf(const ext::SettingValue& value)
{
    if (const auto* flag = std::get_if<bool>(&value)) {
        return *flag ? 1.0 : 0.0;
    }
    if (const auto* whole = std::get_if<std::int64_t>(&value)) {
        return static_cast<double>(*whole);
    }
    if (const auto* number = std::get_if<double>(&value)) {
        return *number;
    }
    return std::nullopt;
}

const settings::Scalar* scalarOf(const settings::Scalars& values, std::string_view key)
{
    const auto found = values.find(key);
    return found == values.end() ? nullptr : &found->second;
}

struct SettingTarget {
    std::string id;
    std::string key;
};

std::optional<SettingTarget> targetOf(const QString& path)
{
    const QString prefix = QStringLiteral("ext:");
    const qsizetype slash = path.startsWith(prefix) ? path.indexOf(u'/', prefix.size()) : -1;
    if (slash < 0) {
        return std::nullopt;
    }
    return SettingTarget{path.mid(prefix.size(), slash - prefix.size()).toStdString(), path.mid(slash + 1).toStdString()};
}

std::optional<settings::Scalar> scalarFrom(const ext::SettingDeclaration& setting, const QVariant& value)
{
    const int type = value.typeId();
    const bool numeric = type == QMetaType::Int || type == QMetaType::LongLong || type == QMetaType::UInt ||
                         type == QMetaType::ULongLong || type == QMetaType::Double || type == QMetaType::Float;
    const double number = numeric ? value.toDouble() : 0.0;
    switch (setting.type) {
    case ext::SettingType::Boolean:
        return type == QMetaType::Bool ? std::optional<settings::Scalar>(value.toBool()) : std::nullopt;
    case ext::SettingType::Integer:
        if (!numeric || !std::isfinite(number) || std::trunc(number) != number || std::abs(number) > 9.0e15) {
            return std::nullopt;
        }
        return settings::Scalar(static_cast<std::int64_t>(number));
    case ext::SettingType::Number:
        return numeric && std::isfinite(number) ? std::optional<settings::Scalar>(number) : std::nullopt;
    case ext::SettingType::Choice:
    case ext::SettingType::Text:
        return type == QMetaType::QString ? std::optional<settings::Scalar>(value.toString().toStdString())
                                          : std::nullopt;
    }
    return std::nullopt;
}

double fitted(const ext::FilterParam& param, double value)
{
    if (param.min) {
        value = std::max(value, *param.min);
    }
    if (param.max) {
        value = std::min(value, *param.max);
    }
    switch (param.type) {
    case ext::ParamType::Int:
        return std::round(value);
    case ext::ParamType::Bool:
        return value != 0.0 ? 1.0 : 0.0;
    case ext::ParamType::Float:
        break;
    }
    return value;
}

fs::path stagingOf(const fs::path& root)
{
    return root / ".staging" / ("bundle-" + QUuid::createUuid().toString(QUuid::Id128).toStdString());
}

std::optional<settings::Scalar> storedOf(const ext::SettingValue& value)
{
    return std::visit(
        [](const auto& held) -> std::optional<settings::Scalar> {
            if constexpr (std::is_same_v<std::decay_t<decltype(held)>, std::monostate>) {
                return std::nullopt;
            } else {
                return settings::Scalar(held);
            }
        },
        value);
}

std::optional<fs::path> localPath(const QString& fileOrUrl)
{
    QString local = fileOrUrl;
    if (fileOrUrl.indexOf(u':') > 1) {
        const QUrl url(fileOrUrl);
        if (!url.isValid() || !url.isLocalFile() || !url.authority().isEmpty()) {
            return std::nullopt;
        }
        local = url.toLocalFile();
    }
    const auto letter = [](QChar c) { return (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z'); };
    const bool drive = local.size() >= 3 && letter(local.at(0)) && local.at(1) == u':' &&
                       (local.at(2) == u'/' || local.at(2) == u'\\');
    if (!drive || local.contains(QChar::Null)) {
        return std::nullopt;
    }
    return fs::path(local.toStdWString());
}

QString folderName(const fs::path& directory)
{
    return QString::fromStdWString((directory.parent_path().filename() / directory.filename()).generic_wstring());
}

std::optional<fs::path> newestFolder(const fs::path& directory, std::optional<std::chrono::minutes> within)
{
    std::optional<fs::path> newest;
    std::error_code error;
    const fs::file_time_type now = fs::file_time_type::clock::now();
    for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        std::error_code ignored;
        if (!it->is_directory(ignored)) {
            continue;
        }
        if (within) {
            const fs::file_time_type written = it->last_write_time(ignored);
            if (ignored || now - written > *within) {
                continue;
            }
        }
        if (!newest || it->path().filename() > newest->filename()) {
            newest = it->path();
        }
    }
    return newest;
}

QVariantMap healthOf(const ExtensionHealth& health, std::string_view id)
{
    const HealthRecord* record = health.find(id);
    if (record == nullptr) {
        return {};
    }
    QVariantList events;
    for (auto it = record->events.rbegin(); it != record->events.rend(); ++it) {
        events.append(QVariantMap{
            {QStringLiteral("at"), QString::fromStdString(ExtensionHealth::timeText(it->at))},
            {QStringLiteral("version"), QString::fromStdString(it->version)},
            {QStringLiteral("kind"), toQString(ExtensionHealth::kindName(it->kind))},
            {QStringLiteral("code"),
             it->code == 0 ? QString() : QStringLiteral("0x") + QString::number(it->code, 16).toUpper().rightJustified(8, u'0')},
            {QStringLiteral("report"), QString::fromStdString(it->report)}});
    }
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return {{QStringLiteral("off"), record->off ? toQString(ExtensionHealth::kindName(*record->off)) : QString()},
            {QStringLiteral("recent"), static_cast<int>(health.crashesSince(id, now - std::chrono::days(7)))},
            {QStringLiteral("events"), events}};
}

std::optional<std::pair<fs::path, fs::path>> folderParts(const QString& folder)
{
    const fs::path path(folder.toStdWString());
    if (folder.contains(QChar::Null) || path.has_root_path()) {
        return std::nullopt;
    }
    const std::vector<fs::path> parts(path.begin(), path.end());
    const bool plain = parts.size() == 2 && std::ranges::none_of(parts, [](const fs::path& part) {
                           return part.empty() || part == L"." || part == L"..";
                       });
    if (!plain) {
        return std::nullopt;
    }
    return std::pair(parts[0], parts[1]);
}

using Release = std::pair<std::string, ext::Version>;

std::vector<Release> builtinReleases(const std::vector<ExtensionHost::Extension>& extensions)
{
    std::vector<Release> releases;
    for (const ExtensionHost::Extension& extension : extensions) {
        if (extension.builtin && extension.usable()) {
            releases.emplace_back(extension.manifest->id, extension.manifest->version);
        }
    }
    return releases;
}

QVariantMap hostProblem(ext::Severity severity, const QString& code, const QString& detail, const QString& hint)
{
    return {{QStringLiteral("severity"), toQString(ext::severityName(severity))},
            {QStringLiteral("code"), code},
            {QStringLiteral("path"), QString()},
            {QStringLiteral("detail"), detail},
            {QStringLiteral("hint"), hint}};
}

QVariantMap busyProblem(const QString& detail)
{
    return hostProblem(ext::Severity::Error, QStringLiteral("install-busy"), detail, QString());
}

QVariantMap notLocalProblem(const QString& fileOrUrl)
{
    return hostProblem(ext::Severity::Error, QStringLiteral("not-local-file"), fileOrUrl,
                       ExtensionHost::tr("Only files on this computer can be opened. Copy the file to a folder on "
                                         "this computer first."));
}

QVariantMap builtinProblem(const ext::Version& version)
{
    return hostProblem(ext::Severity::Error, QStringLiteral("builtin-version"),
                       QString::fromStdString(version.text()),
                       ExtensionHost::tr("This version is already built in, so there is no need to install it."));
}

QVariantMap supersededProblem(const ext::Version& version)
{
    return hostProblem(ext::Severity::Warning, QStringLiteral("superseded-by-builtin"),
                       QString::fromStdString(version.text()),
                       ExtensionHost::tr("The same version is built in, so this copy is not used. You can remove it."));
}

QString hintOf(ext::Code code)
{
    switch (code) {
    case ext::Code::NativeArchitectureMissing:
        return ExtensionHost::tr("This extension has no version for this computer.");
    case ext::Code::SignatureInvalid:
        return ExtensionHost::tr("Its author signature does not match its files: it was changed after it was signed.");
    case ext::Code::SignatureMissing:
        return ExtensionHost::tr("The installed version is signed by its author, but this one is not. Install "
                                 "updates only as its author publishes them.");
    case ext::Code::PublisherChanged:
        return ExtensionHost::tr("This update is signed with a different key than the installed version. To accept "
                                 "the new key, remove the extension and install this package again.");
    default:
        break;
    }
    return toQString(ext::fixHint(code));
}

QVariantList problemList(const std::vector<ext::Diagnostic>& diagnostics)
{
    constexpr std::size_t kMostListed = 100;
    QVariantList list;
    for (const ext::Diagnostic& diagnostic : diagnostics) {
        if (static_cast<std::size_t>(list.size()) == kMostListed) {
            list.append(QVariantMap{{QStringLiteral("severity"), toQString(ext::severityName(ext::Severity::Notice))},
                                    {QStringLiteral("code"), QStringLiteral("more-problems")},
                                    {QStringLiteral("path"), QString()},
                                    {QStringLiteral("detail"),
                                     QStringLiteral("%1 more").arg(diagnostics.size() - kMostListed)},
                                    {QStringLiteral("hint"), QString()}});
            break;
        }
        list.append(QVariantMap{{QStringLiteral("severity"), toQString(ext::severityName(diagnostic.severity))},
                                {QStringLiteral("code"), toQString(ext::codeName(diagnostic.code))},
                                {QStringLiteral("path"), QString::fromStdString(diagnostic.path)},
                                {QStringLiteral("detail"), QString::fromStdString(diagnostic.detail)},
                                {QStringLiteral("hint"), hintOf(diagnostic.code)}});
    }
    return list;
}

QString permissionTitle(std::string_view name)
{
    if (name == "device.input") {
        return ExtensionHost::tr("Send touches and keys");
    }
    if (name == "device.screen") {
        return ExtensionHost::tr("Read the phone screen");
    }
    if (name == "device.clipboard") {
        return ExtensionHost::tr("Use the phone clipboard");
    }
    if (name == "device.apps") {
        return ExtensionHost::tr("List and open apps");
    }
    if (name == "devices.other") {
        return ExtensionHost::tr("Control other phones");
    }
    if (name == "mapping") {
        return ExtensionHost::tr("Trigger controls and switch profiles");
    }
    if (name == "notify") {
        return ExtensionHost::tr("Show system notifications");
    }
    if (name == "network") {
        return ExtensionHost::tr("Use the network");
    }
    if (name == "files.pick") {
        return ExtensionHost::tr("Open files you choose");
    }
    if (name == "files.paths") {
        return ExtensionHost::tr("Read and write folders you choose for it");
    }
    if (name == "input.read") {
        return ExtensionHost::tr("Read your keyboard and mouse");
    }
    if (name == "hardware") {
        return ExtensionHost::tr("Access the devices you choose");
    }
    if (name == "ai") {
        return ExtensionHost::tr("Use the AI service you set up (it spends your quota)");
    }
    if (name == "devices.provide") {
        return ExtensionHost::tr("Add devices to the devices page");
    }
    if (name == "network.listen") {
        return ExtensionHost::tr("Accept connections from programs on this computer");
    }
    if (name == "speech.listen") {
        return ExtensionHost::tr("Listen for voice commands you say");
    }
    if (name == "events.app") {
        return ExtensionHost::tr("Know what happens on every phone and window");
    }
    if (name == "input.provide") {
        return ExtensionHost::tr("Feed the buttons of home-made devices into the key mapping");
    }
    if (name == "transport") {
        return ExtensionHost::tr("Provide ways to connect to phones");
    }
    if (name == ext::kMediaCapturePermission) {
        return ExtensionHost::tr("Provide pictures or sound");
    }
    if (name == ext::kMediaOutputPermission) {
        return ExtensionHost::tr("Send the picture or the sound elsewhere");
    }
    if (name == "media.read") {
        return ExtensionHost::tr("Read the mirrored picture and sound");
    }
    if (name == "ai.provide") {
        return ExtensionHost::tr("Provide AI model services");
    }
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

QString permissionScope(const ext::Permission& permission)
{
    if (permission.name == ext::kNetworkPermission && !permission.hosts.empty()) {
        QStringList hosts;
        for (const std::string& host : permission.hosts) {
            hosts.append(host == "*" ? ExtensionHost::tr("any website") : QString::fromStdString(host));
        }
        return ExtensionHost::tr("Websites: %1").arg(hosts.join(QStringLiteral(", ")));
    }
    if (permission.name == ext::kFilesPathsPermission && !permission.folders.empty()) {
        QStringList folders;
        for (const ext::PermissionFolder& folder : permission.folders) {
            folders.append(QStringLiteral("%1 (%2)").arg(QString::fromStdString(folder.name),
                                                         QString::fromStdString(folder.purpose)));
        }
        return ExtensionHost::tr("Folders: %1").arg(folders.join(QStringLiteral("; ")));
    }
    return {};
}

QString panelProblem(const ext::Manifest& manifest, const ext::PanelContribution& panel)
{
    const QString script = QString::fromStdString(panel.script);
    const auto found = std::ranges::find(manifest.contributes.scripts, panel.script, &ext::ScriptContribution::name);
    if (found == manifest.contributes.scripts.end()) {
        return ExtensionHost::tr("Its script %1 is not in the extension").arg(script);
    }
    const bool forApp = ext::panelForApp(panel.where);
    if (panel.where == ext::PanelPlace::Page && found->run != ext::ScriptRun::App) {
        return ExtensionHost::tr("A page needs a script that runs once for the app, and %1 runs for each phone")
            .arg(script);
    }
    if (forApp && found->run != ext::ScriptRun::App) {
        return ExtensionHost::tr("This place needs a script that runs once for the app, and %1 runs for each phone")
            .arg(script);
    }
    if (!forApp && found->run != ext::ScriptRun::Session) {
        return ExtensionHost::tr("This place needs a script that runs for each phone, and %1 runs once for the app")
            .arg(script);
    }
    return {};
}

QVariantList panelList(const ext::Manifest& manifest, const QString& language)
{
    QVariantList panels;
    for (const ext::PanelContribution& panel : manifest.contributes.panels) {
        panels.append(QVariantMap{
            {QStringLiteral("name"), QString::fromStdString(panel.name)},
            {QStringLiteral("title"), extensionText(manifest, "panels." + panel.name + ".title", panel.title, language)},
            {QStringLiteral("icon"), QString::fromStdString(panel.icon)},
            {QStringLiteral("where"), toQString(ext::kPanelPlaceNames[static_cast<std::size_t>(panel.where)])},
            {QStringLiteral("problem"), panelProblem(manifest, panel)}});
    }
    return panels;
}

QVariantList serviceList(const ext::Manifest& manifest, const QString& language)
{
    QVariantList services;
    for (const ext::ServiceContribution& service : manifest.contributes.services) {
        const std::string base = "services." + service.name;
        services.append(QVariantMap{
            {QStringLiteral("name"), QString::fromStdString(service.name)},
            {QStringLiteral("title"),
             extensionText(manifest, base + ".title", service.title.empty() ? service.name : service.title, language)},
            {QStringLiteral("description"), extensionText(manifest, base + ".description", service.description, language)},
            {QStringLiteral("call"), QString::fromStdString(manifest.id + "/" + service.name)}});
    }
    return services;
}

QVariantList converterList(const ext::Manifest& manifest, const QString& language)
{
    QVariantList converters;
    for (const ext::ConverterContribution& converter : manifest.contributes.converters) {
        QStringList extensions;
        for (const std::string& extension : converter.extensions) {
            extensions.append(QString::fromStdString(extension));
        }
        converters.append(QVariantMap{
            {QStringLiteral("name"), QString::fromStdString(converter.name)},
            {QStringLiteral("title"),
             extensionText(manifest, "converters." + converter.name + ".title", converter.title, language)},
            {QStringLiteral("from"), QString::fromStdString(converter.from)},
            {QStringLiteral("to"), QString::fromStdString(converter.to)},
            {QStringLiteral("extensions"), extensions},
            {QStringLiteral("imports"), ext::appFormat(converter.to)}});
    }
    return converters;
}

QVariantList localeList(const ext::Manifest& manifest)
{
    QVariantList locales;
    for (const ext::LocaleContribution& locale : manifest.contributes.locales) {
        locales.append(QVariantMap{{QStringLiteral("locale"), QString::fromStdString(locale.locale)},
                                   {QStringLiteral("name"), QString::fromStdString(locale.name)}});
    }
    return locales;
}

QVariantMap describe(const std::optional<ext::Manifest>& found, const QString& language)
{
    static const ext::Manifest blank{};
    const ext::Manifest& manifest = found ? *found : blank;
    QStringList authors;
    for (const std::string& author : manifest.authors) {
        authors.append(QString::fromStdString(author));
    }
    QStringList points;
    for (const auto& requirement : manifest.requirements) {
        points.append(toQString(ext::nameOf(requirement.first)));
    }
    QVariantList permissions;
    for (const ext::Permission& permission : manifest.permissions) {
        permissions.append(QVariantMap{{QStringLiteral("name"), QString::fromStdString(permission.name)},
                                       {QStringLiteral("title"), permissionTitle(permission.name)},
                                       {QStringLiteral("reason"), QString::fromStdString(permission.reason)},
                                       {QStringLiteral("scope"), permissionScope(permission)}});
    }
    QVariantList dependencies;
    for (const auto& [id, dependency] : manifest.dependencies) {
        dependencies.append(QVariantMap{{QStringLiteral("id"), QString::fromStdString(id)},
                                        {QStringLiteral("range"), QString::fromStdString(dependency.version.text())},
                                        {QStringLiteral("optional"), dependency.optional}});
    }
    return {{QStringLiteral("id"), QString::fromStdString(manifest.id)},
            {QStringLiteral("version"), found ? QString::fromStdString(manifest.version.text()) : QString()},
            {QStringLiteral("name"), extensionText(manifest, "name", manifest.name, language)},
            {QStringLiteral("description"), extensionText(manifest, "description", manifest.description, language)},
            {QStringLiteral("authors"), authors},
            {QStringLiteral("license"), QString::fromStdString(manifest.license)},
            {QStringLiteral("repository"), QString::fromStdString(manifest.repository)},
            {QStringLiteral("points"), points},
            {QStringLiteral("permissions"), permissions},
            {QStringLiteral("dependencies"), dependencies},
            {QStringLiteral("hasSettings"),
             !manifest.settings.empty() ||
                 (manifest.contributes.native && !manifest.contributes.native->attachments.empty())},
            {QStringLiteral("panels"), panelList(manifest, language)},
            {QStringLiteral("services"), serviceList(manifest, language)},
            {QStringLiteral("converters"), converterList(manifest, language)},
            {QStringLiteral("locales"), localeList(manifest)}};
}

QVariantMap installResult(const ext::InstalledExtension& outcome)
{
    return {{QStringLiteral("ok"), outcome.ok()},
            {QStringLiteral("id"), outcome.manifest ? QString::fromStdString(outcome.manifest->id) : QString()},
            {QStringLiteral("version"),
             outcome.manifest ? QString::fromStdString(outcome.manifest->version.text()) : QString()},
            {QStringLiteral("problems"), problemList(outcome.diagnostics)}};
}

constexpr qsizetype kMostShownFunctions = 8;

QString capabilityTitle(ext::Capability capability)
{
    switch (capability) {
    case ext::Capability::Network:
        return ExtensionHost::tr("Uses the network");
    case ext::Capability::Files:
        return ExtensionHost::tr("Reads and writes files");
    case ext::Capability::Input:
        return ExtensionHost::tr("Listens to the keyboard and mouse");
    case ext::Capability::Processes:
        return ExtensionHost::tr("Starts other programs");
    case ext::Capability::Injection:
        return ExtensionHost::tr("Reaches into other programs");
    }
    return {};
}

QString mismatchText(ext::Capability capability)
{
    switch (capability) {
    case ext::Capability::Network:
        return ExtensionHost::tr("Its code uses the network, but the extension does not ask for the network.");
    case ext::Capability::Files:
        return ExtensionHost::tr("Its code reads and writes files, but the extension does not ask for files.");
    case ext::Capability::Input:
        return ExtensionHost::tr(
            "Its code listens to the keyboard and mouse, but the extension does not ask to read them.");
    case ext::Capability::Processes:
        return ExtensionHost::tr(
            "Its code can start other programs. No permission covers this, so install it only if you trust the author.");
    case ext::Capability::Injection:
        break;
    }
    return {};
}

QString refusalText(ext::NativeRefusal refusal)
{
    switch (refusal) {
    case ext::NativeRefusal::None:
        break;
    case ext::NativeRefusal::Injection:
        return ExtensionHost::tr("Its code can reach into other programs, which extensions may not do.");
    case ext::NativeRefusal::Threat:
        return ExtensionHost::tr("Windows Security found a threat in it.");
    case ext::NativeRefusal::Blocked:
        return ExtensionHost::tr("The security settings of this computer block it.");
    case ext::NativeRefusal::BadSignature:
        return ExtensionHost::tr("Its author signature does not match its files: it was changed after it was signed.");
    case ext::NativeRefusal::Unreadable:
        return ExtensionHost::tr("Its code cannot be read, so it cannot be checked.");
    case ext::NativeRefusal::Architecture:
        return ExtensionHost::tr("This extension has no version for this computer.");
    }
    return {};
}

QVariantMap refusedProblem(ext::NativeRefusal refusal, const std::string& detail)
{
    return hostProblem(ext::Severity::Error, toQString(ext::codeName(ext::Code::NativeRefused)),
                       QString::fromStdString(detail), refusalText(refusal));
}

QVariantMap securityMap(const ext::NativeCheck& check, const std::optional<ext::SignatureCheck>& before)
{
    constexpr std::array<std::string_view, 3> kAuthors{"unsigned", "valid", "invalid"};
    constexpr std::array<std::string_view, 5> kScans{"skipped", "clean", "unavailable", "blocked", "threat"};
    QVariantList capabilities;
    for (const ext::CapabilityUse& use : check.capabilities) {
        QStringList functions;
        for (const std::string& function : use.functions) {
            if (functions.size() == kMostShownFunctions) {
                functions.append(QStringLiteral("\u2026"));
                break;
            }
            functions.append(QString::fromStdString(function));
        }
        capabilities.append(QVariantMap{
            {QStringLiteral("name"), toQString(ext::capabilityName(use.capability))},
            {QStringLiteral("title"), capabilityTitle(use.capability)},
            {QStringLiteral("functions"), functions},
            {QStringLiteral("declared"), std::ranges::find(check.uncovered, use.capability) == check.uncovered.end()},
            {QStringLiteral("blocked"), check.restricted}});
    }
    const bool changed = before.has_value() && ext::authorChanged(*before, check.signature);
    QStringList warnings;
    if (check.scan == ext::ScanVerdict::Unavailable) {
        warnings.append(ExtensionHost::tr("It could not be scanned with Windows Security."));
    }
    for (const ext::Capability capability : check.uncovered) {
        warnings.append(mismatchText(capability));
    }
    QStringList files;
    for (const ext::NativeFile& file : check.files) {
        files.append(QString::fromStdString(file.path));
    }
    const auto keyOf = [](const std::optional<ext::MinisignKey>& key) {
        return key ? QString::fromStdString(ext::minisignKeyId(*key)) : QString();
    };
    const bool signedValid = check.signature.state == ext::SignatureState::Valid;
    return {{QStringLiteral("native"), true},
            {QStringLiteral("author"), toQString(kAuthors[static_cast<std::size_t>(check.signature.state)])},
            {QStringLiteral("keyId"), signedValid ? keyOf(check.signature.key) : QString()},
            {QStringLiteral("signatureProblem"), QString::fromStdString(check.signature.problem)},
            {QStringLiteral("keyChanged"), changed},
            {QStringLiteral("previousKeyId"), changed ? keyOf(before->key) : QString()},
            {QStringLiteral("scan"), toQString(kScans[static_cast<std::size_t>(check.scan)])},
            {QStringLiteral("files"), files},
            {QStringLiteral("capabilities"), capabilities},
            {QStringLiteral("warnings"), warnings},
            {QStringLiteral("refused"), check.refused()},
            {QStringLiteral("reason"), refusalText(check.refusal)},
            {QStringLiteral("detail"), QString::fromStdString(check.detail)},
            {QStringLiteral("restricted"), check.restricted}};
}

QString authorsOf(const ext::Manifest& manifest, const QString& language)
{
    QStringList authors;
    for (const std::string& author : manifest.authors) {
        authors.append(QString::fromStdString(author));
    }
    return authors.isEmpty() ? extensionText(manifest, "name", manifest.name, language)
                             : authors.join(ExtensionHost::tr(", ", "list separator"));
}

QVariantMap publisherMap(const ext::SignatureCheck& signature, const std::optional<ext::MinisignKey>& remembered,
                         const QString& author)
{
    const bool valid = signature.state == ext::SignatureState::Valid && signature.key.has_value();
    const bool same = valid && (!remembered || *remembered == *signature.key);
    const QString keyId = valid ? QString::fromStdString(ext::minisignKeyId(*signature.key)) : QString();
    QString state;
    QString text;
    if (signature.state == ext::SignatureState::Unsigned) {
        state = QStringLiteral("unsigned");
        text = ExtensionHost::tr("Unsigned");
    } else if (same) {
        state = QStringLiteral("verified");
        text = ExtensionHost::tr("Verified publisher: %1 · %2").arg(author, keyId);
    } else if (valid) {
        state = QStringLiteral("changed");
        text = ExtensionHost::tr("Signed with a different key than before");
    } else {
        state = QStringLiteral("invalid");
        text = ExtensionHost::tr("Its signature does not match its files");
    }
    return {{QStringLiteral("state"), state},
            {QStringLiteral("keyId"), keyId},
            {QStringLiteral("previousKeyId"),
             remembered && !same ? QString::fromStdString(ext::minisignKeyId(*remembered)) : QString()},
            {QStringLiteral("text"), text}};
}

fs::path profileFolderOf(const fs::path& directory, const std::string& file)
{
    const fs::path relative = pathOf(file);
    if (relative.filename() != "profile.json" || !relative.has_parent_path()) {
        return {};
    }
    return directory / relative.parent_path();
}

}

QString extensionSettingPath(std::string_view id, std::string_view key)
{
    return QStringLiteral("ext:") + QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size())) + u'/' +
           QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size()));
}

QString extensionText(const ext::Manifest& manifest, std::string_view key, std::string_view fallback,
                      const QString& language)
{
    const std::string full = language.toStdString();
    const std::string base = full.substr(0, full.find('_'));
    for (const std::string& candidate : {full, base}) {
        const auto texts = manifest.l10n.find(candidate);
        if (texts == manifest.l10n.end()) {
            continue;
        }
        const auto text = texts->second.find(key);
        if (text != texts->second.end()) {
            return QString::fromStdString(text->second);
        }
    }
    return QString::fromUtf8(fallback.data(), static_cast<qsizetype>(fallback.size()));
}

QVariant extensionVariant(const ext::SettingDeclaration& setting, const ext::SettingValue& value)
{
    switch (setting.type) {
    case ext::SettingType::Boolean:
        if (const auto* flag = std::get_if<bool>(&value)) {
            return *flag;
        }
        break;
    case ext::SettingType::Integer:
        if (const auto* whole = std::get_if<std::int64_t>(&value)) {
            return QVariant::fromValue(static_cast<qint64>(*whole));
        }
        break;
    case ext::SettingType::Number:
        if (std::holds_alternative<std::int64_t>(value) || std::holds_alternative<double>(value)) {
            return *numberOf(value);
        }
        break;
    case ext::SettingType::Choice:
    case ext::SettingType::Text:
        if (const auto* text = std::get_if<std::string>(&value)) {
            return QString::fromStdString(*text);
        }
        break;
    }
    return {};
}

ExtensionHost::ExtensionHost(AppSettings& settings, ExtensionRoots roots, QObject* parent)
    : QObject(parent), m_settings(settings), m_roots(std::move(roots))
{
    Q_ASSERT(s_instance == nullptr);
    s_instance = this;
    m_scanner = scanWithAmsi;
    connect(&m_settings, &AppSettings::languageChanged, this, &ExtensionHost::extensionsChanged);
    connect(&m_settings, &AppSettings::languageChanged, this, &ExtensionHost::bisectChanged);
    fs::path program = m_roots.hostProgram;
    if (program.empty() && QCoreApplication::instance() != nullptr) {
        program = (QCoreApplication::applicationDirPath() + QStringLiteral("/gs-exthost.exe")).toStdWString();
    }
    const fs::path crashRoot = m_roots.data.empty() ? fs::path() : m_roots.data / "crashes" / "extensions";
    m_health = std::make_unique<ExtensionHealth>(m_roots.data.empty() ? fs::path()
                                                                     : m_roots.data / "extension-health.json");
    m_native = std::make_unique<NativeExtensions>(
        NativeOptions{.program = program,
                      .storeRoot = m_roots.user.empty() ? fs::path() : m_roots.user / ".store",
                      .clock = nullptr,
                      .crashRoot = crashRoot,
                      .logFile = m_roots.log});
    connect(m_native.get(), &NativeExtensions::changed, this, &ExtensionHost::nativeChanged);
    connect(this, &ExtensionHost::valuesChanged, this, &ExtensionHost::settleNative);
    connect(m_native.get(), &NativeExtensions::globalsChanged, this, [this] {
        ++m_addOnsRevision;
        emit addOnsChanged();
    });
    const auto note = [this, crashRoot](const std::string& id, HealthKind kind, std::uint32_t code) {
        HealthEvent event{.at = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()),
                          .version = {},
                          .kind = kind,
                          .code = code,
                          .report = {}};
        for (const Extension& extension : m_extensions) {
            if (extension.active && extension.manifest && extension.manifest->id == id) {
                event.version = extension.manifest->version.text();
            }
        }
        if (kind == HealthKind::Crashed && !crashRoot.empty()) {
            if (const std::optional<fs::path> report = newestFolder(crashRoot / pathOf(id), std::chrono::minutes(2))) {
                event.report = QString::fromStdWString(report->filename().wstring()).toStdString();
            }
        }
        m_health->record(id, std::move(event));
    };
    connect(m_native.get(), &NativeExtensions::crashed, this, [this, note](const QString& id, quint32 code) {
        note(id.toStdString(), HealthKind::Crashed, code);
        emit extensionsChanged();
    });
    connect(m_native.get(), &NativeExtensions::stopped, this, [this, note](const QString& id, const QString& name) {
        note(id.toStdString(), HealthKind::Stopped, 0);
        if (!setEnabled(id.toStdString(), false)) {
            resolve();
        }
        emit extensionsChanged();
        emit nativeStopped(name);
    });
    connect(m_native.get(), &NativeExtensions::modulesFailed, this, [this, note](const QString& id, const QString& name) {
        note(id.toStdString(), HealthKind::ModulesFailed, 0);
        if (!setEnabled(id.toStdString(), false)) {
            resolve();
        }
        emit extensionsChanged();
        emit modulesFailed(name);
    });
}

TouchMethodSource* ExtensionHost::touchSource() const noexcept
{
    return m_native.get();
}

QVariantList ExtensionHost::touchMethodList() const
{
    return m_native->touchMethods();
}

NativeWanted ExtensionHost::nativeWanted(const Extension& extension) const
{
    const ext::Manifest& manifest = *extension.manifest;
    const QString language = m_settings.interfaceLanguage();
    NativeWanted wanted;
    wanted.id = manifest.id;
    wanted.version = manifest.version.text();
    wanted.name = extensionText(manifest, "name", manifest.name, language);
    wanted.directory = extension.directory;
    wanted.official = extension.builtin && manifest.id.starts_with(ext::kReservedIdPrefix);
    wanted.native = *manifest.contributes.native;
    wanted.settings = settingsSnapshot(manifest);
    const settings::ExtensionStates& states = m_settings.extensions();
    if (const auto state = states.find(manifest.id); state != states.end()) {
        wanted.granted = state->second.granted;
    }
    for (const ext::NativeTouchMethod& method : wanted.native.touchMethods) {
        wanted.methodTitles.push_back(extensionText(manifest, "touchMethods." + method.name + ".title", method.title,
                                                    language));
        wanted.methodDescriptions.push_back(
            extensionText(manifest, "touchMethods." + method.name + ".description", method.description, language));
    }
    for (const ext::NativeNode& node : wanted.native.nodes) {
        wanted.nodeDescriptions.push_back(
            extensionText(manifest, "nodes." + node.name + ".description", node.description, language));
    }
    const auto textsOf = [&manifest, &language](const std::string& prefix, const std::string& title,
                                                const std::string& description,
                                                const std::vector<ext::DeclaredParam>& params,
                                                const std::vector<ext::DeclaredSlot>& declaredSlots) {
        NativeTexts texts;
        texts.title = extensionText(manifest, prefix + "title", title, language);
        texts.description = extensionText(manifest, prefix + "description", description, language);
        for (const ext::DeclaredParam& param : params) {
            const std::string at = prefix + "params." + param.key + ".";
            texts.paramTitles.push_back(extensionText(manifest, at + "title", param.title, language));
            texts.paramDescriptions.push_back(extensionText(manifest, at + "description", param.description, language));
            std::vector<QString> options;
            for (const std::string& option : param.options) {
                options.push_back(extensionText(manifest, at + "options." + option, option, language));
            }
            texts.paramOptions.push_back(std::move(options));
        }
        for (const ext::DeclaredSlot& slot : declaredSlots) {
            texts.slotTitles.push_back(extensionText(manifest, prefix + "slots." + slot.name + ".title",
                                                     slot.title.empty() ? slot.name : slot.title, language));
        }
        return texts;
    };
    for (const ext::NativeAttachment& attachment : wanted.native.attachments) {
        wanted.attachmentTexts.push_back(textsOf("attachments." + attachment.name + ".", attachment.title,
                                                 attachment.description, attachment.params, {}));
    }
    for (const ext::NativeBehavior& behavior : wanted.native.behaviors) {
        wanted.behaviorTexts.push_back(textsOf("behaviors." + behavior.name + ".", behavior.title, behavior.description,
                                               behavior.params, behavior.slotList));
    }
    for (const ext::NativeInputSource& source : wanted.native.inputSources) {
        const std::string prefix = "inputSources." + source.name + ".";
        NativeTexts texts;
        texts.title = extensionText(manifest, prefix + "title", source.title, language);
        for (const ext::NativeInputSlot& slot : source.slotList) {
            texts.slotTitles.push_back(extensionText(manifest, prefix + "slots." + slot.name + ".title",
                                                     slot.title.empty() ? slot.name : slot.title, language));
        }
        wanted.inputTexts.push_back(std::move(texts));
    }
    for (const ext::NativeTransport& transport : wanted.native.transports) {
        wanted.transportTitles.push_back(
            extensionText(manifest, "transports." + transport.name + ".title", transport.title, language));
    }
    for (const ext::NativeVideoSource& source : wanted.native.videoSources) {
        wanted.videoSourceTitles.push_back(
            extensionText(manifest, "videoSources." + source.name + ".title", source.title, language));
    }
    for (const ext::NativeAudioSource& source : wanted.native.audioSources) {
        wanted.audioSourceTitles.push_back(
            extensionText(manifest, "audioSources." + source.name + ".title", source.title, language));
    }
    for (const ext::NativeMediaSink& sink : wanted.native.mediaSinks) {
        wanted.sinkTitles.push_back(extensionText(manifest, "mediaSinks." + sink.name + ".title", sink.title, language));
    }
    for (const ext::NativeRecordingExporter& exporter : wanted.native.recordingExporters) {
        wanted.exporterTitles.push_back(
            extensionText(manifest, "recordingExporters." + exporter.name + ".title", exporter.title, language));
    }
    for (const ext::NativeFrameTap& tap : wanted.native.frameTaps) {
        wanted.tapTitles.push_back(extensionText(manifest, "frameTaps." + tap.name + ".title", tap.title, language));
    }
    return wanted;
}

QVariantList ExtensionHost::nativeAttachments() const
{
    return m_native->attachmentList();
}

const ext::NativeAttachment* ExtensionHost::declaredAttachment(std::string_view id, std::string_view name) const
{
    const ext::Manifest* manifest = manifestOf(id);
    if (manifest == nullptr || !manifest->contributes.native) {
        return nullptr;
    }
    const std::vector<ext::NativeAttachment>& attachments = manifest->contributes.native->attachments;
    const auto found = std::ranges::find(attachments, name, &ext::NativeAttachment::name);
    return found != attachments.end() ? &*found : nullptr;
}

QVariantList ExtensionHost::globalAddOns(const QString& id) const
{
    const std::string wanted = id.toStdString();
    const ext::Manifest* manifest = manifestOf(wanted);
    if (manifest == nullptr || !manifest->contributes.native) {
        return {};
    }
    const settings::ExtensionStates& states = m_settings.extensions();
    const auto state = states.find(wanted);
    QVariantList list;
    for (const ext::NativeAttachment& attachment : manifest->contributes.native->attachments) {
        const std::optional<DeclaredModule> declared = m_native->declaredModule(wanted + "/" + attachment.name);
        if (!declared) {
            continue;
        }
        const settings::GlobalAddOn* chosen = nullptr;
        if (state != states.end()) {
            if (const auto found = state->second.addOns.find(attachment.name); found != state->second.addOns.end()) {
                chosen = &found->second;
            }
        }
        QStringList fits;
        for (const std::string& type : attachment.appliesTo) {
            if (const QString name = ProfileEditor::nameFor(QString::fromStdString(type)); !name.isEmpty()) {
                fits.append(name);
            }
        }
        const bool everywhere = attachment.appliesTo.empty() || std::ranges::find(attachment.appliesTo, "*") !=
                                                                    attachment.appliesTo.end();
        list.append(QVariantMap{
            {QStringLiteral("name"), QString::fromStdString(attachment.name)},
            {QStringLiteral("title"), declared->texts.title},
            {QStringLiteral("description"), declared->texts.description},
            {QStringLiteral("fits"), everywhere ? tr("Every kind of key") : fits.join(tr(", ", "list separator"))},
            {QStringLiteral("on"), chosen != nullptr && chosen->on},
            {QStringLiteral("params"), declaredParamMaps(*declared, chosen != nullptr ? chosen->params : std::string())}});
    }
    return list;
}

bool ExtensionHost::setGlobalAddOn(const QString& id, const QString& name, bool on)
{
    const std::string extension = id.toStdString();
    const std::string attachment = name.toStdString();
    if (declaredAttachment(extension, attachment) == nullptr) {
        return false;
    }
    settings::ExtensionStates states = m_settings.extensions();
    settings::GlobalAddOn& chosen = states[extension].addOns[attachment];
    if (chosen.on == on) {
        return true;
    }
    chosen.on = on;
    m_settings.setExtensions(states);
    pushGlobals();
    return true;
}

bool ExtensionHost::setGlobalAddOnParam(const QString& id, const QString& name, const QString& key,
                                        const QString& json)
{
    const std::string extension = id.toStdString();
    const std::string attachment = name.toStdString();
    const std::string field = key.toStdString();
    const ext::NativeAttachment* declared = declaredAttachment(extension, attachment);
    if (declared == nullptr) {
        return false;
    }
    const auto param = std::ranges::find(declared->params, field, &ext::DeclaredParam::key);
    const nlohmann::json value = nlohmann::json::parse(json.toStdString(), nullptr, false);
    if (param == declared->params.end() || value.is_discarded() || value.is_null() ||
        !ext::declaredProblem(*param, value.dump()).empty()) {
        return false;
    }
    settings::ExtensionStates states = m_settings.extensions();
    settings::GlobalAddOn& chosen = states[extension].addOns[attachment];
    nlohmann::json params = chosen.params.empty() ? nlohmann::json::object()
                                                  : nlohmann::json::parse(chosen.params, nullptr, false);
    if (!params.is_object()) {
        params = nlohmann::json::object();
    }
    params[field] = value;
    std::string text = params.dump();
    if (text.size() > settings::GlobalAddOn::kMaxParamsLength) {
        return false;
    }
    if (text == chosen.params) {
        return true;
    }
    chosen.params = std::move(text);
    m_settings.setExtensions(states);
    pushGlobals();
    return true;
}

void ExtensionHost::pushGlobals()
{
    NativeExtensions::GlobalChoices choices;
    for (const auto& [id, state] : m_settings.extensions()) {
        for (const auto& [name, chosen] : state.addOns) {
            if (chosen.on) {
                choices[id][name] = chosen.params;
            }
        }
    }
    m_native->setGlobals(std::move(choices));
}

QVariantList ExtensionHost::nativeBehaviors() const
{
    return m_native->behaviorList();
}

ExtensionHost::~ExtensionHost()
{
    s_instance = nullptr;
}

ExtensionHost* ExtensionHost::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void ExtensionHost::setScanner(ext::NativeScanner scanner)
{
    m_scanner = std::move(scanner);
}

std::vector<const ext::Manifest*> ExtensionHost::settingsManifests() const
{
    std::vector<const ext::Manifest*> manifests;
    for (const Extension& extension : m_extensions) {
        if (!extension.usable() || extension.manifest->settings.empty()) {
            continue;
        }
        const ext::Manifest* chosen = manifestOf(extension.manifest->id);
        if (chosen != nullptr && std::ranges::find(manifests, chosen) == manifests.end()) {
            manifests.push_back(chosen);
        }
    }
    return manifests;
}

QVariantMap ExtensionHost::values() const
{
    const settings::ExtensionStates& states = m_settings.extensions();
    QVariantMap values;
    for (const ext::Manifest* manifest : settingsManifests()) {
        const auto state = states.find(manifest->id);
        for (const ext::SettingDeclaration& setting : manifest->settings) {
            const settings::Scalar* stored = state != states.end() ? scalarOf(state->second.values, setting.key) : nullptr;
            const bool valid = stored != nullptr && ext::settingAccepts(setting, settingValueOf(*stored));
            values.insert(extensionSettingPath(manifest->id, setting.key),
                          extensionVariant(setting, valid ? settingValueOf(*stored) : setting.defaultValue));
        }
    }
    return values;
}

QVariantMap ExtensionHost::overrides() const
{
    QVariantMap byDevice;
    for (const auto& [deviceId, device] : m_settings.devices()) {
        QVariantMap values;
        for (const auto& [id, extension] : device.extensions) {
            for (const auto& [key, stored] : extension.values) {
                const ext::SettingDeclaration* setting = declarationOf(id, key);
                if (setting != nullptr && setting->scope == ext::SettingScope::Device &&
                    ext::settingAccepts(*setting, settingValueOf(stored))) {
                    values.insert(extensionSettingPath(id, key), extensionVariant(*setting, settingValueOf(stored)));
                }
            }
        }
        if (!values.isEmpty()) {
            byDevice.insert(QString::fromStdString(deviceId), values);
        }
    }
    return byDevice;
}

std::string ExtensionHost::settingsSnapshot(const ext::Manifest& manifest) const
{
    const settings::ExtensionStates& states = m_settings.extensions();
    const auto state = states.find(manifest.id);
    nlohmann::json global = nlohmann::json::object();
    for (const ext::SettingDeclaration& setting : manifest.settings) {
        const settings::Scalar* stored = state != states.end() ? scalarOf(state->second.values, setting.key) : nullptr;
        const bool valid = stored != nullptr && ext::settingAccepts(setting, settingValueOf(*stored));
        global[setting.key] = settingJson(valid ? settingValueOf(*stored) : setting.defaultValue);
    }
    nlohmann::json devices = nlohmann::json::object();
    for (const auto& [deviceId, device] : m_settings.devices()) {
        const auto found = device.extensions.find(manifest.id);
        if (found == device.extensions.end()) {
            continue;
        }
        nlohmann::json own = nlohmann::json::object();
        for (const auto& [key, stored] : found->second.values) {
            const auto setting = std::ranges::find(manifest.settings, key, &ext::SettingDeclaration::key);
            if (setting != manifest.settings.end() && setting->scope == ext::SettingScope::Device &&
                ext::settingAccepts(*setting, settingValueOf(stored))) {
                own[key] = settingJson(settingValueOf(stored));
            }
        }
        if (!own.empty()) {
            devices[deviceId] = std::move(own);
        }
    }
    std::string made = nlohmann::json{{"global", global}, {"devices", devices}}.dump();
    if (made.size() > exthost::kMostSettingsBytes) {
        log::warning("extensions", "{}: the per-phone settings are too large to hand to its DLL", manifest.id);
        made = nlohmann::json{{"global", global}, {"devices", nlohmann::json::object()}}.dump();
    }
    return made;
}

std::string ExtensionHost::nativeSettings(std::string_view id) const
{
    const ext::Manifest* manifest = manifestOf(id);
    return manifest != nullptr ? settingsSnapshot(*manifest) : std::string();
}

void ExtensionHost::settleNative()
{
    for (const Extension& extension : m_extensions) {
        if (extension.active && extension.usable() && extension.manifest->contributes.native) {
            m_native->setSettings(extension.manifest->id, settingsSnapshot(*extension.manifest));
        }
    }
}

QString ExtensionHost::modeName() const
{
    switch (m_mode) {
    case Mode::Normal:
        return QStringLiteral("normal");
    case Mode::SafeRequested:
        return QStringLiteral("safeRequested");
    case Mode::SafeAfterCrash:
        return QStringLiteral("safeAfterCrash");
    }
    return {};
}

QVariantList ExtensionHost::entries() const
{
    const QString language = m_settings.interfaceLanguage();
    const auto title = [&language](const Extension& extension) {
        return extension.manifest ? extensionText(*extension.manifest, "name", extension.manifest->name, language)
                                  : folderName(extension.directory);
    };
    std::vector<const Extension*> order;
    order.reserve(m_extensions.size());
    for (const Extension& extension : m_extensions) {
        order.push_back(&extension);
    }
    std::ranges::stable_sort(order, [&title](const Extension* a, const Extension* b) {
        return a->builtin != b->builtin ? a->builtin : QString::localeAwareCompare(title(*a), title(*b)) < 0;
    });
    const settings::ExtensionStates& states = m_settings.extensions();
    QVariantList rows;
    for (const Extension* extension : order) {
        QVariantMap row = describe(extension->manifest, language);
        QVariantList panels = row.value(QStringLiteral("panels")).toList();
        for (QVariant& each : panels) {
            QVariantMap panel = each.toMap();
            const QString icon = panel.value(QStringLiteral("icon")).toString();
            if (icon.startsWith(u"svg:")) {
                panel.insert(QStringLiteral("iconPath"), extensionIconPath(extension->directory, icon.mid(4).toStdString()));
                each = panel;
            }
        }
        row.insert(QStringLiteral("panels"), panels);
        std::vector<ext::Diagnostic> diagnostics = extension->diagnostics;
        if (extension->manifest) {
            const ext::Manifest& manifest = *extension->manifest;
            const auto state = states.find(manifest.id);
            if (!extension->superseded) {
                const std::vector<ext::Diagnostic> shared = problemsOf(manifest.id);
                diagnostics.insert(diagnostics.end(), shared.begin(), shared.end());
            }
            row.insert(QStringLiteral("key"), extension->usable()
                                                  ? QString::fromStdString(manifest.id + '@' + manifest.version.text())
                                                  : QString::fromStdWString(extension->directory.wstring()));
            row.insert(QStringLiteral("enabled"), !extension->superseded && state != states.end() &&
                                                      state->second.enabled && consented(manifest));
            row.insert(QStringLiteral("health"), healthOf(*m_health, manifest.id));
            if (!extension->builtin) {
                const auto remembered = m_publishers.find(manifest.id);
                row.insert(QStringLiteral("publisher"),
                           publisherMap(extension->signature,
                                        remembered != m_publishers.end() ? std::optional(remembered->second)
                                                                         : std::nullopt,
                                        authorsOf(manifest, language)));
            }
            if (std::ranges::find(manifest.permissions, std::string_view("network.listen"), &ext::Permission::name) !=
                manifest.permissions.end()) {
                row.insert(QStringLiteral("localConnections"),
                           state != states.end() &&
                               std::ranges::find(state->second.granted, std::string_view("network.listen")) !=
                                   state->second.granted.end());
            }
            if (manifest.contributes.native) {
                QStringList methods;
                for (const ext::NativeTouchMethod& method : manifest.contributes.native->touchMethods) {
                    methods.append(
                        extensionText(manifest, "touchMethods." + method.name + ".title", method.title, language));
                }
                QStringList nodes;
                for (const ext::NativeNode& node : manifest.contributes.native->nodes) {
                    nodes.append(QString::fromStdString(script::nodePath(manifest.id, node.name)));
                }
                QStringList attachments;
                for (const ext::NativeAttachment& attachment : manifest.contributes.native->attachments) {
                    attachments.append(extensionText(manifest, "attachments." + attachment.name + ".title",
                                                     attachment.title, language));
                }
                QStringList behaviors;
                for (const ext::NativeBehavior& behavior : manifest.contributes.native->behaviors) {
                    behaviors.append(
                        extensionText(manifest, "behaviors." + behavior.name + ".title", behavior.title, language));
                }
                QStringList speechEngines;
                for (const ext::NativeSpeechEngine& engine : manifest.contributes.native->speechEngines) {
                    speechEngines.append(
                        extensionText(manifest, "speechEngines." + engine.name + ".title", engine.title, language));
                }
                QStringList inputSources;
                for (const ext::NativeInputSource& source : manifest.contributes.native->inputSources) {
                    inputSources.append(
                        extensionText(manifest, "inputSources." + source.name + ".title", source.title, language));
                }
                QStringList architectures;
                for (const auto& listed : ext::nativeDlls(*manifest.contributes.native)) {
                    if (!listed.first.empty()) {
                        architectures.append(QString::fromStdString(listed.first));
                    }
                }
                QStringList transports;
                for (const ext::NativeTransport& transport : manifest.contributes.native->transports) {
                    transports.append(
                        extensionText(manifest, "transports." + transport.name + ".title", transport.title, language));
                }
                QStringList taps;
                for (const ext::NativeFrameTap& tap : manifest.contributes.native->frameTaps) {
                    taps.append(extensionText(manifest, "frameTaps." + tap.name + ".title", tap.title, language));
                }
                QStringList aiProviders;
                for (const ext::NativeAiProvider& provider : manifest.contributes.native->aiProviders) {
                    aiProviders.append(
                        extensionText(manifest, "aiProviders." + provider.name + ".title", provider.title, language));
                }
                QStringList visionEngines;
                for (const ext::NativeVisionEngine& engine : manifest.contributes.native->visionEngines) {
                    visionEngines.append(
                        extensionText(manifest, "visionEngines." + engine.name + ".title", engine.title, language));
                }
                row.insert(QStringLiteral("native"),
                           QVariantMap{{QStringLiteral("touchMethods"), methods},
                                       {QStringLiteral("nodes"), nodes},
                                       {QStringLiteral("attachments"), attachments},
                                       {QStringLiteral("behaviors"), behaviors},
                                       {QStringLiteral("speechEngines"), speechEngines},
                                       {QStringLiteral("inputSources"), inputSources},
                                       {QStringLiteral("transports"), transports},
                                       {QStringLiteral("frameTaps"), taps},
                                       {QStringLiteral("aiProviders"), aiProviders},
                                       {QStringLiteral("visionEngines"), visionEngines},
                                       {QStringLiteral("ownProcess"),
                                        !(extension->builtin && manifest.id.starts_with(ext::kReservedIdPrefix)) ||
                                            manifest.contributes.native->sandbox == ext::NativeSandbox::Restricted},
                                       {QStringLiteral("restricted"),
                                        manifest.contributes.native->sandbox == ext::NativeSandbox::Restricted},
                                       {QStringLiteral("architectures"), architectures}});
                const ext::NativeContribution& media = *manifest.contributes.native;
                QStringList sources;
                for (const ext::NativeVideoSource& source : media.videoSources) {
                    sources.append(extensionText(manifest, "videoSources." + source.name + ".title", source.title,
                                                 language));
                }
                for (const ext::NativeAudioSource& source : media.audioSources) {
                    sources.append(extensionText(manifest, "audioSources." + source.name + ".title", source.title,
                                                 language));
                }
                QStringList sinks;
                for (const ext::NativeMediaSink& sink : media.mediaSinks) {
                    sinks.append(extensionText(manifest, "mediaSinks." + sink.name + ".title", sink.title, language));
                }
                QStringList exporters;
                for (const ext::NativeRecordingExporter& exporter : media.recordingExporters) {
                    exporters.append(extensionText(manifest, "recordingExporters." + exporter.name + ".title",
                                                   exporter.title, language));
                }
                QVariantMap dll = row.value(QStringLiteral("native")).toMap();
                dll.insert(QStringLiteral("sources"), sources);
                dll.insert(QStringLiteral("sinks"), sinks);
                dll.insert(QStringLiteral("exporters"), exporters);
                row.insert(QStringLiteral("native"), dll);
            }
        } else {
            const QString id = QString::fromStdWString(extension->directory.parent_path().filename().wstring());
            row.insert(QStringLiteral("key"), QString::fromStdWString(extension->directory.wstring()));
            row.insert(QStringLiteral("id"), id);
            row.insert(QStringLiteral("version"), QString::fromStdWString(extension->directory.filename().wstring()));
            row.insert(QStringLiteral("name"), id);
            row.insert(QStringLiteral("enabled"), false);
        }
        row.insert(QStringLiteral("builtin"), extension->builtin);
        row.insert(QStringLiteral("active"), extension->active);
        row.insert(QStringLiteral("usable"), extension->usable());
        row.insert(QStringLiteral("superseded"), extension->superseded);
        row.insert(QStringLiteral("folder"), extension->builtin ? QString() : folderName(extension->directory));
        QVariantList problems = problemList(diagnostics);
        if (extension->superseded) {
            problems.prepend(supersededProblem(extension->manifest->version));
        }
        row.insert(QStringLiteral("problems"), problems);
        rows.append(row);
    }
    return rows;
}

void ExtensionHost::clearHealth(const QString& id)
{
    m_health->clear(id.toStdString());
    emit extensionsChanged();
}

bool ExtensionHost::localConnections(std::string_view id) const
{
    constexpr std::string_view kListen = "network.listen";
    const auto extension = std::ranges::find_if(m_extensions, [id](const Extension& each) {
        return each.active && each.manifest && each.manifest->id == id;
    });
    if (extension == m_extensions.end() ||
        std::ranges::find(extension->manifest->permissions, kListen, &ext::Permission::name) ==
            extension->manifest->permissions.end()) {
        return false;
    }
    const settings::ExtensionStates& states = m_settings.extensions();
    const auto state = states.find(std::string(id));
    return state != states.end() && std::ranges::find(state->second.granted, kListen) != state->second.granted.end();
}

bool ExtensionHost::setLocalConnections(const QString& id, bool on)
{
    constexpr std::string_view kListen = "network.listen";
    const std::string key = id.toStdString();
    const auto extension = std::ranges::find_if(m_extensions, [&key](const Extension& each) {
        return each.usable() && !each.superseded && each.manifest->id == key;
    });
    if (extension == m_extensions.end() ||
        std::ranges::find(extension->manifest->permissions, kListen, &ext::Permission::name) ==
            extension->manifest->permissions.end()) {
        return false;
    }
    settings::ExtensionStates states = m_settings.extensions();
    const auto state = states.find(key);
    if (state == states.end() || !state->second.enabled) {
        return false;
    }
    std::vector<std::string>& granted = state->second.granted;
    const bool has = std::ranges::find(granted, kListen) != granted.end();
    if (has == on) {
        return true;
    }
    if (on) {
        granted.emplace_back(kListen);
    } else {
        std::erase(granted, std::string(kListen));
    }
    m_settings.setExtensions(states);
    emit extensionsChanged();
    return true;
}

QVariantList ExtensionHost::tours() const
{
    const QString language = m_settings.interfaceLanguage();
    QVariantList list;
    for (const Extension& extension : m_extensions) {
        if (!extension.active || !extension.manifest) {
            continue;
        }
        const ext::Manifest& manifest = *extension.manifest;
        const QString name = extensionText(manifest, "name", manifest.name, language);
        for (const ext::TourContribution& tour : manifest.contributes.tours) {
            const std::string base = "tours." + tour.id;
            QVariantList steps;
            for (std::size_t index = 0; index < tour.steps.size(); ++index) {
                const ext::TourStep& step = tour.steps[index];
                const std::string at = base + "." + std::to_string(index);
                steps.append(QVariantMap{{QStringLiteral("target"), QString::fromStdString(step.target)},
                                         {QStringLiteral("title"), extensionText(manifest, at + ".title", step.title, language)},
                                         {QStringLiteral("text"), extensionText(manifest, at + ".text", step.text, language)}});
            }
            list.append(QVariantMap{
                {QStringLiteral("key"), QString::fromStdString("ext/" + manifest.id + "/" + tour.id)},
                {QStringLiteral("extension"), QString::fromStdString(manifest.id)},
                {QStringLiteral("extensionName"), name},
                {QStringLiteral("id"), QString::fromStdString(tour.id)},
                {QStringLiteral("title"), extensionText(manifest, base + ".title", tour.title, language)},
                {QStringLiteral("start"), toQString(ext::kTourStartNames[static_cast<std::size_t>(tour.start)])},
                {QStringLiteral("steps"), steps}});
        }
    }
    return list;
}

std::string ExtensionHost::diagnosticsText() const
{
    const settings::ExtensionStates& states = m_settings.extensions();
    nlohmann::json list = nlohmann::json::array();
    for (const Extension& extension : m_extensions) {
        if (!extension.manifest) {
            list.push_back({{"folder", folderName(extension.directory).toStdString()}, {"usable", false}});
            continue;
        }
        const ext::Manifest& manifest = *extension.manifest;
        const auto state = states.find(manifest.id);
        list.push_back({{"id", manifest.id},
                        {"version", manifest.version.text()},
                        {"builtin", extension.builtin},
                        {"enabled", state != states.end() && state->second.enabled},
                        {"active", extension.active},
                        {"native", manifest.contributes.native.has_value()},
                        {"granted", state != states.end() ? state->second.granted : std::vector<std::string>{}}});
    }
    const nlohmann::json health = nlohmann::json::parse(m_health->text(), nullptr, false);
    return nlohmann::json{{"extensions", std::move(list)},
                          {"health", health.is_object() && health.contains("extensions") ? health.at("extensions")
                                                                                          : nlohmann::json::object()}}
        .dump(2);
}

std::vector<fs::path> ExtensionHost::crashFolders() const
{
    std::vector<fs::path> found;
    if (m_roots.data.empty()) {
        return found;
    }
    std::error_code error;
    for (fs::directory_iterator it(m_roots.data / "crashes" / "extensions", error), end; !error && it != end;
         it.increment(error)) {
        std::error_code ignored;
        if (!it->is_directory(ignored)) {
            continue;
        }
        if (std::optional<fs::path> newest = newestFolder(it->path(), std::nullopt)) {
            found.push_back(std::move(*newest));
        }
    }
    return found;
}

QVariantList ExtensionHost::themes() const
{
    const QString language = m_settings.interfaceLanguage();
    const auto colors = [](const ext::TextMap& tokens) {
        QVariantMap map;
        for (const auto& [token, color] : tokens) {
            map.insert(QString::fromStdString(token), QString::fromStdString(color));
        }
        return map;
    };
    QVariantList themes;
    for (const Extension& extension : m_extensions) {
        if (!extension.active) {
            continue;
        }
        const ext::Manifest& manifest = *extension.manifest;
        for (const ext::ThemeContribution& theme : manifest.contributes.themes) {
            themes.append(QVariantMap{
                {QStringLiteral("key"), QString::fromStdString(manifest.id + '/' + theme.name)},
                {QStringLiteral("name"), extensionText(manifest, "themes." + theme.name + ".name", theme.name, language)},
                {QStringLiteral("extension"), extensionText(manifest, "name", manifest.name, language)},
                {QStringLiteral("dark"), colors(theme.dark)},
                {QStringLiteral("light"), colors(theme.light)}});
        }
    }
    return themes;
}

bool ExtensionHost::setValue(const QString& path, const QVariant& value)
{
    const std::optional<SettingTarget> target = targetOf(path);
    const ext::SettingDeclaration* setting = target ? declarationOf(target->id, target->key) : nullptr;
    const std::optional<settings::Scalar> scalar = setting != nullptr ? scalarFrom(*setting, value) : std::nullopt;
    return scalar && setValue(target->id, target->key, *scalar);
}

bool ExtensionHost::setDeviceValue(const QString& device, const QString& path, const QVariant& value)
{
    const std::optional<SettingTarget> target = targetOf(path);
    const ext::SettingDeclaration* setting = target ? declarationOf(target->id, target->key) : nullptr;
    const std::optional<settings::Scalar> scalar = setting != nullptr ? scalarFrom(*setting, value) : std::nullopt;
    return scalar && setDeviceValue(device.toStdString(), target->id, target->key, *scalar);
}

void ExtensionHost::resetDeviceValue(const QString& device, const QString& path)
{
    if (const std::optional<SettingTarget> target = targetOf(path)) {
        resetDeviceValue(device.toStdString(), target->id, target->key);
    }
}

bool ExtensionHost::enable(const QString& id, bool on)
{
    return setEnabled(id.toStdString(), on);
}

bool ExtensionHost::remove(const QString& id, const QString& version)
{
    const std::optional<ext::Version> parsed = ext::Version::parse(version.toStdString());
    return parsed && uninstall(id.toStdString(), *parsed);
}

bool ExtensionHost::removeFolder(const QString& folder)
{
    const std::optional<std::pair<fs::path, fs::path>> parts = folderParts(folder);
    if (m_installing || !parts) {
        return false;
    }
    const auto found = std::ranges::find_if(m_extensions, [&](const Extension& extension) {
        return !extension.builtin && !extension.usable() && extension.directory.filename() == parts->second &&
               extension.directory.parent_path().filename() == parts->first;
    });
    if (found == m_extensions.end()) {
        return false;
    }
    const std::string wantedId =
        found->manifest ? found->manifest->id
                        : QString::fromStdWString(found->directory.parent_path().filename().wstring()).toStdString();
    const bool others = std::ranges::any_of(m_extensions, [&](const Extension& extension) {
        return &extension != &*found &&
               (extension.manifest ? extension.manifest->id == wantedId
                                   : extension.directory.parent_path().filename() == pathOf(wantedId));
    });
    const fs::path directory = found->directory;
    std::error_code error;
    fs::remove_all(directory, error);
    const bool removed = !error;
    if (removed) {
        fs::remove(directory.parent_path(), error);
        if (!others) {
            forget(wantedId);
        }
    }
    reload();
    return removed;
}

bool ExtensionHost::isLocalFile(const QString& fileOrUrl) const
{
    return localPath(fileOrUrl).has_value();
}

QVariantMap ExtensionHost::inspect(const QString& fileOrUrl) const
{
    const QString language = m_settings.interfaceLanguage();
    const std::optional<fs::path> package = localPath(fileOrUrl);
    if (!package) {
        QVariantMap refused = describe(std::nullopt, language);
        refused.insert(QStringLiteral("ok"), false);
        refused.insert(QStringLiteral("problems"), QVariantList{notLocalProblem(fileOrUrl)});
        refused.insert(QStringLiteral("installedVersion"), QString());
        refused.insert(QStringLiteral("builtinVersion"), QString());
        return refused;
    }
    const ext::PackageCheck check = ext::checkPackage(*package, {.official = false});
    const auto newest = [&](bool fromBuiltin) {
        const Extension* found = nullptr;
        for (const Extension& extension : m_extensions) {
            if (check.manifest && extension.usable() && extension.builtin == fromBuiltin &&
                extension.manifest->id == check.manifest->id &&
                (found == nullptr || found->manifest->version < extension.manifest->version)) {
                found = &extension;
            }
        }
        return found != nullptr ? QString::fromStdString(found->manifest->version.text()) : QString();
    };
    const bool builtin = check.manifest && exactly(check.manifest->id, check.manifest->version, true) != nullptr;
    QVariantList problems = problemList(check.diagnostics);
    if (builtin) {
        problems.prepend(builtinProblem(check.manifest->version));
    }
    QVariantMap result = describe(check.manifest, language);
    bool refused = false;
    if (check.manifest) {
        const ext::PackageContents contents = ext::archiveContents(*package);
        std::optional<ext::NativeCheck> native;
        if (ext::hasNativeCode(contents, *check.manifest)) {
            native = ext::checkNative(contents, *check.manifest, m_scanner);
            result.insert(QStringLiteral("security"), securityMap(*native, installedSignature(check.manifest->id)));
            if (native->refused()) {
                refused = true;
                problems.prepend(refusedProblem(native->refusal, native->detail));
            }
        }
        const ext::SignatureCheck signature =
            native ? native->signature : ext::checkSignature(contents, check.manifest->authorKey);
        const std::optional<ext::MinisignKey> remembered =
            m_roots.user.empty() ? std::nullopt : ext::publisherOf(m_roots.user, check.manifest->id);
        result.insert(QStringLiteral("publisher"),
                      publisherMap(signature, remembered, authorsOf(*check.manifest, language)));
        const std::optional<ext::Diagnostic> publisher = ext::publisherProblem(remembered, signature);
        if (publisher && !(native && native->refusal == ext::NativeRefusal::BadSignature)) {
            refused = true;
            problems.prepend(problemList({*publisher}).constFirst());
        }
    }
    result.insert(QStringLiteral("ok"), check.ok() && !builtin && !refused);
    result.insert(QStringLiteral("problems"), problems);
    result.insert(QStringLiteral("installedVersion"), newest(false));
    result.insert(QStringLiteral("builtinVersion"), newest(true));
    return result;
}

void ExtensionHost::installPackage(const QString& fileOrUrl)
{
    std::optional<fs::path> package = localPath(fileOrUrl);
    if (m_installing || !package) {
        const QVariantMap problem =
            package ? busyProblem(QString::fromStdWString(package->wstring())) : notLocalProblem(fileOrUrl);
        QVariantMap refused = installResult({});
        refused.insert(QStringLiteral("problems"), QVariantList{problem});
        emit installFinished(refused);
        return;
    }
    m_installing = true;
    emit installingChanged();
    m_installer = std::jthread([this, package = std::move(*package), root = m_roots.user,
                                builtins = builtinReleases(m_extensions), scanner = m_scanner] {
        std::optional<ext::Manifest> builtin;
        if (!builtins.empty()) {
            ext::PackageCheck check = ext::checkPackage(package, {.official = false});
            if (check.manifest &&
                std::ranges::find(builtins, Release(check.manifest->id, check.manifest->version)) != builtins.end()) {
                builtin = std::move(check.manifest);
            }
        }
        ext::NativeRefusal refusal = ext::NativeRefusal::None;
        std::string detail;
        std::optional<ext::Manifest> checked;
        if (!builtin) {
            ext::PackageCheck check = ext::checkPackage(package, {.official = false});
            if (check.manifest) {
                const ext::NativeCheck native =
                    ext::checkNative(ext::archiveContents(package), *check.manifest, scanner);
                refusal = native.refusal;
                detail = native.detail;
                checked = std::move(check.manifest);
            }
        }
        const bool refused = builtin.has_value();
        const bool unsafe = refusal != ext::NativeRefusal::None;
        ext::InstalledExtension outcome = refused  ? ext::InstalledExtension{{}, std::move(builtin), {}}
                                          : unsafe ? ext::InstalledExtension{{}, std::move(checked), {}}
                                                   : ext::installPackage(package, root);
        QMetaObject::invokeMethod(
            this,
            [this, outcome = std::move(outcome), refused, refusal, detail = std::move(detail)]() mutable {
                if (refused) {
                    m_installing = false;
                    emit installingChanged();
                    QVariantMap result = installResult(outcome);
                    result.insert(QStringLiteral("ok"), false);
                    result.insert(QStringLiteral("problems"), QVariantList{builtinProblem(outcome.manifest->version)});
                    emit installFinished(result);
                    return;
                }
                if (refusal != ext::NativeRefusal::None) {
                    m_installing = false;
                    emit installingChanged();
                    QVariantMap result = installResult(outcome);
                    result.insert(QStringLiteral("ok"), false);
                    result.insert(QStringLiteral("problems"), QVariantList{refusedProblem(refusal, detail)});
                    emit installFinished(result);
                    return;
                }
                const ext::InstalledExtension done = finishInstall(std::move(outcome));
                m_installing = false;
                emit installingChanged();
                QVariantMap result = installResult(done);
                if (done.ok()) {
                    const settings::ExtensionStates& states = m_settings.extensions();
                    const auto state = states.find(done.manifest->id);
                    result.insert(QStringLiteral("consentRequired"),
                                  state != states.end() && state->second.enabled && !consented(*done.manifest));
                }
                emit installFinished(result);
            },
            Qt::QueuedConnection);
    });
}

bool ExtensionHost::exportTo(const QString& id, const QString& version, const QString& fileOrUrl) const
{
    const std::optional<ext::Version> parsed = ext::Version::parse(version.toStdString());
    const std::optional<fs::path> target = localPath(fileOrUrl);
    const std::string wanted = id.toStdString();
    const auto found = std::ranges::find_if(m_extensions, [&](const Extension& extension) {
        return parsed && extension.usable() && extension.manifest->id == wanted &&
               extension.manifest->version == *parsed;
    });
    return target && found != m_extensions.end() && !ext::hasErrors(ext::packDirectory(found->directory, *target));
}

void ExtensionHost::start(bool safeModeRequested, std::filesystem::path runMarker, std::filesystem::path bisectFile)
{
    m_runMarker = std::move(runMarker);
    m_bisectFile = std::move(bisectFile);
    m_bisect.reset();
    if (const std::optional<std::string> text =
            m_bisectFile.empty() ? std::nullopt : readText(m_bisectFile, kMaxBisectBytes)) {
        m_bisect = ExtensionBisect::load(*text);
    }
    std::error_code error;
    const bool interrupted = !m_runMarker.empty() && fs::exists(m_runMarker, error);
    if (interrupted) {
        fs::remove(m_runMarker, error);
    }
    const bool bisecting = m_bisect && m_bisect->outcome() == ExtensionBisect::Outcome::Running;
    if (interrupted && bisecting && !safeModeRequested) {
        m_bisect->answer(true);
    }
    storeBisect();
    m_mode = safeModeRequested ? Mode::SafeRequested
             : interrupted && !bisecting ? Mode::SafeAfterCrash
                                         : Mode::Normal;
    if (!m_roots.user.empty()) {
        ext::clearStaging(m_roots.user);
    }
    reload();
}

struct ExtensionHost::BundleWork {
    std::optional<ext::Bundle> bundle;
    std::vector<ext::Diagnostic> problems;
    std::vector<ext::InstalledExtension> installed;
};

QVariantMap ExtensionHost::inspectBundle(const QString& fileOrUrl) const
{
    const std::optional<fs::path> local = localPath(fileOrUrl);
    if (!local) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("name"), QString()},
                {QStringLiteral("description"), QString()},
                {QStringLiteral("extensions"), QVariantList()},
                {QStringLiteral("problems"), QVariantList{notLocalProblem(fileOrUrl)}}};
    }
    const fs::path& archive = *local;
    const QString language = m_settings.interfaceLanguage();
    const ext::BundleCheck check = ext::checkBundle(archive);
    std::vector<ext::Diagnostic> problems = check.diagnostics;
    QVariantList rows;
    QVariantList refusals;
    if (check.bundle && !m_roots.user.empty()) {
        const fs::path staging = stagingOf(m_roots.user);
        for (const ext::BundleEntry& entry : check.bundle->extensions) {
            std::optional<ext::Manifest> manifest;
            QVariantMap security;
            QVariantMap publisher;
            QString status;
            const Extension* builtin = exactly(entry.id, entry.version, true);
            if (entry.builtin || builtin != nullptr) {
                status = QStringLiteral("builtin");
                if (builtin != nullptr) {
                    manifest = builtin->manifest;
                } else {
                    problems.push_back({ext::Severity::Error, ext::Code::BundleBuiltinMissing, "$.extensions",
                                        entry.id + " " + entry.version.text()});
                }
            } else {
                const fs::path inner = staging / pathOf(entry.id + "-" + entry.version.text() + ".gsext");
                std::vector<ext::Diagnostic> extracted = ext::extractBundlePackage(archive, entry, inner);
                std::ranges::move(extracted, std::back_inserter(problems));
                std::error_code missing;
                if (fs::exists(inner, missing)) {
                    ext::PackageCheck package = ext::checkPackage(inner);
                    for (ext::Diagnostic& problem : package.diagnostics) {
                        problem.path = ext::bundlePackagePath(entry) + ": " + problem.path;
                        problems.push_back(std::move(problem));
                    }
                    if (package.manifest &&
                        (package.manifest->id != entry.id || package.manifest->version != entry.version)) {
                        problems.push_back({ext::Severity::Error, ext::Code::BundlePackageMismatch,
                                            ext::bundlePackagePath(entry),
                                            package.manifest->id + " " + package.manifest->version.text()});
                    }
                    if (package.manifest) {
                        const ext::PackageContents contents = ext::archiveContents(inner);
                        std::optional<ext::NativeCheck> native;
                        if (ext::hasNativeCode(contents, *package.manifest)) {
                            native = ext::checkNative(contents, *package.manifest, m_scanner);
                            security = securityMap(*native, installedSignature(entry.id));
                            if (native->refused()) {
                                refusals.append(refusedProblem(native->refusal,
                                                               ext::bundlePackagePath(entry) + ": " + native->detail));
                            }
                        }
                        const ext::SignatureCheck signature =
                            native ? native->signature : ext::checkSignature(contents, package.manifest->authorKey);
                        const std::optional<ext::MinisignKey> remembered = ext::publisherOf(m_roots.user, entry.id);
                        publisher = publisherMap(signature, remembered, authorsOf(*package.manifest, language));
                        std::optional<ext::Diagnostic> refusal = ext::publisherProblem(remembered, signature);
                        if (refusal && !(native && native->refusal == ext::NativeRefusal::BadSignature)) {
                            refusal->path = ext::bundlePackagePath(entry) + ": " + refusal->path;
                            refusals.append(problemList({*refusal}).constFirst());
                        }
                    }
                    manifest = std::move(package.manifest);
                }
                const bool installed = std::ranges::any_of(m_extensions, [&](const Extension& extension) {
                    return !extension.builtin && extension.manifest && extension.manifest->id == entry.id;
                });
                status = exactly(entry.id, entry.version, false) != nullptr ? QStringLiteral("same")
                         : installed                                       ? QStringLiteral("update")
                                                                           : QStringLiteral("new");
            }
            QVariantMap row = describe(manifest, language);
            row.insert(QStringLiteral("id"), QString::fromStdString(entry.id));
            row.insert(QStringLiteral("version"), QString::fromStdString(entry.version.text()));
            if (!manifest) {
                row.insert(QStringLiteral("name"), QString::fromStdString(entry.id));
            }
            row.insert(QStringLiteral("status"), status);
            row.insert(QStringLiteral("enabled"), entry.enabled);
            row.insert(QStringLiteral("values"), static_cast<int>(entry.values.size()));
            if (!security.isEmpty()) {
                row.insert(QStringLiteral("security"), security);
            }
            if (!publisher.isEmpty()) {
                row.insert(QStringLiteral("publisher"), publisher);
            }
            rows.append(row);
        }
        std::error_code error;
        fs::remove_all(staging, error);
    }
    const bool ok =
        check.bundle.has_value() && !m_roots.user.empty() && !ext::hasErrors(problems) && refusals.isEmpty();
    QVariantList listed = problemList(problems);
    listed.append(refusals);
    return {{QStringLiteral("ok"), ok},
            {QStringLiteral("name"), check.bundle ? QString::fromStdString(check.bundle->name) : QString()},
            {QStringLiteral("description"),
             check.bundle ? QString::fromStdString(check.bundle->description) : QString()},
            {QStringLiteral("extensions"), rows},
            {QStringLiteral("problems"), listed}};
}

void ExtensionHost::installBundle(const QString& fileOrUrl)
{
    std::optional<fs::path> local = localPath(fileOrUrl);
    if (!local || m_installing || m_bisect || m_roots.user.empty()) {
        const QVariantMap problem =
            local ? busyProblem(QString::fromStdWString(local->wstring())) : notLocalProblem(fileOrUrl);
        emit bundleFinished({{QStringLiteral("ok"), false}, {QStringLiteral("problems"), QVariantList{problem}}});
        return;
    }
    m_installing = true;
    emit installingChanged();
    m_installer = std::jthread([this, archive = std::move(*local), root = m_roots.user,
                                builtins = builtinReleases(m_extensions), scanner = m_scanner] {
        BundleWork work;
        ext::BundleCheck check = ext::checkBundle(archive);
        work.problems = std::move(check.diagnostics);
        if (check.ok()) {
            const fs::path staging = stagingOf(root);
            for (const ext::BundleEntry& entry : check.bundle->extensions) {
                if (entry.builtin || std::ranges::find(builtins, Release(entry.id, entry.version)) != builtins.end()) {
                    continue;
                }
                const fs::path inner = staging / pathOf(entry.id + "-" + entry.version.text() + ".gsext");
                std::vector<ext::Diagnostic> extracted = ext::extractBundlePackage(archive, entry, inner);
                if (ext::hasErrors(extracted)) {
                    std::ranges::move(extracted, std::back_inserter(work.problems));
                    continue;
                }
                const ext::PackageCheck package = ext::checkPackage(inner);
                if (!package.manifest || package.manifest->id != entry.id || package.manifest->version != entry.version) {
                    work.problems.push_back({ext::Severity::Error, ext::Code::BundlePackageMismatch,
                                             ext::bundlePackagePath(entry), entry.id});
                    continue;
                }
                const ext::NativeCheck native = ext::checkNative(ext::archiveContents(inner), *package.manifest, scanner);
                if (native.refused()) {
                    work.problems.push_back({ext::Severity::Error, ext::Code::NativeRefused,
                                             ext::bundlePackagePath(entry), native.detail});
                    continue;
                }
                work.installed.push_back(ext::installPackage(inner, root));
            }
            std::error_code error;
            fs::remove_all(staging, error);
        }
        work.bundle = std::move(check.bundle);
        QMetaObject::invokeMethod(
            this, [this, work = std::move(work)]() mutable { finishBundle(std::move(work)); }, Qt::QueuedConnection);
    });
}

void ExtensionHost::finishBundle(BundleWork work)
{
    std::vector<ext::Diagnostic> problems = std::move(work.problems);
    for (ext::InstalledExtension& outcome : work.installed) {
        if (!outcome.ok()) {
            std::ranges::move(outcome.diagnostics, std::back_inserter(problems));
            continue;
        }
        static_cast<void>(finishInstall(std::move(outcome)));
    }
    QStringList installed;
    const bool readable = work.bundle.has_value() && !ext::hasErrors(problems);
    if (readable) {
        settings::ExtensionStates states = m_settings.extensions();
        for (const ext::BundleEntry& entry : work.bundle->extensions) {
            const Extension* found = exactly(entry.id, entry.version, true);
            if (found == nullptr && !entry.builtin) {
                found = exactly(entry.id, entry.version, false);
            }
            if (found == nullptr) {
                problems.push_back({ext::Severity::Error,
                                    entry.builtin ? ext::Code::BundleBuiltinMissing : ext::Code::BundlePackageMissing,
                                    "$.extensions", entry.id + " " + entry.version.text()});
                continue;
            }
            const ext::Manifest& manifest = *found->manifest;
            settings::ExtensionState& state = states[entry.id];
            state.enabled = entry.enabled;
            state.version.clear();
            for (const auto& [key, value] : entry.values) {
                const auto declared = std::ranges::find(manifest.settings, key, &ext::SettingDeclaration::key);
                const std::optional<settings::Scalar> stored = storedOf(value);
                if (declared == manifest.settings.end() || !stored || !storable(*declared, *stored)) {
                    problems.push_back({ext::Severity::Warning, ext::Code::WrongType,
                                        memberPath(entry.id) + ".values[\"" + key + "\"]", entry.id});
                    continue;
                }
                state.values.insert_or_assign(key, *stored);
            }
            if (entry.enabled) {
                state.granted.clear();
                for (const ext::Permission& permission : manifest.permissions) {
                    state.granted.push_back(permission.name);
                }
                recordConsent(manifest);
            }
            installed.append(nameOf(entry.id));
        }
        m_settings.setExtensions(states);
    }
    reload();
    emit filtersChanged();
    m_installing = false;
    emit installingChanged();
    emit bundleFinished(
        {{QStringLiteral("ok"), readable && !ext::hasErrors(problems)},
         {QStringLiteral("name"), work.bundle ? QString::fromStdString(work.bundle->name) : QString()},
         {QStringLiteral("installed"), installed},
         {QStringLiteral("problems"), problemList(problems)}});
}

bool ExtensionHost::exportBundle(const QStringList& ids, const QString& name, const QString& fileOrUrl) const
{
    const std::optional<fs::path> target = localPath(fileOrUrl);
    if (!target || m_roots.user.empty() || ids.isEmpty()) {
        return false;
    }
    const settings::ExtensionStates& states = m_settings.extensions();
    const fs::path staging = stagingOf(m_roots.user);
    ext::Bundle bundle;
    bundle.name = name.toStdString();
    bool complete = true;
    for (const QString& wanted : ids) {
        const std::string id = wanted.toStdString();
        const ext::Manifest* manifest = manifestOf(id);
        const auto chosen = std::ranges::find_if(m_extensions, [&](const Extension& extension) {
            return manifest != nullptr && extension.usable() && &*extension.manifest == manifest;
        });
        if (chosen == m_extensions.end()) {
            complete = false;
            break;
        }
        ext::BundleEntry entry;
        entry.id = id;
        entry.version = manifest->version;
        entry.builtin = chosen->builtin;
        const auto state = states.find(id);
        entry.enabled = state != states.end() && state->second.enabled;
        if (state != states.end()) {
            for (const auto& [key, value] : state->second.values) {
                if (std::ranges::find(manifest->settings, key, &ext::SettingDeclaration::key) != manifest->settings.end()) {
                    entry.values.emplace(key, settingValueOf(value));
                }
            }
        }
        if (!entry.builtin) {
            const fs::path package = staging / pathOf(ext::bundlePackagePath(entry));
            std::error_code error;
            fs::create_directories(package.parent_path(), error);
            if (ext::hasErrors(ext::packDirectory(chosen->directory, package))) {
                complete = false;
                break;
            }
        }
        bundle.extensions.push_back(std::move(entry));
    }
    const bool packed = complete && !ext::hasErrors(ext::packBundle(bundle, staging, *target));
    std::error_code error;
    fs::remove_all(staging, error);
    return packed;
}

const ExtensionHost::Extension* ExtensionHost::exactly(std::string_view id, const ext::Version& version,
                                                       bool builtin) const
{
    const auto found = std::ranges::find_if(m_extensions, [&](const Extension& extension) {
        return extension.usable() && extension.builtin == builtin && extension.manifest->id == id &&
               extension.manifest->version == version;
    });
    return found != m_extensions.end() ? &*found : nullptr;
}

bool ExtensionHost::startBisect()
{
    if (m_bisect || m_installing || m_mode == Mode::SafeRequested) {
        return false;
    }
    std::vector<std::string> enabled;
    for (const auto& [id, state] : m_settings.extensions()) {
        if (state.enabled && preferredManifest(id) != nullptr) {
            enabled.push_back(id);
        }
    }
    if (enabled.empty()) {
        return false;
    }
    m_bisect = ExtensionBisect::begin(std::move(enabled));
    m_mode = Mode::Normal;
    applyBisect();
    return true;
}

void ExtensionHost::answerBisect(bool stillHappens)
{
    if (m_bisect) {
        m_bisect->answer(stillHappens);
        applyBisect();
    }
}

void ExtensionHost::stopBisect()
{
    if (m_bisect) {
        m_bisect.reset();
        applyBisect();
    }
}

void ExtensionHost::concludeBisect(bool turnOff)
{
    if (!m_bisect) {
        return;
    }
    const std::optional<std::string> found = m_bisect->found();
    m_bisect.reset();
    applyBisect();
    if (turnOff && found) {
        static_cast<void>(setEnabled(*found, false));
    }
}

QVariantMap ExtensionHost::bisectState() const
{
    if (!m_bisect) {
        return {{QStringLiteral("active"), false}};
    }
    constexpr std::array<std::string_view, 3> outcomes{"running", "found", "notExtensions"};
    QStringList loaded;
    for (const std::string& id : m_bisect->trial()) {
        loaded.append(nameOf(id));
    }
    QVariantMap state{{QStringLiteral("active"), true},
                      {QStringLiteral("step"), m_bisect->step() + 1},
                      {QStringLiteral("steps"), m_bisect->steps()},
                      {QStringLiteral("loaded"), loaded},
                      {QStringLiteral("outcome"),
                       toQString(outcomes[static_cast<std::size_t>(m_bisect->outcome())])}};
    if (const std::optional<std::string> found = m_bisect->found()) {
        QStringList needs;
        if (const ext::Manifest* manifest = manifestOf(*found)) {
            for (const auto& [id, dependency] : manifest->dependencies) {
                if (!dependency.optional) {
                    needs.append(nameOf(id));
                }
            }
        }
        state.insert(QStringLiteral("found"), QVariantMap{{QStringLiteral("id"), QString::fromStdString(*found)},
                                                          {QStringLiteral("name"), nameOf(*found)},
                                                          {QStringLiteral("needs"), needs}});
    }
    return state;
}

void ExtensionHost::storeBisect()
{
    if (m_bisectFile.empty()) {
        return;
    }
    std::error_code error;
    if (m_bisect) {
        fs::create_directories(m_bisectFile.parent_path(), error);
        error = platform::writeFileAtomically(m_bisectFile, m_bisect->save());
    } else {
        error = platform::removeFile(m_bisectFile);
    }
    m_bisectUnsaved = static_cast<bool>(error);
    if (!error) {
        return;
    }
    const bool busy = platform::fileBusy(error);
    log::warning("extensions", "could not save the troubleshooting state ({}error {})",
                 busy ? "another program is using it, " : "", error.value());
    if (busy) {
        emit saveBusy();
    }
}

void ExtensionHost::applyBisect()
{
    storeBisect();
    resolve();
    emit extensionsChanged();
    emit filtersChanged();
    emit valuesChanged();
    emit bisectChanged();
}

QString ExtensionHost::nameOf(std::string_view id) const
{
    const ext::Manifest* manifest = manifestOf(id);
    return manifest != nullptr ? extensionText(*manifest, "name", manifest->name, m_settings.interfaceLanguage())
                               : QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size()));
}

void ExtensionHost::finish()
{
    const auto unsaved = m_unsavedConsents;
    for (const auto& [id, wanted] : unsaved) {
        if (wanted) {
            recordConsent(*wanted);
        } else {
            forgetConsent(id);
        }
    }
    if (m_bisectUnsaved) {
        storeBisect();
    }
    if (!m_runMarker.empty()) {
        if (const std::error_code error = platform::removeFile(m_runMarker)) {
            log::warning("extensions", "could not remove the run marker (error {}): the next start may think "
                                       "GameScrcpy stopped unexpectedly",
                         error.value());
        }
    }
}

std::vector<ext::Diagnostic> ExtensionHost::problemsOf(std::string_view id) const
{
    const std::string prefix = memberPath(id);
    std::vector<ext::Diagnostic> found;
    std::ranges::copy_if(m_problems, std::back_inserter(found),
                         [&](const ext::Diagnostic& problem) { return problem.path.starts_with(prefix); });
    return found;
}

std::vector<render::VideoFilter> ExtensionHost::filters(const std::string& deviceId) const
{
    const settings::ExtensionStates& states = m_settings.extensions();
    const settings::DeviceSettings device = deviceId.empty() ? settings::DeviceSettings{} : m_settings.device(deviceId);
    std::vector<render::VideoFilter> result;
    result.reserve(m_filters.size());
    for (const Filter& filter : m_filters) {
        const auto state = states.find(filter.extensionId);
        const auto overrides = device.extensions.find(filter.extensionId);
        render::VideoFilter video{filter.id, filter.fileName, filter.source, {}, filter.textures};
        for (const ext::FilterParam& param : filter.params) {
            const auto setting = std::ranges::find(filter.settings, param.name, &ext::SettingDeclaration::key);
            std::optional<double> value;
            if (setting != filter.settings.end()) {
                const bool perDevice = setting->scope == ext::SettingScope::Device;
                const settings::Scalar* const candidates[] = {
                    perDevice && overrides != device.extensions.end() ? scalarOf(overrides->second.values, param.name)
                                                                      : nullptr,
                    state != states.end() ? scalarOf(state->second.values, param.name) : nullptr};
                for (const settings::Scalar* candidate : candidates) {
                    if (!value && candidate != nullptr && ext::settingAccepts(*setting, settingValueOf(*candidate))) {
                        value = numberOf(settingValueOf(*candidate));
                    }
                }
                if (!value) {
                    value = numberOf(setting->defaultValue);
                }
            }
            video.values.emplace(param.name, fitted(param, value.value_or(param.defaultValue)));
        }
        result.push_back(std::move(video));
    }
    return result;
}

bool ExtensionHost::setEnabled(const std::string& id, bool enabled)
{
    if (m_installing || m_bisect) {
        return false;
    }
    const ext::Manifest* manifest = preferredManifest(id);
    if (enabled && manifest == nullptr) {
        return false;
    }
    settings::ExtensionStates states = m_settings.extensions();
    settings::ExtensionState& state = states[id];
    if (state.enabled == enabled && (!enabled || consented(*manifest))) {
        return true;
    }
    state.enabled = enabled;
    if (enabled) {
        state.granted.clear();
        for (const ext::Permission& permission : manifest->permissions) {
            state.granted.push_back(permission.name);
        }
        recordConsent(*manifest);
        m_health->clearOff(id);
    }
    if (state == settings::ExtensionState{}) {
        states.erase(id);
    }
    m_settings.setExtensions(states);
    resolve();
    emit extensionsChanged();
    emit filtersChanged();
    emit valuesChanged();
    return true;
}

bool ExtensionHost::setValue(const std::string& id, const std::string& key, const settings::Scalar& value)
{
    const ext::SettingDeclaration* setting = declarationOf(id, key);
    if (setting == nullptr || !storable(*setting, value)) {
        return false;
    }
    settings::ExtensionStates states = m_settings.extensions();
    const settings::Scalar* before = scalarOf(states[id].values, key);
    const bool changed = before == nullptr || *before != value;
    states[id].values.insert_or_assign(key, value);
    m_settings.setExtensions(states);
    emit filtersChanged();
    emit valuesChanged();
    if (changed) {
        publishSettingsChanged({QString::fromStdString(key)}, {}, QString::fromStdString(id));
    }
    return true;
}

bool ExtensionHost::setDeviceValue(const std::string& deviceId, const std::string& id, const std::string& key,
                                   const settings::Scalar& value)
{
    const ext::SettingDeclaration* setting = declarationOf(id, key);
    if (deviceId.empty() || setting == nullptr || setting->scope != ext::SettingScope::Device ||
        !storable(*setting, value)) {
        return false;
    }
    settings::DeviceSettings device = m_settings.device(deviceId);
    const settings::Scalar* before = scalarOf(device.extensions[id].values, key);
    const bool changed = before == nullptr || *before != value;
    device.extensions[id].values.insert_or_assign(key, value);
    if (!m_settings.setDevice(deviceId, device)) {
        return false;
    }
    emit filtersChanged();
    emit valuesChanged();
    if (changed) {
        publishSettingsChanged({QString::fromStdString(key)}, {}, QString::fromStdString(id),
                               QString::fromStdString(deviceId));
    }
    return true;
}

bool ExtensionHost::resetDeviceValue(const std::string& deviceId, const std::string& id, const std::string& key)
{
    settings::DeviceSettings device = m_settings.device(deviceId);
    const auto found = device.extensions.find(id);
    if (found == device.extensions.end() || found->second.values.erase(key) == 0) {
        return false;
    }
    if (found->second.values.empty()) {
        device.extensions.erase(found);
    }
    m_settings.setDevice(deviceId, device);
    emit filtersChanged();
    emit valuesChanged();
    publishSettingsChanged({QString::fromStdString(key)}, {}, QString::fromStdString(id),
                           QString::fromStdString(deviceId));
    return true;
}

ext::InstalledExtension ExtensionHost::install(const std::filesystem::path& package)
{
    const ext::PackageCheck check = ext::checkPackage(package, {.official = false});
    if (check.manifest) {
        const ext::NativeCheck native = ext::checkNative(ext::archiveContents(package), *check.manifest, m_scanner);
        if (native.refused()) {
            return {{}, check.manifest, {{ext::Severity::Error, ext::Code::NativeRefused, "$", native.detail}}};
        }
    }
    return finishInstall(ext::installPackage(package, m_roots.user));
}

ext::InstalledExtension ExtensionHost::finishInstall(ext::InstalledExtension outcome)
{
    if (!outcome.ok()) {
        return outcome;
    }
    const ext::Manifest& installed = *outcome.manifest;
    const settings::ExtensionStates& states = m_settings.extensions();
    const auto state = states.find(installed.id);
    const std::string locked = state != states.end() ? state->second.version : std::string();
    for (const Extension& extension : m_extensions) {
        if (!extension.builtin && extension.manifest && extension.manifest->id == installed.id &&
            extension.manifest->version != installed.version && extension.manifest->version.text() != locked) {
            static_cast<void>(ext::removeInstalled(m_roots.user, installed.id, extension.manifest->version));
        }
    }
    reload();
    emit filtersChanged();
    return outcome;
}

bool ExtensionHost::uninstall(const std::string& id, const ext::Version& version)
{
    if (m_installing) {
        return false;
    }
    const auto found = std::ranges::find_if(m_extensions, [&](const Extension& extension) {
        return extension.usable() && extension.manifest->id == id && extension.manifest->version == version;
    });
    if (found == m_extensions.end() || found->builtin) {
        return false;
    }
    const bool others = std::ranges::any_of(m_extensions, [&](const Extension& extension) {
        return extension.manifest && extension.manifest->id == id && extension.manifest->version != version;
    });
    settings::ExtensionStates states = m_settings.extensions();
    if (!others) {
        states.erase(id);
        forgetConsent(id);
        static_cast<void>(ext::forgetPublisher(m_roots.user, id));
    } else if (const auto state = states.find(id); state != states.end() && state->second.version == version.text()) {
        state->second.version.clear();
    }
    m_settings.setExtensions(states);
    resolve();
    emit filtersChanged();
    const bool removed = ext::removeInstalled(m_roots.user, id, version);
    reload();
    emit filtersChanged();
    return removed;
}

void ExtensionHost::reload()
{
    m_extensions.clear();
    const std::pair<const fs::path*, bool> roots[] = {{&m_roots.builtin, true}, {&m_roots.user, false}};
    for (const auto& [root, builtin] : roots) {
        if (root->empty()) {
            continue;
        }
        for (ext::InstalledExtension& found : ext::scanInstalled(*root, {.official = builtin})) {
            Extension extension{
                std::move(found.directory), std::move(found.manifest), std::move(found.diagnostics), builtin};
            extension.superseded = !builtin && extension.usable() &&
                                   exactly(extension.manifest->id, extension.manifest->version, true) != nullptr;
            if (!builtin && extension.manifest) {
                extension.signature =
                    ext::checkSignedSums(ext::directoryContents(extension.directory), extension.manifest->authorKey);
            }
            m_extensions.push_back(std::move(extension));
        }
    }
    m_publishers.clear();
    for (const Extension& extension : m_extensions) {
        if (extension.builtin || !extension.manifest || m_roots.user.empty() ||
            m_publishers.contains(extension.manifest->id)) {
            continue;
        }
        const std::string& id = extension.manifest->id;
        std::optional<ext::MinisignKey> remembered = ext::rememberedPublisher(m_roots.user, id);
        if (!remembered && extension.signature.state == ext::SignatureState::Valid && extension.signature.key) {
            remembered = extension.signature.key;
            static_cast<void>(ext::rememberPublisher(m_roots.user, id, *remembered));
        }
        if (remembered) {
            m_publishers.emplace(id, *remembered);
        }
    }
    resolve();
    emit extensionsChanged();
    emit valuesChanged();
}

void ExtensionHost::resolve()
{
    m_problems.clear();
    m_filters.clear();
    m_profileSources.clear();
    m_patchSources.clear();
    m_componentSources.clear();
    m_taskSources.clear();
    m_scriptSources.clear();
    for (Extension& extension : m_extensions) {
        extension.active = false;
    }
    std::vector<NativeWanted> native;
    if (m_mode == Mode::Normal) {
        const settings::ExtensionStates& states = m_settings.extensions();
        std::vector<ext::Manifest> candidates;
        std::set<std::string, std::less<>> heldBack;
        for (const Extension& extension : m_extensions) {
            if (!extension.usable()) {
                continue;
            }
            const ext::Manifest& manifest = *extension.manifest;
            const auto state = states.find(manifest.id);
            if (state != states.end() && !state->second.version.empty() &&
                state->second.version != manifest.version.text()) {
                continue;
            }
            std::vector<ext::Diagnostic> incompatible = ext::checkCompatibility(manifest, hostApi());
            if (ext::hasErrors(incompatible)) {
                for (ext::Diagnostic& problem : incompatible) {
                    problem.path = memberPath(manifest.id) + problem.path.substr(1);
                    m_problems.push_back(std::move(problem));
                }
                heldBack.insert(manifest.id);
                continue;
            }
            if (manifest.contributes.native &&
                ext::nativeDllFor(*manifest.contributes.native, ext::hostArchitecture()).empty()) {
                m_problems.push_back({ext::Severity::Error, ext::Code::NativeArchitectureMissing,
                                      memberPath(manifest.id) + ".contributes.native.dll",
                                      std::string(ext::hostArchitecture())});
                heldBack.insert(manifest.id);
                continue;
            }
            if (state != states.end() && state->second.enabled && !consented(manifest)) {
                m_problems.push_back({ext::Severity::Warning, ext::Code::ConsentRequired,
                                      memberPath(manifest.id) + ".permissions", manifest.version.text()});
                heldBack.insert(manifest.id);
                continue;
            }
            candidates.push_back(manifest);
        }
        for (const ext::Manifest& manifest : candidates) {
            heldBack.erase(manifest.id);
        }
        std::vector<std::string> enabled;
        for (const auto& [id, state] : states) {
            if (state.enabled && !heldBack.contains(id) && (!m_bisect || m_bisect->loads(id))) {
                enabled.push_back(id);
            }
        }
        ext::Resolution resolution = ext::resolveDependencies(candidates, enabled);
        std::ranges::move(resolution.problems, std::back_inserter(m_problems));
        for (const ext::Selected& selected : resolution.order) {
            const auto found = std::ranges::find_if(m_extensions, [&](const Extension& extension) {
                return extension.usable() && extension.manifest->id == selected.id &&
                       extension.manifest->version == selected.version;
            });
            if (found != m_extensions.end()) {
                found->active = true;
                load(*found);
                if (found->manifest->contributes.native) {
                    native.push_back(nativeWanted(*found));
                }
            }
        }
    }
    m_native->apply(std::move(native));
    pushGlobals();
    const std::vector<ext::Diagnostic> nativeProblems = m_native->problems();
    m_problems.insert(m_problems.end(), nativeProblems.begin(), nativeProblems.end());
    std::set<std::string, std::less<>> turnedOff;
    for (const Extension& extension : m_extensions) {
        if (!extension.manifest || extension.superseded || !turnedOff.insert(extension.manifest->id).second) {
            continue;
        }
        const HealthRecord* record = m_health->find(extension.manifest->id);
        if (record == nullptr || !record->off) {
            continue;
        }
        const bool stopped = *record->off == HealthKind::Stopped;
        m_problems.push_back({ext::Severity::Warning, stopped ? ext::Code::NativeCrashed : ext::Code::NativeModuleFailed,
                              memberPath(extension.manifest->id) + ".contributes.native",
                              stopped ? "stopped working three times within a minute"
                                      : "failed three times within 10 seconds"});
    }
    composeProfiles();
    composeLibraries();
    markRun();
    std::map<std::string, Announced, std::less<>> announced;
    for (const Extension& extension : m_extensions) {
        if (extension.active && extension.manifest) {
            announced.insert_or_assign(extension.manifest->id,
                                       Announced{.version = extension.manifest->version.text(),
                                                 .name = nameOf(extension.manifest->id)});
        }
    }
    for (const auto& [id, was] : m_announced) {
        if (const auto now = announced.find(id); now == announced.end() || now->second.version != was.version) {
            publishExtensionTurned(false, QString::fromStdString(id), was.name, QString::fromStdString(was.version));
        }
    }
    for (const auto& [id, now] : announced) {
        if (const auto was = m_announced.find(id); was == m_announced.end() || was->second.version != now.version) {
            publishExtensionTurned(true, QString::fromStdString(id), now.name, QString::fromStdString(now.version));
        }
    }
    m_announced = std::move(announced);
}

void ExtensionHost::load(const Extension& extension)
{
    const ext::Manifest& manifest = *extension.manifest;
    for (std::size_t index = 0; index < manifest.contributes.filters.size(); ++index) {
        const ext::FilterContribution& contribution = manifest.contributes.filters[index];
        const std::string where = memberPath(manifest.id) + ".contributes.filters[" + std::to_string(index) + "].file";
        std::optional<std::string> text =
            readText(extension.directory / pathOf(contribution.file), ext::kMaxFilterSourceBytes);
        if (!text) {
            report(ext::Code::FileMissing, where, contribution.file);
            continue;
        }
        ext::FilterSource source = ext::parseFilterSource(*text);
        if (!source.usable()) {
            const auto error = std::ranges::find(source.diagnostics, ext::Severity::Error, &ext::Diagnostic::severity);
            report(error->code, where, contribution.file + ": " + error->path + " " + error->detail);
            continue;
        }
        Filter filter{manifest.id,
                      manifest.id + "/" + contribution.name,
                      manifest.id + "/" + contribution.file,
                      std::move(*text),
                      std::move(source.params),
                      manifest.settings,
                      {}};
        bool complete = true;
        for (const ext::FilterTexture& texture : source.textures) {
            QImageReader reader(QString::fromStdWString((extension.directory / pathOf(texture.file)).wstring()));
            const QSize size = reader.size();
            QImage image;
            if (size.isValid() && size.width() <= kMaxTextureSide && size.height() <= kMaxTextureSide) {
                image = reader.read();
            }
            if (image.isNull()) {
                report(ext::Code::TextureUnreadable, where, texture.file);
                complete = false;
                break;
            }
            filter.textures.emplace(texture.name, std::move(image));
        }
        if (complete) {
            m_filters.push_back(std::move(filter));
        }
    }
    for (std::size_t index = 0; index < manifest.contributes.themes.size(); ++index) {
        const ext::ThemeContribution& theme = manifest.contributes.themes[index];
        const std::string where = memberPath(manifest.id) + ".contributes.themes[" + std::to_string(index) + "].";
        const auto check = [&](std::string_view side, const ext::TextMap& tokens) {
            for (const auto& entry : tokens) {
                const std::string& token = entry.first;
                if (std::ranges::find(kThemeTokenNames, std::string_view(token)) == kThemeTokenNames.end()) {
                    m_problems.push_back({ext::Severity::Warning, ext::Code::ThemeTokenInvalid,
                                          where + std::string(side) + "[\"" + token + "\"]", token});
                }
            }
        };
        check("dark", theme.dark);
        check("light", theme.light);
    }
    for (std::size_t index = 0; index < manifest.contributes.profiles.size(); ++index) {
        const ext::ProfileContribution& contribution = manifest.contributes.profiles[index];
        const std::string where = memberPath(manifest.id) + ".contributes.profiles[" + std::to_string(index) + "]";
        std::optional<std::string> text = readText(extension.directory / pathOf(contribution.file), kMaxProfileBytes);
        if (!text) {
            report(ext::Code::FileMissing, where + ".file", contribution.file);
            continue;
        }
        m_profileSources.push_back({manifest.id + "/" + contribution.name, manifest.id, manifest.version.text(), where,
                                    std::move(*text), profileFolderOf(extension.directory, contribution.file)});
    }
    for (std::size_t index = 0; index < manifest.contributes.profilePatches.size(); ++index) {
        const ext::ProfilePatchContribution& contribution = manifest.contributes.profilePatches[index];
        const std::string where =
            memberPath(manifest.id) + ".contributes.profilePatches[" + std::to_string(index) + "]";
        std::optional<std::string> text = readText(extension.directory / pathOf(contribution.file), kMaxProfileBytes);
        if (!text) {
            report(ext::Code::FileMissing, where + ".file", contribution.file);
            continue;
        }
        m_patchSources.push_back({manifest.id, contribution.target, where, std::move(*text)});
    }
    const auto contribute = [&](std::string_view kind, std::string_view entry, std::size_t index,
                                const std::string& name, const std::string& folder,
                                std::vector<ContentSource>& into) {
        const fs::path found = extension.directory / pathOf(folder);
        std::error_code error;
        if (!fs::is_regular_file(found / pathOf(entry), error)) {
            report(ext::Code::FileMissing,
                   memberPath(manifest.id) + ".contributes." + std::string(kind) + "[" + std::to_string(index) +
                       "].folder",
                   folder + "/" + std::string(entry));
            return;
        }
        into.push_back({manifest.id, name, found});
    };
    for (std::size_t index = 0; index < manifest.contributes.components.size(); ++index) {
        const ext::ComponentContribution& contribution = manifest.contributes.components[index];
        contribute("components", "component.ts", index, contribution.name, contribution.folder, m_componentSources);
    }
    for (std::size_t index = 0; index < manifest.contributes.tasks.size(); ++index) {
        const ext::TaskContribution& contribution = manifest.contributes.tasks[index];
        contribute("tasks", "task.ts", index, contribution.name, contribution.folder, m_taskSources);
    }
    for (std::size_t index = 0; index < manifest.contributes.scripts.size(); ++index) {
        const ext::ScriptContribution& contribution = manifest.contributes.scripts[index];
        std::error_code error;
        if (!fs::is_regular_file(extension.directory / pathOf(contribution.entry), error)) {
            report(ext::Code::FileMissing,
                   memberPath(manifest.id) + ".contributes.scripts[" + std::to_string(index) + "].entry",
                   contribution.entry);
            continue;
        }
        m_scriptSources.push_back({manifest.id, contribution, extension.directory});
    }
    for (std::size_t index = 0; index < manifest.contributes.converters.size(); ++index) {
        const ext::ConverterContribution& contribution = manifest.contributes.converters[index];
        std::error_code error;
        if (!fs::is_regular_file(extension.directory / pathOf(contribution.script), error)) {
            report(ext::Code::FileMissing,
                   memberPath(manifest.id) + ".contributes.converters[" + std::to_string(index) + "].script",
                   contribution.script);
        }
    }
}

void ExtensionHost::composeProfiles()
{
    const auto described = [](const input::Diagnostic& diagnostic) {
        return diagnostic.detail.empty() ? diagnostic.path : diagnostic.path + ": " + diagnostic.detail;
    };
    std::set<std::string, std::less<>> offered;
    for (const ProfileSource& source : m_profileSources) {
        offered.insert(source.id);
    }
    for (const PatchSource& patch : m_patchSources) {
        if (!offered.contains(patch.target)) {
            m_problems.push_back(
                {ext::Severity::Notice, ext::Code::PatchTargetMissing, patch.where + ".target", patch.target});
        }
    }
    std::vector<ExtensionProfile> composed;
    for (const ProfileSource& source : m_profileSources) {
        ExtensionProfile profile{.id = toQString(source.id),
                                 .extensionId = toQString(source.extensionId),
                                 .extensionName = nameOf(source.extensionId),
                                 .version = toQString(source.version),
                                 .json = QByteArray::fromStdString(source.text),
                                 .patchedBy = {},
                                 .conflicts = {},
                                 .folder = source.folder};
        const input::LoadResult base = input::loadProfile(source.text);
        if (!base.ok()) {
            const auto error = std::ranges::find(base.diagnostics, input::Severity::Error, &input::Diagnostic::severity);
            m_problems.push_back(
                {ext::Severity::Warning, ext::Code::ProfileInvalid, source.where + ".file", described(*error)});
            composed.push_back(std::move(profile));
            continue;
        }
        std::string current = source.text;
        std::vector<std::pair<std::string, std::vector<std::string>>> touchedBy;
        for (const PatchSource& patch : m_patchSources) {
            if (patch.target != source.id) {
                continue;
            }
            input::PatchOutcome outcome = input::applyPatch(current, patch.text);
            for (const input::Diagnostic& diagnostic : outcome.diagnostics) {
                const bool missing = diagnostic.code == input::DiagnosticCode::UnknownReference;
                m_problems.push_back({missing ? ext::Severity::Notice : ext::Severity::Warning,
                                      missing ? ext::Code::PatchTargetMissing : ext::Code::PatchInvalid,
                                      patch.where + ".file", described(diagnostic)});
            }
            if (!outcome.applied) {
                continue;
            }
            current = std::move(outcome.profile);
            const QString name = nameOf(patch.extensionId);
            if (!profile.patchedBy.contains(name)) {
                profile.patchedBy.append(name);
            }
            for (const std::string& element : outcome.touched) {
                auto entry = std::ranges::find(touchedBy, element, &std::pair<std::string, std::vector<std::string>>::first);
                if (entry == touchedBy.end()) {
                    entry = touchedBy.emplace(touchedBy.end(), element, std::vector<std::string>{});
                }
                if (entry->second.empty() || entry->second.back() != patch.extensionId) {
                    entry->second.push_back(patch.extensionId);
                }
            }
        }
        profile.json = QByteArray::fromStdString(current);
        for (const auto& [element, by] : touchedBy) {
            if (by.size() < 2) {
                continue;
            }
            QStringList names;
            for (const std::string& id : by) {
                names.append(nameOf(id));
            }
            profile.conflicts.append(QVariantMap{{QStringLiteral("id"), toQString(element.substr(element.find('/') + 1))},
                                                 {QStringLiteral("by"), names}});
        }
        composed.push_back(std::move(profile));
    }
    if (composed != m_profiles) {
        m_profiles = std::move(composed);
        emit profilesChanged();
    }
}

void ExtensionHost::composeLibraries()
{
    std::vector<ExtensionComponent> components;
    for (const ContentSource& source : m_componentSources) {
        const QString extensionId = toQString(source.extensionId);
        const QString name = toQString(source.name);
        components.push_back({.ref = extensionComponentRef(extensionId, name),
                              .name = name,
                              .extensionId = extensionId,
                              .extensionName = nameOf(source.extensionId),
                              .folder = source.folder});
    }
    std::vector<ExtensionTask> tasks;
    for (const ContentSource& source : m_taskSources) {
        const QString extensionId = toQString(source.extensionId);
        const QString name = toQString(source.name);
        tasks.push_back({.id = extensionTaskId(extensionId, name),
                         .name = name,
                         .extensionId = extensionId,
                         .extensionName = nameOf(source.extensionId),
                         .folder = source.folder});
    }
    if (components != m_components) {
        m_components = std::move(components);
        emit componentsChanged();
    }
    if (tasks != m_tasks) {
        m_tasks = std::move(tasks);
        emit tasksChanged();
    }
    std::vector<ExtensionScript> scripts;
    for (const ContributedScript& source : m_scriptSources) {
        scripts.push_back({.extensionId = toQString(source.extensionId),
                           .extensionName = nameOf(source.extensionId),
                           .name = toQString(source.contribution.name),
                           .folder = source.folder,
                           .entry = source.contribution.entry,
                           .app = source.contribution.run == ext::ScriptRun::App,
                           .permissions = grantedPermissions(source.extensionId)});
    }
    if (scripts != m_scripts) {
        m_scripts = std::move(scripts);
        emit scriptsChanged();
    }
}

std::vector<std::string> ExtensionHost::grantedPermissions(std::string_view id) const
{
    std::vector<std::string> granted;
    for (const Extension& extension : m_extensions) {
        if (!extension.active || !extension.manifest || extension.manifest->id != id) {
            continue;
        }
        for (const ext::Permission& permission : extension.manifest->permissions) {
            granted.push_back(permission.name);
        }
        break;
    }
    return granted;
}

QString ExtensionHost::textOf(const ext::Manifest& manifest, std::string_view key, std::string_view fallback) const
{
    return extensionText(manifest, key, fallback, m_settings.interfaceLanguage());
}

std::optional<ext::SignatureCheck> ExtensionHost::installedSignature(std::string_view id) const
{
    const std::optional<ext::MinisignKey> remembered =
        m_roots.user.empty() ? std::nullopt : ext::publisherOf(m_roots.user, id);
    if (!remembered) {
        return std::nullopt;
    }
    return ext::SignatureCheck{.state = ext::SignatureState::Valid, .key = remembered, .problem = {}};
}

void ExtensionHost::markRun() const
{
    if (m_runMarker.empty()) {
        return;
    }
    std::error_code error;
    if (std::ranges::any_of(m_extensions, &Extension::active)) {
        fs::create_directories(m_runMarker.parent_path(), error);
        error = platform::writeFileAtomically(m_runMarker, "extensions loaded\n");
    } else {
        error = platform::removeFile(m_runMarker);
    }
    if (error) {
        log::warning("extensions", "could not update the run marker (error {})", error.value());
    }
}

void ExtensionHost::report(ext::Code code, std::string path, std::string detail)
{
    m_problems.push_back({ext::Severity::Error, code, std::move(path), std::move(detail)});
}

const ext::Manifest* ExtensionHost::preferredManifest(std::string_view id) const
{
    const settings::ExtensionStates& states = m_settings.extensions();
    const auto state = states.find(id);
    const std::string locked = state != states.end() ? state->second.version : std::string();
    const ext::Manifest* preferred = nullptr;
    for (const Extension& extension : m_extensions) {
        if (!extension.usable() || extension.manifest->id != id) {
            continue;
        }
        if (!locked.empty()) {
            if (extension.manifest->version.text() == locked) {
                return &*extension.manifest;
            }
            continue;
        }
        if (preferred == nullptr || preferred->version < extension.manifest->version) {
            preferred = &*extension.manifest;
        }
    }
    return preferred;
}

fs::path ExtensionHost::consentFile(std::string_view id) const
{
    return m_roots.user.empty() ? fs::path() : m_roots.user / ".consent" / pathOf(std::string(id) + ".json");
}

bool ExtensionHost::consented(const ext::Manifest& manifest) const
{
    static const ext::Manifest nothing{};
    if (!ext::compareForUpdate(nothing, manifest).needsConsent()) {
        return true;
    }
    if (const auto unsaved = m_unsavedConsents.find(manifest.id); unsaved != m_unsavedConsents.end()) {
        return unsaved->second.has_value() && !ext::compareForUpdate(*unsaved->second, manifest).needsConsent();
    }
    const fs::path file = consentFile(manifest.id);
    const std::optional<std::string> text = file.empty() ? std::nullopt : readText(file, ext::kMaxManifestBytes);
    if (!text) {
        return false;
    }
    const ext::LoadedManifest saved = ext::loadManifest(*text, {.official = true});
    return saved.usable && saved.manifest.id == manifest.id &&
           !ext::compareForUpdate(saved.manifest, manifest).needsConsent();
}

void ExtensionHost::recordConsent(const ext::Manifest& manifest)
{
    const fs::path file = consentFile(manifest.id);
    if (file.empty()) {
        return;
    }
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    settleConsent(manifest.id, manifest, platform::writeFileAtomically(file, ext::saveManifest(manifest)));
}

void ExtensionHost::forgetConsent(const std::string& id)
{
    if (const fs::path file = consentFile(id); !file.empty()) {
        settleConsent(id, std::nullopt, platform::removeFile(file));
    }
}

void ExtensionHost::settleConsent(const std::string& id, std::optional<ext::Manifest> wanted,
                                  const std::error_code& error)
{
    if (!error) {
        m_unsavedConsents.erase(id);
        return;
    }
    const bool busy = platform::fileBusy(error);
    log::warning("extensions", "could not save what {} is allowed to do ({}error {})", id,
                 busy ? "another program is using it, " : "", error.value());
    m_unsavedConsents.insert_or_assign(id, std::move(wanted));
    if (busy) {
        emit saveBusy();
    }
}

void ExtensionHost::forget(const std::string& id)
{
    settings::ExtensionStates states = m_settings.extensions();
    if (states.erase(id) > 0) {
        m_settings.setExtensions(states);
    }
    forgetConsent(id);
    static_cast<void>(ext::forgetPublisher(m_roots.user, id));
}

const ext::Manifest* ExtensionHost::manifestOf(std::string_view id) const
{
    const Extension* newest = nullptr;
    for (const Extension& extension : m_extensions) {
        if (!extension.usable() || extension.manifest->id != id) {
            continue;
        }
        if (extension.active) {
            return &*extension.manifest;
        }
        if (newest == nullptr || newest->manifest->version < extension.manifest->version) {
            newest = &extension;
        }
    }
    return newest != nullptr ? &*newest->manifest : nullptr;
}

const ext::SettingDeclaration* ExtensionHost::declarationOf(std::string_view id, std::string_view key) const
{
    const ext::Manifest* manifest = manifestOf(id);
    if (manifest == nullptr) {
        return nullptr;
    }
    const auto found = std::ranges::find(manifest->settings, key, &ext::SettingDeclaration::key);
    return found == manifest->settings.end() ? nullptr : &*found;
}

}
