#include "ScriptWorkbenchFiles.h"

#include <gs/platform/AtomicFile.h>
#include <gs/script/ApiFiles.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

constexpr std::size_t kMostTreeEntries = 2000;
constexpr int kMostTreeDepth = 12;
constexpr qsizetype kMostWatchedFolders = 256;

constexpr auto kCodeSuffixes = std::to_array<std::u16string_view>({u"ts", u"mts", u"cts", u"js", u"mjs", u"cjs"});
constexpr auto kImageSuffixes = std::to_array<std::u16string_view>({u"png", u"jpg", u"jpeg", u"bmp", u"gif", u"webp"});
constexpr auto kTextSuffixes = std::to_array<std::u16string_view>({u"json", u"md", u"txt", u"csv", u"yml", u"yaml"});

template <std::size_t N>
bool listed(QStringView suffix, const std::array<std::u16string_view, N>& suffixes)
{
    return std::ranges::any_of(suffixes, [suffix](std::u16string_view candidate) {
        return suffix.compare(QStringView(candidate.data(), static_cast<qsizetype>(candidate.size())),
                              Qt::CaseInsensitive) == 0;
    });
}

QString translated(const char* text)
{
    return QCoreApplication::translate("ScriptWorkbenchFiles", text);
}

std::filesystem::path toPath(const QString& text)
{
    return {text.toStdWString()};
}

QFileInfoList entriesOf(const QString& directory)
{
    return QDir(directory).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                                         QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
}

void listInto(std::vector<ScriptFile>& files, const QDir& root, const QString& relative, int depth,
              const QSet<QString>& collapsed)
{
    if (depth > kMostTreeDepth) {
        return;
    }
    for (const QFileInfo& info : entriesOf(relative.isEmpty() ? root.path() : root.filePath(relative))) {
        if (files.size() >= kMostTreeEntries) {
            return;
        }
        const QString name = info.fileName();
        if (!shownInTree(name)) {
            continue;
        }
        const QString path = relative.isEmpty() ? name : relative + u'/' + name;
        const bool folder = info.isDir();
        const bool open = folder && !collapsed.contains(path);
        files.push_back({.path = path,
                         .name = name,
                         .depth = depth,
                         .folder = folder,
                         .open = open,
                         .kind = folder ? ScriptFileKind::Other : fileKindOf(path)});
        if (open) {
            listInto(files, root, path, depth + 1, collapsed);
        }
    }
}

void foldersInto(QStringList& folders, const QString& directory, int depth)
{
    if (depth > kMostTreeDepth) {
        return;
    }
    for (const QFileInfo& info : entriesOf(directory)) {
        if (folders.size() >= kMostWatchedFolders) {
            return;
        }
        if (info.isDir() && shownInTree(info.fileName())) {
            folders.append(info.absoluteFilePath());
            foldersInto(folders, info.absoluteFilePath(), depth + 1);
        }
    }
}

bool plainIdentifier(const QString& name)
{
    const auto leading = [](QChar c) {
        return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || c == u'_' || c == u'$';
    };
    return !name.isEmpty() && leading(name.front()) &&
           std::all_of(name.begin() + 1, name.end(), [&leading](QChar c) { return leading(c) || (c >= u'0' && c <= u'9'); });
}

bool forbiddenSegment(QStringView segment)
{
    const QStringView forbidden(u"\\:*?\"<>|");
    if (segment.isEmpty() || segment == QStringView(u".") || segment == QStringView(u"..")) {
        return true;
    }
    if (segment.endsWith(u'.') || segment.endsWith(u' ')) {
        return true;
    }
    return std::ranges::any_of(segment,
                               [forbidden](QChar c) { return c.unicode() < 0x20 || forbidden.contains(c); });
}

}

QString controlAccess(const QString& id)
{
    if (plainIdentifier(id)) {
        return QStringLiteral("controls.") + id;
    }
    QString escaped = id;
    escaped.replace(u'\\', QStringLiteral("\\\\"));
    escaped.replace(u'"', QStringLiteral("\\\""));
    return QStringLiteral("controls[\"") + escaped + QStringLiteral("\"]");
}

bool shownInTree(QStringView name)
{
    return !name.isEmpty() && !name.startsWith(u'.') && name != QStringView(u"node_modules") &&
           name != QStringView(u"types") && !name.endsWith(QStringView(u".blueprint.json"), Qt::CaseInsensitive);
}

ScriptFileKind fileKindOf(QStringView path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    const QStringView name = path.sliced(slash + 1);
    if (slash < 0 && name.compare(QStringView(u"profile.json"), Qt::CaseInsensitive) == 0) {
        return ScriptFileKind::Profile;
    }
    const qsizetype dot = name.lastIndexOf(u'.');
    if (dot < 0) {
        return ScriptFileKind::Other;
    }
    const QStringView suffix = name.sliced(dot + 1);
    if (listed(suffix, kCodeSuffixes)) {
        return ScriptFileKind::Code;
    }
    if (listed(suffix, kImageSuffixes)) {
        return ScriptFileKind::Image;
    }
    if (listed(suffix, kTextSuffixes)) {
        return ScriptFileKind::Text;
    }
    return ScriptFileKind::Other;
}

std::vector<ScriptFile> scriptFiles(const QString& folder, const QSet<QString>& collapsed)
{
    std::vector<ScriptFile> files;
    listInto(files, QDir(folder), {}, 0, collapsed);
    return files;
}

QStringList watchedFolders(const QString& folder)
{
    QStringList folders{QDir(folder).absolutePath()};
    foldersInto(folders, folder, 0);
    return folders;
}

QString logicEntryIn(const QString& folder)
{
    for (const QString& name : {QStringLiteral("logic.ts"), QStringLiteral("logic.js"), QStringLiteral("task.ts"),
                                QStringLiteral("component.ts")}) {
        if (QFileInfo(QDir(folder).filePath(name)).isFile()) {
            return name;
        }
    }
    return {};
}

CheckOutcome parseCheckOutput(const QByteArray& output, const QString& folder)
{
    CheckOutcome outcome;
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(output, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return outcome;
    }
    const QJsonObject root = document.object();
    if (const QJsonValue failure = root.value(QStringLiteral("error")); failure.isString()) {
        outcome.failure = failure.toString();
        return outcome;
    }
    const QJsonValue problems = root.value(QStringLiteral("problems"));
    if (!problems.isArray()) {
        return outcome;
    }
    outcome.ran = true;
    const QDir base(folder);
    for (const QJsonValue& value : problems.toArray()) {
        const QJsonObject object = value.toObject();
        ScriptProblem problem;
        const QString given = object.value(QStringLiteral("file")).toString();
        problem.line = std::max(0, object.value(QStringLiteral("line")).toInt());
        problem.column = std::max(0, object.value(QStringLiteral("column")).toInt());
        problem.severity = object.value(QStringLiteral("severity")).toString() == u"warning" ? QStringLiteral("warning")
                                                                                            : QStringLiteral("error");
        problem.code = object.value(QStringLiteral("code")).toString();
        problem.message = object.value(QStringLiteral("message")).toString().simplified();
        problem.source = object.value(QStringLiteral("source")).toString();
        if (!given.isEmpty()) {
            const QString absolute = QDir::cleanPath(base.absoluteFilePath(QDir::fromNativeSeparators(given)));
            const QString relative = base.relativeFilePath(absolute);
            problem.inside = !relative.isEmpty() && relative != u".." && !relative.startsWith(QStringLiteral("../")) &&
                             !QDir::isAbsolutePath(relative);
            problem.file = problem.inside ? relative : given;
        }
        outcome.problems.push_back(std::move(problem));
    }
    return outcome;
}

QString starterLogic(const QStringList& controlIds)
{
    QString text = QStringLiteral("log.info(\"profile logic loaded\");\n");
    if (!controlIds.isEmpty()) {
        const QString control = controlAccess(controlIds.front());
        text += QStringLiteral("\n") + control + QStringLiteral(".onPress(async () => {\n    log.info(\"pressed\", ") +
                control + QStringLiteral(".name);\n});\n");
    }
    return text;
}

QString controlStarter(const QString& id, const QString& shape)
{
    QStringList events;
    if (shape == u"Tap" || shape == u"LongPress" || shape == u"Swipe") {
        events = {QStringLiteral("onPress")};
    } else if (shape == u"Hold" || shape == u"Turbo" || shape == u"MultiPress") {
        events = {QStringLiteral("onPress"), QStringLiteral("onRelease")};
    }
    const QString control = controlAccess(id);
    QString text;
    for (const QString& event : events) {
        text += control + u'.' + event + QStringLiteral("(() => {\n}, { consume: false });\n");
    }
    return text;
}

LogicSnippet referenceSnippet(const QString& id)
{
    const QString head = controlAccess(id) + QStringLiteral(".onPress(async () => {\n    ");
    return {.text = head + QStringLiteral("\n});\n"), .caret = static_cast<int>(head.size())};
}

QString localizedCheckMessage(const QString& code, const QString& message)
{
    struct Known {
        const char* code;
        const char* english;
    };
    static constexpr Known kKnown[] = {
        {"GS-CHECK-001", QT_TRANSLATE_NOOP("ScriptCheck", "the TypeScript compiler is missing next to gs.exe (typescript/tsc.exe); reinstall GameScrcpy")},
        {"GS-CHECK-001", QT_TRANSLATE_NOOP("ScriptCheck", "the TypeScript compiler did not start")},
        {"GS-CHECK-002", QT_TRANSLATE_NOOP("ScriptCheck", "the project has no tsconfig.json; run gs new in an empty folder to get one")},
        {"GS-CHECK-003", QT_TRANSLATE_NOOP("ScriptCheck", "cannot write the temporary type-check files")},
        {"GS-CHECK-004", QT_TRANSLATE_NOOP("ScriptCheck", "the file cannot be read")},
        {"GS-CHECK-006", QT_TRANSLATE_NOOP("ScriptCheck", "the profile folder has no logic.ts or logic.js")},
        {"GS-CHECK-006", QT_TRANSLATE_NOOP("ScriptCheck", "the project has no src/main.ts or src/main.js")},
        {"GS-CHECK-101", QT_TRANSLATE_NOOP("ScriptCheck", "this loop never awaits, so it blocks the script until the watchdog stops it; await sleep() or nextFrame() inside it")},
        {"GS-CHECK-102", QT_TRANSLATE_NOOP("ScriptCheck", "a fixed sleep before looking for a picture waits too long or not long enough; wait for the picture with the action's .until(), such as await touch.tap(x, y).until(() => vision.find(\"res/next.png\"))")},
        {"GS0001", QT_TRANSLATE_NOOP("ScriptCheck", "enum is not erasable; use a const object instead")},
        {"GS0002", QT_TRANSLATE_NOOP("ScriptCheck", "const enum is not erasable; use a const object instead")},
        {"GS0003", QT_TRANSLATE_NOOP("ScriptCheck", "a namespace with runtime code is not erasable; use a module or a plain object instead")},
        {"GS0004", QT_TRANSLATE_NOOP("ScriptCheck", "parameter properties are not erasable; declare the field in the class body and assign it in the constructor")},
        {"GS0005", QT_TRANSLATE_NOOP("ScriptCheck", "import = require() is not erasable; use import ... from instead")},
        {"GS0006", QT_TRANSLATE_NOOP("ScriptCheck", "an import alias is not erasable; use const instead")},
        {"GS0007", QT_TRANSLATE_NOOP("ScriptCheck", "export = is not erasable; use export default instead")},
        {"GS0008", QT_TRANSLATE_NOOP("ScriptCheck", "decorators are not supported; call the function on the class or member instead")},
        {"GS0009", QT_TRANSLATE_NOOP("ScriptCheck", "JSX is not supported in scripts")},
        {"GS0010", QT_TRANSLATE_NOOP("ScriptCheck", "a line break follows this <T> assertion, so erasing it would change the meaning; use 'as T' instead")},
        {"GS0100", QT_TRANSLATE_NOOP("ScriptCheck", "syntax error")},
    };
    for (const Known& known : kKnown) {
        if (code == QLatin1String(known.code) && message == QLatin1String(known.english)) {
            return QCoreApplication::translate("ScriptCheck", known.english);
        }
    }
    if (code == QStringLiteral("GS0100")) {
        return QCoreApplication::translate("ScriptCheck", "Syntax error: %1").arg(message);
    }
    return message;
}

QByteArray profileTsconfig()
{
    return QByteArray::fromStdString(script::profileTsconfig());
}

LogicWrite writeStarterLogic(const QString& folder, const QStringList& controlIds)
{
    if (!logicEntryIn(folder).isEmpty()) {
        return LogicWrite::Exists;
    }
    const std::filesystem::path root = toPath(folder);
    const std::string logic = starterLogic(controlIds).toStdString();
    std::error_code error = platform::writeFileAtomically(root / "logic.ts", logic);
    if (!error) {
        std::error_code missing;
        if (!std::filesystem::exists(root / "tsconfig.json", missing) && !missing) {
            error = platform::writeFileAtomically(root / "tsconfig.json", script::profileTsconfig());
        }
    }
    if (error) {
        return platform::fileBusy(error) ? LogicWrite::Busy : LogicWrite::Failed;
    }
    return LogicWrite::Written;
}

QString checkedFileName(const QString& typed, QString& problem)
{
    QString name = QDir::fromNativeSeparators(typed.trimmed());
    if (name.isEmpty()) {
        problem = translated(QT_TRANSLATE_NOOP("ScriptWorkbenchFiles", "Type a name for the file"));
        return {};
    }
    if (name.startsWith(u'/')) {
        problem = translated(QT_TRANSLATE_NOOP("ScriptWorkbenchFiles", "Name a file inside the profile folder"));
        return {};
    }
    const QStringList segments = name.split(u'/');
    if (std::ranges::any_of(segments, [](const QString& segment) { return forbiddenSegment(segment); })) {
        problem = translated(QT_TRANSLATE_NOOP(
            "ScriptWorkbenchFiles",
            "A file name cannot be empty, contain \\ : * ? \" < > | or end with a dot or a space"));
        return {};
    }
    if (std::ranges::any_of(segments, [](const QString& segment) { return !shownInTree(segment); })) {
        problem = translated(QT_TRANSLATE_NOOP(
            "ScriptWorkbenchFiles", "Names that start with a dot, node_modules and types are hidden here; choose another"));
        return {};
    }
    if (!name.endsWith(QStringLiteral(".ts"), Qt::CaseInsensitive) &&
        !name.endsWith(QStringLiteral(".mts"), Qt::CaseInsensitive)) {
        name += QStringLiteral(".ts");
    }
    problem.clear();
    return name;
}

}
