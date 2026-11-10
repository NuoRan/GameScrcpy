#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace gs {

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

template <typename T, std::size_t Capacity>
class SpscQueue
{
    static_assert(Capacity >= 2 && std::has_single_bit(Capacity), "capacity must be a power of two");
    static_assert(std::is_nothrow_default_constructible_v<T> && std::is_nothrow_move_assignable_v<T> &&
                      std::is_nothrow_move_constructible_v<T>,
                  "slots are default-constructed, filled by move assignment and emptied by move "
                  "construction, all inside noexcept functions");

public:
    bool tryPush(T value) noexcept
    {
        const std::size_t tail = m_producer.tail.load(std::memory_order_relaxed);
        if (tail - m_producer.cachedHead == Capacity) {
            m_producer.cachedHead = m_consumer.head.load(std::memory_order_acquire);
            if (tail - m_producer.cachedHead == Capacity) {
                return false;
            }
        }
        m_slots[tail & kMask] = std::move(value);
        m_producer.tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    std::optional<T> tryPop() noexcept
    {
        const std::size_t head = m_consumer.head.load(std::memory_order_relaxed);
        if (head == m_consumer.cachedTail) {
            m_consumer.cachedTail = m_producer.tail.load(std::memory_order_acquire);
            if (head == m_consumer.cachedTail) {
                return std::nullopt;
            }
        }
        std::optional<T> value{std::move(m_slots[head & kMask])};
        m_consumer.head.store(head + 1, std::memory_order_release);
        return value;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    static constexpr std::size_t kMask = Capacity - 1;
    static constexpr std::size_t kCacheLine = 64;

    struct alignas(kCacheLine) ProducerSide {
        std::atomic<std::size_t> tail{0};
        std::size_t cachedHead = 0;
    };
    struct alignas(kCacheLine) ConsumerSide {
        std::atomic<std::size_t> head{0};
        std::size_t cachedTail = 0;
    };

    ProducerSide m_producer;
    ConsumerSide m_consumer;
    alignas(kCacheLine) std::array<T, Capacity> m_slots{};
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}
