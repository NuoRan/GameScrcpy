#include "FakeRom.h"
#include "TestSupport.h"

#include <gs/espflash/Firmware.h>
#include <gs/espflash/Flasher.h>
#include <gs/espflash/Loader.h>
#include <gs/espflash/Slip.h>
#include <gs/platform/Crypto.h>

#include <doctest/doctest.h>

#include <string>
#include <tuple>
#include <vector>

using esp32test::Bytes;
using esp32test::FakeRom;
using esp32test::PortProxy;
using esp32test::TemporaryFolder;
using gs::esp32::PortKind;
using gs::esp32::SerialFailure;
using gs::espflash::Chip;
using gs::espflash::EnterMode;
using gs::espflash::FlashFailure;
using gs::espflash::FlashHooks;
using gs::espflash::FlashRequest;
using gs::espflash::FlashStage;
namespace rom = gs::espflash::rom;

namespace {

struct Entry {
    std::string target;
    std::string file;
    Bytes image;
    int chipId = 0;
};

Bytes makeImage(std::size_t size, std::uint32_t seed)
{
    Bytes image(size);
    std::uint32_t state = seed;
    for (std::size_t i = 0; i < size; ++i) {
        state = state * 1103515245U + 12345U;
        image[i] = i % 64 < 40 ? static_cast<std::uint8_t>(i / 64) : static_cast<std::uint8_t>(state >> 24U);
    }
    image[0] = 0xE9;
    return image;
}

std::string manifestFor(const std::vector<Entry>& entries, const std::string& version = "1.0.0")
{
    std::string targets;
    for (const Entry& entry : entries) {
        if (!targets.empty()) {
            targets += ", ";
        }
        targets += "\"" + entry.target + "\": {\"file\": \"" + entry.file + "\", \"sha256\": \"" +
                   gs::platform::toHex(gs::platform::sha256(entry.image)) + "\", \"flashSize\": \"4MB\", \"chipId\": " +
                   std::to_string(entry.chipId) + "}";
    }
    return "{\"format\": \"gamescrcpy.esp32-firmware\", \"version\": \"" + version + "\", \"targets\": {" + targets +
           "}}";
}

struct Bench {
    explicit Bench(FakeRom::Options options)
        : rom(options)
    {
        hooks.open = [this](std::string_view name, std::uint32_t baud, SerialFailure& failure) {
            opened.emplace_back(name);
            bauds.push_back(baud);
            if (failuresLeft > 0) {
                --failuresLeft;
                failure = failureKind;
                return std::unique_ptr<gs::esp32::Port>{};
            }
            failure = SerialFailure::None;
            return std::unique_ptr<gs::esp32::Port>(std::make_unique<PortProxy>(rom));
        };
        hooks.pause = [this](std::chrono::milliseconds duration) { pauses.push_back(duration); };
    }

    gs::Expected<gs::espflash::FlashResult, FlashFailure> flash(const FlashRequest& request)
    {
        return gs::espflash::flashFirmware(
            request,
            [this](FlashStage stage, std::size_t, std::size_t) {
                if (stages.empty() || stages.back() != stage) {
                    stages.push_back(stage);
                }
            },
            hooks);
    }

    FakeRom rom;
    FlashHooks hooks;
    std::vector<std::string> opened;
    std::vector<std::uint32_t> bauds;
    std::vector<std::chrono::milliseconds> pauses;
    std::vector<FlashStage> stages;
    int failuresLeft = 0;
    SerialFailure failureKind = SerialFailure::NotFound;
};

std::uint32_t roundedUp(std::size_t size)
{
    return static_cast<std::uint32_t>((size + rom::kBlockSize - 1) / rom::kBlockSize * rom::kBlockSize);
}

}

TEST_CASE("the flasher writes an ESP32-S3 image compressed, checks it with MD5 and restarts the board (HID-004)")
{
    const Bytes image = makeImage(70000, 1);
    TemporaryFolder folder;
    folder.write("gs-esp32-esp32s3.bin", image);
    folder.write("firmware.json", manifestFor({{"esp32s3", "gs-esp32-esp32s3.bin", image, 9}}));
    Bench bench({.chip = Chip::Esp32S3});

    const auto result =
        bench.flash({.port = "COM7", .kind = PortKind::Ch34x, .firmwareFolder = folder.path()});
    REQUIRE(result.has_value());
    CHECK(result->chip == Chip::Esp32S3);
    CHECK(result->compressed);
    CHECK(result->version == "1.0.0");
    CHECK(result->bytes == image.size());
    CHECK(bench.rom.flash(0, image.size()) == image);
    CHECK(bench.rom.flash(image.size(), 64) == Bytes(64, 0xFF));

    const auto begin = bench.rom.begin();
    REQUIRE(begin.has_value());
    CHECK(begin->compressed);
    REQUIRE(begin->words.size() == 5);
    CHECK(begin->words[0] == roundedUp(image.size()));
    CHECK(begin->words[2] == rom::kBlockSize);
    CHECK(begin->words[3] == 0);
    CHECK(begin->words[4] == 0);
    CHECK(bench.rom.count(rom::kFlashDeflData) == begin->words[1]);
    CHECK(bench.rom.count(rom::kChangeBaudrate) == 1);
    CHECK(bench.rom.romBaud() == 460800);
    CHECK(bench.rom.count(rom::kSpiAttach) == 1);
    CHECK(bench.rom.flashSizeSet() == (4U << 20U));
    CHECK(bench.rom.count(rom::kSpiFlashMd5) == 1);
    const auto writes = bench.rom.registerWrites();
    REQUIRE(writes.size() == 1);
    CHECK(writes[0].address == 0x6000812C);
    CHECK(writes[0].value == 0);
    CHECK(writes[0].mask == 1);
    CHECK(bench.rom.endFlag() == std::optional<std::uint32_t>{0});
    const auto lines = bench.rom.lines();
    REQUIRE(lines.size() >= 2);
    CHECK(lines[lines.size() - 2].rts);
    CHECK_FALSE(lines.back().rts);
    CHECK(bench.opened == std::vector<std::string>{"COM7"});
    CHECK(bench.bauds == std::vector<std::uint32_t>{rom::kBaud});
    CHECK(bench.stages.front() == FlashStage::Preparing);
    CHECK(bench.stages.back() == FlashStage::Done);
    CHECK(std::ranges::find(bench.stages, FlashStage::Writing) != bench.stages.end());
    CHECK(std::ranges::find(bench.stages, FlashStage::Verifying) != bench.stages.end());
}

TEST_CASE("a classic ESP32 is found by its magic register and gets the four-word begin of its ROM")
{
    const Bytes image = makeImage(40000, 2);
    TemporaryFolder folder;
    folder.write("gs-esp32-esp32.bin", image);
    folder.write("gs-esp32-esp32s3.bin", makeImage(1000, 3));
    folder.write("firmware.json", manifestFor({{"esp32", "gs-esp32-esp32.bin", image, 0},
                                               {"esp32s3", "gs-esp32-esp32s3.bin", makeImage(1000, 3), 9}}));
    Bench bench({.chip = Chip::Esp32});

    const auto result =
        bench.flash({.port = "COM3", .kind = PortKind::Cp210x, .firmwareFolder = folder.path()});
    REQUIRE(result.has_value());
    CHECK(result->chip == Chip::Esp32);
    CHECK(bench.rom.flash(0, image.size()) == image);
    const auto begin = bench.rom.begin();
    REQUIRE(begin.has_value());
    CHECK(begin->words.size() == 4);
    CHECK(bench.rom.registerWrites().empty());
    CHECK(bench.rom.count(rom::kGetSecurityInfo) == 1);
    CHECK(bench.rom.count(rom::kReadRegister) >= 1);
}

TEST_CASE("over the chip's own USB serial port the flasher keeps the baud and uses the USB reset")
{
    const Bytes image = makeImage(20000, 4);
    TemporaryFolder folder;
    folder.write("gs-esp32-esp32c3.bin", image);
    folder.write("firmware.json", manifestFor({{"esp32c3", "gs-esp32-esp32c3.bin", image, 5}}));
    Bench bench({.chip = Chip::Esp32C3});

    const auto result = bench.flash(
        {.port = "COM9", .kind = PortKind::EspressifUsb, .usbSerialJtag = true, .firmwareFolder = folder.path()});
    REQUIRE(result.has_value());
    CHECK(result->chip == Chip::Esp32C3);
    CHECK(bench.rom.count(rom::kChangeBaudrate) == 0);
    CHECK(bench.rom.flash(0, image.size()) == image);
}

TEST_CASE("a board not yet in download mode is put there with the DTR and RTS reset")
{
    const Bytes image = makeImage(5000, 5);
    TemporaryFolder folder;
    folder.write("gs-esp32-esp32s3.bin", image);
    folder.write("firmware.json", manifestFor({{"esp32s3", "gs-esp32-esp32s3.bin", image, 9}}));
    Bench bench({.chip = Chip::Esp32S3, .inLoader = false, .enterOnClassicReset = true});

    const auto result =
        bench.flash({.port = "COM4", .kind = PortKind::Ch34x, .firmwareFolder = folder.path()});
    REQUIRE(result.has_value());
    const auto lines = bench.rom.lines();
    const auto reset = std::ranges::find_if(lines, [](const esp32test::LineChange& line) { return !line.dtr && line.rts; });
    REQUIRE(reset != lines.end());
    const auto strap = std::find_if(reset, lines.end(), [](const esp32test::LineChange& line) { return line.dtr && !line.rts; });
    CHECK(strap != lines.end());
    CHECK_FALSE(bench.pauses.empty());
}

TEST_CASE("no answer to SYNC ends with the hint to hold BOOT while plugging in (HID-004)")
{
    const Bytes image = makeImage(5000, 6);
    TemporaryFolder folder;
    folder.write("gs-esp32-esp32s3.bin", image);
    folder.write("firmware.json", manifestFor({{"esp32s3", "gs-esp32-esp32s3.bin", image, 9}}));
    Bench bench({.silent = true});

    const auto result =
        bench.flash({.port = "COM4", .kind = PortKind::Ch34x, .firmwareFolder = folder.path()});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == FlashFailure::NoDownloadMode);
    CHECK(gs::espflash::failureText(result.error()).find("BOOT") != std::string_view::npos);
    CHECK_FALSE(bench.rom.lines().empty());
    CHECK(bench.stages.back() == FlashStage::Connecting);
}

TEST_CASE("an MD5 that does not match the image fails the flash")
{
    const Bytes image = makeImage(9000, 7);
    TemporaryFolder folder;
    folder.write("gs-esp32-esp32s3.bin", image);
    folder.write("firmware.json", manifestFor({{"esp32s3", "gs-esp32-esp32s3.bin", image, 9}}));
    Bench bench({.corruptFlash = true});

    const auto result =
        bench.flash({.port = "COM4", .kind = PortKind::Ch34x, .firmwareFolder = folder.path()});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == FlashFailure::VerifyFailed);
    CHECK(bench.rom.endFlag() == std::nullopt);
}

TEST_CASE("when the ROM refuses compressed writing the flasher writes plain blocks padded with 0xFF")
{
    const Bytes image = makeImage(3000, 8);
    TemporaryFolder folder;
    folder.write("gs-esp32-esp32s3.bin", image);
    folder.write("firmware.json", manifestFor({{"esp32s3", "gs-esp32-esp32s3.bin", image, 9}}));
    Bench bench({.refuseDeflate = true});

    const auto result =
        bench.flash({.port = "COM4", .kind = PortKind::Ch34x, .firmwareFolder = folder.path()});
    REQUIRE(result.has_value());
    CHECK_FALSE(result->compressed);
    const auto begin = bench.rom.begin();
    REQUIRE(begin.has_value());
    CHECK_FALSE(begin->compressed);
    CHECK(begin->words[0] == image.size());
    CHECK(begin->words[1] == 3);
    CHECK(bench.rom.count(rom::kFlashData) == 3);
    CHECK(bench.rom.flash(0, image.size()) == image);
    CHECK(bench.rom.flash(image.size(), 3 * rom::kBlockSize - image.size()) ==
          Bytes(3 * rom::kBlockSize - image.size(), 0xFF));
}

TEST_CASE("the image's SHA-256 is checked before anything is written")
{
    const Bytes image = makeImage(3000, 9);
    TemporaryFolder folder;
    folder.write("gs-esp32-esp32s3.bin", image);
    folder.write("firmware.json", manifestFor({{"esp32s3", "gs-esp32-esp32s3.bin", makeImage(3000, 10), 9}}));
    Bench bench({});

    const auto result =
        bench.flash({.port = "COM4", .kind = PortKind::Ch34x, .firmwareFolder = folder.path()});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == FlashFailure::ImageCorrupt);
    CHECK(bench.rom.count(rom::kFlashDeflBegin) == 0);
    CHECK(bench.rom.count(rom::kFlashBegin) == 0);
}

TEST_CASE("a chip without a bundled image, no bundled firmware and a busy port each fail with their own reason")
{
    const Bytes image = makeImage(3000, 11);
    TemporaryFolder folder;
    folder.write("gs-esp32-esp32s3.bin", image);
    folder.write("firmware.json", manifestFor({{"esp32s3", "gs-esp32-esp32s3.bin", image, 9}}));
    {
        Bench bench({.chip = Chip::Esp32C6});
        const auto result = bench.flash({.port = "COM4", .kind = PortKind::Ch34x, .firmwareFolder = folder.path()});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == FlashFailure::NoImageForChip);
    }
    {
        TemporaryFolder empty;
        empty.write("firmware.json", manifestFor({}));
        Bench bench({});
        const auto result = bench.flash({.port = "COM4", .kind = PortKind::Ch34x, .firmwareFolder = empty.path()});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == FlashFailure::NoFirmware);
        CHECK(bench.opened.empty());
    }
    {
        Bench bench({});
        bench.failuresLeft = 1;
        bench.failureKind = SerialFailure::Busy;
        const auto result = bench.flash({.port = "COM4", .kind = PortKind::Ch34x, .firmwareFolder = folder.path()});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == FlashFailure::PortBusy);
    }
}

TEST_CASE("after ENTER_DOWNLOAD the flasher waits for the port to come back")
{
    const Bytes image = makeImage(3000, 12);
    TemporaryFolder folder;
    folder.write("gs-esp32-esp32s3.bin", image);
    folder.write("firmware.json", manifestFor({{"esp32s3", "gs-esp32-esp32s3.bin", image, 9}}));
    Bench bench({});
    bench.failuresLeft = 2;

    const auto result = bench.flash(
        {.port = "COM5", .kind = PortKind::EspressifUsb, .usbSerialJtag = true, .enter = EnterMode::Firmware,
         .firmwareFolder = folder.path()});
    REQUIRE(result.has_value());
    CHECK(bench.opened.size() == 3);
    CHECK(std::ranges::count(bench.pauses, std::chrono::milliseconds{200}) == 2);
    CHECK(bench.rom.flash(0, image.size()) == image);
}

TEST_CASE("ROM loader packets: SLIP escaping, the 0xEF checksum and four status bytes")
{
    const Bytes raw{0x01, 0xC0, 0x02, 0xDB, 0x03};
    const Bytes framed = gs::espflash::slipEncode(raw);
    CHECK(framed == Bytes{0xC0, 0x01, 0xDB, 0xDC, 0x02, 0xDB, 0xDD, 0x03, 0xC0});
    gs::espflash::SlipDecoder decoder;
    const Bytes noise{'b', 'o', 'o', 't', '\r', '\n'};
    decoder.feed(noise);
    decoder.feed(std::span<const std::uint8_t>(framed).first(4));
    CHECK_FALSE(decoder.next().has_value());
    decoder.feed(std::span<const std::uint8_t>(framed).subspan(4));
    CHECK(decoder.next() == std::optional<Bytes>{raw});
    CHECK_FALSE(decoder.next().has_value());

    CHECK(gs::espflash::dataChecksum({}) == 0xEF);
    const Bytes data{0xEF, 0x01};
    CHECK(gs::espflash::dataChecksum(data) == 0x01);
    CHECK(gs::espflash::commandPacket(rom::kReadRegister, Bytes{0x00, 0x10, 0x00, 0x40}, 0) ==
          Bytes{0x00, 0x0A, 0x04, 0x00, 0, 0, 0, 0, 0x00, 0x10, 0x00, 0x40});
    CHECK(gs::espflash::syncPayload().size() == 36);
    CHECK(gs::espflash::syncPayload()[35] == 0x55);

    const Bytes response{0x01, 0x0A, 0x04, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const auto parsed = gs::espflash::parseResponse(response);
    REQUIRE(parsed.has_value());
    CHECK(parsed->command == rom::kReadRegister);
    CHECK(parsed->value == 9);
    CHECK(parsed->ok());
    CHECK(parsed->data.empty());
    const Bytes refused{0x01, 0x14, 0x04, 0x00, 0, 0, 0, 0, 0x01, 0x05, 0x00, 0x00};
    const auto failed = gs::espflash::parseResponse(refused);
    REQUIRE(failed.has_value());
    CHECK_FALSE(failed->ok());
    CHECK(failed->error == 0x05);
    CHECK_FALSE(gs::espflash::parseResponse(Bytes{0x00, 0x0A, 0x00, 0x00, 0, 0, 0, 0}).has_value());
    CHECK_FALSE(gs::espflash::parseResponse(Bytes{0x01, 0x0A, 0x08, 0x00, 0, 0, 0, 0, 0}).has_value());
}

TEST_CASE("MD5 of the image matches the known digest")
{
    CHECK(gs::espflash::md5Hex({}) == "d41d8cd98f00b204e9800998ecf8427e");
    const Bytes abc{'a', 'b', 'c'};
    CHECK(gs::espflash::md5Hex(abc) == "900150983cd24fb0d6963f7d28e17f72");
}

TEST_CASE("chips are known by image id and by the ROM's magic register")
{
    REQUIRE(gs::espflash::chipByImageId(9) != nullptr);
    CHECK(gs::espflash::chipByImageId(9)->chip == Chip::Esp32S3);
    CHECK(gs::espflash::chipByImageId(18)->target == "esp32p4");
    CHECK(gs::espflash::chipByImageId(77) == nullptr);
    CHECK(gs::espflash::chipByMagic(0x00F01D83)->chip == Chip::Esp32);
    CHECK(gs::espflash::chipByMagic(0x000007C6)->chip == Chip::Esp32S2);
    CHECK(gs::espflash::chipByMagic(0x1B31506F)->chip == Chip::Esp32C3);
    CHECK(gs::espflash::chipByMagic(0x2CE0806F)->chip == Chip::Esp32C6);
    CHECK(gs::espflash::chipByMagic(0x12345678) == nullptr);
    CHECK_FALSE(gs::espflash::chipInfo(Chip::Esp32).encryptedFlag);
    CHECK(gs::espflash::chipInfo(Chip::Esp32S3).encryptedFlag);
    CHECK(gs::espflash::chips().size() == 6);
}
