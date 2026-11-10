#include "CommandMatcher.h"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace gs::app {

namespace {

constexpr int kTier = 1000;
constexpr int kWordStartTier = 6 * kTier;
constexpr int kSubstringTier = 5 * kTier;
constexpr int kSubsequenceTier = 4 * kTier;
constexpr int kTitleOrGroupTier = 3 * kTier;
constexpr int kKeywordTier = 2 * kTier;
constexpr int kIdTier = kTier;
constexpr int kFieldWordStart = kTier / 2;
constexpr int kGapCost = 10;
constexpr int kBreakCost = 30;
constexpr int kInnerCost = 50;
constexpr qsizetype kNone = std::numeric_limits<qsizetype>::max();
constexpr char16_t kFullWidthFirst = u'\uFF01';
constexpr char16_t kFullWidthLast = u'\uFF5E';
constexpr char16_t kFullWidthShift = 0xFEE0;

struct Hit {
    int score = 0;
    std::vector<TextRange> ranges;
};

struct Field {
    QStringView text;
    QString folded;
    int tier = 0;
};

QChar fold(QChar c)
{
    if (c.unicode() >= kFullWidthFirst && c.unicode() <= kFullWidthLast) {
        c = QChar(static_cast<char16_t>(c.unicode() - kFullWidthShift));
    }
    return c.toCaseFolded();
}

QString fold(QStringView text)
{
    QString folded;
    folded.reserve(text.size());
    for (const QChar c : text) {
        folded.append(fold(c));
    }
    return folded;
}

bool isHan(QChar c)
{
    return c.script() == QChar::Script_Han;
}

bool startsWord(QStringView text, qsizetype at)
{
    const QChar c = text[at];
    if (isHan(c)) {
        return true;
    }
    if (!c.isLetterOrNumber()) {
        return false;
    }
    if (at == 0) {
        return true;
    }
    const QChar before = text[at - 1];
    return !before.isLetterOrNumber() || isHan(before) || (c.isUpper() && before.isLower());
}

int earliness(qsizetype at, int range)
{
    return range - 1 - static_cast<int>(std::min<qsizetype>(at, range - 1));
}

TextRange rangeOf(qsizetype start, qsizetype length)
{
    return {static_cast<int>(start), static_cast<int>(length)};
}

Hit subsequence(QStringView text, QStringView folded, QStringView term)
{
    const qsizetype length = folded.size();
    const qsizetype letters = term.size();
    if (letters == 0 || letters > length) {
        return {};
    }
    const auto cell = [length](qsizetype letter, qsizetype at) {
        return static_cast<std::size_t>(letter * length + at);
    };
    const auto inner = [text](qsizetype at) {
        return startsWord(text, at) ? 0 : kInnerCost;
    };
    std::vector<qsizetype> costs(static_cast<std::size_t>(letters * length), kNone);
    std::vector<qsizetype> links(costs.size(), -1);
    for (qsizetype at = 0; at < length; ++at) {
        if (folded[at] == term[0]) {
            costs[cell(0, at)] = at + inner(at);
        }
    }
    for (qsizetype letter = 1; letter < letters; ++letter) {
        qsizetype carried = kNone;
        qsizetype carriedFrom = -1;
        for (qsizetype at = letter; at < length; ++at) {
            if (carried != kNone) {
                carried += kGapCost;
            }
            const qsizetype skipFrom = at - 2;
            if (skipFrom >= 0 && costs[cell(letter - 1, skipFrom)] != kNone) {
                const qsizetype skip = costs[cell(letter - 1, skipFrom)] + kGapCost + kBreakCost;
                if (skip < carried) {
                    carried = skip;
                    carriedFrom = skipFrom;
                }
            }
            if (folded[at] != term[letter]) {
                continue;
            }
            qsizetype best = costs[cell(letter - 1, at - 1)];
            qsizetype from = at - 1;
            if (carried < best) {
                best = carried;
                from = carriedFrom;
            }
            if (best != kNone) {
                costs[cell(letter, at)] = best + inner(at);
                links[cell(letter, at)] = from;
            }
        }
    }
    qsizetype end = -1;
    for (qsizetype at = letters - 1; at < length; ++at) {
        const qsizetype total = costs[cell(letters - 1, at)];
        if (total != kNone && (end < 0 || total < costs[cell(letters - 1, end)])) {
            end = at;
        }
    }
    if (end < 0) {
        return {};
    }
    std::vector<qsizetype> positions(static_cast<std::size_t>(letters));
    for (qsizetype letter = letters - 1, at = end; letter >= 0; --letter) {
        positions[static_cast<std::size_t>(letter)] = at;
        at = links[cell(letter, at)];
    }
    Hit hit;
    const qsizetype total = costs[cell(letters - 1, end)];
    hit.score = kSubsequenceTier + static_cast<int>(std::max<qsizetype>(0, kTier - 1 - total));
    for (const qsizetype at : positions) {
        if (!hit.ranges.empty() && hit.ranges.back().start + hit.ranges.back().length == at) {
            ++hit.ranges.back().length;
        } else {
            hit.ranges.push_back(rangeOf(at, 1));
        }
    }
    return hit;
}

Hit titleHit(QStringView text, QStringView folded, QStringView term)
{
    qsizetype inside = -1;
    for (qsizetype at = folded.indexOf(term); at >= 0; at = folded.indexOf(term, at + 1)) {
        if (startsWord(text, at)) {
            return {kWordStartTier + earliness(at, kTier), {rangeOf(at, term.size())}};
        }
        if (inside < 0) {
            inside = at;
        }
    }
    if (inside >= 0) {
        return {kSubstringTier + earliness(inside, kTier), {rangeOf(inside, term.size())}};
    }
    return subsequence(text, folded, term);
}

int fieldScore(const Field& field, QStringView term)
{
    int best = 0;
    const QStringView folded = field.folded;
    for (qsizetype at = folded.indexOf(term); at >= 0; at = folded.indexOf(term, at + 1)) {
        const int wordStart = startsWord(field.text, at) ? kFieldWordStart : 0;
        best = std::max(best, field.tier + wordStart + earliness(at, kFieldWordStart));
    }
    return best;
}

}

CommandMatcher::CommandMatcher(QStringView query)
    : m_terms(fold(query).simplified().split(u' ', Qt::SkipEmptyParts))
{}

CommandMatch CommandMatcher::match(const CommandText& text) const
{
    return match(std::span(&text, 1));
}

CommandMatch CommandMatcher::match(std::span<const CommandText> texts) const
{
    CommandMatch result;
    if (texts.empty()) {
        return result;
    }
    for (const CommandText& text : texts) {
        result.score = std::max(result.score, score(text));
    }
    result.matched = m_terms.isEmpty() || result.score > 0;
    if (result.matched) {
        result.highlights = highlights(texts.front().title);
    }
    return result;
}

std::vector<TextRange> CommandMatcher::highlights(QStringView title) const
{
    const QString folded = fold(title);
    std::vector<TextRange> ranges;
    for (const QString& term : m_terms) {
        const Hit hit = titleHit(title, folded, term);
        ranges.insert(ranges.end(), hit.ranges.begin(), hit.ranges.end());
    }
    std::ranges::sort(ranges, {}, &TextRange::start);
    std::vector<TextRange> merged;
    for (const TextRange& range : ranges) {
        if (merged.empty() || range.start > merged.back().start + merged.back().length) {
            merged.push_back(range);
        } else {
            merged.back().length =
                std::max(merged.back().length, range.start + range.length - merged.back().start);
        }
    }
    return merged;
}

bool CommandMatcher::ranksBefore(const CommandMatch& first, const CommandText& firstText,
                                 const CommandMatch& second, const CommandText& secondText)
{
    if (first.score != second.score) {
        return first.score > second.score;
    }
    if (firstText.title.size() != secondText.title.size()) {
        return firstText.title.size() < secondText.title.size();
    }
    return firstText.id < secondText.id;
}

int CommandMatcher::score(const CommandText& text) const
{
    if (m_terms.isEmpty()) {
        return 0;
    }
    const QString title = fold(text.title);
    std::vector<Field> fields;
    const auto add = [&fields](const QString& value, int tier) {
        if (!value.isEmpty()) {
            fields.push_back({value, fold(value), tier});
        }
    };
    add(text.sourceTitle, kTitleOrGroupTier);
    for (const QString& groupTitle : text.groupTitles) {
        add(groupTitle, kTitleOrGroupTier);
    }
    for (const QString& keywords : text.keywords) {
        add(keywords, kKeywordTier);
    }
    add(text.id, kIdTier);

    int total = 0;
    for (const QString& term : m_terms) {
        int best = titleHit(text.title, title, term).score;
        if (best == 0) {
            for (const Field& field : fields) {
                best = std::max(best, fieldScore(field, term));
            }
        }
        if (best == 0) {
            return 0;
        }
        total += best;
    }
    return total;
}

}
