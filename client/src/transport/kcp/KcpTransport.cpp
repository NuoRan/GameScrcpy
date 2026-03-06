/**
 * @file KcpTransport.cpp
 * @brief KCP Transport Layer - Native Winsock2 implementation
 */

#include "KcpTransport.h"
#include <algorithm>

#define LOG_TAG "KcpTransport"
#include "Logger.h"

#ifdef _WIN32
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

static constexpr int UDP_RECV_BUFFER_SIZE = 2 * 1024 * 1024;
static constexpr int UDP_SEND_BUFFER_SIZE = 1 * 1024 * 1024;

KcpTransport::KcpTransport(uint32_t conv)
{
    m_kcp = std::make_unique<KcpCore>(conv, nullptr);
    m_kcp->setOutput([this](const char *buf, int len, void *) {
        return this->udpOutput(buf, len);
    });
    m_kcp->setFastMode();

    m_socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket != INVALID_SOCKET) {
        // Set non-blocking
        u_long nonBlocking = 1;
        ::ioctlsocket(m_socket, FIONBIO, &nonBlocking);
    }

    m_clock.start();

#ifdef _WIN32
    timeBeginPeriod(1);
#endif
}

KcpTransport::~KcpTransport()
{
    close();
#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

bool KcpTransport::bind(uint16_t port)
{
    if (m_socket == INVALID_SOCKET) {
        if (m_errorCb) m_errorCb("Invalid socket");
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(m_socket, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        if (m_errorCb) m_errorCb("Failed to bind UDP socket: " + std::to_string(WSAGetLastError()));
        return false;
    }

    // Get actual bound port
    sockaddr_in localAddr{};
    int addrLen = sizeof(localAddr);
    if (::getsockname(m_socket, reinterpret_cast<sockaddr *>(&localAddr), &addrLen) == 0) {
        m_localPort = ntohs(localAddr.sin_port);
    }

    // Set buffer sizes
    int bufSize = UDP_RECV_BUFFER_SIZE;
    ::setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&bufSize), sizeof(bufSize));
    bufSize = UDP_SEND_BUFFER_SIZE;
    ::setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char *>(&bufSize), sizeof(bufSize));

    m_active.store(true);
    m_stopRequested.store(false);
    m_ioThread = std::thread(&KcpTransport::ioLoop, this);
    return true;
}

void KcpTransport::connectTo(const std::string &address, uint16_t port)
{
    m_remoteAddr.sin_family = AF_INET;
    m_remoteAddr.sin_port = htons(port);
    inet_pton(AF_INET, address.c_str(), &m_remoteAddr.sin_addr);
    m_remotePort = port;

    // If not yet bound, bind to any port
    if (m_localPort == 0 && m_socket != INVALID_SOCKET) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = 0;
        ::bind(m_socket, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));

        sockaddr_in localAddr{};
        int addrLen = sizeof(localAddr);
        if (::getsockname(m_socket, reinterpret_cast<sockaddr *>(&localAddr), &addrLen) == 0) {
            m_localPort = ntohs(localAddr.sin_port);
        }

        int bufSize = UDP_RECV_BUFFER_SIZE;
        ::setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&bufSize), sizeof(bufSize));
        bufSize = UDP_SEND_BUFFER_SIZE;
        ::setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char *>(&bufSize), sizeof(bufSize));
    }

    if (!m_active.load()) {
        m_active.store(true);
        m_stopRequested.store(false);
        m_ioThread = std::thread(&KcpTransport::ioLoop, this);
    }

    if (m_peerConnectedCb) m_peerConnectedCb();
}

void KcpTransport::close()
{
    m_active.store(false);
    m_stopRequested.store(true);

    if (m_socket != INVALID_SOCKET) {
        ::closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }

    if (m_ioThread.joinable()) {
        m_ioThread.join();
    }

    if (m_disconnectedCb) m_disconnectedCb();
}

uint16_t KcpTransport::localPort() const
{
    return m_localPort;
}

std::string KcpTransport::remoteAddress() const
{
    char buf[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &m_remoteAddr.sin_addr, buf, sizeof(buf));
    return buf;
}

int KcpTransport::send(const char *data, int len)
{
    if (!m_kcp || !m_active.load() || len <= 0) return -1;
    int ret = m_kcp->send(data, len);
    if (ret >= 0) {
        m_kcp->update(currentMs());
    }
    return ret;
}

std::vector<uint8_t> KcpTransport::recv()
{
    if (!m_kcp) return {};
    int size = m_kcp->peekSize();
    if (size <= 0) return {};
    std::vector<uint8_t> data(size);
    int ret = m_kcp->recv(reinterpret_cast<char *>(data.data()), size);
    if (ret < 0) return {};
    data.resize(ret);
    return data;
}

int KcpTransport::peekSize() const { return m_kcp ? m_kcp->peekSize() : -1; }
int KcpTransport::pending() const { return m_kcp ? m_kcp->waitSnd() : 0; }

void KcpTransport::setFastMode() { if (m_kcp) m_kcp->setFastMode(); }
void KcpTransport::setVideoStreamMode() { if (m_kcp) m_kcp->setVideoStreamMode(); }
void KcpTransport::setNormalMode() { if (m_kcp) m_kcp->setNormalMode(); }
void KcpTransport::setDefaultMode() { if (m_kcp) m_kcp->setDefaultMode(); }
void KcpTransport::setWindowSize(int s, int r) { if (m_kcp) m_kcp->setWindowSize(s, r); }
void KcpTransport::setMtu(int m) { if (m_kcp) m_kcp->setMtu(m); }

void KcpTransport::setUpdateInterval(int interval)
{
    m_updateInterval = std::clamp(interval, 1, 100);
}

void KcpTransport::setNoDelay(int nodelay, int interval, int resend, int nc)
{
    if (m_kcp) m_kcp->setNoDelay(nodelay, interval, resend, nc);
}

void KcpTransport::setMinRto(int minrto) { if (m_kcp) m_kcp->setMinRto(minrto); }
void KcpTransport::setStreamMode(int stream) { if (m_kcp) m_kcp->setStream(stream); }

void KcpTransport::setFecEnabled(bool enabled, int groupSize)
{
    m_fecEnabled = enabled;
    if (enabled) {
        m_fecEncoder = std::make_unique<fec::FecEncoder>(groupSize);
        m_fecDecoder = std::make_unique<fec::FecDecoder>();
        LOG_I("[KcpTransport] FEC enabled: groupSize=%d", groupSize);
    } else {
        m_fecEncoder.reset();
        m_fecDecoder.reset();
        LOG_I("[KcpTransport] FEC disabled");
    }
}

// IO thread: select() on socket + KCP update
void KcpTransport::ioLoop()
{
    while (!m_stopRequested.load()) {
        if (m_socket == INVALID_SOCKET) break;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(m_socket, &readfds);

        // Use KCP check to determine next update time
        uint32_t current = currentMs();
        uint32_t next = m_kcp->check(current);
        int delayMs = static_cast<int>(next - current);
        delayMs = std::clamp(delayMs, 1, 100);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = delayMs * 1000;

        int ret = ::select(0, &readfds, nullptr, nullptr, &tv);
        if (m_stopRequested.load()) break;

        if (ret > 0 && FD_ISSET(m_socket, &readfds)) {
            processIncoming();
        }

        // KCP update
        m_kcp->update(currentMs());
    }
}

void KcpTransport::processIncoming()
{
    struct UdpPacket {
        char data[1500];
        int size;
    };
    static constexpr int MAX_BATCH = 64;
    UdpPacket packets[MAX_BATCH];
    const char *ptrs[MAX_BATCH];
    int sizes[MAX_BATCH];
    int count = 0;

    while (count < MAX_BATCH) {
        sockaddr_in sender{};
        int senderLen = sizeof(sender);
        int size = ::recvfrom(m_socket, packets[count].data, sizeof(packets[count].data), 0,
                              reinterpret_cast<sockaddr *>(&sender), &senderLen);
        if (size <= 0) break;

        if (m_remotePort == 0) {
            m_remoteAddr = sender;
            m_remotePort = ntohs(sender.sin_port);
            if (m_peerConnectedCb) m_peerConnectedCb();
        }

        packets[count].size = size;
        ptrs[count] = packets[count].data;
        sizes[count] = size;
        count++;
    }

    if (count == 0) return;

    bool hasData = false;
    if (m_fecEnabled && m_fecDecoder) {
        for (int i = 0; i < count; ++i) {
            m_fecDecoder->decode(reinterpret_cast<const uint8_t *>(ptrs[i]), sizes[i],
                [this](const uint8_t *data, int dataLen) {
                    m_kcp->input(reinterpret_cast<const char *>(data), dataLen);
                });
        }
        m_kcp->update(currentMs());
        hasData = m_kcp->peekSize() > 0;
    } else {
        hasData = m_kcp->processInputBatch(ptrs, sizes, count, currentMs()) > 0;
    }

    if (hasData && m_dataReadyCb) {
        m_dataReadyCb();
    }
}

int KcpTransport::udpOutput(const char *buf, int len)
{
    if (m_socket == INVALID_SOCKET || !m_active.load() || m_remotePort == 0) return -1;

    if (m_fecEnabled && m_fecEncoder) {
        m_fecEncoder->encode(reinterpret_cast<const uint8_t *>(buf), len,
            [this](const uint8_t *data, int dataLen) {
                ::sendto(m_socket, reinterpret_cast<const char *>(data), dataLen, 0,
                         reinterpret_cast<const sockaddr *>(&m_remoteAddr), sizeof(m_remoteAddr));
            });
        return len;
    }

    int sent = ::sendto(m_socket, buf, len, 0,
                        reinterpret_cast<const sockaddr *>(&m_remoteAddr), sizeof(m_remoteAddr));
    return sent < 0 ? -1 : sent;
}

uint32_t KcpTransport::currentMs() const
{
    return static_cast<uint32_t>(m_clock.elapsed());
}