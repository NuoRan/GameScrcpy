/**
 * @file KcpCore.cpp
 * @brief KCP核心实现 - 全新重构
 *
 * 严格按照 kcp-master 的 test.cpp 和 README.md 实现
 */

#include "KcpCore.h"
#include <cstring>

//=============================================================================
// 构造/析构
//=============================================================================

KcpCore::KcpCore(uint32_t conv, void *user)
    : m_conv(conv)
    , m_user(user)
{
    // 创建KCP对象 (参考 test.cpp: ikcp_create(0x11223344, (void*)0))
    m_kcp = ikcp_create(conv, this);
    if (m_kcp) {
        // 设置输出回调 (参考 test.cpp: kcp->output = udp_output)
        m_kcp->output = &KcpCore::kcpOutputCallback;
    }
}

KcpCore::~KcpCore()
{
    // 释放KCP对象 (参考 test.cpp: ikcp_release)
    if (m_kcp) {
        ikcp_release(m_kcp);
        m_kcp = nullptr;
    }
}

//=============================================================================
// 基本操作
//=============================================================================

void KcpCore::setOutput(OutputCallback callback)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_output = std::move(callback);
}

int KcpCore::send(const char *data, int len)
{
    if (!m_kcp || !data || len <= 0) {
        return -1;
    }

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    return ikcp_send(m_kcp, data, len);
}

int KcpCore::recv(char *buffer, int len)
{
    if (!m_kcp || !buffer || len <= 0) {
        return -1;
    }

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    return ikcp_recv(m_kcp, buffer, len);
}

int KcpCore::input(const char *data, int size)
{
    if (!m_kcp || !data || size <= 0) {
        return -1;
    }

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    return ikcp_input(m_kcp, data, size);
}

void KcpCore::update(uint32_t current)
{
    if (!m_kcp) {
        return;
    }

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    ikcp_update(m_kcp, current);
}

void KcpCore::flush()
{
    if (!m_kcp) {
        return;
    }

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    ikcp_flush(m_kcp);
}

int KcpCore::peekSize() const
{
    if (!m_kcp) {
        return -1;
    }

    std::shared_lock<std::shared_mutex> lock(m_mutex);  // C-K02: 只读方法使用共享锁
    return ikcp_peeksize(m_kcp);
}

int KcpCore::waitSnd() const
{
    if (!m_kcp) {
        return 0;
    }

    std::shared_lock<std::shared_mutex> lock(m_mutex);  // 只读方法使用共享锁
    return ikcp_waitsnd(m_kcp);
}

uint32_t KcpCore::check(uint32_t current) const
{
    if (!m_kcp) {
        return current;
    }

    std::shared_lock<std::shared_mutex> lock(m_mutex);  // 只读方法使用共享锁
    return ikcp_check(m_kcp, current);
}

//=============================================================================
// 配置
//=============================================================================

void KcpCore::setFastMode()
{
    if (!m_kcp) return;

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    // 快速模式配置: nodelay=2, interval=10, resend=2, nc=1
    ikcp_nodelay(m_kcp, 2, 10, 2, 1);
    m_kcp->rx_minrto = 10;
    m_kcp->fastresend = 1;
    ikcp_wndsize(m_kcp, 128, 128);
}

void KcpCore::setNormalMode()
{
    if (!m_kcp) return;

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    ikcp_nodelay(m_kcp, 0, 10, 0, 1);
    ikcp_wndsize(m_kcp, 128, 128);
}

void KcpCore::setDefaultMode()
{
    if (!m_kcp) return;

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    ikcp_nodelay(m_kcp, 0, 10, 0, 0);
    ikcp_wndsize(m_kcp, 32, 128);
}

void KcpCore::setNoDelay(int nodelay, int interval, int resend, int nc)
{
    if (!m_kcp) return;

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    ikcp_nodelay(m_kcp, nodelay, interval, resend, nc);
}

void KcpCore::setWindowSize(int sndwnd, int rcvwnd)
{
    if (!m_kcp) return;

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    ikcp_wndsize(m_kcp, sndwnd, rcvwnd);
}

int KcpCore::setMtu(int mtu)
{
    if (!m_kcp) return -1;

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    return ikcp_setmtu(m_kcp, mtu);
}

void KcpCore::setMinRto(int minrto)
{
    if (!m_kcp) return;

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_kcp->rx_minrto = minrto;
}

void KcpCore::setStream(int stream)
{
    if (!m_kcp) return;

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_kcp->stream = stream;
}

int KcpCore::state() const
{
    if (!m_kcp) return -1;

    std::shared_lock<std::shared_mutex> lock(m_mutex);  // 只读方法使用共享锁
    return static_cast<int>(m_kcp->state);
}

//=============================================================================
// 私有方法
//=============================================================================

int KcpCore::kcpOutputCallback(const char *buf, int len, ikcpcb *kcp, void *user)
{
    // 参考 test.cpp: udp_output 回调
    auto *self = static_cast<KcpCore *>(user);
    if (!self || !self->m_output) {
        return -1;
    }

    // 调用用户设置的输出回调
    return self->m_output(buf, len, self->m_user);
}
