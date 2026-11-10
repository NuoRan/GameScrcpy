#include "ScriptHighlighter.h"

#include <QTextDocument>

#include <algorithm>
#include <array>
#include <string_view>

namespace gs::app {

namespace {

constexpr auto kKeywords = std::to_array<std::u16string_view>({
    u"abstract", u"as",       u"async",      u"await",     u"break",      u"case",      u"catch",
    u"class",    u"const",    u"continue",   u"debugger",  u"declare",    u"default",   u"delete",
    u"do",       u"else",     u"enum",       u"export",    u"extends",    u"false",     u"finally",
    u"for",      u"from",     u"function",   u"if",        u"implements", u"import",    u"in",
    u"instanceof", u"interface", u"keyof",   u"let",       u"namespace",  u"new",       u"null",
    u"of",       u"private",  u"protected",  u"public",    u"readonly",   u"return",    u"satisfies",
    u"static",   u"super",    u"switch",     u"this",      u"throw",      u"true",      u"try",
    u"type",     u"typeof",   u"undefined",  u"var",       u"void",       u"while",     u"with",
    u"yield",
});

constexpr auto kPrimitiveTypes = std::to_array<std::u16string_view>({
    u"any", u"bigint", u"boolean", u"never", u"null", u"number", u"object", u"string", u"symbol",
    u"this", u"undefined", u"unknown", u"void",
});

template <std::size_t N>
bool isOneOf(QStringView word, const std::array<std::u16string_view, N>& words)
{
    return std::ranges::any_of(words, [word](std::u16string_view candidate) {
        return word == QStringView(candidate.data(), static_cast<qsizetype>(candidate.size()));
    });
}

bool startsType(QStringView word)
{
    return word.front().isUpper() || isOneOf(word, kPrimitiveTypes);
}

bool hexDigit(QChar c) noexcept
{
    return c.isDigit() || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F');
}

qsizetype quotedEnd(QStringView text, qsizetype from, QChar quote, bool& closed)
{
    qsizetype at = from;
    while (at < text.size()) {
        const QChar c = text[at];
        if (c == u'\\') {
            at += 2;
            continue;
        }
        if (c == quote) {
            closed = true;
            return at + 1;
        }
        ++at;
    }
    closed = false;
    return text.size();
}

qsizetype digitsEnd(QStringView text, qsizetype at, bool hex)
{
    while (at < text.size() && (text[at] == u'_' || (hex ? hexDigit(text[at]) : text[at].isDigit()))) {
        ++at;
    }
    return at;
}

qsizetype numberEnd(QStringView text, qsizetype from)
{
    const qsizetype size = text.size();
    qsizetype at = from;
    const QChar mark = at + 1 < size ? text[at + 1].toLower() : QChar();
    if (text[at] == u'0' && (mark == u'x' || mark == u'b' || mark == u'o')) {
        at = digitsEnd(text, at + 2, true);
    } else {
        at = digitsEnd(text, at, false);
        if (at < size && text[at] == u'.') {
            at = digitsEnd(text, at + 1, false);
        }
        if (at < size && (text[at] == u'e' || text[at] == u'E')) {
            qsizetype exponent = at + 1;
            if (exponent < size && (text[exponent] == u'+' || text[exponent] == u'-')) {
                ++exponent;
            }
            if (exponent < size && text[exponent].isDigit()) {
                at = digitsEnd(text, exponent, false);
            }
        }
    }
    if (at < size && text[at] == u'n') {
        ++at;
    }
    return at;
}

}

bool identifierStart(QChar c) noexcept
{
    return c.isLetter() || c == u'_' || c == u'$';
}

bool identifierPart(QChar c) noexcept
{
    return c.isLetterOrNumber() || c == u'_' || c == u'$';
}

ScriptLine scriptLine(QStringView text, int state)
{
    ScriptLine line;
    const qsizetype size = text.size();
    const auto add = [&line](qsizetype start, qsizetype end, ScriptToken token) {
        if (end > start) {
            line.spans.push_back(
                {.start = static_cast<int>(start), .length = static_cast<int>(end - start), .token = token});
        }
    };
    qsizetype at = 0;
    QChar previous;
    if (state == kScriptInComment) {
        const qsizetype close = text.indexOf(u"*/");
        if (close < 0) {
            add(0, size, ScriptToken::Comment);
            line.state = kScriptInComment;
            return line;
        }
        add(0, close + 2, ScriptToken::Comment);
        at = close + 2;
    } else if (state == kScriptInTemplate) {
        bool closed = false;
        const qsizetype end = quotedEnd(text, 0, u'`', closed);
        add(0, end, ScriptToken::String);
        if (!closed) {
            line.state = kScriptInTemplate;
            return line;
        }
        at = end;
        previous = u'`';
    }
    bool expectType = false;
    bool inType = false;
    int angles = 0;
    while (at < size) {
        const QChar c = text[at];
        if (c.isSpace()) {
            ++at;
            continue;
        }
        const QChar next = at + 1 < size ? text[at + 1] : QChar();
        if (c == u'/' && next == u'/') {
            add(at, size, ScriptToken::Comment);
            break;
        }
        if (c == u'/' && next == u'*') {
            const qsizetype close = text.indexOf(u"*/", at + 2);
            if (close < 0) {
                add(at, size, ScriptToken::Comment);
                line.state = kScriptInComment;
                return line;
            }
            add(at, close + 2, ScriptToken::Comment);
            at = close + 2;
            continue;
        }
        if (c == u'"' || c == u'\'' || c == u'`') {
            bool closed = false;
            const qsizetype end = quotedEnd(text, at + 1, c, closed);
            add(at, end, ScriptToken::String);
            if (!closed) {
                if (c == u'`') {
                    line.state = kScriptInTemplate;
                } else {
                    line.open = true;
                }
                return line;
            }
            at = end;
            previous = c;
            expectType = false;
            inType = false;
            angles = 0;
            continue;
        }
        if (c.isDigit() || (c == u'.' && next.isDigit())) {
            const qsizetype end = numberEnd(text, at);
            add(at, end, ScriptToken::Number);
            at = end;
            previous = u'0';
            expectType = false;
            inType = false;
            angles = 0;
            continue;
        }
        if (identifierStart(c)) {
            qsizetype end = at + 1;
            while (end < size && identifierPart(text[end])) {
                ++end;
            }
            const QStringView word = text.sliced(at, end - at);
            if (previous != u'.') {
                if ((expectType || inType) && (inType || startsType(word))) {
                    add(at, end, ScriptToken::Type);
                    inType = true;
                } else {
                    inType = false;
                    angles = 0;
                    if (isOneOf(word, kKeywords)) {
                        add(at, end, ScriptToken::Keyword);
                    }
                }
            }
            expectType = false;
            at = end;
            previous = u'a';
            continue;
        }
        if (c == u':') {
            expectType = true;
            inType = false;
            angles = 0;
        } else if (inType && c == u'<') {
            ++angles;
        } else if (inType && c == u'>') {
            angles = std::max(0, angles - 1);
        } else if (!(inType && (c == u'|' || c == u'&' || c == u'[' || c == u']' || c == u'.' ||
                                (c == u',' && angles > 0)))) {
            expectType = false;
            inType = false;
            angles = 0;
        }
        previous = c;
        ++at;
    }
    return line;
}

bool insideLiteral(QStringView text, int state, int column)
{
    const qsizetype cut = std::clamp<qsizetype>(column, 0, text.size());
    const QStringView before = text.first(cut);
    const ScriptLine line = scriptLine(before, state);
    if (line.state != kScriptPlain || line.open) {
        return true;
    }
    if (line.spans.empty()) {
        return false;
    }
    const ScriptSpan& last = line.spans.back();
    return last.token == ScriptToken::Comment && last.start + last.length == cut &&
           before.sliced(last.start).startsWith(u"//");
}

ScriptHighlighter::ScriptHighlighter(QTextDocument* document)
    : QSyntaxHighlighter(document)
{
}

void ScriptHighlighter::setColors(const std::array<QColor, kScriptTokenKinds>& colors)
{
    bool changed = false;
    for (std::size_t i = 0; i < colors.size(); ++i) {
        if (!m_formats[i].hasProperty(QTextFormat::ForegroundBrush) || m_formats[i].foreground().color() != colors[i]) {
            m_formats[i].setForeground(colors[i]);
            changed = true;
        }
    }
    if (changed) {
        rehighlight();
    }
}

void ScriptHighlighter::highlightBlock(const QString& text)
{
    const ScriptLine line = scriptLine(text, std::max(previousBlockState(), kScriptPlain));
    for (const ScriptSpan& span : line.spans) {
        setFormat(span.start, span.length, m_formats[static_cast<std::size_t>(span.token)]);
    }
    setCurrentBlockState(line.state);
}

}
