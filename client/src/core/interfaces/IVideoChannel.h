#ifndef CORE_IVIDEOCHANNEL_H
#define CORE_IVIDEOCHANNEL_H

#include <cstdint>
#include <functional>

namespace qsc {
namespace core {

/**
 * @brief 视频数据通道接口 / Video Data Channel Interface
 *
 * 定义视频数据传输通道的通用接口，支持多种实现 / Generic video transport interface, multiple implementations:
 * - KcpVideoChannel: KCP/UDP 传输 (WiFi 低延迟) / KCP/UDP (WiFi low latency)
 * - TcpVideoChannel: TCP 传输 (USB 稳定) / TCP (USB stable)
 */
class IVideoChannel {
public:
    virtual ~IVideoChannel() = default;

    /**
     * @brief 数据接收回调类型
     * @param data 接收到的数据
     * @param size 数据大小
     */
    using DataCallback = std::function<void(const uint8_t* data, int size)>;

    /**
     * @brief 连接到视频源
     * @param host 主机地址 (IP 或 localhost)
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
     * @brief 阻塞式接收数据
     * @param buf 接收缓冲区
     * @param size 缓冲区大小
     * @return 实际接收的字节数，-1 表示错误
     */
    virtual int32_t recv(uint8_t* buf, int32_t size) = 0;

    /**
     * @brief 设置数据回调（可选，用于异步模式）
     */
    virtual void setDataCallback(DataCallback callback) = 0;

    /**
     * @brief 获取通道类型名称 (用于调试)
     */
    virtual const char* typeName() const = 0;
};

} // namespace core
} // namespace qsc

#endif // CORE_IVIDEOCHANNEL_H
