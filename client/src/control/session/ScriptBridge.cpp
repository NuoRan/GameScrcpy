#include "ScriptBridge.h"
#include "SessionVars.h"
#include "SessionContext.h"
#include "ScriptEngine.h"
#include "controller.h"
#include "fastmsg.h"
#include "keymap.h"
#include "SteerWheelHandler.h"
#include "ViewportHandler.h"
#include "FreeLookHandler.h"
#include "CursorHandler.h"
#include "KeyboardHandler.h"

#include <QDebug>
#include <QDir>

ScriptBridge::ScriptBridge(QPointer<Controller> controller, SessionVars* vars, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_vars(vars)
{
    // 创建脚本引擎（使用 QPointer 安全访问 controller）
    // 注意：ScriptEngine 需要 SessionContext，但这里我们传 nullptr
    // 因为 ScriptBridge 会在 SessionContext 中创建，由 SessionContext 提供上下文
    m_scriptEngine = new ScriptEngine(controller.data(), nullptr, this);
    m_scriptEngine->setScriptBasePath(QDir::currentPath() + "/keymap/scripts");

    setupConnections();
}

ScriptBridge::~ScriptBridge()
{
    if (m_scriptEngine) {
        m_scriptEngine->stopAll();
    }
}

void ScriptBridge::setupConnections()
{
    if (!m_scriptEngine || m_controller.isNull()) return;

    // 使用 QPointer 捕获，确保安全访问
    QPointer<Controller> safeController = m_controller;

    // 连接 ScriptEngine 信号到主线程处理
    connect(m_scriptEngine, &ScriptEngine::touchRequested, this,
        [safeController](quint32 seqId, quint8 action, quint16 x, quint16 y) {
            if (safeController.isNull()) return;
            if (action != FTA_DOWN && action != FTA_UP && action != FTA_MOVE) return;
            safeController->postFastMsg(FastMsg::serializeTouch(FastTouchEvent(seqId, action, x, y)));
        });

    connect(m_scriptEngine, &ScriptEngine::keyRequested, this,
        [safeController](quint8 action, quint16 keycode) {
            if (safeController.isNull()) return;
            safeController->postFastMsg(FastMsg::serializeKey(FastKeyEvent(action, keycode)));
        });

    connect(m_scriptEngine, &ScriptEngine::shotmodeRequested, this,
        [this](bool gameMode) {
            // 这个需要由 SessionContext 来处理，因为涉及光标状态
            // 暂时留空，由 SessionContext 自己连接
            Q_UNUSED(gameMode);
        });

    connect(m_scriptEngine, &ScriptEngine::radialParamRequested, this,
        [this](double up, double down, double left, double right) {
            script_setSteerWheelCoefficient(up, down, left, right);
        });

    connect(m_scriptEngine, &ScriptEngine::resetviewRequested, this,
        [this]() {
            script_resetView();
        });

    connect(m_scriptEngine, &ScriptEngine::resetWheelRequested, this,
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

void ScriptBridge::setScriptBasePath(const QString& path)
{
    if (m_scriptEngine) {
        m_scriptEngine->setScriptBasePath(path);
    }
}

void ScriptBridge::setVideoSize(const QSize& size)
{
    if (m_scriptEngine) {
        m_scriptEngine->setVideoSize(size);
    }
}

void ScriptBridge::setFrameGrabCallback(std::function<QImage()> callback)
{
    m_frameGrabCallback = callback;
    if (m_scriptEngine) {
        m_scriptEngine->setFrameGrabCallback(callback);
    }
}

QImage ScriptBridge::grabFrame() const
{
    if (m_frameGrabCallback) {
        return m_frameGrabCallback();
    }
    return QImage();
}

void ScriptBridge::connectScriptTipSignal(std::function<void(const QString&, int, int)> callback)
{
    if (!m_scriptEngine) return;

    disconnect(m_scriptEngine, &ScriptEngine::tipRequested, this, nullptr);
    m_tipCallback = callback;

    if (callback) {
        connect(m_scriptEngine, &ScriptEngine::tipRequested, this, &ScriptBridge::onTipRequested);
    }
}

void ScriptBridge::connectKeyMapOverlayUpdateSignal(std::function<void()> callback)
{
    if (!m_scriptEngine) return;

    disconnect(m_scriptEngine, &ScriptEngine::keyMapOverlayUpdateRequested, this, nullptr);
    m_overlayUpdateCallback = callback;

    if (callback) {
        connect(m_scriptEngine, &ScriptEngine::keyMapOverlayUpdateRequested,
                this, &ScriptBridge::onKeyMapOverlayUpdateRequested);
    }
}

void ScriptBridge::onTipRequested(const QString& msg, int durationMs, int keyId)
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
    QHash<int, QList<quint32>> allSeqs = m_vars->takeAllTouchSeqs();
    if (allSeqs.isEmpty()) return;

    if (m_controller.isNull()) return;

    for (auto it = allSeqs.constBegin(); it != allSeqs.constEnd(); ++it) {
        for (quint32 seqId : it.value()) {
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
        if (node.type == KeyMap::KMT_SCRIPT && !node.script.isEmpty()) {
            if (ScriptEngine::isAutoStartScript(node.script)) {
                m_scriptEngine->runAutoStartScript(node.script);
            }
        }
    }
}

void ScriptBridge::runInlineScript(const QString& script, int keyId, const QPointF& pos, bool isPress)
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

QPointF ScriptBridge::script_getMousePos(bool cursorCaptured)
{
    if (cursorCaptured) {
        return m_viewportHandler ? m_viewportHandler->lastConvertedPos() : QPointF();
    } else {
        return m_cursorHandler ? m_cursorHandler->lastPos() : QPointF();
    }
}

void ScriptBridge::script_setGameMapMode(bool enter, bool& cursorCaptured,
                                          std::function<void()> toggleCallback)
{
    if (cursorCaptured != enter && toggleCallback) {
        toggleCallback();
    }
}

int ScriptBridge::script_getKeyState(int qtKey, const QHash<int, bool>& keyStates)
{
    return keyStates.value(qtKey, false) ? 1 : 0;
}

int ScriptBridge::script_getKeyStateByName(const QString& displayName, KeyMap* keyMap,
                                            const QHash<int, bool>& keyStates)
{
    if (!keyMap) return 0;

    const KeyMap::KeyMapNode& node = keyMap->getKeyMapNodeByDisplayName(displayName);
    if (node.type == KeyMap::KMT_INVALID) {
        return 0;
    }

    int key = 0;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;

    if (node.type == KeyMap::KMT_SCRIPT) {
        key = node.data.script.keyNode.key;
        modifiers = node.data.script.keyNode.modifiers;
    } else {
        return 0;
    }

    if (!keyStates.value(key, false)) {
        return 0;
    }

    if (modifiers & Qt::ControlModifier) {
        if (!keyStates.value(Qt::Key_Control, false)) return 0;
    }
    if (modifiers & Qt::ShiftModifier) {
        if (!keyStates.value(Qt::Key_Shift, false)) return 0;
    }
    if (modifiers & Qt::AltModifier) {
        if (!keyStates.value(Qt::Key_Alt, false)) return 0;
    }
    if (modifiers & Qt::MetaModifier) {
        if (!keyStates.value(Qt::Key_Meta, false)) return 0;
    }

    return 1;
}

QVariantMap ScriptBridge::script_getKeyPos(int qtKey, KeyMap* keyMap)
{
    QVariantMap map;
    map.insert("x", -1);
    map.insert("y", -1);

    if (!keyMap) return map;

    const KeyMap::KeyMapNode& node = keyMap->getKeyMapNodeKey(qtKey);

    if (node.type == KeyMap::KMT_INVALID) return map;

    QPointF pos;
    bool hasPos = false;

    switch(node.type) {
    case KeyMap::KMT_STEER_WHEEL:
        pos = node.data.steerWheel.centerPos;
        hasPos = true;
        break;
    case KeyMap::KMT_SCRIPT:
        pos = node.data.script.keyNode.pos;
        hasPos = true;
        break;
    default:
        break;
    }

    if (hasPos) {
        map["x"] = qRound(pos.x() * 10000.0) / 10000.0;
        map["y"] = qRound(pos.y() * 10000.0) / 10000.0;
        map["valid"] = true;
    }
    return map;
}

QVariantMap ScriptBridge::script_getKeyPosByName(const QString& displayName, KeyMap* keyMap)
{
    QVariantMap map;
    map.insert("x", 0.0);
    map.insert("y", 0.0);
    map.insert("valid", false);

    if (!keyMap) return map;

    const KeyMap::KeyMapNode& node = keyMap->getKeyMapNodeByDisplayName(displayName);

    if (node.type == KeyMap::KMT_INVALID) return map;

    QPointF pos;
    bool hasPos = false;

    switch(node.type) {
    case KeyMap::KMT_STEER_WHEEL:
        pos = node.data.steerWheel.centerPos;
        hasPos = true;
        break;
    case KeyMap::KMT_SCRIPT:
        pos = node.data.script.keyNode.pos;
        hasPos = true;
        break;
    default:
        break;
    }

    if (hasPos) {
        map["x"] = qRound(pos.x() * 10000.0) / 10000.0;
        map["y"] = qRound(pos.y() * 10000.0) / 10000.0;
        map["valid"] = true;
    }
    return map;
}
