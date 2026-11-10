#include "TestSupport.h"

#include <gs/espflash/Firmware.h>
#include <gs/platform/Crypto.h>

#include <doctest/doctest.h>

#include <string>

using esp32test::Bytes;
using esp32test::TemporaryFolder;
using gs::espflash::compareVersions;
using gs::espflash::FirmwareProblem;

namespace {

std::string shaOf(const Bytes& bytes)
{
    return gs::platform::toHex(gs::platform::sha256(bytes));
}

}

TEST_CASE("firmware.json lists one image per target with its SHA-256, flash size and chip id; the chip picks the image")
{
    const Bytes s3{1, 2, 3, 4};
    const Bytes classic{5, 6, 7};
    const std::string json = R"({"format": "gamescrcpy.esp32-firmware", "version": "1.2.0", "targets": {
        "esp32s3": {"file": "gs-esp32-esp32s3.bin", "sha256": ")" + shaOf(s3) + R"(", "flashSize": "4MB", "chipId": 9},
        "esp32": {"file": "gs-esp32-esp32.bin", "sha256": ")" + shaOf(classic) + R"(", "flashSize": "4MB", "chipId": 0, "offset": 0}}})";
    const auto bundle = gs::espflash::parseFirmwareBundle(json, "C:/firmware");
    REQUIRE(bundle.has_value());
    CHECK(bundle->version == "1.2.0");
    REQUIRE(bundle->images.size() == 2);
    const gs::espflash::FirmwareImage* image = gs::espflash::imageForChip(*bundle, 9);
    REQUIRE(image != nullptr);
    CHECK(image->target == "esp32s3");
    CHECK(image->file == "gs-esp32-esp32s3.bin");
    CHECK(image->flashSize == "4MB");
    CHECK(image->offset == 0);
    CHECK(image->sha256 == gs::platform::sha256(s3));
    REQUIRE(gs::espflash::imageForChip(*bundle, 0) != nullptr);
    CHECK(gs::espflash::imageForChip(*bundle, 0)->target == "esp32");
    CHECK(gs::espflash::imageForChip(*bundle, 5) == nullptr);
}

TEST_CASE("a damaged firmware.json is refused as a whole")
{
    const std::string sha(64, 'a');
    const auto refused = [](const std::string& json) {
        const auto bundle = gs::espflash::parseFirmwareBundle(json, "C:/firmware");
        return !bundle.has_value() && bundle.error() == FirmwareProblem::Malformed;
    };
    CHECK(refused("not json"));
    CHECK(refused(R"({"version": "1.0.0", "targets": {}})"));
    CHECK(refused(R"({"format": "gamescrcpy.esp32-firmware", "targets": {}})"));
    CHECK(refused(R"({"format": "gamescrcpy.esp32-firmware", "version": "1.0.0", "targets": []})"));
    CHECK(refused(R"({"format": "gamescrcpy.esp32-firmware", "version": "1.0.0", "targets": {"esp32": {"file": "../x.bin", "sha256": ")" +
                  sha + R"(", "flashSize": "4MB", "chipId": 0}}})"));
    CHECK(refused(R"({"format": "gamescrcpy.esp32-firmware", "version": "1.0.0", "targets": {"esp32": {"file": "x.bin", "sha256": "abc", "flashSize": "4MB", "chipId": 0}}})"));
    CHECK(refused(R"({"format": "gamescrcpy.esp32-firmware", "version": "1.0.0", "targets": {"esp32": {"file": "x.bin", "sha256": ")" +
                  sha + R"(", "flashSize": "3MB", "chipId": 0}}})"));
    CHECK(refused(R"({"format": "gamescrcpy.esp32-firmware", "version": "1.0.0", "targets": {"esp32": {"file": "x.bin", "sha256": ")" +
                  sha + R"(", "flashSize": "4MB", "chipId": -1}}})"));
    const auto empty = gs::espflash::parseFirmwareBundle(
        R"({"format": "gamescrcpy.esp32-firmware", "version": "1.0.0", "targets": {}})", "C:/firmware");
    REQUIRE(empty.has_value());
    CHECK(empty->images.empty());
}

TEST_CASE("the release folder in the repository holds a firmware.json the program can read")
{
    const auto bundle = gs::espflash::loadFirmwareBundle(GS_ESP32_RELEASE_DIR);
    REQUIRE(bundle.has_value());
    CHECK_FALSE(bundle->version.empty());
    for (const gs::espflash::FirmwareImage& image : bundle->images) {
        INFO(image.target);
        CHECK(gs::espflash::readImage(*bundle, image).has_value());
    }
}

TEST_CASE("reading an image checks its SHA-256")
{
    const Bytes image{0xE9, 0x03, 0x02, 0x20, 0x10};
    TemporaryFolder folder;
    folder.write("good.bin", image);
    folder.write("firmware.json", R"({"format": "gamescrcpy.esp32-firmware", "version": "1.0.0", "targets": {
        "esp32s3": {"file": "good.bin", "sha256": ")" + shaOf(image) + R"(", "flashSize": "4MB", "chipId": 9},
        "esp32c3": {"file": "missing.bin", "sha256": ")" + shaOf(image) + R"(", "flashSize": "4MB", "chipId": 5}}})");
    const auto bundle = gs::espflash::loadFirmwareBundle(folder.path());
    REQUIRE(bundle.has_value());
    const auto good = gs::espflash::readImage(*bundle, *gs::espflash::imageForChip(*bundle, 9));
    REQUIRE(good.has_value());
    CHECK(*good == image);
    const auto missing = gs::espflash::readImage(*bundle, *gs::espflash::imageForChip(*bundle, 5));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error() == FirmwareProblem::ImageMissing);
    folder.write("good.bin", Bytes{0xE9, 0x03, 0x02, 0x20, 0x11});
    const auto changed = gs::espflash::readImage(*bundle, *gs::espflash::imageForChip(*bundle, 9));
    REQUIRE_FALSE(changed.has_value());
    CHECK(changed.error() == FirmwareProblem::ImageCorrupt);

    TemporaryFolder none;
    const auto absent = gs::espflash::loadFirmwareBundle(none.path());
    REQUIRE_FALSE(absent.has_value());
    CHECK(absent.error() == FirmwareProblem::Missing);
}

TEST_CASE("firmware versions compare by number, a pre-release before its release")
{
    CHECK(compareVersions("1.0.0", "1.0.0") == 0);
    CHECK(compareVersions("1.0.0", "1.0.1") < 0);
    CHECK(compareVersions("1.10.0", "1.9.9") > 0);
    CHECK(compareVersions("2.0", "1.99.99") > 0);
    CHECK(compareVersions("1.0.0-rc.1", "1.0.0") < 0);
    CHECK(compareVersions("1.0.0", "1.0.0-rc.1") > 0);
    CHECK(compareVersions("1.0.0-rc.1", "1.0.0-rc.2") < 0);
    CHECK(compareVersions("v1.2.3", "1.2.3") == 0);
}

TEST_CASE("flash sizes are written as 2MB to 128MB")
{
    CHECK(gs::espflash::flashSizeBytes("4MB") == std::optional<std::uint32_t>{4U << 20U});
    CHECK(gs::espflash::flashSizeBytes("16MB") == std::optional<std::uint32_t>{16U << 20U});
    CHECK_FALSE(gs::espflash::flashSizeBytes("4mb").has_value());
    CHECK_FALSE(gs::espflash::flashSizeBytes("keep").has_value());
}
