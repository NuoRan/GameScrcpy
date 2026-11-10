#include "params.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <system_error>

namespace feel {

namespace {

constexpr int kMostDepth = 32;

class Reader
{
public:
    explicit Reader(std::string_view text) noexcept
        : m_text(text)
    {
    }

    [[nodiscard]] bool value(Value& out, int depth)
    {
        space();
        if (depth > kMostDepth || m_at >= m_text.size()) {
            return false;
        }
        const char next = m_text[m_at];
        if (next == '{') {
            return object(out, depth + 1);
        }
        if (next == '[') {
            return list(out, depth + 1);
        }
        if (next == '"') {
            out.kind = Value::Kind::Text;
            return text(out.text);
        }
        if (literal("true")) {
            out.kind = Value::Kind::Boolean;
            out.flag = true;
            return true;
        }
        if (literal("false")) {
            out.kind = Value::Kind::Boolean;
            out.flag = false;
            return true;
        }
        if (literal("null")) {
            out.kind = Value::Kind::Null;
            return true;
        }
        out.kind = Value::Kind::Number;
        return number(out.number);
    }

    [[nodiscard]] bool finished()
    {
        space();
        return m_at == m_text.size();
    }

private:
    void space() noexcept
    {
        while (m_at < m_text.size() &&
               (m_text[m_at] == ' ' || m_text[m_at] == '\t' || m_text[m_at] == '\n' || m_text[m_at] == '\r')) {
            ++m_at;
        }
    }

    [[nodiscard]] bool take(char wanted) noexcept
    {
        space();
        if (m_at < m_text.size() && m_text[m_at] == wanted) {
            ++m_at;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool literal(std::string_view word) noexcept
    {
        if (m_text.substr(m_at, word.size()) == word) {
            m_at += word.size();
            return true;
        }
        return false;
    }

    [[nodiscard]] bool object(Value& out, int depth)
    {
        out.kind = Value::Kind::Object;
        ++m_at;
        if (take('}')) {
            return true;
        }
        do {
            space();
            std::string key;
            if (!text(key) || !take(':')) {
                return false;
            }
            Value member;
            if (!value(member, depth)) {
                return false;
            }
            out.members.emplace_back(std::move(key), std::move(member));
        } while (take(','));
        return take('}');
    }

    [[nodiscard]] bool list(Value& out, int depth)
    {
        out.kind = Value::Kind::List;
        ++m_at;
        if (take(']')) {
            return true;
        }
        do {
            Value item;
            if (!value(item, depth)) {
                return false;
            }
            out.items.push_back(std::move(item));
        } while (take(','));
        return take(']');
    }

    [[nodiscard]] bool hex(std::uint32_t& out) noexcept
    {
        if (m_text.size() - m_at < 4) {
            return false;
        }
        const char* first = m_text.data() + m_at;
        const auto [end, error] = std::from_chars(first, first + 4, out, 16);
        if (error != std::errc{} || end != first + 4) {
            return false;
        }
        m_at += 4;
        return true;
    }

    static void append(std::string& out, std::uint32_t code)
    {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    [[nodiscard]] bool text(std::string& out)
    {
        if (m_at >= m_text.size() || m_text[m_at] != '"') {
            return false;
        }
        ++m_at;
        while (m_at < m_text.size()) {
            const char c = m_text[m_at++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (m_at >= m_text.size()) {
                return false;
            }
            const char escaped = m_text[m_at++];
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                out += escaped;
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                std::uint32_t code = 0;
                if (!hex(code)) {
                    return false;
                }
                if (code >= 0xD800 && code < 0xDC00 && literal("\\u")) {
                    std::uint32_t low = 0;
                    if (!hex(low) || low < 0xDC00 || low >= 0xE000) {
                        return false;
                    }
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                }
                append(out, code);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    [[nodiscard]] bool number(double& out) noexcept
    {
        const char* first = m_text.data() + m_at;
        const char* last = m_text.data() + m_text.size();
        const auto [end, error] = std::from_chars(first, last, out);
        if (error != std::errc{} || end == first) {
            return false;
        }
        m_at += static_cast<std::size_t>(end - first);
        return true;
    }

    std::string_view m_text;
    std::size_t m_at = 0;
};

}

std::optional<Value> parseJson(std::string_view json)
{
    Reader reader(json);
    Value root;
    if (!reader.value(root, 0) || !reader.finished()) {
        return std::nullopt;
    }
    return root;
}

Params::Params(std::string_view json)
{
    if (std::optional<Value> parsed = parseJson(json); parsed && parsed->kind == Value::Kind::Object) {
        m_root = std::move(*parsed);
    }
}

const Value* Params::find(std::string_view key) const
{
    for (const auto& [name, member] : m_root.members) {
        if (name == key) {
            return &member;
        }
    }
    return nullptr;
}

double Params::number(std::string_view key, double fallback) const
{
    const Value* found = find(key);
    return found != nullptr && found->kind == Value::Kind::Number ? found->number : fallback;
}

bool Params::flag(std::string_view key, bool fallback) const
{
    const Value* found = find(key);
    return found != nullptr && found->kind == Value::Kind::Boolean ? found->flag : fallback;
}

std::optional<Point> Params::point(std::string_view key) const
{
    const Value* found = find(key);
    if (found == nullptr || found->kind != Value::Kind::List || found->items.size() < 2 ||
        found->items[0].kind != Value::Kind::Number || found->items[1].kind != Value::Kind::Number) {
        return std::nullopt;
    }
    return Point{found->items[0].number, found->items[1].number};
}

std::vector<Point> Params::points(std::string_view key) const
{
    std::vector<Point> made;
    const Value* found = find(key);
    if (found == nullptr || found->kind != Value::Kind::List) {
        return made;
    }
    for (const Value& item : found->items) {
        if (item.kind != Value::Kind::List || item.items.size() != 2 || item.items[0].kind != Value::Kind::Number ||
            item.items[1].kind != Value::Kind::Number) {
            return {};
        }
        made.push_back({item.items[0].number, item.items[1].number});
    }
    return made;
}

}
