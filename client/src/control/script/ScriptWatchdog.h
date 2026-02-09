#ifndef SCRIPTWATCHDOG_H
#define SCRIPTWATCHDOG_H

#include <QObject>
#include <QTimer>
#include <atomic>

/**
 * @brief 脚本超时看门狗 / Script Timeout Watchdog
 *
 * 监控脚本执行时间，超时后触发中断机制：
 * Monitors script execution time, triggers interrupt mechanism on timeout:
 * 1. 首先尝试 QJSEngine::setInterrupted(true) / First try QJSEngine::setInterrupted(true)
 * 2. 如果仍未停止，强制终止线程 / If still running, force terminate thread
 */
class ScriptWatchdog : public QObject
{
    Q_OBJECT
public:
    explicit ScriptWatchdog(int timeoutMs = 30000, QObject* parent = nullptr);
    ~ScriptWatchdog();

    // 启动看门狗
    void start();

    // 停止看门狗（脚本正常结束时调用）
    void stop();

    // 喂狗（脚本中的 sleep 等操作时调用，重置超时计时器）
    void feed();

    // 设置超时时间
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    int timeoutMs() const { return m_timeoutMs; }

    // 检查是否已超时
    bool isTimedOut() const { return m_timedOut.load(); }

signals:
    // 首次超时信号（应该尝试 setInterrupted）
    void softTimeout();

    // 强制终止信号（软超时后仍未停止）
    void hardTimeout();

private slots:
    void onTimeout();
    void onHardTimeout();

private:
    QTimer* m_timer = nullptr;
    QTimer* m_hardTimer = nullptr;  // 硬超时定时器
    int m_timeoutMs = 30000;        // 软超时时间（默认 30 秒）
    int m_hardTimeoutMs = 1000;     // 硬超时时间（软超时后 1 秒）
    std::atomic<bool> m_timedOut{false};
    std::atomic<bool> m_running{false};
};

#endif // SCRIPTWATCHDOG_H
