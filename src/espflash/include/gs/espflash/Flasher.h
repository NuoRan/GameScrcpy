#pragma once

#include "gs/espflash/Chips.h"

#include <gs/base/Expected.h>
#include <gs/esp32/Port.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>

namespace gs::espflash {

enum class EnterMode : std::uint8_t { Auto, Firmware, Classic, UsbJtag, None };

enum class FlashStage : std::uint8_t { Preparing, Connecting, Detecting, Erasing, Writing, Verifying, Restarting, Done };

enum class FlashFailure : std::uint8_t {
    NoFirmware,
    NoImageForChip,
    ImageMissing,
    ImageCorrupt,
    PortMissing,
    PortBusy,
    NoDownloadMode,
    UnknownChip,
    Refused,
    WriteFailed,
    VerifyFailed,
    Lost,
    Cancelled,
};

struct FlashRequest {
    std::string port;
    esp32::PortKind kind = esp32::PortKind::Other;
    bool usbSerialJtag = false;
    EnterMode enter = EnterMode::Auto;
    std::filesystem::path firmwareFolder;
    std::uint32_t fastBaud = 460800;
};

struct FlashResult {
    Chip chip = Chip::Esp32;
    std::string version;
    std::size_t bytes = 0;
    bool compressed = true;
};

using FlashProgress = std::function<void(FlashStage stage, std::size_t done, std::size_t total)>;
using PortOpener =
    std::function<std::unique_ptr<esp32::Port>(std::string_view name, std::uint32_t baud, esp32::SerialFailure& failure)>;
using Pause = std::function<void(std::chrono::milliseconds duration)>;

struct FlashHooks {
    PortOpener open;
    Pause pause;
    std::stop_token stop;
    std::chrono::milliseconds reopenWindow{4000};
};

[[nodiscard]] Expected<FlashResult, FlashFailure> flashFirmware(const FlashRequest& request,
                                                              const FlashProgress& progress = {},
                                                              const FlashHooks& hooks = {});
[[nodiscard]] std::string_view failureText(FlashFailure failure) noexcept;

}
