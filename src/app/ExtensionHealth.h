#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::app {

enum class HealthKind : std::uint8_t { Crashed, Stopped, ModulesFailed };

struct HealthEvent {
    std::chrono::sys_seconds at{};
    std::string version;
    HealthKind kind = HealthKind::Crashed;
    std::uint32_t code = 0;
    std::string report;

    friend bool operator==(const HealthEvent&, const HealthEvent&) = default;
};

struct HealthRecord {
    std::vector<HealthEvent> events;
    std::optional<HealthKind> off;

    friend bool operator==(const HealthRecord&, const HealthRecord&) = default;
};

class ExtensionHealth
{
public:
    static constexpr std::size_t kMostEvents = 20;
    static constexpr std::size_t kMostExtensions = 64;
    static constexpr std::size_t kMostBytes = std::size_t{512} * 1024;

    explicit ExtensionHealth(std::filesystem::path file = {});

    void record(std::string_view id, HealthEvent event);
    void clearOff(std::string_view id);
    void clear(std::string_view id);
    [[nodiscard]] const HealthRecord* find(std::string_view id) const;
    [[nodiscard]] std::size_t crashesSince(std::string_view id, std::chrono::sys_seconds since) const;
    [[nodiscard]] std::string text() const;

    [[nodiscard]] static std::string_view kindName(HealthKind kind) noexcept;
    [[nodiscard]] static std::string timeText(std::chrono::sys_seconds at);

private:
    void load();
    void save() const;

    std::filesystem::path m_file;
    std::map<std::string, HealthRecord, std::less<>> m_records;
};

}
