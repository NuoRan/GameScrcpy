#include "gs/input/PointerAllocator.h"

#include <algorithm>
#include <bit>

namespace gs::input {

PointerAllocator::PointerAllocator(std::uint8_t capacity) noexcept
    : m_capacity(std::min(capacity, kMaxPointers))
    , m_freeCount(m_capacity)
{
    for (std::uint8_t id = 0; id < m_capacity; ++id) {
        m_free[id] = id;
    }
}

std::optional<std::uint8_t> PointerAllocator::acquire() noexcept
{
    if (m_freeCount == 0) {
        return std::nullopt;
    }
    const std::uint8_t id = m_free[m_head];
    m_head = static_cast<std::uint8_t>((m_head + 1) % kMaxPointers);
    --m_freeCount;
    m_used |= 1U << id;
    return id;
}

void PointerAllocator::release(std::uint8_t id) noexcept
{
    if (!inUse(id)) {
        return;
    }
    m_used &= ~(1U << id);
    m_free[(m_head + m_freeCount) % kMaxPointers] = id;
    ++m_freeCount;
}

bool PointerAllocator::inUse(std::uint8_t id) const noexcept
{
    return id < m_capacity && (m_used & (1U << id)) != 0;
}

std::uint8_t PointerAllocator::activeCount() const noexcept
{
    return static_cast<std::uint8_t>(std::popcount(m_used));
}

}
