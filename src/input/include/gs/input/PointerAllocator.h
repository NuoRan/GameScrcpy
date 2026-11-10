#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace gs::input {

class PointerAllocator
{
public:
    static constexpr std::uint8_t kMaxPointers = 32;

    explicit PointerAllocator(std::uint8_t capacity = kMaxPointers) noexcept;

    [[nodiscard]] std::optional<std::uint8_t> acquire() noexcept;

    void release(std::uint8_t id) noexcept;

    [[nodiscard]] bool inUse(std::uint8_t id) const noexcept;
    [[nodiscard]] std::uint8_t activeCount() const noexcept;
    [[nodiscard]] std::uint8_t capacity() const noexcept { return m_capacity; }

private:
    std::array<std::uint8_t, kMaxPointers> m_free{};
    std::uint8_t m_capacity;
    std::uint8_t m_head = 0;
    std::uint8_t m_freeCount;
    std::uint32_t m_used = 0;
};

}
