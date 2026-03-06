#include "ScriptWatchdog.h"
#include "ThreadDispatcher.h"
#define LOG_TAG "ScriptWatchdog"
#include "Logger.h"

ScriptWatchdog::ScriptWatchdog(int timeoutMs)
    : m_timeoutMs(timeoutMs)
{
    m_timer.setSingleShot(true);
    m_timer.setCallback([this]() { onTimeout(); });

    m_hardTimer.setSingleShot(true);
    m_hardTimer.setCallback([this]() { onHardTimeout(); });
}

ScriptWatchdog::~ScriptWatchdog()
{
    stop();
}

void ScriptWatchdog::start()
{
    if (m_running.exchange(true)) {
        return;  // 已经在运行
    }

    m_timedOut.store(false);
    m_timer.start(m_timeoutMs);
}

void ScriptWatchdog::stop()
{
    m_running.store(false);
    m_timer.stop();
    m_hardTimer.stop();
}

void ScriptWatchdog::feed()
{
    // 喂狗：重置超时计时器
    // 由于可能从工作线程调用，使用 dispatch::postToMain 确保在主线程执行
    if (m_running.load() && !m_timedOut.load()) {
        dispatch::postToMain([this]() {
            if (m_running.load() && !m_timedOut.load()) {
                m_timer.start(m_timeoutMs);
            }
        });
    }
}

void ScriptWatchdog::onTimeout()
{
    if (!m_running.load()) {
        return;
    }

    m_timedOut.store(true);
    LOGW() << "[ScriptWatchdog] Soft timeout triggered, attempting graceful interrupt...";

    // 发送软超时信号
    softTimeout.fire();

    // 启动硬超时定时器
    m_hardTimer.start(m_hardTimeoutMs);
}

void ScriptWatchdog::onHardTimeout()
{
    if (!m_running.load()) {
        return;
    }

    LOGE() << "[ScriptWatchdog] Hard timeout triggered, forcing termination!";

    // 发送强制终止信号
    hardTimeout.fire();
}
