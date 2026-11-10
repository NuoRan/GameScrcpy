#include <gs/capture/CropDetector.h>

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

using gs::capture::CropBox;
using gs::capture::CropDetector;
using gs::capture::LumaPlane;
using gs::capture::PixelBox;
using Clock = CropDetector::Clock;
using namespace std::chrono_literals;

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 180;
constexpr std::uint8_t kBlack = 16;

class Picture
{
public:
    Picture(int width = kWidth, int height = kHeight)
        : m_width(width), m_height(height), m_pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), kBlack)
    {
    }

    Picture& content(int left, int top, int width, int height)
    {
        for (int y = top; y < top + height; ++y) {
            for (int x = left; x < left + width; ++x) {
                at(x, y) = static_cast<std::uint8_t>(40 + (x * 7 + y * 13) % 180);
            }
        }
        return *this;
    }

    Picture& paint(int left, int top, int width, int height, std::uint8_t value)
    {
        for (int y = top; y < top + height; ++y) {
            for (int x = left; x < left + width; ++x) {
                at(x, y) = value;
            }
        }
        return *this;
    }

    std::uint8_t& at(int x, int y) { return m_pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(x)]; }

    [[nodiscard]] LumaPlane plane() const { return {m_pixels.data(), m_width, m_height, m_width}; }

private:
    int m_width;
    int m_height;
    std::vector<std::uint8_t> m_pixels;
};

CropBox boxOf(int left, int top, int width, int height, int frameWidth = kWidth, int frameHeight = kHeight)
{
    return {.x = static_cast<double>(left) / frameWidth,
            .y = static_cast<double>(top) / frameHeight,
            .width = static_cast<double>(width) / frameWidth,
            .height = static_cast<double>(height) / frameHeight};
}

bool sameBox(const std::optional<CropBox>& found, const CropBox& wanted)
{
    return found && found->closeTo(wanted, 1e-9);
}

constexpr int kPortraitLeft = 118;
constexpr int kPortraitWidth = 84;
constexpr int kLandscapeTop = 16;
constexpr int kLandscapeHeight = 148;

Picture portrait()
{
    return Picture().content(kPortraitLeft, 0, kPortraitWidth, kHeight);
}

Picture landscape()
{
    return Picture().content(0, kLandscapeTop, kWidth, kLandscapeHeight);
}

struct Feeder {
    CropDetector detector;
    Clock::time_point now = Clock::time_point{} + 100s;

    bool feed(const Picture& picture, Clock::duration step = 500ms)
    {
        const bool changed = detector.feed(picture.plane(), now);
        now += step;
        return changed;
    }

    int feedFor(const Picture& picture, Clock::duration span, Clock::duration step = 500ms)
    {
        int changes = 0;
        for (Clock::duration done{}; done < span; done += step) {
            changes += feed(picture, step) ? 1 : 0;
        }
        return changes;
    }
};

}

TEST_CASE("a phone held upright shows with black bars left and right, and its screen is found after a second (MED-014)")
{
    Feeder feeder;
    const Picture picture = portrait();
    CHECK_FALSE(feeder.feed(picture));
    CHECK_FALSE(feeder.detector.box());
    CHECK_FALSE(feeder.feed(picture));
    CHECK_FALSE(feeder.detector.box());
    CHECK(feeder.feed(picture));
    CHECK(sameBox(feeder.detector.box(), boxOf(kPortraitLeft, 0, kPortraitWidth, kHeight)));
    CHECK(feeder.feedFor(picture, 5s) == 0);
    CHECK(sameBox(feeder.detector.box(), boxOf(kPortraitLeft, 0, kPortraitWidth, kHeight)));
    CHECK(gs::capture::rotationOf(*feeder.detector.box(), kWidth, kHeight, false) == 0);
    CHECK(gs::capture::rotationOf(*feeder.detector.box(), kWidth, kHeight, true) == 0);
}

TEST_CASE("a phone held sideways in a wider frame shows with bars above and below, and is found too (MED-014)")
{
    Feeder feeder;
    CHECK(feeder.feedFor(landscape(), 1500ms) == 1);
    REQUIRE(feeder.detector.box());
    CHECK(sameBox(feeder.detector.box(), boxOf(0, kLandscapeTop, kWidth, kLandscapeHeight)));
    CHECK(gs::capture::landscapeIn(*feeder.detector.box(), kWidth, kHeight));
    CHECK(gs::capture::rotationOf(*feeder.detector.box(), kWidth, kHeight, false) == 1);
    CHECK(gs::capture::rotationOf(*feeder.detector.box(), kWidth, kHeight, true) == 3);
}

TEST_CASE("a dark scene inside the phone screen does not make the box smaller (MED-014)")
{
    Feeder feeder;
    feeder.feedFor(portrait(), 1500ms);
    REQUIRE(sameBox(feeder.detector.box(), boxOf(kPortraitLeft, 0, kPortraitWidth, kHeight)));

    const Picture dark = Picture().paint(kPortraitLeft + 30, 70, 20, 30, 200);
    CHECK(feeder.feedFor(dark, 10s) == 0);
    CHECK(sameBox(feeder.detector.box(), boxOf(kPortraitLeft, 0, kPortraitWidth, kHeight)));

    const Picture black;
    CHECK(feeder.feedFor(black, 5s) == 0);
    CHECK(sameBox(feeder.detector.box(), boxOf(kPortraitLeft, 0, kPortraitWidth, kHeight)));
}

TEST_CASE("a game that starts dark is found once its screen has lit up its whole box within the last seconds")
{
    Feeder feeder;
    CHECK(feeder.feedFor(Picture().content(kPortraitLeft, 0, kPortraitWidth, 90), 1s) == 0);
    CHECK_FALSE(feeder.detector.box());
    CHECK(feeder.feedFor(Picture().content(kPortraitLeft, 90, kPortraitWidth, 90), 1500ms) == 1);
    CHECK(sameBox(feeder.detector.box(), boxOf(kPortraitLeft, 0, kPortraitWidth, kHeight)));
}

TEST_CASE("turning the phone moves the box after it has stayed turned for a second, both ways (MED-014)")
{
    Feeder feeder;
    feeder.feedFor(portrait(), 1500ms);
    REQUIRE(sameBox(feeder.detector.box(), boxOf(kPortraitLeft, 0, kPortraitWidth, kHeight)));

    const Picture sideways = landscape();
    CHECK_FALSE(feeder.feed(sideways));
    CHECK_FALSE(feeder.feed(sideways));
    CHECK(sameBox(feeder.detector.box(), boxOf(kPortraitLeft, 0, kPortraitWidth, kHeight)));
    CHECK(feeder.feed(sideways));
    CHECK(sameBox(feeder.detector.box(), boxOf(0, kLandscapeTop, kWidth, kLandscapeHeight)));

    const Picture upright = portrait();
    CHECK_FALSE(feeder.feed(upright));
    CHECK_FALSE(feeder.feed(upright));
    CHECK(feeder.feed(upright));
    CHECK(sameBox(feeder.detector.box(), boxOf(kPortraitLeft, 0, kPortraitWidth, kHeight)));
    CHECK(feeder.feedFor(upright, 6s) == 0);
}

TEST_CASE("a turn that lasts less than a second leaves the box where it was")
{
    Feeder feeder;
    feeder.feedFor(portrait(), 1500ms);
    CHECK_FALSE(feeder.feed(landscape()));
    CHECK_FALSE(feeder.feed(landscape()));
    CHECK_FALSE(feeder.feed(portrait()));
    CHECK(feeder.feedFor(portrait(), 3s) == 0);
    CHECK(sameBox(feeder.detector.box(), boxOf(kPortraitLeft, 0, kPortraitWidth, kHeight)));
}

TEST_CASE("a short turn while sideways does not widen the box to the whole frame")
{
    Feeder feeder;
    feeder.feedFor(landscape(), 1500ms);
    REQUIRE(sameBox(feeder.detector.box(), boxOf(0, kLandscapeTop, kWidth, kLandscapeHeight)));
    CHECK_FALSE(feeder.feed(portrait()));
    CHECK(feeder.feedFor(landscape(), 6s) == 0);
    CHECK(sameBox(feeder.detector.box(), boxOf(0, kLandscapeTop, kWidth, kLandscapeHeight)));
}

TEST_CASE("a turn within the first second takes the new way, not the whole frame")
{
    Feeder feeder;
    CHECK_FALSE(feeder.feed(portrait()));
    CHECK(feeder.feedFor(landscape(), 1500ms) == 1);
    CHECK(sameBox(feeder.detector.box(), boxOf(0, kLandscapeTop, kWidth, kLandscapeHeight)));
}

TEST_CASE("noise in the black bars, even lone bright dots, does not move the box (MED-014)")
{
    Picture noisy = portrait();
    std::uint32_t seed = 2026;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (x >= kPortraitLeft && x < kPortraitLeft + kPortraitWidth) {
                continue;
            }
            seed = seed * 1664525U + 1013904223U;
            noisy.at(x, y) = static_cast<std::uint8_t>(4 + (seed >> 24) % 26);
            if (x % 7 == 3 && y % 5 == 2) {
                noisy.at(x, y) = 255;
            }
        }
    }
    Feeder feeder;
    CHECK(feeder.feedFor(noisy, 1500ms) == 1);
    CHECK(sameBox(feeder.detector.box(), boxOf(kPortraitLeft, 0, kPortraitWidth, kHeight)));
}

TEST_CASE("a picture that fills the frame is taken whole, and pictures that are no phone screen are not taken")
{
    Feeder whole;
    CHECK(whole.feedFor(Picture().content(0, 0, kWidth, kHeight), 1500ms) == 1);
    REQUIRE(whole.detector.box());
    CHECK(whole.detector.box()->whole());

    Feeder square;
    CHECK(square.feedFor(Picture().content(70, 0, 180, kHeight), 3s) == 0);
    CHECK_FALSE(square.detector.box());

    Feeder small;
    CHECK(small.feedFor(Picture().content(100, 40, 40, 90), 3s) == 0);
    CHECK_FALSE(small.detector.box());

    Feeder black;
    CHECK(black.feedFor(Picture(), 3s) == 0);
    CHECK_FALSE(black.detector.box());
}

TEST_CASE("a new picture size starts over and says the box changed")
{
    Feeder feeder;
    feeder.feedFor(portrait(), 1500ms);
    REQUIRE(feeder.detector.box());
    const Picture smaller = Picture(160, 90).content(59, 0, 42, 90);
    CHECK(feeder.feed(smaller));
    CHECK_FALSE(feeder.detector.box());
    CHECK(feeder.feedFor(smaller, 1500ms) == 1);
    CHECK(sameBox(feeder.detector.box(), boxOf(59, 0, 42, 90, 160, 90)));
    feeder.detector.reset();
    CHECK_FALSE(feeder.detector.box());
}

TEST_CASE("the row and column peaks ignore single bright pixels and find the content's edges")
{
    Picture picture(8, 6);
    picture.paint(2, 1, 4, 3, 100);
    picture.at(7, 5) = 255;
    std::vector<std::uint8_t> rows(6);
    std::vector<std::uint8_t> columns(8);
    gs::capture::peaksOf(picture.plane(), rows, columns);
    CHECK(rows == std::vector<std::uint8_t>{kBlack, 100, 100, 100, kBlack, kBlack});
    CHECK(columns == std::vector<std::uint8_t>{kBlack, kBlack, 100, 100, 100, 100, kBlack, kBlack});
    const std::optional<CropBox> box = gs::capture::contentBox(rows, columns, 32);
    REQUIRE(box);
    CHECK(sameBox(box, boxOf(2, 1, 4, 3, 8, 6)));
    CHECK_FALSE(gs::capture::contentBox(std::vector<std::uint8_t>(6, kBlack), columns, 32));
}

TEST_CASE("the box in pixels grows to even edges inside the frame")
{
    CHECK(gs::capture::pixelBoxOf(boxOf(kPortraitLeft, 0, kPortraitWidth, kHeight), kWidth, kHeight) ==
          PixelBox{kPortraitLeft, 0, kPortraitWidth, kHeight});
    CHECK(gs::capture::pixelBoxOf(boxOf(119, 3, 81, 170), kWidth, kHeight) == PixelBox{118, 2, 82, 172});
    CHECK(gs::capture::pixelBoxOf(CropBox{}, kWidth, kHeight) == PixelBox{0, 0, kWidth, kHeight});
    CHECK(gs::capture::pixelBoxOf(CropBox{}, 321, 181) == PixelBox{0, 0, 320, 180});
    CHECK(gs::capture::pixelBoxOf(CropBox{.x = 0.999, .y = 0.999, .width = 0.0, .height = 0.0}, kWidth, kHeight) ==
          PixelBox{318, 178, 2, 2});
    CHECK(gs::capture::pixelBoxOf(CropBox{}, 1, 1) == PixelBox{0, 0, 1, 1});
}
