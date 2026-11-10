#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gs::ext {

using Digest512 = std::array<std::uint8_t, 64>;
using Ed25519Signature = std::array<std::uint8_t, 64>;
using Ed25519Key = std::array<std::uint8_t, 32>;

struct MinisignKey {
    std::array<std::uint8_t, 8> id{};
    Ed25519Key publicKey{};

    friend bool operator==(const MinisignKey&, const MinisignKey&) = default;
};

enum class MinisignResult : std::uint8_t { Valid, Malformed, OtherKey, BadSignature, BadComment };

[[nodiscard]] Digest512 sha512(std::string_view data);
[[nodiscard]] Digest512 blake2b512(std::string_view data);
[[nodiscard]] bool verifyEd25519(std::string_view message, const Ed25519Signature& signature,
                                 const Ed25519Key& publicKey);
[[nodiscard]] std::optional<MinisignKey> parseMinisignKey(std::string_view text);
[[nodiscard]] std::string minisignKeyId(const MinisignKey& key);
[[nodiscard]] MinisignResult verifyMinisign(std::string_view message, std::string_view signatureFile,
                                            const MinisignKey& key);

}
