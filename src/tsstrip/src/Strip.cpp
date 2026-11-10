#include <gs/tsstrip/Strip.h>

#include "Scanner.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::tsstrip {

namespace {

using detail::Keyword;
using detail::Scanner;
using detail::Token;
using detail::TokenType;

constexpr int kMaxDepth = 200;
constexpr std::uint32_t kNoStatement = std::numeric_limits<std::uint32_t>::max();

struct Abort {
};

struct Span {
    std::uint32_t start = 0;
    std::uint32_t end = 0;
};

struct Patch {
    std::uint32_t position = 0;
    char text = ' ';
};

struct Issue {
    std::uint32_t position = 0;
    ProblemCode code = ProblemCode::Syntax;
    std::string message;
};

class Flag
{
public:
    Flag(bool& target, bool value) noexcept : m_target(target), m_saved(std::exchange(target, value)) {}
    ~Flag() { m_target = m_saved; }
    Flag(const Flag&) = delete;
    Flag& operator=(const Flag&) = delete;

private:
    bool& m_target;
    bool m_saved;
};

std::string_view messageOf(ProblemCode code) noexcept
{
    switch (code) {
    case ProblemCode::Enum:
        return "enum is not erasable; use a const object instead";
    case ProblemCode::ConstEnum:
        return "const enum is not erasable; use a const object instead";
    case ProblemCode::Namespace:
        return "a namespace with runtime code is not erasable; use a module or a plain object instead";
    case ProblemCode::ParameterProperty:
        return "parameter properties are not erasable; declare the field in the class body and assign it in the "
               "constructor";
    case ProblemCode::ImportRequire:
        return "import = require() is not erasable; use import ... from instead";
    case ProblemCode::ImportAlias:
        return "an import alias is not erasable; use const instead";
    case ProblemCode::ExportAssignment:
        return "export = is not erasable; use export default instead";
    case ProblemCode::Decorator:
        return "decorators are not supported; call the function on the class or member instead";
    case ProblemCode::Jsx:
        return "JSX is not supported in scripts";
    case ProblemCode::UnsafeAssertion:
        return "a line break follows this <T> assertion, so erasing it would change the meaning; use 'as T' instead";
    case ProblemCode::Syntax:
        break;
    }
    return "syntax error";
}

bool isBinaryOperator(TokenType type) noexcept
{
    switch (type) {
    case TokenType::QuestionQuestion:
    case TokenType::BarBar:
    case TokenType::AmpAmp:
    case TokenType::Bar:
    case TokenType::Caret:
    case TokenType::Amp:
    case TokenType::Equal:
    case TokenType::NotEqual:
    case TokenType::StrictEqual:
    case TokenType::StrictNotEqual:
    case TokenType::Less:
    case TokenType::Greater:
    case TokenType::LessEqual:
    case TokenType::GreaterEqual:
    case TokenType::LessLess:
    case TokenType::GreaterGreater:
    case TokenType::GreaterGreaterGreater:
    case TokenType::Plus:
    case TokenType::Minus:
    case TokenType::Star:
    case TokenType::StarStar:
    case TokenType::Slash:
    case TokenType::Percent:
        return true;
    default:
        return false;
    }
}

bool isAssignmentOperator(TokenType type) noexcept
{
    switch (type) {
    case TokenType::Assign:
    case TokenType::PlusAssign:
    case TokenType::MinusAssign:
    case TokenType::StarAssign:
    case TokenType::StarStarAssign:
    case TokenType::SlashAssign:
    case TokenType::PercentAssign:
    case TokenType::LessLessAssign:
    case TokenType::GreaterGreaterAssign:
    case TokenType::GreaterGreaterGreaterAssign:
    case TokenType::AmpAssign:
    case TokenType::AmpAmpAssign:
    case TokenType::BarAssign:
    case TokenType::BarBarAssign:
    case TokenType::CaretAssign:
    case TokenType::QuestionQuestionAssign:
        return true;
    default:
        return false;
    }
}

bool isBinaryKeyword(const Token& token) noexcept
{
    return token.type == TokenType::Name
        && (token.keyword == Keyword::In || token.keyword == Keyword::Instanceof || token.keyword == Keyword::As
            || token.keyword == Keyword::Satisfies);
}

bool startsExpression(const Token& token) noexcept
{
    switch (token.type) {
    case TokenType::Name:
        return !isBinaryKeyword(token);
    case TokenType::PrivateName:
    case TokenType::String:
    case TokenType::Number:
    case TokenType::Regex:
    case TokenType::Template:
    case TokenType::TemplateHead:
    case TokenType::Slash:
    case TokenType::SlashAssign:
    case TokenType::LeftParen:
    case TokenType::LeftBracket:
    case TokenType::LeftBrace:
    case TokenType::Plus:
    case TokenType::Minus:
    case TokenType::PlusPlus:
    case TokenType::MinusMinus:
    case TokenType::Bang:
    case TokenType::Tilde:
    case TokenType::Less:
    case TokenType::At:
        return true;
    default:
        return false;
    }
}

bool canFollowTypeArguments(const Token& token) noexcept
{
    switch (token.type) {
    case TokenType::LeftParen:
    case TokenType::Template:
    case TokenType::TemplateHead:
        return true;
    case TokenType::Less:
    case TokenType::Greater:
    case TokenType::Plus:
    case TokenType::Minus:
        return false;
    default:
        break;
    }
    return token.newlineBefore || isBinaryOperator(token.type) || isBinaryKeyword(token) || !startsExpression(token);
}

bool continuesStatement(TokenType type) noexcept
{
    switch (type) {
    case TokenType::LeftParen:
    case TokenType::LeftBracket:
    case TokenType::Template:
    case TokenType::TemplateHead:
    case TokenType::Plus:
    case TokenType::Minus:
    case TokenType::Slash:
    case TokenType::SlashAssign:
        return true;
    default:
        return false;
    }
}

bool startsPropertyName(const Token& token) noexcept
{
    switch (token.type) {
    case TokenType::Name:
    case TokenType::String:
    case TokenType::Number:
    case TokenType::PrivateName:
    case TokenType::LeftBracket:
        return true;
    default:
        return false;
    }
}

bool startsType(const Token& token) noexcept
{
    switch (token.type) {
    case TokenType::Name:
    case TokenType::String:
    case TokenType::Number:
    case TokenType::Minus:
    case TokenType::LeftBrace:
    case TokenType::LeftBracket:
    case TokenType::LeftParen:
    case TokenType::Less:
    case TokenType::Template:
    case TokenType::TemplateHead:
        return true;
    default:
        return false;
    }
}

bool canFollowModifier(const Token& next, bool newlineAllowed) noexcept
{
    if (next.newlineBefore && !newlineAllowed) {
        return false;
    }
    switch (next.type) {
    case TokenType::Name:
    case TokenType::String:
    case TokenType::Number:
    case TokenType::PrivateName:
    case TokenType::LeftBracket:
    case TokenType::LeftBrace:
    case TokenType::Star:
        return true;
    default:
        return false;
    }
}

bool endsOperand(const Token& token) noexcept
{
    switch (token.type) {
    case TokenType::Name:
        switch (token.keyword) {
        case Keyword::Return:
        case Keyword::Typeof:
        case Keyword::Instanceof:
        case Keyword::In:
        case Keyword::Of:
        case Keyword::New:
        case Keyword::Delete:
        case Keyword::Void:
        case Keyword::Throw:
        case Keyword::Case:
        case Keyword::Do:
        case Keyword::Else:
        case Keyword::Yield:
        case Keyword::Await:
            return false;
        default:
            return true;
        }
    case TokenType::PrivateName:
    case TokenType::Number:
    case TokenType::String:
    case TokenType::Regex:
    case TokenType::Template:
    case TokenType::TemplateTail:
    case TokenType::RightParen:
    case TokenType::RightBracket:
    case TokenType::RightBrace:
    case TokenType::PlusPlus:
    case TokenType::MinusMinus:
        return true;
    default:
        return false;
    }
}

class Parser
{
public:
    Parser(std::string_view source, Offsets offsets) noexcept
        : m_source(source), m_scanner(source), m_offsets(offsets)
    {
    }

    Result run()
    {
        try {
            advance();
            while (!at(TokenType::Eof)) {
                parseStatement();
            }
        } catch (const Abort&) {
        }
        return build();
    }

private:
    struct Mark {
        Scanner::State state;
        std::uint32_t previousEnd = 0;
        Keyword previousKeyword = Keyword::None;
        std::size_t spans = 0;
        std::size_t patches = 0;
        std::size_t issues = 0;
    };

    class Deeper
    {
    public:
        explicit Deeper(Parser& parser) : m_parser(parser)
        {
            if (++m_parser.m_depth > kMaxDepth) {
                --m_parser.m_depth;
                m_parser.fail(m_parser.token().start, "syntax error: the code is nested too deeply");
            }
        }
        ~Deeper() { --m_parser.m_depth; }
        Deeper(const Deeper&) = delete;
        Deeper& operator=(const Deeper&) = delete;

    private:
        Parser& m_parser;
    };

    [[nodiscard]] const Token& token() const noexcept { return m_scanner.token(); }
    [[nodiscard]] bool at(TokenType type) const noexcept { return m_scanner.token().type == type; }
    [[nodiscard]] bool atKeyword(Keyword keyword) const noexcept
    {
        return m_scanner.token().type == TokenType::Name && m_scanner.token().keyword == keyword;
    }
    [[nodiscard]] bool atText(std::string_view text) const noexcept
    {
        return m_scanner.token().type == TokenType::Name && m_scanner.text(m_scanner.token()) == text;
    }
    [[nodiscard]] char byteAt(std::uint32_t position) const noexcept
    {
        return position < m_source.size() ? m_source[position] : '\0';
    }

    void advance()
    {
        m_previousEnd = m_scanner.token().end;
        m_previousKeyword =
            m_scanner.token().type == TokenType::Name ? m_scanner.token().keyword : Keyword::None;
        m_scanner.next();
        checkLexical();
    }

    void checkLexical()
    {
        if (at(TokenType::Invalid)) {
            fail(token().start, std::string(detail::lexErrorMessage(token().error)));
        }
    }

    [[noreturn]] void fail(std::uint32_t position, std::string message)
    {
        if (!m_speculating) {
            m_issues.push_back({position, ProblemCode::Syntax, std::move(message)});
        }
        throw Abort{};
    }

    void expect(TokenType type, std::string_view what)
    {
        if (!at(type)) {
            fail(token().start, "syntax error: expected '" + std::string(what) + "'");
        }
        advance();
    }

    void expectKeyword(Keyword keyword, std::string_view what)
    {
        if (!atKeyword(keyword)) {
            fail(token().start, "syntax error: expected '" + std::string(what) + "'");
        }
        advance();
    }

    void expectName()
    {
        if (!at(TokenType::Name)) {
            fail(token().start, "syntax error: expected a name");
        }
        advance();
    }

    [[nodiscard]] Token peek(int count = 1)
    {
        const Scanner::State state = m_scanner.save();
        for (int i = 0; i < count; ++i) {
            m_scanner.next();
        }
        const Token result = m_scanner.token();
        m_scanner.restore(state);
        return result;
    }

    void erase(std::uint32_t start, std::uint32_t end)
    {
        if (end > start) {
            m_spans.push_back({start, end});
        }
    }

    void eraseToken()
    {
        erase(token().start, token().end);
        advance();
    }

    void patch(std::uint32_t position, char text) { m_patches.push_back({position, text}); }

    void issue(ProblemCode code, std::uint32_t position)
    {
        m_issues.push_back({position, code, std::string(messageOf(code))});
    }

    [[nodiscard]] Mark mark() const noexcept
    {
        return {m_scanner.save(), m_previousEnd, m_previousKeyword, m_spans.size(), m_patches.size(), m_issues.size()};
    }

    void reset(const Mark& to)
    {
        m_scanner.restore(to.state);
        m_previousEnd = to.previousEnd;
        m_previousKeyword = to.previousKeyword;
        m_spans.resize(to.spans);
        m_patches.resize(to.patches);
        m_issues.resize(to.issues);
    }

    template <typename Body>
    bool attempt(Body&& body)
    {
        const Mark from = mark();
        const Flag speculating(m_speculating, true);
        try {
            body();
            return true;
        } catch (const Abort&) {
            reset(from);
            return false;
        }
    }

    void eraseWhole(const Mark& from, std::uint32_t start)
    {
        m_spans.resize(from.spans);
        m_patches.resize(from.patches);
        erase(start, m_previousEnd);
        if (m_previousEnd > start) {
            patch(m_previousEnd - 1, ';');
        }
        m_erasedStatement = start;
    }

    [[nodiscard]] bool lineBreakBetween(std::uint32_t from, std::uint32_t to) const noexcept
    {
        for (std::uint32_t i = from; i < to && i < m_source.size(); ++i) {
            const auto c = static_cast<std::uint8_t>(m_source[i]);
            if (c == '\n' || c == '\r') {
                return true;
            }
            if (c == 0xE2 && byteAt(i + 1) == '\x80' && (byteAt(i + 2) == '\xA8' || byteAt(i + 2) == '\xA9')) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool nextIsKeywordOnSameLine(Keyword keyword)
    {
        const Token next = peek();
        return next.type == TokenType::Name && next.keyword == keyword && !next.newlineBefore;
    }

    [[nodiscard]] bool nextIsNameOnSameLine()
    {
        const Token next = peek();
        return next.type == TokenType::Name && !next.newlineBefore && !detail::isReserved(next.keyword);
    }

    [[nodiscard]] bool nextIsNamespaceName()
    {
        const Token next = peek();
        return !next.newlineBefore
            && ((next.type == TokenType::Name && !detail::isReserved(next.keyword)) || next.type == TokenType::String);
    }

    [[nodiscard]] bool nextStartsDeclaration()
    {
        const Token next = peek();
        if (next.type != TokenType::Name || next.newlineBefore) {
            return false;
        }
        switch (next.keyword) {
        case Keyword::Var:
        case Keyword::Let:
        case Keyword::Const:
        case Keyword::Function:
        case Keyword::Class:
        case Keyword::Enum:
        case Keyword::Namespace:
        case Keyword::Module:
        case Keyword::Global:
        case Keyword::Abstract:
        case Keyword::Interface:
        case Keyword::Type:
        case Keyword::Async:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool letStartsDeclaration()
    {
        const Token next = peek();
        return next.type == TokenType::LeftBracket || next.type == TokenType::LeftBrace
            || (next.type == TokenType::Name && next.keyword != Keyword::In && next.keyword != Keyword::Instanceof);
    }

    void consumeSemicolon()
    {
        if (at(TokenType::Semicolon)) {
            advance();
            return;
        }
        if (at(TokenType::RightBrace) || at(TokenType::Eof)) {
            return;
        }
        if (!token().newlineBefore) {
            fail(token().start, "syntax error: expected ';'");
        }
        if (!m_spans.empty() && m_spans.back().end == m_previousEnd && continuesStatement(token().type)) {
            patch(m_previousEnd - 1, ';');
        }
    }

    void parseStatement()
    {
        const Mark from = mark();
        parseStatementAt(token().start, from);
    }

    void parseStatementAt(std::uint32_t start, const Mark& from)
    {
        const Deeper deeper(*this);
        switch (token().type) {
        case TokenType::LeftBrace:
            parseBlock();
            return;
        case TokenType::Semicolon:
            advance();
            return;
        case TokenType::At:
            parseDecorators();
            return;
        case TokenType::Name:
            break;
        default:
            parseExpressionStatement();
            return;
        }
        switch (token().keyword) {
        case Keyword::Var:
            advance();
            parseVariableDeclarations();
            consumeSemicolon();
            return;
        case Keyword::Const:
            if (nextIsKeywordOnSameLine(Keyword::Enum)) {
                parseEnum(true);
                return;
            }
            advance();
            parseVariableDeclarations();
            consumeSemicolon();
            return;
        case Keyword::Let:
            if (letStartsDeclaration()) {
                advance();
                parseVariableDeclarations();
                consumeSemicolon();
                return;
            }
            break;
        case Keyword::Function:
            parseFunctionDeclaration(start, from);
            return;
        case Keyword::Async:
            if (nextIsKeywordOnSameLine(Keyword::Function)) {
                parseFunctionDeclaration(start, from);
                return;
            }
            break;
        case Keyword::Class:
            parseClass();
            return;
        case Keyword::Abstract:
            if (nextIsKeywordOnSameLine(Keyword::Class)) {
                eraseToken();
                parseClass();
                return;
            }
            break;
        case Keyword::If:
            advance();
            parseCondition();
            parseStatement();
            if (atKeyword(Keyword::Else)) {
                advance();
                parseStatement();
            }
            return;
        case Keyword::For:
            parseFor();
            return;
        case Keyword::While:
            advance();
            parseCondition();
            parseStatement();
            return;
        case Keyword::Do:
            advance();
            parseStatement();
            expectKeyword(Keyword::While, "while");
            parseCondition();
            if (at(TokenType::Semicolon)) {
                advance();
            }
            return;
        case Keyword::Switch:
            parseSwitch();
            return;
        case Keyword::Try:
            parseTry();
            return;
        case Keyword::Return:
            advance();
            if (!at(TokenType::Semicolon) && !at(TokenType::RightBrace) && !at(TokenType::Eof)
                && !token().newlineBefore) {
                parseExpression();
            }
            consumeSemicolon();
            return;
        case Keyword::Throw:
            advance();
            parseExpression();
            consumeSemicolon();
            return;
        case Keyword::Break:
        case Keyword::Continue:
            advance();
            if (at(TokenType::Name) && !token().newlineBefore && !detail::isReserved(token().keyword)) {
                advance();
            }
            consumeSemicolon();
            return;
        case Keyword::Debugger:
            advance();
            consumeSemicolon();
            return;
        case Keyword::Import: {
            const Token next = peek();
            if (next.type == TokenType::LeftParen || next.type == TokenType::Dot) {
                break;
            }
            parseImport(start, from);
            return;
        }
        case Keyword::Export:
            parseExport(start, from);
            return;
        case Keyword::Interface:
            if (nextIsNameOnSameLine()) {
                parseInterface(start, from);
                return;
            }
            break;
        case Keyword::Type:
            if (nextIsNameOnSameLine()) {
                parseTypeAlias(start, from);
                return;
            }
            break;
        case Keyword::Declare:
            if (nextStartsDeclaration()) {
                parseDeclare(start, from);
                return;
            }
            break;
        case Keyword::Enum:
            parseEnum(false);
            return;
        case Keyword::Namespace:
        case Keyword::Module:
            if (nextIsNamespaceName()) {
                parseNamespace(start, from);
                return;
            }
            break;
        case Keyword::Global:
            if (m_ambient && peek().type == TokenType::LeftBrace) {
                parseNamespace(start, from);
                return;
            }
            break;
        default:
            break;
        }
        if (!detail::isReserved(token().keyword) && peek().type == TokenType::Colon) {
            advance();
            advance();
            parseStatement();
            return;
        }
        parseExpressionStatement();
    }

    void parseExpressionStatement()
    {
        parseExpression();
        consumeSemicolon();
    }

    void parseBlock()
    {
        expect(TokenType::LeftBrace, "{");
        const Flag noIn(m_noIn, false);
        while (!at(TokenType::RightBrace)) {
            if (at(TokenType::Eof)) {
                fail(token().start, "syntax error: expected '}'");
            }
            parseStatement();
        }
        advance();
    }

    void parseCondition()
    {
        expect(TokenType::LeftParen, "(");
        {
            const Flag noIn(m_noIn, false);
            parseExpression();
        }
        expect(TokenType::RightParen, ")");
    }

    void parseFor()
    {
        advance();
        if (atKeyword(Keyword::Await)) {
            advance();
        }
        expect(TokenType::LeftParen, "(");
        if (!at(TokenType::Semicolon)) {
            const Flag noIn(m_noIn, true);
            if (atKeyword(Keyword::Var) || atKeyword(Keyword::Const)
                || (atKeyword(Keyword::Let) && letStartsDeclaration())) {
                advance();
                parseVariableDeclarations();
            } else {
                parseExpression();
            }
        }
        if (atKeyword(Keyword::Of)) {
            advance();
            parseAssignment();
        } else if (atKeyword(Keyword::In)) {
            advance();
            parseExpression();
        } else {
            expect(TokenType::Semicolon, ";");
            if (!at(TokenType::Semicolon)) {
                parseExpression();
            }
            expect(TokenType::Semicolon, ";");
            if (!at(TokenType::RightParen)) {
                parseExpression();
            }
        }
        expect(TokenType::RightParen, ")");
        parseStatement();
    }

    void parseSwitch()
    {
        advance();
        parseCondition();
        expect(TokenType::LeftBrace, "{");
        while (!at(TokenType::RightBrace)) {
            if (atKeyword(Keyword::Case)) {
                advance();
                parseExpression();
                expect(TokenType::Colon, ":");
            } else if (atKeyword(Keyword::Default)) {
                advance();
                expect(TokenType::Colon, ":");
            } else if (at(TokenType::Eof)) {
                fail(token().start, "syntax error: expected '}'");
            } else {
                parseStatement();
            }
        }
        advance();
    }

    void parseTry()
    {
        advance();
        parseBlock();
        if (atKeyword(Keyword::Catch)) {
            advance();
            if (at(TokenType::LeftParen)) {
                advance();
                parseBindingTarget();
                if (at(TokenType::Colon)) {
                    eraseTypeAnnotation();
                }
                expect(TokenType::RightParen, ")");
            }
            parseBlock();
        }
        if (atKeyword(Keyword::Finally)) {
            advance();
            parseBlock();
        }
    }

    void parseVariableDeclarations()
    {
        for (;;) {
            parseBindingTarget();
            if (at(TokenType::Bang) && !token().newlineBefore) {
                eraseToken();
            }
            if (at(TokenType::Colon)) {
                eraseTypeAnnotation();
            }
            if (at(TokenType::Assign)) {
                advance();
                parseAssignment();
            }
            if (!at(TokenType::Comma)) {
                return;
            }
            advance();
        }
    }

    void eraseTypeAnnotation()
    {
        const std::uint32_t start = token().start;
        advance();
        parseType();
        erase(start, m_previousEnd);
    }

    void eraseReturnType()
    {
        const std::uint32_t start = token().start;
        advance();
        parseReturnType();
        erase(start, m_previousEnd);
    }

    void parseBindingTarget()
    {
        switch (token().type) {
        case TokenType::Name:
            advance();
            return;
        case TokenType::LeftBracket:
            parseArrayBinding();
            return;
        case TokenType::LeftBrace:
            parseObjectBinding();
            return;
        default:
            fail(token().start, "syntax error: expected a name");
        }
    }

    void parseBindingElement()
    {
        parseBindingTarget();
        if (at(TokenType::Assign)) {
            advance();
            parseAssignment();
        }
    }

    void parseArrayBinding()
    {
        advance();
        const Flag noIn(m_noIn, false);
        while (!at(TokenType::RightBracket)) {
            if (at(TokenType::Comma)) {
                advance();
                continue;
            }
            if (at(TokenType::Ellipsis)) {
                advance();
            }
            parseBindingElement();
            if (!at(TokenType::RightBracket)) {
                expect(TokenType::Comma, ",");
            }
        }
        advance();
    }

    void parseObjectBinding()
    {
        advance();
        const Flag noIn(m_noIn, false);
        while (!at(TokenType::RightBrace)) {
            if (at(TokenType::Ellipsis)) {
                advance();
                parseBindingTarget();
            } else {
                parsePropertyName();
                if (at(TokenType::Colon)) {
                    advance();
                    parseBindingElement();
                } else if (at(TokenType::Assign)) {
                    advance();
                    parseAssignment();
                }
            }
            if (!at(TokenType::RightBrace)) {
                expect(TokenType::Comma, ",");
            }
        }
        advance();
    }

    void parsePropertyName()
    {
        switch (token().type) {
        case TokenType::Name:
        case TokenType::String:
        case TokenType::Number:
        case TokenType::PrivateName:
            advance();
            return;
        case TokenType::LeftBracket: {
            advance();
            {
                const Flag noIn(m_noIn, false);
                parseAssignment();
            }
            expect(TokenType::RightBracket, "]");
            return;
        }
        default:
            fail(token().start, "syntax error: expected a property name");
        }
    }

    void parseFunctionDeclaration(std::uint32_t start, const Mark& from)
    {
        if (atKeyword(Keyword::Async)) {
            advance();
        }
        advance();
        if (at(TokenType::Star)) {
            advance();
        }
        if (at(TokenType::Name)) {
            advance();
        }
        if (!parseSignature(false)) {
            consumeSemicolon();
            eraseWhole(from, start);
        }
    }

    bool parseSignature(bool constructor)
    {
        if (at(TokenType::Less)) {
            parseTypeParameters(true);
        }
        parseParameters(constructor, true);
        if (at(TokenType::Colon)) {
            eraseReturnType();
        }
        if (!at(TokenType::LeftBrace)) {
            return false;
        }
        parseBlock();
        return true;
    }

    void parseParameters(bool constructor, bool erasing)
    {
        expect(TokenType::LeftParen, "(");
        const Flag noIn(m_noIn, false);
        const Flag conditional(m_noConditionalTypes, false);
        bool first = true;
        while (!at(TokenType::RightParen)) {
            const std::uint32_t start = token().start;
            if (at(TokenType::At)) {
                parseDecorators();
            }
            if (first && atKeyword(Keyword::This)) {
                const Token next = peek();
                if (next.type == TokenType::Colon || next.type == TokenType::Comma
                    || next.type == TokenType::RightParen) {
                    advance();
                    if (at(TokenType::Colon)) {
                        advance();
                        parseType();
                    }
                    if (at(TokenType::Comma)) {
                        advance();
                    }
                    if (erasing) {
                        erase(start, m_previousEnd);
                    }
                    first = false;
                    continue;
                }
            }
            first = false;
            parseParameterModifiers(constructor);
            if (at(TokenType::Ellipsis)) {
                advance();
            }
            parseBindingTarget();
            if (at(TokenType::Question)) {
                if (erasing) {
                    eraseToken();
                } else {
                    advance();
                }
            }
            if (at(TokenType::Colon)) {
                if (erasing) {
                    eraseTypeAnnotation();
                } else {
                    advance();
                    parseType();
                }
            }
            if (at(TokenType::Assign)) {
                advance();
                parseAssignment();
            }
            if (!at(TokenType::RightParen)) {
                expect(TokenType::Comma, ",");
            }
        }
        advance();
    }

    void parseParameterModifiers(bool constructor)
    {
        bool reported = false;
        while (at(TokenType::Name)) {
            const Keyword keyword = token().keyword;
            if (keyword != Keyword::Public && keyword != Keyword::Private && keyword != Keyword::Protected
                && keyword != Keyword::Readonly && keyword != Keyword::Override) {
                return;
            }
            const Token next = peek();
            if (next.type != TokenType::Name && next.type != TokenType::LeftBrace
                && next.type != TokenType::LeftBracket && next.type != TokenType::Ellipsis) {
                return;
            }
            if (!reported && !m_ambient && constructor) {
                issue(ProblemCode::ParameterProperty, token().start);
                reported = true;
            }
            advance();
        }
    }

    void parseReturnType()
    {
        if (atKeyword(Keyword::Asserts)) {
            const Token next = peek();
            if (next.type == TokenType::Name && !next.newlineBefore) {
                advance();
                advance();
                if (atKeyword(Keyword::Is) && !token().newlineBefore) {
                    advance();
                    parseType();
                }
                return;
            }
        }
        if (at(TokenType::Name)) {
            const Token next = peek();
            if (next.type == TokenType::Name && next.keyword == Keyword::Is && !next.newlineBefore) {
                advance();
                advance();
                parseType();
                return;
            }
        }
        parseType();
    }

    void parseTypeParameters(bool erasing)
    {
        const std::uint32_t start = token().start;
        expect(TokenType::Less, "<");
        {
            const Flag conditional(m_noConditionalTypes, false);
            while (!at(TokenType::Greater)) {
                while ((atKeyword(Keyword::Const) || atKeyword(Keyword::In) || atText("out"))
                       && peek().type == TokenType::Name) {
                    advance();
                }
                expectName();
                if (atKeyword(Keyword::Extends)) {
                    advance();
                    parseType();
                }
                if (at(TokenType::Assign)) {
                    advance();
                    parseType();
                }
                if (!at(TokenType::Greater)) {
                    expect(TokenType::Comma, ",");
                }
            }
        }
        advance();
        if (erasing) {
            erase(start, m_previousEnd);
        }
    }

    void parseTypeArguments()
    {
        m_scanner.rescanLess();
        expect(TokenType::Less, "<");
        {
            const Flag conditional(m_noConditionalTypes, false);
            while (!at(TokenType::Greater)) {
                parseType();
                if (!at(TokenType::Greater)) {
                    expect(TokenType::Comma, ",");
                }
            }
        }
        advance();
    }

    void parseType()
    {
        const Deeper deeper(*this);
        if (startsFunctionType()) {
            parseFunctionType();
            return;
        }
        parseUnionType();
        if (m_noConditionalTypes || !atKeyword(Keyword::Extends) || token().newlineBefore) {
            return;
        }
        advance();
        {
            const Flag noConditional(m_noConditionalTypes, true);
            parseType();
        }
        expect(TokenType::Question, "?");
        parseType();
        expect(TokenType::Colon, ":");
        parseType();
    }

    [[nodiscard]] bool startsFunctionType()
    {
        if (at(TokenType::Less) || atKeyword(Keyword::New)) {
            return true;
        }
        if (atKeyword(Keyword::Abstract)) {
            const Token next = peek();
            return next.type == TokenType::Name && next.keyword == Keyword::New;
        }
        if (!at(TokenType::LeftParen)) {
            return false;
        }
        const Scanner::State state = m_scanner.save();
        const bool result = scanFunctionTypeStart();
        m_scanner.restore(state);
        return result;
    }

    [[nodiscard]] bool scanFunctionTypeStart()
    {
        m_scanner.next();
        const TokenType first = m_scanner.token().type;
        if (first == TokenType::RightParen || first == TokenType::Ellipsis) {
            return true;
        }
        if (first == TokenType::Name) {
            m_scanner.next();
        } else if (first == TokenType::LeftBracket || first == TokenType::LeftBrace) {
            if (!skipBalanced()) {
                return false;
            }
        } else {
            return false;
        }
        switch (m_scanner.token().type) {
        case TokenType::Colon:
        case TokenType::Comma:
        case TokenType::Question:
        case TokenType::Assign:
            return true;
        case TokenType::RightParen:
            m_scanner.next();
            return m_scanner.token().type == TokenType::Arrow;
        default:
            return false;
        }
    }

    bool skipBalanced()
    {
        int depth = 0;
        for (;;) {
            switch (m_scanner.token().type) {
            case TokenType::LeftBracket:
            case TokenType::LeftBrace:
            case TokenType::LeftParen:
                ++depth;
                break;
            case TokenType::RightBracket:
            case TokenType::RightBrace:
            case TokenType::RightParen:
                --depth;
                break;
            case TokenType::Eof:
            case TokenType::Invalid:
                return false;
            default:
                break;
            }
            m_scanner.next();
            if (depth == 0) {
                return true;
            }
        }
    }

    void parseFunctionType()
    {
        if (atKeyword(Keyword::Abstract)) {
            advance();
        }
        if (atKeyword(Keyword::New)) {
            advance();
        }
        if (at(TokenType::Less)) {
            parseTypeParameters(false);
        }
        parseParameters(false, false);
        expect(TokenType::Arrow, "=>");
        parseReturnType();
    }

    void parseUnionType()
    {
        if (at(TokenType::Bar)) {
            advance();
        }
        parseIntersectionType();
        while (at(TokenType::Bar)) {
            advance();
            parseIntersectionType();
        }
    }

    void parseIntersectionType()
    {
        if (at(TokenType::Amp)) {
            advance();
        }
        parseTypeOperator();
        while (at(TokenType::Amp)) {
            advance();
            parseTypeOperator();
        }
    }

    void parseTypeOperator()
    {
        if ((atKeyword(Keyword::Keyof) || atKeyword(Keyword::Unique) || atKeyword(Keyword::Readonly))
            && startsType(peek())) {
            advance();
            parseTypeOperator();
            return;
        }
        if (atKeyword(Keyword::Infer)) {
            advance();
            expectName();
            if (atKeyword(Keyword::Extends) && !token().newlineBefore) {
                const bool outer = m_noConditionalTypes;
                attempt([&] {
                    advance();
                    {
                        const Flag noConditional(m_noConditionalTypes, true);
                        parseType();
                    }
                    if (!outer && at(TokenType::Question)) {
                        throw Abort{};
                    }
                });
            }
            return;
        }
        if (startsFunctionType()) {
            parseFunctionType();
            return;
        }
        parsePrimaryType();
        while (at(TokenType::LeftBracket) && !token().newlineBefore) {
            advance();
            if (at(TokenType::RightBracket)) {
                advance();
                continue;
            }
            {
                const Flag conditional(m_noConditionalTypes, false);
                parseType();
            }
            expect(TokenType::RightBracket, "]");
        }
    }

    void parsePrimaryType()
    {
        switch (token().type) {
        case TokenType::Name:
            if (atKeyword(Keyword::Typeof)) {
                advance();
                if (atKeyword(Keyword::Import)) {
                    parseImportType();
                    return;
                }
                parseEntityName();
                if (at(TokenType::Less) && !token().newlineBefore) {
                    parseTypeArguments();
                }
                return;
            }
            if (atKeyword(Keyword::Import)) {
                parseImportType();
                return;
            }
            parseEntityName();
            if ((at(TokenType::Less) || at(TokenType::LessLess)) && !token().newlineBefore) {
                parseTypeArguments();
            }
            return;
        case TokenType::String:
        case TokenType::Number:
        case TokenType::Template:
            advance();
            return;
        case TokenType::Minus:
            advance();
            expect(TokenType::Number, "number");
            return;
        case TokenType::TemplateHead:
            parseTemplateType();
            return;
        case TokenType::LeftBrace:
            if (startsMappedType()) {
                parseMappedType();
            } else {
                parseObjectType();
            }
            return;
        case TokenType::LeftBracket:
            parseTupleType();
            return;
        case TokenType::LeftParen: {
            advance();
            {
                const Flag conditional(m_noConditionalTypes, false);
                parseType();
            }
            expect(TokenType::RightParen, ")");
            return;
        }
        default:
            fail(token().start, "syntax error: expected a type");
        }
    }

    void parseEntityName()
    {
        expectName();
        while (at(TokenType::Dot)) {
            advance();
            if (!at(TokenType::Name) && !at(TokenType::PrivateName)) {
                fail(token().start, "syntax error: expected a name");
            }
            advance();
        }
    }

    void parseImportType()
    {
        advance();
        expect(TokenType::LeftParen, "(");
        expect(TokenType::String, "string");
        if (at(TokenType::Comma)) {
            advance();
            if (at(TokenType::LeftBrace)) {
                parseObjectLiteral();
                if (at(TokenType::Comma)) {
                    advance();
                }
            }
        }
        expect(TokenType::RightParen, ")");
        while (at(TokenType::Dot)) {
            advance();
            expectName();
        }
        if (at(TokenType::Less) && !token().newlineBefore) {
            parseTypeArguments();
        }
    }

    void parseTemplateType()
    {
        advance();
        const Flag conditional(m_noConditionalTypes, false);
        for (;;) {
            parseType();
            if (!at(TokenType::RightBrace)) {
                fail(token().start, "syntax error: expected '}'");
            }
            m_scanner.rescanTemplateContinuation();
            checkLexical();
            const bool tail = at(TokenType::TemplateTail);
            advance();
            if (tail) {
                return;
            }
        }
    }

    [[nodiscard]] bool startsMappedType()
    {
        const Scanner::State state = m_scanner.save();
        m_scanner.next();
        bool result = false;
        bool valid = true;
        if (m_scanner.token().type == TokenType::Plus || m_scanner.token().type == TokenType::Minus) {
            m_scanner.next();
            valid = m_scanner.token().type == TokenType::Name && m_scanner.token().keyword == Keyword::Readonly;
            m_scanner.next();
        } else if (m_scanner.token().type == TokenType::Name && m_scanner.token().keyword == Keyword::Readonly) {
            m_scanner.next();
        }
        if (valid && m_scanner.token().type == TokenType::LeftBracket) {
            m_scanner.next();
            if (m_scanner.token().type == TokenType::Name) {
                m_scanner.next();
                result = m_scanner.token().type == TokenType::Name && m_scanner.token().keyword == Keyword::In;
            }
        }
        m_scanner.restore(state);
        return result;
    }

    void parseMappedType()
    {
        advance();
        const Flag conditional(m_noConditionalTypes, false);
        if (at(TokenType::Plus) || at(TokenType::Minus)) {
            advance();
        }
        if (atKeyword(Keyword::Readonly)) {
            advance();
        }
        expect(TokenType::LeftBracket, "[");
        expectName();
        expectKeyword(Keyword::In, "in");
        parseType();
        if (atKeyword(Keyword::As)) {
            advance();
            parseType();
        }
        expect(TokenType::RightBracket, "]");
        if (at(TokenType::Plus) || at(TokenType::Minus)) {
            advance();
        }
        if (at(TokenType::Question)) {
            advance();
        }
        if (at(TokenType::Colon)) {
            advance();
            parseType();
        }
        if (at(TokenType::Semicolon) || at(TokenType::Comma)) {
            advance();
        }
        expect(TokenType::RightBrace, "}");
    }

    void parseObjectType()
    {
        expect(TokenType::LeftBrace, "{");
        const Flag conditional(m_noConditionalTypes, false);
        while (!at(TokenType::RightBrace)) {
            if (at(TokenType::Eof)) {
                fail(token().start, "syntax error: expected '}'");
            }
            parseTypeMember();
            if (at(TokenType::Semicolon) || at(TokenType::Comma)) {
                advance();
            } else if (!at(TokenType::RightBrace) && !token().newlineBefore) {
                fail(token().start, "syntax error: expected ';'");
            }
        }
        advance();
    }

    void parseTypeMember()
    {
        const Deeper deeper(*this);
        if (at(TokenType::LeftParen) || at(TokenType::Less)) {
            parseCallSignature();
            return;
        }
        if (atKeyword(Keyword::New)) {
            const Token next = peek();
            if (next.type == TokenType::LeftParen || next.type == TokenType::Less) {
                advance();
                parseCallSignature();
                return;
            }
        }
        if (atKeyword(Keyword::Readonly)) {
            const Token next = peek();
            if (startsPropertyName(next) && !next.newlineBefore) {
                advance();
            }
        }
        if ((atKeyword(Keyword::Get) || atKeyword(Keyword::Set)) && startsPropertyName(peek())) {
            advance();
        }
        if (at(TokenType::LeftBracket) && startsIndexSignature()) {
            parseIndexSignature();
            return;
        }
        parsePropertyName();
        if (at(TokenType::Question)) {
            advance();
        }
        if (at(TokenType::LeftParen) || at(TokenType::Less)) {
            parseCallSignature();
            return;
        }
        if (at(TokenType::Colon)) {
            advance();
            parseType();
        }
    }

    void parseCallSignature()
    {
        if (at(TokenType::Less)) {
            parseTypeParameters(false);
        }
        parseParameters(false, false);
        if (at(TokenType::Colon)) {
            advance();
            parseReturnType();
        }
    }

    [[nodiscard]] bool startsIndexSignature()
    {
        const Scanner::State state = m_scanner.save();
        m_scanner.next();
        bool result = false;
        if (m_scanner.token().type == TokenType::Name) {
            m_scanner.next();
            result = m_scanner.token().type == TokenType::Colon;
        }
        m_scanner.restore(state);
        return result;
    }

    void parseIndexSignature()
    {
        advance();
        expectName();
        expect(TokenType::Colon, ":");
        parseType();
        expect(TokenType::RightBracket, "]");
        if (at(TokenType::Colon)) {
            advance();
            parseType();
        }
    }

    void parseTupleType()
    {
        advance();
        const Flag conditional(m_noConditionalTypes, false);
        while (!at(TokenType::RightBracket)) {
            if (at(TokenType::Ellipsis)) {
                advance();
            }
            if (at(TokenType::Name)) {
                const Token next = peek();
                if (next.type == TokenType::Colon) {
                    advance();
                    advance();
                } else if (next.type == TokenType::Question && peek(2).type == TokenType::Colon) {
                    advance();
                    advance();
                    advance();
                }
            }
            parseType();
            if (at(TokenType::Question)) {
                advance();
            }
            if (!at(TokenType::RightBracket)) {
                expect(TokenType::Comma, ",");
            }
        }
        advance();
    }

    void parseClass()
    {
        advance();
        if (at(TokenType::Name) && !atKeyword(Keyword::Extends) && !atKeyword(Keyword::Implements)) {
            advance();
        }
        if (at(TokenType::Less)) {
            parseTypeParameters(true);
        }
        if (atKeyword(Keyword::Extends)) {
            advance();
            parseLeftHandSide();
            if (at(TokenType::Less) || at(TokenType::LessLess)) {
                const std::uint32_t start = token().start;
                parseTypeArguments();
                erase(start, m_previousEnd);
            }
        }
        if (atKeyword(Keyword::Implements)) {
            const std::uint32_t start = token().start;
            advance();
            for (;;) {
                parseEntityName();
                if (at(TokenType::Less)) {
                    parseTypeArguments();
                }
                if (!at(TokenType::Comma)) {
                    break;
                }
                advance();
            }
            erase(start, m_previousEnd);
        }
        parseClassBody();
    }

    void parseClassBody()
    {
        expect(TokenType::LeftBrace, "{");
        const Flag noIn(m_noIn, false);
        while (!at(TokenType::RightBrace)) {
            if (at(TokenType::Eof)) {
                fail(token().start, "syntax error: expected '}'");
            }
            parseClassMember();
        }
        advance();
    }

    [[nodiscard]] bool atConstructorName() const noexcept
    {
        if (at(TokenType::Name)) {
            return m_scanner.text(token()) == "constructor";
        }
        if (at(TokenType::String)) {
            const std::string_view text = m_scanner.text(token());
            return text == "\"constructor\"" || text == "'constructor'";
        }
        return false;
    }

    void parseClassMember()
    {
        const Deeper deeper(*this);
        if (at(TokenType::Semicolon)) {
            advance();
            return;
        }
        const Mark from = mark();
        const std::uint32_t start = token().start;
        if (at(TokenType::At)) {
            parseDecorators();
        }
        bool remove = false;
        while (at(TokenType::Name)) {
            const Keyword keyword = token().keyword;
            const bool typescript = keyword == Keyword::Public || keyword == Keyword::Private
                || keyword == Keyword::Protected || keyword == Keyword::Readonly || keyword == Keyword::Override
                || keyword == Keyword::Abstract || keyword == Keyword::Declare || keyword == Keyword::Accessor;
            if (!typescript && keyword != Keyword::Static) {
                break;
            }
            const Token next = peek();
            if (!canFollowModifier(next, keyword == Keyword::Static)) {
                break;
            }
            if (keyword == Keyword::Static) {
                if (next.type == TokenType::LeftBrace) {
                    advance();
                    parseBlock();
                    return;
                }
                advance();
                continue;
            }
            if (next.type == TokenType::LeftBrace) {
                break;
            }
            if (keyword == Keyword::Abstract || keyword == Keyword::Declare) {
                remove = true;
            }
            eraseToken();
        }
        if (at(TokenType::LeftBracket) && startsIndexSignature()) {
            parseIndexSignature();
            consumeSemicolon();
            eraseWhole(from, start);
            return;
        }
        if (atKeyword(Keyword::Async)) {
            const Token next = peek();
            if (!next.newlineBefore && (startsPropertyName(next) || next.type == TokenType::Star)) {
                advance();
            }
        }
        if (at(TokenType::Star)) {
            advance();
        }
        if ((atKeyword(Keyword::Get) || atKeyword(Keyword::Set)) && startsPropertyName(peek())) {
            advance();
        }
        const bool constructor = atConstructorName();
        parsePropertyName();
        if (at(TokenType::Question) || (at(TokenType::Bang) && !token().newlineBefore)) {
            eraseToken();
        }
        if (at(TokenType::LeftParen) || at(TokenType::Less)) {
            if (!parseSignature(constructor)) {
                consumeSemicolon();
                remove = true;
            }
            if (remove) {
                eraseWhole(from, start);
            }
            return;
        }
        if (at(TokenType::Colon)) {
            eraseTypeAnnotation();
        }
        if (at(TokenType::Assign)) {
            advance();
            parseAssignment();
        }
        consumeSemicolon();
        if (remove) {
            eraseWhole(from, start);
        }
    }

    void parseDecorators()
    {
        while (at(TokenType::At)) {
            issue(ProblemCode::Decorator, token().start);
            advance();
            if (at(TokenType::LeftParen)) {
                advance();
                {
                    const Flag noIn(m_noIn, false);
                    parseExpression();
                }
                expect(TokenType::RightParen, ")");
                continue;
            }
            expectName();
            while (at(TokenType::Dot)) {
                advance();
                expectName();
            }
            if (at(TokenType::Less)) {
                static_cast<void>(tryTypeArgumentsInExpression());
            }
            if (at(TokenType::LeftParen)) {
                parseArguments();
            }
        }
    }

    void parseInterface(std::uint32_t start, const Mark& from)
    {
        advance();
        expectName();
        if (at(TokenType::Less)) {
            parseTypeParameters(false);
        }
        if (atKeyword(Keyword::Extends)) {
            advance();
            for (;;) {
                parseEntityName();
                if (at(TokenType::Less)) {
                    parseTypeArguments();
                }
                if (!at(TokenType::Comma)) {
                    break;
                }
                advance();
            }
        }
        parseObjectType();
        eraseWhole(from, start);
    }

    void parseTypeAlias(std::uint32_t start, const Mark& from)
    {
        advance();
        expectName();
        if (at(TokenType::Less)) {
            parseTypeParameters(false);
        }
        expect(TokenType::Assign, "=");
        parseType();
        consumeSemicolon();
        eraseWhole(from, start);
    }

    void parseDeclare(std::uint32_t start, const Mark& from)
    {
        advance();
        {
            const Flag ambient(m_ambient, true);
            parseStatement();
        }
        eraseWhole(from, start);
    }

    void parseEnum(bool isConst)
    {
        const std::uint32_t position = token().start;
        if (isConst) {
            advance();
        }
        advance();
        if (!m_ambient) {
            issue(isConst ? ProblemCode::ConstEnum : ProblemCode::Enum, position);
        }
        expectName();
        expect(TokenType::LeftBrace, "{");
        while (!at(TokenType::RightBrace)) {
            parsePropertyName();
            if (at(TokenType::Assign)) {
                advance();
                parseAssignment();
            }
            if (!at(TokenType::RightBrace)) {
                expect(TokenType::Comma, ",");
            }
        }
        advance();
    }

    void parseNamespace(std::uint32_t start, const Mark& from)
    {
        const std::uint32_t keyword = token().start;
        const bool global = atKeyword(Keyword::Global);
        advance();
        if (!global) {
            if (at(TokenType::String)) {
                advance();
            } else {
                expectName();
                while (at(TokenType::Dot)) {
                    advance();
                    expectName();
                }
            }
        }
        if (!at(TokenType::LeftBrace)) {
            consumeSemicolon();
            eraseWhole(from, start);
            return;
        }
        advance();
        bool values = false;
        {
            const Flag noIn(m_noIn, false);
            while (!at(TokenType::RightBrace)) {
                if (at(TokenType::Eof)) {
                    fail(token().start, "syntax error: expected '}'");
                }
                const std::uint32_t statement = token().start;
                const bool empty = at(TokenType::Semicolon);
                parseStatement();
                if (!empty && m_erasedStatement != statement) {
                    values = true;
                }
            }
        }
        advance();
        if (m_ambient || !values) {
            eraseWhole(from, start);
            return;
        }
        issue(ProblemCode::Namespace, keyword);
    }

    void parseImport(std::uint32_t start, const Mark& from)
    {
        advance();
        if (at(TokenType::String)) {
            advance();
            parseImportAttributes();
            consumeSemicolon();
            return;
        }
        if (atKeyword(Keyword::Type) && importIsTypeOnly()) {
            advance();
            parseImportClause(start, true);
            eraseWhole(from, start);
            return;
        }
        parseImportClause(start, false);
    }

    [[nodiscard]] bool importIsTypeOnly()
    {
        const Token next = peek();
        if (next.type == TokenType::LeftBrace || next.type == TokenType::Star) {
            return true;
        }
        if (next.type != TokenType::Name) {
            return false;
        }
        if (next.keyword != Keyword::From) {
            return true;
        }
        const Token after = peek(2);
        return after.type == TokenType::Name && after.keyword == Keyword::From;
    }

    void parseImportClause(std::uint32_t start, bool typeOnly)
    {
        if (at(TokenType::Name)) {
            if (peek().type == TokenType::Assign) {
                parseImportEquals(start, typeOnly);
                return;
            }
            advance();
            if (!at(TokenType::Comma)) {
                parseFromClause();
                return;
            }
            advance();
        }
        if (at(TokenType::Star)) {
            advance();
            expectKeyword(Keyword::As, "as");
            expectName();
        } else if (at(TokenType::LeftBrace)) {
            parseSpecifiers(!typeOnly);
        } else {
            fail(token().start, "syntax error: expected '{'");
        }
        parseFromClause();
    }

    void parseFromClause()
    {
        expectKeyword(Keyword::From, "from");
        expect(TokenType::String, "string");
        parseImportAttributes();
        consumeSemicolon();
    }

    void parseImportAttributes()
    {
        if ((atKeyword(Keyword::With) || atText("assert")) && !token().newlineBefore) {
            advance();
            parseObjectLiteral();
        }
    }

    void parseImportEquals(std::uint32_t start, bool typeOnly)
    {
        advance();
        advance();
        bool require = false;
        if (atKeyword(Keyword::Require) && peek().type == TokenType::LeftParen) {
            require = true;
            advance();
            advance();
            expect(TokenType::String, "string");
            expect(TokenType::RightParen, ")");
        } else {
            parseEntityName();
        }
        consumeSemicolon();
        if (!typeOnly) {
            issue(require ? ProblemCode::ImportRequire : ProblemCode::ImportAlias, start);
        }
    }

    void parseModuleExportName()
    {
        if (!at(TokenType::Name) && !at(TokenType::String)) {
            fail(token().start, "syntax error: expected a name");
        }
        advance();
    }

    [[nodiscard]] bool specifierIsTypeOnly()
    {
        const Token next = peek();
        if (next.type == TokenType::Comma || next.type == TokenType::RightBrace) {
            return false;
        }
        if (next.type == TokenType::Name && next.keyword == Keyword::As) {
            const Token after = peek(2);
            if (after.type == TokenType::Comma || after.type == TokenType::RightBrace) {
                return true;
            }
            return after.type == TokenType::Name && after.keyword == Keyword::As;
        }
        return next.type == TokenType::Name || next.type == TokenType::String;
    }

    void parseSpecifiers(bool eraseTypeOnly)
    {
        advance();
        std::uint32_t comma = 0;
        bool commaErased = true;
        while (!at(TokenType::RightBrace)) {
            const std::uint32_t start = token().start;
            bool typeOnly = false;
            if (atKeyword(Keyword::Type) && specifierIsTypeOnly()) {
                typeOnly = true;
                advance();
            }
            parseModuleExportName();
            if (atKeyword(Keyword::As)) {
                advance();
                parseModuleExportName();
            }
            if (at(TokenType::Comma)) {
                const std::uint32_t commaStart = token().start;
                advance();
                if (typeOnly && eraseTypeOnly) {
                    erase(start, m_previousEnd);
                }
                comma = commaStart;
                commaErased = typeOnly && eraseTypeOnly;
                continue;
            }
            if (typeOnly && eraseTypeOnly) {
                erase(commaErased ? start : comma, m_previousEnd);
            }
            break;
        }
        expect(TokenType::RightBrace, "}");
    }

    void parseExport(std::uint32_t start, const Mark& from)
    {
        advance();
        if (at(TokenType::Assign)) {
            issue(ProblemCode::ExportAssignment, start);
            advance();
            parseAssignment();
            consumeSemicolon();
            return;
        }
        if (atKeyword(Keyword::Type)) {
            const Token next = peek();
            if (next.type == TokenType::LeftBrace || next.type == TokenType::Star) {
                advance();
                if (at(TokenType::Star)) {
                    advance();
                    if (atKeyword(Keyword::As)) {
                        advance();
                        parseModuleExportName();
                    }
                    parseFromClause();
                } else {
                    parseSpecifiers(false);
                    if (atKeyword(Keyword::From)) {
                        parseFromClause();
                    } else {
                        consumeSemicolon();
                    }
                }
                eraseWhole(from, start);
                return;
            }
        }
        if (atKeyword(Keyword::As) && nextIsKeywordOnSameLine(Keyword::Namespace)) {
            advance();
            advance();
            expectName();
            consumeSemicolon();
            eraseWhole(from, start);
            return;
        }
        if (atKeyword(Keyword::Import)) {
            advance();
            if (!at(TokenType::Name) || peek().type != TokenType::Assign) {
                fail(token().start, "syntax error: expected a name");
            }
            parseImportEquals(start, false);
            return;
        }
        if (atKeyword(Keyword::Default)) {
            advance();
            if (atKeyword(Keyword::Interface) && nextIsNameOnSameLine()) {
                parseInterface(start, from);
                return;
            }
            if (atKeyword(Keyword::Abstract) && nextIsKeywordOnSameLine(Keyword::Class)) {
                eraseToken();
                parseClass();
                return;
            }
            if (atKeyword(Keyword::Class)) {
                parseClass();
                return;
            }
            if (atKeyword(Keyword::Function)
                || (atKeyword(Keyword::Async) && nextIsKeywordOnSameLine(Keyword::Function))) {
                parseFunctionDeclaration(start, from);
                return;
            }
            parseAssignment();
            consumeSemicolon();
            return;
        }
        if (at(TokenType::Star)) {
            advance();
            if (atKeyword(Keyword::As)) {
                advance();
                parseModuleExportName();
            }
            parseFromClause();
            return;
        }
        if (at(TokenType::LeftBrace)) {
            parseSpecifiers(true);
            if (atKeyword(Keyword::From)) {
                parseFromClause();
            } else {
                consumeSemicolon();
            }
            return;
        }
        if (at(TokenType::At)) {
            parseDecorators();
        }
        parseStatementAt(start, from);
    }

    void parseExpression()
    {
        parseAssignment();
        while (at(TokenType::Comma)) {
            advance();
            parseAssignment();
        }
    }

    void parseAssignment()
    {
        const bool strict = std::exchange(m_conditionalTrue, false);
        const Deeper deeper(*this);
        if (tryArrowFunction(strict)) {
            return;
        }
        if (atKeyword(Keyword::Yield)) {
            advance();
            if (at(TokenType::Star) && !token().newlineBefore) {
                advance();
                parseAssignment();
                return;
            }
            if (!token().newlineBefore && startsExpression(token())) {
                parseAssignment();
            }
            return;
        }
        parseConditional();
        if (isAssignmentOperator(token().type)) {
            advance();
            parseAssignment();
        }
    }

    void parseConditional()
    {
        parseBinary();
        if (!at(TokenType::Question)) {
            return;
        }
        advance();
        {
            const Flag noIn(m_noIn, false);
            m_conditionalTrue = true;
            parseAssignment();
        }
        expect(TokenType::Colon, ":");
        parseAssignment();
    }

    void parseBinary()
    {
        parseUnary();
        for (;;) {
            if (at(TokenType::Greater)) {
                m_scanner.rescanGreater();
            }
            if (isBinaryOperator(token().type) || atKeyword(Keyword::Instanceof)
                || (atKeyword(Keyword::In) && !m_noIn)) {
                advance();
                parseUnary();
                continue;
            }
            if ((atKeyword(Keyword::As) || atKeyword(Keyword::Satisfies)) && !token().newlineBefore) {
                const std::uint32_t start = token().start;
                advance();
                if (atKeyword(Keyword::Const)) {
                    advance();
                } else {
                    parseType();
                }
                erase(start, m_previousEnd);
                continue;
            }
            return;
        }
    }

    void parseUnary()
    {
        const Deeper deeper(*this);
        switch (token().type) {
        case TokenType::Bang:
        case TokenType::Tilde:
        case TokenType::Plus:
        case TokenType::Minus:
        case TokenType::PlusPlus:
        case TokenType::MinusMinus:
            advance();
            parseUnary();
            return;
        case TokenType::Less:
            parseTypeAssertion();
            return;
        case TokenType::Name:
            if (atKeyword(Keyword::Typeof) || atKeyword(Keyword::Void) || atKeyword(Keyword::Delete)
                || atKeyword(Keyword::Await)) {
                advance();
                parseUnary();
                return;
            }
            break;
        default:
            break;
        }
        parseLeftHandSide();
        if ((at(TokenType::PlusPlus) || at(TokenType::MinusMinus)) && !token().newlineBefore) {
            advance();
        }
    }

    void parseTypeAssertion()
    {
        const std::uint32_t start = token().start;
        const bool restricted = m_previousKeyword == Keyword::Return || m_previousKeyword == Keyword::Throw
            || m_previousKeyword == Keyword::Yield;
        const bool typed = attempt([&] {
            advance();
            parseType();
            if (!at(TokenType::Greater)) {
                throw Abort{};
            }
            advance();
        });
        if (!typed) {
            if (!m_speculating) {
                issue(ProblemCode::Jsx, start);
            }
            throw Abort{};
        }
        erase(start, m_previousEnd);
        if (restricted && token().newlineBefore) {
            issue(ProblemCode::UnsafeAssertion, start);
        }
        parseUnary();
    }

    void parseLeftHandSide()
    {
        if (atKeyword(Keyword::New)) {
            parseNew();
        } else {
            parsePrimary();
        }
        parseCallTail(true);
    }

    void parseNew()
    {
        advance();
        if (at(TokenType::Dot)) {
            advance();
            expectName();
            return;
        }
        if (atKeyword(Keyword::New)) {
            parseNew();
        } else {
            parsePrimary();
        }
        parseCallTail(false);
        if (at(TokenType::LeftParen)) {
            parseArguments();
        }
    }

    void parseCallTail(bool calls)
    {
        for (;;) {
            switch (token().type) {
            case TokenType::Dot:
                advance();
                if (!at(TokenType::Name) && !at(TokenType::PrivateName)) {
                    fail(token().start, "syntax error: expected a property name");
                }
                advance();
                continue;
            case TokenType::QuestionDot:
                advance();
                if (at(TokenType::LeftParen)) {
                    parseArguments();
                } else if (at(TokenType::LeftBracket)) {
                    parseIndex();
                } else if (at(TokenType::Less) || at(TokenType::LessLess)) {
                    if (!tryTypeArgumentsInExpression()) {
                        fail(token().start, "syntax error: expected a property name");
                    }
                    if (at(TokenType::LeftParen)) {
                        parseArguments();
                    }
                } else {
                    if (!at(TokenType::Name) && !at(TokenType::PrivateName)) {
                        fail(token().start, "syntax error: expected a property name");
                    }
                    advance();
                }
                continue;
            case TokenType::LeftBracket:
                parseIndex();
                continue;
            case TokenType::LeftParen:
                if (!calls) {
                    return;
                }
                parseArguments();
                continue;
            case TokenType::Template:
            case TokenType::TemplateHead:
                parseTemplate();
                continue;
            case TokenType::Bang:
                if (token().newlineBefore) {
                    return;
                }
                eraseToken();
                continue;
            case TokenType::Less:
            case TokenType::LessLess:
                if (tryTypeArgumentsInExpression()) {
                    continue;
                }
                return;
            default:
                return;
            }
        }
    }

    void parseIndex()
    {
        advance();
        {
            const Flag noIn(m_noIn, false);
            parseExpression();
        }
        expect(TokenType::RightBracket, "]");
    }

    void parseArguments()
    {
        advance();
        const Flag noIn(m_noIn, false);
        while (!at(TokenType::RightParen)) {
            if (at(TokenType::Ellipsis)) {
                advance();
            }
            parseAssignment();
            if (!at(TokenType::RightParen)) {
                expect(TokenType::Comma, ",");
            }
        }
        advance();
    }

    [[nodiscard]] bool mayBeTypeArguments()
    {
        const Scanner::State state = m_scanner.save();
        int angles = at(TokenType::LessLess) ? 2 : 1;
        int nesting = 0;
        bool result = false;
        bool done = false;
        while (!done) {
            m_scanner.next();
            const Token current = m_scanner.token();
            switch (current.type) {
            case TokenType::Less:
                ++angles;
                break;
            case TokenType::LessLess:
                angles += 2;
                break;
            case TokenType::Greater:
                if (--angles == 0) {
                    done = true;
                    if (nesting == 0 && byteAt(current.end) != '=' && byteAt(current.end) != '>') {
                        m_scanner.next();
                        result = canFollowTypeArguments(m_scanner.token());
                    }
                }
                break;
            case TokenType::LeftParen:
            case TokenType::LeftBracket:
            case TokenType::LeftBrace:
                ++nesting;
                break;
            case TokenType::RightParen:
            case TokenType::RightBracket:
            case TokenType::RightBrace:
                done = --nesting < 0;
                break;
            case TokenType::Name:
            case TokenType::String:
            case TokenType::Number:
            case TokenType::Dot:
            case TokenType::Comma:
            case TokenType::Colon:
            case TokenType::Question:
            case TokenType::Bar:
            case TokenType::Amp:
            case TokenType::Arrow:
            case TokenType::Ellipsis:
            case TokenType::Minus:
            case TokenType::Template:
                break;
            case TokenType::Semicolon:
            case TokenType::Plus:
                done = nesting == 0;
                break;
            case TokenType::TemplateHead:
                result = true;
                done = true;
                break;
            default:
                done = true;
                break;
            }
        }
        m_scanner.restore(state);
        return result;
    }

    bool tryTypeArgumentsInExpression()
    {
        if (!mayBeTypeArguments()) {
            return false;
        }
        const std::uint32_t start = token().start;
        const bool parsed = attempt([&] {
            parseTypeArguments();
            const char follow = byteAt(m_previousEnd);
            if (follow == '=' || follow == '>' || !canFollowTypeArguments(token())) {
                throw Abort{};
            }
        });
        if (parsed) {
            erase(start, m_previousEnd);
        }
        return parsed;
    }

    void parsePrimary()
    {
        switch (token().type) {
        case TokenType::Name:
            if (atKeyword(Keyword::Function)) {
                parseFunctionExpression();
                return;
            }
            if (atKeyword(Keyword::Class)) {
                parseClass();
                return;
            }
            if (atKeyword(Keyword::Async) && nextIsKeywordOnSameLine(Keyword::Function)) {
                advance();
                parseFunctionExpression();
                return;
            }
            advance();
            return;
        case TokenType::Number:
        case TokenType::String:
        case TokenType::Template:
        case TokenType::PrivateName:
        case TokenType::Regex:
            advance();
            return;
        case TokenType::TemplateHead:
            parseTemplate();
            return;
        case TokenType::Slash:
        case TokenType::SlashAssign:
            m_scanner.rescanRegex();
            checkLexical();
            advance();
            return;
        case TokenType::LeftParen:
            advance();
            {
                const Flag noIn(m_noIn, false);
                parseExpression();
            }
            expect(TokenType::RightParen, ")");
            return;
        case TokenType::LeftBracket:
            parseArrayLiteral();
            return;
        case TokenType::LeftBrace:
            parseObjectLiteral();
            return;
        case TokenType::At:
            parseDecorators();
            if (!atKeyword(Keyword::Class)) {
                fail(token().start, "syntax error: expected 'class'");
            }
            parseClass();
            return;
        default:
            fail(token().start, "syntax error: expected an expression");
        }
    }

    void parseFunctionExpression()
    {
        advance();
        if (at(TokenType::Star)) {
            advance();
        }
        if (at(TokenType::Name)) {
            advance();
        }
        if (!parseSignature(false)) {
            fail(token().start, "syntax error: expected '{'");
        }
    }

    void parseArrayLiteral()
    {
        advance();
        const Flag noIn(m_noIn, false);
        while (!at(TokenType::RightBracket)) {
            if (at(TokenType::Comma)) {
                advance();
                continue;
            }
            if (at(TokenType::Ellipsis)) {
                advance();
            }
            parseAssignment();
            if (!at(TokenType::RightBracket)) {
                expect(TokenType::Comma, ",");
            }
        }
        advance();
    }

    void parseObjectLiteral()
    {
        advance();
        const Flag noIn(m_noIn, false);
        while (!at(TokenType::RightBrace)) {
            if (at(TokenType::Ellipsis)) {
                advance();
                parseAssignment();
            } else {
                parseObjectMember();
            }
            if (!at(TokenType::RightBrace)) {
                expect(TokenType::Comma, ",");
            }
        }
        advance();
    }

    void parseObjectMember()
    {
        if (atKeyword(Keyword::Async)) {
            const Token next = peek();
            if (!next.newlineBefore && (startsPropertyName(next) || next.type == TokenType::Star)) {
                advance();
            }
        }
        if (at(TokenType::Star)) {
            advance();
        }
        if ((atKeyword(Keyword::Get) || atKeyword(Keyword::Set)) && startsPropertyName(peek())) {
            advance();
        }
        parsePropertyName();
        if (at(TokenType::LeftParen) || at(TokenType::Less)) {
            if (!parseSignature(false)) {
                fail(token().start, "syntax error: expected '{'");
            }
            return;
        }
        if (at(TokenType::Colon) || at(TokenType::Assign)) {
            advance();
            parseAssignment();
        }
    }

    void parseTemplate()
    {
        if (at(TokenType::Template)) {
            advance();
            return;
        }
        advance();
        const Flag noIn(m_noIn, false);
        for (;;) {
            parseExpression();
            if (!at(TokenType::RightBrace)) {
                fail(token().start, "syntax error: expected '}'");
            }
            m_scanner.rescanTemplateContinuation();
            checkLexical();
            const bool tail = at(TokenType::TemplateTail);
            advance();
            if (tail) {
                return;
            }
        }
    }

    bool tryArrowFunction(bool strict)
    {
        const Token current = token();
        if (current.type == TokenType::LeftParen || current.type == TokenType::Less) {
            return parseParenthesizedArrow(strict);
        }
        if (current.type != TokenType::Name) {
            return false;
        }
        if (current.keyword == Keyword::Async) {
            const Token next = peek();
            if (!next.newlineBefore && (next.type == TokenType::LeftParen || next.type == TokenType::Less)) {
                const Mark from = mark();
                advance();
                if (parseParenthesizedArrow(strict)) {
                    return true;
                }
                reset(from);
                return false;
            }
            if (!next.newlineBefore && next.type == TokenType::Name && !detail::isReserved(next.keyword)) {
                const Token after = peek(2);
                if (after.type == TokenType::Arrow && !after.newlineBefore) {
                    advance();
                    advance();
                    advance();
                    parseArrowBody();
                    return true;
                }
            }
        }
        if (detail::isReserved(current.keyword)) {
            return false;
        }
        const Token next = peek();
        if (next.type != TokenType::Arrow || next.newlineBefore) {
            return false;
        }
        advance();
        advance();
        parseArrowBody();
        return true;
    }

    bool parseParenthesizedArrow(bool strict)
    {
        if (at(TokenType::LeftParen)) {
            const Token after = tokenAfterParentheses();
            const bool arrow = after.type == TokenType::Arrow && !after.newlineBefore;
            if (arrow) {
                static_cast<void>(parseArrowHead());
                parseArrowBody();
                return true;
            }
            if (after.type != TokenType::Invalid && after.type != TokenType::Colon) {
                return false;
            }
        }
        if (!strict) {
            if (!attempt([&] { static_cast<void>(parseArrowHead()); })) {
                return false;
            }
            parseArrowBody();
            return true;
        }
        return attempt([&] {
            const bool typed = parseArrowHead();
            parseArrowBody();
            if (typed && !at(TokenType::Colon)) {
                throw Abort{};
            }
        });
    }

    bool parseArrowHead()
    {
        if (at(TokenType::Less)) {
            const std::uint32_t less = token().start;
            parseTypeParameters(true);
            if (at(TokenType::LeftParen) && token().newlineBefore) {
                patch(less, '(');
                erase(token().start, token().end);
            }
        }
        parseParameters(false, true);
        const std::uint32_t close = m_previousEnd - 1;
        bool typed = false;
        if (at(TokenType::Colon)) {
            typed = true;
            eraseReturnType();
        }
        if (!at(TokenType::Arrow) || token().newlineBefore) {
            fail(token().start, "syntax error: expected '=>'");
        }
        if (typed && lineBreakBetween(close, token().start)) {
            erase(close, close + 1);
            patch(m_previousEnd - 1, ')');
        }
        advance();
        return typed;
    }

    void parseArrowBody()
    {
        if (at(TokenType::LeftBrace)) {
            parseBlock();
            return;
        }
        parseAssignment();
    }

    [[nodiscard]] Token tokenAfterParentheses()
    {
        const Scanner::State state = m_scanner.save();
        std::array<TokenType, 64> closers{};
        std::size_t depth = 0;
        closers[depth++] = TokenType::RightParen;
        Token result;
        result.type = TokenType::Invalid;
        Token previous = m_scanner.token();
        for (;;) {
            m_scanner.next();
            Token current = m_scanner.token();
            if (current.type == TokenType::Eof || current.type == TokenType::Invalid) {
                break;
            }
            if (current.type == TokenType::Slash || current.type == TokenType::SlashAssign) {
                if (!endsOperand(previous)) {
                    m_scanner.rescanRegex();
                    current = m_scanner.token();
                    if (current.type == TokenType::Invalid) {
                        break;
                    }
                }
            } else if (current.type == TokenType::LeftParen || current.type == TokenType::LeftBracket
                       || current.type == TokenType::LeftBrace || current.type == TokenType::TemplateHead) {
                if (depth == closers.size()) {
                    break;
                }
                closers[depth++] = current.type == TokenType::LeftParen     ? TokenType::RightParen
                                 : current.type == TokenType::LeftBracket ? TokenType::RightBracket
                                 : current.type == TokenType::LeftBrace   ? TokenType::RightBrace
                                                                          : TokenType::TemplateHead;
            } else if (current.type == TokenType::RightBrace && closers[depth - 1] == TokenType::TemplateHead) {
                m_scanner.rescanTemplateContinuation();
                current = m_scanner.token();
                if (current.type == TokenType::Invalid) {
                    break;
                }
                if (current.type == TokenType::TemplateTail) {
                    --depth;
                }
            } else if (current.type == TokenType::RightParen || current.type == TokenType::RightBracket
                       || current.type == TokenType::RightBrace) {
                if (closers[depth - 1] != current.type) {
                    break;
                }
                if (--depth == 0) {
                    m_scanner.next();
                    result = m_scanner.token();
                    break;
                }
            }
            previous = current;
        }
        m_scanner.restore(state);
        return result;
    }

    [[nodiscard]] std::vector<Problem> locate()
    {
        std::stable_sort(m_issues.begin(), m_issues.end(),
                         [](const Issue& a, const Issue& b) { return a.position < b.position; });
        std::vector<Problem> problems;
        problems.reserve(m_issues.size());
        std::uint32_t line = 1;
        std::uint32_t column = 1;
        std::size_t i = 0;
        for (Issue& found : m_issues) {
            while (i < found.position && i < m_source.size()) {
                const auto c = static_cast<std::uint8_t>(m_source[i]);
                if (c == '\n' || c == '\r') {
                    ++line;
                    column = 1;
                    i += c == '\r' && byteAt(static_cast<std::uint32_t>(i + 1)) == '\n' ? 2 : 1;
                    continue;
                }
                if (c < 0x80) {
                    ++column;
                    ++i;
                    continue;
                }
                const detail::CodePoint point = detail::decodeUtf8(m_source, i);
                if (detail::isLineTerminator(point.value)) {
                    ++line;
                    column = 1;
                } else {
                    column += point.value >= 0x10000 ? 2 : 1;
                }
                i += point.length;
            }
            problems.push_back({line, column, found.code, std::move(found.message)});
        }
        return problems;
    }

    [[nodiscard]] Result build()
    {
        Result result;
        const std::size_t size = m_source.size();
        std::vector<std::uint8_t> erased(size, 0);
        for (const Span& span : m_spans) {
            const std::size_t end = std::min<std::size_t>(span.end, size);
            if (span.start >= end) {
                continue;
            }
            std::fill(erased.begin() + static_cast<std::ptrdiff_t>(span.start),
                      erased.begin() + static_cast<std::ptrdiff_t>(end), std::uint8_t{1});
        }
        std::stable_sort(m_patches.begin(), m_patches.end(),
                         [](const Patch& a, const Patch& b) { return a.position < b.position; });
        std::string& out = result.javascript;
        out.reserve(size);
        std::size_t nextPatch = 0;
        std::size_t i = 0;
        while (i < size) {
            const auto c = static_cast<std::uint8_t>(m_source[i]);
            if (erased[i] == 0 || c == '\n' || c == '\r') {
                out.push_back(m_source[i]);
                ++i;
                continue;
            }
            std::size_t length = 1;
            std::size_t units = 1;
            if (c >= 0x80) {
                const detail::CodePoint point = detail::decodeUtf8(m_source, i);
                length = std::min<std::size_t>(point.length, size - i);
                if (detail::isLineTerminator(point.value)) {
                    out.append(m_source.substr(i, length));
                    i += length;
                    continue;
                }
                units = point.value >= 0x10000 ? 2 : 1;
            }
            while (nextPatch < m_patches.size() && m_patches[nextPatch].position < i) {
                ++nextPatch;
            }
            if (m_offsets == Offsets::Bytes) {
                for (std::size_t byte = i; byte < i + length; ++byte) {
                    while (nextPatch < m_patches.size() && m_patches[nextPatch].position < byte) {
                        ++nextPatch;
                    }
                    const bool patched = nextPatch < m_patches.size() && m_patches[nextPatch].position == byte;
                    out.push_back(patched ? m_patches[nextPatch].text : ' ');
                }
                i += length;
                continue;
            }
            char first = ' ';
            if (nextPatch < m_patches.size() && m_patches[nextPatch].position < i + length) {
                first = m_patches[nextPatch].text;
            }
            out.push_back(first);
            out.append(units - 1, ' ');
            i += length;
        }
        result.problems = locate();
        return result;
    }

    std::string_view m_source;
    Scanner m_scanner;
    Offsets m_offsets = Offsets::Utf16;
    std::uint32_t m_previousEnd = 0;
    Keyword m_previousKeyword = Keyword::None;
    std::vector<Span> m_spans;
    std::vector<Patch> m_patches;
    std::vector<Issue> m_issues;
    std::uint32_t m_erasedStatement = kNoStatement;
    int m_depth = 0;
    bool m_speculating = false;
    bool m_noIn = false;
    bool m_noConditionalTypes = false;
    bool m_conditionalTrue = false;
    bool m_ambient = false;
};

}

Result strip(std::string_view source, Offsets offsets)
{
    if (source.size() >= kNoStatement) {
        return {std::string(source), {Problem{1, 1, ProblemCode::Syntax, "syntax error: the script is too large"}}};
    }
    Parser parser(source, offsets);
    return parser.run();
}

std::string codeName(ProblemCode code)
{
    const auto number = static_cast<unsigned>(code);
    std::string name = "GS0000";
    name[2] = static_cast<char>('0' + number / 1000 % 10);
    name[3] = static_cast<char>('0' + number / 100 % 10);
    name[4] = static_cast<char>('0' + number / 10 % 10);
    name[5] = static_cast<char>('0' + number % 10);
    return name;
}

}
