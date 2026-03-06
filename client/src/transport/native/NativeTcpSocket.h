#pragma once
/**
 * @file NativeTcpSocket.h
 * @brief 原生 Winsock2 TCP Socket 封装
 *
 * 替代 QTcpSocket，用于数据传输层。
 * 提供阻塞式 send/recv，零 Qt 依赖。
 * 线程安全：同一 socket 可在不同线程 send/recv（OS 层面安全）。
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <cstdint>
#include <string>
#include <atomic>

#pragma comment(lib, "Ws2_32.lib")

class NativeTcpSocket {
public:
    NativeTcpSocket();

    /// 接管已有 SOCKET（如来自 accept）
    explicit NativeTcpSocket(SOCKET socket);

    ~NativeTcpSocket();

    // 禁止拷贝
    NativeTcpSocket(const NativeTcpSocket&) = delete;
    NativeTcpSocket& operator=(const NativeTcpSocket&) = delete;

    // 移动语义
    NativeTcpSocket(NativeTcpSocket&& other) noexcept;
    NativeTcpSocket& operator=(NativeTcpSocket&& other) noexcept;

    // === 连接管理 ===

    /**
     * @brief 连接到远程主机
     * @param host 主机名或 IP
     * @param port 端口号
     * @param timeoutMs 超时毫秒 (-1=无超时)
     * @return 成功返回 true
     */
    bool connectToHost(const std::string& host, uint16_t port, int timeoutMs = 5000);

    /**
     * @brief 关闭 socket (graceful shutdown + closesocket)
     */
    void close();

    /**
     * @brief 请求停止阻塞 I/O（线程安全）
     * 设置停止标志并 shutdown socket，使阻塞的 recv/send 立即返回。
     */
    void requestStop();

    // === 阻塞 I/O ===

    /**
     * @brief 发送数据（阻塞，保证全部发送或失败）
     * @return 实际发送字节数，-1=错误
     */
    int send(const char* data, int len);

    /**
     * @brief 接收数据（阻塞，至多 maxLen 字节）
     * @return 实际接收字节数，0=对端关闭，-1=错误
     */
    int recv(char* buf, int maxLen);

    /**
     * @brief 接收精确 N 字节（阻塞循环 recv）
     * @return 成功返回 true，失败或断开返回 false
     */
    bool recvAll(char* buf, int len);

    // === 状态查询 ===

    bool isValid() const { return m_socket != INVALID_SOCKET; }
    SOCKET handle() const { return m_socket; }

    /**
     * @brief 释放 socket 所有权（调用者负责关闭）
     */
    SOCKET release();

    // === Socket 选项 ===

    void setNoDelay(bool enable);
    void setRecvBufferSize(int size);
    void setSendBufferSize(int size);
    void setRecvTimeout(int ms);
    void setSendTimeout(int ms);

    // === 全局 WSA 初始化 ===

    static void ensureWsaInit();

private:
    SOCKET m_socket = INVALID_SOCKET;
    std::atomic<bool> m_stopRequested{false};

    static std::atomic<bool> s_wsaInitialized;
};
