#include "NativeTcpSocket.h"
#define LOG_TAG "NativeTcpSocket"
#include "Logger.h"

std::atomic<bool> NativeTcpSocket::s_wsaInitialized{false};

void NativeTcpSocket::ensureWsaInit()
{
    // 简单 relaxed 检查 + 可能多次 WSAStartup（WSA 允许多次调用）
    if (!s_wsaInitialized.load(std::memory_order_relaxed)) {
        WSADATA wsa;
        int ret = ::WSAStartup(MAKEWORD(2, 2), &wsa);
        if (ret != 0) {
            LOGE() << "WSAStartup failed:" << ret;
        } else {
            s_wsaInitialized.store(true, std::memory_order_release);
        }
    }
}

NativeTcpSocket::NativeTcpSocket()
{
    ensureWsaInit();
}

NativeTcpSocket::NativeTcpSocket(SOCKET socket)
    : m_socket(socket)
{
    ensureWsaInit();
}

NativeTcpSocket::~NativeTcpSocket()
{
    close();
}

NativeTcpSocket::NativeTcpSocket(NativeTcpSocket&& other) noexcept
    : m_socket(other.m_socket)
    , m_stopRequested(other.m_stopRequested.load())
{
    other.m_socket = INVALID_SOCKET;
}

NativeTcpSocket& NativeTcpSocket::operator=(NativeTcpSocket&& other) noexcept
{
    if (this != &other) {
        close();
        m_socket = other.m_socket;
        m_stopRequested.store(other.m_stopRequested.load());
        other.m_socket = INVALID_SOCKET;
    }
    return *this;
}

bool NativeTcpSocket::connectToHost(const std::string& host, uint16_t port, int timeoutMs)
{
    close();
    m_stopRequested.store(false);

    // 解析地址
    struct addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    std::string portStr = std::to_string(port);
    int ret = ::getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
    if (ret != 0 || !result) {
        LOGE() << "getaddrinfo failed for" << host.c_str() << ":" << port << "error:" << ret;
        return false;
    }

    m_socket = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (m_socket == INVALID_SOCKET) {
        LOGE() << "socket() failed:" << ::WSAGetLastError();
        ::freeaddrinfo(result);
        return false;
    }

    // 设置连接超时：先设为非阻塞，connect，等待，再设回阻塞
    if (timeoutMs > 0) {
        u_long nonBlock = 1;
        ::ioctlsocket(m_socket, FIONBIO, &nonBlock);

        ret = ::connect(m_socket, result->ai_addr, (int)result->ai_addrlen);
        ::freeaddrinfo(result);

        if (ret == SOCKET_ERROR) {
            int err = ::WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                LOGE() << "connect() failed immediately:" << err;
                close();
                return false;
            }

            // 等待连接完成
            fd_set wset, eset;
            FD_ZERO(&wset);
            FD_ZERO(&eset);
            FD_SET(m_socket, &wset);
            FD_SET(m_socket, &eset);

            struct timeval tv;
            tv.tv_sec = timeoutMs / 1000;
            tv.tv_usec = (timeoutMs % 1000) * 1000;

            ret = ::select(0, nullptr, &wset, &eset, &tv);
            if (ret <= 0 || FD_ISSET(m_socket, &eset)) {
                LOGE() << "connect() timed out or failed";
                close();
                return false;
            }
        }

        // 恢复阻塞模式
        u_long blocking = 0;
        ::ioctlsocket(m_socket, FIONBIO, &blocking);
    } else {
        ret = ::connect(m_socket, result->ai_addr, (int)result->ai_addrlen);
        ::freeaddrinfo(result);

        if (ret == SOCKET_ERROR) {
            LOGE() << "connect() failed:" << ::WSAGetLastError();
            close();
            return false;
        }
    }

    LOGD() << "Connected to" << host.c_str() << ":" << port;
    return true;
}

void NativeTcpSocket::close()
{
    if (m_socket != INVALID_SOCKET) {
        ::shutdown(m_socket, SD_BOTH);
        ::closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
}

void NativeTcpSocket::requestStop()
{
    m_stopRequested.store(true, std::memory_order_release);
    if (m_socket != INVALID_SOCKET) {
        // shutdown 使阻塞的 recv 返回 0
        ::shutdown(m_socket, SD_BOTH);
    }
}

int NativeTcpSocket::send(const char* data, int len)
{
    if (m_socket == INVALID_SOCKET || !data || len <= 0) return -1;

    int totalSent = 0;
    while (totalSent < len) {
        if (m_stopRequested.load(std::memory_order_acquire)) return -1;

        int ret = ::send(m_socket, data + totalSent, len - totalSent, 0);
        if (ret == SOCKET_ERROR) {
            int err = ::WSAGetLastError();
            if (err == WSAEWOULDBLOCK) continue;
            LOGE() << "send() error:" << err;
            return -1;
        }
        totalSent += ret;
    }
    return totalSent;
}

int NativeTcpSocket::recv(char* buf, int maxLen)
{
    if (m_socket == INVALID_SOCKET || !buf || maxLen <= 0) return -1;
    if (m_stopRequested.load(std::memory_order_acquire)) return -1;

    int ret = ::recv(m_socket, buf, maxLen, 0);
    if (ret == SOCKET_ERROR) {
        int err = ::WSAGetLastError();
        if (err == WSAECONNRESET || err == WSAECONNABORTED) return 0;
        LOGE() << "recv() error:" << err;
        return -1;
    }
    return ret;  // 0 = 对端关闭
}

bool NativeTcpSocket::recvAll(char* buf, int len)
{
    if (!buf || len <= 0) return false;

    int received = 0;
    while (received < len) {
        if (m_stopRequested.load(std::memory_order_acquire)) return false;

        int ret = ::recv(m_socket, buf + received, len - received, 0);
        if (ret <= 0) {
            if (ret == 0) {
                LOGD() << "Connection closed by peer";
            } else {
                LOGE() << "recv() error:" << ::WSAGetLastError();
            }
            return false;
        }
        received += ret;
    }
    return true;
}

SOCKET NativeTcpSocket::release()
{
    SOCKET s = m_socket;
    m_socket = INVALID_SOCKET;
    return s;
}

void NativeTcpSocket::setNoDelay(bool enable)
{
    if (m_socket == INVALID_SOCKET) return;
    int flag = enable ? 1 : 0;
    ::setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&flag, sizeof(flag));
}

void NativeTcpSocket::setRecvBufferSize(int size)
{
    if (m_socket == INVALID_SOCKET) return;
    ::setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, (const char*)&size, sizeof(size));
}

void NativeTcpSocket::setSendBufferSize(int size)
{
    if (m_socket == INVALID_SOCKET) return;
    ::setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, (const char*)&size, sizeof(size));
}

void NativeTcpSocket::setRecvTimeout(int ms)
{
    if (m_socket == INVALID_SOCKET) return;
    DWORD timeout = static_cast<DWORD>(ms);
    ::setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
}

void NativeTcpSocket::setSendTimeout(int ms)
{
    if (m_socket == INVALID_SOCKET) return;
    DWORD timeout = static_cast<DWORD>(ms);
    ::setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
}
