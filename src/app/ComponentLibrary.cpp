#include "ComponentLibrary.h"

#include "ExtensionExport.h"
#include "TranslateNoop.h"

#include <gs/input/Profile.h>
#include <gs/platform/AtomicFile.h>
#include <gs/script/ApiFiles.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

ComponentLibrary* s_instance = nullptr;
constexpr int kSettleMs = 300;
constexpr std::size_t kMostFileBytes = std::size_t{1} << 20;
constexpr int kMostFolderName = 64;
constexpr int kMostName = 64;
constexpr const char* kInfoFile = "component.json";

struct Example {
    const char* id;
    const char* name;
    const char* description;
    const char* icon;
};

constexpr std::array<Example, 5> kExamples{{
    {"toggle-hold", GS_TRANSLATE_NOOP("ComponentLibrary", "Toggle hold"),
     GS_TRANSLATE_NOOP("ComponentLibrary", "Press to hold, press again to let go"), "pin"},
    {"double-tap", GS_TRANSLATE_NOOP("ComponentLibrary", "Double tap"),
     GS_TRANSLATE_NOOP("ComponentLibrary", "Taps twice for each press"), "mouse-pointer-click"},
    {"burst", GS_TRANSLATE_NOOP("ComponentLibrary", "Burst"),
     GS_TRANSLATE_NOOP("ComponentLibrary", "Taps several times for each press"), "zap"},
    {"in-turn", GS_TRANSLATE_NOOP("ComponentLibrary", "Tap in turn"),
     GS_TRANSLATE_NOOP("ComponentLibrary", "Each press taps the next of a few places"), "refresh-cw"},
    {"show-all-hints", GS_TRANSLATE_NOOP("ComponentLibrary", "Show all key hints while held"),
     GS_TRANSLATE_NOOP("ComponentLibrary", "Hold to see every key at full opacity"), "eye"},
}};

constexpr std::array<const char*, 5> kExampleLabels{
    GS_TRANSLATE_NOOP("ComponentLibrary", "Key"), GS_TRANSLATE_NOOP("ComponentLibrary", "Gap (ms)"),
    GS_TRANSLATE_NOOP("ComponentLibrary", "Times"), GS_TRANSLATE_NOOP("ComponentLibrary", "Second place"),
    GS_TRANSLATE_NOOP("ComponentLibrary", "Third place")};

constexpr std::array<const char*, 16> kIcons{"sparkles", "mouse-pointer-click", "pointer",   "pin",
                                             "timer",    "tap-echo",            "zap",       "refresh-cw",
                                             "target",   "crosshair",           "move",      "eye",
                                             "keyboard", "swipe-path",          "layers",    "bot"};

QString translated(const char* text)
{
    return QCoreApplication::translate("ComponentLibrary", text);
}

std::filesystem::path toPath(const QString& text)
{
    return {text.toStdWString()};
}

QString pathText(const std::filesystem::path& path)
{
    return QDir::fromNativeSeparators(QString::fromStdWString(path.wstring()));
}

QString readText(const std::filesystem::path& path)
{
    std::string contents;
    if (platform::readFile(path, kMostFileBytes, contents)) {
        return {};
    }
    return QString::fromUtf8(contents);
}

bool knownIcon(const QString& icon)
{
    return std::ranges::any_of(kIcons, [&](const char* each) { return icon == QLatin1StringView(each); });
}

QString folderFor(const QString& name)
{
    QString id;
    for (const QChar c : name.trimmed()) {
        if (c.isLetterOrNumber()) {
            id += c.toLower();
        } else if (!id.isEmpty() && !id.endsWith(u'-')) {
            id += u'-';
        }
    }
    while (id.endsWith(u'-')) {
        id.chop(1);
    }
    return id.isEmpty() ? QStringLiteral("component") : id.left(kMostFolderName);
}

QString starterComponent(const QString& keyLabel)
{
    QString label = keyLabel;
    label.replace(u'\\', QStringLiteral("\\\\"));
    label.replace(u'"', QStringLiteral("\\\""));
    return QStringLiteral("export default class MyComponent extends Component {\n"
                          "    static props = {\n"
                          "        key: prop.key(\"F\", { label: \"%1\" }),\n"
                          "    };\n"
                          "\n"
                          "    setup() {\n"
                          "        this.props.key.onPress(async () => {\n"
                          "            const at = this.position;\n"
                          "            if (at !== null) {\n"
                          "                await touch.tap(at);\n"
                          "            }\n"
                          "        });\n"
                          "    }\n"
                          "}\n")
        .arg(label);
}

QString classNameFor(const QString& folder, const QString& taken)
{
    QString name;
    bool upper = true;
    for (const QChar c : folder) {
        if (!c.isLetter() && !(c.isDigit() && c.unicode() < 0x80)) {
            upper = true;
            continue;
        }
        name += upper ? c.toUpper() : c;
        upper = false;
    }
    if (name.isEmpty() || name.front().isDigit() || name == u"Component" || name == u"Task" ||
        !builtInType(name).isEmpty() || name == taken) {
        name.prepend(QStringLiteral("My"));
    }
    return name == taken ? name + u'2' : name;
}

QString firstEventOf(const QString& type)
{
    if (type == u"hold") {
        return QStringLiteral("onPressed");
    }
    if (type == u"longPress" || type == u"swipe" || type == u"freeLook") {
        return QStringLiteral("onStart");
    }
    if (type == u"wasd" || type == u"padStick") {
        return QStringLiteral("onMoveStart");
    }
    if (type == u"rightWalk") {
        return QStringLiteral("onWalkStart");
    }
    if (type == u"camera" || type == u"padCamera") {
        return QStringLiteral("onTurnStart");
    }
    if (type == u"skill") {
        return QStringLiteral("onAimStart");
    }
    if (type == u"dial") {
        return QStringLiteral("onChoose");
    }
    if (type == u"multiPress") {
        return QStringLiteral("onPressed");
    }
    if (type == u"tempCursor") {
        return QStringLiteral("onShow");
    }
    if (type == u"actionWheel") {
        return QStringLiteral("onChoose");
    }
    return type.isEmpty() ? QStringLiteral("setup") : QStringLiteral("onPress");
}

QString extendedComponent(const QString& name, const QString& type)
{
    return QStringLiteral("export default class %1 extends %2 {\n"
                          "    %3() {\n"
                          "        log.info(this.name);\n"
                          "    }\n"
                          "}\n")
        .arg(name, builtInClass(type), firstEventOf(type));
}

QString derivedComponent(const QString& name, const QString& base, const QString& folder, const QString& type)
{
    return QStringLiteral("import %2 from \"../%4/component.ts\";\n"
                          "\n"
                          "export default class %1 extends %2 {\n"
                          "    %3() {\n"
                          "        super.%3();\n"
                          "        log.info(this.name);\n"
                          "    }\n"
                          "}\n")
        .arg(name, base, firstEventOf(type), folder);
}

QString siblingOf(const QString& from)
{
    static const QRegularExpression sibling(QStringLiteral(R"(^\.\./([^/\\]+)/component\.ts$)"));
    const QRegularExpressionMatch match = sibling.match(from);
    return match.hasMatch() && match.captured(1) != u"." && match.captured(1) != u".." ? match.captured(1) : QString();
}

std::vector<DeclaredSpec> mergedProps(const std::vector<DeclaredSpec>& base, const std::vector<DeclaredSpec>& own)
{
    std::vector<DeclaredSpec> merged = base;
    for (const DeclaredSpec& spec : own) {
        const auto same = std::ranges::find(merged, spec.name, &DeclaredSpec::name);
        if (same != merged.end()) {
            *same = spec;
        } else {
            merged.push_back(spec);
        }
    }
    return merged;
}

void resolveBases(std::vector<ComponentEntry>& entries)
{
    std::vector<bool> done(entries.size(), false);
    const auto indexOf = [&entries](const QString& ref) {
        return static_cast<std::size_t>(
            std::distance(entries.begin(), std::ranges::find(entries, ref, &ComponentEntry::ref)));
    };
    for (std::size_t i = 0; i < entries.size(); ++i) {
        std::vector<std::size_t> chain{i};
        while (!done[chain.back()] && !entries[chain.back()].parent.isEmpty()) {
            const std::size_t next = indexOf(entries[chain.back()].parent);
            if (next >= entries.size() || std::ranges::find(chain, next) != chain.end()) {
                break;
            }
            chain.push_back(next);
        }
        for (std::size_t k = chain.size(); k-- > 0;) {
            if (done[chain[k]]) {
                continue;
            }
            ComponentEntry& entry = entries[chain[k]];
            entry.type = builtInType(entry.base);
            if (k + 1 < chain.size()) {
                const ComponentEntry& base = entries[chain[k + 1]];
                entry.type = base.type;
                entry.props = mergedProps(base.props, entry.props);
                entry.children.insert(entry.children.begin(), base.children.begin(), base.children.end());
            }
            done[chain[k]] = true;
        }
    }
    for (ComponentEntry& entry : entries) {
        const std::vector<std::string_view> builtInKeys = input::inputSlots(entry.type.toStdString());
        std::erase_if(entry.props, [&builtInKeys](const DeclaredSpec& spec) {
            return spec.kind == u"key" && std::ranges::any_of(builtInKeys, [&spec](std::string_view key) {
                       return spec.name == QLatin1StringView(key.data(), static_cast<qsizetype>(key.size()));
                   });
        });
    }
}

QByteArray stampOf(const std::filesystem::path& folder)
{
    std::error_code error;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::recursive_directory_iterator it(folder, error), end; !error && it != end;
         it.increment(error)) {
        const std::filesystem::path extension = it->path().extension();
        if (it->is_regular_file(error) &&
            (extension == L".ts" || extension == L".js" || extension == L".mts" || extension == L".mjs")) {
            files.push_back(it->path());
        }
    }
    std::ranges::sort(files);
    QCryptographicHash hash(QCryptographicHash::Sha1);
    for (const std::filesystem::path& file : files) {
        hash.addData(pathText(file.lexically_relative(folder)).toUtf8());
        std::string contents;
        if (!platform::readFile(file, kMostFileBytes, contents)) {
            hash.addData(QByteArrayView(contents.data(), static_cast<qsizetype>(contents.size())));
        }
    }
    return hash.result();
}

QVariantMap itemOf(const ComponentEntry& entry)
{
    QVariantList props;
    for (const DeclaredSpec& spec : entry.props) {
        props.append(QVariantMap{{QStringLiteral("name"), spec.name},
                                 {QStringLiteral("kind"), spec.kind},
                                 {QStringLiteral("label"), spec.label.isEmpty() ? spec.name : spec.label},
                                 {QStringLiteral("value"), spec.value}});
    }
    return {{QStringLiteral("ref"), entry.ref},
            {QStringLiteral("name"), entry.name},
            {QStringLiteral("description"), entry.description},
            {QStringLiteral("icon"), entry.icon},
            {QStringLiteral("category"), entry.category},
            {QStringLiteral("folder"), entry.folder},
            {QStringLiteral("example"), entry.example},
            {QStringLiteral("type"), entry.type},
            {QStringLiteral("parent"), entry.parent},
            {QStringLiteral("props"), props},
            {QStringLiteral("extension"), entry.extensionName},
            {QStringLiteral("extensionId"), entry.extensionId}};
}

std::optional<ComponentEntry> readComponent(const std::filesystem::path& folder, const QString& ref, bool example,
                                            bool builtIn)
{
    const QString code = readText(folder / toPath(ComponentLibrary::entryName()));
    const QString id = pathText(folder.filename());
    ComponentEntry entry{.ref = ref, .folder = pathText(folder), .example = example, .props = componentPropsIn(code)};
    if (code.isEmpty() || !input::isComponentRef(entry.ref.toStdString())) {
        return std::nullopt;
    }
    const DefaultClass head = defaultClassIn(code);
    entry.className = head.name;
    if (head.from.isEmpty()) {
        entry.base = head.base;
    } else if (const QString sibling = siblingOf(head.from); !sibling.isEmpty()) {
        entry.parent = (example ? QStringLiteral("examples/") : QStringLiteral("mine/")) + sibling;
    }
    for (const auto& [child, className] : childCallsIn(head.body)) {
        entry.children.push_back({.name = child, .type = builtInType(className)});
    }
    const QJsonObject info = QJsonDocument::fromJson(readText(folder / kInfoFile).toUtf8()).object();
    if (builtIn) {
        for (DeclaredSpec& spec : entry.props) {
            const std::string label = spec.label.toStdString();
            if (std::ranges::any_of(kExampleLabels, [&](const char* each) { return label == each; })) {
                spec.label = translated(label.c_str());
            }
        }
        const auto known =
            std::ranges::find_if(kExamples, [&](const Example& each) { return id == QLatin1StringView(each.id); });
        if (known != kExamples.end()) {
            entry.name = translated(known->name);
            entry.description = translated(known->description);
            entry.icon = QString::fromLatin1(known->icon);
        }
    }
    if (entry.name.isEmpty()) {
        entry.name = info.value(QStringLiteral("name")).toString(id).left(kMostName);
    }
    if (entry.description.isEmpty()) {
        entry.description = info.value(QStringLiteral("description")).toString();
    }
    if (entry.icon.isEmpty()) {
        entry.icon = info.value(QStringLiteral("icon")).toString();
    }
    if (!knownIcon(entry.icon)) {
        entry.icon = QStringLiteral("sparkles");
    }
    entry.category = info.value(QStringLiteral("category")).toString();
    return entry;
}

void relinkSiblings(const std::filesystem::path& file, const std::vector<std::pair<QString, QString>>& renamed)
{
    QString code = readText(file);
    const QString original = code;
    for (const auto& [from, to] : renamed) {
        if (from == to) {
            continue;
        }
        for (const QChar quote : {u'"', u'\''}) {
            code.replace(quote + QStringLiteral("../") + from + u'/', quote + QStringLiteral("../") + to + u'/');
        }
    }
    if (code != original) {
        static_cast<void>(platform::writeFileAtomically(file, code.toStdString()));
    }
}

}

std::vector<DeclaredSpec> componentPropsIn(const QString& code)
{
    return declaredIn(code, QStringLiteral("props"), QStringLiteral("prop"),
                      {QStringLiteral("key"), QStringLiteral("position"), QStringLiteral("number"),
                       QStringLiteral("text"), QStringLiteral("toggle"), QStringLiteral("component")});
}

ComponentLibrary::ComponentLibrary(std::filesystem::path examples, std::filesystem::path mine, QObject* parent)
    : QObject(parent)
    , m_examplesDirectory(std::move(examples))
    , m_mineDirectory(std::move(mine))
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
    std::error_code ignored;
    std::filesystem::create_directories(m_mineDirectory, ignored);
    m_settle.setInterval(kSettleMs);
    m_settle.setSingleShot(true); // source-rule-allow R9: a debounce; editors write a file in several steps
    connect(&m_settle, &QTimer::timeout, this, &ComponentLibrary::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_settle.start(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_settle.start(); });
    reload();
}

ComponentLibrary::~ComponentLibrary()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

ComponentLibrary* ComponentLibrary::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

ComponentLibrary* ComponentLibrary::instance()
{
    return s_instance;
}

QStringList ComponentLibrary::icons()
{
    QStringList list;
    for (const char* icon : kIcons) {
        list.append(QString::fromLatin1(icon));
    }
    return list;
}

const ComponentEntry* ComponentLibrary::find(const QString& ref) const
{
    const auto found = std::ranges::find(m_entries, ref, &ComponentEntry::ref);
    return found != m_entries.end() ? &*found : nullptr;
}

std::filesystem::path ComponentLibrary::folderOf(const QString& ref) const
{
    const ComponentEntry* entry = find(ref);
    return entry != nullptr ? toPath(entry->folder) : std::filesystem::path();
}

bool ComponentLibrary::extends(const QString& ref, const QString& base) const
{
    QString at = ref;
    for (std::size_t step = 0; !at.isEmpty() && step <= m_entries.size(); ++step) {
        if (at == base) {
            return true;
        }
        const ComponentEntry* entry = find(at);
        if (entry == nullptr) {
            return false;
        }
        at = entry->parent;
    }
    return false;
}

QVariantMap ComponentLibrary::info(const QString& ref) const
{
    const ComponentEntry* entry = find(ref);
    return entry != nullptr ? itemOf(*entry) : QVariantMap();
}

QString ComponentLibrary::add(const QString& name, const QString& base)
{
    const ComponentEntry* parent = base.contains(u'/') ? find(base) : nullptr;
    const QString builtIn = base.contains(u'/') ? QString() : builtInClass(base);
    if (!base.isEmpty() && (parent == nullptr || parent->example) && builtIn.isEmpty()) {
        return {};
    }
    const QString title = freeName(name.trimmed().isEmpty() ? tr("New component") : name.trimmed().left(kMostName));
    const QString id = freeFolder(folderFor(title));
    QString code = starterComponent(tr("Key"));
    if (parent != nullptr) {
        const QString baseClass = parent->className.isEmpty() ? QStringLiteral("Base") : parent->className;
        code = derivedComponent(classNameFor(id, baseClass), baseClass, parent->ref.section(u'/', 1), parent->type);
    } else if (!builtIn.isEmpty()) {
        code = extendedComponent(classNameFor(id, builtIn), base);
    }
    const std::filesystem::path folder = m_mineDirectory / toPath(id);
    std::error_code error;
    if (!std::filesystem::create_directories(folder, error) || error) {
        return {};
    }
    const ComponentEntry entry{.ref = QStringLiteral("mine/") + id, .name = title, .icon = QStringLiteral("sparkles")};
    if (platform::writeFileAtomically(folder / toPath(entryName()), code.toStdString()) ||
        platform::writeFileAtomically(folder / "tsconfig.json", script::profileTsconfig()) ||
        !writeInfo(folder, entry)) {
        std::filesystem::remove_all(folder, error);
        return {};
    }
    reload();
    return entry.ref;
}

QString ComponentLibrary::copyToMine(const QString& ref)
{
    const ComponentEntry* source = find(ref);
    if (source == nullptr || !source->example) {
        return {};
    }
    std::vector<const ComponentEntry*> chain{source};
    for (const ComponentEntry* base = find(source->parent);
         base != nullptr && base->example && chain.size() <= m_entries.size(); base = find(base->parent)) {
        if (std::ranges::find(chain, base) != chain.end()) {
            break;
        }
        chain.push_back(base);
    }
    std::vector<std::filesystem::path> made;
    std::vector<std::pair<QString, QString>> renamed;
    QString copied;
    for (auto at = chain.rbegin(); at != chain.rend(); ++at) {
        const ComponentEntry& each = **at;
        const ComponentEntry entry{.name = freeName(each.name),
                                   .description = each.description,
                                   .icon = each.icon,
                                   .category = each.category};
        const QString id = freeFolder(folderFor(entry.name));
        const std::filesystem::path folder = m_mineDirectory / toPath(id);
        std::error_code error;
        std::filesystem::copy(toPath(each.folder), folder, std::filesystem::copy_options::recursive, error);
        made.push_back(folder);
        if (!error && !std::filesystem::exists(folder / "tsconfig.json", error)) {
            error = platform::writeFileAtomically(folder / "tsconfig.json", script::profileTsconfig());
        }
        if (error || !writeInfo(folder, entry)) {
            for (const std::filesystem::path& undone : made) {
                std::filesystem::remove_all(undone, error);
            }
            return {};
        }
        renamed.emplace_back(each.ref.section(u'/', 1), id);
        copied = QStringLiteral("mine/") + id;
    }
    for (const std::filesystem::path& folder : made) {
        relinkSiblings(folder / toPath(entryName()), renamed);
    }
    reload();
    return copied;
}

bool ComponentLibrary::setInfo(const QString& ref, const QString& key, const QString& value)
{
    const ComponentEntry* found = find(ref);
    if (found == nullptr || found->example) {
        return false;
    }
    ComponentEntry entry = *found;
    const QString text = value.trimmed();
    if (key == u"name") {
        if (text.isEmpty() || text.size() > kMostName) {
            return false;
        }
        entry.name = text;
    } else if (key == u"description") {
        entry.description = text.left(kMostName * 2);
    } else if (key == u"icon") {
        if (!knownIcon(text)) {
            return false;
        }
        entry.icon = text;
    } else if (key == u"category") {
        entry.category = text.left(kMostName);
    } else {
        return false;
    }
    if (!writeInfo(toPath(entry.folder), entry)) {
        return false;
    }
    reload();
    return true;
}

bool ComponentLibrary::remove(const QString& ref)
{
    const ComponentEntry* entry = find(ref);
    if (entry == nullptr || entry->example) {
        return false;
    }
    const QString folder = entry->folder;
    if (!QFile::moveToTrash(folder)) {
        std::error_code error;
        std::filesystem::remove_all(toPath(folder), error);
        if (error) {
            return false;
        }
    }
    reload();
    return true;
}

void ComponentLibrary::reveal(const QString& ref) const
{
    if (const ComponentEntry* entry = find(ref)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(entry->folder));
    }
}

QString ComponentLibrary::install(const std::filesystem::path& source)
{
    if (readText(source / toPath(entryName())).isEmpty()) {
        return {};
    }
    const QJsonObject info = QJsonDocument::fromJson(readText(source / kInfoFile).toUtf8()).object();
    const QString name = info.value(QStringLiteral("name")).toString(pathText(source.filename())).left(kMostName);
    const QByteArray stamp = stampOf(source);
    bool taken = false;
    for (const ComponentEntry& entry : m_entries) {
        if (entry.example || entry.name != name) {
            continue;
        }
        if (stampOf(toPath(entry.folder)) == stamp) {
            return entry.ref;
        }
        taken = true;
    }
    const ComponentEntry entry{.name = taken ? freeName(name) : name,
                               .description = info.value(QStringLiteral("description")).toString(),
                               .icon = info.value(QStringLiteral("icon")).toString(QStringLiteral("sparkles")),
                               .category = info.value(QStringLiteral("category")).toString()};
    const QString id = freeFolder(folderFor(entry.name));
    const std::filesystem::path folder = m_mineDirectory / toPath(id);
    std::error_code error;
    std::filesystem::copy(source, folder, std::filesystem::copy_options::recursive, error);
    if (error || !writeInfo(folder, entry)) {
        std::filesystem::remove_all(folder, error);
        return {};
    }
    reload();
    return QStringLiteral("mine/") + id;
}

void ComponentLibrary::setExtensionComponents(std::vector<ExtensionComponent> components)
{
    if (components == m_extensionComponents) {
        return;
    }
    m_extensionComponents = std::move(components);
    reload();
}

bool ComponentLibrary::exportPackage(const QString& ref, const QString& fileOrUrl) const
{
    const ComponentEntry* entry = find(ref);
    const std::optional<std::filesystem::path> target = exportTarget(fileOrUrl);
    if (entry == nullptr || entry->example || !target) {
        return false;
    }
    std::vector<ExportedComponent> chain;
    for (const ComponentEntry* at = entry; at != nullptr && !at->example && chain.size() <= m_entries.size();
         at = find(at->parent)) {
        if (std::ranges::any_of(chain, [at](const ExportedComponent& each) { return each.ref == at->ref; })) {
            break;
        }
        chain.push_back({.ref = at->ref, .folder = toPath(at->folder)});
    }
    return exportComponentPackage(entry->name, entry->description, chain, *target);
}

void ComponentLibrary::reload()
{
    std::vector<ComponentEntry> entries;
    scan(m_examplesDirectory, true, entries);
    for (const ExtensionComponent& contributed : m_extensionComponents) {
        if (std::optional<ComponentEntry> entry = readComponent(contributed.folder, contributed.ref, true, false)) {
            entry->extensionId = contributed.extensionId;
            entry->extensionName = contributed.extensionName;
            entries.push_back(std::move(*entry));
        }
    }
    scan(m_mineDirectory, false, entries);
    resolveBases(entries);
    m_entries = std::move(entries);
    std::map<QString, QByteArray> stamps;
    for (const ComponentEntry& entry : m_entries) {
        stamps[entry.ref] = stampOf(toPath(entry.folder));
    }
    QStringList saved;
    if (m_loaded) {
        for (const auto& [ref, stamp] : stamps) {
            const auto before = m_stamps.find(ref);
            if (before != m_stamps.end() && before->second != stamp) {
                saved.append(ref);
            }
        }
    }
    m_stamps = std::move(stamps);
    m_loaded = true;
    refreshLists();
    watch();
    emit componentsChanged();
    for (const QString& ref : std::as_const(saved)) {
        emit componentSaved(ref);
    }
}

void ComponentLibrary::scan(const std::filesystem::path& directory, bool example, std::vector<ComponentEntry>& into) const
{
    std::error_code error;
    std::vector<std::filesystem::path> folders;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        if (it->is_directory(error)) {
            folders.push_back(it->path());
        }
    }
    std::ranges::sort(folders);
    for (const std::filesystem::path& folder : folders) {
        const QString ref =
            (example ? QStringLiteral("examples/") : QStringLiteral("mine/")) + pathText(folder.filename());
        if (std::optional<ComponentEntry> entry = readComponent(folder, ref, example, example)) {
            into.push_back(std::move(*entry));
        }
    }
}

bool ComponentLibrary::writeInfo(const std::filesystem::path& folder, const ComponentEntry& entry) const
{
    const QJsonObject info{{QStringLiteral("format"), QStringLiteral("gamescrcpy.component")},
                           {QStringLiteral("version"), 1},
                           {QStringLiteral("name"), entry.name},
                           {QStringLiteral("description"), entry.description},
                           {QStringLiteral("icon"), entry.icon},
                           {QStringLiteral("category"), entry.category}};
    return !platform::writeFileAtomically(folder / kInfoFile,
                                          QJsonDocument(info).toJson(QJsonDocument::Indented).toStdString());
}

QString ComponentLibrary::freeFolder(const QString& wanted) const
{
    QString id = wanted;
    std::error_code error;
    for (int copy = 2; std::filesystem::exists(m_mineDirectory / toPath(id), error); ++copy) {
        id = wanted + u'-' + QString::number(copy);
    }
    return id;
}

QString ComponentLibrary::freeName(const QString& wanted) const
{
    const auto taken = [this](const QString& name) {
        return std::ranges::any_of(m_entries, [&](const ComponentEntry& entry) { return entry.name == name; });
    };
    QString name = wanted;
    for (int copy = 2; taken(name); ++copy) {
        name = wanted + u' ' + QString::number(copy);
    }
    return name;
}

void ComponentLibrary::refreshLists()
{
    m_examples.clear();
    m_mine.clear();
    for (const ComponentEntry& entry : m_entries) {
        (entry.example ? m_examples : m_mine).append(itemOf(entry));
    }
}

void ComponentLibrary::watch()
{
    if (!m_watcher.files().isEmpty()) {
        m_watcher.removePaths(m_watcher.files());
    }
    if (!m_watcher.directories().isEmpty()) {
        m_watcher.removePaths(m_watcher.directories());
    }
    QStringList paths{pathText(m_mineDirectory)};
    for (const ComponentEntry& entry : m_entries) {
        if (entry.example) {
            continue;
        }
        paths.append(entry.folder);
        paths.append(entry.folder + u'/' + entryName());
        if (const QString info = entry.folder + u'/' + QLatin1StringView(kInfoFile); QFile::exists(info)) {
            paths.append(info);
        }
    }
    m_watcher.addPaths(paths);
}

}
