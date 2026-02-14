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
    uint8_t* dataUV = nullptr;  // [低延迟 Step4] NV12 UV 交织平面指针

    // 每个平面的行字节数 / Bytes per row for each plane
    int linesizeY = 0;
    int linesizeU = 0;
    int linesizeV = 0;
    int linesizeUV = 0;  // [低延迟 Step4] NV12 UV stride

    // 帧尺寸 / Frame dimensions
    int width = 0;
    int height = 0;

    // 时间戳 (微秒) / Timestamp (microseconds)
    int64_t pts = 0;

    // 帧序号 (用于调试) / Frame index (for debugging)
    uint64_t frameIndex = 0;

    // [低延迟 Step4] 是否为 NV12 格式（硬解直通，跳过 CPU 去交织）
    bool isNV12 = false;

    // ========================================================
    // GPU 直通渲染 / GPU Direct Rendering
    // ========================================================
    // 当 isGPUDirect==true 时，dataY/U/V 无效，渲染器直接使用 GPU 纹理
    bool isGPUDirect = false;

    // D3D11VA: ID3D11Texture2D* 指针 (由 FFmpeg hw_frames_ctx 管理)
    void* d3d11Texture = nullptr;
    // D3D11VA: texture array 中的 subresource index
    int d3d11TextureIndex = 0;

    // 对应的 AVFrame* (需要 hold 住以保持 GPU 纹理有效，渲染完成后 av_frame_unref)
    // 类型为 void* 以避免在头文件中包含 FFmpeg 头
    void* hwAVFrame = nullptr;

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
        isNV12 = false;
        isGPUDirect = false;
        d3d11Texture = nullptr;
        d3d11TextureIndex = 0;
        hwAVFrame = nullptr;
        // 不重置数据指针和尺寸，这些由 FramePool 管理
    }
};

} // namespace core
} // namespace qsc

#endif // CORE_FRAMEDATA_H
