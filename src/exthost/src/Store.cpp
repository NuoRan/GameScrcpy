#include "Store.h"

#include "gs/exthost/Module.h"

#include <gs/platform/AtomicFile.h>

#include <nlohmann/json.hpp>

#include <system_error>
#include <utility>

namespace gs::exthost::detail {

ModuleStore::ModuleStore(std::filesystem::path file)
    : m_file(std::move(file))
{
}

void ModuleStore::loadOnce()
{
    if (m_loaded) {
        return;
    }
    m_loaded = true;
    if (m_file.empty()) {
        return;
    }
    std::string contents;
    if (platform::readFile(m_file, kStoreQuota, contents, platform::WhenBusy::GiveUp)) {
        return;
    }
    const nlohmann::json root = nlohmann::json::parse(contents, nullptr, false);
    if (!root.is_object()) {
        return;
    }
    for (auto entry = root.begin(); entry != root.end(); ++entry) {
        m_values.emplace(entry.key(), entry.value().dump());
    }
}

std::optional<std::string> ModuleStore::serialized(const std::map<std::string, std::string, std::less<>>& values) const
{
    nlohmann::ordered_json root = nlohmann::ordered_json::object();
    for (const auto& [key, json] : values) {
        root[key] = nlohmann::ordered_json::parse(json, nullptr, false);
    }
    std::string text = root.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
    if (text.size() > kStoreQuota) {
        return std::nullopt;
    }
    return text;
}

std::optional<std::string> ModuleStore::get(std::string_view key)
{
    const std::scoped_lock lock(m_mutex);
    loadOnce();
    const auto found = m_values.find(key);
    if (found == m_values.end()) {
        return std::nullopt;
    }
    return found->second;
}

StoreResult ModuleStore::set(std::string_view key, std::optional<std::string_view> json)
{
    if (key.empty() || key.size() > kMostStoreKeyBytes || !validUtf8(key)) {
        return StoreResult::BadArgument;
    }
    if (json && (!validUtf8(*json) || !nlohmann::json::accept(*json))) {
        return StoreResult::BadArgument;
    }
    const std::scoped_lock lock(m_mutex);
    if (m_file.empty()) {
        return StoreResult::Unavailable;
    }
    loadOnce();
    std::map<std::string, std::string, std::less<>> next = m_values;
    if (json) {
        next.insert_or_assign(std::string(key), std::string(*json));
    } else {
        const auto found = next.find(key);
        if (found == next.end()) {
            return StoreResult::Ok;
        }
        next.erase(found);
    }
    const std::optional<std::string> text = serialized(next);
    if (!text) {
        return StoreResult::Full;
    }
    std::error_code error;
    std::filesystem::create_directories(m_file.parent_path(), error);
    if (platform::writeFileAtomically(m_file, *text)) {
        return StoreResult::Failed;
    }
    m_values = std::move(next);
    return StoreResult::Ok;
}

}
