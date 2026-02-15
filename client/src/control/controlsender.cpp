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
    // 零延迟合并定时器，使用 0ms 单次定时器在下一次事件循环迭代时 flush
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

// 统一写入接口
qint64 ControlSender::doWrite(const QByteArray &data)
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
        return m_tcpSocket->write(data);
    }
    if (m_socket && m_socket->isValid()) {
        return m_socket->write(data);
    }
    return -1;
}

bool ControlSender::send(const QByteArray &data)
{
    if (data.isEmpty() || !m_running.load(std::memory_order_relaxed)) {
        return false;
    }

    // 事件循环合并：同一迭代内的消息追加到缓冲区，下次迭代一次性发送
    if (m_coalesceEnabled) {
        m_coalesceBuf.append(data);
        if (!m_coalesceTimer->isActive()) {
            m_coalesceTimer->start();
        }
        return true;
    }

    qint64 written = doWrite(data);

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

    qint64 written = doWrite(m_coalesceBuf);

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
