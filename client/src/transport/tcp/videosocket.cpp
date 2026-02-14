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

    // [超低延迟优化] 连接 readyRead 信号到事件驱动唤醒
    // 当 socket 有数据可读时，立即唤醒等待中的子线程，
    // 消除旧版 10ms 轮询带来的平均 5ms 延迟
    connect(this, &QTcpSocket::readyRead, this, &VideoSocket::onReadyRead);
}

VideoSocket::~VideoSocket()
{
}

void VideoSocket::requestStop()
{
    m_stopRequested.store(true);
    // 唤醒可能在等待的子线程
    m_dataAvailable.wakeAll();
}

void VideoSocket::onReadyRead()
{
    // [超低延迟] 数据到达时立即唤醒等待中的子线程
    m_dataAvailable.wakeAll();
}

qint32 VideoSocket::subThreadRecvData(quint8 *buf, qint32 bufSize)
{
    if (!buf) {
        return 0;
    }
    // 此函数只能在子线程调用
    Q_ASSERT(QCoreApplication::instance()->thread() != QThread::currentThread());

    // [超低延迟优化] 事件驱动替代轮询
    // 旧方案: waitForReadyRead(10ms) 轮询 → 最坏延迟 10ms，平均 5ms
    // 新方案: QWaitCondition + readyRead 信号 → 数据到达立即唤醒 (~0ms)
    // 保留 50ms 超时作为安全守卫，防止信号丢失导致永久阻塞
    static constexpr int SAFETY_TIMEOUT_MS = 50;

    while (bytesAvailable() < bufSize) {
        // 检查停止请求
        if (m_stopRequested.load(std::memory_order_acquire)) {
            return 0;
        }
        if (state() != QAbstractSocket::ConnectedState) {
            return 0;
        }

        // 数据不足时，先尝试 waitForReadyRead(0) 非阻塞检查
        // 如果仍没有数据，则使用 QWaitCondition 等待信号唤醒
        if (!waitForReadyRead(0)) {
            QMutexLocker locker(&m_waitMutex);
            // 双重检查：加锁后再看一次是否已有足够数据
            if (bytesAvailable() >= bufSize) {
                break;
            }
            // 等待 readyRead 信号唤醒，超时作为安全守卫
            m_dataAvailable.wait(&m_waitMutex, SAFETY_TIMEOUT_MS);
        }
    }

    return read((char *)buf, bufSize);
}
