#include "ExtensionHealth.h"

#include <gs/base/Log.h>
#include <gs/platform/AtomicFile.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <sstream>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

constexpr std::string_view kCategory = "extensions";
constexpr std::string_view kFormat = "gamescrcpy.extension-health";
constexpr int kVersion = 1;
constexpr std::array<std::string_view, 3> kKindNames{"crashed", "stopped", "modulesFailed"};
constexpr std::size_t kMostTextBytes = 256;

std::string textAt(const nlohmann::json& object, const char* key)
{
    if (!object.contains(key) || !object.at(key).is_string()) {
        return {};
    }
    const std::string& text = object.at(key).get_ref<const std::string&>();
    return text.size() <= kMostTextBytes ? text : std::string();
}

std::optional<HealthKind> kindNamed(std::string_view name)
{
    const auto found = std::ranges::find(kKindNames, name);
    return found == kKindNames.end() ? std::nullopt
                                     : std::optional<HealthKind>(static_cast<HealthKind>(found - kKindNames.begin()));
}

std::optional<std::chrono::sys_seconds> timeNamed(const std::string& text)
{
    std::istringstream in(text);
    std::chrono::sys_seconds at{};
    in >> std::chrono::parse("%FT%TZ", at);
    return in.fail() ? std::nullopt : std::optional<std::chrono::sys_seconds>(at);
}

bool plainName(std::string_view name)
{
    return name.find_first_of("/\\:") == std::string_view::npos && name != "." && name != "..";
}

std::chrono::sys_seconds lastOf(const HealthRecord& record)
{
    return record.events.empty() ? std::chrono::sys_seconds{} : record.events.back().at;
}

void trim(HealthRecord& record)
{
    if (record.events.size() > ExtensionHealth::kMostEvents) {
        record.events.erase(record.events.begin(),
                            record.events.begin() +
                                static_cast<std::ptrdiff_t>(record.events.size() - ExtensionHealth::kMostEvents));
    }
}

}

ExtensionHealth::ExtensionHealth(std::filesystem::path file)
    : m_file(std::move(file))
{
    load();
}

void ExtensionHealth::record(std::string_view id, HealthEvent event)
{
    if (id.empty()) {
        return;
    }
    auto found = m_records.find(id);
    if (found == m_records.end()) {
        found = m_records.emplace(std::string(id), HealthRecord{}).first;
    }
    HealthRecord& record = found->second;
    if (event.kind != HealthKind::Crashed) {
        record.off = event.kind;
    }
    record.events.push_back(std::move(event));
    trim(record);
    while (m_records.size() > kMostExtensions) {
        auto oldest = m_records.end();
        for (auto it = m_records.begin(); it != m_records.end(); ++it) {
            if (it->first != id && (oldest == m_records.end() || lastOf(it->second) < lastOf(oldest->second))) {
                oldest = it;
            }
        }
        m_records.erase(oldest);
    }
    save();
}

void ExtensionHealth::clearOff(std::string_view id)
{
    const auto found = m_records.find(id);
    if (found == m_records.end() || !found->second.off) {
        return;
    }
    found->second.off.reset();
    if (found->second.events.empty()) {
        m_records.erase(found);
    }
    save();
}

void ExtensionHealth::clear(std::string_view id)
{
    const auto found = m_records.find(id);
    if (found == m_records.end() || found->second.events.empty()) {
        return;
    }
    found->second.events.clear();
    if (!found->second.off) {
        m_records.erase(found);
    }
    save();
}

const HealthRecord* ExtensionHealth::find(std::string_view id) const
{
    const auto found = m_records.find(id);
    return found == m_records.end() ? nullptr : &found->second;
}

std::size_t ExtensionHealth::crashesSince(std::string_view id, std::chrono::sys_seconds since) const
{
    const HealthRecord* record = find(id);
    if (record == nullptr) {
        return 0;
    }
    return static_cast<std::size_t>(std::ranges::count_if(record->events, [since](const HealthEvent& event) {
        return event.kind == HealthKind::Crashed && event.at >= since;
    }));
}

std::string ExtensionHealth::text() const
{
    nlohmann::json extensions = nlohmann::json::object();
    for (const auto& [id, record] : m_records) {
        nlohmann::json events = nlohmann::json::array();
        for (const HealthEvent& event : record.events) {
            nlohmann::json entry{{"at", timeText(event.at)}, {"version", event.version}, {"kind", kindName(event.kind)}};
            if (event.code != 0) {
                entry["code"] = event.code;
            }
            if (!event.report.empty()) {
                entry["report"] = event.report;
            }
            events.push_back(std::move(entry));
        }
        nlohmann::json entry{{"events", std::move(events)}};
        if (record.off) {
            entry["off"] = kindName(*record.off);
        }
        extensions[id] = std::move(entry);
    }
    return nlohmann::json{{"format", kFormat}, {"version", kVersion}, {"extensions", std::move(extensions)}}.dump(2);
}

std::string_view ExtensionHealth::kindName(HealthKind kind) noexcept
{
    return kKindNames[static_cast<std::size_t>(kind)];
}

std::string ExtensionHealth::timeText(std::chrono::sys_seconds at)
{
    return std::format("{:%FT%TZ}", at);
}

void ExtensionHealth::load()
{
    if (m_file.empty()) {
        return;
    }
    std::string contents;
    const std::error_code error = platform::readFile(m_file, kMostBytes, contents);
    if (error) {
        if (!platform::fileMissing(error)) {
            log::warning(kCategory, "the health record of the extensions could not be read (error {})", error.value());
        }
        return;
    }
    const nlohmann::json root = nlohmann::json::parse(contents, nullptr, false);
    if (!root.is_object() || textAt(root, "format") != kFormat || !root.contains("version") ||
        !root.at("version").is_number_integer() || root.at("version").get<int>() != kVersion ||
        !root.contains("extensions") || !root.at("extensions").is_object()) {
        log::warning(kCategory, "the health record of the extensions is not one GameScrcpy can read; starting with none");
        return;
    }
    for (const auto& [id, entry] : root.at("extensions").items()) {
        if (m_records.size() >= kMostExtensions) {
            break;
        }
        if (id.empty() || id.size() > kMostTextBytes || !entry.is_object()) {
            continue;
        }
        HealthRecord record;
        record.off = kindNamed(textAt(entry, "off"));
        if (record.off == HealthKind::Crashed) {
            record.off.reset();
        }
        if (entry.contains("events") && entry.at("events").is_array()) {
            for (const nlohmann::json& item : entry.at("events")) {
                if (!item.is_object()) {
                    continue;
                }
                const std::optional<HealthKind> kind = kindNamed(textAt(item, "kind"));
                const std::optional<std::chrono::sys_seconds> at = timeNamed(textAt(item, "at"));
                if (!kind || !at) {
                    continue;
                }
                HealthEvent event{.at = *at, .version = textAt(item, "version"), .kind = *kind, .code = 0, .report = {}};
                if (item.contains("code") && item.at("code").is_number_unsigned()) {
                    const auto code = item.at("code").get<std::uint64_t>();
                    event.code = code <= 0xFFFF'FFFFU ? static_cast<std::uint32_t>(code) : 0;
                }
                if (const std::string report = textAt(item, "report"); plainName(report)) {
                    event.report = report;
                }
                record.events.push_back(std::move(event));
            }
        }
        trim(record);
        if (!record.events.empty() || record.off) {
            m_records.emplace(id, std::move(record));
        }
    }
}

void ExtensionHealth::save() const
{
    if (m_file.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(m_file.parent_path(), error);
    error = platform::writeFileAtomically(m_file, text());
    if (error) {
        log::warning(kCategory, "the health record of the extensions could not be saved (error {})", error.value());
    }
}

}
