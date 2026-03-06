#ifndef CONTROLSENDER_H
#define CONTROLSENDER_H

#include "NativeTimer.h"
#include "GameSignal.h"

class NativeTcpSocket;
#include <string>
#include <functional>
#include <atomic>
#include <vector>
#include <cstdint>

class KcpControlSocket;

namespace qsc { namespace core { class IControlChannel; } }

using SendCallback = std::function<int64_t(const char*, int)>;

/**
 * 控制消息即时发送器 / Control Message Instant Sender
 *
 * 直接发送模式，无队列缓冲，最低延迟。
 * Direct send mode, no queue buffering, minimal latency.
 * - KCP 模式：直接调用 KCP 写入 / KCP mode: direct KCP write
 * - TCP 模式：直接调用 TCP 写入 / TCP mode: direct TCP write
 * 支持事件循环合并模式，同一迭代内的多次 send() 合并为一次系统调用
 */
class ControlSender
{
public:
    explicit ControlSender();
    ~ControlSender();

    // 设置目标 KCP socket (WiFi 模式)
    void setSocket(KcpControlSocket *socket);

    // 设置目标 TCP socket (USB 模式)
    void setTcpSocket(NativeTcpSocket *socket);

    // 设置控制通道接口
    void setControlChannel(qsc::core::IControlChannel* channel);

    // 设置发送回调函数
    void setSendCallback(SendCallback callback);

    // 启用/禁用事件循环合并模式（同一迭代内多条消息合并为一次写入）
    void setCoalesceEnabled(bool enabled);

    // 发送数据（即时发送）
    bool send(const char *data, int len);
    bool send(const std::vector<uint8_t> &data) { return send(reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size())); }

    // 启动/停止发送器
    void start();
    void stop();

    // 获取统计信息
    uint64_t droppedCount() const { return m_droppedCount; }
    uint64_t sentCount() const { return m_sentCount; }
    uint64_t batchCount() const { return m_batchCount; }

    Signal<const std::string&> sendError;

private:
    void flushCoalesced();

private:
    int64_t doWrite(const char *data, int len);

private:
    KcpControlSocket *m_socket = nullptr;
    NativeTcpSocket *m_tcpSocket = nullptr;
    qsc::core::IControlChannel* m_controlChannel = nullptr;
    SendCallback m_sendCallback;

    std::atomic<bool> m_running{false};

    // 事件循环合并
    bool m_coalesceEnabled = false;
    std::vector<uint8_t> m_coalesceBuf;
    NativeTimer m_coalesceTimer;

    // 统计
    uint64_t m_droppedCount = 0;
    uint64_t m_sentCount = 0;
    uint64_t m_batchCount = 0;  // 合并批次计数
};

#endif // CONTROLSENDER_H
