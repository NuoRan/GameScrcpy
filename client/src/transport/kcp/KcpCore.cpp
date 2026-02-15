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

    // ============================================
    // 极致低延迟配置
    // ============================================

    // nodelay=2: 最激进模式，立即发送不等待
    // interval=1: 内部更新间隔1ms（最小值）
    // resend=2: 2次ACK跨越立即重传
    // nc=1: 关闭拥塞控制，不减速
    ikcp_nodelay(m_kcp, 2, 1, 2, 1);

    // 最小RTO=1ms（默认30ms，我们追求极致）
    m_kcp->rx_minrto = 1;

    // 快速重传触发阈值
    m_kcp->fastresend = 1;

    // 死链检测（可选，避免无限等待）
    m_kcp->dead_link = 50;

    // 窗口大小：发送256，接收256（足够8Mbps@60fps）
    ikcp_wndsize(m_kcp, 256, 256);
}

void KcpCore::setVideoStreamMode()
{
    if (!m_kcp) return;

    std::unique_lock<std::shared_mutex> lock(m_mutex);

    // ============================================
    // 视频流专用配置 - 针对高码率视频流优化
    // ============================================

    // 最激进的nodelay配置
    ikcp_nodelay(m_kcp, 2, 1, 2, 1);

    // 最小RTO
    m_kcp->rx_minrto = 1;
    m_kcp->fastresend = 1;

    // 流模式：视频不需要消息边界
    m_kcp->stream = 1;

    // 大窗口：支持高码率
    ikcp_wndsize(m_kcp, 512, 512);

    // 死链检测宽松一点（视频偶尔卡顿正常）
    m_kcp->dead_link = 100;
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

int KcpCore::getRtt() const
{
    if (!m_kcp) return 0;
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_kcp->rx_srtt;
}

int KcpCore::processInputBatch(const char* const* data, const int* sizes, int count, uint32_t current)
{
    if (!m_kcp || !data || !sizes || count <= 0) return -1;

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    for (int i = 0; i < count; ++i) {
        if (data[i] && sizes[i] > 0) {
            ikcp_input(m_kcp, data[i], sizes[i]);
        }
    }
    ikcp_update(m_kcp, current);
    return ikcp_peeksize(m_kcp);
}

int KcpCore::recvAll(char *buffer, int maxLen)
{
    if (!m_kcp || !buffer || maxLen <= 0) return 0;

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    int totalRecv = 0;
    int peekSz;
    while ((peekSz = ikcp_peeksize(m_kcp)) > 0) {
        if (totalRecv + peekSz > maxLen) break;  // 缓冲区不足，停止
        int n = ikcp_recv(m_kcp, buffer + totalRecv, maxLen - totalRecv);
        if (n < 0) break;
        totalRecv += n;
    }
    return totalRecv;
}
