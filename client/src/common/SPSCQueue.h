#ifndef SPSCQUEUE_H
#define SPSCQUEUE_H

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>

namespace qsc {

/**
 * @brief 无锁单生产者单消费者队列
 *
 * 基于环形缓冲区实现的无锁 SPSC (Single Producer Single Consumer) 队列。
 * 特点：
 * - 零锁竞争：生产者和消费者可以完全并行操作
 * - Cache-friendly：使用 cache line padding 避免伪共享
 * - 固定容量：编译时或运行时确定容量，无动态内存分配
 * - 高性能：适用于实时系统和低延迟场景
 *
 * 使用场景：
 * - 控制消息从 UI 线程发送到网络线程
 * - 解码帧从解码线程传递到渲染线程
 *
 * @tparam T 元素类型
 * @tparam Capacity 队列容量（必须是2的幂）
 */
template<typename T, size_t Capacity = 1024>
class SPSCQueue
{
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(Capacity >= 2, "Capacity must be at least 2");

public:
    SPSCQueue() : m_buffer(new Cell[Capacity])
    {
        for (size_t i = 0; i < Capacity; ++i) {
            m_buffer[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~SPSCQueue()
    {
        // 清理未消费的元素
        T item;
        while (tryPop(item)) {}
    }

    // 禁止拷贝和移动
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;

    /**
     * @brief 尝试入队（非阻塞）
     * @param item 要入队的元素
     * @return 是否成功
     */
    bool tryPush(const T& item)
    {
        Cell* cell;
        size_t pos = m_enqueuePos.load(std::memory_order_relaxed);

        for (;;) {
            cell = &m_buffer[pos & (Capacity - 1)];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {
                // 槽位可用，尝试占用
                if (m_enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                // 队列已满
                return false;
            } else {
                // 其他生产者已经前进，但这是 SPSC，不应该发生
                pos = m_enqueuePos.load(std::memory_order_relaxed);
            }
        }

        cell->data = item;
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief 尝试入队（移动语义）
     */
    bool tryPush(T&& item)
    {
        Cell* cell;
        size_t pos = m_enqueuePos.load(std::memory_order_relaxed);

        for (;;) {
            cell = &m_buffer[pos & (Capacity - 1)];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {
                if (m_enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = m_enqueuePos.load(std::memory_order_relaxed);
            }
        }

        cell->data = std::move(item);
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief 尝试出队（非阻塞）
     * @param item 出队元素存放位置
     * @return 是否成功
     */
    bool tryPop(T& item)
    {
        Cell* cell;
        size_t pos = m_dequeuePos.load(std::memory_order_relaxed);

        for (;;) {
            cell = &m_buffer[pos & (Capacity - 1)];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

            if (diff == 0) {
                // 有数据可用，尝试消费
                if (m_dequeuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                // 队列为空
                return false;
            } else {
                // 其他消费者已经前进，但这是 SPSC，不应该发生
                pos = m_dequeuePos.load(std::memory_order_relaxed);
            }
        }

        item = std::move(cell->data);
        cell->sequence.store(pos + Capacity, std::memory_order_release);
        return true;
    }

    /**
     * @brief 检查队列是否为空
     * @note 这是一个近似检查，可能有 ABA 问题，仅用于提示
     */
    bool isEmpty() const
    {
        size_t enq = m_enqueuePos.load(std::memory_order_acquire);
        size_t deq = m_dequeuePos.load(std::memory_order_acquire);
        return enq == deq;
    }

    /**
     * @brief 检查队列是否已满
     * @note 这是一个近似检查
     */
    bool isFull() const
    {
        size_t enq = m_enqueuePos.load(std::memory_order_acquire);
        size_t deq = m_dequeuePos.load(std::memory_order_acquire);
        return (enq - deq) >= Capacity;
    }

    /**
     * @brief 获取当前队列大小
     * @note 这是一个近似值
     */
    size_t size() const
    {
        size_t enq = m_enqueuePos.load(std::memory_order_acquire);
        size_t deq = m_dequeuePos.load(std::memory_order_acquire);
        return enq >= deq ? enq - deq : 0;
    }

    /**
     * @brief 获取队列容量
     */
    constexpr size_t capacity() const { return Capacity; }

    /**
     * @brief 清空队列
     * @note 仅供消费者调用
     */
    void clear()
    {
        T item;
        while (tryPop(item)) {}
    }

private:
    // Cache line size (通常为64字节)
    static constexpr size_t CacheLineSize = 64;

    struct Cell
    {
        std::atomic<size_t> sequence;
        T data;
    };

    // 使用 padding 避免伪共享
    alignas(CacheLineSize) std::atomic<size_t> m_enqueuePos{0};
    char m_padding1[CacheLineSize - sizeof(std::atomic<size_t>)];

    alignas(CacheLineSize) std::atomic<size_t> m_dequeuePos{0};
    char m_padding2[CacheLineSize - sizeof(std::atomic<size_t>)];

    std::unique_ptr<Cell[]> m_buffer;
};

/**
 * @brief 动态容量的 SPSC 队列
 *
 * 运行时指定容量的版本，会自动调整到最近的2的幂
 */
template<typename T>
class DynamicSPSCQueue
{
public:
    explicit DynamicSPSCQueue(size_t capacity)
        : m_capacity(nextPowerOf2(capacity))
        , m_mask(m_capacity - 1)
        , m_buffer(new Cell[m_capacity])
    {
        for (size_t i = 0; i < m_capacity; ++i) {
            m_buffer[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~DynamicSPSCQueue()
    {
        T item;
        while (tryPop(item)) {}
    }

    DynamicSPSCQueue(const DynamicSPSCQueue&) = delete;
    DynamicSPSCQueue& operator=(const DynamicSPSCQueue&) = delete;

    bool tryPush(const T& item)
    {
        Cell* cell;
        size_t pos = m_enqueuePos.load(std::memory_order_relaxed);

        for (;;) {
            cell = &m_buffer[pos & m_mask];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {
                if (m_enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = m_enqueuePos.load(std::memory_order_relaxed);
            }
        }

        cell->data = item;
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool tryPush(T&& item)
    {
        Cell* cell;
        size_t pos = m_enqueuePos.load(std::memory_order_relaxed);

        for (;;) {
            cell = &m_buffer[pos & m_mask];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {
                if (m_enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = m_enqueuePos.load(std::memory_order_relaxed);
            }
        }

        cell->data = std::move(item);
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool tryPop(T& item)
    {
        Cell* cell;
        size_t pos = m_dequeuePos.load(std::memory_order_relaxed);

        for (;;) {
            cell = &m_buffer[pos & m_mask];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

            if (diff == 0) {
                if (m_dequeuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = m_dequeuePos.load(std::memory_order_relaxed);
            }
        }

        item = std::move(cell->data);
        cell->sequence.store(pos + m_capacity, std::memory_order_release);
        return true;
    }

    bool isEmpty() const
    {
        return m_enqueuePos.load(std::memory_order_acquire) ==
               m_dequeuePos.load(std::memory_order_acquire);
    }

    size_t size() const
    {
        size_t enq = m_enqueuePos.load(std::memory_order_acquire);
        size_t deq = m_dequeuePos.load(std::memory_order_acquire);
        return enq >= deq ? enq - deq : 0;
    }

    size_t capacity() const { return m_capacity; }

    void clear()
    {
        T item;
        while (tryPop(item)) {}
    }

private:
    static size_t nextPowerOf2(size_t n)
    {
        if (n < 2) return 2;
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }

    static constexpr size_t CacheLineSize = 64;

    struct Cell
    {
        std::atomic<size_t> sequence;
        T data;
    };

    const size_t m_capacity;
    const size_t m_mask;

    alignas(CacheLineSize) std::atomic<size_t> m_enqueuePos{0};
    char m_padding1[CacheLineSize - sizeof(std::atomic<size_t>)];

    alignas(CacheLineSize) std::atomic<size_t> m_dequeuePos{0};
    char m_padding2[CacheLineSize - sizeof(std::atomic<size_t>)];

    std::unique_ptr<Cell[]> m_buffer;
};

} // namespace qsc

#endif // SPSCQUEUE_H
