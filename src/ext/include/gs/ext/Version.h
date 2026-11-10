#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::ext {

struct Version {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
    std::string prerelease;

    [[nodiscard]] static std::optional<Version> parse(std::string_view text);
    [[nodiscard]] std::string text() const;
    [[nodiscard]] bool sameRelease(const Version& other) const noexcept;

    friend bool operator==(const Version&, const Version&) = default;
    friend std::strong_ordering operator<=>(const Version& a, const Version& b);
};

class VersionRange
{
public:
    enum class Op : std::uint8_t { Equal, Greater, GreaterEqual, Less, LessEqual };

    struct Comparator {
        Op op = Op::Equal;
        Version version;

        friend bool operator==(const Comparator&, const Comparator&) = default;
    };

    [[nodiscard]] static std::optional<VersionRange> parse(std::string_view text);
    [[nodiscard]] static VersionRange any();

    [[nodiscard]] bool contains(const Version& version) const;
    [[nodiscard]] const std::string& text() const noexcept { return m_text; }
    [[nodiscard]] const std::vector<Comparator>& comparators() const noexcept { return m_comparators; }

    friend bool operator==(const VersionRange&, const VersionRange&) = default;

private:
    std::string m_text = "*";
    std::vector<Comparator> m_comparators;
};

}
