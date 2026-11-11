#include "TapSection.h"

#include <cstring>
#include <new>

namespace gs::exthost::detail {

namespace {

constexpr std::uint32_t kMagic = 0x50415447;
constexpr std::uint32_t kLayoutVersion = 1;
constexpr std::uint64_t kDataOffset = (sizeof(TapLayout) + 63) / 64 * 64;
constexpr std::uint64_t kMostSlotBytes = std::uint64_t{4} * kMostTapSide * kMostTapSide;
constexpr std::uint64_t kMostBytes = kDataOffset + kTapSlots * kMostSlotBytes;

enum SlotState : std::uint32_t { kFree = 0, kWriting = 1, kWaiting = 2, kReading = 3 };

}

TapSection::~TapSection()
{
    if (m_view != nullptr) {
        UnmapViewOfFile(m_view);
    }
    if (m_mapping != nullptr) {
        CloseHandle(m_mapping);
    }
}

std::unique_ptr<TapSection> TapSection::create(std::uint32_t width, std::uint32_t height)
{
    if (width < 1 || height < 1 || width > kMostTapSide || height > kMostTapSide) {
        return nullptr;
    }
    const std::uint64_t slotBytes = std::uint64_t{4} * width * height;
    const std::uint64_t total = kDataOffset + kTapSlots * slotBytes;
    auto made = std::make_unique<TapSection>();
    made->m_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, static_cast<DWORD>(total >> 32),
                                         static_cast<DWORD>(total & 0xFFFFFFFFu), nullptr);
    if (made->m_mapping == nullptr) {
        return nullptr;
    }
    made->m_view = MapViewOfFile(made->m_mapping, FILE_MAP_ALL_ACCESS, 0, 0, static_cast<SIZE_T>(total));
    if (made->m_view == nullptr) {
        return nullptr;
    }
    made->m_bytes = total;
    auto* layout = new (made->m_view) TapLayout{};
    layout->magic = kMagic;
    layout->version = kLayoutVersion;
    layout->slotBytes = slotBytes;
    return made;
}

std::unique_ptr<TapSection> TapSection::open(HANDLE mapping, std::uint64_t bytes)
{
    auto made = std::make_unique<TapSection>();
    made->m_mapping = mapping;
    if (mapping == nullptr || bytes <= kDataOffset || bytes > kMostBytes) {
        return nullptr;
    }
    made->m_view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, static_cast<SIZE_T>(bytes));
    if (made->m_view == nullptr) {
        return nullptr;
    }
    made->m_bytes = bytes;
    const TapLayout& layout = made->layout();
    if (layout.magic != kMagic || layout.version != kLayoutVersion || layout.slotBytes == 0 ||
        layout.slotBytes > kMostSlotBytes || kDataOffset + kTapSlots * layout.slotBytes > bytes) {
        return nullptr;
    }
    return made;
}

TapLayout& TapSection::layout() const noexcept
{
    return *std::launder(static_cast<TapLayout*>(m_view));
}

std::uint8_t* TapSection::slotData(std::size_t slot) const noexcept
{
    return static_cast<std::uint8_t*>(m_view) + kDataOffset + slot * layout().slotBytes;
}

bool TapSection::put(const TapFrame& frame)
{
    if (m_view == nullptr || !validTapFrame(frame)) {
        return false;
    }
    TapLayout& shared = layout();
    const std::uint64_t row = std::uint64_t{4} * frame.width;
    if (row * frame.height > shared.slotBytes) {
        return false;
    }
    const std::scoped_lock lock(m_writing);
    std::optional<std::size_t> target;
    for (const std::uint32_t from : {static_cast<std::uint32_t>(kWaiting), static_cast<std::uint32_t>(kFree)}) {
        for (std::size_t slot = 0; slot < kTapSlots && !target; ++slot) {
            std::uint32_t expected = from;
            if (shared.buffers[slot].state.compare_exchange_strong(expected, kWriting, std::memory_order_acq_rel)) {
                target = slot;
            }
        }
    }
    if (!target) {
        return false;
    }
    std::uint8_t* data = slotData(*target);
    for (std::uint32_t line = 0; line < frame.height; ++line) {
        std::memcpy(data + line * row, frame.pixels + static_cast<std::size_t>(line) * frame.stride,
                    static_cast<std::size_t>(row));
    }
    TapSlot& slot = shared.buffers[*target];
    slot.width = frame.width;
    slot.height = frame.height;
    slot.stride = static_cast<std::uint32_t>(row);
    slot.timeUs = frame.timeUs;
    slot.state.store(kWaiting, std::memory_order_release);
    return true;
}

std::optional<std::size_t> TapSection::take(TapFrame& frame)
{
    if (m_view == nullptr) {
        return std::nullopt;
    }
    TapLayout& shared = layout();
    for (std::size_t slot = 0; slot < kTapSlots; ++slot) {
        std::uint32_t expected = kWaiting;
        if (!shared.buffers[slot].state.compare_exchange_strong(expected, kReading, std::memory_order_acq_rel)) {
            continue;
        }
        const TapSlot& taken = shared.buffers[slot];
        frame = TapFrame{.pixels = slotData(slot),
                         .stride = taken.stride,
                         .width = taken.width,
                         .height = taken.height,
                         .timeUs = taken.timeUs};
        if (!validTapFrame(frame) || std::uint64_t{frame.stride} * frame.height > shared.slotBytes) {
            done(slot);
            return std::nullopt;
        }
        return slot;
    }
    return std::nullopt;
}

void TapSection::done(std::size_t slot)
{
    if (m_view != nullptr && slot < kTapSlots) {
        layout().buffers[slot].state.store(kFree, std::memory_order_release);
    }
}

void TapSection::clear()
{
    if (m_view == nullptr) {
        return;
    }
    const std::scoped_lock lock(m_writing);
    for (TapSlot& slot : layout().buffers) {
        slot.state.store(kFree, std::memory_order_release);
    }
}

}
