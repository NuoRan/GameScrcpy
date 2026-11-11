#include <gs/capture/FrameBuilder.h>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace gs;

namespace {

std::vector<std::uint8_t> filled(int width, int height, std::array<std::uint8_t, 4> color)
{
    std::vector<std::uint8_t> pixels;
    for (int at = 0; at < width * height; ++at) {
        pixels.insert(pixels.end(), color.begin(), color.end());
    }
    return pixels;
}

}

TEST_CASE("ADR-0112: a BGRA picture from an extension becomes BT.709 limited-range NV12")
{
    struct Case {
        std::array<std::uint8_t, 4> bgra;
        int luma;
        int blue;
        int red;
    };
    const Case cases[] = {
        {{255, 255, 255, 255}, 235, 128, 128},
        {{0, 0, 0, 255}, 16, 128, 128},
        {{0, 0, 255, 255}, 63, 102, 240},
        {{255, 0, 0, 255}, 32, 240, 118},
        {{0, 255, 0, 255}, 172, 42, 26},
    };
    for (const Case& each : cases) {
        CAPTURE(each.luma);
        const std::vector<std::uint8_t> pixels = filled(4, 2, each.bgra);
        std::vector<std::uint8_t> luma(8, 0);
        std::vector<std::uint8_t> chroma(4, 0);
        capture::bgraToNv12(pixels.data(), 16, 4, 2, luma.data(), 4, chroma.data(), 4);
        for (const std::uint8_t value : luma) {
            CHECK(value == each.luma);
        }
        CHECK(chroma == std::vector<std::uint8_t>{static_cast<std::uint8_t>(each.blue),
                                                  static_cast<std::uint8_t>(each.red),
                                                  static_cast<std::uint8_t>(each.blue),
                                                  static_cast<std::uint8_t>(each.red)});
    }
}

TEST_CASE("ADR-0112: an odd-sized BGRA picture keeps its last column and row, and chroma averages 2 x 2 pixels")
{
    const std::vector<std::uint8_t> white = filled(3, 3, {255, 255, 255, 255});
    std::vector<std::uint8_t> luma(std::size_t{4} * 3, 0);
    std::vector<std::uint8_t> chroma(std::size_t{4} * 2, 0);
    capture::bgraToNv12(white.data(), 12, 3, 3, luma.data(), 4, chroma.data(), 4);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            CHECK(luma[static_cast<std::size_t>(y * 4 + x)] == 235);
        }
    }
    CHECK(chroma == std::vector<std::uint8_t>{128, 128, 128, 128, 128, 128, 128, 128});

    std::vector<std::uint8_t> split = filled(2, 2, {255, 255, 255, 255});
    split[4] = split[5] = split[6] = 0;
    split[12] = split[13] = split[14] = 0;
    std::vector<std::uint8_t> half(4, 0);
    std::vector<std::uint8_t> mixed(2, 0);
    capture::bgraToNv12(split.data(), 8, 2, 2, half.data(), 2, mixed.data(), 2);
    CHECK(half == std::vector<std::uint8_t>{235, 16, 235, 16});
    CHECK(mixed == std::vector<std::uint8_t>{128, 128});
}
