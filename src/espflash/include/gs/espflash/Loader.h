#pragma once

#include "gs/espflash/Slip.h"

#include <gs/esp32/Port.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gs::espflash {

namespace rom {
inline constexpr std::uint8_t kFlashBegin = 0x02;
inline constexpr std::uint8_t kFlashData = 0x03;
inline constexpr std::uint8_t kFlashEnd = 0x04;
inline constexpr std::uint8_t kSync = 0x08;
inline constexpr std::uint8_t kWriteRegister = 0x09;
inline constexpr std::uint8_t kReadRegister = 0x0A;
inline constexpr std::uint8_t kSpiSetParams = 0x0B;
inline constexpr std::uint8_t kSpiAttach = 0x0D;
inline constexpr std::uint8_t kChangeBaudrate = 0x0F;
inline constexpr std::uint8_t kFlashDeflBegin = 0x10;
inline constexpr std::uint8_t kFlashDeflData = 0x11;
inline constexpr std::uint8_t kFlashDeflEnd = 0x12;
inline constexpr std::uint8_t kSpiFlashMd5 = 0x13;
inline constexpr std::uint8_t kGetSecurityInfo = 0x14;
inline constexpr std::uint8_t kRequest = 0x00;
inline constexpr std::uint8_t kResponse = 0x01;
inline constexpr std::uint32_t kChecksumSeed = 0xEF;
inline constexpr std::uint32_t kBlockSize = 0x400;
inline constexpr std::uint32_t kBaud = 115200;
inline constexpr std::uint8_t kInvalidMessage = 0x05;
}

struct Response {
    std::uint8_t command = 0;
    std::uint32_t value = 0;
    std::vector<std::uint8_t> data;
    std::uint8_t status = 0;
    std::uint8_t error = 0;

    [[nodiscard]] bool ok() const noexcept { return status == 0; }
};

[[nodiscard]] std::uint32_t dataChecksum(std::span<const std::uint8_t> data) noexcept;
[[nodiscard]] std::vector<std::uint8_t> commandPacket(std::uint8_t op, std::span<const std::uint8_t> data,
                                                      std::uint32_t checksum);
[[nodiscard]] std::optional<Response> parseResponse(std::span<const std::uint8_t> packet);
[[nodiscard]] std::vector<std::uint8_t> syncPayload();

class RomLoader
{
public:
    explicit RomLoader(esp32::Port& port) noexcept;

    [[nodiscard]] std::optional<Response> command(std::uint8_t op, std::span<const std::uint8_t> data,
                                                  std::uint32_t checksum, std::chrono::milliseconds timeout);
    [[nodiscard]] bool sync(int attempts, std::chrono::milliseconds timeout);
    [[nodiscard]] std::optional<std::uint32_t> readRegister(std::uint32_t address);
    [[nodiscard]] bool writeRegister(std::uint32_t address, std::uint32_t value, std::uint32_t mask);
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> securityInfo();
    [[nodiscard]] bool attachFlash();
    [[nodiscard]] bool setFlashParameters(std::uint32_t size);
    [[nodiscard]] bool changeBaud(std::uint32_t baud);
    [[nodiscard]] std::optional<Response> beginFlash(bool compressed, std::uint32_t size, std::uint32_t packets,
                                                     std::uint32_t packetSize, std::uint32_t offset,
                                                     bool encryptedFlag, std::chrono::milliseconds timeout);
    [[nodiscard]] std::optional<Response> flashBlock(bool compressed, std::span<const std::uint8_t> block,
                                                     std::uint32_t sequence, std::chrono::milliseconds timeout);
    void finishFlash(bool compressed, bool reboot);
    [[nodiscard]] std::optional<std::string> flashMd5(std::uint32_t offset, std::uint32_t size,
                                                      std::chrono::milliseconds timeout);
    void drain(std::chrono::milliseconds window);

private:
    [[nodiscard]] std::optional<Response> receive(std::uint8_t op, std::chrono::steady_clock::time_point deadline);

    esp32::Port& m_port;
    SlipDecoder m_decoder;
};

}
