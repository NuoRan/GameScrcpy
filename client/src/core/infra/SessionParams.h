#ifndef CORE_SESSIONPARAMS_H
#define CORE_SESSIONPARAMS_H

#include <QString>
#include <QSize>
#include <cstdint>

namespace qsc {
namespace core {

/**
 * @brief 会话连接参数 / Session Connection Parameters
 *
 * 包含建立设备连接所需的所有参数。
 * Contains all parameters needed to establish a device connection.
 */
struct SessionParams {
    // 必需参数 / Required parameters
    QString serial;                 // 设备序列号 / Device serial number
    QString serverLocalPath;        // 本地 server jar 路径 / Local server jar path

    // 视频参数 / Video parameters
    uint16_t maxSize = 720;         // 最大分辨率 / Max resolution
    uint32_t bitRate = 8000000;     // 码率 (bps) / Bitrate (bps)
    uint32_t maxFps = 60;           // 最大帧率 / Max FPS

    // 连接参数 / Connection parameters
    bool useKcp = false;            // true=KCP/UDP(WiFi), false=TCP(USB)
    QString deviceIP;               // WiFi 模式下的设备 IP / Device IP for WiFi mode
    uint16_t kcpPort = 27185;       // KCP 端口 / KCP port
    uint16_t tcpPort = 27183;       // TCP 端口
    bool useReverse = true;         // 是否使用 adb reverse

    // Server 参数
    QString serverRemotePath = "/data/local/tmp/scrcpy-server.jar";
    QString serverVersion = "3.3.4";
    QString logLevel = "info";
    QString codecOptions;
    QString codecName;
    uint32_t scid = 0;              // 连接标识 (随机数)

    // 显示参数
    bool closeScreen = false;       // 启动时息屏
    bool renderExpiredFrames = false;
    QSize frameSize;                // 预期帧尺寸

    // 键位脚本
    QString keyMapJson;             // 键位映射 JSON（别名 gameScript）
    int maxTouchPoints = 10;        // 最大触摸点数

    // 兼容别名
    const QString& gameScript() const { return keyMapJson; }

    // 辅助方法
    bool isWiFiMode() const { return useKcp && !deviceIP.isEmpty(); }
    bool isValid() const { return !serial.isEmpty() && !serverLocalPath.isEmpty(); }
};

/**
 * @brief 会话状态枚举
 *
 * 状态转换图：
 *
 *                    ┌──────────────────────────────────────┐
 *                    │                                      │
 *                    ▼                                      │
 *   ┌──────────────────────┐                                │
 *   │    Disconnected      │◄───────────────────────────────┤
 *   │   (初始/断开状态)    │                                │
 *   └──────────┬───────────┘                                │
 *              │ connectDevice()                            │
 *              ▼                                            │
 *   ┌──────────────────────┐                                │
 *   │     Connecting       │──────────► Error ──────────────┤
 *   │   (正在建立连接)     │         (连接失败)             │
 *   └──────────┬───────────┘                                │
 *              │ Server 启动成功                            │
 *              ▼                                            │
 *   ┌──────────────────────┐                                │
 *   │     Handshaking      │──────────► Error ──────────────┤
 *   │   (协议握手中)       │         (握手失败)             │
 *   └──────────┬───────────┘                                │
 *              │ 握手成功，收到设备信息                     │
 *              ▼                                            │
 *   ┌──────────────────────┐                                │
 *   │     Streaming        │──────────► Error ──────────────┤
 *   │   (视频流传输中)     │         (流中断)               │
 *   └──────────┬───────────┘                                │
 *              │ disconnectDevice() / 窗口关闭              │
 *              ▼                                            │
 *   ┌──────────────────────┐                                │
 *   │    Disconnecting     │────────────────────────────────┘
 *   │   (正在断开连接)     │
 *   └──────────────────────┘
 *
 * 状态说明：
 * - Disconnected: 初始状态，或已完全断开
 * - Connecting: 正在启动 Server，建立 Socket 连接
 * - Handshaking: Socket 已连接，正在进行协议握手
 * - Streaming: 视频流正常传输中
 * - Disconnecting: 正在清理资源，断开连接
 * - Error: 发生错误，需要重新连接
 * - Paused: 暂停状态（保持连接但不渲染）
 */
enum class SessionState {
    Disconnected,   // 未连接（初始/断开完成）
    Connecting,     // 连接中（启动 Server）
    Handshaking,    // 握手中（协议协商）
    Streaming,      // 流传输中（正常工作）
    Paused,         // 暂停（保持连接，不渲染）
    Disconnecting,  // 断开中（清理资源）
    Error           // 错误（需重连）
};

/**
 * @brief 获取状态名称（用于调试）
 */
inline const char* sessionStateToString(SessionState state) {
    switch (state) {
    case SessionState::Disconnected:  return "Disconnected";
    case SessionState::Connecting:    return "Connecting";
    case SessionState::Handshaking:   return "Handshaking";
    case SessionState::Streaming:     return "Streaming";
    case SessionState::Paused:        return "Paused";
    case SessionState::Disconnecting: return "Disconnecting";
    case SessionState::Error:         return "Error";
    default:                          return "Unknown";
    }
}

/**
 * @brief 检查状态转换是否有效
 */
inline bool isValidStateTransition(SessionState from, SessionState to) {
    switch (from) {
    case SessionState::Disconnected:
        return to == SessionState::Connecting;

    case SessionState::Connecting:
        return to == SessionState::Handshaking ||
               to == SessionState::Error ||
               to == SessionState::Disconnecting;

    case SessionState::Handshaking:
        return to == SessionState::Streaming ||
               to == SessionState::Error ||
               to == SessionState::Disconnecting;

    case SessionState::Streaming:
        return to == SessionState::Paused ||
               to == SessionState::Error ||
               to == SessionState::Disconnecting;

    case SessionState::Paused:
        return to == SessionState::Streaming ||
               to == SessionState::Disconnecting;

    case SessionState::Disconnecting:
        return to == SessionState::Disconnected;

    case SessionState::Error:
        return to == SessionState::Disconnected ||
               to == SessionState::Connecting;  // 允许重连

    default:
        return false;
    }
}

} // namespace core
} // namespace qsc

#endif // CORE_SESSIONPARAMS_H
