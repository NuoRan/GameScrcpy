#pragma once

#include <functional>
#include <atomic>
#include <memory>

/**
 * @brief 原生 Win32 定时器，替代非 UI 代码中的 QTimer
 *
 * 使用 Win32 CreateTimerQueueTimer 实现高效定时。
 * 回调通过 dispatch::postToMain() 在主线程执行，
 * 行为与 QTimer 一致（主线程回调）。
 *
 * 支持周期性和单次触发模式。
 */
class NativeTimer {
public:
    NativeTimer();
    ~NativeTimer();

    // 不可拷贝、不可移动
    NativeTimer(const NativeTimer&) = delete;
    NativeTimer& operator=(const NativeTimer&) = delete;

    /// 设置定时间隔（毫秒）
    void setInterval(int intervalMs);

    /// 设置单次触发模式（触发一次后自动停止）
    void setSingleShot(bool singleShot);

    /// 设置回调函数（定时器触发时在主线程执行）
    void setCallback(std::function<void()> callback);

    /// 使用当前间隔启动定时器
    void start();

    /// 设置间隔并启动定时器
    void start(int intervalMs);

    /// 停止定时器。阻塞直到正在执行的回调完成。
    void stop();

    /// 定时器是否正在运行
    bool isActive() const;

    /// 获取当前间隔（毫秒）
    int interval() const;

    /// 是否为单次触发模式
    bool isSingleShot() const;

    /// 静态便捷方法：延迟 delayMs 毫秒后在主线程执行一次回调
    static void singleShot(int delayMs, std::function<void()> callback);

private:
    std::function<void()> m_callback;
    int m_intervalMs = 0;
    bool m_singleShot = false;
    std::atomic<bool> m_active{false};

    // Win32 HANDLE (= void*), 避免在头文件中包含 Windows.h
    void* m_timerHandle = nullptr;

    // 回调数据指针 (TimerCallbackData*)，在 stop() 中释放
    void* m_callbackData = nullptr;

    // 共享的存活标志，用于安全地跨线程取消已投递到主线程的回调
    std::shared_ptr<std::atomic<bool>> m_alive;
};
