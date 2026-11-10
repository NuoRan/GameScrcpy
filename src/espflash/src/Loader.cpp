#include "gs/espflash/Loader.h"

#include <gs/platform/Crypto.h>

#include <algorithm>
#include <array>
#include <cctype>

namespace gs::espflash {

namespace {

constexpr std::chrono::milliseconds kCommandTimeout{3000};
constexpr std::chrono::milliseconds kShortTimeout{500};
constexpr std::chrono::milliseconds kReadSlice{100};
constexpr std::size_t kHeaderSize = 8;
constexpr std::size_t kSyncSize = 36;
constexpr std::size_t kMd5TextSize = 32;
constexpr std::size_t kMd5RawSize = 16;
constexpr std::uint32_t kFlashBlock = 0x10000;
constexpr std::uint32_t kFlashSector = 0x1000;
constexpr std::uint32_t kFlashPage = 0x100;
constexpr std::uint32_t kStatusMask = 0xFFFF;

void append16(std::vector<std::uint8_t>& out, std::uint16_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

std::uint16_t load16(std::span<const std::uint8_t> bytes, std::size_t at) noexcept
{
    return static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8U));
}

std::uint32_t load32(std::span<const std::uint8_t> bytes, std::size_t at) noexcept
{
    return static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16U) | (static_cast<std::uint32_t>(bytes[at + 3]) << 24U);
}

std::vector<std::uint8_t> words(std::initializer_list<std::uint32_t> values)
{
    std::vector<std::uint8_t> out;
    out.reserve(values.size() * 4);
    for (const std::uint32_t value : values) {
        append32(out, value);
    }
    return out;
}

std::chrono::milliseconds remaining(std::chrono::steady_clock::time_point deadline)
{
    const auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    return std::max(left, std::chrono::milliseconds{0});
}

}

std::uint32_t dataChecksum(std::span<const std::uint8_t> data) noexcept
{
    std::uint32_t value = rom::kChecksumSeed;
    for (const std::uint8_t byte : data) {
        value ^= byte;
    }
    return value;
}

std::vector<std::uint8_t> commandPacket(std::uint8_t op, std::span<const std::uint8_t> data, std::uint32_t checksum)
{
    std::vector<std::uint8_t> packet;
    packet.reserve(kHeaderSize + data.size());
    packet.push_back(rom::kRequest);
    packet.push_back(op);
    append16(packet, static_cast<std::uint16_t>(data.size()));
    append32(packet, checksum);
    packet.insert(packet.end(), data.begin(), data.end());
    return packet;
}

std::optional<Response> parseResponse(std::span<const std::uint8_t> packet)
{
    if (packet.size() < kHeaderSize || packet[0] != rom::kResponse) {
        return std::nullopt;
    }
    const std::size_t size = load16(packet, 2);
    if (packet.size() < kHeaderSize + size) {
        return std::nullopt;
    }
    Response response;
    response.command = packet[1];
    response.value = load32(packet, 4);
    const std::span<const std::uint8_t> data = packet.subspan(kHeaderSize, size);
    std::size_t statusSize = 0;
    if (size >= 4) {
        statusSize = 4;
    } else if (size >= 2) {
        statusSize = 2;
    } else {
        return std::nullopt;
    }
    response.status = data[size - statusSize];
    response.error = data[size - statusSize + 1];
    response.data.assign(data.begin(), data.end() - static_cast<std::ptrdiff_t>(statusSize));
    return response;
}

std::vector<std::uint8_t> syncPayload()
{
    std::vector<std::uint8_t> payload(kSyncSize, 0x55);
    payload[0] = 0x07;
    payload[1] = 0x07;
    payload[2] = 0x12;
    payload[3] = 0x20;
    return payload;
}

RomLoader::RomLoader(esp32::Port& port) noexcept
    : m_port(port)
{
}

std::optional<Response> RomLoader::command(std::uint8_t op, std::span<const std::uint8_t> data, std::uint32_t checksum,
                                           std::chrono::milliseconds timeout)
{
    const std::vector<std::uint8_t> framed = slipEncode(commandPacket(op, data, checksum));
    if (!m_port.write(framed)) {
        return std::nullopt;
    }
    return receive(op, std::chrono::steady_clock::now() + timeout);
}

std::optional<Response> RomLoader::receive(std::uint8_t op, std::chrono::steady_clock::time_point deadline)
{
    std::array<std::uint8_t, 4096> buffer{};
    for (;;) {
        while (std::optional<std::vector<std::uint8_t>> packet = m_decoder.next()) {
            std::optional<Response> response = parseResponse(*packet);
            if (response && response->command == op) {
                return response;
            }
        }
        const std::chrono::milliseconds left = remaining(deadline);
        if (left.count() <= 0 || m_port.gone()) {
            return std::nullopt;
        }
        const std::size_t received = m_port.read(buffer, std::min(left, kReadSlice));
        if (received > 0) {
            m_decoder.feed({buffer.data(), received});
        }
    }
}

void RomLoader::drain(std::chrono::milliseconds window)
{
    const auto deadline = std::chrono::steady_clock::now() + window;
    std::array<std::uint8_t, 1024> buffer{};
    for (std::chrono::milliseconds left = remaining(deadline); left.count() > 0; left = remaining(deadline)) {
        (void)m_port.read(buffer, std::min(left, kReadSlice));
    }
    m_decoder.clear();
    m_port.discardInput();
}

bool RomLoader::sync(int attempts, std::chrono::milliseconds timeout)
{
    const std::vector<std::uint8_t> payload = syncPayload();
    for (int attempt = 0; attempt < attempts; ++attempt) {
        m_decoder.clear();
        const std::optional<Response> response = command(rom::kSync, payload, 0, timeout);
        if (response && response->ok()) {
            drain(kReadSlice);
            return true;
        }
        if (m_port.gone()) {
            return false;
        }
    }
    return false;
}

std::optional<std::uint32_t> RomLoader::readRegister(std::uint32_t address)
{
    const std::optional<Response> response = command(rom::kReadRegister, words({address}), 0, kCommandTimeout);
    if (!response || !response->ok()) {
        return std::nullopt;
    }
    return response->value;
}

bool RomLoader::writeRegister(std::uint32_t address, std::uint32_t value, std::uint32_t mask)
{
    const std::optional<Response> response =
        command(rom::kWriteRegister, words({address, value, mask, 0}), 0, kCommandTimeout);
    return response && response->ok();
}

std::optional<std::vector<std::uint8_t>> RomLoader::securityInfo()
{
    const std::optional<Response> response = command(rom::kGetSecurityInfo, {}, 0, kShortTimeout);
    if (!response || !response->ok()) {
        return std::nullopt;
    }
    return response->data;
}

bool RomLoader::attachFlash()
{
    const std::optional<Response> response = command(rom::kSpiAttach, words({0, 0}), 0, kCommandTimeout);
    return response && response->ok();
}

bool RomLoader::setFlashParameters(std::uint32_t size)
{
    const std::optional<Response> response = command(
        rom::kSpiSetParams, words({0, size, kFlashBlock, kFlashSector, kFlashPage, kStatusMask}), 0, kCommandTimeout);
    return response && response->ok();
}

bool RomLoader::changeBaud(std::uint32_t baud)
{
    const std::optional<Response> response = command(rom::kChangeBaudrate, words({baud, 0}), 0, kCommandTimeout);
    return response && response->ok();
}

std::optional<Response> RomLoader::beginFlash(bool compressed, std::uint32_t size, std::uint32_t packets,
                                              std::uint32_t packetSize, std::uint32_t offset, bool encryptedFlag,
                                              std::chrono::milliseconds timeout)
{
    std::vector<std::uint8_t> data = words({size, packets, packetSize, offset});
    if (encryptedFlag) {
        append32(data, 0);
    }
    return command(compressed ? rom::kFlashDeflBegin : rom::kFlashBegin, data, 0, timeout);
}

std::optional<Response> RomLoader::flashBlock(bool compressed, std::span<const std::uint8_t> block,
                                              std::uint32_t sequence, std::chrono::milliseconds timeout)
{
    std::vector<std::uint8_t> data = words({static_cast<std::uint32_t>(block.size()), sequence, 0, 0});
    data.insert(data.end(), block.begin(), block.end());
    return command(compressed ? rom::kFlashDeflData : rom::kFlashData, data, dataChecksum(block), timeout);
}

void RomLoader::finishFlash(bool compressed, bool reboot)
{
    (void)command(compressed ? rom::kFlashDeflEnd : rom::kFlashEnd, words({reboot ? 0U : 1U}), 0, kShortTimeout);
}

std::optional<std::string> RomLoader::flashMd5(std::uint32_t offset, std::uint32_t size,
                                               std::chrono::milliseconds timeout)
{
    const std::optional<Response> response = command(rom::kSpiFlashMd5, words({offset, size, 0, 0}), 0, timeout);
    if (!response || !response->ok()) {
        return std::nullopt;
    }
    if (response->data.size() == kMd5TextSize) {
        std::string text(response->data.begin(), response->data.end());
        std::ranges::transform(text, text.begin(),
                               [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        return text;
    }
    if (response->data.size() == kMd5RawSize) {
        return platform::toHex(response->data);
    }
    return std::nullopt;
}

}
