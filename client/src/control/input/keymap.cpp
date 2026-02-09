#include <QCoreApplication>

#include <QDebug>

#include <QFile>

#include <QFileInfo>

#include <QJsonArray>

#include <QJsonDocument>

#include <QMetaEnum>



#include "keymap.h"



KeyMap::KeyMap(QObject *parent) : QObject(parent) {}



KeyMap::~KeyMap() {}



void KeyMap::loadKeyMap(const QString &json)

{

    QString errorString;

    QJsonParseError jsonError;

    QJsonDocument jsonDoc;

    QJsonObject rootObj;



    // 初始化

    m_idxSteerWheel = -1;

    m_idxMouseMove = -1;

    m_keyMapNodes.clear();



    // 默认开关键

    m_switchKey = { AT_KEY, Qt::Key_QuoteLeft };



    jsonDoc = QJsonDocument::fromJson(json.toUtf8(), &jsonError);



    if (jsonError.error != QJsonParseError::NoError) {

        errorString = QString("json error: %1").arg(jsonError.errorString());

        goto parseError;

    }



    rootObj = jsonDoc.object();



    // 尝试读取根节点的 switchKey

    if (checkItemString(rootObj, "switchKey")) {

        ParsedKey sk = getItemKey(rootObj, "switchKey");

        if (sk.type != AT_INVALID) {

            m_switchKey.type = sk.type;

            m_switchKey.key = sk.key;

        }

    }



    // 兼容旧的 mouseMoveMap 配置

    if (checkItemObject(rootObj, "mouseMoveMap")) {

        QJsonObject mouseMoveMap = getItemObject(rootObj, "mouseMoveMap");

        KeyMapNode keyMapNode;

        keyMapNode.type = KMT_MOUSE_MOVE;



        bool have_speedRatio = false;



        if (checkItemDouble(mouseMoveMap, "speedRatio")) {

            float ratio = static_cast<float>(getItemDouble(mouseMoveMap, "speedRatio"));

            keyMapNode.data.mouseMove.speedRatio.setX(ratio);

            keyMapNode.data.mouseMove.speedRatio.setY(ratio / 2.25f);

            have_speedRatio = true;

        }



        if (checkItemDouble(mouseMoveMap, "speedRatioX")) {

            keyMapNode.data.mouseMove.speedRatio.setX(static_cast<float>(getItemDouble(mouseMoveMap, "speedRatioX")));

            have_speedRatio = true;

        }



        if (checkItemDouble(mouseMoveMap, "speedRatioY")) {

            keyMapNode.data.mouseMove.speedRatio.setY(static_cast<float>(getItemDouble(mouseMoveMap, "speedRatioY")));

            have_speedRatio = true;

        }



        if (have_speedRatio && checkItemObject(mouseMoveMap, "startPos")) {

            keyMapNode.data.mouseMove.startPos = getItemPos(mouseMoveMap, "startPos");

            m_idxMouseMove = m_keyMapNodes.size();

            m_keyMapNodes.push_back(keyMapNode);

        }

    }



    // keyMapNodes 解析

    if (rootObj.contains("keyMapNodes") && rootObj.value("keyMapNodes").isArray()) {

        QJsonArray keyMapNodes = rootObj.value("keyMapNodes").toArray();

        QJsonObject node;

        int size = keyMapNodes.size();

        for (int i = 0; i < size; i++) {

            if (!keyMapNodes.at(i).isObject()) continue;

            node = keyMapNodes.at(i).toObject();

            if (!node.contains("type") || !node.value("type").isString()) continue;



            KeyMap::KeyMapType type = getItemKeyMapType(node, "type");

            switch (type) {



            case KeyMap::KMT_STEER_WHEEL: {

                if (!checkForSteerWhell(node)) {

                    qWarning() << "json error: format error (steerWheel)";

                    break;

                }

                ParsedKey leftKey = getItemKey(node, "leftKey");

                ParsedKey rightKey = getItemKey(node, "rightKey");

                ParsedKey upKey = getItemKey(node, "upKey");

                ParsedKey downKey = getItemKey(node, "downKey");



                if (leftKey.type == AT_INVALID || rightKey.type == AT_INVALID || upKey.type == AT_INVALID || downKey.type == AT_INVALID) break;



                KeyMapNode keyMapNode;

                keyMapNode.type = type;

                keyMapNode.data.steerWheel.left = { leftKey.type, leftKey.key, leftKey.modifiers, QPointF(0, 0), QPointF(0, 0), getItemDouble(node, "leftOffset") };

                keyMapNode.data.steerWheel.right = { rightKey.type, rightKey.key, rightKey.modifiers, QPointF(0, 0), QPointF(0, 0), getItemDouble(node, "rightOffset") };

                keyMapNode.data.steerWheel.up = { upKey.type, upKey.key, upKey.modifiers, QPointF(0, 0), QPointF(0, 0), getItemDouble(node, "upOffset") };

                keyMapNode.data.steerWheel.down = { downKey.type, downKey.key, downKey.modifiers, QPointF(0, 0), QPointF(0, 0), getItemDouble(node, "downOffset") };

                keyMapNode.data.steerWheel.centerPos = getItemPos(node, "centerPos");

                m_idxSteerWheel = m_keyMapNodes.size();

                m_keyMapNodes.push_back(keyMapNode);

            } break;



            case KeyMap::KMT_ANDROID_KEY: {

                if (!checkForAndroidKey(node)) break;

                ParsedKey key = getItemKey(node, "key");

                if (key.type == AT_INVALID) break;



                KeyMapNode keyMapNode;

                keyMapNode.type = type;

                keyMapNode.data.androidKey.keyNode.type = key.type;

                keyMapNode.data.androidKey.keyNode.key = key.key;

                keyMapNode.data.androidKey.keyNode.modifiers = key.modifiers;

                keyMapNode.data.androidKey.keyNode.androidKey = static_cast<AndroidKeycode>(getItemDouble(node, "androidKey"));

                m_keyMapNodes.push_back(keyMapNode);

            } break;



            case KeyMap::KMT_SCRIPT: {

                if (!checkForScript(node)) break;

                ParsedKey key = getItemKey(node, "key");

                if (key.type == AT_INVALID) break;



                KeyMapNode keyMapNode;

                keyMapNode.type = type;

                keyMapNode.data.script.keyNode.type = key.type;

                keyMapNode.data.script.keyNode.key = key.key;

                keyMapNode.data.script.keyNode.modifiers = key.modifiers;

                keyMapNode.data.script.keyNode.pos = getItemPos(node, "pos");

                keyMapNode.script = getItemString(node, "script");

                m_keyMapNodes.push_back(keyMapNode);

            } break;



                // 【新增】处理 KMT_CAMERA_MOVE

            case KeyMap::KMT_CAMERA_MOVE: {

                if (!checkForCamera(node)) {

                    qWarning() << "json error: format error (camera)";

                    break;

                }



                // 1. 设置开关按键 (这是核心，将 Camera 组件的按键设为 战斗模式开关)

                ParsedKey key = getItemKey(node, "key");

                if (key.type != AT_INVALID) {

                    m_switchKey.type = key.type;

                    m_switchKey.key = key.key;

                }



                // 2. 转换为内部的 MOUSE_MOVE 节点，用于控制视角

                KeyMapNode keyMapNode;

                keyMapNode.type = KMT_MOUSE_MOVE;



                keyMapNode.data.mouseMove.startPos = getItemPos(node, "pos");

                keyMapNode.data.mouseMove.speedRatio.setX(getItemDouble(node, "speedRatioX"));

                keyMapNode.data.mouseMove.speedRatio.setY(getItemDouble(node, "speedRatioY"));



                // 记录索引，让 isValidMouseMoveMap() 返回 true

                m_idxMouseMove = m_keyMapNodes.size();

                m_keyMapNodes.push_back(keyMapNode);

            } break;



            // 【新增】小眼睛自由视角

            case KeyMap::KMT_FREE_LOOK: {

                if (!checkForFreeLook(node)) {

                    qWarning() << "json error: format error (freeLook)";

                    break;

                }

                ParsedKey key = getItemKey(node, "key");

                if (key.type == AT_INVALID) break;



                KeyMapNode keyMapNode;

                keyMapNode.type = type;

                keyMapNode.data.freeLook.keyNode.type = key.type;

                keyMapNode.data.freeLook.keyNode.key = key.key;

                keyMapNode.data.freeLook.keyNode.modifiers = key.modifiers;

                keyMapNode.data.freeLook.startPos = getItemPos(node, "startPos");

                keyMapNode.data.freeLook.speedRatio.setX(getItemDouble(node, "speedRatioX"));

                keyMapNode.data.freeLook.speedRatio.setY(getItemDouble(node, "speedRatioY"));

                keyMapNode.data.freeLook.resetViewOnRelease = getItemBool(node, "resetViewOnRelease");

                m_keyMapNodes.push_back(keyMapNode);

            } break;



            default:

                break;

            }

        }

    }



    makeReverseMap();



parseError:

    if (!errorString.isEmpty()) {

        qWarning() << errorString;

    }

    return;

}



const KeyMap::KeyMapNode &KeyMap::getKeyMapNode(int key)

{

    auto p = m_rmapKey.value(key, &m_invalidNode);

    if (p == &m_invalidNode) {

        return *m_rmapMouse.value(key, &m_invalidNode);

    }

    return *p;

}



const KeyMap::KeyMapNode &KeyMap::getKeyMapNodeKey(int key, Qt::KeyboardModifiers modifiers)

{

    // 首先尝试精确匹配（带修饰键）

    qint64 hash = makeKeyHash(key, modifiers);

    KeyMapNode *p = m_rmapKey.value(hash, nullptr);

    if (p) {

        return *p;

    }



    // 如果没有精确匹配，尝试仅匹配按键（无修饰键）

    // 这样可以兼容旧的配置

    hash = makeKeyHash(key, Qt::NoModifier);

    p = m_rmapKey.value(hash, nullptr);

    if (p) {

        return *p;

    }



    return m_invalidNode;

}



const KeyMap::KeyMapNode &KeyMap::getKeyMapNodeMouse(int key)

{

    return *m_rmapMouse.value(key, &m_invalidNode);

}

// 从显示名称转换为 Qt 键码和修饰键
// 返回 QPair<key, modifiers>
static QPair<int, Qt::KeyboardModifiers> displayNameToKeyWithModifiers(const QString& displayName)
{
    QString name = displayName.trimmed();
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;

    // 处理组合键 (如 "Ctrl+J", "Shift+G", "Ctrl+Shift+A")
    QStringList parts = name.split('+', Qt::SkipEmptyParts);
    QString keyPart = name;

    if (parts.size() > 1) {
        keyPart = parts.last().trimmed();  // 最后一部分是实际按键
        for (int i = 0; i < parts.size() - 1; ++i) {
            QString mod = parts[i].trimmed().toLower();
            if (mod == "ctrl" || mod == "control") {
                modifiers |= Qt::ControlModifier;
            } else if (mod == "shift") {
                modifiers |= Qt::ShiftModifier;
            } else if (mod == "alt") {
                modifiers |= Qt::AltModifier;
            } else if (mod == "meta" || mod == "win") {
                modifiers |= Qt::MetaModifier;
            }
        }
    }

    // 鼠标按键
    if (keyPart == "LMB" || keyPart.compare("LeftButton", Qt::CaseInsensitive) == 0) return {Qt::LeftButton, modifiers};
    if (keyPart == "RMB" || keyPart.compare("RightButton", Qt::CaseInsensitive) == 0) return {Qt::RightButton, modifiers};
    if (keyPart == "MMB" || keyPart.compare("MiddleButton", Qt::CaseInsensitive) == 0) return {Qt::MiddleButton, modifiers};

    // 滚轮
    if (keyPart.compare("WheelUp", Qt::CaseInsensitive) == 0 || keyPart == "滚上") return {WHEEL_UP, modifiers};
    if (keyPart.compare("WheelDown", Qt::CaseInsensitive) == 0 || keyPart == "滚下") return {WHEEL_DOWN, modifiers};

    // 符号键
    if (keyPart == "=" || keyPart == "Equal") return {Qt::Key_Equal, modifiers};
    if (keyPart == "+" || keyPart == "Plus") return {Qt::Key_Plus, modifiers};
    if (keyPart == "-" || keyPart == "Minus") return {Qt::Key_Minus, modifiers};
    if (keyPart == "*" || keyPart == "Asterisk") return {Qt::Key_Asterisk, modifiers};
    if (keyPart == "/" || keyPart == "Slash") return {Qt::Key_Slash, modifiers};
    if (keyPart == "`" || keyPart == "QuoteLeft") return {Qt::Key_QuoteLeft, modifiers};
    if (keyPart == "~" || keyPart == "AsciiTilde") return {Qt::Key_AsciiTilde, modifiers};
    if (keyPart == "\\" || keyPart == "Backslash") return {Qt::Key_Backslash, modifiers};
    if (keyPart == "[" || keyPart == "BracketLeft") return {Qt::Key_BracketLeft, modifiers};
    if (keyPart == "]" || keyPart == "BracketRight") return {Qt::Key_BracketRight, modifiers};
    if (keyPart == ";" || keyPart == "Semicolon") return {Qt::Key_Semicolon, modifiers};
    if (keyPart == "'" || keyPart == "Apostrophe") return {Qt::Key_Apostrophe, modifiers};
    if (keyPart == "," || keyPart == "Comma") return {Qt::Key_Comma, modifiers};
    if (keyPart == "." || keyPart == "Period") return {Qt::Key_Period, modifiers};

    // 特殊键
    if (keyPart.compare("Space", Qt::CaseInsensitive) == 0) return {Qt::Key_Space, modifiers};
    if (keyPart.compare("Tab", Qt::CaseInsensitive) == 0) return {Qt::Key_Tab, modifiers};
    if (keyPart.compare("Enter", Qt::CaseInsensitive) == 0 || keyPart.compare("Return", Qt::CaseInsensitive) == 0) return {Qt::Key_Return, modifiers};
    if (keyPart.compare("Esc", Qt::CaseInsensitive) == 0 || keyPart.compare("Escape", Qt::CaseInsensitive) == 0) return {Qt::Key_Escape, modifiers};
    if (keyPart.compare("Shift", Qt::CaseInsensitive) == 0) return {Qt::Key_Shift, modifiers};
    if (keyPart.compare("Ctrl", Qt::CaseInsensitive) == 0 || keyPart.compare("Control", Qt::CaseInsensitive) == 0) return {Qt::Key_Control, modifiers};
    if (keyPart.compare("Alt", Qt::CaseInsensitive) == 0) return {Qt::Key_Alt, modifiers};
    if (keyPart.compare("Backspace", Qt::CaseInsensitive) == 0) return {Qt::Key_Backspace, modifiers};
    if (keyPart.compare("Up", Qt::CaseInsensitive) == 0 || keyPart == "↑") return {Qt::Key_Up, modifiers};
    if (keyPart.compare("Down", Qt::CaseInsensitive) == 0 || keyPart == "↓") return {Qt::Key_Down, modifiers};
    if (keyPart.compare("Left", Qt::CaseInsensitive) == 0 || keyPart == "←") return {Qt::Key_Left, modifiers};
    if (keyPart.compare("Right", Qt::CaseInsensitive) == 0 || keyPart == "→") return {Qt::Key_Right, modifiers};

    // F1-F12
    if (keyPart.length() >= 2 && keyPart.length() <= 3 && keyPart[0].toUpper() == 'F') {
        bool ok;
        int num = keyPart.mid(1).toInt(&ok);
        if (ok && num >= 1 && num <= 12) {
            return {Qt::Key_F1 + num - 1, modifiers};
        }
    }

    // 单字符（字母、数字）
    if (keyPart.length() == 1) {
        QChar c = keyPart[0].toUpper();
        if (c >= 'A' && c <= 'Z') return {Qt::Key_A + (c.toLatin1() - 'A'), modifiers};
        if (c >= '0' && c <= '9') return {Qt::Key_0 + (c.toLatin1() - '0'), modifiers};
    }

    return {0, Qt::NoModifier};  // 未找到
}

const KeyMap::KeyMapNode &KeyMap::getKeyMapNodeByDisplayName(const QString& displayName)
{
    auto [key, modifiers] = displayNameToKeyWithModifiers(displayName);
    if (key == 0) return m_invalidNode;

    // 判断是键盘键还是鼠标按钮/滚轮
    if (key == Qt::LeftButton || key == Qt::RightButton || key == Qt::MiddleButton ||
        key == WHEEL_UP || key == WHEEL_DOWN) {
        return getKeyMapNodeMouse(key);
    } else {
        return getKeyMapNodeKey(key, modifiers);
    }
}

bool KeyMap::isSwitchOnKeyboard()

{

    return m_switchKey.type == AT_KEY;

}



int KeyMap::getSwitchKey()

{

    return m_switchKey.key;

}



bool KeyMap::isValidMouseMoveMap()

{

    return m_idxMouseMove != -1;

}



bool KeyMap::isValidSteerWheelMap()

{

    return m_idxSteerWheel != -1;

}



const KeyMap::KeyMapNode &KeyMap::getMouseMoveMap()

{

    if (m_idxMouseMove >= 0 && m_idxMouseMove < m_keyMapNodes.size()) {

        return m_keyMapNodes[m_idxMouseMove];

    }

    return m_invalidNode;

}



void KeyMap::setSteerWheelCoefficient(double up, double down, double left, double right)
{
    // 设置临时系数，默认 1.0（不变）
    m_steerWheelCoeff[0] = up;
    m_steerWheelCoeff[1] = down;
    m_steerWheelCoeff[2] = left;
    m_steerWheelCoeff[3] = right;

    // 标记系数已变化，需要实时更新
    m_coefficientChanged = true;
}

void KeyMap::resetSteerWheelCoefficient()
{
    m_steerWheelCoeff[0] = 1.0;
    m_steerWheelCoeff[1] = 1.0;
    m_steerWheelCoeff[2] = 1.0;
    m_steerWheelCoeff[3] = 1.0;
    m_coefficientChanged = true;
}

double KeyMap::getSteerWheelCoefficient(int direction) const
{
    if (direction < 0 || direction > 3) return 1.0;
    return m_steerWheelCoeff[direction];
}

const KeyMap::KeyMapNode* KeyMap::getSteerWheelNode() const
{
    if (m_idxSteerWheel >= 0 && m_idxSteerWheel < m_keyMapNodes.size()) {
        return &m_keyMapNodes[m_idxSteerWheel];
    }
    return nullptr;
}

double KeyMap::getSteerWheelOffset(int direction) const
{
    if (m_idxSteerWheel < 0 || m_idxSteerWheel >= m_keyMapNodes.size()) {
        return 0.0;
    }

    const KeyMapNode &node = m_keyMapNodes[m_idxSteerWheel];
    double baseOffset = 0.0;

    switch (direction) {
        case 0: baseOffset = node.data.steerWheel.up.extendOffset; break;
        case 1: baseOffset = node.data.steerWheel.down.extendOffset; break;
        case 2: baseOffset = node.data.steerWheel.left.extendOffset; break;
        case 3: baseOffset = node.data.steerWheel.right.extendOffset; break;
        default: return 0.0;
    }

    // 应用系数
    return baseOffset * m_steerWheelCoeff[direction];
}



void KeyMap::makeReverseMap()

{

    m_rmapKey.clear();

    m_rmapMouse.clear();

    for (int i = 0; i < m_keyMapNodes.size(); ++i) {

        auto &node = m_keyMapNodes[i];

        switch (node.type) {



        case KMT_STEER_WHEEL: {

            // 方向盘的四个方向键

            if (node.data.steerWheel.left.type == AT_KEY) {

                m_rmapKey.insert(makeKeyHash(node.data.steerWheel.left.key, node.data.steerWheel.left.modifiers), &node);

            } else {

                m_rmapMouse.insert(node.data.steerWheel.left.key, &node);

            }

            if (node.data.steerWheel.right.type == AT_KEY) {

                m_rmapKey.insert(makeKeyHash(node.data.steerWheel.right.key, node.data.steerWheel.right.modifiers), &node);

            } else {

                m_rmapMouse.insert(node.data.steerWheel.right.key, &node);

            }

            if (node.data.steerWheel.up.type == AT_KEY) {

                m_rmapKey.insert(makeKeyHash(node.data.steerWheel.up.key, node.data.steerWheel.up.modifiers), &node);

            } else {

                m_rmapMouse.insert(node.data.steerWheel.up.key, &node);

            }

            if (node.data.steerWheel.down.type == AT_KEY) {

                m_rmapKey.insert(makeKeyHash(node.data.steerWheel.down.key, node.data.steerWheel.down.modifiers), &node);

            } else {

                m_rmapMouse.insert(node.data.steerWheel.down.key, &node);

            }

        } break;



        case KMT_ANDROID_KEY: {

            if (node.data.androidKey.keyNode.type == AT_KEY) {

                m_rmapKey.insert(makeKeyHash(node.data.androidKey.keyNode.key, node.data.androidKey.keyNode.modifiers), &node);

            } else {

                m_rmapMouse.insert(node.data.androidKey.keyNode.key, &node);

            }

        } break;



        case KMT_SCRIPT: {

            if (node.data.script.keyNode.type == AT_KEY) {

                m_rmapKey.insert(makeKeyHash(node.data.script.keyNode.key, node.data.script.keyNode.modifiers), &node);

            } else {

                m_rmapMouse.insert(node.data.script.keyNode.key, &node);

            }

        } break;



        case KMT_FREE_LOOK: {

            if (node.data.freeLook.keyNode.type == AT_KEY) {

                m_rmapKey.insert(makeKeyHash(node.data.freeLook.keyNode.key, node.data.freeLook.keyNode.modifiers), &node);

            } else {

                m_rmapMouse.insert(node.data.freeLook.keyNode.key, &node);

            }

        } break;



        default:

            break;

        }

    }

}



QString KeyMap::getItemString(const QJsonObject &node, const QString &name)

{

    return node.value(name).toString();

}



double KeyMap::getItemDouble(const QJsonObject &node, const QString &name)

{

    return node.value(name).toDouble();

}



bool KeyMap::getItemBool(const QJsonObject &node, const QString &name)

{

    return node.value(name).toBool(false);

}



QJsonObject KeyMap::getItemObject(const QJsonObject &node, const QString &name)

{

    return node.value(name).toObject();

}



QPointF KeyMap::getItemPos(const QJsonObject &node, const QString &name)

{

    QJsonObject pos = node.value(name).toObject();

    return QPointF(pos.value("x").toDouble(), pos.value("y").toDouble());

}



KeyMap::ParsedKey KeyMap::getItemKey(const QJsonObject &node, const QString &name)

{

    QString value = getItemString(node, name);

    ParsedKey result;

    result.type = AT_INVALID;

    result.key = Qt::Key_unknown;

    result.modifiers = Qt::NoModifier;



    if (value.isEmpty()) {

        return result;

    }



    // 先检查是否是鼠标按钮

    int btn = m_metaEnumMouseButtons.keyToValue(value.toStdString().c_str());

    if (btn != -1) {

        result.type = AT_MOUSE;

        result.key = btn;

        return result;

    }

    // 检查是否是滚轮
    if (value.compare("WheelUp", Qt::CaseInsensitive) == 0) {
        result.type = AT_MOUSE;
        result.key = WHEEL_UP;
        return result;
    }
    if (value.compare("WheelDown", Qt::CaseInsensitive) == 0) {
        result.type = AT_MOUSE;
        result.key = WHEEL_DOWN;
        return result;
    }

    // 解析组合键（如 "Shift+G", "Ctrl+Alt+X"）

    Qt::KeyboardModifiers modifiers = Qt::NoModifier;

    QString keyPart = value;



    // 检查是否包含修饰键

    if (value.contains('+')) {

        QStringList parts = value.split('+');

        keyPart = parts.last();  // 最后一部分是主键



        for (int i = 0; i < parts.size() - 1; ++i) {

            QString mod = parts[i].trimmed();

            if (mod == "Shift") {

                modifiers |= Qt::ShiftModifier;

            } else if (mod == "Ctrl" || mod == "Control") {

                modifiers |= Qt::ControlModifier;

            } else if (mod == "Alt") {

                modifiers |= Qt::AltModifier;

            } else if (mod == "Meta" || mod == "Win") {

                modifiers |= Qt::MetaModifier;

            }

        }

    }



    // 尝试解析键值

    int key = -1;



    // 首先尝试带 "Key_" 前缀

    QString keyWithPrefix = keyPart;

    if (!keyPart.startsWith("Key_")) {

        keyWithPrefix = "Key_" + keyPart;

    }

    key = m_metaEnumKey.keyToValue(keyWithPrefix.toStdString().c_str());



    // 如果失败，尝试不带前缀的原始值

    if (key == -1) {

        key = m_metaEnumKey.keyToValue(keyPart.toStdString().c_str());

    }



    // 如果还是失败，尝试使用 QKeySequence 解析单字符

    if (key == -1 && keyPart.length() == 1) {

        QChar c = keyPart[0].toUpper();

        if (c >= 'A' && c <= 'Z') {

            key = Qt::Key_A + (c.unicode() - 'A');

        } else if (c >= '0' && c <= '9') {

            key = Qt::Key_0 + (c.unicode() - '0');

        }

    }



    if (key != -1) {

        result.type = AT_KEY;

        result.key = key;

        result.modifiers = modifiers;

    }



    return result;

}



KeyMap::KeyMapType KeyMap::getItemKeyMapType(const QJsonObject &node, const QString &name)

{

    QString value = getItemString(node, name);

    return static_cast<KeyMap::KeyMapType>(m_metaEnumKeyMapType.keyToValue(value.toStdString().c_str()));

}



bool KeyMap::checkItemString(const QJsonObject &node, const QString &name)

{

    return node.contains(name) && node.value(name).isString();

}



bool KeyMap::checkItemDouble(const QJsonObject &node, const QString &name)

{

    return node.contains(name) && node.value(name).isDouble();

}



bool KeyMap::checkItemBool(const QJsonObject &node, const QString &name)

{

    return node.contains(name) && node.value(name).isBool();

}



bool KeyMap::checkItemObject(const QJsonObject &node, const QString &name)

{

    return node.contains(name) && node.value(name).isObject();

}



bool KeyMap::checkItemPos(const QJsonObject &node, const QString &name)

{

    if (node.contains(name) && node.value(name).isObject()) {

        QJsonObject pos = node.value(name).toObject();

        return pos.contains("x") && pos.value("x").isDouble() && pos.contains("y") && pos.value("y").isDouble();

    }

    return false;

}



bool KeyMap::checkForAndroidKey(const QJsonObject &node)

{

    return checkItemString(node, "key") && checkItemDouble(node, "androidKey");

}



bool KeyMap::checkForSteerWhell(const QJsonObject &node)

{

    return checkItemString(node, "leftKey") && checkItemString(node, "rightKey") && checkItemString(node, "upKey") && checkItemString(node, "downKey")

    && checkItemDouble(node, "leftOffset") && checkItemDouble(node, "rightOffset") && checkItemDouble(node, "upOffset")

        && checkItemDouble(node, "downOffset") && checkItemPos(node, "centerPos");

}



bool KeyMap::checkForScript(const QJsonObject &node)

{

    return checkItemString(node, "key") && checkItemPos(node, "pos") && checkItemString(node, "script");

}



// 【新增】视角控制检查

bool KeyMap::checkForCamera(const QJsonObject &node)

{

    return checkItemString(node, "key") && checkItemPos(node, "pos")

    && checkItemDouble(node, "speedRatioX") && checkItemDouble(node, "speedRatioY");

}

// 【新增】小眼睛自由视角检查

bool KeyMap::checkForFreeLook(const QJsonObject &node)

{

    return checkItemString(node, "key") && checkItemPos(node, "startPos")

    && checkItemDouble(node, "speedRatioX") && checkItemDouble(node, "speedRatioY");

}
