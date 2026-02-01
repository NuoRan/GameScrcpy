#include <cmath>
#include <QDebug>
#include <QCursor>
#include <QGuiApplication>
#include <QTimer>
#include <QTime>
#include <QRandomGenerator>

#include "inputconvertgame.h"
#include "scriptapi.h"
#include "controlmsg.h"
#include "controlmsgpool.h"
#include "fastmsg.h"
#include "controller.h"

#define CURSOR_POS_CHECK 50

InputConvertGame::InputConvertGame(Controller *controller) : InputConvertBase(controller) {
    m_ctrlSteerWheel.delayData.timer = new QTimer(this);
    m_ctrlSteerWheel.delayData.timer->setSingleShot(true);
    connect(m_ctrlSteerWheel.delayData.timer, &QTimer::timeout, this, &InputConvertGame::onSteerWheelTimer);

    m_jsEngine = new QJSEngine(this);
    m_scriptApi = new ScriptApi(controller, this, this);

    m_moveSendTimer = new QTimer(this);
    m_moveSendTimer->setSingleShot(false);
    m_moveSendTimer->setInterval(8); // 8毫秒发一次（约125fps），平衡流畅度和带宽
    connect(m_moveSendTimer, &QTimer::timeout, this, &InputConvertGame::onMouseMoveTimer);
    m_moveSendTimer->start();

    // 初始化边缘回中延迟定时器
    m_ctrlMouseMove.centerRepressTimer = new QTimer(this);
    m_ctrlMouseMove.centerRepressTimer->setSingleShot(true);
    m_ctrlMouseMove.centerRepressTimer->setInterval(15); // 15ms延迟
    connect(m_ctrlMouseMove.centerRepressTimer, &QTimer::timeout, this, &InputConvertGame::onCenterRepressTimer);

    // 初始化空闲回中定时器（鼠标不动就回中心）
    m_ctrlMouseMove.idleCenterTimer = new QTimer(this);
    m_ctrlMouseMove.idleCenterTimer->setSingleShot(true);
    m_ctrlMouseMove.idleCenterTimer->setInterval(100); // 100ms 无移动则回中心
    connect(m_ctrlMouseMove.idleCenterTimer, &QTimer::timeout, this, &InputConvertGame::onIdleCenterTimer);

    QJSValue apiObj = m_jsEngine->newQObject(m_scriptApi);
    m_jsEngine->globalObject().setProperty("mapi", apiObj);
    m_jsEngine->installExtensions(QJSEngine::ConsoleExtension);

    // 默认状态：显示光标 (m_cursorCaptured = false)
    setCursorCaptured(false);
}

InputConvertGame::~InputConvertGame() {
    // 停止定时器
    if (m_moveSendTimer) {
        m_moveSendTimer->stop();
    }

    // 清理轮盘状态（发送 UP 事件）
    resetSteerWheelState();

    // 清理视角控制状态
    mouseMoveStopTouch();
}

// [新增] 设置帧获取回调 (用于脚本图像识别)
void InputConvertGame::setFrameGrabCallback(std::function<QImage()> callback)
{
    if (m_scriptApi) {
        m_scriptApi->setFrameGrabCallback(callback);
    }
}

// ---------------------------------------------------------
// 鼠标事件处理
// 核心逻辑：区分光标显示/隐藏状态
// ---------------------------------------------------------
void InputConvertGame::mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize)
{
    // 1. 更新尺寸
    updateSize(frameSize, showSize);

    // 2. 检测“模式切换”热键（现在是光标显示开关）
    // 即使在光标显示模式下，热键检测逻辑也必须优先执行
    if (m_keyMap.isSwitchOnKeyboard() == false && m_keyMap.getSwitchKey() == static_cast<int>(from->button())) {
        if (from->type() != QEvent::MouseButtonPress) {
            return;
        }
        // 切换光标状态
        if (!toggleCursorCaptured()) {
            m_needBackMouseMove = false;
        }
        return;
    }

    // 3. 状态分支
    if (!m_cursorCaptured) {
        // [状态 A：光标显示]
        // 行为：左键点击屏幕直接触发触控（从普通模式移植），键盘 WASD 依然有效。
        // 重要：这里直接处理并 Return，从而【屏蔽】了后面 GameMap 对左键/右键的映射。
        processCursorMouse(from);
        return;
    }

    // [状态 B：光标隐藏/捕获] (游戏模式)
    if (!m_needBackMouseMove) {
        // 【优化】鼠标按钮事件优先处理，不要被 Move 事件的处理阻塞
        // 这样可以避免 QCursor::setPos() 的延迟影响点击响应
        if (from->type() == QEvent::MouseButtonPress || from->type() == QEvent::MouseButtonRelease) {
            if (processMouseClick(from)) {
                return;
            }
        }

        // MouseMove 事件处理
        if (m_keyMap.isValidMouseMoveMap()) {
            if (processMouseMove(from)) {
                return;
            }
        }
    }
}

void InputConvertGame::wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize)
{
    Q_UNUSED(from);
    Q_UNUSED(frameSize);
    Q_UNUSED(showSize);
    // 滚轮事件已禁用 - 游戏投屏模式下不需要滚动功能
    // 如果需要切枪等功能，请在 keymap 中映射按键
}

void InputConvertGame::keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize)
{
    // 键盘事件无论光标是否显示，都始终处理
    // 这样保证了 "一边点菜单(光标显示) 一边 WASD 移动(游戏映射)" 的需求

    if (from->type() == QEvent::KeyPress) {
        m_keyStates[from->key()] = true;
    } else if (from->type() == QEvent::KeyRelease && !from->isAutoRepeat()) {
        m_keyStates[from->key()] = false;
    }

    // 检测键盘上的切换键
    if (m_keyMap.isSwitchOnKeyboard() && m_keyMap.getSwitchKey() == from->key()) {
        if (QEvent::KeyPress != from->type()) {
            return;
        }
        if (!toggleCursorCaptured()) {
            m_needBackMouseMove = false;
        }
        return;
    }

    // 辅助按键处理（Shift+数字等）
    int key = from->key();
    const KeyMap::KeyMapNode *pNode = &m_keyMap.getKeyMapNodeKey(key);

    if (pNode->type == KeyMap::KMT_INVALID && (from->modifiers() & Qt::ShiftModifier)) {
        int tempKey = 0;
        switch (key) {
        case Qt::Key_Exclam:        tempKey = Qt::Key_1; break;
        case Qt::Key_At:            tempKey = Qt::Key_2; break;
        case Qt::Key_NumberSign:    tempKey = Qt::Key_3; break;
        case Qt::Key_Dollar:        tempKey = Qt::Key_4; break;
        case Qt::Key_Percent:       tempKey = Qt::Key_5; break;
        case Qt::Key_AsciiCircum:   tempKey = Qt::Key_6; break;
        case Qt::Key_Ampersand:     tempKey = Qt::Key_7; break;
        case Qt::Key_Asterisk:      tempKey = Qt::Key_8; break;
        case Qt::Key_ParenLeft:     tempKey = Qt::Key_9; break;
        case Qt::Key_ParenRight:    tempKey = Qt::Key_0; break;
        case Qt::Key_Underscore:    tempKey = Qt::Key_Minus; break;
        case Qt::Key_Plus:          tempKey = Qt::Key_Equal; break;
        }

        if (tempKey != 0) {
            const KeyMap::KeyMapNode *tempNode = &m_keyMap.getKeyMapNodeKey(tempKey);
            if (tempNode->type != KeyMap::KMT_INVALID) {
                pNode = tempNode;
            }
        }
    }

    const KeyMap::KeyMapNode &node = *pNode;

    updateSize(frameSize, showSize);
    if (!from || from->isAutoRepeat()) {
        return;
    }

    switch (node.type) {
    case KeyMap::KMT_STEER_WHEEL:
        processSteerWheel(node, from);
        return;

    case KeyMap::KMT_ANDROID_KEY:
        processAndroidKey(node.data.androidKey.keyNode.androidKey, from);
        return;

    case KeyMap::KMT_SCRIPT:
        if (from->type() == QEvent::KeyPress || from->type() == QEvent::KeyRelease) {
            processScript(node, from->type() == QEvent::KeyPress);
        }
        return;

    case KeyMap::KMT_CAMERA_MOVE:
        break;

    default:
        // 如果没有映射，尝试发送普通 Android 按键
        // 这样可以实现在菜单中输入文字等功能
        {
            AndroidKeyeventAction action = (from->type() == QEvent::KeyPress)
            ? AKEY_EVENT_ACTION_DOWN : AKEY_EVENT_ACTION_UP;
            AndroidKeycode keyCode = convertKeyCode(from->key(), from->modifiers());
            if (keyCode != AKEYCODE_UNKNOWN) {
                sendKeyEvent(action, keyCode);
            }
        }
        break;
    }
}

// ---------------------------------------------------------
// [核心] 处理光标显示时的鼠标事件
// 完全从 InputConvertNormal 移植而来，实现左键点触屏幕
// ---------------------------------------------------------
void InputConvertGame::processCursorMouse(const QMouseEvent *from)
{
    if (!from) return;

    // 【光标显示模式】所有鼠标按钮（左键、中键、右键）都直接映射为屏幕点击
    // 这样可以在光标模式下自由点击屏幕任意位置，屏蔽掉脚本中的鼠标按钮映射
    //
    // 实现细节：
    // - 只响应左键的实际触摸（因为 Android 只识别一个主触摸点）
    // - 中键和右键的事件被"吃掉"，不触发任何操作，也不传递给游戏映射

    AndroidMotioneventAction action;
    switch (from->type()) {
    case QEvent::MouseButtonPress:
        // 只有左键按下才触发触摸
        if (from->button() != Qt::LeftButton) {
            return;  // 中键/右键：直接吃掉，不做任何处理
        }
        action = AMOTION_EVENT_ACTION_DOWN;
        break;
    case QEvent::MouseButtonRelease:
        // 只有左键释放才触发触摸
        if (from->button() != Qt::LeftButton) {
            return;  // 中键/右键：直接吃掉，不做任何处理
        }
        action = AMOTION_EVENT_ACTION_UP;
        break;
    case QEvent::MouseMove:
        // 普通模式下，只响应按住左键的移动（拖拽）
        if (!(from->buttons() & Qt::LeftButton)) {
            return;
        }
        action = AMOTION_EVENT_ACTION_MOVE;
        break;
    default:
        return;
    }

    QSize targetSize = getTargetSize(m_frameSize, m_showSize);
    if (targetSize.isEmpty()) return;

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QPointF localPos = from->localPos();
#else
    QPointF localPos = from->position();
#endif

    QPointF absolutePos;
    if (m_showSize.width() > 0 && m_showSize.height() > 0) {
        absolutePos.setX(localPos.x() / m_showSize.width() * targetSize.width());
        absolutePos.setY(localPos.y() / m_showSize.height() * targetSize.height());
    } else {
        return;
    }

    // 【优化】使用对象池减少堆分配
    auto pooledMsg = ControlMsgPool::instance().acquire(ControlMsg::CMT_INJECT_TOUCH);
    ControlMsg *controlMsg = pooledMsg.get();
    if (!controlMsg) return;

    // 强制使用 GENERIC_FINGER ID，不占用游戏的多点触控 ID
    controlMsg->setInjectTouchMsgData(
        static_cast<quint64>(POINTER_ID_GENERIC_FINGER),
        action,
        convertMouseButton(from->button()),
        convertMouseButtons(from->buttons()),
        QRect(absolutePos.toPoint(), targetSize),
        AMOTION_EVENT_ACTION_DOWN == action ? 1.0f : 0.0f);

    sendControlMsg(pooledMsg.release());
}

// 辅助函数：发送视角控制的 FastMsg 触摸事件
void InputConvertGame::sendViewFastTouch(quint8 action, const QPointF& pos) {
    if (!m_controller) return;

    quint16 nx = static_cast<quint16>(qBound(0.0, pos.x(), 1.0) * 65535);
    quint16 ny = static_cast<quint16>(qBound(0.0, pos.y(), 1.0) * 65535);

    QByteArray data;
    if (action == FTA_DOWN) {
        data = FastMsg::touchDownRaw(m_ctrlMouseMove.fastTouchSeqId, nx, ny);
    } else if (action == FTA_UP) {
        data = FastMsg::touchUpRaw(m_ctrlMouseMove.fastTouchSeqId, nx, ny);
    } else {
        data = FastMsg::touchMoveRaw(m_ctrlMouseMove.fastTouchSeqId, nx, ny);
    }

    m_controller->postFastMsg(data);
}

void InputConvertGame::onMouseMoveTimer() {
    // 如果正在等待边缘回中重按，暂时不处理移动（积累到 pendingOvershoot）
    if (m_ctrlMouseMove.waitingForCenterRepress) {
        // 继续积累移动增量到 pendingOvershoot
        m_ctrlMouseMove.pendingOvershoot += m_pendingMoveDelta;
        m_pendingMoveDelta = {0, 0};
        return;
    }

    // 如果这段时间内没有移动量，直接返回，不发空包
    if (m_pendingMoveDelta.isNull()) {
        return;
    }

    // 有新的移动：重置空闲计时器
    if (m_ctrlMouseMove.idleCenterTimer) {
        m_ctrlMouseMove.idleCenterTimer->start();
    }

    // 1. 计算新位置
    QPointF newPos = m_ctrlMouseMove.lastConverPos + m_pendingMoveDelta;
    m_pendingMoveDelta = {0, 0}; // 清空积攒量

    QPointF centerPos = m_keyMap.getMouseMoveMap().data.mouseMove.startPos;
    const double MARGIN = 0.05;
    const double EDGE_MIN = MARGIN;
    const double EDGE_MAX = 1.0 - MARGIN;

    // 边界检测函数
    auto isOutOfBounds = [&](const QPointF& pos) {
        return pos.x() < EDGE_MIN || pos.x() > EDGE_MAX ||
               pos.y() < EDGE_MIN || pos.y() > EDGE_MAX;
    };

    // 2. [边界处理] 如果超出边界，使用边缘回中延迟定时器
    if (isOutOfBounds(newPos) && m_ctrlMouseMove.touching) {
        // 停止空闲定时器（边缘回中优先）
        if (m_ctrlMouseMove.idleCenterTimer) {
            m_ctrlMouseMove.idleCenterTimer->stop();
        }

        // Step 1: 移动到边缘
        QPointF edgePos;
        edgePos.setX(qBound(EDGE_MIN, newPos.x(), EDGE_MAX));
        edgePos.setY(qBound(EDGE_MIN, newPos.y(), EDGE_MAX));
        sendViewFastTouch(FTA_MOVE, edgePos);

        // Step 2: 在边缘抬起手指
        sendViewFastTouch(FTA_UP, edgePos);
        m_ctrlMouseMove.touching = false;

        // Step 3: 保存状态，启动延迟定时器
        m_ctrlMouseMove.waitingForCenterRepress = true;
        m_ctrlMouseMove.pendingCenterPos = centerPos;
        m_ctrlMouseMove.pendingOvershoot = newPos - edgePos;  // 超出边界的剩余增量
        m_ctrlMouseMove.centerRepressTimer->start();
        return;
    }

    // 3. 正常情况：更新位置并发送
    m_ctrlMouseMove.lastConverPos = newPos;
    if (m_ctrlMouseMove.touching) {
        sendViewFastTouch(FTA_MOVE, m_ctrlMouseMove.lastConverPos);
    }
}

// 空闲回中定时器回调（鼠标停止移动后执行）
void InputConvertGame::onIdleCenterTimer() {
    // 如果正在等待边缘回中，不执行空闲回中
    if (m_ctrlMouseMove.waitingForCenterRepress) {
        return;
    }

    if (!m_ctrlMouseMove.touching) {
        return;
    }

    // 鼠标停止移动：使用边缘回中延迟定时器的方式回到中心
    QPointF centerPos = m_keyMap.getMouseMoveMap().data.mouseMove.startPos;

    // Step 1: 在当前位置抬起手指
    sendViewFastTouch(FTA_UP, m_ctrlMouseMove.lastConverPos);
    m_ctrlMouseMove.touching = false;

    // Step 2: 保存状态，启动延迟定时器（和边缘回中一样）
    m_ctrlMouseMove.waitingForCenterRepress = true;
    m_ctrlMouseMove.pendingCenterPos = centerPos;
    m_ctrlMouseMove.pendingOvershoot = {0, 0};  // 空闲回中没有超出增量
    m_ctrlMouseMove.centerRepressTimer->start();
}

// 边缘回中延迟定时器回调（到达边缘后延迟执行）
void InputConvertGame::onCenterRepressTimer() {
    if (!m_ctrlMouseMove.waitingForCenterRepress) {
        return;
    }

    const double MARGIN = 0.05;
    const double EDGE_MIN = MARGIN;
    const double EDGE_MAX = 1.0 - MARGIN;

    // 边界检测函数
    auto isOutOfBounds = [&](const QPointF& pos) {
        return pos.x() < EDGE_MIN || pos.x() > EDGE_MAX ||
               pos.y() < EDGE_MIN || pos.y() > EDGE_MAX;
    };

    // Step 3: 在中心重新按下（生成新的 seqId）
    m_ctrlMouseMove.fastTouchSeqId = FastTouchSeq::next();
    sendViewFastTouch(FTA_DOWN, m_ctrlMouseMove.pendingCenterPos);
    m_ctrlMouseMove.touching = true;

    // 计算新的中心位置（加上等待期间积累的增量）
    QPointF newCenterPos = m_ctrlMouseMove.pendingCenterPos + m_ctrlMouseMove.pendingOvershoot;

    // 如果新位置仍然越界，clamp 到边界
    if (isOutOfBounds(newCenterPos)) {
        newCenterPos.setX(qBound(EDGE_MIN, newCenterPos.x(), EDGE_MAX));
        newCenterPos.setY(qBound(EDGE_MIN, newCenterPos.y(), EDGE_MAX));
    }

    // Step 4: 移动到新位置
    sendViewFastTouch(FTA_MOVE, newCenterPos);
    m_ctrlMouseMove.lastConverPos = newCenterPos;

    // 清除等待状态
    m_ctrlMouseMove.waitingForCenterRepress = false;
    m_ctrlMouseMove.pendingOvershoot = {0, 0};

    // 重新启动空闲定时器
    if (m_ctrlMouseMove.idleCenterTimer) {
        m_ctrlMouseMove.idleCenterTimer->start();
    }
}

void InputConvertGame::processScript(const KeyMap::KeyMapNode &node, bool isPress)
{
    if (!m_jsEngine || !m_scriptApi) return;

    int key = node.data.script.keyNode.key;

    // 使用按键值作为 keyId，用于 ScriptApi 中的 holdpress 状态管理
    // 注意：不再使用 attachTouchID/detachTouchID，因为 FastMsg 协议由服务端管理多指
    m_scriptApi->setKeyId(key);

    m_scriptApi->setAnchorPosition(node.data.script.keyNode.pos);
    m_scriptApi->setPress(isPress);

    QString script = node.script;
    if (script.isEmpty()) return;

    // 【脚本预编译优化】使用缓存避免每次 evaluate() 的解析开销
    QJSValue compiledScript;

    auto it = m_compiledScripts.find(script);
    if (it != m_compiledScripts.end()) {
        // 命中缓存
        compiledScript = it.value();
    } else {
        // 未命中，编译并缓存
        // 将脚本包装为函数以便重复调用
        QString wrappedScript = QStringLiteral("(function() { %1 })").arg(script);
        compiledScript = m_jsEngine->evaluate(wrappedScript);

        if (compiledScript.isError()) {
            qWarning() << "Script compile error:" << compiledScript.toString();
            return;
        }

        m_compiledScripts.insert(script, compiledScript);
    }

    // 调用预编译的函数
    QJSValue result = compiledScript.call();
    if (result.isError()) {
        qWarning() << "Script execution error:" << result.toString();
    }
}

void InputConvertGame::script_resetView()
{
    mouseMoveStopTouch();
    m_ctrlMouseMove.touching = false;
    mouseMoveStartTouch(nullptr);
}

void InputConvertGame::script_setSteerWheelOffset(double up, double down, double left, double right)
{
    m_keyMap.updateSteerWheelOffset(up, down, left, right);
}

QPointF InputConvertGame::script_getMousePos()
{
    return m_ctrlMouseMove.lastConverPos;
}

void InputConvertGame::script_setGameMapMode(bool enter)
{
    // 脚本请求切换模式，现在映射为设置光标
    if (m_cursorCaptured != enter) {
        toggleCursorCaptured();
    }
}

int InputConvertGame::script_getKeyState(int qtKey)
{
    return m_keyStates.value(qtKey, false) ? 1 : 0;
}

QVariantMap InputConvertGame::script_getKeyPos(int qtKey)
{
    QVariantMap map;
    map.insert("x", -1);
    map.insert("y", -1);

    const KeyMap::KeyMapNode& node = m_keyMap.getKeyMapNodeKey(qtKey);

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
        map["x"] = pos.x();
        map["y"] = pos.y();
    }
    return map;
}

void InputConvertGame::loadKeyMap(const QString &json)
{
    m_keyMap.loadKeyMap(json);

    // 【优化】清空脚本缓存，因为新的 keymap 可能包含不同的脚本
    m_compiledScripts.clear();
}

void InputConvertGame::updateSize(const QSize &frameSize, const QSize &showSize)
{
    if (showSize != m_showSize) {
        if (m_cursorCaptured && m_keyMap.isValidMouseMoveMap()) {
#ifdef QT_NO_DEBUG
            emit grabCursor(true);
#endif
        }
    }
    m_frameSize = frameSize;
    m_showSize = showSize;

    if (m_scriptApi) {
        QSize realSize = getTargetSize(frameSize, showSize);
        m_scriptApi->setVideoSize(realSize);

    }
}

void InputConvertGame::sendTouchDownEvent(int id, QPointF pos)
{
    sendTouchEvent(id, pos, AMOTION_EVENT_ACTION_DOWN);
}

void InputConvertGame::sendTouchMoveEvent(int id, QPointF pos)
{
    sendTouchEvent(id, pos, AMOTION_EVENT_ACTION_MOVE);
}

void InputConvertGame::sendTouchUpEvent(int id, QPointF pos)
{
    sendTouchEvent(id, pos, AMOTION_EVENT_ACTION_UP);
}

void InputConvertGame::sendTouchEvent(int id, QPointF pos, AndroidMotioneventAction action)
{
    if (0 > id || MULTI_TOUCH_MAX_NUM - 1 < id) {
        Q_ASSERT(0);
        return;
    }

    // 【优化】使用对象池减少堆分配
    auto pooledMsg = ControlMsgPool::instance().acquire(ControlMsg::CMT_INJECT_TOUCH);
    ControlMsg *controlMsg = pooledMsg.get();
    if (!controlMsg) return;

    QSize targetSize = getTargetSize(m_frameSize, m_showSize);

    QPoint absolutePos;
    absolutePos.setX(targetSize.width() * pos.x());
    absolutePos.setY(targetSize.height() * pos.y());

    controlMsg->setInjectTouchMsgData(
        static_cast<quint64>(id),
        action,
        static_cast<AndroidMotioneventButtons>(0),
        static_cast<AndroidMotioneventButtons>(0),
        QRect(absolutePos, targetSize),
        AMOTION_EVENT_ACTION_DOWN == action ? 1.0f : 0.0f);
    sendControlMsg(pooledMsg.release());
}

void InputConvertGame::sendKeyEvent(AndroidKeyeventAction action, AndroidKeycode keyCode) {
    // 【优化】使用对象池减少堆分配
    auto pooledMsg = ControlMsgPool::instance().acquire(ControlMsg::CMT_INJECT_KEYCODE);
    ControlMsg *controlMsg = pooledMsg.get();
    if (!controlMsg) {
        return;
    }

    controlMsg->setInjectKeycodeMsgData(action, keyCode, 0, AMETA_NONE);
    sendControlMsg(pooledMsg.release());
}

QPointF InputConvertGame::calcFrameAbsolutePos(QPointF relativePos)
{
    QPointF absolutePos;
    QSize targetSize = getTargetSize(m_frameSize, m_showSize);

    absolutePos.setX(targetSize.width() * relativePos.x());
    absolutePos.setY(targetSize.height() * relativePos.y());
    return absolutePos;
}

QPointF InputConvertGame::calcScreenAbsolutePos(QPointF relativePos)
{
    QPointF absolutePos;
    absolutePos.setX(m_showSize.width() * relativePos.x());
    absolutePos.setY(m_showSize.height() * relativePos.y());
    return absolutePos;
}

int InputConvertGame::attachTouchID(int key)
{
    for (int i = 0; i < MULTI_TOUCH_MAX_NUM; i++) {
        if (0 == m_multiTouchID[i]) {
            m_multiTouchID[i] = key;
            return i;
        }
    }
    return -1;
}

void InputConvertGame::detachTouchID(int key)
{
    for (int i = 0; i < MULTI_TOUCH_MAX_NUM; i++) {
        if (key == m_multiTouchID[i]) {
            m_multiTouchID[i] = 0;
            return;
        }
    }
}

int InputConvertGame::getTouchID(int key)
{
    for (int i = 0; i < MULTI_TOUCH_MAX_NUM; i++) {
        if (key == m_multiTouchID[i]) {
            return i;
        }
    }
    return -1;
}

void InputConvertGame::getDelayQueue(const QPointF& start, const QPointF& end,
                                     const double& distanceStep, const double& posStepconst,
                                     quint32 lowestTimer, quint32 highestTimer,
                                     QQueue<QPointF>& queuePos, QQueue<quint32>& queueTimer) {
    double x1 = start.x();
    double y1 = start.y();
    double x2 = end.x();
    double y2 = end.y();

    double dx=x2-x1;
    double dy=y2-y1;
    double e=(fabs(dx)>fabs(dy))?fabs(dx):fabs(dy);
    e /= distanceStep;
    dx/=e;
    dy/=e;

    QQueue<QPointF> queue;
    QQueue<quint32> queue2;
    for(int i=1;i<=e;i++) {
        QPointF pos(x1+(QRandomGenerator::global()->bounded(posStepconst*2)-posStepconst), y1+(QRandomGenerator::global()->bounded(posStepconst*2)-posStepconst));
        queue.enqueue(pos);
        queue2.enqueue(QRandomGenerator::global()->bounded(lowestTimer, highestTimer));
        x1+=dx;
        y1+=dy;
    }

    queuePos = queue;
    queueTimer = queue2;
}

// 辅助函数：发送轮盘的 FastMsg 触摸事件
void InputConvertGame::sendSteerWheelFastTouch(quint8 action, const QPointF& pos) {
    if (!m_controller) return;

    quint16 nx = static_cast<quint16>(qBound(0.0, pos.x(), 1.0) * 65535);
    quint16 ny = static_cast<quint16>(qBound(0.0, pos.y(), 1.0) * 65535);

    QByteArray data;
    if (action == FTA_DOWN) {
        data = FastMsg::touchDownRaw(m_ctrlSteerWheel.fastTouchSeqId, nx, ny);
    } else if (action == FTA_UP) {
        data = FastMsg::touchUpRaw(m_ctrlSteerWheel.fastTouchSeqId, nx, ny);
    } else {
        data = FastMsg::touchMoveRaw(m_ctrlSteerWheel.fastTouchSeqId, nx, ny);
    }

    m_controller->postFastMsg(data);
}

void InputConvertGame::onSteerWheelTimer() {
    if(m_ctrlSteerWheel.delayData.queuePos.empty()) {
        return;
    }
    m_ctrlSteerWheel.delayData.currentPos = m_ctrlSteerWheel.delayData.queuePos.dequeue();
    sendSteerWheelFastTouch(FTA_MOVE, m_ctrlSteerWheel.delayData.currentPos);

    if(m_ctrlSteerWheel.delayData.queuePos.empty() && m_ctrlSteerWheel.delayData.pressedNum == 0) {
        sendSteerWheelFastTouch(FTA_UP, m_ctrlSteerWheel.delayData.currentPos);
        m_ctrlSteerWheel.fastTouchSeqId = 0;
        return;
    }

    if(!m_ctrlSteerWheel.delayData.queuePos.empty()) {
        m_ctrlSteerWheel.delayData.timer->start(m_ctrlSteerWheel.delayData.queueTimer.dequeue());
    }
}

void InputConvertGame::processSteerWheel(const KeyMap::KeyMapNode &node, const QKeyEvent *from)
{
    int key = from->key();
    bool flag = from->type() == QEvent::KeyPress;

    if (key == node.data.steerWheel.up.key) {
        m_ctrlSteerWheel.pressedUp = flag;
    } else if (key == node.data.steerWheel.right.key) {
        m_ctrlSteerWheel.pressedRight = flag;
    } else if (key == node.data.steerWheel.down.key) {
        m_ctrlSteerWheel.pressedDown = flag;
    } else {
        m_ctrlSteerWheel.pressedLeft = flag;
    }

    QPointF offset(0.0, 0.0);
    int pressedNum = 0;
    if (m_ctrlSteerWheel.pressedUp) {
        ++pressedNum;
        offset.ry() -= node.data.steerWheel.up.extendOffset;
    }
    if (m_ctrlSteerWheel.pressedRight) {
        ++pressedNum;
        offset.rx() += node.data.steerWheel.right.extendOffset;
    }
    if (m_ctrlSteerWheel.pressedDown) {
        ++pressedNum;
        offset.ry() += node.data.steerWheel.down.extendOffset;
    }
    if (m_ctrlSteerWheel.pressedLeft) {
        ++pressedNum;
        offset.rx() -= node.data.steerWheel.left.extendOffset;
    }
    m_ctrlSteerWheel.delayData.pressedNum = pressedNum;

    if (pressedNum == 0) {
        if (m_ctrlSteerWheel.delayData.timer->isActive()) {
            m_ctrlSteerWheel.delayData.timer->stop();
            m_ctrlSteerWheel.delayData.queueTimer.clear();
            m_ctrlSteerWheel.delayData.queuePos.clear();
        }

        // 使用 FastMsg 协议发送 UP
        if (m_ctrlSteerWheel.fastTouchSeqId != 0) {
            sendSteerWheelFastTouch(FTA_UP, m_ctrlSteerWheel.delayData.currentPos);
            m_ctrlSteerWheel.fastTouchSeqId = 0;
        }
        return;
    }

    m_ctrlSteerWheel.delayData.timer->stop();
    m_ctrlSteerWheel.delayData.queueTimer.clear();
    m_ctrlSteerWheel.delayData.queuePos.clear();

    if (pressedNum == 1 && flag) {
        m_ctrlSteerWheel.touchKey = from->key();
        // 使用 FastMsg 协议：生成新的序列 ID 并发送 DOWN
        m_ctrlSteerWheel.fastTouchSeqId = FastTouchSeq::next();
        m_ctrlSteerWheel.delayData.currentPos = node.data.steerWheel.centerPos;
        sendSteerWheelFastTouch(FTA_DOWN, node.data.steerWheel.centerPos);

        getDelayQueue(node.data.steerWheel.centerPos, node.data.steerWheel.centerPos+offset,
                      0.01f, 0.002f, 2, 8,
                      m_ctrlSteerWheel.delayData.queuePos,
                      m_ctrlSteerWheel.delayData.queueTimer);
    } else {
        getDelayQueue(m_ctrlSteerWheel.delayData.currentPos, node.data.steerWheel.centerPos+offset,
                      0.01f, 0.002f, 2, 8,
                      m_ctrlSteerWheel.delayData.queuePos,
                      m_ctrlSteerWheel.delayData.queueTimer);
    }
    m_ctrlSteerWheel.delayData.timer->start();
    return;
}

void InputConvertGame::processAndroidKey(AndroidKeycode androidKey, const QKeyEvent *from)
{
    if (AKEYCODE_UNKNOWN == androidKey) {
        return;
    }

    AndroidKeyeventAction action;
    switch (from->type()) {
    case QEvent::KeyPress:
        action = AKEY_EVENT_ACTION_DOWN;
        break;
    case QEvent::KeyRelease:
        action = AKEY_EVENT_ACTION_UP;
        break;
    default:
        return;
    }

    sendKeyEvent(action, androidKey);
}

bool InputConvertGame::processMouseClick(const QMouseEvent *from)
{
    const KeyMap::KeyMapNode &node = m_keyMap.getKeyMapNodeMouse(from->button());
    if (KeyMap::KMT_INVALID == node.type) {
        return false;
    }

    if (node.type == KeyMap::KMT_SCRIPT) {
        if (from->type() == QEvent::MouseButtonPress || from->type() == QEvent::MouseButtonRelease) {
            processScript(node, from->type() == QEvent::MouseButtonPress);
        }
        return true;
    }

    return false;
}


bool InputConvertGame::processMouseMove(const QMouseEvent *from)
{
    if (QEvent::MouseMove != from->type()) {
        return false;
    }

    // 1. 如果这是上次 SetPos 产生的“回声”事件，直接丢弃
    if (m_ctrlMouseMove.ignoreCount > 0) {
        --m_ctrlMouseMove.ignoreCount;
        return true;
    }

    QPoint center(m_showSize.width() / 2, m_showSize.height() / 2);

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QPointF currentPos = from->localPos();
#else
    QPointF currentPos = from->position();
#endif

    // 计算你手动的偏移量
    QPointF delta = currentPos - center;
    if (delta.isNull()) {
        return true;
    }

    // 如果偏移量极小（比如浮点数误差），也忽略，防止微小震荡
    if (delta.manhattanLength() < 1.0) {
        return true;
    }

    // ============================================
    // 【核心修复点】
    // 在强制归中之前，必须告诉程序：
    // "接下来操作系统会发一个回到中心的事件，那个不是用户动的，忽略它！"
    // ============================================
    m_ctrlMouseMove.ignoreCount = 1;
    moveCursorTo(from, center);

    // 下面处理正常的移动逻辑
    if (m_processMouseMove) {
        // 2. 如果没按下且不在等待回中状态，先按下
        // 注意：如果 waitingForCenterRepress=true，不要创建新触摸，让 onCenterRepressTimer 处理
        if (!m_ctrlMouseMove.touching && !m_ctrlMouseMove.waitingForCenterRepress) {
            mouseMoveStartTouch(from);
        }

        // 3. 计算 Android 增量
        QPointF speedRatio = m_keyMap.getMouseMoveMap().data.mouseMove.speedRatio;
        QSize targetSize = getTargetSize(m_frameSize, m_showSize);
        QPointF distance(0, 0);

        if (targetSize.width() > 0 && targetSize.height() > 0 && speedRatio.x() > 0 && speedRatio.y() > 0) {
            distance.setX(delta.x() / speedRatio.x() / targetSize.width());
            distance.setY(delta.y() / speedRatio.y() / targetSize.height());
        }

        m_pendingMoveDelta += distance;
        startMouseMoveTimer();
    }

    return true;
}

void InputConvertGame::moveCursorTo(const QMouseEvent *from, const QPoint &localPosPixel)
{
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QPoint posOffset = from->pos() - localPosPixel;
    QPoint globalPos = from->globalPos();
#else
    QPoint posOffset = from->position().toPoint() - localPosPixel;
    QPoint globalPos = from->globalPosition().toPoint();
#endif
    globalPos -= posOffset;

    // 【优化】延迟执行 setPos，避免阻塞当前事件处理
    // 这样鼠标按钮事件可以立即被处理，不会被 setPos 的系统调用延迟
    QTimer::singleShot(0, [globalPos]() {
        QCursor::setPos(globalPos);
    });
}

void InputConvertGame::mouseMoveStartTouch(const QMouseEvent *from)
{
    Q_UNUSED(from)
    if (!m_ctrlMouseMove.touching) {
        QPointF mouseMoveStartPos = m_keyMap.getMouseMoveMap().data.mouseMove.startPos;
        // 使用 FastMsg 协议：生成新的序列 ID 并发送 DOWN
        m_ctrlMouseMove.fastTouchSeqId = FastTouchSeq::next();
        sendViewFastTouch(FTA_DOWN, mouseMoveStartPos);
        m_ctrlMouseMove.lastConverPos = mouseMoveStartPos;
        m_ctrlMouseMove.touching = true;
    }
}

void InputConvertGame::mouseMoveStopTouch()
{
    // 停止边缘回中延迟定时器，清除等待状态
    if (m_ctrlMouseMove.centerRepressTimer) {
        m_ctrlMouseMove.centerRepressTimer->stop();
    }
    m_ctrlMouseMove.waitingForCenterRepress = false;
    m_ctrlMouseMove.pendingOvershoot = {0, 0};

    // 停止空闲回中定时器
    if (m_ctrlMouseMove.idleCenterTimer) {
        m_ctrlMouseMove.idleCenterTimer->stop();
    }

    if (m_ctrlMouseMove.touching) {
        // 使用 FastMsg 协议发送 UP
        sendViewFastTouch(FTA_UP, m_ctrlMouseMove.lastConverPos);
        m_ctrlMouseMove.touching = false;
        m_ctrlMouseMove.fastTouchSeqId = 0;
    }
}

void InputConvertGame::startMouseMoveTimer()
{
    stopMouseMoveTimer();
    m_ctrlMouseMove.timer = startTimer(500);
}

void InputConvertGame::stopMouseMoveTimer()
{
    if (0 != m_ctrlMouseMove.timer) {
        killTimer(m_ctrlMouseMove.timer);
        m_ctrlMouseMove.timer = 0;
    }
}

// ---------------------------------------------------------
// 切换光标捕获/显示状态
// 替代了原来的 switchGameMap
// ---------------------------------------------------------
bool InputConvertGame::toggleCursorCaptured()
{
    setCursorCaptured(!m_cursorCaptured);
    return m_cursorCaptured;
}

void InputConvertGame::setCursorCaptured(bool captured)
{
    m_cursorCaptured = captured;

    if (m_cursorCaptured) {
        // [捕获模式]：隐藏鼠标，锁定中心，启用视角控制
        if (m_keyMap.isValidMouseMoveMap()) {
#ifdef QT_NO_DEBUG
            // 生产环境：真正隐藏光标
            QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
#else
            // 调试环境：十字光标方便看位置
            QGuiApplication::setOverrideCursor(QCursor(Qt::CrossCursor));
#endif
            // 发送信号让 MainWindow 锁定鼠标位置
            emit grabCursor(true);
        }
        // 准备进行视角控制的防抖
        m_ctrlMouseMove.ignoreCount = 1;
    } else {
        // [显示模式]：恢复鼠标，停止视角控制
        QGuiApplication::restoreOverrideCursor();
        emit grabCursor(false);

        // 停止之前的视角移动操作
        stopMouseMoveTimer();
        mouseMoveStopTouch();

        // 【修复】清理轮盘状态，防止卡死
        resetSteerWheelState();
    }
}

// 重置轮盘状态（用于状态恢复）
void InputConvertGame::resetSteerWheelState()
{
    // 停止定时器
    if (m_ctrlSteerWheel.delayData.timer && m_ctrlSteerWheel.delayData.timer->isActive()) {
        m_ctrlSteerWheel.delayData.timer->stop();
    }
    m_ctrlSteerWheel.delayData.queueTimer.clear();
    m_ctrlSteerWheel.delayData.queuePos.clear();

    // 如果有活跃的触摸，发送 UP 事件
    if (m_ctrlSteerWheel.fastTouchSeqId != 0) {
        sendSteerWheelFastTouch(FTA_UP, m_ctrlSteerWheel.delayData.currentPos);
        m_ctrlSteerWheel.fastTouchSeqId = 0;
    }

    // 重置按键状态
    m_ctrlSteerWheel.pressedUp = false;
    m_ctrlSteerWheel.pressedDown = false;
    m_ctrlSteerWheel.pressedLeft = false;
    m_ctrlSteerWheel.pressedRight = false;
    m_ctrlSteerWheel.delayData.pressedNum = 0;
    m_ctrlSteerWheel.touchKey = Qt::Key_unknown;
}

void InputConvertGame::timerEvent(QTimerEvent *event)
{
    if (m_ctrlMouseMove.timer == event->timerId()) {
        stopMouseMoveTimer();
        mouseMoveStopTouch();
    }
}

// ---------------------------------------------------------
// 以下是从 InputConvertNormal 移植过来的辅助转换函数
// ---------------------------------------------------------

AndroidMotioneventButtons InputConvertGame::convertMouseButtons(Qt::MouseButtons buttonState)
{
    quint32 buttons = 0;
    if (buttonState & Qt::LeftButton) {
        buttons |= AMOTION_EVENT_BUTTON_PRIMARY;
    }
    return static_cast<AndroidMotioneventButtons>(buttons);
}

AndroidMotioneventButtons InputConvertGame::convertMouseButton(Qt::MouseButton button)
{
    if (button == Qt::LeftButton) {
        return AMOTION_EVENT_BUTTON_PRIMARY;
    }
    return static_cast<AndroidMotioneventButtons>(0);
}

AndroidKeycode InputConvertGame::convertKeyCode(int key, Qt::KeyboardModifiers modifiers)
{
    AndroidKeycode keyCode = AKEYCODE_UNKNOWN;
    switch (key) {
    case Qt::Key_Return:        keyCode = AKEYCODE_ENTER; break;
    case Qt::Key_Enter:         keyCode = AKEYCODE_NUMPAD_ENTER; break;
    case Qt::Key_Escape:        keyCode = AKEYCODE_ESCAPE; break;
    case Qt::Key_Backspace:     keyCode = AKEYCODE_DEL; break;
    case Qt::Key_Delete:        keyCode = AKEYCODE_FORWARD_DEL; break;
    case Qt::Key_Tab:           keyCode = AKEYCODE_TAB; break;
    case Qt::Key_Home:          keyCode = AKEYCODE_MOVE_HOME; break;
    case Qt::Key_End:           keyCode = AKEYCODE_MOVE_END; break;
    case Qt::Key_PageUp:        keyCode = AKEYCODE_PAGE_UP; break;
    case Qt::Key_PageDown:      keyCode = AKEYCODE_PAGE_DOWN; break;
    case Qt::Key_Left:          keyCode = AKEYCODE_DPAD_LEFT; break;
    case Qt::Key_Right:         keyCode = AKEYCODE_DPAD_RIGHT; break;
    case Qt::Key_Up:            keyCode = AKEYCODE_DPAD_UP; break;
    case Qt::Key_Down:          keyCode = AKEYCODE_DPAD_DOWN; break;
    }

    if (AKEYCODE_UNKNOWN != keyCode) return keyCode;
    if (modifiers & (Qt::AltModifier | Qt::MetaModifier)) return keyCode;

    switch (key) {
    case Qt::Key_A: keyCode = AKEYCODE_A; break;
    case Qt::Key_B: keyCode = AKEYCODE_B; break;
    case Qt::Key_C: keyCode = AKEYCODE_C; break;
    case Qt::Key_D: keyCode = AKEYCODE_D; break;
    case Qt::Key_E: keyCode = AKEYCODE_E; break;
    case Qt::Key_F: keyCode = AKEYCODE_F; break;
    case Qt::Key_G: keyCode = AKEYCODE_G; break;
    case Qt::Key_H: keyCode = AKEYCODE_H; break;
    case Qt::Key_I: keyCode = AKEYCODE_I; break;
    case Qt::Key_J: keyCode = AKEYCODE_J; break;
    case Qt::Key_K: keyCode = AKEYCODE_K; break;
    case Qt::Key_L: keyCode = AKEYCODE_L; break;
    case Qt::Key_M: keyCode = AKEYCODE_M; break;
    case Qt::Key_N: keyCode = AKEYCODE_N; break;
    case Qt::Key_O: keyCode = AKEYCODE_O; break;
    case Qt::Key_P: keyCode = AKEYCODE_P; break;
    case Qt::Key_Q: keyCode = AKEYCODE_Q; break;
    case Qt::Key_R: keyCode = AKEYCODE_R; break;
    case Qt::Key_S: keyCode = AKEYCODE_S; break;
    case Qt::Key_T: keyCode = AKEYCODE_T; break;
    case Qt::Key_U: keyCode = AKEYCODE_U; break;
    case Qt::Key_V: keyCode = AKEYCODE_V; break;
    case Qt::Key_W: keyCode = AKEYCODE_W; break;
    case Qt::Key_X: keyCode = AKEYCODE_X; break;
    case Qt::Key_Y: keyCode = AKEYCODE_Y; break;
    case Qt::Key_Z: keyCode = AKEYCODE_Z; break;
    case Qt::Key_0: keyCode = AKEYCODE_0; break;
    case Qt::Key_1: case Qt::Key_Exclam: keyCode = AKEYCODE_1; break;
    case Qt::Key_2: keyCode = AKEYCODE_2; break;
    case Qt::Key_3: keyCode = AKEYCODE_3; break;
    case Qt::Key_4: case Qt::Key_Dollar: keyCode = AKEYCODE_4; break;
    case Qt::Key_5: case Qt::Key_Percent: keyCode = AKEYCODE_5; break;
    case Qt::Key_6: case Qt::Key_AsciiCircum: keyCode = AKEYCODE_6; break;
    case Qt::Key_7: case Qt::Key_Ampersand: keyCode = AKEYCODE_7; break;
    case Qt::Key_8: keyCode = AKEYCODE_8; break;
    case Qt::Key_9: keyCode = AKEYCODE_9; break;
    case Qt::Key_Space: keyCode = AKEYCODE_SPACE; break;
    case Qt::Key_Comma: case Qt::Key_Less: keyCode = AKEYCODE_COMMA; break;
    case Qt::Key_Period: case Qt::Key_Greater: keyCode = AKEYCODE_PERIOD; break;
    case Qt::Key_Minus: case Qt::Key_Underscore: keyCode = AKEYCODE_MINUS; break;
    case Qt::Key_Equal: keyCode = AKEYCODE_EQUALS; break;
    case Qt::Key_BracketLeft: case Qt::Key_BraceLeft: keyCode = AKEYCODE_LEFT_BRACKET; break;
    case Qt::Key_BracketRight: case Qt::Key_BraceRight: keyCode = AKEYCODE_RIGHT_BRACKET; break;
    case Qt::Key_Backslash: case Qt::Key_Bar: keyCode = AKEYCODE_BACKSLASH; break;
    case Qt::Key_Semicolon: case Qt::Key_Colon: keyCode = AKEYCODE_SEMICOLON; break;
    case Qt::Key_Apostrophe: case Qt::Key_QuoteDbl: keyCode = AKEYCODE_APOSTROPHE; break;
    case Qt::Key_Slash: case Qt::Key_Question: keyCode = AKEYCODE_SLASH; break;
    case Qt::Key_At: keyCode = AKEYCODE_AT; break;
    case Qt::Key_Plus: keyCode = AKEYCODE_PLUS; break;
    case Qt::Key_QuoteLeft: case Qt::Key_AsciiTilde: keyCode = AKEYCODE_GRAVE; break;
    case Qt::Key_NumberSign: keyCode = AKEYCODE_POUND; break;
    case Qt::Key_ParenLeft: keyCode = AKEYCODE_NUMPAD_LEFT_PAREN; break;
    case Qt::Key_ParenRight: keyCode = AKEYCODE_NUMPAD_RIGHT_PAREN; break;
    case Qt::Key_Asterisk: keyCode = AKEYCODE_STAR; break;
    }
    return keyCode;
}

AndroidMetastate InputConvertGame::convertMetastate(Qt::KeyboardModifiers modifiers)
{
    int metastate = AMETA_NONE;
    if (modifiers & Qt::ShiftModifier) metastate |= AMETA_SHIFT_ON;
    if (modifiers & Qt::ControlModifier) metastate |= AMETA_CTRL_ON;
    if (modifiers & Qt::AltModifier) metastate |= AMETA_ALT_ON;
    if (modifiers & Qt::MetaModifier) metastate |= AMETA_META_ON;
    return static_cast<AndroidMetastate>(metastate);
}
