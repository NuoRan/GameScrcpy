#include "gs/espflash/Md5.h"

#include <gs/platform/Crypto.h>

#include <windows.h>
#include <bcrypt.h>

#include <limits>

namespace gs::espflash {

Md5Digest md5(std::span<const std::uint8_t> data)
{
    Md5Digest digest{};
    if (data.size() > std::numeric_limits<ULONG>::max()) {
        return digest;
    }
    const NTSTATUS status = BCryptHash(BCRYPT_MD5_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(data.data()),
                                       static_cast<ULONG>(data.size()), digest.data(), static_cast<ULONG>(digest.size()));
    if (!BCRYPT_SUCCESS(status)) {
        digest = {};
    }
    return digest;
}

std::string md5Hex(std::span<const std::uint8_t> data)
{
    return platform::toHex(md5(data));
}

}
