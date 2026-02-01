/**
 * @file KcpCore.h
 * @brief KCP核心封装 - 全新重构
 *
 * 严格按照 kcp-master/test.cpp 的使用方式实现
 * 参考: https://github.com/skywind3000/kcp
 *
 * 使用示例 (参考 test.cpp):
 * @code
 * // 创建KCP对象
 * KcpCore kcp(0x11223344);
 *
 * // 设置输出回调
 * kcp.setOutput([](const char* buf, int len, void* user) {
 *     // 发送UDP数据包
 *     return udpSend(buf, len);
 * });
 *
 * // 配置快速模式
 * kcp.setFastMode();
 * kcp.setWindowSize(128, 128);
 *
 * // 主循环
 * while (running) {
 *     // 更新KCP状态
 *     kcp.update(currentTimeMs());
 *
 *     // 发送数据
 *     kcp.send(data, len);
 *
 *     // 输入UDP数据
 *     kcp.input(udpData, udpLen);
 *
 *     // 接收数据
 *     while (kcp.peekSize() > 0) {
 *         int n = kcp.recv(buffer, bufSize);
 *         if (n > 0) processData(buffer, n);
 *     }
 * }
 * @endcode
 */

#ifndef KCP_CORE_H
#define KCP_CORE_H

#include <cstdint>
#include <functional>
#include <shared_mutex>

extern "C" {
#include "ikcp.h"
}

/**
 * @brief KCP核心类 - 对ikcp的直接封装
 *
 * 设计原则:
 * - 最小化封装，不添加额外复杂性
 * - 严格遵循kcp原始API设计
 * - 线程安全
 */
class KcpCore
{
public:
    // 输出回调类型 (参考 test.cpp 的 udp_output)
    using OutputCallback = std::function<int(const char *buf, int len, void *user)>;

    /**
     * @brief 构造函数
     * @param conv 会话ID，通信双方必须相同（参考 test.cpp: ikcp_create(0x11223344, user)）
     * @param user 用户数据指针，传递给输出回调
     */
    explicit KcpCore(uint32_t conv, void *user = nullptr);

    ~KcpCore();

    // 禁止拷贝
    KcpCore(const KcpCore &) = delete;
    KcpCore &operator=(const KcpCore &) = delete;

    //=========================================================================
    // 基本操作 - 对应 ikcp 的核心API
    //=========================================================================

    /**
     * @brief 设置输出回调 (参考 test.cpp: kcp->output = udp_output)
     */
    void setOutput(OutputCallback callback);

    /**
     * @brief 发送数据 (参考 test.cpp: ikcp_send)
     * @return 0成功，<0失败
     */
    int send(const char *data, int len);

    /**
     * @brief 接收数据 (参考 test.cpp: ikcp_recv)
     * @return 实际接收字节数，<0无数据
     */
    int recv(char *buffer, int len);

    /**
     * @brief 输入下层协议数据 (参考 test.cpp: ikcp_input)
     * @return 0成功，<0失败
     */
    int input(const char *data, int size);

    /**
     * @brief 更新KCP状态 (参考 test.cpp: ikcp_update)
     * @param current 当前时间戳（毫秒）
     */
    void update(uint32_t current);

    /**
     * @brief 刷新发送 (参考 test.cpp: 无直接调用，update内部会调用)
     */
    void flush();

    /**
     * @brief 查看下一个消息大小 (参考 README: ikcp_peeksize)
     * @return 消息大小，<0无完整消息
     */
    int peekSize() const;

    /**
     * @brief 获取待发送数据包数量 (参考 README: ikcp_waitsnd)
     */
    int waitSnd() const;

    /**
     * @brief 检查下次需要调用update的时间 (参考 README: ikcp_check)
     */
    uint32_t check(uint32_t current) const;

    //=========================================================================
    // 配置 - 对应 README.md 中的协议配置
    //=========================================================================

    /**
     * @brief 快速模式 (参考 test.cpp mode=2)
     *
     * 配置: nodelay=2, interval=10, resend=2, nc=1
     * 适用于游戏、投屏等低延迟场景
     */
    void setFastMode();

    /**
     * @brief 普通模式 (参考 test.cpp mode=1)
     *
     * 配置: nodelay=0, interval=10, resend=0, nc=1
     */
    void setNormalMode();

    /**
     * @brief 默认模式 (参考 test.cpp mode=0)
     *
     * 类似TCP，配置: nodelay=0, interval=10, resend=0, nc=0
     */
    void setDefaultMode();

    /**
     * @brief 设置nodelay参数 (参考 README: ikcp_nodelay)
     *
     * @param nodelay 0:关闭(默认) 1:启用 2:激进模式
     * @param interval 内部更新间隔(毫秒)，默认100，建议10-40
     * @param resend 快速重传，0关闭(默认)，建议2
     * @param nc 关闭拥塞控制，0不关(默认)，1关闭
     */
    void setNoDelay(int nodelay, int interval, int resend, int nc);

    /**
     * @brief 设置窗口大小 (参考 test.cpp: ikcp_wndsize)
     * @param sndwnd 发送窗口，默认32
     * @param rcvwnd 接收窗口，默认128
     */
    void setWindowSize(int sndwnd, int rcvwnd);

    /**
     * @brief 设置MTU (参考 README: ikcp_setmtu)
     * @param mtu 最大传输单元，默认1400
     */
    int setMtu(int mtu);

    /**
     * @brief 设置最小RTO (参考 test.cpp: kcp->rx_minrto)
     */
    void setMinRto(int minrto);

    /**
     * @brief 设置流模式
     * @param stream 0:消息模式(默认) 1:流模式
     */
    void setStream(int stream);

    //=========================================================================
    // 状态查询
    //=========================================================================

    uint32_t conv() const { return m_conv; }
    bool isValid() const { return m_kcp != nullptr; }

    /**
     * @brief 获取连接状态
     * @return 0正常，-1死链
     */
    int state() const;

private:
    // KCP输出回调（静态，传递给ikcp）
    static int kcpOutputCallback(const char *buf, int len, ikcpcb *kcp, void *user);

private:
    ikcpcb *m_kcp = nullptr;
    uint32_t m_conv;
    void *m_user = nullptr;
    OutputCallback m_output;
    mutable std::shared_mutex m_mutex;  // C-K01: 使用读写锁优化，只读方法用 shared_lock
};

#endif // KCP_CORE_H
