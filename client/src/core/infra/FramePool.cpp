#include "FramePool.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
#include <malloc.h>
#endif

namespace qsc {
namespace core {

FramePool::FramePool(int poolSize, int maxWidth, int maxHeight)
    : m_frames(poolSize)
    , m_inUse(poolSize, false)
    , m_width(maxWidth)
    , m_height(maxHeight)
{
    // 预分配所有帧内存
    for (int i = 0; i < poolSize; ++i) {
        m_frames[i].poolIndex = i;
        allocateFrame(m_frames[i], maxWidth, maxHeight);
    }
}

FramePool::~FramePool()
{
    for (auto& frame : m_frames) {
        deallocateFrame(frame);
    }
}

FrameData* FramePool::acquire()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // 查找空闲帧
    for (size_t i = 0; i < m_frames.size(); ++i) {
        if (!m_inUse[i]) {
            // 检查帧尺寸是否与当前池尺寸匹配
            // 如果不匹配（旧帧被释放后），重新分配
            if (m_frames[i].width != m_width || m_frames[i].height != m_height) {
                deallocateFrame(m_frames[i]);
                allocateFrame(m_frames[i], m_width, m_height);
            }

            m_inUse[i] = true;
            m_frames[i].refCount.store(1, std::memory_order_release);
            m_frames[i].reset();
            return &m_frames[i];
        }
    }

    // 池已满
    return nullptr;
}

void FramePool::release(FrameData* frame)
{
    if (!frame || frame->poolIndex < 0) {
        return;
    }

    // 减少引用计数
    int oldCount = frame->refCount.fetch_sub(1, std::memory_order_acq_rel);
    if (oldCount == 1) {
        // 最后一个引用，归还到池中
        std::lock_guard<std::mutex> lock(m_mutex);
        if (frame->poolIndex < static_cast<int>(m_inUse.size())) {
            m_inUse[frame->poolIndex] = false;
        }
    }
}

void FramePool::resize(int width, int height)
{
    if (width == m_width && height == m_height) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    m_width = width;
    m_height = height;

    // 重新分配所有帧的内存（包括正在使用的帧）
    // 注意：正在使用的帧会被标记为新尺寸，但内存可能不够大
    // 需要在 acquire 时检查并跳过不匹配的帧
    for (size_t i = 0; i < m_frames.size(); ++i) {
        if (!m_inUse[i]) {
            // 空闲帧：直接重新分配
            deallocateFrame(m_frames[i]);
            allocateFrame(m_frames[i], width, height);
        }
        // 正在使用的帧：保留原内存，等释放后由 acquire 检测处理
    }
}

int FramePool::availableCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(std::count(m_inUse.begin(), m_inUse.end(), false));
}

void FramePool::allocateFrame(FrameData& frame, int width, int height)
{
    // YUV420P 格式：Y 平面完整，U/V 平面各为 Y 的 1/4
    // linesize 使用 32 字节对齐，与 FFmpeg 默认策略一致
    // 这样当 FFmpeg 输出也是 32 字节对齐时，可以直接 memcpy 整个平面
    constexpr int ALIGN = 32;
    int alignedWidth = (width + ALIGN - 1) & ~(ALIGN - 1);

    frame.width = width;
    frame.height = height;
    frame.linesizeY = alignedWidth;
    frame.linesizeU = (alignedWidth / 2 + ALIGN - 1) & ~(ALIGN - 1);  // UV 也对齐
    frame.linesizeV = frame.linesizeU;

    int sizeY = frame.linesizeY * height;
    int sizeU = frame.linesizeU * (height / 2);
    int sizeV = frame.linesizeV * (height / 2);

    // 分配 32 字节对齐的连续内存块（支持 AVX）
    size_t totalSize = sizeY + sizeU + sizeV + ALIGN;
#ifdef _WIN32
    uint8_t* rawBuffer = static_cast<uint8_t*>(_aligned_malloc(totalSize, ALIGN));
#else
    uint8_t* rawBuffer = nullptr;
    posix_memalign(reinterpret_cast<void**>(&rawBuffer), ALIGN, totalSize);
#endif

    frame.dataY = rawBuffer;
    frame.dataU = rawBuffer + sizeY;
    frame.dataV = rawBuffer + sizeY + sizeU;

    // 初始化为黑色（Y=0, U=V=128）
    std::memset(frame.dataY, 0, sizeY);
    std::memset(frame.dataU, 128, sizeU);
    std::memset(frame.dataV, 128, sizeV);
}

void FramePool::deallocateFrame(FrameData& frame)
{
    // dataY 是整块内存的起始地址
    if (frame.dataY) {
#ifdef _WIN32
        _aligned_free(frame.dataY);
#else
        free(frame.dataY);
#endif
    }

    frame.dataY = nullptr;
    frame.dataU = nullptr;
    frame.dataV = nullptr;
    frame.width = 0;
    frame.height = 0;
    frame.linesizeY = 0;
    frame.linesizeU = 0;
    frame.linesizeV = 0;
}

} // namespace core
} // namespace qsc
