/**
 * @file KcpTransport.h
 * @brief KCP Transport Layer (Native Winsock2)
 *
 * UDP send/recv + KCP timer update with native Winsock2 + std::thread.
 * No Qt dependency.
 */

#ifndef KCP_TRANSPORT_H
#define KCP_TRANSPORT_H

#include <winsock2.h>
#include <ws2tcpip.h>

#include "ElapsedTimer.h"
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>
#include <atomic>
#include <thread>
#include <string>

#include "KcpCore.h"
#include "FecCodec.h"

/**
 * @brief KCP Transport - UDP + KCP protocol stack (no Qt)
 */
class KcpTransport
{
public:
    static constexpr uint32_t CONV_VIDEO = 0x11223344;
    static constexpr uint32_t CONV_CONTROL = 0x22334455;

    using DataReadyCallback = std::function<void()>;
    using PeerConnectedCallback = std::function<void()>;
    using DisconnectedCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const std::string &)>;

    explicit KcpTransport(uint32_t conv);
    ~KcpTransport();

    KcpTransport(const KcpTransport &) = delete;
    KcpTransport &operator=(const KcpTransport &) = delete;

    // Connection
    bool bind(uint16_t port = 0);
    void connectTo(const std::string &address, uint16_t port);
    void close();
    bool isActive() const { return m_active.load(); }

    uint16_t localPort() const;
    std::string remoteAddress() const;
    uint16_t remotePort() const { return m_remotePort; }

    // Data
    int send(const char *data, int len);
    std::vector<uint8_t> recv();
    int peekSize() const;
    int pending() const;

    // Config
    void setFastMode();
    void setVideoStreamMode();
    void setNormalMode();
    void setDefaultMode();
    void setWindowSize(int sndwnd, int rcvwnd);
    void setMtu(int mtu);
    void setUpdateInterval(int interval);
    void setFecEnabled(bool enabled, int groupSize = 10);
    bool isFecEnabled() const { return m_fecEnabled; }
    void setNoDelay(int nodelay, int interval, int resend, int nc);
    void setMinRto(int minrto);
    void setStreamMode(int stream);
    KcpCore *core() { return m_kcp.get(); }

    // Callbacks (replace Qt signals)
    void setDataReadyCallback(DataReadyCallback cb) { m_dataReadyCb = std::move(cb); }
    void setPeerConnectedCallback(PeerConnectedCallback cb) { m_peerConnectedCb = std::move(cb); }
    void setDisconnectedCallback(DisconnectedCallback cb) { m_disconnectedCb = std::move(cb); }
    void setErrorCallback(ErrorCallback cb) { m_errorCb = std::move(cb); }

private:
    void ioLoop();
    void processIncoming();
    int udpOutput(const char *buf, int len);
    uint32_t currentMs() const;

private:
    std::unique_ptr<KcpCore> m_kcp;

    SOCKET m_socket = INVALID_SOCKET;
    sockaddr_in m_remoteAddr{};
    uint16_t m_remotePort = 0;
    uint16_t m_localPort = 0;

    std::atomic<bool> m_active{false};
    int m_updateInterval = 1;

    ElapsedTimer m_clock;

    std::thread m_ioThread;
    std::atomic<bool> m_stopRequested{false};

    bool m_fecEnabled = false;
    std::unique_ptr<fec::FecEncoder> m_fecEncoder;
    std::unique_ptr<fec::FecDecoder> m_fecDecoder;

    DataReadyCallback m_dataReadyCb;
    PeerConnectedCallback m_peerConnectedCb;
    DisconnectedCallback m_disconnectedCb;
    ErrorCallback m_errorCb;
};

#endif // KCP_TRANSPORT_H