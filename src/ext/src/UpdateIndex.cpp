#include "gs/ext/UpdateIndex.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <set>
#include <utility>

namespace gs::ext {

namespace {

using Json = nlohmann::json;

constexpr int kMostIndexDepth = 8;
constexpr std::size_t kMostRequirements = 32;
constexpr std::size_t kMostVersionLength = 64;
constexpr std::size_t kMostKeyLength = 64;
constexpr std::size_t kDigestLength = 64;

bool plainKey(std::string_view key)
{
    return !key.empty() && key.size() <= 40 && std::ranges::all_of(key, [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                      c == '-' || c == '.';
           });
}

std::string memberPath(const std::string& path, std::string_view key)
{
    return plainKey(key) ? path + "[\"" + std::string(key) + "\"]" : path + "[?]";
}

std::string itemPath(const std::string& path, std::size_t index)
{
    return path + "[" + std::to_string(index) + "]";
}

bool refuse(std::string& problem, const std::string& path, std::string_view what)
{
    problem = path + ": " + std::string(what);
    return false;
}

bool onlyFields(const Json& object, std::initializer_list<std::string_view> fields, const std::string& path,
                std::string& problem)
{
    for (const auto& item : object.items()) {
        if (std::ranges::find(fields, std::string_view(item.key())) == fields.end()) {
            return refuse(problem, memberPath(path, item.key()), "not a field here");
        }
    }
    return true;
}

const Json* member(const Json& object, std::string_view name)
{
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &*found;
}

bool text(const Json& object, std::string_view name, const std::string& path, bool required, std::size_t most,
          std::string& out, std::string& problem)
{
    const std::string at = path + "." + std::string(name);
    const Json* node = member(object, name);
    if (node == nullptr) {
        return !required || refuse(problem, at, "missing");
    }
    if (!node->is_string()) {
        return refuse(problem, at, "not a string");
    }
    const auto& value = node->get_ref<const std::string&>();
    if (value.empty() && required) {
        return refuse(problem, at, "empty");
    }
    if (value.size() > most) {
        return refuse(problem, at, "longer than " + std::to_string(most) + " bytes");
    }
    out = value;
    return true;
}

bool hexDigit(char c) noexcept
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int digitsValue(std::string_view digits)
{
    int value = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9') {
            return -1;
        }
        value = value * 10 + (c - '0');
    }
    return value;
}

bool validDate(std::string_view text)
{
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        return false;
    }
    const int year = digitsValue(text.substr(0, 4));
    const int month = digitsValue(text.substr(5, 2));
    const int day = digitsValue(text.substr(8, 2));
    if (year < 0 || month < 0 || day < 0) {
        return false;
    }
    const std::chrono::year_month_day date{std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(month)},
                                           std::chrono::day{static_cast<unsigned>(day)}};
    return date.ok();
}

bool readSize(const Json& object, const std::string& path, std::uint64_t& out, std::string& problem)
{
    const std::string at = path + ".size";
    const Json* node = member(object, "size");
    if (node == nullptr) {
        return refuse(problem, at, "missing");
    }
    if (!node->is_number_integer()) {
        return refuse(problem, at, "not a whole number of bytes");
    }
    const std::uint64_t size = node->is_number_unsigned() ? node->get<std::uint64_t>() : 0;
    if (size == 0 || size > kMostUpdatePackageBytes) {
        return refuse(problem, at, "not between 1 byte and 200 MiB");
    }
    out = size;
    return true;
}

bool readRequirements(const Json& object, const std::string& path, IndexedRelease& release, std::string& problem)
{
    const std::string at = path + ".requires";
    const Json* node = member(object, "requires");
    if (node == nullptr) {
        return true;
    }
    if (!node->is_object()) {
        return refuse(problem, at, "not an object");
    }
    if (node->size() > kMostRequirements) {
        return refuse(problem, at, "more than " + std::to_string(kMostRequirements) + " entries");
    }
    for (const auto& item : node->items()) {
        const std::string entry = memberPath(at, item.key());
        if (item.key().size() > kMostKeyLength) {
            return refuse(problem, entry, "the name is too long");
        }
        if (!item.value().is_string() || item.value().get_ref<const std::string&>().size() > kMostVersionLength) {
            return refuse(problem, entry, "not a version range");
        }
        const std::optional<VersionRange> range = VersionRange::parse(item.value().get_ref<const std::string&>());
        if (!range) {
            return refuse(problem, entry, "not a version range");
        }
        if (const std::optional<ExtensionPoint> point = extensionPointNamed(item.key())) {
            release.requirements[*point] = *range;
        } else {
            release.needsUnknown = true;
        }
    }
    return true;
}

std::optional<IndexedRelease> readRelease(const Json& node, const std::string& path, std::string& problem)
{
    if (!node.is_object()) {
        refuse(problem, path, "not an object");
        return std::nullopt;
    }
    if (!onlyFields(node, {"version", "url", "sha256", "size", "publisher", "requires", "released", "notes"}, path,
                    problem)) {
        return std::nullopt;
    }
    IndexedRelease release;
    std::string version;
    if (!text(node, "version", path, true, kMostVersionLength, version, problem)) {
        return std::nullopt;
    }
    const std::optional<Version> parsed = Version::parse(version);
    if (!parsed) {
        refuse(problem, path + ".version", "not a version such as 1.2.0");
        return std::nullopt;
    }
    release.version = *parsed;
    if (!text(node, "url", path, true, kMaxUpdateIndexLength, release.url, problem)) {
        return std::nullopt;
    }
    if (!validUpdateUrl(release.url)) {
        refuse(problem, path + ".url", "not an https address with a host");
        return std::nullopt;
    }
    if (!text(node, "sha256", path, true, kDigestLength, release.sha256, problem)) {
        return std::nullopt;
    }
    if (release.sha256.size() != kDigestLength || !std::ranges::all_of(release.sha256, hexDigit)) {
        refuse(problem, path + ".sha256", "not 64 hexadecimal digits");
        return std::nullopt;
    }
    std::ranges::transform(release.sha256, release.sha256.begin(),
                           [](char c) { return c >= 'A' && c <= 'F' ? static_cast<char>(c - 'A' + 'a') : c; });
    if (!readSize(node, path, release.size, problem)) {
        return std::nullopt;
    }
    std::string publisher;
    if (!text(node, "publisher", path, false, kMostKeyLength, publisher, problem)) {
        return std::nullopt;
    }
    if (member(node, "publisher") != nullptr) {
        release.publisher = parseMinisignKey(publisher);
        if (!release.publisher) {
            refuse(problem, path + ".publisher", "not a minisign public key");
            return std::nullopt;
        }
    }
    if (!readRequirements(node, path, release, problem)) {
        return std::nullopt;
    }
    if (!text(node, "released", path, false, 10, release.released, problem)) {
        return std::nullopt;
    }
    if (member(node, "released") != nullptr && !validDate(release.released)) {
        refuse(problem, path + ".released", "not a date such as 2026-11-12");
        return std::nullopt;
    }
    if (!text(node, "notes", path, false, kMostReleaseNotesBytes, release.notes, problem)) {
        return std::nullopt;
    }
    return release;
}

std::optional<IndexedExtension> readExtension(const Json& node, const std::string& path, std::string& problem)
{
    if (!node.is_object()) {
        refuse(problem, path, "not an object");
        return std::nullopt;
    }
    if (!onlyFields(node, {"id", "versions"}, path, problem)) {
        return std::nullopt;
    }
    IndexedExtension extension;
    if (!text(node, "id", path, true, kMaxIdLength, extension.id, problem)) {
        return std::nullopt;
    }
    if (!validExtensionId(extension.id)) {
        refuse(problem, path + ".id", "not an extension id");
        return std::nullopt;
    }
    const std::string at = path + ".versions";
    const Json* versions = member(node, "versions");
    if (versions == nullptr) {
        refuse(problem, at, "missing");
        return std::nullopt;
    }
    if (!versions->is_array()) {
        refuse(problem, at, "not a list");
        return std::nullopt;
    }
    if (versions->empty() || versions->size() > kMostIndexedReleases) {
        refuse(problem, at, "not between 1 and " + std::to_string(kMostIndexedReleases) + " versions");
        return std::nullopt;
    }
    for (std::size_t index = 0; index < versions->size(); ++index) {
        std::optional<IndexedRelease> release = readRelease((*versions)[index], itemPath(at, index), problem);
        if (!release) {
            return std::nullopt;
        }
        if (std::ranges::find(extension.releases, release->version, &IndexedRelease::version) !=
            extension.releases.end()) {
            refuse(problem, itemPath(at, index) + ".version", "listed twice");
            return std::nullopt;
        }
        extension.releases.push_back(std::move(*release));
    }
    return extension;
}

}

const IndexedExtension* UpdateIndex::find(std::string_view id) const
{
    const auto found = std::ranges::find(extensions, id, &IndexedExtension::id);
    return found == extensions.end() ? nullptr : &*found;
}

bool validUpdateUrl(std::string_view url)
{
    constexpr std::string_view kScheme = "https://";
    if (url.size() <= kScheme.size() || url.size() > kMaxUpdateIndexLength || !url.starts_with(kScheme) ||
        !std::ranges::all_of(url, [](char c) { return c > 0x20 && c < 0x7F; })) {
        return false;
    }
    const std::string_view rest = url.substr(kScheme.size());
    const std::string_view authority = rest.substr(0, rest.find_first_of("/?#"));
    return !authority.empty() && authority.front() != ':' && authority.find('@') == std::string_view::npos;
}

ReadIndex readUpdateIndex(std::string_view json)
{
    if (json.size() > kMostUpdateIndexBytes) {
        return {std::nullopt, "$: larger than 1 MiB"};
    }
    bool deep = false;
    bool repeated = false;
    std::vector<std::set<std::string, std::less<>>> keys;
    const Json::parser_callback_t watch = [&](int depth, Json::parse_event_t event, Json& parsed) {
        switch (event) {
        case Json::parse_event_t::object_start:
            keys.emplace_back();
            deep = deep || depth > kMostIndexDepth;
            break;
        case Json::parse_event_t::array_start:
            deep = deep || depth > kMostIndexDepth;
            break;
        case Json::parse_event_t::key:
            if (!keys.empty() && parsed.is_string() && !keys.back().insert(parsed.get<std::string>()).second) {
                repeated = true;
            }
            break;
        case Json::parse_event_t::object_end:
            if (!keys.empty()) {
                keys.pop_back();
            }
            break;
        default:
            break;
        }
        return true;
    };
    const Json root = Json::parse(json, watch, false);
    if (root.is_discarded()) {
        return {std::nullopt, "$: not JSON"};
    }
    if (deep) {
        return {std::nullopt, "$: nested too deeply"};
    }
    if (repeated) {
        return {std::nullopt, "$: an object names the same field twice"};
    }
    std::string problem;
    if (!root.is_object()) {
        refuse(problem, "$", "not an object");
        return {std::nullopt, problem};
    }
    if (!onlyFields(root, {"format", "version", "extensions"}, "$", problem)) {
        return {std::nullopt, problem};
    }
    std::string format;
    if (!text(root, "format", "$", true, 64, format, problem)) {
        return {std::nullopt, problem};
    }
    if (format != kUpdateIndexFormat) {
        refuse(problem, "$.format", "not " + std::string(kUpdateIndexFormat));
        return {std::nullopt, problem};
    }
    const Json* version = member(root, "version");
    if (version == nullptr || !version->is_number_integer()) {
        refuse(problem, "$.version", version == nullptr ? "missing" : "not a whole number");
        return {std::nullopt, problem};
    }
    constexpr auto kKnownVersion = static_cast<std::uint64_t>(kUpdateIndexVersion);
    const std::uint64_t number = version->is_number_unsigned() ? version->get<std::uint64_t>() : 0;
    if (number != kKnownVersion) {
        refuse(problem, "$.version",
               number > kKnownVersion ? "a newer format than this GameScrcpy reads" : "not a known format version");
        return {std::nullopt, problem};
    }
    const Json* extensions = member(root, "extensions");
    if (extensions == nullptr || !extensions->is_array()) {
        refuse(problem, "$.extensions", extensions == nullptr ? "missing" : "not a list");
        return {std::nullopt, problem};
    }
    if (extensions->size() > kMostIndexedExtensions) {
        refuse(problem, "$.extensions", "more than " + std::to_string(kMostIndexedExtensions) + " extensions");
        return {std::nullopt, problem};
    }
    UpdateIndex index;
    for (std::size_t at = 0; at < extensions->size(); ++at) {
        std::optional<IndexedExtension> extension =
            readExtension((*extensions)[at], itemPath("$.extensions", at), problem);
        if (!extension) {
            return {std::nullopt, problem};
        }
        if (index.find(extension->id) != nullptr) {
            refuse(problem, itemPath("$.extensions", at) + ".id", "listed twice");
            return {std::nullopt, problem};
        }
        index.extensions.push_back(std::move(*extension));
    }
    return {std::move(index), {}};
}

bool runsOn(const IndexedRelease& release, const HostApi& host)
{
    if (release.needsUnknown) {
        return false;
    }
    return std::ranges::all_of(release.requirements, [&host](const auto& requirement) {
        const auto offered = host.versions.find(requirement.first);
        const auto fits = [&requirement](const Version& version) { return requirement.second.contains(version); };
        return offered != host.versions.end() && std::ranges::any_of(offered->second, fits);
    });
}

const IndexedRelease* offeredRelease(const IndexedExtension& extension, const Version& installed, const HostApi& host,
                                     const std::optional<Version>& skipped)
{
    const IndexedRelease* best = nullptr;
    for (const IndexedRelease& release : extension.releases) {
        const bool newer = installed < release.version && (!skipped || *skipped < release.version);
        const bool stable = release.version.prerelease.empty() || !installed.prerelease.empty();
        if (newer && stable && runsOn(release, host) && (best == nullptr || best->version < release.version)) {
            best = &release;
        }
    }
    return best;
}

}
