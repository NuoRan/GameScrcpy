#include "gs/protocol/Codec.h"

namespace gs::protocol {

bool isValidUtf8(std::span<const std::uint8_t> bytes) noexcept
{
    std::size_t i = 0;
    while (i < bytes.size()) {
        const std::uint8_t lead = bytes[i];
        if (lead < 0x80U) {
            ++i;
            continue;
        }

        std::size_t length = 0;
        std::uint32_t codePoint = 0;
        std::uint32_t minimum = 0;
        if ((lead & 0xE0U) == 0xC0U) {
            length = 2;
            codePoint = lead & 0x1FU;
            minimum = 0x80U;
        } else if ((lead & 0xF0U) == 0xE0U) {
            length = 3;
            codePoint = lead & 0x0FU;
            minimum = 0x800U;
        } else if ((lead & 0xF8U) == 0xF0U) {
            length = 4;
            codePoint = lead & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }

        if (bytes.size() - i < length) {
            return false;
        }
        for (std::size_t k = 1; k < length; ++k) {
            const std::uint8_t next = bytes[i + k];
            if ((next & 0xC0U) != 0x80U) {
                return false;
            }
            codePoint = (codePoint << 6U) | (next & 0x3FU);
        }
        if (codePoint < minimum || codePoint > 0x10FFFFU ||
            (codePoint >= 0xD800U && codePoint <= 0xDFFFU)) {
            return false;
        }
        i += length;
    }
    return true;
}

}
