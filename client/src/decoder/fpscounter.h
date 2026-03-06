#ifndef FPSCOUNTER_H
#define FPSCOUNTER_H

#include <atomic>
#include <cstdint>

/**
 * @brief 帧率统计器 / FPS Counter (纯 C++ 原子计数器)
 *
 * 统计渲染帧率和丢帧数。无定时器 / 无 QObject。
 * 消费者通过 pollFps() 以固定间隔（如 1 秒）轮询获取 FPS。
 *
 * Tracks rendered FPS and dropped frames using lock-free atomics.
 * No timer, no QObject. Consumers call pollFps() at a fixed interval (e.g. 1 s).
 */
class FpsCounter
{
public:
    FpsCounter() = default;
    ~FpsCounter() = default;

    void start();
    void stop();
    bool isStarted() const;

    /// 解码线程调用 / Called from decode thread
    void addRenderedFrame();
    void addSkippedFrame();

    /// 由 GUI 层每秒调用一次，返回上一周期渲染帧数并重置计数器
    /// Called once per second from GUI layer; returns rendered count and resets.
    uint32_t pollFps();

    /// 上一次 pollFps() 返回的值（只读）
    uint32_t lastFps() const { return m_lastRendered.load(std::memory_order_relaxed); }

private:
    std::atomic<bool>     m_running{false};
    std::atomic<uint32_t> m_rendered{0};
    std::atomic<uint32_t> m_skipped{0};
    std::atomic<uint32_t> m_lastRendered{0};
};

#endif // FPSCOUNTER_H
