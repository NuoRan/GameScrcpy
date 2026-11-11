#pragma once

#include "gs/exthost/Module.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace gs::exthost::detail {

inline constexpr std::size_t kTapSlots = 2;

struct TapSlot {
    std::atomic<std::uint32_t> state{0};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;
    std::uint64_t timeUs = 0;
};

struct TapLayout {
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint64_t slotBytes = 0;
    std::array<TapSlot, kTapSlots> buffers{};
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

class TapSection
{
public:
    TapSection() = default;
    ~TapSection();
    TapSection(const TapSection&) = delete;
    TapSection& operator=(const TapSection&) = delete;

    [[nodiscard]] static std::unique_ptr<TapSection> create(std::uint32_t width, std::uint32_t height);
    [[nodiscard]] static std::unique_ptr<TapSection> open(HANDLE mapping, std::uint64_t bytes);

    [[nodiscard]] HANDLE mapping() const noexcept { return m_mapping; }
    [[nodiscard]] std::uint64_t bytes() const noexcept { return m_bytes; }

    [[nodiscard]] bool put(const TapFrame& frame);
    [[nodiscard]] std::optional<std::size_t> take(TapFrame& frame);
    void done(std::size_t slot);
    void clear();

private:
    [[nodiscard]] TapLayout& layout() const noexcept;
    [[nodiscard]] std::uint8_t* slotData(std::size_t slot) const noexcept;

    HANDLE m_mapping = nullptr;
    void* m_view = nullptr;
    std::uint64_t m_bytes = 0;
    std::mutex m_writing;
};

}
