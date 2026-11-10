#include "CompanionCipher.h"

#include <gs/base/ByteOrder.h>
#include <gs/platform/Crypto.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <system_error>

namespace gs::app {

namespace {

constexpr std::string_view kBase64Url = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::span<const std::uint8_t> bytesOf(std::string_view text) noexcept
{
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

template <std::size_t N>
std::optional<std::array<std::uint8_t, N>> hexArray(std::string_view text)
{
    if (text.size() != 2 * N) {
        return std::nullopt;
    }
    std::array<std::uint8_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        const char* first = text.data() + 2 * i;
        unsigned value = 0;
        const auto [end, error] = std::from_chars(first, first + 2, value, 16);
        if (error != std::errc{} || end != first + 2) {
            return std::nullopt;
        }
        out[i] = static_cast<std::uint8_t>(value);
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> hexBytes(std::string_view text)
{
    if (text.empty() || text.size() % 2 != 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out(text.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const char* first = text.data() + 2 * i;
        unsigned value = 0;
        const auto [end, error] = std::from_chars(first, first + 2, value, 16);
        if (error != std::errc{} || end != first + 2) {
            return std::nullopt;
        }
        out[i] = static_cast<std::uint8_t>(value);
    }
    return out;
}

std::array<std::uint8_t, platform::kAesGcmNonceSize> recordNonce(LanDirection direction,
                                                                 std::uint64_t counter) noexcept
{
    std::array<std::uint8_t, platform::kAesGcmNonceSize> nonce{};
    storeBigEndian<std::uint32_t>(static_cast<std::uint32_t>(direction), std::span(nonce).first<4>());
    storeBigEndian<std::uint64_t>(counter, std::span(nonce).subspan<4, 8>());
    return nonce;
}

Expected<std::uint8_t, LanError> checkHead(std::span<const std::uint8_t> bytes, std::size_t size)
{
    if (bytes.size() < size) {
        return makeUnexpected(LanError::Truncated);
    }
    if (!std::equal(kLanMagic.begin(), kLanMagic.end(), bytes.begin())) {
        return makeUnexpected(LanError::BadMagic);
    }
    if (bytes[4] != kLanVersion) {
        return makeUnexpected(LanError::VersionMismatch);
    }
    return bytes[5];
}

}

std::string_view lanErrorName(LanError error) noexcept
{
    switch (error) {
    case LanError::Truncated:
        return "Truncated";
    case LanError::BadMagic:
        return "BadMagic";
    case LanError::VersionMismatch:
        return "VersionMismatch";
    case LanError::LengthExceeded:
        return "LengthExceeded";
    case LanError::InvalidEnum:
        return "InvalidEnum";
    case LanError::OutOfRange:
        return "OutOfRange";
    case LanError::BadTag:
        break;
    }
    return "BadTag";
}

std::string base64Url(std::span<const std::uint8_t> bytes)
{
    std::string text;
    text.reserve((bytes.size() * 4 + 2) / 3);
    std::size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3) {
        const std::uint32_t group = (std::uint32_t{bytes[i]} << 16U) | (std::uint32_t{bytes[i + 1]} << 8U) | bytes[i + 2];
        text.push_back(kBase64Url[(group >> 18U) & 0x3FU]);
        text.push_back(kBase64Url[(group >> 12U) & 0x3FU]);
        text.push_back(kBase64Url[(group >> 6U) & 0x3FU]);
        text.push_back(kBase64Url[group & 0x3FU]);
    }
    const std::size_t left = bytes.size() - i;
    if (left == 1) {
        const std::uint32_t group = std::uint32_t{bytes[i]} << 16U;
        text.push_back(kBase64Url[(group >> 18U) & 0x3FU]);
        text.push_back(kBase64Url[(group >> 12U) & 0x3FU]);
    } else if (left == 2) {
        const std::uint32_t group = (std::uint32_t{bytes[i]} << 16U) | (std::uint32_t{bytes[i + 1]} << 8U);
        text.push_back(kBase64Url[(group >> 18U) & 0x3FU]);
        text.push_back(kBase64Url[(group >> 12U) & 0x3FU]);
        text.push_back(kBase64Url[(group >> 6U) & 0x3FU]);
    }
    return text;
}

std::string newLanToken()
{
    std::array<std::uint8_t, 16> random{};
    platform::randomBytes(random);
    return base64Url(random);
}

LanKey newLanKey()
{
    LanKey key{};
    platform::randomBytes(key);
    return key;
}

LanId newLanId()
{
    LanId id{};
    platform::randomBytes(id);
    return id;
}

std::string companionQrText(std::string_view host, std::uint16_t port, std::string_view token, const LanKey& key)
{
    return std::format("http://{}:{}/c/{}#k={}", host, port, token, base64Url(key));
}

std::string requestTargetOf(std::string_view url)
{
    const std::size_t scheme = url.find("://");
    std::string_view rest = scheme == std::string_view::npos ? url : url.substr(scheme + 3);
    const std::size_t path = rest.find('/');
    rest = path == std::string_view::npos ? std::string_view("/") : rest.substr(path);
    return std::string(rest.substr(0, rest.find('#')));
}

LanPairingText pairingText(const LanPairing& pairing)
{
    return {.phone = platform::toHex(pairing.phone),
            .key = protectedLanKey(pairing.key),
            .pc = platform::toHex(pairing.pc)};
}

std::optional<LanPairing> pairingOf(const LanPairingText& text)
{
    const auto phone = hexArray<kLanIdSize>(text.phone);
    const auto key = lanKeyOf(text.key);
    const auto pc = hexArray<kLanIdSize>(text.pc);
    if (!phone || !key || !pc) {
        return std::nullopt;
    }
    return LanPairing{.phone = *phone, .key = *key, .pc = *pc};
}

std::string protectedLanKey(const LanKey& key)
{
    const std::optional<std::vector<std::uint8_t>> sealed =
        platform::protectForUser(key, bytesOf(kLanKeyEntropy));
    if (!sealed) {
        return {};
    }
    return std::string(kLanKeyProtected) + platform::toHex(*sealed);
}

std::optional<LanKey> lanKeyOf(std::string_view stored)
{
    if (!stored.starts_with(kLanKeyProtected)) {
        return hexArray<kLanKeySize>(stored);
    }
    const std::optional<std::vector<std::uint8_t>> sealed = hexBytes(stored.substr(kLanKeyProtected.size()));
    if (!sealed) {
        return std::nullopt;
    }
    const std::optional<std::vector<std::uint8_t>> opened =
        platform::unprotectForUser(*sealed, bytesOf(kLanKeyEntropy));
    if (!opened || opened->size() != kLanKeySize) {
        return std::nullopt;
    }
    LanKey key{};
    std::ranges::copy(*opened, key.begin());
    return key;
}

std::optional<std::string> reprotectedLanKey(std::string_view stored)
{
    if (stored.starts_with(kLanKeyProtected)) {
        return std::nullopt;
    }
    const std::optional<LanKey> key = hexArray<kLanKeySize>(stored);
    if (!key) {
        return std::nullopt;
    }
    std::string sealed = protectedLanKey(*key);
    if (sealed.empty()) {
        return std::nullopt;
    }
    return sealed;
}

std::vector<std::uint8_t> encodeLanHello(const LanHello& hello)
{
    const std::size_t token = std::min(hello.token.size(), kLanMaxToken);
    std::vector<std::uint8_t> out(kLanMagic.begin(), kLanMagic.end());
    out.push_back(kLanVersion);
    out.push_back(static_cast<std::uint8_t>(token));
    const std::span<const std::uint8_t> text = bytesOf(hello.token).first(token);
    out.insert(out.end(), text.begin(), text.end());
    out.insert(out.end(), hello.phone.begin(), hello.phone.end());
    out.insert(out.end(), hello.nonce.begin(), hello.nonce.end());
    return out;
}

Expected<std::size_t, LanError> lanHelloSize(std::span<const std::uint8_t> head)
{
    const auto token = checkHead(head, kLanHelloHead);
    if (!token) {
        return makeUnexpected(token.error());
    }
    if (*token > kLanMaxToken) {
        return makeUnexpected(LanError::LengthExceeded);
    }
    return kLanHelloHead + *token + kLanIdSize + kLanNonceSize;
}

Expected<LanHello, LanError> decodeLanHello(std::span<const std::uint8_t> bytes)
{
    const auto size = lanHelloSize(bytes);
    if (!size) {
        return makeUnexpected(size.error());
    }
    if (bytes.size() < *size) {
        return makeUnexpected(LanError::Truncated);
    }
    LanHello hello;
    const std::size_t token = *size - kLanHelloHead - kLanIdSize - kLanNonceSize;
    const std::span<const std::uint8_t> text = bytes.subspan(kLanHelloHead, token);
    hello.token.assign(reinterpret_cast<const char*>(text.data()), text.size());
    const std::span<const std::uint8_t> phone = bytes.subspan(kLanHelloHead + token, kLanIdSize);
    std::ranges::copy(phone, hello.phone.begin());
    const std::span<const std::uint8_t> nonce = bytes.subspan(kLanHelloHead + token + kLanIdSize, kLanNonceSize);
    std::ranges::copy(nonce, hello.nonce.begin());
    return hello;
}

std::vector<std::uint8_t> encodeLanReply(const LanReply& reply)
{
    std::vector<std::uint8_t> out(kLanMagic.begin(), kLanMagic.end());
    out.push_back(kLanVersion);
    out.push_back(static_cast<std::uint8_t>(reply.status));
    out.insert(out.end(), reply.pc.begin(), reply.pc.end());
    out.insert(out.end(), reply.nonce.begin(), reply.nonce.end());
    return out;
}

Expected<LanReply, LanError> decodeLanReply(std::span<const std::uint8_t> bytes)
{
    const auto status = checkHead(bytes, kLanReplySize);
    if (!status) {
        return makeUnexpected(status.error());
    }
    if (*status > static_cast<std::uint8_t>(LanStatus::VersionMismatch)) {
        return makeUnexpected(LanError::InvalidEnum);
    }
    LanReply reply;
    reply.status = static_cast<LanStatus>(*status);
    std::ranges::copy(bytes.subspan(kLanHelloHead, kLanIdSize), reply.pc.begin());
    std::ranges::copy(bytes.subspan(kLanHelloHead + kLanIdSize, kLanNonceSize), reply.nonce.begin());
    return reply;
}

LanKey lanSessionKey(const LanKey& key, std::span<const std::uint8_t> hello, std::span<const std::uint8_t> reply)
{
    return platform::hmacSha256(key, {bytesOf(kLanContext), hello, reply});
}

LanSealer::LanSealer(const LanKey& session, LanDirection direction, std::uint64_t counter) noexcept
    : m_key(session), m_direction(direction), m_counter(counter)
{
}

std::vector<std::uint8_t> LanSealer::seal(std::span<const std::uint8_t> frame)
{
    LanPrefix prefix{};
    storeBigEndian<std::uint32_t>(static_cast<std::uint32_t>(frame.size() + kLanTagSize), prefix);
    const auto nonce = recordNonce(m_direction, m_counter++);
    const std::vector<std::uint8_t> sealed = platform::aesGcmSeal(m_key, nonce, frame, prefix);
    std::vector<std::uint8_t> record(prefix.begin(), prefix.end());
    record.insert(record.end(), sealed.begin(), sealed.end());
    return record;
}

LanOpener::LanOpener(const LanKey& session, LanDirection direction, std::uint64_t counter) noexcept
    : m_key(session), m_direction(direction), m_counter(counter)
{
}

Expected<std::uint32_t, LanError> LanOpener::recordLength(const LanPrefix& prefix) noexcept
{
    const std::uint32_t length = loadBigEndian<std::uint32_t>(prefix);
    if (length < kLanMinRecord || length > kLanMaxRecord) {
        return makeUnexpected(LanError::OutOfRange);
    }
    return length;
}

Expected<std::vector<std::uint8_t>, LanError> LanOpener::open(const LanPrefix& prefix,
                                                               std::span<const std::uint8_t> body)
{
    const auto length = recordLength(prefix);
    if (!length) {
        return makeUnexpected(length.error());
    }
    if (body.size() != *length) {
        return makeUnexpected(LanError::Truncated);
    }
    const auto nonce = recordNonce(m_direction, m_counter);
    std::optional<std::vector<std::uint8_t>> plain = platform::aesGcmOpen(m_key, nonce, body, prefix);
    if (!plain) {
        return makeUnexpected(LanError::BadTag);
    }
    ++m_counter;
    return std::move(*plain);
}

}
