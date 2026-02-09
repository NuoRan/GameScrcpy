#include "ScriptWatchdog.h"
#include <QDebug>

ScriptWatchdog::ScriptWatchdog(int timeoutMs, QObject* parent)
    : QObject(parent)
    , m_timeoutMs(timeoutMs)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &ScriptWatchdog::onTimeout);

    m_hardTimer = new QTimer(this);
    m_hardTimer->setSingleShot(true);
    connect(m_hardTimer, &QTimer::timeout, this, &ScriptWatchdog::onHardTimeout);
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
    m_timer->start(m_timeoutMs);
}

void ScriptWatchdog::stop()
{
    m_running.store(false);
    m_timer->stop();
    m_hardTimer->stop();
}

void ScriptWatchdog::feed()
{
    // 喂狗：重置超时计时器
    // 由于可能从工作线程调用，使用 QMetaObject::invokeMethod 确保在主线程执行
    if (m_running.load() && !m_timedOut.load()) {
        QMetaObject::invokeMethod(m_timer, [this]() {
            if (m_running.load() && !m_timedOut.load()) {
                m_timer->start(m_timeoutMs);
            }
        }, Qt::QueuedConnection);
    }
}

void ScriptWatchdog::onTimeout()
{
    if (!m_running.load()) {
        return;
    }

    m_timedOut.store(true);
    qWarning() << "[ScriptWatchdog] Soft timeout triggered, attempting graceful interrupt...";

    // 发送软超时信号
    emit softTimeout();

    // 启动硬超时定时器
    m_hardTimer->start(m_hardTimeoutMs);
}

void ScriptWatchdog::onHardTimeout()
{
    if (!m_running.load()) {
        return;
    }

    qCritical() << "[ScriptWatchdog] Hard timeout triggered, forcing termination!";

    // 发送强制终止信号
    emit hardTimeout();
}
