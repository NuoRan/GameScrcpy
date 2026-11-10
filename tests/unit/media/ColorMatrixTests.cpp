#include "gs/media/ColorMatrix.h"

extern "C" {
#include <libavutil/frame.h>
}

#include <doctest/doctest.h>

#include <array>

using gs::media::ColorMatrix;
using gs::media::yuvToRgb;

namespace {

ColorMatrix matrixFor(AVColorSpace space, AVColorRange range, int height)
{
    AVFrame* frame = av_frame_alloc();
    frame->colorspace = space;
    frame->color_range = range;
    frame->height = height;
    const ColorMatrix matrix = yuvToRgb(*frame);
    av_frame_free(&frame);
    return matrix;
}

std::array<float, 3> apply(const ColorMatrix& matrix, int y, int cb, int cr)
{
    std::array<float, 3> rgb{};
    for (std::size_t i = 0; i < 3; ++i) {
        const auto& row = matrix.rows[i];
        rgb[i] = row[0] * static_cast<float>(y) / 255.0F + row[1] * static_cast<float>(cb) / 255.0F +
                 row[2] * static_cast<float>(cr) / 255.0F + row[3];
    }
    return rgb;
}

void checkRgb(const std::array<float, 3>& rgb, float r, float g, float b)
{
    CHECK(rgb[0] == doctest::Approx(r).epsilon(0.01));
    CHECK(rgb[1] == doctest::Approx(g).epsilon(0.01));
    CHECK(rgb[2] == doctest::Approx(b).epsilon(0.01));
}

}

TEST_CASE("color matrix: limited range black, white and BT.601 primaries")
{
    const ColorMatrix bt601 = matrixFor(AVCOL_SPC_SMPTE170M, AVCOL_RANGE_MPEG, 640);
    checkRgb(apply(bt601, 16, 128, 128), 0.0F, 0.0F, 0.0F);
    checkRgb(apply(bt601, 235, 128, 128), 1.0F, 1.0F, 1.0F);
    checkRgb(apply(bt601, 81, 90, 240), 1.0F, 0.0F, 0.0F);
    checkRgb(apply(bt601, 145, 54, 34), 0.0F, 1.0F, 0.0F);
    checkRgb(apply(bt601, 41, 240, 110), 0.0F, 0.0F, 1.0F);
}

TEST_CASE("color matrix: BT.709 primaries and full range")
{
    const ColorMatrix bt709 = matrixFor(AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, 1080);
    checkRgb(apply(bt709, 63, 102, 240), 1.0F, 0.0F, 0.0F);
    const ColorMatrix full = matrixFor(AVCOL_SPC_BT709, AVCOL_RANGE_JPEG, 1080);
    checkRgb(apply(full, 0, 128, 128), 0.0F, 0.0F, 0.0F);
    checkRgb(apply(full, 255, 128, 128), 1.0F, 1.0F, 1.0F);
}

TEST_CASE("color matrix: unstated matrix follows the picture size")
{
    const ColorMatrix hd = matrixFor(AVCOL_SPC_UNSPECIFIED, AVCOL_RANGE_UNSPECIFIED, 1080);
    const ColorMatrix sd = matrixFor(AVCOL_SPC_UNSPECIFIED, AVCOL_RANGE_UNSPECIFIED, 640);
    CHECK(hd.rows == matrixFor(AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, 1080).rows);
    CHECK(sd.rows == matrixFor(AVCOL_SPC_SMPTE170M, AVCOL_RANGE_MPEG, 640).rows);
}
