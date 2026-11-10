#pragma once

#include "TestSupport.h"

#include <gs/esp32/Port.h>

#include <gs_esp32_protocol.h>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace esp32test {

struct ReceivedFrame {
    std::uint8_t type = 0;
    Bytes payload;
};

class Journal
{
public:
    void wrote(std::span<const std::uint8_t> bytes)
    {
        const std::lock_guard lock(m_mutex);
        m_written.insert(m_written.end(), bytes.begin(), bytes.end());
    }

    void received(std::uint8_t type, Bytes payload)
    {
        const std::lock_guard lock(m_mutex);
        m_frames.push_back({type, std::move(payload)});
    }

    [[nodiscard]] std::vector<ReceivedFrame> frames() const
    {
        const std::lock_guard lock(m_mutex);
        return m_frames;
    }

    [[nodiscard]] std::vector<ReceivedFrame> framesOf(std::uint8_t type) const
    {
        std::vector<ReceivedFrame> found;
        for (const ReceivedFrame& frame : frames()) {
            if (frame.type == type) {
                found.push_back(frame);
            }
        }
        return found;
    }

    [[nodiscard]] std::size_t count(std::uint8_t type) const { return framesOf(type).size(); }

    [[nodiscard]] Bytes written() const
    {
        const std::lock_guard lock(m_mutex);
        return m_written;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<ReceivedFrame> m_frames;
    Bytes m_written;
};

class FakeBoard final : public gs::esp32::Port
{
public:
    struct Options {
        bool answers = true;
        bool datagrams = false;
        std::uint32_t features = GS_ESP32_FEATURE_USB_HID | GS_ESP32_FEATURE_WIFI;
        bool acceptFastBaud = true;
        bool hostSwitches = true;
        int probationWrites = 2;
        bool revertsOnGarbage = true;
        std::uint32_t startBaud = GS_ESP32_HANDSHAKE_BAUD;
        Bytes says;
        bool reportParts = false;
        gs_esp32_parts parts{};
    };

    explicit FakeBoard(Options options)
        : m_options(options)
        , m_boardBaud(options.startBaud)
    {
        gs_esp32_parser_reset(&m_parser);
    }

    [[nodiscard]] bool write(std::span<const std::uint8_t> bytes) override
    {
        const std::lock_guard lock(m_mutex);
        m_journal->wrote(bytes);
        if (!m_options.answers) {
            if (!m_options.says.empty()) {
                m_toHost.push_back(m_options.says);
                m_changed.notify_all();
            }
            return true;
        }
        if (m_hostBaud != m_boardBaud) {
            if (m_options.revertsOnGarbage && ++m_garbageWrites >= m_options.probationWrites) {
                m_boardBaud = GS_ESP32_HANDSHAKE_BAUD;
                m_garbageWrites = 0;
            }
            return true;
        }
        if (m_options.datagrams) {
            gs_esp32_frame frame{};
            if (gs_esp32_frame_decode(bytes.data(), bytes.size(), &frame)) {
                handle(frame);
            }
            return true;
        }
        std::size_t at = 0;
        while (at < bytes.size()) {
            at += gs_esp32_parser_feed(&m_parser, bytes.data() + at, bytes.size() - at);
            gs_esp32_frame frame{};
            while (gs_esp32_parser_next(&m_parser, &frame)) {
                handle(frame);
            }
        }
        return true;
    }

    [[nodiscard]] std::size_t read(std::span<std::uint8_t> buffer, std::chrono::milliseconds timeout) override
    {
        std::unique_lock lock(m_mutex);
        m_changed.wait_for(lock, timeout, [this] { return !m_toHost.empty() || m_interrupted; });
        if (m_toHost.empty()) {
            return 0;
        }
        if (m_options.datagrams) {
            const Bytes datagram = std::move(m_toHost.front());
            m_toHost.pop_front();
            const std::size_t size = std::min(datagram.size(), buffer.size());
            std::copy_n(datagram.begin(), size, buffer.begin());
            return size;
        }
        std::size_t size = 0;
        while (!m_toHost.empty() && size < buffer.size()) {
            Bytes& front = m_toHost.front();
            const std::size_t take = std::min(front.size(), buffer.size() - size);
            std::copy_n(front.begin(), take, buffer.begin() + static_cast<std::ptrdiff_t>(size));
            size += take;
            if (take == front.size()) {
                m_toHost.pop_front();
            } else {
                front.erase(front.begin(), front.begin() + static_cast<std::ptrdiff_t>(take));
            }
        }
        return size;
    }

    [[nodiscard]] bool setBaud(std::uint32_t baud) override
    {
        const std::lock_guard lock(m_mutex);
        if (!m_options.hostSwitches && baud != GS_ESP32_HANDSHAKE_BAUD) {
            return false;
        }
        m_hostBaud = baud;
        return true;
    }

    [[nodiscard]] bool setDtr(bool) override { return true; }
    [[nodiscard]] bool setRts(bool) override { return true; }

    void discardInput() override
    {
        const std::lock_guard lock(m_mutex);
        m_toHost.clear();
    }

    void interrupt() override
    {
        {
            const std::lock_guard lock(m_mutex);
            m_interrupted = true;
        }
        m_changed.notify_all();
    }

    [[nodiscard]] bool datagrams() const override { return m_options.datagrams; }
    [[nodiscard]] bool gone() const override { return false; }

    void setAnswering(bool answers)
    {
        const std::lock_guard lock(m_mutex);
        m_options.answers = answers;
    }

    void restart()
    {
        const std::lock_guard lock(m_mutex);
        m_boardBaud = GS_ESP32_HANDSHAKE_BAUD;
        m_garbageWrites = 0;
        m_toHost.clear();
        gs_esp32_parser_reset(&m_parser);
    }

    void pushStatus(std::uint8_t usb, std::uint8_t ble, const std::string& peer)
    {
        gs_esp32_status status{};
        status.usb = usb;
        status.ble = ble;
        std::memcpy(status.peer, peer.data(), std::min<std::size_t>(peer.size(), GS_ESP32_MAX_NAME));
        status.wifi = GS_ESP32_WIFI_OFF;
        std::array<std::uint8_t, GS_ESP32_MAX_PAYLOAD> payload{};
        const std::size_t size = gs_esp32_encode_status(payload.data(), payload.size(), &status);
        {
            const std::lock_guard lock(m_mutex);
            queue(GS_ESP32_MSG_STATUS, payload.data(), size);
        }
        m_changed.notify_all();
    }

    [[nodiscard]] std::shared_ptr<const Journal> journal() const { return m_journal; }

    [[nodiscard]] std::vector<ReceivedFrame> frames() const { return m_journal->frames(); }

    [[nodiscard]] std::vector<ReceivedFrame> framesOf(std::uint8_t type) const { return m_journal->framesOf(type); }

    [[nodiscard]] std::size_t count(std::uint8_t type) const { return m_journal->count(type); }

    [[nodiscard]] Bytes written() const { return m_journal->written(); }

    [[nodiscard]] std::uint32_t hostBaud() const
    {
        const std::lock_guard lock(m_mutex);
        return m_hostBaud;
    }

    [[nodiscard]] std::uint32_t boardBaud() const
    {
        const std::lock_guard lock(m_mutex);
        return m_boardBaud;
    }

private:
    void queue(std::uint8_t type, const std::uint8_t* payload, std::size_t size)
    {
        std::array<std::uint8_t, GS_ESP32_MAX_FRAME> frame{};
        const std::size_t length = gs_esp32_frame_encode(frame.data(), frame.size(), type, m_seq++, payload, size);
        if (length > 0) {
            m_toHost.emplace_back(frame.begin(), frame.begin() + static_cast<std::ptrdiff_t>(length));
        }
    }

    void handle(const gs_esp32_frame& frame)
    {
        m_journal->received(frame.type, Bytes(frame.payload, frame.payload + frame.length));
        std::array<std::uint8_t, GS_ESP32_MAX_PAYLOAD> payload{};
        switch (frame.type) {
        case GS_ESP32_MSG_HELLO_REQ: {
            gs_esp32_hello hello{};
            hello.protocol = GS_ESP32_PROTOCOL_VERSION;
            std::memcpy(hello.firmware, "1.0.0", 5);
            hello.chip_id = GS_ESP32_CHIP_ESP32S3;
            hello.chip_revision = 2;
            hello.features = m_options.features;
            const std::array<std::uint8_t, 6> mac{0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
            std::copy(mac.begin(), mac.end(), hello.mac);
            hello.wifi = GS_ESP32_WIFI_OFF;
            std::memcpy(hello.name, "Fake S3", 7);
            if (m_options.reportParts) {
                hello.extension = GS_ESP32_HELLO_EXTENSION;
                hello.parts = m_options.parts;
            }
            queue(GS_ESP32_MSG_HELLO, payload.data(), gs_esp32_encode_hello(payload.data(), payload.size(), &hello));
            break;
        }
        case GS_ESP32_MSG_PING: {
            std::uint32_t token = 0;
            if (gs_esp32_decode_token(frame.payload, frame.length, &token)) {
                queue(GS_ESP32_MSG_PONG, payload.data(), gs_esp32_encode_token(payload.data(), payload.size(), token));
            }
            break;
        }
        case GS_ESP32_MSG_SET_BAUD: {
            gs_esp32_set_baud request{};
            if (!gs_esp32_decode_set_baud(frame.payload, frame.length, &request)) {
                break;
            }
            if (!m_options.acceptFastBaud) {
                gs_esp32_error error{};
                error.code = GS_ESP32_ERROR_UNSUPPORTED;
                std::memcpy(error.text, "unsupported", 11);
                queue(GS_ESP32_MSG_ERROR, payload.data(), gs_esp32_encode_error(payload.data(), payload.size(), &error));
                break;
            }
            queue(GS_ESP32_MSG_PONG, payload.data(),
                  gs_esp32_encode_token(payload.data(), payload.size(), request.token));
            m_boardBaud = request.baud;
            break;
        }
        default:
            break;
        }
        m_changed.notify_all();
    }

    Options m_options;
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    gs_esp32_parser m_parser{};
    std::deque<Bytes> m_toHost;
    std::shared_ptr<Journal> m_journal = std::make_shared<Journal>();
    std::uint32_t m_hostBaud = GS_ESP32_HANDSHAKE_BAUD;
    std::uint32_t m_boardBaud = GS_ESP32_HANDSHAKE_BAUD;
    int m_garbageWrites = 0;
    std::uint8_t m_seq = 0;
    bool m_interrupted = false;
};

}
