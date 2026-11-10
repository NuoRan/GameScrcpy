#include "gs/ext/Version.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <utility>

namespace gs::ext {

namespace {

constexpr std::size_t kMaxVersionLength = 64;
constexpr std::size_t kMaxRangeLength = 128;
constexpr std::size_t kMaxNumberDigits = 9;

bool digitsOnly(std::string_view text) noexcept
{
    return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

bool identifierChar(char c) noexcept
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
}

std::vector<std::string_view> split(std::string_view text, char separator)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = text.find(separator, start);
        if (end == std::string_view::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

std::optional<std::uint32_t> number(std::string_view text) noexcept
{
    if (!digitsOnly(text) || text.size() > kMaxNumberDigits || (text.size() > 1 && text.front() == '0')) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

bool validPrerelease(std::string_view text)
{
    return !text.empty() && std::ranges::all_of(split(text, '.'), [](std::string_view part) {
        return !part.empty() && std::ranges::all_of(part, identifierChar) &&
               !(digitsOnly(part) && part.size() > 1 && part.front() == '0');
    });
}

struct Partial {
    Version version;
    std::size_t components = 0;
};

std::optional<Partial> partial(std::string_view text)
{
    if (text.empty() || text.size() > kMaxVersionLength) {
        return std::nullopt;
    }
    Partial result;
    const std::size_t dash = text.find('-');
    if (dash != std::string_view::npos) {
        const std::string_view tag = text.substr(dash + 1);
        if (!validPrerelease(tag)) {
            return std::nullopt;
        }
        result.version.prerelease = std::string(tag);
    }
    const std::vector<std::string_view> parts = split(text.substr(0, dash), '.');
    if (parts.size() > 3) {
        return std::nullopt;
    }
    std::uint32_t* const fields[] = {&result.version.major, &result.version.minor, &result.version.patch};
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const std::optional<std::uint32_t> value = number(parts[i]);
        if (!value) {
            return std::nullopt;
        }
        *fields[i] = *value;
    }
    result.components = parts.size();
    if (!result.version.prerelease.empty() && result.components != 3) {
        return std::nullopt;
    }
    return result;
}

std::strong_ordering comparePrerelease(std::string_view a, std::string_view b)
{
    if (a == b) {
        return std::strong_ordering::equal;
    }
    if (a.empty() || b.empty()) {
        return a.empty() ? std::strong_ordering::greater : std::strong_ordering::less;
    }
    const std::vector<std::string_view> left = split(a, '.');
    const std::vector<std::string_view> right = split(b, '.');
    for (std::size_t i = 0; i < std::min(left.size(), right.size()); ++i) {
        const bool leftNumeric = digitsOnly(left[i]);
        const bool rightNumeric = digitsOnly(right[i]);
        if (leftNumeric != rightNumeric) {
            return leftNumeric ? std::strong_ordering::less : std::strong_ordering::greater;
        }
        if (leftNumeric && left[i].size() != right[i].size()) {
            return left[i].size() <=> right[i].size();
        }
        if (const std::strong_ordering order = left[i] <=> right[i]; order != 0) {
            return order;
        }
    }
    return left.size() <=> right.size();
}

std::string_view trimmed(std::string_view text) noexcept
{
    const auto blank = [](char c) { return c == ' ' || c == '\t'; };
    while (!text.empty() && blank(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && blank(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

bool satisfies(const VersionRange::Comparator& comparator, const Version& version)
{
    const std::strong_ordering order = version <=> comparator.version;
    switch (comparator.op) {
    case VersionRange::Op::Equal:
        return order == 0;
    case VersionRange::Op::Greater:
        return order > 0;
    case VersionRange::Op::GreaterEqual:
        return order >= 0;
    case VersionRange::Op::Less:
        return order < 0;
    case VersionRange::Op::LessEqual:
        return order <= 0;
    }
    return false;
}

}

std::optional<Version> Version::parse(std::string_view text)
{
    const std::optional<Partial> parsed = partial(text);
    if (!parsed || parsed->components != 3) {
        return std::nullopt;
    }
    return parsed->version;
}

std::string Version::text() const
{
    std::string text = std::to_string(major) + '.' + std::to_string(minor) + '.' + std::to_string(patch);
    if (!prerelease.empty()) {
        text += '-' + prerelease;
    }
    return text;
}

bool Version::sameRelease(const Version& other) const noexcept
{
    return major == other.major && minor == other.minor && patch == other.patch;
}

std::strong_ordering operator<=>(const Version& a, const Version& b)
{
    if (const std::strong_ordering order = a.major <=> b.major; order != 0) {
        return order;
    }
    if (const std::strong_ordering order = a.minor <=> b.minor; order != 0) {
        return order;
    }
    if (const std::strong_ordering order = a.patch <=> b.patch; order != 0) {
        return order;
    }
    return comparePrerelease(a.prerelease, b.prerelease);
}

std::optional<VersionRange> VersionRange::parse(std::string_view text)
{
    const std::string_view range = trimmed(text);
    if (range.empty() || range.size() > kMaxRangeLength) {
        return std::nullopt;
    }
    VersionRange result;
    result.m_text = std::string(range);
    if (range == "*") {
        return result;
    }
    if (range.front() == '^' || range.front() == '~') {
        const std::optional<Partial> base = partial(range.substr(1));
        if (!base) {
            return std::nullopt;
        }
        const Version& low = base->version;
        Version high;
        if (range.front() == '~') {
            high = base->components == 1 ? Version{low.major + 1, 0, 0, {}} : Version{low.major, low.minor + 1, 0, {}};
        } else if (low.major > 0 || base->components == 1) {
            high = Version{low.major + 1, 0, 0, {}};
        } else if (low.minor > 0 || base->components == 2) {
            high = Version{0, low.minor + 1, 0, {}};
        } else {
            high = Version{0, 0, low.patch + 1, {}};
        }
        result.m_comparators = {{Op::GreaterEqual, low}, {Op::Less, high}};
        return result;
    }
    for (const std::string_view token : split(range, ' ')) {
        if (token.empty()) {
            continue;
        }
        static constexpr std::pair<std::string_view, Op> kOperators[] = {
            {">=", Op::GreaterEqual}, {"<=", Op::LessEqual}, {">", Op::Greater}, {"<", Op::Less}, {"=", Op::Equal}};
        Op op = Op::Equal;
        std::string_view rest = token;
        bool written = false;
        for (const auto& [prefix, candidate] : kOperators) {
            if (rest.starts_with(prefix)) {
                op = candidate;
                rest.remove_prefix(prefix.size());
                written = true;
                break;
            }
        }
        const std::optional<Partial> bound = partial(rest);
        if (!bound || ((!written || op == Op::Equal) && bound->components != 3)) {
            return std::nullopt;
        }
        result.m_comparators.push_back({op, bound->version});
    }
    if (result.m_comparators.empty()) {
        return std::nullopt;
    }
    return result;
}

VersionRange VersionRange::any()
{
    return {};
}

bool VersionRange::contains(const Version& version) const
{
    if (!version.prerelease.empty() && std::ranges::none_of(m_comparators, [&version](const Comparator& comparator) {
            return !comparator.version.prerelease.empty() && comparator.version.sameRelease(version);
        })) {
        return false;
    }
    return std::ranges::all_of(m_comparators,
                               [&version](const Comparator& comparator) { return satisfies(comparator, version); });
}

}
