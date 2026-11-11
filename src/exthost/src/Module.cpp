#include "gs/exthost/Module.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace gs::exthost {

std::optional<ValueType> valueTypeNamed(std::string_view name) noexcept
{
    const auto found = std::ranges::find(kValueTypeNames, name);
    if (found == kValueTypeNames.end()) {
        return std::nullopt;
    }
    return static_cast<ValueType>(found - kValueTypeNames.begin() + 1);
}

std::string_view valueTypeName(ValueType type) noexcept
{
    if (type == ValueType::None) {
        return "nothing";
    }
    return kValueTypeNames[static_cast<std::size_t>(type) - 1];
}

bool validColor(std::string_view text) noexcept
{
    return text.size() == 7 && text.front() == '#' && std::ranges::all_of(text.substr(1), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
           });
}

bool validTopic(std::string_view topic) noexcept
{
    return !topic.empty() && topic.size() <= kMostTopicBytes && std::ranges::all_of(topic, [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
           });
}

bool appTopic(std::string_view topic) noexcept
{
    return std::ranges::find(kAppTopics, topic) != kAppTopics.end();
}

bool phoneTopic(std::string_view topic) noexcept
{
    return appTopic(topic) && std::ranges::find(kAppWideTopics, topic) == kAppWideTopics.end();
}

bool reservedTopic(std::string_view topic) noexcept
{
    return std::ranges::any_of(kAppTopics, [topic](std::string_view known) {
        return topic.starts_with(known.substr(0, known.find('.') + 1));
    });
}

bool validResource(std::string_view resource) noexcept
{
    constexpr std::array<std::string_view, 4> kKinds{"serial:", "ble:", "hid:", "usb:"};
    if (resource.size() > kMostResourceBytes || !validUtf8(resource) ||
        std::ranges::any_of(resource, [](char c) { return static_cast<unsigned char>(c) < 0x20; })) {
        return false;
    }
    return std::ranges::any_of(kKinds, [resource](std::string_view kind) {
        return resource.size() > kind.size() &&
               std::ranges::equal(resource.substr(0, kind.size()), kind, [](char given, char wanted) {
                   return (given >= 'A' && given <= 'Z' ? static_cast<char>(given - 'A' + 'a') : given) == wanted;
               });
    });
}

bool validUtf8(std::string_view text) noexcept
{
    std::size_t at = 0;
    while (at < text.size()) {
        const auto lead = static_cast<unsigned char>(text[at]);
        std::size_t extra = 0;
        std::uint32_t code = 0;
        if (lead < 0x80) {
            ++at;
            continue;
        }
        if ((lead & 0xE0) == 0xC0) {
            extra = 1;
            code = lead & 0x1Fu;
        } else if ((lead & 0xF0) == 0xE0) {
            extra = 2;
            code = lead & 0x0Fu;
        } else if ((lead & 0xF8) == 0xF0) {
            extra = 3;
            code = lead & 0x07u;
        } else {
            return false;
        }
        if (at + extra >= text.size()) {
            return false;
        }
        for (std::size_t index = 1; index <= extra; ++index) {
            const auto next = static_cast<unsigned char>(text[at + index]);
            if ((next & 0xC0) != 0x80) {
                return false;
            }
            code = (code << 6) | (next & 0x3Fu);
        }
        const bool overlong =
            (extra == 1 && code < 0x80) || (extra == 2 && code < 0x800) || (extra == 3 && code < 0x10000);
        if (overlong || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
            return false;
        }
        at += extra + 1;
    }
    return true;
}

std::string checkValue(const Value& value, ValueType expected)
{
    if (value.type != expected) {
        return "a " + std::string(valueTypeName(expected)) + " belongs here, not " +
               (value.type == ValueType::None ? std::string("nothing") : "a " + std::string(valueTypeName(value.type)));
    }
    switch (value.type) {
    case ValueType::None:
    case ValueType::Boolean:
        return {};
    case ValueType::Number:
        return std::isfinite(value.number) ? std::string() : std::string("the number is not finite");
    case ValueType::Duration:
        return std::isfinite(value.number) && value.number >= 0.0 ? std::string()
                                                                  : std::string("the duration is not 0 or more milliseconds");
    case ValueType::Point:
        return std::isfinite(value.x) && std::isfinite(value.y) ? std::string() : std::string("the point is not finite");
    case ValueType::Region:
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.width) &&
                       std::isfinite(value.height) && value.width >= 0.0 && value.height >= 0.0
                   ? std::string()
                   : std::string("the region is not finite or has a negative size");
    case ValueType::Color:
        return validColor(value.text) ? std::string() : std::string("the color is not written #RRGGBB");
    case ValueType::String:
        if (value.text.size() > kMostTextBytes) {
            return "the text is longer than " + std::to_string(kMostTextBytes) + " bytes";
        }
        return validUtf8(value.text) ? std::string() : std::string("the text is not UTF-8");
    case ValueType::Json:
        if (value.text.size() > kMostTextBytes) {
            return "the JSON text is longer than " + std::to_string(kMostTextBytes) + " bytes";
        }
        return validUtf8(value.text) && nlohmann::json::accept(value.text) ? std::string()
                                                                            : std::string("the text is not valid JSON");
    }
    return "an unknown type";
}

}
