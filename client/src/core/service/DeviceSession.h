#ifndef CORE_DEVICESESSION_H
#define CORE_DEVICESESSION_H

#include <QObject>
#include <QSize>
#include <QString>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <memory>
#include <functional>

#include "infra/SessionParams.h"

// 前向声明
class Decoder;
class QYUVOpenGLWidget;

namespace qsc {
namespace core {

// 前向声明
class StreamManager;
class InputManager;
class IVideoChannel;
class IControlChannel;
class FrameQueue;
struct FrameData;

/**
 * @brief 设备会话门面类 / Device Session Facade
 *
 * DeviceSession 是 UI 层与核心层的唯一接口。
 * DeviceSession is the sole interface between UI layer and core layer.
 * UI 层只通过 DeviceSession 的信号槽与核心交互，不直接访问内部实现。
 * UI interacts only via signals/slots, no direct access to internals.
 *
 * 内部使用 / Internal components:
 * - StreamManager: 管理视频流（接收 -> 解码 -> 渲染）/ Manages video stream (receive -> decode -> render)
 * - InputManager: 管理输入（事件处理 -> 控制发送）/ Manages input (event processing -> control sending)
 */
class DeviceSession : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param params 会话参数
     * @param parent 父对象
     */
    explicit DeviceSession(const SessionParams& params, QObject* parent = nullptr);

    ~DeviceSession() override;

    // 禁止拷贝
    DeviceSession(const DeviceSession&) = delete;
    DeviceSession& operator=(const DeviceSession&) = delete;

    // === 连接管理 ===

    /**
     * @brief 连接设备（外部已建立连接后调用）
     * @param decoder 解码器实例
     * @param videoChannel 视频通道
     * @param controlChannel 控制通道（KCP 模式下使用）
     * @return 成功返回 true
     */
    bool start(Decoder* decoder,
               IVideoChannel* videoChannel,
               IControlChannel* controlChannel = nullptr);

    /**
     * @brief 停止会话
     */
    void stop();

    /**
     * @brief 获取当前状态
     */
    SessionState state() const { return m_state; }

    /**
     * @brief 是否正在运行
     */
    bool isRunning() const;

    /**
     * @brief 获取设备序列号
     */
    QString serial() const { return m_params.serial; }

    /**
     * @brief 获取手机屏幕尺寸
     */
    QSize getMobileSize() const { return m_mobileSize; }

    /**
     * @brief 获取当前 FPS
     */
    quint32 fps() const;

    // === 输入事件 ===

    void keyEvent(const QKeyEvent* event, const QSize& frameSize, const QSize& showSize);
    void mouseEvent(const QMouseEvent* event, const QSize& frameSize, const QSize& showSize);
    void wheelEvent(const QWheelEvent* event, const QSize& frameSize, const QSize& showSize);

    // === 系统按键 ===

    void postGoBack();
    void postGoHome();
    void postGoMenu();
    void postAppSwitch();
    void postPower();
    void postVolumeUp();
    void postVolumeDown();

    // === 功能控制 ===

    /**
     * @brief 截图
     * @param callback 截图完成回调
     */
    void screenshot(std::function<void(int, int, uint8_t*)> callback);

    /**
     * @brief 更新键位脚本
     */
    void updateScript(const QString& json, bool runAutoStart = true);

    /**
     * @brief 是否处于游戏模式
     */
    bool isCurrentCustomKeymap() const;

    // === 状态管理 ===

    void onWindowFocusLost();
    void resetScriptState();
    void runAutoStartScripts();
    void resetAllTouchPoints();

    // === 回调设置 ===

    void setFrameGrabCallback(std::function<QImage()> callback);

    // === 获取内部管理器 ===

    StreamManager* streamManager() const { return m_streamManager.get(); }
    InputManager* inputManager() const { return m_inputManager.get(); }

    // === 零拷贝帧访问（供渲染器直接使用）===

    /**
     * @brief 设置帧队列（由 DeviceController 调用）
     */
    void setFrameQueue(FrameQueue* queue) { m_frameQueue = queue; }

    /**
     * @brief 消费一帧（渲染器调用）
     * @return 帧数据指针，使用完后必须调用 releaseFrame()
     */
    FrameData* consumeFrame();

    /**
     * @brief 增加帧引用计数（跨线程传递时使用）
     * 允许多个消费者持有同一帧，每个消费者用完后调用 releaseFrame()
     */
    void retainFrame(FrameData* frame);

    /**
     * @brief 归还帧到池中
     */
    void releaseFrame(FrameData* frame);

signals:
    // === 状态信号 ===

    void stateChanged(SessionState state);
    void started(const QString& serial, const QSize& size);
    void stopped(const QString& serial);
    void error(const QString& message);

    // === 视频信号 ===

    /**
     * @brief 新帧可用（零拷贝模式）
     *
     * 信号发出时，帧数据在 FrameQueue 中。
     * 消费者通过 consumeFrame() 获取，用完后调用 releaseFrame()。
     */
    void frameAvailable();

    /**
     * @brief 新帧可用（兼容旧接口）
     * @deprecated 使用 frameAvailable() + consumeFrame() 替代
     */
    void frameReady(int width, int height,
                    uint8_t* y, uint8_t* u, uint8_t* v,
                    int linesizeY, int linesizeU, int linesizeV);

    void fpsUpdated(quint32 fps);
    void frameSizeChanged(const QSize& size);
    void decoderInfo(bool hardwareAccelerated, const QString& decoderName);

    // === 输入信号 ===

    void cursorGrabChanged(bool grabbed);

    // === 脚本信号 ===

    void scriptTip(const QString& msg, int keyId, int durationMs);
    void keyMapOverlayUpdated();

private:
    void setState(SessionState state);
    void setupConnections();

private:
    SessionParams m_params;
    SessionState m_state = SessionState::Disconnected;
    QSize m_mobileSize;

    // 服务管理器
    std::unique_ptr<StreamManager> m_streamManager;
    std::unique_ptr<InputManager> m_inputManager;

    // 外部提供的组件（不拥有所有权）
    IVideoChannel* m_videoChannel = nullptr;
    IControlChannel* m_controlChannel = nullptr;

    // 零拷贝帧队列（由 DeviceController 设置）
    FrameQueue* m_frameQueue = nullptr;

    // 帧获取回调
    std::function<QImage()> m_frameGrabCallback;
};

} // namespace core
} // namespace qsc

#endif // CORE_DEVICESESSION_H
