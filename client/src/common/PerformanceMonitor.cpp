#include "PerformanceMonitor.h"
#include "StringUtils.h"

namespace qsc {

// 单例实例（唯一定义点）
PerformanceMonitor& PerformanceMonitor::instance()
{
    static PerformanceMonitor inst;
    return inst;
}

PerformanceMonitor::PerformanceMonitor()
{
}

// === 视频指标报告 ===

void PerformanceMonitor::reportFps(uint32_t fps)
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

void PerformanceMonitor::reportBytesSent(uint64_t bytes)
{
    m_metrics.bytesSent += bytes;
}

void PerformanceMonitor::reportBytesReceived(uint64_t bytes)
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

std::string PerformanceMonitor::formatSummary() const
{
    auto m = currentMetrics();
    return strutil::format(
        "FPS: %u | \xe8\xa7\xa3\xe7\xa0\x81: %.1fms | \xe6\xb8\xb2\xe6\x9f\x93: %.1fms | \xe7\xbd\x91\xe7\xbb\x9c: %.1fms | \xe4\xb8\xa2\xe5\xb8\xa7: %llu",
        m.fps,
        m.avgDecodeLatencyMs,
        m.avgRenderLatencyMs,
        m.networkLatencyMs,
        static_cast<unsigned long long>(m.droppedFrames));
}

std::string PerformanceMonitor::formatDetailed() const
{
    auto m = currentMetrics();
    double dropPercent = m.totalFrames > 0 ? 100.0 * m.droppedFrames / m.totalFrames : 0;
    return strutil::format(
        "=== \xe8\xa7\x86\xe9\xa2\x91\xe7\xae\xa1\xe7\xba\xbf ===\n"
        "FPS: %u\n"
        "\xe8\xa7\xa3\xe7\xa0\x81\xe5\xbb\xb6\xe8\xbf\x9f: %.2f ms (avg)\n"
        "\xe6\xb8\xb2\xe6\x9f\x93\xe5\xbb\xb6\xe8\xbf\x9f: %.2f ms (avg)\n"
        "\xe6\x80\xbb\xe5\xb8\xa7\xe6\x95\xb0: %llu\n"
        "\xe4\xb8\xa2\xe5\xb8\xa7\xe6\x95\xb0: %llu (%.2f%%)\n"
        "\xe5\xb8\xa7\xe9\x98\x9f\xe5\x88\x97\xe6\xb7\xb1\xe5\xba\xa6: %d\n"
        "\n=== \xe7\xbd\x91\xe7\xbb\x9c ===\n"
        "\xe5\xbb\xb6\xe8\xbf\x9f: %.2f ms\n"
        "\xe5\x8f\x91\xe9\x80\x81: %.1f KB\n"
        "\xe6\x8e\xa5\xe6\x94\xb6: %.1f KB\n"
        "\xe5\xbe\x85\xe5\x8f\x91\xe9\x80\x81: %d bytes\n"
        "KCP\xe9\x87\x8d\xe4\xbc\xa0: %d\n"
        "\n=== \xe8\xbe\x93\xe5\x85\xa5 ===\n"
        "\xe5\xbb\xb6\xe8\xbf\x9f: %.2f ms (avg)\n"
        "\xe5\xb7\xb2\xe5\xa4\x84\xe7\x90\x86: %llu\n"
        "\xe5\xb7\xb2\xe4\xb8\xa2\xe5\xbc\x83: %llu\n"
        "\n=== \xe5\xb8\xa7\xe6\xb1\xa0 ===\n"
        "\xe4\xbd\xbf\xe7\x94\xa8: %d / %d",
        m.fps,
        m.avgDecodeLatencyMs,
        m.avgRenderLatencyMs,
        static_cast<unsigned long long>(m.totalFrames),
        static_cast<unsigned long long>(m.droppedFrames),
        dropPercent,
        m.frameQueueDepth,
        m.networkLatencyMs,
        m.bytesSent / 1024.0,
        m.bytesReceived / 1024.0,
        m.pendingBytes,
        m.kcpRetransmits,
        m.avgInputLatencyMs,
        static_cast<unsigned long long>(m.inputEventsProcessed),
        static_cast<unsigned long long>(m.inputEventsDropped),
        m.framePoolUsed,
        m.framePoolTotal);
}

} // namespace qsc
