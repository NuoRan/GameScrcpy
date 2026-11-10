#include "DraftDiff.h"

#include "QtConversions.h"

#include <gs/blueprint/Document.h>
#include <gs/input/Profile.h>

#include <QVariantMap>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace gs::app {

namespace {

constexpr qsizetype kMostDiffCells = 2'000'000;

enum class Step : std::uint8_t { Same, Gone, Added };

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

QVariantMap lineEntry(const DraftLine& line)
{
    const QString kind = line.kind == DraftLine::Kind::Added ? QStringLiteral("added")
                         : line.kind == DraftLine::Kind::Removed ? QStringLiteral("removed")
                                                                 : QStringLiteral("context");
    return {{QStringLiteral("kind"), kind}, {QStringLiteral("text"), line.text}};
}

}

QStringList draftLinesOf(const QString& text)
{
    QString plain = text;
    plain.remove(u'\r');
    QStringList lines = plain.split(u'\n');
    if (!lines.isEmpty() && lines.back().isEmpty()) {
        lines.removeLast();
    }
    return lines;
}

DraftTextDiff draftTextDiff(const std::optional<QString>& before, const std::optional<QString>& after, qsizetype context,
                            qsizetype mostLines)
{
    DraftTextDiff diff;
    const QStringList first = before ? draftLinesOf(*before) : QStringList();
    const QStringList second = after ? draftLinesOf(*after) : QStringList();
    const std::vector<Step> steps = stepsOf(first, second);
    struct Line {
        Step step = Step::Same;
        qsizetype first = 0;
        qsizetype second = 0;
    };
    std::vector<Line> lines;
    lines.reserve(steps.size());
    qsizetype inFirst = 0;
    qsizetype inSecond = 0;
    qsizetype added = 0;
    qsizetype removed = 0;
    for (const Step step : steps) {
        lines.push_back({.step = step, .first = inFirst, .second = inSecond});
        if (step != Step::Added) {
            ++inFirst;
        } else {
            ++added;
        }
        if (step != Step::Gone) {
            ++inSecond;
        } else {
            ++removed;
        }
    }
    diff.added = static_cast<int>(added);
    diff.removed = static_cast<int>(removed);
    if (lines.empty()) {
        return diff;
    }
    const auto same = [](const Line& line) { return line.step == Step::Same; };
    if (std::ranges::all_of(lines, same) && before.has_value() == after.has_value()) {
        return diff;
    }
    const std::size_t room = context > 0 ? static_cast<std::size_t>(context) : std::size_t{0};
    qsizetype budget = mostLines > 0 ? mostLines : 0;
    std::size_t index = 0;
    const auto countChanges = [&lines](std::size_t from) {
        qsizetype count = 0;
        for (std::size_t at = from; at < lines.size(); ++at) {
            count += lines[at].step != Step::Same ? 1 : 0;
        }
        return count;
    };
    while (index < lines.size()) {
        if (lines[index].step == Step::Same) {
            ++index;
            continue;
        }
        if (budget <= 0) {
            diff.leftOut += static_cast<int>(countChanges(index));
            break;
        }
        const std::size_t from = index >= room ? index - room : 0;
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
            if (run < lines.size() && run - scan <= 2 * room) {
                scan = run;
                continue;
            }
            break;
        }
        std::size_t to = std::min(lines.size(), last + 1 + room);
        if (static_cast<qsizetype>(to - from) > budget) {
            to = from + static_cast<std::size_t>(budget);
        }
        DraftHunk hunk;
        qsizetype firstCount = 0;
        qsizetype secondCount = 0;
        for (std::size_t at = from; at < to; ++at) {
            firstCount += lines[at].step != Step::Added ? 1 : 0;
            secondCount += lines[at].step != Step::Gone ? 1 : 0;
        }
        hunk.beforeStart = static_cast<int>(lines[from].first + (firstCount == 0 ? 0 : 1));
        hunk.beforeCount = static_cast<int>(firstCount);
        hunk.afterStart = static_cast<int>(lines[from].second + (secondCount == 0 ? 0 : 1));
        hunk.afterCount = static_cast<int>(secondCount);
        hunk.lines.reserve(to - from);
        for (std::size_t at = from; at < to; ++at) {
            const Line& line = lines[at];
            if (line.step == Step::Same) {
                hunk.lines.push_back({.kind = DraftLine::Kind::Context, .text = first[line.first]});
            } else if (line.step == Step::Gone) {
                hunk.lines.push_back({.kind = DraftLine::Kind::Removed, .text = first[line.first]});
            } else {
                hunk.lines.push_back({.kind = DraftLine::Kind::Added, .text = second[line.second]});
            }
        }
        budget -= static_cast<qsizetype>(to - from);
        diff.hunks.push_back(std::move(hunk));
        index = to;
    }
    return diff;
}

QVariantList draftDiffList(const DraftTextDiff& diff)
{
    QVariantList rows;
    if (diff.binary) {
        rows.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("note")},
                                {QStringLiteral("text"), QStringLiteral("This is not a text file")}});
        return rows;
    }
    for (const DraftHunk& hunk : diff.hunks) {
        rows.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("header")},
                                {QStringLiteral("text"), QStringLiteral("@@ -%1 +%2 @@")
                                                            .arg(rangeText(hunk.beforeStart - (hunk.beforeCount == 0 ? 0 : 1),
                                                                           hunk.beforeCount),
                                                                 rangeText(hunk.afterStart - (hunk.afterCount == 0 ? 0 : 1),
                                                                           hunk.afterCount))}});
        for (const DraftLine& line : hunk.lines) {
            rows.append(lineEntry(line));
        }
    }
    if (diff.leftOut > 0) {
        rows.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("note")},
                                {QStringLiteral("text"), QStringLiteral("… %1 more lines").arg(diff.leftOut)}});
    }
    return rows;
}

QString draftChangeName(DraftChange change)
{
    switch (change) {
    case DraftChange::Added:
        return QStringLiteral("added");
    case DraftChange::Changed:
        return QStringLiteral("changed");
    case DraftChange::Removed:
        return QStringLiteral("removed");
    case DraftChange::None:
        break;
    }
    return {};
}

QHash<QString, QString> draftControlChanges(const input::Profile& live, const input::Profile& draft)
{
    QHash<QString, QString> marks;
    for (const input::Control& control : live.controls) {
        const auto found = std::ranges::find(draft.controls, control.id, &input::Control::id);
        if (found == draft.controls.end()) {
            marks.insert(QString::fromStdString(control.id), draftChangeName(DraftChange::Removed));
        } else if (!(*found == control)) {
            marks.insert(QString::fromStdString(control.id), draftChangeName(DraftChange::Changed));
        }
    }
    for (const input::Control& control : draft.controls) {
        if (std::ranges::find(live.controls, control.id, &input::Control::id) == live.controls.end()) {
            marks.insert(QString::fromStdString(control.id), draftChangeName(DraftChange::Added));
        }
    }
    return marks;
}

QString draftNodeSignature(const blueprint::GraphNode& node)
{
    QStringList parts;
    parts << toQString(node.kind) << toQString(node.tone) << toQString(node.icon) << toQString(node.title)
          << toQString(node.badge) << toQString(node.subtitle) << toQString(node.note) << toQString(node.code)
          << QString::number(node.floating ? 1 : 0) << QString::number(node.start ? 1 : 0) << toQString(node.problem);
    for (const blueprint::GraphPin& pin : node.inputs) {
        parts << QStringLiteral("in") << toQString(pin.name) << toQString(pin.type) << toQString(pin.value)
              << QString::number(pin.connected ? 1 : 0) << QString::number(pin.optional ? 1 : 0);
    }
    for (const blueprint::GraphPin& pin : node.outputs) {
        parts << QStringLiteral("out") << toQString(pin.name) << toQString(pin.type)
              << QString::number(pin.connected ? 1 : 0);
    }
    return parts.join(QChar(u'\u001f'));
}

DraftGraphChanges draftGraphChanges(const blueprint::Graph& live, const blueprint::Graph& draft)
{
    DraftGraphChanges changes;
    const auto findLive = [&live](std::string_view key) {
        return std::ranges::find(live.nodes, key, &blueprint::GraphNode::key);
    };
    for (const blueprint::GraphNode& node : draft.nodes) {
        const QString key = toQString(node.key);
        const auto found = findLive(node.key);
        if (found == live.nodes.end()) {
            changes.marks.insert(key, draftChangeName(DraftChange::Added));
            ++changes.added;
        } else if (draftNodeSignature(node) != draftNodeSignature(*found)) {
            changes.marks.insert(key, draftChangeName(DraftChange::Changed));
            ++changes.changed;
        }
    }
    for (const blueprint::GraphNode& node : live.nodes) {
        const auto found = std::ranges::find(draft.nodes, node.key, &blueprint::GraphNode::key);
        if (found != draft.nodes.end()) {
            continue;
        }
        changes.removed.append(QVariantMap{{QStringLiteral("key"), toQString(node.key)},
                                           {QStringLiteral("title"), toQString(node.title)},
                                           {QStringLiteral("kind"), toQString(node.kind)},
                                           {QStringLiteral("note"), toQString(node.note)}});
        ++changes.removedCount;
    }
    return changes;
}

}
