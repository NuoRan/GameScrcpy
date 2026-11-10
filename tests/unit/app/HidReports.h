#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <utility>
#include <vector>

namespace gs::test {

inline constexpr std::uint32_t kHidFinger = 0x000D0022;
inline constexpr std::uint32_t kHidTipSwitch = 0x000D0042;
inline constexpr std::uint32_t kHidContactIdentifier = 0x000D0051;
inline constexpr std::uint32_t kHidContactCount = 0x000D0054;
inline constexpr std::uint32_t kHidContactCountMaximum = 0x000D0055;
inline constexpr std::uint32_t kHidX = 0x00010030;
inline constexpr std::uint32_t kHidY = 0x00010031;

enum class HidKind : std::uint8_t { Input, Output, Feature };

struct HidField {
    HidKind kind = HidKind::Input;
    std::uint8_t reportId = 0;
    std::uint32_t usage = 0;
    std::size_t bit = 0;
    std::size_t size = 0;
    std::int64_t logicalMaximum = 0;
    int finger = -1;
};

struct HidLayout {
    bool numbered = false;
    bool complete = false;
    int fingers = 0;
    std::vector<HidField> fields;
    std::map<std::pair<HidKind, std::uint8_t>, std::size_t> bits;

    [[nodiscard]] std::size_t bytes(HidKind kind, std::uint8_t reportId) const
    {
        const auto found = bits.find({kind, reportId});
        const std::size_t payload = found == bits.end() ? 0 : (found->second + 7) / 8;
        return payload + (numbered ? 1 : 0);
    }

    [[nodiscard]] const HidField* field(HidKind kind, std::uint8_t reportId, std::uint32_t usage, int finger = -1) const
    {
        for (const HidField& candidate : fields) {
            if (candidate.kind == kind && candidate.reportId == reportId && candidate.usage == usage &&
                candidate.finger == finger) {
                return &candidate;
            }
        }
        return nullptr;
    }
};

inline std::int64_t hidSigned(std::uint32_t value, std::size_t size) noexcept
{
    switch (size) {
    case 1:
        return static_cast<std::int8_t>(value);
    case 2:
        return static_cast<std::int16_t>(value);
    case 4:
        return static_cast<std::int32_t>(value);
    default:
        return 0;
    }
}

inline HidLayout parseHid(std::span<const std::uint8_t> descriptor)
{
    HidLayout layout;
    std::uint32_t usagePage = 0;
    std::int64_t logicalMaximum = 0;
    std::size_t reportSize = 0;
    std::size_t reportCount = 0;
    std::uint8_t reportId = 0;
    std::vector<std::uint32_t> usages;
    std::vector<int> collections;
    std::size_t at = 0;
    while (at < descriptor.size()) {
        const std::uint8_t prefix = descriptor[at];
        const std::size_t size = (prefix & 3U) == 3U ? 4 : (prefix & 3U);
        if (prefix == 0xFE || at + 1 + size > descriptor.size()) {
            return layout;
        }
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < size; ++i) {
            value |= static_cast<std::uint32_t>(descriptor[at + 1 + i]) << (8 * i);
        }
        const int finger = collections.empty() ? -1 : collections.back();
        switch (prefix & 0xFCU) {
        case 0x04:
            usagePage = value;
            break;
        case 0x08:
            usages.push_back(size == 4 ? value : (usagePage << 16U) | value);
            break;
        case 0x24:
            logicalMaximum = hidSigned(value, size);
            break;
        case 0x74:
            reportSize = value;
            break;
        case 0x84:
            reportId = static_cast<std::uint8_t>(value);
            layout.numbered = true;
            break;
        case 0x94:
            reportCount = value;
            break;
        case 0xA0:
            collections.push_back(value == 0x02 && !usages.empty() && usages.back() == kHidFinger ? layout.fingers++
                                                                                                    : finger);
            usages.clear();
            break;
        case 0xC0:
            if (!collections.empty()) {
                collections.pop_back();
            }
            usages.clear();
            break;
        case 0x80:
        case 0x90:
        case 0xB0: {
            const HidKind kind = (prefix & 0xFCU) == 0x80 ? HidKind::Input
                                 : (prefix & 0xFCU) == 0x90 ? HidKind::Output
                                                            : HidKind::Feature;
            std::size_t& bits = layout.bits[{kind, reportId}];
            if ((value & 1U) == 0) {
                for (std::size_t i = 0; i < reportCount; ++i) {
                    layout.fields.push_back({.kind = kind,
                                             .reportId = reportId,
                                             .usage = usages.empty() ? 0 : usages[std::min(i, usages.size() - 1)],
                                             .bit = bits + i * reportSize,
                                             .size = reportSize,
                                             .logicalMaximum = logicalMaximum,
                                             .finger = finger});
                }
            }
            bits += reportSize * reportCount;
            usages.clear();
            break;
        }
        default:
            break;
        }
        at += 1 + size;
    }
    layout.complete = collections.empty();
    return layout;
}

inline std::uint32_t hidValue(std::span<const std::uint8_t> report, const HidField& field, bool numbered) noexcept
{
    const std::size_t base = numbered ? 8 : 0;
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < field.size && i < 32; ++i) {
        const std::size_t bit = base + field.bit + i;
        if (bit / 8 < report.size() && ((report[bit / 8] >> (bit % 8)) & 1U) != 0) {
            value |= 1U << i;
        }
    }
    return value;
}

}
