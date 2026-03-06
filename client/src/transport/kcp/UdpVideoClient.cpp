/**
 * @file UdpVideoClient.cpp
 * @brief Raw UDP Video Receiver - Native Winsock2 implementation
 */

#include "UdpVideoClient.h"
#include <algorithm>
#include <chrono>

#define LOG_TAG "UdpVideoClient"
#include "Logger.h"

#pragma comment(lib, "ws2_32.lib")

UdpVideoClient::UdpVideoClient()
{
    m_socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket != INVALID_SOCKET) {
        u_long nonBlocking = 1;
        ::ioctlsocket(m_socket, FIONBIO, &nonBlocking);
    }

    m_ringBuffer.reserve(m_ringBufferSize);
    m_frameBuffer = new char[m_frameBufferSize];
}

UdpVideoClient::~UdpVideoClient()
{
    close();
    delete[] m_frameBuffer;
    m_frameBuffer = nullptr;
}

void UdpVideoClient::configure(uint32_t bitrateBps, uint32_t maxFps)
{
    uint32_t fps = (maxFps > 0) ? maxFps : 60;

    int64_t ringSize = static_cast<int64_t>(bitrateBps) / 8 * 3;
    m_ringBufferSize = std::max(static_cast<int>(std::min(ringSize, static_cast<int64_t>(64 * 1024 * 1024))),
                                MIN_RING_BUFFER);

    int64_t recvSize = static_cast<int64_t>(bitrateBps) / 8 / fps * 10;
    m_recvBufferSize = std::max(static_cast<int>(std::min(recvSize, static_cast<int64_t>(16 * 1024 * 1024))),
                                MIN_RECV_BUFFER);

    int64_t frameSize = static_cast<int64_t>(bitrateBps) / 8;
    m_frameBufferSize = std::max(static_cast<int>(std::min(frameSize, static_cast<int64_t>(8 * 1024 * 1024))),
                                 MIN_FRAME_BUFFER);

    m_ringBuffer.reserve(m_ringBufferSize);
    delete[] m_frameBuffer;
    m_frameBuffer = new char[m_frameBufferSize];
    m_frameLen = 0;

    LOG_I("[UdpVideoClient] configure: bitrate=%uMbps, fps=%u, ring=%dMB, recv=%dMB, frame=%dKB",
          bitrateBps / 1000000, fps,
          m_ringBufferSize / (1024 * 1024),
          m_recvBufferSize / (1024 * 1024),
          m_frameBufferSize / 1024);
}

bool UdpVideoClient::bind(uint16_t port)
{
    if (m_socket == INVALID_SOCKET) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(m_socket, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        if (m_errorCb) m_errorCb("Failed to bind UDP socket: " + std::to_string(WSAGetLastError()));
        return false;
    }

    sockaddr_in localAddr{};
    int addrLen = sizeof(localAddr);
    if (::getsockname(m_socket, reinterpret_cast<sockaddr *>(&localAddr), &addrLen) == 0) {
        m_localPort = ntohs(localAddr.sin_port);
    }

    // Set OS recv buffer size
    int bufSize = m_recvBufferSize;
    ::setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&bufSize), sizeof(bufSize));

    m_active.store(true);

    LOG_I("[UdpVideoClient] bound port %d, ring=%dMB, recv=%dMB, frame=%dKB",
          port,
          m_ringBufferSize / (1024 * 1024),
          m_recvBufferSize / (1024 * 1024),
          m_frameBufferSize / 1024);

    // Start IO thread
    m_ioThread = std::thread(&UdpVideoClient::ioLoop, this);
    return true;
}

uint16_t UdpVideoClient::localPort() const
{
    return m_localPort;
}

void UdpVideoClient::connectTo(const std::string &host, uint16_t port)
{
    (void)host;
    (void)port;
    // UDP video is unidirectional receive, no connect needed
    if (!m_active.load()) {
        m_active.store(true);
    }
}

bool UdpVideoClient::isActive() const
{
    return m_active.load() && !m_closed.load();
}

int UdpVideoClient::recvBlocking(char *buf, int bufSize, int timeoutMs)
{
    if (!buf || bufSize <= 0 || m_closed.load()) return 0;

    std::unique_lock<std::mutex> locker(m_mutex);
    while (m_ringBuffer.available() < bufSize) {
        if (m_closed.load()) return 0;
        if (timeoutMs < 0) {
            m_dataAvailable.wait(locker);
        } else {
            auto status = m_dataAvailable.wait_for(locker, std::chrono::milliseconds(timeoutMs));
            if (status == std::cv_status::timeout && m_ringBuffer.available() < bufSize) return 0;
        }
    }

    int toRead = std::min(bufSize, m_ringBuffer.available());
    m_ringBuffer.read(buf, toRead);
    return toRead;
}

int UdpVideoClient::available() const
{
    std::lock_guard<std::mutex> locker(m_mutex);
    return m_ringBuffer.available();
}

void UdpVideoClient::close()
{
    m_closed.store(true);
    m_active.store(false);

    if (m_socket != INVALID_SOCKET) {
        ::closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }

    m_dataAvailable.notify_all();

    if (m_ioThread.joinable()) {
        m_ioThread.join();
    }

    if (m_disconnectedCb) m_disconnectedCb();
}

std::string UdpVideoClient::stats() const
{
    char buf[256];
    snprintf(buf, sizeof(buf), "recv=%llu,buf=%d,pkts=%llu,gaps=%llu,frames=%llu,drops=%llu",
             m_totalRecv.load(), m_ringBuffer.available(),
             m_totalPackets.load(), m_gapCount.load(),
             m_completedFrames.load(), m_droppedFrames.load());
    return buf;
}

// IO thread: select() on socket, frame reassembly
void UdpVideoClient::ioLoop()
{
    while (!m_closed.load()) {
        if (m_socket == INVALID_SOCKET) break;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(m_socket, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 10000;  // 10ms timeout

        int ret = ::select(0, &readfds, nullptr, nullptr, &tv);
        if (m_closed.load()) break;
        if (ret <= 0) continue;

        // Process all pending datagrams
        char recvBuf[1500];
        bool committed = false;

        for (;;) {
            sockaddr_in sender{};
            int senderLen = sizeof(sender);
            int size = ::recvfrom(m_socket, recvBuf, sizeof(recvBuf), 0,
                                  reinterpret_cast<sockaddr *>(&sender), &senderLen);
            if (size <= SEQ_HEADER_SIZE) break;

            // Parse header
            uint32_t seq = (static_cast<uint32_t>(static_cast<uint8_t>(recvBuf[0])) << 24) |
                           (static_cast<uint32_t>(static_cast<uint8_t>(recvBuf[1])) << 16) |
                           (static_cast<uint32_t>(static_cast<uint8_t>(recvBuf[2])) << 8)  |
                            static_cast<uint32_t>(static_cast<uint8_t>(recvBuf[3]));
            uint8_t flags = static_cast<uint8_t>(recvBuf[4]);

            if (m_firstPacket) {
                m_expectedSeq = seq;
                m_firstPacket = false;
                if (m_connectedCb) m_connectedCb();
            }

            if (seq != m_expectedSeq) {
                uint32_t gap = seq - m_expectedSeq;
                m_gapCount.fetch_add(gap);
            }
            m_expectedSeq = seq + 1;

            int payloadSize = size - SEQ_HEADER_SIZE;
            m_totalPackets++;
            m_totalRecv += payloadSize;

            // Frame reassembly state machine
            if (flags & FLAG_SOF) {
                if (m_frameState == FrameState::COLLECTING) {
                    m_droppedFrames++;
                }
                m_frameLen = 0;
                m_lastSeq = seq;

                if (payloadSize <= m_frameBufferSize) {
                    memcpy(m_frameBuffer, recvBuf + SEQ_HEADER_SIZE, payloadSize);
                    m_frameLen = payloadSize;
                }

                if (flags & FLAG_EOF) {
                    commitFrame();
                    committed = true;
                    m_frameState = FrameState::WAITING_SOF;
                } else {
                    m_frameState = FrameState::COLLECTING;
                }
            } else if (m_frameState == FrameState::COLLECTING) {
                if (seq != m_lastSeq + 1) {
                    m_droppedFrames++;
                    m_frameLen = 0;
                    m_frameState = FrameState::WAITING_SOF;
                } else {
                    m_lastSeq = seq;
                    if (m_frameLen + payloadSize > m_frameBufferSize) {
                        m_droppedFrames++;
                        m_frameLen = 0;
                        m_frameState = FrameState::WAITING_SOF;
                    } else {
                        memcpy(m_frameBuffer + m_frameLen,
                               recvBuf + SEQ_HEADER_SIZE, payloadSize);
                        m_frameLen += payloadSize;

                        if (flags & FLAG_EOF) {
                            commitFrame();
                            committed = true;
                            m_frameState = FrameState::WAITING_SOF;
                        }
                    }
                }
            }
        }

        if (committed) {
            m_dataAvailable.notify_all();
        }
    }
}

void UdpVideoClient::commitFrame()
{
    if (m_frameLen <= 0) return;

    std::lock_guard<std::mutex> locker(m_mutex);
    if (m_ringBuffer.freeSpace() < m_frameLen) {
        m_droppedFrames++;
        m_frameLen = 0;
        return;
    }
    m_ringBuffer.write(m_frameBuffer, m_frameLen);
    m_completedFrames++;
    m_frameLen = 0;
}