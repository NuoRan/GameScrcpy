#pragma once

#include <gs/base/Expected.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::espflash {

inline constexpr std::string_view kFirmwareFormat = "gamescrcpy.esp32-firmware";
inline constexpr std::string_view kFirmwareManifest = "firmware.json";
inline constexpr std::string_view kFirmwareFolder = "esp32-firmware";

struct FirmwareImage {
    std::string target;
    std::string file;
    std::array<std::uint8_t, 32> sha256{};
    std::string flashSize;
    std::uint32_t chipId = 0;
    std::uint32_t offset = 0;

    friend bool operator==(const FirmwareImage&, const FirmwareImage&) = default;
};

struct FirmwareBundle {
    std::string version;
    std::vector<FirmwareImage> images;
    std::filesystem::path folder;
};

enum class FirmwareProblem : std::uint8_t { Missing, Malformed, ImageMissing, ImageCorrupt };

[[nodiscard]] Expected<FirmwareBundle, FirmwareProblem> parseFirmwareBundle(std::string_view json,
                                                                           std::filesystem::path folder);
[[nodiscard]] Expected<FirmwareBundle, FirmwareProblem> loadFirmwareBundle(const std::filesystem::path& folder);
[[nodiscard]] const FirmwareImage* imageForChip(const FirmwareBundle& bundle, std::uint32_t chipId) noexcept;
[[nodiscard]] Expected<std::vector<std::uint8_t>, FirmwareProblem> readImage(const FirmwareBundle& bundle,
                                                                            const FirmwareImage& image);
[[nodiscard]] std::optional<std::uint32_t> flashSizeBytes(std::string_view text) noexcept;
[[nodiscard]] int compareVersions(std::string_view left, std::string_view right) noexcept;

}
