#include "CompanionLan.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <gs/base/Log.h>
#include <gs/platform/Crypto.h>
#include <gs/platform/Thread.h>
#include <gs/protocol/Codec.h>

#include <QHostAddress>
#include <QNetworkInterface>
#include <QString>

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <variant>

namespace gs::app {

namespace {

using namespace std::chrono_literals;

constexpr std::string_view kLog = "companion";
constexpr std::chrono::milliseconds kHandshakeTimeout = 5s;
constexpr std::chrono::milliseconds kDownloadTimeout = 30s;
constexpr std::chrono::milliseconds kDrainTimeout = 2s;
constexpr std::size_t kMostConnections = 8;
constexpr std::size_t kMostOlderPhones = 16;
constexpr std::string_view kApkName = "gs-companion.apk";

constexpr std::string_view kPageEnglish =
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>GameScrcpy Companion</title>"
    "<style>body{font-family:sans-serif;margin:2em;line-height:1.5;color:#1b1c1f}"
    "a{display:inline-block;margin-top:1em;padding:.8em 1.4em;background:#ff6b3d;color:#fff;border-radius:8px;"
    "text-decoration:none}</style></head><body><h1>GameScrcpy Companion</h1>"
    "<p>GameScrcpy on your computer controls this phone. The companion app shows the computer's key hints on "
    "this phone, lets you type with the computer's keyboard, and connects over Wi-Fi without USB debugging.</p>"
    "<p>Download and install it, open GameScrcpy Companion, tap Scan to connect, and scan the code on the computer "
    "again.</p><a href=\"";
constexpr std::string_view kPageEnglishEnd = "\">Download GameScrcpy Companion</a></body></html>\n";
constexpr std::string_view kPageChinese =
    "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>GameScrcpy 伴侣</title>"
    "<style>body{font-family:sans-serif;margin:2em;line-height:1.6;color:#1b1c1f}"
    "a{display:inline-block;margin-top:1em;padding:.8em 1.4em;background:#ff6b3d;color:#fff;border-radius:8px;"
    "text-decoration:none}</style></head><body><h1>GameScrcpy 伴侣</h1>"
    "<p>电脑上的 GameScrcpy 正在控制这台手机。伴侣 App 在手机上显示键位提示、让你用电脑键盘打字，"
    "没有 USB 调试时也能经 Wi-Fi 连接。</p>"
    "<p>下载并安装后，打开 GameScrcpy 伴侣，点“扫码连接”，再扫一次电脑上的二维码。</p><a href=\"";
constexpr std::string_view kPageChineseEnd = "\">下载 GameScrcpy 伴侣</a></body></html>\n";

bool ensureWinsock() noexcept
{
    static const bool ready = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ready;
}

std::string_view trimmed(std::string_view text) noexcept
{
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
}

bool sameLetters(std::string_view left, std::string_view right) noexcept
{
    return left.size() == right.size() && std::ranges::equal(left, right, [](char a, char b) {
               const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
               return lower(a) == lower(b);
           });
}

std::span<const std::uint8_t> bytesOf(std::string_view text) noexcept
{
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

std::string headOf(std::string_view status, std::string_view type, std::size_t length, std::string_view extra)
{
    return std::format("HTTP/1.1 {}\r\nContent-Type: {}\r\nContent-Length: {}\r\n{}Cache-Control: no-store\r\n"
                       "X-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",
                       status, type, length, extra);
}

class LanSocket final
{
public:
    explicit LanSocket(SOCKET handle) noexcept
        : m_handle(handle)
    {
        const BOOL noDelay = TRUE;
        setsockopt(m_handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
    }

    ~LanSocket() { closesocket(m_handle); }
    LanSocket(const LanSocket&) = delete;
    LanSocket& operator=(const LanSocket&) = delete;

    void setTimeouts(std::chrono::milliseconds read, std::chrono::milliseconds write) noexcept
    {
        const auto option = [this](int name, std::chrono::milliseconds timeout) {
            const auto value = static_cast<DWORD>(std::clamp<std::chrono::milliseconds::rep>(timeout.count(), 0, MAXDWORD));
            setsockopt(m_handle, SOL_SOCKET, name, reinterpret_cast<const char*>(&value), sizeof(value));
        };
        option(SO_RCVTIMEO, read);
        option(SO_SNDTIMEO, write);
    }

    [[nodiscard]] bool writeAll(std::span<const std::uint8_t> bytes) noexcept
    {
        while (!bytes.empty()) {
            const int length = static_cast<int>(std::min<std::size_t>(bytes.size(), INT_MAX));
            const int sent = ::send(m_handle, reinterpret_cast<const char*>(bytes.data()), length, 0);
            if (sent <= 0) {
                return false;
            }
            bytes = bytes.subspan(static_cast<std::size_t>(sent));
        }
        return true;
    }

    [[nodiscard]] std::size_t readSome(std::span<std::uint8_t> bytes) noexcept
    {
        const int length = static_cast<int>(std::min<std::size_t>(bytes.size(), INT_MAX));
        const int received = ::recv(m_handle, reinterpret_cast<char*>(bytes.data()), length, 0);
        m_timedOut = received == SOCKET_ERROR && WSAGetLastError() == WSAETIMEDOUT;
        return received > 0 ? static_cast<std::size_t>(received) : 0;
    }

    [[nodiscard]] bool readExact(std::span<std::uint8_t> bytes) noexcept
    {
        while (!bytes.empty()) {
            const std::size_t received = readSome(bytes);
            if (received == 0) {
                return false;
            }
            bytes = bytes.subspan(received);
        }
        return true;
    }

    [[nodiscard]] bool timedOut() const noexcept { return m_timedOut; }

    void interrupt() noexcept { shutdown(m_handle, SD_BOTH); }

    void finish() noexcept
    {
        shutdown(m_handle, SD_SEND);
        setTimeouts(kDrainTimeout, kDrainTimeout);
        std::array<std::uint8_t, 512> scratch{};
        while (readSome(scratch) > 0) {
        }
    }

private:
    SOCKET m_handle;
    std::atomic<bool> m_timedOut{false};
};

class LanChannel final : public CompanionChannel
{
public:
    LanChannel(std::shared_ptr<LanSocket> socket, const LanKey& session) noexcept
        : m_socket(std::move(socket)), m_sealer(session, LanDirection::ToCompanion),
          m_opener(session, LanDirection::ToComputer)
    {
    }

    void setTimeouts(std::chrono::milliseconds read, std::chrono::milliseconds write) override
    {
        m_socket->setTimeouts(read, write);
    }

    bool send(std::span<const std::uint8_t> frame) override { return m_socket->writeAll(m_sealer.seal(frame)); }

    CompanionRead receive(std::vector<std::uint8_t>& frame) override
    {
        LanPrefix prefix{};
        if (!m_socket->readExact(prefix)) {
            return ended();
        }
        const auto length = LanOpener::recordLength(prefix);
        if (!length) {
            return CompanionRead::Malformed;
        }
        std::vector<std::uint8_t> body(*length);
        if (!m_socket->readExact(body)) {
            return ended();
        }
        auto opened = m_opener.open(prefix, body);
        if (!opened) {
            return CompanionRead::Malformed;
        }
        const auto header =
            protocol::decodeFrameHeader(protocol::FrameContext::Companion, protocol::Direction::ToClient, *opened);
        if (!header || opened->size() != protocol::kFrameHeaderSize + header->length) {
            return CompanionRead::Malformed;
        }
        frame = std::move(*opened);
        return CompanionRead::Frame;
    }

    void interrupt() override { m_socket->interrupt(); }

private:
    [[nodiscard]] CompanionRead ended() const noexcept
    {
        return m_socket->timedOut() ? CompanionRead::TimedOut : CompanionRead::Closed;
    }

    std::shared_ptr<LanSocket> m_socket;
    LanSealer m_sealer;
    LanOpener m_opener;
};

std::optional<std::string> readHttpHead(LanSocket& socket, std::string text)
{
    std::array<std::uint8_t, 1024> buffer{};
    while (text.find("\r\n\r\n") == std::string::npos) {
        if (text.size() > kHttpHeadLimit) {
            return std::nullopt;
        }
        const std::size_t received = socket.readSome(buffer);
        if (received == 0) {
            return std::nullopt;
        }
        text.append(reinterpret_cast<const char*>(buffer.data()), received);
    }
    return text;
}

struct Greeting {
    std::optional<protocol::CompanionWelcome> welcome;
    bool olderApp = false;
};

Greeting provenWelcome(CompanionChannel& channel, std::uint16_t version)
{
    protocol::CompanionHello hello;
    hello.version = version;
    const auto frame = protocol::encodeFrame(hello);
    if (!frame || !channel.send(*frame)) {
        return {};
    }
    std::vector<std::uint8_t> reply;
    if (channel.receive(reply) != CompanionRead::Frame) {
        return {};
    }
    const auto message = protocol::decodeToClientFrame(protocol::FrameContext::Companion, reply);
    if (!message) {
        return {};
    }
    if (const auto* welcome = std::get_if<protocol::CompanionWelcome>(&*message)) {
        return {.welcome = *welcome};
    }
    return {.olderApp = version > 1 && companionRefusesVersion(*message)};
}

struct MdnsInstance {
    LPWSTR instanceName;
    LPWSTR hostName;
    DWORD* ip4Address;
    void* ip6Address;
    WORD port;
    WORD priority;
    WORD weight;
    DWORD propertyCount;
    PWSTR* keys;
    PWSTR* values;
    DWORD interfaceIndex;
};

using MdnsComplete = void(WINAPI*)(DWORD status, PVOID context, MdnsInstance* instance);

struct MdnsRequest {
    ULONG version;
    ULONG interfaceIndex;
    MdnsInstance* instance;
    MdnsComplete completed;
    PVOID context;
    HANDLE credentials;
    BOOL unicastEnabled;
};

using MdnsConstruct = MdnsInstance*(WINAPI*)(PCWSTR name, PCWSTR host, DWORD* ip4, void* ip6, WORD port,
                                              WORD priority, WORD weight, DWORD count, PCWSTR* keys, PCWSTR* values);
using MdnsRegister = DWORD(WINAPI*)(MdnsRequest* request, void* cancel);
using MdnsFree = void(WINAPI*)(MdnsInstance* instance);

constexpr ULONG kMdnsRequestVersion = 1;
constexpr DWORD kMdnsPending = 9506;

std::atomic<MdnsFree> g_mdnsFree{nullptr};

void WINAPI mdnsCompleted(DWORD status, PVOID, MdnsInstance* instance)
{
    if (instance != nullptr) {
        if (const MdnsFree release = g_mdnsFree.load()) {
            release(instance);
        }
    }
    if (status != ERROR_SUCCESS) {
        log::info(kLog, "an mDNS announcement for the companion finished with status {}", status);
    }
}

std::wstring widened(std::string_view ascii)
{
    return {ascii.begin(), ascii.end()};
}

std::wstring dnsHostName()
{
    std::array<wchar_t, 256> name{};
    DWORD size = static_cast<DWORD>(name.size());
    if (GetComputerNameExW(ComputerNameDnsHostname, name.data(), &size) == FALSE || size == 0) {
        return L"gamescrcpy.local";
    }
    return std::wstring(name.data(), size) + L".local";
}

HttpRoute routeAmong(const HttpRequest& request, const std::vector<std::string>& tokens, const CompanionApk* apk,
                     std::string& token)
{
    for (const std::string& known : tokens) {
        const HttpRoute found = routeOf(request, known);
        if (found == HttpRoute::NotFound) {
            continue;
        }
        if (found == HttpRoute::Apk && (apk == nullptr || apk->bytes.empty())) {
            return HttpRoute::NotFound;
        }
        token = known;
        return found;
    }
    return HttpRoute::NotFound;
}

std::string_view routeName(HttpRoute route) noexcept
{
    switch (route) {
    case HttpRoute::Page:
        return "the download page";
    case HttpRoute::Apk:
        return "the companion app";
    case HttpRoute::NotFound:
        break;
    }
    return "something unknown";
}

}

std::optional<HttpRequest> parseHttpRequest(std::string_view head)
{
    const std::size_t lineEnd = head.find("\r\n");
    if (lineEnd == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view line = head.substr(0, lineEnd);
    const std::size_t methodEnd = line.find(' ');
    const std::size_t targetEnd = methodEnd == std::string_view::npos ? std::string_view::npos : line.find(' ', methodEnd + 1);
    if (methodEnd == std::string_view::npos || targetEnd == std::string_view::npos ||
        !line.substr(targetEnd + 1).starts_with("HTTP/")) {
        return std::nullopt;
    }
    HttpRequest request;
    request.method = line.substr(0, methodEnd);
    request.target = line.substr(methodEnd + 1, targetEnd - methodEnd - 1);
    std::string_view rest = head.substr(lineEnd + 2);
    while (!rest.empty()) {
        const std::size_t end = rest.find("\r\n");
        const std::string_view header = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 2);
        const std::size_t colon = header.find(':');
        if (colon == std::string_view::npos || !sameLetters(trimmed(header.substr(0, colon)), "accept-language")) {
            continue;
        }
        const std::string_view value = trimmed(header.substr(colon + 1));
        const std::string_view preferred = trimmed(value.substr(0, value.find_first_of(",;")));
        request.chinese = preferred.size() >= 2 && sameLetters(preferred.substr(0, 2), "zh");
    }
    return request;
}

HttpRoute routeOf(const HttpRequest& request, std::string_view token)
{
    if ((request.method != "GET" && request.method != "HEAD") || token.empty()) {
        return HttpRoute::NotFound;
    }
    const std::string page = std::format("/c/{}", token);
    const std::string_view target = std::string_view(request.target).substr(0, request.target.find('?'));
    if (target == page || target == page + "/") {
        return HttpRoute::Page;
    }
    if (target == std::format("{}/{}", page, kApkName)) {
        return HttpRoute::Apk;
    }
    return HttpRoute::NotFound;
}

std::string companionPage(std::string_view token, bool chinese)
{
    const std::string link = std::format("/c/{}/{}", token, kApkName);
    std::string page(chinese ? kPageChinese : kPageEnglish);
    page += link;
    page += chinese ? kPageChineseEnd : kPageEnglishEnd;
    return page;
}

std::vector<std::uint8_t> httpResponse(const HttpRequest& request, const std::vector<std::string>& tokens,
                                       const CompanionApk* apk)
{
    std::string token;
    const HttpRoute route = routeAmong(request, tokens, apk, token);
    std::string text;
    std::string head;
    std::span<const std::uint8_t> body;
    switch (route) {
    case HttpRoute::Page:
        text = companionPage(token, request.chinese);
        head = headOf("200 OK", "text/html; charset=utf-8", text.size(), {});
        body = bytesOf(text);
        break;
    case HttpRoute::Apk:
        head = headOf("200 OK", "application/vnd.android.package-archive", apk->bytes.size(),
                      std::format("Content-Disposition: attachment; filename=\"{}\"\r\n", kApkName));
        body = apk->bytes;
        break;
    case HttpRoute::NotFound:
        text = "Not found\n";
        head = headOf("404 Not Found", "text/plain; charset=utf-8", text.size(), {});
        body = bytesOf(text);
        break;
    }
    std::vector<std::uint8_t> out(head.begin(), head.end());
    if (request.method != "HEAD") {
        out.insert(out.end(), body.begin(), body.end());
    }
    return out;
}

std::string mdnsInstanceName(const LanId& pc)
{
    return "gs-" + platform::toHex(pc);
}

std::string lanAddress()
{
    int bestScore = 0;
    QString best;
    for (const QNetworkInterface& network : QNetworkInterface::allInterfaces()) {
        const QNetworkInterface::InterfaceFlags flags = network.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning) ||
            flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        int score = network.type() == QNetworkInterface::Wifi       ? 30
                    : network.type() == QNetworkInterface::Ethernet ? 20
                                                                    : 10;
        const QString name = network.humanReadableName().toLower();
        for (const char* virtualName : {"vethernet", "vmware", "virtualbox", "hyper-v", "wsl", "docker", "vpn", "tap"}) {
            if (name.contains(QLatin1String(virtualName))) {
                score -= 15;
                break;
            }
        }
        for (const QNetworkAddressEntry& entry : network.addressEntries()) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol || ip.isLoopback() || ip.isLinkLocal()) {
                continue;
            }
            const quint32 v4 = ip.toIPv4Address();
            const bool privateRange = (v4 >> 24U) == 10U || (v4 >> 20U) == 0xAC1U || (v4 >> 16U) == 0xC0A8U;
            const int total = score + (privateRange ? 5 : 0);
            if (total > bestScore) {
                bestScore = total;
                best = ip.toString();
            }
        }
    }
    return best.toStdString();
}

class CompanionLan::Announcer final
{
public:
    Announcer()
    {
        HMODULE dns = LoadLibraryExW(L"dnsapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (dns == nullptr) {
            return;
        }
        m_construct = reinterpret_cast<MdnsConstruct>(reinterpret_cast<void*>(GetProcAddress(dns, "DnsServiceConstructInstance")));
        m_register = reinterpret_cast<MdnsRegister>(reinterpret_cast<void*>(GetProcAddress(dns, "DnsServiceRegister")));
        m_deregister = reinterpret_cast<MdnsRegister>(reinterpret_cast<void*>(GetProcAddress(dns, "DnsServiceDeRegister")));
        g_mdnsFree = reinterpret_cast<MdnsFree>(reinterpret_cast<void*>(GetProcAddress(dns, "DnsServiceFreeInstance")));
        m_host = dnsHostName();
        if (m_construct == nullptr || m_register == nullptr || m_deregister == nullptr || g_mdnsFree.load() == nullptr) {
            log::info(kLog, "this Windows cannot announce over mDNS; the companion reconnects to the last address only");
        }
    }

    ~Announcer() { announce({}, 0); }
    Announcer(const Announcer&) = delete;
    Announcer& operator=(const Announcer&) = delete;

    void announce(const std::set<std::string>& names, std::uint16_t port)
    {
        const std::lock_guard lock(m_mutex);
        for (auto it = m_registered.begin(); it != m_registered.end();) {
            if (names.contains(it->first) && it->second->port == port) {
                ++it;
                continue;
            }
            if (m_deregister != nullptr) {
                static_cast<void>(m_deregister(&it->second->request, nullptr));
            }
            m_retired.push_back(std::move(it->second));
            it = m_registered.erase(it);
        }
        if (m_construct == nullptr || m_register == nullptr || port == 0) {
            return;
        }
        const std::string address = lanAddress();
        in_addr ip{};
        const bool haveIp = !address.empty() && inet_pton(AF_INET, address.c_str(), &ip) == 1;
        for (const std::string& name : names) {
            if (m_registered.contains(name)) {
                continue;
            }
            auto entry = std::make_unique<Entry>();
            entry->service = widened(std::format("{}.{}.local", name, kMdnsService));
            entry->ip = ip.S_un.S_addr;
            entry->port = port;
            entry->instance = m_construct(entry->service.c_str(), m_host.c_str(), haveIp ? &entry->ip : nullptr, nullptr,
                                          port, 0, 0, 0, nullptr, nullptr);
            if (entry->instance == nullptr) {
                continue;
            }
            entry->request = MdnsRequest{.version = kMdnsRequestVersion,
                                         .interfaceIndex = 0,
                                         .instance = entry->instance,
                                         .completed = &mdnsCompleted,
                                         .context = nullptr,
                                         .credentials = nullptr,
                                         .unicastEnabled = FALSE};
            if (const DWORD status = m_register(&entry->request, nullptr); status != kMdnsPending) {
                log::info(kLog, "the companion could not be announced over mDNS (status {})", status);
                m_retired.push_back(std::move(entry));
                continue;
            }
            log::info(kLog, "announcing {} over mDNS on port {}", name, port);
            m_registered.emplace(name, std::move(entry));
        }
    }

private:
    struct Entry {
        std::wstring service;
        DWORD ip = 0;
        std::uint16_t port = 0;
        MdnsInstance* instance = nullptr;
        MdnsRequest request{};
    };

    std::mutex m_mutex;
    MdnsConstruct m_construct = nullptr;
    MdnsRegister m_register = nullptr;
    MdnsRegister m_deregister = nullptr;
    std::wstring m_host;
    std::map<std::string, std::unique_ptr<Entry>> m_registered;
    std::vector<std::unique_ptr<Entry>> m_retired;
};

struct CompanionLan::Slot {
    std::mutex mutex;
    std::shared_ptr<LanSocket> socket;
    std::atomic<bool> done{false};
    std::jthread thread;
};

CompanionLan::CompanionLan(std::shared_ptr<const CompanionApk> apk, Arrived arrived, std::uint16_t port,
                           bool loopbackOnly)
    : m_apk(std::move(apk)), m_arrived(std::move(arrived)), m_loopbackOnly(loopbackOnly)
{
    if (!ensureWinsock()) {
        log::warning(kLog, "the companion cannot listen on the local network: Winsock did not start");
        return;
    }
    const SOCKET listener =
        WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (listener == INVALID_SOCKET) {
        return;
    }
    const BOOL exclusive = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(m_loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY);
    address.sin_port = htons(port);
    bool bound = bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
    if (!bound && port != 0) {
        address.sin_port = 0;
        bound = bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
    }
    if (!bound || listen(listener, static_cast<int>(kMostConnections)) != 0) {
        log::warning(kLog, "the companion cannot listen on the local network (error {})", WSAGetLastError());
        closesocket(listener);
        return;
    }
    int size = sizeof(address);
    getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size);
    m_port = ntohs(address.sin_port);
    m_listener = static_cast<std::uintptr_t>(listener);
    if (!m_loopbackOnly) {
        m_announcer = std::make_unique<Announcer>();
    }
    log::info(kLog, "listening for the companion on port {}{}", m_port, m_loopbackOnly ? " of this computer only" : "");
    m_acceptor = std::jthread([this] { acceptLoop(); });
}

CompanionLan::~CompanionLan()
{
    m_closing = true;
    if (m_listener != kNoSocket) {
        closesocket(static_cast<SOCKET>(m_listener));
    }
    if (m_acceptor.joinable()) {
        m_acceptor.join();
    }
    std::vector<std::shared_ptr<Slot>> taken;
    {
        const std::lock_guard lock(m_mutex);
        taken.swap(m_slots);
    }
    for (const std::shared_ptr<Slot>& slot : taken) {
        const std::lock_guard lock(slot->mutex);
        if (slot->socket != nullptr) {
            slot->socket->interrupt();
        }
    }
    for (const std::shared_ptr<Slot>& slot : taken) {
        if (slot->thread.joinable()) {
            slot->thread.join();
        }
    }
    m_announcer.reset();
    if (m_listener != kNoSocket) {
        log::info(kLog, "stopped listening for the companion");
    }
}

void CompanionLan::expect(std::vector<LanExpectation> expectations)
{
    std::set<std::string> names;
    for (const LanExpectation& expectation : expectations) {
        if (expectation.announce && expectation.paired) {
            names.insert(mdnsInstanceName(expectation.paired->pc));
        }
    }
    {
        const std::lock_guard lock(m_mutex);
        m_expected = std::move(expectations);
    }
    if (m_announcer != nullptr) {
        m_announcer->announce(names, m_port);
    }
}

void CompanionLan::acceptLoop()
{
    platform::setCurrentThreadName("gs-companion-lan");
    const auto listener = static_cast<SOCKET>(m_listener);
    for (;;) {
        const SOCKET client = accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            const int error = WSAGetLastError();
            if (m_closing || error == WSAENOTSOCK || error == WSAEINVAL || error == WSAEINTR) {
                return;
            }
            continue;
        }
        auto slot = std::make_shared<Slot>();
        slot->socket = std::make_shared<LanSocket>(client);
        {
            const std::lock_guard lock(m_mutex);
            std::erase_if(m_slots, [](const std::shared_ptr<Slot>& open) { return open->done.load(); });
            if (m_slots.size() >= kMostConnections) {
                continue;
            }
            m_slots.push_back(slot);
        }
        Slot* const serving = slot.get();
        slot->thread = std::jthread([this, serving] {
            serve(*serving);
            serving->done = true;
        });
    }
}

void CompanionLan::serve(Slot& slot)
{
    std::shared_ptr<LanSocket> socket;
    {
        const std::lock_guard lock(slot.mutex);
        socket = slot.socket;
    }
    if (socket == nullptr) {
        return;
    }
    socket->setTimeouts(kHandshakeTimeout, kHandshakeTimeout);
    std::array<std::uint8_t, kLanHelloHead> top{};
    if (!socket->readExact(std::span(top).first<4>())) {
        return;
    }
    if (!std::equal(kLanMagic.begin(), kLanMagic.end(), top.begin())) {
        const auto head = readHttpHead(*socket, std::string(top.begin(), top.begin() + 4));
        const std::optional<HttpRequest> request = head ? parseHttpRequest(*head) : std::nullopt;
        if (!request) {
            return;
        }
        std::vector<std::string> tokens;
        {
            const std::lock_guard lock(m_mutex);
            for (const LanExpectation& expectation : m_expected) {
                if (!expectation.token.empty()) {
                    tokens.push_back(expectation.token);
                }
            }
        }
        std::string token;
        const HttpRoute route = routeAmong(*request, tokens, m_apk.get(), token);
        const std::vector<std::uint8_t> response = httpResponse(*request, tokens, m_apk.get());
        socket->setTimeouts(kHandshakeTimeout, kDownloadTimeout);
        const bool sent = socket->writeAll(response);
        socket->finish();
        log::info(kLog, "a phone on the local network asked for {} ({} bytes{})", routeName(route), response.size(),
                  sent ? "" : ", not all sent");
        return;
    }
    if (!socket->readExact(std::span(top).subspan<4>())) {
        return;
    }
    const auto size = lanHelloSize(top);
    if (!size) {
        if (size.error() == LanError::VersionMismatch) {
            static_cast<void>(socket->writeAll(encodeLanReply({.status = LanStatus::VersionMismatch})));
            socket->finish();
        }
        log::info(kLog, "refused a companion on the local network: {}", lanErrorName(size.error()));
        return;
    }
    std::vector<std::uint8_t> hello(*size);
    std::ranges::copy(top, hello.begin());
    if (!socket->readExact(std::span(hello).subspan(kLanHelloHead))) {
        return;
    }
    const auto decoded = decodeLanHello(hello);
    if (!decoded) {
        return;
    }
    std::optional<LanArrival> arrival;
    LanKey key{};
    LanId pc{};
    {
        const std::lock_guard lock(m_mutex);
        for (const LanExpectation& expectation : m_expected) {
            if (!decoded->token.empty() && expectation.token == decoded->token) {
                key = expectation.qrKey;
                pc = newLanId();
                arrival = LanArrival{.serial = expectation.serial,
                                     .paired = LanPairing{.phone = decoded->phone, .key = key, .pc = pc}};
                break;
            }
            if (decoded->token.empty() && expectation.paired && expectation.paired->phone == decoded->phone) {
                key = expectation.paired->key;
                pc = expectation.paired->pc;
                arrival = LanArrival{.serial = expectation.serial};
                break;
            }
        }
        const auto older = std::ranges::find(m_older, decoded->phone, &OlderPhone::phone);
        if (!arrival && decoded->token.empty() && older != m_older.end() && older->pairing &&
            std::ranges::any_of(m_expected, [&older](const LanExpectation& expectation) {
                return expectation.serial == older->serial && !expectation.token.empty();
            })) {
            key = older->pairing->key;
            pc = older->pairing->pc;
            arrival = LanArrival{.serial = older->serial, .paired = older->pairing};
        }
    }
    if (!arrival) {
        static_cast<void>(socket->writeAll(encodeLanReply({.status = LanStatus::Unknown})));
        socket->finish();
        log::info(kLog, "refused a companion on the local network: {}",
                  decoded->token.empty() ? "this phone is not paired" : "the code is not the one shown");
        return;
    }
    LanNonce nonce{};
    platform::randomBytes(nonce);
    const std::vector<std::uint8_t> reply = encodeLanReply({.status = LanStatus::Accepted, .pc = pc, .nonce = nonce});
    if (!socket->writeAll(reply)) {
        return;
    }
    auto channel = std::make_unique<LanChannel>(socket, lanSessionKey(key, hello, reply));
    bool older = false;
    {
        const std::lock_guard lock(m_mutex);
        older = std::ranges::find(m_older, decoded->phone, &OlderPhone::phone) != m_older.end();
    }
    const Greeting greeting = provenWelcome(*channel, older ? std::uint16_t{1} : protocol::kCompanionVersion);
    const std::optional<protocol::CompanionWelcome>& welcome = greeting.welcome;
    if (!welcome) {
        socket->interrupt();
        if (greeting.olderApp) {
            const std::lock_guard lock(m_mutex);
            std::erase_if(m_older, [&decoded](const OlderPhone& phone) { return phone.phone == decoded->phone; });
            if (m_older.size() >= kMostOlderPhones) {
                m_older.erase(m_older.begin());
            }
            m_older.push_back({.phone = decoded->phone, .serial = arrival->serial, .pairing = arrival->paired});
            log::info(kLog, "[{}] the companion on the local network speaks only version 1 of its protocol; it is "
                            "greeted with version 1 when it comes back",
                      arrival->serial);
            return;
        }
        log::info(kLog, "[{}] turned away a companion on the local network: it did not prove it holds the key",
                  arrival->serial);
        return;
    }
    {
        const std::lock_guard lock(slot.mutex);
        slot.socket.reset();
    }
    arrival->channel = std::move(channel);
    arrival->welcome = *welcome;
    log::info(kLog, "[{}] a companion {} over the local network", arrival->serial,
              arrival->paired ? "paired" : "came back");
    if (m_arrived) {
        m_arrived(std::move(*arrival));
    }
}

}
