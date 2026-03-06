#include "NativeTcpSocket.h"
#define LOG_TAG "AuxChannel"
#include "Logger.h"

#include "AuxChannelClient.h"

void AuxChannelClient::setTcpSocket(NativeTcpSocket *socket)
{
    m_tcpSocket = socket;
    LOGD() << "[AuxChannel] Configured TCP mode";
}

bool AuxChannelClient::isReady() const
{
    return m_tcpSocket && m_tcpSocket->isValid();
}

void AuxChannelClient::sendVideoParams(uint32_t bitrate, uint16_t maxFps, uint16_t maxSize)
{
    // [type:1][bitrate:4BE][maxFps:2BE][maxSize:2BE] = 9 bytes
    char buf[9];
    buf[0] = static_cast<char>(TYPE_SET_VIDEO_PARAMS);
    buf[1] = static_cast<char>((bitrate >> 24) & 0xFF);
    buf[2] = static_cast<char>((bitrate >> 16) & 0xFF);
    buf[3] = static_cast<char>((bitrate >>  8) & 0xFF);
    buf[4] = static_cast<char>((bitrate      ) & 0xFF);
    buf[5] = static_cast<char>((maxFps >> 8) & 0xFF);
    buf[6] = static_cast<char>((maxFps     ) & 0xFF);
    buf[7] = static_cast<char>((maxSize >> 8) & 0xFF);
    buf[8] = static_cast<char>((maxSize     ) & 0xFF);

    writeMessage(buf, 9);
}

void AuxChannelClient::sendVideoStreaming(bool on)
{
    // [type:1][mode:1] = 2 bytes
    char buf[2];
    buf[0] = static_cast<char>(TYPE_SET_VIDEO_STREAMING);
    buf[1] = on ? 1 : 0;

    writeMessage(buf, 2);
}

void AuxChannelClient::writeMessage(const char *data, int size)
{
    if (m_tcpSocket && m_tcpSocket->isValid()) {
        int written = m_tcpSocket->send(data, size);
        if (written < 0) {
            LOGW() << "[AuxChannel] TCP write failed";
        }
    } else {
        LOG_W("[AuxChannel] TCP not ready, message dropped");
    }
}
