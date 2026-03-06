#ifndef CORE_DEVICESESSION_H
#define CORE_DEVICESESSION_H

#include <string>
#include <cstdint>
#include <opencv2/core.hpp>
#include <memory>
#include <functional>
#include "GameTypes.h"

#include "infra/SessionParams.h"
#include "GrayFrame.h"
#include "GameSignal.h"

// 前向声明
class Decoder;
class AuxChannelClient;
class AudioStreamManager;
struct InputEvent;

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
class DeviceSession
{
public:
    DeviceSession(const SessionParams& params);
    ~DeviceSession();

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
    const std::string& serial() const { return m_params.serial; }

    /**
     * @brief 获取手机屏幕尺寸
     */
    Size getMobileSize() const { return m_mobileSize; }

    /**
     * @brief 获取当前 FPS
     */
    uint32_t fps() const;

    // === 输入事件 ===

    void keyEvent(const InputEvent& event, const Size& frameSize, const Size& showSize);
    void mouseEvent(const InputEvent& event, const Size& frameSize, const Size& showSize);
    void wheelEvent(const InputEvent& event, const Size& frameSize, const Size& showSize);

    /**
     * @brief 设置设备像素比（用于 DPI 感知的光标定位）
     */
    void setDevicePixelRatio(double dpr);

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
    void updateScript(const std::string& json, bool runAutoStart = true);

    /**
     * @brief 是否处于游戏模式
     */
    bool isCurrentCustomKeymap() const;

    // === 状态管理 ===

    void onWindowFocusLost();
    void resetScriptState();
    void runAutoStartScripts();
    void resetAllTouchPoints();

    // === 运行时参数调整 ===

    void setVideoBitRate(uint32_t bitrate);
    void setDisplayPower(bool on);
    void setVideoParams(uint32_t bitrate, uint16_t maxFps, uint16_t maxSize);
    void setVideoStreaming(bool on);

    // === 辅助通道 ===

    /**
     * @brief 设置辅助通道客户端（独立于控制通道）
     * @param client 由 DeviceController 创建并传入，DeviceSession 不拥有所有权
     */
    void setAuxChannel(AuxChannelClient* client);

    /**
     * @brief 设置音频流管理器
     * @param mgr 由 DeviceController 创建并传入，DeviceSession 不拥有所有权
     */
    void setAudioManager(AudioStreamManager* mgr);
    AudioStreamManager* audioManager() const { return m_audioManager; }

    // === 回调设置 ===

    void setFrameGrabCallback(std::function<cv::Mat()> callback);
    void setGrayFrameGrabCallback(GrayFrameGrabCallback callback);

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

    // === Signals (Signal<>) ===

    // 状态信号
    Signal<SessionState> stateChanged;
    Signal<const std::string&, const Size&> started;
    Signal<const std::string&> stopped;
    Signal<const std::string&> error;

    // 视频信号
    Signal<> frameAvailable;
    Signal<int, int, uint8_t*, uint8_t*, uint8_t*, int, int, int> frameReady;
    Signal<uint32_t> fpsUpdated;
    Signal<const Size&> frameSizeChanged;
    Signal<bool, const std::string&> decoderInfo;

    // 输入信号
    Signal<bool> cursorGrabChanged;

    // 脚本信号
    Signal<const std::string&, int, int> scriptTip;
    Signal<> keyMapOverlayUpdated;

private:
    void setState(SessionState state);
    void setupConnections();

private:
    SessionParams m_params;
    SessionState m_state = SessionState::Disconnected;
    Size m_mobileSize;

    // 服务管理器
    std::unique_ptr<StreamManager> m_streamManager;
    std::unique_ptr<InputManager> m_inputManager;

    // 外部提供的组件（不拥有所有权）
    IVideoChannel* m_videoChannel = nullptr;
    IControlChannel* m_controlChannel = nullptr;

    // 零拷贝帧队列（由 DeviceController 设置）
    FrameQueue* m_frameQueue = nullptr;

    // 辅助通道（由 DeviceController 创建，不拥有所有权）
    AuxChannelClient* m_auxChannel = nullptr;

    // 音频流管理器（由 DeviceController 创建，不拥有所有权）
    AudioStreamManager* m_audioManager = nullptr;

    // 帧获取回调
    std::function<cv::Mat()> m_frameGrabCallback;
    GrayFrameGrabCallback m_grayFrameGrabCallback;
};

} // namespace core
} // namespace qsc

#endif // CORE_DEVICESESSION_H
