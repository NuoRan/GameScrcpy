#pragma once

#include "gs/esp32/Contacts.h"
#include "gs/esp32/Keys.h"
#include "gs/esp32/Link.h"
#include "gs/esp32/Port.h"

#include <gs/base/Expected.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>

namespace gs::esp32 {

enum class LinkMode : std::uint8_t { Serial, Wifi };

inline constexpr std::string_view kAutomaticPort = "auto";

struct Esp32Settings {
    std::string port = std::string(kAutomaticPort);
    LinkMode link = LinkMode::Serial;
    std::string address;
    std::filesystem::path boardsFile;
};

enum class OpenFailure : std::uint8_t { NoBoard, PortMissing, PortBusy, NoAnswer, NoKey, BadAddress, Stopped };

struct OpenedLink {
    std::unique_ptr<Esp32Link> link;
    std::string where;
    PortKind kind = PortKind::Other;
    bool wifi = false;
};

[[nodiscard]] Expected<OpenedLink, OpenFailure> openBoard(const Esp32Settings& settings, LinkEvents events = {},
                                                        const std::stop_token& stop = {});

struct Esp32Gamepad {
    std::uint32_t buttons = 0;
    std::uint8_t hat = GS_ESP32_HAT_CENTERED;
    std::int16_t lx = 0;
    std::int16_t ly = 0;
    std::int16_t rx = 0;
    std::int16_t ry = 0;
    std::uint8_t lt = 0;
    std::uint8_t rt = 0;

    friend bool operator==(const Esp32Gamepad&, const Esp32Gamepad&) = default;
};

struct Esp32Capabilities {
    int maxContacts = kMaxContacts;
    bool keys = true;
    bool cancel = false;
    bool mouse = true;
    bool gamepad = true;

    friend bool operator==(const Esp32Capabilities&, const Esp32Capabilities&) = default;
};

class Esp32TouchMethod
{
public:
    Esp32TouchMethod();
    ~Esp32TouchMethod();
    Esp32TouchMethod(const Esp32TouchMethod&) = delete;
    Esp32TouchMethod& operator=(const Esp32TouchMethod&) = delete;

    [[nodiscard]] Expected<void, OpenFailure> open(const Esp32Settings& settings, const std::stop_token& stop = {});
    void attach(std::unique_ptr<Esp32Link> link);
    [[nodiscard]] Esp32Capabilities capabilities() const;
    [[nodiscard]] bool frame(std::span<const Contact> contacts, int rotation);
    [[nodiscard]] bool key(std::uint32_t androidKeycode, bool down);
    [[nodiscard]] bool text(std::string_view ascii);
    [[nodiscard]] bool mouse(std::uint8_t buttons, int dx, int dy, int wheel);
    [[nodiscard]] bool gamepad(const Esp32Gamepad& pad);
    void close();
    [[nodiscard]] bool opened() const;
    [[nodiscard]] std::optional<LinkState> state() const;
    [[nodiscard]] int ignoredContacts() const;

private:
    [[nodiscard]] bool sendKeyboardLocked();
    void releaseLocked();

    mutable std::mutex m_mutex;
    std::unique_ptr<Esp32Link> m_link;
    ContactSlots m_slots;
    KeyboardState m_keyboard;
    std::uint16_t m_consumer = 0;
    std::uint8_t m_buttons = 0;
    Esp32Gamepad m_gamepad;
};

}
