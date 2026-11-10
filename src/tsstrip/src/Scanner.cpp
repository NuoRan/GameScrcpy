#include "Scanner.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace gs::tsstrip::detail {

namespace {

constexpr bool isDigit(std::uint8_t c) noexcept
{
    return c >= '0' && c <= '9';
}

constexpr bool isHexDigit(std::uint8_t c) noexcept
{
    const auto lower = static_cast<std::uint8_t>(c | 0x20);
    return isDigit(c) || (lower >= 'a' && lower <= 'f');
}

constexpr bool isNameStart(std::uint8_t c) noexcept
{
    const auto lower = static_cast<std::uint8_t>(c | 0x20);
    return (lower >= 'a' && lower <= 'z') || c == '$' || c == '_';
}

constexpr std::array<bool, 256> kNamePart = [] {
    std::array<bool, 256> table{};
    for (std::size_t c = 0; c < table.size(); ++c) {
        const auto byte = static_cast<std::uint8_t>(c);
        table[c] = isNameStart(byte) || isDigit(byte);
    }
    return table;
}();

struct KeywordText {
    std::string_view text;
    Keyword keyword = Keyword::None;
};

constexpr KeywordText kKeywords[] = {
    {"abstract", Keyword::Abstract},   {"accessor", Keyword::Accessor},     {"as", Keyword::As},
    {"asserts", Keyword::Asserts},     {"async", Keyword::Async},           {"await", Keyword::Await},
    {"break", Keyword::Break},         {"case", Keyword::Case},             {"catch", Keyword::Catch},
    {"class", Keyword::Class},         {"const", Keyword::Const},           {"continue", Keyword::Continue},
    {"debugger", Keyword::Debugger},   {"declare", Keyword::Declare},       {"default", Keyword::Default},
    {"delete", Keyword::Delete},       {"do", Keyword::Do},                 {"else", Keyword::Else},
    {"enum", Keyword::Enum},           {"export", Keyword::Export},         {"extends", Keyword::Extends},
    {"false", Keyword::False},         {"finally", Keyword::Finally},       {"for", Keyword::For},
    {"from", Keyword::From},           {"function", Keyword::Function},     {"get", Keyword::Get},
    {"global", Keyword::Global},       {"if", Keyword::If},                 {"implements", Keyword::Implements},
    {"import", Keyword::Import},       {"in", Keyword::In},                 {"infer", Keyword::Infer},
    {"instanceof", Keyword::Instanceof}, {"interface", Keyword::Interface}, {"is", Keyword::Is},
    {"keyof", Keyword::Keyof},         {"let", Keyword::Let},               {"module", Keyword::Module},
    {"namespace", Keyword::Namespace}, {"new", Keyword::New},               {"null", Keyword::Null},
    {"of", Keyword::Of},               {"override", Keyword::Override},     {"private", Keyword::Private},
    {"protected", Keyword::Protected}, {"public", Keyword::Public},         {"readonly", Keyword::Readonly},
    {"require", Keyword::Require},     {"return", Keyword::Return},         {"satisfies", Keyword::Satisfies},
    {"set", Keyword::Set},             {"static", Keyword::Static},         {"super", Keyword::Super},
    {"switch", Keyword::Switch},       {"this", Keyword::This},             {"throw", Keyword::Throw},
    {"true", Keyword::True},           {"try", Keyword::Try},               {"type", Keyword::Type},
    {"typeof", Keyword::Typeof},       {"unique", Keyword::Unique},         {"using", Keyword::Using},
    {"var", Keyword::Var},             {"void", Keyword::Void},             {"while", Keyword::While},
    {"with", Keyword::With},           {"yield", Keyword::Yield},
};

struct LetterRange {
    std::uint8_t begin = 0;
    std::uint8_t end = 0;
};

constexpr std::array<LetterRange, 26> kLetters = [] {
    std::array<LetterRange, 26> ranges{};
    for (std::size_t i = 0; i < std::size(kKeywords); ++i) {
        const auto letter = static_cast<std::size_t>(kKeywords[i].text[0] - 'a');
        if (ranges[letter].end == 0) {
            ranges[letter].begin = static_cast<std::uint8_t>(i);
        }
        ranges[letter].end = static_cast<std::uint8_t>(i + 1);
    }
    return ranges;
}();

Keyword keywordOf(std::string_view name) noexcept
{
    if (name.size() < 2 || name[0] < 'a' || name[0] > 'z') {
        return Keyword::None;
    }
    const LetterRange range = kLetters[static_cast<std::size_t>(name[0] - 'a')];
    for (std::size_t i = range.begin; i < range.end; ++i) {
        if (kKeywords[i].text == name) {
            return kKeywords[i].keyword;
        }
    }
    return Keyword::None;
}

}

CodePoint decodeUtf8(std::string_view text, std::size_t at) noexcept
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

bool isLineTerminator(std::uint32_t value) noexcept
{
    return value == '\n' || value == '\r' || value == 0x2028 || value == 0x2029;
}

bool isSpace(std::uint32_t value) noexcept
{
    return value == ' ' || value == '\t' || value == 0x0B || value == 0x0C || value == 0xA0 || value == 0xFEFF ||
           value == 0x1680 || (value >= 0x2000 && value <= 0x200A) || value == 0x202F || value == 0x205F ||
           value == 0x3000;
}

bool isReserved(Keyword keyword) noexcept
{
    switch (keyword) {
    case Keyword::Break:
    case Keyword::Case:
    case Keyword::Catch:
    case Keyword::Class:
    case Keyword::Const:
    case Keyword::Continue:
    case Keyword::Debugger:
    case Keyword::Default:
    case Keyword::Delete:
    case Keyword::Do:
    case Keyword::Else:
    case Keyword::Enum:
    case Keyword::Export:
    case Keyword::Extends:
    case Keyword::False:
    case Keyword::Finally:
    case Keyword::For:
    case Keyword::Function:
    case Keyword::If:
    case Keyword::Import:
    case Keyword::In:
    case Keyword::Instanceof:
    case Keyword::New:
    case Keyword::Null:
    case Keyword::Return:
    case Keyword::Super:
    case Keyword::Switch:
    case Keyword::This:
    case Keyword::Throw:
    case Keyword::True:
    case Keyword::Try:
    case Keyword::Typeof:
    case Keyword::Var:
    case Keyword::Void:
    case Keyword::While:
    case Keyword::With:
        return true;
    default:
        return false;
    }
}

std::string_view lexErrorMessage(LexError error) noexcept
{
    switch (error) {
    case LexError::None:
        return "syntax error";
    case LexError::UnterminatedString:
        return "syntax error: unterminated string literal";
    case LexError::UnterminatedTemplate:
        return "syntax error: unterminated template literal";
    case LexError::UnterminatedComment:
        return "syntax error: unterminated comment";
    case LexError::UnterminatedRegex:
        return "syntax error: unterminated regular expression";
    case LexError::InvalidCharacter:
        return "syntax error: invalid character";
    case LexError::InvalidEscape:
        return "syntax error: invalid escape in a name";
    case LexError::InvalidNumber:
        return "syntax error: a name cannot start right after a number";
    }
    return "syntax error";
}

bool Scanner::lineSeparatorAt(std::uint32_t index) const noexcept
{
    return at(index) == 0xE2 && at(index + 1) == 0x80 && (at(index + 2) == 0xA8 || at(index + 2) == 0xA9);
}

void Scanner::skipLine() noexcept
{
    const auto size = static_cast<std::uint32_t>(m_source.size());
    while (m_position < size) {
        const std::uint8_t c = at(m_position);
        if (c == '\n' || c == '\r' || lineSeparatorAt(m_position)) {
            return;
        }
        ++m_position;
    }
}

bool Scanner::skipTrivia(bool& newline) noexcept
{
    const auto size = static_cast<std::uint32_t>(m_source.size());
    while (m_position < size) {
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
        if (c == '/') {
            const std::uint8_t d = at(m_position + 1);
            if (d == '/') {
                skipLine();
                continue;
            }
            if (d == '*') {
                const std::uint32_t start = m_position;
                m_position += 2;
                bool closed = false;
                while (m_position < size) {
                    const std::uint8_t e = at(m_position);
                    if (e == '*' && at(m_position + 1) == '/') {
                        m_position += 2;
                        closed = true;
                        break;
                    }
                    if (e == '\n' || e == '\r' || lineSeparatorAt(m_position)) {
                        newline = true;
                    }
                    ++m_position;
                }
                if (!closed) {
                    m_token = Token{.type = TokenType::Invalid,
                                    .error = LexError::UnterminatedComment,
                                    .newlineBefore = newline,
                                    .start = start,
                                    .end = size};
                    m_position = size;
                    return false;
                }
                continue;
            }
            return true;
        }
        if (c == '#' && m_position == 0 && at(1) == '!') {
            skipLine();
            continue;
        }
        if (c < 0x80) {
            return true;
        }
        const CodePoint point = decodeUtf8(m_source, m_position);
        if (isLineTerminator(point.value)) {
            newline = true;
            m_position += point.length;
            continue;
        }
        if (isSpace(point.value)) {
            m_position += point.length;
            continue;
        }
        return true;
    }
    return true;
}

void Scanner::finish(TokenType type, std::uint32_t length) noexcept
{
    m_position += length;
    m_token.type = type;
    m_token.end = m_position;
}

void Scanner::reshape(TokenType type, std::uint32_t length) noexcept
{
    m_token.type = type;
    m_token.end = m_token.start + length;
    m_position = m_token.end;
}

void Scanner::fail(LexError error) noexcept
{
    const auto size = static_cast<std::uint32_t>(m_source.size());
    m_token.type = TokenType::Invalid;
    m_token.error = error;
    m_token.end = size;
    m_position = size;
}

void Scanner::scanOperator(TokenType one, TokenType oneAssign, TokenType two, TokenType twoAssign) noexcept
{
    const std::uint8_t c = at(m_position);
    const std::uint8_t second = at(m_position + 1);
    if (second == c && two != one) {
        if (twoAssign != two && at(m_position + 2) == '=') {
            return finish(twoAssign, 3);
        }
        return finish(two, 2);
    }
    if (second == '=') {
        return finish(oneAssign, 2);
    }
    finish(one, 1);
}

void Scanner::next() noexcept
{
    bool newline = false;
    if (!skipTrivia(newline)) {
        return;
    }
    m_token = Token{.newlineBefore = newline, .start = m_position, .end = m_position};
    if (m_position >= m_source.size()) {
        return;
    }
    const std::uint8_t c = at(m_position);
    switch (c) {
    case '{':
        return finish(TokenType::LeftBrace, 1);
    case '}':
        return finish(TokenType::RightBrace, 1);
    case '(':
        return finish(TokenType::LeftParen, 1);
    case ')':
        return finish(TokenType::RightParen, 1);
    case '[':
        return finish(TokenType::LeftBracket, 1);
    case ']':
        return finish(TokenType::RightBracket, 1);
    case ';':
        return finish(TokenType::Semicolon, 1);
    case ',':
        return finish(TokenType::Comma, 1);
    case ':':
        return finish(TokenType::Colon, 1);
    case '~':
        return finish(TokenType::Tilde, 1);
    case '@':
        return finish(TokenType::At, 1);
    case '>':
        return finish(TokenType::Greater, 1);
    case '.':
        if (isDigit(at(m_position + 1))) {
            return scanNumber();
        }
        if (at(m_position + 1) == '.' && at(m_position + 2) == '.') {
            return finish(TokenType::Ellipsis, 3);
        }
        return finish(TokenType::Dot, 1);
    case '?':
        if (at(m_position + 1) == '?') {
            return at(m_position + 2) == '=' ? finish(TokenType::QuestionQuestionAssign, 3)
                                             : finish(TokenType::QuestionQuestion, 2);
        }
        if (at(m_position + 1) == '.' && !isDigit(at(m_position + 2))) {
            return finish(TokenType::QuestionDot, 2);
        }
        return finish(TokenType::Question, 1);
    case '=':
        if (at(m_position + 1) == '=') {
            return at(m_position + 2) == '=' ? finish(TokenType::StrictEqual, 3) : finish(TokenType::Equal, 2);
        }
        if (at(m_position + 1) == '>') {
            return finish(TokenType::Arrow, 2);
        }
        return finish(TokenType::Assign, 1);
    case '!':
        if (at(m_position + 1) == '=') {
            return at(m_position + 2) == '=' ? finish(TokenType::StrictNotEqual, 3) : finish(TokenType::NotEqual, 2);
        }
        return finish(TokenType::Bang, 1);
    case '<':
        return scanOperator(TokenType::Less, TokenType::LessEqual, TokenType::LessLess, TokenType::LessLessAssign);
    case '+':
        return scanOperator(TokenType::Plus, TokenType::PlusAssign, TokenType::PlusPlus, TokenType::PlusPlus);
    case '-':
        return scanOperator(TokenType::Minus, TokenType::MinusAssign, TokenType::MinusMinus, TokenType::MinusMinus);
    case '*':
        return scanOperator(TokenType::Star, TokenType::StarAssign, TokenType::StarStar, TokenType::StarStarAssign);
    case '&':
        return scanOperator(TokenType::Amp, TokenType::AmpAssign, TokenType::AmpAmp, TokenType::AmpAmpAssign);
    case '|':
        return scanOperator(TokenType::Bar, TokenType::BarAssign, TokenType::BarBar, TokenType::BarBarAssign);
    case '/':
        return scanOperator(TokenType::Slash, TokenType::SlashAssign, TokenType::Slash, TokenType::Slash);
    case '%':
        return scanOperator(TokenType::Percent, TokenType::PercentAssign, TokenType::Percent, TokenType::Percent);
    case '^':
        return scanOperator(TokenType::Caret, TokenType::CaretAssign, TokenType::Caret, TokenType::Caret);
    case '"':
    case '\'':
        return scanString(c);
    case '`':
        ++m_position;
        return scanTemplate(TokenType::Template, TokenType::TemplateHead);
    case '#': {
        const std::uint8_t d = at(m_position + 1);
        if (isNameStart(d) || d == '\\' || d >= 0x80) {
            ++m_position;
            return scanName(TokenType::PrivateName);
        }
        return fail(LexError::InvalidCharacter);
    }
    default:
        break;
    }
    if (isDigit(c)) {
        return scanNumber();
    }
    if (isNameStart(c) || c == '\\' || c >= 0x80) {
        return scanName(TokenType::Name);
    }
    fail(LexError::InvalidCharacter);
}

bool Scanner::scanEscape() noexcept
{
    if (at(m_position + 1) != 'u') {
        return false;
    }
    std::uint32_t p = m_position + 2;
    if (at(p) == '{') {
        ++p;
        const std::uint32_t digits = p;
        while (isHexDigit(at(p))) {
            ++p;
        }
        if (p == digits || at(p) != '}') {
            return false;
        }
        m_position = p + 1;
        return true;
    }
    for (std::uint32_t i = 0; i < 4; ++i) {
        if (!isHexDigit(at(p + i))) {
            return false;
        }
    }
    m_position = p + 4;
    return true;
}

void Scanner::scanName(TokenType type) noexcept
{
    const auto size = static_cast<std::uint32_t>(m_source.size());
    const std::uint32_t nameStart = m_position;
    bool escaped = false;
    while (m_position < size) {
        const std::uint8_t c = at(m_position);
        if (c < 0x80) {
            if (kNamePart[c]) {
                ++m_position;
                continue;
            }
            if (c == '\\') {
                if (!scanEscape()) {
                    return fail(LexError::InvalidEscape);
                }
                escaped = true;
                continue;
            }
            break;
        }
        const CodePoint point = decodeUtf8(m_source, m_position);
        if (isSpace(point.value) || isLineTerminator(point.value)) {
            break;
        }
        m_position += point.length;
    }
    m_token.type = type;
    m_token.end = m_position;
    m_token.escaped = escaped;
    if (type == TokenType::Name && !escaped) {
        m_token.keyword = keywordOf(m_source.substr(nameStart, m_position - nameStart));
    }
}

void Scanner::scanNumber() noexcept
{
    const auto digitsOrSeparator = [this] {
        while (isDigit(at(m_position)) || at(m_position) == '_') {
            ++m_position;
        }
    };
    const std::uint8_t radix = static_cast<std::uint8_t>(at(m_position + 1) | 0x20);
    if (at(m_position) == '0' && (radix == 'x' || radix == 'o' || radix == 'b')) {
        m_position += 2;
        while (isHexDigit(at(m_position)) || at(m_position) == '_') {
            ++m_position;
        }
    } else {
        digitsOrSeparator();
        if (at(m_position) == '.') {
            ++m_position;
            digitsOrSeparator();
        }
        if ((at(m_position) | 0x20) == 'e') {
            std::uint32_t p = m_position + 1;
            if (at(p) == '+' || at(p) == '-') {
                ++p;
            }
            if (isDigit(at(p))) {
                m_position = p;
                digitsOrSeparator();
            }
        }
    }
    if (at(m_position) == 'n') {
        ++m_position;
    }
    if (kNamePart[at(m_position)] || at(m_position) == '\\') {
        return fail(LexError::InvalidNumber);
    }
    m_token.type = TokenType::Number;
    m_token.end = m_position;
}

void Scanner::scanString(std::uint8_t quote) noexcept
{
    const auto size = static_cast<std::uint32_t>(m_source.size());
    ++m_position;
    while (m_position < size) {
        const std::uint8_t c = at(m_position);
        if (c == quote) {
            ++m_position;
            m_token.type = TokenType::String;
            m_token.end = m_position;
            return;
        }
        if (c == '\\') {
            if (at(m_position + 1) == '\r' && at(m_position + 2) == '\n') {
                m_position += 3;
            } else {
                m_position += 2;
            }
            continue;
        }
        if (c == '\n' || c == '\r') {
            break;
        }
        ++m_position;
    }
    fail(LexError::UnterminatedString);
}

void Scanner::scanTemplate(TokenType closed, TokenType open) noexcept
{
    const auto size = static_cast<std::uint32_t>(m_source.size());
    while (m_position < size) {
        const std::uint8_t c = at(m_position);
        if (c == '`') {
            ++m_position;
            m_token.type = closed;
            m_token.end = m_position;
            return;
        }
        if (c == '$' && at(m_position + 1) == '{') {
            m_position += 2;
            m_token.type = open;
            m_token.end = m_position;
            return;
        }
        if (c == '\\') {
            m_position = std::min(m_position + 2, size);
            continue;
        }
        ++m_position;
    }
    fail(LexError::UnterminatedTemplate);
}

void Scanner::rescanRegex() noexcept
{
    const auto size = static_cast<std::uint32_t>(m_source.size());
    std::uint32_t p = m_token.start + 1;
    bool inClass = false;
    while (p < size) {
        const std::uint8_t c = at(p);
        if (c == '\n' || c == '\r' || lineSeparatorAt(p)) {
            break;
        }
        if (c == '\\') {
            const std::uint8_t d = at(p + 1);
            if (d == '\n' || d == '\r' || lineSeparatorAt(p + 1)) {
                break;
            }
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
            while (p < size && kNamePart[at(p)]) {
                ++p;
            }
            m_position = p;
            m_token.type = TokenType::Regex;
            m_token.end = p;
            return;
        }
        ++p;
    }
    fail(LexError::UnterminatedRegex);
}

void Scanner::rescanTemplateContinuation() noexcept
{
    m_position = m_token.start + 1;
    scanTemplate(TokenType::TemplateTail, TokenType::TemplateMiddle);
}

void Scanner::rescanGreater() noexcept
{
    if (m_token.type != TokenType::Greater) {
        return;
    }
    const std::uint32_t p = m_token.start;
    if (at(p + 1) == '>') {
        if (at(p + 2) == '>') {
            return at(p + 3) == '=' ? reshape(TokenType::GreaterGreaterGreaterAssign, 4)
                                    : reshape(TokenType::GreaterGreaterGreater, 3);
        }
        return at(p + 2) == '=' ? reshape(TokenType::GreaterGreaterAssign, 3) : reshape(TokenType::GreaterGreater, 2);
    }
    if (at(p + 1) == '=') {
        reshape(TokenType::GreaterEqual, 2);
    }
}

void Scanner::rescanLess() noexcept
{
    if (m_token.type == TokenType::LessLess || m_token.type == TokenType::LessLessAssign) {
        reshape(TokenType::Less, 1);
    }
}

void Scanner::forceEof() noexcept
{
    const auto size = static_cast<std::uint32_t>(m_source.size());
    m_position = size;
    m_token = Token{.start = size, .end = size};
}

}
