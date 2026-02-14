#ifndef PERFORMANCEMONITOR_H
#define PERFORMANCEMONITOR_H

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <atomic>

namespace qsc {

/**
 * @brief 性能指标数据结构 / Performance Metrics Data Structure
 */
struct PerformanceMetrics {
    // 视频管线指标 / Video pipeline metrics
    quint32 fps = 0;                    // 当前帧率 / Current FPS
    double avgDecodeLatencyMs = 0;      // 平均解码延迟 (ms) / Average decode latency (ms)
    double avgRenderLatencyMs = 0;      // 平均渲染延迟 (ms) / Average render latency (ms)
    quint64 totalFrames = 0;            // 总帧数 / Total frames
    quint64 droppedFrames = 0;          // 丢帧数 / Dropped frames
    int frameQueueDepth = 0;            // 帧队列深度 / Frame queue depth

    // 网络指标 / Network metrics
    double networkLatencyMs = 0;        // 网络延迟 (ms) / Network latency (ms)
    quint64 bytesSent = 0;              // 发送字节数 / Bytes sent
    quint64 bytesReceived = 0;          // 接收字节数 / Bytes received
    int pendingBytes = 0;               // 待发送字节数 / Pending bytes
    int kcpRetransmits = 0;             // KCP 重传次数 / KCP retransmissions

    // 输入指标 / Input metrics
    double avgInputLatencyMs = 0;       // 平均输入延迟 (ms) / Average input latency (ms)
    quint64 inputEventsProcessed = 0;   // 已处理输入事件数 / Input events processed
    quint64 inputEventsDropped = 0;     // 丢弃的输入事件数 / Input events dropped

    // 内存指标 / Memory metrics
    quint64 memoryUsageBytes = 0;       // 内存使用 (字节) / Memory usage (bytes)
    int framePoolUsed = 0;              // 已使用帧池数 / Frame pool used
    int framePoolTotal = 0;             // 帧池总数 / Frame pool total

    // 系统指标 / System metrics
    double cpuUsagePercent = 0;         // CPU 使用率 / CPU usage percent
    double gpuUsagePercent = 0;         // GPU 使用率 / GPU usage percent
};

/**
 * @brief 无锁滑动窗口延迟统计器 / Lock-free Sliding Window Latency Tracker
 *
 * 替代原 QMutex + std::deque 的有锁版本。
 * 使用固定大小数组 + 原子写索引，addSample 完全无锁。
 * average/min/max 无锁读取（可能读到部分更新的值，但对统计信息可接受）。
 */
class LatencyTracker {
public:
    explicit LatencyTracker(int windowSize = 60)
        : m_windowSize(qMin(windowSize, MAX_SAMPLES)) {}

    void addSample(double latencyMs) {
        // 无锁: 仅原子递增写索引
        int idx = m_writeIndex.fetch_add(1, std::memory_order_relaxed) % m_windowSize;
        m_samples[idx] = latencyMs;
        int cur = m_count.load(std::memory_order_relaxed);
        if (cur < m_windowSize) {
            m_count.compare_exchange_weak(cur, cur + 1, std::memory_order_relaxed);
        }
    }

    double average() const {
        int count = m_count.load(std::memory_order_relaxed);
        if (count == 0) return 0;
        double sum = 0;
        for (int i = 0; i < count; ++i) sum += m_samples[i];
        return sum / count;
    }

    double max() const {
        int count = m_count.load(std::memory_order_relaxed);
        if (count == 0) return 0;
        double maxVal = 0;
        for (int i = 0; i < count; ++i) {
            double v = m_samples[i];
            if (v > maxVal) maxVal = v;
        }
        return maxVal;
    }

    double min() const {
        int count = m_count.load(std::memory_order_relaxed);
        if (count == 0) return 0;
        double minVal = m_samples[0];
        for (int i = 1; i < count; ++i) {
            double v = m_samples[i];
            if (v < minVal) minVal = v;
        }
        return minVal;
    }

    void reset() {
        m_writeIndex.store(0, std::memory_order_relaxed);
        m_count.store(0, std::memory_order_relaxed);
    }

private:
    static constexpr int MAX_SAMPLES = 128;  // 固定上限，2 的幂
    double m_samples[MAX_SAMPLES] = {};       // 固定大小数组，无堆分配
    std::atomic<int> m_writeIndex{0};         // 原子写索引
    std::atomic<int> m_count{0};              // 当前样本数（≤ windowSize）
    int m_windowSize;
};

/**
 * @brief 性能监控器 (单例)
 *
 * 收集和汇总所有性能指标，定期发送更新信号。
 *
 * 使用方式：
 * 1. 在各模块中报告指标：
 *    PerformanceMonitor::instance().reportDecodeLatency(latencyMs);
 *    PerformanceMonitor::instance().reportFrameDropped();
 *
 * 2. 在 UI 中连接信号：
 *    connect(&PerformanceMonitor::instance(), &PerformanceMonitor::metricsUpdated,
 *            this, &MyWidget::onMetricsUpdated);
 */
class PerformanceMonitor : public QObject {
    Q_OBJECT

public:
    // 单例访问（实现在 .cpp 中确保唯一实例）
    static PerformanceMonitor& instance();

    // === 视频指标报告 ===
    void reportFps(quint32 fps);
    void reportDecodeLatency(double latencyMs);
    void reportRenderLatency(double latencyMs);
    void reportFrameDecoded();
    void reportFrameDropped();
    void reportFrameQueueDepth(int depth);

    // === 网络指标报告 ===
    void reportNetworkLatency(double latencyMs);
    void reportBytesSent(quint64 bytes);
    void reportBytesReceived(quint64 bytes);
    void reportPendingBytes(int bytes);
    void reportKcpRetransmit();

    // === 输入指标报告 ===
    void reportInputLatency(double latencyMs);
    void reportInputProcessed();
    void reportInputDropped();

    // === 内存指标报告 ===
    void reportFramePoolUsage(int used, int total);

    // === 获取当前指标 ===
    PerformanceMetrics currentMetrics() const;

    // === 控制 ===
    void setEnabled(bool enabled);
    bool isEnabled() const;
    void reset();

    // === 格式化输出 ===
    QString formatSummary() const;
    QString formatDetailed() const;

signals:
    void metricsUpdated(const PerformanceMetrics& metrics);

private:
    PerformanceMonitor();
    ~PerformanceMonitor() override = default;

    PerformanceMonitor(const PerformanceMonitor&) = delete;
    PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;

private:
    PerformanceMetrics m_metrics;
    LatencyTracker m_decodeLatency{60};
    LatencyTracker m_renderLatency{60};
    LatencyTracker m_networkLatency{60};
    LatencyTracker m_inputLatency{60};

    QTimer* m_updateTimer = nullptr;
    bool m_enabled = false;
};

// 便捷宏：报告延迟
#define PERF_REPORT_DECODE_LATENCY(ms) \
    qsc::PerformanceMonitor::instance().reportDecodeLatency(ms)

#define PERF_REPORT_RENDER_LATENCY(ms) \
    qsc::PerformanceMonitor::instance().reportRenderLatency(ms)

#define PERF_REPORT_INPUT_LATENCY(ms) \
    qsc::PerformanceMonitor::instance().reportInputLatency(ms)

#define PERF_REPORT_FRAME_DECODED() \
    qsc::PerformanceMonitor::instance().reportFrameDecoded()

#define PERF_REPORT_FRAME_DROPPED() \
    qsc::PerformanceMonitor::instance().reportFrameDropped()

// 作用域延迟计时器
#define PERF_SCOPE_DECODE() \
    QElapsedTimer __perfDecodeTimer; __perfDecodeTimer.start(); \
    auto __perfDecodeGuard = qScopeGuard([&]() { \
        PERF_REPORT_DECODE_LATENCY(__perfDecodeTimer.nsecsElapsed() / 1000000.0); \
    })

#define PERF_SCOPE_RENDER() \
    QElapsedTimer __perfRenderTimer; __perfRenderTimer.start(); \
    auto __perfRenderGuard = qScopeGuard([&]() { \
        PERF_REPORT_RENDER_LATENCY(__perfRenderTimer.nsecsElapsed() / 1000000.0); \
    })

} // namespace qsc

#endif // PERFORMANCEMONITOR_H
