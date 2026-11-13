#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace gs::media {

class PendingArrivals final
{
public:
    static constexpr std::size_t kCapacity = 32;

    void sent(std::int64_t ptsUs, std::int64_t receivedUs) noexcept;
    [[nodiscard]] std::optional<std::int64_t> returned(std::int64_t ptsUs) noexcept;
    void forget(std::int64_t ptsUs) noexcept;
    void clear() noexcept;

    [[nodiscard]] int held() const noexcept { return static_cast<int>(m_count); }
    [[nodiscard]] std::uint64_t dropped() const noexcept { return m_dropped; }

private:
    struct Entry {
        std::int64_t ptsUs = 0;
        std::int64_t receivedUs = 0;
    };

    std::array<Entry, kCapacity> m_entries{};
    std::size_t m_count = 0;
    std::uint64_t m_dropped = 0;
};

}
