#ifndef CORE_FRAMEPOOL_H
#define CORE_FRAMEPOOL_H

#include "FrameData.h"
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>

namespace qsc {
namespace core {

/**
 * @brief 帧对象池 / Frame Object Pool
 *
 * 预分配固定数量的帧内存，避免频繁 malloc/free。
 * Pre-allocates fixed number of frame buffers to avoid frequent malloc/free.
 * 支持多线程安全的 acquire/release 操作。
 * Supports thread-safe acquire/release operations.
 */
class FramePool {
public:
    /**
     * @brief 构造函数
     * @param poolSize 池中帧数量
     * @param maxWidth 最大帧宽度
     * @param maxHeight 最大帧高度
     */
    explicit FramePool(int poolSize = 4, int maxWidth = 1920, int maxHeight = 1080);
    ~FramePool();

    // 禁止拷贝
    FramePool(const FramePool&) = delete;
    FramePool& operator=(const FramePool&) = delete;

    /**
     * @brief 获取一个空闲帧
     * @return 帧指针，如果池已满返回 nullptr
     */
    FrameData* acquire();

    /**
     * @brief 归还帧到池中
     * @param frame 要归还的帧
     */
    void release(FrameData* frame);

    /**
     * @brief 重新分配帧内存（帧尺寸变化时调用）
     * @param width 新宽度
     * @param height 新高度
     */
    void resize(int width, int height);

    /**
     * @brief 获取当前可用帧数
     */
    int availableCount() const;

    /**
     * @brief 获取池大小
     */
    int poolSize() const { return static_cast<int>(m_frames.size()); }

private:
    void allocateFrame(FrameData& frame, int width, int height);
    void deallocateFrame(FrameData& frame);

private:
    // 使用 atomic flags 实现无锁 acquire/release
    static constexpr int MAX_POOL_SIZE = 16;
    std::vector<FrameData> m_frames;
    std::atomic<bool> m_inUse[MAX_POOL_SIZE];  // 无锁标志位
    std::mutex m_resizeMutex;                   // 仅用于 resize 和帧重新分配
    std::atomic<int> m_width{0};
    std::atomic<int> m_height{0};
};

} // namespace core
} // namespace qsc

#endif // CORE_FRAMEPOOL_H
