#include "HelpMarkdown.h"

#include "ScriptHighlighter.h"

#include <QList>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace gs::app {

using namespace Qt::StringLiterals;

namespace {

struct ListEntry {
    QStringList lines;
    std::vector<QStringList> subs;
};

struct Built {
    QVariantMap block;
    QString plain;
};

bool wide(QChar c)
{
    const char16_t u = c.unicode();
    return (u >= 0x2E80 && u <= 0x9FFF) || (u >= 0xF900 && u <= 0xFAFF) || (u >= 0xFF00 && u <= 0xFFEF) ||
           (u >= 0x3000 && u <= 0x303F) || u == 0x201C || u == 0x201D || u == 0x2018 || u == 0x2019;
}

QString joined(const QStringList& lines)
{
    QString text;
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (!text.isEmpty() && !(wide(text.back()) && wide(line.front()))) {
            text += u' ';
        }
        text += line;
    }
    return text;
}

int indentOf(QStringView line)
{
    int indent = 0;
    for (const QChar c : line) {
        if (c == u' ') {
            ++indent;
        } else if (c == u'\t') {
            indent += 4;
        } else {
            break;
        }
    }
    return indent;
}

bool blank(QStringView line)
{
    return line.trimmed().isEmpty();
}

const QRegularExpression& orderedItem()
{
    static const QRegularExpression pattern(uR"(^(\s*)\d+[.)]\s+(.*)$)"_s);
    return pattern;
}

const QRegularExpression& bulletItem()
{
    static const QRegularExpression pattern(uR"(^(\s*)[-*+]\s+(.*)$)"_s);
    return pattern;
}

const QRegularExpression& headingLine()
{
    static const QRegularExpression pattern(uR"(^(#{1,6})\s+(.*?)\s*$)"_s);
    return pattern;
}

const QRegularExpression& fenceLine()
{
    static const QRegularExpression pattern(uR"re(^\s*```\s*([A-Za-z0-9_+-]*)\s*(?:title="([^"]*)")?\s*$)re"_s);
    return pattern;
}

const QRegularExpression& figureLine()
{
    static const QRegularExpression pattern(uR"(^\s*!\[([^\]]*)\]\(\s*([^)\s]+)\s*\)\s*$)"_s);
    return pattern;
}

const QRegularExpression& boldLead()
{
    static const QRegularExpression pattern(uR"(^\*\*(.+?)\*\*\s*[：:。.]?\s*(.*)$)"_s);
    return pattern;
}

bool startsBlock(const QString& line)
{
    const QString trimmed = line.trimmed();
    return trimmed.startsWith(u"```"_s) || trimmed.startsWith(u'>') || trimmed.startsWith(u'|') ||
           headingLine().match(line).hasMatch() || orderedItem().match(line).hasMatch() ||
           bulletItem().match(line).hasMatch() || figureLine().match(line).hasMatch();
}

QString escaped(QStringView text)
{
    return text.toString().toHtmlEscaped();
}

QString unescaped(QString text)
{
    text.replace(u"&lt;"_s, u"<"_s);
    text.replace(u"&gt;"_s, u">"_s);
    text.replace(u"&quot;"_s, u"\""_s);
    text.replace(u"&#39;"_s, u"'"_s);
    text.replace(u"&nbsp;"_s, u" "_s);
    text.replace(u"&amp;"_s, u"&"_s);
    return text;
}

QString tagless(const QString& html)
{
    static const QRegularExpression tags(uR"(<[^>]*>)"_s);
    return unescaped(QString(html).remove(tags));
}

QString strongHtml(const QString& inner)
{
    return u"<span style=\"color:%T1%;font-weight:600\">"_s + inner + u"</span>"_s;
}

QString nameHtml(const QString& inner)
{
    return u"<span style=\"color:%T1%;font-weight:500\">"_s + inner + u"</span>"_s;
}

QString codeHtml(QStringView text)
{
    return u"<span style=\"font-family:'%MONO%';background-color:%CODEBG%;color:%CODE%\">&nbsp;"_s + escaped(text) +
           u"&nbsp;</span>"_s;
}

QString keyHtml(QStringView text)
{
    return u"<span style=\"background-color:%KEYBG%;color:%T1%;font-weight:600\">&nbsp;"_s + escaped(text) +
           u"&nbsp;</span>"_s;
}

QString pathHtml(QStringView text)
{
    QString html;
    const QList<QStringView> parts = text.split(u'>');
    for (qsizetype i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            html += u"<span style=\"color:%T3%\">&nbsp;›&nbsp;</span>"_s;
        }
        html += u"<span style=\"color:%T1%;font-weight:500\">"_s + escaped(parts[i].trimmed()) + u"</span>"_s;
    }
    return html;
}

QString preserved(QStringView text)
{
    QString html = text.toString().toHtmlEscaped();
    html.replace(u' ', u"&nbsp;"_s);
    html.replace(u'\t', u"&nbsp;&nbsp;&nbsp;&nbsp;"_s);
    return html;
}

bool scriptLanguage(const QString& language)
{
    static const QStringList script{u"ts"_s, u"typescript"_s, u"js"_s, u"javascript"_s};
    return script.contains(language.toLower());
}

QVariantMap attributesOf(QString& raw)
{
    QVariantMap attributes;
    raw = raw.trimmed();
    while (raw.endsWith(u'}')) {
        const qsizetype open = raw.lastIndexOf(u'{');
        if (open < 0) {
            break;
        }
        const QString inside = raw.sliced(open + 1, raw.size() - open - 2).trimmed();
        const qsizetype equals = inside.indexOf(u'=');
        if (inside.startsWith(u'#')) {
            attributes.insert(u"anchor"_s, inside.mid(1).trimmed());
        } else if (equals > 0 && !inside.left(equals).contains(u' ')) {
            attributes.insert(inside.left(equals).trimmed(), inside.mid(equals + 1).trimmed());
        } else {
            attributes.insert(u"note"_s, inside);
        }
        raw = raw.left(open).trimmed();
    }
    return attributes;
}

QStringList cellsOf(const QString& line)
{
    QString text = line.trimmed();
    if (text.startsWith(u'|')) {
        text.remove(0, 1);
    }
    if (text.endsWith(u'|') && !text.endsWith(u"\\|"_s)) {
        text.chop(1);
    }
    QStringList cells;
    QString cell;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i] == u'\\' && i + 1 < text.size() && text[i + 1] == u'|') {
            cell += u"\\|"_s;
            ++i;
            continue;
        }
        if (text[i] == u'|') {
            cells.append(cell.trimmed());
            cell.clear();
            continue;
        }
        cell += text[i];
    }
    cells.append(cell.trimmed());
    return cells;
}

QStringList keysOf(const QString& cell)
{
    static const QRegularExpression key(uR"(<kbd>(.*?)</kbd>)"_s);
    QStringList keys;
    for (QRegularExpressionMatchIterator it = key.globalMatch(cell); it.hasNext();) {
        keys.append(it.next().captured(1).trimmed());
    }
    return keys;
}

std::vector<ListEntry> parseList(const QStringList& lines, qsizetype& at, bool ordered)
{
    const QRegularExpression& marker = ordered ? orderedItem() : bulletItem();
    std::vector<ListEntry> entries;
    while (at < lines.size()) {
        const QString& line = lines[at];
        if (blank(line)) {
            qsizetype next = at + 1;
            while (next < lines.size() && blank(lines[next])) {
                ++next;
            }
            const bool sibling = next < lines.size() && indentOf(lines[next]) < 2 && marker.match(lines[next]).hasMatch();
            const bool nested = next < lines.size() && indentOf(lines[next]) >= 2 && !entries.empty();
            if (!sibling && !nested) {
                break;
            }
            at = next;
            continue;
        }
        if (indentOf(line) < 2) {
            const QRegularExpressionMatch item = marker.match(line);
            if (item.hasMatch()) {
                entries.push_back({QStringList{item.captured(2)}, {}});
                ++at;
                continue;
            }
            if (entries.empty() || startsBlock(line)) {
                break;
            }
            entries.back().lines.append(line);
            ++at;
            continue;
        }
        if (entries.empty()) {
            break;
        }
        const QRegularExpressionMatch sub = bulletItem().match(line);
        const QRegularExpressionMatch subOrdered = orderedItem().match(line);
        if (sub.hasMatch()) {
            entries.back().subs.push_back({sub.captured(2)});
        } else if (subOrdered.hasMatch()) {
            entries.back().subs.push_back({subOrdered.captured(2)});
        } else if (!entries.back().subs.empty()) {
            entries.back().subs.back().append(line);
        } else {
            entries.back().lines.append(line);
        }
        ++at;
    }
    return entries;
}

QString withSubs(QString html, const ListEntry& entry, QString& plain)
{
    for (const QStringList& sub : entry.subs) {
        const QString text = joined(sub);
        if (!html.isEmpty()) {
            html += u"<br>"_s;
        }
        html += u"•&nbsp;&nbsp;"_s + helpInline(text);
        plain += u' ' + helpPlain(text);
    }
    return html;
}

Built stepsBlock(const std::vector<ListEntry>& entries)
{
    QVariantList items;
    QString plain;
    for (const ListEntry& entry : entries) {
        const QString text = joined(entry.lines);
        plain += u' ' + helpPlain(text);
        QString title;
        QString detail;
        if (const QRegularExpressionMatch lead = boldLead().match(text); lead.hasMatch()) {
            title = helpInline(lead.captured(1));
            detail = helpInline(lead.captured(2));
        } else {
            detail = helpInline(text);
        }
        detail = withSubs(detail, entry, plain);
        items.append(QVariantMap{{u"title"_s, title}, {u"html"_s, detail}});
    }
    return {{{u"kind"_s, u"steps"_s}, {u"items"_s, items}}, plain};
}

Built bulletsBlock(const std::vector<ListEntry>& entries)
{
    static const QRegularExpression choice(uR"(^:([a-z0-9-]+):\s*\*\*(.+?)\*\*\s*[：:]?\s*(.*)$)"_s);
    QString plain;
    const bool choices = !entries.empty() && std::ranges::all_of(entries, [](const ListEntry& entry) {
        return entry.subs.empty() && choice.match(joined(entry.lines)).hasMatch();
    });
    QVariantList items;
    QStringList raw;
    for (const ListEntry& entry : entries) {
        const QString text = joined(entry.lines);
        raw.append(text);
        if (choices) {
            const QRegularExpressionMatch match = choice.match(text);
            plain += u' ' + helpPlain(match.captured(2)) + u' ' + helpPlain(match.captured(3));
            items.append(QVariantMap{{u"icon"_s, match.captured(1)},
                                     {u"title"_s, helpInline(match.captured(2))},
                                     {u"html"_s, helpInline(match.captured(3))}});
            continue;
        }
        plain += u' ' + helpPlain(text);
        items.append(withSubs(helpInline(text), entry, plain));
    }
    return {{{u"kind"_s, choices ? u"choices"_s : u"bullets"_s}, {u"items"_s, items}, {u"raw"_s, raw}}, plain};
}

Built calloutBlock(const QStringList& quoted)
{
    static const QRegularExpression kind(uR"(^\[!(TIP|NOTE|WARNING|IMPORTANT|CAUTION)\]\s*(.*)$)"_s,
                                         QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression onlyLink(uR"(^\s*\[([^\]]+)\]\(\s*([^)\s]+)\s*\)\s*$)"_s);
    QStringList lines;
    for (const QString& line : quoted) {
        QString text = line.trimmed().mid(1);
        if (text.startsWith(u' ')) {
            text.remove(0, 1);
        }
        lines.append(text);
    }
    QString tone = u"note"_s;
    if (!lines.isEmpty()) {
        if (const QRegularExpressionMatch match = kind.match(lines.front()); match.hasMatch()) {
            const QString name = match.captured(1).toUpper();
            tone = name == u"TIP"_s ? u"tip"_s : name == u"NOTE"_s ? u"note"_s : u"warning"_s;
            lines.front() = match.captured(2);
        }
    }
    std::vector<QStringList> paragraphs(1);
    for (const QString& line : lines) {
        if (blank(line)) {
            if (!paragraphs.back().isEmpty()) {
                paragraphs.emplace_back();
            }
            continue;
        }
        paragraphs.back().append(line);
    }
    if (paragraphs.back().isEmpty()) {
        paragraphs.pop_back();
    }
    QVariantMap action;
    if (!paragraphs.empty()) {
        if (const QRegularExpressionMatch match = onlyLink.match(paragraphs.back().back()); match.hasMatch()) {
            action = {{u"label"_s, helpPlain(match.captured(1))}, {u"target"_s, match.captured(2)}};
            paragraphs.back().removeLast();
            if (paragraphs.back().isEmpty()) {
                paragraphs.pop_back();
            }
        }
    }
    QString title;
    QStringList bodies;
    QString plain;
    for (std::size_t i = 0; i < paragraphs.size(); ++i) {
        QString text = joined(paragraphs[i]);
        if (i == 0) {
            if (const QRegularExpressionMatch lead = boldLead().match(text); lead.hasMatch()) {
                title = helpInline(lead.captured(1));
                plain += helpPlain(lead.captured(1));
                text = lead.captured(2);
            }
        }
        if (!text.isEmpty()) {
            bodies.append(helpInline(text));
            plain += u' ' + helpPlain(text);
        }
    }
    return {{{u"kind"_s, u"callout"_s},
             {u"tone"_s, tone},
             {u"title"_s, title},
             {u"html"_s, bodies.join(u"<br>"_s)},
             {u"action"_s, action}},
            plain};
}

Built tableBlock(const QStringList& rows)
{
    static const QRegularExpression separator(uR"(^\s*\|?\s*:?-{2,}:?\s*(\|\s*:?-{2,}:?\s*)*\|?\s*$)"_s);
    QStringList header;
    std::vector<QStringList> body;
    for (qsizetype i = 0; i < rows.size(); ++i) {
        if (i == 1 && separator.match(rows[i]).hasMatch()) {
            continue;
        }
        if (i == 0) {
            header = cellsOf(rows[i]);
        } else {
            body.push_back(cellsOf(rows[i]));
        }
    }
    QString plain;
    for (const QString& cell : header) {
        plain += u' ' + helpPlain(cell);
    }
    for (const QStringList& row : body) {
        for (const QString& cell : row) {
            plain += u' ' + helpPlain(cell);
        }
    }
    const bool keys = !body.empty() && std::ranges::all_of(body, [](const QStringList& row) {
        return !row.isEmpty() && row.front().contains(u"<kbd>"_s);
    });
    if (keys) {
        QVariantList items;
        for (const QStringList& row : body) {
            items.append(QVariantMap{{u"keys"_s, keysOf(row.front())},
                                     {u"html"_s, helpInline(row.mid(1).join(u' '))}});
        }
        return {{{u"kind"_s, u"keys"_s}, {u"items"_s, items}}, plain};
    }
    QVariantList headerHtml;
    for (const QString& cell : header) {
        headerHtml.append(helpInline(cell));
    }
    QVariantList rowsHtml;
    QVariantList rowsRaw;
    for (const QStringList& row : body) {
        QVariantList cells;
        for (const QString& cell : row) {
            cells.append(helpInline(cell));
        }
        rowsHtml.append(QVariant(cells));
        rowsRaw.append(QVariant(row));
    }
    return {{{u"kind"_s, u"table"_s}, {u"header"_s, headerHtml}, {u"rows"_s, rowsHtml}, {u"raw"_s, rowsRaw}}, plain};
}

Built figureBlock(const QString& caption, const QString& target)
{
    if (!target.startsWith(u"figure:"_s)) {
        return {{{u"kind"_s, u"image"_s}, {u"source"_s, target}, {u"caption"_s, helpPlain(caption)}}, helpPlain(caption)};
    }
    QStringList names;
    int focus = -1;
    for (QString name : target.mid(7).split(u',')) {
        name = name.trimmed();
        if (name.endsWith(u'!')) {
            name.chop(1);
            focus = static_cast<int>(names.size());
        }
        if (name.isEmpty() && !names.isEmpty()) {
            name = names.back();
        }
        names.append(name);
    }
    if (focus < 0) {
        focus = static_cast<int>(names.size()) - 1;
    }
    const QString kind = names == QStringList{u"setup-steps"_s} ? u"setup"_s : u"figure"_s;
    return {{{u"kind"_s, kind},
             {u"names"_s, names},
             {u"focus"_s, focus},
             {u"caption"_s, helpPlain(caption)}},
            helpPlain(caption)};
}

Built paragraphBlock(const QString& text)
{
    static const QRegularExpression endLink(uR"(^(.*?)\[([^\]]+)\]\(\s*(help:[^)\s]+)\s*\)\s*[。.]?\s*$)"_s);
    if (const QRegularExpressionMatch match = endLink.match(text);
        match.hasMatch() && !match.captured(1).contains(u"]("_s)) {
        QString lead = helpPlain(match.captured(1)).trimmed();
        while (!lead.isEmpty() && (lead.endsWith(u'：') || lead.endsWith(u':'))) {
            lead.chop(1);
        }
        return {{{u"kind"_s, u"link"_s},
                 {u"lead"_s, lead},
                 {u"label"_s, helpPlain(match.captured(2))},
                 {u"target"_s, match.captured(3)}},
                helpPlain(text)};
    }
    return {{{u"kind"_s, u"paragraph"_s}, {u"html"_s, helpInline(text)}}, helpPlain(text)};
}

int readingMinutes(const QString& plain)
{
    qsizetype characters = 0;
    qsizetype words = 0;
    bool inWord = false;
    for (const QChar c : plain) {
        if (wide(c)) {
            ++characters;
            inWord = false;
        } else if (c.isLetterOrNumber()) {
            if (!inWord) {
                ++words;
            }
            inWord = true;
        } else {
            inWord = false;
        }
    }
    return std::max(1, static_cast<int>(std::lround(static_cast<double>(characters) / 400.0 +
                                                    static_cast<double>(words) / 200.0)));
}

void pairBlocks(QVariantList& blocks)
{
    static const QRegularExpression iconLead(uR"(^:([a-z0-9-]+):\s*(.*)$)"_s);
    for (qsizetype i = 0; i + 1 < blocks.size(); ++i) {
        QVariantMap first = blocks[i].toMap();
        const QVariantMap second = blocks[i + 1].toMap();
        const QString kind = first.value(u"kind"_s).toString();
        const QString next = second.value(u"kind"_s).toString();
        if (kind == u"steps"_s && next == u"figure"_s) {
            first.insert(u"figure"_s, second);
            blocks[i] = first;
            blocks.removeAt(i + 1);
        } else if (kind == u"figure"_s && next == u"table"_s) {
            QVariantList legend;
            for (const QVariant& row : second.value(u"raw"_s).toList()) {
                const QStringList cells = row.toStringList();
                QString title = cells.value(0);
                QString icon;
                if (const QRegularExpressionMatch match = iconLead.match(title); match.hasMatch()) {
                    icon = match.captured(1);
                    title = match.captured(2);
                }
                QStringList rest;
                for (qsizetype c = 1; c < cells.size(); ++c) {
                    rest.append(helpInline(cells[c]));
                }
                legend.append(QVariantMap{{u"title"_s, helpInline(title)},
                                          {u"icon"_s, icon},
                                          {u"html"_s, rest.join(u' ')}});
            }
            first.insert(u"legend"_s, legend);
            blocks[i] = first;
            blocks.removeAt(i + 1);
        }
    }
}

bool isHeading(const QVariantMap& block, int level)
{
    return block.value(u"kind"_s).toString() == u"heading"_s && block.value(u"level"_s).toInt() == level;
}

}

QString helpInline(QStringView text)
{
    static const QString escapable = u"\\`*_[]()#+-.!|<>"_s;
    QString out;
    QString plain;
    const auto flush = [&out, &plain] {
        out += plain.toHtmlEscaped();
        plain.clear();
    };
    const qsizetype n = text.size();
    qsizetype i = 0;
    while (i < n) {
        const QChar c = text[i];
        const QStringView rest = text.sliced(i);
        if (c == u'\\' && i + 1 < n && escapable.contains(text[i + 1])) {
            plain += text[i + 1];
            i += 2;
            continue;
        }
        if (c == u'`') {
            if (const qsizetype end = text.indexOf(u'`', i + 1); end > i + 1) {
                flush();
                QString inner = text.sliced(i + 1, end - i - 1).toString();
                inner.replace(u"\\|"_s, u"|"_s);
                out += codeHtml(inner);
                i = end + 1;
                continue;
            }
        }
        if (rest.startsWith(u"**")) {
            if (const qsizetype end = text.indexOf(u"**", i + 2); end > i + 2) {
                flush();
                const QStringView inner = text.sliced(i + 2, end - i - 2);
                out += inner.contains(u" > ") ? pathHtml(inner) : strongHtml(helpInline(inner));
                i = end + 2;
                continue;
            }
        }
        if (rest.startsWith(u"<kbd>")) {
            if (const qsizetype end = text.indexOf(u"</kbd>", i + 5); end >= i + 5) {
                flush();
                out += keyHtml(text.sliced(i + 5, end - i - 5));
                i = end + 6;
                continue;
            }
        }
        if (c == u'[') {
            const qsizetype close = text.indexOf(u']', i + 1);
            if (close > i && close + 1 < n && text[close + 1] == u'(') {
                if (const qsizetype end = text.indexOf(u')', close + 2); end > close + 1) {
                    flush();
                    const QString target = text.sliced(close + 2, end - close - 2).trimmed().toString();
                    out += u"<a href=\""_s + target.toHtmlEscaped() + u"\" style=\"color:%LINK%;text-decoration:none\">"_s +
                           helpInline(text.sliced(i + 1, close - i - 1)) + u"</a>"_s;
                    i = end + 1;
                    continue;
                }
            }
        }
        if (c == u'“') {
            if (const qsizetype end = text.indexOf(u'”', i + 1); end > i + 1) {
                flush();
                const QStringView inner = text.sliced(i + 1, end - i - 1);
                out += inner.contains(u" > ") ? pathHtml(inner) : nameHtml(u"“"_s + helpInline(inner) + u"”"_s);
                i = end + 1;
                continue;
            }
        }
        plain += c;
        ++i;
    }
    flush();
    return out;
}

QString helpPlain(QStringView text)
{
    static const QRegularExpression link(uR"(\[([^\]]*)\]\([^)]*\))"_s);
    static const QRegularExpression marks(uR"(\*\*|`|</?kbd>|\\(?=[\\`*_\[\]()#+\-.!|<>]))"_s);
    static const QRegularExpression icons(uR"((?<!\S):[a-z0-9-]+:(?=\s))"_s);
    QString plain = text.toString();
    plain.replace(link, u"\\1"_s);
    plain.replace(marks, QString());
    plain.replace(icons, QString());
    return plain.simplified();
}

QString helpCodeHtml(const QString& code, const QString& language)
{
    static constexpr std::array<QStringView, kScriptTokenKinds> inks{u"%KW%", u"%STR%", u"%NUM%", u"%COM%", u"%TY%"};
    const bool script = scriptLanguage(language);
    QStringList lines;
    int state = kScriptPlain;
    for (const QString& line : code.split(u'\n')) {
        if (!script) {
            lines.append(preserved(line));
            continue;
        }
        const ScriptLine parsed = scriptLine(line, state);
        state = parsed.state;
        const QStringView view(line);
        const auto size = static_cast<int>(view.size());
        QString html;
        int at = 0;
        for (const ScriptSpan& span : parsed.spans) {
            const int start = std::clamp(span.start, at, size);
            const int end = std::clamp(span.start + span.length, start, size);
            html += preserved(view.sliced(at, start - at));
            html += u"<span style=\"color:"_s + inks[static_cast<std::size_t>(span.token)] + u"\">"_s +
                    preserved(view.sliced(start, end - start)) + u"</span>"_s;
            at = end;
        }
        html += preserved(view.sliced(at));
        lines.append(html);
    }
    return lines.join(u"<br>"_s);
}

HelpDocument parseHelpMarkdown(const QString& markdown)
{
    QString text = markdown;
    text.replace(u"\r\n"_s, u"\n"_s);
    const QStringList lines = text.split(u'\n');
    std::vector<Built> built;
    int h2 = 0;
    int h3 = 0;
    qsizetype at = 0;
    while (at < lines.size()) {
        const QString& line = lines[at];
        if (blank(line)) {
            ++at;
            continue;
        }
        if (const QRegularExpressionMatch fence = fenceLine().match(line); fence.hasMatch()) {
            QStringList code;
            ++at;
            while (at < lines.size() && !lines[at].trimmed().startsWith(u"```"_s)) {
                code.append(lines[at]);
                ++at;
            }
            ++at;
            const QString body = code.join(u'\n');
            built.push_back({{{u"kind"_s, u"code"_s},
                              {u"language"_s, fence.captured(1)},
                              {u"title"_s, fence.captured(2)},
                              {u"text"_s, body},
                              {u"html"_s, helpCodeHtml(body, fence.captured(1))}},
                             body});
            continue;
        }
        if (const QRegularExpressionMatch heading = headingLine().match(line); heading.hasMatch()) {
            const int level = std::clamp(static_cast<int>(heading.captured(1).size()), 2, 3);
            QString raw = heading.captured(2);
            const QVariantMap attributes = attributesOf(raw);
            if (level == 2) {
                ++h2;
                h3 = 0;
            } else {
                ++h3;
            }
            QString anchor = attributes.value(u"anchor"_s).toString();
            if (anchor.isEmpty()) {
                anchor = level == 2 ? u"s%1"_s.arg(h2) : u"s%1-%2"_s.arg(h2).arg(h3);
            }
            QVariantMap block{{u"kind"_s, u"heading"_s},
                              {u"level"_s, level},
                              {u"text"_s, helpPlain(raw)},
                              {u"html"_s, helpInline(raw)},
                              {u"anchor"_s, anchor},
                              {u"note"_s, attributes.value(u"note"_s)},
                              {u"icon"_s, attributes.value(u"icon"_s)},
                              {u"tone"_s, attributes.value(u"tone"_s)}};
            built.push_back({block, helpPlain(raw)});
            ++at;
            continue;
        }
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(u'>')) {
            QStringList quoted;
            while (at < lines.size() && lines[at].trimmed().startsWith(u'>')) {
                quoted.append(lines[at]);
                ++at;
            }
            built.push_back(calloutBlock(quoted));
            continue;
        }
        if (trimmed.startsWith(u'|')) {
            QStringList rows;
            while (at < lines.size() && lines[at].trimmed().startsWith(u'|')) {
                rows.append(lines[at]);
                ++at;
            }
            built.push_back(tableBlock(rows));
            continue;
        }
        if (orderedItem().match(line).hasMatch()) {
            built.push_back(stepsBlock(parseList(lines, at, true)));
            continue;
        }
        if (bulletItem().match(line).hasMatch()) {
            built.push_back(bulletsBlock(parseList(lines, at, false)));
            continue;
        }
        if (const QRegularExpressionMatch figure = figureLine().match(line); figure.hasMatch()) {
            built.push_back(figureBlock(figure.captured(1), figure.captured(2)));
            ++at;
            continue;
        }
        QStringList paragraph{line};
        ++at;
        while (at < lines.size() && !blank(lines[at]) && !startsBlock(lines[at])) {
            paragraph.append(lines[at]);
            ++at;
        }
        built.push_back(paragraphBlock(joined(paragraph)));
    }

    HelpDocument document;
    QString allPlain;
    for (const Built& entry : built) {
        allPlain += u' ' + entry.plain;
        if (isHeading(entry.block, 2) || isHeading(entry.block, 3)) {
            document.searchable.push_back({entry.block.value(u"anchor"_s).toString(),
                                           entry.block.value(u"text"_s).toString(),
                                           entry.block.value(u"level"_s).toInt(),
                                           {}});
            if (isHeading(entry.block, 2)) {
                document.sections.append(QVariantMap{{u"title"_s, entry.block.value(u"text"_s)},
                                                     {u"anchor"_s, entry.block.value(u"anchor"_s)}});
            }
            continue;
        }
        if (!document.searchable.empty()) {
            document.searchable.back().text += u' ' + entry.plain;
        }
    }
    document.minutes = readingMinutes(allPlain);

    qsizetype first = 0;
    if (!built.empty() && built.front().block.value(u"kind"_s).toString() == u"paragraph"_s) {
        document.lead = built.front().block.value(u"html"_s).toString();
        first = 1;
    }
    for (qsizetype i = first; i < static_cast<qsizetype>(built.size()); ++i) {
        document.blocks.append(built[static_cast<std::size_t>(i)].block);
    }
    pairBlocks(document.blocks);
    return document;
}

QVariantList helpTabs(const QVariantList& blocks)
{
    QVariantList out;
    qsizetype i = 0;
    while (i < blocks.size()) {
        const QVariantMap block = blocks[i].toMap();
        if (!isHeading(block, 3)) {
            out.append(block);
            ++i;
            continue;
        }
        QVariantList tabs;
        qsizetype j = i;
        while (j < blocks.size() && isHeading(blocks[j].toMap(), 3)) {
            const QVariantMap heading = blocks[j].toMap();
            QVariantList content;
            qsizetype k = j + 1;
            while (k < blocks.size() && blocks[k].toMap().value(u"kind"_s).toString() != u"heading"_s) {
                content.append(blocks[k]);
                ++k;
            }
            const bool steps = std::ranges::any_of(content, [](const QVariant& entry) {
                return entry.toMap().value(u"kind"_s).toString() == u"steps"_s;
            });
            if (!steps) {
                break;
            }
            tabs.append(QVariantMap{{u"title"_s, heading.value(u"text"_s)},
                                    {u"anchor"_s, heading.value(u"anchor"_s)},
                                    {u"icon"_s, heading.value(u"icon"_s)},
                                    {u"blocks"_s, content}});
            j = k;
        }
        if (tabs.size() >= 2) {
            out.append(QVariantMap{{u"kind"_s, u"tabs"_s}, {u"tabs"_s, tabs}});
            i = j;
        } else {
            out.append(block);
            ++i;
        }
    }
    return out;
}

QVariantList helpFaq(const QVariantList& blocks)
{
    QVariantList categories;
    QVariantMap category;
    QVariantList questions;
    QVariantMap question;
    QVariantList answer;
    const auto closeQuestion = [&question, &answer, &questions] {
        if (!question.isEmpty()) {
            question.insert(u"blocks"_s, answer);
            questions.append(question);
        }
        question.clear();
        answer.clear();
    };
    const auto closeCategory = [&] {
        closeQuestion();
        if (!category.isEmpty() || !questions.isEmpty()) {
            category.insert(u"questions"_s, questions);
            categories.append(category);
        }
        category.clear();
        questions.clear();
    };
    for (const QVariant& value : blocks) {
        const QVariantMap block = value.toMap();
        if (isHeading(block, 2)) {
            closeCategory();
            category = {{u"title"_s, block.value(u"text"_s)},
                        {u"anchor"_s, block.value(u"anchor"_s)},
                        {u"icon"_s, block.value(u"icon"_s)},
                        {u"tone"_s, block.value(u"tone"_s)}};
            continue;
        }
        if (isHeading(block, 3)) {
            closeQuestion();
            question = {{u"title"_s, block.value(u"text"_s)}, {u"anchor"_s, block.value(u"anchor"_s)}};
            continue;
        }
        if (!question.isEmpty()) {
            answer.append(block);
        }
    }
    closeCategory();
    return categories;
}

QVariantList helpReference(const QVariantList& blocks)
{
    static const QRegularExpression returns(uR"(^Returns\s+(.+?)\.\s+([a-z]+)\s+since API\s+([^\s.]+)\.?\s*(.*)$)"_s);
    static const QRegularExpression failure(uR"(^`([^`]+)`\s+when\s+(.*?)\.\s+(.*)$)"_s);
    QVariantList out;
    qsizetype i = 0;
    while (i < blocks.size()) {
        const QVariantMap block = blocks[i].toMap();
        const bool function = isHeading(block, 2) && i + 1 < blocks.size() && [&blocks, i] {
            for (qsizetype k = i + 1; k < blocks.size(); ++k) {
                const QString kind = blocks[k].toMap().value(u"kind"_s).toString();
                if (kind == u"code"_s) {
                    return true;
                }
                if (kind == u"heading"_s) {
                    return false;
                }
            }
            return false;
        }();
        if (!function) {
            out.append(block);
            ++i;
            continue;
        }
        QVariantMap card{{u"kind"_s, u"function"_s},
                         {u"title"_s, block.value(u"text"_s)},
                         {u"anchor"_s, block.value(u"anchor"_s)}};
        QStringList docs;
        QString signature;
        QString signatureHtml;
        QString example;
        QString exampleHtml;
        QVariantList params;
        QVariantList failures;
        bool errors = false;
        qsizetype k = i + 1;
        for (; k < blocks.size(); ++k) {
            const QVariantMap part = blocks[k].toMap();
            const QString kind = part.value(u"kind"_s).toString();
            if (kind == u"heading"_s) {
                break;
            }
            if (kind == u"code"_s) {
                if (signature.isEmpty()) {
                    signature = part.value(u"text"_s).toString();
                    signatureHtml = part.value(u"html"_s).toString();
                } else {
                    example = part.value(u"text"_s).toString();
                    exampleHtml = part.value(u"html"_s).toString();
                }
            } else if (kind == u"table"_s) {
                for (const QVariant& row : part.value(u"raw"_s).toList()) {
                    const QStringList cells = row.toStringList();
                    QString name = helpPlain(cells.value(0));
                    const bool optional = name.contains(u"(optional)"_s);
                    name.remove(u"(optional)"_s);
                    params.append(QVariantMap{{u"name"_s, name.trimmed()},
                                              {u"optional"_s, optional},
                                              {u"type"_s, helpPlain(cells.value(1)).replace(u"\\|"_s, u"|"_s)},
                                              {u"html"_s, helpInline(cells.value(2))}});
                }
            } else if (kind == u"bullets"_s && errors) {
                for (const QVariant& item : part.value(u"raw"_s).toList()) {
                    const QString text = item.toString();
                    if (const QRegularExpressionMatch match = failure.match(text); match.hasMatch()) {
                        failures.append(QVariantMap{{u"code"_s, match.captured(1)},
                                                    {u"html"_s, helpInline(match.captured(2))},
                                                    {u"fix"_s, helpInline(match.captured(3))}});
                    } else {
                        failures.append(QVariantMap{{u"code"_s, QString()}, {u"html"_s, helpInline(text)}, {u"fix"_s, QString()}});
                    }
                }
            } else if (kind == u"paragraph"_s) {
                const QString html = part.value(u"html"_s).toString();
                if (tagless(html).simplified() == u"Errors:"_s) {
                    errors = true;
                    continue;
                }
                docs.append(html);
            } else if (kind == u"bullets"_s) {
                for (const QVariant& item : part.value(u"items"_s).toList()) {
                    docs.append(u"•&nbsp;&nbsp;"_s + item.toString());
                }
            }
        }
        QString returnsHtml;
        QString stability;
        QString since;
        QString extra;
        QStringList kept;
        for (const QString& html : docs) {
            const QString plain = tagless(html);
            if (const QRegularExpressionMatch match = returns.match(plain.simplified()); match.hasMatch()) {
                returnsHtml = helpInline(u"`"_s + match.captured(1).trimmed() + u"`"_s);
                stability = match.captured(2);
                since = match.captured(3);
                extra = match.captured(4).trimmed();
                continue;
            }
            kept.append(html);
        }
        card.insert(u"html"_s, kept.join(u"<br><br>"_s));
        card.insert(u"signature"_s, signature);
        card.insert(u"signatureHtml"_s, signatureHtml);
        card.insert(u"example"_s, example);
        card.insert(u"exampleHtml"_s, exampleHtml);
        card.insert(u"params"_s, params);
        card.insert(u"failures"_s, failures);
        card.insert(u"returns"_s, returnsHtml);
        card.insert(u"stability"_s, stability);
        card.insert(u"since"_s, since);
        card.insert(u"extra"_s, extra);
        out.append(card);
        i = k;
    }
    return out;
}

QVariantList helpReferenceSections(const QVariantList& blocks)
{
    QVariantList sections;
    for (const QVariant& value : blocks) {
        const QVariantMap block = value.toMap();
        if (block.value(u"kind"_s).toString() == u"function"_s || isHeading(block, 2)) {
            sections.append(QVariantMap{{u"title"_s, block.value(u"title"_s, block.value(u"text"_s))},
                                        {u"anchor"_s, block.value(u"anchor"_s)}});
        }
    }
    return sections;
}

}
