#include "Settings.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace gs::app::settings {

namespace {

using Json = nlohmann::ordered_json;

constexpr std::size_t kMaxBytes = 1024 * 1024;
constexpr int kMaxNesting = 64;

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

bool contains(const std::vector<std::string>& keys, const std::string& key)
{
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

template <std::size_t N>
bool languageAccepted(std::string_view text, const std::array<std::string_view, N>& names)
{
    return std::ranges::find(names, text) != names.end() || languageTag(text);
}

struct Context {
    std::vector<Diagnostic>& diagnostics;
    bool usable = true;
    bool newer = false;

    void report(Severity severity, Problem problem, std::string path, std::string detail = {})
    {
        diagnostics.push_back(Diagnostic{severity, problem, std::move(path), std::move(detail)});
    }
};

std::optional<Value> valueFromJson(const Json& json, Kind kind)
{
    switch (kind) {
    case Kind::Choice:
    case Kind::Text:
        return json.is_string() ? std::optional<Value>(json.get<std::string>()) : std::nullopt;
    case Kind::Integer:
        if (!json.is_number_integer() ||
            (json.is_number_unsigned() &&
             json.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))) {
            return std::nullopt;
        }
        return Value(json.get<std::int64_t>());
    case Kind::Boolean:
        return json.is_boolean() ? std::optional<Value>(json.get<bool>()) : std::nullopt;
    }
    return std::nullopt;
}

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
                m_context.report(Severity::Warning, Problem::UnknownField, pathOf(it.key()));
            }
        }
    }

    void constantText(std::string_view name, std::string_view expected)
    {
        const Json* value = find(name);
        if (!value || !value->is_string() || value->get_ref<const std::string&>() != expected) {
            m_context.report(Severity::Error, Problem::WrongFormat, pathOf(name), value ? value->dump() : "");
            m_context.usable = false;
        }
    }

    void constantInteger(std::string_view name, int expected)
    {
        const Json* value = find(name);
        const bool integer = value && value->is_number_integer();
        if (integer && value->get<std::int64_t>() == expected) {
            return;
        }
        if (integer && value->get<std::int64_t>() > expected) {
            m_context.newer = true;
        }
        m_context.report(Severity::Error, Problem::WrongVersion, pathOf(name), value ? value->dump() : "");
        m_context.usable = false;
    }

    template <typename E, std::size_t N>
    void choice(std::string_view name, E& field, const std::array<std::string_view, N>& names, const Facets& = {})
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        if (!value->is_string()) {
            wrongType(name, *value);
            return;
        }
        const std::string& text = value->get_ref<const std::string&>();
        for (std::size_t i = 0; i < N; ++i) {
            if (names[i] == text) {
                field = static_cast<E>(i);
                return;
            }
        }
        m_context.report(Severity::Error, Problem::UnknownName, pathOf(name), value->dump());
    }

    template <std::size_t N>
    void language(std::string_view name, std::string& field, const std::array<std::string_view, N>& names,
                  const Facets& = {})
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        if (!value->is_string()) {
            wrongType(name, *value);
            return;
        }
        const std::string& text = value->get_ref<const std::string&>();
        if (!languageAccepted(text, names)) {
            m_context.report(Severity::Error, Problem::UnknownName, pathOf(name), value->dump());
            return;
        }
        field = text;
    }

    template <typename T>
    void integer(std::string_view name, T& field, std::int64_t min, std::int64_t max, const Facets& = {})
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        if (!value->is_number_integer()) {
            wrongType(name, *value);
            return;
        }
        const bool huge = value->is_number_unsigned() && value->get<std::uint64_t>() > static_cast<std::uint64_t>(max);
        if (huge || value->get<std::int64_t>() < min || value->get<std::int64_t>() > max) {
            m_context.report(Severity::Error, Problem::OutOfRange, pathOf(name), value->dump());
            return;
        }
        field = static_cast<T>(value->get<std::int64_t>());
    }

    void boolean(std::string_view name, bool& field, const Facets& = {})
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        if (!value->is_boolean()) {
            wrongType(name, *value);
            return;
        }
        field = value->get<bool>();
    }

    void text(std::string_view name, std::string& field, std::size_t maxLength, const Facets& = {})
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        if (!value->is_string()) {
            wrongType(name, *value);
            return;
        }
        const std::string& text = value->get_ref<const std::string&>();
        if (text.size() > maxLength) {
            m_context.report(Severity::Error, Problem::OutOfRange, pathOf(name));
            return;
        }
        field = text;
    }

    template <typename T>
    void object(std::string_view name, T& field, Page = Page::None)
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        if (!value->is_object()) {
            wrongType(name, *value);
            return;
        }
        Reader nested(*value, pathOf(name), m_context);
        T::visit(nested, field);
        nested.finish();
    }

    void overrides(std::string_view name, std::map<std::string, Value, std::less<>>& field)
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        if (!value->is_object()) {
            wrongType(name, *value);
            return;
        }
        field.clear();
        for (auto it = value->begin(); it != value->end(); ++it) {
            const std::string path = pathOf(name) + "[" + Json(it.key()).dump() + "]";
            const Entry* entry = findSetting(it.key());
            if (entry == nullptr || entry->facets.scope != Scope::Device) {
                m_context.report(Severity::Warning, Problem::UnknownField, path);
                continue;
            }
            std::optional<Value> parsed = valueFromJson(it.value(), entry->kind);
            if (!parsed) {
                m_context.report(Severity::Error, Problem::WrongType, path, it.value().dump());
                continue;
            }
            Settings scratch;
            if (!assign(scratch, it.key(), *parsed)) {
                m_context.report(Severity::Error,
                                 entry->kind == Kind::Choice ? Problem::UnknownName : Problem::OutOfRange, path,
                                 it.value().dump());
                continue;
            }
            field.emplace(it.key(), std::move(*parsed));
        }
    }

    template <typename T, typename Compare>
    void map(std::string_view name, std::map<std::string, T, Compare>& field, std::size_t maxKeyLength)
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        if (!value->is_object()) {
            wrongType(name, *value);
            return;
        }
        field.clear();
        for (auto it = value->begin(); it != value->end(); ++it) {
            const std::string path = pathOf(name) + "[" + Json(it.key()).dump() + "]";
            if (it.key().empty() || it.key().size() > maxKeyLength) {
                m_context.report(Severity::Error, Problem::OutOfRange, path);
                continue;
            }
            if (!it.value().is_object()) {
                m_context.report(Severity::Error, Problem::WrongType, path, it.value().dump());
                continue;
            }
            T entry{};
            Reader nested(it.value(), path, m_context);
            T::visit(nested, entry);
            nested.finish();
            field.emplace(it.key(), std::move(entry));
        }
    }

    void textMap(std::string_view name, std::map<std::string, std::string, std::less<>>& field,
                 std::size_t maxKeyLength, std::size_t maxValueLength)
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        if (!value->is_object()) {
            wrongType(name, *value);
            return;
        }
        field.clear();
        for (auto it = value->begin(); it != value->end(); ++it) {
            const std::string path = pathOf(name) + "[" + Json(it.key()).dump() + "]";
            if (it.key().empty() || it.key().size() > maxKeyLength) {
                m_context.report(Severity::Error, Problem::OutOfRange, path);
                continue;
            }
            if (!it.value().is_string()) {
                m_context.report(Severity::Error, Problem::WrongType, path, it.value().dump());
                continue;
            }
            if (it.value().get_ref<const std::string&>().size() > maxValueLength) {
                m_context.report(Severity::Error, Problem::OutOfRange, path);
                continue;
            }
            field.emplace(it.key(), it.value().get<std::string>());
        }
    }

    void textList(std::string_view name, std::vector<std::string>& field, std::size_t maxCount,
                  std::size_t maxLength)
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        if (!value->is_array()) {
            wrongType(name, *value);
            return;
        }
        field.clear();
        for (std::size_t i = 0; i < value->size(); ++i) {
            const std::string path = pathOf(name) + "[" + std::to_string(i) + "]";
            const Json& item = (*value)[i];
            if (!item.is_string()) {
                m_context.report(Severity::Error, Problem::WrongType, path, item.dump());
                continue;
            }
            const std::string& text = item.get_ref<const std::string&>();
            if (text.empty() || text.size() > maxLength || field.size() == maxCount) {
                m_context.report(Severity::Error, Problem::OutOfRange, path);
                continue;
            }
            field.push_back(text);
        }
    }

    void scalars(std::string_view name, Scalars& field, std::size_t maxKeyLength, std::size_t maxTextLength)
    {
        const Json* value = find(name);
        if (!value) {
            return;
        }
        if (!value->is_object()) {
            wrongType(name, *value);
            return;
        }
        field.clear();
        for (auto it = value->begin(); it != value->end(); ++it) {
            const std::string path = pathOf(name) + "[" + Json(it.key()).dump() + "]";
            const Json& item = it.value();
            if (it.key().empty() || it.key().size() > maxKeyLength) {
                m_context.report(Severity::Error, Problem::OutOfRange, path);
            } else if (item.is_boolean()) {
                field.emplace(it.key(), item.get<bool>());
            } else if (item.is_number_integer()) {
                if (item.is_number_unsigned() &&
                    item.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                    m_context.report(Severity::Error, Problem::OutOfRange, path, item.dump());
                } else {
                    field.emplace(it.key(), item.get<std::int64_t>());
                }
            } else if (item.is_number_float() && std::isfinite(item.get<double>())) {
                field.emplace(it.key(), item.get<double>());
            } else if (item.is_string() && item.get_ref<const std::string&>().size() <= maxTextLength) {
                field.emplace(it.key(), item.get<std::string>());
            } else {
                m_context.report(Severity::Error, item.is_string() ? Problem::OutOfRange : Problem::WrongType, path,
                                 item.dump());
            }
        }
    }

private:
    const Json* find(std::string_view name)
    {
        std::string key(name);
        const auto it = m_node.find(key);
        m_visited.push_back(std::move(key));
        return it == m_node.end() ? nullptr : &*it;
    }

    void wrongType(std::string_view name, const Json& value)
    {
        m_context.report(Severity::Error, Problem::WrongType, pathOf(name), value.dump());
    }

    [[nodiscard]] std::string pathOf(std::string_view name) const { return m_path + "." + std::string(name); }

    const Json& m_node;
    std::string m_path;
    Context& m_context;
    std::vector<std::string> m_visited;
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

    void constantText(std::string_view name, std::string_view value) { set(name, std::string(value), true); }
    void constantInteger(std::string_view name, int value) { set(name, value, true); }

    template <typename E, std::size_t N>
    void choice(std::string_view name, const E& field, const std::array<std::string_view, N>& names, const Facets& = {})
    {
        set(name, std::string(names[static_cast<std::size_t>(field)]));
    }

    template <std::size_t N>
    void language(std::string_view name, const std::string& field, const std::array<std::string_view, N>&,
                  const Facets& = {})
    {
        set(name, field);
    }

    template <typename T>
    void integer(std::string_view name, const T& field, std::int64_t, std::int64_t, const Facets& = {})
    {
        set(name, static_cast<std::int64_t>(field));
    }

    void boolean(std::string_view name, const bool& field, const Facets& = {}) { set(name, field); }
    void text(std::string_view name, const std::string& field, std::size_t, const Facets& = {}) { set(name, field); }

    template <typename T>
    void object(std::string_view name, const T& field, Page = Page::None)
    {
        set(name, writeObject(field));
    }

    void overrides(std::string_view name, const std::map<std::string, Value, std::less<>>& field)
    {
        Json object = Json::object();
        for (const auto& [path, value] : field) {
            object[path] = std::visit([](const auto& scalar) { return Json(scalar); }, value);
        }
        set(name, std::move(object));
    }

    template <typename T, typename Compare>
    void map(std::string_view name, const std::map<std::string, T, Compare>& field, std::size_t)
    {
        Json object = Json::object();
        for (const auto& [key, entry] : field) {
            Json written = writeObject(entry);
            if (!written.empty()) {
                object[key] = std::move(written);
            }
        }
        set(name, std::move(object));
    }

    void textMap(std::string_view name, const std::map<std::string, std::string, std::less<>>& field, std::size_t,
                 std::size_t)
    {
        Json object = Json::object();
        for (const auto& [key, text] : field) {
            object[key] = text;
        }
        set(name, std::move(object));
    }

    void textList(std::string_view name, const std::vector<std::string>& field, std::size_t, std::size_t)
    {
        Json array = Json::array();
        for (const std::string& text : field) {
            array.push_back(text);
        }
        set(name, std::move(array));
    }

    void scalars(std::string_view name, const Scalars& field, std::size_t, std::size_t)
    {
        Json object = Json::object();
        for (const auto& [key, value] : field) {
            object[key] = std::visit([](const auto& scalar) { return Json(scalar); }, value);
        }
        set(name, std::move(object));
    }

private:
    template <typename Field>
    void set(std::string_view name, Field&& value, bool alwaysWrite = false)
    {
        std::string key(name);
        if (alwaysWrite) {
            required.push_back(key);
        }
        m_out[std::move(key)] = std::forward<Field>(value);
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
        const bool keep = contains(writer.required, it.key()) || defaults.at(it.key()) != it.value();
        it = keep ? std::next(it) : full.erase(it);
    }
    return full;
}

class PathWalker
{
public:
    void constantText(std::string_view, std::string_view) {}
    void constantInteger(std::string_view, int) {}

    template <typename T, typename Compare>
    void map(std::string_view, const std::map<std::string, T, Compare>&, std::size_t)
    {
    }

    void textMap(std::string_view, const std::map<std::string, std::string, std::less<>>&, std::size_t, std::size_t)
    {
    }

    void textList(std::string_view, const std::vector<std::string>&, std::size_t, std::size_t) {}
    void scalars(std::string_view, const Scalars&, std::size_t, std::size_t) {}

protected:
    template <typename Self, typename T>
    void descend(Self& self, std::string_view name, T& field, Page page)
    {
        const std::string outerPrefix = m_prefix;
        const Page outerPage = m_page;
        m_prefix = pathOf(name);
        m_page = page;
        std::remove_const_t<T>::visit(self, field);
        m_prefix = outerPrefix;
        m_page = outerPage;
    }

    [[nodiscard]] std::string pathOf(std::string_view name) const
    {
        return m_prefix.empty() ? std::string(name) : m_prefix + "." + std::string(name);
    }

    [[nodiscard]] Page page() const noexcept { return m_page; }

private:
    std::string m_prefix;
    Page m_page = Page::None;
};

class Cataloger : public PathWalker
{
public:
    std::vector<Entry> entries;

    template <typename E, std::size_t N>
    void choice(std::string_view name, const E& field, const std::array<std::string_view, N>& names,
                const Facets& facets = {})
    {
        if (!facets.title.empty()) {
            Entry entry = make(name, Kind::Choice, facets, std::string(names[static_cast<std::size_t>(field)]));
            entry.choices.assign(names.begin(), names.end());
            entries.push_back(std::move(entry));
        }
    }

    template <std::size_t N>
    void language(std::string_view name, const std::string& field, const std::array<std::string_view, N>& names,
                  const Facets& facets = {})
    {
        if (!facets.title.empty()) {
            Entry entry = make(name, Kind::Choice, facets, field);
            entry.choices.assign(names.begin(), names.end());
            entries.push_back(std::move(entry));
        }
    }

    template <typename T>
    void integer(std::string_view name, const T& field, std::int64_t min, std::int64_t max, const Facets& facets = {})
    {
        if (!facets.title.empty()) {
            Entry entry = make(name, Kind::Integer, facets, static_cast<std::int64_t>(field));
            entry.min = min;
            entry.max = max;
            entries.push_back(std::move(entry));
        }
    }

    void boolean(std::string_view name, const bool& field, const Facets& facets = {})
    {
        if (!facets.title.empty()) {
            entries.push_back(make(name, Kind::Boolean, facets, field));
        }
    }

    void text(std::string_view name, const std::string& field, std::size_t maxLength, const Facets& facets = {})
    {
        if (!facets.title.empty()) {
            Entry entry = make(name, Kind::Text, facets, field);
            entry.maxLength = maxLength;
            entries.push_back(std::move(entry));
        }
    }

    template <typename T>
    void object(std::string_view name, const T& field, Page page = Page::None)
    {
        descend(*this, name, field, page);
    }

private:
    [[nodiscard]] Entry make(std::string_view name, Kind kind, const Facets& facets, Value defaultValue) const
    {
        Entry entry;
        entry.path = pathOf(name);
        entry.page = page();
        entry.kind = kind;
        entry.facets = facets;
        entry.defaultValue = std::move(defaultValue);
        return entry;
    }
};

class Getter : public PathWalker
{
public:
    explicit Getter(std::string_view path) : m_path(path) {}

    std::optional<Value> found;

    template <typename E, std::size_t N>
    void choice(std::string_view name, const E& field, const std::array<std::string_view, N>& names,
                const Facets& facets = {})
    {
        if (wanted(name, facets)) {
            found = std::string(names[static_cast<std::size_t>(field)]);
        }
    }

    template <std::size_t N>
    void language(std::string_view name, const std::string& field, const std::array<std::string_view, N>&,
                  const Facets& facets = {})
    {
        if (wanted(name, facets)) {
            found = field;
        }
    }

    template <typename T>
    void integer(std::string_view name, const T& field, std::int64_t, std::int64_t, const Facets& facets = {})
    {
        if (wanted(name, facets)) {
            found = static_cast<std::int64_t>(field);
        }
    }

    void boolean(std::string_view name, const bool& field, const Facets& facets = {})
    {
        if (wanted(name, facets)) {
            found = field;
        }
    }

    void text(std::string_view name, const std::string& field, std::size_t, const Facets& facets = {})
    {
        if (wanted(name, facets)) {
            found = field;
        }
    }

    template <typename T>
    void object(std::string_view name, const T& field, Page page = Page::None)
    {
        descend(*this, name, field, page);
    }

private:
    [[nodiscard]] bool wanted(std::string_view name, const Facets& facets) const
    {
        return !facets.title.empty() && pathOf(name) == m_path;
    }

    std::string_view m_path;
};

class Setter : public PathWalker
{
public:
    Setter(std::string_view path, const Value& value) : m_path(path), m_value(value) {}

    bool assigned = false;

    template <typename E, std::size_t N>
    void choice(std::string_view name, E& field, const std::array<std::string_view, N>& names, const Facets& facets = {})
    {
        const auto* text = wanted(name, facets) ? std::get_if<std::string>(&m_value) : nullptr;
        if (text == nullptr) {
            return;
        }
        const auto found = std::ranges::find(names, *text);
        if (found != names.end()) {
            field = static_cast<E>(found - names.begin());
            assigned = true;
        }
    }

    template <std::size_t N>
    void language(std::string_view name, std::string& field, const std::array<std::string_view, N>& names,
                  const Facets& facets = {})
    {
        const auto* text = wanted(name, facets) ? std::get_if<std::string>(&m_value) : nullptr;
        if (text != nullptr && languageAccepted(*text, names)) {
            field = *text;
            assigned = true;
        }
    }

    template <typename T>
    void integer(std::string_view name, T& field, std::int64_t min, std::int64_t max, const Facets& facets = {})
    {
        const auto* number = wanted(name, facets) ? std::get_if<std::int64_t>(&m_value) : nullptr;
        if (number != nullptr && *number >= min && *number <= max) {
            field = static_cast<T>(*number);
            assigned = true;
        }
    }

    void boolean(std::string_view name, bool& field, const Facets& facets = {})
    {
        const auto* flag = wanted(name, facets) ? std::get_if<bool>(&m_value) : nullptr;
        if (flag != nullptr) {
            field = *flag;
            assigned = true;
        }
    }

    void text(std::string_view name, std::string& field, std::size_t maxLength, const Facets& facets = {})
    {
        const auto* text = wanted(name, facets) ? std::get_if<std::string>(&m_value) : nullptr;
        if (text != nullptr && text->size() <= maxLength) {
            field = *text;
            assigned = true;
        }
    }

    template <typename T>
    void object(std::string_view name, T& field, Page page = Page::None)
    {
        descend(*this, name, field, page);
    }

private:
    [[nodiscard]] bool wanted(std::string_view name, const Facets& facets) const
    {
        return !facets.title.empty() && pathOf(name) == m_path;
    }

    std::string_view m_path;
    const Value& m_value;
};

void migrateStatusCapsule(Json& root)
{
    const auto display = root.find("display");
    if (display == root.end() || !display->is_object() || !display->contains("showStatus")) {
        return;
    }
    const Json& shown = (*display)["showStatus"];
    if (shown.is_boolean() && !shown.get<bool>() && !display->contains("statusCapsule")) {
        (*display)["statusCapsule"] = "hidden";
    }
    display->erase("showStatus");
}

void dropMouseButtons(Json& root)
{
    const auto keymap = root.find("keymap");
    if (keymap == root.end() || !keymap->is_object()) {
        return;
    }
    for (const std::string_view name : {"rightButton", "middleButton", "backButton", "forwardButton"}) {
        keymap->erase(std::string(name));
    }
}

void migrateSystemToggles(Json& root)
{
    const auto appearance = root.find("appearance");
    if (appearance == root.end() || !appearance->is_object()) {
        return;
    }
    for (const std::string_view name : {"reducedMotion", "reducedTransparency"}) {
        const auto toggle = appearance->find(std::string(name));
        if (toggle != appearance->end() && toggle->is_boolean()) {
            *toggle = toggle->get<bool>() ? "on" : "system";
        }
    }
}

void migrateDeepThinking(Json& root)
{
    const auto assistant = root.find("assistant");
    if (assistant == root.end() || !assistant->is_object() || !assistant->contains("deepThinking")) {
        return;
    }
    const Json& thinking = (*assistant)["deepThinking"];
    if (thinking.is_boolean() && thinking.get<bool>()) {
        for (const std::string_view name : kAiServiceNames) {
            Json& endpoint = (*assistant)[std::string(name)];
            if (endpoint.is_null()) {
                endpoint = Json::object();
            }
            if (endpoint.is_object() && !endpoint.contains("thinking")) {
                endpoint["thinking"] = true;
            }
        }
    }
    assistant->erase("deepThinking");
}

void migrateHiddenCaptures(Json& root)
{
    const auto devices = root.find("devices");
    if (devices == root.end() || !devices->is_object()) {
        return;
    }
    for (auto device = devices->begin(); device != devices->end(); ++device) {
        if (!device->is_object()) {
            continue;
        }
        const auto capture = device->find("capture");
        if (capture == device->end() || !capture->is_object() || !capture->contains("hidden")) {
            continue;
        }
        const Json& hidden = (*capture)["hidden"];
        if (hidden.is_boolean() && hidden.get<bool>() && !capture->contains("kind")) {
            (*capture)["kind"] = std::string(kCaptureKindNames[static_cast<std::size_t>(CaptureKind::NotCaptureCard)]);
        }
        capture->erase("hidden");
    }
}

}

const std::vector<Entry>& catalog()
{
    static const std::vector<Entry> entries = [] {
        Cataloger cataloger;
        const Settings defaults{};
        Settings::visit(cataloger, defaults);
        return std::move(cataloger.entries);
    }();
    return entries;
}

const Entry* findSetting(std::string_view path)
{
    const auto& entries = catalog();
    const auto found = std::ranges::find(entries, path, &Entry::path);
    return found == entries.end() ? nullptr : &*found;
}

std::optional<Value> valueOf(const Settings& settings, std::string_view path)
{
    Getter getter(path);
    Settings::visit(getter, settings);
    return getter.found;
}

bool assign(Settings& settings, std::string_view path, const Value& value)
{
    Setter setter(path, value);
    Settings::visit(setter, settings);
    return setter.assigned;
}

bool languageTag(std::string_view text) noexcept
{
    const auto lower = [](char c) { return c >= 'a' && c <= 'z'; };
    const auto upper = [](char c) { return c >= 'A' && c <= 'Z'; };
    const std::size_t split = text.find('_');
    const std::string_view language = text.substr(0, split);
    if (language.size() < 2 || language.size() > 3 || !std::ranges::all_of(language, lower)) {
        return false;
    }
    if (split == std::string_view::npos) {
        return true;
    }
    const std::string_view region = text.substr(split + 1);
    return region.size() == 2 && std::ranges::all_of(region, upper);
}

Settings effectiveSettings(const Settings& global, const DeviceSettings& device)
{
    Settings settings = global;
    for (const auto& [path, value] : device.overrides) {
        (void)assign(settings, path, value);
    }
    return settings;
}

bool setOverride(DeviceSettings& device, std::string_view path, const Value& value)
{
    const Entry* entry = findSetting(path);
    Settings scratch;
    if (entry == nullptr || entry->facets.scope != Scope::Device || !assign(scratch, path, value)) {
        return false;
    }
    device.overrides.insert_or_assign(std::string(path), value);
    return true;
}

bool clearOverride(DeviceSettings& device, std::string_view path)
{
    const auto found = device.overrides.find(path);
    if (found == device.overrides.end()) {
        return false;
    }
    device.overrides.erase(found);
    return true;
}

bool rememberClosedStart(DeviceSettings& device, std::string_view package)
{
    if (package.empty() || package.size() > DeviceSettings::kMaxPackageLength ||
        std::ranges::find(device.closedStarts, package) != device.closedStarts.end()) {
        return false;
    }
    device.closedStarts.emplace_back(package);
    while (device.closedStarts.size() > DeviceSettings::kMaxClosedStarts) {
        device.closedStarts.erase(device.closedStarts.begin());
    }
    return true;
}

template <typename Document>
Loaded<Document> load(std::string_view json)
{
    Loaded<Document> result;
    Context context{result.diagnostics};
    if (json.size() > kMaxBytes || !nestingWithinLimit(json)) {
        context.report(Severity::Error, Problem::TooLarge, "$");
        result.usable = false;
        return result;
    }
    Json root = Json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded()) {
        context.report(Severity::Error, Problem::MalformedJson, "$");
        result.usable = false;
        return result;
    }
    if (!root.is_object()) {
        context.report(Severity::Error, Problem::WrongFormat, "$", root.type_name());
        result.usable = false;
        return result;
    }
    if constexpr (std::is_same_v<Document, Settings>) {
        migrateStatusCapsule(root);
        dropMouseButtons(root);
        migrateSystemToggles(root);
        migrateDeepThinking(root);
    }
    if constexpr (std::is_same_v<Document, Devices>) {
        migrateHiddenCaptures(root);
    }
    Reader reader(root, "$", context);
    Document::visit(reader, result.document);
    reader.finish();
    result.usable = context.usable;
    result.newer = context.newer;
    if (!result.usable) {
        result.document = Document{};
    }
    return result;
}

template <typename Document>
std::string save(const Document& document)
{
    return writeObject(document).dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

template Loaded<Settings> load<Settings>(std::string_view);
template Loaded<Devices> load<Devices>(std::string_view);
template std::string save<Settings>(const Settings&);
template std::string save<Devices>(const Devices&);

void rememberConnection(Wireless& wireless, const std::string& address, std::int64_t now)
{
    if (address.empty() || address.size() > Wireless::kMaxAddressLength) {
        return;
    }
    wireless.recent[address].lastUsed = std::clamp<std::int64_t>(now, 0, RecentConnection::kMaxTime);
    while (wireless.recent.size() > Wireless::kMaxRecent) {
        wireless.recent.erase(std::ranges::min_element(wireless.recent, {}, [](const auto& entry) {
            return entry.second.lastUsed;
        }));
    }
}

std::string_view problemName(Problem problem) noexcept
{
    switch (problem) {
    case Problem::TooLarge:
        return "too large";
    case Problem::MalformedJson:
        return "malformed JSON";
    case Problem::WrongFormat:
        return "not a settings file";
    case Problem::WrongVersion:
        return "unknown version";
    case Problem::WrongType:
        return "wrong type";
    case Problem::OutOfRange:
        return "out of range";
    case Problem::UnknownName:
        return "unknown name";
    case Problem::UnknownField:
        return "unknown field";
    }
    return "problem";
}

}
