#include "HidTouchscreen.h"

#include <gs/base/InplaceVector.h>

#include <algorithm>

namespace gs::app {

namespace {

constexpr std::uint8_t kTouchReport = 1;
constexpr std::uint8_t kLimitsReport = 2;
constexpr auto kContacts = static_cast<std::uint8_t>(kUhidMaxContacts);
constexpr auto kLogicalLow = static_cast<std::uint8_t>(kUhidLogicalMaximum & 0xFFU);
constexpr auto kLogicalHigh = static_cast<std::uint8_t>(kUhidLogicalMaximum >> 8U);
constexpr std::size_t kCountOffset = 1;
constexpr std::size_t kContactsOffset = 2;
constexpr std::size_t kContactSize = 6;
constexpr std::uint32_t kPictureScale = 0xFFFF;

constexpr std::array<std::uint8_t, 20> kHead{
    0x05, 0x0D, 0x09, 0x04, 0xA1, 0x01, 0x85, kTouchReport, 0x09, 0x54,
    0x15, 0x00, 0x25, kContacts, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02,
};

constexpr std::array<std::uint8_t, 46> kFinger{
    0x09, 0x22, 0xA1, 0x02, 0x09, 0x42, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
    0x75, 0x07, 0x81, 0x03, 0x09, 0x51, 0x25, 0x7F, 0x75, 0x08, 0x81, 0x02, 0x05, 0x01, 0x09, 0x30,
    0x09, 0x31, 0x26, kLogicalLow, kLogicalHigh, 0x75, 0x10, 0x95, 0x02, 0x81, 0x02, 0x05, 0x0D, 0xC0,
};

constexpr std::array<std::uint8_t, 13> kTail{
    0x85, kLimitsReport, 0x09, 0x55, 0x25, kContacts, 0x75, 0x08, 0x95, 0x01, 0xB1, 0x02, 0xC0,
};

constexpr auto kDescriptor = [] {
    std::array<std::uint8_t, kHead.size() + kFinger.size() * static_cast<std::size_t>(kUhidMaxContacts) + kTail.size()>
        bytes{};
    std::size_t at = 0;
    for (const std::uint8_t byte : kHead) {
        bytes[at++] = byte;
    }
    for (int finger = 0; finger < kUhidMaxContacts; ++finger) {
        for (const std::uint8_t byte : kFinger) {
            bytes[at++] = byte;
        }
    }
    for (const std::uint8_t byte : kTail) {
        bytes[at++] = byte;
    }
    return bytes;
}();

static_assert(kContactsOffset + kContactSize * static_cast<std::size_t>(kUhidMaxContacts) == kUhidTouchReportSize);
static_assert(kDescriptor.size() <= 4096);

std::uint16_t logical(std::uint32_t picture) noexcept
{
    return static_cast<std::uint16_t>((picture * kUhidLogicalMaximum + kPictureScale / 2) / kPictureScale);
}

void storeLittle(std::uint16_t value, std::uint8_t* out) noexcept
{
    out[0] = static_cast<std::uint8_t>(value & 0xFFU);
    out[1] = static_cast<std::uint8_t>(value >> 8U);
}

bool lifts(const protocol::TouchPoint& point) noexcept
{
    return point.action == protocol::TouchAction::Up || point.action == protocol::TouchAction::Cancel;
}

template <typename Slots>
int heldSlot(const Slots& slots, std::uint8_t pointerId) noexcept
{
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        if (slots[slot].held && slots[slot].pointerId == pointerId) {
            return static_cast<int>(slot);
        }
    }
    return -1;
}

template <typename Slots>
int freeSlot(const Slots& slots, std::uint32_t lifting) noexcept
{
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        if (!slots[slot].held && (lifting & (1U << slot)) == 0) {
            return static_cast<int>(slot);
        }
    }
    return -1;
}

}

UhidPosition naturalPosition(std::uint16_t pictureX, std::uint16_t pictureY, int rotation) noexcept
{
    const std::uint32_t x = pictureX;
    const std::uint32_t y = pictureY;
    switch (rotation & 3) {
    case 1:
        return {.x = logical(kPictureScale - y), .y = logical(x)};
    case 2:
        return {.x = logical(kPictureScale - x), .y = logical(kPictureScale - y)};
    case 3:
        return {.x = logical(y), .y = logical(kPictureScale - x)};
    default:
        return {.x = logical(x), .y = logical(y)};
    }
}

std::span<const std::uint8_t> uhidTouchDescriptor() noexcept
{
    return kDescriptor;
}

bool sameContacts(const TouchReport& a, const TouchReport& b) noexcept
{
    if (a[0] != b[0] || a[kCountOffset] != b[kCountOffset]) {
        return false;
    }
    const std::size_t count = std::min<std::size_t>(a[kCountOffset], static_cast<std::size_t>(kUhidMaxContacts));
    for (std::size_t contact = 0; contact < count; ++contact) {
        const std::size_t at = kContactsOffset + contact * kContactSize;
        if (a[at] != b[at] || a[at + 1] != b[at + 1] || a[at] == 0) {
            return false;
        }
    }
    return true;
}

bool TouchscreenEncoder::holding() const noexcept
{
    return std::ranges::any_of(m_slots, [](const Slot& slot) { return slot.held; });
}

bool TouchscreenEncoder::release(const Emit& emit)
{
    std::uint32_t lifting = 0;
    for (std::size_t slot = 0; slot < m_slots.size(); ++slot) {
        if (m_slots[slot].held) {
            lifting |= 1U << slot;
        }
    }
    if (lifting == 0) {
        return true;
    }
    Slots lifted = m_slots;
    for (Slot& slot : lifted) {
        slot.held = false;
    }
    if (!emit(report(lifted, lifting))) {
        return false;
    }
    m_slots = {};
    return true;
}

bool TouchscreenEncoder::encode(const protocol::TouchFrame& frame, bool stale, int rotation, const Emit& emit)
{
    Slots next = m_slots;
    std::uint32_t lifting = 0;
    bool liftMoved = false;
    bool changed = false;
    InplaceVector<protocol::TouchPoint, protocol::TouchFrame::kMaxPoints> downs;
    for (const protocol::TouchPoint& point : frame.points) {
        const int slot = heldSlot(next, point.pointerId);
        if (lifts(point)) {
            if (slot < 0) {
                continue;
            }
            Slot& lifted = next[static_cast<std::size_t>(slot)];
            if (!stale && point.action == protocol::TouchAction::Up) {
                const UhidPosition target = naturalPosition(point.x, point.y, rotation);
                liftMoved = liftMoved || target != lifted.position;
                lifted.position = target;
            }
            lifting |= 1U << static_cast<unsigned>(slot);
            continue;
        }
        if (stale) {
            continue;
        }
        if (slot >= 0) {
            Slot& moved = next[static_cast<std::size_t>(slot)];
            const UhidPosition target = naturalPosition(point.x, point.y, rotation);
            changed = changed || target != moved.position;
            moved.position = target;
        } else if (point.action == protocol::TouchAction::Down && !downs.full()) {
            downs.push_back(point);
        }
    }
    if (liftMoved) {
        if (!emit(report(next, 0))) {
            return false;
        }
        m_slots = next;
        changed = false;
    }
    for (std::size_t slot = 0; slot < next.size(); ++slot) {
        if ((lifting & (1U << slot)) != 0) {
            next[slot].held = false;
        }
    }
    changed = changed || lifting != 0;
    int ignored = 0;
    for (const protocol::TouchPoint& point : downs) {
        int slot = freeSlot(next, lifting);
        if (slot < 0 && lifting != 0) {
            if (!emit(report(next, lifting))) {
                return false;
            }
            m_slots = next;
            lifting = 0;
            changed = false;
            slot = freeSlot(next, lifting);
        }
        if (slot < 0) {
            ++ignored;
            continue;
        }
        next[static_cast<std::size_t>(slot)] = {.held = true,
                                                .pointerId = point.pointerId,
                                                .position = naturalPosition(point.x, point.y, rotation)};
        changed = true;
    }
    if (changed && !emit(report(next, lifting))) {
        return false;
    }
    m_slots = next;
    m_ignored += ignored;
    return true;
}

TouchReport TouchscreenEncoder::report(const Slots& slots, std::uint32_t lifting)
{
    TouchReport bytes{};
    bytes[0] = kTouchReport;
    std::size_t count = 0;
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        const bool lifted = (lifting & (1U << slot)) != 0;
        if (!slots[slot].held && !lifted) {
            continue;
        }
        std::uint8_t* contact = bytes.data() + kContactsOffset + count * kContactSize;
        contact[0] = static_cast<std::uint8_t>(slots[slot].held ? 1 : 0);
        contact[1] = static_cast<std::uint8_t>(slot + 1);
        storeLittle(slots[slot].position.x, contact + 2);
        storeLittle(slots[slot].position.y, contact + 4);
        ++count;
    }
    bytes[kCountOffset] = static_cast<std::uint8_t>(count);
    return bytes;
}

}
