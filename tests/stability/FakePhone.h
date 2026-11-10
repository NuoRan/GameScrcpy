#pragma once

#include "fakes/FakeDeviceServer.h"
#include "unit/adb/FakeAdbServer.h"

#include "gs/adb/ShellProtocol.h"
#include "gs/protocol/Constants.h"
#include "gs/session/SessionState.h"

#include <winsock2.h>

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace gs::stability {

class FakePhone
{
public:
    FakePhone(session::test::FakeDeviceServer& device, std::string serial)
        : m_device(device), m_serial(std::move(serial))
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

    ~FakePhone()
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

    FakePhone(const FakePhone&) = delete;
    FakePhone& operator=(const FakePhone&) = delete;

    [[nodiscard]] std::uint16_t port() const noexcept { return m_port; }

    [[nodiscard]] int launches() const
    {
        const std::lock_guard lock(m_mutex);
        return m_launches;
    }

    [[nodiscard]] int pushes() const
    {
        const std::lock_guard lock(m_mutex);
        return m_pushes;
    }

    [[nodiscard]] std::vector<std::string> unexpected() const
    {
        const std::lock_guard lock(m_mutex);
        return m_unexpected;
    }

private:
    static constexpr std::chrono::milliseconds kPoll{20};
    static constexpr std::uint32_t kMostSyncBytes = 64 * 1024;
    static constexpr std::uint32_t kMostStdinBytes = 64 * 1024;
    static constexpr std::uint32_t kRegularFile = 0100644;
    static constexpr std::size_t kSyncHeaderSize = 8;

    using Connection = adb::test::FakeConnection;

    static std::string le32(std::uint32_t value)
    {
        std::string bytes(4, '\0');
        for (std::size_t i = 0; i < 4; ++i) {
            bytes[i] = static_cast<char>((value >> (8 * i)) & 0xFFU);
        }
        return bytes;
    }

    static std::uint32_t loadLe32(std::string_view bytes)
    {
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])) << (8 * i);
        }
        return value;
    }

    static std::string shellPacket(adb::ShellPacketId id, std::string_view data)
    {
        std::string packet(1, static_cast<char>(id));
        packet += le32(static_cast<std::uint32_t>(data.size()));
        packet += data;
        return packet;
    }

    static std::string exitPacket() { return shellPacket(adb::ShellPacketId::Exit, std::string_view("\0", 1)); }

    static std::optional<session::Token> parseToken(std::string_view hex)
    {
        session::Token token{};
        if (hex.size() != token.size() * 2) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < token.size(); ++i) {
            const char* begin = hex.data() + 2 * i;
            const auto [end, error] = std::from_chars(begin, begin + 2, token[i], 16);
            if (error != std::errc{} || end != begin + 2) {
                return std::nullopt;
            }
        }
        return token;
    }

    static bool closedByPeer(SOCKET socket)
    {
        WSAPOLLFD entry{.fd = socket, .events = POLLRDNORM, .revents = 0};
        const int ready = WSAPoll(&entry, 1, 0);
        if (ready == 0) {
            return false;
        }
        if (ready == SOCKET_ERROR || (entry.revents & (POLLERR | POLLNVAL)) != 0) {
            return true;
        }
        std::array<char, 256> buffer{};
        return recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0) <= 0;
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
            m_open.push_back(client);
            m_handlers.emplace_back([this, client] {
                serve(client);
                finish(client);
            });
        }
    }

    void serve(SOCKET socket)
    {
        Connection connection(socket);
        const std::string request = connection.readRequest();
        const std::string routed = "host-serial:" + m_serial + ":";
        if (request == "host:devices-l") {
            connection.write("OKAY" + Connection::prefixed(m_serial + "          device product:fake model:Phone "
                                                                      "device:fake transport_id:1\n"));
        } else if (request == "host:transport:" + m_serial) {
            connection.write("OKAY");
            serveTransport(connection, socket);
        } else if (request.starts_with(routed + "forward:tcp:0;localabstract:")) {
            connection.write("OKAYOKAY" + Connection::prefixed(std::to_string(m_device.port())));
        } else if (request.starts_with(routed + "killforward:tcp:")) {
            connection.write("OKAYOKAY");
        } else if (!request.empty()) {
            note(request);
            connection.write("FAIL" + Connection::prefixed("unknown request"));
        }
    }

    void serveTransport(Connection& connection, SOCKET socket)
    {
        constexpr std::string_view shell = "shell,v2,raw:";
        const std::string service = connection.readRequest();
        if (service == "sync:") {
            connection.write("OKAY");
            serveSync(connection);
            return;
        }
        if (!service.starts_with(shell)) {
            note(service);
            connection.write("FAIL" + Connection::prefixed("unknown service"));
            return;
        }
        connection.write("OKAY");
        const std::string_view command = std::string_view(service).substr(shell.size());
        if (command.find("app_process") != std::string_view::npos) {
            serveServer(connection, socket);
            return;
        }
        if (command == "getprop ro.build.version.sdk") {
            connection.write(shellPacket(adb::ShellPacketId::Stdout, "35\n"));
        }
        connection.write(exitPacket());
    }

    void serveSync(Connection& connection)
    {
        while (true) {
            const std::string header = connection.read(kSyncHeaderSize);
            if (header.size() != kSyncHeaderSize) {
                return;
            }
            const std::string_view id = std::string_view(header).substr(0, 4);
            const std::uint32_t length = loadLe32(std::string_view(header).substr(4));
            if (id == "QUIT" || length > kMostSyncBytes) {
                return;
            }
            const std::string argument = connection.read(length);
            if (argument.size() != length) {
                return;
            }
            if (id == "STAT") {
                std::optional<std::uint32_t> size;
                {
                    const std::lock_guard lock(m_mutex);
                    if (const auto found = m_files.find(argument); found != m_files.end()) {
                        size = found->second;
                    }
                }
                connection.write("STAT" + le32(size ? kRegularFile : 0) + le32(size.value_or(0)) + le32(0));
            } else if (id == "SEND") {
                const std::optional<std::uint32_t> size = receiveFile(connection);
                if (!size) {
                    return;
                }
                {
                    const std::lock_guard lock(m_mutex);
                    m_files.insert_or_assign(argument.substr(0, argument.rfind(',')), *size);
                    ++m_pushes;
                }
                connection.write("OKAY" + le32(0));
            } else {
                note("sync " + std::string(id));
                return;
            }
        }
    }

    static std::optional<std::uint32_t> receiveFile(Connection& connection)
    {
        std::uint32_t size = 0;
        while (true) {
            const std::string header = connection.read(kSyncHeaderSize);
            if (header.size() != kSyncHeaderSize) {
                return std::nullopt;
            }
            if (header.starts_with("DONE")) {
                return size;
            }
            const std::uint32_t length = loadLe32(std::string_view(header).substr(4));
            if (!header.starts_with("DATA") || length > kMostSyncBytes || connection.read(length).size() != length) {
                return std::nullopt;
            }
            size += length;
        }
    }

    void serveServer(Connection& connection, SOCKET socket)
    {
        std::string input;
        while (input.find('\n') == std::string::npos) {
            const std::string header = connection.read(adb::kShellHeaderSize);
            if (header.size() != adb::kShellHeaderSize) {
                return;
            }
            const std::uint32_t length = loadLe32(std::string_view(header).substr(1));
            if (length > kMostStdinBytes) {
                return;
            }
            const std::string data = connection.read(length);
            if (data.size() != length) {
                return;
            }
            if (static_cast<std::uint8_t>(header[0]) == static_cast<std::uint8_t>(adb::ShellPacketId::Stdin)) {
                input += data;
            }
        }
        constexpr std::string_view prefix = "GS_TOKEN=";
        const std::string_view line = std::string_view(input).substr(0, input.find('\n'));
        const std::optional<session::Token> token =
            line.starts_with(prefix) ? parseToken(line.substr(prefix.size())) : std::nullopt;
        if (!token) {
            note("a server launch without a token");
            return;
        }
        m_device.setToken(*token);
        {
            const std::lock_guard lock(m_mutex);
            ++m_launches;
        }
        connection.write(shellPacket(adb::ShellPacketId::Stdout, std::format("GS_READY {}\n", protocol::kProtocolVersion)));
        while (!m_device.waitForReceived<protocol::Goodbye>(1, kPoll)) {
            if (stopping() || closedByPeer(socket)) {
                return;
            }
        }
        connection.write(exitPacket());
    }

    void finish(SOCKET socket)
    {
        {
            const std::lock_guard lock(m_mutex);
            std::erase(m_open, socket);
        }
        closesocket(socket);
    }

    void note(std::string what)
    {
        const std::lock_guard lock(m_mutex);
        m_unexpected.push_back(std::move(what));
    }

    [[nodiscard]] bool stopping() const
    {
        const std::lock_guard lock(m_mutex);
        return m_stopping;
    }

    session::test::FakeDeviceServer& m_device;
    const std::string m_serial;
    SOCKET m_listener = INVALID_SOCKET;
    std::uint16_t m_port = 0;
    std::thread m_acceptor;

    mutable std::mutex m_mutex;
    std::vector<std::thread> m_handlers;
    std::vector<SOCKET> m_open;
    std::map<std::string, std::uint32_t, std::less<>> m_files;
    std::vector<std::string> m_unexpected;
    int m_launches = 0;
    int m_pushes = 0;
    bool m_stopping = false;
};

}
