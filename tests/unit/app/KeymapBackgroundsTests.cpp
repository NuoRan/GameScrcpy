#include "KeymapBackgrounds.h"

#include "unit/ext/TestFiles.h"

#include <doctest/doctest.h>

#include <QString>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using gs::app::BackgroundProblem;
using gs::app::backgroundStem;
using gs::app::KeymapBackgrounds;
using gs::app::kMostBackgroundBytes;
using gs::app::PictureInfo;
using gs::app::pictureInfoOf;
using gs::app::PictureKind;
using gs::ext::test::Scratch;
namespace fs = std::filesystem;

namespace {

std::uint8_t high(std::uint16_t value)
{
    return static_cast<std::uint8_t>(value >> 8U);
}

std::uint8_t low(std::uint16_t value)
{
    return static_cast<std::uint8_t>(value & 0xFFU);
}

std::vector<std::uint8_t> pngOf(std::uint32_t width, std::uint32_t height)
{
    std::vector<std::uint8_t> bytes{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 'I', 'H', 'D', 'R'};
    for (const std::uint32_t value : {width, height}) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            bytes.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }
    bytes.insert(bytes.end(), {0x08, 0x06, 0x00, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78});
    return bytes;
}

std::vector<std::uint8_t> jpegOf(std::uint16_t width, std::uint16_t height, std::uint8_t frame = 0xC0)
{
    std::vector<std::uint8_t> bytes{0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J',  'F',  'I',  'F',
                                    0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00};
    bytes.insert(bytes.end(), {0xFF, 0xC4, 0x00, 0x04, 0x00, 0x00});
    bytes.insert(bytes.end(), {0xFF, frame, 0x00, 0x11, 0x08, high(height), low(height), high(width), low(width), 0x03,
                               0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01});
    bytes.insert(bytes.end(), {0xFF, 0xD9});
    return bytes;
}

std::span<const std::uint8_t> bytesOf(std::string_view text)
{
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

void writeBytes(const fs::path& file, const std::vector<std::uint8_t>& bytes)
{
    gs::ext::test::writeFile(file, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

}

TEST_CASE("a background picture's kind and size come from its header alone (HID-008)")
{
    CHECK(pictureInfoOf(pngOf(1080, 2400)) == PictureInfo{.kind = PictureKind::Png, .width = 1080, .height = 2400});
    CHECK(pictureInfoOf(jpegOf(2400, 1080)) == PictureInfo{.kind = PictureKind::Jpeg, .width = 2400, .height = 1080});
    CHECK(pictureInfoOf(jpegOf(640, 480, 0xC2)) == PictureInfo{.kind = PictureKind::Jpeg, .width = 640, .height = 480});
    CHECK_FALSE(pictureInfoOf(jpegOf(640, 480, 0xC4)).has_value());
    CHECK_FALSE(pictureInfoOf(pngOf(0, 2400)).has_value());

    std::vector<std::uint8_t> cut = pngOf(1080, 2400);
    cut.resize(20);
    CHECK_FALSE(pictureInfoOf(cut).has_value());
    std::vector<std::uint8_t> cutJpeg = jpegOf(1080, 2400);
    cutJpeg.resize(30);
    CHECK_FALSE(pictureInfoOf(cutJpeg).has_value());
    CHECK_FALSE(pictureInfoOf(bytesOf("plain text, not a picture")).has_value());
    CHECK_FALSE(pictureInfoOf({}).has_value());
}

TEST_CASE("the file name of a background keeps letters, digits, dots, dashes and underscores (HID-008)")
{
    CHECK(backgroundStem(u"R58N123ABC") == QStringLiteral("R58N123ABC"));
    CHECK(backgroundStem(u"esp32-COM5") == QStringLiteral("esp32-COM5"));
    CHECK(backgroundStem(u"a b/c\\d:e*") == QStringLiteral("a_b_c_d_e_"));
    CHECK(backgroundStem(u"") == QStringLiteral("phone"));
    CHECK(backgroundStem(QString(200, u'x')).size() == 96);
}

TEST_CASE("a background is kept for each phone and orientation and found again later (HID-008)")
{
    const Scratch scratch;
    const fs::path folder = scratch / "backgrounds";
    const KeymapBackgrounds backgrounds(folder);
    CHECK_FALSE(backgrounds.find(u"phone-1", false).has_value());

    const auto portrait = backgrounds.store(u"phone-1", pngOf(1080, 2400));
    REQUIRE(portrait.has_value());
    CHECK_FALSE(portrait->landscape);
    CHECK(portrait->width == 1080);
    CHECK(portrait->height == 2400);
    CHECK(portrait->path == folder / L"phone-1-portrait.png");
    CHECK(fs::file_size(portrait->path) == pngOf(1080, 2400).size());

    const auto landscape = backgrounds.store(u"phone-1", jpegOf(2400, 1080));
    REQUIRE(landscape.has_value());
    CHECK(landscape->landscape);
    CHECK(landscape->path == folder / L"phone-1-landscape.jpg");

    const KeymapBackgrounds again(folder);
    CHECK(again.find(u"phone-1", false) == folder / L"phone-1-portrait.png");
    CHECK(again.find(u"phone-1", true) == folder / L"phone-1-landscape.jpg");
    CHECK_FALSE(again.find(u"phone-2", false).has_value());

    REQUIRE(backgrounds.store(u"phone-1", jpegOf(1080, 2340)).has_value());
    CHECK(again.find(u"phone-1", false) == folder / L"phone-1-portrait.jpg");
    CHECK_FALSE(fs::exists(folder / L"phone-1-portrait.png"));

    CHECK(backgrounds.clear(u"phone-1", false));
    CHECK_FALSE(backgrounds.find(u"phone-1", false).has_value());
    CHECK(backgrounds.find(u"phone-1", true).has_value());
    CHECK_FALSE(backgrounds.clear(u"phone-1", false));
}

TEST_CASE("a background that is too large, no picture or missing is refused, and a file on disk is taken (HID-008)")
{
    const Scratch scratch;
    const KeymapBackgrounds backgrounds(scratch / "backgrounds");

    std::vector<std::uint8_t> huge = pngOf(1080, 2400);
    huge.resize(kMostBackgroundBytes + 1);
    const auto tooLarge = backgrounds.store(u"phone", huge);
    REQUIRE_FALSE(tooLarge.has_value());
    CHECK(tooLarge.error() == BackgroundProblem::TooLarge);

    const auto notPicture = backgrounds.store(u"phone", bytesOf("plain text"));
    REQUIRE_FALSE(notPicture.has_value());
    CHECK(notPicture.error() == BackgroundProblem::NotPicture);

    const auto missing = backgrounds.importFile(u"phone", scratch / "absent.png");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error() == BackgroundProblem::Missing);

    writeBytes(scratch / "huge.png", huge);
    const auto hugeFile = backgrounds.importFile(u"phone", scratch / "huge.png");
    REQUIRE_FALSE(hugeFile.has_value());
    CHECK(hugeFile.error() == BackgroundProblem::TooLarge);

    writeBytes(scratch / "shot.jpg", jpegOf(1080, 2400));
    const auto imported = backgrounds.importFile(u"phone", scratch / "shot.jpg");
    REQUIRE(imported.has_value());
    CHECK_FALSE(imported->landscape);
    CHECK(imported->width == 1080);
    CHECK(imported->height == 2400);
    CHECK(backgrounds.find(u"phone", false) == imported->path);
    CHECK_FALSE(backgrounds.find(u"phone", true).has_value());
}
