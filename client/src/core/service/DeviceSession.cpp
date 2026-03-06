#include "DeviceSession.h"
#include "StreamManager.h"
#include "InputManager.h"
#include "interfaces/IVideoChannel.h"
#include "interfaces/IControlChannel.h"
#include "infra/FrameQueue.h"
#include "AuxChannelClient.h"
#include "decoder.h"

#define LOG_TAG "DeviceSession"
#include "Logger.h"

namespace qsc {
namespace core {

DeviceSession::DeviceSession(const SessionParams& params)
    : m_params(params)
    , m_streamManager(std::make_unique<StreamManager>())
    , m_inputManager(std::make_unique<InputManager>())
{
    LOGD() << "[DeviceSession] Created for device:" << params.serial;
    setupConnections();
}

DeviceSession::~DeviceSession()
{
    LOGD() << "[DeviceSession] Destroying" << m_params.serial;
    stop();
}

void DeviceSession::setState(SessionState state)
{
    if (m_state == state) {
        return;  // 状态未变化
    }

    // 验证状态转换是否有效
    if (!isValidStateTransition(m_state, state)) {
        LOG_W("[DeviceSession] Invalid state transition: %s -> %s",
                 sessionStateToString(m_state), sessionStateToString(state));
        return;
    }

    LOG_D("[DeviceSession] State: %s -> %s",
           sessionStateToString(m_state), sessionStateToString(state));

    m_state = state;
    stateChanged.fire(state);
}

void DeviceSession::setupConnections()
{
    // 连接 StreamManager 信号 (Signal<>)
    m_streamManager->fpsUpdated.connect([this](uint32_t fps) {
        fpsUpdated.fire(fps);
    });
    m_streamManager->streamStopped.connect([this]() {
        LOGD() << "[DeviceSession] Stream stopped";
        stop();
    });
    m_streamManager->frameSizeChanged.connect([this](const Size& size) {
        frameSizeChanged.fire(size);
    });
    m_streamManager->decoderInfo.connect([this](bool hwAccel, const std::string& name) {
        decoderInfo.fire(hwAccel, name);
    });

    // 注册 InputManager 回调
    m_inputManager->setCursorGrabCallback([this](bool grabbed) {
        cursorGrabChanged.fire(grabbed);
    });
    m_inputManager->setScriptTipCallback([this](const std::string& msg, int durationMs, int keyId) {
        scriptTip.fire(msg, keyId, durationMs);
    });
    m_inputManager->setKeyMapOverlayCallback([this]() {
        keyMapOverlayUpdated.fire();
    });
}

bool DeviceSession::start(Decoder* decoder,
                          IVideoChannel* videoChannel,
                          IControlChannel* controlChannel)
{
    if (m_state != SessionState::Disconnected && m_state != SessionState::Error) {
        LOGW() << "[DeviceSession] Cannot start: current state is"
                   << sessionStateToString(m_state);
        return false;
    }

    setState(SessionState::Connecting);
    LOGD() << "[DeviceSession] Starting session for" << m_params.serial;

    m_videoChannel = videoChannel;
    m_controlChannel = controlChannel;

    // 配置 StreamManager
    m_streamManager->setVideoChannel(videoChannel);
    m_streamManager->setDecoder(decoder);

    // 配置 InputManager
    m_inputManager->setMobileSize(m_mobileSize);

    // 进入握手状态
    setState(SessionState::Handshaking);

    // 握手成功后进入流传输状态
    // 注意：实际的握手由 Server 完成，这里简化处理
    setState(SessionState::Streaming);

    return true;
}

void DeviceSession::stop()
{
    if (m_state == SessionState::Disconnected ||
        m_state == SessionState::Disconnecting) {
        return;
    }

    setState(SessionState::Disconnecting);
    LOGD() << "[DeviceSession] Stopping session for" << m_params.serial;

    if (m_streamManager) {
        m_streamManager->stop();
    }

    if (m_inputManager) {
        m_inputManager->stop();
    }

    setState(SessionState::Disconnected);
    stopped.fire(m_params.serial);
}

bool DeviceSession::isRunning() const
{
    return m_state == SessionState::Streaming ||
           m_state == SessionState::Paused;
}

uint32_t DeviceSession::fps() const
{
    return m_streamManager ? m_streamManager->fps() : 0;
}

// === 输入事件 ===

void DeviceSession::keyEvent(const InputEvent& event, const Size& frameSize, const Size& showSize)
{
    if (m_inputManager) {
        m_inputManager->keyEvent(event, frameSize, showSize);
    }
}

void DeviceSession::mouseEvent(const InputEvent& event, const Size& frameSize, const Size& showSize)
{
    if (m_inputManager) {
        m_inputManager->mouseEvent(event, frameSize, showSize);
    }
}

void DeviceSession::wheelEvent(const InputEvent& event, const Size& frameSize, const Size& showSize)
{
    if (m_inputManager) {
        m_inputManager->wheelEvent(event, frameSize, showSize);
    }
}

void DeviceSession::setDevicePixelRatio(double dpr)
{
    if (m_inputManager) {
        m_inputManager->setDevicePixelRatio(dpr);
    }
}

// === 系统按键 ===

void DeviceSession::postGoBack()
{
    if (m_inputManager) {
        m_inputManager->postGoBack();
    }
}

void DeviceSession::postGoHome()
{
    if (m_inputManager) {
        m_inputManager->postGoHome();
    }
}

void DeviceSession::postGoMenu()
{
    if (m_inputManager) {
        m_inputManager->postGoMenu();
    }
}

void DeviceSession::postAppSwitch()
{
    if (m_inputManager) {
        m_inputManager->postAppSwitch();
    }
}

void DeviceSession::postPower()
{
    if (m_inputManager) {
        m_inputManager->postPower();
    }
}

void DeviceSession::postVolumeUp()
{
    if (m_inputManager) {
        m_inputManager->postVolumeUp();
    }
}

void DeviceSession::postVolumeDown()
{
    if (m_inputManager) {
        m_inputManager->postVolumeDown();
    }
}

// === 功能控制 ===

void DeviceSession::screenshot(std::function<void(int, int, uint8_t*)> callback)
{
    if (m_streamManager) {
        m_streamManager->screenshot(callback);
    }
}

void DeviceSession::updateScript(const std::string& json, bool runAutoStart)
{
    if (m_inputManager) {
        m_inputManager->updateScript(json, runAutoStart);
    }
}

bool DeviceSession::isCurrentCustomKeymap() const
{
    return m_inputManager ? m_inputManager->isCurrentCustomKeymap() : false;
}

// === 状态管理 ===

void DeviceSession::onWindowFocusLost()
{
    if (m_inputManager) {
        m_inputManager->onWindowFocusLost();
    }
}

void DeviceSession::resetScriptState()
{
    if (m_inputManager) {
        m_inputManager->resetScriptState();
    }
}

void DeviceSession::runAutoStartScripts()
{
    if (m_inputManager) {
        m_inputManager->runAutoStartScripts();
    }
}

void DeviceSession::resetAllTouchPoints()
{
    if (m_inputManager) {
        m_inputManager->resetAllTouchPoints();
    }
}

void DeviceSession::setVideoBitRate(uint32_t bitrate)
{
    if (m_inputManager) {
        m_inputManager->postSetVideoBitRate(bitrate);
    }
}

void DeviceSession::setDisplayPower(bool on)
{
    if (m_inputManager) {
        m_inputManager->postSetDisplayPower(on);
    }
}

void DeviceSession::setVideoParams(uint32_t bitrate, uint16_t maxFps, uint16_t maxSize)
{
    if (m_auxChannel) {
        m_auxChannel->sendVideoParams(bitrate, maxFps, maxSize);
    } else {
        LOG_W("[DeviceSession] setVideoParams: aux channel not available");
    }
}

void DeviceSession::setVideoStreaming(bool on)
{
    if (m_auxChannel) {
        m_auxChannel->sendVideoStreaming(on);
    } else {
        LOG_W("[DeviceSession] setVideoStreaming: aux channel not available");
    }
}

void DeviceSession::setAuxChannel(AuxChannelClient* client)
{
    m_auxChannel = client;
}

void DeviceSession::setAudioManager(AudioStreamManager* mgr)
{
    m_audioManager = mgr;
}

// === 回调设置 ===

void DeviceSession::setFrameGrabCallback(std::function<cv::Mat()> callback)
{
    m_frameGrabCallback = callback;  // 先拷贝保存
    if (m_inputManager) {
        m_inputManager->setFrameGrabCallback(std::move(callback));  // 再 move 给 InputManager
    }
}

void DeviceSession::setGrayFrameGrabCallback(GrayFrameGrabCallback callback)
{
    m_grayFrameGrabCallback = callback;
    if (m_inputManager) {
        m_inputManager->setGrayFrameGrabCallback(std::move(callback));
    }
}

// === 零拷贝帧访问 ===

FrameData* DeviceSession::consumeFrame()
{
    if (!m_frameQueue) return nullptr;
    return m_frameQueue->popFrame();
}

void DeviceSession::retainFrame(FrameData* frame)
{
    if (m_frameQueue && frame) {
        m_frameQueue->retainFrame(frame);
    }
}

void DeviceSession::releaseFrame(FrameData* frame)
{
    if (m_frameQueue && frame) {
        m_frameQueue->releaseFrame(frame);
    }
}

} // namespace core
} // namespace qsc
