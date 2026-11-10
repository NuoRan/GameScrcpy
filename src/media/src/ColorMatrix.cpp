#include "gs/media/ColorMatrix.h"

extern "C" {
#include <libavutil/frame.h>
}

namespace gs::media {

ColorMatrix yuvToRgb(const AVFrame& frame) noexcept
{
    double kr = 0.299;
    double kb = 0.114;
    switch (frame.colorspace) {
    case AVCOL_SPC_BT709:
        kr = 0.2126;
        kb = 0.0722;
        break;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        kr = 0.2627;
        kb = 0.0593;
        break;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
        break;
    default:
        if (frame.height >= 720) {
            kr = 0.2126;
            kb = 0.0722;
        }
        break;
    }
    const double kg = 1.0 - kr - kb;
    const bool full = frame.color_range == AVCOL_RANGE_JPEG;
    const double lumaScale = full ? 1.0 : 255.0 / 219.0;
    const double lumaOffset = full ? 0.0 : 16.0 / 255.0;
    const double chromaScale = full ? 1.0 : 255.0 / 224.0;
    const double chromaOffset = 128.0 / 255.0;

    const double crToR = 2.0 * (1.0 - kr) * chromaScale;
    const double cbToB = 2.0 * (1.0 - kb) * chromaScale;
    const double cbToG = -2.0 * kb * (1.0 - kb) / kg * chromaScale;
    const double crToG = -2.0 * kr * (1.0 - kr) / kg * chromaScale;
    const double luma = -lumaOffset * lumaScale;

    ColorMatrix matrix;
    const auto row = [&](double y, double cb, double cr) {
        return std::array<float, 4>{static_cast<float>(y), static_cast<float>(cb), static_cast<float>(cr),
                                    static_cast<float>(luma - (cb + cr) * chromaOffset)};
    };
    matrix.rows[0] = row(lumaScale, 0.0, crToR);
    matrix.rows[1] = row(lumaScale, cbToG, crToG);
    matrix.rows[2] = row(lumaScale, cbToB, 0.0);
    return matrix;
}

}
