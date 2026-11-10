#include "LogicReferences.h"

#include <gs/platform/AtomicFile.h>
#include <gs/tsstrip/Tokens.h>

#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QStringView>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

constexpr int kMostLogicDepth = 12;
constexpr qsizetype kMostLogicFiles = 2000;
constexpr std::size_t kMostSearchedBytes = std::size_t{64} << 20;
constexpr std::uint32_t kMostCodePoint = 0x10FFFF;
constexpr std::size_t kNoToken = std::string_view::npos;
constexpr std::string_view kBom = "\xEF\xBB\xBF";
constexpr std::string_view kHex = "0123456789ABCDEF";

constexpr auto kReserved = std::to_array<std::string_view>(
    {"await", "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete",
     "do", "else", "enum", "export", "extends", "false", "finally", "for", "function", "if",
     "import", "in", "instanceof", "let", "new", "null", "return", "static", "super", "switch",
     "this", "throw", "true", "try", "typeof", "var", "void", "while", "with", "yield"});

bool skippedFolder(const QString& name, int depth)
{
    if (name.startsWith(u'.') || name.compare(QStringView(u"node_modules"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    return depth == 0 && (name.compare(QStringView(u"types"), Qt::CaseInsensitive) == 0 ||
                          name.compare(QStringView(u"tests"), Qt::CaseInsensitive) == 0);
}

bool logicFile(const QString& name)
{
    const auto ends = [&name](QStringView suffix) { return name.endsWith(suffix, Qt::CaseInsensitive); };
    if (name.startsWith(u'.') || ends(QStringView(u".d.ts")) || ends(QStringView(u".d.mts"))) {
        return false;
    }
    return ends(QStringView(u".ts")) || ends(QStringView(u".mts")) || ends(QStringView(u".js"));
}

void logicFilesInto(QStringList& files, const QDir& root, const QString& relative, int depth)
{
    if (depth > kMostLogicDepth) {
        return;
    }
    const QDir directory(relative.isEmpty() ? root.path() : root.filePath(relative));
    const QFileInfoList entries =
        directory.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                                QDir::DirsLast | QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& info : entries) {
        if (files.size() >= kMostLogicFiles) {
            return;
        }
        const QString name = info.fileName();
        const QString path = relative.isEmpty() ? name : relative + u'/' + name;
        if (info.isDir()) {
            if (!skippedFolder(name, depth)) {
                logicFilesInto(files, root, path, depth + 1);
            }
        } else if (logicFile(name)) {
            files.append(path);
        }
    }
}

bool samePath(const QString& first, const QString& second)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(first))
               .compare(QDir::cleanPath(QDir::fromNativeSeparators(second)), Qt::CaseInsensitive) == 0;
}

bool literalToken(const tsstrip::CodeToken& token)
{
    if (token.kind == tsstrip::TokenKind::String) {
        return true;
    }
    return token.kind == tsstrip::TokenKind::Template && token.text.size() >= 2 && token.text.front() == '`' &&
           token.text.back() == '`';
}

bool punctuator(const tsstrip::CodeToken& token, std::string_view text)
{
    return token.kind == tsstrip::TokenKind::Punctuator && token.text == text;
}

bool opening(const tsstrip::CodeToken& token)
{
    return punctuator(token, "(") || punctuator(token, "[") || punctuator(token, "{");
}

bool closing(const tsstrip::CodeToken& token)
{
    return punctuator(token, ")") || punctuator(token, "]") || punctuator(token, "}");
}

std::size_t offsetOf(std::string_view source, std::string_view part)
{
    return static_cast<std::size_t>(part.data() - source.data());
}

struct ControlHit {
    std::size_t first = 0;
    std::size_t last = 0;
    std::size_t begin = 0;
    std::size_t end = 0;
};

std::vector<ControlHit> controlHits(std::string_view source, std::string_view id)
{
    std::vector<ControlHit> hits;
    if (id.empty()) {
        return hits;
    }
    const std::vector<tsstrip::CodeToken> tokens = tsstrip::tokenize(source);
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i].kind != tsstrip::TokenKind::Name || tokens[i].text != "controls") {
            continue;
        }
        if (i > 0 && (punctuator(tokens[i - 1], ".") || punctuator(tokens[i - 1], "?."))) {
            continue;
        }
        if (punctuator(tokens[i + 1], ".") && i + 2 < tokens.size()) {
            const tsstrip::CodeToken& member = tokens[i + 2];
            if (member.kind == tsstrip::TokenKind::Name && member.text == id) {
                hits.push_back({i, i + 2, offsetOf(source, tokens[i].text), offsetOf(source, member.text) + member.text.size()});
            }
        } else if (punctuator(tokens[i + 1], "[") && i + 3 < tokens.size()) {
            const tsstrip::CodeToken& literal = tokens[i + 2];
            if (!literalToken(literal) || !punctuator(tokens[i + 3], "]")) {
                continue;
            }
            const std::optional<std::string> value = literalValue(literal.text);
            if (value && *value == id) {
                hits.push_back({i, i + 3, offsetOf(source, tokens[i].text), offsetOf(source, tokens[i + 3].text) + 1});
            }
        }
    }
    return hits;
}

std::size_t matchingOpen(const std::vector<tsstrip::CodeToken>& tokens, std::size_t close)
{
    int depth = 0;
    for (std::size_t i = close + 1; i-- > 0;) {
        if (closing(tokens[i])) {
            ++depth;
        } else if (opening(tokens[i])) {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return kNoToken;
}

void widenUnbraced(const std::vector<tsstrip::CodeToken>& tokens, std::size_t& first)
{
    if (first == 0) {
        return;
    }
    const tsstrip::CodeToken& before = tokens[first - 1];
    std::size_t close = kNoToken;
    if (punctuator(before, ")")) {
        close = first - 1;
    } else {
        return;
    }
    const std::size_t open = matchingOpen(tokens, close);
    if (open == kNoToken || open == 0) {
        return;
    }
    const tsstrip::CodeToken& keyword = tokens[open - 1];
    if (keyword.kind != tsstrip::TokenKind::Name) {
        return;
    }
    if (keyword.text == "if" || keyword.text == "for" || keyword.text == "while" || keyword.text == "with" ||
        keyword.text == "switch" || keyword.text == "catch") {
        first = open - 1;
    }
}

std::size_t statementStart(const std::vector<tsstrip::CodeToken>& tokens, std::size_t at)
{
    int depth = 0;
    for (std::size_t i = at; i-- > 0;) {
        const tsstrip::CodeToken& token = tokens[i];
        if (!closing(token) && !opening(token) && !punctuator(token, ";")) {
            continue;
        }
        if (closing(token)) {
            if (depth == 0 && punctuator(token, "}")) {
                return i + 1;
            }
            ++depth;
            continue;
        }
        if (opening(token)) {
            if (depth == 0) {
                return i + 1;
            }
            --depth;
            continue;
        }
        if (depth == 0) {
            return i + 1;
        }
    }
    return 0;
}

std::size_t statementEnd(const std::vector<tsstrip::CodeToken>& tokens, std::size_t at)
{
    int depth = 0;
    for (std::size_t i = at; i < tokens.size(); ++i) {
        const tsstrip::CodeToken& token = tokens[i];
        if (opening(token)) {
            ++depth;
            continue;
        }
        if (closing(token)) {
            if (depth == 0) {
                return i;
            }
            --depth;
            if (depth == 0 && punctuator(token, "}")) {
                return i + 1;
            }
            continue;
        }
        if (depth == 0 && punctuator(token, ";")) {
            return i + 1;
        }
    }
    return tokens.size();
}

std::optional<std::uint32_t> hexValue(std::string_view digits)
{
    if (digits.empty()) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (const char c : digits) {
        std::uint32_t digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<std::uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<std::uint32_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<std::uint32_t>(c - 'A' + 10);
        } else {
            return std::nullopt;
        }
        if (value > kMostCodePoint) {
            return std::nullopt;
        }
        value = value * 16 + digit;
    }
    return value;
}

std::optional<std::uint32_t> unicodeEscape(std::string_view body, std::size_t& at)
{
    if (at < body.size() && body[at] == '{') {
        const std::size_t close = body.find('}', at + 1);
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        const std::optional<std::uint32_t> value = hexValue(body.substr(at + 1, close - at - 1));
        if (!value || *value > kMostCodePoint) {
            return std::nullopt;
        }
        at = close + 1;
        return value;
    }
    if (body.size() - at < 4) {
        return std::nullopt;
    }
    const std::optional<std::uint32_t> value = hexValue(body.substr(at, 4));
    if (value) {
        at += 4;
    }
    return value;
}

void appendUtf8(std::string& text, std::uint32_t point)
{
    if (point < 0x80) {
        text += static_cast<char>(point);
    } else if (point < 0x800) {
        text += static_cast<char>(0xC0 | (point >> 6));
        text += static_cast<char>(0x80 | (point & 0x3F));
    } else if (point < 0x10000) {
        text += static_cast<char>(0xE0 | (point >> 12));
        text += static_cast<char>(0x80 | ((point >> 6) & 0x3F));
        text += static_cast<char>(0x80 | (point & 0x3F));
    } else {
        text += static_cast<char>(0xF0 | (point >> 18));
        text += static_cast<char>(0x80 | ((point >> 12) & 0x3F));
        text += static_cast<char>(0x80 | ((point >> 6) & 0x3F));
        text += static_cast<char>(0x80 | (point & 0x3F));
    }
}

class LineIndex
{
public:
    explicit LineIndex(std::string_view source) : m_source(source)
    {
        const std::size_t first = source.starts_with(kBom) ? kBom.size() : 0;
        m_starts.push_back(first);
        for (std::size_t at = first; at < source.size(); ++at) {
            const char c = source[at];
            if (c == '\n' || (c == '\r' && (at + 1 >= source.size() || source[at + 1] != '\n'))) {
                m_starts.push_back(at + 1);
            }
        }
    }

    [[nodiscard]] LogicReference reference(const QString& file, ReferenceKind kind, std::size_t start,
                                           std::size_t size) const
    {
        const auto after = std::ranges::upper_bound(m_starts, start);
        const auto row = static_cast<std::size_t>(std::max<std::ptrdiff_t>(after - m_starts.begin(), 1) - 1);
        const std::size_t lineStart = std::min(m_starts[row], start);
        const std::size_t lineEnd = std::min(m_source.find_first_of("\r\n", lineStart), m_source.size());
        return {.file = file,
                .kind = kind,
                .line = static_cast<int>(row + 1),
                .column = utf16Length(lineStart, start) + 1,
                .length = utf16Length(start, start + size),
                .start = start,
                .end = start + size,
                .lineText = QString::fromUtf8(m_source.data() + lineStart, static_cast<qsizetype>(lineEnd - lineStart))};
    }

private:
    [[nodiscard]] int utf16Length(std::size_t from, std::size_t to) const
    {
        return static_cast<int>(QString::fromUtf8(m_source.data() + from, static_cast<qsizetype>(to - from)).size());
    }

    std::string_view m_source;
    std::vector<std::size_t> m_starts;
};

}

std::optional<std::string> literalValue(std::string_view literal)
{
    if (literal.size() < 2) {
        return std::nullopt;
    }
    const char quote = literal.front();
    if ((quote != '"' && quote != '\'' && quote != '`') || literal.back() != quote) {
        return std::nullopt;
    }
    const std::string_view body = literal.substr(1, literal.size() - 2);
    std::string value;
    value.reserve(body.size());
    std::size_t at = 0;
    while (at < body.size()) {
        const char c = body[at];
        if (c == '\r' && quote == '`') {
            value += '\n';
            at += at + 1 < body.size() && body[at + 1] == '\n' ? std::size_t{2} : std::size_t{1};
            continue;
        }
        if (c != '\\') {
            value += c;
            ++at;
            continue;
        }
        if (at + 1 >= body.size()) {
            return std::nullopt;
        }
        const char escaped = body[at + 1];
        at += 2;
        switch (escaped) {
        case 'n':
            value += '\n';
            break;
        case 't':
            value += '\t';
            break;
        case 'r':
            value += '\r';
            break;
        case 'b':
            value += '\b';
            break;
        case 'f':
            value += '\f';
            break;
        case 'v':
            value += '\v';
            break;
        case '0':
            if (at < body.size() && body[at] >= '0' && body[at] <= '9') {
                return std::nullopt;
            }
            value += '\0';
            break;
        case 'x': {
            const std::optional<std::uint32_t> point =
                body.size() - at >= 2 ? hexValue(body.substr(at, 2)) : std::nullopt;
            if (!point) {
                return std::nullopt;
            }
            appendUtf8(value, *point);
            at += 2;
            break;
        }
        case 'u': {
            std::optional<std::uint32_t> point = unicodeEscape(body, at);
            if (!point || (*point >= 0xDC00 && *point <= 0xDFFF)) {
                return std::nullopt;
            }
            if (*point >= 0xD800 && *point <= 0xDBFF) {
                std::size_t next = at + 2;
                const std::optional<std::uint32_t> low =
                    body.substr(at, 2) == "\\u" ? unicodeEscape(body, next) : std::nullopt;
                if (!low || *low < 0xDC00 || *low > 0xDFFF) {
                    return std::nullopt;
                }
                point = 0x10000 + ((*point - 0xD800) << 10) + (*low - 0xDC00);
                at = next;
            }
            appendUtf8(value, *point);
            break;
        }
        case '\r':
            if (at < body.size() && body[at] == '\n') {
                ++at;
            }
            break;
        case '\n':
            break;
        default:
            if (escaped >= '1' && escaped <= '9') {
                return std::nullopt;
            }
            if (escaped == '\xE2' && at + 1 < body.size() && body[at] == '\x80' &&
                (body[at + 1] == '\xA8' || body[at + 1] == '\xA9')) {
                at += 2;
                break;
            }
            value += escaped;
            break;
        }
    }
    return value;
}

std::string quotedLiteral(std::string_view value, char quote)
{
    std::string text(1, quote);
    for (std::size_t at = 0; at < value.size(); ++at) {
        const char c = value[at];
        const auto byte = static_cast<unsigned char>(c);
        if (c == '\\' || c == quote) {
            text += '\\';
            text += c;
        } else if (c == '\n') {
            text += "\\n";
        } else if (c == '\r') {
            text += "\\r";
        } else if (c == '$' && quote == '`' && at + 1 < value.size() && value[at + 1] == '{') {
            text += "\\$";
        } else if (byte < 0x20 || byte == 0x7F) {
            text += "\\x";
            text += kHex[static_cast<std::size_t>(byte >> 4)];
            text += kHex[static_cast<std::size_t>(byte & 0x0F)];
        } else if (c == '\xE2' && at + 2 < value.size() && value[at + 1] == '\x80' &&
                   (value[at + 2] == '\xA8' || value[at + 2] == '\xA9')) {
            text += value[at + 2] == '\xA8' ? "\\u2028" : "\\u2029";
            at += 2;
        } else {
            text += c;
        }
    }
    text += quote;
    return text;
}

QStringList logicFilesIn(const std::filesystem::path& folder)
{
    QStringList files;
    logicFilesInto(files, QDir(QString::fromStdWString(folder.wstring())), {}, 0);
    return files;
}

std::vector<LogicReference> referencesIn(std::string_view source, const QString& file, const ReferenceQuery& query)
{
    std::vector<LogicReference> found;
    if (query.text.empty() || (!query.strings && !query.names)) {
        return found;
    }
    std::optional<LineIndex> lines;
    for (const tsstrip::CodeToken& token : tsstrip::tokenize(source)) {
        bool hit = false;
        ReferenceKind kind = ReferenceKind::Name;
        if (token.kind == tsstrip::TokenKind::Name) {
            hit = query.names && token.text == query.text;
        } else if (query.strings && literalToken(token)) {
            const std::optional<std::string> value = literalValue(token.text);
            hit = value.has_value() && *value == query.text;
            kind = ReferenceKind::String;
        }
        if (!hit) {
            continue;
        }
        if (!lines) {
            lines.emplace(source);
        }
        found.push_back(lines->reference(file, kind, offsetOf(source, token.text), token.text.size()));
    }
    return found;
}

ReferenceSearch findLogicReferences(const std::filesystem::path& folder, const ReferenceQuery& query,
                                    const OpenTexts& open)
{
    ReferenceSearch search;
    std::size_t searched = 0;
    const QStringList files = logicFilesIn(folder);
    for (const QString& file : files) {
        const auto given =
            std::ranges::find_if(open, [&file](const auto& entry) { return samePath(entry.first, file); });
        std::string contents;
        if (given != open.end()) {
            contents.assign(given->second.constData(), static_cast<std::size_t>(given->second.size()));
        } else if (searched >= kMostSearchedBytes ||
                   platform::readFile(folder / file.toStdWString(), kMostLogicFileBytes, contents,
                                      platform::WhenBusy::GiveUp)) {
            search.unread.append(file);
            continue;
        }
        searched += contents.size();
        std::vector<LogicReference> found = referencesIn(contents, file, query);
        search.found.insert(search.found.end(), std::make_move_iterator(found.begin()),
                            std::make_move_iterator(found.end()));
    }
    return search;
}

std::optional<ReferenceTarget> referenceTargetAt(const QString& text, int position)
{
    const qsizetype at = std::clamp<qsizetype>(position, 0, text.size());
    const QByteArray bytes = text.toUtf8();
    const std::string_view source(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    const auto offset = static_cast<std::size_t>(QStringView(text).first(at).toUtf8().size());
    const std::vector<tsstrip::CodeToken> tokens = tsstrip::tokenize(source);
    const tsstrip::CodeToken* inside = nullptr;
    const tsstrip::CodeToken* before = nullptr;
    for (const tsstrip::CodeToken& token : tokens) {
        const std::size_t start = offsetOf(source, token.text);
        if (start > offset) {
            break;
        }
        if (token.kind != tsstrip::TokenKind::Name && !literalToken(token)) {
            continue;
        }
        if (offset < start + token.text.size()) {
            inside = &token;
            break;
        }
        if (offset == start + token.text.size()) {
            before = &token;
        }
    }
    const tsstrip::CodeToken* chosen = inside != nullptr ? inside : before;
    if (chosen == nullptr) {
        return std::nullopt;
    }
    if (chosen->kind == tsstrip::TokenKind::Name) {
        if (std::ranges::find(kReserved, chosen->text) != kReserved.end()) {
            return std::nullopt;
        }
        return ReferenceTarget{
            .text = QString::fromUtf8(chosen->text.data(), static_cast<qsizetype>(chosen->text.size())),
            .kind = ReferenceKind::Name};
    }
    const std::optional<std::string> value = literalValue(chosen->text);
    if (!value || value->empty()) {
        return std::nullopt;
    }
    return ReferenceTarget{.text = QString::fromStdString(*value), .kind = ReferenceKind::String};
}

LiteralRename renameLiterals(std::string_view source, std::string_view from, std::string_view to)
{
    LiteralRename renamed;
    std::size_t copied = 0;
    for (const tsstrip::CodeToken& token : tsstrip::tokenize(source)) {
        if (!literalToken(token)) {
            continue;
        }
        const std::optional<std::string> value = literalValue(token.text);
        if (!value || *value != from) {
            continue;
        }
        const std::size_t start = offsetOf(source, token.text);
        renamed.text.append(source.substr(copied, start - copied));
        renamed.text += quotedLiteral(to, token.text.front());
        copied = start + token.text.size();
        ++renamed.changed;
    }
    renamed.text.append(source.substr(copied));
    return renamed;
}

std::vector<ControlUse> controlUsesIn(std::string_view source, const QString& file, std::string_view id)
{
    std::vector<ControlUse> found;
    const std::vector<ControlHit> hits = controlHits(source, id);
    if (hits.empty()) {
        return found;
    }
    const std::vector<tsstrip::CodeToken> tokens = tsstrip::tokenize(source);
    const LineIndex lines(source);
    std::vector<std::pair<std::size_t, std::size_t>> statements;
    for (const ControlHit& hit : hits) {
        std::size_t first = statementStart(tokens, hit.first);
        while (first > 0 && (punctuator(tokens[first - 1], "(") || punctuator(tokens[first - 1], "["))) {
            first = statementStart(tokens, first - 1);
        }
        widenUnbraced(tokens, first);
        const std::size_t end = statementEnd(tokens, first);
        if (first >= tokens.size()) {
            continue;
        }
        const std::size_t begin = offsetOf(source, tokens[first].text);
        const std::size_t last = std::min(end, tokens.size()) - 1;
        const std::size_t after = end > first ? offsetOf(source, tokens[last].text) + tokens[last].text.size() : begin;
        if (after <= begin) {
            continue;
        }
        const bool known = std::ranges::any_of(statements, [&](const auto& span) {
            return span.first <= begin && after <= span.second;
        });
        if (known) {
            continue;
        }
        statements.emplace_back(begin, after);
        LogicReference place = lines.reference(file, ReferenceKind::Name, hit.begin, hit.end - hit.begin);
        place.start = begin;
        place.end = after;
        found.push_back({.file = file,
                         .line = place.line,
                         .column = place.column,
                         .length = place.length,
                         .start = begin,
                         .end = after,
                         .useStart = hit.begin,
                         .useEnd = hit.end,
                         .lineText = place.lineText});
    }
    return found;
}

ControlUseSearch findControlUses(const std::filesystem::path& folder, std::string_view id, const OpenTexts& open)
{
    ControlUseSearch search;
    std::size_t searched = 0;
    for (const QString& file : logicFilesIn(folder)) {
        const auto given = std::ranges::find_if(open, [&file](const auto& entry) { return samePath(entry.first, file); });
        std::string contents;
        if (given != open.end()) {
            contents.assign(given->second.constData(), static_cast<std::size_t>(given->second.size()));
        } else if (searched >= kMostSearchedBytes ||
                   platform::readFile(folder / file.toStdWString(), kMostLogicFileBytes, contents,
                                      platform::WhenBusy::GiveUp)) {
            search.unread.append(file);
            continue;
        }
        searched += contents.size();
        std::vector<ControlUse> found = controlUsesIn(contents, file, id);
        search.found.insert(search.found.end(), std::make_move_iterator(found.begin()),
                            std::make_move_iterator(found.end()));
    }
    return search;
}

ControlRewrite renameControlUses(std::string_view source, std::string_view from, std::string_view to)
{
    ControlRewrite renamed;
    const std::vector<ControlHit> hits = controlHits(source, from);
    std::size_t copied = 0;
    for (const ControlHit& hit : hits) {
        const std::string_view text = source.substr(hit.begin, hit.end - hit.begin);
        const bool bracket = text.find('[') != std::string_view::npos;
        renamed.text.append(source.substr(copied, hit.begin - copied));
        if (!bracket) {
            renamed.text += "controls.";
            renamed.text += to;
        } else {
            const std::size_t open = source.find_first_of("\"'`", hit.begin);
            const char quote = open != std::string_view::npos && open < hit.end ? source[open] : '"';
            renamed.text += "controls[";
            renamed.text += quotedLiteral(to, quote);
            renamed.text += ']';
        }
        copied = hit.end;
        ++renamed.changed;
    }
    renamed.text.append(source.substr(copied));
    return renamed;
}

ControlRewrite removeControlUses(std::string_view source, std::string_view id)
{
    ControlRewrite removed;
    const std::vector<ControlUse> uses = controlUsesIn(source, QString(), id);
    if (uses.empty()) {
        removed.text = std::string(source);
        return removed;
    }
    std::vector<std::pair<std::size_t, std::size_t>> spans;
    for (const ControlUse& use : uses) {
        spans.emplace_back(use.start, use.end);
    }
    std::ranges::sort(spans);
    std::vector<std::pair<std::size_t, std::size_t>> merged;
    for (const auto& span : spans) {
        if (!merged.empty() && span.first <= merged.back().second) {
            merged.back().second = std::max(merged.back().second, span.second);
            continue;
        }
        merged.push_back(span);
    }
    std::size_t copied = 0;
    for (const auto& [begin, end] : merged) {
        std::size_t start = begin;
        while (start > copied && (source[start - 1] == ' ' || source[start - 1] == '\t')) {
            --start;
        }
        const bool alone = start == 0 || source[start - 1] == '\n';
        std::size_t after = end;
        while (after < source.size() && (source[after] == ' ' || source[after] == '\t')) {
            ++after;
        }
        if (alone && after < source.size() && source[after] == '\r') {
            ++after;
        }
        if (alone && after < source.size() && source[after] == '\n') {
            ++after;
        }
        removed.text.append(source.substr(copied, start - copied));
        copied = after;
        ++removed.changed;
    }
    removed.text.append(source.substr(copied));
    return removed;
}

bool listedPath(const QStringList& paths, const QString& path)
{
    return std::ranges::any_of(paths, [&path](const QString& listed) { return samePath(listed, path); });
}
}
