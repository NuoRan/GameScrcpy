#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gs::blueprint {

struct TextChange {
    std::size_t start = 0;
    std::size_t end = 0;
    std::string text;

    friend bool operator==(const TextChange&, const TextChange&) = default;
};

[[nodiscard]] std::string applied(std::string_view text, std::span<const TextChange> changes);

struct Place {
    std::uint32_t line = 1;
    std::uint32_t column = 1;

    friend bool operator==(const Place&, const Place&) = default;
};

class Lines
{
public:
    explicit Lines(std::string_view text);

    [[nodiscard]] Place place(std::size_t offset) const;
    [[nodiscard]] std::string position(std::size_t offset) const;
    [[nodiscard]] std::size_t offset(Place place) const;
    [[nodiscard]] std::size_t lineStart(std::uint32_t line) const;
    [[nodiscard]] std::uint32_t lineOf(std::size_t offset) const;
    [[nodiscard]] std::uint32_t count() const noexcept { return static_cast<std::uint32_t>(m_starts.size()); }

private:
    std::string_view m_text;
    std::vector<std::size_t> m_starts;
};

[[nodiscard]] std::optional<Place> parsePosition(std::string_view position);
[[nodiscard]] std::string positionText(Place place);

}
