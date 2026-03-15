#ifndef CORE_INPUTMANAGER_H
#define CORE_INPUTMANAGER_H

#include <string>
#include <cstdint>
#include <opencv2/core.hpp>
#include <memory>
#include <functional>
#include "GrayFrame.h"
#include "GameTypes.h"

// 前向声明
class Controller;
class TouchRouter;
struct InputEvent;
class KcpControlSocket;
class NativeTcpSocket;

namespace qsc {
namespace core {

// 前向声明
class IControlChannel;
class IInputProcessor;

/**
 * @brief 输入管理器 / Input Manager
 *
 * 管理输入处理和控制命令发送 / Manages input processing and control command sending:
 * UI Events -> InputProcessor -> Controller -> ControlChannel -> Device
 *
 * 职责：
 * - 协调输入处理器和控制器的生命周期
 * - 路由键盘/鼠标/滚轮事件
 * - 管理键位映射和脚本系统
 * - 处理系统快捷命令（返回、主页等）
 */
class InputManager {
public:
    /**
     * @brief 回调类型
     */
    using CursorGrabCallback = std::function<void(bool grabbed)>;
    using ScriptTipCallback = std::function<void(const std::string& msg, int durationMs, int keyId)>;
    using KeyMapOverlayCallback = std::function<void()>;

    /**
     * @brief KCP 发送回调类型
     */
    using KcpSendCallback = std::function<int64_t(const char*, int)>;

    explicit InputManager();
    ~InputManager();

    /**
     * @brief 初始化（创建 Controller）
     * @param sendCallback KCP 发送回调
     * @param gameScript 初始键位配置
     */
    void initialize(KcpSendCallback sendCallback, const std::string& gameScript = "");

    /**
     * @brief 设置控制通道（用于非阻塞发送）
     */
    void setControlChannel(IControlChannel* channel);

    /**
     * @brief 设置 KCP 控制 Socket
     */
    void setKcpControlSocket(KcpControlSocket* socket);

    /**
     * @brief 设置 TCP 控制 Socket
     */
    void setTcpControlSocket(NativeTcpSocket* socket);

    /**
     * @brief 设置设备分辨率
     */
    void setMobileSize(const Size& size);

    /**
     * @brief 启动控制发送
     */
    void start();

    /**
     * @brief 停止控制发送
     */
    void stop();

    // === 事件处理 ===

    void keyEvent(const InputEvent& event, const Size& frameSize, const Size& showSize);
    void mouseEvent(const InputEvent& event, const Size& frameSize, const Size& showSize);
    void wheelEvent(const InputEvent& event, const Size& frameSize, const Size& showSize);

    void setDevicePixelRatio(double dpr);

    // === 系统命令 ===

    void postGoBack();
    void postGoHome();
    void postGoMenu();
    void postAppSwitch();
    void postPower();
    void postVolumeUp();
    void postVolumeDown();
    void postBackOrScreenOn(bool down);
    void postKeyCodeClick(int keycode);
    void postDisconnect();

    // === 运行时参数调整 ===

    void postSetVideoBitRate(uint32_t bitrate);
    void postSetDisplayPower(bool on);

    // === 状态管理 ===

    void onWindowFocusLost();
    void resetAllTouchPoints();

    // === 脚本管理 ===

    void updateScript(const std::string& gameScript, bool runAutoStartScripts = true);
    void resetScriptState();
    void runAutoStartScripts();
    bool isCurrentCustomKeymap() const;

    // === 帧获取（用于脚本图像识别）===

    void setFrameGrabCallback(std::function<cv::Mat()> callback);
    void setGrayFrameGrabCallback(GrayFrameGrabCallback callback);

    // === 回调注册 ===

    void setCursorGrabCallback(CursorGrabCallback cb) { m_cursorGrabCb = std::move(cb); }
    void setScriptTipCallback(ScriptTipCallback cb) { m_scriptTipCb = std::move(cb); }
    void setKeyMapOverlayCallback(KeyMapOverlayCallback cb) { m_keyMapOverlayCb = std::move(cb); }

    // === 获取底层 Controller ===

    Controller* controller() const { return m_controller.get(); }

    // === 触控路由 ===

    void setTouchRouter(TouchRouter* router);
    TouchRouter* touchRouter() const;

private:
    std::unique_ptr<Controller> m_controller;
    IControlChannel* m_controlChannel = nullptr;
    Size m_mobileSize;
    CursorGrabCallback m_cursorGrabCb;
    ScriptTipCallback m_scriptTipCb;
    KeyMapOverlayCallback m_keyMapOverlayCb;
};

} // namespace core
} // namespace qsc

#endif // CORE_INPUTMANAGER_H
