#include "McpDrafts.h"

#include "ComponentLibrary.h"
#include "NativeExtensions.h"
#include "ProfileLibrary.h"
#include "ScriptWorkbenchFiles.h"
#include "TaskLibrary.h"

#include <gs/input/Modules.h>
#include <gs/input/Profile.h>
#include <gs/platform/AtomicFile.h>
#include <gs/script/Resources.h>

#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringDecoder>

#include <algorithm>
#include <string>
#include <system_error>
#include <tuple>

namespace gs::app {

namespace {

namespace fs = std::filesystem;

constexpr qsizetype kMostPathDepth = 12;
constexpr qsizetype kMostPathChars = 240;
constexpr qsizetype kMostDiffCells = 2'000'000;
constexpr std::size_t kContext = 3;
constexpr qsizetype kMostFolderChars = 48;
constexpr std::size_t kMostMetaBytes = std::size_t{4} << 20;
constexpr auto kMetaFormat = QLatin1StringView("gamescrcpy.mcp-draft");
constexpr auto kMetaSuffix = QLatin1StringView(".draft.json");

enum class Step : std::uint8_t { Same, Gone, Added };

QString pathText(const fs::path& path)
{
    return QDir::fromNativeSeparators(QString::fromStdWString(path.wstring()));
}

fs::path toPath(const QString& text)
{
    return {text.toStdWString()};
}

QByteArray hashOf(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

QString shortHash(const QByteArray& bytes, qsizetype length)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().left(length));
}

std::optional<QByteArray> readAll(const fs::path& file, std::size_t most)
{
    std::string contents;
    if (platform::readFile(file, most, contents, platform::WhenBusy::GiveUp)) {
        return std::nullopt;
    }
    return QByteArray::fromStdString(contents);
}

bool reservedName(const QString& segment)
{
    const QString stem = segment.section(u'.', 0, 0).toUpper();
    if (stem == u"CON" || stem == u"PRN" || stem == u"AUX" || stem == u"NUL") {
        return true;
    }
    return stem.size() == 4 && (stem.startsWith(u"COM") || stem.startsWith(u"LPT")) && stem.at(3) >= u'1' &&
           stem.at(3) <= u'9';
}

bool safeRelative(const QString& path)
{
    if (path.isEmpty() || path.size() > kMostPathChars || QDir::isAbsolutePath(path) || path.contains(u'\\') ||
        path.contains(u':')) {
        return false;
    }
    const QStringList segments = path.split(u'/');
    return std::ranges::none_of(segments, [](const QString& segment) {
        return segment.isEmpty() || segment == u"." || segment == u"..";
    });
}

std::vector<std::pair<QString, fs::path>> listFiles(const fs::path& folder)
{
    std::vector<std::pair<QString, fs::path>> files;
    std::error_code error;
    if (folder.empty() || !fs::is_directory(folder, error)) {
        return files;
    }
    for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error)) {
        std::error_code kind;
        if (mcpSkippedName(QString::fromStdWString(it->path().filename().wstring()))) {
            if (it->is_directory(kind)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (it->is_regular_file(kind)) {
            files.emplace_back(pathText(it->path().lexically_relative(folder)), it->path());
        }
    }
    std::ranges::sort(files, {}, [](const auto& file) -> const QString& { return file.first; });
    return files;
}

QString clientKey(const QString& client)
{
    return shortHash(client.toUtf8(), 12);
}

QString folderName(const QString& target)
{
    const QString kind = target.section(u':', 0, 0);
    const QString id = target.section(u':', 1);
    QString safe;
    for (const QChar c : id) {
        const bool plain = (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') ||
                           c == u'-' || c == u'_';
        safe += plain ? c : QChar(u'-');
    }
    if (safe != id || safe.size() > kMostFolderChars) {
        safe = safe.left(kMostFolderChars) + u'-' + shortHash(id.toUtf8(), 8);
    }
    return kind + u'-' + safe;
}

bool freshId(const QString& id)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9]+(-[a-z0-9]+)*$"));
    return id.size() <= 64 && pattern.match(id).hasMatch();
}

std::vector<Step> stepsOf(const QStringList& before, const QStringList& after)
{
    qsizetype start = 0;
    while (start < before.size() && start < after.size() && before[start] == after[start]) {
        ++start;
    }
    qsizetype endBefore = before.size();
    qsizetype endAfter = after.size();
    while (endBefore > start && endAfter > start && before[endBefore - 1] == after[endAfter - 1]) {
        --endBefore;
        --endAfter;
    }
    std::vector<Step> steps(static_cast<std::size_t>(start), Step::Same);
    const qsizetype n = endBefore - start;
    const qsizetype m = endAfter - start;
    if (n > 0 && m > 0 && n * m <= kMostDiffCells) {
        const auto width = static_cast<std::size_t>(m + 1);
        std::vector<std::uint16_t> table(static_cast<std::size_t>(n + 1) * width, 0);
        const auto at = [&table, width](qsizetype row, qsizetype column) -> std::uint16_t& {
            return table[static_cast<std::size_t>(row) * width + static_cast<std::size_t>(column)];
        };
        for (qsizetype row = n - 1; row >= 0; --row) {
            for (qsizetype column = m - 1; column >= 0; --column) {
                at(row, column) = before[start + row] == after[start + column]
                                      ? static_cast<std::uint16_t>(at(row + 1, column + 1) + 1)
                                      : std::max(at(row + 1, column), at(row, column + 1));
            }
        }
        qsizetype row = 0;
        qsizetype column = 0;
        while (row < n && column < m) {
            if (before[start + row] == after[start + column]) {
                steps.push_back(Step::Same);
                ++row;
                ++column;
            } else if (at(row + 1, column) >= at(row, column + 1)) {
                steps.push_back(Step::Gone);
                ++row;
            } else {
                steps.push_back(Step::Added);
                ++column;
            }
        }
        steps.insert(steps.end(), static_cast<std::size_t>(n - row), Step::Gone);
        steps.insert(steps.end(), static_cast<std::size_t>(m - column), Step::Added);
    } else {
        steps.insert(steps.end(), static_cast<std::size_t>(n), Step::Gone);
        steps.insert(steps.end(), static_cast<std::size_t>(m), Step::Added);
    }
    steps.insert(steps.end(), static_cast<std::size_t>(before.size() - endBefore), Step::Same);
    return steps;
}

QString rangeText(qsizetype start, qsizetype count)
{
    if (count == 0) {
        return QStringLiteral("%1,0").arg(start);
    }
    if (count == 1) {
        return QString::number(start + 1);
    }
    return QStringLiteral("%1,%2").arg(start + 1).arg(count);
}

QString countText(const McpDraftFile& file)
{
    if (file.state == u"added") {
        return QStringLiteral("%1 (new, %2 lines)").arg(file.path).arg(file.added);
    }
    if (file.state == u"removed") {
        return QStringLiteral("%1 (removed)").arg(file.path);
    }
    return QStringLiteral("%1 (+%2 -%3)").arg(file.path).arg(file.added).arg(file.removed);
}

QString checkLine(const char* file, const char* severity, const char* code, const std::string& detail)
{
    return QStringLiteral("%1: %2 %3 %4").arg(QLatin1StringView(file), QLatin1StringView(severity),
                                             QLatin1StringView(code), QString::fromStdString(detail));
}

void checkProfile(const QString& text, QStringList& errors, QStringList& warnings)
{
    const input::LoadResult loaded = input::loadProfile(text.toStdString());
    for (const input::Diagnostic& diagnostic : loaded.diagnostics) {
        if (diagnostic.severity == input::Severity::Info) {
            continue;
        }
        const std::string detail =
            diagnostic.detail.empty() ? diagnostic.path : diagnostic.path + ": " + diagnostic.detail;
        if (diagnostic.severity == input::Severity::Error) {
            errors.append(checkLine("profile.json", "error", "GS-CHECK-005", detail));
        } else {
            warnings.append(checkLine("profile.json", "warning", "GS-CHECK-005", detail));
        }
    }
    const NativeExtensions* native = NativeExtensions::instance();
    if (!loaded.ok() || native == nullptr) {
        return;
    }
    for (const input::Diagnostic& diagnostic : input::checkModules(loaded.profile, native->moduleCatalog())) {
        warnings.append(checkLine("profile.json", "warning", "GS-CHECK-005", diagnostic.path + ": " + diagnostic.detail));
    }
}

void checkResources(const QString& text, QStringList& errors)
{
    const script::LoadedResources resources = script::parseResources(text.toStdString());
    if (resources) {
        return;
    }
    for (const std::string& each : resources.error()) {
        errors.append(checkLine("res/resources.json", "error", "GS-CHECK-007", each));
    }
}

QString errorCodeOf(const QStringList& errors)
{
    return std::ranges::any_of(errors, [](const QString& line) { return line.contains(u"GS-CHECK-005"); })
               ? QStringLiteral("GS-CHECK-005")
               : QStringLiteral("GS-CHECK-007");
}

}

QStringList mcpLinesOf(const QString& text)
{
    QString plain = text;
    plain.remove(u'\r');
    QStringList lines = plain.split(u'\n');
    if (!lines.isEmpty() && lines.back().isEmpty()) {
        lines.removeLast();
    }
    return lines;
}

McpLineChange mcpLineChange(const QString& before, const QString& after)
{
    McpLineChange change;
    for (const Step step : stepsOf(mcpLinesOf(before), mcpLinesOf(after))) {
        if (step == Step::Added) {
            ++change.added;
        } else if (step == Step::Gone) {
            ++change.removed;
        }
    }
    return change;
}

QString mcpUnifiedDiff(const QString& path, const std::optional<QString>& before, const std::optional<QString>& after)
{
    const QStringList first = before ? mcpLinesOf(*before) : QStringList();
    const QStringList second = after ? mcpLinesOf(*after) : QStringList();
    const std::vector<Step> steps = stepsOf(first, second);
    const bool same = std::ranges::all_of(steps, [](Step step) { return step == Step::Same; });
    if (same && before.has_value() == after.has_value()) {
        return {};
    }
    QString text = QStringLiteral("--- %1\n+++ %2\n")
                       .arg(before ? QString(QStringLiteral("a/") + path) : QStringLiteral("/dev/null"),
                            after ? QString(QStringLiteral("b/") + path) : QStringLiteral("/dev/null"));
    struct Line {
        Step step = Step::Same;
        qsizetype first = 0;
        qsizetype second = 0;
    };
    std::vector<Line> lines;
    lines.reserve(steps.size());
    qsizetype inFirst = 0;
    qsizetype inSecond = 0;
    for (const Step step : steps) {
        lines.push_back({.step = step, .first = inFirst, .second = inSecond});
        if (step != Step::Added) {
            ++inFirst;
        }
        if (step != Step::Gone) {
            ++inSecond;
        }
    }
    std::size_t index = 0;
    while (index < lines.size()) {
        if (lines[index].step == Step::Same) {
            ++index;
            continue;
        }
        const std::size_t from = index >= kContext ? index - kContext : 0;
        std::size_t last = index;
        std::size_t scan = index;
        while (scan < lines.size()) {
            if (lines[scan].step != Step::Same) {
                last = scan;
                ++scan;
                continue;
            }
            std::size_t run = scan;
            while (run < lines.size() && lines[run].step == Step::Same) {
                ++run;
            }
            if (run < lines.size() && run - scan <= 2 * kContext) {
                scan = run;
                continue;
            }
            break;
        }
        const std::size_t to = std::min(lines.size(), last + 1 + kContext);
        qsizetype firstCount = 0;
        qsizetype secondCount = 0;
        for (std::size_t at = from; at < to; ++at) {
            firstCount += lines[at].step != Step::Added ? 1 : 0;
            secondCount += lines[at].step != Step::Gone ? 1 : 0;
        }
        text += QStringLiteral("@@ -%1 +%2 @@\n")
                    .arg(rangeText(lines[from].first, firstCount), rangeText(lines[from].second, secondCount));
        for (std::size_t at = from; at < to; ++at) {
            const Line& line = lines[at];
            if (line.step == Step::Same) {
                text += u' ' + first[line.first] + u'\n';
            } else if (line.step == Step::Gone) {
                text += u'-' + first[line.first] + u'\n';
            } else {
                text += u'+' + second[line.second] + u'\n';
            }
        }
        index = to;
    }
    return text;
}

QString mcpCheckedPath(const QString& given, QString& problem)
{
    const QString path = QDir::fromNativeSeparators(given.trimmed());
    const QStringList segments = path.split(u'/');
    const QStringView forbidden(u"\\:*?\"<>|");
    const auto bad = [&forbidden](const QString& segment) {
        return segment.isEmpty() || segment == u"." || segment == u".." || segment.endsWith(u'.') ||
               segment.endsWith(u' ') || reservedName(segment) ||
               std::ranges::any_of(segment, [&forbidden](QChar c) { return c.unicode() < 0x20 || forbidden.contains(c); });
    };
    if (path.isEmpty() || path.size() > kMostPathChars || segments.size() > kMostPathDepth ||
        std::ranges::any_of(segments, bad)) {
        problem = QStringLiteral("%1 is not a path inside the folder").arg(given);
        return {};
    }
    if (std::ranges::any_of(segments, [](const QString& segment) { return !shownInTree(segment); })) {
        problem = QStringLiteral("%1 is hidden or made by GameScrcpy (names that start with a dot, node_modules, "
                                 "types and blueprint layouts), so it cannot be written")
                      .arg(given);
        return {};
    }
    const ScriptFileKind kind = fileKindOf(path);
    if (kind == ScriptFileKind::Profile) {
        return QStringLiteral("profile.json");
    }
    if (kind != ScriptFileKind::Code && kind != ScriptFileKind::Text) {
        problem = QStringLiteral("%1 is not a code or text file; only .ts, .js, .json, .md, .txt, .csv and .yaml files "
                                 "can be written")
                      .arg(given);
        return {};
    }
    return path;
}

QByteArray mcpEncodedLike(const QString& text, const QByteArray& existing)
{
    QString written = text;
    if (existing.contains("\r\n") && !text.contains(u'\r')) {
        written.replace(u'\n', QStringLiteral("\r\n"));
    }
    QByteArray bytes = written.toUtf8();
    if (existing.startsWith("\xEF\xBB\xBF") && !bytes.startsWith("\xEF\xBB\xBF")) {
        bytes.prepend("\xEF\xBB\xBF");
    }
    return bytes;
}

std::optional<QString> mcpUtf8(const QByteArray& bytes)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder(bytes);
    if (decoder.hasError()) {
        return std::nullopt;
    }
    return text;
}

QString mcpTargetOf(const QString& given)
{
    const QString trimmed = given.trimmed();
    for (const QString& prefix : {QStringLiteral("profile:"), QStringLiteral("component:"), QStringLiteral("task:")}) {
        if (trimmed.startsWith(prefix)) {
            return trimmed.size() > prefix.size() ? trimmed : QString();
        }
    }
    return trimmed.isEmpty() || trimmed.contains(u':') ? QString() : QString(QStringLiteral("profile:") + trimmed);
}

bool mcpSkippedName(const QString& name)
{
    return name.startsWith(u'.') || name == u"node_modules" || name == u"types";
}

McpDrafts::McpDrafts(McpDraftSetup setup, QObject* parent)
    : QObject(parent)
    , m_setup(std::move(setup))
{
    prune();
}

McpDrafts::~McpDrafts() = default;

std::filesystem::path McpDrafts::clientFolder(const QString& client) const
{
    return m_setup.root / toPath(clientKey(client));
}

std::filesystem::path McpDrafts::folderOf(const QString& client, const QString& target) const
{
    return clientFolder(client) / toPath(folderName(target));
}

std::filesystem::path McpDrafts::metaFile(const QString& client, const QString& target) const
{
    return clientFolder(client) / toPath(folderName(target) + kMetaSuffix);
}

std::optional<McpDrafts::Meta> McpDrafts::readMeta(const QString& client, const QString& target) const
{
    const std::optional<QByteArray> bytes = readAll(metaFile(client, target), kMostMetaBytes);
    if (!bytes) {
        return std::nullopt;
    }
    const QJsonObject root = QJsonDocument::fromJson(*bytes).object();
    if (root.value(QStringLiteral("format")).toString() != kMetaFormat ||
        root.value(QStringLiteral("client")).toString() != client ||
        root.value(QStringLiteral("target")).toString() != target) {
        return std::nullopt;
    }
    Meta meta{.client = client,
              .target = target,
              .single = root.value(QStringLiteral("single")).toBool(),
              .fresh = root.value(QStringLiteral("fresh")).toBool()};
    const QJsonObject copied = root.value(QStringLiteral("copied")).toObject();
    for (auto at = copied.begin(); at != copied.end(); ++at) {
        meta.copied.emplace(at.key(), at.value().toString().toLatin1());
    }
    return meta;
}

bool McpDrafts::writeMeta(const Meta& meta) const
{
    QJsonObject copied;
    for (const auto& [path, hash] : meta.copied) {
        copied.insert(path, QString::fromLatin1(hash));
    }
    const QJsonObject root{{QStringLiteral("format"), QString(kMetaFormat)},
                           {QStringLiteral("version"), 1},
                           {QStringLiteral("client"), meta.client},
                           {QStringLiteral("target"), meta.target},
                           {QStringLiteral("single"), meta.single},
                           {QStringLiteral("fresh"), meta.fresh},
                           {QStringLiteral("copied"), copied}};
    return !platform::writeFileAtomically(metaFile(meta.client, meta.target),
                                          QJsonDocument(root).toJson(QJsonDocument::Indented).toStdString());
}

std::optional<McpDrafts::Real> McpDrafts::real(const QString& target, McpDraftResult& result) const
{
    const QString kind = target.section(u':', 0, 0);
    const QString id = target.section(u':', 1);
    if (kind == u"profile") {
        const ProfileEntry* entry = m_setup.profiles != nullptr ? m_setup.profiles->find(id) : nullptr;
        if (entry == nullptr) {
            result.code = QStringLiteral("GS-MCP-006");
            result.problem = QStringLiteral("There is no profile with the id %1; list_profiles lists them").arg(id);
            return std::nullopt;
        }
        return Real{.folder = entry->folder,
                    .file = entry->folder.empty() ? entry->path : fs::path(),
                    .name = entry->name,
                    .readOnly = entry->readOnly()};
    }
    if (kind == u"component") {
        if (const ComponentEntry* entry = m_setup.components != nullptr ? m_setup.components->find(id) : nullptr) {
            return Real{.folder = toPath(entry->folder), .name = entry->name, .readOnly = entry->example};
        }
        if (m_setup.components != nullptr && id.startsWith(QStringLiteral("mine/")) && freshId(id.mid(5))) {
            return Real{.name = id.mid(5), .fresh = true};
        }
        result.code = QStringLiteral("GS-MCP-019");
        result.problem = QStringLiteral("There is no component %1; list_components lists them, and a new one is named "
                                        "mine/<name> with lower-case letters, digits and hyphens")
                             .arg(id);
        return std::nullopt;
    }
    if (kind == u"task") {
        if (const TaskEntry* entry = m_setup.tasks != nullptr ? m_setup.tasks->find(id) : nullptr) {
            return Real{.folder = toPath(entry->folder), .name = entry->name};
        }
        if (m_setup.tasks != nullptr && freshId(id)) {
            return Real{.name = id, .fresh = true};
        }
        result.code = QStringLiteral("GS-MCP-019");
        result.problem = QStringLiteral("There is no task %1; list_tasks lists them, and a new one is named with "
                                        "lower-case letters, digits and hyphens")
                             .arg(id);
        return std::nullopt;
    }
    result.code = QStringLiteral("GS-MCP-004");
    result.problem = QStringLiteral("the target must be profile:<id>, component:<ref> or task:<id>");
    return std::nullopt;
}

std::vector<std::pair<QString, std::filesystem::path>> McpDrafts::realFiles(const Meta& meta) const
{
    McpDraftResult ignored;
    const std::optional<Real> found = real(meta.target, ignored);
    if (!found || found->fresh) {
        return {};
    }
    if (!found->file.empty()) {
        return {{QStringLiteral("profile.json"), found->file}};
    }
    return listFiles(found->folder);
}

QStringList McpDrafts::targets(const QString& client) const
{
    QStringList found;
    std::error_code error;
    for (fs::directory_iterator it(clientFolder(client), error), end; !error && it != end; it.increment(error)) {
        const QString name = QString::fromStdWString(it->path().filename().wstring());
        if (!name.endsWith(kMetaSuffix)) {
            continue;
        }
        const std::optional<QByteArray> bytes = readAll(it->path(), kMostMetaBytes);
        const QJsonObject root = bytes ? QJsonDocument::fromJson(*bytes).object() : QJsonObject();
        const QString target = root.value(QStringLiteral("target")).toString();
        if (root.value(QStringLiteral("client")).toString() == client && !target.isEmpty() && has(client, target)) {
            found.append(target);
        }
    }
    found.sort();
    return found;
}

std::vector<McpDraftFacts> McpDrafts::pending() const
{
    std::vector<McpDraftFacts> found;
    std::error_code error;
    for (fs::directory_iterator client(m_setup.root, error), end; !error && client != end; client.increment(error)) {
        std::error_code kind;
        if (!client->is_directory(kind)) {
            continue;
        }
        std::error_code listing;
        for (fs::directory_iterator entry(client->path(), listing), last; !listing && entry != last;
             entry.increment(listing)) {
            const QString name = QString::fromStdWString(entry->path().filename().wstring());
            if (!name.endsWith(kMetaSuffix)) {
                continue;
            }
            const std::optional<QByteArray> bytes = readAll(entry->path(), kMostMetaBytes);
            const QJsonObject root = bytes ? QJsonDocument::fromJson(*bytes).object() : QJsonObject();
            if (root.value(QStringLiteral("format")).toString() != kMetaFormat) {
                continue;
            }
            const QString who = root.value(QStringLiteral("client")).toString();
            const QString target = root.value(QStringLiteral("target")).toString();
            if (!who.isEmpty() && !target.isEmpty() && has(who, target)) {
                found.push_back({.client = who, .target = target});
            }
        }
    }
    std::ranges::sort(found, {}, [](const McpDraftFacts& each) { return std::tie(each.target, each.client); });
    return found;
}

std::optional<McpDraftPreview> McpDrafts::preview(const QString& client, const QString& target) const
{
    if (target.section(u':', 0, 0) != u"profile") {
        return std::nullopt;
    }
    const std::optional<Meta> meta = readMeta(client, target);
    if (!meta) {
        return std::nullopt;
    }
    const std::optional<QByteArray> drafted = readAll(folderOf(client, target) / "profile.json", kMostCopiedFileBytes);
    const std::optional<QString> draftedText = drafted ? mcpUtf8(*drafted) : std::nullopt;
    if (!draftedText) {
        return std::nullopt;
    }
    McpDraftPreview shown{.client = client,
                          .target = target,
                          .name = nameOf(target),
                          .profileText = *draftedText,
                          .draftFolder = pathText(folderOf(client, target)),
                          .fresh = meta->fresh};
    McpDraftResult ignored;
    const std::optional<Real> found = real(target, ignored);
    if (found && !found->readOnly) {
        std::optional<QByteArray> bytes;
        if (!found->file.empty()) {
            bytes = readAll(found->file, kMostCopiedFileBytes);
        } else if (!found->folder.empty()) {
            shown.liveFolder = pathText(found->folder);
            bytes = readAll(found->folder / "profile.json", kMostCopiedFileBytes);
        }
        if (bytes) {
            shown.liveText = mcpUtf8(*bytes).value_or(QString());
        }
    }
    return shown;
}

bool McpDrafts::has(const QString& client, const QString& target) const
{
    std::error_code error;
    return readMeta(client, target).has_value() && fs::is_directory(folderOf(client, target), error);
}

QString McpDrafts::nameOf(const QString& target) const
{
    McpDraftResult ignored;
    const std::optional<Real> found = real(target, ignored);
    return found ? found->name : target.section(u':', 1);
}

std::vector<McpDraftFile> McpDrafts::diff(const QString& client, const QString& target) const
{
    std::vector<McpDraftFile> files;
    const std::optional<Meta> meta = readMeta(client, target);
    if (!meta) {
        return files;
    }
    std::map<QString, std::pair<fs::path, fs::path>> all;
    for (const auto& [path, file] : listFiles(folderOf(client, target))) {
        all[path].first = file;
    }
    for (const auto& [path, file] : realFiles(*meta)) {
        all[path].second = file;
    }
    const auto lineCount = [](const std::optional<QByteArray>& bytes) {
        const std::optional<QString> text = bytes ? mcpUtf8(*bytes) : std::nullopt;
        return text ? static_cast<int>(mcpLinesOf(*text).size()) : 0;
    };
    for (const auto& [path, pair] : all) {
        const fs::path& drafted = pair.first;
        const fs::path& current = pair.second;
        const auto copied = meta->copied.find(path);
        if (!drafted.empty() && !current.empty()) {
            const std::optional<QByteArray> mine = readAll(drafted, kMostCopiedFileBytes);
            const std::optional<QByteArray> theirs = readAll(current, kMostCopiedFileBytes);
            if (!mine || !theirs || *mine == *theirs) {
                continue;
            }
            McpDraftFile file{.path = path, .state = QStringLiteral("changed")};
            const std::optional<QString> after = mcpUtf8(*mine);
            const std::optional<QString> before = mcpUtf8(*theirs);
            if (after && before) {
                const McpLineChange change = mcpLineChange(*before, *after);
                file.added = static_cast<int>(change.added);
                file.removed = static_cast<int>(change.removed);
            }
            file.conflict = copied == meta->copied.end() || copied->second != hashOf(*theirs);
            files.push_back(std::move(file));
        } else if (!drafted.empty()) {
            files.push_back(McpDraftFile{.path = path,
                                         .state = QStringLiteral("added"),
                                         .added = lineCount(readAll(drafted, kMostCopiedFileBytes)),
                                         .conflict = copied != meta->copied.end()});
        } else if (copied != meta->copied.end()) {
            const std::optional<QByteArray> theirs = readAll(current, kMostCopiedFileBytes);
            files.push_back(McpDraftFile{.path = path,
                                         .state = QStringLiteral("removed"),
                                         .removed = lineCount(theirs),
                                         .conflict = !theirs || copied->second != hashOf(*theirs)});
        }
    }
    return files;
}

QString McpDrafts::fileDiff(const QString& client, const QString& target, const QString& path) const
{
    const QString relative = QDir::fromNativeSeparators(path.trimmed());
    const std::optional<Meta> meta = readMeta(client, target);
    if (!meta || !safeRelative(relative)) {
        return {};
    }
    std::error_code error;
    const fs::path drafted = folderOf(client, target) / toPath(relative);
    std::optional<QByteArray> mine;
    if (fs::is_regular_file(drafted, error)) {
        mine = readAll(drafted, kMostCopiedFileBytes);
    }
    std::optional<QByteArray> theirs;
    for (const auto& [name, file] : realFiles(*meta)) {
        if (name == relative) {
            theirs = readAll(file, kMostCopiedFileBytes);
        }
    }
    if (!mine && !theirs) {
        return {};
    }
    if (mine && theirs && *mine == *theirs) {
        return {};
    }
    const std::optional<QString> after = mine ? mcpUtf8(*mine) : std::nullopt;
    const std::optional<QString> before = theirs ? mcpUtf8(*theirs) : std::nullopt;
    if ((mine && !after) || (theirs && !before)) {
        return QStringLiteral("Binary file %1 differs\n").arg(relative);
    }
    return mcpUnifiedDiff(relative, before, after);
}

bool McpDrafts::convertsToFolder(const QString& client, const QString& target) const
{
    const std::optional<Meta> meta = readMeta(client, target);
    if (!meta || target.section(u':', 0, 0) != u"profile") {
        return false;
    }
    McpDraftResult ignored;
    const std::optional<Real> found = real(target, ignored);
    if (!found || found->file.empty()) {
        return false;
    }
    const std::vector<McpDraftFile> files = diff(client, target);
    return std::ranges::any_of(files, [](const McpDraftFile& file) { return file.path != u"profile.json"; });
}

McpDraftResult McpDrafts::problems(const QString& client, const QString& target) const
{
    McpDraftResult result;
    result.target = target;
    const std::optional<Meta> meta = readMeta(client, target);
    if (!meta) {
        result.code = QStringLiteral("GS-MCP-014");
        result.problem = QStringLiteral("There is no draft of %1 for this client").arg(target);
        return result;
    }
    const fs::path folder = folderOf(client, target);
    QStringList errors;
    for (const McpDraftFile& file : diff(client, target)) {
        if (file.state == u"removed") {
            continue;
        }
        const bool profile = file.path == u"profile.json";
        const bool resources = file.path.compare(QStringLiteral("res/resources.json"), Qt::CaseInsensitive) == 0;
        if (!profile && !resources) {
            continue;
        }
        const std::optional<QByteArray> bytes = readAll(folder / toPath(file.path), kMostFileBytes);
        const QString text = bytes ? mcpUtf8(*bytes).value_or(QString()) : QString();
        if (profile) {
            checkProfile(text, errors, result.warnings);
        } else {
            checkResources(text, errors);
        }
    }
    if (meta->fresh) {
        const QString kind = target.section(u':', 0, 0);
        const QString entry = kind == u"component" ? ComponentLibrary::entryName() : TaskLibrary::entryName();
        std::error_code error;
        if (!fs::is_regular_file(folder / toPath(entry), error)) {
            result.code = QStringLiteral("GS-MCP-004");
            result.problem = QStringLiteral("The draft of the new %1 %2 has no %3; write it first")
                                 .arg(kind, target.section(u':', 1), entry);
            return result;
        }
    }
    if (!errors.isEmpty()) {
        result.code = errorCodeOf(errors);
        result.problem = QStringLiteral("The draft has problems, so it cannot be applied:\n") + errors.join(u'\n');
    }
    return result;
}

std::filesystem::path McpDrafts::open(const QString& client, const QString& target, McpDraftResult& result)
{
    const fs::path folder = folderOf(client, target);
    if (has(client, target)) {
        return folder;
    }
    const std::optional<Real> found = real(target, result);
    if (!found) {
        return {};
    }
    if (found->readOnly) {
        result.code = QStringLiteral("GS-MCP-007");
        result.problem = target.startsWith(QStringLiteral("component:"))
                             ? QStringLiteral("%1 is an example component and read-only; ask the user to copy it to "
                                              "My components in GameScrcpy and change the copy")
                                   .arg(found->name)
                             : QStringLiteral("%1 comes with an extension and is read-only; ask the user to duplicate "
                                              "it in GameScrcpy and change the copy")
                                   .arg(found->name);
        return {};
    }
    std::error_code error;
    fs::remove_all(folder, error);
    error.clear();
    fs::create_directories(folder, error);
    const auto fail = [&](const QString& problem) {
        std::error_code ignored;
        fs::remove_all(folder, ignored);
        fs::remove(metaFile(client, target), ignored);
        result.code = QStringLiteral("GS-MCP-010");
        result.problem = problem;
        return fs::path();
    };
    if (error) {
        return fail(QStringLiteral("The draft folder of %1 cannot be made").arg(found->name));
    }
    Meta meta{.client = client, .target = target, .single = !found->file.empty(), .fresh = found->fresh};
    if (!found->file.empty()) {
        const std::optional<QByteArray> bytes = readAll(found->file, static_cast<std::size_t>(kMostFileBytes));
        if (!bytes || platform::writeFileAtomically(folder / "profile.json", bytes->toStdString())) {
            return fail(QStringLiteral("%1 cannot be copied into a draft").arg(found->name));
        }
        meta.copied.emplace(QStringLiteral("profile.json"), hashOf(*bytes));
    } else if (!found->fresh) {
        int count = 0;
        std::uintmax_t total = 0;
        for (const auto& [relative, file] : listFiles(found->folder)) {
            std::error_code sized;
            if (fs::file_size(file, sized) > kMostCopiedFileBytes || sized) {
                continue;
            }
            const std::optional<QByteArray> bytes = readAll(file, kMostCopiedFileBytes);
            if (!bytes) {
                return fail(QStringLiteral("%1 of %2 cannot be read, so no draft was made").arg(relative, found->name));
            }
            ++count;
            total += static_cast<std::uintmax_t>(bytes->size());
            if (count > kMostCopiedFiles || total > kMostCopiedBytes) {
                return fail(QStringLiteral("%1 holds more than %2 files or 256 MiB, so no draft was made")
                                .arg(found->name)
                                .arg(kMostCopiedFiles));
            }
            const fs::path to = folder / toPath(relative);
            fs::create_directories(to.parent_path(), error);
            if (error || platform::writeFileAtomically(to, bytes->toStdString())) {
                return fail(QStringLiteral("%1 of %2 cannot be copied into a draft").arg(relative, found->name));
            }
            meta.copied.emplace(relative, hashOf(*bytes));
        }
    }
    if (!writeMeta(meta)) {
        return fail(QStringLiteral("The draft of %1 cannot be noted").arg(found->name));
    }
    emit draftsChanged(client);
    return folder;
}

McpDraftResult McpDrafts::write(const QString& client, const QString& target, const std::vector<McpDraftWrite>& files)
{
    McpDraftResult result;
    result.target = target;
    if (files.empty() || static_cast<qsizetype>(files.size()) > kMostWrittenFiles) {
        result.code = QStringLiteral("GS-MCP-004");
        result.problem = QStringLiteral("files must hold from 1 to %1 files").arg(kMostWrittenFiles);
        return result;
    }
    const QString kind = target.section(u':', 0, 0);
    std::vector<McpDraftWrite> planned;
    QStringList errors;
    qsizetype total = 0;
    for (const McpDraftWrite& file : files) {
        QString problem;
        const QString path = mcpCheckedPath(file.path, problem);
        if (path.isEmpty() || (kind != u"profile" && path == u"profile.json") ||
            (kind == u"task" && path.compare(QStringLiteral("task.json"), Qt::CaseInsensitive) == 0)) {
            result.code = QStringLiteral("GS-MCP-004");
            result.problem = !path.isEmpty() && path == u"profile.json"
                                 ? QStringLiteral("profile.json belongs to profiles; a %1 cannot hold one").arg(kind)
                             : !path.isEmpty()
                                 ? QStringLiteral("task.json holds the task's settings in GameScrcpy and cannot be written")
                                 : problem;
            return result;
        }
        if (std::ranges::any_of(planned, [&path](const McpDraftWrite& each) {
                return each.path.compare(path, Qt::CaseInsensitive) == 0;
            })) {
            result.code = QStringLiteral("GS-MCP-004");
            result.problem = QStringLiteral("%1 is given twice").arg(path);
            return result;
        }
        const qsizetype bytes = file.text.toUtf8().size();
        total += bytes;
        if (bytes > kMostFileBytes || total > kMostWrittenBytes) {
            result.code = QStringLiteral("GS-MCP-004");
            result.problem =
                QStringLiteral("%1 makes the files too large: at most 2 MiB a file and 8 MiB in all").arg(path);
            return result;
        }
        if (path == u"profile.json") {
            checkProfile(file.text, errors, result.warnings);
        } else if (path.compare(QStringLiteral("res/resources.json"), Qt::CaseInsensitive) == 0) {
            checkResources(file.text, errors);
        }
        planned.push_back({.path = path, .text = file.text});
    }
    if (!errors.isEmpty()) {
        result.code = errorCodeOf(errors);
        result.problem = QStringLiteral("The files have problems, so nothing was written:\n") + errors.join(u'\n');
        return result;
    }
    const fs::path folder = open(client, target, result);
    if (folder.empty()) {
        return result;
    }
    for (const McpDraftWrite& file : planned) {
        const fs::path destination = folder / toPath(file.path);
        std::error_code error;
        const bool existed = fs::is_regular_file(destination, error);
        const QByteArray existing = existed ? readAll(destination, kMostCopiedFileBytes).value_or(QByteArray()) : QByteArray();
        const QByteArray bytes = mcpEncodedLike(file.text, existing);
        if (existed && bytes == existing) {
            continue;
        }
        fs::create_directories(destination.parent_path(), error);
        if (error || platform::writeFileAtomically(destination, bytes.toStdString())) {
            result.code = QStringLiteral("GS-MCP-010");
            result.problem = QStringLiteral("%1 could not be written into the draft").arg(file.path);
            if (!result.written.isEmpty()) {
                result.problem += QStringLiteral("; written before it: %1").arg(result.written.join(QStringLiteral(", ")));
            }
            emit draftsChanged(client);
            return result;
        }
        if (existed) {
            const McpLineChange change = mcpLineChange(mcpUtf8(existing).value_or(QString()), file.text);
            result.written.append(QStringLiteral("%1 (+%2 -%3)").arg(file.path).arg(change.added).arg(change.removed));
        } else {
            result.written.append(QStringLiteral("%1 (new, %2 lines)").arg(file.path).arg(mcpLinesOf(file.text).size()));
        }
    }
    if (!result.written.isEmpty()) {
        emit draftsChanged(client);
    }
    return result;
}

McpDraftResult McpDrafts::store(const QString& client, const QString& target, const QString& path,
                                const QByteArray& bytes)
{
    McpDraftResult result;
    result.target = target;
    const QString relative = QDir::fromNativeSeparators(path);
    if (!safeRelative(relative)) {
        result.code = QStringLiteral("GS-MCP-004");
        result.problem = QStringLiteral("%1 is not a path inside the folder").arg(path);
        return result;
    }
    const fs::path folder = open(client, target, result);
    if (folder.empty()) {
        return result;
    }
    const fs::path destination = folder / toPath(relative);
    std::error_code error;
    fs::create_directories(destination.parent_path(), error);
    if (error || platform::writeFileAtomically(destination, bytes.toStdString())) {
        result.code = QStringLiteral("GS-MCP-010");
        result.problem = QStringLiteral("%1 could not be written into the draft").arg(relative);
        return result;
    }
    result.written.append(relative);
    emit draftsChanged(client);
    return result;
}

bool McpDrafts::apply(const QString& client, const QString& target, QString& problem)
{
    const McpDraftResult result = apply(client, target);
    problem = result.ok() ? QString() : QString(result.code + u' ' + result.problem);
    return result.ok();
}

McpDraftResult McpDrafts::apply(const QString& client, const QString& target)
{
    McpDraftResult result = problems(client, target);
    if (!result.ok()) {
        return result;
    }
    const std::vector<McpDraftFile> files = diff(client, target);
    if (files.empty()) {
        discard(client, target);
        return result;
    }
    const std::optional<Real> found = real(target, result);
    if (!found) {
        return result;
    }
    if (found->readOnly) {
        result.code = QStringLiteral("GS-MCP-007");
        result.problem = QStringLiteral("%1 is read-only, so the draft cannot be applied").arg(found->name);
        return result;
    }
    const QString kind = target.section(u':', 0, 0);
    const QString id = target.section(u':', 1);
    fs::path folder = found->folder;
    if (found->fresh && kind == u"component") {
        const QString ref = m_setup.components != nullptr ? m_setup.components->add(id.mid(5)) : QString();
        if (ref.isEmpty()) {
            result.code = QStringLiteral("GS-MCP-010");
            result.problem = QStringLiteral("The component %1 could not be made").arg(id);
            return result;
        }
        folder = m_setup.components->folderOf(ref);
        result.target = QStringLiteral("component:") + ref;
    } else if (found->fresh) {
        const QString made = m_setup.tasks != nullptr ? m_setup.tasks->add(id) : QString();
        const TaskEntry* entry = made.isEmpty() ? nullptr : m_setup.tasks->find(made);
        if (entry == nullptr) {
            result.code = QStringLiteral("GS-MCP-010");
            result.problem = QStringLiteral("The task %1 could not be made").arg(id);
            return result;
        }
        folder = toPath(entry->folder);
        result.target = QStringLiteral("task:") + made;
    } else if (!found->file.empty() &&
               std::ranges::any_of(files, [](const McpDraftFile& file) { return file.path != u"profile.json"; })) {
        const QString made = m_setup.profiles != nullptr ? m_setup.profiles->ensureFolder(id) : QString();
        if (made.isEmpty()) {
            result.code = QStringLiteral("GS-MCP-010");
            result.problem = m_setup.profiles != nullptr && m_setup.profiles->lastWriteBusy()
                                 ? QStringLiteral("%1 could not become a folder: another program is using its file")
                                       .arg(found->name)
                                 : QStringLiteral("%1 could not become a folder").arg(found->name);
            return result;
        }
        folder = toPath(made);
    }
    const fs::path draft = folderOf(client, target);
    for (const McpDraftFile& file : files) {
        const fs::path destination = folder.empty() ? found->file : folder / toPath(file.path);
        std::error_code error;
        if (file.state == u"removed") {
            error = platform::removeFile(destination);
            if (platform::fileMissing(error)) {
                error.clear();
            }
        } else {
            const std::optional<QByteArray> bytes = readAll(draft / toPath(file.path), kMostCopiedFileBytes);
            if (!bytes) {
                error = std::make_error_code(std::errc::io_error);
            } else {
                fs::create_directories(destination.parent_path(), error);
                if (!error) {
                    error = platform::writeFileAtomically(destination, bytes->toStdString());
                }
            }
        }
        if (error) {
            result.code = QStringLiteral("GS-MCP-010");
            result.problem = platform::fileBusy(error)
                                 ? QStringLiteral("%1 was not saved: another program is using the file").arg(file.path)
                                 : QStringLiteral("%1 could not be saved (error %2)").arg(file.path).arg(error.value());
            if (!result.written.isEmpty()) {
                result.problem += QStringLiteral("; saved before it: %1").arg(result.written.join(QStringLiteral(", ")));
            }
            reloadLibrary(result.target);
            return result;
        }
        result.written.append(countText(file));
        if (file.conflict) {
            result.conflicts.append(file.path);
        }
    }
    std::error_code ignored;
    fs::remove_all(draft, ignored);
    fs::remove(metaFile(client, target), ignored);
    reloadLibrary(result.target);
    qInfo("mcp %s: applied the draft of %s: %s", qUtf8Printable(client), qUtf8Printable(result.target),
          qUtf8Printable(result.written.join(QStringLiteral(", "))));
    emit draftsChanged(client);
    return result;
}

void McpDrafts::discard(const QString& client, const QString& target)
{
    std::error_code error;
    const bool folder = fs::remove_all(folderOf(client, target), error) > 0;
    const bool meta = fs::remove(metaFile(client, target), error);
    if (folder || meta) {
        emit draftsChanged(client);
    }
}

void McpDrafts::discardAll(const QString& client)
{
    std::error_code error;
    if (fs::remove_all(clientFolder(client), error) > 0) {
        emit draftsChanged(client);
    }
}

void McpDrafts::prune()
{
    std::error_code error;
    const auto now = fs::file_time_type::clock::now();
    for (fs::directory_iterator client(m_setup.root, error), end; !error && client != end; client.increment(error)) {
        std::error_code kind;
        if (!client->is_directory(kind)) {
            continue;
        }
        std::vector<fs::path> metas;
        std::error_code listing;
        for (fs::directory_iterator entry(client->path(), listing), last; !listing && entry != last;
             entry.increment(listing)) {
            if (QString::fromStdWString(entry->path().filename().wstring()).endsWith(kMetaSuffix)) {
                metas.push_back(entry->path());
            }
        }
        for (const fs::path& meta : metas) {
            const QString name = QString::fromStdWString(meta.filename().wstring());
            const fs::path draft = meta.parent_path() / toPath(name.chopped(kMetaSuffix.size()));
            std::error_code stamp;
            auto latest = fs::last_write_time(meta, stamp);
            if (stamp) {
                continue;
            }
            for (const auto& file : listFiles(draft)) {
                std::error_code touched;
                const auto written = fs::last_write_time(file.second, touched);
                if (!touched && written > latest) {
                    latest = written;
                }
            }
            if (now - latest > kKeptFor) {
                std::error_code ignored;
                fs::remove_all(draft, ignored);
                fs::remove(meta, ignored);
                qInfo("mcp: removed a draft that was not touched for 7 days (%s)", qUtf8Printable(pathText(draft)));
            }
        }
    }
}

void McpDrafts::reloadLibrary(const QString& target) const
{
    const QString kind = target.section(u':', 0, 0);
    if (kind == u"profile" && m_setup.profiles != nullptr) {
        m_setup.profiles->reload();
    } else if (kind == u"component" && m_setup.components != nullptr) {
        m_setup.components->reload();
    } else if (kind == u"task" && m_setup.tasks != nullptr) {
        m_setup.tasks->reload();
    }
}

}
