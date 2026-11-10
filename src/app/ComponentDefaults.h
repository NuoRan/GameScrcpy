#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string_view>
#include <system_error>

namespace gs::app {

class ComponentDefaults
{
public:
    static constexpr std::string_view kFormatName = "gamescrcpy.componentDefaults";
    static constexpr int kFormatVersion = 1;

    explicit ComponentDefaults(std::filesystem::path file);
    ~ComponentDefaults();
    ComponentDefaults(const ComponentDefaults&) = delete;
    ComponentDefaults& operator=(const ComponentDefaults&) = delete;

    [[nodiscard]] static ComponentDefaults* instance() noexcept { return s_instance; }

    [[nodiscard]] std::optional<nlohmann::json> find(std::string_view component) const;
    [[nodiscard]] bool contains(std::string_view component) const;
    [[nodiscard]] bool readOnly() const noexcept { return m_readOnly; }
    [[nodiscard]] bool lastSaveBusy() const noexcept { return m_busy; }
    bool store(std::string_view component, const nlohmann::json& control);
    bool forget(std::string_view component);

private:
    void load();
    bool keep(nlohmann::json next);
    [[nodiscard]] std::error_code save() const;

    static inline ComponentDefaults* s_instance = nullptr;

    std::filesystem::path m_file;
    nlohmann::json m_components = nlohmann::json::object();
    bool m_readOnly = false;
    bool m_unsaved = false;
    bool m_busy = false;
};

}
