#include "gs/ext/Bundle.h"

#include "gs/ext/Archive.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <set>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

namespace gs::ext {

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

constexpr std::size_t kMaxNameCharacters = 100;
constexpr std::size_t kMaxDescriptionCharacters = 1000;
constexpr std::size_t kMaxVersionCharacters = 64;
constexpr std::size_t kMaxValues = 128;
constexpr std::size_t kMaxValueKeyBytes = 64;
constexpr std::size_t kMaxValueTextCharacters = 4096;
constexpr std::size_t kMostReportsPerCode = 50;
constexpr std::size_t kMostKeyBytes = 64;

std::size_t characters(std::string_view text) noexcept
{
    return static_cast<std::size_t>(
        std::ranges::count_if(text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

fs::path pathOf(std::string_view text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string utf8(const fs::path& path)
{
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::string keyed(const std::string& path, const std::string& key)
{
    std::string shown = key;
    if (shown.size() > kMostKeyBytes) {
        std::size_t end = kMostKeyBytes;
        while (end > 0 && (static_cast<unsigned char>(shown[end]) & 0xC0) == 0x80) {
            --end;
        }
        shown = shown.substr(0, end) + "\u2026";
    }
    return path + "[" + Json(shown).dump(-1, ' ', false, Json::error_handler_t::replace) + "]";
}

class Reader
{
public:
    explicit Reader(std::vector<Diagnostic>& diagnostics) : m_diagnostics(diagnostics) {}

    void report(Severity severity, Code code, std::string path, std::string detail = {})
    {
        if (++m_reported[code] <= kMostReportsPerCode) {
            m_diagnostics.push_back({severity, code, std::move(path), std::move(detail)});
        } else {
            ++m_dropped[code];
        }
    }

    void finish()
    {
        for (const auto& [code, count] : m_dropped) {
            m_diagnostics.push_back({Severity::Warning, code, "$", std::to_string(count) + " more"});
        }
        m_dropped.clear();
    }

    std::string text(const Json& object, const std::string& key, const std::string& path, bool required,
                     std::size_t most)
    {
        const auto found = object.find(key);
        if (found == object.end()) {
            if (required) {
                report(Severity::Error, Code::MissingField, path);
            }
            return {};
        }
        if (!found->is_string()) {
            report(Severity::Error, Code::WrongType, path, found->type_name());
            return {};
        }
        std::string value = found->get<std::string>();
        if (characters(value) > most) {
            report(Severity::Error, Code::TooLong, path, std::to_string(most));
            return {};
        }
        return value;
    }

    bool flag(const Json& object, const std::string& key, const std::string& path, bool fallback)
    {
        const auto found = object.find(key);
        if (found == object.end()) {
            return fallback;
        }
        if (!found->is_boolean()) {
            report(Severity::Error, Code::WrongType, path, found->type_name());
            return fallback;
        }
        return found->get<bool>();
    }

    void unknown(const Json& object, const std::string& path, std::initializer_list<std::string_view> known)
    {
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (std::ranges::find(known, std::string_view(it.key())) == known.end()) {
                report(Severity::Warning, Code::UnknownField, keyed(path, it.key()));
            }
        }
    }

    std::optional<SettingValue> scalar(const Json& value, const std::string& path)
    {
        if (value.is_boolean()) {
            return SettingValue(value.get<bool>());
        }
        if (value.is_number_unsigned() &&
            value.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            report(Severity::Error, Code::OutOfRange, path);
            return std::nullopt;
        }
        if (value.is_number_integer()) {
            return SettingValue(value.get<std::int64_t>());
        }
        if (value.is_number_float() && std::isfinite(value.get<double>())) {
            return SettingValue(value.get<double>());
        }
        if (value.is_string()) {
            std::string text = value.get<std::string>();
            if (characters(text) > kMaxValueTextCharacters) {
                report(Severity::Error, Code::TooLong, path, std::to_string(kMaxValueTextCharacters));
                return std::nullopt;
            }
            return SettingValue(std::move(text));
        }
        report(Severity::Error, Code::WrongType, path, value.type_name());
        return std::nullopt;
    }

private:
    std::vector<Diagnostic>& m_diagnostics;
    std::map<Code, std::size_t> m_reported;
    std::map<Code, std::size_t> m_dropped;
};

std::optional<BundleEntry> entryOf(Reader& reader, const Json& item, const std::string& path)
{
    if (!item.is_object()) {
        reader.report(Severity::Error, Code::NotAnObject, path, item.type_name());
        return std::nullopt;
    }
    BundleEntry entry;
    bool valid = true;
    entry.id = reader.text(item, "id", path + ".id", true, kMaxIdLength);
    if (entry.id.empty()) {
        valid = false;
    } else if (!validExtensionId(entry.id)) {
        reader.report(Severity::Error, Code::IdInvalid, path + ".id", entry.id);
        valid = false;
    }
    const std::string version = reader.text(item, "version", path + ".version", true, kMaxVersionCharacters);
    if (const std::optional<Version> parsed = Version::parse(version)) {
        entry.version = *parsed;
    } else {
        if (!version.empty()) {
            reader.report(Severity::Error, Code::VersionInvalid, path + ".version", version);
        }
        valid = false;
    }
    entry.builtin = reader.flag(item, "builtin", path + ".builtin", false);
    entry.enabled = reader.flag(item, "enabled", path + ".enabled", true);
    if (const auto values = item.find("values"); values != item.end()) {
        if (!values->is_object()) {
            reader.report(Severity::Error, Code::WrongType, path + ".values", values->type_name());
        } else if (values->size() > kMaxValues) {
            reader.report(Severity::Error, Code::TooMany, path + ".values", std::to_string(kMaxValues));
        } else {
            for (auto it = values->begin(); it != values->end(); ++it) {
                const std::string where = keyed(path + ".values", it.key());
                if (it.key().empty() || it.key().size() > kMaxValueKeyBytes) {
                    reader.report(Severity::Error, Code::SettingKeyInvalid, where);
                    continue;
                }
                if (std::optional<SettingValue> value = reader.scalar(it.value(), where)) {
                    entry.values.emplace(it.key(), std::move(*value));
                }
            }
        }
    }
    reader.unknown(item, path, {"id", "version", "builtin", "enabled", "values"});
    return valid ? std::optional<BundleEntry>(std::move(entry)) : std::nullopt;
}

}

LoadedBundle loadBundle(std::string_view json)
{
    LoadedBundle result;
    Reader reader(result.diagnostics);
    if (json.size() > kMaxBundleIndexBytes) {
        reader.report(Severity::Error, Code::TooLarge, "$", std::to_string(kMaxBundleIndexBytes));
        return result;
    }
    const Json root = Json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded()) {
        reader.report(Severity::Error, Code::MalformedJson, "$");
        return result;
    }
    if (!root.is_object()) {
        reader.report(Severity::Error, Code::NotAnObject, "$", root.type_name());
        return result;
    }
    if (const auto schema = root.find("schemaVersion"); schema == root.end()) {
        reader.report(Severity::Error, Code::MissingField, "$.schemaVersion");
    } else if (!schema->is_number_integer()) {
        reader.report(Severity::Error, Code::WrongType, "$.schemaVersion", schema->type_name());
    } else if (*schema != kBundleSchemaVersion) {
        reader.report(Severity::Error, Code::SchemaVersion, "$.schemaVersion", schema->dump());
    }
    result.bundle.name = reader.text(root, "name", "$.name", true, kMaxNameCharacters);
    result.bundle.description = reader.text(root, "description", "$.description", false, kMaxDescriptionCharacters);
    if (const auto list = root.find("extensions"); list == root.end()) {
        reader.report(Severity::Error, Code::MissingField, "$.extensions");
    } else if (!list->is_array()) {
        reader.report(Severity::Error, Code::WrongType, "$.extensions", list->type_name());
    } else if (list->size() > kMaxBundleExtensions) {
        reader.report(Severity::Error, Code::TooMany, "$.extensions", std::to_string(kMaxBundleExtensions));
    } else {
        std::set<std::string> seen;
        for (std::size_t index = 0; index < list->size(); ++index) {
            const std::string path = "$.extensions[" + std::to_string(index) + "]";
            std::optional<BundleEntry> entry = entryOf(reader, (*list)[index], path);
            if (!entry) {
                continue;
            }
            if (!seen.insert(entry->id).second) {
                reader.report(Severity::Error, Code::Duplicate, path + ".id", entry->id);
                continue;
            }
            result.bundle.extensions.push_back(std::move(*entry));
        }
    }
    reader.unknown(root, "$", {"schemaVersion", "name", "description", "extensions"});
    reader.finish();
    result.usable = !hasErrors(result.diagnostics);
    return result;
}

std::string saveBundle(const Bundle& bundle)
{
    Json root = Json::object();
    root["schemaVersion"] = bundle.schemaVersion;
    root["name"] = bundle.name;
    if (!bundle.description.empty()) {
        root["description"] = bundle.description;
    }
    Json list = Json::array();
    for (const BundleEntry& entry : bundle.extensions) {
        Json item = {{"id", entry.id}, {"version", entry.version.text()}};
        if (entry.builtin) {
            item["builtin"] = true;
        }
        item["enabled"] = entry.enabled;
        if (!entry.values.empty()) {
            Json values = Json::object();
            for (const auto& [key, value] : entry.values) {
                std::visit(
                    [&values, &key](const auto& held) {
                        if constexpr (!std::is_same_v<std::decay_t<decltype(held)>, std::monostate>) {
                            values[key] = held;
                        }
                    },
                    value);
            }
            item["values"] = std::move(values);
        }
        list.push_back(std::move(item));
    }
    root["extensions"] = std::move(list);
    return root.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

std::string bundlePackagePath(const BundleEntry& entry)
{
    return "extensions/" + entry.id + "-" + entry.version.text() + ".gsext";
}

BundleCheck checkBundle(const std::filesystem::path& archive)
{
    BundleCheck check;
    const ArchiveListing listing = inspectArchive(archive);
    check.diagnostics = listing.problems;
    if (!listing.ok()) {
        return check;
    }
    const auto index = std::ranges::find(listing.files, std::string(kBundleIndexName), &ArchiveEntry::path);
    if (index == listing.files.end()) {
        check.diagnostics.push_back({Severity::Error, Code::BundleIndexMissing, "$", std::string(kBundleIndexName)});
        return check;
    }
    if (index->size > kMaxBundleIndexBytes) {
        check.diagnostics.push_back({Severity::Error, Code::TooLarge, std::string(kBundleIndexName),
                                     std::to_string(kMaxBundleIndexBytes)});
        return check;
    }
    const std::optional<std::string> text = readArchiveFile(archive, kBundleIndexName, kMaxBundleIndexBytes);
    if (!text) {
        check.diagnostics.push_back({Severity::Error, Code::ArchiveCorrupt, std::string(kBundleIndexName)});
        return check;
    }
    LoadedBundle loaded = loadBundle(*text);
    std::ranges::move(loaded.diagnostics, std::back_inserter(check.diagnostics));
    for (const BundleEntry& entry : loaded.bundle.extensions) {
        if (entry.builtin) {
            continue;
        }
        const std::string path = bundlePackagePath(entry);
        if (std::ranges::find(listing.files, path, &ArchiveEntry::path) == listing.files.end()) {
            check.diagnostics.push_back({Severity::Error, Code::BundlePackageMissing, path, entry.id});
        }
    }
    check.bundle = std::move(loaded.bundle);
    return check;
}

std::vector<Diagnostic> extractBundlePackage(const std::filesystem::path& archive, const BundleEntry& entry,
                                             const std::filesystem::path& file)
{
    const std::string path = bundlePackagePath(entry);
    const std::optional<std::string> bytes = readArchiveFile(archive, path, ArchiveLimits{}.maxFileBytes);
    if (!bytes) {
        return {{Severity::Error, Code::BundlePackageMissing, path, entry.id}};
    }
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes->data(), static_cast<std::streamsize>(bytes->size()));
    out.close();
    if (!out) {
        return {{Severity::Error, Code::ArchiveWriteFailed, path, utf8(file)}};
    }
    return {};
}

std::vector<Diagnostic> packBundle(const Bundle& bundle, const std::filesystem::path& staging,
                                   const std::filesystem::path& archive)
{
    std::error_code error;
    fs::create_directories(staging, error);
    std::ofstream out(staging / pathOf(kBundleIndexName), std::ios::binary | std::ios::trunc);
    out << saveBundle(bundle);
    out.close();
    if (!out) {
        return {{Severity::Error, Code::ArchiveWriteFailed, std::string(kBundleIndexName), utf8(staging)}};
    }
    return packDirectory(staging, archive);
}

}
