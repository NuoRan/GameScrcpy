#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gs::platform {

using Sha256Digest = std::array<std::uint8_t, 32>;

[[nodiscard]] Sha256Digest sha256(std::span<const std::uint8_t> data);

[[nodiscard]] Sha256Digest hmacSha256(std::span<const std::uint8_t> key,
                                      std::initializer_list<std::span<const std::uint8_t>> parts);

void randomBytes(std::span<std::uint8_t> out);

[[nodiscard]] std::string toHex(std::span<const std::uint8_t> bytes);

inline constexpr std::size_t kAesGcmKeySize = 32;
inline constexpr std::size_t kAesGcmNonceSize = 12;
inline constexpr std::size_t kAesGcmTagSize = 16;

[[nodiscard]] std::vector<std::uint8_t> aesGcmSeal(std::span<const std::uint8_t, kAesGcmKeySize> key,
                                                   std::span<const std::uint8_t, kAesGcmNonceSize> nonce,
                                                   std::span<const std::uint8_t> plaintext,
                                                   std::span<const std::uint8_t> aad);

[[nodiscard]] std::optional<std::vector<std::uint8_t>> aesGcmOpen(std::span<const std::uint8_t, kAesGcmKeySize> key,
                                                                  std::span<const std::uint8_t, kAesGcmNonceSize> nonce,
                                                                  std::span<const std::uint8_t> sealed,
                                                                  std::span<const std::uint8_t> aad);

[[nodiscard]] std::optional<std::vector<std::uint8_t>> protectForUser(std::span<const std::uint8_t> secret,
                                                                      std::span<const std::uint8_t> entropy);

[[nodiscard]] std::optional<std::vector<std::uint8_t>> unprotectForUser(std::span<const std::uint8_t> blob,
                                                                        std::span<const std::uint8_t> entropy);

}
