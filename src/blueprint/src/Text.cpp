#include "gs/blueprint/Text.h"

#include <algorithm>
#include <charconv>

namespace gs::blueprint {

namespace {

constexpr std::string_view kBom = "\xEF\xBB\xBF";

std::size_t unitsOf(std::uint8_t lead) noexcept
{
    return lead >= 0xF0 ? 2 : 1;
}

std::size_t lengthOf(std::uint8_t lead) noexcept
{
    if (lead < 0x80) {
        return 1;
    }
    if (lead >= 0xF0) {
        return 4;
    }
    if (lead >= 0xE0) {
        return 3;
    }
    if (lead >= 0xC0) {
        return 2;
    }
    return 1;
}

}

std::string applied(std::string_view text, std::span<const TextChange> changes)
{
    std::vector<const TextChange*> order;
    order.reserve(changes.size());
    for (const TextChange& change : changes) {
        order.push_back(&change);
    }
    std::ranges::stable_sort(order, [](const TextChange* a, const TextChange* b) { return a->start < b->start; });
    std::string out;
    out.reserve(text.size());
    std::size_t at = 0;
    for (const TextChange* change : order) {
        const std::size_t start = std::clamp(change->start, at, text.size());
        const std::size_t end = std::clamp(change->end, start, text.size());
        out.append(text.substr(at, start - at));
        out += change->text;
        at = end;
    }
    out.append(text.substr(std::min(at, text.size())));
    return out;
}

Lines::Lines(std::string_view text) : m_text(text)
{
    m_starts.push_back(0);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            m_starts.push_back(i + 1);
        } else if (c == '\n') {
            m_starts.push_back(i + 1);
        }
    }
}

std::size_t Lines::lineStart(std::uint32_t line) const
{
    if (line == 0) {
        return 0;
    }
    const std::size_t index = std::min<std::size_t>(line - 1, m_starts.size() - 1);
    const std::size_t start = m_starts[index];
    if (index == 0 && m_text.starts_with(kBom)) {
        return kBom.size();
    }
    return start;
}

std::uint32_t Lines::lineOf(std::size_t offset) const
{
    const auto found = std::ranges::upper_bound(m_starts, offset);
    return static_cast<std::uint32_t>(found - m_starts.begin());
}

Place Lines::place(std::size_t offset) const
{
    offset = std::min(offset, m_text.size());
    const std::uint32_t line = lineOf(offset);
    std::size_t at = lineStart(line);
    std::uint32_t column = 1;
    while (at < offset) {
        const auto lead = static_cast<std::uint8_t>(m_text[at]);
        column += static_cast<std::uint32_t>(unitsOf(lead));
        at += lengthOf(lead);
    }
    return {line, column};
}

std::string Lines::position(std::size_t offset) const
{
    return positionText(place(offset));
}

std::size_t Lines::offset(Place place) const
{
    std::size_t at = lineStart(place.line);
    const std::size_t end = place.line < m_starts.size() ? m_starts[place.line] : m_text.size();
    std::uint32_t column = 1;
    while (column < place.column && at < end) {
        const auto lead = static_cast<std::uint8_t>(m_text[at]);
        if (lead == '\n' || lead == '\r') {
            break;
        }
        column += static_cast<std::uint32_t>(unitsOf(lead));
        at += lengthOf(lead);
    }
    return std::min(at, m_text.size());
}

std::optional<Place> parsePosition(std::string_view position)
{
    const std::size_t colon = position.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    Place place;
    const std::string_view line = position.substr(0, colon);
    const std::string_view column = position.substr(colon + 1);
    const auto first = std::from_chars(line.data(), line.data() + line.size(), place.line);
    const auto second = std::from_chars(column.data(), column.data() + column.size(), place.column);
    if (first.ec != std::errc() || first.ptr != line.data() + line.size() || second.ec != std::errc()
        || second.ptr != column.data() + column.size() || place.line == 0 || place.column == 0) {
        return std::nullopt;
    }
    return place;
}

std::string positionText(Place place)
{
    return std::to_string(place.line) + ":" + std::to_string(place.column);
}

}
