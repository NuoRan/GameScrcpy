#include "SessionContext.h"
#include "SessionVars.h"
#include "ScriptBridge.h"
#include "InputDispatcher.h"
#include "ScriptEngine.h"
#include "controller.h"
#include "fastmsg.h"
#include "HandlerChain.h"
#include "SteerWheelHandler.h"
#include "ViewportHandler.h"
#include "FreeLookHandler.h"
#include "CursorHandler.h"
#include "KeyboardHandler.h"

#include <QDebug>
#include <QKeyEvent>
#include <QDir>

SessionContext::SessionContext(const QString& deviceId, Controller* controller, QObject* parent)
    : QObject(parent)
    , m_deviceId(deviceId)
    , m_controller(controller)
{
    qDebug() << "[SessionContext] Created for device:" << deviceId;
    initComponents();
}

SessionContext::~SessionContext()
{
    qDebug() << "[SessionContext] Destroying for device:" << m_deviceId;

    // 首先停止脚本引擎并同步等待
    // 必须在任何其他清理之前完成，防止脚本线程访问已销毁的对象
    if (m_scriptBridge) {
        // 先断开 ScriptEngine 对 SessionContext 的引用
        if (ScriptEngine* engine = m_scriptBridge->scriptEngine()) {
            engine->setSessionContext(nullptr);  // 防止新的访问
        }
        m_scriptBridge->stopAll();  // 这会同步等待所有脚本停止
    }

    // 清理轮盘状态
    if (m_steerWheelHandler) {
        m_steerWheelHandler->reset();
    }

    qDebug() << "[SessionContext] Destroyed for device:" << m_deviceId;
}

void SessionContext::initComponents()
{
    // 1. 创建会话变量存储
    m_vars = new SessionVars(this);

    // 2. 创建脚本桥接器（先传 nullptr，稍后设置 SessionContext）
    m_scriptBridge = new ScriptBridge(m_controller, m_vars, this);
    m_scriptBridge->setSessionContext(this);

    // 3. 创建 HandlerChain 和 Handler
    m_handlerChain = new HandlerChain(this);

    m_steerWheelHandler = new SteerWheelHandler();
    m_steerWheelHandler->setKeyMap(&m_keyMap);
    m_handlerChain->addHandler(m_steerWheelHandler);

    m_viewportHandler = new ViewportHandler();
    m_viewportHandler->setKeyMap(&m_keyMap);
    m_handlerChain->addHandler(m_viewportHandler);

    m_freeLookHandler = new FreeLookHandler();
    m_freeLookHandler->setKeyMap(&m_keyMap);
    m_handlerChain->addHandler(m_freeLookHandler);

    m_cursorHandler = new CursorHandler();
    m_handlerChain->addHandler(m_cursorHandler);

    m_keyboardHandler = new KeyboardHandler();
    m_keyboardHandler->setKeyMap(&m_keyMap);
    m_handlerChain->addHandler(m_keyboardHandler);

    m_handlerChain->init(m_controller.data(), this);

    // 4. 设置 ScriptBridge 的 Handler 引用
    m_scriptBridge->setHandlers(m_steerWheelHandler, m_viewportHandler,
                                 m_freeLookHandler, m_cursorHandler, m_keyboardHandler);

    // 5. 创建输入分发器
    m_inputDispatcher = new InputDispatcher(m_controller, &m_keyMap, this);
    m_inputDispatcher->setHandlerChain(m_handlerChain);
    m_inputDispatcher->setSteerWheelHandler(m_steerWheelHandler);
    m_inputDispatcher->setViewportHandler(m_viewportHandler);
    m_inputDispatcher->setFreeLookHandler(m_freeLookHandler);
    m_inputDispatcher->setCursorHandler(m_cursorHandler);
    m_inputDispatcher->setKeyboardHandler(m_keyboardHandler);
    m_inputDispatcher->setScriptBridge(m_scriptBridge);

    // 连接信号
    connect(m_inputDispatcher, &InputDispatcher::grabCursor, this, &SessionContext::grabCursor);

    // 连接脚本桥接器的模式切换信号
    if (ScriptEngine* engine = m_scriptBridge->scriptEngine()) {
        connect(engine, &ScriptEngine::shotmodeRequested, this, [this](bool gameMode) {
            script_setGameMapMode(gameMode);
        });
        connect(engine, &ScriptEngine::simulateKeyRequested, this, [this](const QString& keyName, bool press) {
            script_simulateKey(keyName, press);
        });
    }

    // 默认状态：显示光标
    setCursorCaptured(false);
}

// ========== 子组件访问 ==========

ScriptEngine* SessionContext::scriptEngine() const
{
    return m_scriptBridge ? m_scriptBridge->scriptEngine() : nullptr;
}

// ========== 事件处理 ==========

void SessionContext::mouseEvent(const QMouseEvent* from, const QSize& frameSize, const QSize& showSize)
{
    if (m_inputDispatcher) {
        m_inputDispatcher->mouseEvent(from, frameSize, showSize);
    }
}

void SessionContext::wheelEvent(const QWheelEvent* from, const QSize& frameSize, const QSize& showSize)
{
    if (m_inputDispatcher) {
        m_inputDispatcher->wheelEvent(from, frameSize, showSize);
    }
}

void SessionContext::keyEvent(const QKeyEvent* from, const QSize& frameSize, const QSize& showSize)
{
    if (m_inputDispatcher) {
        m_inputDispatcher->keyEvent(from, frameSize, showSize);
    }
}

void SessionContext::onWindowFocusLost()
{
    if (m_inputDispatcher) {
        m_inputDispatcher->onWindowFocusLost();
    }
}

// ========== 脚本管理 ==========

void SessionContext::resetScriptState()
{
    if (m_scriptBridge) {
        m_scriptBridge->stopAll();
    }
}

void SessionContext::runAutoStartScripts()
{
    if (m_scriptBridge) {
        m_scriptBridge->runAutoStartScripts(&m_keyMap);
    }
}

// ========== KeyMap 管理 ==========

void SessionContext::loadKeyMap(const QString& json, bool runAutoStartScripts)
{
    if (m_scriptBridge) {
        m_scriptBridge->reset();
    }

    m_keyMap.loadKeyMap(json);

    if (runAutoStartScripts) {
        this->runAutoStartScripts();
    }
}

// ========== 帧获取回调 ==========

void SessionContext::setFrameGrabCallback(std::function<QImage()> callback)
{
    if (m_scriptBridge) {
        m_scriptBridge->setFrameGrabCallback(callback);
    }
}

QImage SessionContext::grabFrame() const
{
    return m_scriptBridge ? m_scriptBridge->grabFrame() : QImage();
}

// ========== 信号连接 ==========

void SessionContext::connectScriptTipSignal(std::function<void(const QString&, int, int)> callback)
{
    if (m_scriptBridge) {
        m_scriptBridge->connectScriptTipSignal(callback);
    }
}

void SessionContext::connectKeyMapOverlayUpdateSignal(std::function<void()> callback)
{
    if (m_scriptBridge) {
        m_scriptBridge->connectKeyMapOverlayUpdateSignal(callback);
    }
}

// ========== 尺寸信息 ==========

void SessionContext::setFrameSize(const QSize& size)
{
    if (m_inputDispatcher) {
        m_inputDispatcher->setFrameSize(size);
    }
}

void SessionContext::setShowSize(const QSize& size)
{
    if (m_inputDispatcher) {
        m_inputDispatcher->setShowSize(size);
    }
}

void SessionContext::setMobileSize(const QSize& size)
{
    if (m_inputDispatcher) {
        m_inputDispatcher->setMobileSize(size);
    }
}

QSize SessionContext::frameSize() const
{
    return m_inputDispatcher ? m_inputDispatcher->frameSize() : QSize();
}

QSize SessionContext::showSize() const
{
    return m_inputDispatcher ? m_inputDispatcher->showSize() : QSize();
}

QSize SessionContext::mobileSize() const
{
    return m_inputDispatcher ? m_inputDispatcher->mobileSize() : QSize();
}

// ========== 光标状态 ==========

bool SessionContext::isCursorCaptured() const
{
    return m_inputDispatcher ? m_inputDispatcher->isCursorCaptured() : false;
}

bool SessionContext::toggleCursorCaptured()
{
    return m_inputDispatcher ? m_inputDispatcher->toggleCursorCaptured() : false;
}

void SessionContext::setCursorCaptured(bool captured)
{
    if (m_inputDispatcher) {
        m_inputDispatcher->setCursorCaptured(captured);
    }
}

// ========== 脚本 API ==========

void SessionContext::script_resetView()
{
    if (m_scriptBridge) {
        m_scriptBridge->script_resetView();
    }
}

void SessionContext::script_setSteerWheelCoefficient(double up, double down, double left, double right)
{
    if (m_scriptBridge) {
        m_scriptBridge->script_setSteerWheelCoefficient(up, down, left, right);
    }
}

void SessionContext::script_resetSteerWheelCoefficient()
{
    if (m_scriptBridge) {
        m_scriptBridge->script_resetSteerWheelCoefficient();
    }
}

void SessionContext::script_resetWheel()
{
    if (m_scriptBridge) {
        m_scriptBridge->script_resetWheel();
    }
}

QPointF SessionContext::script_getMousePos()
{
    if (m_scriptBridge && m_inputDispatcher) {
        return m_scriptBridge->script_getMousePos(m_inputDispatcher->isCursorCaptured());
    }
    return QPointF();
}

void SessionContext::script_setGameMapMode(bool enter)
{
    if (m_inputDispatcher && m_inputDispatcher->isCursorCaptured() != enter) {
        toggleCursorCaptured();
    }
}

int SessionContext::script_getKeyState(int qtKey)
{
    if (m_scriptBridge && m_inputDispatcher) {
        return m_scriptBridge->script_getKeyState(qtKey, m_inputDispatcher->keyStates());
    }
    return 0;
}

int SessionContext::script_getKeyStateByName(const QString& displayName)
{
    if (m_scriptBridge && m_inputDispatcher) {
        return m_scriptBridge->script_getKeyStateByName(displayName, &m_keyMap, m_inputDispatcher->keyStates());
    }
    return 0;
}

QVariantMap SessionContext::script_getKeyPos(int qtKey)
{
    if (m_scriptBridge) {
        return m_scriptBridge->script_getKeyPos(qtKey, &m_keyMap);
    }
    return QVariantMap();
}

QVariantMap SessionContext::script_getKeyPosByName(const QString& displayName)
{
    if (m_scriptBridge) {
        return m_scriptBridge->script_getKeyPosByName(displayName, &m_keyMap);
    }
    return QVariantMap();
}

void SessionContext::script_simulateKey(const QString& keyName, bool press)
{
    int qtKey = keyNameToQtKey(keyName);
    if (qtKey == 0) {
        qWarning() << "[script_simulateKey] Unknown key:" << keyName;
        return;
    }

    QKeyEvent event(press ? QEvent::KeyPress : QEvent::KeyRelease,
                    qtKey, Qt::NoModifier);

    keyEvent(&event, frameSize(), showSize());
}

int SessionContext::keyNameToQtKey(const QString& keyName)
{
    QString k = keyName.toUpper();

    if (k == "SPACE" || k == " ") return Qt::Key_Space;
    if (k == "ENTER" || k == "RETURN") return Qt::Key_Return;
    if (k == "ESC" || k == "ESCAPE") return Qt::Key_Escape;
    if (k == "TAB") return Qt::Key_Tab;
    if (k == "BACKSPACE") return Qt::Key_Backspace;
    if (k == "SHIFT") return Qt::Key_Shift;
    if (k == "CTRL" || k == "CONTROL") return Qt::Key_Control;
    if (k == "ALT") return Qt::Key_Alt;
    if (k == "UP") return Qt::Key_Up;
    if (k == "DOWN") return Qt::Key_Down;
    if (k == "LEFT") return Qt::Key_Left;
    if (k == "RIGHT") return Qt::Key_Right;
    if (k == "TILDE" || k == "`") return Qt::Key_QuoteLeft;

    if (k.startsWith("F") && k.length() <= 3) {
        bool ok;
        int num = k.mid(1).toInt(&ok);
        if (ok && num >= 1 && num <= 12) {
            return Qt::Key_F1 + num - 1;
        }
    }

    if (k.length() == 1) {
        QChar c = k.at(0);
        if (c >= 'A' && c <= 'Z') return Qt::Key_A + (c.toLatin1() - 'A');
        if (c >= '0' && c <= '9') return Qt::Key_0 + (c.toLatin1() - '0');

        switch (c.toLatin1()) {
            case '`': return Qt::Key_QuoteLeft;
            case '~': return Qt::Key_AsciiTilde;
            case '-': return Qt::Key_Minus;
            case '=': return Qt::Key_Equal;
            case '[': return Qt::Key_BracketLeft;
            case ']': return Qt::Key_BracketRight;
            case '\\': return Qt::Key_Backslash;
            case ';': return Qt::Key_Semicolon;
            case '\'': return Qt::Key_Apostrophe;
            case ',': return Qt::Key_Comma;
            case '.': return Qt::Key_Period;
            case '/': return Qt::Key_Slash;
        }
    }

    return 0;
}

// ========== 会话变量 ==========

QVariant SessionContext::getVar(const QString& key, const QVariant& defaultValue) const
{
    return m_vars ? m_vars->getVar(key, defaultValue) : defaultValue;
}

void SessionContext::setVar(const QString& key, const QVariant& value)
{
    if (m_vars) {
        m_vars->setVar(key, value);
    }
}

bool SessionContext::hasVar(const QString& key) const
{
    return m_vars ? m_vars->hasVar(key) : false;
}

void SessionContext::removeVar(const QString& key)
{
    if (m_vars) {
        m_vars->removeVar(key);
    }
}

void SessionContext::clearVars()
{
    if (m_vars) {
        m_vars->clearVars();
    }
}

// ========== 触摸序列 ID ==========

void SessionContext::addTouchSeq(int keyId, quint32 seqId)
{
    if (m_vars) {
        m_vars->addTouchSeq(keyId, seqId);
    }
}

QList<quint32> SessionContext::takeTouchSeqs(int keyId)
{
    return m_vars ? m_vars->takeTouchSeqs(keyId) : QList<quint32>();
}

int SessionContext::touchSeqCount(int keyId) const
{
    return m_vars ? m_vars->touchSeqCount(keyId) : 0;
}

bool SessionContext::hasTouchSeqs(int keyId) const
{
    return m_vars ? m_vars->hasTouchSeqs(keyId) : false;
}

void SessionContext::clearTouchSeqs()
{
    if (m_vars) {
        m_vars->clearTouchSeqs();
    }
}

// ========== 轮盘参数 ==========

void SessionContext::setRadialParamKeyId(const QString& keyId)
{
    if (m_vars) {
        m_vars->setRadialParamKeyId(keyId);
    }
}

QString SessionContext::radialParamKeyId() const
{
    return m_vars ? m_vars->radialParamKeyId() : QString();
}

// ========== 工具函数 ==========

QPointF SessionContext::calcFrameAbsolutePos(QPointF relativePos) const
{
    return m_inputDispatcher ? m_inputDispatcher->calcFrameAbsolutePos(relativePos) : QPointF();
}

QPointF SessionContext::calcScreenAbsolutePos(QPointF relativePos) const
{
    return m_inputDispatcher ? m_inputDispatcher->calcScreenAbsolutePos(relativePos) : QPointF();
}

void SessionContext::sendKeyEvent(int action, int keyCode)
{
    if (m_controller.isNull()) return;

    m_controller->postFastMsg(FastMsg::serializeKey(
        FastKeyEvent(action == 0 ? FKA_DOWN : FKA_UP, static_cast<quint16>(keyCode))));
}
