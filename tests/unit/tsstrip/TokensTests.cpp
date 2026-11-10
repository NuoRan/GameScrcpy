#include <gs/tsstrip/Tokens.h>

#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

using namespace gs::tsstrip;

namespace {

std::vector<std::string> textsOf(const std::vector<CodeToken>& tokens)
{
    std::vector<std::string> texts;
    for (const CodeToken& token : tokens) {
        texts.emplace_back(token.text);
    }
    return texts;
}

std::vector<TokenKind> kindsOf(const std::vector<CodeToken>& tokens)
{
    std::vector<TokenKind> kinds;
    for (const CodeToken& token : tokens) {
        kinds.push_back(token.kind);
    }
    return kinds;
}

}

TEST_CASE("tokens skip comments and keep strings whole, with their lines")
{
    const std::vector<CodeToken> tokens = tokenize("// controls.a.onPress(\n"
                                                   "const s = \"controls.b.onPress(\"; /* controls.c\n"
                                                   " */ controls.d.onPress(() => {});\r\n"
                                                   "x\r"
                                                   "y");
    CHECK(textsOf(tokens) == std::vector<std::string>{"const", "s", "=", "\"controls.b.onPress(\"", ";", "controls", ".",
                                                      "d", ".", "onPress", "(", "(", ")", "=>", "{", "}", ")", ";",
                                                      "x", "y"});
    REQUIRE(tokens.size() == 20);
    CHECK(tokens[0].line == 2);
    CHECK(tokens[0].newlineBefore);
    CHECK(tokens[3].kind == TokenKind::String);
    CHECK(tokens[5].line == 3);
    CHECK(tokens[5].newlineBefore);
    CHECK_FALSE(tokens[6].newlineBefore);
    CHECK(tokens[13].kind == TokenKind::Punctuator);
    CHECK(tokens[18].line == 4);
    CHECK(tokens[19].line == 5);
}

TEST_CASE("a slash starts a regular expression where an expression may start, and divides elsewhere")
{
    const std::vector<CodeToken> regex = tokenize("const r = /\"[/]'/g; f(a / b, /x/); return /y/");
    CHECK(textsOf(regex) == std::vector<std::string>{"const", "r", "=", "/\"[/]'/g", ";", "f", "(", "a", "/", "b", ",",
                                                     "/x/", ")", ";", "return", "/y/"});
    CHECK(kindsOf(regex)[3] == TokenKind::Regex);
    CHECK(kindsOf(regex)[8] == TokenKind::Punctuator);
    CHECK(kindsOf(regex)[11] == TokenKind::Regex);
    CHECK(kindsOf(regex)[15] == TokenKind::Regex);
    const std::vector<CodeToken> division = tokenize("x = (a) / 2 / c[1] / 3");
    CHECK(textsOf(division) == std::vector<std::string>{"x", "=", "(", "a", ")", "/", "2", "/", "c", "[", "1", "]", "/",
                                                        "3"});
}

TEST_CASE("template literals keep their text parts whole and give the code inside their substitutions")
{
    const std::vector<CodeToken> tokens = tokenize("t = `a ${ {k: `in ${x}`}.k } b ${y} c`; z >= 1;");
    CHECK(textsOf(tokens) == std::vector<std::string>{"t", "=", "`a ${", "{", "k", ":", "`in ${", "x", "}`", "}", ".",
                                                      "k", "} b ${", "y", "} c`", ";", "z", ">=", "1", ";"});
    CHECK(kindsOf(tokens)[2] == TokenKind::Template);
    CHECK(kindsOf(tokens)[8] == TokenKind::Template);
    CHECK(kindsOf(tokens)[12] == TokenKind::Template);
    CHECK(kindsOf(tokens)[14] == TokenKind::Template);
}

TEST_CASE("unterminated input ends the tokens instead of looping")
{
    CHECK(tokenize("").empty());
    CHECK_FALSE(tokenize("a /* never closed").empty());
    CHECK_FALSE(tokenize("s = \"open").empty());
    CHECK_FALSE(tokenize("t = `open ${x").empty());
}
