#include "Syntax.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>
#include <utility>

namespace gs::blueprint::syntax {

namespace {

constexpr int kMostDepth = 256;

enum class T : std::uint8_t { Eof, Name, Private, Number, String, Template, Head, Middle, Tail, Regex, Punct, Invalid };

constexpr std::uint64_t packed(std::string_view text, std::size_t from) noexcept
{
    std::uint64_t code = 0;
    for (std::size_t i = 0; i < 8 && from + i < text.size(); ++i) {
        code |= static_cast<std::uint64_t>(static_cast<unsigned char>(text[from + i])) << (8 * i);
    }
    return code;
}

consteval std::uint64_t spell(const char* text)
{
    return packed(std::string_view(text), 0);
}

struct Spelling {
    std::uint64_t code = 0;
    std::uint64_t more = 0;

    consteval Spelling(const char* text) : code(packed(text, 0)), more(packed(text, 8)) {}
};

struct Token {
    T type = T::Eof;
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    bool newline = false;
    std::string_view text;
    std::uint64_t code = 0;
    std::uint64_t more = 0;
};

struct Abort {
};

struct CodePoint {
    std::uint32_t value = 0;
    std::uint32_t length = 1;
};

CodePoint decode(std::string_view text, std::size_t at) noexcept
{
    const auto lead = static_cast<std::uint8_t>(text[at]);
    if (lead < 0x80) {
        return {lead, 1};
    }
    std::uint32_t length = 0;
    std::uint32_t value = 0;
    if ((lead & 0xE0) == 0xC0) {
        length = 2;
        value = lead & 0x1Fu;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        value = lead & 0x0Fu;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        value = lead & 0x07u;
    } else {
        return {0xFFFD, 1};
    }
    if (at + length > text.size()) {
        return {0xFFFD, 1};
    }
    for (std::uint32_t i = 1; i < length; ++i) {
        const auto byte = static_cast<std::uint8_t>(text[at + i]);
        if ((byte & 0xC0) != 0x80) {
            return {0xFFFD, 1};
        }
        value = (value << 6) | (byte & 0x3Fu);
    }
    return {value, length};
}

bool lineBreak(std::uint32_t value) noexcept
{
    return value == '\n' || value == '\r' || value == 0x2028 || value == 0x2029;
}

bool space(std::uint32_t value) noexcept
{
    return value == ' ' || value == '\t' || value == 0x0B || value == 0x0C || value == 0xA0 || value == 0xFEFF
        || value == 0x1680 || (value >= 0x2000 && value <= 0x200A) || value == 0x202F || value == 0x205F
        || value == 0x3000;
}

bool digit(std::uint8_t c) noexcept
{
    return c >= '0' && c <= '9';
}

bool nameStart(std::uint8_t c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '$' || c == '_' || c == '\\' || c >= 0x80;
}

bool namePart(std::uint8_t c) noexcept
{
    return nameStart(c) || digit(c);
}

bool reservedCode(std::uint64_t code, std::uint64_t more) noexcept
{
    static constexpr Spelling kInstanceof{"instanceof"};
    if (more != 0) {
        return code == kInstanceof.code && more == kInstanceof.more;
    }
    switch (code) {
    case spell("break"):
    case spell("case"):
    case spell("catch"):
    case spell("class"):
    case spell("const"):
    case spell("continue"):
    case spell("debugger"):
    case spell("default"):
    case spell("delete"):
    case spell("do"):
    case spell("else"):
    case spell("enum"):
    case spell("export"):
    case spell("extends"):
    case spell("false"):
    case spell("finally"):
    case spell("for"):
    case spell("function"):
    case spell("if"):
    case spell("import"):
    case spell("in"):
    case spell("new"):
    case spell("null"):
    case spell("return"):
    case spell("super"):
    case spell("switch"):
    case spell("this"):
    case spell("throw"):
    case spell("true"):
    case spell("try"):
    case spell("typeof"):
    case spell("var"):
    case spell("void"):
    case spell("while"):
    case spell("with"):
        return true;
    default:
        return false;
    }
}

bool reserved(std::string_view name) noexcept
{
    return name.size() <= 16 && reservedCode(packed(name, 0), packed(name, 8));
}

bool statementWord(std::uint64_t code) noexcept
{
    switch (code) {
    case spell("var"):
    case spell("const"):
    case spell("let"):
    case spell("function"):
    case spell("async"):
    case spell("class"):
    case spell("if"):
    case spell("for"):
    case spell("while"):
    case spell("do"):
    case spell("return"):
    case spell("throw"):
    case spell("break"):
    case spell("continue"):
    case spell("try"):
    case spell("switch"):
    case spell("debugger"):
    case spell("import"):
    case spell("export"):
    case spell("enum"):
    case spell("namespac"):
    case spell("module"):
    case spell("with"):
        return true;
    default:
        return false;
    }
}

class Lexer
{
public:
    Lexer(std::string_view source, std::vector<Comment>& comments) noexcept : m_source(source), m_comments(comments) {}

    [[nodiscard]] std::uint32_t position() const noexcept { return m_position; }
    void seek(std::uint32_t position) noexcept { m_position = position; }

    Token next()
    {
        bool newline = false;
        if (!skip(newline)) {
            return {T::Invalid, m_position, size(), newline, "unterminated comment"};
        }
        Token token{T::Eof, m_position, m_position, newline, {}};
        if (m_position >= size()) {
            return token;
        }
        const std::uint8_t c = at(m_position);
        if (digit(c) || (c == '.' && digit(at(m_position + 1)))) {
            return number(token);
        }
        if (c == '"' || c == '\'') {
            return string(token, c);
        }
        if (c == '`') {
            m_position += 1;
            return chunk(token, T::Template, T::Head);
        }
        if (c == '#' && nameStart(at(m_position + 1))) {
            m_position += 1;
            name();
            return finish(token, T::Private);
        }
        if (nameStart(c)) {
            name();
            return finish(token, T::Name);
        }
        return punct(token);
    }

    Token regex(const Token& slash)
    {
        std::uint32_t p = slash.start + 1;
        bool inClass = false;
        while (p < size()) {
            const std::uint8_t c = at(p);
            if (c == '\n' || c == '\r') {
                break;
            }
            if (c == '\\') {
                p += 2;
                continue;
            }
            if (inClass) {
                if (c == ']') {
                    inClass = false;
                }
            } else if (c == '[') {
                inClass = true;
            } else if (c == '/') {
                ++p;
                while (p < size() && namePart(at(p))) {
                    ++p;
                }
                m_position = p;
                Token token = slash;
                token.type = T::Regex;
                token.end = p;
                token.text = m_source.substr(token.start, token.end - token.start);
                return token;
            }
            ++p;
        }
        m_position = size();
        return {T::Invalid, slash.start, size(), slash.newline, "unterminated regular expression"};
    }

    Token continuation(const Token& brace)
    {
        m_position = brace.start + 1;
        return chunk(brace, T::Tail, T::Middle);
    }

private:
    [[nodiscard]] std::uint32_t size() const noexcept { return static_cast<std::uint32_t>(m_source.size()); }
    [[nodiscard]] std::uint8_t at(std::uint32_t index) const noexcept
    {
        return index < m_source.size() ? static_cast<std::uint8_t>(m_source[index]) : 0;
    }

    Token finish(Token token, T type) const
    {
        token.type = type;
        token.end = m_position;
        token.text = m_source.substr(token.start, token.end - token.start);
        if (token.text.size() <= 16) {
            token.code = packed(token.text, 0);
            token.more = packed(token.text, 8);
        } else {
            token.code = ~std::uint64_t{0};
            token.more = ~std::uint64_t{0};
        }
        return token;
    }

    void record(std::uint32_t start, std::uint32_t end, bool block)
    {
        if (m_comments.empty() || start > m_comments.back().start) {
            m_comments.push_back({start, end, block});
        }
    }

    bool skip(bool& newline)
    {
        if (m_position == 0 && at(0) == '#' && at(1) == '!') {
            while (m_position < size() && at(m_position) != '\n' && at(m_position) != '\r') {
                ++m_position;
            }
        }
        while (m_position < size()) {
            const std::uint8_t c = at(m_position);
            if (c == ' ' || c == '\t' || c == 0x0B || c == 0x0C) {
                ++m_position;
                continue;
            }
            if (c == '\n' || c == '\r') {
                newline = true;
                ++m_position;
                continue;
            }
            if (c == '/' && at(m_position + 1) == '/') {
                const std::uint32_t start = m_position;
                while (m_position < size()) {
                    const std::uint8_t d = at(m_position);
                    if (d == '\n' || d == '\r') {
                        break;
                    }
                    if (d >= 0x80 && lineBreak(decode(m_source, m_position).value)) {
                        break;
                    }
                    ++m_position;
                }
                record(start, m_position, false);
                continue;
            }
            if (c == '/' && at(m_position + 1) == '*') {
                const std::uint32_t start = m_position;
                m_position += 2;
                bool closed = false;
                while (m_position < size()) {
                    const std::uint8_t d = at(m_position);
                    if (d == '*' && at(m_position + 1) == '/') {
                        m_position += 2;
                        closed = true;
                        break;
                    }
                    if (d == '\n' || d == '\r') {
                        newline = true;
                    }
                    ++m_position;
                }
                if (!closed) {
                    return false;
                }
                record(start, m_position, true);
                continue;
            }
            if (c < 0x80) {
                return true;
            }
            const CodePoint point = decode(m_source, m_position);
            if (lineBreak(point.value)) {
                newline = true;
                m_position += point.length;
                continue;
            }
            if (space(point.value)) {
                m_position += point.length;
                continue;
            }
            return true;
        }
        return true;
    }

    void name()
    {
        while (m_position < size()) {
            const std::uint8_t c = at(m_position);
            if (c < 0x80) {
                if (c == '\\') {
                    m_position += 2;
                    if (at(m_position - 1) == 'u' && at(m_position) == '{') {
                        while (m_position < size() && at(m_position) != '}') {
                            ++m_position;
                        }
                        ++m_position;
                    } else {
                        m_position += 4;
                    }
                    continue;
                }
                if (!namePart(c)) {
                    return;
                }
                ++m_position;
                continue;
            }
            const CodePoint point = decode(m_source, m_position);
            if (space(point.value) || lineBreak(point.value)) {
                return;
            }
            m_position += point.length;
        }
    }

    Token number(Token token)
    {
        const auto digits = [this] {
            while (digit(at(m_position)) || at(m_position) == '_') {
                ++m_position;
            }
        };
        const auto radix = static_cast<std::uint8_t>(at(m_position + 1) | 0x20);
        if (at(m_position) == '0' && (radix == 'x' || radix == 'o' || radix == 'b')) {
            m_position += 2;
            while (namePart(at(m_position)) && at(m_position) != '\\' && at(m_position) < 0x80) {
                ++m_position;
            }
            return finish(token, T::Number);
        }
        digits();
        if (at(m_position) == '.') {
            ++m_position;
            digits();
        }
        if ((at(m_position) | 0x20) == 'e') {
            std::uint32_t p = m_position + 1;
            if (at(p) == '+' || at(p) == '-') {
                ++p;
            }
            if (digit(at(p))) {
                m_position = p;
                digits();
            }
        }
        if (at(m_position) == 'n') {
            ++m_position;
        }
        if (namePart(at(m_position))) {
            m_position = size();
            return {T::Invalid, token.start, size(), token.newline, "a name cannot start right after a number"};
        }
        return finish(token, T::Number);
    }

    Token string(Token token, std::uint8_t quote)
    {
        ++m_position;
        while (m_position < size()) {
            const std::uint8_t c = at(m_position);
            if (c == quote) {
                ++m_position;
                return finish(token, T::String);
            }
            if (c == '\\') {
                m_position += at(m_position + 1) == '\r' && at(m_position + 2) == '\n' ? 3 : 2;
                continue;
            }
            if (c == '\n' || c == '\r') {
                break;
            }
            ++m_position;
        }
        m_position = size();
        return {T::Invalid, token.start, size(), token.newline, "unterminated string literal"};
    }

    Token chunk(Token token, T closed, T open)
    {
        while (m_position < size()) {
            const std::uint8_t c = at(m_position);
            if (c == '`') {
                ++m_position;
                return finish(token, closed);
            }
            if (c == '$' && at(m_position + 1) == '{') {
                m_position += 2;
                return finish(token, open);
            }
            if (c == '\\') {
                m_position = std::min(m_position + 2, size());
                continue;
            }
            ++m_position;
        }
        m_position = size();
        return {T::Invalid, token.start, size(), token.newline, "unterminated template literal"};
    }

    Token punct(Token token)
    {
        const std::uint8_t c = at(m_position);
        const std::uint8_t c1 = at(m_position + 1);
        const std::uint8_t c2 = at(m_position + 2);
        std::uint32_t length = 1;
        switch (c) {
        case '{':
        case '}':
        case '(':
        case ')':
        case '[':
        case ']':
        case ';':
        case ',':
        case '~':
        case ':':
        case '@':
        case '#':
            break;
        case '.':
            length = c1 == '.' && c2 == '.' ? 3 : 1;
            break;
        case '?':
            if (c1 == '?') {
                length = c2 == '=' ? 3 : 2;
            } else if (c1 == '.' && !digit(c2)) {
                length = 2;
            }
            break;
        case '=':
            if (c1 == '>') {
                length = 2;
            } else if (c1 == '=') {
                length = c2 == '=' ? 3 : 2;
            }
            break;
        case '!':
            if (c1 == '=') {
                length = c2 == '=' ? 3 : 2;
            }
            break;
        case '<':
            if (c1 == '<') {
                length = c2 == '=' ? 3 : 2;
            } else if (c1 == '=') {
                length = 2;
            }
            break;
        case '>':
            if (c1 == '>') {
                if (c2 == '>') {
                    length = at(m_position + 3) == '=' ? 4 : 3;
                } else {
                    length = c2 == '=' ? 3 : 2;
                }
            } else if (c1 == '=') {
                length = 2;
            }
            break;
        case '*':
            if (c1 == '*') {
                length = c2 == '=' ? 3 : 2;
            } else if (c1 == '=') {
                length = 2;
            }
            break;
        case '&':
        case '|':
            if (c1 == c) {
                length = c2 == '=' ? 3 : 2;
            } else if (c1 == '=') {
                length = 2;
            }
            break;
        case '+':
        case '-':
            length = c1 == c || c1 == '=' ? 2 : 1;
            break;
        case '/':
        case '%':
        case '^':
            length = c1 == '=' ? 2 : 1;
            break;
        default:
            m_position = size();
            return {T::Invalid, token.start, size(), token.newline, "invalid character"};
        }
        m_position += length;
        return finish(token, T::Punct);
    }

    std::string_view m_source;
    std::vector<Comment>& m_comments;
    std::uint32_t m_position = 0;
};

bool endsOperand(const Token& token) noexcept
{
    switch (token.type) {
    case T::Name:
        if (token.more != 0) {
            return token.code != spell("instance");
        }
        switch (token.code) {
        case spell("return"):
        case spell("typeof"):
        case spell("in"):
        case spell("of"):
        case spell("new"):
        case spell("delete"):
        case spell("void"):
        case spell("throw"):
        case spell("case"):
        case spell("do"):
        case spell("else"):
        case spell("yield"):
        case spell("await"):
            return false;
        default:
            return true;
        }
    case T::Private:
    case T::Number:
    case T::String:
    case T::Regex:
    case T::Template:
    case T::Tail:
        return true;
    case T::Punct:
        switch (token.code) {
        case spell(")"):
        case spell("]"):
        case spell("}"):
        case spell("++"):
        case spell("--"):
            return true;
        default:
            return false;
        }
    default:
        return false;
    }
}

int precedence(const Token& token, bool noIn) noexcept
{
    if (token.type == T::Name) {
        static constexpr Spelling kInstanceof{"instanceof"};
        if ((token.code == kInstanceof.code && token.more == kInstanceof.more) || (token.code == spell("in") && !noIn)) {
            return 8;
        }
        return 0;
    }
    if (token.type != T::Punct) {
        return 0;
    }
    switch (token.code) {
    case spell("??"):
        return 1;
    case spell("||"):
        return 2;
    case spell("&&"):
        return 3;
    case spell("|"):
        return 4;
    case spell("^"):
        return 5;
    case spell("&"):
        return 6;
    case spell("=="):
    case spell("!="):
    case spell("==="):
    case spell("!=="):
        return 7;
    case spell("<"):
    case spell(">"):
    case spell("<="):
    case spell(">="):
        return 8;
    case spell("<<"):
    case spell(">>"):
    case spell(">>>"):
        return 9;
    case spell("+"):
    case spell("-"):
        return 10;
    case spell("*"):
    case spell("/"):
    case spell("%"):
        return 11;
    case spell("**"):
        return 12;
    default:
        return 0;
    }
}

bool assignmentOperator(const Token& token) noexcept
{
    if (token.type != T::Punct) {
        return false;
    }
    switch (token.code) {
    case spell("="):
    case spell("+="):
    case spell("-="):
    case spell("*="):
    case spell("/="):
    case spell("%="):
    case spell("**="):
    case spell("<<="):
    case spell(">>="):
    case spell(">>>="):
    case spell("&="):
    case spell("|="):
    case spell("^="):
    case spell("&&="):
    case spell("||="):
    case spell("??="):
        return true;
    default:
        return false;
    }
}

bool closesOperand(const Token& token) noexcept
{
    if (token.type == T::Eof) {
        return true;
    }
    if (token.type != T::Punct) {
        return false;
    }
    switch (token.code) {
    case spell(")"):
    case spell("]"):
    case spell("}"):
    case spell(","):
    case spell(";"):
    case spell(":"):
    case spell("="):
    case spell("."):
        return true;
    default:
        return false;
    }
}

class Parser
{
public:
    Parser(std::string_view source, Tree& tree) : m_source(source), m_tree(tree), m_lexer(source, tree.comments) {}

    void run()
    {
        Node* program = m_tree.make(Kind::Program, 0);
        m_tree.program = program;
        try {
            advance();
            while (m_token.type != T::Eof) {
                program->items.push_back(statement());
            }
        } catch (const Abort&) {
        }
        program->end = static_cast<std::uint32_t>(m_source.size());
    }

private:
    class Deeper
    {
    public:
        explicit Deeper(Parser& parser) : m_parser(parser)
        {
            if (++m_parser.m_depth > kMostDepth) {
                m_parser.fail("syntax error: the code is nested too deeply");
            }
        }
        ~Deeper() { --m_parser.m_depth; }
        Deeper(const Deeper&) = delete;
        Deeper& operator=(const Deeper&) = delete;

    private:
        Parser& m_parser;
    };

    [[noreturn]] void fail(std::string message)
    {
        m_tree.failures.push_back({m_token.start, std::move(message)});
        throw Abort{};
    }

    void advance()
    {
        m_previousEnd = m_token.end;
        m_token = m_lexer.next();
        if (m_token.type == T::Invalid) {
            fail("syntax error: " + std::string(m_token.text));
        }
    }

    [[nodiscard]] bool is(Spelling punct) const noexcept
    {
        return m_token.type == T::Punct && m_token.code == punct.code;
    }

    [[nodiscard]] bool word(Spelling name) const noexcept
    {
        return m_token.type == T::Name && m_token.code == name.code && m_token.more == name.more;
    }

    [[nodiscard]] bool isText(std::string_view punct) const noexcept
    {
        return m_token.type == T::Punct && m_token.text == punct;
    }

    void expect(std::string_view punct)
    {
        if (!isText(punct)) {
            fail("syntax error: expected '" + std::string(punct) + "'");
        }
        advance();
    }

    [[nodiscard]] Token peek()
    {
        const std::uint32_t saved = m_lexer.position();
        const Token next = m_lexer.next();
        m_lexer.seek(saved);
        return next;
    }

    Node* make(Kind kind, std::uint32_t start) { return m_tree.make(kind, start); }

    Node* close(Node* node) const
    {
        node->end = m_previousEnd;
        return node;
    }

    Node* leaf(Kind kind)
    {
        Node* node = make(kind, m_token.start);
        node->text = m_token.text;
        advance();
        return close(node);
    }

    void semicolon()
    {
        if (is(";")) {
            advance();
            return;
        }
        if (is("}") || m_token.type == T::Eof || m_token.newline) {
            return;
        }
        fail("syntax error: expected ';'");
    }

    Node* statement()
    {
        const Deeper deeper(*this);
        const std::uint32_t start = m_token.start;
        if (m_token.type == T::Punct) {
            if (is("{")) {
                return block();
            }
            if (is(";")) {
                Node* node = make(Kind::Empty, start);
                advance();
                return close(node);
            }
            if (is("@")) {
                decorators();
                return statement();
            }
        }
        if (m_token.type == T::Name) {
            if (Node* node = keywordStatement(start)) {
                return node;
            }
        }
        Node* node = make(Kind::ExpressionStatement, start);
        node->target = expression();
        semicolon();
        return close(node);
    }

    Node* keywordStatement(std::uint32_t start)
    {
        if (!statementWord(m_token.code)) {
            return reservedCode(m_token.code, m_token.more) ? nullptr : labeled(start);
        }
        const std::string_view w = m_token.text;
        if (w == "var" || w == "const") {
            const Token next = peek();
            if (w == "const" && next.type == T::Name && next.text == "enum") {
                return opaque(start);
            }
            Node* node = variables(start);
            semicolon();
            return close(node);
        }
        if (w == "let") {
            const Token next = peek();
            if ((next.type == T::Punct && (next.text == "[" || next.text == "{"))
                || (next.type == T::Name && next.text != "in" && next.text != "instanceof")) {
                Node* node = variables(start);
                semicolon();
                return close(node);
            }
            return nullptr;
        }
        if (w == "function") {
            return function(start, 0, true);
        }
        if (w == "async") {
            const Token next = peek();
            if (next.type == T::Name && next.text == "function" && !next.newline) {
                advance();
                return function(start, flag::kAsync, true);
            }
            return nullptr;
        }
        if (w == "class") {
            return classNode(start, true);
        }
        if (w == "if") {
            Node* node = make(Kind::If, start);
            advance();
            node->test = condition();
            node->body = statement();
            if (word("else")) {
                advance();
                node->other = statement();
            }
            return close(node);
        }
        if (w == "for") {
            return forStatement(start);
        }
        if (w == "while") {
            Node* node = make(Kind::While, start);
            advance();
            node->test = condition();
            node->body = statement();
            return close(node);
        }
        if (w == "do") {
            Node* node = make(Kind::DoWhile, start);
            advance();
            node->body = statement();
            if (!word("while")) {
                fail("syntax error: expected 'while'");
            }
            advance();
            node->test = condition();
            if (is(";")) {
                advance();
            }
            return close(node);
        }
        if (w == "return" || w == "throw") {
            Node* node = make(w == "return" ? Kind::Return : Kind::Throw, start);
            advance();
            if (!is(";") && !is("}") && m_token.type != T::Eof && !m_token.newline) {
                node->value = expression();
            } else if (node->kind == Kind::Throw) {
                fail("syntax error: expected an expression");
            }
            semicolon();
            return close(node);
        }
        if (w == "break" || w == "continue") {
            Node* node = make(w == "break" ? Kind::Break : Kind::Continue, start);
            advance();
            if (m_token.type == T::Name && !m_token.newline && !reserved(m_token.text)) {
                node->target = leaf(Kind::Identifier);
            }
            semicolon();
            return close(node);
        }
        if (w == "try") {
            return tryStatement(start);
        }
        if (w == "switch") {
            return switchStatement(start);
        }
        if (w == "debugger") {
            Node* node = make(Kind::Debugger, start);
            advance();
            semicolon();
            return close(node);
        }
        if (w == "import") {
            const Token next = peek();
            if (next.type == T::Punct && (next.text == "(" || next.text == ".")) {
                return nullptr;
            }
            return importDeclaration(start);
        }
        if (w == "export") {
            return exportDeclaration(start);
        }
        if (w == "enum") {
            return opaque(start);
        }
        if (w == "namespace" || w == "module") {
            const Token next = peek();
            if ((next.type == T::Name || next.type == T::String) && !next.newline) {
                return opaque(start);
            }
            return nullptr;
        }
        if (w == "with") {
            Node* node = make(Kind::Opaque, start);
            advance();
            static_cast<void>(condition());
            static_cast<void>(statement());
            return close(node);
        }
        if (!reserved(w)) {
            return labeled(start);
        }
        return nullptr;
    }

    Node* labeled(std::uint32_t start)
    {
        const Token next = peek();
        if (next.type == T::Punct && next.code == spell(":")) {
            Node* node = make(Kind::Labeled, start);
            node->target = leaf(Kind::Identifier);
            advance();
            node->body = statement();
            return close(node);
        }
        return nullptr;
    }

    Node* opaque(std::uint32_t start)
    {
        Node* node = make(Kind::Opaque, start);
        while (!is("{")) {
            if (m_token.type == T::Eof || is(";")) {
                fail("syntax error: expected '{'");
            }
            advance();
        }
        int depth = 0;
        do {
            if (is("{")) {
                ++depth;
            } else if (is("}")) {
                --depth;
            } else if (m_token.type == T::Eof) {
                fail("syntax error: expected '}'");
            }
            advance();
        } while (depth > 0);
        return close(node);
    }

    Node* block()
    {
        Node* node = make(Kind::Block, m_token.start);
        expect("{");
        const bool noIn = std::exchange(m_noIn, false);
        while (!is("}")) {
            if (m_token.type == T::Eof) {
                fail("syntax error: expected '}'");
            }
            node->items.push_back(statement());
        }
        m_noIn = noIn;
        advance();
        return close(node);
    }

    Node* condition()
    {
        expect("(");
        const bool noIn = std::exchange(m_noIn, false);
        Node* node = expression();
        m_noIn = noIn;
        expect(")");
        return node;
    }

    Node* variables(std::uint32_t start)
    {
        Node* node = make(Kind::VarDecl, start);
        node->text = m_token.text;
        advance();
        for (;;) {
            Node* declarator = make(Kind::Declarator, m_token.start);
            declarator->target = binding();
            if (is("=")) {
                advance();
                declarator->value = assignment();
            }
            node->items.push_back(close(declarator));
            if (!is(",")) {
                break;
            }
            advance();
        }
        return close(node);
    }

    Node* binding()
    {
        if (m_token.type == T::Name) {
            return leaf(Kind::Identifier);
        }
        if (is("[")) {
            Node* node = make(Kind::ArrayPattern, m_token.start);
            advance();
            while (!is("]")) {
                if (is(",")) {
                    node->items.push_back(leaf(Kind::Elision));
                    continue;
                }
                node->items.push_back(element());
                if (!is("]")) {
                    expect(",");
                }
            }
            advance();
            return close(node);
        }
        if (is("{")) {
            Node* node = make(Kind::ObjectPattern, m_token.start);
            advance();
            while (!is("}")) {
                if (is("...")) {
                    Node* rest = make(Kind::Rest, m_token.start);
                    advance();
                    rest->target = binding();
                    node->items.push_back(close(rest));
                } else {
                    Node* property = make(Kind::Property, m_token.start);
                    property->target = propertyKey(property);
                    if (is(":")) {
                        advance();
                        property->value = element();
                    } else {
                        property->flags |= flag::kShorthand;
                        property->value = property->target;
                        if (is("=")) {
                            Node* fallback = make(Kind::AssignPattern, property->target->start);
                            fallback->target = property->target;
                            advance();
                            fallback->value = assignment();
                            property->value = close(fallback);
                        }
                    }
                    node->items.push_back(close(property));
                }
                if (!is("}")) {
                    expect(",");
                }
            }
            advance();
            return close(node);
        }
        fail("syntax error: expected a name");
    }

    Node* element()
    {
        if (is("...")) {
            Node* rest = make(Kind::Rest, m_token.start);
            advance();
            rest->target = binding();
            return close(rest);
        }
        Node* target = binding();
        if (!is("=")) {
            return target;
        }
        Node* node = make(Kind::AssignPattern, target->start);
        node->target = target;
        advance();
        node->value = assignment();
        return close(node);
    }

    void parameters(Node* function)
    {
        expect("(");
        const bool noIn = std::exchange(m_noIn, false);
        while (!is(")")) {
            if (is("@")) {
                decorators();
            }
            function->items.push_back(element());
            if (!is(")")) {
                expect(",");
            }
        }
        m_noIn = noIn;
        advance();
    }

    Node* function(std::uint32_t start, std::uint32_t flags, bool declaration)
    {
        Node* node = make(Kind::Function, start);
        node->flags = flags | (declaration ? flag::kDeclaration : 0u);
        advance();
        if (is("*")) {
            node->flags |= flag::kGenerator;
            advance();
        }
        if (m_token.type == T::Name) {
            node->target = leaf(Kind::Identifier);
        }
        parameters(node);
        node->body = block();
        return close(node);
    }

    Node* classNode(std::uint32_t start, bool declaration)
    {
        Node* node = make(Kind::Class, start);
        node->flags = declaration ? flag::kDeclaration : 0u;
        advance();
        if (m_token.type == T::Name && m_token.text != "extends") {
            node->target = leaf(Kind::Identifier);
        }
        if (word("extends")) {
            advance();
            node->other = leftHandSide();
        }
        Node* body = make(Kind::Block, m_token.start);
        expect("{");
        const bool noIn = std::exchange(m_noIn, false);
        while (!is("}")) {
            if (m_token.type == T::Eof) {
                fail("syntax error: expected '}'");
            }
            if (is(";")) {
                advance();
                continue;
            }
            node->items.push_back(classMember());
        }
        m_noIn = noIn;
        advance();
        node->body = close(body);
        return close(node);
    }

    [[nodiscard]] static bool startsKey(const Token& token) noexcept
    {
        return token.type == T::Name || token.type == T::String || token.type == T::Number
            || token.type == T::Private || (token.type == T::Punct && (token.text == "[" || token.text == "*"));
    }

    Node* classMember()
    {
        const Deeper deeper(*this);
        while (is("@")) {
            decorators();
        }
        const std::uint32_t start = m_token.start;
        Node* node = make(Kind::ClassMember, start);
        if (word("static")) {
            const Token next = peek();
            if (next.type == T::Punct && next.text == "{") {
                Node* blockNode = make(Kind::StaticBlock, start);
                advance();
                blockNode->body = block();
                return close(blockNode);
            }
            if (startsKey(next)) {
                node->flags |= flag::kStatic;
                advance();
            }
        }
        if (word("async")) {
            const Token next = peek();
            if (!next.newline && startsKey(next)) {
                node->flags |= flag::kAsync;
                advance();
            }
        }
        if (is("*")) {
            node->flags |= flag::kGenerator;
            advance();
        }
        if (word("get") || word("set")) {
            const Token next = peek();
            if (startsKey(next) && !(next.type == T::Punct && next.text == "*")) {
                node->flags |= word("get") ? flag::kGetter : flag::kSetter;
                advance();
            }
        }
        node->target = propertyKey(node);
        if (node->target->kind == Kind::Identifier && node->target->text == "constructor") {
            node->flags |= flag::kConstructor;
        }
        if (is("(")) {
            node->flags |= flag::kMethod;
            Node* method = make(Kind::Function, m_token.start);
            method->flags = node->flags & (flag::kAsync | flag::kGenerator);
            parameters(method);
            method->body = block();
            node->value = close(method);
            return close(node);
        }
        node->flags |= flag::kField;
        if (is("=")) {
            advance();
            node->value = assignment();
        }
        semicolon();
        return close(node);
    }

    Node* propertyKey(Node* owner)
    {
        switch (m_token.type) {
        case T::Name:
            return leaf(Kind::Identifier);
        case T::String:
            return leaf(Kind::String);
        case T::Number:
            return leaf(Kind::Number);
        case T::Private:
            return leaf(Kind::PrivateName);
        default:
            break;
        }
        if (is("[")) {
            owner->flags |= flag::kComputed;
            advance();
            const bool noIn = std::exchange(m_noIn, false);
            Node* key = assignment();
            m_noIn = noIn;
            expect("]");
            return key;
        }
        fail("syntax error: expected a property name");
    }

    [[nodiscard]] bool declarationAhead()
    {
        if (word("var") || word("const")) {
            return true;
        }
        if (!word("let")) {
            return false;
        }
        const Token next = peek();
        return next.type == T::Name || (next.type == T::Punct && (next.text == "[" || next.text == "{"));
    }

    Node* forStatement(std::uint32_t start)
    {
        Node* node = make(Kind::For, start);
        advance();
        if (word("await")) {
            node->flags |= flag::kAwait;
            advance();
        }
        expect("(");
        if (!is(";")) {
            const bool noIn = std::exchange(m_noIn, true);
            if (declarationAhead()) {
                node->target = variables(m_token.start);
            } else {
                node->target = expression();
            }
            m_noIn = noIn;
        }
        if (word("of")) {
            node->kind = Kind::ForOf;
            advance();
            node->value = assignment();
        } else if (word("in")) {
            node->kind = Kind::ForIn;
            advance();
            node->value = expression();
        } else {
            expect(";");
            if (!is(";")) {
                node->test = expression();
            }
            expect(";");
            if (!is(")")) {
                node->other = expression();
            }
        }
        expect(")");
        node->body = statement();
        return close(node);
    }

    Node* tryStatement(std::uint32_t start)
    {
        Node* node = make(Kind::Try, start);
        advance();
        node->body = block();
        if (word("catch")) {
            Node* handler = make(Kind::Catch, m_token.start);
            advance();
            if (is("(")) {
                advance();
                handler->target = binding();
                expect(")");
            }
            handler->body = block();
            node->target = close(handler);
        }
        if (word("finally")) {
            advance();
            node->other = block();
        }
        if (node->target == nullptr && node->other == nullptr) {
            fail("syntax error: expected 'catch' or 'finally'");
        }
        return close(node);
    }

    Node* switchStatement(std::uint32_t start)
    {
        Node* node = make(Kind::Switch, start);
        advance();
        node->test = condition();
        expect("{");
        Node* current = nullptr;
        while (!is("}")) {
            if (word("case") || word("default")) {
                current = make(Kind::Case, m_token.start);
                const bool fallback = word("default");
                advance();
                if (!fallback) {
                    current->test = expression();
                }
                expect(":");
                close(current);
                node->items.push_back(current);
                continue;
            }
            if (m_token.type == T::Eof || current == nullptr) {
                fail("syntax error: expected 'case'");
            }
            current->items.push_back(statement());
            close(current);
        }
        advance();
        return close(node);
    }

    Node* moduleName()
    {
        if (m_token.type != T::String) {
            fail("syntax error: expected a module name");
        }
        Node* node = leaf(Kind::String);
        if ((word("with") || word("assert")) && !m_token.newline) {
            advance();
            static_cast<void>(objectLiteral());
        }
        return node;
    }

    Node* exportName()
    {
        if (m_token.type != T::Name && m_token.type != T::String) {
            fail("syntax error: expected a name");
        }
        return leaf(m_token.type == T::String ? Kind::String : Kind::Identifier);
    }

    void specifiers(Node* node)
    {
        expect("{");
        while (!is("}")) {
            Node* imported = exportName();
            if (word("as")) {
                advance();
                node->items.push_back(exportName());
            } else {
                node->items.push_back(imported);
            }
            if (!is("}")) {
                expect(",");
            }
        }
        advance();
    }

    Node* importDeclaration(std::uint32_t start)
    {
        Node* node = make(Kind::Import, start);
        advance();
        if (m_token.type == T::String) {
            node->value = moduleName();
            semicolon();
            return close(node);
        }
        if (m_token.type == T::Name) {
            node->items.push_back(leaf(Kind::Identifier));
            if (is(",")) {
                advance();
            }
        }
        if (is("*")) {
            advance();
            if (!word("as")) {
                fail("syntax error: expected 'as'");
            }
            advance();
            node->items.push_back(leaf(Kind::Identifier));
        } else if (is("{")) {
            specifiers(node);
        }
        if (!word("from")) {
            fail("syntax error: expected 'from'");
        }
        advance();
        node->value = moduleName();
        semicolon();
        return close(node);
    }

    Node* exportDeclaration(std::uint32_t start)
    {
        advance();
        if (word("default")) {
            Node* node = make(Kind::ExportDefault, start);
            advance();
            const std::uint32_t inner = m_token.start;
            if (word("function")) {
                node->target = function(inner, 0, true);
            } else if (word("async") && peek().type == T::Name && peek().text == "function" && !peek().newline) {
                advance();
                node->target = function(inner, flag::kAsync, true);
            } else if (word("class")) {
                node->target = classNode(inner, true);
            } else {
                node->target = assignment();
                semicolon();
            }
            return close(node);
        }
        Node* node = make(Kind::Export, start);
        if (is("*")) {
            advance();
            if (word("as")) {
                advance();
                node->items.push_back(exportName());
            }
            if (!word("from")) {
                fail("syntax error: expected 'from'");
            }
            advance();
            node->value = moduleName();
            semicolon();
            return close(node);
        }
        if (is("{")) {
            specifiers(node);
            if (word("from")) {
                advance();
                node->value = moduleName();
            }
            semicolon();
            return close(node);
        }
        node->target = statement();
        return close(node);
    }

    void decorators()
    {
        while (is("@")) {
            advance();
            if (is("(")) {
                advance();
                static_cast<void>(expression());
                expect(")");
            } else {
                static_cast<void>(leaf(Kind::Identifier));
                while (is(".")) {
                    advance();
                    static_cast<void>(leaf(Kind::Identifier));
                }
            }
            if (is("(")) {
                Node* ignored = make(Kind::Call, m_token.start);
                arguments(ignored);
            }
        }
    }

    Node* expression()
    {
        Node* first = assignment();
        if (!is(",")) {
            return first;
        }
        Node* node = make(Kind::Sequence, first->start);
        node->items.push_back(first);
        while (is(",")) {
            advance();
            node->items.push_back(assignment());
        }
        return close(node);
    }

    [[nodiscard]] Token afterParentheses()
    {
        const std::uint32_t saved = m_lexer.position();
        std::vector<char> closers{')'};
        Token previous = m_token;
        Token result{T::Invalid, 0, 0, false, {}};
        for (;;) {
            Token current = m_lexer.next();
            if (current.type == T::Eof || current.type == T::Invalid) {
                break;
            }
            if (current.type == T::Punct && (current.text == "/" || current.text == "/=") && !endsOperand(previous)) {
                current = m_lexer.regex(current);
                if (current.type == T::Invalid) {
                    break;
                }
            } else if (current.type == T::Head) {
                closers.push_back('`');
            } else if (current.type == T::Punct
                       && (current.text == "(" || current.text == "[" || current.text == "{")) {
                closers.push_back(current.text == "(" ? ')' : current.text == "[" ? ']' : '}');
            } else if (current.type == T::Punct && current.text == "}" && closers.back() == '`') {
                current = m_lexer.continuation(current);
                if (current.type == T::Invalid) {
                    break;
                }
                if (current.type == T::Tail) {
                    closers.pop_back();
                }
            } else if (current.type == T::Punct
                       && (current.text == ")" || current.text == "]" || current.text == "}")) {
                if (closers.back() != current.text.front()) {
                    break;
                }
                closers.pop_back();
                if (closers.empty()) {
                    result = m_lexer.next();
                    break;
                }
            }
            previous = current;
        }
        m_lexer.seek(saved);
        return result;
    }

    [[nodiscard]] static bool isArrowToken(const Token& token) noexcept
    {
        return token.type == T::Punct && token.code == spell("=>") && !token.newline;
    }

    [[nodiscard]] bool arrowAhead()
    {
        if (is("(")) {
            return isArrowToken(afterParentheses());
        }
        if (m_token.type != T::Name || reservedCode(m_token.code, m_token.more)) {
            return false;
        }
        const Token next = peek();
        if (isArrowToken(next)) {
            return true;
        }
        if (m_token.code != spell("async") || next.newline) {
            return false;
        }
        const std::uint32_t saved = m_lexer.position();
        const Token savedToken = m_token;
        bool result = false;
        if (next.type == T::Name && !reserved(next.text)) {
            static_cast<void>(m_lexer.next());
            result = isArrowToken(m_lexer.next());
        } else if (next.type == T::Punct && next.text == "(") {
            m_token = m_lexer.next();
            result = isArrowToken(afterParentheses());
        }
        m_lexer.seek(saved);
        m_token = savedToken;
        return result;
    }

    Node* arrow()
    {
        Node* node = make(Kind::Arrow, m_token.start);
        if (word("async") && !isArrowToken(peek())) {
            node->flags |= flag::kAsync;
            advance();
        }
        if (m_token.type == T::Name) {
            node->items.push_back(leaf(Kind::Identifier));
        } else {
            parameters(node);
        }
        expect("=>");
        if (is("{")) {
            node->body = block();
        } else {
            node->flags |= flag::kExpressionBody;
            node->body = assignment();
        }
        return close(node);
    }

    Node* assignment()
    {
        const Deeper deeper(*this);
        if (arrowAhead()) {
            return arrow();
        }
        if (word("yield")) {
            Node* node = make(Kind::Yield, m_token.start);
            advance();
            if (is("*") && !m_token.newline) {
                node->flags |= flag::kDelegate;
                advance();
                node->target = assignment();
            } else if (!m_token.newline && !closesOperand(m_token)) {
                node->target = assignment();
            }
            return close(node);
        }
        Node* left = conditional();
        if (!assignmentOperator(m_token)) {
            return left;
        }
        Node* node = make(Kind::Assign, left->start);
        node->text = m_token.text;
        node->target = left;
        advance();
        node->value = assignment();
        return close(node);
    }

    Node* conditional()
    {
        Node* test = binary(0);
        if (!is("?")) {
            return test;
        }
        Node* node = make(Kind::Conditional, test->start);
        node->test = test;
        advance();
        const bool noIn = std::exchange(m_noIn, false);
        node->body = assignment();
        m_noIn = noIn;
        expect(":");
        node->other = assignment();
        return close(node);
    }

    Node* binary(int floor)
    {
        Node* left = unary();
        for (;;) {
            const int level = precedence(m_token, m_noIn);
            if (level == 0 || level <= floor) {
                return left;
            }
            const std::string_view op = m_token.text;
            advance();
            Node* right = op == "**" ? binary(level - 1) : binary(level);
            Node* node = make(op == "&&" || op == "||" || op == "??" ? Kind::Logical : Kind::Binary, left->start);
            node->text = op;
            node->target = left;
            node->value = right;
            left = close(node);
        }
    }

    Node* unary()
    {
        const Deeper deeper(*this);
        const std::uint32_t start = m_token.start;
        if (is("!") || is("~") || is("+") || is("-") || is("++") || is("--")) {
            Node* node = make(is("++") || is("--") ? Kind::Update : Kind::Unary, start);
            node->text = m_token.text;
            node->flags |= flag::kPrefix;
            advance();
            node->target = unary();
            return close(node);
        }
        if (word("typeof") || word("void") || word("delete") || (word("await") && !closesOperand(peek()))) {
            Node* node = make(word("await") ? Kind::Await : Kind::Unary, start);
            node->text = m_token.text;
            advance();
            node->target = unary();
            return close(node);
        }
        Node* operand = leftHandSide();
        if ((is("++") || is("--")) && !m_token.newline) {
            Node* node = make(Kind::Update, operand->start);
            node->text = m_token.text;
            node->target = operand;
            advance();
            return close(node);
        }
        return operand;
    }

    void arguments(Node* call)
    {
        expect("(");
        const bool noIn = std::exchange(m_noIn, false);
        while (!is(")")) {
            if (is("...")) {
                Node* spread = make(Kind::Spread, m_token.start);
                advance();
                spread->target = assignment();
                call->items.push_back(close(spread));
            } else {
                call->items.push_back(assignment());
            }
            if (!is(")")) {
                expect(",");
            }
        }
        m_noIn = noIn;
        advance();
    }

    Node* leftHandSide()
    {
        Node* node = word("new") ? newExpression() : primary();
        return tail(node, true);
    }

    Node* newExpression()
    {
        const std::uint32_t start = m_token.start;
        advance();
        if (is(".")) {
            advance();
            Node* meta = make(Kind::MetaProperty, start);
            meta->text = m_source.substr(start, m_token.end - start);
            advance();
            return close(meta);
        }
        Node* node = make(Kind::New, start);
        Node* callee = word("new") ? newExpression() : primary();
        node->target = tail(callee, false);
        if (is("(")) {
            arguments(node);
        }
        return close(node);
    }

    Node* memberName(Node* object, std::uint32_t flags)
    {
        if (m_token.type != T::Name && m_token.type != T::Private) {
            fail("syntax error: expected a property name");
        }
        Node* member = make(Kind::Member, object->start);
        member->flags |= flags;
        member->target = object;
        member->value = leaf(m_token.type == T::Private ? Kind::PrivateName : Kind::Identifier);
        return close(member);
    }

    Node* computedMember(Node* object, std::uint32_t flags)
    {
        Node* member = make(Kind::Member, object->start);
        member->flags |= flags | flag::kComputed;
        member->target = object;
        advance();
        const bool noIn = std::exchange(m_noIn, false);
        member->value = expression();
        m_noIn = noIn;
        expect("]");
        return close(member);
    }

    Node* tail(Node* node, bool calls)
    {
        for (;;) {
            if (is(".")) {
                advance();
                node = memberName(node, 0);
                continue;
            }
            if (is("?.")) {
                advance();
                if (is("(")) {
                    Node* call = make(Kind::Call, node->start);
                    call->flags |= flag::kOptional;
                    call->target = node;
                    arguments(call);
                    node = close(call);
                } else if (is("[")) {
                    node = computedMember(node, flag::kOptional);
                } else {
                    node = memberName(node, flag::kOptional);
                }
                continue;
            }
            if (is("[")) {
                node = computedMember(node, 0);
                continue;
            }
            if (is("(") && calls) {
                Node* call = make(Kind::Call, node->start);
                call->target = node;
                arguments(call);
                node = close(call);
                continue;
            }
            if (m_token.type == T::Template || m_token.type == T::Head) {
                Node* tagged = make(Kind::TaggedTemplate, node->start);
                tagged->target = node;
                tagged->value = templateLiteral();
                node = close(tagged);
                continue;
            }
            return node;
        }
    }

    Node* templateLiteral()
    {
        Node* node = make(Kind::Template, m_token.start);
        if (m_token.type == T::Template) {
            advance();
            return close(node);
        }
        advance();
        const bool noIn = std::exchange(m_noIn, false);
        for (;;) {
            node->items.push_back(expression());
            if (!is("}")) {
                fail("syntax error: expected '}'");
            }
            m_token = m_lexer.continuation(m_token);
            if (m_token.type == T::Invalid) {
                fail("syntax error: unterminated template literal");
            }
            const bool done = m_token.type == T::Tail;
            advance();
            if (done) {
                break;
            }
        }
        m_noIn = noIn;
        return close(node);
    }

    Node* namePrimary(std::uint32_t start)
    {
        if (m_token.code != spell("async") && !reservedCode(m_token.code, m_token.more)) {
            return leaf(Kind::Identifier);
        }
        const std::string_view w = m_token.text;
        if (w == "function") {
            return function(start, 0, false);
        }
        if (w == "async") {
            const Token next = peek();
            if (next.type == T::Name && next.text == "function" && !next.newline) {
                advance();
                return function(start, flag::kAsync, false);
            }
        }
        if (w == "class") {
            return classNode(start, false);
        }
        if (w == "this") {
            return leaf(Kind::This);
        }
        if (w == "super") {
            return leaf(Kind::Super);
        }
        if (w == "null") {
            return leaf(Kind::Null);
        }
        if (w == "true") {
            return leaf(Kind::True);
        }
        if (w == "false") {
            return leaf(Kind::False);
        }
        if (w == "import") {
            const Token next = peek();
            if (next.type == T::Punct && next.text == ".") {
                advance();
                advance();
                Node* meta = make(Kind::MetaProperty, start);
                meta->text = m_source.substr(start, m_token.end - start);
                advance();
                return close(meta);
            }
            Node* node = make(Kind::ImportCall, start);
            advance();
            arguments(node);
            return close(node);
        }
        if (reserved(w)) {
            fail("syntax error: unexpected '" + std::string(w) + "'");
        }
        return leaf(Kind::Identifier);
    }

    Node* primary()
    {
        const std::uint32_t start = m_token.start;
        switch (m_token.type) {
        case T::Name:
            return namePrimary(start);
        case T::Private:
            return leaf(Kind::PrivateName);
        case T::Number:
            return leaf(Kind::Number);
        case T::String:
            return leaf(Kind::String);
        case T::Template:
        case T::Head:
            return templateLiteral();
        case T::Punct:
            break;
        default:
            fail("syntax error: expected an expression");
        }
        if (is("/") || is("/=")) {
            m_token = m_lexer.regex(m_token);
            if (m_token.type == T::Invalid) {
                fail("syntax error: unterminated regular expression");
            }
            return leaf(Kind::Regex);
        }
        if (is("(")) {
            Node* node = make(Kind::Paren, start);
            advance();
            const bool noIn = std::exchange(m_noIn, false);
            node->target = expression();
            m_noIn = noIn;
            expect(")");
            return close(node);
        }
        if (is("[")) {
            return arrayLiteral(start);
        }
        if (is("{")) {
            return objectLiteral();
        }
        if (is("@")) {
            decorators();
            if (!word("class")) {
                fail("syntax error: expected 'class'");
            }
            return classNode(m_token.start, false);
        }
        fail("syntax error: expected an expression");
    }

    Node* arrayLiteral(std::uint32_t start)
    {
        Node* node = make(Kind::Array, start);
        advance();
        const bool noIn = std::exchange(m_noIn, false);
        while (!is("]")) {
            if (is(",")) {
                node->items.push_back(leaf(Kind::Elision));
                continue;
            }
            if (is("...")) {
                Node* spread = make(Kind::Spread, m_token.start);
                advance();
                spread->target = assignment();
                node->items.push_back(close(spread));
            } else {
                node->items.push_back(assignment());
            }
            if (!is("]")) {
                expect(",");
            }
        }
        m_noIn = noIn;
        advance();
        return close(node);
    }

    Node* objectLiteral()
    {
        Node* node = make(Kind::Object, m_token.start);
        expect("{");
        const bool noIn = std::exchange(m_noIn, false);
        while (!is("}")) {
            Node* property = make(Kind::Property, m_token.start);
            if (is("...")) {
                property->flags |= flag::kSpread;
                advance();
                property->value = assignment();
            } else {
                objectMember(property);
            }
            node->items.push_back(close(property));
            if (!is("}")) {
                expect(",");
            }
        }
        m_noIn = noIn;
        advance();
        return close(node);
    }

    void objectMember(Node* property)
    {
        if (word("async")) {
            const Token next = peek();
            if (!next.newline && startsKey(next)) {
                property->flags |= flag::kAsync;
                advance();
            }
        }
        if (is("*")) {
            property->flags |= flag::kGenerator;
            advance();
        }
        if ((word("get") || word("set")) && startsKey(peek())) {
            property->flags |= word("get") ? flag::kGetter : flag::kSetter;
            advance();
        }
        property->target = propertyKey(property);
        if (is("(")) {
            property->flags |= flag::kMethod;
            Node* method = make(Kind::Function, m_token.start);
            method->flags = property->flags & (flag::kAsync | flag::kGenerator);
            parameters(method);
            method->body = block();
            property->value = close(method);
        } else if (is(":")) {
            advance();
            property->value = assignment();
        } else {
            property->flags |= flag::kShorthand;
            property->value = property->target;
            if (is("=")) {
                Node* fallback = make(Kind::AssignPattern, property->target->start);
                fallback->target = property->target;
                advance();
                fallback->value = assignment();
                property->value = close(fallback);
            }
        }
    }

    std::string_view m_source;
    Tree& m_tree;
    Lexer m_lexer;
    Token m_token;
    std::uint32_t m_previousEnd = 0;
    int m_depth = 0;
    bool m_noIn = false;
};

}

Tree parse(std::string_view javascript)
{
    Tree tree;
    Parser parser(javascript, tree);
    parser.run();
    return tree;
}

bool isFunction(const Node* node) noexcept
{
    return node != nullptr && (node->kind == Kind::Function || node->kind == Kind::Arrow);
}

const Node* unparen(const Node* node) noexcept
{
    while (node != nullptr && node->kind == Kind::Paren) {
        node = node->target;
    }
    return node;
}

}
