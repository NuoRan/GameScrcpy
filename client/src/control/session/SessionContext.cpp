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

#include "InputEvent.h"

#include "StringUtils.h"

#define LOG_TAG "SessionCtx"
#include "Logger.h"

SessionContext::SessionContext(const std::string& deviceId, Controller* controller)
    : m_deviceId(deviceId)
    , m_controller(controller)
{
    LOGD() << "[SessionContext] Created for device:" << deviceId;
    initComponents();
}

SessionContext::~SessionContext()
{
    LOGD() << "[SessionContext] Destroying for device:" << m_deviceId;

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

    LOGD() << "[SessionContext] Destroyed for device:" << m_deviceId;

    // 手动清理子组件（不再依赖 QObject 父子关系）
    delete m_inputDispatcher;
    delete m_handlerChain;
    delete m_steerWheelHandler;
    delete m_viewportHandler;
    delete m_freeLookHandler;
    delete m_cursorHandler;
    delete m_keyboardHandler;
    delete m_scriptBridge;
    delete m_vars;
}

void SessionContext::initComponents()
{
    // 1. 创建会话变量存储
    m_vars = new SessionVars();

    // 2. 创建脚本桥接器（先传 nullptr，稍后设置 SessionContext）
    m_scriptBridge = new ScriptBridge(m_controller, m_vars);
    m_scriptBridge->setSessionContext(this);

    // 3. 创建 HandlerChain 和 Handler
    m_handlerChain = new HandlerChain();

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

    m_handlerChain->init(m_controller, this);

    // 4. 设置 ScriptBridge 的 Handler 引用
    m_scriptBridge->setHandlers(m_steerWheelHandler, m_viewportHandler,
                                 m_freeLookHandler, m_cursorHandler, m_keyboardHandler);

    // 5. 创建输入分发器
    m_inputDispatcher = new InputDispatcher(m_controller, &m_keyMap);
    m_inputDispatcher->setHandlerChain(m_handlerChain);
    m_inputDispatcher->setSteerWheelHandler(m_steerWheelHandler);
    m_inputDispatcher->setViewportHandler(m_viewportHandler);
    m_inputDispatcher->setFreeLookHandler(m_freeLookHandler);
    m_inputDispatcher->setCursorHandler(m_cursorHandler);
    m_inputDispatcher->setKeyboardHandler(m_keyboardHandler);
    m_inputDispatcher->setScriptBridge(m_scriptBridge);

    // 连接信号
    m_inputDispatcher->grabCursor.connect([this](bool g) { grabCursor.fire(g); });

    // 连接脚本桥接器的模式切换信号
    if (ScriptEngine* engine = m_scriptBridge->scriptEngine()) {
        engine->shotmodeRequested.connect([this](bool gameMode) {
            script_setGameMapMode(gameMode);
        });
        engine->simulateKeyRequested.connect([this](const std::string& keyName, bool press) {
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

void SessionContext::mouseEvent(const InputEvent& from, const Size& frameSize, const Size& showSize)
{
    if (m_inputDispatcher) {
        m_inputDispatcher->mouseEvent(from, frameSize, showSize);
    }
}

void SessionContext::wheelEvent(const InputEvent& from, const Size& frameSize, const Size& showSize)
{
    if (m_inputDispatcher) {
        m_inputDispatcher->wheelEvent(from, frameSize, showSize);
    }
}

void SessionContext::keyEvent(const InputEvent& from, const Size& frameSize, const Size& showSize)
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

void SessionContext::loadKeyMap(const std::string& json, bool runAutoStartScripts)
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

void SessionContext::setFrameGrabCallback(std::function<cv::Mat()> callback)
{
    if (m_scriptBridge) {
        m_scriptBridge->setFrameGrabCallback(callback);
    }
}

void SessionContext::setGrayFrameGrabCallback(GrayFrameGrabCallback callback)
{
    if (m_scriptBridge) {
        m_scriptBridge->setGrayFrameGrabCallback(std::move(callback));
    }
}

cv::Mat SessionContext::grabFrame() const
{
    return m_scriptBridge ? m_scriptBridge->grabFrame() : cv::Mat();
}

// ========== 信号连接 ==========

void SessionContext::connectScriptTipSignal(std::function<void(const std::string&, int, int)> callback)
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

void SessionContext::setFrameSize(const Size& size)
{
    if (m_inputDispatcher) {
        m_inputDispatcher->setFrameSize(size);
    }
}

void SessionContext::setShowSize(const Size& size)
{
    if (m_inputDispatcher) {
        m_inputDispatcher->setShowSize(size);
    }
}

void SessionContext::setMobileSize(const Size& size)
{
    if (m_inputDispatcher) {
        m_inputDispatcher->setMobileSize(size);
    }
}

void SessionContext::setDevicePixelRatio(double dpr)
{
    if (m_inputDispatcher) {
        m_inputDispatcher->setDevicePixelRatio(dpr);
    }
}

Size SessionContext::frameSize() const
{
    return m_inputDispatcher ? m_inputDispatcher->frameSize() : Size();
}

Size SessionContext::showSize() const
{
    return m_inputDispatcher ? m_inputDispatcher->showSize() : Size();
}

Size SessionContext::mobileSize() const
{
    return m_inputDispatcher ? m_inputDispatcher->mobileSize() : Size();
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

PointF SessionContext::script_getMousePos()
{
    if (m_scriptBridge && m_inputDispatcher) {
        return m_scriptBridge->script_getMousePos(m_inputDispatcher->isCursorCaptured());
    }
    return PointF();
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

int SessionContext::script_getKeyStateByName(const std::string& displayName)
{
    if (m_scriptBridge && m_inputDispatcher) {
        return m_scriptBridge->script_getKeyStateByName(displayName, &m_keyMap, m_inputDispatcher->keyStates());
    }
    return 0;
}

KeyPosResult SessionContext::script_getKeyPos(int qtKey)
{
    if (m_scriptBridge) {
        return m_scriptBridge->script_getKeyPos(qtKey, &m_keyMap);
    }
    return KeyPosResult();
}

KeyPosResult SessionContext::script_getKeyPosByName(const std::string& displayName)
{
    if (m_scriptBridge) {
        return m_scriptBridge->script_getKeyPosByName(displayName, &m_keyMap);
    }
    return KeyPosResult();
}

void SessionContext::script_simulateKey(const std::string& keyName, bool press)
{
    int qtKey = keyNameToQtKey(keyName);
    if (qtKey == 0) {
        LOGW() << "[script_simulateKey] Unknown key:" << keyName;
        return;
    }

    InputEvent event{};
    event.type = press ? InputEventType::KeyPress : InputEventType::KeyRelease;
    event.key = qtKey;
    event.modifiers = InputModifier::None;
    event.isAutoRepeat = false;

    keyEvent(event, frameSize(), showSize());
}

int SessionContext::keyNameToQtKey(const std::string& keyName)
{
    // Convert to uppercase for comparison
    std::string k = keyName;
    for (auto& c : k) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    if (k == "SPACE" || k == " ") return GameKey::Key_Space;
    if (k == "ENTER" || k == "RETURN") return GameKey::Key_Return;
    if (k == "ESC" || k == "ESCAPE") return GameKey::Key_Escape;
    if (k == "TAB") return GameKey::Key_Tab;
    if (k == "BACKSPACE") return GameKey::Key_Backspace;
    if (k == "SHIFT") return GameKey::Key_Shift;
    if (k == "CTRL" || k == "CONTROL") return GameKey::Key_Control;
    if (k == "ALT") return GameKey::Key_Alt;
    if (k == "UP") return GameKey::Key_Up;
    if (k == "DOWN") return GameKey::Key_Down;
    if (k == "LEFT") return GameKey::Key_Left;
    if (k == "RIGHT") return GameKey::Key_Right;
    if (k == "TILDE" || k == "`") return GameKey::Key_QuoteLeft;

    if (k.size() >= 2 && k.size() <= 3 && k[0] == 'F') {
        try {
            int num = std::stoi(k.substr(1));
            if (num >= 1 && num <= 12) {
                return GameKey::Key_F1 + num - 1;
            }
        } catch (...) {}
    }

    if (k.size() == 1) {
        char c = k[0];
        if (c >= 'A' && c <= 'Z') return GameKey::Key_A + (c - 'A');
        if (c >= '0' && c <= '9') return GameKey::Key_0 + (c - '0');

        switch (c) {
            case '`': return GameKey::Key_QuoteLeft;
            case '~': return GameKey::Key_AsciiTilde;
            case '-': return GameKey::Key_Minus;
            case '=': return GameKey::Key_Equal;
            case '[': return GameKey::Key_BracketLeft;
            case ']': return GameKey::Key_BracketRight;
            case '\\': return GameKey::Key_Backslash;
            case ';': return GameKey::Key_Semicolon;
            case '\'': return GameKey::Key_Apostrophe;
            case ',': return GameKey::Key_Comma;
            case '.': return GameKey::Key_Period;
            case '/': return GameKey::Key_Slash;
        }
    }

    return 0;
}

// ========== 会话变量 ==========

ScriptValue SessionContext::getVar(const std::string& key, const ScriptValue& defaultValue) const
{
    return m_vars ? m_vars->getVar(key, defaultValue) : defaultValue;
}

void SessionContext::setVar(const std::string& key, const ScriptValue& value)
{
    if (m_vars) {
        m_vars->setVar(key, value);
    }
}

bool SessionContext::hasVar(const std::string& key) const
{
    return m_vars ? m_vars->hasVar(key) : false;
}

void SessionContext::removeVar(const std::string& key)
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

void SessionContext::addTouchSeq(int keyId, uint32_t seqId)
{
    if (m_vars) {
        m_vars->addTouchSeq(keyId, seqId);
    }
}

std::vector<uint32_t> SessionContext::takeTouchSeqs(int keyId)
{
    return m_vars ? m_vars->takeTouchSeqs(keyId) : std::vector<uint32_t>();
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

void SessionContext::setRadialParamKeyId(const std::string& keyId)
{
    if (m_vars) {
        m_vars->setRadialParamKeyId(keyId);
    }
}

std::string SessionContext::radialParamKeyId() const
{
    return m_vars ? m_vars->radialParamKeyId() : std::string();
}

// ========== 工具函数 ==========

PointF SessionContext::calcFrameAbsolutePos(PointF relativePos) const
{
    return m_inputDispatcher ? m_inputDispatcher->calcFrameAbsolutePos(relativePos) : PointF();
}

PointF SessionContext::calcScreenAbsolutePos(PointF relativePos) const
{
    return m_inputDispatcher ? m_inputDispatcher->calcScreenAbsolutePos(relativePos) : PointF();
}

void SessionContext::sendKeyEvent(int action, int keyCode)
{
    if (!m_controller) return;

    m_controller->postFastMsg(FastMsg::serializeKey(
        FastKeyEvent(action == 0 ? FKA_DOWN : FKA_UP, static_cast<uint16_t>(keyCode))));
}
