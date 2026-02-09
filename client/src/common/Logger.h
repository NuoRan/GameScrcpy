#ifndef LOGGER_H
#define LOGGER_H

#include <QDebug>
#include <QString>

/**
 * @file Logger.h
 * @brief 统一日志系统 / Unified Logging System
 *
 * 提供模块化的日志宏，统一日志格式：[ModuleName] message
 * Provides modular logging macros with unified format: [ModuleName] message
 *
 * 使用方式 / Usage:
 * 1. 在源文件顶部定义模块名 / Define module name at top of source file:
 *    #define LOG_TAG "VideoForm"
 *
 * 2. 使用日志宏 / Use logging macros:
 *    LOG_D("Frame received: %dx%d", width, height);
 *
 * 日志级别 / Log levels:
 * - LOG_D: Debug 调试信息 / Debug info
 * - LOG_I: Info 一般信息 / General info
 * - LOG_W: Warning 警告 / Warning
 * - LOG_E: Error 错误 / Error
 * - LOG_C: Critical 严重错误 / Critical error
 */

namespace qsc {

// 日志级别枚举 / Log level enumeration
enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
    Critical
};

// 全局日志级别（可在运行时调整）
inline LogLevel& globalLogLevel() {
    static LogLevel level = LogLevel::Debug;
    return level;
}

inline void setLogLevel(LogLevel level) {
    globalLogLevel() = level;
}

inline bool shouldLog(LogLevel level) {
    return static_cast<int>(level) >= static_cast<int>(globalLogLevel());
}

} // namespace qsc

// ============================================================================
// 日志宏定义
// ============================================================================

// 检查是否定义了 LOG_TAG
#ifndef LOG_TAG
#define LOG_TAG "Unknown"
#endif

// 格式化日志前缀
#define LOG_PREFIX "[" LOG_TAG "]"

// Debug 级别日志
#define LOG_D(fmt, ...) \
    do { \
        if (qsc::shouldLog(qsc::LogLevel::Debug)) { \
            qDebug(LOG_PREFIX " " fmt, ##__VA_ARGS__); \
        } \
    } while(0)

// Info 级别日志
#define LOG_I(fmt, ...) \
    do { \
        if (qsc::shouldLog(qsc::LogLevel::Info)) { \
            qInfo(LOG_PREFIX " " fmt, ##__VA_ARGS__); \
        } \
    } while(0)

// Warning 级别日志
#define LOG_W(fmt, ...) \
    do { \
        if (qsc::shouldLog(qsc::LogLevel::Warning)) { \
            qWarning(LOG_PREFIX " " fmt, ##__VA_ARGS__); \
        } \
    } while(0)

// Error 级别日志
#define LOG_E(fmt, ...) \
    do { \
        if (qsc::shouldLog(qsc::LogLevel::Error)) { \
            qCritical(LOG_PREFIX " " fmt, ##__VA_ARGS__); \
        } \
    } while(0)

// Critical 级别日志
#define LOG_C(fmt, ...) \
    do { \
        qCritical(LOG_PREFIX " [CRITICAL] " fmt, ##__VA_ARGS__); \
    } while(0)

// ============================================================================
// Qt 流式日志宏（支持 << 操作符）
// ============================================================================

// Debug 级别（流式）
#define LOGD() \
    if (qsc::shouldLog(qsc::LogLevel::Debug)) qDebug().noquote() << LOG_PREFIX

// Info 级别（流式）
#define LOGI() \
    if (qsc::shouldLog(qsc::LogLevel::Info)) qInfo().noquote() << LOG_PREFIX

// Warning 级别（流式）
#define LOGW() \
    if (qsc::shouldLog(qsc::LogLevel::Warning)) qWarning().noquote() << LOG_PREFIX

// Error 级别（流式）
#define LOGE() \
    if (qsc::shouldLog(qsc::LogLevel::Error)) qCritical().noquote() << LOG_PREFIX

// ============================================================================
// 条件日志宏
// ============================================================================

// 条件 Debug
#define LOG_D_IF(cond, fmt, ...) \
    do { \
        if ((cond) && qsc::shouldLog(qsc::LogLevel::Debug)) { \
            qDebug(LOG_PREFIX " " fmt, ##__VA_ARGS__); \
        } \
    } while(0)

// 条件 Warning
#define LOG_W_IF(cond, fmt, ...) \
    do { \
        if ((cond) && qsc::shouldLog(qsc::LogLevel::Warning)) { \
            qWarning(LOG_PREFIX " " fmt, ##__VA_ARGS__); \
        } \
    } while(0)

// ============================================================================
// 性能日志宏
// ============================================================================

#include <QElapsedTimer>

// 作用域计时器
#define LOG_PERF_SCOPE(name) \
    QElapsedTimer __perfTimer_##name; \
    __perfTimer_##name.start(); \
    auto __perfGuard_##name = qScopeGuard([&]() { \
        if (qsc::shouldLog(qsc::LogLevel::Debug)) { \
            qDebug(LOG_PREFIX " [PERF] %s took %lld ms", #name, __perfTimer_##name.elapsed()); \
        } \
    })

// 手动计时
#define LOG_PERF_START(name) \
    QElapsedTimer __perfTimer_##name; \
    __perfTimer_##name.start()

#define LOG_PERF_END(name) \
    do { \
        if (qsc::shouldLog(qsc::LogLevel::Debug)) { \
            qDebug(LOG_PREFIX " [PERF] %s took %lld ms", #name, __perfTimer_##name.elapsed()); \
        } \
    } while(0)

// ============================================================================
// 一次性日志宏（避免重复打印）
// ============================================================================

#define LOG_ONCE(level, fmt, ...) \
    do { \
        static bool __logged = false; \
        if (!__logged) { \
            __logged = true; \
            q##level(LOG_PREFIX " " fmt, ##__VA_ARGS__); \
        } \
    } while(0)

#define LOG_D_ONCE(fmt, ...) LOG_ONCE(Debug, fmt, ##__VA_ARGS__)
#define LOG_I_ONCE(fmt, ...) LOG_ONCE(Info, fmt, ##__VA_ARGS__)
#define LOG_W_ONCE(fmt, ...) LOG_ONCE(Warning, fmt, ##__VA_ARGS__)

// ============================================================================
// 频率限制日志宏（每 N 毫秒最多打印一次）
// ============================================================================

#define LOG_THROTTLE(level, interval_ms, fmt, ...) \
    do { \
        static qint64 __lastLogTime = 0; \
        qint64 __now = QDateTime::currentMSecsSinceEpoch(); \
        if (__now - __lastLogTime >= (interval_ms)) { \
            __lastLogTime = __now; \
            q##level(LOG_PREFIX " " fmt, ##__VA_ARGS__); \
        } \
    } while(0)

#define LOG_D_THROTTLE(interval_ms, fmt, ...) LOG_THROTTLE(Debug, interval_ms, fmt, ##__VA_ARGS__)
#define LOG_W_THROTTLE(interval_ms, fmt, ...) LOG_THROTTLE(Warning, interval_ms, fmt, ##__VA_ARGS__)

#endif // LOGGER_H
