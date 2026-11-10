#pragma once

#include <cstdint>
#include <string_view>

namespace gs::tsstrip::detail {

enum class TokenType : std::uint8_t {
    Eof,
    Invalid,
    Name,
    PrivateName,
    String,
    Number,
    Regex,
    Template,
    TemplateHead,
    TemplateMiddle,
    TemplateTail,
    LeftBrace,
    RightBrace,
    LeftParen,
    RightParen,
    LeftBracket,
    RightBracket,
    Dot,
    Ellipsis,
    Semicolon,
    Comma,
    Colon,
    Question,
    QuestionDot,
    QuestionQuestion,
    QuestionQuestionAssign,
    Arrow,
    At,
    Less,
    LessEqual,
    LessLess,
    LessLessAssign,
    Greater,
    GreaterEqual,
    GreaterGreater,
    GreaterGreaterAssign,
    GreaterGreaterGreater,
    GreaterGreaterGreaterAssign,
    Assign,
    Equal,
    StrictEqual,
    NotEqual,
    StrictNotEqual,
    Plus,
    PlusPlus,
    PlusAssign,
    Minus,
    MinusMinus,
    MinusAssign,
    Star,
    StarStar,
    StarAssign,
    StarStarAssign,
    Slash,
    SlashAssign,
    Percent,
    PercentAssign,
    Amp,
    AmpAmp,
    AmpAssign,
    AmpAmpAssign,
    Bar,
    BarBar,
    BarAssign,
    BarBarAssign,
    Caret,
    CaretAssign,
    Bang,
    Tilde,
};

enum class Keyword : std::uint8_t {
    None,
    Abstract,
    Accessor,
    As,
    Asserts,
    Async,
    Await,
    Break,
    Case,
    Catch,
    Class,
    Const,
    Continue,
    Debugger,
    Declare,
    Default,
    Delete,
    Do,
    Else,
    Enum,
    Export,
    Extends,
    False,
    Finally,
    For,
    From,
    Function,
    Get,
    Global,
    If,
    Implements,
    Import,
    In,
    Infer,
    Instanceof,
    Interface,
    Is,
    Keyof,
    Let,
    Module,
    Namespace,
    New,
    Null,
    Of,
    Override,
    Private,
    Protected,
    Public,
    Readonly,
    Require,
    Return,
    Satisfies,
    Set,
    Static,
    Super,
    Switch,
    This,
    Throw,
    True,
    Try,
    Type,
    Typeof,
    Unique,
    Using,
    Var,
    Void,
    While,
    With,
    Yield,
};

enum class LexError : std::uint8_t {
    None,
    UnterminatedString,
    UnterminatedTemplate,
    UnterminatedComment,
    UnterminatedRegex,
    InvalidCharacter,
    InvalidEscape,
    InvalidNumber,
};

struct Token {
    TokenType type = TokenType::Eof;
    Keyword keyword = Keyword::None;
    LexError error = LexError::None;
    bool newlineBefore = false;
    bool escaped = false;
    std::uint32_t start = 0;
    std::uint32_t end = 0;
};

struct CodePoint {
    std::uint32_t value = 0;
    std::uint32_t length = 1;
};

[[nodiscard]] CodePoint decodeUtf8(std::string_view text, std::size_t at) noexcept;
[[nodiscard]] bool isLineTerminator(std::uint32_t value) noexcept;
[[nodiscard]] bool isSpace(std::uint32_t value) noexcept;
[[nodiscard]] bool isReserved(Keyword keyword) noexcept;
[[nodiscard]] std::string_view lexErrorMessage(LexError error) noexcept;

class Scanner
{
public:
    struct State {
        Token token;
        std::uint32_t position = 0;
    };

    explicit Scanner(std::string_view source) noexcept : m_source(source) {}

    void next() noexcept;
    void rescanRegex() noexcept;
    void rescanTemplateContinuation() noexcept;
    void rescanGreater() noexcept;
    void rescanLess() noexcept;
    void forceEof() noexcept;

    [[nodiscard]] const Token& token() const noexcept { return m_token; }
    [[nodiscard]] State save() const noexcept { return {m_token, m_position}; }
    void restore(const State& state) noexcept
    {
        m_token = state.token;
        m_position = state.position;
    }
    [[nodiscard]] std::string_view text(const Token& of) const noexcept
    {
        return m_source.substr(of.start, of.end - of.start);
    }
    [[nodiscard]] std::string_view source() const noexcept { return m_source; }

private:
    [[nodiscard]] std::uint8_t at(std::uint32_t index) const noexcept
    {
        return index < m_source.size() ? static_cast<std::uint8_t>(m_source[index]) : 0;
    }
    [[nodiscard]] bool lineSeparatorAt(std::uint32_t index) const noexcept;
    bool skipTrivia(bool& newline) noexcept;
    void skipLine() noexcept;
    void finish(TokenType type, std::uint32_t length) noexcept;
    void reshape(TokenType type, std::uint32_t length) noexcept;
    void fail(LexError error) noexcept;
    void scanOperator(TokenType one, TokenType oneAssign, TokenType two, TokenType twoAssign) noexcept;
    void scanName(TokenType type) noexcept;
    bool scanEscape() noexcept;
    void scanNumber() noexcept;
    void scanString(std::uint8_t quote) noexcept;
    void scanTemplate(TokenType closed, TokenType open) noexcept;

    std::string_view m_source;
    std::uint32_t m_position = 0;
    Token m_token;
};

}
