#ifndef CORE_ICONTROLCHANNEL_H
#define CORE_ICONTROLCHANNEL_H

#include <cstdint>

namespace qsc {
namespace core {

/**
 * @brief 控制命令通道接口 / Control Command Channel Interface
 *
 * 定义控制命令传输通道的通用接口，支持多种实现 / Defines generic interface for control channel, supporting multiple implementations:
 * - KcpControlChannel: KCP/UDP 传输 / KCP/UDP transport
 * - TcpControlChannel: TCP 传输 / TCP transport
 *
 * 控制命令包括：触摸事件、按键事件、系统命令等
 * Control commands include: touch events, key events, system commands, etc.
 */
class IControlChannel {
public:
    virtual ~IControlChannel() = default;

    /**
     * @brief 连接到控制服务
     * @param host 主机地址
     * @param port 端口号
     * @return 成功返回 true
     */
    virtual bool connect(const char* host, uint16_t port) = 0;

    /**
     * @brief 断开连接
     */
    virtual void disconnect() = 0;

    /**
     * @brief 是否已连接
     */
    virtual bool isConnected() const = 0;

    /**
     * @brief 发送控制数据 (非阻塞)
     * @param data 数据指针
     * @param size 数据大小
     * @return 成功返回 true
     */
    virtual bool send(const uint8_t* data, int32_t size) = 0;

    /**
     * @brief 发送触摸事件 (FastMsg 协议)
     * @param seqId 序列 ID
     * @param action 动作 (DOWN/MOVE/UP)
     * @param x 归一化 X 坐标 (0-65535)
     * @param y 归一化 Y 坐标 (0-65535)
     * @return 成功返回 true
     */
    virtual bool sendTouch(uint32_t seqId, uint8_t action, uint16_t x, uint16_t y) = 0;

    /**
     * @brief 发送按键事件
     * @param action 动作 (DOWN/UP)
     * @param keycode Android 键码
     * @return 成功返回 true
     */
    virtual bool sendKey(uint8_t action, int32_t keycode) = 0;

    /**
     * @brief 获取通道类型名称 (用于调试)
     */
    virtual const char* typeName() const = 0;
};

} // namespace core
} // namespace qsc

#endif // CORE_ICONTROLCHANNEL_H
