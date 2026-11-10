#include "Types.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace gs::blueprint {

namespace {

bool wordStart(char c) noexcept
{
    const auto value = static_cast<unsigned char>(c);
    return std::isalpha(value) != 0 || c == '_' || c == '$' || value >= 0x80;
}

bool wordPart(char c) noexcept
{
    const auto value = static_cast<unsigned char>(c);
    return std::isalnum(value) != 0 || c == '_' || c == '$' || value >= 0x80;
}

std::string_view trimmed(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'
                             || text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'
                             || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

class Reader
{
public:
    explicit Reader(std::string_view text) noexcept : m_text(text) {}

    [[nodiscard]] bool done() const noexcept { return m_at >= m_text.size(); }
    [[nodiscard]] std::size_t at() const noexcept { return m_at; }
    [[nodiscard]] char peek(std::size_t ahead = 0) const noexcept
    {
        return m_at + ahead < m_text.size() ? m_text[m_at + ahead] : '\0';
    }
    void seek(std::size_t at) noexcept { m_at = std::min(at, m_text.size()); }
    void advance() noexcept { m_at = std::min(m_at + 1, m_text.size()); }

    void skipTrivia() noexcept
    {
        while (m_at < m_text.size()) {
            const char c = m_text[m_at];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++m_at;
                continue;
            }
            if (c == '/' && peek(1) == '/') {
                m_at += 2;
                while (m_at < m_text.size() && m_text[m_at] != '\n' && m_text[m_at] != '\r') {
                    ++m_at;
                }
                continue;
            }
            if (c == '/' && peek(1) == '*') {
                m_at += 2;
                while (m_at + 1 < m_text.size() && !(m_text[m_at] == '*' && m_text[m_at + 1] == '/')) {
                    ++m_at;
                }
                m_at = std::min(m_at + 2, m_text.size());
                continue;
            }
            break;
        }
    }

    void skipString() noexcept
    {
        const char quote = peek();
        if (quote != '"' && quote != '\'' && quote != '`') {
            advance();
            return;
        }
        advance();
        while (m_at < m_text.size()) {
            const char c = m_text[m_at];
            if (c == '\\') {
                m_at = std::min(m_at + 2, m_text.size());
                continue;
            }
            ++m_at;
            if (c == quote) {
                return;
            }
        }
    }

    [[nodiscard]] std::string identifier() noexcept
    {
        std::string out;
        while (m_at < m_text.size() && wordPart(m_text[m_at])) {
            out.push_back(m_text[m_at]);
            ++m_at;
        }
        return out;
    }

    [[nodiscard]] std::string stringValue() noexcept
    {
        const char quote = peek();
        if (quote != '"' && quote != '\'') {
            return {};
        }
        advance();
        std::string out;
        while (m_at < m_text.size()) {
            const char c = m_text[m_at];
            if (c == '\\' && m_at + 1 < m_text.size()) {
                out.push_back(m_text[m_at + 1]);
                m_at += 2;
                continue;
            }
            ++m_at;
            if (c == quote) {
                break;
            }
            out.push_back(c);
        }
        return out;
    }

    [[nodiscard]] bool word(std::string_view wanted) noexcept
    {
        skipTrivia();
        if (m_at + wanted.size() > m_text.size() || m_text.substr(m_at, wanted.size()) != wanted) {
            return false;
        }
        const std::size_t after = m_at + wanted.size();
        if (after < m_text.size() && wordPart(m_text[after])) {
            return false;
        }
        m_at = after;
        return true;
    }

    [[nodiscard]] bool punctuator(char wanted) noexcept
    {
        skipTrivia();
        if (peek() != wanted) {
            return false;
        }
        advance();
        return true;
    }

    [[nodiscard]] std::string typeText() noexcept
    {
        std::string out;
        int depth = 0;
        while (m_at < m_text.size()) {
            const char c = m_text[m_at];
            if (c == '/' && peek(1) == '/') {
                break;
            }
            if (c == '/' && peek(1) == '*') {
                break;
            }
            if (depth == 0 && (c == ';' || c == ',' || c == '}')) {
                break;
            }
            if (depth == 0 && (c == '\n' || c == '\r') && endsType(out)) {
                break;
            }
            if (c == '<' || c == '(' || c == '[' || c == '{') {
                ++depth;
            } else if (c == '>' || c == ')' || c == ']' || c == '}') {
                depth = std::max(0, depth - 1);
            }
            out.push_back(c);
            ++m_at;
        }
        return std::string(trimmed(out));
    }

private:
    [[nodiscard]] bool endsType(std::string_view sofar) const noexcept
    {
        const std::string_view text = trimmed(sofar);
        if (text.empty()) {
            return false;
        }
        const char last = text.back();
        if (last == '|' || last == '&' || last == '<' || last == ',' || last == ':' || last == '(' || last == '=') {
            return false;
        }
        if (text.ends_with("=>")) {
            return false;
        }
        std::size_t next = m_at;
        while (next < m_text.size()
               && (m_text[next] == ' ' || m_text[next] == '\t' || m_text[next] == '\r' || m_text[next] == '\n')) {
            ++next;
        }
        return next >= m_text.size() || (m_text[next] != '|' && m_text[next] != '&');
    }

    std::string_view m_text;
    std::size_t m_at = 0;
};

bool readFields(Reader& reader, std::vector<StructFieldInfo>& fields)
{
    while (true) {
        reader.skipTrivia();
        if (reader.done()) {
            return false;
        }
        const char c = reader.peek();
        if (c == '}') {
            reader.advance();
            return true;
        }
        if (c == ';' || c == ',') {
            reader.advance();
            continue;
        }
        if (c != '"' && c != '\'' && !wordStart(c)) {
            return false;
        }
        StructFieldInfo field;
        if (c == '"' || c == '\'') {
            field.name = reader.stringValue();
        } else {
            field.name = reader.identifier();
        }
        if (field.name.empty()) {
            return false;
        }
        reader.skipTrivia();
        if (reader.peek() == '?') {
            field.optional = true;
            reader.advance();
        }
        if (!reader.punctuator(':')) {
            return false;
        }
        field.type = reader.typeText();
        if (field.type.empty()) {
            return false;
        }
        fields.push_back(std::move(field));
    }
}

bool readStruct(Reader& reader, std::size_t start, StructInfo& info, bool alias)
{
    reader.skipTrivia();
    if (!wordStart(reader.peek())) {
        return false;
    }
    info.name = reader.identifier();
    if (info.name.empty()) {
        return false;
    }
    reader.skipTrivia();
    if (reader.peek() == '<') {
        return false;
    }
    if (alias) {
        if (!reader.punctuator('=')) {
            return false;
        }
    }
    if (!reader.punctuator('{')) {
        return false;
    }
    if (!readFields(reader, info.fields)) {
        return false;
    }
    reader.skipTrivia();
    if (reader.peek() == ';') {
        reader.advance();
    }
    info.start = static_cast<std::uint32_t>(start);
    info.end = static_cast<std::uint32_t>(reader.at());
    info.alias = alias;
    return true;
}

bool readEnum(Reader& reader, std::size_t start, EnumInfo& info)
{
    reader.skipTrivia();
    if (!wordStart(reader.peek())) {
        return false;
    }
    info.name = reader.identifier();
    if (info.name.empty()) {
        return false;
    }
    reader.skipTrivia();
    if (reader.peek() == '<' || !reader.punctuator('=')) {
        return false;
    }
    if (!reader.punctuator('{')) {
        return false;
    }
    std::vector<std::string> options;
    while (true) {
        reader.skipTrivia();
        if (reader.done()) {
            return false;
        }
        const char c = reader.peek();
        if (c == '}') {
            reader.advance();
            break;
        }
        if (c == ',') {
            reader.advance();
            continue;
        }
        std::string key;
        if (c == '"' || c == '\'') {
            key = reader.stringValue();
        } else if (wordStart(c)) {
            key = reader.identifier();
        } else {
            return false;
        }
        if (key.empty() || !reader.punctuator(':')) {
            return false;
        }
        reader.skipTrivia();
        if (reader.peek() != '"' && reader.peek() != '\'') {
            return false;
        }
        if (reader.stringValue() != key) {
            return false;
        }
        options.push_back(std::move(key));
    }
    if (options.empty() || !reader.word("as") || !reader.word("const")) {
        return false;
    }
    reader.skipTrivia();
    if (reader.peek() == ';') {
        reader.advance();
    }
    const std::size_t afterConst = reader.at();
    reader.skipTrivia();
    if (reader.word("type")) {
        reader.skipTrivia();
        const std::string alias = reader.identifier();
        reader.skipTrivia();
        if (alias == info.name && reader.peek() == '=') {
            while (!reader.done() && reader.peek() != '\n' && reader.peek() != '\r') {
                reader.advance();
            }
        } else {
            reader.seek(afterConst);
        }
    } else {
        reader.seek(afterConst);
    }
    info.options = std::move(options);
    info.start = static_cast<std::uint32_t>(start);
    info.end = static_cast<std::uint32_t>(reader.at());
    return true;
}

}

UserTypes scanTypes(std::string_view text)
{
    UserTypes types;
    Reader reader(text);
    int depth = 0;
    while (!reader.done()) {
        reader.skipTrivia();
        if (reader.done()) {
            break;
        }
        const char c = reader.peek();
        if (c == '"' || c == '\'' || c == '`') {
            reader.skipString();
            continue;
        }
        if (c == '{' || c == '(' || c == '[') {
            ++depth;
            reader.advance();
            continue;
        }
        if (c == '}' || c == ')' || c == ']') {
            depth = std::max(0, depth - 1);
            reader.advance();
            continue;
        }
        if (!wordStart(c)) {
            reader.advance();
            continue;
        }
        const std::size_t start = reader.at();
        const std::string word = reader.identifier();
        if (depth != 0) {
            continue;
        }
        if (word == "interface") {
            StructInfo info;
            if (readStruct(reader, start, info, false)) {
                types.structs.push_back(std::move(info));
            }
            continue;
        }
        if (word == "type") {
            StructInfo info;
            if (readStruct(reader, start, info, true)) {
                types.structs.push_back(std::move(info));
            }
            continue;
        }
        if (word == "const") {
            EnumInfo info;
            if (readEnum(reader, start, info)) {
                types.enums.push_back(std::move(info));
            }
            continue;
        }
    }
    return types;
}

const StructInfo* structNamed(const UserTypes& types, std::string_view name)
{
    const auto found = std::ranges::find(types.structs, name, &StructInfo::name);
    return found == types.structs.end() ? nullptr : &*found;
}

const EnumInfo* enumNamed(const UserTypes& types, std::string_view name)
{
    const auto found = std::ranges::find(types.enums, name, &EnumInfo::name);
    return found == types.enums.end() ? nullptr : &*found;
}

const StructFieldInfo* fieldNamed(const StructInfo& info, std::string_view name)
{
    const auto found = std::ranges::find(info.fields, name, &StructFieldInfo::name);
    return found == info.fields.end() ? nullptr : &*found;
}

std::string structDeclaration(std::string_view name, const std::vector<StructFieldInfo>& fields,
                              std::string_view indent, std::string_view lineBreak)
{
    std::string out = "interface ";
    out.append(name);
    out.append(" {");
    out.append(lineBreak);
    for (const StructFieldInfo& field : fields) {
        out.append(indent);
        out.append(field.name);
        if (field.optional) {
            out.push_back('?');
        }
        out.append(": ");
        out.append(field.type.empty() ? std::string_view("unknown") : std::string_view(field.type));
        out.append(lineBreak);
    }
    out.append("}");
    out.append(lineBreak);
    return out;
}

std::string enumDeclaration(std::string_view name, const std::vector<std::string>& options, std::string_view indent,
                            std::string_view lineBreak, char quote)
{
    const std::string quoted = std::string(1, quote);
    std::string out = "const ";
    out.append(name);
    out.append(" = {");
    out.append(lineBreak);
    for (const std::string& option : options) {
        out.append(indent);
        out.append(option);
        out.append(": ");
        out.append(quoted);
        out.append(option);
        out.append(quoted);
        out.append(",");
        out.append(lineBreak);
    }
    out.append("} as const");
    out.append(lineBreak);
    out.append(enumTypeLine(name));
    out.append(lineBreak);
    return out;
}

std::string enumTypeLine(std::string_view name)
{
    std::string out = "type ";
    out.append(name);
    out.append(" = typeof ");
    out.append(name);
    out.append("[keyof typeof ");
    out.append(name);
    out.append("]");
    return out;
}

}
