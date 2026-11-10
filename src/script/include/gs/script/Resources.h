#pragma once

#include "gs/script/Services.h"

#include <gs/base/Expected.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace gs::script {

inline constexpr std::string_view kResourcesFile = "res/resources.json";
inline constexpr std::string_view kResourcesFormat = "gamescrcpy.resources";
inline constexpr int kResourcesVersion = 1;
inline constexpr std::size_t kMostResourceNameChars = 64;
inline constexpr std::size_t kMostResourcesOfAKind = 1000;
inline constexpr std::size_t kMostPathPoints = 64;
inline constexpr int kMostPictureSide = 65535;
inline constexpr double kDefaultTolerance = 0.1;
inline constexpr double kNewTemplateThreshold = 0.9;
inline constexpr double kDefaultPathMs = 300.0;
inline constexpr double kLongestPathMs = 60'000.0;

enum class ResourceKind : std::uint8_t { Template, Region, Point, Path, Color };

struct TemplateResource {
    std::string name;
    std::string file;
    double threshold = 0.8;
    MatchMode match = MatchMode::Grey;
    std::string region;
    PictureSize source;

    friend bool operator==(const TemplateResource&, const TemplateResource&) = default;
};

struct RegionResource {
    std::string name;
    Region area;

    friend bool operator==(const RegionResource&, const RegionResource&) = default;
};

struct PointResource {
    std::string name;
    Point at;

    friend bool operator==(const PointResource&, const PointResource&) = default;
};

struct PathResource {
    std::string name;
    std::vector<Point> points;
    double durationMs = kDefaultPathMs;

    friend bool operator==(const PathResource&, const PathResource&) = default;
};

struct ColorResource {
    std::string name;
    Rgb color;
    double tolerance = kDefaultTolerance;
    std::string region;

    friend bool operator==(const ColorResource&, const ColorResource&) = default;
};

struct Resources {
    std::vector<TemplateResource> templates;
    std::vector<RegionResource> regions;
    std::vector<PointResource> points;
    std::vector<PathResource> paths;
    std::vector<ColorResource> colors;

    [[nodiscard]] const TemplateResource* findTemplate(std::string_view name) const noexcept;
    [[nodiscard]] const RegionResource* findRegion(std::string_view name) const noexcept;
    [[nodiscard]] const PointResource* findPoint(std::string_view name) const noexcept;
    [[nodiscard]] const PathResource* findPath(std::string_view name) const noexcept;
    [[nodiscard]] const ColorResource* findColor(std::string_view name) const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    friend bool operator==(const Resources&, const Resources&) = default;
};

using LoadedResources = Expected<Resources, std::vector<std::string>>;

[[nodiscard]] bool resourceName(std::string_view name) noexcept;
[[nodiscard]] bool resourceFile(std::string_view file) noexcept;
[[nodiscard]] std::size_t characterCount(std::string_view text) noexcept;
[[nodiscard]] std::optional<Rgb> parseColor(std::string_view text) noexcept;
[[nodiscard]] std::string colorText(Rgb color);
[[nodiscard]] std::string_view kindName(ResourceKind kind) noexcept;

[[nodiscard]] std::vector<std::string> checkResources(const Resources& resources);
[[nodiscard]] LoadedResources parseResources(std::string_view text);
[[nodiscard]] std::string writeResources(const Resources& resources);
[[nodiscard]] LoadedResources loadResources(const std::filesystem::path& file);
[[nodiscard]] std::error_code saveResources(const std::filesystem::path& file, const Resources& resources);

class ResourceFile
{
public:
    explicit ResourceFile(std::filesystem::path file);

    [[nodiscard]] const LoadedResources& current();

private:
    std::filesystem::path m_file;
    bool m_read = false;
    bool m_present = false;
    std::filesystem::file_time_type m_written{};
    std::uintmax_t m_size = 0;
    LoadedResources m_loaded;
};

}
