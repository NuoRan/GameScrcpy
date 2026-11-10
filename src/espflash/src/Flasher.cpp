#include "gs/espflash/Flasher.h"

#include "gs/espflash/Firmware.h"
#include "gs/espflash/Loader.h"
#include "gs/espflash/Md5.h"

#include <gs/base/Log.h>

#include <miniz.h>

#include <algorithm>
#include <optional>
#include <span>
#include <thread>
#include <vector>

namespace gs::espflash {

namespace {

constexpr std::string_view kCategory = "espflash";
constexpr std::chrono::milliseconds kSyncTimeout{100};
constexpr std::chrono::milliseconds kResetHold{100};
constexpr std::chrono::milliseconds kStrapHold{50};
constexpr std::chrono::milliseconds kReopenStep{200};
constexpr std::chrono::milliseconds kBaudSettle{50};
constexpr std::chrono::milliseconds kBlockTimeout{10000};
constexpr std::chrono::milliseconds kMinimumLongTimeout{3000};
constexpr std::chrono::milliseconds kEraseTimeoutPerMegabyte{30000};
constexpr std::chrono::milliseconds kMd5TimeoutPerMegabyte{8000};
constexpr int kFirstSyncs = 2;
constexpr int kFirmwareSyncs = 10;
constexpr int kResetSyncs = 5;
constexpr int kResetAttempts = 2;
constexpr int kBaudChecks = 3;
constexpr std::size_t kSecurityInfoWithChip = 20;
constexpr std::size_t kSecurityInfoChipAt = 12;
constexpr std::uint32_t kDefaultFlashSize = 4U << 20U;

using Report = std::function<void(FlashStage stage, std::size_t done, std::size_t total)>;

void sleepFor(std::chrono::milliseconds duration)
{
    std::this_thread::sleep_for(duration); // source-rule-allow R9: the reset and boot lines must stay set this long
}

std::unique_ptr<esp32::Port> openDefault(std::string_view name, std::uint32_t baud, esp32::SerialFailure& failure)
{
    return esp32::openSerialPort(name, baud, failure);
}

std::chrono::milliseconds scaled(std::chrono::milliseconds perMegabyte, std::size_t bytes)
{
    const double milliseconds =
        static_cast<double>(perMegabyte.count()) * static_cast<double>(bytes) / (1024.0 * 1024.0);
    return std::max(std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(milliseconds)),
                    kMinimumLongTimeout);
}

std::uint32_t blocksOf(std::size_t size, std::uint32_t block) noexcept
{
    return static_cast<std::uint32_t>((size + block - 1) / block);
}

std::uint32_t roundedUp(std::size_t size, std::uint32_t block) noexcept
{
    return blocksOf(size, block) * block;
}

std::optional<std::vector<std::uint8_t>> deflateImage(std::span<const std::uint8_t> image)
{
    mz_ulong size = mz_compressBound(static_cast<mz_ulong>(image.size()));
    std::vector<std::uint8_t> out(size);
    if (mz_compress2(out.data(), &size, image.data(), static_cast<mz_ulong>(image.size()), MZ_BEST_COMPRESSION) !=
        MZ_OK) {
        return std::nullopt;
    }
    out.resize(size);
    return out;
}

void classicReset(esp32::Port& port, const Pause& pause)
{
    (void)port.setDtr(false);
    (void)port.setRts(true);
    pause(kResetHold);
    (void)port.setDtr(true);
    (void)port.setRts(false);
    pause(kStrapHold);
    (void)port.setDtr(false);
}

void usbJtagReset(esp32::Port& port, const Pause& pause)
{
    (void)port.setRts(false);
    (void)port.setDtr(false);
    pause(kResetHold);
    (void)port.setDtr(true);
    (void)port.setRts(false);
    pause(kResetHold);
    (void)port.setRts(true);
    (void)port.setDtr(false);
    (void)port.setRts(true);
    pause(kResetHold);
    (void)port.setDtr(false);
    (void)port.setRts(false);
}

void hardReset(esp32::Port& port, const Pause& pause)
{
    (void)port.setDtr(false);
    (void)port.setRts(true);
    pause(kResetHold);
    (void)port.setRts(false);
}

bool trySync(RomLoader& loader, esp32::Port& port, int attempts)
{
    port.discardInput();
    return loader.sync(attempts, kSyncTimeout);
}

bool enterLoader(RomLoader& loader, esp32::Port& port, const FlashRequest& request, const Pause& pause,
                 const std::stop_token& stop)
{
    const bool jtag =
        request.enter == EnterMode::UsbJtag || (request.enter != EnterMode::Classic && request.usbSerialJtag);
    const bool expected = request.enter == EnterMode::Firmware || request.enter == EnterMode::None;
    if (trySync(loader, port, expected ? kFirmwareSyncs : kFirstSyncs)) {
        return true;
    }
    for (int attempt = 0; attempt < kResetAttempts && !stop.stop_requested() && !port.gone(); ++attempt) {
        if (jtag) {
            usbJtagReset(port, pause);
        } else {
            classicReset(port, pause);
        }
        if (trySync(loader, port, kResetSyncs)) {
            return true;
        }
    }
    return false;
}

const ChipInfo* detectChip(RomLoader& loader)
{
    if (const std::optional<std::vector<std::uint8_t>> info = loader.securityInfo();
        info && info->size() >= kSecurityInfoWithChip) {
        const std::vector<std::uint8_t>& bytes = *info;
        const std::uint32_t id = static_cast<std::uint32_t>(bytes[kSecurityInfoChipAt]) |
                                 (static_cast<std::uint32_t>(bytes[kSecurityInfoChipAt + 1]) << 8U) |
                                 (static_cast<std::uint32_t>(bytes[kSecurityInfoChipAt + 2]) << 16U) |
                                 (static_cast<std::uint32_t>(bytes[kSecurityInfoChipAt + 3]) << 24U);
        if (const ChipInfo* chip = chipByImageId(id)) {
            return chip;
        }
    }
    if (const std::optional<std::uint32_t> magic = loader.readRegister(kChipMagicRegister)) {
        return chipByMagic(*magic);
    }
    return nullptr;
}

Expected<std::unique_ptr<esp32::Port>, FlashFailure> openPort(const PortOpener& open, const FlashRequest& request,
                                                              const FlashHooks& hooks, const Pause& pause)
{
    const auto window = request.enter == EnterMode::Firmware ? hooks.reopenWindow : std::chrono::milliseconds{0};
    const auto deadline = std::chrono::steady_clock::now() + window;
    esp32::SerialFailure failure = esp32::SerialFailure::None;
    for (;;) {
        if (std::unique_ptr<esp32::Port> port = open(request.port, rom::kBaud, failure)) {
            return port;
        }
        if (hooks.stop.stop_requested()) {
            return makeUnexpected(FlashFailure::Cancelled);
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        pause(kReopenStep);
    }
    return makeUnexpected(failure == esp32::SerialFailure::Busy ? FlashFailure::PortBusy : FlashFailure::PortMissing);
}

bool switchBaud(RomLoader& loader, esp32::Port& port, std::uint32_t baud, const Pause& pause)
{
    if (!loader.changeBaud(baud)) {
        return true;
    }
    if (port.setBaud(baud)) {
        pause(kBaudSettle);
        if (trySync(loader, port, kBaudChecks)) {
            log::info(kCategory, "download at {} baud", baud);
            return true;
        }
    }
    (void)port.setBaud(rom::kBaud);
    pause(kBaudSettle);
    return trySync(loader, port, kBaudChecks);
}

Expected<bool, FlashFailure> writeImage(RomLoader& loader, const ChipInfo& chip, std::span<const std::uint8_t> image,
                                        std::uint32_t offset, const Report& report, const std::stop_token& stop)
{
    if (const std::optional<std::vector<std::uint8_t>> compressed = deflateImage(image)) {
        const std::uint32_t packets = blocksOf(compressed->size(), rom::kBlockSize);
        const std::uint32_t eraseSize = roundedUp(image.size(), rom::kBlockSize);
        report(FlashStage::Erasing, 0, 1);
        const std::optional<Response> begun = loader.beginFlash(true, eraseSize, packets, rom::kBlockSize, offset,
                                                                chip.encryptedFlag,
                                                                scaled(kEraseTimeoutPerMegabyte, eraseSize));
        if (!begun) {
            return makeUnexpected(FlashFailure::Lost);
        }
        if (begun->ok()) {
            const std::span<const std::uint8_t> all(*compressed);
            for (std::uint32_t sequence = 0; sequence < packets; ++sequence) {
                if (stop.stop_requested()) {
                    return makeUnexpected(FlashFailure::Cancelled);
                }
                report(FlashStage::Writing, sequence, packets);
                const std::size_t at = static_cast<std::size_t>(sequence) * rom::kBlockSize;
                const std::size_t length = std::min<std::size_t>(rom::kBlockSize, all.size() - at);
                const std::optional<Response> written =
                    loader.flashBlock(true, all.subspan(at, length), sequence, kBlockTimeout);
                if (!written) {
                    return makeUnexpected(FlashFailure::Lost);
                }
                if (!written->ok()) {
                    return makeUnexpected(FlashFailure::WriteFailed);
                }
            }
            report(FlashStage::Writing, packets, packets);
            return true;
        }
        log::warning(kCategory, "the loader refused compressed writing (error {}); writing uncompressed", begun->error);
    }
    const std::uint32_t packets = blocksOf(image.size(), rom::kBlockSize);
    report(FlashStage::Erasing, 0, 1);
    const std::optional<Response> begun =
        loader.beginFlash(false, static_cast<std::uint32_t>(image.size()), packets, rom::kBlockSize, offset,
                          chip.encryptedFlag, scaled(kEraseTimeoutPerMegabyte, image.size()));
    if (!begun) {
        return makeUnexpected(FlashFailure::Lost);
    }
    if (!begun->ok()) {
        return makeUnexpected(FlashFailure::Refused);
    }
    std::vector<std::uint8_t> block(rom::kBlockSize);
    for (std::uint32_t sequence = 0; sequence < packets; ++sequence) {
        if (stop.stop_requested()) {
            return makeUnexpected(FlashFailure::Cancelled);
        }
        report(FlashStage::Writing, sequence, packets);
        const std::size_t at = static_cast<std::size_t>(sequence) * rom::kBlockSize;
        const std::size_t length = std::min<std::size_t>(rom::kBlockSize, image.size() - at);
        std::ranges::fill(block, std::uint8_t{0xFF});
        std::copy_n(image.begin() + static_cast<std::ptrdiff_t>(at), length, block.begin());
        const std::optional<Response> written = loader.flashBlock(false, block, sequence, kBlockTimeout);
        if (!written) {
            return makeUnexpected(FlashFailure::Lost);
        }
        if (!written->ok()) {
            return makeUnexpected(FlashFailure::WriteFailed);
        }
    }
    report(FlashStage::Writing, packets, packets);
    return false;
}

}

Expected<FlashResult, FlashFailure> flashFirmware(const FlashRequest& request, const FlashProgress& progress,
                                                  const FlashHooks& hooks)
{
    const Report report = [&progress](FlashStage stage, std::size_t done, std::size_t total) {
        if (progress) {
            progress(stage, done, total);
        }
    };
    const PortOpener open = hooks.open ? hooks.open : PortOpener(&openDefault);
    const Pause pause = hooks.pause ? hooks.pause : Pause(&sleepFor);

    report(FlashStage::Preparing, 0, 1);
    const Expected<FirmwareBundle, FirmwareProblem> bundle = loadFirmwareBundle(request.firmwareFolder);
    if (!bundle || bundle->images.empty()) {
        return makeUnexpected(FlashFailure::NoFirmware);
    }

    report(FlashStage::Connecting, 0, 1);
    Expected<std::unique_ptr<esp32::Port>, FlashFailure> opened = openPort(open, request, hooks, pause);
    if (!opened) {
        return makeUnexpected(opened.error());
    }
    esp32::Port& port = **opened;
    RomLoader loader(port);
    if (!enterLoader(loader, port, request, pause, hooks.stop)) {
        return makeUnexpected(hooks.stop.stop_requested() ? FlashFailure::Cancelled : FlashFailure::NoDownloadMode);
    }

    report(FlashStage::Detecting, 0, 1);
    const ChipInfo* chip = detectChip(loader);
    if (chip == nullptr) {
        return makeUnexpected(FlashFailure::UnknownChip);
    }
    log::info(kCategory, "{} is in download mode on {}", chip->name, request.port);
    const FirmwareImage* image = imageForChip(*bundle, chip->imageId);
    if (image == nullptr) {
        return makeUnexpected(FlashFailure::NoImageForChip);
    }
    const Expected<std::vector<std::uint8_t>, FirmwareProblem> data = readImage(*bundle, *image);
    if (!data) {
        return makeUnexpected(data.error() == FirmwareProblem::ImageMissing ? FlashFailure::ImageMissing
                                                                            : FlashFailure::ImageCorrupt);
    }
    if (!request.usbSerialJtag && request.fastBaud > rom::kBaud && !switchBaud(loader, port, request.fastBaud, pause)) {
        return makeUnexpected(FlashFailure::Lost);
    }
    if (!loader.attachFlash() ||
        !loader.setFlashParameters(flashSizeBytes(image->flashSize).value_or(kDefaultFlashSize))) {
        return makeUnexpected(FlashFailure::Refused);
    }

    const Expected<bool, FlashFailure> compressed = writeImage(loader, *chip, *data, image->offset, report, hooks.stop);
    if (!compressed) {
        return makeUnexpected(compressed.error());
    }

    report(FlashStage::Verifying, 0, 1);
    const std::optional<std::string> digest = loader.flashMd5(image->offset, static_cast<std::uint32_t>(data->size()),
                                                              scaled(kMd5TimeoutPerMegabyte, data->size()));
    if (!digest) {
        return makeUnexpected(FlashFailure::Lost);
    }
    if (*digest != md5Hex(*data)) {
        log::warning(kCategory, "flash MD5 {} does not match the image", *digest);
        return makeUnexpected(FlashFailure::VerifyFailed);
    }

    report(FlashStage::Restarting, 0, 1);
    if (chip->forceDownloadRegister != 0) {
        (void)loader.writeRegister(chip->forceDownloadRegister, 0, chip->forceDownloadMask);
    }
    loader.finishFlash(*compressed, true);
    hardReset(port, pause);
    report(FlashStage::Done, 1, 1);
    log::info(kCategory, "wrote firmware {} ({} bytes) to the {}", bundle->version, data->size(), chip->name);
    return FlashResult{.chip = chip->chip, .version = bundle->version, .bytes = data->size(), .compressed = *compressed};
}

std::string_view failureText(FlashFailure failure) noexcept
{
    switch (failure) {
    case FlashFailure::NoFirmware:
        return "No firmware is bundled with GameScrcpy.";
    case FlashFailure::NoImageForChip:
        return "The bundled firmware has no image for this chip.";
    case FlashFailure::ImageMissing:
        return "The firmware image file is missing.";
    case FlashFailure::ImageCorrupt:
        return "The firmware image file is damaged.";
    case FlashFailure::PortMissing:
        return "The serial port is not there.";
    case FlashFailure::PortBusy:
        return "Another program is using the serial port.";
    case FlashFailure::NoDownloadMode:
        return "Could not enter download mode: hold BOOT while plugging in the cable, then try again.";
    case FlashFailure::UnknownChip:
        return "The chip on the board is not one GameScrcpy knows.";
    case FlashFailure::Refused:
        return "The board refused a flashing command.";
    case FlashFailure::WriteFailed:
        return "Writing to the flash failed.";
    case FlashFailure::VerifyFailed:
        return "The written firmware does not match; flash it again.";
    case FlashFailure::Lost:
        return "The board stopped answering while flashing.";
    case FlashFailure::Cancelled:
        return "Flashing was cancelled.";
    }
    return "Flashing failed.";
}

}
