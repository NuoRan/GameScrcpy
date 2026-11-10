#pragma once

#include "gs/esp32/Diagnosis.h"
#include "gs/esp32/Port.h"

#include <gs_esp32_protocol.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace gs::esp32 {

enum class LinkState : std::uint8_t { Opening, Ready, Legacy, Lost, Closed };

struct BoardParts {
    std::uint8_t usb = GS_ESP32_USB_NONE;
    std::uint8_t usbProblem = GS_ESP32_PART_UNSUPPORTED;
    std::uint8_t ble = GS_ESP32_BLE_NONE;
    std::uint8_t bleProblem = GS_ESP32_PART_UNSUPPORTED;
    std::uint8_t wifiProblem = GS_ESP32_PART_UNSUPPORTED;
    bool known = false;

    friend bool operator==(const BoardParts&, const BoardParts&) = default;
};

struct BoardInfo {
    int protocol = 0;
    std::string firmware;
    std::uint16_t chipId = 0;
    std::uint16_t chipRevision = 0;
    std::uint32_t features = 0;
    std::array<std::uint8_t, 6> mac{};
    std::string name;
    std::uint8_t wifi = GS_ESP32_WIFI_NONE;
    BoardParts parts;

    friend bool operator==(const BoardInfo&, const BoardInfo&) = default;
};

struct BoardStatus {
    std::uint8_t usb = GS_ESP32_USB_NONE;
    std::uint8_t ble = GS_ESP32_BLE_NONE;
    std::string peer;
    std::uint8_t wifi = GS_ESP32_WIFI_NONE;
    std::array<std::uint8_t, 4> ipv4{};

    friend bool operator==(const BoardStatus&, const BoardStatus&) = default;
};

struct LinkOptions {
    bool fastBaud = false;
    bool allowLegacy = true;
    std::chrono::milliseconds helloWait{GS_ESP32_HELLO_WAIT_MS};
    std::chrono::milliseconds keepAlive{500};
    std::chrono::milliseconds silence{3000};
    std::chrono::milliseconds udpGiveUp{3000};
    std::chrono::milliseconds udpRetry{500};
    std::chrono::milliseconds legacyRetry{1000};
    int legacyRetries = 3;
};

struct LinkEvents {
    std::function<void(LinkState state)> state;
    std::function<void(const BoardInfo& board, const BoardStatus& status)> hello;
    std::function<void(const BoardStatus& status)> status;
    std::function<void(std::chrono::microseconds roundTrip)> roundTrip;
    std::function<void(int level, std::string_view text)> log;
    std::function<void(int code, std::string_view text)> error;
};

[[nodiscard]] std::string chipName(std::uint16_t chipId);
[[nodiscard]] std::string chipTarget(std::uint16_t chipId);
[[nodiscard]] std::string ipv4Text(const std::array<std::uint8_t, 4>& address);
[[nodiscard]] bool frameFreeOf(std::span<const std::uint8_t> frame, std::uint8_t byte) noexcept;

class Esp32Link
{
public:
    Esp32Link(std::unique_ptr<Port> port, LinkOptions options, LinkEvents events = {});
    ~Esp32Link();
    Esp32Link(const Esp32Link&) = delete;
    Esp32Link& operator=(const Esp32Link&) = delete;

    [[nodiscard]] LinkState state() const;
    [[nodiscard]] LinkState waitSettled(std::chrono::milliseconds timeout) const;
    [[nodiscard]] std::optional<BoardInfo> board() const;
    [[nodiscard]] BoardStatus status() const;
    [[nodiscard]] BoardTrouble trouble() const;
    [[nodiscard]] std::optional<std::chrono::microseconds> roundTrip() const;
    [[nodiscard]] std::uint32_t baud() const;
    [[nodiscard]] bool datagrams() const;

    [[nodiscard]] bool sendTouch(const gs_esp32_touch& touch);
    [[nodiscard]] bool sendKeyboard(const gs_esp32_keyboard& keyboard);
    [[nodiscard]] bool sendConsumer(std::uint16_t usage);
    [[nodiscard]] bool sendMouse(const gs_esp32_mouse& mouse);
    [[nodiscard]] bool sendGamepad(const gs_esp32_gamepad& gamepad);
    [[nodiscard]] bool releaseAll();
    [[nodiscard]] bool requestHello();
    [[nodiscard]] bool ping();
    [[nodiscard]] bool setWifi(std::string_view ssid, std::string_view password,
                               std::span<const std::uint8_t, GS_ESP32_KEY_SIZE> key);
    [[nodiscard]] bool forgetWifi();
    [[nodiscard]] bool setName(std::string_view name);
    [[nodiscard]] bool reboot();
    [[nodiscard]] bool enterDownload();
    void close();

private:
    using Clock = std::chrono::steady_clock;

    void run(const std::stop_token& stop);
    [[nodiscard]] bool handshake(const std::stop_token& stop);
    [[nodiscard]] bool helloAtFastBaud(const std::stop_token& stop);
    [[nodiscard]] bool negotiateBaud(const std::stop_token& stop);
    [[nodiscard]] bool awaitPong(std::uint32_t token, std::chrono::milliseconds timeout, const std::stop_token& stop);
    [[nodiscard]] bool pingAndWait(std::chrono::milliseconds timeout, const std::stop_token& stop);
    [[nodiscard]] bool askAgain(const std::stop_token& stop);
    [[nodiscard]] bool resync(const std::stop_token& stop);
    [[nodiscard]] bool helloSeen() const;
    [[nodiscard]] std::uint32_t helloCount() const;
    void pump(std::chrono::milliseconds timeout);
    void noteHeard(std::span<const std::uint8_t> bytes);
    void reportHeard(std::string_view when) const;
    void dispatch(const gs_esp32_frame& frame);
    [[nodiscard]] bool writeBytes(std::span<const std::uint8_t> bytes);
    [[nodiscard]] bool sendFrame(std::uint8_t type, std::span<const std::uint8_t> payload);
    [[nodiscard]] bool sendHelloRequest();
    [[nodiscard]] bool sendLegacyTouch(const gs_esp32_touch& touch);
    [[nodiscard]] bool sendWhenReady(std::uint8_t type, std::span<const std::uint8_t> payload);
    void setState(LinkState next);
    [[nodiscard]] std::uint32_t nextToken();

    std::unique_ptr<Port> m_port;
    LinkOptions m_options;
    LinkEvents m_events;
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_settled;
    std::mutex m_sendMutex;
    LinkState m_state = LinkState::Opening;
    std::optional<BoardInfo> m_board;
    BoardStatus m_status;
    std::optional<std::chrono::microseconds> m_roundTrip;
    std::optional<std::pair<std::uint32_t, Clock::time_point>> m_ping;
    std::uint32_t m_baud = GS_ESP32_HANDSHAKE_BAUD;
    std::uint8_t m_seq = 0;
    std::uint32_t m_token = 0;
    std::uint32_t m_lastPong = 0;
    bool m_helloSeen = false;
    std::uint32_t m_hellos = 0;
    std::atomic<bool> m_resyncing{false};
    std::string m_heard;
    std::array<std::uint8_t, 512> m_rawHeard{};
    std::size_t m_rawHeardCount = 0;
    std::size_t m_heardCount = 0;
    std::size_t m_unframed = 0;
    BoardTrouble m_trouble;
    std::atomic<Clock::rep> m_lastSent{0};
    Clock::time_point m_lastReceived{};
    gs_esp32_parser m_parser{};
    std::jthread m_thread;
};

}
