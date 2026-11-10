#include "gs/ext/Minisign.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
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

}
