#include "gs/tsstrip/Tokens.h"

#include "Scanner.h"

#include <algorithm>
#include <array>

namespace gs::tsstrip {

namespace {

using detail::Scanner;
using detail::Token;
using detail::TokenType;

constexpr std::array<std::string_view, 13> kBeforeExpression{
    "return", "typeof", "instanceof", "in", "of", "new", "delete", "void", "throw", "case", "do", "else", "await",
};

TokenKind kindOf(TokenType type)
{
    switch (type) {
    case TokenType::Name:
    case TokenType::PrivateName:
        return TokenKind::Name;
    case TokenType::String:
        return TokenKind::String;
    case TokenType::Number:
        return TokenKind::Number;
    case TokenType::Regex:
        return TokenKind::Regex;
    case TokenType::Template:
    case TokenType::TemplateHead:
    case TokenType::TemplateMiddle:
    case TokenType::TemplateTail:
        return TokenKind::Template;
    case TokenType::Invalid:
    case TokenType::Eof:
        return TokenKind::Invalid;
    default:
        return TokenKind::Punctuator;
    }
}

bool regexAllowed(const std::vector<CodeToken>& tokens)
{
    if (tokens.empty()) {
        return true;
    }
    const CodeToken& last = tokens.back();
    switch (last.kind) {
    case TokenKind::Name:
        return std::ranges::find(kBeforeExpression, last.text) != kBeforeExpression.end();
    case TokenKind::Punctuator:
        return last.text != ")" && last.text != "]" && last.text != "}" && last.text != "++" && last.text != "--";
    case TokenKind::Template:
        return last.text.ends_with("${");
    case TokenKind::String:
    case TokenKind::Number:
    case TokenKind::Regex:
    case TokenKind::Invalid:
        break;
    }
    return false;
}

}

std::vector<CodeToken> tokenize(std::string_view javascript)
{
    std::vector<CodeToken> tokens;
    Scanner scanner(javascript);
    std::vector<bool> braces;
    std::uint32_t line = 1;
    std::size_t counted = 0;
    const auto lineAt = [&](std::uint32_t offset) {
        for (; counted < offset && counted < javascript.size(); ++counted) {
            const char c = javascript[counted];
            if (c == '\n' || (c == '\r' && (counted + 1 >= javascript.size() || javascript[counted + 1] != '\n'))) {
                ++line;
            }
        }
        return line;
    };
    bool started = false;
    std::uint32_t previous = 0;
    for (;;) {
        scanner.next();
        Token token = scanner.token();
        if (token.type == TokenType::Eof || (started && token.start <= previous)) {
            break;
        }
        if ((token.type == TokenType::Slash || token.type == TokenType::SlashAssign) && regexAllowed(tokens)) {
            scanner.rescanRegex();
            token = scanner.token();
        } else if (token.type == TokenType::RightBrace && !braces.empty() && braces.back()) {
            scanner.rescanTemplateContinuation();
            token = scanner.token();
            if (token.type != TokenType::TemplateMiddle) {
                braces.pop_back();
            }
        } else if (token.type == TokenType::Greater) {
            scanner.rescanGreater();
            token = scanner.token();
        }
        if (token.type == TokenType::LeftBrace) {
            braces.push_back(false);
        } else if (token.type == TokenType::RightBrace && !braces.empty()) {
            braces.pop_back();
        } else if (token.type == TokenType::TemplateHead) {
            braces.push_back(true);
        }
        started = true;
        previous = token.start;
        tokens.push_back(CodeToken{.kind = kindOf(token.type),
                                   .text = scanner.text(token),
                                   .line = lineAt(token.start),
                                   .newlineBefore = token.newlineBefore});
    }
    return tokens;
}

}
