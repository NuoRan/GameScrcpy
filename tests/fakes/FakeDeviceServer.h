#pragma once

#include "gs/platform/Crypto.h"
#include "gs/protocol/Codec.h"
#include "gs/session/SessionState.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

namespace gs::session::test {

inline protocol::Welcome fakeWelcome()
{
    protocol::Welcome welcome;
    welcome.serverVersion = "fake";
    welcome.manufacturer = "Fake";
    welcome.model = "Device";
    welcome.androidRelease = "15";
    welcome.sdkInt = 35;
    welcome.serialNo = "FAKE0001";
    protocol::Display display;
    display.width = 1080;
    display.height = 2400;
    display.densityDpi = 420;
    display.refreshMilliHz = 60000;
    welcome.displays.push_back(display);
    return welcome;
}

struct FakeServerOptions {
    std::uint16_t protocolVersion = protocol::kProtocolVersion;
    protocol::Welcome welcome = fakeWelcome();
    std::vector<protocol::Bytes> videoPackets;
    std::vector<protocol::Bytes> audioPackets;
    bool refuseAudio = false;
    std::int64_t clockOffsetUs = 0;
};

class FakeDeviceServer
{
public:
    enum class Mode : std::uint8_t {
        Normal,
        Refuse,
        Silent,
    };

    explicit FakeDeviceServer(FakeServerOptions options = {}) : m_options(std::move(options))
    {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
        m_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(m_listener, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        listen(m_listener, 16);
        int size = sizeof(address);
        getsockname(m_listener, reinterpret_cast<sockaddr*>(&address), &size);
        m_port = ntohs(address.sin_port);
        m_acceptor = std::thread([this] { acceptLoop(); });
    }

    ~FakeDeviceServer()
    {
        {
            const std::lock_guard lock(m_mutex);
            m_stopping = true;
            for (const SOCKET open : m_open) {
                shutdown(open, SD_BOTH);
            }
        }
        closesocket(m_listener);
        m_acceptor.join();
        for (std::thread& handler : m_handlers) {
            handler.join();
        }
        WSACleanup();
    }

    FakeDeviceServer(const FakeDeviceServer&) = delete;
    FakeDeviceServer& operator=(const FakeDeviceServer&) = delete;

    [[nodiscard]] std::uint16_t port() const { return m_port; }

    void setToken(const Token& token)
    {
        const std::lock_guard lock(m_mutex);
        m_token = token;
    }

    void setMode(Mode mode)
    {
        const std::lock_guard lock(m_mutex);
        m_mode = mode;
    }

    void setAnswerPings(bool answer)
    {
        const std::lock_guard lock(m_mutex);
        m_answerPings = answer;
    }

    void dropControl()
    {
        const std::lock_guard lock(m_mutex);
        if (m_control != INVALID_SOCKET) {
            shutdown(m_control, SD_BOTH);
        }
    }

    template <typename M>
    bool sendControl(const M& message)
    {
        const auto frame = protocol::encodeFrame(message);
        return frame && sendControlBytes(*frame);
    }

    bool sendControlBytes(std::span<const std::uint8_t> bytes)
    {
        const std::lock_guard lock(m_mutex);
        return m_control != INVALID_SOCKET && writeAll(m_control, bytes);
    }

    template <typename M>
    bool sendBulk(const M& message)
    {
        const auto frame = protocol::encodeFrame(message);
        const std::lock_guard lock(m_mutex);
        return frame && m_bulk != INVALID_SOCKET && writeAll(m_bulk, *frame);
    }

    bool waitForBulkOpens(int count, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        return waitFor(timeout, [&] { return m_bulkOpens >= count; });
    }

    [[nodiscard]] int bulkOpens() const
    {
        const std::lock_guard lock(m_mutex);
        return m_bulkOpens;
    }

    template <typename M>
    [[nodiscard]] std::vector<M> received() const
    {
        const std::lock_guard lock(m_mutex);
        std::vector<M> matching;
        for (const protocol::ToServerMessage& message : m_received) {
            if (const auto* typed = std::get_if<M>(&message)) {
                matching.push_back(*typed);
            }
        }
        return matching;
    }

    [[nodiscard]] std::vector<protocol::ToServerMessage> receivedAll() const
    {
        const std::lock_guard lock(m_mutex);
        return m_received;
    }

    template <typename M>
    bool waitForReceived(std::size_t count, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        return waitFor(timeout, [&] {
            return static_cast<std::size_t>(std::ranges::count_if(
                       m_received, [](const auto& message) { return std::holds_alternative<M>(message); })) >= count;
        });
    }

    bool waitForHandshakes(int count, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        return waitFor(timeout, [&] { return m_handshakes >= count; });
    }

    bool waitForVideoOpens(int count, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        return waitFor(timeout, [&] { return m_videoOpens >= count; });
    }

    bool waitForAudioOpens(int count, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        return waitFor(timeout, [&] { return m_audioOpens >= count; });
    }

    bool waitForConnections(int count, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        return waitFor(timeout, [&] { return m_connections >= count; });
    }

    [[nodiscard]] int connections() const
    {
        const std::lock_guard lock(m_mutex);
        return m_connections;
    }

    [[nodiscard]] int handshakes() const
    {
        const std::lock_guard lock(m_mutex);
        return m_handshakes;
    }

    [[nodiscard]] int videoOpens() const
    {
        const std::lock_guard lock(m_mutex);
        return m_videoOpens;
    }

    [[nodiscard]] int audioOpens() const
    {
        const std::lock_guard lock(m_mutex);
        return m_audioOpens;
    }

    [[nodiscard]] std::vector<bool> resumedFlags() const
    {
        const std::lock_guard lock(m_mutex);
        return m_resumedFlags;
    }

private:
    static constexpr std::string_view kHelloContext = "GSCP/1 hello";
    static constexpr std::string_view kChannelContext = "GSCP/1 channel";

    static std::span<const std::uint8_t> bytesOf(std::string_view text)
    {
        return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
    }

    static bool writeAll(SOCKET socket, std::span<const std::uint8_t> bytes)
    {
        while (!bytes.empty()) {
            const int sent = send(socket, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), 0);
            if (sent <= 0) {
                return false;
            }
            bytes = bytes.subspan(static_cast<std::size_t>(sent));
        }
        return true;
    }

    static bool readExact(SOCKET socket, std::span<std::uint8_t> bytes)
    {
        while (!bytes.empty()) {
            const int received = recv(socket, reinterpret_cast<char*>(bytes.data()), static_cast<int>(bytes.size()), 0);
            if (received <= 0) {
                return false;
            }
            bytes = bytes.subspan(static_cast<std::size_t>(received));
        }
        return true;
    }

    static std::optional<protocol::ToServerMessage> readMessage(SOCKET socket, protocol::FrameContext context)
    {
        std::array<std::uint8_t, protocol::kFrameHeaderSize> head{};
        if (!readExact(socket, head)) {
            return std::nullopt;
        }
        const auto header = protocol::decodeFrameHeader(context, protocol::Direction::ToServer, head);
        if (!header) {
            return std::nullopt;
        }
        std::vector<std::uint8_t> payload(header->length);
        if (!readExact(socket, payload)) {
            return std::nullopt;
        }
        auto message = protocol::decodeToServerPayload(header->type, payload);
        if (!message) {
            return std::nullopt;
        }
        return std::move(*message);
    }

    template <typename M>
    bool write(SOCKET socket, const M& message)
    {
        const auto frame = protocol::encodeFrame(message);
        const std::lock_guard lock(m_mutex);
        return frame && writeAll(socket, *frame);
    }

    template <typename Predicate>
    bool waitFor(std::chrono::milliseconds timeout, Predicate predicate)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, timeout, predicate);
    }

    void acceptLoop()
    {
        while (true) {
            const SOCKET client = accept(m_listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) {
                return;
            }
            const std::lock_guard lock(m_mutex);
            if (m_stopping) {
                closesocket(client);
                return;
            }
            ++m_connections;
            m_open.push_back(client);
            m_handlers.emplace_back([this, client] { serve(client); });
            m_changed.notify_all();
        }
    }

    void serve(SOCKET socket)
    {
        const BOOL noDelay = TRUE;
        setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
        Mode mode = Mode::Normal;
        Token token{};
        {
            const std::lock_guard lock(m_mutex);
            mode = m_mode;
            token = m_token;
        }
        if (mode == Mode::Normal) {
            greet(socket, token);
        } else if (mode == Mode::Silent) {
            char byte = 0;
            recv(socket, &byte, 1, 0);
        }
        finish(socket);
    }

    void greet(SOCKET socket, const Token& token)
    {
        protocol::ServerHello hello;
        hello.protocolVersion = m_options.protocolVersion;
        platform::randomBytes(hello.nonce);
        if (!write(socket, hello)) {
            return;
        }
        const auto first = readMessage(socket, protocol::FrameContext::Handshake);
        if (!first) {
            return;
        }
        if (const auto* client = std::get_if<protocol::ClientHello>(&*first)) {
            if (client->proof != platform::hmacSha256(token, {bytesOf(kHelloContext), hello.nonce})) {
                reject(socket, protocol::ErrorCode::AuthenticationFailed, "wrong proof");
                return;
            }
            protocol::Welcome welcome = m_options.welcome;
            {
                const std::lock_guard lock(m_mutex);
                welcome.resumed = m_hadSession;
                m_hadSession = true;
                m_resumedFlags.push_back(welcome.resumed);
                if (m_control != INVALID_SOCKET) {
                    shutdown(m_control, SD_BOTH);
                }
                m_control = socket;
            }
            if (!write(socket, welcome)) {
                return;
            }
            {
                const std::lock_guard lock(m_mutex);
                ++m_handshakes;
            }
            m_changed.notify_all();
            serveControl(socket);
        } else if (const auto* open = std::get_if<protocol::ChannelOpen>(&*first)) {
            bool established = false;
            {
                const std::lock_guard lock(m_mutex);
                established = m_hadSession;
            }
            const std::array<std::uint8_t, 1> number{static_cast<std::uint8_t>(open->channel)};
            if (!established) {
                reject(socket, protocol::ErrorCode::UnexpectedMessage, "no session yet");
                return;
            }
            if (open->proof != platform::hmacSha256(token, {bytesOf(kChannelContext), hello.nonce, number})) {
                reject(socket, protocol::ErrorCode::AuthenticationFailed, "wrong channel proof");
                return;
            }
            const bool audio = open->channel == protocol::ChannelId::Audio;
            if (audio && m_options.refuseAudio) {
                reject(socket, protocol::ErrorCode::ChannelUnavailable, "no audio");
                return;
            }
            protocol::ChannelAccepted accepted;
            accepted.channel = open->channel;
            if (!write(socket, accepted)) {
                return;
            }
            if (open->channel == protocol::ChannelId::Bulk) {
                {
                    const std::lock_guard lock(m_mutex);
                    m_bulk = socket;
                    ++m_bulkOpens;
                }
                m_changed.notify_all();
                char byte = 0;
                recv(socket, &byte, 1, 0);
                return;
            }
            {
                const std::lock_guard lock(m_mutex);
                (audio ? m_audio : m_video) = socket;
                ++(audio ? m_audioOpens : m_videoOpens);
            }
            m_changed.notify_all();
            for (const protocol::Bytes& packet : audio ? m_options.audioPackets : m_options.videoPackets) {
                writeAll(socket, packet);
            }
            char byte = 0;
            recv(socket, &byte, 1, 0);
        }
    }

    void serveControl(SOCKET socket)
    {
        while (true) {
            auto message = readMessage(socket, protocol::FrameContext::Control);
            if (!message) {
                break;
            }
            if (const auto* ping = std::get_if<protocol::Ping>(&*message)) {
                bool answer = false;
                {
                    const std::lock_guard lock(m_mutex);
                    answer = m_answerPings;
                }
                if (answer) {
                    protocol::Pong pong;
                    pong.nonce = ping->nonce;
                    pong.clientTimeUs = ping->clientTimeUs;
                    pong.serverTimeUs = static_cast<std::uint64_t>(static_cast<std::int64_t>(ping->clientTimeUs) +
                                                                   m_options.clockOffsetUs);
                    write(socket, pong);
                }
            }
            const bool goodbye = std::holds_alternative<protocol::Goodbye>(*message);
            {
                const std::lock_guard lock(m_mutex);
                m_received.push_back(std::move(*message));
            }
            m_changed.notify_all();
            if (goodbye) {
                break;
            }
        }
        const std::lock_guard lock(m_mutex);
        if (m_control != socket) {
            return;
        }
        for (const SOCKET channel : {m_video, m_audio, m_bulk}) {
            if (channel != INVALID_SOCKET) {
                shutdown(channel, SD_BOTH);
            }
        }
    }

    void reject(SOCKET socket, protocol::ErrorCode code, std::string_view reason)
    {
        protocol::HandshakeError error;
        error.code = code;
        error.message = std::string(reason);
        write(socket, error);
    }

    void finish(SOCKET socket)
    {
        {
            const std::lock_guard lock(m_mutex);
            std::erase(m_open, socket);
            if (m_control == socket) {
                m_control = INVALID_SOCKET;
            }
            if (m_video == socket) {
                m_video = INVALID_SOCKET;
            }
            if (m_audio == socket) {
                m_audio = INVALID_SOCKET;
            }
            if (m_bulk == socket) {
                m_bulk = INVALID_SOCKET;
            }
        }
        closesocket(socket);
        m_changed.notify_all();
    }

    const FakeServerOptions m_options;
    SOCKET m_listener = INVALID_SOCKET;
    std::uint16_t m_port = 0;
    std::thread m_acceptor;

    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<std::thread> m_handlers;
    std::vector<SOCKET> m_open;
    SOCKET m_control = INVALID_SOCKET;
    SOCKET m_video = INVALID_SOCKET;
    SOCKET m_audio = INVALID_SOCKET;
    SOCKET m_bulk = INVALID_SOCKET;
    Token m_token{};
    Mode m_mode = Mode::Normal;
    bool m_answerPings = true;
    bool m_hadSession = false;
    bool m_stopping = false;
    int m_connections = 0;
    int m_handshakes = 0;
    int m_videoOpens = 0;
    int m_audioOpens = 0;
    int m_bulkOpens = 0;
    std::vector<bool> m_resumedFlags;
    std::vector<protocol::ToServerMessage> m_received;
};

}
