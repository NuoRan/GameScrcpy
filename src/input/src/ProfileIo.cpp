#include "gs/input/Profile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <utility>

namespace gs::input {

namespace {

using Json = nlohmann::ordered_json;

constexpr std::size_t kMaxProfileBytes = 1024 * 1024;
constexpr int kMaxNesting = 64;
constexpr int kVersionWithCommands = 2;

bool isIdentifier(std::string_view text) noexcept
{
    return !text.empty() && text.size() <= 64 && std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

std::size_t codePointCount(std::string_view utf8) noexcept
{
    return static_cast<std::size_t>(std::count_if(utf8.begin(), utf8.end(), [](char c) {
        return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U;
    }));
}

bool nestingWithinLimit(std::string_view text) noexcept
{
    int depth = 0;
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
            if (++depth > kMaxNesting) {
                return false;
            }
        } else if (c == '}' || c == ']') {
            --depth;
        }
    }
    return true;
}

template <std::size_t N>
std::optional<std::size_t> nameIndex(const std::array<std::string_view, N>& names, std::string_view name)
{
    for (std::size_t i = 0; i < N; ++i) {
        if (names[i] == name) {
            return i;
        }
    }
    return std::nullopt;
}

template <typename Variant, std::size_t... I>
void emplaceIndex(Variant& variant, std::size_t index, std::index_sequence<I...>)
{
    static_cast<void>(((index == I ? (variant.template emplace<I>(), true) : false) || ...));
}

bool isTwoDimensional(SlotKind slot) noexcept
{
    return slot == SlotKind::Axis2D || slot == SlotKind::HeldAxis2D;
}

bool accepts(SlotKind slot, InputSource source) noexcept
{
    return source.device == Device::Extension || isTwoDimensional(slot) == (kindOf(source) == SourceKind::Axis2D);
}

bool contains(const std::vector<std::string>& keys, const std::string& key)
{
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

bool overlaps(Claim a, Claim b) noexcept
{
    return a == b || a == Claim::Both || b == Claim::Both;
}

SlotKind slotKindOf(const Json& binding)
{
    const Json* input = &binding;
    if (binding.is_object()) {
        const auto found = binding.find("input");
        input = found != binding.end() ? &*found : nullptr;
    }
    if (input == nullptr || !input->is_string()) {
        return SlotKind::Digital;
    }
    const std::optional<Chord> chord = parseChord(input->get_ref<const std::string&>());
    return chord && kindOf(chord->source) == SourceKind::Axis2D ? SlotKind::Axis2D : SlotKind::Digital;
}

bool hyphenatedWord(std::string_view part) noexcept
{
    return !part.empty() && part.front() != '-' && part.back() != '-' && part.find("--") == std::string_view::npos &&
           std::ranges::all_of(part, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

struct Context {
    std::vector<Diagnostic>& diagnostics;
    std::size_t errors = 0;

    void report(Severity severity, DiagnosticCode code, std::string path, std::string detail = {})
    {
        if (severity == Severity::Error) {
            ++errors;
        }
        diagnostics.push_back(Diagnostic{severity, code, std::move(path), std::move(detail)});
    }
};

class Reader
{
public:
    Reader(const Json& node, std::string path, Context& context)
        : m_node(node)
        , m_path(std::move(path))
        , m_context(context)
    {
    }

    void finish()
    {
        for (auto it = m_node.begin(); it != m_node.end(); ++it) {
            if (!contains(m_visited, it.key())) {
                m_context.report(Severity::Warning, DiagnosticCode::UnknownField, pathOf(it.key()));
            }
        }
    }

    void constantText(std::string_view name, std::string_view expected)
    {
        const Json* value = required(name);
        if (value && (!value->is_string() || value->get_ref<const std::string&>() != expected)) {
            m_context.report(Severity::Error, DiagnosticCode::WrongFormat, pathOf(name), value->dump());
        }
    }

    void retired(std::string_view name)
    {
        if (!contains(m_visited, std::string(name))) {
            m_visited.emplace_back(name);
        }
    }

    void constantInteger(std::string_view name, int expected)
    {
        const Json* value = required(name);
        if (value && !(value->is_number_integer() && value->get<std::int64_t>() == expected)) {
            m_context.report(Severity::Error, DiagnosticCode::WrongVersion, pathOf(name), value->dump());
        }
    }

    void text(std::string_view name, std::string& field, std::size_t maxChars)
    {
        if (const Json* value = optional(name)) {
            readText(name, *value, field, maxChars);
        }
    }

    void requiredText(std::string_view name, std::string& field, std::size_t maxChars)
    {
        if (const Json* value = required(name)) {
            readText(name, *value, field, maxChars);
        }
    }

    void optionalText(std::string_view name, std::string& field, std::size_t maxChars)
    {
        if (const Json* value = find(name)) {
            readText(name, *value, field, maxChars);
        }
    }

    void extension(std::string_view name, std::string& component, std::vector<NamedBinding>& keys,
                   std::vector<NamedValue>& props)
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        readText(name, *value, component, kComponentRefLength);
        if (value->is_string() && value->get_ref<const std::string&>().empty()) {
            m_context.report(Severity::Error, DiagnosticCode::InvalidValue, pathOf(name), value->dump());
        }
        keys = std::move(m_extraKeys);
        valueTable("props", props);
    }

    void identifier(std::string_view name, std::string& field)
    {
        if (const Json* value = required(name)) {
            readIdentifier(name, *value, field);
        }
    }

    void optionalIdentifier(std::string_view name, std::string& field)
    {
        if (const Json* value = find(name)) {
            readIdentifier(name, *value, field);
        }
    }

    void identifierList(std::string_view name, std::vector<std::string>& field)
    {
        const Json* value = optional(name);
        if (!value) {
            return;
        }
        if (!value->is_array()) {
            wrongType(pathOf(name), *value);
            return;
        }
        field.clear();
        for (std::size_t i = 0; i < value->size(); ++i) {
            const Json& item = (*value)[i];
            if (!item.is_string() || !isIdentifier(item.get_ref<const std::string&>())) {
                m_context.report(Severity::Error, DiagnosticCode::InvalidValue, indexPath(name, i), item.dump());
                continue;
            }
            field.push_back(item.get<std::string>());
        }
    }

    template <typename E, std::size_t N>
    void choice(std::string_view name, E& field, const std::array<std::string_view, N>& names)
    {
        if (const Json* value = optional(name)) {
            if (const auto chosen = readChoice<E>(pathOf(name), *value, names)) {
                field = *chosen;
            }
        }
    }

    template <typename E, std::size_t N>
    void optionalChoice(std::string_view name, std::optional<E>& field, const std::array<std::string_view, N>& names)
    {
        if (const Json* value = find(name)) {
            field = readChoice<E>(pathOf(name), *value, names);
        }
    }

    template <typename T>
    void integer(std::string_view name, T& field, std::int64_t min, std::int64_t max)
    {
        const Json* value = optional(name);
        if (!value) {
            return;
        }
        if (!value->is_number_integer()) {
            wrongType(pathOf(name), *value);
            return;
        }
        const bool huge = value->is_number_unsigned() && value->get<std::uint64_t>() > static_cast<std::uint64_t>(max);
        if (huge || value->get<std::int64_t>() < min || value->get<std::int64_t>() > max) {
            m_context.report(Severity::Error, DiagnosticCode::OutOfRange, pathOf(name), value->dump());
            return;
        }
        field = static_cast<T>(value->get<std::int64_t>());
    }

    void number(std::string_view name, double& field, double min, double max)
    {
        const Json* value = optional(name);
        if (!value) {
            return;
        }
        if (const auto number = readNumber(pathOf(name), *value, min, max)) {
            field = *number;
        }
    }

    void boolean(std::string_view name, bool& field)
    {
        const Json* value = optional(name);
        if (!value) {
            return;
        }
        if (!value->is_boolean()) {
            wrongType(pathOf(name), *value);
            return;
        }
        field = value->get<bool>();
    }

    void flag(std::string_view name, bool& field)
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        if (!value->is_boolean()) {
            wrongType(pathOf(name), *value);
            return;
        }
        field = value->get<bool>();
    }

    void value(std::string_view name, VarValue& field, std::size_t maxChars)
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        if (value->is_boolean()) {
            field = value->get<bool>();
        } else if (value->is_number()) {
            const double number = value->get<double>();
            if (!std::isfinite(number)) {
                m_context.report(Severity::Error, DiagnosticCode::OutOfRange, pathOf(name), value->dump());
                return;
            }
            field = number;
        } else if (value->is_string()) {
            const std::string& text = value->get_ref<const std::string&>();
            if (codePointCount(text) > maxChars) {
                m_context.report(Severity::Error, DiagnosticCode::OutOfRange, pathOf(name),
                                 std::to_string(codePointCount(text)));
                return;
            }
            field = text;
        } else {
            wrongType(pathOf(name), *value);
        }
    }

    void condition(std::string_view name, std::string& field)
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        if (!value->is_string()) {
            wrongType(pathOf(name), *value);
            return;
        }
        const std::string& text = value->get_ref<const std::string&>();
        const std::string_view state = text.starts_with('!') ? std::string_view(text).substr(1) : std::string_view(text);
        if (!isIdentifier(state)) {
            m_context.report(Severity::Error, DiagnosticCode::InvalidValue, pathOf(name), value->dump());
            return;
        }
        field = text;
    }

    void vec2(std::string_view name, Vec2& field, double min, double max)
    {
        if (const Json* value = optional(name)) {
            if (const auto point = readVec2(pathOf(name), *value, min, max)) {
                field = *point;
            }
        }
    }

    void optionalVec2(std::string_view name, std::optional<Vec2>& field, double min, double max)
    {
        if (const Json* value = find(name)) {
            field = readVec2(pathOf(name), *value, min, max);
        }
    }

    void points(std::string_view name, std::vector<Vec2>& field, std::size_t minCount, double min, double max)
    {
        const Json* value = required(name);
        if (!value) {
            return;
        }
        if (!value->is_array()) {
            wrongType(pathOf(name), *value);
            return;
        }
        if (value->size() < minCount) {
            m_context.report(Severity::Error, DiagnosticCode::TooFewPoints, pathOf(name), std::to_string(value->size()));
            return;
        }
        field.clear();
        for (std::size_t i = 0; i < value->size(); ++i) {
            if (const auto point = readVec2(indexPath(name, i), (*value)[i], min, max)) {
                field.push_back(*point);
            }
        }
    }

    void integerPair(std::string_view name, std::array<std::uint16_t, 2>& field, std::int64_t min, std::int64_t max)
    {
        const Json* value = optional(name);
        if (!value) {
            return;
        }
        const std::string path = pathOf(name);
        if (!value->is_array() || value->size() != 2 || !(*value)[0].is_number_integer() || !(*value)[1].is_number_integer()) {
            wrongType(path, *value);
            return;
        }
        for (std::size_t i = 0; i < 2; ++i) {
            const Json& item = (*value)[i];
            if ((item.is_number_unsigned() && item.get<std::uint64_t>() > static_cast<std::uint64_t>(max)) ||
                item.get<std::int64_t>() < min || item.get<std::int64_t>() > max) {
                m_context.report(Severity::Error, DiagnosticCode::OutOfRange, path, value->dump());
                return;
            }
        }
        field = {static_cast<std::uint16_t>((*value)[0].get<std::int64_t>()),
                 static_cast<std::uint16_t>((*value)[1].get<std::int64_t>())};
    }

    void chord(std::string_view name, Chord& field)
    {
        if (const Json* value = required(name)) {
            if (const auto parsed = readChord(pathOf(name), *value)) {
                field = *parsed;
            }
        }
    }

    void optionalChord(std::string_view name, std::optional<Chord>& field)
    {
        if (const Json* value = find(name)) {
            field = readChord(pathOf(name), *value);
        }
    }

    void binding(std::string_view name, std::optional<Binding>& field, SlotKind slot, Claim fallback = Claim::Lock)
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        const std::string path = pathOf(name);
        const std::size_t errorsBefore = m_context.errors;
        Binding result{.claim = fallback};
        if (value->is_string()) {
            const auto parsed = readChord(path, *value);
            if (!parsed) {
                return;
            }
            result.input = *parsed;
        } else if (value->is_object()) {
            Reader nested(*value, path, m_context);
            Binding::visit(nested, result);
            nested.finish();
            if (m_context.errors != errorsBefore) {
                return;
            }
            if (!result.input) {
                m_context.report(Severity::Error, DiagnosticCode::MissingField, path + ".input");
                return;
            }
            if (value->contains("claim") && !claimable(result.input)) {
                m_context.report(Severity::Error, DiagnosticCode::InputKindMismatch, path + ".claim",
                                 chordName(result.input));
                return;
            }
        } else {
            wrongType(path, *value);
            return;
        }
        if (!accepts(slot, result.input->source)) {
            m_context.report(Severity::Error, DiagnosticCode::InputKindMismatch, path, chordName(result.input));
            return;
        }
        field = result;
    }

    template <typename T>
    void object(std::string_view name, T& field)
    {
        const Json* value = optional(name);
        if (!value) {
            return;
        }
        if (!value->is_object()) {
            wrongType(pathOf(name), *value);
            return;
        }
        Reader nested(*value, pathOf(name), m_context);
        T::visit(nested, field);
        nested.finish();
    }

    void bindingTable(std::string_view name, std::vector<NamedBinding>& field)
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        if (!value->is_object()) {
            wrongType(pathOf(name), *value);
            return;
        }
        field.clear();
        Reader nested(*value, pathOf(name), m_context);
        nested.rest(field);
        nested.finish();
    }

    void rest(std::vector<NamedBinding>& field)
    {
        for (auto it = m_node.begin(); it != m_node.end(); ++it) {
            if (contains(m_visited, it.key())) {
                continue;
            }
            if (!propName(it.key())) {
                m_context.report(Severity::Error, DiagnosticCode::InvalidValue, pathOf(it.key()), it.key());
                retired(it.key());
                continue;
            }
            NamedBinding entry{.name = it.key()};
            binding(it.key(), entry.binding, SlotKind::Digital);
            if (entry.binding) {
                field.push_back(std::move(entry));
            }
        }
    }

    void slotTable(std::string_view name, std::vector<NamedBinding>& field)
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        if (!value->is_object()) {
            wrongType(pathOf(name), *value);
            return;
        }
        field.clear();
        Reader nested(*value, pathOf(name), m_context);
        nested.slotEntries(field);
        nested.finish();
    }

    void slotEntries(std::vector<NamedBinding>& field)
    {
        for (auto it = m_node.begin(); it != m_node.end(); ++it) {
            if (!propName(it.key())) {
                m_context.report(Severity::Error, DiagnosticCode::InvalidValue, pathOf(it.key()), it.key());
                retired(it.key());
                continue;
            }
            NamedBinding entry{.name = it.key()};
            binding(it.key(), entry.binding, slotKindOf(it.value()));
            if (entry.binding) {
                field.push_back(std::move(entry));
            }
        }
    }

    void json(std::string_view name, std::string& field)
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        if (!value->is_object()) {
            wrongType(pathOf(name), *value);
            return;
        }
        if (value->empty()) {
            field.clear();
            return;
        }
        std::string text = value->dump(-1, ' ', false, Json::error_handler_t::replace);
        if (text.size() > kMostModuleParamBytes) {
            m_context.report(Severity::Error, DiagnosticCode::OutOfRange, pathOf(name), std::to_string(text.size()));
            return;
        }
        field = std::move(text);
    }

    void valueTable(std::string_view name, std::vector<NamedValue>& field)
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        if (!value->is_object()) {
            wrongType(pathOf(name), *value);
            return;
        }
        field.clear();
        for (auto it = value->begin(); it != value->end(); ++it) {
            const std::string path = pathOf(name) + "." + it.key();
            const Json& item = it.value();
            if (!propName(it.key())) {
                m_context.report(Severity::Error, DiagnosticCode::InvalidValue, path, it.key());
            } else if (item.is_boolean()) {
                field.push_back({it.key(), item.get<bool>()});
            } else if (item.is_number()) {
                if (const auto number = readNumber(path, item, -1e9, 1e9)) {
                    field.push_back({it.key(), *number});
                }
            } else if (item.is_string()) {
                const std::string& text = item.get_ref<const std::string&>();
                if (codePointCount(text) > kPropTextLength) {
                    m_context.report(Severity::Error, DiagnosticCode::OutOfRange, path, std::to_string(codePointCount(text)));
                } else {
                    field.push_back({it.key(), text});
                }
            } else if (item.is_array()) {
                if (const auto point = readVec2(path, item, -2.0, 2.0)) {
                    field.push_back({it.key(), *point});
                }
            } else {
                wrongType(path, item);
            }
        }
    }

    template <typename Fn>
    void group(std::string_view name, Fn&& visitFields)
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        if (!value->is_object()) {
            wrongType(pathOf(name), *value);
            return;
        }
        Reader nested(*value, pathOf(name), m_context);
        visitFields(nested);
        if (name == "inputs" && m_node.contains("component")) {
            nested.rest(m_extraKeys);
        }
        nested.finish();
    }

    template <typename T>
    void list(std::string_view name, std::vector<T>& field)
    {
        const Json* value = optional(name);
        if (!value) {
            return;
        }
        if (!value->is_array()) {
            wrongType(pathOf(name), *value);
            return;
        }
        field.clear();
        for (std::size_t i = 0; i < value->size(); ++i) {
            const Json& item = (*value)[i];
            const std::string path = indexPath(name, i);
            if (!item.is_object()) {
                wrongType(path, item);
                continue;
            }
            T element{};
            Reader nested(item, path, m_context);
            T::visit(nested, element);
            nested.finish();
            field.push_back(std::move(element));
        }
    }

    template <typename... Alternatives, std::size_t N>
    void variant(std::string_view name, std::variant<Alternatives...>& field, const std::array<std::string_view, N>& names)
    {
        static_assert(N == sizeof...(Alternatives));
        const Json* value = required(name);
        if (!value) {
            return;
        }
        const auto index = value->is_string() ? nameIndex(names, value->get_ref<const std::string&>()) : std::nullopt;
        if (!index) {
            m_context.report(Severity::Error, DiagnosticCode::InvalidValue, pathOf(name), value->dump());
            return;
        }
        emplaceIndex(field, *index, std::index_sequence_for<Alternatives...>{});
        std::visit([this](auto& alternative) { std::decay_t<decltype(alternative)>::visit(*this, alternative); }, field);
    }

private:
    const Json* find(std::string_view name)
    {
        m_visited.emplace_back(name);
        const auto it = m_node.find(std::string(name));
        return it == m_node.end() ? nullptr : &*it;
    }

    const Json* optional(std::string_view name)
    {
        const Json* value = find(name);
        if (!value) {
            m_context.report(Severity::Info, DiagnosticCode::DefaultUsed, pathOf(name));
        }
        return value;
    }

    const Json* required(std::string_view name)
    {
        const Json* value = find(name);
        if (!value) {
            m_context.report(Severity::Error, DiagnosticCode::MissingField, pathOf(name));
        }
        return value;
    }

    void readText(std::string_view name, const Json& value, std::string& field, std::size_t maxChars)
    {
        if (!value.is_string()) {
            wrongType(pathOf(name), value);
            return;
        }
        const std::string& text = value.get_ref<const std::string&>();
        if (codePointCount(text) > maxChars) {
            m_context.report(Severity::Error, DiagnosticCode::OutOfRange, pathOf(name), std::to_string(codePointCount(text)));
            return;
        }
        field = text;
    }

    void readIdentifier(std::string_view name, const Json& value, std::string& field)
    {
        if (!value.is_string() || !isIdentifier(value.get_ref<const std::string&>())) {
            m_context.report(Severity::Error, DiagnosticCode::InvalidValue, pathOf(name), value.dump());
            return;
        }
        field = value.get<std::string>();
    }

    template <typename E, std::size_t N>
    std::optional<E> readChoice(const std::string& path, const Json& value, const std::array<std::string_view, N>& names)
    {
        const auto index = value.is_string() ? nameIndex(names, value.get_ref<const std::string&>()) : std::nullopt;
        if (!index) {
            m_context.report(Severity::Error, DiagnosticCode::InvalidValue, path, value.dump());
            return std::nullopt;
        }
        return static_cast<E>(*index);
    }

    std::optional<double> readNumber(const std::string& path, const Json& value, double min, double max)
    {
        if (!value.is_number()) {
            wrongType(path, value);
            return std::nullopt;
        }
        const double number = value.get<double>();
        if (!(number >= min && number <= max)) {
            m_context.report(Severity::Error, DiagnosticCode::OutOfRange, path, value.dump());
            return std::nullopt;
        }
        return number;
    }

    std::optional<Vec2> readVec2(const std::string& path, const Json& value, double min, double max)
    {
        if (!value.is_array() || value.size() != 2) {
            wrongType(path, value);
            return std::nullopt;
        }
        const auto x = readNumber(path + "[0]", value[0], min, max);
        const auto y = readNumber(path + "[1]", value[1], min, max);
        if (!x || !y) {
            return std::nullopt;
        }
        return Vec2{*x, *y};
    }

    std::optional<Chord> readChord(const std::string& path, const Json& value)
    {
        if (!value.is_string()) {
            wrongType(path, value);
            return std::nullopt;
        }
        auto parsed = parseChord(value.get_ref<const std::string&>());
        if (!parsed) {
            m_context.report(Severity::Error, DiagnosticCode::InvalidInput, path, value.get<std::string>());
        }
        return parsed;
    }

    void wrongType(const std::string& path, const Json& value)
    {
        m_context.report(Severity::Error, DiagnosticCode::WrongType, path, value.type_name());
    }

    static bool propName(const std::string& name)
    {
        return !name.empty() && codePointCount(name) <= kPropNameLength && name.find('.') == std::string::npos;
    }

    std::string pathOf(std::string_view name) const { return m_path + "." + std::string(name); }

    std::string indexPath(std::string_view name, std::size_t index) const
    {
        return pathOf(name) + "[" + std::to_string(index) + "]";
    }

    const Json& m_node;
    std::string m_path;
    Context& m_context;
    std::vector<std::string> m_visited;
    std::vector<NamedBinding> m_extraKeys;
};

template <typename T>
Json writeObject(const T& value);

class Writer
{
public:
    explicit Writer(Json& out)
        : m_out(out)
    {
    }

    std::vector<std::string> required;
    std::vector<std::string> fromVariant;

    void constantText(std::string_view name, std::string_view value) { set(name, std::string(value), true); }
    void constantInteger(std::string_view name, int value) { set(name, value, true); }
    void text(std::string_view name, const std::string& field, std::size_t) { set(name, field); }
    void requiredText(std::string_view name, const std::string& field, std::size_t) { set(name, field, true); }
    void identifier(std::string_view name, const std::string& field) { set(name, field, true); }

    void optionalText(std::string_view name, const std::string& field, std::size_t)
    {
        if (!field.empty()) {
            set(name, field);
        }
    }

    void extension(std::string_view name, const std::string& component, const std::vector<NamedBinding>& keys,
                   const std::vector<NamedValue>& props)
    {
        if (component.empty()) {
            return;
        }
        set(name, component);
        Json extra = Json::object();
        Writer nested(extra);
        for (const NamedBinding& entry : keys) {
            nested.binding(entry.name, entry.binding, SlotKind::Digital);
        }
        if (!extra.empty()) {
            const auto inputs = m_out.find("inputs");
            if (inputs == m_out.end() || !inputs->is_object()) {
                set("inputs", std::move(extra));
            } else {
                for (auto it = extra.begin(); it != extra.end(); ++it) {
                    if (!inputs->contains(it.key())) {
                        (*inputs)[it.key()] = it.value();
                    }
                }
            }
        }
        valueTable("props", props);
    }

    void optionalIdentifier(std::string_view name, const std::string& field)
    {
        if (!field.empty()) {
            set(name, field);
        }
    }

    void identifierList(std::string_view name, const std::vector<std::string>& field) { set(name, field); }

    template <typename E, std::size_t N>
    void choice(std::string_view name, const E& field, const std::array<std::string_view, N>& names)
    {
        set(name, std::string(names[static_cast<std::size_t>(field)]));
    }

    template <typename E, std::size_t N>
    void optionalChoice(std::string_view name, const std::optional<E>& field, const std::array<std::string_view, N>& names)
    {
        if (field) {
            set(name, std::string(names[static_cast<std::size_t>(*field)]));
        }
    }

    template <typename T>
    void integer(std::string_view name, const T& field, std::int64_t, std::int64_t)
    {
        set(name, static_cast<std::int64_t>(field));
    }

    void number(std::string_view name, const double& field, double, double) { set(name, field); }
    void boolean(std::string_view name, const bool& field) { set(name, field); }
    void flag(std::string_view name, const bool& field) { set(name, field); }

    void value(std::string_view name, const VarValue& field, std::size_t)
    {
        std::visit([&](const auto& held) { set(name, held); }, field);
    }

    void condition(std::string_view name, const std::string& field) { set(name, field); }
    void vec2(std::string_view name, const Vec2& field, double, double) { set(name, Json::array({field.x, field.y})); }

    void optionalVec2(std::string_view name, const std::optional<Vec2>& field, double, double)
    {
        if (field) {
            set(name, Json::array({field->x, field->y}));
        }
    }

    void points(std::string_view name, const std::vector<Vec2>& field, std::size_t, double, double)
    {
        Json array = Json::array();
        for (const Vec2& point : field) {
            array.push_back(Json::array({point.x, point.y}));
        }
        set(name, std::move(array), true);
    }

    void integerPair(std::string_view name, const std::array<std::uint16_t, 2>& field, std::int64_t, std::int64_t)
    {
        set(name, Json::array({field[0], field[1]}));
    }

    void chord(std::string_view name, const Chord& field) { set(name, chordName(field), true); }

    void optionalChord(std::string_view name, const std::optional<Chord>& field)
    {
        if (field) {
            set(name, chordName(*field));
        }
    }

    void binding(std::string_view name, const std::optional<Binding>& field, SlotKind, Claim fallback = Claim::Lock)
    {
        if (!field || !field->input) {
            return;
        }
        Json value = writeObject(*field);
        value.erase("claim");
        if (field->claim != fallback && claimable(field->input)) {
            value["claim"] = std::string(kClaimNames[static_cast<std::size_t>(field->claim)]);
        }
        if (value.size() == 1 && value.contains("input")) {
            set(name, value.at("input"));
        } else {
            set(name, std::move(value));
        }
    }

    template <typename T>
    void object(std::string_view name, const T& field)
    {
        set(name, writeObject(field));
    }

    void bindingTable(std::string_view name, const std::vector<NamedBinding>& field)
    {
        Json child = Json::object();
        Writer nested(child);
        for (const NamedBinding& entry : field) {
            nested.binding(entry.name, entry.binding, SlotKind::Digital);
        }
        if (!child.empty()) {
            set(name, std::move(child));
        }
    }

    void slotTable(std::string_view name, const std::vector<NamedBinding>& field) { bindingTable(name, field); }

    void json(std::string_view name, const std::string& field)
    {
        if (field.empty()) {
            return;
        }
        Json parsed = Json::parse(field, nullptr, false);
        if (parsed.is_object() && !parsed.empty()) {
            set(name, std::move(parsed));
        }
    }

    void valueTable(std::string_view name, const std::vector<NamedValue>& field)
    {
        Json child = Json::object();
        for (const NamedValue& entry : field) {
            std::visit(
                [&](const auto& held) {
                    if constexpr (std::is_same_v<std::decay_t<decltype(held)>, Vec2>) {
                        child[entry.name] = Json::array({held.x, held.y});
                    } else {
                        child[entry.name] = held;
                    }
                },
                entry.value);
        }
        if (!child.empty()) {
            set(name, std::move(child));
        }
    }

    template <typename Fn>
    void group(std::string_view name, Fn&& visitFields)
    {
        Json child = Json::object();
        Writer nested(child);
        visitFields(nested);
        if (!child.empty()) {
            set(name, std::move(child));
        }
    }

    template <typename T>
    void list(std::string_view name, const std::vector<T>& field)
    {
        Json array = Json::array();
        for (const T& element : field) {
            array.push_back(writeObject(element));
        }
        set(name, std::move(array));
    }

    template <typename... Alternatives, std::size_t N>
    void variant(std::string_view name, const std::variant<Alternatives...>& field,
                 const std::array<std::string_view, N>& names)
    {
        set(name, std::string(names[field.index()]), true);
        std::visit([this](const auto& alternative) { merge(writeObject(alternative)); }, field);
    }

private:
    template <typename V>
    void set(std::string_view name, V&& value, bool alwaysWrite = false)
    {
        std::string key(name);
        if (alwaysWrite) {
            required.push_back(key);
        }
        m_out[std::move(key)] = std::forward<V>(value);
    }

    void merge(const Json& fields)
    {
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            m_out[it.key()] = it.value();
            fromVariant.push_back(it.key());
        }
    }

    Json& m_out;
};

template <typename T>
Json writeObject(const T& value)
{
    Json full = Json::object();
    Writer writer(full);
    T::visit(writer, value);

    Json defaults = Json::object();
    Writer defaultWriter(defaults);
    const T fresh{};
    T::visit(defaultWriter, fresh);

    for (auto it = full.begin(); it != full.end();) {
        const std::string key = it.key();
        const bool keep = contains(writer.required, key) || contains(writer.fromVariant, key) ||
                          !defaults.contains(key) || defaults.at(key) != it.value();
        it = keep ? std::next(it) : full.erase(it);
    }
    return full;
}

class Upgrade
{
public:
    explicit Upgrade(std::vector<Diagnostic>& notes)
        : m_notes(notes)
    {
    }

    void run(Json& root)
    {
        root["version"] = kFormatVersion;
        note(Severity::Info, "$.version", std::to_string(kVersionWithCommands));
        if (const auto commands = root.find("commands"); commands != root.end()) {
            if (commands->is_array()) {
                for (std::size_t i = 0; i < commands->size(); ++i) {
                    note(Severity::Warning, "$.commands[" + std::to_string(i) + "]", describeCommand((*commands)[i]));
                }
            }
            root.erase("commands");
        }
        if (const auto states = root.find("states"); states != root.end()) {
            Json moved = std::move(*states);
            root.erase("states");
            root["vars"] = std::move(moved);
        }
        const auto controls = root.find("controls");
        if (controls == root.end() || !controls->is_array()) {
            return;
        }
        collectIds(root);
        Json converted = Json::array();
        for (std::size_t i = 0; i < controls->size(); ++i) {
            const Json& control = (*controls)[i];
            const std::string path = "$.controls[" + std::to_string(i) + "]";
            if (!control.is_object()) {
                converted.push_back(control);
                continue;
            }
            for (Json& part : convertControl(control, path)) {
                converted.push_back(std::move(part));
            }
        }
        for (Json& control : converted) {
            if (!control.is_object()) {
                continue;
            }
            const auto pauses = control.find("pauses");
            if (pauses == control.end() || !pauses->is_string()) {
                continue;
            }
            if (const auto moved = m_walkerOf.find(pauses->get<std::string>()); moved != m_walkerOf.end()) {
                *pauses = moved->second;
            }
        }
        *controls = std::move(converted);
    }

private:
    static std::string textAt(const Json& object, const char* key)
    {
        const auto found = object.find(key);
        return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
    }

    static std::string describeCommand(const Json& command)
    {
        if (!command.is_object()) {
            return {};
        }
        const std::string id = textAt(command, "id");
        const std::string kind = textAt(command, "command");
        return id.empty() ? kind : id + " " + kind;
    }

    static std::string triggerOf(const Json& binding)
    {
        return binding.is_object() && binding.contains("trigger") && binding["trigger"].is_string()
                   ? binding["trigger"].get<std::string>()
                   : std::string("hold");
    }

    static std::optional<std::string> sourceOf(const Json& binding)
    {
        if (binding.is_string()) {
            return binding.get<std::string>();
        }
        if (binding.is_object() && binding.contains("input") && binding["input"].is_string()) {
            return binding["input"].get<std::string>();
        }
        return std::nullopt;
    }

    void note(Severity severity, std::string path, std::string detail = {})
    {
        m_notes.push_back({severity, DiagnosticCode::Converted, std::move(path), std::move(detail)});
    }

    void collectIds(const Json& root)
    {
        for (const char* section : {"controls", "vars"}) {
            const auto list = root.find(section);
            if (list == root.end() || !list->is_array()) {
                continue;
            }
            for (const Json& item : *list) {
                if (item.is_object()) {
                    if (const std::string id = textAt(item, "id"); !id.empty()) {
                        m_ids.insert(id);
                    }
                }
            }
        }
    }

    std::string freshId(const std::string& base, std::string_view suffix)
    {
        const std::string stem = base.substr(0, std::min<std::size_t>(base.size(), 60 - suffix.size()));
        std::string candidate = stem + std::string(suffix);
        for (int number = 2; m_ids.contains(candidate); ++number) {
            candidate = stem + std::string(suffix) + std::to_string(number);
        }
        m_ids.insert(candidate);
        return candidate;
    }

    void plainInputs(Json& control, const std::string& path, bool keepTriggerQuiet)
    {
        const auto inputs = control.find("inputs");
        if (inputs == control.end() || !inputs->is_object()) {
            return;
        }
        Json kept = Json::object();
        for (auto it = inputs->begin(); it != inputs->end(); ++it) {
            const std::string slotPath = path + ".inputs." + it.key();
            Json binding = it.value();
            if (binding.is_object()) {
                const std::string trigger = triggerOf(binding);
                if (trigger != "hold" && !keepTriggerQuiet) {
                    note(Severity::Warning, slotPath + ".trigger", trigger);
                }
                for (const char* retired : {"trigger", "longPressMs", "doubleTapMs", "rateHz"}) {
                    binding.erase(retired);
                }
                if (!binding.contains("input")) {
                    continue;
                }
                if (binding.size() == 1) {
                    binding = binding["input"];
                }
            }
            kept[it.key()] = std::move(binding);
        }
        if (kept.empty()) {
            control.erase("inputs");
        } else {
            *inputs = std::move(kept);
        }
    }

    std::vector<Json> convertControl(const Json& control, const std::string& path)
    {
        const std::string type = textAt(control, "type");
        Json result = control;
        if (type == "button") {
            convertButton(result, path);
            return {std::move(result)};
        }
        if (type == "joystick") {
            return splitJoystick(control, path);
        }
        if (type == "camera") {
            bool pad = false;
            if (const auto inputs = control.find("inputs"); inputs != control.end() && inputs->is_object()) {
                if (const auto look = inputs->find("look"); look != inputs->end()) {
                    pad = sourceOf(*look).value_or(std::string()).starts_with("Gamepad.");
                }
            }
            if (pad) {
                result["type"] = "padCamera";
                result.erase("lockKey");
                result.erase("crosshair");
            }
        } else if (type == "aim") {
            result["type"] = "skill";
        }
        plainInputs(result, path, false);
        return {std::move(result)};
    }

    void convertButton(Json& control, const std::string& path)
    {
        const bool logicOnly = control.contains("logicOnly") && control["logicOnly"] == true;
        control.erase("logicOnly");
        std::string trigger = "hold";
        std::optional<Json> rate;
        if (const auto inputs = control.find("inputs"); inputs != control.end() && inputs->is_object()) {
            if (const auto press = inputs->find("press"); press != inputs->end()) {
                trigger = triggerOf(*press);
                if (press->is_object() && press->contains("rateHz")) {
                    rate = (*press)["rateHz"];
                }
            }
        }
        const std::string type = trigger == "tap" ? "tap" : trigger == "turbo" ? "turbo" : "hold";
        control["type"] = type;
        if (type == "turbo" && rate) {
            control["rateHz"] = *rate;
        }
        if (type == "hold") {
            control.erase("tapMs");
        }
        if (trigger != "hold" && type == "hold") {
            note(Severity::Warning, path + ".inputs.press.trigger", trigger);
        }
        if (logicOnly) {
            note(Severity::Warning, path + ".logicOnly");
            if (const auto inputs = control.find("inputs"); inputs != control.end() && inputs->is_object()) {
                inputs->erase("press");
                if (inputs->empty()) {
                    control.erase(inputs);
                }
            }
        }
        plainInputs(control, path, true);
    }

    std::vector<Json> splitJoystick(const Json& control, const std::string& path)
    {
        struct Part {
            const char* type;
            std::vector<const char*> slots;
            std::vector<const char*> fields;
            const char* suffix;
        };
        const std::array<Part, 3> parts{
            Part{"wasd", {"up", "down", "left", "right", "walk", "sprint"}, {"radius", "walkScale", "sprintScale"}, ""},
            Part{"rightWalk", {"walkTo", "stop"}, {"radius", "walkMsPerUnit", "stopOnRelease"}, "-walk"},
            Part{"padStick", {"stick"}, {"radius"}, "-pad"},
        };
        const auto inputs = control.find("inputs");
        const bool hasInputs = inputs != control.end() && inputs->is_object();
        const auto slotsOf = [&](const Part& part) {
            Json slots = Json::object();
            if (hasInputs) {
                for (const char* slot : part.slots) {
                    if (const auto found = inputs->find(slot); found != inputs->end()) {
                        slots[slot] = *found;
                    }
                }
            }
            return slots;
        };
        const bool anySlot = std::ranges::any_of(parts, [&](const Part& part) { return !slotsOf(part).empty(); });
        const std::string id = textAt(control, "id");
        std::vector<Json> made;
        std::string walker;
        for (const Part& part : parts) {
            Json slots = slotsOf(part);
            if (slots.empty() && (anySlot || &part != &parts.front())) {
                continue;
            }
            Json piece = Json::object();
            piece["id"] = made.empty() || id.empty() ? id : freshId(id, part.suffix);
            for (const char* key : {"name", "enabled", "when"}) {
                if (control.contains(key)) {
                    piece[key] = control[key];
                }
            }
            piece["type"] = part.type;
            for (const char* key : {"anchor", "offset"}) {
                if (control.contains(key)) {
                    piece[key] = control[key];
                }
            }
            if (!slots.empty()) {
                piece["inputs"] = std::move(slots);
            }
            for (const char* field : part.fields) {
                if (control.contains(field)) {
                    piece[field] = control[field];
                }
            }
            if (std::string_view(part.type) == "rightWalk") {
                walker = textAt(piece, "id");
            }
            plainInputs(piece, path, false);
            made.push_back(std::move(piece));
        }
        if (made.size() > 1) {
            note(Severity::Info, path + ".type", "joystick");
        }
        if (!walker.empty() && !id.empty() && walker != id) {
            m_walkerOf[id] = walker;
        }
        return made;
    }

    std::vector<Diagnostic>& m_notes;
    std::set<std::string> m_ids;
    std::map<std::string, std::string> m_walkerOf;
};

bool upgradeIfOld(Json& root, std::vector<Diagnostic>& notes)
{
    const auto version = root.find("version");
    if (version == root.end() || !version->is_number_integer() || version->get<std::int64_t>() != kVersionWithCommands ||
        root.value("format", std::string()) != kFormatName) {
        return false;
    }
    Upgrade(notes).run(root);
    return true;
}

struct BindingRef {
    Binding binding;
    std::string path;
    bool controlSlot = false;
    bool held = false;
    std::string when;
    bool enabled = true;
};

class BindingCollector
{
public:
    BindingCollector(std::string path, std::vector<BindingRef>& out, std::string when = {}, bool enabled = true)
        : m_path(std::move(path))
        , m_out(out)
        , m_when(std::move(when))
        , m_enabled(enabled)
    {
    }

    template <typename... Args> void identifier(Args&&...) {}
    template <typename... Args> void optionalIdentifier(Args&&...) {}
    template <typename... Args> void text(Args&&...) {}
    template <typename... Args> void optionalText(Args&&...) {}
    template <typename... Args> void requiredText(Args&&...) {}
    template <typename... Args> void valueTable(Args&&...) {}
    template <typename... Args> void optionalVec2(Args&&...) {}
    template <typename... Args> void choice(Args&&...) {}
    template <typename... Args> void integer(Args&&...) {}
    template <typename... Args> void number(Args&&...) {}
    template <typename... Args> void boolean(Args&&...) {}
    template <typename... Args> void vec2(Args&&...) {}
    template <typename... Args> void points(Args&&...) {}
    template <typename... Args> void value(Args&&...) {}
    template <typename... Args> void list(Args&&...) {}
    template <typename... Args> void json(Args&&...) {}

    void flag(std::string_view name, const bool& field)
    {
        if (name == "enabled") {
            m_enabled = field;
        }
    }

    void condition(std::string_view, const std::string& field) { m_when = field; }

    void optionalChord(std::string_view name, const std::optional<Chord>& field)
    {
        if (field) {
            m_out.push_back(BindingRef{Binding{.input = *field, .claim = Claim::Both}, m_path + "." + std::string(name),
                                       true, false, m_when, m_enabled});
        }
    }

    template <typename Fn>
    void group(std::string_view name, Fn&& visitFields)
    {
        BindingCollector nested(m_path + "." + std::string(name), m_out, m_when, m_enabled);
        visitFields(nested);
    }

    void binding(std::string_view name, const std::optional<Binding>& field, SlotKind slot, Claim = Claim::Lock)
    {
        if (field && field->input) {
            m_out.push_back(BindingRef{*field, m_path + "." + std::string(name), true,
                                       slot == SlotKind::HeldAxis2D || name == "cast", m_when, m_enabled});
        }
    }

    void bindingTable(std::string_view name, const std::vector<NamedBinding>& field)
    {
        for (const NamedBinding& entry : field) {
            if (entry.binding && entry.binding->input) {
                m_out.push_back(BindingRef{*entry.binding, m_path + "." + std::string(name) + "." + entry.name, true,
                                           false, m_when, m_enabled});
            }
        }
    }

    void slotTable(std::string_view name, const std::vector<NamedBinding>& field) { bindingTable(name, field); }

    void extension(std::string_view, const std::string& component, const std::vector<NamedBinding>& keys,
                   const std::vector<NamedValue>&)
    {
        if (!component.empty()) {
            bindingTable("inputs", keys);
        }
    }

    template <typename... Alternatives, std::size_t N>
    void variant(std::string_view, const std::variant<Alternatives...>& field, const std::array<std::string_view, N>&)
    {
        std::visit([this](const auto& alternative) { std::decay_t<decltype(alternative)>::visit(*this, alternative); },
                   field);
    }

private:
    std::string m_path;
    std::vector<BindingRef>& m_out;
    std::string m_when;
    bool m_enabled = true;
};

bool exclusiveConditions(const std::string& a, const std::string& b)
{
    return !a.empty() && !b.empty() && (a == "!" + b || b == "!" + a);
}

class SlotLister
{
public:
    explicit SlotLister(std::vector<std::string_view>& out, bool inside = false)
        : m_out(out)
        , m_inside(inside)
    {
    }

    template <typename... Args> void identifier(Args&&...) {}
    template <typename... Args> void optionalIdentifier(Args&&...) {}
    template <typename... Args> void text(Args&&...) {}
    template <typename... Args> void optionalText(Args&&...) {}
    template <typename... Args> void requiredText(Args&&...) {}
    template <typename... Args> void bindingTable(Args&&...) {}
    template <typename... Args> void valueTable(Args&&...) {}
    template <typename... Args> void optionalVec2(Args&&...) {}
    template <typename... Args> void optionalChord(Args&&...) {}
    template <typename... Args> void choice(Args&&...) {}
    template <typename... Args> void integer(Args&&...) {}
    template <typename... Args> void number(Args&&...) {}
    template <typename... Args> void boolean(Args&&...) {}
    template <typename... Args> void vec2(Args&&...) {}
    template <typename... Args> void points(Args&&...) {}
    template <typename... Args> void list(Args&&...) {}
    template <typename... Args> void slotTable(Args&&...) {}
    template <typename... Args> void json(Args&&...) {}

    template <typename Fn>
    void group(std::string_view, Fn&& visitFields)
    {
        SlotLister nested(m_out, true);
        visitFields(nested);
    }

    void binding(std::string_view name, const std::optional<Binding>&, SlotKind, Claim = Claim::Lock)
    {
        if (m_inside) {
            m_out.push_back(name);
        }
    }

private:
    std::vector<std::string_view>& m_out;
    bool m_inside = false;
};

bool clash(const BindingRef& a, const BindingRef& b)
{
    return !a.held && !b.held && a.binding.input && a.binding.input == b.binding.input && a.enabled && b.enabled &&
           !exclusiveConditions(a.when, b.when) &&
           (!claimable(a.binding.input) || overlaps(a.binding.claim, b.binding.claim));
}

std::vector<BindingRef> collectBindings(const Profile& profile, std::vector<ConflictingBinding>* owners = nullptr)
{
    std::vector<BindingRef> refs;
    for (std::size_t i = 0; i < profile.controls.size(); ++i) {
        const std::size_t before = refs.size();
        BindingCollector collector("$.controls[" + std::to_string(i) + "]", refs);
        Control::visit(collector, profile.controls[i]);
        if (owners != nullptr) {
            for (std::size_t r = before; r < refs.size(); ++r) {
                owners->push_back({i, refs[r].path});
            }
        }
    }
    return refs;
}

class ProfileChecker
{
public:
    ProfileChecker(const Profile& profile, Context& context)
        : m_profile(profile)
        , m_context(context)
    {
    }

    void run()
    {
        collectIds();
        checkReferences();
        checkBindings();
    }

private:
    void claim(const std::string& id, const std::string& path)
    {
        if (id.empty()) {
            return;
        }
        const auto [existing, inserted] = m_ids.emplace(id, path);
        if (!inserted) {
            m_context.report(Severity::Error, DiagnosticCode::DuplicateId, path + ".id", existing->second);
        }
    }

    void collectIds()
    {
        for (std::size_t i = 0; i < m_profile.controls.size(); ++i) {
            claim(m_profile.controls[i].id, "$.controls[" + std::to_string(i) + "]");
        }
        for (std::size_t i = 0; i < m_profile.vars.size(); ++i) {
            claim(m_profile.vars[i].id, "$.vars[" + std::to_string(i) + "]");
            m_varIds.insert(m_profile.vars[i].id);
        }
    }

    void checkCondition(const std::string& when, const std::string& path)
    {
        if (when.empty()) {
            return;
        }
        const std::string var = when.starts_with('!') ? when.substr(1) : when;
        if (!m_varIds.contains(var)) {
            m_context.report(Severity::Error, DiagnosticCode::UnknownReference, path + ".when", var);
        }
    }

    void checkReferences()
    {
        for (std::size_t i = 0; i < m_profile.controls.size(); ++i) {
            const Control& control = m_profile.controls[i];
            const std::string path = "$.controls[" + std::to_string(i) + "]";
            checkCondition(control.when, path);
            if (const auto* skill = std::get_if<SkillControl>(&control.kind)) {
                checkSkill(*skill, path);
            } else if (const auto* multi = std::get_if<MultiPressControl>(&control.kind)) {
                if (multi->points.size() > kMostMultiPressPoints) {
                    m_context.report(Severity::Error, DiagnosticCode::OutOfRange, path + ".points",
                                     std::to_string(multi->points.size()));
                }
            } else if (const auto* wheel = std::get_if<ActionWheelControl>(&control.kind)) {
                checkWheel(control, *wheel, path);
            } else if (const auto* component = std::get_if<ComponentControl>(&control.kind)) {
                if (!component->component.empty() && !isComponentRef(component->component)) {
                    m_context.report(Severity::Error, DiagnosticCode::InvalidValue, path + ".component",
                                     component->component);
                }
            }
            if (!control.component.empty() && !isComponentRef(control.component)) {
                m_context.report(Severity::Error, DiagnosticCode::InvalidValue, path + ".component", control.component);
            }
            checkLink(control, path);
            checkModuleRefs(control, path);
        }
    }

    void checkModuleRefs(const Control& control, const std::string& path)
    {
        if (control.attachments.size() > kMostAttachments) {
            m_context.report(Severity::Error, DiagnosticCode::OutOfRange, path + ".attachments",
                             std::to_string(control.attachments.size()));
        }
        for (std::size_t i = 0; i < control.attachments.size(); ++i) {
            const std::string& module = control.attachments[i].module;
            if (!isModuleRef(module)) {
                m_context.report(Severity::Error, DiagnosticCode::InvalidValue,
                                 path + ".attachments[" + std::to_string(i) + "].module", module);
            }
        }
        if (control.skipGlobal.size() > kMostSkippedGlobals) {
            m_context.report(Severity::Error, DiagnosticCode::OutOfRange, path + ".skipGlobal",
                             std::to_string(control.skipGlobal.size()));
        }
        for (std::size_t i = 0; i < control.skipGlobal.size(); ++i) {
            const std::string& module = control.skipGlobal[i].module;
            if (!isModuleRef(module)) {
                m_context.report(Severity::Error, DiagnosticCode::InvalidValue,
                                 path + ".skipGlobal[" + std::to_string(i) + "].module", module);
            }
        }
        const auto* behavior = std::get_if<BehaviorControl>(&control.kind);
        if (behavior == nullptr) {
            return;
        }
        if (!isModuleRef(behavior->behavior)) {
            m_context.report(Severity::Error, DiagnosticCode::InvalidValue, path + ".behavior", behavior->behavior);
        }
        if (behavior->inputs.size() > kMostBehaviorSlots) {
            m_context.report(Severity::Error, DiagnosticCode::OutOfRange, path + ".inputs",
                             std::to_string(behavior->inputs.size()));
        }
    }

    void checkLink(const Control& control, const std::string& path)
    {
        if (control.parent.empty()) {
            if (!control.child.empty()) {
                m_context.report(Severity::Warning, DiagnosticCode::MissingField, path + ".parent");
            }
            return;
        }
        if (control.child.empty()) {
            m_context.report(Severity::Warning, DiagnosticCode::MissingField, path + ".child");
        }
        const bool known = control.parent != control.id &&
                           std::ranges::any_of(m_profile.controls,
                                               [&](const Control& other) { return other.id == control.parent; });
        if (!known) {
            m_context.report(Severity::Warning, DiagnosticCode::UnknownReference, path + ".parent", control.parent);
        }
    }

    void checkWheel(const Control& control, const ActionWheelControl& wheel, const std::string& path)
    {
        if (wheel.cells.size() < kFewestWheelCells || wheel.cells.size() > kMostWheelCells) {
            m_context.report(Severity::Error, DiagnosticCode::OutOfRange, path + ".cells", std::to_string(wheel.cells.size()));
        }
        for (std::size_t i = 0; i < wheel.cells.size(); ++i) {
            const WheelCell& cell = wheel.cells[i];
            if (cell.action != WheelAction::Control) {
                continue;
            }
            const std::string where = path + ".cells[" + std::to_string(i) + "].control";
            const auto target = std::ranges::find(m_profile.controls, cell.control, &Control::id);
            if (cell.control.empty() || target == m_profile.controls.end() || target->id == control.id ||
                std::holds_alternative<ActionWheelControl>(target->kind)) {
                m_context.report(Severity::Warning, DiagnosticCode::UnknownReference, where, cell.control);
            }
        }
    }

    void checkSkill(const SkillControl& skill, const std::string& path)
    {
        if (skill.direction == AimDirection::Cursor && skill.look) {
            m_context.report(Severity::Warning, DiagnosticCode::InvalidValue, path + ".inputs.look", "direction");
        }
        if (skill.pauses.empty()) {
            return;
        }
        const auto target = std::ranges::find(m_profile.controls, skill.pauses, &Control::id);
        if (target == m_profile.controls.end() || !isMovement(target->kind)) {
            m_context.report(Severity::Error, DiagnosticCode::UnknownReference, path + ".pauses", skill.pauses);
        }
    }

    void checkBindings()
    {
        const std::vector<BindingRef> refs = collectBindings(m_profile);
        for (std::size_t a = 0; a < refs.size(); ++a) {
            for (std::size_t b = 0; b < a; ++b) {
                if (clash(refs[a], refs[b])) {
                    m_context.report(Severity::Warning, DiagnosticCode::Conflict, refs[a].path, refs[b].path);
                    break;
                }
            }
        }
    }

    const Profile& m_profile;
    Context& m_context;
    std::map<std::string, std::string> m_ids;
    std::set<std::string> m_varIds;
};

}

bool LoadResult::ok() const noexcept
{
    return std::none_of(diagnostics.begin(), diagnostics.end(),
                        [](const Diagnostic& d) { return d.severity == Severity::Error; });
}

bool LoadResult::converted() const noexcept
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const Diagnostic& d) { return d.code == DiagnosticCode::Converted; });
}

bool isComponentRef(std::string_view ref) noexcept
{
    const std::size_t slash = ref.find('/');
    if (slash == std::string_view::npos) {
        return false;
    }
    const std::string_view shelf = ref.substr(0, slash);
    const std::string_view name = ref.substr(slash + 1);
    return (shelf == "examples" || shelf == "mine") && !name.empty() && name != "." && name != ".." &&
           name.find_first_of("/\\:*?\"<>|") == std::string_view::npos && ref.size() <= kComponentRefLength;
}

bool isModuleRef(std::string_view ref) noexcept
{
    const std::size_t slash = ref.rfind('/');
    if (slash == std::string_view::npos || ref.size() > kModuleRefLength) {
        return false;
    }
    const std::string_view id = ref.substr(0, slash);
    const std::string_view name = ref.substr(slash + 1);
    const bool nameFits = !name.empty() && name.size() <= 64 && name.front() >= 'a' && name.front() <= 'z' &&
                          std::ranges::all_of(name, [](char c) {
                              return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
                          });
    if (!nameFits || id.empty() || id.size() > 64) {
        return false;
    }
    std::size_t parts = 0;
    std::size_t start = 0;
    while (true) {
        const std::size_t dot = id.find('.', start);
        const std::string_view part = id.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
        if (!hyphenatedWord(part)) {
            return false;
        }
        ++parts;
        if (dot == std::string_view::npos) {
            break;
        }
        start = dot + 1;
    }
    return parts >= 2;
}

bool usesComponents(const Profile& profile) noexcept
{
    return std::ranges::any_of(profile.controls, [](const Control& control) { return !componentRef(control).empty(); });
}

const std::string& componentRef(const Control& control) noexcept
{
    const auto* placed = std::get_if<ComponentControl>(&control.kind);
    return placed != nullptr ? placed->component : control.component;
}

const std::vector<NamedBinding>& componentKeys(const Control& control) noexcept
{
    const auto* placed = std::get_if<ComponentControl>(&control.kind);
    return placed != nullptr ? placed->keys : control.keys;
}

const std::vector<NamedValue>& componentProps(const Control& control) noexcept
{
    const auto* placed = std::get_if<ComponentControl>(&control.kind);
    return placed != nullptr ? placed->props : control.props;
}

std::vector<std::string_view> inputSlots(std::string_view type)
{
    std::vector<std::string_view> slots;
    const auto index = nameIndex(kControlTypeNames, type);
    if (!index) {
        return slots;
    }
    ControlKind kind;
    emplaceIndex(kind, *index, std::make_index_sequence<std::variant_size_v<ControlKind>>{});
    SlotLister lister(slots);
    std::visit([&lister](const auto& alternative) { std::decay_t<decltype(alternative)>::visit(lister, alternative); },
               kind);
    return slots;
}

bool isMovement(const ControlKind& kind) noexcept
{
    return std::holds_alternative<WasdControl>(kind) || std::holds_alternative<RightWalkControl>(kind) ||
           std::holds_alternative<PadStickControl>(kind);
}

bool truthy(const VarValue& value) noexcept
{
    if (const auto* flag = std::get_if<bool>(&value)) {
        return *flag;
    }
    if (const auto* number = std::get_if<double>(&value)) {
        return *number != 0.0;
    }
    return !std::get<std::string>(value).empty();
}

LoadResult loadProfile(std::string_view json)
{
    LoadResult result;
    Context context{result.diagnostics};
    if (json.size() > kMaxProfileBytes || !nestingWithinLimit(json)) {
        context.report(Severity::Error, DiagnosticCode::TooLarge, "$");
        return result;
    }
    Json root = Json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded()) {
        context.report(Severity::Error, DiagnosticCode::MalformedJson, "$");
        return result;
    }
    if (!root.is_object()) {
        context.report(Severity::Error, DiagnosticCode::WrongType, "$", root.type_name());
        return result;
    }
    upgradeIfOld(root, result.diagnostics);
    Reader reader(root, "$", context);
    Profile::visit(reader, result.profile);
    reader.finish();
    ProfileChecker(result.profile, context).run();
    return result;
}

std::string saveProfile(const Profile& profile)
{
    return writeObject(profile).dump(2) + "\n";
}

std::string controlSettings(const Control& control)
{
    Profile single;
    single.controls.push_back(control);
    const Json written = writeObject(single);
    const auto controls = written.find("controls");
    if (controls == written.end() || !controls->is_array() || controls->empty()) {
        return "{}";
    }
    Json settings = controls->front();
    settings.erase("id");
    settings.erase("name");
    settings.erase("type");
    return settings.dump(-1, ' ', false, Json::error_handler_t::replace);
}

std::string upgradeProfile(std::string_view json, std::vector<Diagnostic>* notes)
{
    if (json.size() > kMaxProfileBytes || !nestingWithinLimit(json)) {
        return std::string(json);
    }
    Json root = Json::parse(json.begin(), json.end(), nullptr, false);
    if (!root.is_object()) {
        return std::string(json);
    }
    std::vector<Diagnostic> ignored;
    if (!upgradeIfOld(root, notes != nullptr ? *notes : ignored)) {
        return std::string(json);
    }
    return root.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

std::vector<Chord> boundInputs(const Profile& profile)
{
    std::vector<Chord> chords;
    for (const BindingRef& ref : collectBindings(profile)) {
        chords.push_back(*ref.binding.input);
    }
    return chords;
}

std::vector<InputSource> cursorClaims(const Profile& profile)
{
    std::vector<InputSource> sources;
    for (const BindingRef& ref : collectBindings(profile)) {
        if (claimable(ref.binding.input) && claimsCursor(ref.binding.claim) &&
            std::ranges::find(sources, ref.binding.input->source) == sources.end()) {
            sources.push_back(ref.binding.input->source);
        }
    }
    return sources;
}

std::vector<CursorClaim> cursorClaimChords(const Profile& profile)
{
    std::vector<ConflictingBinding> owners;
    const std::vector<BindingRef> refs = collectBindings(profile, &owners);
    std::vector<CursorClaim> claims;
    for (std::size_t i = 0; i < refs.size() && i < owners.size(); ++i) {
        const BindingRef& ref = refs[i];
        if (!claimable(ref.binding.input) || !claimsCursor(ref.binding.claim) || ref.path.ends_with(".inputs.cast")) {
            continue;
        }
        claims.push_back({.chord = *ref.binding.input, .index = owners[i].index});
    }
    return claims;
}

bool claimable(const std::optional<Chord>& input) noexcept
{
    return input && input->source.device == Device::Mouse && kindOf(input->source) == SourceKind::Button;
}

bool claimsLocked(Claim claim) noexcept
{
    return claim != Claim::Cursor;
}

bool claimsCursor(Claim claim) noexcept
{
    return claim != Claim::Lock;
}

std::vector<InputConflict> inputConflicts(const Profile& profile)
{
    std::vector<ConflictingBinding> owners;
    const std::vector<BindingRef> refs = collectBindings(profile, &owners);
    std::vector<InputConflict> conflicts;
    const auto add = [&conflicts](const Chord& input, const ConflictingBinding& owner) {
        auto group = std::ranges::find(conflicts, input, &InputConflict::input);
        if (group == conflicts.end()) {
            conflicts.push_back({input, {}});
            group = std::prev(conflicts.end());
        }
        if (std::ranges::find(group->bindings, owner.path, &ConflictingBinding::path) == group->bindings.end()) {
            group->bindings.push_back(owner);
        }
    };
    for (std::size_t a = 0; a < refs.size(); ++a) {
        for (std::size_t b = 0; b < a; ++b) {
            if (clash(refs[a], refs[b])) {
                add(*refs[a].binding.input, owners[b]);
                add(*refs[a].binding.input, owners[a]);
            }
        }
    }
    return conflicts;
}

}
