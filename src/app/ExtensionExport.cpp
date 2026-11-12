#include "ExtensionExport.h"

#include "ExtensionContent.h"

#include <gs/ext/Archive.h>
#include <gs/ext/Manifest.h>
#include <gs/ext/Package.h>
#include <gs/ext/Resolution.h>
#include <gs/ext/Version.h>
#include <gs/platform/AtomicFile.h>

#include <QByteArray>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QTemporaryDir>
#include <QUrl>

#include <algorithm>
#include <map>
#include <optional>
#include <system_error>
#include <utility>
#include <variant>

namespace gs::app {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kMostSlug = 40;
constexpr std::size_t kMostCodeBytes = std::size_t{1} << 20;
constexpr std::string_view kLicense = "All rights reserved";

const std::vector<std::string>& unpacked()
{
    static const std::vector<std::string> names{"node_modules"};
    return names;
}

fs::path pathOf(std::string_view text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

fs::path pathOf(const QString& text)
{
    return fs::path(text.toStdWString());
}

std::string slugOf(const QString& text)
{
    std::string slug;
    for (const QChar c : text) {
        const char16_t unit = c.unicode();
        if ((unit >= u'a' && unit <= u'z') || (unit >= u'0' && unit <= u'9')) {
            slug += static_cast<char>(unit);
        } else if (unit >= u'A' && unit <= u'Z') {
            slug += static_cast<char>(unit - u'A' + u'a');
        } else if (!slug.empty() && slug.back() != '-') {
            slug += '-';
        }
    }
    if (slug.size() > kMostSlug) {
        slug.resize(kMostSlug);
    }
    while (!slug.empty() && slug.back() == '-') {
        slug.pop_back();
    }
    return slug;
}

std::string contributionName(const QString& text, std::string_view fallback, std::vector<std::string>& used,
                             std::size_t most)
{
    std::string name = slugOf(text);
    if (name.size() > most) {
        name.resize(most);
    }
    while (!name.empty() && name.back() == '-') {
        name.pop_back();
    }
    if (name.empty()) {
        name = std::string(fallback);
    }
    std::string unique = name;
    for (int copy = 2; std::ranges::find(used, unique) != used.end(); ++copy) {
        const std::string suffix = "-" + std::to_string(copy);
        unique = name.substr(0, most - suffix.size()) + suffix;
    }
    used.push_back(unique);
    return unique;
}

QString limited(const QString& text, qsizetype most)
{
    const QList<uint> points = text.toUcs4();
    if (points.size() <= most) {
        return text;
    }
    return QString::fromUcs4(reinterpret_cast<const char32_t*>(points.constData()), most);
}

ext::Manifest baseManifest(const QString& name, const QString& description, std::string_view kind)
{
    ext::Manifest manifest;
    manifest.id = exportedExtensionId(name, kind);
    manifest.version = ext::Version{1, 0, 0};
    manifest.name = limited(name.trimmed().isEmpty() ? QString::fromStdString(manifest.id) : name.trimmed(), 100)
                        .toStdString();
    manifest.description = limited(description.trimmed(), 1000).toStdString();
    manifest.license = std::string(kLicense);
    return manifest;
}

void needs(ext::Manifest& manifest, ext::ExtensionPoint point)
{
    if (const std::optional<ext::VersionRange> range = ext::requirementFor(point)) {
        manifest.requirements.insert_or_assign(point, *range);
    }
}

bool copyFolder(const fs::path& from, const fs::path& to)
{
    const ext::ArchiveListing listing = ext::inspectDirectory(from, {}, unpacked());
    if (!listing.ok()) {
        return false;
    }
    std::error_code error;
    fs::create_directories(to, error);
    for (const ext::ArchiveEntry& entry : listing.files) {
        const fs::path relative = pathOf(entry.path);
        if (!error) {
            fs::create_directories((to / relative).parent_path(), error);
        }
        if (!error) {
            fs::copy_file(from / relative, to / relative, fs::copy_options::overwrite_existing, error);
        }
        if (error) {
            return false;
        }
    }
    return !error;
}

bool relink(const fs::path& file, const std::vector<std::pair<QString, QString>>& renamed)
{
    std::string contents;
    if (platform::readFile(file, kMostCodeBytes, contents)) {
        return false;
    }
    QString code = QString::fromUtf8(contents);
    const QString original = code;
    for (const auto& [from, to] : renamed) {
        if (from == to) {
            continue;
        }
        for (const QChar quote : {u'"', u'\''}) {
            code.replace(quote + QStringLiteral("../") + from + u'/', quote + QStringLiteral("../") + to + u'/');
        }
    }
    return code == original || !platform::writeFileAtomically(file, code.toStdString());
}

std::optional<std::map<QString, QString>> addComponents(ext::Manifest& manifest, const fs::path& root,
                                                        const std::vector<ExportedComponent>& components)
{
    std::map<QString, QString> refs;
    std::vector<std::pair<QString, QString>> renamed;
    std::vector<std::string> used;
    std::vector<fs::path> copied;
    const QString id = QString::fromStdString(manifest.id);
    for (const ExportedComponent& component : components) {
        if (refs.contains(component.ref)) {
            continue;
        }
        const QString folder = component.ref.section(u'/', 1);
        const std::string name = contributionName(folder, "component", used, ext::kMaxComponentNameLength);
        const fs::path into = root / "components" / pathOf(name);
        if (!copyFolder(component.folder, into)) {
            return std::nullopt;
        }
        manifest.contributes.components.push_back({.name = name, .folder = "components/" + name});
        const QString ref = extensionComponentRef(id, QString::fromStdString(name));
        refs.emplace(component.ref, ref);
        renamed.emplace_back(folder, ref.section(u'/', 1));
        copied.push_back(into / "component.ts");
    }
    for (const fs::path& file : copied) {
        if (!relink(file, renamed)) {
            return std::nullopt;
        }
    }
    return refs;
}

bool packed(const ext::Manifest& manifest, const fs::path& root, const fs::path& target)
{
    if (platform::writeFileAtomically(root / "manifest.json", ext::saveManifest(manifest))) {
        return false;
    }
    if (!ext::packDirectory(root, target).empty()) {
        return false;
    }
    if (!ext::checkPackage(target).ok()) {
        std::error_code error;
        fs::remove(target, error);
        return false;
    }
    return true;
}

}

std::optional<fs::path> exportTarget(const QString& fileOrUrl)
{
    const QUrl url(fileOrUrl);
    const QString local = url.isLocalFile() ? url.toLocalFile() : fileOrUrl;
    if (local.isEmpty() || local.contains(QChar::Null) || !QFileInfo(local).isAbsolute()) {
        return std::nullopt;
    }
    return pathOf(local);
}

std::string exportedExtensionId(const QString& name, std::string_view kind)
{
    std::string slug = slugOf(name);
    if (slug.empty()) {
        const QByteArray digest =
            QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256).toHex().left(8);
        slug = std::string(kind) + "-" + digest.toStdString();
    }
    return "my." + slug;
}

bool exportComponentPackage(const QString& name, const QString& description,
                            const std::vector<ExportedComponent>& components, const fs::path& target)
{
    const QTemporaryDir staging;
    if (components.empty() || !staging.isValid()) {
        return false;
    }
    const fs::path root = pathOf(staging.path());
    ext::Manifest manifest = baseManifest(name, description, "component");
    needs(manifest, ext::ExtensionPoint::Component);
    return addComponents(manifest, root, components).has_value() && packed(manifest, root, target);
}

bool exportTaskPackage(const ExportedTask& task, const fs::path& target)
{
    const QTemporaryDir staging;
    if (!staging.isValid()) {
        return false;
    }
    const fs::path root = pathOf(staging.path());
    ext::Manifest manifest = baseManifest(task.name, {}, "task");
    needs(manifest, ext::ExtensionPoint::Task);
    std::vector<std::string> used;
    const std::string name = contributionName(task.id, "task", used, 64);
    const fs::path into = root / "tasks" / pathOf(name);
    if (!copyFolder(task.folder, into)) {
        return false;
    }
    const QJsonObject settings{
        {QStringLiteral("format"), QStringLiteral("gamescrcpy.task")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("name"), task.name},
        {QStringLiteral("phones"), QJsonArray()},
        {QStringLiteral("schedule"),
         QJsonObject{{QStringLiteral("mode"), QStringLiteral("manual")}, {QStringLiteral("time"), QStringLiteral("08:00")}}},
        {QStringLiteral("onError"), task.onError.isEmpty() ? QStringLiteral("stop") : task.onError},
        {QStringLiteral("wake"), task.wake},
        {QStringLiteral("sleepAfter"), task.sleepAfter},
        {QStringLiteral("values"), QJsonObject::fromVariantMap(task.values)},
    };
    if (platform::writeFileAtomically(into / "task.json",
                                      QJsonDocument(settings).toJson(QJsonDocument::Indented).toStdString())) {
        return false;
    }
    manifest.contributes.tasks.push_back({.name = name, .folder = "tasks/" + name});
    return packed(manifest, root, target);
}

bool exportProfilePackage(const ExportedProfile& exported, const std::vector<ExportedComponent>& components,
                          const fs::path& target)
{
    const QTemporaryDir staging;
    if (!staging.isValid()) {
        return false;
    }
    const fs::path root = pathOf(staging.path());
    const QString title = exported.name.trimmed().isEmpty() ? exported.id.section(u'/', -1) : exported.name;
    ext::Manifest manifest = baseManifest(title, {}, "profile");
    needs(manifest, ext::ExtensionPoint::Profile);
    std::vector<std::string> used;
    const std::string name = contributionName(exported.id.section(u'/', -1), "profile", used, 64);
    const fs::path into = root / "profiles" / pathOf(name);
    if (!exported.folder.empty() && !copyFolder(exported.folder, into)) {
        return false;
    }
    input::Profile profile = exported.profile;
    if (!components.empty()) {
        needs(manifest, ext::ExtensionPoint::Component);
        const std::optional<std::map<QString, QString>> refs = addComponents(manifest, root, components);
        if (!refs) {
            return false;
        }
        for (input::Control& control : profile.controls) {
            auto* placed = std::get_if<input::ComponentControl>(&control.kind);
            std::string& ref = placed != nullptr ? placed->component : control.component;
            if (const auto found = refs->find(QString::fromStdString(ref)); found != refs->end()) {
                ref = found->second.toStdString();
            }
        }
    }
    std::error_code error;
    fs::create_directories(into, error);
    if (error || platform::writeFileAtomically(into / "profile.json", input::saveProfile(profile))) {
        return false;
    }
    manifest.contributes.profiles.push_back({.name = name, .file = "profiles/" + name + "/profile.json"});
    return packed(manifest, root, target);
}

}
