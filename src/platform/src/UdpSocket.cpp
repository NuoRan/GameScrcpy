#include "gs/platform/UdpSocket.h"

#include "gs/base/Log.h"
#include "gs/platform/Thread.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <qos2.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

namespace gs::platform {

namespace {

bool ensureWinsock() noexcept
{
    static const bool ready = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ready;
}

SOCKET native(std::uintptr_t socket) noexcept
{
    return static_cast<SOCKET>(socket);
}

int clampedLength(std::size_t size) noexcept
{
    return static_cast<int>(std::min<std::size_t>(size, INT_MAX));
}

int toSockaddr(const UdpEndpoint& endpoint, sockaddr_storage& storage) noexcept
{
    std::memset(&storage, 0, sizeof(storage));
    if (endpoint.v6) {
        auto& address = reinterpret_cast<sockaddr_in6&>(storage);
        address.sin6_family = AF_INET6;
        address.sin6_port = htons(endpoint.port);
        std::memcpy(&address.sin6_addr, endpoint.address.data(), 16);
        return sizeof(sockaddr_in6);
    }
    auto& address = reinterpret_cast<sockaddr_in&>(storage);
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);
    std::memcpy(&address.sin_addr, endpoint.address.data(), 4);
    return sizeof(sockaddr_in);
}

UdpEndpoint fromSockaddr(const sockaddr_storage& storage) noexcept
{
    UdpEndpoint endpoint;
    if (storage.ss_family == AF_INET6) {
        const auto& address = reinterpret_cast<const sockaddr_in6&>(storage);
        endpoint.v6 = true;
        endpoint.port = ntohs(address.sin6_port);
        std::memcpy(endpoint.address.data(), &address.sin6_addr, 16);
    } else {
        const auto& address = reinterpret_cast<const sockaddr_in&>(storage);
        endpoint.port = ntohs(address.sin_port);
        std::memcpy(endpoint.address.data(), &address.sin_addr, 4);
    }
    return endpoint;
}

constexpr std::string_view kLog = "network";
constexpr std::size_t kMaxPendingMarks = 8;

std::atomic<std::size_t> g_pendingMarks{0};
std::atomic<bool> g_markingReported{false};

void reportMarking(std::string_view what, DWORD error) noexcept
{
    if (log::isEnabled(log::Level::Debug) &&
        !g_markingReported.exchange(true, std::memory_order_relaxed)) {
        log::debug(kLog, "UDP packets are not marked as video traffic: {} (error {})", what, error);
    }
}

struct Qwave {
    decltype(&QOSCreateHandle) createHandle = nullptr;
    decltype(&QOSCloseHandle) closeHandle = nullptr;
    decltype(&QOSAddSocketToFlow) addSocketToFlow = nullptr;
    decltype(&QOSRemoveSocketFromFlow) removeSocketFromFlow = nullptr;
    DWORD loadError = ERROR_SUCCESS;

    [[nodiscard]] bool ready() const noexcept
    {
        return createHandle != nullptr && closeHandle != nullptr && addSocketToFlow != nullptr &&
               removeSocketFromFlow != nullptr;
    }
};

template <typename Function>
Function qwaveEntry(HMODULE library, const char* name) noexcept
{
    return reinterpret_cast<Function>(reinterpret_cast<void*>(GetProcAddress(library, name)));
}

const Qwave& qwave() noexcept
{
    static const Qwave api = [] {
        Qwave loaded;
        const HMODULE library = LoadLibraryExW(L"qwave.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (library == nullptr) {
            loaded.loadError = GetLastError();
            return loaded;
        }
        loaded.createHandle = qwaveEntry<decltype(&QOSCreateHandle)>(library, "QOSCreateHandle");
        loaded.closeHandle = qwaveEntry<decltype(&QOSCloseHandle)>(library, "QOSCloseHandle");
        loaded.addSocketToFlow =
            qwaveEntry<decltype(&QOSAddSocketToFlow)>(library, "QOSAddSocketToFlow");
        loaded.removeSocketFromFlow =
            qwaveEntry<decltype(&QOSRemoveSocketFromFlow)>(library, "QOSRemoveSocketFromFlow");
        if (!loaded.ready()) {
            loaded.loadError = ERROR_PROC_NOT_FOUND;
        }
        return loaded;
    }();
    return api;
}

}

std::optional<UdpEndpoint> UdpEndpoint::parse(std::string_view host, std::uint16_t port)
{
    if (host.empty() || host.size() >= INET6_ADDRSTRLEN) {
        return std::nullopt;
    }
    char text[INET6_ADDRSTRLEN]{};
    std::memcpy(text, host.data(), host.size());
    UdpEndpoint endpoint;
    endpoint.port = port;
    in_addr v4{};
    if (inet_pton(AF_INET, text, &v4) == 1) {
        std::memcpy(endpoint.address.data(), &v4, 4);
        return endpoint;
    }
    in6_addr v6{};
    if (inet_pton(AF_INET6, text, &v6) == 1) {
        endpoint.v6 = true;
        std::memcpy(endpoint.address.data(), &v6, 16);
        return endpoint;
    }
    return std::nullopt;
}

UdpEndpoint UdpEndpoint::loopback(std::uint16_t port) noexcept
{
    UdpEndpoint endpoint;
    endpoint.address = {127, 0, 0, 1};
    endpoint.port = port;
    return endpoint;
}

UdpEndpoint UdpEndpoint::any(bool v6, std::uint16_t port) noexcept
{
    UdpEndpoint endpoint;
    endpoint.v6 = v6;
    endpoint.port = port;
    return endpoint;
}

std::string UdpEndpoint::toString() const
{
    char text[INET6_ADDRSTRLEN]{};
    if (v6) {
        in6_addr raw{};
        std::memcpy(&raw, address.data(), 16);
        inet_ntop(AF_INET6, &raw, text, sizeof(text));
        return std::format("[{}]:{}", text, port);
    }
    in_addr raw{};
    std::memcpy(&raw, address.data(), 4);
    inet_ntop(AF_INET, &raw, text, sizeof(text));
    return std::format("{}:{}", text, port);
}

struct UdpSocket::VideoFlow {
    explicit VideoFlow(SOCKET owner) noexcept : socket(owner) {}

    void attach(const UdpEndpoint& to) noexcept;
    void fail(std::string_view what, DWORD error) noexcept;
    void release() noexcept;

    const SOCKET socket;
    std::mutex mutex;
    bool open = true;
    Marking state = Marking::Pending;
    HANDLE qos = nullptr;
    QOS_FLOWID flow = 0;
};

void UdpSocket::VideoFlow::attach(const UdpEndpoint& to) noexcept
{
    const Qwave& api = qwave();
    if (!api.ready()) {
        fail("qwave.dll cannot be loaded", api.loadError);
        return;
    }
    QOS_VERSION version{.MajorVersion = 1, .MinorVersion = 0};
    HANDLE created = nullptr;
    if (api.createHandle(&version, &created) == FALSE) {
        fail("QOSCreateHandle failed", GetLastError());
        return;
    }
    sockaddr_storage storage{};
    toSockaddr(to, storage);
    std::unique_lock lock(mutex);
    if (open) {
        QOS_FLOWID added = 0;
        if (api.addSocketToFlow(created, socket, reinterpret_cast<sockaddr*>(&storage),
                                QOSTrafficTypeAudioVideo, QOS_NON_ADAPTIVE_FLOW, &added) != FALSE) {
            qos = created;
            flow = added;
            state = Marking::Video;
            return;
        }
        const DWORD error = GetLastError();
        state = Marking::Failed;
        reportMarking("QOSAddSocketToFlow failed", error);
    }
    lock.unlock();
    api.closeHandle(created);
}

void UdpSocket::VideoFlow::fail(std::string_view what, DWORD error) noexcept
{
    const std::lock_guard lock(mutex);
    state = Marking::Failed;
    if (open) {
        reportMarking(what, error);
    }
}

void UdpSocket::VideoFlow::release() noexcept
{
    HANDLE owned = nullptr;
    QOS_FLOWID ownedFlow = 0;
    {
        const std::lock_guard lock(mutex);
        open = false;
        owned = std::exchange(qos, nullptr);
        ownedFlow = std::exchange(flow, QOS_FLOWID{0});
    }
    if (owned != nullptr) {
        const Qwave& api = qwave();
        api.removeSocketFromFlow(owned, socket, ownedFlow, 0);
        api.closeHandle(owned);
    }
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : m_socket(std::exchange(other.m_socket, kInvalid)), m_v6(other.m_v6),
      m_interrupted(other.m_interrupted.load(std::memory_order_relaxed)),
      m_videoFlow(std::move(other.m_videoFlow))
{
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept
{
    if (this != &other) {
        close();
        m_socket = std::exchange(other.m_socket, kInvalid);
        m_v6 = other.m_v6;
        m_interrupted.store(other.m_interrupted.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_videoFlow = std::move(other.m_videoFlow);
    }
    return *this;
}

UdpSocket::~UdpSocket()
{
    close();
}

void UdpSocket::close() noexcept
{
    if (m_videoFlow != nullptr) {
        m_videoFlow->release();
        m_videoFlow.reset();
    }
    if (m_socket != kInvalid) {
        closesocket(native(m_socket));
        m_socket = kInvalid;
    }
}

std::optional<UdpSocket> UdpSocket::bind(const UdpEndpoint& local)
{
    if (!ensureWinsock()) {
        return std::nullopt;
    }
    const SOCKET handle = WSASocketW(local.v6 ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0,
                                     WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (handle == INVALID_SOCKET) {
        return std::nullopt;
    }
    UdpSocket socket(static_cast<std::uintptr_t>(handle), local.v6);
    BOOL report = FALSE;
    DWORD returned = 0;
    WSAIoctl(handle, SIO_UDP_CONNRESET, &report, sizeof(report), nullptr, 0, &returned, nullptr, nullptr);

    sockaddr_storage storage{};
    const int length = toSockaddr(local, storage);
    if (::bind(handle, reinterpret_cast<const sockaddr*>(&storage), length) != 0) {
        return std::nullopt;
    }
    return socket;
}

std::uint16_t UdpSocket::localPort() const noexcept
{
    sockaddr_storage storage{};
    int length = sizeof(storage);
    if (m_socket == kInvalid || getsockname(native(m_socket), reinterpret_cast<sockaddr*>(&storage), &length) != 0) {
        return 0;
    }
    return fromSockaddr(storage).port;
}

void UdpSocket::setReceiveTimeout(std::chrono::milliseconds timeout) noexcept
{
    const auto milliseconds = static_cast<DWORD>(std::clamp<std::chrono::milliseconds::rep>(timeout.count(), 0, MAXDWORD));
    setsockopt(native(m_socket), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&milliseconds),
               sizeof(milliseconds));
}

bool UdpSocket::setReceiveBufferSize(std::size_t bytes) noexcept
{
    const int size = clampedLength(bytes);
    return setsockopt(native(m_socket), SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&size), sizeof(size)) == 0;
}

bool UdpSocket::markAsVideo(const UdpEndpoint& to) noexcept
{
    if (m_socket == kInvalid || m_videoFlow != nullptr || to.v6 != m_v6) {
        return false;
    }
    if (g_pendingMarks.fetch_add(1, std::memory_order_relaxed) >= kMaxPendingMarks) {
        g_pendingMarks.fetch_sub(1, std::memory_order_relaxed);
        reportMarking("earlier requests to qWAVE have not finished", ERROR_BUSY);
        return false;
    }
    try {
        auto flow = std::make_shared<VideoFlow>(native(m_socket));
        std::thread([flow, to] {
            setCurrentThreadName("gs-udp-qos");
            flow->attach(to);
            g_pendingMarks.fetch_sub(1, std::memory_order_relaxed);
        }).detach();
        m_videoFlow = std::move(flow);
        return true;
    } catch (...) {
        g_pendingMarks.fetch_sub(1, std::memory_order_relaxed);
        return false;
    }
}

UdpSocket::Marking UdpSocket::marking() const noexcept
{
    if (m_videoFlow == nullptr) {
        return Marking::None;
    }
    const std::lock_guard lock(m_videoFlow->mutex);
    return m_videoFlow->state;
}

bool UdpSocket::sendTo(const UdpEndpoint& to, std::span<const std::uint8_t> datagram) noexcept
{
    if (m_interrupted.load(std::memory_order_acquire) || to.v6 != m_v6) {
        return false;
    }
    sockaddr_storage storage{};
    const int length = toSockaddr(to, storage);
    const int sent = sendto(native(m_socket), reinterpret_cast<const char*>(datagram.data()),
                            clampedLength(datagram.size()), 0, reinterpret_cast<const sockaddr*>(&storage), length);
    return sent == static_cast<int>(datagram.size());
}

std::optional<UdpSocket::Received> UdpSocket::receive(std::span<std::uint8_t> buffer) noexcept
{
    m_receiveTimedOut = false;
    while (!m_interrupted.load(std::memory_order_acquire)) {
        sockaddr_storage storage{};
        int length = sizeof(storage);
        const int received = recvfrom(native(m_socket), reinterpret_cast<char*>(buffer.data()),
                                      clampedLength(buffer.size()), 0, reinterpret_cast<sockaddr*>(&storage), &length);
        if (received >= 0) {
            return Received{static_cast<std::size_t>(received), fromSockaddr(storage)};
        }
        const int error = WSAGetLastError();
        if (error == WSAEMSGSIZE) {
            continue;
        }
        m_receiveTimedOut = error == WSAETIMEDOUT;
        break;
    }
    return std::nullopt;
}

void UdpSocket::interrupt() noexcept
{
    m_interrupted.store(true, std::memory_order_release);
    if (m_socket == kInvalid) {
        return;
    }
    shutdown(native(m_socket), SD_BOTH);
    CancelIoEx(reinterpret_cast<HANDLE>(m_socket), nullptr);
}

}
