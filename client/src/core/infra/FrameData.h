#ifndef CORE_FRAMEDATA_H
#define CORE_FRAMEDATA_H

#include <cstdint>
#include <atomic>

namespace qsc {
namespace core {

/**
 * @brief 视频帧数据结构 / Video Frame Data Structure
 *
 * 用于在解码器和渲染器之间传递帧数据，支持零拷贝。
 * Transfers frame data between decoder and renderer with zero-copy support.
 * 内存由 FramePool 管理，使用完毕后调用 release() 归还。
 * Memory managed by FramePool; call release() when done.
 */
struct FrameData {
    // YUV420P 数据指针 / YUV420P data pointers
    uint8_t* dataY = nullptr;
    uint8_t* dataU = nullptr;
    uint8_t* dataV = nullptr;

    // 每个平面的行字节数 / Bytes per row for each plane
    int linesizeY = 0;
    int linesizeU = 0;
    int linesizeV = 0;

    // 帧尺寸 / Frame dimensions
    int width = 0;
    int height = 0;

    // 时间戳 (微秒) / Timestamp (microseconds)
    int64_t pts = 0;

    // 帧序号 (用于调试) / Frame index (for debugging)
    uint64_t frameIndex = 0;

    // 引用计数 (由 FramePool 管理) / Ref count (managed by FramePool)
    std::atomic<int> refCount{0};

    // 所属帧池索引 (用于归还) / Pool index for returning
    int poolIndex = -1;

    // 获取 Y 平面大小 / Get Y plane size
    int yPlaneSize() const { return linesizeY * height; }

    // 获取 U/V 平面大小 / Get U/V plane size
    int uvPlaneSize() const { return linesizeU * (height / 2); }

    // 是否有效 / Check validity
    bool isValid() const {
        return dataY != nullptr && width > 0 && height > 0;
    }

    // 重置（不释放内存）/ Reset (does not free memory)
    void reset() {
        pts = 0;
        frameIndex = 0;
        // 不重置数据指针和尺寸，这些由 FramePool 管理
    }
};

} // namespace core
} // namespace qsc

#endif // CORE_FRAMEDATA_H
