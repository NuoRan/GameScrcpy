#include "NativeTimer.h"
#include "ThreadDispatcher.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <thread>
#include <chrono>
#include <QDebug>

// ---------------------------------------------------------
// 回调数据结构 — 传递给 CreateTimerQueueTimer
// ---------------------------------------------------------
struct TimerCallbackData {
    std::shared_ptr<std::atomic<bool>> alive;
    std::function<void()> callback;
    bool singleShot = false;
    std::atomic<bool>* activeFlag = nullptr; // 指向 NativeTimer::m_active
};

// ---------------------------------------------------------
// Win32 定时器回调（在线程池线程上执行）
// 通过 dispatch::postToMain 将用户回调投递到主线程
// ---------------------------------------------------------
static void CALLBACK timerQueueCallback(PVOID lpParameter, BOOLEAN /*TimerOrWaitFired*/)
{
    auto* data = static_cast<TimerCallbackData*>(lpParameter);
    if (!data || !data->alive->load(std::memory_order_acquire)) {
        return;
    }

    // 捕获 shared_ptr 副本，确保 alive 标志在主线程回调执行前不被释放
    auto alive = data->alive;
    auto callback = data->callback;
    bool isSingleShot = data->singleShot;
    auto* activeFlag = data->activeFlag;

    dispatch::postToMain([alive, callback, isSingleShot, activeFlag]() {
        if (alive->load(std::memory_order_acquire) && callback) {
            // 单次定时器触发后，在用户回调之前标记为非活动（与 QTimer 行为一致）
            if (isSingleShot && activeFlag) {
                activeFlag->store(false, std::memory_order_release);
            }
            callback();
        }
    });
}

// ---------------------------------------------------------
// NativeTimer 实现
// ---------------------------------------------------------

NativeTimer::NativeTimer() = default;

NativeTimer::~NativeTimer()
{
    stop();
}

void NativeTimer::setInterval(int intervalMs)
{
    m_intervalMs = intervalMs;
}

void NativeTimer::setSingleShot(bool singleShot)
{
    m_singleShot = singleShot;
}

void NativeTimer::setCallback(std::function<void()> callback)
{
    m_callback = std::move(callback);
}

void NativeTimer::start()
{
    // 先停止已有定时器
    stop();

    if (m_intervalMs < 0 || !m_callback) {
        return;
    }

    // 创建共享存活标志
    m_alive = std::make_shared<std::atomic<bool>>(true);

    // 创建回调数据（堆分配，在 stop() 中释放）
    auto* data = new TimerCallbackData{m_alive, m_callback, m_singleShot, &m_active};
    m_callbackData = data;

    DWORD dueTime = static_cast<DWORD>(m_intervalMs);
    DWORD period = m_singleShot ? 0 : dueTime;

    BOOL ok = CreateTimerQueueTimer(
        reinterpret_cast<PHANDLE>(&m_timerHandle),
        nullptr,            // 默认定时器队列
        timerQueueCallback,
        data,
        dueTime,            // 首次触发延迟
        period,             // 重复间隔（0 = 单次）
        WT_EXECUTEDEFAULT
    );

    if (ok) {
        m_active = true;
    } else {
        delete data;
        m_callbackData = nullptr;
        m_timerHandle = nullptr;
        m_alive.reset();
    }
}

void NativeTimer::start(int intervalMs)
{
    setInterval(intervalMs);
    start();
}

void NativeTimer::stop()
{
    if (!m_timerHandle) {
        return;
    }

    // 1. 标记存活标志为 false，阻止已投递但尚未执行的主线程回调
    if (m_alive) {
        m_alive->store(false, std::memory_order_release);
    }

    // 2. 删除定时器并等待当前正在执行的回调完成
    //    INVALID_HANDLE_VALUE 使调用阻塞直到所有回调结束
    //    安全：timerQueueCallback 运行在线程池线程，stop() 运行在主线程，不会死锁
    auto t0 = std::chrono::steady_clock::now();
    DeleteTimerQueueTimer(nullptr, m_timerHandle, INVALID_HANDLE_VALUE);
    auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    if (dt > 1) {
        qWarning("[NativeTimer] DeleteTimerQueueTimer blocked for %lldms", dt);
    }

    // 3. 释放回调数据
    //    此时保证没有 timerQueueCallback 在访问 data
    //    已投递到主线程的 lambda 持有 alive 和 callback 的副本，不依赖 data
    delete static_cast<TimerCallbackData*>(m_callbackData);
    m_callbackData = nullptr;

    m_timerHandle = nullptr;
    m_alive.reset();
    m_active = false;
}

bool NativeTimer::isActive() const
{
    return m_active.load(std::memory_order_acquire);
}

int NativeTimer::interval() const
{
    return m_intervalMs;
}

bool NativeTimer::isSingleShot() const
{
    return m_singleShot;
}

// ---------------------------------------------------------
// 静态单次定时器
// 使用分离线程 + sleep 实现，简单可靠，无句柄泄漏
// ---------------------------------------------------------
void NativeTimer::singleShot(int delayMs, std::function<void()> callback)
{
    if (!callback) return;

    std::thread([delayMs, cb = std::move(callback)]() {
        if (delayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }
        dispatch::postToMain(std::move(cb));
    }).detach();
}
