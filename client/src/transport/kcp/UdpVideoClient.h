/**
 * @file UdpVideoClient.h
 * @brief Raw UDP Video Receiver (no Qt dependency)
 *
 * Replaces KcpVideoClient for video channel.
 * No KCP: no ACK, no retransmit, no congestion control, no lock overhead.
 *
 * Protocol: each UDP packet = [uint32 seq] + [uint8 flags] + [payload]
 *   - flags bit 0 (SOF): Start of frame
 *   - flags bit 1 (EOF): End of frame
 *   - Single-packet frame: flags = SOF|EOF (0x03)
 */

#ifndef UDP_VIDEO_CLIENT_H
#define UDP_VIDEO_CLIENT_H

#include <winsock2.h>
#include <ws2tcpip.h>

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <cstdint>

#include "KcpClient.h"  // CircularBuffer

class UdpVideoClient
{
public:
    static constexpr int SEQ_HEADER_SIZE = 5;
    static constexpr uint8_t FLAG_SOF = 0x01;
    static constexpr uint8_t FLAG_EOF = 0x02;
    static constexpr int MIN_RING_BUFFER   = 4 * 1024 * 1024;
    static constexpr int MIN_RECV_BUFFER   = 2 * 1024 * 1024;
    static constexpr int MIN_FRAME_BUFFER  = 1024 * 1024;

    using ConnectedCallback = std::function<void()>;
    using DisconnectedCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const std::string &)>;

    UdpVideoClient();
    ~UdpVideoClient();

    void configure(uint32_t bitrateBps, uint32_t maxFps);
    bool bind(uint16_t port);
    uint16_t localPort() const;
    void connectTo(const std::string &host, uint16_t port);
    bool isActive() const;
    int recvBlocking(char *buf, int bufSize, int timeoutMs = -1);
    int available() const;
    void close();
    std::string stats() const;

    void setConnectedCallback(ConnectedCallback cb) { m_connectedCb = std::move(cb); }
    void setDisconnectedCallback(DisconnectedCallback cb) { m_disconnectedCb = std::move(cb); }
    void setErrorCallback(ErrorCallback cb) { m_errorCb = std::move(cb); }

private:
    void ioLoop();
    void commitFrame();

    SOCKET m_socket = INVALID_SOCKET;
    std::thread m_ioThread;

    CircularBuffer m_ringBuffer;
    mutable std::mutex m_mutex;
    std::condition_variable m_dataAvailable;

    int m_ringBufferSize = MIN_RING_BUFFER;
    int m_recvBufferSize = MIN_RECV_BUFFER;
    int m_frameBufferSize = MIN_FRAME_BUFFER;

    std::atomic<bool> m_active{false};
    std::atomic<bool> m_closed{false};

    uint32_t m_expectedSeq = 0;
    bool m_firstPacket = true;

    enum class FrameState { WAITING_SOF, COLLECTING };
    FrameState m_frameState = FrameState::WAITING_SOF;
    char *m_frameBuffer = nullptr;
    int m_frameLen = 0;
    uint32_t m_lastSeq = 0;

    std::atomic<uint64_t> m_totalRecv{0};
    std::atomic<uint64_t> m_totalPackets{0};
    std::atomic<uint64_t> m_gapCount{0};
    std::atomic<uint64_t> m_droppedFrames{0};
    std::atomic<uint64_t> m_completedFrames{0};

    uint16_t m_localPort = 0;

    ConnectedCallback m_connectedCb;
    DisconnectedCallback m_disconnectedCb;
    ErrorCallback m_errorCb;
};

#endif // UDP_VIDEO_CLIENT_H