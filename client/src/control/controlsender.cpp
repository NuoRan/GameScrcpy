#define LOG_TAG "ControlSender"
#include "Logger.h"

#include "controlsender.h"
#include "kcpcontrolsocket.h"
#include "NativeTcpSocket.h"
#include "interfaces/IControlChannel.h"

/**
 * 即时发送模式
 *
 * 控制消息直接发送，不经过队列缓冲，最低延迟。
 * - KCP 模式：直接调用 KCP 写入（线程安全）
 * - TCP 模式：直接调用 TCP 写入（需在主线程）
 */

ControlSender::ControlSender()
{
    // 零延迟合并定时器，使用 0ms 单次定时器在下一次事件循环迭代时 flush
    m_coalesceTimer.setSingleShot(true);
    m_coalesceTimer.setInterval(0);
    m_coalesceTimer.setCallback([this]() { flushCoalesced(); });
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

void ControlSender::setTcpSocket(NativeTcpSocket *socket)
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
    if (!enabled && !m_coalesceBuf.empty()) {
        flushCoalesced();
    }
    LOGI() << "[ControlSender] Coalesce mode:" << (enabled ? "enabled" : "disabled");
}

void ControlSender::start()
{
    if (m_running.load()) return;
    m_running.store(true);

    if (m_tcpSocket) {
        LOGI() << "[ControlSender] Started (TCP immediate mode)";
    } else if (m_controlChannel || m_socket) {
        LOGI() << "[ControlSender] Started (KCP immediate mode)";
    } else {
        LOGW() << "[ControlSender] No channel configured!";
    }
}

void ControlSender::stop()
{
    if (!m_running.load()) return;
    m_running.store(false);

    LOGI() << "[ControlSender] Stopped";
}

// 统一写入接口
int64_t ControlSender::doWrite(const char *data, int len)
{
    // 优先使用 IControlChannel 接口
    if (m_controlChannel && m_controlChannel->isConnected()) {
        bool ok = m_controlChannel->send(
            reinterpret_cast<const uint8_t*>(data), len);
        return ok ? len : -1;
    }
    if (m_sendCallback) {
        return m_sendCallback(data, len);
    }
    if (m_tcpSocket && m_tcpSocket->isValid()) {
        return m_tcpSocket->send(data, len);
    }
    if (m_socket && m_socket->isValid()) {
        return m_socket->write(data, len);
    }
    return -1;
}

bool ControlSender::send(const char *data, int len)
{
    if (!data || len <= 0 || !m_running.load(std::memory_order_relaxed)) {
        return false;
    }

    // 事件循环合并：同一迭代内的消息追加到缓冲区，下次迭代一次性发送
    if (m_coalesceEnabled) {
        m_coalesceBuf.insert(m_coalesceBuf.end(),
            reinterpret_cast<const uint8_t*>(data),
            reinterpret_cast<const uint8_t*>(data) + len);
        if (!m_coalesceTimer.isActive()) {
            m_coalesceTimer.start();
        }
        return true;
    }

    int64_t written = doWrite(data, len);

    if (written == len) {
        m_sentCount++;
        return true;
    }

    m_droppedCount++;
    return false;
}

void ControlSender::flushCoalesced()
{
    if (m_coalesceBuf.empty()) return;

    int bufLen = static_cast<int>(m_coalesceBuf.size());
    int64_t written = doWrite(reinterpret_cast<const char*>(m_coalesceBuf.data()), bufLen);

    if (written == bufLen) {
        m_sentCount++;
        m_batchCount++;
    } else {
        m_droppedCount++;
    }

    m_coalesceBuf.clear();
    m_coalesceBuf.reserve(128);
}
