/**
 * @file KcpVideoSocket.h
 * @brief UDP Video Receive Socket (QObject adapter)
 *
 * Wraps UdpVideoClient with a Qt signal interface.
 * Upper-layer code can transparently switch between UDP/TCP modes.
 */

#ifndef KCPVIDEOSOCKET_H
#define KCPVIDEOSOCKET_H

#include "GameSignal.h"

#include <string>
#include <cstdint>
#include <atomic>

// Forward declaration
class UdpVideoClient;

class KcpVideoSocket
{
public:
    static constexpr uint32_t KCP_CONV = 0x11223344;
    static constexpr int UPDATE_INTERVAL_MS = 10;

    explicit KcpVideoSocket();
    virtual ~KcpVideoSocket();

    void setBitrate(uint32_t bitrateBps, uint32_t maxFps = 60);
    bool bind(uint16_t port = 0);
    uint16_t localPort() const;
    std::string localAddress() const;
    void connectToHost(const std::string &host, uint16_t port);
    bool isValid() const;
    int32_t subThreadRecvData(uint8_t *buf, int32_t bufSize);
    void close();
    int64_t bytesAvailable() const;
    std::string getStats() const;

    // 信号 (Signal<>) — 替代 Qt signals
    Signal<> readyRead;
    Signal<> connected;
    Signal<> disconnected;
    Signal<const std::string&> errorOccurred;

private:
    UdpVideoClient *m_client = nullptr;
};

#endif // KCPVIDEOSOCKET_H