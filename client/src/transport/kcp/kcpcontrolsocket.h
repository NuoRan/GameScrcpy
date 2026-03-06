/**
 * @file kcpcontrolsocket.h
 * @brief KCP Control Socket - QObject adapter
 *
 * Wraps KcpControlClient with a Qt signal interface.
 */

#ifndef KCPCONTROLSOCKET_H
#define KCPCONTROLSOCKET_H

#include "GameSignal.h"

#include <string>
#include <vector>
#include <cstdint>
#include <atomic>

// Forward declaration
class KcpControlClient;

class KcpControlSocket
{
public:
    static constexpr uint32_t KCP_CONV_CONTROL = 0x22334455;
    static constexpr int UPDATE_INTERVAL_MS = 5;
    static constexpr int MAX_RECV_BUFFER = 64 * 1024;

    explicit KcpControlSocket();
    virtual ~KcpControlSocket();

    bool bind(uint16_t port = 0);
    uint16_t localPort() const;
    std::string localAddress() const;
    void connectToHost(const std::string &host, uint16_t port);
    bool isValid() const;
    int64_t write(const char *data, int64_t len);
    std::vector<uint8_t> readAll();
    int64_t bytesAvailable() const;
    void close();

    // 信号 (Signal<>) — 替代 Qt signals
    Signal<> readyRead;
    Signal<> connected;
    Signal<> disconnected;
    Signal<const std::string&> errorOccurred;

private:
    void onDataReady();

    KcpControlClient *m_client = nullptr;
    std::vector<uint8_t> m_readBuffer;
};

#endif // KCPCONTROLSOCKET_H