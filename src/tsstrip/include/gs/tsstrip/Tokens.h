#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace gs::tsstrip {

enum class TokenKind : std::uint8_t { Name, String, Number, Template, Regex, Punctuator, Invalid };

struct CodeToken {
    TokenKind kind = TokenKind::Invalid;
    std::string_view text;
    std::uint32_t line = 1;
    bool newlineBefore = false;

    friend bool operator==(const CodeToken&, const CodeToken&) = default;
};

[[nodiscard]] std::vector<CodeToken> tokenize(std::string_view javascript);

}
