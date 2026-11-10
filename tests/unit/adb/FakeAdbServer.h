#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace gs::adb::test {

class FakeConnection
{
public:
    explicit FakeConnection(SOCKET socket) : m_socket(socket) {}

    std::string readRequest()
    {
        const std::string header = read(4);
        if (header.size() != 4) {
            return {};
        }
        std::size_t length = 0;
        std::from_chars(header.data(), header.data() + 4, length, 16);
        return read(length);
    }

    std::string read(std::size_t size)
    {
        std::string data(size, '\0');
        std::size_t done = 0;
        while (done < size) {
            const int n = recv(m_socket, data.data() + done, static_cast<int>(size - done), 0);
            if (n <= 0) {
                data.resize(done);
                break;
            }
            done += static_cast<std::size_t>(n);
        }
        return data;
    }

    bool waitForClientClose()
    {
        char byte = 0;
        return recv(m_socket, &byte, 1, 0) <= 0;
    }

    void write(std::string_view bytes) { send(m_socket, bytes.data(), static_cast<int>(bytes.size()), 0); }
    void write(std::span<const std::uint8_t> bytes)
    {
        send(m_socket, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), 0);
    }

    static std::string prefixed(std::string_view text)
    {
        char header[5] = {};
        std::to_chars(header, header + 4, text.size(), 16);
        std::string length(header);
        return std::string(4 - length.size(), '0') + length + std::string(text);
    }

private:
    SOCKET m_socket;
};

class FakeAdbServer
{
public:
    using Handler = std::function<void(FakeConnection&)>;

    explicit FakeAdbServer(Handler handler) : m_handler(std::move(handler))
    {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
        m_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(m_listener, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        listen(m_listener, 8);
        int size = sizeof(address);
        getsockname(m_listener, reinterpret_cast<sockaddr*>(&address), &size);
        m_port = ntohs(address.sin_port);
        m_thread = std::thread([this] { serve(); });
    }

    ~FakeAdbServer()
    {
        m_stopping = true;
        closesocket(m_listener);
        m_thread.join();
        WSACleanup();
    }

    FakeAdbServer(const FakeAdbServer&) = delete;
    FakeAdbServer& operator=(const FakeAdbServer&) = delete;

    [[nodiscard]] std::uint16_t port() const { return m_port; }

    bool waitForConnections(int count, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, timeout, [&] { return m_connections >= count; });
    }

    bool waitForHandled(int count, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, timeout, [&] { return m_handled >= count; });
    }

private:
    void serve()
    {
        while (!m_stopping) {
            const SOCKET client = accept(m_listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) {
                return;
            }
            {
                const std::lock_guard lock(m_mutex);
                ++m_connections;
            }
            m_changed.notify_all();
            FakeConnection connection(client);
            m_handler(connection);
            closesocket(client);
            {
                const std::lock_guard lock(m_mutex);
                ++m_handled;
            }
            m_changed.notify_all();
        }
    }

    Handler m_handler;
    SOCKET m_listener = INVALID_SOCKET;
    std::uint16_t m_port = 0;
    std::atomic<bool> m_stopping{false};
    std::mutex m_mutex;
    std::condition_variable m_changed;
    int m_connections = 0;
    int m_handled = 0;
    std::thread m_thread;
};

class FakeStorage
{
public:
    static constexpr std::int64_t kTime = 1700000000;
    static constexpr std::uint32_t kFolderMode = 040771;
    static constexpr std::uint32_t kFileMode = 0100660;
    static constexpr std::size_t kChunk = 64U * 1024U;

    struct Node {
        bool directory = false;
        std::string content;
        std::uint32_t mode = 0;
        std::int64_t mtime = 0;
        std::optional<std::uint64_t> reportedSize;
    };

    explicit FakeStorage(std::string serial = "emulator-5556",
                         std::string features = "shell_v2,cmd,stat_v2,ls_v2,sendrecv_v2")
        : m_serial(std::move(serial)), m_features(std::move(features))
    {
        addFolder("/sdcard");
    }

    void setFeatures(std::string features)
    {
        const std::lock_guard lock(m_mutex);
        m_features = std::move(features);
    }

    void setStall(bool stall)
    {
        const std::lock_guard lock(m_mutex);
        m_stall = stall;
    }

    void addFolder(const std::string& path, std::int64_t mtime = kTime)
    {
        const std::lock_guard lock(m_mutex);
        makeFolder(path, mtime);
    }

    void addFile(const std::string& path, std::string content, std::int64_t mtime = kTime,
                 std::uint32_t mode = kFileMode)
    {
        const std::lock_guard lock(m_mutex);
        makeFolder(parentOf(path), kTime);
        m_nodes[path] = Node{.directory = false, .content = std::move(content), .mode = mode, .mtime = mtime};
    }

    void setReportedSize(const std::string& path, std::uint64_t size)
    {
        const std::lock_guard lock(m_mutex);
        m_nodes[path].reportedSize = size;
    }

    [[nodiscard]] std::optional<Node> node(const std::string& path) const
    {
        const std::lock_guard lock(m_mutex);
        const auto found = m_nodes.find(path);
        return found == m_nodes.end() ? std::nullopt : std::optional<Node>(found->second);
    }

    [[nodiscard]] bool exists(const std::string& path) const { return node(path).has_value(); }

    [[nodiscard]] std::vector<std::string> requests() const
    {
        const std::lock_guard lock(m_mutex);
        return m_requests;
    }

    [[nodiscard]] std::vector<std::string> commands() const
    {
        const std::lock_guard lock(m_mutex);
        return m_commands;
    }

    [[nodiscard]] std::vector<std::uint32_t> sentChunks() const
    {
        const std::lock_guard lock(m_mutex);
        return m_sentChunks;
    }

    [[nodiscard]] std::string sentTarget() const
    {
        const std::lock_guard lock(m_mutex);
        return m_sentTarget;
    }

    [[nodiscard]] std::uint32_t sentTime() const
    {
        const std::lock_guard lock(m_mutex);
        return m_sentTime;
    }

    void serve(FakeConnection& c)
    {
        const std::string first = c.readRequest();
        std::string serial;
        std::string features;
        {
            const std::lock_guard lock(m_mutex);
            serial = m_serial;
            features = m_features;
        }
        if (first == "host-serial:" + serial + ":features") {
            log("features");
            c.write("OKAY" + FakeConnection::prefixed(features));
            return;
        }
        if (first != "host:transport:" + serial) {
            c.write("FAIL" + FakeConnection::prefixed("device not found"));
            return;
        }
        c.write("OKAY");
        const std::string service = c.readRequest();
        if (service == "sync:") {
            c.write("OKAY");
            sync(c);
            return;
        }
        constexpr std::string_view kShell = "shell,v2,raw:";
        if (service.starts_with(kShell)) {
            const std::string command = service.substr(kShell.size());
            {
                const std::lock_guard lock(m_mutex);
                m_commands.push_back(command);
            }
            const auto [status, error] = runShell(command);
            std::string reply = "OKAY";
            if (!error.empty()) {
                reply += shellPacket(2, error);
            }
            reply += shellPacket(3, std::string(1, static_cast<char>(status)));
            c.write(reply);
            return;
        }
        c.write("FAIL" + FakeConnection::prefixed("not supported by the fake storage: " + service));
    }

    static std::vector<std::string> words(std::string_view command)
    {
        std::vector<std::string> found;
        std::string word;
        bool inWord = false;
        bool quoted = false;
        for (std::size_t i = 0; i < command.size(); ++i) {
            const char c = command[i];
            if (quoted) {
                if (c == '\'') {
                    quoted = false;
                } else {
                    word.push_back(c);
                }
                continue;
            }
            if (c == '\'') {
                quoted = true;
                inWord = true;
            } else if (c == '\\' && i + 1 < command.size()) {
                word.push_back(command[++i]);
                inWord = true;
            } else if (c == ' ') {
                if (inWord) {
                    found.push_back(word);
                    word.clear();
                    inWord = false;
                }
            } else {
                word.push_back(c);
                inWord = true;
            }
        }
        if (inWord) {
            found.push_back(word);
        }
        return found;
    }

private:
    static std::string le32(std::uint32_t value)
    {
        std::string bytes(4, '\0');
        for (std::size_t i = 0; i < 4; ++i) {
            bytes[i] = static_cast<char>((value >> (8 * i)) & 0xFFU);
        }
        return bytes;
    }

    static std::string le64(std::uint64_t value)
    {
        return le32(static_cast<std::uint32_t>(value & 0xFFFFFFFFU)) + le32(static_cast<std::uint32_t>(value >> 32U));
    }

    static std::uint32_t readLe32(FakeConnection& c)
    {
        const std::string bytes = c.read(4);
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])) << (8 * i);
        }
        return value;
    }

    static std::string shellPacket(char id, std::string_view data)
    {
        return std::string(1, id) + le32(static_cast<std::uint32_t>(data.size())) + std::string(data);
    }

    static std::string parentOf(const std::string& path)
    {
        const std::size_t slash = path.find_last_of('/');
        return slash == std::string::npos || slash == 0 ? std::string("/") : path.substr(0, slash);
    }

    static std::string nameOf(const std::string& path)
    {
        const std::size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    static bool inside(const std::string& path, const std::string& folder)
    {
        return path.size() > folder.size() + 1 && path.starts_with(folder) && path[folder.size()] == '/';
    }

    void makeFolder(const std::string& path, std::int64_t mtime)
    {
        if (path.empty() || path == "/") {
            return;
        }
        makeFolder(parentOf(path), mtime);
        if (!m_nodes.contains(path)) {
            m_nodes[path] = Node{.directory = true, .content = {}, .mode = kFolderMode, .mtime = mtime};
        }
    }

    void log(std::string text)
    {
        const std::lock_guard lock(m_mutex);
        m_requests.push_back(std::move(text));
    }

    std::uint64_t sizeOf(const Node& found) const
    {
        return found.reportedSize.value_or(found.directory ? 4096U : found.content.size());
    }

    void sync(FakeConnection& c)
    {
        for (;;) {
            const std::string id = c.read(4);
            if (id.size() < 4) {
                return;
            }
            const std::uint32_t length = readLe32(c);
            if (id == "QUIT") {
                return;
            }
            const std::string path = c.read(length);
            log(id + " " + path);
            if (id == "LIST" || id == "LIS2") {
                if (!supports(id == "LIS2" ? "ls_v2" : "")) {
                    c.write("FAIL" + le32(15) + "unknown command");
                    return;
                }
                list(c, path, id == "LIS2");
            } else if (id == "STAT") {
                const std::optional<Node> found = node(path);
                c.write("STAT" + le32(found ? found->mode : 0) +
                        le32(found ? static_cast<std::uint32_t>(sizeOf(*found) & 0xFFFFFFFFU) : 0) +
                        le32(found ? static_cast<std::uint32_t>(found->mtime) : 0));
            } else if (id == "STA2" || id == "LST2") {
                const std::optional<Node> found = node(path);
                c.write(id + le32(found ? 0U : 2U) + le64(0) + le64(0) + le32(found ? found->mode : 0U) + le32(1) +
                        le32(0) + le32(0) + le64(found ? sizeOf(*found) : 0) +
                        le64(found ? static_cast<std::uint64_t>(found->mtime) : 0) +
                        le64(found ? static_cast<std::uint64_t>(found->mtime) : 0) +
                        le64(found ? static_cast<std::uint64_t>(found->mtime) : 0));
            } else if (id == "RECV" || id == "RCV2") {
                if (id == "RCV2") {
                    const std::string setup = c.read(4);
                    const std::uint32_t flags = readLe32(c);
                    if (setup != "RCV2" || flags != 0) {
                        c.write("FAIL" + le32(13) + "unknown flags");
                        return;
                    }
                }
                if (!serveFile(c, path)) {
                    return;
                }
            } else if (id == "SEND") {
                if (!storeFile(c, path)) {
                    return;
                }
            } else {
                c.write("FAIL" + le32(15) + "unknown command");
                return;
            }
        }
    }

    bool supports(std::string_view feature) const
    {
        if (feature.empty()) {
            return true;
        }
        const std::lock_guard lock(m_mutex);
        return m_features.find(feature) != std::string::npos;
    }

    void list(FakeConnection& c, const std::string& folder, bool v2)
    {
        std::vector<std::pair<std::string, Node>> children;
        {
            const std::lock_guard lock(m_mutex);
            for (const auto& [path, found] : m_nodes) {
                if (inside(path, folder) && path.find('/', folder.size() + 1) == std::string::npos) {
                    children.emplace_back(nameOf(path), found);
                }
            }
        }
        const Node self{.directory = true, .content = {}, .mode = kFolderMode, .mtime = kTime};
        children.insert(children.begin(), {{".", self}, {"..", self}});
        std::string reply;
        for (const auto& [name, found] : children) {
            const auto size = sizeOf(found);
            if (v2) {
                reply += "DNT2" + le32(0) + le64(0) + le64(0) + le32(found.mode) + le32(1) + le32(0) + le32(0) +
                         le64(size) + le64(static_cast<std::uint64_t>(found.mtime)) +
                         le64(static_cast<std::uint64_t>(found.mtime)) + le64(static_cast<std::uint64_t>(found.mtime));
            } else {
                reply += "DENT" + le32(found.mode) + le32(static_cast<std::uint32_t>(size & 0xFFFFFFFFU)) +
                         le32(static_cast<std::uint32_t>(found.mtime));
            }
            reply += le32(static_cast<std::uint32_t>(name.size())) + name;
        }
        reply += "DONE" + std::string(v2 ? 72 : 16, '\0');
        c.write(reply);
    }

    bool serveFile(FakeConnection& c, const std::string& path)
    {
        const std::optional<Node> found = node(path);
        if (!found || found->directory) {
            const std::string reason = found ? "read failed: Is a directory" : "No such file or directory";
            c.write("FAIL" + le32(static_cast<std::uint32_t>(reason.size())) + reason);
            return true;
        }
        bool stall = false;
        {
            const std::lock_guard lock(m_mutex);
            stall = m_stall;
        }
        std::string_view rest = found->content;
        while (!rest.empty()) {
            const std::size_t size = std::min(rest.size(), kChunk);
            c.write("DATA" + le32(static_cast<std::uint32_t>(size)) + std::string(rest.substr(0, size)));
            rest.remove_prefix(size);
            if (stall) {
                c.waitForClientClose();
                return false;
            }
        }
        c.write("DONE" + le32(0));
        return true;
    }

    bool storeFile(FakeConnection& c, const std::string& target)
    {
        const std::size_t comma = target.find_last_of(',');
        const std::string path = target.substr(0, comma);
        std::uint32_t mode = 0;
        if (comma != std::string::npos) {
            const std::string text = target.substr(comma + 1);
            std::from_chars(text.data(), text.data() + text.size(), mode);
        }
        std::string content;
        std::vector<std::uint32_t> chunks;
        for (;;) {
            const std::string id = c.read(4);
            if (id.size() < 4) {
                return false;
            }
            const std::uint32_t value = readLe32(c);
            if (id == "DONE") {
                {
                    const std::lock_guard lock(m_mutex);
                    makeFolder(parentOf(path), kTime);
                    m_nodes[path] =
                        Node{.directory = false, .content = std::move(content), .mode = mode, .mtime = value};
                    m_sentTarget = target;
                    m_sentTime = value;
                    m_sentChunks = std::move(chunks);
                }
                c.write("OKAY" + le32(0));
                return true;
            }
            if (id != "DATA") {
                return false;
            }
            const std::string data = c.read(value);
            if (data.size() != value) {
                return false;
            }
            content += data;
            chunks.push_back(value);
        }
    }

    std::pair<int, std::string> runShell(const std::string& command)
    {
        const std::vector<std::string> args = words(command);
        const std::lock_guard lock(m_mutex);
        if (args.size() >= 2 && args[0] == "rm" && args[1] == "-rf") {
            for (std::size_t i = 2; i < args.size(); ++i) {
                std::erase_if(m_nodes, [&](const auto& entry) {
                    return entry.first == args[i] || inside(entry.first, args[i]);
                });
            }
            return {0, {}};
        }
        if (args.size() >= 2 && args[0] == "mkdir" && args[1] == "-p") {
            for (std::size_t i = 2; i < args.size(); ++i) {
                makeFolder(args[i], kTime);
            }
            return {0, {}};
        }
        const auto mv = std::ranges::find(args, std::string("mv"));
        if (!args.empty() && args[0] == "if" && mv != args.end() && args.end() - mv >= 3) {
            const std::string from = *(mv + 1);
            const std::string to = *(mv + 2);
            if (m_nodes.contains(to)) {
                return {1, "already exists\n"};
            }
            if (!m_nodes.contains(from)) {
                return {1, "mv: bad '" + from + "': No such file or directory\n"};
            }
            std::map<std::string, Node> moved;
            for (auto it = m_nodes.begin(); it != m_nodes.end();) {
                if (it->first == from || inside(it->first, from)) {
                    moved[to + it->first.substr(from.size())] = it->second;
                    it = m_nodes.erase(it);
                } else {
                    ++it;
                }
            }
            m_nodes.merge(moved);
            return {0, {}};
        }
        return {127, "not found\n"};
    }

    mutable std::mutex m_mutex;
    std::string m_serial;
    std::string m_features;
    bool m_stall = false;
    std::map<std::string, Node> m_nodes;
    std::vector<std::string> m_requests;
    std::vector<std::string> m_commands;
    std::vector<std::uint32_t> m_sentChunks;
    std::string m_sentTarget;
    std::uint32_t m_sentTime = 0;
};

inline std::uint16_t closedPort()
{
    WSADATA data{};
    WSAStartup(MAKEWORD(2, 2), &data);
    const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(s, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    int size = sizeof(address);
    getsockname(s, reinterpret_cast<sockaddr*>(&address), &size);
    closesocket(s);
    WSACleanup();
    return ntohs(address.sin_port);
}

}
