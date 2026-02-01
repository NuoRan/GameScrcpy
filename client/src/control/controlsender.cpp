#include <QDebug>
#include <QThread>

#include "controlsender.h"
#include "kcpcontrolsocket.h"

/**
 * A-06 优化: 使用条件变量替代 QTimer 轮询
 *
 * 原方案: QTimer 每 2ms 触发一次检查队列
 * 新方案:
 * - KCP 模式: QWaitCondition，数据到达时立即唤醒处理 (KCP 是线程安全的)
 * - TCP 模式: 主线程定时器，因为 QTcpSocket 不支持跨线程操作
 *
 * 优点:
 * - KCP 模式: 降低延迟，降低 CPU 占用，更精确的响应
 * - TCP 模式: 保证线程安全，避免跨线程访问 QTcpSocket 导致的问题
 */

ControlSender::ControlSender(QObject *parent)
    : QObject(parent)
{
    // A-06: 创建工作线程 (仅 KCP 模式使用)
    m_workerThread = new QThread(this);
    m_workerThread->setObjectName("ControlSenderWorker");

    // TCP 模式: 创建主线程定时器
    m_tcpFlushTimer = new QTimer(this);
    m_tcpFlushTimer->setInterval(1);  // 1ms 间隔，低延迟
    connect(m_tcpFlushTimer, &QTimer::timeout, this, &ControlSender::onTcpFlushTimer);
}

ControlSender::~ControlSender()
{
    stop();
}

void ControlSender::setSocket(KcpControlSocket *socket)
{
    m_socket = socket;
    m_tcpSocket = nullptr;  // 互斥
}

void ControlSender::setTcpSocket(QTcpSocket *socket)
{
    m_tcpSocket = socket;
    m_socket = nullptr;  // 互斥
}

void ControlSender::setSendCallback(SendCallback callback)
{
    m_sendCallback = callback;
}

void ControlSender::start()
{
    if (m_running.load()) return;
    m_running.store(true);

    // 根据 socket 类型选择不同的发送模式
    if (m_tcpSocket) {
        // TCP 模式: 使用主线程定时器 (QTcpSocket 不能跨线程)
        qInfo() << "[ControlSender] Starting TCP mode (main thread timer)";
        m_tcpFlushTimer->start();
    } else {
        // KCP 模式: 使用工作线程 (KCP 是线程安全的)
        qInfo() << "[ControlSender] Starting KCP mode (worker thread)";
        if (!m_workerThread->isRunning()) {
            connect(m_workerThread, &QThread::started, this, &ControlSender::workerLoop, Qt::DirectConnection);
            m_workerThread->start();
        }
    }
}

void ControlSender::stop()
{
    if (!m_running.load()) return;
    m_running.store(false);

    // 停止 TCP 定时器
    if (m_tcpFlushTimer) {
        m_tcpFlushTimer->stop();
    }

    // A-06: 唤醒并等待工作线程结束
    {
        QMutexLocker locker(&m_mutex);
        m_condition.wakeAll();
    }

    if (m_workerThread && m_workerThread->isRunning()) {
        m_workerThread->quit();
        m_workerThread->wait(100);
    }

    QMutexLocker locker(&m_mutex);
    m_queue.clear();
    m_pendingBytes = 0;
}

// KCP 写入接口 (线程安全，可在工作线程调用)
qint64 ControlSender::doWriteKcp(const QByteArray &data)
{
    if (m_sendCallback) {
        return m_sendCallback(data);
    }
    if (m_socket && m_socket->isValid()) {
        return m_socket->write(data);
    }
    return -1;
}

// TCP 写入接口 (仅主线程调用)
qint64 ControlSender::doWriteTcp(const QByteArray &data)
{
    if (m_sendCallback) {
        return m_sendCallback(data);
    }
    if (m_tcpSocket && m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
        return m_tcpSocket->write(data);
    }
    return -1;
}

// 入队时更新累计变量
void ControlSender::enqueue(const QByteArray &data)
{
    m_queue.enqueue(data);
    m_pendingBytes += data.size();
}

// 出队时更新累计变量
QByteArray ControlSender::dequeue()
{
    if (m_queue.isEmpty()) return QByteArray();
    QByteArray data = m_queue.dequeue();
    m_pendingBytes -= data.size();
    return data;
}

// 前置入队
void ControlSender::prepend(const QByteArray &data)
{
    m_queue.prepend(data);
    m_pendingBytes += data.size();
}

bool ControlSender::send(const QByteArray &data)
{
    if (data.isEmpty()) {
        return false;
    }

    QMutexLocker locker(&m_mutex);

    // 检查缓冲区状态
    if (m_pendingBytes > MAX_PENDING_BYTES) {
        m_droppedCount++;
        return false;
    }

    if (m_pendingBytes > WARN_PENDING_BYTES) {
        emit bufferWarning(m_pendingBytes, MAX_PENDING_BYTES);
    }

    // 入队
    if (m_queue.size() >= MAX_QUEUE_SIZE) {
        // 丢弃一半旧数据，保留最新的控制命令
        int dropCount = MAX_QUEUE_SIZE / 2;
        for (int i = 0; i < dropCount && !m_queue.isEmpty(); i++) {
            dequeue();
            m_droppedCount++;
        }
    }
    enqueue(data);

    // KCP 模式: 唤醒工作线程
    if (!m_tcpSocket) {
        m_condition.wakeOne();
    }

    return true;
}

/**
 * TCP 模式: 主线程定时器回调
 * QTcpSocket 必须在创建它的线程（主线程）中使用
 */
void ControlSender::onTcpFlushTimer()
{
    if (!m_running.load()) return;

    QMutexLocker locker(&m_mutex);
    processQueue();
}

/**
 * 处理队列中的数据 (通用逻辑)
 */
void ControlSender::processQueue()
{
    int maxBatch = 128;
    while (!m_queue.isEmpty() && maxBatch-- > 0) {
        QByteArray data = dequeue();

        // 根据模式选择写入方法
        qint64 written;
        if (m_tcpSocket) {
            written = doWriteTcp(data);
        } else {
            // 注意: 此分支在 workerLoop 中不会执行，因为有单独的锁管理
            written = doWriteKcp(data);
        }

        if (written == data.size()) {
            m_sentCount++;
        } else if (written > 0) {
            // 部分写入，剩余部分重新入队
            prepend(data.mid(static_cast<int>(written)));
            break;
        } else {
            // 写入失败，放回队列
            prepend(data);
            break;
        }
    }
}

/**
 * A-06: KCP 模式工作线程循环
 * 使用条件变量等待数据，有数据时立即处理
 * 注意: 此方法仅在 KCP 模式下运行，不会访问 m_tcpSocket
 */
void ControlSender::workerLoop()
{
    while (m_running.load()) {
        QMutexLocker locker(&m_mutex);

        // A-06: 等待数据或超时 (1ms 超时用于检查 running 状态)
        if (m_queue.isEmpty()) {
            m_condition.wait(&m_mutex, 1);
            continue;
        }

        // 处理队列中的数据
        int maxBatch = 128;
        while (!m_queue.isEmpty() && m_pendingBytes < WARN_PENDING_BYTES && maxBatch-- > 0) {
            QByteArray data = dequeue();

            // 临时解锁以避免在写入时阻塞其他线程
            locker.unlock();
            qint64 written = doWriteKcp(data);
            locker.relock();

            if (written == data.size()) {
                m_sentCount++;
            } else if (written > 0) {
                // 部分写入，剩余部分重新入队
                prepend(data.mid(static_cast<int>(written)));
                break;
            } else {
                // 写入失败，放回队列
                prepend(data);
                break;
            }
        }
    }
}
