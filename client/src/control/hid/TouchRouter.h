/**
 * @file TouchRouter.h
 * @brief 触控路由器 / Touch Event Router
 *
 * 在 Controller 和传输层之间插入路由层，
 * 根据当前触控方式将触摸事件分发到不同后端:
 * - Adb: 走原有 ControlSender (FastMsg → KCP/TCP)
 * - Uhid: 走 UHID HID 设备 (需 server)
 * - Esp32: 走 ESP32 串口
 * - Aoa: 走 AOA HID 设备 (USB 控制传输)
 *
 * 视频来源由 videoChannelEnabled 配置独立控制，
 * 不影响触控方式选择。
 */
#ifndef TOUCH_ROUTER_H
#define TOUCH_ROUTER_H

#include <QObject>
#include <cstdint>
#include <string>
#include <functional>

class ControlSender;
class ITouchBackend;

/**
 * @brief 触控方式 (4 种)
 *
 * 视频来源（scrcpy 画面 / 无画面 / UDP 画面）由 videoChannelEnabled 等
 * 配置项独立控制，与触控方式正交。
 */
enum class TouchMethod {
    Adb   = 0,  // adb 触控 (scrcpy 原生，需 server)
    Uhid  = 1,  // UHID 触控 (推荐，需 server)
    Esp32 = 2,  // ESP32 串口触控 (不需 server)
    Aoa   = 3,  // AOA HID 触控 (OTG, 不需 server/adb)
};

// ===== 辅助函数 =====

/// 该触控方式是否需要 scrcpy server (adb)
inline bool methodNeedsServer(TouchMethod m) {
    return m == TouchMethod::Adb || m == TouchMethod::Uhid;
}

/// 该触控方式是否走 scrcpy 控制通道 (adb)
inline bool methodUsesScrcpyTouch(TouchMethod m) {
    return m == TouchMethod::Adb;
}

/// 该触控方式是否走 HID 后端 (UHID/ESP32/AOA)
inline bool methodUsesHid(TouchMethod m) {
    return m == TouchMethod::Uhid
        || m == TouchMethod::Esp32
        || m == TouchMethod::Aoa;
}

inline bool methodUsesUhid(TouchMethod m) { return m == TouchMethod::Uhid; }
inline bool methodUsesEsp32(TouchMethod m) { return m == TouchMethod::Esp32; }
inline bool methodUsesAoa(TouchMethod m) { return m == TouchMethod::Aoa; }

class TouchRouter : public QObject {
    Q_OBJECT
public:
    explicit TouchRouter(QObject* parent = nullptr);
    ~TouchRouter() override;

    void setMethod(TouchMethod method);
    TouchMethod method() const { return m_method; }

    /// 设置后端
    void setScrcpySender(ControlSender* sender);
    void setHidBackend(ITouchBackend* backend);
    ITouchBackend* hidBackend() const { return m_hidBackend; }

    /// 路由 FastMsg 数据 (从 Controller::postFastMsg 调用)
    void routeFastMsg(const char* data, int len);

    /// scrcpy 通道是否可用 (按键可能需要回退 scrcpy 发送)
    void setScrcpyAvailable(bool available) { m_scrcpyAvailable = available; }
    bool isScrcpyAvailable() const { return m_scrcpyAvailable; }

    /// HID 后端是否已连接
    bool isHidConnected() const;

signals:
    void methodChanged(TouchMethod method);
    void hidStatusChanged(const QString& status);

private:
    /// 解析 FastMsg 并根据方式分发
    void routeTouchMsg(uint8_t type, const char* data, int len);
    void routeKeyMsg(const char* data, int len);
    void sendToScrcpy(const char* data, int len);

    TouchMethod     m_method = TouchMethod::Adb;
    ControlSender*  m_scrcpySender = nullptr;
    ITouchBackend*  m_hidBackend = nullptr;  // UHID / AOA / ESP32
    bool            m_scrcpyAvailable = true;
};

#endif // TOUCH_ROUTER_H
