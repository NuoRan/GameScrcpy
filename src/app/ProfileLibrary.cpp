#include "ProfileLibrary.h"

#include "ComponentLibrary.h"
#include "ExtensionExport.h"
#include "InputHost.h"
#include "TranslateNoop.h"

#include <gs/ext/Archive.h>
#include <gs/input/KeyHints.h>
#include <gs/input/ProfileDiff.h>
#include <gs/input/ProfilePatch.h>
#include <gs/platform/AtomicFile.h>
#include <gs/script/LogicHost.h>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

constexpr qint64 kMostProfileBytes = 1024 * 1024;
constexpr qsizetype kMostNameBytes = 64;
constexpr qsizetype kMostNameChars = 64;
constexpr qsizetype kMostStemLength = 64;
constexpr qsizetype kMostPackageBytes = 255;
constexpr double kLongSide = 16.0;
constexpr double kShortSide = 9.0;
constexpr int kMostBases = 16;
constexpr const char* kTemplateContext = "ProfileTemplates";

struct ProfileTemplate {
    std::string_view id;
    const char* name;
    std::string_view icon;
};

constexpr std::array kTemplates{
    ProfileTemplate{.id = "shooter", .name = GS_TRANSLATE_NOOP("ProfileTemplates", "Shooter"), .icon = "crosshair"},
    ProfileTemplate{.id = "moba", .name = GS_TRANSLATE_NOOP("ProfileTemplates", "MOBA"), .icon = "target"},
    ProfileTemplate{.id = "racing", .name = GS_TRANSLATE_NOOP("ProfileTemplates", "Racing"), .icon = "gauge"},
    ProfileTemplate{.id = "action", .name = GS_TRANSLATE_NOOP("ProfileTemplates", "Action / RPG"), .icon = "zap"},
    ProfileTemplate{.id = "cards",
                    .name = GS_TRANSLATE_NOOP("ProfileTemplates", "Cards / Tapping"),
                    .icon = "mouse-pointer-click"},
    ProfileTemplate{.id = "blank", .name = GS_TRANSLATE_NOOP("ProfileTemplates", "Blank"), .icon = "plus"},
};

constexpr std::array kTemplateNames{
    GS_TRANSLATE_NOOP("ProfileTemplates", "Move"),       GS_TRANSLATE_NOOP("ProfileTemplates", "View"),
    GS_TRANSLATE_NOOP("ProfileTemplates", "Fire"),       GS_TRANSLATE_NOOP("ProfileTemplates", "Aim down sights"),
    GS_TRANSLATE_NOOP("ProfileTemplates", "Jump"),       GS_TRANSLATE_NOOP("ProfileTemplates", "Crouch"),
    GS_TRANSLATE_NOOP("ProfileTemplates", "Prone"),      GS_TRANSLATE_NOOP("ProfileTemplates", "Reload"),
    GS_TRANSLATE_NOOP("ProfileTemplates", "Interact"),   GS_TRANSLATE_NOOP("ProfileTemplates", "Grenade"),
    GS_TRANSLATE_NOOP("ProfileTemplates", "Bag"),        GS_TRANSLATE_NOOP("ProfileTemplates", "Map"),
    GS_TRANSLATE_NOOP("ProfileTemplates", "Attack"),     GS_TRANSLATE_NOOP("ProfileTemplates", "Skill 1"),
    GS_TRANSLATE_NOOP("ProfileTemplates", "Skill 2"),    GS_TRANSLATE_NOOP("ProfileTemplates", "Skill 3"),
    GS_TRANSLATE_NOOP("ProfileTemplates", "Spell"),      GS_TRANSLATE_NOOP("ProfileTemplates", "Recall"),
    GS_TRANSLATE_NOOP("ProfileTemplates", "Heal"),       GS_TRANSLATE_NOOP("ProfileTemplates", "Item"),
    GS_TRANSLATE_NOOP("ProfileTemplates", "Shop"),       GS_TRANSLATE_NOOP("ProfileTemplates", "Left"),
    GS_TRANSLATE_NOOP("ProfileTemplates", "Right"),      GS_TRANSLATE_NOOP("ProfileTemplates", "Accelerate"),
    GS_TRANSLATE_NOOP("ProfileTemplates", "Brake"),      GS_TRANSLATE_NOOP("ProfileTemplates", "Drift"),
    GS_TRANSLATE_NOOP("ProfileTemplates", "Nitro"),      GS_TRANSLATE_NOOP("ProfileTemplates", "Skill"),
    GS_TRANSLATE_NOOP("ProfileTemplates", "Burst"),      GS_TRANSLATE_NOOP("ProfileTemplates", "Dodge"),
    GS_TRANSLATE_NOOP("ProfileTemplates", "Member 1"),   GS_TRANSLATE_NOOP("ProfileTemplates", "Member 2"),
    GS_TRANSLATE_NOOP("ProfileTemplates", "Member 3"),   GS_TRANSLATE_NOOP("ProfileTemplates", "Member 4"),
    GS_TRANSLATE_NOOP("ProfileTemplates", "Card 1"),     GS_TRANSLATE_NOOP("ProfileTemplates", "Card 2"),
    GS_TRANSLATE_NOOP("ProfileTemplates", "Card 3"),     GS_TRANSLATE_NOOP("ProfileTemplates", "Card 4"),
};

QString toQString(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString pathText(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

std::filesystem::path toPath(const QString& text)
{
    return {text.toStdWString()};
}

std::string_view bytesOf(const QByteArray& bytes)
{
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

bool moveToTrash(const QString& path)
{
    return QFile::moveToTrash(path);
}

const std::filesystem::path& profileFile()
{
    static const std::filesystem::path name = "profile.json";
    return name;
}

const std::vector<std::string>& unpacked()
{
    static const std::vector<std::string> names{"node_modules"};
    return names;
}

bool hasSuffix(const QString& path, QStringView suffix)
{
    return path.endsWith(suffix, Qt::CaseInsensitive);
}

bool carriesLogic(const QString& source, bool folder)
{
    const std::filesystem::path path = toPath(source);
    if (folder) {
        std::error_code error;
        return !logicEntryOf(path).empty() || std::filesystem::is_directory(path / "components", error);
    }
    const ext::ArchiveListing listing = ext::inspectArchive(path);
    return std::ranges::any_of(listing.files, [](const ext::ArchiveEntry& entry) {
        return entry.path == "logic.ts" || entry.path == "logic.js" || entry.path.starts_with("components/");
    });
}

std::vector<std::filesystem::path> carriedComponents(const input::Profile* profile)
{
    std::vector<std::filesystem::path> folders;
    const ComponentLibrary* library = ComponentLibrary::instance();
    if (profile == nullptr || library == nullptr) {
        return folders;
    }
    for (const input::Control& control : profile->controls) {
        QString ref = QString::fromStdString(input::componentRef(control));
        for (int depth = 0; ref.startsWith(u"mine/") && depth < kMostBases; ++depth) {
            const ComponentEntry* entry = library->find(ref);
            if (entry == nullptr) {
                break;
            }
            const std::filesystem::path folder = library->folderOf(ref);
            if (!folder.empty() && std::ranges::find(folders, folder) == folders.end()) {
                folders.push_back(folder);
            }
            ref = entry->parent;
        }
    }
    return folders;
}

std::vector<ExportedComponent> carriedChain(const input::Profile* profile)
{
    std::vector<ExportedComponent> chain;
    const ComponentLibrary* library = ComponentLibrary::instance();
    if (profile == nullptr || library == nullptr) {
        return chain;
    }
    for (const input::Control& control : profile->controls) {
        QString ref = QString::fromStdString(input::componentRef(control));
        for (int depth = 0; ref.startsWith(u"mine/") && depth < kMostBases; ++depth) {
            const ComponentEntry* entry = library->find(ref);
            if (entry == nullptr) {
                break;
            }
            if (std::ranges::none_of(chain, [&ref](const ExportedComponent& each) { return each.ref == ref; })) {
                chain.push_back({.ref = ref, .folder = library->folderOf(ref)});
            }
            ref = entry->parent;
        }
    }
    return chain;
}

bool relinkBases(const std::vector<std::filesystem::path>& fresh, const std::map<std::string, std::string>& renamed)
{
    bool changed = false;
    for (const std::filesystem::path& folder : fresh) {
        QFile file(pathText(folder / "component.ts"));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        QString code = QString::fromUtf8(file.readAll());
        file.close();
        const QString original = code;
        for (const auto& [from, to] : renamed) {
            const QString old = QString::fromStdString(from).section(u'/', 1);
            const QString now = QString::fromStdString(to).section(u'/', 1);
            if (old == now) {
                continue;
            }
            for (const QChar quote : {u'"', u'\''}) {
                code.replace(quote + QStringLiteral("../") + old + u'/', quote + QStringLiteral("../") + now + u'/');
            }
        }
        if (code != original && !platform::writeFileAtomically(folder / "component.ts", code.toStdString())) {
            changed = true;
        }
    }
    return changed;
}

bool copyListed(const ext::ArchiveListing& listing, const std::filesystem::path& from, const std::filesystem::path& to)
{
    std::error_code error;
    for (const ext::ArchiveEntry& entry : listing.files) {
        const std::filesystem::path relative = toPath(QString::fromStdString(entry.path));
        std::filesystem::create_directories((to / relative).parent_path(), error);
        if (!error) {
            std::filesystem::copy_file(from / relative, to / relative, error);
        }
        if (error) {
            std::error_code ignored;
            std::filesystem::remove_all(to, ignored);
            return false;
        }
    }
    return true;
}

QVariantList markersOf(const input::Profile& profile, bool portrait)
{
    const input::ScreenGeometry screen = portrait ? input::ScreenGeometry{.width = kShortSide, .height = kLongSide}
                                                  : input::ScreenGeometry{.width = kLongSide, .height = kShortSide};
    QVariantList markers;
    for (const input::KeyHint& hint : input::keyHints(profile, screen, defaultLockKey())) {
        markers.append(QVariantMap{{QStringLiteral("type"), toQString(hint.type)},
                                   {QStringLiteral("x"), hint.center.x},
                                   {QStringLiteral("y"), hint.center.y},
                                   {QStringLiteral("rx"), hint.extent.x},
                                   {QStringLiteral("ry"), hint.extent.y}});
    }
    return markers;
}

QString limitedName(QString name)
{
    qsizetype points = 0;
    for (qsizetype i = 0; i < name.size(); ++i) {
        if (i > 0 && name[i].isLowSurrogate() && name[i - 1].isHighSurrogate()) {
            continue;
        }
        if (++points > kMostNameChars) {
            name.truncate(i);
            break;
        }
    }
    return name;
}

std::string templateText(const std::string& text)
{
    const bool known = std::ranges::any_of(kTemplateNames, [&](const char* name) { return text == name; });
    return known ? limitedName(QCoreApplication::translate(kTemplateContext, text.c_str())).toStdString() : text;
}

std::optional<input::Profile> templateProfile(std::string_view id)
{
    QFile file(QStringLiteral(":/templates/%1.json").arg(toQString(id)));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray bytes = file.read(kMostProfileBytes + 1);
    input::LoadResult loaded = input::loadProfile(bytesOf(bytes));
    if (!loaded.ok()) {
        return std::nullopt;
    }
    return std::move(loaded.profile);
}

bool reservedOnWindows(const QString& stem)
{
    const QString base = stem.section(u'.', 0, 0).trimmed().toLower();
    if (base == u"con" || base == u"prn" || base == u"aux" || base == u"nul") {
        return true;
    }
    return base.size() == 4 && (base.startsWith(u"com") || base.startsWith(u"lpt")) && base[3] >= u'1' &&
           base[3] <= u'9';
}

QString fileStem(const QString& name)
{
    const QString forbidden = QStringLiteral("<>:\"/\\|?*");
    QString stem;
    for (const QChar c : name) {
        stem.append(c.unicode() < 0x20 || forbidden.contains(c) ? QChar(u'-') : c);
    }
    if (stem.size() > kMostStemLength) {
        stem.truncate(kMostStemLength);
        if (stem.back().isHighSurrogate()) {
            stem.chop(1);
        }
    }
    stem = stem.trimmed();
    while (stem.endsWith(u'.')) {
        stem.chop(1);
        stem = stem.trimmed();
    }
    if (reservedOnWindows(stem)) {
        stem.prepend(u'_');
    }
    return stem;
}

int editsIn(const input::PatchOutcome& outcome, const QByteArray& patch)
{
    const QJsonObject document = QJsonDocument::fromJson(patch).object();
    int edits = static_cast<int>(outcome.touched.size());
    for (const QString& merged : {QStringLiteral("meta"), QStringLiteral("screen")}) {
        if (!document.value(merged).toObject().isEmpty()) {
            ++edits;
        }
    }
    if (document.contains(QStringLiteral("gamepad"))) {
        ++edits;
    }
    return edits;
}

}

ProfileLibrary::ProfileLibrary(std::filesystem::path directory, Discard discard, QObject* parent)
    : QObject(parent)
    , m_directory(std::move(directory))
    , m_discard(discard ? std::move(discard) : Discard(moveToTrash))
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
    std::error_code ignored;
    std::filesystem::create_directories(m_directory, ignored);
    std::vector<std::filesystem::path> leftovers;
    for (std::filesystem::directory_iterator it(m_directory, ignored);
         !ignored && it != std::filesystem::directory_iterator(); it.increment(ignored)) {
        std::error_code kind;
        if (it->is_directory(kind) && it->path().filename().wstring().starts_with(L".import-")) {
            leftovers.push_back(it->path());
        }
    }
    for (const std::filesystem::path& leftover : leftovers) {
        std::error_code removed;
        std::filesystem::remove_all(leftover, removed);
    }
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &ProfileLibrary::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &ProfileLibrary::reload);
    reload();
}

ProfileLibrary::~ProfileLibrary()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

ProfileLibrary* ProfileLibrary::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

QVariantList ProfileLibrary::entries() const
{
    return m_list;
}

QString ProfileLibrary::folder() const
{
    return pathText(m_directory);
}

const ProfileEntry* ProfileLibrary::find(const QString& id) const
{
    const auto found = std::ranges::find(m_entries, id, &ProfileEntry::id);
    return found != m_entries.end() ? &*found : nullptr;
}

std::shared_ptr<const input::Profile> ProfileLibrary::profile(const QString& id) const
{
    const ProfileEntry* entry = find(id);
    return entry != nullptr ? entry->profile : nullptr;
}

QString ProfileLibrary::neighbour(const QString& id, int step) const
{
    const ProfileEntry* current = find(id);
    std::vector<const ProfileEntry*> ring;
    for (const ProfileEntry& entry : m_entries) {
        if (entry.profile != nullptr && (current == nullptr || entry.app == current->app)) {
            ring.push_back(&entry);
        }
    }
    if (ring.empty()) {
        return {};
    }
    const auto at = std::ranges::find(ring, current);
    if (at == ring.end()) {
        return (step >= 0 ? ring.front() : ring.back())->id;
    }
    const auto size = static_cast<std::ptrdiff_t>(ring.size());
    const std::ptrdiff_t index = ((at - ring.begin()) + step % size + size) % size;
    return ring[static_cast<std::size_t>(index)]->id;
}

const ProfileEntry* ProfileLibrary::linkedTo(const QString& package) const
{
    if (package.isEmpty()) {
        return nullptr;
    }
    const auto found = std::ranges::find_if(
        m_entries, [&](const ProfileEntry& entry) { return entry.profile != nullptr && entry.app == package; });
    return found != m_entries.end() ? &*found : nullptr;
}

int ProfileLibrary::usableCount() const
{
    return static_cast<int>(
        std::ranges::count_if(m_entries, [](const ProfileEntry& entry) { return entry.profile != nullptr; }));
}

bool ProfileLibrary::isProfileFile(const QUrl& url) const
{
    if (!url.isLocalFile()) {
        return false;
    }
    const QString path = url.toLocalFile();
    if (QFileInfo(path).isDir()) {
        return QFileInfo::exists(QDir(path).filePath(QStringLiteral("profile.json")));
    }
    return hasSuffix(path, u".json") || hasSuffix(path, u".gsprofile");
}

QString ProfileLibrary::importFolder(const QString& source, const QStringList& reserved, QString& name,
                                     QString& reason)
{
    QFile file(QDir(source).filePath(QStringLiteral("profile.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        reason = tr("The folder has no readable profile.json.");
        return {};
    }
    const input::LoadResult loaded = input::loadProfile(bytesOf(file.read(kMostProfileBytes + 1)));
    if (!loaded.ok()) {
        reason = problemText(loaded);
        return {};
    }
    const std::filesystem::path from = toPath(source);
    const ext::ArchiveListing listing = ext::inspectDirectory(from, {}, unpacked());
    if (!listing.ok()) {
        reason = tr("The folder is too large or has files that cannot be copied.");
        return {};
    }
    const QString id = freeId(fileStem(QFileInfo(source).fileName()), reserved);
    if (!copyListed(listing, from, m_directory / toPath(id))) {
        reason = tr("The profile could not be saved.");
        return {};
    }
    name = loaded.profile.meta.name.empty() ? QFileInfo(source).fileName() : toQString(loaded.profile.meta.name);
    return id;
}

QString ProfileLibrary::importPackage(const QString& source, const QStringList& reserved, QString& name,
                                      QString& reason)
{
    const std::filesystem::path archive = toPath(source);
    const std::optional<std::string> json =
        ext::readArchiveFile(archive, "profile.json", static_cast<std::uint64_t>(kMostProfileBytes));
    if (!json) {
        reason = tr("The package has no readable profile.json.");
        return {};
    }
    const input::LoadResult loaded = input::loadProfile(*json);
    if (!loaded.ok()) {
        reason = problemText(loaded);
        return {};
    }
    const QString id = freeId(fileStem(QFileInfo(source).completeBaseName()), reserved);
    const std::filesystem::path staging = m_directory / toPath(QStringLiteral(".import-") + id);
    const std::filesystem::path target = m_directory / toPath(id);
    std::error_code error;
    std::filesystem::remove_all(staging, error);
    if (!ext::extractArchive(archive, staging).empty()) {
        std::error_code ignored;
        std::filesystem::remove_all(staging, ignored);
        reason = tr("The package cannot be opened.");
        return {};
    }
    std::filesystem::rename(staging, target, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove_all(staging, ignored);
        reason = tr("The profile could not be saved.");
        return {};
    }
    name = loaded.profile.meta.name.empty() ? QFileInfo(source).completeBaseName()
                                            : toQString(loaded.profile.meta.name);
    return id;
}

QVariantMap ProfileLibrary::importFiles(const QList<QUrl>& files, bool allowLogic)
{
    m_busy = false;
    const QString library = QDir(folder()).canonicalPath();
    QStringList imported;
    QStringList reserved;
    QVariantList failed;
    QVariantList logic;
    QStringList confirmed;
    bool wrote = false;
    for (const QUrl& url : files) {
        const QString source = url.toLocalFile();
        const QFileInfo info(source);
        const auto fail = [&](const QString& reason) {
            failed.append(QVariantMap{{QStringLiteral("file"), info.fileName()}, {QStringLiteral("reason"), reason}});
        };
        if (!url.isLocalFile()) {
            fail(tr("Only files on this computer can be imported."));
            continue;
        }
        if (info.isDir() || hasSuffix(source, u".gsprofile")) {
            if (info.isDir() && info.dir().canonicalPath() == library) {
                const ProfileEntry* entry = find(info.fileName());
                imported.append(entry != nullptr ? entry->name : info.fileName());
                continue;
            }
            const bool withLogic = carriesLogic(source, info.isDir());
            if (!allowLogic && withLogic) {
                logic.append(QVariantMap{{QStringLiteral("file"), info.fileName()}, {QStringLiteral("url"), url}});
                continue;
            }
            QString name;
            QString reason;
            const QString id = info.isDir() ? importFolder(source, reserved, name, reason)
                                            : importPackage(source, reserved, name, reason);
            if (id.isEmpty()) {
                fail(reason);
                continue;
            }
            adoptComponents(m_directory / toPath(id));
            reserved.append(id);
            imported.append(name);
            if (withLogic) {
                confirmed.append(id);
            }
            wrote = true;
            continue;
        }
        QFile file(source);
        if (!file.open(QIODevice::ReadOnly)) {
            fail(tr("The file cannot be read."));
            continue;
        }
        const QByteArray bytes = file.read(kMostProfileBytes + 1);
        const input::LoadResult loaded = input::loadProfile(bytesOf(bytes));
        if (!loaded.ok()) {
            fail(problemText(loaded));
            continue;
        }
        const QString name =
            loaded.profile.meta.name.empty() ? info.completeBaseName() : toQString(loaded.profile.meta.name);
        if (info.absoluteDir().canonicalPath() == library) {
            imported.append(name);
            continue;
        }
        const QString id = freeId(info.completeBaseName(), reserved);
        if (!write(m_directory / toPath(id + QStringLiteral(".json")), bytesOf(bytes))) {
            fail(tr("The profile could not be saved."));
            continue;
        }
        reserved.append(id);
        imported.append(name);
        wrote = true;
    }
    if (!confirmed.isEmpty()) {
        emit logicConfirmed(confirmed);
    }
    if (wrote) {
        reload();
    }
    return {{QStringLiteral("imported"), imported},
            {QStringLiteral("failed"), failed},
            {QStringLiteral("logic"), logic}};
}

QVariantList ProfileLibrary::templates() const
{
    QVariantList list;
    for (const ProfileTemplate& each : kTemplates) {
        const std::optional<input::Profile> profile = templateProfile(each.id);
        if (!profile) {
            continue;
        }
        QStringList includes;
        for (const input::Control& control : profile->controls) {
            includes.append(toQString(templateText(control.name)));
        }
        list.append(QVariantMap{{QStringLiteral("id"), toQString(each.id)},
                                {QStringLiteral("name"), QCoreApplication::translate(kTemplateContext, each.name)},
                                {QStringLiteral("icon"), toQString(each.icon)},
                                {QStringLiteral("controls"), static_cast<int>(profile->controls.size())},
                                {QStringLiteral("includes"), includes},
                                {QStringLiteral("markers"), markersOf(*profile, false)},
                                {QStringLiteral("portraitMarkers"), markersOf(*profile, true)}});
    }
    return list;
}

QString ProfileLibrary::createFromTemplate(const QString& templateId, const QString& name, const QString& app,
                                           bool portrait)
{
    const std::string wanted = templateId.toStdString();
    const auto chosen = std::ranges::find(kTemplates, std::string_view(wanted), &ProfileTemplate::id);
    if (chosen == kTemplates.end() || app.toUtf8().size() > kMostPackageBytes) {
        return {};
    }
    std::optional<input::Profile> profile = templateProfile(chosen->id);
    if (!profile) {
        return {};
    }
    for (input::Control& control : profile->controls) {
        control.name = templateText(control.name);
    }
    const QString title = name.trimmed();
    profile->meta.name =
        limitedName(title.isEmpty() ? QCoreApplication::translate(kTemplateContext, chosen->name) : title).toStdString();
    profile->meta.app = app.toStdString();
    profile->meta.orientation = portrait ? input::Orientation::Portrait : input::Orientation::Landscape;
    return add(*profile);
}

QString ProfileLibrary::copyName(const QString& name) const
{
    QString base = name;
    QString copy = tr("%1 copy").arg(base);
    while (copy.toUtf8().size() > kMostNameBytes && !base.isEmpty()) {
        base.chop(1);
        copy = tr("%1 copy").arg(base.trimmed());
    }
    return copy;
}

QString ProfileLibrary::add(const input::Profile& profile)
{
    m_busy = false;
    const std::string saved = input::saveProfile(profile);
    if (!input::loadProfile(saved).ok()) {
        return {};
    }
    const QString id = freeId(fileStem(toQString(profile.meta.name)));
    if (!write(m_directory / toPath(id + QStringLiteral(".json")), saved)) {
        return {};
    }
    reload();
    return id;
}

QString ProfileLibrary::duplicate(const QString& id)
{
    m_busy = false;
    const ProfileEntry* entry = find(id);
    if (entry == nullptr || entry->profile == nullptr) {
        return {};
    }
    const QString name = copyName(entry->name);
    input::Profile copy = *entry->profile;
    copy.meta.name = name.toStdString();
    const QString stem = entry->readOnly() ? entry->id.section(u'/', -1) : entry->id;
    const QString target = freeId(stem + QStringLiteral("-copy"));
    const std::string saved = input::saveProfile(copy);
    const std::filesystem::path& source = entry->folder.empty() ? entry->extensionFolder : entry->folder;
    if (!source.empty()) {
        const std::filesystem::path destination = m_directory / toPath(target);
        const ext::ArchiveListing listing = ext::inspectDirectory(source, {}, unpacked());
        if (!listing.ok() || !copyListed(listing, source, destination) ||
            !write(destination / profileFile(), saved)) {
            std::error_code ignored;
            std::filesystem::remove_all(destination, ignored);
            return {};
        }
        if (!entry->readOnly()) {
            emit profileDuplicated(entry->id, target);
        } else if (carriesLogic(pathText(destination), true)) {
            emit logicConfirmed({target});
        }
        reload();
        return target;
    }
    if (!write(m_directory / toPath(target + QStringLiteral(".json")), saved)) {
        return {};
    }
    reload();
    return target;
}

QString ProfileLibrary::ensureFolder(const QString& id)
{
    m_busy = false;
    const ProfileEntry* entry = find(id);
    if (entry == nullptr || entry->readOnly() || entry->profile == nullptr) {
        return {};
    }
    if (!entry->folder.empty()) {
        return pathText(entry->folder);
    }
    const auto contents = m_contents.find(id);
    if (contents == m_contents.end()) {
        return {};
    }
    const std::filesystem::path original = entry->path;
    const std::filesystem::path folder = m_directory / toPath(id);
    std::error_code error;
    const bool existed = std::filesystem::exists(folder, error);
    if (!error && !existed) {
        std::filesystem::create_directory(folder, error);
    }
    if (error) {
        return {};
    }
    if (!write(folder / profileFile(), bytesOf(contents->second))) {
        if (!existed) {
            std::error_code ignored;
            std::filesystem::remove(folder, ignored);
        }
        return {};
    }
    if (const std::error_code removed = platform::removeFile(original)) {
        qWarning("profile %s: now a folder; the old file could not be removed (error %d) and is ignored",
                 qUtf8Printable(pathText(original)), removed.value());
    }
    reload();
    return pathText(folder);
}

bool ProfileLibrary::setApp(const QString& id, const QString& package)
{
    m_busy = false;
    const ProfileEntry* entry = find(id);
    if (entry == nullptr || entry->readOnly() || entry->profile == nullptr ||
        package.toUtf8().size() > kMostPackageBytes) {
        return false;
    }
    input::Profile linked = *entry->profile;
    linked.meta.app = package.toStdString();
    const std::string saved = input::saveProfile(linked);
    if (!write(entry->path, saved)) {
        return false;
    }
    reload();
    return true;
}

bool ProfileLibrary::save(const QString& id, const input::Profile& profile)
{
    m_busy = false;
    const ProfileEntry* entry = find(id);
    if (entry == nullptr) {
        return false;
    }
    if (entry->readOnly()) {
        return savePatch(*entry, profile);
    }
    if (!write(entry->path, input::saveProfile(profile))) {
        return false;
    }
    reload();
    return true;
}

bool ProfileLibrary::restore(const QString& id)
{
    m_busy = false;
    const ProfileEntry* entry = find(id);
    if (entry == nullptr || !entry->readOnly() || !entry->userPatch) {
        return false;
    }
    const std::error_code error = platform::removeFile(patchPath(id));
    m_busy = platform::fileBusy(error);
    reload();
    return !error;
}

bool ProfileLibrary::remove(const QString& id)
{
    const ProfileEntry* entry = find(id);
    if (entry == nullptr || entry->readOnly()) {
        return false;
    }
    const bool removed = m_discard(pathText(entry->folder.empty() ? entry->path : entry->folder));
    if (removed) {
        emit profileRemoved(id);
    }
    reload();
    return removed;
}

bool ProfileLibrary::exportTo(const QString& id, const QUrl& target) const
{
    m_busy = false;
    const ProfileEntry* entry = find(id);
    const auto contents = m_contents.find(id);
    if (entry == nullptr || contents == m_contents.end() || !target.isLocalFile()) {
        return false;
    }
    const QString file = target.toLocalFile();
    if (!entry->folder.empty() && !hasSuffix(file, u".json")) {
        const std::vector<std::filesystem::path> carried = carriedComponents(entry->profile.get());
        if (carried.empty()) {
            return ext::packDirectory(entry->folder, toPath(file), {}, unpacked()).empty();
        }
        const QTemporaryDir temporary;
        if (!temporary.isValid()) {
            return false;
        }
        const std::filesystem::path staging = toPath(temporary.path()) / "profile";
        std::error_code error;
        std::filesystem::copy(entry->folder, staging, std::filesystem::copy_options::recursive, error);
        for (const std::filesystem::path& folder : carried) {
            if (!error) {
                std::filesystem::create_directories(staging / "components", error);
            }
            if (!error) {
                std::filesystem::copy(folder, staging / "components" / folder.filename(),
                                      std::filesystem::copy_options::recursive, error);
            }
        }
        return !error && ext::packDirectory(staging, toPath(file), {}, unpacked()).empty();
    }
    return write(toPath(file), bytesOf(contents->second));
}

bool ProfileLibrary::exportPackage(const QString& id, const QString& fileOrUrl) const
{
    m_busy = false;
    const ProfileEntry* entry = find(id);
    const std::optional<std::filesystem::path> target = exportTarget(fileOrUrl);
    if (entry == nullptr || entry->readOnly() || entry->profile == nullptr || !target) {
        return false;
    }
    const ExportedProfile exported{
        .id = entry->id, .name = entry->name, .folder = entry->folder, .profile = *entry->profile};
    return exportProfilePackage(exported, carriedChain(entry->profile.get()), *target);
}

QVariantList ProfileLibrary::usersOf(const QString& ref) const
{
    const std::string wanted = ref.toStdString();
    QVariantList users;
    for (const ProfileEntry& entry : m_entries) {
        if (entry.profile == nullptr) {
            continue;
        }
        const auto count = std::ranges::count_if(entry.profile->controls, [&](const input::Control& control) {
            return input::componentRef(control) == wanted;
        });
        if (count > 0) {
            users.append(QVariantMap{{QStringLiteral("id"), entry.id},
                                     {QStringLiteral("name"), entry.name},
                                     {QStringLiteral("count"), static_cast<int>(count)}});
        }
    }
    return users;
}

void ProfileLibrary::adoptComponents(const std::filesystem::path& folder)
{
    const std::filesystem::path carried = folder / "components";
    std::error_code error;
    if (!std::filesystem::is_directory(carried, error)) {
        return;
    }
    std::map<std::string, std::string> renamed;
    if (ComponentLibrary* library = ComponentLibrary::instance()) {
        std::vector<QString> before;
        for (const ComponentEntry& entry : library->entries()) {
            before.push_back(entry.ref);
        }
        std::vector<std::filesystem::path> fresh;
        for (std::filesystem::directory_iterator it(carried, error), end; !error && it != end; it.increment(error)) {
            std::error_code kind;
            if (!it->is_directory(kind)) {
                continue;
            }
            const QString installed = library->install(it->path());
            if (!installed.isEmpty()) {
                renamed.emplace("mine/" + pathText(it->path().filename()).toStdString(), installed.toStdString());
                if (std::ranges::find(before, installed) == before.end()) {
                    fresh.push_back(library->folderOf(installed));
                }
            }
        }
        if (relinkBases(fresh, renamed)) {
            library->reload();
        }
    }
    std::error_code ignored;
    std::filesystem::remove_all(carried, ignored);
    if (renamed.empty()) {
        return;
    }
    QFile file(QString::fromStdWString((folder / "profile.json").wstring()));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    input::LoadResult loaded = input::loadProfile(bytesOf(file.read(kMostProfileBytes + 1)));
    file.close();
    if (!loaded.ok()) {
        return;
    }
    bool changed = false;
    for (input::Control& control : loaded.profile.controls) {
        auto* placed = std::get_if<input::ComponentControl>(&control.kind);
        std::string& ref = placed != nullptr ? placed->component : control.component;
        if (const auto found = renamed.find(ref); found != renamed.end() && found->second != ref) {
            ref = found->second;
            changed = true;
        }
    }
    if (changed) {
        static_cast<void>(write(folder / "profile.json", input::saveProfile(loaded.profile)));
    }
}

bool ProfileLibrary::write(const std::filesystem::path& path, std::string_view contents) const
{
    const std::error_code error = platform::writeFileAtomically(path, contents);
    m_busy = platform::fileBusy(error);
    return !error;
}

QString ProfileLibrary::exportName(const QString& id) const
{
    const ProfileEntry* entry = find(id);
    if (entry == nullptr) {
        return {};
    }
    return entry->id.section(u'/', -1) + (entry->folder.empty() ? QStringLiteral(".json") : QStringLiteral(".gsprofile"));
}

void ProfileLibrary::reveal(const QString& id) const
{
    if (const ProfileEntry* entry = find(id); entry != nullptr && !entry->readOnly()) {
        const std::filesystem::path& shown = entry->folder.empty() ? entry->path : entry->folder;
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,"), QDir::toNativeSeparators(pathText(shown))});
    }
}

void ProfileLibrary::openFolder() const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder()));
}

void ProfileLibrary::setExtensionProfiles(std::vector<ExtensionProfile> profiles)
{
    if (profiles == m_extensionProfiles) {
        return;
    }
    m_extensionProfiles = std::move(profiles);
    reload();
}

void ProfileLibrary::reload()
{
    std::vector<ProfileEntry> entries;
    std::map<QString, QByteArray> contents;
    std::vector<std::filesystem::path> files;
    std::vector<std::filesystem::path> incomplete;
    std::error_code error;
    for (std::filesystem::directory_iterator it(m_directory, error), end; !error && it != end; it.increment(error)) {
        const std::filesystem::path& path = it->path();
        const std::wstring leaf = path.filename().wstring();
        std::error_code kind;
        if (it->is_directory(kind)) {
            std::error_code missing;
            if (leaf.starts_with(L'.') || leaf == L"patches") {
                continue;
            }
            if (!std::filesystem::is_regular_file(path / profileFile(), missing)) {
                incomplete.push_back(path);
                continue;
            }
            QByteArray bytes;
            ProfileEntry entry = read(path / profileFile(), QString::fromStdWString(leaf), bytes);
            entry.folder = path;
            entry.logic = !logicEntryOf(path).empty();
            entry.modified = QFileInfo(pathText(path / profileFile())).lastModified();
            contents.emplace(entry.id, std::move(bytes));
            entries.push_back(std::move(entry));
        } else if (it->is_regular_file(kind) &&
                   QString::fromStdWString(path.extension().wstring()).compare(u".json", Qt::CaseInsensitive) == 0) {
            files.push_back(path);
        }
    }
    for (const std::filesystem::path& path : files) {
        const QString id = QString::fromStdWString(path.stem().wstring());
        if (contents.contains(id)) {
            qWarning("profile %s: a folder with the same name is used instead", qUtf8Printable(pathText(path)));
            continue;
        }
        QByteArray bytes;
        ProfileEntry entry = read(path, id, bytes);
        entry.modified = QFileInfo(pathText(path)).lastModified();
        contents.emplace(entry.id, std::move(bytes));
        entries.push_back(std::move(entry));
    }
    std::vector<ProfileEntry> contributed;
    for (const ExtensionProfile& source : m_extensionProfiles) {
        ProfileEntry entry;
        entry.id = source.id;
        entry.name = source.id.section(u'/', -1);
        entry.extensionId = source.extensionId;
        entry.extensionName = source.extensionName;
        entry.version = source.version;
        entry.patchedBy = source.patchedBy;
        entry.conflicts = source.conflicts;
        entry.extensionFolder = source.folder;
        entry.logic = !source.folder.empty() && !logicEntryOf(source.folder).empty();
        QByteArray shown = source.json;
        applyUserPatch(entry, shown);
        describe(entry, shown);
        contents.emplace(entry.id, std::move(shown));
        contributed.push_back(std::move(entry));
    }
    const auto byName = [](const ProfileEntry& a, const ProfileEntry& b) {
        const int order = QString::localeAwareCompare(a.name, b.name);
        return order != 0 ? order < 0 : a.id < b.id;
    };
    std::ranges::sort(entries, byName);
    std::ranges::sort(contributed, byName);
    std::ranges::move(contributed, std::back_inserter(entries));

    QStringList changed;
    for (const auto& [id, bytes] : contents) {
        const auto before = m_contents.find(id);
        if (before != m_contents.end() && before->second != bytes) {
            changed.append(id);
        }
    }
    for (ProfileEntry& entry : entries) {
        const ProfileEntry* old = find(entry.id);
        const auto before = m_contents.find(entry.id);
        if (entry.profile != nullptr && old != nullptr && old->profile != nullptr && old->path == entry.path &&
            before != m_contents.end() && before->second == contents[entry.id]) {
            entry.profile = old->profile;
        }
    }
    m_entries = std::move(entries);
    m_contents = std::move(contents);
    m_list.clear();
    for (const ProfileEntry& entry : m_entries) {
        m_list.append(QVariantMap{{QStringLiteral("id"), entry.id},
                                  {QStringLiteral("name"), entry.name},
                                  {QStringLiteral("app"), entry.app},
                                  {QStringLiteral("portrait"), entry.portrait},
                                  {QStringLiteral("controls"), entry.controls},
                                  {QStringLiteral("warnings"), entry.warnings},
                                  {QStringLiteral("error"), entry.error},
                                  {QStringLiteral("markers"), entry.markers},
                                  {QStringLiteral("modified"), entry.modified},
                                  {QStringLiteral("readOnly"), entry.readOnly()},
                                  {QStringLiteral("extensionName"), entry.extensionName},
                                  {QStringLiteral("version"), entry.version},
                                  {QStringLiteral("patchedBy"), entry.patchedBy},
                                  {QStringLiteral("conflicts"), entry.conflicts},
                                  {QStringLiteral("userPatch"), entry.userPatch},
                                  {QStringLiteral("userEdits"), entry.userEdits},
                                  {QStringLiteral("userPatchProblem"), entry.userPatchProblem},
                                  {QStringLiteral("folder"), !entry.folder.empty()},
                                  {QStringLiteral("logic"), entry.logic}});
    }

    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty()) {
        m_watcher.removePaths(watched);
    }
    m_watcher.addPath(folder());
    for (const std::filesystem::path& waiting : incomplete) {
        m_watcher.addPath(pathText(waiting));
    }
    for (const ProfileEntry& entry : m_entries) {
        if (!entry.readOnly()) {
            m_watcher.addPath(pathText(entry.path));
        }
        if (!entry.folder.empty()) {
            m_watcher.addPath(pathText(entry.folder));
        }
    }
    emit entriesChanged();
    for (const QString& id : changed) {
        emit profileChanged(id);
    }
}

ProfileEntry ProfileLibrary::read(const std::filesystem::path& path, const QString& id, QByteArray& contents) const
{
    ProfileEntry entry;
    entry.path = path;
    entry.id = id;
    entry.name = entry.id;
    QFile file(pathText(path));
    if (!file.open(QIODevice::ReadOnly)) {
        entry.error = tr("The file cannot be read.");
        return entry;
    }
    contents = file.read(kMostProfileBytes + 1);
    describe(entry, contents);
    return entry;
}

void ProfileLibrary::describe(ProfileEntry& entry, const QByteArray& contents) const
{
    const input::LoadResult loaded = input::loadProfile(bytesOf(contents));
    if (!loaded.profile.meta.name.empty()) {
        entry.name = toQString(loaded.profile.meta.name);
    }
    entry.app = toQString(loaded.profile.meta.app);
    entry.portrait = loaded.profile.meta.orientation == input::Orientation::Portrait;
    entry.controls = static_cast<int>(loaded.profile.controls.size());
    entry.warnings = static_cast<int>(std::ranges::count_if(
        loaded.diagnostics, [](const input::Diagnostic& d) { return d.severity == input::Severity::Warning; }));
    if (!loaded.ok()) {
        entry.error = problemText(loaded);
        return;
    }
    entry.profile = std::make_shared<const input::Profile>(loaded.profile);
    entry.markers = markersOf(*entry.profile, entry.portrait);
}

std::filesystem::path ProfileLibrary::patchPath(const QString& id) const
{
    const qsizetype slash = id.lastIndexOf(u'/');
    if (slash <= 0 || slash + 1 >= id.size()) {
        return {};
    }
    return m_directory / "patches" / toPath(id.left(slash)) / toPath(id.mid(slash + 1) + QStringLiteral(".json"));
}

void ProfileLibrary::applyUserPatch(ProfileEntry& entry, QByteArray& contents) const
{
    const std::filesystem::path path = patchPath(entry.id);
    if (path.empty()) {
        return;
    }
    QFile file(pathText(path));
    if (!file.exists()) {
        return;
    }
    entry.userPatch = true;
    entry.modified = QFileInfo(file).lastModified();
    if (!file.open(QIODevice::ReadOnly)) {
        entry.userPatchProblem = tr("Your changes to this profile cannot be read.");
        return;
    }
    const QByteArray patch = file.read(kMostProfileBytes + 1);
    const input::PatchOutcome outcome = input::applyPatch(bytesOf(contents), bytesOf(patch));
    if (!outcome.applied) {
        entry.userPatchProblem = tr("Your changes no longer fit this profile.");
        return;
    }
    contents = QByteArray::fromStdString(outcome.profile);
    entry.userEdits = editsIn(outcome, patch);
}

bool ProfileLibrary::savePatch(const ProfileEntry& entry, const input::Profile& profile)
{
    const auto source = std::ranges::find(m_extensionProfiles, entry.id, &ExtensionProfile::id);
    const std::filesystem::path path = patchPath(entry.id);
    if (source == m_extensionProfiles.end() || path.empty()) {
        return false;
    }
    const input::LoadResult base = input::loadProfile(bytesOf(source->json));
    if (!base.ok()) {
        return false;
    }
    const input::ProfileDiff diff = input::diffProfiles(base.profile, profile);
    if (!diff.exact) {
        return false;
    }
    std::error_code error;
    if (diff.empty) {
        std::filesystem::remove(path, error);
        reload();
        return !error;
    }
    const input::PatchOutcome outcome = input::applyPatch(bytesOf(source->json), diff.patch);
    const input::LoadResult wanted = input::loadProfile(input::saveProfile(profile));
    if (!outcome.applied || !wanted.ok() || input::loadProfile(outcome.profile).profile != wanted.profile) {
        return false;
    }
    std::filesystem::create_directories(path.parent_path(), error);
    if (error || !write(path, diff.patch)) {
        return false;
    }
    reload();
    return true;
}

QString ProfileLibrary::freeId(const QString& base, const QStringList& reserved) const
{
    const QString stem = base.isEmpty() ? QStringLiteral("profile") : base;
    const auto taken = [&](const QString& id) {
        return QFileInfo::exists(pathText(m_directory / toPath(id + QStringLiteral(".json")))) ||
               QFileInfo::exists(pathText(m_directory / toPath(id))) || reserved.contains(id, Qt::CaseInsensitive);
    };
    if (!taken(stem)) {
        return stem;
    }
    for (int n = 2;; ++n) {
        const QString candidate = stem + u'-' + QString::number(n);
        if (!taken(candidate)) {
            return candidate;
        }
    }
}

std::string logicEntryOf(const std::filesystem::path& folder)
{
    for (const char* name : {"logic.ts", "logic.js"}) {
        std::error_code error;
        if (std::filesystem::is_regular_file(folder / name, error)) {
            return name;
        }
    }
    return script::controlModules(folder).empty() ? std::string() : std::string("logic.ts");
}

ProfileChoice chooseForApp(const ProfileLibrary& library,
                           const std::map<std::string, std::string, std::less<>>& remembered,
                           const QString& package, const QString& current)
{
    if (package.isEmpty()) {
        return {};
    }
    QString chosen;
    bool decided = false;
    if (const auto found = remembered.find(package.toStdString()); found != remembered.end()) {
        const QString id = QString::fromStdString(found->second);
        if (id.isEmpty() || library.profile(id) != nullptr) {
            chosen = id;
            decided = true;
        }
    }
    if (!decided) {
        if (const ProfileEntry* linked = library.linkedTo(package)) {
            chosen = linked->id;
            decided = true;
        }
    }
    if (!decided) {
        const ProfileEntry* inUse = library.find(current);
        if (inUse == nullptr || inUse->app.isEmpty()) {
            return {};
        }
    }
    if (chosen == current) {
        return {};
    }
    return {.change = true, .id = chosen};
}

bool offersStart(const ProfileLibrary& library, const std::map<std::string, std::string, std::less<>>& remembered,
                 const std::vector<std::string>& closed, const QString& package, bool game)
{
    if (package.isEmpty() || library.linkedTo(package) != nullptr) {
        return false;
    }
    const std::string key = package.toStdString();
    if (std::ranges::find(closed, key) != closed.end()) {
        return false;
    }
    if (const auto found = remembered.find(key); found != remembered.end()) {
        return !found->second.empty() && library.profile(QString::fromStdString(found->second)) == nullptr;
    }
    return game;
}

QString ProfileLibrary::problemText(const input::LoadResult& loaded) const
{
    const auto error = std::ranges::find_if(
        loaded.diagnostics, [](const input::Diagnostic& d) { return d.severity == input::Severity::Error; });
    if (error == loaded.diagnostics.end()) {
        return tr("The profile cannot be used.");
    }
    switch (error->code) {
    case input::DiagnosticCode::TooLarge:
        return tr("The file is too large for a profile.");
    case input::DiagnosticCode::MalformedJson:
        return tr("The file is not valid JSON.");
    case input::DiagnosticCode::WrongFormat:
        return tr("This is not a GameScrcpy key mapping profile.");
    case input::DiagnosticCode::WrongVersion:
        return tr("The profile was made for another version of the format.");
    default:
        break;
    }
    return tr("Something at %1 is not valid.").arg(toQString(error->path));
}

}
