#include "DeviceSession.h"
#include "StreamManager.h"
#include "InputManager.h"
#include "interfaces/IVideoChannel.h"
#include "interfaces/IControlChannel.h"
#include "infra/FrameQueue.h"
#include "decoder.h"

#include <QDebug>

namespace qsc {
namespace core {

DeviceSession::DeviceSession(const SessionParams& params, QObject* parent)
    : QObject(parent)
    , m_params(params)
    , m_streamManager(std::make_unique<StreamManager>(this))
    , m_inputManager(std::make_unique<InputManager>(this))
{
    qDebug() << "[DeviceSession] Created for device:" << params.serial;
    setupConnections();
}

DeviceSession::~DeviceSession()
{
    qDebug() << "[DeviceSession] Destroying" << m_params.serial;
    stop();
}

void DeviceSession::setState(SessionState state)
{
    if (m_state == state) {
        return;  // 状态未变化
    }

    // 验证状态转换是否有效
    if (!isValidStateTransition(m_state, state)) {
        qWarning("[DeviceSession] Invalid state transition: %s -> %s",
                 sessionStateToString(m_state), sessionStateToString(state));
        return;
    }

    qDebug("[DeviceSession] State: %s -> %s",
           sessionStateToString(m_state), sessionStateToString(state));

    m_state = state;
    emit stateChanged(state);
}

void DeviceSession::setupConnections()
{
    // 连接 StreamManager 信号
    connect(m_streamManager.get(), &StreamManager::fpsUpdated,
            this, &DeviceSession::fpsUpdated);
    connect(m_streamManager.get(), &StreamManager::streamStopped,
            this, [this]() {
                qDebug() << "[DeviceSession] Stream stopped";
                stop();
            });
    connect(m_streamManager.get(), &StreamManager::frameSizeChanged,
            this, &DeviceSession::frameSizeChanged);
    connect(m_streamManager.get(), &StreamManager::decoderInfo,
            this, &DeviceSession::decoderInfo);

    // 连接 InputManager 信号
    connect(m_inputManager.get(), &InputManager::cursorGrabChanged,
            this, &DeviceSession::cursorGrabChanged);
    connect(m_inputManager.get(), &InputManager::scriptTip,
            this, [this](const QString& msg, int durationMs, int keyId) {
                emit scriptTip(msg, keyId, durationMs);
            });
    connect(m_inputManager.get(), &InputManager::keyMapOverlayUpdated,
            this, &DeviceSession::keyMapOverlayUpdated);
}

bool DeviceSession::start(Decoder* decoder,
                          IVideoChannel* videoChannel,
                          IControlChannel* controlChannel)
{
    if (m_state != SessionState::Disconnected && m_state != SessionState::Error) {
        qWarning() << "[DeviceSession] Cannot start: current state is"
                   << sessionStateToString(m_state);
        return false;
    }

    setState(SessionState::Connecting);
    qDebug() << "[DeviceSession] Starting session for" << m_params.serial;

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
    qDebug() << "[DeviceSession] Stopping session for" << m_params.serial;

    if (m_streamManager) {
        m_streamManager->stop();
    }

    if (m_inputManager) {
        m_inputManager->stop();
    }

    setState(SessionState::Disconnected);
    emit stopped(m_params.serial);
}

bool DeviceSession::isRunning() const
{
    return m_state == SessionState::Streaming ||
           m_state == SessionState::Paused;
}

quint32 DeviceSession::fps() const
{
    return m_streamManager ? m_streamManager->fps() : 0;
}

// === 输入事件 ===

void DeviceSession::keyEvent(const QKeyEvent* event, const QSize& frameSize, const QSize& showSize)
{
    if (m_inputManager) {
        m_inputManager->keyEvent(event, frameSize, showSize);
    }
}

void DeviceSession::mouseEvent(const QMouseEvent* event, const QSize& frameSize, const QSize& showSize)
{
    if (m_inputManager) {
        m_inputManager->mouseEvent(event, frameSize, showSize);
    }
}

void DeviceSession::wheelEvent(const QWheelEvent* event, const QSize& frameSize, const QSize& showSize)
{
    if (m_inputManager) {
        m_inputManager->wheelEvent(event, frameSize, showSize);
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

void DeviceSession::updateScript(const QString& json, bool runAutoStart)
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

// === 回调设置 ===

void DeviceSession::setFrameGrabCallback(std::function<QImage()> callback)
{
    m_frameGrabCallback = callback;  // 先拷贝保存
    if (m_inputManager) {
        m_inputManager->setFrameGrabCallback(std::move(callback));  // 再 move 给 InputManager
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
