#include <gs/capture/CaptureFormat.h>

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using gs::capture::CaptureFormat;
using gs::capture::Subtype;

namespace {

CaptureFormat mode(int width, int height, std::uint32_t numerator, std::uint32_t denominator, Subtype subtype)
{
    return {.width = width, .height = height, .rateNumerator = numerator, .rateDenominator = denominator,
            .subtype = subtype};
}

CaptureFormat mode(int width, int height, std::uint32_t rate, Subtype subtype)
{
    return mode(width, height, rate, 1, subtype);
}

}

TEST_CASE("a USB 3 card gives raw 1080p at 60 frames per second, which wins over MJPEG and over 4K at 30 (MED-014)")
{
    const std::vector<CaptureFormat> usb3{
        mode(3840, 2160, 30, Subtype::Nv12), mode(1920, 1080, 30, Subtype::Yuy2), mode(1920, 1080, 60, Subtype::Mjpg),
        mode(1280, 720, 60, Subtype::Nv12), mode(1920, 1080, 60000, 1001, Subtype::Nv12),
        mode(1920, 1080, 60, Subtype::Yuy2), mode(2560, 1440, 60, Subtype::Mjpg)};
    const std::optional<CaptureFormat> chosen = gs::capture::chooseFormat(usb3);
    REQUIRE(chosen);
    CHECK(chosen->width == 1920);
    CHECK(chosen->height == 1080);
    CHECK(chosen->subtype == Subtype::Nv12);
    CHECK(chosen->framesPerSecond() == doctest::Approx(59.94).epsilon(0.001));
}

TEST_CASE("a USB 2 card gives MJPEG at 60, which wins over raw pictures at 30 or smaller raw pictures (MED-014)")
{
    const std::vector<CaptureFormat> usb2{
        mode(1920, 1080, 5, Subtype::Yuy2), mode(1280, 720, 10, Subtype::Yuy2), mode(640, 480, 30, Subtype::Yuy2),
        mode(1920, 1080, 30, Subtype::Mjpg), mode(1920, 1080, 60, Subtype::Mjpg), mode(1280, 720, 60, Subtype::Mjpg),
        mode(1920, 1080, 60, Subtype::H264)};
    const std::optional<CaptureFormat> chosen = gs::capture::chooseFormat(usb2);
    REQUIRE(chosen);
    CHECK(*chosen == mode(1920, 1080, 60, Subtype::Mjpg));

    const std::vector<CaptureFormat> slow{mode(1920, 1080, 30, Subtype::Mjpg), mode(1920, 1080, 30, Subtype::Yuy2),
                                          mode(3840, 2160, 30, Subtype::Nv12), mode(1280, 720, 25, Subtype::Nv12)};
    const std::optional<CaptureFormat> thirty = gs::capture::chooseFormat(slow);
    REQUIRE(thirty);
    CHECK(*thirty == mode(1920, 1080, 30, Subtype::Yuy2));
}

TEST_CASE("among smooth formats the chooser keeps 1080p or less, the largest of those, and the lower rate on a tie")
{
    CHECK(*gs::capture::chooseFormat(std::vector{mode(3840, 2160, 60, Subtype::Nv12), mode(2560, 1440, 60, Subtype::Nv12)}) ==
          mode(2560, 1440, 60, Subtype::Nv12));
    CHECK(*gs::capture::chooseFormat(std::vector{mode(1280, 720, 60, Subtype::Nv12), mode(1920, 1080, 60, Subtype::H264)}) ==
          mode(1920, 1080, 60, Subtype::H264));
    CHECK(*gs::capture::chooseFormat(std::vector{mode(1920, 1080, 120, Subtype::Nv12), mode(1920, 1080, 60, Subtype::Nv12)}) ==
          mode(1920, 1080, 60, Subtype::Nv12));
    CHECK(*gs::capture::chooseFormat(std::vector{mode(1080, 1920, 60, Subtype::Yuy2), mode(1440, 2560, 60, Subtype::Nv12)}) ==
          mode(1080, 1920, 60, Subtype::Yuy2));
    CHECK(*gs::capture::chooseFormat(std::vector{mode(1920, 1080, 50, Subtype::Nv12), mode(1920, 1080, 30, Subtype::Nv12)}) ==
          mode(1920, 1080, 50, Subtype::Nv12));
}

TEST_CASE("formats the app cannot show are never chosen")
{
    CHECK_FALSE(gs::capture::chooseFormat(std::vector<CaptureFormat>{}));
    CHECK_FALSE(gs::capture::chooseFormat(std::vector{mode(1920, 1080, 60, Subtype::Other)}));
    CHECK_FALSE(gs::capture::chooseFormat(std::vector{mode(0, 1080, 60, Subtype::Nv12), mode(1920, 1080, 0, Subtype::Nv12)}));
    CHECK_FALSE(gs::capture::chooseFormat(std::vector{mode(1920, 1080, 60, 0, Subtype::Nv12)}));
    CHECK(*gs::capture::chooseFormat(std::vector{mode(1920, 1080, 60, Subtype::Other), mode(640, 480, 30, Subtype::Yuy2)}) ==
          mode(640, 480, 30, Subtype::Yuy2));
}

TEST_CASE("a format picked by hand is used while the card offers it, otherwise the automatic choice (MED-014)")
{
    const std::vector<CaptureFormat> offered{mode(1920, 1080, 60, Subtype::Nv12), mode(1280, 720, 60, Subtype::Yuy2),
                                             mode(1920, 1080, 30000, 1001, Subtype::Mjpg)};
    CHECK(*gs::capture::chooseFormat(offered, "1280x720@60/YUY2") == mode(1280, 720, 60, Subtype::Yuy2));
    CHECK(*gs::capture::chooseFormat(offered, "1280x720@60/yuy2") == mode(1280, 720, 60, Subtype::Yuy2));
    CHECK(*gs::capture::chooseFormat(offered, "1920x1080@29.97/MJPG") == mode(1920, 1080, 30000, 1001, Subtype::Mjpg));
    CHECK(*gs::capture::chooseFormat(offered, "640x480@30/YUY2") == mode(1920, 1080, 60, Subtype::Nv12));
    CHECK(*gs::capture::chooseFormat(offered, "1280x720@30/YUY2") == mode(1920, 1080, 60, Subtype::Nv12));
    CHECK(*gs::capture::chooseFormat(offered, "not a format") == mode(1920, 1080, 60, Subtype::Nv12));
    CHECK(*gs::capture::chooseFormat(offered, "") == mode(1920, 1080, 60, Subtype::Nv12));
}

TEST_CASE("format names are written and read the same way, and wrong names are refused")
{
    CHECK(gs::capture::formatName(mode(1920, 1080, 60000, 1001, Subtype::Nv12)) == "1920x1080@59.94/NV12");
    CHECK(gs::capture::formatName(mode(1280, 720, 60, Subtype::Yuy2)) == "1280x720@60/YUY2");
    CHECK(gs::capture::formatName(mode(1920, 1080, 30000, 1001, Subtype::Mjpg)) == "1920x1080@29.97/MJPG");
    CHECK(gs::capture::formatName(mode(1920, 1080, 25, Subtype::H264)) == "1920x1080@25/H264");

    const std::optional<CaptureFormat> parsed = gs::capture::parseFormatName("1920x1080@59.94/NV12");
    REQUIRE(parsed);
    CHECK(parsed->width == 1920);
    CHECK(parsed->height == 1080);
    CHECK(parsed->subtype == Subtype::Nv12);
    CHECK(gs::capture::sameMode(*parsed, mode(1920, 1080, 60000, 1001, Subtype::Nv12)));
    CHECK_FALSE(gs::capture::sameMode(*parsed, mode(1920, 1080, 60, Subtype::Nv12)));
    CHECK_FALSE(gs::capture::sameMode(*parsed, mode(1920, 1080, 60000, 1001, Subtype::Yuy2)));

    for (const char* wrong : {"1920x1080/NV12", "1920x1080@60", "x@/", "1920x1080@0/NV12", "1920x1080@60/RGB",
                              "0x1080@60/NV12", "1920x-5@60/NV12", "1920x1080@60/NV12 ", "1920@60x1080/NV12",
                              "1920x1080@5000/NV12"}) {
        CAPTURE(std::string(wrong));
        CHECK_FALSE(gs::capture::parseFormatName(wrong));
    }

    CHECK(gs::capture::subtypeOfName("mjpg") == Subtype::Mjpg);
    CHECK_FALSE(gs::capture::subtypeOfName("RGB32"));
    CHECK(gs::capture::subtypeName(Subtype::Other) == "OTHER");
    CHECK(mode(1920, 1080, 60, 0, Subtype::Nv12).framesPerSecond() == 0.0);
}
