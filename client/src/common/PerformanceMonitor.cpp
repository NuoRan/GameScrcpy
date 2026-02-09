#include "PerformanceMonitor.h"
#include <QThread>

namespace qsc {

// 单例实例（唯一定义点）
PerformanceMonitor& PerformanceMonitor::instance()
{
    static PerformanceMonitor inst;
    return inst;
}

PerformanceMonitor::PerformanceMonitor()
{
    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, [this]() {
        if (m_enabled) {
            emit metricsUpdated(currentMetrics());
        }
    });
}

// === 视频指标报告 ===

void PerformanceMonitor::reportFps(quint32 fps)
{
    m_metrics.fps = fps;
}

void PerformanceMonitor::reportDecodeLatency(double latencyMs)
{
    m_decodeLatency.addSample(latencyMs);
}

void PerformanceMonitor::reportRenderLatency(double latencyMs)
{
    m_renderLatency.addSample(latencyMs);
}

void PerformanceMonitor::reportFrameDecoded()
{
    m_metrics.totalFrames++;
}

void PerformanceMonitor::reportFrameDropped()
{
    m_metrics.droppedFrames++;
}

void PerformanceMonitor::reportFrameQueueDepth(int depth)
{
    m_metrics.frameQueueDepth = depth;
}

// === 网络指标报告 ===

void PerformanceMonitor::reportNetworkLatency(double latencyMs)
{
    m_networkLatency.addSample(latencyMs);
}

void PerformanceMonitor::reportBytesSent(quint64 bytes)
{
    m_metrics.bytesSent += bytes;
}

void PerformanceMonitor::reportBytesReceived(quint64 bytes)
{
    m_metrics.bytesReceived += bytes;
}

void PerformanceMonitor::reportPendingBytes(int bytes)
{
    m_metrics.pendingBytes = bytes;
}

void PerformanceMonitor::reportKcpRetransmit()
{
    m_metrics.kcpRetransmits++;
}

// === 输入指标报告 ===

void PerformanceMonitor::reportInputLatency(double latencyMs)
{
    m_inputLatency.addSample(latencyMs);
}

void PerformanceMonitor::reportInputProcessed()
{
    m_metrics.inputEventsProcessed++;
}

void PerformanceMonitor::reportInputDropped()
{
    m_metrics.inputEventsDropped++;
}

// === 内存指标报告 ===

void PerformanceMonitor::reportFramePoolUsage(int used, int total)
{
    m_metrics.framePoolUsed = used;
    m_metrics.framePoolTotal = total;
}

// === 获取当前指标 ===

PerformanceMetrics PerformanceMonitor::currentMetrics() const
{
    PerformanceMetrics m = m_metrics;
    m.avgDecodeLatencyMs = m_decodeLatency.average();
    m.avgRenderLatencyMs = m_renderLatency.average();
    m.networkLatencyMs = m_networkLatency.average();
    m.avgInputLatencyMs = m_inputLatency.average();
    return m;
}

// === 控制 ===

void PerformanceMonitor::setEnabled(bool enabled)
{
    m_enabled = enabled;

    // 定时器操作必须在创建它的线程中执行
    if (QThread::currentThread() != m_updateTimer->thread()) {
        // 跨线程调用：使用 invokeMethod
        QMetaObject::invokeMethod(m_updateTimer, [this, enabled]() {
            if (enabled && !m_updateTimer->isActive()) {
                m_updateTimer->start(1000);
            } else if (!enabled) {
                m_updateTimer->stop();
            }
        }, Qt::QueuedConnection);
    } else {
        // 同线程：直接操作
        if (enabled && !m_updateTimer->isActive()) {
            m_updateTimer->start(1000);
        } else if (!enabled) {
            m_updateTimer->stop();
        }
    }
}

bool PerformanceMonitor::isEnabled() const
{
    return m_enabled;
}

void PerformanceMonitor::reset()
{
    m_metrics = PerformanceMetrics();
    m_decodeLatency.reset();
    m_renderLatency.reset();
    m_networkLatency.reset();
    m_inputLatency.reset();
}

// === 格式化输出 ===

QString PerformanceMonitor::formatSummary() const
{
    auto m = currentMetrics();
    return QString("FPS: %1 | 解码: %2ms | 渲染: %3ms | 网络: %4ms | 丢帧: %5")
        .arg(m.fps)
        .arg(m.avgDecodeLatencyMs, 0, 'f', 1)
        .arg(m.avgRenderLatencyMs, 0, 'f', 1)
        .arg(m.networkLatencyMs, 0, 'f', 1)
        .arg(m.droppedFrames);
}

QString PerformanceMonitor::formatDetailed() const
{
    auto m = currentMetrics();
    return QString(
        "=== 视频管线 ===\n"
        "FPS: %1\n"
        "解码延迟: %2 ms (avg)\n"
        "渲染延迟: %3 ms (avg)\n"
        "总帧数: %4\n"
        "丢帧数: %5 (%6%)\n"
        "帧队列深度: %7\n"
        "\n=== 网络 ===\n"
        "延迟: %8 ms\n"
        "发送: %9 KB\n"
        "接收: %10 KB\n"
        "待发送: %11 bytes\n"
        "KCP重传: %12\n"
        "\n=== 输入 ===\n"
        "延迟: %13 ms (avg)\n"
        "已处理: %14\n"
        "已丢弃: %15\n"
        "\n=== 帧池 ===\n"
        "使用: %16 / %17"
    )
    .arg(m.fps)
    .arg(m.avgDecodeLatencyMs, 0, 'f', 2)
    .arg(m.avgRenderLatencyMs, 0, 'f', 2)
    .arg(m.totalFrames)
    .arg(m.droppedFrames)
    .arg(m.totalFrames > 0 ? 100.0 * m.droppedFrames / m.totalFrames : 0, 0, 'f', 2)
    .arg(m.frameQueueDepth)
    .arg(m.networkLatencyMs, 0, 'f', 2)
    .arg(m.bytesSent / 1024.0, 0, 'f', 1)
    .arg(m.bytesReceived / 1024.0, 0, 'f', 1)
    .arg(m.pendingBytes)
    .arg(m.kcpRetransmits)
    .arg(m.avgInputLatencyMs, 0, 'f', 2)
    .arg(m.inputEventsProcessed)
    .arg(m.inputEventsDropped)
    .arg(m.framePoolUsed)
    .arg(m.framePoolTotal);
}

} // namespace qsc
