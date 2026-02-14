#include <QDebug>
#include <QThread>

#include "controlsender.h"
#include "kcpcontrolsocket.h"
#include "interfaces/IControlChannel.h"

/**
 * 即时发送模式
 *
 * 控制消息直接发送，不经过队列缓冲，最低延迟。
 * - KCP 模式：直接调用 KCP 写入（线程安全）
 * - TCP 模式：直接调用 TCP 写入（需在主线程）
 */

ControlSender::ControlSender(QObject *parent)
    : QObject(parent)
{
    // [超低延迟优化] 零延迟合并定时器
    // 使用 0ms 单次定时器，在下一次事件循环迭代时 flush
    m_coalesceTimer = new QTimer(this);
    m_coalesceTimer->setSingleShot(true);
    m_coalesceTimer->setInterval(0);
    connect(m_coalesceTimer, &QTimer::timeout, this, &ControlSender::flushCoalesced);
}

ControlSender::~ControlSender()
{
    stop();
}

void ControlSender::setSocket(KcpControlSocket *socket)
{
    m_socket = socket;
    m_tcpSocket = nullptr;
    m_controlChannel = nullptr;
}

void ControlSender::setTcpSocket(QTcpSocket *socket)
{
    m_tcpSocket = socket;
    m_socket = nullptr;
    m_controlChannel = nullptr;
}

void ControlSender::setControlChannel(qsc::core::IControlChannel* channel)
{
    m_controlChannel = channel;
    m_socket = nullptr;
    m_tcpSocket = nullptr;
}

void ControlSender::setSendCallback(SendCallback callback)
{
    m_sendCallback = callback;
}

void ControlSender::setCoalesceEnabled(bool enabled)
{
    m_coalesceEnabled = enabled;
    if (!enabled && !m_coalesceBuf.isEmpty()) {
        flushCoalesced();
    }
    qInfo() << "[ControlSender] Coalesce mode:" << (enabled ? "enabled" : "disabled");
}

void ControlSender::start()
{
    if (m_running.load()) return;
    m_running.store(true);

    if (m_tcpSocket) {
        qInfo() << "[ControlSender] Started (TCP immediate mode)";
    } else if (m_controlChannel || m_socket) {
        qInfo() << "[ControlSender] Started (KCP immediate mode)";
    } else {
        qWarning() << "[ControlSender] No channel configured!";
    }
}

void ControlSender::stop()
{
    if (!m_running.load()) return;
    m_running.store(false);

    qInfo() << "[ControlSender] Stopped";
}

// KCP 写入接口
qint64 ControlSender::doWriteKcp(const QByteArray &data)
{
    // 优先使用 IControlChannel 接口
    if (m_controlChannel && m_controlChannel->isConnected()) {
        bool ok = m_controlChannel->send(
            reinterpret_cast<const uint8_t*>(data.constData()),
            data.size());
        return ok ? data.size() : -1;
    }
    if (m_sendCallback) {
        return m_sendCallback(data);
    }
    if (m_socket && m_socket->isValid()) {
        return m_socket->write(data);
    }
    return -1;
}

// TCP 写入接口
qint64 ControlSender::doWriteTcp(const QByteArray &data)
{
    // 优先使用 IControlChannel 接口
    if (m_controlChannel && m_controlChannel->isConnected()) {
        bool ok = m_controlChannel->send(
            reinterpret_cast<const uint8_t*>(data.constData()),
            data.size());
        return ok ? data.size() : -1;
    }
    if (m_sendCallback) {
        return m_sendCallback(data);
    }
    if (m_tcpSocket && m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
        qint64 written = m_tcpSocket->write(data);
        // [优化] 不再每次 write 后同步 flush()
        // Qt 内部写缓冲区会在事件循环返回时自动 flush
        // 高频输入时避免每次发送都阻塞等待内核缓冲区
        return written;
    }
    return -1;
}

bool ControlSender::send(const QByteArray &data)
{
    if (data.isEmpty() || !m_running.load(std::memory_order_relaxed)) {
        return false;
    }

    // [超低延迟优化] 事件循环合并模式
    // 将同一事件循环迭代内的多条消息合并为一次 write()
    // 零额外延迟：如果当前已有 pending 的消息，追加到缓冲区；
    // 否则启动 0ms 定时器，在下次事件循环迭代时一次性发送
    if (m_coalesceEnabled) {
        m_coalesceBuf.append(data);
        if (!m_coalesceTimer->isActive()) {
            m_coalesceTimer->start();
        }
        return true;
    }

    // P-KCP: 直接发送，不重试不sleep
    // KCP 自身已有可靠重传机制，此层重试完全冗余且引入阻塞延迟
    qint64 written = m_tcpSocket ? doWriteTcp(data) : doWriteKcp(data);

    if (written == data.size()) {
        m_sentCount++;
        return true;
    }

    m_droppedCount++;
    return false;
}

void ControlSender::flushCoalesced()
{
    if (m_coalesceBuf.isEmpty()) return;

    qint64 written = m_tcpSocket ? doWriteTcp(m_coalesceBuf) : doWriteKcp(m_coalesceBuf);

    if (written == m_coalesceBuf.size()) {
        m_sentCount++;
        m_batchCount++;
    } else {
        m_droppedCount++;
    }

    m_coalesceBuf.clear();
    // 预留空间，减少下次 append 的内存分配
    m_coalesceBuf.reserve(128);
}
