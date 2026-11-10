#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace gs::espflash {

enum class Chip : std::uint8_t { Esp32, Esp32S2, Esp32S3, Esp32C3, Esp32C6, Esp32P4 };

struct ChipInfo {
    Chip chip = Chip::Esp32;
    std::uint32_t imageId = 0;
    std::string_view name;
    std::string_view target;
    bool encryptedFlag = false;
    std::uint32_t forceDownloadRegister = 0;
    std::uint32_t forceDownloadMask = 0;
};

inline constexpr std::uint32_t kChipMagicRegister = 0x40001000;

[[nodiscard]] std::span<const ChipInfo> chips() noexcept;
[[nodiscard]] const ChipInfo* chipByImageId(std::uint32_t imageId) noexcept;
[[nodiscard]] const ChipInfo* chipByMagic(std::uint32_t magic) noexcept;
[[nodiscard]] const ChipInfo& chipInfo(Chip chip) noexcept;

}
