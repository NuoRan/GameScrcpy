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
    , m_width(maxWidth)
    , m_height(maxHeight)
{
    // 初始化无锁标志位
    for (int i = 0; i < MAX_POOL_SIZE; ++i) {
        m_inUse[i].store(false, std::memory_order_relaxed);
    }

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
    // 无锁 CAS 扫描
    const int poolSz = static_cast<int>(m_frames.size());
    const int curW = m_width.load(std::memory_order_acquire);
    const int curH = m_height.load(std::memory_order_acquire);

    for (int i = 0; i < poolSz; ++i) {
        bool expected = false;
        if (m_inUse[i].compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            // CAS 成功，拿到此槽位

            // 检查帧尺寸是否匹配（resize 后旧帧可能不匹配）
            if (m_frames[i].width != curW || m_frames[i].height != curH) {
                // 罕见路径：需要重新分配，使用 resize mutex
                std::lock_guard<std::mutex> lock(m_resizeMutex);
                deallocateFrame(m_frames[i]);
                allocateFrame(m_frames[i], curW, curH);
            }

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

    // 无锁 release
    int oldCount = frame->refCount.fetch_sub(1, std::memory_order_acq_rel);
    if (oldCount == 1) {
        // 最后一个引用，原子标记为可用（无需 mutex）
        m_inUse[frame->poolIndex].store(false, std::memory_order_release);
    }
}

void FramePool::resize(int width, int height)
{
    if (width == m_width.load(std::memory_order_relaxed) &&
        height == m_height.load(std::memory_order_relaxed)) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_resizeMutex);

    m_width.store(width, std::memory_order_release);
    m_height.store(height, std::memory_order_release);

    // 重新分配空闲帧的内存
    for (size_t i = 0; i < m_frames.size(); ++i) {
        if (!m_inUse[i].load(std::memory_order_acquire)) {
            deallocateFrame(m_frames[i]);
            allocateFrame(m_frames[i], width, height);
        }
    }
}

int FramePool::availableCount() const
{
    // 无锁计数
    int count = 0;
    for (size_t i = 0; i < m_frames.size(); ++i) {
        if (!m_inUse[i].load(std::memory_order_relaxed)) {
            ++count;
        }
    }
    return count;
}

void FramePool::allocateFrame(FrameData& frame, int width, int height)
{
    // YUV420P 格式：Y 平面完整，U/V 平面各为 Y 的 1/4
    // 额外分配 NV12 UV 交织平面空间
    // linesize 使用 32 字节对齐，与 FFmpeg 默认策略一致
    constexpr int ALIGN = 32;
    int alignedWidth = (width + ALIGN - 1) & ~(ALIGN - 1);

    frame.width = width;
    frame.height = height;
    frame.linesizeY = alignedWidth;
    frame.linesizeU = (alignedWidth / 2 + ALIGN - 1) & ~(ALIGN - 1);  // UV 也对齐
    frame.linesizeV = frame.linesizeU;
    // NV12: UV 交织平面 linesize = width (两个分量交织, 每个分量 w/2, 总共 w)
    frame.linesizeUV = alignedWidth;

    int sizeY = frame.linesizeY * height;
    int sizeU = frame.linesizeU * (height / 2);
    int sizeV = frame.linesizeV * (height / 2);
    int sizeUV = frame.linesizeUV * (height / 2);  // NV12 UV 平面

    // 分配 32 字节对齐的连续内存块（支持 AVX）
    // 布局: [Y][U][V][UV_NV12]
    size_t totalSize = sizeY + sizeU + sizeV + sizeUV + ALIGN;
#ifdef _WIN32
    uint8_t* rawBuffer = static_cast<uint8_t*>(_aligned_malloc(totalSize, ALIGN));
#else
    uint8_t* rawBuffer = nullptr;
    posix_memalign(reinterpret_cast<void**>(&rawBuffer), ALIGN, totalSize);
#endif

    frame.dataY = rawBuffer;
    frame.dataU = rawBuffer + sizeY;
    frame.dataV = rawBuffer + sizeY + sizeU;
    frame.dataUV = rawBuffer + sizeY + sizeU + sizeV;

    // 初始化为黑色（Y=0, U=V=128）
    std::memset(frame.dataY, 0, sizeY);
    std::memset(frame.dataU, 128, sizeU);
    std::memset(frame.dataV, 128, sizeV);
    std::memset(frame.dataUV, 128, sizeUV);
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
    frame.dataUV = nullptr;
    frame.width = 0;
    frame.height = 0;
    frame.linesizeY = 0;
    frame.linesizeU = 0;
    frame.linesizeV = 0;
    frame.linesizeUV = 0;
    frame.isNV12 = false;
}

} // namespace core
} // namespace qsc
