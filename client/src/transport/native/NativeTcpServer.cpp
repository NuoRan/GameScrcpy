#include "NativeTcpServer.h"
#include "NativeTcpSocket.h"
#define LOG_TAG "NativeTcpServer"
#include "Logger.h"

NativeTcpServer::NativeTcpServer()
{
    NativeTcpSocket::ensureWsaInit();
}

NativeTcpServer::~NativeTcpServer()
{
    close();
}

bool NativeTcpServer::listen(const std::string& address, uint16_t port)
{
    close();

    m_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_socket == INVALID_SOCKET) {
        LOGE() << "socket() failed:" << ::WSAGetLastError();
        return false;
    }

    // 允许端口复用
    int optval = 1;
    ::setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));

    // 设为非阻塞模式（用于 tryAccept）
    u_long nonBlock = 1;
    ::ioctlsocket(m_socket, FIONBIO, &nonBlock);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (address == "0.0.0.0" || address.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (::inet_pton(AF_INET, address.c_str(), &addr.sin_addr) != 1) {
            LOGE() << "Invalid address:" << address.c_str();
            close();
            return false;
        }
    }

    if (::bind(m_socket, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        LOGE() << "bind() failed on port" << port << ":" << ::WSAGetLastError();
        close();
        return false;
    }

    if (::listen(m_socket, SOMAXCONN) == SOCKET_ERROR) {
        LOGE() << "listen() failed:" << ::WSAGetLastError();
        close();
        return false;
    }

    // 获取实际绑定端口（如果传入 0）
    struct sockaddr_in boundAddr{};
    int addrLen = sizeof(boundAddr);
    if (::getsockname(m_socket, (struct sockaddr*)&boundAddr, &addrLen) == 0) {
        m_port = ntohs(boundAddr.sin_port);
    } else {
        m_port = port;
    }

    LOGD() << "Listening on" << address.c_str() << ":" << m_port;
    return true;
}

NativeTcpSocket* NativeTcpServer::tryAccept()
{
    if (m_socket == INVALID_SOCKET) return nullptr;

    struct sockaddr_in clientAddr{};
    int addrLen = sizeof(clientAddr);
    SOCKET clientSock = ::accept(m_socket, (struct sockaddr*)&clientAddr, &addrLen);

    if (clientSock == INVALID_SOCKET) {
        int err = ::WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            return nullptr;  // 无待处理连接
        }
        LOGE() << "accept() failed:" << err;
        return nullptr;
    }

    // 新 socket 设为阻塞模式（数据传输用）
    u_long blocking = 0;
    ::ioctlsocket(clientSock, FIONBIO, &blocking);

    LOGD() << "Accepted connection from"
           << (int)(clientAddr.sin_addr.S_un.S_un_b.s_b1) << "."
           << (int)(clientAddr.sin_addr.S_un.S_un_b.s_b2) << "."
           << (int)(clientAddr.sin_addr.S_un.S_un_b.s_b3) << "."
           << (int)(clientAddr.sin_addr.S_un.S_un_b.s_b4)
           << ":" << ntohs(clientAddr.sin_port);

    return new NativeTcpSocket(clientSock);
}

NativeTcpSocket* NativeTcpServer::acceptBlocking(int timeoutMs)
{
    if (m_socket == INVALID_SOCKET) return nullptr;

    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(m_socket, &rset);

    struct timeval tv;
    struct timeval* ptv = nullptr;
    if (timeoutMs >= 0) {
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        ptv = &tv;
    }

    int ret = ::select(0, &rset, nullptr, nullptr, ptv);
    if (ret <= 0) {
        return nullptr;  // 超时或错误
    }

    return tryAccept();
}

void NativeTcpServer::close()
{
    if (m_socket != INVALID_SOCKET) {
        ::closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
    m_port = 0;
}
