#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <initializer_list>
#include <utility>

namespace gs {

template <typename T, std::size_t N>
class InplaceVector
{
public:
    using value_type = T;
    using iterator = T*;
    using const_iterator = const T*;

    constexpr InplaceVector() = default;

    constexpr InplaceVector(std::initializer_list<T> items)
    {
        for (const T& item : items) {
            push_back(item);
        }
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_size; }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] constexpr bool full() const noexcept { return m_size == N; }

    constexpr void push_back(T value)
    {
        if (full()) {
            std::terminate();
        }
        m_items[m_size++] = std::move(value);
    }

    constexpr void clear() noexcept { m_size = 0; }

    [[nodiscard]] constexpr T& operator[](std::size_t index) noexcept { return m_items[index]; }
    [[nodiscard]] constexpr const T& operator[](std::size_t index) const noexcept
    {
        return m_items[index];
    }

    [[nodiscard]] constexpr T& front() noexcept { return m_items[0]; }
    [[nodiscard]] constexpr const T& front() const noexcept { return m_items[0]; }
    [[nodiscard]] constexpr T& back() noexcept { return m_items[m_size - 1]; }
    [[nodiscard]] constexpr const T& back() const noexcept { return m_items[m_size - 1]; }

    [[nodiscard]] constexpr iterator begin() noexcept { return m_items.data(); }
    [[nodiscard]] constexpr iterator end() noexcept { return m_items.data() + m_size; }
    [[nodiscard]] constexpr const_iterator begin() const noexcept { return m_items.data(); }
    [[nodiscard]] constexpr const_iterator end() const noexcept { return m_items.data() + m_size; }

    friend constexpr bool operator==(const InplaceVector& lhs, const InplaceVector& rhs)
    {
        return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
    }

private:
    std::array<T, N> m_items{};
    std::size_t m_size = 0;
};

}
