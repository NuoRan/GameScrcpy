#pragma once

#include <gs_esp32_protocol.h>

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace gs::esp32 {

enum class ContactAction : std::uint8_t { Down, Move, Up, Cancel };

struct Contact {
    std::uint32_t pointerId = 0;
    ContactAction action = ContactAction::Move;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
};

struct NaturalPoint {
    std::uint16_t x = 0;
    std::uint16_t y = 0;

    friend bool operator==(const NaturalPoint&, const NaturalPoint&) = default;
};

inline constexpr int kMaxContacts = GS_ESP32_MAX_CONTACTS;

[[nodiscard]] NaturalPoint naturalPoint(std::uint16_t pictureX, std::uint16_t pictureY, int rotation) noexcept;

class ContactSlots
{
public:
    using Sink = std::function<bool(const gs_esp32_touch& touch)>;

    [[nodiscard]] bool frame(std::span<const Contact> contacts, int rotation, const Sink& send);
    [[nodiscard]] bool release(const Sink& send);
    [[nodiscard]] bool holding() const noexcept;
    [[nodiscard]] int ignored() const noexcept { return m_ignored; }
    void reset() noexcept;

private:
    struct Slot {
        bool held = false;
        std::uint32_t pointerId = 0;
        NaturalPoint position;
    };

    using Table = std::array<Slot, kMaxContacts>;

    [[nodiscard]] static gs_esp32_touch report(const Table& table, std::uint32_t lifting);

    Table m_slots{};
    int m_ignored = 0;
};

}
