#pragma once

#include <gs/base/Expected.h>
#include <gs/protocol/Constants.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gs::app {

inline constexpr std::array<std::uint8_t, 4> kLanMagic{'G', 'S', 'C', 'L'};
inline constexpr std::uint8_t kLanVersion = 1;
inline constexpr std::size_t kLanKeySize = 32;
inline constexpr std::size_t kLanIdSize = 16;
inline constexpr std::size_t kLanNonceSize = 16;
inline constexpr std::size_t kLanMaxToken = 64;
inline constexpr std::size_t kLanHelloHead = 6;
inline constexpr std::size_t kLanReplySize = 6 + kLanIdSize + kLanNonceSize;
inline constexpr std::size_t kLanTagSize = 16;
inline constexpr std::uint32_t kLanMinRecord = protocol::kFrameHeaderSize + kLanTagSize;
inline constexpr std::uint32_t kLanMaxRecord =
    protocol::kFrameHeaderSize + protocol::kCompanionPayloadLimit + kLanTagSize;
inline constexpr std::uint16_t kLanPort = 27185;
inline constexpr std::string_view kLanContext = "GSCP/1 companion lan";
inline constexpr std::string_view kLanKeyProtected = "dpapi:";
inline constexpr std::string_view kLanKeyEntropy = "GameScrcpy companion pairing key 1";

using LanKey = std::array<std::uint8_t, kLanKeySize>;
using LanId = std::array<std::uint8_t, kLanIdSize>;
using LanNonce = std::array<std::uint8_t, kLanNonceSize>;
using LanPrefix = std::array<std::uint8_t, 4>;

enum class LanStatus : std::uint8_t { Accepted = 0, Unknown = 1, VersionMismatch = 2 };
enum class LanError : std::uint8_t { Truncated, BadMagic, VersionMismatch, LengthExceeded, InvalidEnum, OutOfRange, BadTag };
enum class LanDirection : std::uint32_t { ToCompanion = 0, ToComputer = 1 };

struct LanHello {
    std::string token;
    LanId phone{};
    LanNonce nonce{};

    friend bool operator==(const LanHello&, const LanHello&) = default;
};

struct LanReply {
    LanStatus status = LanStatus::Accepted;
    LanId pc{};
    LanNonce nonce{};

    friend bool operator==(const LanReply&, const LanReply&) = default;
};

struct LanPairing {
    LanId phone{};
    LanKey key{};
    LanId pc{};

    friend bool operator==(const LanPairing&, const LanPairing&) = default;
};

struct LanPairingText {
    std::string phone;
    std::string key;
    std::string pc;
};

[[nodiscard]] std::string_view lanErrorName(LanError error) noexcept;
[[nodiscard]] std::string base64Url(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::string newLanToken();
[[nodiscard]] LanKey newLanKey();
[[nodiscard]] LanId newLanId();
[[nodiscard]] std::string companionQrText(std::string_view host, std::uint16_t port, std::string_view token,
                                          const LanKey& key);
[[nodiscard]] std::string requestTargetOf(std::string_view url);
[[nodiscard]] LanPairingText pairingText(const LanPairing& pairing);
[[nodiscard]] std::optional<LanPairing> pairingOf(const LanPairingText& text);
[[nodiscard]] std::string protectedLanKey(const LanKey& key);
[[nodiscard]] std::optional<LanKey> lanKeyOf(std::string_view stored);
[[nodiscard]] std::optional<std::string> reprotectedLanKey(std::string_view stored);

[[nodiscard]] std::vector<std::uint8_t> encodeLanHello(const LanHello& hello);
[[nodiscard]] Expected<std::size_t, LanError> lanHelloSize(std::span<const std::uint8_t> head);
[[nodiscard]] Expected<LanHello, LanError> decodeLanHello(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::vector<std::uint8_t> encodeLanReply(const LanReply& reply);
[[nodiscard]] Expected<LanReply, LanError> decodeLanReply(std::span<const std::uint8_t> bytes);
[[nodiscard]] LanKey lanSessionKey(const LanKey& key, std::span<const std::uint8_t> hello,
                                   std::span<const std::uint8_t> reply);

class LanSealer final
{
public:
    LanSealer(const LanKey& session, LanDirection direction, std::uint64_t counter = 0) noexcept;

    [[nodiscard]] std::vector<std::uint8_t> seal(std::span<const std::uint8_t> frame);

private:
    LanKey m_key;
    LanDirection m_direction;
    std::uint64_t m_counter;
};

class LanOpener final
{
public:
    LanOpener(const LanKey& session, LanDirection direction, std::uint64_t counter = 0) noexcept;

    [[nodiscard]] static Expected<std::uint32_t, LanError> recordLength(const LanPrefix& prefix) noexcept;
    [[nodiscard]] Expected<std::vector<std::uint8_t>, LanError> open(const LanPrefix& prefix,
                                                                     std::span<const std::uint8_t> body);

private:
    LanKey m_key;
    LanDirection m_direction;
    std::uint64_t m_counter;
};

}
