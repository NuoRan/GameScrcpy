#include "Declarations.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <utility>

namespace gs::app {

namespace {

struct BuiltIn {
    const char* name;
    const char* type;
};

constexpr std::array<BuiltIn, 17> kBuiltIns{{{"Tap", "tap"},
                                             {"Hold", "hold"},
                                             {"LongPress", "longPress"},
                                             {"Turbo", "turbo"},
                                             {"Swipe", "swipe"},
                                             {"Pinch", "pinch"},
                                             {"Wasd", "wasd"},
                                             {"RightWalk", "rightWalk"},
                                             {"PadStick", "padStick"},
                                             {"Camera", "camera"},
                                             {"PadCamera", "padCamera"},
                                             {"FreeLook", "freeLook"},
                                             {"Skill", "skill"},
                                             {"Dial", "dial"},
                                             {"MultiPress", "multiPress"},
                                             {"TempCursor", "tempCursor"},
                                             {"ActionWheel", "actionWheel"}}};

int commentEnd(const QString& text, int at)
{
    if (text[at + 1] == u'/') {
        const int end = static_cast<int>(text.indexOf(u'\n', at));
        return end < 0 ? static_cast<int>(text.size()) - 1 : end;
    }
    const int end = static_cast<int>(text.indexOf(QStringLiteral("*/"), at + 2));
    return end < 0 ? static_cast<int>(text.size()) - 1 : end + 1;
}

bool commentAt(const QString& text, int at)
{
    return text[at] == u'/' && at + 1 < text.size() && (text[at + 1] == u'/' || text[at + 1] == u'*');
}

std::vector<int> depthsOf(const QString& text)
{
    std::vector<int> depths(static_cast<std::size_t>(text.size()), -1);
    int depth = 0;
    QChar quote;
    for (int at = 0; at < text.size(); ++at) {
        const QChar c = text[at];
        if (!quote.isNull()) {
            if (c == u'\\') {
                ++at;
            } else if (c == quote) {
                quote = QChar();
            }
            continue;
        }
        if (commentAt(text, at)) {
            at = commentEnd(text, at);
            continue;
        }
        if (c == u'"' || c == u'\'' || c == u'`') {
            quote = c;
            continue;
        }
        if (c == u')' || c == u'}' || c == u']') {
            --depth;
        }
        depths[static_cast<std::size_t>(at)] = depth;
        if (c == u'(' || c == u'{' || c == u'[') {
            ++depth;
        }
    }
    return depths;
}

int closingOf(const QString& text, int open)
{
    int depth = 0;
    QChar quote;
    for (int at = open; at < text.size(); ++at) {
        const QChar c = text[at];
        if (!quote.isNull()) {
            if (c == u'\\') {
                ++at;
            } else if (c == quote) {
                quote = QChar();
            }
            continue;
        }
        if (commentAt(text, at)) {
            at = commentEnd(text, at);
            continue;
        }
        if (c == u'"' || c == u'\'' || c == u'`') {
            quote = c;
        } else if (c == u'(' || c == u'{' || c == u'[') {
            ++depth;
        } else if ((c == u')' || c == u'}' || c == u']') && --depth == 0) {
            return at;
        }
    }
    return -1;
}

QStringList partsOf(const QString& text)
{
    QStringList parts;
    const std::vector<int> depths = depthsOf(text);
    int start = 0;
    for (int at = 0; at < text.size(); ++at) {
        if (text[at] == u',' && depths[static_cast<std::size_t>(at)] == 0) {
            parts.append(text.mid(start, at - start));
            start = at + 1;
        }
    }
    parts.append(text.mid(start));
    return parts;
}

int firstComma(const QString& text)
{
    int depth = 0;
    QChar quote;
    for (int at = 0; at < text.size(); ++at) {
        const QChar c = text[at];
        if (!quote.isNull()) {
            if (c == u'\\') {
                ++at;
            } else if (c == quote) {
                quote = QChar();
            }
            continue;
        }
        if (c == u'"' || c == u'\'' || c == u'`') {
            quote = c;
        } else if (c == u'(' || c == u'{' || c == u'[') {
            ++depth;
        } else if (c == u')' || c == u'}' || c == u']') {
            --depth;
        } else if (c == u',' && depth == 0) {
            return at;
        }
    }
    return -1;
}

bool quoted(const QString& text)
{
    return text.size() >= 2 && (text.front() == u'"' || text.front() == u'\'') && text.back() == text.front();
}

QString unquoted(const QString& text)
{
    const QString trimmed = text.trimmed();
    return quoted(trimmed) ? trimmed.mid(1, trimmed.size() - 2) : trimmed;
}

bool readFirst(DeclaredSpec& spec, const QString& first)
{
    if (spec.kind == u"number") {
        bool ok = false;
        const double number = first.toDouble(&ok);
        if (!ok) {
            return false;
        }
        spec.value = number;
    } else if (spec.kind == u"toggle") {
        if (first != u"true" && first != u"false") {
            return false;
        }
        spec.value = first == u"true";
    } else if (spec.kind == u"text") {
        if (!quoted(first)) {
            return false;
        }
        spec.value = unquoted(first);
    } else if (spec.kind == u"key") {
        if (first.isEmpty() || first == u"null" || first == u"undefined") {
            return true;
        }
        if (!quoted(first)) {
            return false;
        }
        spec.value = chordForKey(unquoted(first));
    } else if (spec.kind == u"component") {
        if (quoted(first)) {
            spec.value = unquoted(first);
        } else {
            spec.of = builtInType(first);
        }
    }
    return true;
}

}

std::vector<DeclaredSpec> declaredIn(const QString& code, const QString& field, const QString& maker,
                                     const QStringList& kinds)
{
    std::vector<DeclaredSpec> found;
    const QRegularExpression head(QStringLiteral(R"(static\s+%1\s*=\s*\{)").arg(QRegularExpression::escape(field)));
    const QRegularExpressionMatch start = head.match(code);
    if (!start.hasMatch()) {
        return found;
    }
    const int open = static_cast<int>(start.capturedEnd()) - 1;
    const int close = closingOf(code, open);
    if (close < 0) {
        return found;
    }
    const QString body = code.mid(open, close - open + 1);
    const QRegularExpression item(
        QStringLiteral(R"((?<![\p{L}\p{N}_$.])([\p{L}_$][\p{L}\p{N}_$]*|"[^"]*"|'[^']*')\s*:\s*%1\s*\.\s*)"
                       R"((%2)\s*\()")
            .arg(QRegularExpression::escape(maker), kinds.join(u'|')),
        QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression label(QStringLiteral(R"(label\s*:\s*("[^"]*"|'[^']*'))"));
    static const QRegularExpression least(QStringLiteral(R"(min\s*:\s*(-?\d+(?:\.\d+)?))"));
    static const QRegularExpression most(QStringLiteral(R"(max\s*:\s*(-?\d+(?:\.\d+)?))"));
    QRegularExpressionMatchIterator matches = item.globalMatch(body);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const int argumentsOpen = static_cast<int>(match.capturedEnd()) - 1;
        const int argumentsClose = closingOf(body, argumentsOpen);
        if (argumentsClose < 0) {
            break;
        }
        const QString arguments = body.mid(argumentsOpen + 1, argumentsClose - argumentsOpen - 1);
        const int comma = firstComma(arguments);
        const QString first = (comma < 0 ? arguments : arguments.left(comma)).trimmed();
        const QString options = comma < 0 ? QString() : arguments.mid(comma + 1);
        DeclaredSpec spec{.name = unquoted(match.captured(1)), .kind = match.captured(2)};
        if (!readFirst(spec, first)) {
            continue;
        }
        if (const QRegularExpressionMatch hit = label.match(options); hit.hasMatch()) {
            spec.label = unquoted(hit.captured(1));
        }
        if (const QRegularExpressionMatch hit = least.match(options); hit.hasMatch()) {
            spec.min = hit.captured(1).toDouble();
        }
        if (const QRegularExpressionMatch hit = most.match(options); hit.hasMatch()) {
            spec.max = hit.captured(1).toDouble();
        }
        found.push_back(std::move(spec));
    }
    return found;
}

QString chordForKey(const QString& name)
{
    static const QHash<QString, QString> aliases{
        {QStringLiteral("Esc"), QStringLiteral("Escape")},       {QStringLiteral("Ctrl"), QStringLiteral("ControlLeft")},
        {QStringLiteral("Control"), QStringLiteral("ControlLeft")}, {QStringLiteral("Shift"), QStringLiteral("ShiftLeft")},
        {QStringLiteral("Alt"), QStringLiteral("AltLeft")},      {QStringLiteral("Up"), QStringLiteral("ArrowUp")},
        {QStringLiteral("Down"), QStringLiteral("ArrowDown")},   {QStringLiteral("Left"), QStringLiteral("ArrowLeft")},
        {QStringLiteral("Right"), QStringLiteral("ArrowRight")}, {QStringLiteral("Return"), QStringLiteral("Enter")}};
    static const QRegularExpression letter(QStringLiteral("^[A-Za-z]$"));
    static const QRegularExpression digit(QStringLiteral("^[0-9]$"));
    static const QRegularExpression wheel(QStringLiteral("^Wheel(Up|Down|Left|Right)$"));
    static const QRegularExpression mouse(QStringLiteral("^Mouse[A-Z0-9]"));
    static const QRegularExpression pad(QStringLiteral("^Gamepad[A-Z]"));
    if (letter.match(name).hasMatch()) {
        return QStringLiteral("Key") + name.toUpper();
    }
    if (digit.match(name).hasMatch()) {
        return QStringLiteral("Digit") + name;
    }
    if (const auto alias = aliases.constFind(name); alias != aliases.constEnd()) {
        return *alias;
    }
    if (wheel.match(name).hasMatch()) {
        return QStringLiteral("Mouse.") + name;
    }
    if (mouse.match(name).hasMatch()) {
        return QStringLiteral("Mouse.") + name.mid(5);
    }
    if (pad.match(name).hasMatch()) {
        return QStringLiteral("Gamepad.") + name.mid(7);
    }
    return name;
}

DefaultClass defaultClassIn(const QString& code)
{
    constexpr auto kUnicode = QRegularExpression::UseUnicodePropertiesOption;
    static const QRegularExpression direct(
        QStringLiteral(R"(export\s+default\s+class\b\s*([\p{L}_$][\p{L}\p{N}_$]*)?\s*)"
                       R"((?:extends\s+([\p{L}_$][\p{L}\p{N}_$]*)\s*)?\{)"),
        kUnicode);
    static const QRegularExpression exported(QStringLiteral(R"(export\s+default\s+([\p{L}_$][\p{L}\p{N}_$]*)\s*;?)"),
                                             kUnicode);
    DefaultClass found;
    QRegularExpressionMatch match = direct.match(code);
    if (match.hasMatch()) {
        found.name = match.captured(1);
        found.base = match.captured(2);
    } else {
        const QRegularExpressionMatch named = exported.match(code);
        if (!named.hasMatch() || named.captured(1) == u"class") {
            return found;
        }
        const QRegularExpression declared(
            QStringLiteral(R"((?<![\p{L}\p{N}_$.])class\s+%1\s*(?:extends\s+([\p{L}_$][\p{L}\p{N}_$]*)\s*)?\{)")
                .arg(QRegularExpression::escape(named.captured(1))),
            kUnicode);
        match = declared.match(code);
        if (!match.hasMatch()) {
            return found;
        }
        found.name = named.captured(1);
        found.base = match.captured(1);
    }
    const int open = static_cast<int>(match.capturedEnd()) - 1;
    const int close = closingOf(code, open);
    if (close > open) {
        found.body = code.mid(open + 1, close - open - 1);
    }
    if (!found.base.isEmpty()) {
        const QRegularExpression imported(
            QStringLiteral(R"(import\s+%1\s+from\s+("[^"]*"|'[^']*'))").arg(QRegularExpression::escape(found.base)));
        if (const QRegularExpressionMatch hit = imported.match(code); hit.hasMatch()) {
            found.from = unquoted(hit.captured(1));
        }
    }
    return found;
}

std::vector<std::pair<QString, QString>> childCallsIn(const QString& body)
{
    constexpr auto kUnicode = QRegularExpression::UseUnicodePropertiesOption;
    static const QRegularExpression field(
        QStringLiteral(R"((?<![\p{L}\p{N}_$.#])(#?[\p{L}_$][\p{L}\p{N}_$]*)\s*[?!]?\s*(?::[^=;{}()]*)?=(?![=>])\s*)"),
        kUnicode);
    static const QRegularExpression call(
        QStringLiteral(R"(^this\s*\.\s*child\s*(?:<[^>]*>)?\s*\(\s*([\p{L}_$][\p{L}\p{N}_$]*)\s*\))"), kUnicode);
    static const QRegularExpression staticBefore(QStringLiteral(R"((?:^|[^\p{L}\p{N}_$])static\s*$)"), kUnicode);
    std::vector<std::pair<QString, QString>> found;
    const std::vector<int> depths = depthsOf(body);
    QRegularExpressionMatchIterator matches = field.globalMatch(body);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const int start = static_cast<int>(match.capturedStart(1));
        if (depths[static_cast<std::size_t>(start)] != 0 || staticBefore.match(body.left(start)).hasMatch()) {
            continue;
        }
        const QString name = match.captured(1);
        const int value = static_cast<int>(match.capturedEnd());
        const QString rest = body.mid(value);
        if (const QRegularExpressionMatch single = call.match(rest); single.hasMatch()) {
            found.emplace_back(name, single.captured(1));
            continue;
        }
        if (!rest.startsWith(u'[')) {
            continue;
        }
        const int close = closingOf(body, value);
        if (close < 0) {
            continue;
        }
        const QStringList items = partsOf(body.mid(value + 1, close - value - 1));
        for (qsizetype index = 0; index < items.size(); ++index) {
            if (const QRegularExpressionMatch item = call.match(items[index].trimmed()); item.hasMatch()) {
                found.emplace_back(name + u'.' + QString::number(index), item.captured(1));
            }
        }
    }
    return found;
}

QString builtInType(const QString& className)
{
    const auto found = std::ranges::find_if(kBuiltIns, [&](const BuiltIn& each) { return className == QLatin1StringView(each.name); });
    return found != kBuiltIns.end() ? QString::fromLatin1(found->type) : QString();
}

QString builtInClass(const QString& type)
{
    const auto found = std::ranges::find_if(kBuiltIns, [&](const BuiltIn& each) { return type == QLatin1StringView(each.type); });
    return found != kBuiltIns.end() ? QString::fromLatin1(found->name) : QString();
}

}
