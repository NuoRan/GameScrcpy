/**
 * @file KcpClient.h
 * @brief KCP Client Unified Interface (no Qt dependency)
 */

#ifndef KCP_CLIENT_H
#define KCP_CLIENT_H

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <cstdint>
#include <functional>
#include <string>
#include <algorithm>
#include <cstring>

#include "KcpTransport.h"

/**
 * @brief CircularBuffer - O(1) ring buffer (replaces QByteArray append/remove)
 */
class CircularBuffer {
public:
    explicit CircularBuffer(int capacity = 4 * 1024 * 1024)
        : m_buffer(capacity), m_capacity(capacity) {}

    void reserve(int newCapacity) {
        if (newCapacity > m_capacity) {
            std::vector<char> newBuf(newCapacity);
            int avail = available();
            if (avail > 0) {
                peek(newBuf.data(), avail);
            }
            m_buffer = std::move(newBuf);
            m_capacity = newCapacity;
            m_readPos = 0;
            m_writePos = avail;
            m_size = avail;
        }
    }

    int write(const char* data, int len) {
        int space = freeSpace();
        if (len > space) len = space;
        if (len <= 0) return 0;

        int firstChunk = std::min(len, m_capacity - m_writePos);
        memcpy(m_buffer.data() + m_writePos, data, firstChunk);
        if (len > firstChunk) {
            memcpy(m_buffer.data(), data + firstChunk, len - firstChunk);
        }
        m_writePos = (m_writePos + len) % m_capacity;
        m_size += len;
        return len;
    }

    int read(char* data, int len) {
        int avail = available();
        if (len > avail) len = avail;
        if (len <= 0) return 0;

        int firstChunk = std::min(len, m_capacity - m_readPos);
        memcpy(data, m_buffer.data() + m_readPos, firstChunk);
        if (len > firstChunk) {
            memcpy(data + firstChunk, m_buffer.data(), len - firstChunk);
        }
        m_readPos = (m_readPos + len) % m_capacity;
        m_size -= len;
        return len;
    }

    int peek(char* data, int len) const {
        int avail = available();
        if (len > avail) len = avail;
        if (len <= 0) return 0;

        int firstChunk = std::min(len, m_capacity - m_readPos);
        memcpy(data, m_buffer.data() + m_readPos, firstChunk);
        if (len > firstChunk) {
            memcpy(data + firstChunk, m_buffer.data(), len - firstChunk);
        }
        return len;
    }

    void drop(int len) {
        int avail = available();
        if (len > avail) len = avail;
        m_readPos = (m_readPos + len) % m_capacity;
        m_size -= len;
    }

    int available() const { return m_size; }
    int freeSpace() const { return m_capacity - m_size; }
    int capacity() const { return m_capacity; }
    void clear() { m_readPos = 0; m_writePos = 0; m_size = 0; }

private:
    std::vector<char> m_buffer;
    int m_capacity = 0;
    int m_readPos = 0;
    int m_writePos = 0;
    int m_size = 0;
};

/**
 * @brief KCP Video Receiver (pure C++)
 *
 * Based on KcpTransport, provides blocking receive API for decode thread.
 */
class KcpVideoClient
{
public:
    static constexpr uint32_t CONV_VIDEO = KcpTransport::CONV_VIDEO;
    static constexpr int DEFAULT_BUFFER_SIZE = 4 * 1024 * 1024;

    using ConnectedCallback = std::function<void()>;
    using DisconnectedCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const std::string &)>;

    KcpVideoClient();
    ~KcpVideoClient();

    void configureBitrate(int bitrateBps);
    bool bind(uint16_t port = 0);
    uint16_t localPort() const;
    void connectTo(const std::string &host, uint16_t port);
    bool isActive() const;
    int recvBlocking(char *buf, int bufSize, int timeoutMs = -1);
    std::vector<uint8_t> recv();
    int available() const;
    void close();
    std::string stats() const;

    void setConnectedCallback(ConnectedCallback cb) { m_connectedCb = std::move(cb); }
    void setDisconnectedCallback(DisconnectedCallback cb) { m_disconnectedCb = std::move(cb); }
    void setErrorCallback(ErrorCallback cb) { m_errorCb = std::move(cb); }

private:
    void onDataReady();

    KcpTransport *m_transport = nullptr;
    CircularBuffer m_ringBuffer;
    mutable std::mutex m_mutex;
    std::condition_variable m_dataAvailable;

    int m_maxBufferSize = DEFAULT_BUFFER_SIZE;
    std::atomic<bool> m_closed{false};
    std::atomic<uint64_t> m_totalRecv{0};

    ConnectedCallback m_connectedCb;
    DisconnectedCallback m_disconnectedCb;
    ErrorCallback m_errorCb;
};

/**
 * @brief KCP Control Channel Client (pure C++)
 *
 * Bidirectional, message-mode (preserves message boundaries).
 */
class KcpControlClient
{
public:
    static constexpr uint32_t CONV_CONTROL = KcpTransport::CONV_CONTROL;

    using ConnectedCallback = std::function<void()>;
    using DisconnectedCallback = std::function<void()>;
    using DataReadyCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const std::string &)>;

    KcpControlClient();
    ~KcpControlClient();

    bool bind(uint16_t port = 0);
    uint16_t localPort() const;
    void connectTo(const std::string &host, uint16_t port);
    bool isActive() const;
    int send(const char *data, int len);
    int recvBlocking(char *buf, int bufSize, int timeoutMs = -1);
    std::vector<uint8_t> recv();
    void close();

    void setConnectedCallback(ConnectedCallback cb) { m_connectedCb = std::move(cb); }
    void setDisconnectedCallback(DisconnectedCallback cb) { m_disconnectedCb = std::move(cb); }
    void setDataReadyCallback(DataReadyCallback cb) { m_dataReadyCb = std::move(cb); }
    void setErrorCallback(ErrorCallback cb) { m_errorCb = std::move(cb); }

private:
    void onDataReady();

    KcpTransport *m_transport = nullptr;
    std::vector<uint8_t> m_buffer;
    mutable std::mutex m_mutex;
    std::condition_variable m_dataAvailable;

    std::atomic<bool> m_closed{false};

    ConnectedCallback m_connectedCb;
    DisconnectedCallback m_disconnectedCb;
    DataReadyCallback m_dataReadyCb;
    ErrorCallback m_errorCb;
};

#endif // KCP_CLIENT_H