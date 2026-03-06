#pragma once
/**
 * @file NativeTcpServer.h
 * @brief 原生 Winsock2 TCP 服务器封装
 *
 * 替代 QTcpServer，用于 adb reverse 模式接受连接。
 * 非阻塞 accept，适合在 timer/事件循环中轮询。
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <cstdint>
#include <string>

#pragma comment(lib, "Ws2_32.lib")

class NativeTcpSocket;

class NativeTcpServer {
public:
    NativeTcpServer();
    ~NativeTcpServer();

    // 禁止拷贝
    NativeTcpServer(const NativeTcpServer&) = delete;
    NativeTcpServer& operator=(const NativeTcpServer&) = delete;

    /**
     * @brief 开始监听
     * @param address 绑定地址 ("127.0.0.1", "0.0.0.0" 等)
     * @param port 端口号
     * @return 成功返回 true
     */
    bool listen(const std::string& address, uint16_t port);

    /**
     * @brief 非阻塞式接受一个连接
     * @return 新连接的 NativeTcpSocket 指针（调用者负责 delete），无连接返回 nullptr
     */
    NativeTcpSocket* tryAccept();

    /**
     * @brief 阻塞式接受一个连接
     * @param timeoutMs 超时毫秒 (-1=无限等待)
     * @return 新连接的 NativeTcpSocket 指针（调用者负责 delete），超时/失败返回 nullptr
     */
    NativeTcpSocket* acceptBlocking(int timeoutMs = -1);

    /**
     * @brief 关闭服务器
     */
    void close();

    bool isListening() const { return m_socket != INVALID_SOCKET; }
    uint16_t port() const { return m_port; }
    SOCKET handle() const { return m_socket; }

private:
    SOCKET m_socket = INVALID_SOCKET;
    uint16_t m_port = 0;
};
