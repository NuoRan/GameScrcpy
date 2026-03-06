#include "videosocket.h"
#define LOG_TAG "VideoSocket"
#include "Logger.h"

VideoSocket::VideoSocket()
{
}

VideoSocket::~VideoSocket()
{
}

void VideoSocket::adoptSocket(SOCKET handle)
{
    m_socket = NativeTcpSocket(handle);
    m_socket.setNoDelay(true);
    m_socket.setRecvBufferSize(256 * 1024);
}

bool VideoSocket::connectToHost(const char* host, uint16_t port, int timeoutMs)
{
    if (!m_socket.connectToHost(host, port, timeoutMs)) {
        return false;
    }
    m_socket.setNoDelay(true);
    m_socket.setRecvBufferSize(256 * 1024);
    return true;
}

int VideoSocket::readInfoData(unsigned char* buf, int bufSize, int timeoutMs)
{
    if (!buf || bufSize <= 0) return 0;

    // 使用 recv timeout 来实现超时
    m_socket.setRecvTimeout(timeoutMs);
    bool ok = m_socket.recvAll((char*)buf, bufSize);
    m_socket.setRecvTimeout(0);  // 恢复无超时
    return ok ? bufSize : 0;
}

int32_t VideoSocket::subThreadRecvData(uint8_t *buf, int32_t bufSize)
{
    if (!buf || bufSize <= 0) {
        return 0;
    }

    // 使用 NativeTcpSocket::recvAll 阻塞读取精确字节数
    // requestStop() 会 shutdown socket，使 recv 返回 0/error
    bool ok = m_socket.recvAll((char*)buf, bufSize);
    return ok ? bufSize : 0;
}

void VideoSocket::requestStop()
{
    m_socket.requestStop();
}
