#pragma once

#include <gs/vision/Image.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace gs::vision::testing {

inline Image readPgm(std::string_view name)
{
    std::ifstream file(std::filesystem::path(GS_VISION_TEST_DATA_DIR) / name, std::ios::binary);
    REQUIRE_MESSAGE(file, name);
    std::string magic;
    int width = 0;
    int height = 0;
    int maximum = 0;
    file >> magic >> width >> height >> maximum;
    REQUIRE(magic == "P5");
    REQUIRE(maximum == 255);
    REQUIRE(width > 0);
    REQUIRE(height > 0);
    file.get();
    Image image(width, height);
    const std::streamsize size = static_cast<std::streamsize>(width) * height;
    file.read(reinterpret_cast<char*>(image.data()), size);
    REQUIRE(file.gcount() == size);
    return image;
}

class Dice
{
public:
    explicit Dice(std::uint32_t seed)
        : m_engine(seed)
    {
    }

    int below(int bound) { return static_cast<int>(m_engine() % static_cast<std::uint32_t>(bound)); }

private:
    std::mt19937 m_engine;
};

inline Image scene(int width, int height, std::uint32_t seed)
{
    constexpr int kCell = 8;
    Dice dice(seed);
    const int columns = width / kCell + 2;
    const int rows = height / kCell + 2;
    std::vector<int> grid(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows));
    for (int& value : grid) {
        value = dice.below(256);
    }
    const auto knot = [&](int x, int y) { return grid[static_cast<std::size_t>(y * columns + x)]; };
    Image image(width, height);
    for (int y = 0; y < height; ++y) {
        const int gy = y / kCell;
        const int fy = y % kCell;
        for (int x = 0; x < width; ++x) {
            const int gx = x / kCell;
            const int fx = x % kCell;
            const int upper = knot(gx, gy) * (kCell - fx) + knot(gx + 1, gy) * fx;
            const int lower = knot(gx, gy + 1) * (kCell - fx) + knot(gx + 1, gy + 1) * fx;
            const int smooth = (upper * (kCell - fy) + lower * fy) / (kCell * kCell);
            image.at(x, y) = static_cast<std::uint8_t>(std::clamp(smooth + dice.below(33) - 16, 0, 255));
        }
    }
    const int blocks = width * height / 4096;
    for (int block = 0; block < blocks; ++block) {
        const int w = 4 + dice.below(28);
        const int h = 4 + dice.below(28);
        const int left = dice.below(std::max(width - w, 1));
        const int top = dice.below(std::max(height - h, 1));
        const auto shade = static_cast<std::uint8_t>(dice.below(256));
        for (int y = top; y < std::min(top + h, height); ++y) {
            for (int x = left; x < std::min(left + w, width); ++x) {
                image.at(x, y) = shade;
            }
        }
    }
    return image;
}

inline Image crop(const Image& source, const Rect& area)
{
    return Image::copyOf(source.view().crop(area));
}

inline void paste(Image& target, const Image& pixels, int left, int top, const Image* mask = nullptr)
{
    for (int y = 0; y < pixels.height(); ++y) {
        for (int x = 0; x < pixels.width(); ++x) {
            if (mask == nullptr || mask->at(x, y) != 0) {
                target.at(left + x, top + y) = pixels.at(x, y);
            }
        }
    }
}

inline Image screen1080()
{
    const Image h265Top = readPgm("frame_h265_top.pgm");
    const Image av1Top = readPgm("frame_av1_top.pgm");
    const Image h264Scrolled = readPgm("frame_h264_scrolled.pgm");
    const Image av1Bottom = readPgm("frame_av1_bottom.pgm");
    const Image landscape = readPgm("frame_av1_landscape.pgm");
    const Image rotating = readPgm("frame_h264_rotating.pgm");
    const auto sideBySide = [&landscape](const Image& left, int leftRow, const Image& right, int rightRow) {
        Image slot(640, 288);
        for (int y = 0; y < 288; ++y) {
            std::copy_n(left.row(leftRow + y), 288, slot.row(y));
            std::copy_n(right.row(rightRow + y), 288, slot.row(y) + 288);
            std::copy_n(landscape.row(y) + 576, 64, slot.row(y) + 576);
        }
        return slot;
    };
    const std::vector<Image> slots{sideBySide(h265Top, 0, av1Top, 352), sideBySide(h264Scrolled, 0, av1Bottom, 352),
                                   landscape, sideBySide(av1Top, 0, h265Top, 352), rotating,
                                   sideBySide(av1Bottom, 0, h264Scrolled, 352)};
    Image screen(1920, 1080);
    for (int index = 0; index < 12; ++index) {
        const Image& slot = slots[static_cast<std::size_t>(index % 6)];
        const bool flipped = index >= 6;
        const int top = index / 3 * 288;
        const int left = index % 3 * 640;
        for (int y = 0; y < 288 && top + y < 1080; ++y) {
            std::copy_n(slot.row(flipped ? 287 - y : y), 640, screen.row(top + y) + left);
        }
    }
    return screen;
}

inline Image disc(int width, int height)
{
    Image mask(width, height);
    const double cx = (width - 1) / 2.0;
    const double cy = (height - 1) / 2.0;
    const double rx = width / 2.0;
    const double ry = height / 2.0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double dx = (x - cx) / rx;
            const double dy = (y - cy) / ry;
            mask.at(x, y) = static_cast<std::uint8_t>(dx * dx + dy * dy <= 1.0 ? 255 : 0);
        }
    }
    return mask;
}

}
