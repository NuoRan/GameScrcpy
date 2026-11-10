#include "gs/esp32/Contacts.h"

#include <gs/base/InplaceVector.h>

#include <algorithm>

namespace gs::esp32 {

namespace {

constexpr std::uint32_t kPictureScale = 0xFFFF;
constexpr std::uint32_t kLogicalMaximum = GS_ESP32_COORD_MAX;
constexpr std::size_t kMostDowns = 32;

std::uint16_t logical(std::uint32_t picture) noexcept
{
    return static_cast<std::uint16_t>((picture * kLogicalMaximum + kPictureScale / 2) / kPictureScale);
}

bool lifts(const Contact& contact) noexcept
{
    return contact.action == ContactAction::Up || contact.action == ContactAction::Cancel;
}

template <typename Table>
int heldSlot(const Table& table, std::uint32_t pointerId) noexcept
{
    for (std::size_t slot = 0; slot < table.size(); ++slot) {
        if (table[slot].held && table[slot].pointerId == pointerId) {
            return static_cast<int>(slot);
        }
    }
    return -1;
}

template <typename Table>
int freeSlot(const Table& table, std::uint32_t lifting) noexcept
{
    for (std::size_t slot = 0; slot < table.size(); ++slot) {
        if (!table[slot].held && (lifting & (1U << slot)) == 0) {
            return static_cast<int>(slot);
        }
    }
    return -1;
}

}

NaturalPoint naturalPoint(std::uint16_t pictureX, std::uint16_t pictureY, int rotation) noexcept
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

bool ContactSlots::holding() const noexcept
{
    return std::ranges::any_of(m_slots, [](const Slot& slot) { return slot.held; });
}

void ContactSlots::reset() noexcept
{
    m_slots = {};
}

bool ContactSlots::release(const Sink& send)
{
    std::uint32_t lifting = 0;
    Table lifted = m_slots;
    for (std::size_t slot = 0; slot < lifted.size(); ++slot) {
        if (lifted[slot].held) {
            lifting |= 1U << slot;
            lifted[slot].held = false;
        }
    }
    if (lifting == 0) {
        return true;
    }
    if (!send(report(lifted, lifting))) {
        return false;
    }
    m_slots = {};
    return true;
}

bool ContactSlots::frame(std::span<const Contact> contacts, int rotation, const Sink& send)
{
    Table next = m_slots;
    std::uint32_t lifting = 0;
    bool liftMoved = false;
    bool changed = false;
    int dropped = 0;
    InplaceVector<Contact, kMostDowns> downs;
    for (const Contact& contact : contacts) {
        const int slot = heldSlot(next, contact.pointerId);
        if (lifts(contact)) {
            if (slot < 0) {
                continue;
            }
            Slot& lifted = next[static_cast<std::size_t>(slot)];
            if (contact.action == ContactAction::Up) {
                const NaturalPoint target = naturalPoint(contact.x, contact.y, rotation);
                liftMoved = liftMoved || target != lifted.position;
                lifted.position = target;
            }
            lifting |= 1U << static_cast<unsigned>(slot);
            continue;
        }
        if (slot >= 0) {
            Slot& moved = next[static_cast<std::size_t>(slot)];
            const NaturalPoint target = naturalPoint(contact.x, contact.y, rotation);
            changed = changed || target != moved.position;
            moved.position = target;
        } else if (contact.action == ContactAction::Down) {
            if (downs.full()) {
                ++dropped;
            } else {
                downs.push_back(contact);
            }
        }
    }
    if (liftMoved) {
        if (!send(report(next, 0))) {
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
    for (const Contact& down : downs) {
        int slot = freeSlot(next, lifting);
        if (slot < 0 && lifting != 0) {
            if (!send(report(next, lifting))) {
                return false;
            }
            m_slots = next;
            lifting = 0;
            changed = false;
            slot = freeSlot(next, lifting);
        }
        if (slot < 0) {
            ++dropped;
            continue;
        }
        next[static_cast<std::size_t>(slot)] = {.held = true,
                                                .pointerId = down.pointerId,
                                                .position = naturalPoint(down.x, down.y, rotation)};
        changed = true;
    }
    if (changed && !send(report(next, lifting))) {
        return false;
    }
    m_slots = next;
    m_ignored += dropped;
    return true;
}

gs_esp32_touch ContactSlots::report(const Table& table, std::uint32_t lifting)
{
    gs_esp32_touch touch{};
    for (std::size_t slot = 0; slot < table.size(); ++slot) {
        const bool lifted = (lifting & (1U << slot)) != 0;
        if (!table[slot].held && !lifted) {
            continue;
        }
        gs_esp32_contact& contact = touch.contacts[touch.count];
        contact.id = static_cast<std::uint8_t>(slot);
        contact.tip = static_cast<std::uint8_t>(table[slot].held ? 1 : 0);
        contact.x = table[slot].position.x;
        contact.y = table[slot].position.y;
        touch.count = static_cast<std::uint8_t>(touch.count + 1);
    }
    return touch;
}

}
