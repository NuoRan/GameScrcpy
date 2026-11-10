#include "gs/platform/Crypto.h"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace gs::platform {

namespace {

void require(NTSTATUS status) noexcept
{
    if (!BCRYPT_SUCCESS(status)) {
        std::abort();
    }
}

PUCHAR input(std::span<const std::uint8_t> bytes) noexcept
{
    return const_cast<PUCHAR>(bytes.data());
}

ULONG length(std::span<const std::uint8_t> bytes) noexcept
{
    if (bytes.size() > std::numeric_limits<ULONG>::max()) {
        std::abort();
    }
    return static_cast<ULONG>(bytes.size());
}

BCRYPT_ALG_HANDLE aesGcm() noexcept
{
    static const BCRYPT_ALG_HANDLE handle = [] {
        BCRYPT_ALG_HANDLE opened = nullptr;
        require(BCryptOpenAlgorithmProvider(&opened, BCRYPT_AES_ALGORITHM, nullptr, 0));
        require(BCryptSetProperty(opened, BCRYPT_CHAINING_MODE,
                                  reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                                  static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_GCM)), 0));
        return opened;
    }();
    return handle;
}

class GcmKey final
{
public:
    explicit GcmKey(std::span<const std::uint8_t, kAesGcmKeySize> key) noexcept
    {
        require(BCryptGenerateSymmetricKey(aesGcm(), &m_key, nullptr, 0, input(key), length(key), 0));
    }

    ~GcmKey() { BCryptDestroyKey(m_key); }
    GcmKey(const GcmKey&) = delete;
    GcmKey& operator=(const GcmKey&) = delete;

    [[nodiscard]] BCRYPT_KEY_HANDLE get() const noexcept { return m_key; }

private:
    BCRYPT_KEY_HANDLE m_key = nullptr;
};

BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO gcmInfo(std::span<const std::uint8_t> nonce, std::span<const std::uint8_t> aad,
                                              std::span<std::uint8_t> tag) noexcept
{
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = input(nonce);
    info.cbNonce = length(nonce);
    info.pbAuthData = aad.empty() ? nullptr : input(aad);
    info.cbAuthData = length(aad);
    info.pbTag = tag.data();
    info.cbTag = length(tag);
    return info;
}

DATA_BLOB blobOf(std::span<const std::uint8_t> bytes) noexcept
{
    return {.cbData = length(bytes), .pbData = const_cast<BYTE*>(bytes.data())};
}

std::vector<std::uint8_t> takeBlob(DATA_BLOB& blob, bool secret)
{
    std::vector<std::uint8_t> bytes;
    if (blob.pbData != nullptr) {
        bytes.assign(blob.pbData, blob.pbData + blob.cbData);
        if (secret) {
            SecureZeroMemory(blob.pbData, blob.cbData);
        }
        LocalFree(blob.pbData);
    }
    return bytes;
}

}

Sha256Digest sha256(std::span<const std::uint8_t> data)
{
    Sha256Digest digest{};
    require(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, input(data), length(data), digest.data(),
                       static_cast<ULONG>(digest.size())));
    return digest;
}

Sha256Digest hmacSha256(std::span<const std::uint8_t> key, std::initializer_list<std::span<const std::uint8_t>> parts)
{
    BCRYPT_HASH_HANDLE hash = nullptr;
    require(BCryptCreateHash(BCRYPT_HMAC_SHA256_ALG_HANDLE, &hash, nullptr, 0, input(key), length(key), 0));
    for (const std::span<const std::uint8_t> part : parts) {
        if (!part.empty()) {
            require(BCryptHashData(hash, input(part), length(part), 0));
        }
    }
    Sha256Digest digest{};
    require(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0));
    BCryptDestroyHash(hash);
    return digest;
}

void randomBytes(std::span<std::uint8_t> out)
{
    if (!out.empty()) {
        require(BCryptGenRandom(nullptr, out.data(), length(out), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
    }
}

std::string toHex(std::span<const std::uint8_t> bytes)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const std::uint8_t byte : bytes) {
        text.push_back(kDigits[byte >> 4U]);
        text.push_back(kDigits[byte & 0x0FU]);
    }
    return text;
}

std::vector<std::uint8_t> aesGcmSeal(std::span<const std::uint8_t, kAesGcmKeySize> key,
                                     std::span<const std::uint8_t, kAesGcmNonceSize> nonce,
                                     std::span<const std::uint8_t> plaintext, std::span<const std::uint8_t> aad)
{
    const GcmKey handle(key);
    std::array<std::uint8_t, kAesGcmTagSize> tag{};
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info = gcmInfo(nonce, aad, tag);
    std::vector<std::uint8_t> sealed(plaintext.size() + kAesGcmTagSize);
    std::uint8_t scratch = 0;
    const bool empty = plaintext.empty();
    ULONG written = 0;
    require(BCryptEncrypt(handle.get(), empty ? &scratch : input(plaintext), length(plaintext), &info, nullptr, 0,
                          empty ? &scratch : sealed.data(), length(plaintext), &written, 0));
    std::ranges::copy(tag, sealed.begin() + static_cast<std::ptrdiff_t>(plaintext.size()));
    return sealed;
}

std::optional<std::vector<std::uint8_t>> aesGcmOpen(std::span<const std::uint8_t, kAesGcmKeySize> key,
                                                    std::span<const std::uint8_t, kAesGcmNonceSize> nonce,
                                                    std::span<const std::uint8_t> sealed,
                                                    std::span<const std::uint8_t> aad)
{
    if (sealed.size() < kAesGcmTagSize) {
        return std::nullopt;
    }
    const std::span<const std::uint8_t> ciphertext = sealed.first(sealed.size() - kAesGcmTagSize);
    std::array<std::uint8_t, kAesGcmTagSize> tag{};
    std::ranges::copy(sealed.last(kAesGcmTagSize), tag.begin());
    const GcmKey handle(key);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info = gcmInfo(nonce, aad, tag);
    std::vector<std::uint8_t> plaintext(ciphertext.size());
    std::uint8_t scratch = 0;
    const bool empty = ciphertext.empty();
    ULONG written = 0;
    const NTSTATUS status =
        BCryptDecrypt(handle.get(), empty ? &scratch : input(ciphertext), length(ciphertext), &info, nullptr, 0,
                      empty ? &scratch : plaintext.data(), length(ciphertext), &written, 0);
    if (!BCRYPT_SUCCESS(status) || written != length(ciphertext)) {
        return std::nullopt;
    }
    return plaintext;
}

std::optional<std::vector<std::uint8_t>> protectForUser(std::span<const std::uint8_t> secret,
                                                        std::span<const std::uint8_t> entropy)
{
    DATA_BLOB plain = blobOf(secret);
    DATA_BLOB salt = blobOf(entropy);
    DATA_BLOB sealed{};
    if (CryptProtectData(&plain, nullptr, entropy.empty() ? nullptr : &salt, nullptr, nullptr,
                         CRYPTPROTECT_UI_FORBIDDEN, &sealed) == FALSE) {
        return std::nullopt;
    }
    return takeBlob(sealed, false);
}

std::optional<std::vector<std::uint8_t>> unprotectForUser(std::span<const std::uint8_t> blob,
                                                          std::span<const std::uint8_t> entropy)
{
    if (blob.empty()) {
        return std::nullopt;
    }
    DATA_BLOB sealed = blobOf(blob);
    DATA_BLOB salt = blobOf(entropy);
    DATA_BLOB plain{};
    if (CryptUnprotectData(&sealed, nullptr, entropy.empty() ? nullptr : &salt, nullptr, nullptr,
                           CRYPTPROTECT_UI_FORBIDDEN, &plain) == FALSE) {
        return std::nullopt;
    }
    return takeBlob(plain, true);
}

}
