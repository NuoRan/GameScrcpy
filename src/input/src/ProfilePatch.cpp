#include "gs/input/ProfilePatch.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace gs::input {

namespace {

using Json = nlohmann::ordered_json;

constexpr std::size_t kMaxPatchBytes = 1024 * 1024;
constexpr std::array<std::string_view, 2> kMerged{"meta", "screen"};
constexpr std::array<std::string_view, 2> kSections{"controls", "vars"};
constexpr std::array<std::string_view, 1> kReplaced{"gamepad"};
constexpr std::array<std::string_view, 3> kOperations{"remove", "change", "add"};

bool listed(const auto& names, std::string_view name)
{
    return std::ranges::find(names, name) != names.end();
}

Json::iterator find(Json& list, const std::string& id)
{
    return std::find_if(list.begin(), list.end(), [&](const Json& item) {
        const auto found = item.find("id");
        return found != item.end() && found->is_string() && found->get_ref<const std::string&>() == id;
    });
}

class Patcher
{
public:
    Patcher(Json& profile, PatchOutcome& outcome)
        : m_profile(profile)
        , m_outcome(outcome)
    {
    }

    void apply(const Json& patch)
    {
        for (const auto& [key, value] : patch.items()) {
            if (key == "format" || key == "version") {
                continue;
            }
            if (listed(kMerged, key)) {
                merge(key, value);
            } else if (listed(kSections, key)) {
                section(key, value);
            } else if (key == "states") {
                section("vars", value);
            } else if (listed(kReplaced, key)) {
                replace(key, value);
            } else {
                warn(DiagnosticCode::UnknownField, "$." + key);
            }
        }
    }

    [[nodiscard]] bool failed() const noexcept { return m_failed; }

private:
    void fail(DiagnosticCode code, std::string path, std::string detail = {})
    {
        m_failed = true;
        m_outcome.diagnostics.push_back({Severity::Error, code, std::move(path), std::move(detail)});
    }

    void warn(DiagnosticCode code, std::string path, std::string detail = {})
    {
        m_outcome.diagnostics.push_back({Severity::Warning, code, std::move(path), std::move(detail)});
    }

    void touch(const std::string& section, const std::string& id)
    {
        std::string key = section + "/" + id;
        if (std::ranges::find(m_outcome.touched, key) == m_outcome.touched.end()) {
            m_outcome.touched.push_back(std::move(key));
        }
    }

    void merge(const std::string& key, const Json& change)
    {
        if (!change.is_object()) {
            fail(DiagnosticCode::WrongType, "$." + key, change.type_name());
            return;
        }
        Json& target = m_profile[key];
        if (!target.is_object()) {
            target = Json::object();
        }
        target.merge_patch(change);
    }

    void replace(const std::string& key, const Json& value)
    {
        if (value.is_null()) {
            m_profile.erase(key);
        } else {
            m_profile[key] = value;
        }
    }

    void section(const std::string& name, const Json& operations)
    {
        const std::string path = "$." + name;
        if (!operations.is_object()) {
            fail(DiagnosticCode::WrongType, path, operations.type_name());
            return;
        }
        for (const auto& [operation, value] : operations.items()) {
            if (!listed(kOperations, operation)) {
                warn(DiagnosticCode::UnknownField, path + "." + operation);
            }
        }
        Json& list = m_profile[name];
        if (list.is_null()) {
            list = Json::array();
        }
        if (!list.is_array()) {
            fail(DiagnosticCode::PatchBreaksProfile, path, list.type_name());
            return;
        }
        if (const auto removed = operations.find("remove"); removed != operations.end()) {
            remove(name, list, *removed);
        }
        if (const auto changed = operations.find("change"); changed != operations.end()) {
            change(name, list, *changed);
        }
        if (const auto added = operations.find("add"); added != operations.end()) {
            add(name, list, *added);
        }
    }

    void remove(const std::string& name, Json& list, const Json& ids)
    {
        const std::string path = "$." + name + ".remove";
        if (!ids.is_array()) {
            fail(DiagnosticCode::WrongType, path, ids.type_name());
            return;
        }
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const std::string at = path + "[" + std::to_string(i) + "]";
            if (!ids[i].is_string()) {
                fail(DiagnosticCode::WrongType, at, ids[i].type_name());
                continue;
            }
            const std::string& id = ids[i].get_ref<const std::string&>();
            const auto found = find(list, id);
            if (found == list.end()) {
                warn(DiagnosticCode::UnknownReference, at, id);
                continue;
            }
            list.erase(found);
            touch(name, id);
        }
    }

    void change(const std::string& name, Json& list, const Json& changes)
    {
        const std::string path = "$." + name + ".change";
        if (!changes.is_object()) {
            fail(DiagnosticCode::WrongType, path, changes.type_name());
            return;
        }
        for (const auto& [id, fields] : changes.items()) {
            const std::string at = path + "." + id;
            if (!fields.is_object()) {
                fail(DiagnosticCode::WrongType, at, fields.type_name());
                continue;
            }
            const auto found = find(list, id);
            if (found == list.end()) {
                warn(DiagnosticCode::UnknownReference, at, id);
                continue;
            }
            Json merged = fields;
            if (merged.erase("id") > 0) {
                warn(DiagnosticCode::InvalidValue, at + ".id", id);
            }
            found->merge_patch(merged);
            touch(name, id);
        }
    }

    void add(const std::string& name, Json& list, const Json& items)
    {
        const std::string path = "$." + name + ".add";
        if (!items.is_array()) {
            fail(DiagnosticCode::WrongType, path, items.type_name());
            return;
        }
        for (std::size_t i = 0; i < items.size(); ++i) {
            const std::string at = path + "[" + std::to_string(i) + "]";
            const Json& item = items[i];
            if (!item.is_object()) {
                fail(DiagnosticCode::WrongType, at, item.type_name());
                continue;
            }
            const auto id = item.find("id");
            if (id == item.end()) {
                fail(DiagnosticCode::MissingField, at + ".id");
                continue;
            }
            if (!id->is_string()) {
                fail(DiagnosticCode::WrongType, at + ".id", id->type_name());
                continue;
            }
            const std::string& text = id->get_ref<const std::string&>();
            if (find(list, text) != list.end()) {
                warn(DiagnosticCode::DuplicateId, at + ".id", text);
                continue;
            }
            list.push_back(item);
            touch(name, text);
        }
    }

    Json& m_profile;
    PatchOutcome& m_outcome;
    bool m_failed = false;
};

}

PatchOutcome applyPatch(std::string_view profile, std::string_view patch)
{
    PatchOutcome outcome;
    outcome.profile = std::string(profile);
    const auto reject = [&outcome](DiagnosticCode code, std::string path, std::string detail = {}) {
        outcome.diagnostics.push_back({Severity::Error, code, std::move(path), std::move(detail)});
        outcome.touched.clear();
    };
    if (patch.size() > kMaxPatchBytes) {
        reject(DiagnosticCode::TooLarge, "$");
        return outcome;
    }
    const Json document = Json::parse(patch.begin(), patch.end(), nullptr, false);
    if (document.is_discarded()) {
        reject(DiagnosticCode::MalformedJson, "$");
        return outcome;
    }
    if (!document.is_object()) {
        reject(DiagnosticCode::WrongType, "$", document.type_name());
        return outcome;
    }
    const auto format = document.find("format");
    if (format == document.end()) {
        reject(DiagnosticCode::MissingField, "$.format");
        return outcome;
    }
    if (!format->is_string() || format->get_ref<const std::string&>() != kPatchFormatName) {
        reject(DiagnosticCode::WrongFormat, "$.format");
        return outcome;
    }
    const auto version = document.find("version");
    if (version == document.end()) {
        reject(DiagnosticCode::MissingField, "$.version");
        return outcome;
    }
    if (!version->is_number_integer() || version->get<std::int64_t>() != kPatchFormatVersion) {
        reject(DiagnosticCode::WrongVersion, "$.version");
        return outcome;
    }
    const std::string base = upgradeProfile(profile);
    Json target = Json::parse(base.begin(), base.end(), nullptr, false);
    if (!target.is_object()) {
        reject(DiagnosticCode::PatchBreaksProfile, "$");
        return outcome;
    }
    Patcher patcher(target, outcome);
    patcher.apply(document);
    if (patcher.failed()) {
        outcome.touched.clear();
        return outcome;
    }
    const LoadResult loaded = loadProfile(target.dump(-1, ' ', false, Json::error_handler_t::replace));
    if (!loaded.ok()) {
        const auto error = std::ranges::find(loaded.diagnostics, Severity::Error, &Diagnostic::severity);
        reject(DiagnosticCode::PatchBreaksProfile, error->path, error->detail);
        return outcome;
    }
    outcome.applied = true;
    outcome.profile = saveProfile(loaded.profile);
    return outcome;
}

}
