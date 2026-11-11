#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gs::ext {

using Digest512 = std::array<std::uint8_t, 64>;
using Digest256 = std::array<std::uint8_t, 32>;
using Ed25519Signature = std::array<std::uint8_t, 64>;
using Ed25519Key = std::array<std::uint8_t, 32>;
using Ed25519Seed = std::array<std::uint8_t, 32>;
using MinisignKeyId = std::array<std::uint8_t, 8>;

struct MinisignKey {
    MinisignKeyId id{};
    Ed25519Key publicKey{};

    friend bool operator==(const MinisignKey&, const MinisignKey&) = default;
};

struct MinisignSecretKey {
    MinisignKeyId id{};
    Ed25519Seed seed{};
    Ed25519Key publicKey{};
};

enum class MinisignResult : std::uint8_t { Valid, Malformed, OtherKey, BadSignature, BadComment };

enum class SecretKeyState : std::uint8_t { Valid, Malformed, Encrypted, Damaged };

struct LoadedSecretKey {
    SecretKeyState state = SecretKeyState::Malformed;
    MinisignSecretKey key;
};

[[nodiscard]] Digest512 sha512(std::string_view data);
[[nodiscard]] Digest512 blake2b512(std::string_view data);
[[nodiscard]] Digest256 blake2b256(std::string_view data);
[[nodiscard]] bool verifyEd25519(std::string_view message, const Ed25519Signature& signature,
                                 const Ed25519Key& publicKey);
[[nodiscard]] Ed25519Key ed25519PublicKey(const Ed25519Seed& seed);
[[nodiscard]] Ed25519Signature signEd25519(std::string_view message, const Ed25519Seed& seed);
[[nodiscard]] std::optional<MinisignKey> parseMinisignKey(std::string_view text);
[[nodiscard]] std::string minisignKeyId(const MinisignKey& key);
[[nodiscard]] MinisignResult verifyMinisign(std::string_view message, std::string_view signatureFile,
                                            const MinisignKey& key);

[[nodiscard]] MinisignSecretKey makeMinisignKey(const Ed25519Seed& seed, const MinisignKeyId& id);
[[nodiscard]] MinisignSecretKey generateMinisignKey();
[[nodiscard]] MinisignKey minisignPublicKey(const MinisignSecretKey& key);
[[nodiscard]] std::string minisignKeyText(const MinisignKey& key);
[[nodiscard]] std::string minisignPublicKeyFile(const MinisignKey& key);
[[nodiscard]] std::string minisignSecretKeyFile(const MinisignSecretKey& key);
[[nodiscard]] LoadedSecretKey parseMinisignSecretKey(std::string_view file);
[[nodiscard]] std::string signMinisign(std::string_view message, const MinisignSecretKey& key,
                                       std::string_view trustedComment);

}
