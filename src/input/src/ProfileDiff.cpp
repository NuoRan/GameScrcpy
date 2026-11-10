#include "gs/input/ProfileDiff.h"

#include <gs/input/ProfilePatch.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::input {

namespace {

using Json = nlohmann::ordered_json;

constexpr std::array<std::string_view, 2> kHeader{"format", "version"};
constexpr std::array<std::string_view, 2> kMerged{"meta", "screen"};
constexpr std::array<std::string_view, 2> kSections{"controls", "vars"};
constexpr std::array<std::string_view, 1> kReplaced{"gamepad"};

bool listed(const auto& names, std::string_view name)
{
    return std::ranges::find(names, name) != names.end();
}

const Json& member(const Json& object, const std::string& key, const Json& fallback)
{
    const auto found = object.find(key);
    return found != object.end() ? *found : fallback;
}

std::optional<std::string> idOf(const Json& element)
{
    const auto id = element.find("id");
    if (id == element.end() || !id->is_string() || id->get_ref<const std::string&>().empty()) {
        return std::nullopt;
    }
    return id->get<std::string>();
}

std::optional<std::size_t> positionOf(const Json& list, const std::string& id)
{
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (idOf(list[i]) == id) {
            return i;
        }
    }
    return std::nullopt;
}

Json mergeDiff(const Json& from, const Json& to)
{
    Json patch = Json::object();
    for (const auto& [key, value] : from.items()) {
        if (!to.contains(key)) {
            patch[key] = nullptr;
        }
    }
    for (const auto& [key, value] : to.items()) {
        const auto before = from.find(key);
        if (before == from.end()) {
            patch[key] = value;
        } else if (before->is_object() && value.is_object()) {
            Json nested = mergeDiff(*before, value);
            if (!nested.empty()) {
                patch[key] = std::move(nested);
            }
        } else if (*before != value) {
            patch[key] = value;
        }
    }
    return patch;
}

struct SectionDiff {
    Json operations = Json::object();
    bool exact = true;
};

SectionDiff diffSection(const Json& from, const Json& to)
{
    SectionDiff diff;
    std::vector<std::size_t> anonymous;
    for (std::size_t i = 0; i < from.size(); ++i) {
        if (!idOf(from[i])) {
            anonymous.push_back(i);
        }
    }
    std::vector<bool> kept(from.size(), false);
    std::size_t nextAnonymous = 0;
    std::optional<std::size_t> last;
    Json changes = Json::object();
    std::size_t prefix = 0;
    for (; prefix < to.size(); ++prefix) {
        const Json& element = to[prefix];
        const std::optional<std::string> id = idOf(element);
        std::optional<std::size_t> at;
        if (id) {
            at = positionOf(from, *id);
        } else if (nextAnonymous < anonymous.size() && from[anonymous[nextAnonymous]] == element) {
            at = anonymous[nextAnonymous];
        }
        if (!at || (last && *at <= *last)) {
            break;
        }
        if (id) {
            Json change = mergeDiff(from[*at], element);
            if (!change.empty()) {
                changes[*id] = std::move(change);
            }
        } else {
            ++nextAnonymous;
        }
        kept[*at] = true;
        last = at;
    }
    Json removed = Json::array();
    for (std::size_t i = 0; i < from.size(); ++i) {
        if (kept[i]) {
            continue;
        }
        if (const std::optional<std::string> id = idOf(from[i])) {
            removed.push_back(*id);
        } else {
            diff.exact = false;
        }
    }
    Json added = Json::array();
    for (std::size_t i = prefix; i < to.size(); ++i) {
        if (idOf(to[i])) {
            added.push_back(to[i]);
        } else {
            diff.exact = false;
        }
    }
    if (!removed.empty()) {
        diff.operations["remove"] = std::move(removed);
    }
    if (!changes.empty()) {
        diff.operations["change"] = std::move(changes);
    }
    if (!added.empty()) {
        diff.operations["add"] = std::move(added);
    }
    return diff;
}

bool patchable(std::string_view key)
{
    return listed(kHeader, key) || listed(kMerged, key) || listed(kSections, key) || listed(kReplaced, key);
}

}

ProfileDiff diffProfiles(const Profile& original, const Profile& changed)
{
    ProfileDiff diff;
    Json patch = {{"format", std::string(kPatchFormatName)}, {"version", kPatchFormatVersion}};
    const Json from = Json::parse(saveProfile(original), nullptr, false);
    const Json to = Json::parse(saveProfile(changed), nullptr, false);
    if (!from.is_object() || !to.is_object()) {
        diff.exact = false;
        diff.patch = patch.dump(2) + "\n";
        return diff;
    }
    const Json noObject = Json::object();
    const Json noList = Json::array();
    for (const std::string_view name : kMerged) {
        const std::string key(name);
        Json change = mergeDiff(member(from, key, noObject), member(to, key, noObject));
        if (!change.empty()) {
            patch[key] = std::move(change);
        }
    }
    const Json unset;
    for (const std::string_view name : kReplaced) {
        const std::string key(name);
        const Json& after = member(to, key, unset);
        if (member(from, key, unset) != after) {
            patch[key] = after;
        }
    }
    for (const std::string_view name : kSections) {
        const std::string key(name);
        SectionDiff section = diffSection(member(from, key, noList), member(to, key, noList));
        diff.exact = diff.exact && section.exact;
        if (!section.operations.empty()) {
            patch[key] = std::move(section.operations);
        }
    }
    const Json nothing;
    for (const auto& [key, value] : to.items()) {
        if (!patchable(key) && member(from, key, nothing) != value) {
            diff.exact = false;
        }
    }
    for (const auto& [key, value] : from.items()) {
        if (!patchable(key) && !to.contains(key)) {
            diff.exact = false;
        }
    }
    diff.empty = patch.size() == 2;
    diff.patch = patch.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
    return diff;
}

}
