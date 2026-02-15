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

    // 使用 waitForReadyRead() 进行 OS 级 socket 阻塞等待
    // waitForReadyRead() 不依赖 Qt 事件循环，直接调用系统 select/poll，
    // 数据到达时立即返回（微秒级），无轮询延迟。
    //
    // 为什么不用 QWaitCondition + readyRead 信号：
    // Demuxer 线程的 run() 是紧密循环，没有事件循环（不调用 exec()），
    // 导致 readyRead 信号永远无法分发，QWaitCondition 总是等满超时。
    // 120fps 下超时 50ms → 每 50ms 堆积 6 帧 → 只显示最后 1 帧 → 体感 20fps。
    static constexpr int WAIT_TIMEOUT_MS = 50;

    while (bytesAvailable() < bufSize) {
        if (m_stopRequested.load(std::memory_order_acquire)) {
            return 0;
        }
        if (state() != QAbstractSocket::ConnectedState) {
            return 0;
        }
        // OS 级阻塞：数据到达立即返回，最多等 50ms 作为安全守卫
        waitForReadyRead(WAIT_TIMEOUT_MS);
    }

    return read((char *)buf, bufSize);
}
