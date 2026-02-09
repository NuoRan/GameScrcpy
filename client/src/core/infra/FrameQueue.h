#ifndef CORE_FRAMEQUEUE_H
#define CORE_FRAMEQUEUE_H

#include "FrameData.h"
#include "FramePool.h"
#include "SPSCQueue.h"
#include <memory>
#include <functional>

namespace qsc {
namespace core {

/**
 * @brief 零拷贝帧队列 / Zero-Copy Frame Queue
 *
 * 整合 FramePool 和 SPSCQueue，提供完整的零拷贝帧传递方案：
 * Integrates FramePool and SPSCQueue for a complete zero-copy frame pipeline:
 * - 生产者：从 FramePool 获取帧 → 填充数据 → 入队
 *   Producer: acquire frame from FramePool → fill data → enqueue
 * - 消费者：出队 → 使用帧数据 → 归还到 FramePool
 *   Consumer: dequeue → use frame data → release to FramePool
 *
 * 特性 / Features:
 * - 预分配内存，无运行时 malloc / Pre-allocated memory, no runtime malloc
 * - 无锁队列，单生产者单消费者 / Lock-free SPSC queue
 * - 帧复用，减少 GC 压力 / Frame reuse, reduced allocation overhead
 */
class FrameQueue {
public:
    /**
     * @brief 构造函数 / Constructor
     * @param poolSize 帧池大小 / Frame pool size
     * @param queueCapacity 队列容量（必须是2的幂）/ Queue capacity (power of 2)
     * @param maxWidth 最大帧宽度 / Max frame width
     * @param maxHeight 最大帧高度 / Max frame height
     */
    explicit FrameQueue(int poolSize = 8,
                       size_t queueCapacity = 8,
                       int maxWidth = 1920,
                       int maxHeight = 1080)
        : m_pool(std::make_unique<FramePool>(poolSize, maxWidth, maxHeight))
        , m_queue(std::make_unique<DynamicSPSCQueue<FrameData*>>(queueCapacity))
    {}

    ~FrameQueue() {
        // 清空队列，归还所有帧
        FrameData* frame = nullptr;
        while (m_queue->tryPop(frame)) {
            if (frame) {
                m_pool->release(frame);
            }
        }
    }

    // 禁止拷贝
    FrameQueue(const FrameQueue&) = delete;
    FrameQueue& operator=(const FrameQueue&) = delete;

    // === 生产者 API ===

    /**
     * @brief 获取一个空闲帧用于填充 / Acquire an idle frame for filling
     * @return 帧指针，池已满返回 nullptr / Frame pointer, nullptr if pool exhausted
     */
    FrameData* acquireFrame() {
        return m_pool->acquire();
    }

    /**
     * @brief 将填充好的帧入队 / Enqueue a filled frame
     * @param frame 要入队的帧 / Frame to enqueue
     * @return 是否成功 / Whether enqueue succeeded
     */
    bool pushFrame(FrameData* frame) {
        if (!frame) return false;
        if (!m_queue->tryPush(frame)) {
            // 队列满，归还帧
            m_pool->release(frame);
            return false;
        }
        return true;
    }

    // === 消费者 API / Consumer API ===

    /**
     * @brief 从队列获取帧 / Pop a frame from queue
     * @return 帧指针，队列空返回 nullptr / Frame pointer, nullptr if empty
     */
    FrameData* popFrame() {
        FrameData* frame = nullptr;
        m_queue->tryPop(frame);
        return frame;
    }

    /**
     * @brief 增加帧引用计数 / Retain frame (for cross-thread passing)
     * @param frame 要增加引用的帧 / Frame to retain
     */
    void retainFrame(FrameData* frame) {
        if (frame) {
            frame->refCount.fetch_add(1, std::memory_order_acq_rel);
        }
    }

    /**
     * @brief 归还帧到池中 / Release frame back to pool
     * @param frame 要归还的帧 / Frame to release
     */
    void releaseFrame(FrameData* frame) {
        if (frame) {
            m_pool->release(frame);
        }
    }

    // === 状态查询 ===

    /**
     * @brief 队列是否为空
     */
    bool isEmpty() const {
        return m_queue->isEmpty();
    }

    /**
     * @brief 队列当前大小
     */
    size_t queueSize() const {
        return m_queue->size();
    }

    /**
     * @brief 帧池可用帧数
     */
    int availableFrames() const {
        return m_pool->availableCount();
    }

    /**
     * @brief 帧池总大小
     */
    int poolSize() const {
        return m_pool->poolSize();
    }

    /**
     * @brief 帧池已使用帧数
     */
    int usedFrames() const {
        return m_pool->poolSize() - m_pool->availableCount();
    }

    /**
     * @brief 调整帧池尺寸（分辨率变化时调用）
     */
    void resize(int width, int height) {
        m_pool->resize(width, height);
    }

    /**
     * @brief 清空队列
     */
    void clear() {
        FrameData* frame = nullptr;
        while (m_queue->tryPop(frame)) {
            if (frame) {
                m_pool->release(frame);
            }
        }
    }

private:
    std::unique_ptr<FramePool> m_pool;
    std::unique_ptr<DynamicSPSCQueue<FrameData*>> m_queue;
};

} // namespace core
} // namespace qsc

#endif // CORE_FRAMEQUEUE_H
