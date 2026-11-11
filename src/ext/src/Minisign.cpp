#include "gs/ext/Minisign.h"

#include <gs/platform/Crypto.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gs::ext {

namespace {

constexpr std::string_view kUntrustedComment = "untrusted comment:";
constexpr std::string_view kTrustedComment = "trusted comment: ";
constexpr std::size_t kKeyBytes = 42;
constexpr std::size_t kSignatureBytes = 74;
constexpr std::size_t kMostSignatureFileBytes = 4096;
constexpr std::size_t kSecretKeyBytes = 158;
constexpr std::size_t kMostSecretKeyFileBytes = 4096;
constexpr std::size_t kMostTrustedCommentBytes = 1024;
constexpr std::string_view kBase64Digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64Value(char c) noexcept
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

std::optional<std::string> decoded(std::string_view text)
{
    if (text.empty() || text.size() % 4 != 0) {
        return std::nullopt;
    }
    const std::size_t padding = text.ends_with("==") ? 2 : text.ends_with('=') ? 1 : 0;
    std::string out;
    out.reserve(text.size() / 4 * 3);
    std::uint32_t group = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const int value = i >= text.size() - padding ? 0 : base64Value(text[i]);
        if (value < 0) {
            return std::nullopt;
        }
        group = (group << 6) | static_cast<std::uint32_t>(value);
        if (i % 4 == 3) {
            out.push_back(static_cast<char>((group >> 16) & 0xff));
            out.push_back(static_cast<char>((group >> 8) & 0xff));
            out.push_back(static_cast<char>(group & 0xff));
            group = 0;
        }
    }
    out.resize(out.size() - padding);
    return out;
}

std::vector<std::string_view> linesOf(std::string_view text)
{
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(start, end - start);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        start = end + 1;
    }
    return lines;
}

template <std::size_t N>
std::array<std::uint8_t, N> bytesAt(const std::string& data, std::size_t offset)
{
    std::array<std::uint8_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = static_cast<std::uint8_t>(data[offset + i]);
    }
    return out;
}

std::string encodedBase64(std::string_view bytes)
{
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::size_t count = std::min<std::size_t>(3, bytes.size() - i);
        std::uint32_t group = 0;
        for (std::size_t k = 0; k < 3; ++k) {
            const std::uint32_t byte = k < count ? static_cast<std::uint8_t>(bytes[i + k]) : 0U;
            group = (group << 8) | byte;
        }
        out.push_back(kBase64Digits[(group >> 18) & 63U]);
        out.push_back(kBase64Digits[(group >> 12) & 63U]);
        out.push_back(count > 1 ? kBase64Digits[(group >> 6) & 63U] : '=');
        out.push_back(count > 2 ? kBase64Digits[group & 63U] : '=');
    }
    return out;
}

template <std::size_t N>
void appendBytes(std::string& out, const std::array<std::uint8_t, N>& bytes)
{
    out.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::string_view viewOf(std::span<const std::uint8_t> bytes)
{
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::string secretKeyBlock(const MinisignSecretKey& key)
{
    std::string head = "Ed";
    appendBytes(head, key.id);
    appendBytes(head, key.seed);
    appendBytes(head, key.publicKey);
    const Digest256 checksum = blake2b256(head);
    std::string block = "Ed";
    block.append(2, '\0');
    block.append("B2");
    block.append(48, '\0');
    block.append(head.substr(2));
    appendBytes(block, checksum);
    return block;
}

std::string cleanComment(std::string_view text)
{
    std::string out;
    for (const char c : text.substr(0, kMostTrustedCommentBytes)) {
        out.push_back(c == '\n' || c == '\r' ? ' ' : c);
    }
    return out;
}

}

std::optional<MinisignKey> parseMinisignKey(std::string_view text)
{
    const std::optional<std::string> raw = decoded(text);
    if (!raw || raw->size() != kKeyBytes || (*raw)[0] != 'E' || (*raw)[1] != 'd') {
        return std::nullopt;
    }
    return MinisignKey{.id = bytesAt<8>(*raw, 2), .publicKey = bytesAt<32>(*raw, 10)};
}

std::string minisignKeyId(const MinisignKey& key)
{
    constexpr std::string_view kDigits = "0123456789ABCDEF";
    std::string text;
    text.reserve(16);
    for (std::size_t i = key.id.size(); i-- > 0;) {
        text.push_back(kDigits[key.id[i] >> 4]);
        text.push_back(kDigits[key.id[i] & 0x0f]);
    }
    return text;
}

MinisignResult verifyMinisign(std::string_view message, std::string_view signatureFile, const MinisignKey& key)
{
    if (signatureFile.size() > kMostSignatureFileBytes) {
        return MinisignResult::Malformed;
    }
    const std::vector<std::string_view> lines = linesOf(signatureFile);
    if (lines.size() < 4 || !lines[0].starts_with(kUntrustedComment) || !lines[2].starts_with(kTrustedComment)) {
        return MinisignResult::Malformed;
    }
    const std::optional<std::string> signature = decoded(lines[1]);
    const std::optional<std::string> global = decoded(lines[3]);
    if (!signature || signature->size() != kSignatureBytes || !global || global->size() != 64) {
        return MinisignResult::Malformed;
    }
    const std::string_view algorithm = std::string_view(*signature).substr(0, 2);
    if (algorithm != "Ed" && algorithm != "ED") {
        return MinisignResult::Malformed;
    }
    if (bytesAt<8>(*signature, 2) != key.id) {
        return MinisignResult::OtherKey;
    }
    const Ed25519Signature detached = bytesAt<64>(*signature, 10);
    const Digest512 digest = algorithm == "ED" ? blake2b512(message) : Digest512{};
    const std::string_view payload =
        algorithm == "ED" ? std::string_view(reinterpret_cast<const char*>(digest.data()), digest.size()) : message;
    if (!verifyEd25519(payload, detached, key.publicKey)) {
        return MinisignResult::BadSignature;
    }
    std::string commented(reinterpret_cast<const char*>(detached.data()), detached.size());
    commented.append(lines[2].substr(kTrustedComment.size()));
    if (!verifyEd25519(commented, bytesAt<64>(*global, 0), key.publicKey)) {
        return MinisignResult::BadComment;
    }
    return MinisignResult::Valid;
}

MinisignSecretKey makeMinisignKey(const Ed25519Seed& seed, const MinisignKeyId& id)
{
    return MinisignSecretKey{.id = id, .seed = seed, .publicKey = ed25519PublicKey(seed)};
}

MinisignSecretKey generateMinisignKey()
{
    Ed25519Seed seed{};
    MinisignKeyId id{};
    platform::randomBytes(seed);
    platform::randomBytes(id);
    return makeMinisignKey(seed, id);
}

MinisignKey minisignPublicKey(const MinisignSecretKey& key)
{
    return MinisignKey{.id = key.id, .publicKey = key.publicKey};
}

std::string minisignKeyText(const MinisignKey& key)
{
    std::string raw = "Ed";
    appendBytes(raw, key.id);
    appendBytes(raw, key.publicKey);
    return encodedBase64(raw);
}

std::string minisignPublicKeyFile(const MinisignKey& key)
{
    return std::string(kUntrustedComment) + " minisign public key " + minisignKeyId(key) + "\n" + minisignKeyText(key) +
           "\n";
}

std::string minisignSecretKeyFile(const MinisignSecretKey& key)
{
    return std::string(kUntrustedComment) + " minisign secret key " + minisignKeyId(minisignPublicKey(key)) +
           ", not protected by a password\n" + encodedBase64(secretKeyBlock(key)) + "\n";
}

LoadedSecretKey parseMinisignSecretKey(std::string_view file)
{
    LoadedSecretKey loaded;
    if (file.size() > kMostSecretKeyFileBytes) {
        return loaded;
    }
    const std::vector<std::string_view> lines = linesOf(file);
    if (lines.size() < 2 || !lines[0].starts_with(kUntrustedComment)) {
        return loaded;
    }
    const std::optional<std::string> raw = decoded(lines[1]);
    if (!raw || raw->size() != kSecretKeyBytes || raw->substr(0, 2) != "Ed" || raw->substr(4, 2) != "B2") {
        return loaded;
    }
    const std::string_view kdf = std::string_view(*raw).substr(2, 2);
    if (kdf == "Sc") {
        loaded.state = SecretKeyState::Encrypted;
        return loaded;
    }
    if (kdf != std::string_view("\0\0", 2)) {
        return loaded;
    }
    constexpr std::size_t kKeyStart = 54;
    MinisignSecretKey key{.id = bytesAt<8>(*raw, kKeyStart),
                          .seed = bytesAt<32>(*raw, kKeyStart + 8),
                          .publicKey = bytesAt<32>(*raw, kKeyStart + 40)};
    std::string head = "Ed";
    head.append(raw->substr(kKeyStart, 72));
    const Digest256 checksum = blake2b256(head);
    if (bytesAt<32>(*raw, kKeyStart + 72) != checksum || ed25519PublicKey(key.seed) != key.publicKey) {
        loaded.state = SecretKeyState::Damaged;
        return loaded;
    }
    loaded.state = SecretKeyState::Valid;
    loaded.key = key;
    return loaded;
}

std::string signMinisign(std::string_view message, const MinisignSecretKey& key, std::string_view trustedComment)
{
    const std::string comment = cleanComment(trustedComment);
    const Digest512 digest = blake2b512(message);
    const Ed25519Signature detached = signEd25519(viewOf(digest), key.seed);
    std::string block = "ED";
    appendBytes(block, key.id);
    appendBytes(block, detached);
    std::string commented(viewOf(detached));
    commented.append(comment);
    const Ed25519Signature global = signEd25519(commented, key.seed);
    return std::string(kUntrustedComment) + " signature from minisign secret key\n" + encodedBase64(block) + "\n" +
           std::string(kTrustedComment) + comment + "\n" + encodedBase64(viewOf(global)) + "\n";
}

}
