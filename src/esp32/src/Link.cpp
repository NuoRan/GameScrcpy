#include "gs/esp32/Link.h"

#include <gs/base/Log.h>
#include <gs/platform/Thread.h>

#include <algorithm>
#include <array>
#include <format>
#include <iterator>

namespace gs::esp32 {

namespace {

constexpr std::string_view kCategory = "esp32";
constexpr std::chrono::milliseconds kPumpSlice{20};
constexpr std::chrono::milliseconds kIdleSlice{50};
constexpr std::chrono::milliseconds kBaudAck{300};
constexpr std::chrono::milliseconds kBaudProbe{150};
constexpr std::chrono::milliseconds kBaudRecovery{1500};
constexpr int kBaudProbes = 3;
constexpr std::size_t kTouchPayload = 1 + 6 * static_cast<std::size_t>(GS_ESP32_MAX_CONTACTS);
constexpr std::size_t kWifiPayload = 2 + GS_ESP32_MAX_SSID + GS_ESP32_MAX_PASSWORD + GS_ESP32_KEY_SIZE;
constexpr std::size_t kHeardShown = 240;

std::chrono::milliseconds remaining(std::chrono::steady_clock::time_point deadline)
{
    const auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    return std::max(left, std::chrono::milliseconds{0});
}

template <std::size_t N>
bool copyText(std::string_view text, char (&out)[N]) noexcept
{
    if (text.size() >= N || text.find('\0') != std::string_view::npos) {
        return false;
    }
    std::ranges::copy(text, out);
    out[text.size()] = '\0';
    return true;
}

}

std::string chipName(std::uint16_t chipId)
{
    switch (chipId) {
    case GS_ESP32_CHIP_ESP32:
        return "ESP32";
    case GS_ESP32_CHIP_ESP32S2:
        return "ESP32-S2";
    case GS_ESP32_CHIP_ESP32C3:
        return "ESP32-C3";
    case GS_ESP32_CHIP_ESP32S3:
        return "ESP32-S3";
    case GS_ESP32_CHIP_ESP32C6:
        return "ESP32-C6";
    case GS_ESP32_CHIP_ESP32P4:
        return "ESP32-P4";
    default:
        return std::format("ESP32 ({})", chipId);
    }
}

std::string chipTarget(std::uint16_t chipId)
{
    switch (chipId) {
    case GS_ESP32_CHIP_ESP32:
        return "esp32";
    case GS_ESP32_CHIP_ESP32S2:
        return "esp32s2";
    case GS_ESP32_CHIP_ESP32C3:
        return "esp32c3";
    case GS_ESP32_CHIP_ESP32S3:
        return "esp32s3";
    case GS_ESP32_CHIP_ESP32C6:
        return "esp32c6";
    case GS_ESP32_CHIP_ESP32P4:
        return "esp32p4";
    default:
        return {};
    }
}

std::string ipv4Text(const std::array<std::uint8_t, 4>& address)
{
    return std::format("{}.{}.{}.{}", address[0], address[1], address[2], address[3]);
}

bool frameFreeOf(std::span<const std::uint8_t> frame, std::uint8_t byte) noexcept
{
    return std::ranges::find(frame, byte) == frame.end();
}

Esp32Link::Esp32Link(std::unique_ptr<Port> port, LinkOptions options, LinkEvents events)
    : m_port(std::move(port))
    , m_options(options)
    , m_events(std::move(events))
{
    gs_esp32_parser_reset(&m_parser);
    m_thread = std::jthread([this](const std::stop_token& stop) { run(stop); });
}

Esp32Link::~Esp32Link()
{
    close();
}

void Esp32Link::close()
{
    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_port->interrupt();
        m_thread.join();
    }
    {
        const std::lock_guard lock(m_mutex);
        m_state = LinkState::Closed;
    }
    m_settled.notify_all();
}

LinkState Esp32Link::state() const
{
    const std::lock_guard lock(m_mutex);
    return m_state;
}

LinkState Esp32Link::waitSettled(std::chrono::milliseconds timeout) const
{
    std::unique_lock lock(m_mutex);
    m_settled.wait_for(lock, timeout, [this] { return m_state != LinkState::Opening; });
    return m_state;
}

std::optional<BoardInfo> Esp32Link::board() const
{
    const std::lock_guard lock(m_mutex);
    return m_board;
}

BoardStatus Esp32Link::status() const
{
    const std::lock_guard lock(m_mutex);
    return m_status;
}

std::optional<std::chrono::microseconds> Esp32Link::roundTrip() const
{
    const std::lock_guard lock(m_mutex);
    return m_roundTrip;
}

BoardTrouble Esp32Link::trouble() const
{
    const std::lock_guard lock(m_mutex);
    return m_trouble;
}

std::uint32_t Esp32Link::baud() const
{
    const std::lock_guard lock(m_mutex);
    return m_baud;
}

bool Esp32Link::datagrams() const
{
    return m_port->datagrams();
}

void Esp32Link::setState(LinkState next)
{
    {
        const std::lock_guard lock(m_mutex);
        if (m_state == next || m_state == LinkState::Lost || m_state == LinkState::Closed) {
            return;
        }
        m_state = next;
    }
    m_settled.notify_all();
    if (next == LinkState::Lost) {
        log::info(kCategory, "the board stopped answering");
    }
    if (m_events.state) {
        m_events.state(next);
    }
}

std::uint32_t Esp32Link::nextToken()
{
    const std::lock_guard lock(m_mutex);
    ++m_token;
    if (m_token == 0) {
        ++m_token;
    }
    return m_token;
}

bool Esp32Link::helloSeen() const
{
    const std::lock_guard lock(m_mutex);
    return m_helloSeen;
}

void Esp32Link::run(const std::stop_token& stop)
{
    platform::setCurrentThreadName("ESP32 link");
    m_lastReceived = Clock::now();
    if (!handshake(stop)) {
        if (!stop.stop_requested()) {
            setState(LinkState::Lost);
        }
        return;
    }
    const auto slice = std::min(kIdleSlice, std::max(m_options.keepAlive / 2, std::chrono::milliseconds{1}));
    int legacyRetries = 0;
    auto nextLegacyRetry = Clock::now() + m_options.legacyRetry;
    while (!stop.stop_requested()) {
        if (m_port->gone()) {
            setState(LinkState::Lost);
            return;
        }
        const auto now = Clock::now();
        const LinkState current = state();
        if (current == LinkState::Ready) {
            const Clock::time_point lastSent{Clock::duration{m_lastSent.load(std::memory_order_acquire)}};
            if (now - lastSent >= m_options.keepAlive) {
                (void)ping();
            }
            if (now - m_lastReceived >= m_options.silence) {
                setState(LinkState::Lost);
                return;
            }
        } else if (current == LinkState::Legacy && legacyRetries < m_options.legacyRetries && now >= nextLegacyRetry) {
            (void)sendHelloRequest();
            ++legacyRetries;
            nextLegacyRetry = now + m_options.legacyRetry;
            if (legacyRetries == m_options.legacyRetries) {
                reportHeard("after asking again");
            }
        } else if (current != LinkState::Legacy) {
            return;
        }
        pump(slice);
    }
}

bool Esp32Link::handshake(const std::stop_token& stop)
{
    m_port->discardInput();
    const bool udp = m_port->datagrams();
    const auto start = Clock::now();
    const auto deadline = start + (udp ? m_options.udpGiveUp : m_options.helloWait);
    auto nextRequest = start;
    while (!stop.stop_requested() && !helloSeen()) {
        const auto now = Clock::now();
        if (now >= deadline) {
            break;
        }
        if (now >= nextRequest) {
            if (!sendHelloRequest()) {
                return false;
            }
            nextRequest = udp ? now + m_options.udpRetry : deadline;
        }
        pump(std::min(kPumpSlice, remaining(deadline)));
    }
    if (stop.stop_requested()) {
        return false;
    }
    if (!helloSeen() && !udp && m_options.fastBaud && m_rawHeardCount == 0) {
        (void)helloAtFastBaud(stop);
        if (stop.stop_requested()) {
            return false;
        }
    }
    if (!helloSeen()) {
        const BoardTrouble trouble = diagnoseHeard({m_rawHeard.data(), m_rawHeardCount});
        {
            const std::lock_guard lock(m_mutex);
            m_trouble = trouble;
        }
        const bool legacy = !udp && m_options.allowLegacy
                         && (trouble.problem == BoardProblem::OldFirmware || trouble.problem == BoardProblem::Silent);
        reportHeard(legacy ? "before falling back" : "before giving up");
        if (legacy) {
            if (trouble.problem == BoardProblem::Silent) {
                log::info(kCategory, "no HELLO within {} ms and nothing heard: treating the board as the old "
                                     "QineTouch firmware, so touches go out as old packets only",
                          m_options.helloWait.count());
            } else {
                log::info(kCategory, "no HELLO within {} ms: the board sent the old QineTouch greeting, so touches "
                                     "go out as old packets only",
                          m_options.helloWait.count());
            }
            setState(LinkState::Legacy);
            return true;
        }
        log::warning(kCategory, "no HELLO within {} ms: {}", m_options.helloWait.count(),
                     boardProblemName(trouble.problem));
        if (!trouble.resetReason.empty()) {
            log::warning(kCategory, "the last reset of the board was {}", trouble.resetReason);
        }
        return false;
    }
    const std::optional<BoardInfo> info = board();
    const bool serialLink = info && (info->features & GS_ESP32_FEATURE_USB_SERIAL_LINK) != 0;
    if (m_options.fastBaud && !udp && !serialLink && baud() != GS_ESP32_FAST_BAUD && !negotiateBaud(stop)) {
        return false;
    }
    setState(LinkState::Ready);
    return true;
}

bool Esp32Link::helloAtFastBaud(const std::stop_token& stop)
{
    if (!m_port->setBaud(GS_ESP32_FAST_BAUD)) {
        return false;
    }
    m_port->discardInput();
    const auto deadline = Clock::now() + m_options.helloWait;
    if (sendHelloRequest()) {
        while (!stop.stop_requested() && !helloSeen() && Clock::now() < deadline) {
            pump(std::min(kPumpSlice, remaining(deadline)));
        }
    }
    if (helloSeen()) {
        {
            const std::lock_guard lock(m_mutex);
            m_baud = GS_ESP32_FAST_BAUD;
        }
        log::info(kCategory, "the board was still at {} baud from an earlier session", GS_ESP32_FAST_BAUD);
        return true;
    }
    (void)m_port->setBaud(GS_ESP32_HANDSHAKE_BAUD);
    m_rawHeardCount = 0;
    return false;
}

bool Esp32Link::negotiateBaud(const std::stop_token& stop)
{
    const std::uint32_t token = nextToken();
    const gs_esp32_set_baud request{.baud = GS_ESP32_FAST_BAUD, .token = token};
    std::array<std::uint8_t, 8> payload{};
    const std::size_t size = gs_esp32_encode_set_baud(payload.data(), payload.size(), &request);
    if (size == 0 || !sendFrame(GS_ESP32_MSG_SET_BAUD, {payload.data(), size}) || !awaitPong(token, kBaudAck, stop)) {
        return !stop.stop_requested();
    }
    if (m_port->setBaud(GS_ESP32_FAST_BAUD)) {
        for (int probe = 0; probe < kBaudProbes && !stop.stop_requested(); ++probe) {
            if (pingAndWait(kBaudProbe, stop)) {
                {
                    const std::lock_guard lock(m_mutex);
                    m_baud = GS_ESP32_FAST_BAUD;
                }
                log::info(kCategory, "serial link at {} baud", GS_ESP32_FAST_BAUD);
                return true;
            }
        }
    }
    (void)m_port->setBaud(GS_ESP32_HANDSHAKE_BAUD);
    const auto deadline = Clock::now() + kBaudRecovery;
    while (!stop.stop_requested() && Clock::now() < deadline) {
        if (pingAndWait(kBaudProbe, stop)) {
            log::info(kCategory, "{} baud did not work; staying at {}", GS_ESP32_FAST_BAUD, GS_ESP32_HANDSHAKE_BAUD);
            return true;
        }
    }
    return false;
}

bool Esp32Link::awaitPong(std::uint32_t token, std::chrono::milliseconds timeout, const std::stop_token& stop)
{
    const auto deadline = Clock::now() + timeout;
    while (!stop.stop_requested()) {
        {
            const std::lock_guard lock(m_mutex);
            if (m_lastPong == token) {
                return true;
            }
        }
        if (Clock::now() >= deadline) {
            return false;
        }
        pump(std::min(kPumpSlice, remaining(deadline)));
    }
    return false;
}

bool Esp32Link::pingAndWait(std::chrono::milliseconds timeout, const std::stop_token& stop)
{
    const std::uint32_t token = nextToken();
    std::array<std::uint8_t, 4> payload{};
    const std::size_t size = gs_esp32_encode_token(payload.data(), payload.size(), token);
    return sendFrame(GS_ESP32_MSG_PING, {payload.data(), size}) && awaitPong(token, timeout, stop);
}

void Esp32Link::pump(std::chrono::milliseconds timeout)
{
    std::array<std::uint8_t, GS_ESP32_MAX_DATAGRAM> buffer{};
    const std::size_t received = m_port->read(buffer, timeout);
    if (received == 0) {
        return;
    }
    if (m_port->datagrams()) {
        gs_esp32_frame frame{};
        if (gs_esp32_frame_decode(buffer.data(), received, &frame)) {
            dispatch(frame);
        }
        return;
    }
    if (!helloSeen()) {
        noteHeard({buffer.data(), received});
    }
    std::size_t at = 0;
    while (at < received) {
        at += gs_esp32_parser_feed(&m_parser, buffer.data() + at, received - at);
        gs_esp32_frame frame{};
        while (gs_esp32_parser_next(&m_parser, &frame)) {
            dispatch(frame);
        }
    }
}

void Esp32Link::noteHeard(std::span<const std::uint8_t> bytes)
{
    m_heardCount += bytes.size();
    for (const std::uint8_t byte : bytes) {
        if (m_rawHeardCount < m_rawHeard.size()) {
            m_rawHeard[m_rawHeardCount++] = byte;
        }
    }
    for (const std::uint8_t byte : bytes) {
        if (m_heard.size() >= kHeardShown) {
            return;
        }
        if (byte == '\r') {
            m_heard += "\\r";
        } else if (byte == '\n') {
            m_heard += "\\n";
        } else if (byte >= 0x20 && byte < 0x7F) {
            m_heard.push_back(static_cast<char>(byte));
        } else {
            m_heard += std::format("\\x{:02X}", byte);
        }
    }
}

void Esp32Link::reportHeard(std::string_view when) const
{
    if (m_heardCount == 0) {
        log::info(kCategory, "the board sent nothing {}", when);
        return;
    }
    log::info(kCategory, "the board sent {} bytes {}, starting with: {}", m_heardCount, when, m_heard);
}

void Esp32Link::dispatch(const gs_esp32_frame& frame)
{
    m_lastReceived = Clock::now();
    switch (frame.type) {
    case GS_ESP32_MSG_HELLO: {
        gs_esp32_hello hello{};
        if (!gs_esp32_decode_hello(frame.payload, frame.length, &hello)) {
            log::warning(kCategory, "malformed HELLO");
            return;
        }
        BoardInfo info{.protocol = hello.protocol,
                       .firmware = hello.firmware,
                       .chipId = hello.chip_id,
                       .chipRevision = hello.chip_revision,
                       .features = hello.features,
                       .name = hello.name,
                       .wifi = hello.wifi,
                       .parts = BoardParts{.usb = hello.parts.usb,
                                           .usbProblem = hello.parts.usb_problem,
                                           .ble = hello.parts.ble,
                                           .bleProblem = hello.parts.ble_problem,
                                           .wifiProblem = hello.parts.wifi_problem,
                                           .known = hello.extension != 0}};
        std::copy(std::begin(hello.mac), std::end(hello.mac), info.mac.begin());
        BoardStatus snapshot;
        bool promote = false;
        {
            const std::lock_guard lock(m_mutex);
            m_board = info;
            m_status.wifi = hello.wifi;
            if (hello.parts.usb != GS_ESP32_USB_NONE) {
                m_status.usb = hello.parts.usb;
            }
            if (hello.parts.ble != GS_ESP32_BLE_NONE) {
                m_status.ble = hello.parts.ble;
            }
            std::copy(std::begin(hello.ipv4), std::end(hello.ipv4), m_status.ipv4.begin());
            snapshot = m_status;
            m_helloSeen = true;
            promote = m_state == LinkState::Legacy;
        }
        if (promote) {
            log::info(kCategory, "the board answered after all: using protocol v2");
            setState(LinkState::Ready);
        }
        if (m_events.hello) {
            m_events.hello(info, snapshot);
        }
        return;
    }
    case GS_ESP32_MSG_STATUS: {
        gs_esp32_status message{};
        if (!gs_esp32_decode_status(frame.payload, frame.length, &message)) {
            log::warning(kCategory, "malformed STATUS");
            return;
        }
        BoardStatus snapshot{.usb = message.usb, .ble = message.ble, .peer = message.peer, .wifi = message.wifi};
        std::copy(std::begin(message.ipv4), std::end(message.ipv4), snapshot.ipv4.begin());
        {
            const std::lock_guard lock(m_mutex);
            m_status = snapshot;
        }
        if (m_events.status) {
            m_events.status(snapshot);
        }
        return;
    }
    case GS_ESP32_MSG_PONG: {
        std::uint32_t token = 0;
        if (!gs_esp32_decode_token(frame.payload, frame.length, &token)) {
            return;
        }
        std::optional<std::chrono::microseconds> measured;
        {
            const std::lock_guard lock(m_mutex);
            m_lastPong = token;
            if (m_ping && m_ping->first == token) {
                measured = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - m_ping->second);
                m_roundTrip = measured;
                m_ping.reset();
            }
        }
        if (measured && m_events.roundTrip) {
            m_events.roundTrip(*measured);
        }
        return;
    }
    case GS_ESP32_MSG_LOG: {
        gs_esp32_log message{};
        if (!gs_esp32_decode_log(frame.payload, frame.length, &message)) {
            return;
        }
        log::debug(kCategory, "board: {}", message.text);
        if (m_events.log) {
            m_events.log(message.level, message.text);
        }
        return;
    }
    case GS_ESP32_MSG_ERROR: {
        gs_esp32_error message{};
        if (!gs_esp32_decode_error(frame.payload, frame.length, &message)) {
            return;
        }
        log::warning(kCategory, "board error {}: {}", message.code, message.text);
        if (m_events.error) {
            m_events.error(message.code, message.text);
        }
        return;
    }
    default:
        return;
    }
}

bool Esp32Link::writeBytes(std::span<const std::uint8_t> bytes)
{
    if (!m_port->write(bytes)) {
        return false;
    }
    m_lastSent.store(Clock::now().time_since_epoch().count(), std::memory_order_release);
    return true;
}

bool Esp32Link::sendFrame(std::uint8_t type, std::span<const std::uint8_t> payload)
{
    std::array<std::uint8_t, GS_ESP32_MAX_FRAME> frame{};
    const std::lock_guard lock(m_sendMutex);
    const std::size_t length =
        gs_esp32_frame_encode(frame.data(), frame.size(), type, m_seq, payload.data(), payload.size());
    if (length == 0) {
        return false;
    }
    ++m_seq;
    return writeBytes({frame.data(), length});
}

bool Esp32Link::sendHelloRequest()
{
    std::array<std::uint8_t, 1> payload{};
    const std::size_t size = gs_esp32_encode_hello_req(payload.data(), payload.size(), GS_ESP32_PROTOCOL_VERSION);
    std::array<std::uint8_t, GS_ESP32_FRAME_OVERHEAD + 1> frame{};
    const std::lock_guard lock(m_sendMutex);
    for (int attempt = 0; attempt < 256; ++attempt) {
        const std::uint8_t seq = m_seq;
        ++m_seq;
        const std::size_t length =
            gs_esp32_frame_encode(frame.data(), frame.size(), GS_ESP32_MSG_HELLO_REQ, seq, payload.data(), size);
        if (length > 0 && frameFreeOf({frame.data(), length}, GS_ESP32_LEGACY_MAGIC)) {
            return writeBytes({frame.data(), length});
        }
    }
    return false;
}

bool Esp32Link::sendLegacyTouch(const gs_esp32_touch& touch)
{
    if (touch.count == 0) {
        return true;
    }
    if (touch.count > GS_ESP32_MAX_CONTACTS) {
        return false;
    }
    std::array<std::uint8_t, static_cast<std::size_t>(GS_ESP32_LEGACY_PACKET) * GS_ESP32_MAX_CONTACTS> packets{};
    for (std::size_t i = 0; i < static_cast<std::size_t>(touch.count); ++i) {
        const gs_esp32_contact& contact = touch.contacts[i];
        gs_esp32_legacy_packet(packets.data() + i * GS_ESP32_LEGACY_PACKET, contact.tip, contact.id, contact.x,
                               contact.y, touch.count);
    }
    const std::lock_guard lock(m_sendMutex);
    return writeBytes({packets.data(), static_cast<std::size_t>(touch.count) * GS_ESP32_LEGACY_PACKET});
}

bool Esp32Link::sendWhenReady(std::uint8_t type, std::span<const std::uint8_t> payload)
{
    return state() == LinkState::Ready && sendFrame(type, payload);
}

bool Esp32Link::sendTouch(const gs_esp32_touch& touch)
{
    const LinkState current = state();
    if (current == LinkState::Legacy) {
        return sendLegacyTouch(touch);
    }
    if (current != LinkState::Ready) {
        return false;
    }
    std::array<std::uint8_t, kTouchPayload> payload{};
    const std::size_t size = gs_esp32_encode_touch(payload.data(), payload.size(), &touch);
    return size > 0 && sendFrame(GS_ESP32_MSG_TOUCH, {payload.data(), size});
}

bool Esp32Link::sendKeyboard(const gs_esp32_keyboard& keyboard)
{
    std::array<std::uint8_t, 1 + GS_ESP32_KEYS> payload{};
    const std::size_t size = gs_esp32_encode_keyboard(payload.data(), payload.size(), &keyboard);
    return size > 0 && sendWhenReady(GS_ESP32_MSG_KEYBOARD, {payload.data(), size});
}

bool Esp32Link::sendConsumer(std::uint16_t usage)
{
    std::array<std::uint8_t, 2> payload{};
    const std::size_t size = gs_esp32_encode_consumer(payload.data(), payload.size(), usage);
    return size > 0 && sendWhenReady(GS_ESP32_MSG_CONSUMER, {payload.data(), size});
}

bool Esp32Link::sendMouse(const gs_esp32_mouse& mouse)
{
    std::array<std::uint8_t, 6> payload{};
    const std::size_t size = gs_esp32_encode_mouse(payload.data(), payload.size(), &mouse);
    return size > 0 && sendWhenReady(GS_ESP32_MSG_MOUSE, {payload.data(), size});
}

bool Esp32Link::sendGamepad(const gs_esp32_gamepad& gamepad)
{
    std::array<std::uint8_t, 15> payload{};
    const std::size_t size = gs_esp32_encode_gamepad(payload.data(), payload.size(), &gamepad);
    return size > 0 && sendWhenReady(GS_ESP32_MSG_GAMEPAD, {payload.data(), size});
}

bool Esp32Link::releaseAll()
{
    return sendWhenReady(GS_ESP32_MSG_RELEASE_ALL, {});
}

bool Esp32Link::requestHello()
{
    return state() == LinkState::Ready && sendHelloRequest();
}

bool Esp32Link::ping()
{
    if (state() != LinkState::Ready) {
        return false;
    }
    const std::uint32_t token = nextToken();
    {
        const std::lock_guard lock(m_mutex);
        m_ping = std::pair{token, Clock::now()};
    }
    std::array<std::uint8_t, 4> payload{};
    const std::size_t size = gs_esp32_encode_token(payload.data(), payload.size(), token);
    return sendFrame(GS_ESP32_MSG_PING, {payload.data(), size});
}

bool Esp32Link::setWifi(std::string_view ssid, std::string_view password,
                        std::span<const std::uint8_t, GS_ESP32_KEY_SIZE> key)
{
    gs_esp32_wifi_set message{};
    if (ssid.empty() || !copyText(ssid, message.ssid) || !copyText(password, message.password)) {
        return false;
    }
    std::ranges::copy(key, message.key);
    std::array<std::uint8_t, kWifiPayload> payload{};
    const std::size_t size = gs_esp32_encode_wifi_set(payload.data(), payload.size(), &message);
    return size > 0 && sendWhenReady(GS_ESP32_MSG_WIFI_SET, {payload.data(), size});
}

bool Esp32Link::forgetWifi()
{
    return sendWhenReady(GS_ESP32_MSG_WIFI_FORGET, {});
}

bool Esp32Link::setName(std::string_view name)
{
    char text[GS_ESP32_MAX_NAME + 1]{};
    if (name.empty() || !copyText(name, text)) {
        return false;
    }
    std::array<std::uint8_t, 1 + GS_ESP32_MAX_NAME> payload{};
    const std::size_t size = gs_esp32_encode_name(payload.data(), payload.size(), text);
    return size > 0 && sendWhenReady(GS_ESP32_MSG_SET_NAME, {payload.data(), size});
}

bool Esp32Link::reboot()
{
    return sendWhenReady(GS_ESP32_MSG_REBOOT, {});
}

bool Esp32Link::enterDownload()
{
    return sendWhenReady(GS_ESP32_MSG_ENTER_DOWNLOAD, {});
}

}
