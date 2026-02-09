#ifndef CORE_INPUTMANAGER_H
#define CORE_INPUTMANAGER_H

#include <QObject>
#include <QSize>
#include <QImage>
#include <memory>
#include <functional>

// 前向声明
class Controller;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class KcpControlSocket;
class QTcpSocket;

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
class InputManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief KCP 发送回调类型
     */
    using KcpSendCallback = std::function<qint64(const QByteArray&)>;

    explicit InputManager(QObject* parent = nullptr);
    ~InputManager() override;

    /**
     * @brief 初始化（创建 Controller）
     * @param sendCallback KCP 发送回调
     * @param gameScript 初始键位配置
     */
    void initialize(KcpSendCallback sendCallback, const QString& gameScript = "");

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
    void setTcpControlSocket(QTcpSocket* socket);

    /**
     * @brief 设置设备分辨率
     */
    void setMobileSize(const QSize& size);

    /**
     * @brief 启动控制发送
     */
    void start();

    /**
     * @brief 停止控制发送
     */
    void stop();

    // === 事件处理 ===

    void keyEvent(const QKeyEvent* event, const QSize& frameSize, const QSize& showSize);
    void mouseEvent(const QMouseEvent* event, const QSize& frameSize, const QSize& showSize);
    void wheelEvent(const QWheelEvent* event, const QSize& frameSize, const QSize& showSize);

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

    // === 状态管理 ===

    void onWindowFocusLost();
    void resetAllTouchPoints();

    // === 脚本管理 ===

    void updateScript(const QString& gameScript, bool runAutoStartScripts = true);
    void resetScriptState();
    void runAutoStartScripts();
    bool isCurrentCustomKeymap() const;

    // === 帧获取（用于脚本图像识别）===

    void setFrameGrabCallback(std::function<QImage()> callback);

    // === 获取底层 Controller ===

    Controller* controller() const { return m_controller.get(); }

signals:
    /**
     * @brief 光标抓取状态变化
     */
    void cursorGrabChanged(bool grabbed);

    /**
     * @brief 脚本提示信号
     */
    void scriptTip(const QString& msg, int durationMs, int keyId);

    /**
     * @brief 键位覆盖层更新信号
     */
    void keyMapOverlayUpdated();

private:
    std::unique_ptr<Controller> m_controller;
    IControlChannel* m_controlChannel = nullptr;
    QSize m_mobileSize;
};

} // namespace core
} // namespace qsc

#endif // CORE_INPUTMANAGER_H
