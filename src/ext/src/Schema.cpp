#include "gs/ext/Schema.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace gs::ext {

namespace {

using Json = nlohmann::json;

constexpr std::array<std::string_view, 7> kTypeNames{"null", "boolean", "number", "integer", "string", "array", "object"};
constexpr std::array<std::string_view, 10> kKeywords{"type",    "properties", "required",  "items", "enum",
                                                     "minimum", "maximum",    "maxLength", "title", "description"};
constexpr std::size_t kMostProperties = 64;
constexpr std::size_t kMostChoices = 64;

bool nestingWithin(std::string_view text, std::size_t most) noexcept
{
    std::size_t depth = 0;
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
            if (++depth > most) {
                return false;
            }
        } else if ((c == '}' || c == ']') && depth > 0) {
            --depth;
        }
    }
    return true;
}

std::size_t characters(std::string_view text) noexcept
{
    return static_cast<std::size_t>(
        std::ranges::count_if(text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

bool plainKey(std::string_view key) noexcept
{
    const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    return !key.empty() && key.size() <= 64 && letter(key.front()) &&
           std::ranges::all_of(key, [&letter](char c) { return letter(c) || (c >= '0' && c <= '9'); });
}

std::string member(const std::string& path, const std::string& key)
{
    return plainKey(key) ? path + "." + key
                         : path + "[" + Json(key).dump(-1, ' ', false, Json::error_handler_t::replace) + "]";
}

bool knownType(const Json& name)
{
    return name.is_string() && std::ranges::find(kTypeNames, name.get_ref<const std::string&>()) != kTypeNames.end();
}

bool finiteNumber(const Json& node)
{
    return node.is_number() && std::isfinite(node.get<double>());
}

bool wholeNumber(const Json& node)
{
    if (node.is_number_integer()) {
        return true;
    }
    if (!node.is_number_float()) {
        return false;
    }
    const double value = node.get<double>();
    return std::isfinite(value) && std::floor(value) == value;
}

bool typed(const Json& value, std::string_view type)
{
    if (type == "null") {
        return value.is_null();
    }
    if (type == "boolean") {
        return value.is_boolean();
    }
    if (type == "number") {
        return finiteNumber(value);
    }
    if (type == "integer") {
        return wholeNumber(value);
    }
    if (type == "string") {
        return value.is_string();
    }
    if (type == "array") {
        return value.is_array();
    }
    return value.is_object();
}

std::string typeTitle(std::string_view type)
{
    if (type == "null") {
        return "null";
    }
    if (type == "boolean") {
        return "true or false";
    }
    if (type == "number") {
        return "a number";
    }
    if (type == "integer") {
        return "a whole number";
    }
    if (type == "string") {
        return "text";
    }
    if (type == "array") {
        return "a list";
    }
    return "an object";
}

std::string checkSchema(const Json& schema, const std::string& path, std::size_t depth)
{
    if (!schema.is_object()) {
        return path + " is not an object";
    }
    if (depth > kMostSchemaDepth) {
        return path + " is nested more than 16 levels deep";
    }
    for (auto it = schema.begin(); it != schema.end(); ++it) {
        if (std::ranges::find(kKeywords, it.key()) == kKeywords.end()) {
            return member(path, it.key()) + " is not a keyword GameScrcpy checks: use type, properties, required, "
                                            "items, enum, minimum, maximum, maxLength, title and description";
        }
    }
    if (const auto type = schema.find("type"); type != schema.end()) {
        if (type->is_array() && !type->empty()) {
            std::set<std::string> seen;
            for (const Json& each : *type) {
                if (!knownType(each) || !seen.insert(each.get<std::string>()).second) {
                    return path + ".type lists a name that is not a JSON type, or one twice";
                }
            }
        } else if (!knownType(*type)) {
            return path + ".type is not one of null, boolean, number, integer, string, array and object, or a list "
                          "of them";
        }
    }
    if (const auto properties = schema.find("properties"); properties != schema.end()) {
        if (!properties->is_object()) {
            return path + ".properties is not an object";
        }
        if (properties->size() > kMostProperties) {
            return path + ".properties has more than 64 entries";
        }
        for (const auto& [key, child] : properties->items()) {
            if (std::string problem = checkSchema(child, member(path + ".properties", key), depth + 1);
                !problem.empty()) {
                return problem;
            }
        }
    }
    if (const auto required = schema.find("required"); required != schema.end()) {
        if (!required->is_array()) {
            return path + ".required is not a list";
        }
        std::set<std::string> seen;
        for (const Json& each : *required) {
            if (!each.is_string() || !seen.insert(each.get<std::string>()).second) {
                return path + ".required is not a list of different names";
            }
        }
    }
    if (const auto items = schema.find("items"); items != schema.end()) {
        if (std::string problem = checkSchema(*items, path + ".items", depth + 1); !problem.empty()) {
            return problem;
        }
    }
    if (const auto choices = schema.find("enum"); choices != schema.end()) {
        if (!choices->is_array() || choices->empty() || choices->size() > kMostChoices) {
            return path + ".enum is not a list of 1 to 64 values";
        }
    }
    const auto low = schema.find("minimum");
    const auto high = schema.find("maximum");
    if (low != schema.end() && !finiteNumber(*low)) {
        return path + ".minimum is not a number";
    }
    if (high != schema.end() && !finiteNumber(*high)) {
        return path + ".maximum is not a number";
    }
    if (low != schema.end() && high != schema.end() && low->get<double>() > high->get<double>()) {
        return path + ".minimum is above maximum";
    }
    if (const auto length = schema.find("maxLength");
        length != schema.end() && !(wholeNumber(*length) && length->get<double>() >= 0.0)) {
        return path + ".maxLength is not a whole number of at least 0";
    }
    for (const char* text : {"title", "description"}) {
        if (const auto found = schema.find(text); found != schema.end() && !found->is_string()) {
            return path + "." + text + " is not text";
        }
    }
    return {};
}

std::string checkValue(const Json& schema, const Json& value, const std::string& path)
{
    if (const auto type = schema.find("type"); type != schema.end()) {
        std::string wanted;
        bool fits = false;
        const Json listed = type->is_array() ? *type : Json::array({*type});
        for (const Json& each : listed) {
            const std::string& name = each.get_ref<const std::string&>();
            fits = fits || typed(value, name);
            wanted += (wanted.empty() ? "" : " or ") + typeTitle(name);
        }
        if (!fits) {
            return path + " is not " + wanted;
        }
    }
    if (const auto choices = schema.find("enum");
        choices != schema.end() && std::find(choices->begin(), choices->end(), value) == choices->end()) {
        return path + " is not one of the values that the schema lists";
    }
    if (value.is_number()) {
        const double number = value.get<double>();
        if (const auto low = schema.find("minimum"); low != schema.end() && number < low->get<double>()) {
            return path + " is below " + low->dump();
        }
        if (const auto high = schema.find("maximum"); high != schema.end() && number > high->get<double>()) {
            return path + " is above " + high->dump();
        }
    }
    if (value.is_string()) {
        if (const auto most = schema.find("maxLength");
            most != schema.end() &&
            static_cast<double>(characters(value.get_ref<const std::string&>())) > most->get<double>()) {
            return path + " is longer than " + most->dump() + " characters";
        }
    }
    if (value.is_object()) {
        if (const auto required = schema.find("required"); required != schema.end()) {
            for (const Json& name : *required) {
                if (!value.contains(name.get_ref<const std::string&>())) {
                    return member(path, name.get<std::string>()) + " is missing";
                }
            }
        }
        if (const auto properties = schema.find("properties"); properties != schema.end()) {
            for (const auto& [key, child] : properties->items()) {
                if (const auto found = value.find(key); found != value.end()) {
                    if (std::string problem = checkValue(child, *found, member(path, key)); !problem.empty()) {
                        return problem;
                    }
                }
            }
        }
    }
    if (value.is_array()) {
        if (const auto items = schema.find("items"); items != schema.end()) {
            for (std::size_t index = 0; index < value.size(); ++index) {
                if (std::string problem = checkValue(*items, value[index], path + "[" + std::to_string(index) + "]");
                    !problem.empty()) {
                    return problem;
                }
            }
        }
    }
    return {};
}

}

std::string schemaProblem(std::string_view schema)
{
    if (schema.size() > kMostSchemaBytes) {
        return "the schema is larger than 16 KiB";
    }
    if (!nestingWithin(schema, kMostValueDepth)) {
        return "the schema is nested more than 64 levels deep";
    }
    const Json parsed = Json::parse(schema.begin(), schema.end(), nullptr, false);
    if (parsed.is_discarded()) {
        return "the schema is not JSON";
    }
    return checkSchema(parsed, "$", 0);
}

std::string valueProblem(std::string_view schema, std::string_view value)
{
    if (!nestingWithin(value, kMostValueDepth)) {
        return "$ is nested more than 64 levels deep";
    }
    const Json parsed = Json::parse(value.begin(), value.end(), nullptr, false);
    if (parsed.is_discarded()) {
        return "$ is not JSON";
    }
    if (schema.empty()) {
        return {};
    }
    if (!schemaProblem(schema).empty()) {
        return "the schema itself is not valid";
    }
    return checkValue(Json::parse(schema.begin(), schema.end(), nullptr, false), parsed, "$");
}

}
