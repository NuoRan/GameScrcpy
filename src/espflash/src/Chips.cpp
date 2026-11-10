#include "gs/espflash/Chips.h"

#include <algorithm>
#include <array>

namespace gs::espflash {

namespace {

constexpr std::array kChips{
    ChipInfo{.chip = Chip::Esp32, .imageId = 0, .name = "ESP32", .target = "esp32", .encryptedFlag = false},
    ChipInfo{.chip = Chip::Esp32S2,
             .imageId = 2,
             .name = "ESP32-S2",
             .target = "esp32s2",
             .encryptedFlag = true,
             .forceDownloadRegister = 0x3F408128,
             .forceDownloadMask = 0x1},
    ChipInfo{.chip = Chip::Esp32S3,
             .imageId = 9,
             .name = "ESP32-S3",
             .target = "esp32s3",
             .encryptedFlag = true,
             .forceDownloadRegister = 0x6000812C,
             .forceDownloadMask = 0x1},
    ChipInfo{.chip = Chip::Esp32C3, .imageId = 5, .name = "ESP32-C3", .target = "esp32c3", .encryptedFlag = true},
    ChipInfo{.chip = Chip::Esp32C6, .imageId = 13, .name = "ESP32-C6", .target = "esp32c6", .encryptedFlag = true},
    ChipInfo{.chip = Chip::Esp32P4, .imageId = 18, .name = "ESP32-P4", .target = "esp32p4", .encryptedFlag = true},
};

struct Magic {
    std::uint32_t value = 0;
    Chip chip = Chip::Esp32;
};

constexpr std::array kMagics{
    Magic{0x00F01D83, Chip::Esp32},   Magic{0x000007C6, Chip::Esp32S2}, Magic{0x00000009, Chip::Esp32S3},
    Magic{0x6921506F, Chip::Esp32C3}, Magic{0x1B31506F, Chip::Esp32C3}, Magic{0x4881606F, Chip::Esp32C3},
    Magic{0x4361606F, Chip::Esp32C3}, Magic{0x2CE0806F, Chip::Esp32C6},
};

}

std::span<const ChipInfo> chips() noexcept
{
    return kChips;
}

const ChipInfo* chipByImageId(std::uint32_t imageId) noexcept
{
    const auto found = std::ranges::find(kChips, imageId, &ChipInfo::imageId);
    return found == kChips.end() ? nullptr : &*found;
}

const ChipInfo* chipByMagic(std::uint32_t magic) noexcept
{
    const auto found = std::ranges::find(kMagics, magic, &Magic::value);
    return found == kMagics.end() ? nullptr : &chipInfo(found->chip);
}

const ChipInfo& chipInfo(Chip chip) noexcept
{
    const auto found = std::ranges::find(kChips, chip, &ChipInfo::chip);
    return found == kChips.end() ? kChips.front() : *found;
}

}
