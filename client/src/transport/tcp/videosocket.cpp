#include <QCoreApplication>
#include <QDebug>
#include <QThread>

#include "videosocket.h"

VideoSocket::VideoSocket(QObject *parent) : QTcpSocket(parent)
{
    // 禁用 Nagle 算法
    setSocketOption(QAbstractSocket::LowDelayOption, 1);

    // 设置较大的接收缓冲区（视频数据量大）
    setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 256 * 1024);
}

VideoSocket::~VideoSocket()
{
}

void VideoSocket::requestStop()
{
    m_stopRequested.store(true);
}

qint32 VideoSocket::subThreadRecvData(quint8 *buf, qint32 bufSize)
{
    if (!buf) {
        return 0;
    }
    // 此函数只能在子线程调用
    Q_ASSERT(QCoreApplication::instance()->thread() != QThread::currentThread());

    // 使用较短的超时以便快速响应停止请求
    static constexpr int READ_TIMEOUT_MS = 100;  // 100ms 超时，更快响应停止

    while (bytesAvailable() < bufSize) {
        // 检查停止请求
        if (m_stopRequested.load()) {
            return 0;
        }
        if (state() != QAbstractSocket::ConnectedState) {
            return 0;
        }
        if (!waitForReadyRead(READ_TIMEOUT_MS)) {
            if (error() == QAbstractSocket::SocketTimeoutError) {
                continue;  // 超时但仍连接，继续等待
            }
            return 0;
        }
    }

    return read((char *)buf, bufSize);
}
