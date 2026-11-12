#pragma once

#include <cstddef>
#include <string_view>

namespace gs {

inline constexpr std::size_t kMostJsonNesting = 64;

[[nodiscard]] constexpr bool jsonNestingWithin(std::string_view text, std::size_t most) noexcept
{
    std::size_t depth = 0;
    bool inString = false;
    bool escaped = false;
    for (const char c : text) {
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
        } else if (c == '"') {
            inString = true;
        } else if (c == '{' || c == '[') {
            if (++depth > most) {
                return false;
            }
        } else if ((c == '}' || c == ']') && depth > 0) {
            --depth;
        }
    }
    return true;
}

}
