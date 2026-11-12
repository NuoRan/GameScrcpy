#include "ComponentDefaults.h"

#include <gs/base/JsonNesting.h>
#include <gs/base/Log.h>
#include <gs/platform/AtomicFile.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

using Json = nlohmann::json;

constexpr std::uintmax_t kMostBytes = 1024 * 1024;

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

}

ComponentDefaults::ComponentDefaults(std::filesystem::path file)
    : m_file(std::move(file))
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
    load();
}

ComponentDefaults::~ComponentDefaults()
{
    if (m_unsaved) {
        static_cast<void>(save());
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

std::optional<Json> ComponentDefaults::find(std::string_view component) const
{
    const auto found = m_components.find(std::string(component));
    if (found == m_components.end() || !found->is_object()) {
        return std::nullopt;
    }
    return *found;
}

bool ComponentDefaults::contains(std::string_view component) const
{
    return find(component).has_value();
}

bool ComponentDefaults::store(std::string_view component, const Json& control)
{
    m_busy = false;
    if (component.empty() || !control.is_object()) {
        return false;
    }
    Json next = m_components;
    next[std::string(component)] = control;
    return keep(std::move(next));
}

bool ComponentDefaults::forget(std::string_view component)
{
    m_busy = false;
    if (!contains(component)) {
        return false;
    }
    Json next = m_components;
    next.erase(std::string(component));
    return keep(std::move(next));
}

bool ComponentDefaults::keep(Json next)
{
    if (m_readOnly) {
        return false;
    }
    std::swap(m_components, next);
    const std::error_code error = save();
    if (!error) {
        m_unsaved = false;
        return true;
    }
    if (platform::fileBusy(error)) {
        m_unsaved = true;
        m_busy = true;
        return true;
    }
    std::swap(m_components, next);
    return false;
}

void ComponentDefaults::load()
{
    std::error_code error;
    if (!std::filesystem::exists(m_file, error)) {
        return;
    }
    if (std::filesystem::file_size(m_file, error) > kMostBytes || error) {
        log::warning("defaults", "{} is too large or unreadable: using the factory defaults", utf8(m_file));
        m_readOnly = true;
        return;
    }
    std::ifstream in(m_file, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    in.close();
    if (!jsonNestingWithin(text, kMostJsonNesting)) {
        log::warning("defaults", "{} is nested more than 64 levels deep: using the factory defaults", utf8(m_file));
        m_readOnly = true;
        return;
    }
    const Json root = Json::parse(text, nullptr, false);
    const auto format = root.is_object() ? root.find("format") : root.end();
    if (format == root.end() || !format->is_string() || format->get_ref<const std::string&>() != kFormatName) {
        std::filesystem::path aside = m_file;
        aside += ".bad";
        std::filesystem::rename(m_file, aside, error);
        log::warning("defaults", "{} cannot be read: using the factory defaults; the file {}", utf8(m_file),
                     error ? "stays where it is" : "is kept as " + utf8(aside.filename()));
        return;
    }
    const auto version = root.find("version");
    if (version == root.end() || !version->is_number_integer() || version->get<int>() != kFormatVersion) {
        log::warning("defaults", "{} comes from another GameScrcpy: using the factory defaults, and not saving over it",
                     utf8(m_file));
        m_readOnly = true;
        return;
    }
    const auto components = root.find("components");
    if (components == root.end() || !components->is_object()) {
        return;
    }
    for (auto it = components->begin(); it != components->end(); ++it) {
        if (it->is_object()) {
            m_components[it.key()] = *it;
        }
    }
}

std::error_code ComponentDefaults::save() const
{
    const Json root{{"format", std::string(kFormatName)}, {"version", kFormatVersion}, {"components", m_components}};
    std::error_code error;
    std::filesystem::create_directories(m_file.parent_path(), error);
    error = platform::writeFileAtomically(m_file, root.dump(2) + "\n");
    if (error) {
        log::warning("defaults", "could not save {} ({}error {})", utf8(m_file),
                     platform::fileBusy(error) ? "another program is using it, " : "", error.value());
    }
    return error;
}

}
