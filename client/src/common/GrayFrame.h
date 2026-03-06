#ifndef GRAYFRAME_H
#define GRAYFRAME_H

#include <cstdint>
#include <functional>
#include <vector>

/**
 * @brief 灰度帧数据 / Grayscale Frame Data
 *
 * 轻量级帧结构，持有灰度像素数据 (owning)。
 * 用于图像匹配管线，避免 QImage→cv::Mat 的无谓深拷贝。
 *
 * Lightweight frame structure owning grayscale pixel data.
 * Used in the image matching pipeline to avoid unnecessary
 * QImage → cv::Mat deep copies.
 */
struct GrayFrame
{
    std::vector<uint8_t> data;   ///< Y 分量 / Grayscale pixels (row-major, no padding)
    int width  = 0;              ///< 帧宽 / Frame width in pixels
    int height = 0;              ///< 帧高 / Frame height in pixels

    bool isValid() const { return !data.empty() && width > 0 && height > 0; }
};

/// 灰度帧抓取回调 / Grayscale frame grab callback
using GrayFrameGrabCallback = std::function<GrayFrame()>;

#endif // GRAYFRAME_H
