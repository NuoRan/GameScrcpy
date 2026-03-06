#include "ScriptBridge.h"
#include "SessionVars.h"
#include "SessionContext.h"
#include "ScriptEngine.h"
#include "controller.h"
#include "fastmsg.h"
#include "keymap.h"
#include "StringUtils.h"
#include "SteerWheelHandler.h"
#include "ViewportHandler.h"
#include "FreeLookHandler.h"
#include "CursorHandler.h"
#include "KeyboardHandler.h"

#include <filesystem>
#define LOG_TAG "ScriptBridge"
#include "Logger.h"

ScriptBridge::ScriptBridge(Controller* controller, SessionVars* vars)
    : m_controller(controller)
    , m_vars(vars)
{
    // 创建脚本引擎（controller 生命周期由 SessionContext 管理）
    // 注意：ScriptEngine 需要 SessionContext，但这里我们传 nullptr
    // 因为 ScriptBridge 会在 SessionContext 中创建，由 SessionContext 提供上下文
    m_scriptEngine = new ScriptEngine(controller, nullptr);
    m_scriptEngine->setScriptBasePath((std::filesystem::current_path() / "keymap" / "scripts").string());

    setupConnections();
}

ScriptBridge::~ScriptBridge()
{
    if (m_scriptEngine) {
        m_scriptEngine->stopAll();
        delete m_scriptEngine;
        m_scriptEngine = nullptr;
    }
}

void ScriptBridge::setupConnections()
{
    if (!m_scriptEngine || !m_controller) return;

    // 使用裸指针捕获，生命周期由 SessionContext 管理
    Controller* safeController = m_controller;

    // 连接 ScriptEngine Signal<> 信号到主线程处理
    m_scriptEngine->touchRequested.connect(
        [safeController](uint32_t seqId, uint8_t action, uint16_t x, uint16_t y) {
            if (!safeController) return;
            if (action != FTA_DOWN && action != FTA_UP && action != FTA_MOVE) return;
            safeController->postFastMsg(FastMsg::serializeTouch(FastTouchEvent(seqId, action, x, y)));
        });

    m_scriptEngine->keyRequested.connect(
        [safeController](uint8_t action, uint16_t keycode) {
            if (!safeController) return;
            safeController->postFastMsg(FastMsg::serializeKey(FastKeyEvent(action, keycode)));
        });

    m_scriptEngine->shotmodeRequested.connect(
        [this](bool gameMode) {
            (void)gameMode;
        });

    m_scriptEngine->radialParamRequested.connect(
        [this](double up, double down, double left, double right) {
            script_setSteerWheelCoefficient(up, down, left, right);
        });

    m_scriptEngine->resetviewRequested.connect(
        [this]() {
            script_resetView();
        });

    m_scriptEngine->resetWheelRequested.connect(
        [this]() {
            script_resetWheel();
        });
}

void ScriptBridge::setSessionContext(SessionContext* ctx)
{
    if (m_scriptEngine) {
        m_scriptEngine->setSessionContext(ctx);
    }
}

void ScriptBridge::setScriptBasePath(const std::string& path)
{
    if (m_scriptEngine) {
        m_scriptEngine->setScriptBasePath(path);
    }
}

void ScriptBridge::setVideoSize(const Size& size)
{
    if (m_scriptEngine) {
        m_scriptEngine->setVideoSize(size);
    }
}

void ScriptBridge::setFrameGrabCallback(std::function<cv::Mat()> callback)
{
    m_frameGrabCallback = callback;
    if (m_scriptEngine) {
        m_scriptEngine->setFrameGrabCallback(callback);
    }
}

void ScriptBridge::setGrayFrameGrabCallback(GrayFrameGrabCallback callback)
{
    if (m_scriptEngine) {
        m_scriptEngine->setGrayFrameGrabCallback(std::move(callback));
    }
}

cv::Mat ScriptBridge::grabFrame() const
{
    if (m_frameGrabCallback) {
        return m_frameGrabCallback();
    }
    return cv::Mat();
}

void ScriptBridge::connectScriptTipSignal(std::function<void(const std::string&, int, int)> callback)
{
    if (!m_scriptEngine) return;

    m_scriptEngine->tipRequested.disconnectAll();
    m_tipCallback = callback;

    if (callback) {
        m_scriptEngine->tipRequested.connect(
            [this](const std::string& msg, int durationMs, int keyId) { onTipRequested(msg, durationMs, keyId); });
    }
}

void ScriptBridge::connectKeyMapOverlayUpdateSignal(std::function<void()> callback)
{
    if (!m_scriptEngine) return;

    m_scriptEngine->keyMapOverlayUpdateRequested.disconnectAll();
    m_overlayUpdateCallback = callback;

    if (callback) {
        m_scriptEngine->keyMapOverlayUpdateRequested.connect(
            [this]() { onKeyMapOverlayUpdateRequested(); });
    }
}

void ScriptBridge::onTipRequested(const std::string& msg, int durationMs, int keyId)
{
    if (m_tipCallback) {
        m_tipCallback(msg, durationMs, keyId);
    }
}

void ScriptBridge::onKeyMapOverlayUpdateRequested()
{
    if (m_overlayUpdateCallback) {
        m_overlayUpdateCallback();
    }
}

void ScriptBridge::stopAll()
{
    if (m_scriptEngine) {
        m_scriptEngine->stopAll();
    }
}

void ScriptBridge::reset()
{
    if (m_scriptEngine) {
        m_scriptEngine->reset();
    }
}

void ScriptBridge::releaseAllScriptTouches()
{
    if (!m_vars) return;

    // 取出所有活跃的脚本触摸序列并发送 FTA_UP 释放
    auto allSeqs = m_vars->takeAllTouchSeqs();
    if (allSeqs.empty()) return;

    if (!m_controller) return;

    for (auto& [keyId, seqs] : allSeqs) {
        for (uint32_t seqId : seqs) {
            m_controller->postFastMsg(FastMsg::serializeTouch(
                FastTouchEvent(seqId, FTA_UP, 0, 0)));
        }
    }
}

void ScriptBridge::runAutoStartScripts(KeyMap* keyMap)
{
    if (!m_scriptEngine || !keyMap) return;

    const auto& nodes = keyMap->getKeyMapNodes();
    for (const auto& node : nodes) {
        if (node.type == KeyMap::KMT_SCRIPT && !node.script.empty()) {
            if (ScriptEngine::isAutoStartScript(node.script)) {
                m_scriptEngine->runAutoStartScript(node.script);
            }
        }
    }
}

void ScriptBridge::runInlineScript(const std::string& script, int keyId, const PointF& pos, bool isPress)
{
    if (m_scriptEngine) {
        m_scriptEngine->runInlineScript(script, keyId, pos, isPress);
    }
}

void ScriptBridge::setHandlers(SteerWheelHandler* steerWheel,
                                ViewportHandler* viewport,
                                FreeLookHandler* freeLook,
                                CursorHandler* cursor,
                                KeyboardHandler* keyboard)
{
    m_steerWheelHandler = steerWheel;
    m_viewportHandler = viewport;
    m_freeLookHandler = freeLook;
    m_cursorHandler = cursor;
    m_keyboardHandler = keyboard;
}

// ========== 脚本 API 方法 ==========

void ScriptBridge::script_resetView()
{
    if (m_viewportHandler) {
        m_viewportHandler->resetView();
    }
}

void ScriptBridge::script_setSteerWheelCoefficient(double up, double down, double left, double right)
{
    if (m_steerWheelHandler) {
        m_steerWheelHandler->setCoefficient(up, down, left, right);
    }
}

void ScriptBridge::script_resetSteerWheelCoefficient()
{
    if (m_steerWheelHandler) {
        m_steerWheelHandler->resetCoefficient();
    }
}

void ScriptBridge::script_resetWheel()
{
    if (m_steerWheelHandler) {
        m_steerWheelHandler->resetWheel();
    }
}

PointF ScriptBridge::script_getMousePos(bool cursorCaptured)
{
    if (cursorCaptured) {
        return m_viewportHandler ? m_viewportHandler->lastConvertedPos() : PointF();
    } else {
        return m_cursorHandler ? m_cursorHandler->lastPos() : PointF();
    }
}

void ScriptBridge::script_setGameMapMode(bool enter, bool& cursorCaptured,
                                          std::function<void()> toggleCallback)
{
    if (cursorCaptured != enter && toggleCallback) {
        toggleCallback();
    }
}

int ScriptBridge::script_getKeyState(int qtKey, const std::unordered_map<int, bool>& keyStates)
{
    auto it = keyStates.find(qtKey);
    return (it != keyStates.end() && it->second) ? 1 : 0;
}

int ScriptBridge::script_getKeyStateByName(const std::string& displayName, KeyMap* keyMap,
                                            const std::unordered_map<int, bool>& keyStates)
{
    if (!keyMap) return 0;

    const KeyMap::KeyMapNode& node = keyMap->getKeyMapNodeByDisplayName(displayName);
    if (node.type == KeyMap::KMT_INVALID) {
        return 0;
    }

    int key = 0;
    uint32_t modifiers = GameMod::NoModifier;

    if (node.type == KeyMap::KMT_SCRIPT) {
        key = node.data.script.keyNode.key;
        modifiers = node.data.script.keyNode.modifiers;
    } else {
        return 0;
    }

    auto isKeyDown = [&keyStates](int k) -> bool {
        auto it = keyStates.find(k);
        return it != keyStates.end() && it->second;
    };

    if (!isKeyDown(key)) {
        return 0;
    }

    if (modifiers & GameMod::ControlModifier) {
        if (!isKeyDown(GameKey::Key_Control)) return 0;
    }
    if (modifiers & GameMod::ShiftModifier) {
        if (!isKeyDown(GameKey::Key_Shift)) return 0;
    }
    if (modifiers & GameMod::AltModifier) {
        if (!isKeyDown(GameKey::Key_Alt)) return 0;
    }
    if (modifiers & GameMod::MetaModifier) {
        if (!isKeyDown(GameKey::Key_Meta)) return 0;
    }

    return 1;
}

KeyPosResult ScriptBridge::script_getKeyPos(int qtKey, KeyMap* keyMap)
{
    KeyPosResult result;
    result.x = -1;
    result.y = -1;
    result.valid = false;

    if (!keyMap) return result;

    const KeyMap::KeyMapNode& node = keyMap->getKeyMapNodeKey(qtKey);

    if (node.type == KeyMap::KMT_INVALID) return result;

    double posX = 0, posY = 0;
    bool hasPos = false;

    switch(node.type) {
    case KeyMap::KMT_STEER_WHEEL:
        posX = node.data.steerWheel.centerPos.x;
        posY = node.data.steerWheel.centerPos.y;
        hasPos = true;
        break;
    case KeyMap::KMT_SCRIPT:
        posX = node.data.script.keyNode.pos.x;
        posY = node.data.script.keyNode.pos.y;
        hasPos = true;
        break;
    default:
        break;
    }

    if (hasPos) {
        result.x = std::round(posX * 10000.0) / 10000.0;
        result.y = std::round(posY * 10000.0) / 10000.0;
        result.valid = true;
    }
    return result;
}

KeyPosResult ScriptBridge::script_getKeyPosByName(const std::string& displayName, KeyMap* keyMap)
{
    KeyPosResult result;
    result.x = 0.0;
    result.y = 0.0;
    result.valid = false;

    if (!keyMap) return result;

    const KeyMap::KeyMapNode& node = keyMap->getKeyMapNodeByDisplayName(displayName);

    if (node.type == KeyMap::KMT_INVALID) return result;

    double posX = 0, posY = 0;
    bool hasPos = false;

    switch(node.type) {
    case KeyMap::KMT_STEER_WHEEL:
        posX = node.data.steerWheel.centerPos.x;
        posY = node.data.steerWheel.centerPos.y;
        hasPos = true;
        break;
    case KeyMap::KMT_SCRIPT:
        posX = node.data.script.keyNode.pos.x;
        posY = node.data.script.keyNode.pos.y;
        hasPos = true;
        break;
    default:
        break;
    }

    if (hasPos) {
        result.x = std::round(posX * 10000.0) / 10000.0;
        result.y = std::round(posY * 10000.0) / 10000.0;
        result.valid = true;
    }
    return result;
}
