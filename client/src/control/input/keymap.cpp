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

        QPair<ActionType, int> sk = getItemKey(rootObj, "switchKey");

        if (sk.first != AT_INVALID) {

            m_switchKey.type = sk.first;

            m_switchKey.key = sk.second;

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

                QPair<ActionType, int> leftKey = getItemKey(node, "leftKey");

                QPair<ActionType, int> rightKey = getItemKey(node, "rightKey");

                QPair<ActionType, int> upKey = getItemKey(node, "upKey");

                QPair<ActionType, int> downKey = getItemKey(node, "downKey");



                if (leftKey.first == AT_INVALID || rightKey.first == AT_INVALID || upKey.first == AT_INVALID || downKey.first == AT_INVALID) break;



                KeyMapNode keyMapNode;

                keyMapNode.type = type;

                keyMapNode.data.steerWheel.left = { leftKey.first, leftKey.second, QPointF(0, 0), QPointF(0, 0), getItemDouble(node, "leftOffset") };

                keyMapNode.data.steerWheel.right = { rightKey.first, rightKey.second, QPointF(0, 0), QPointF(0, 0), getItemDouble(node, "rightOffset") };

                keyMapNode.data.steerWheel.up = { upKey.first, upKey.second, QPointF(0, 0), QPointF(0, 0), getItemDouble(node, "upOffset") };

                keyMapNode.data.steerWheel.down = { downKey.first, downKey.second, QPointF(0, 0), QPointF(0, 0), getItemDouble(node, "downOffset") };

                keyMapNode.data.steerWheel.centerPos = getItemPos(node, "centerPos");

                m_idxSteerWheel = m_keyMapNodes.size();

                m_keyMapNodes.push_back(keyMapNode);

            } break;



            case KeyMap::KMT_ANDROID_KEY: {

                if (!checkForAndroidKey(node)) break;

                QPair<ActionType, int> key = getItemKey(node, "key");

                if (key.first == AT_INVALID) break;



                KeyMapNode keyMapNode;

                keyMapNode.type = type;

                keyMapNode.data.androidKey.keyNode.type = key.first;

                keyMapNode.data.androidKey.keyNode.key = key.second;

                keyMapNode.data.androidKey.keyNode.androidKey = static_cast<AndroidKeycode>(getItemDouble(node, "androidKey"));

                m_keyMapNodes.push_back(keyMapNode);

            } break;



            case KeyMap::KMT_SCRIPT: {

                if (!checkForScript(node)) break;

                QPair<ActionType, int> key = getItemKey(node, "key");

                if (key.first == AT_INVALID) break;



                KeyMapNode keyMapNode;

                keyMapNode.type = type;

                keyMapNode.data.script.keyNode.type = key.first;

                keyMapNode.data.script.keyNode.key = key.second;

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

                QPair<ActionType, int> key = getItemKey(node, "key");

                if (key.first != AT_INVALID) {

                    m_switchKey.type = key.first;

                    m_switchKey.key = key.second;

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



const KeyMap::KeyMapNode &KeyMap::getKeyMapNodeKey(int key)

{

    return *m_rmapKey.value(key, &m_invalidNode);

}



const KeyMap::KeyMapNode &KeyMap::getKeyMapNodeMouse(int key)

{

    return *m_rmapMouse.value(key, &m_invalidNode);

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



bool KeyMap::updateSteerWheelOffset(double up, double down, double left, double right)

{

    if (m_idxSteerWheel < 0 || m_idxSteerWheel >= m_keyMapNodes.size()) {

        return false;

    }

    KeyMapNode &node = m_keyMapNodes[m_idxSteerWheel];

    node.data.steerWheel.up.extendOffset = up;

    node.data.steerWheel.down.extendOffset = down;

    node.data.steerWheel.left.extendOffset = left;

    node.data.steerWheel.right.extendOffset = right;

    return true;

}



void KeyMap::makeReverseMap()

{

    m_rmapKey.clear();

    m_rmapMouse.clear();

    for (int i = 0; i < m_keyMapNodes.size(); ++i) {

        auto &node = m_keyMapNodes[i];

        switch (node.type) {



        case KMT_STEER_WHEEL: {

            QMultiHash<int, KeyMapNode *> &ml = node.data.steerWheel.left.type == AT_KEY ? m_rmapKey : m_rmapMouse;

            ml.insert(node.data.steerWheel.left.key, &node);

            QMultiHash<int, KeyMapNode *> &mr = node.data.steerWheel.right.type == AT_KEY ? m_rmapKey : m_rmapMouse;

            mr.insert(node.data.steerWheel.right.key, &node);

            QMultiHash<int, KeyMapNode *> &mu = node.data.steerWheel.up.type == AT_KEY ? m_rmapKey : m_rmapMouse;

            mu.insert(node.data.steerWheel.up.key, &node);

            QMultiHash<int, KeyMapNode *> &md = node.data.steerWheel.down.type == AT_KEY ? m_rmapKey : m_rmapMouse;

            md.insert(node.data.steerWheel.down.key, &node);

        } break;



        case KMT_ANDROID_KEY: {

            QMultiHash<int, KeyMapNode *> &m = node.data.androidKey.keyNode.type == AT_KEY ? m_rmapKey : m_rmapMouse;

            m.insert(node.data.androidKey.keyNode.key, &node);

        } break;



        case KMT_SCRIPT: {

            QMultiHash<int, KeyMapNode *> &m = node.data.script.keyNode.type == AT_KEY ? m_rmapKey : m_rmapMouse;

            m.insert(node.data.script.keyNode.key, &node);

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



QPair<KeyMap::ActionType, int> KeyMap::getItemKey(const QJsonObject &node, const QString &name)

{

    QString value = getItemString(node, name);

    int key = m_metaEnumKey.keyToValue(value.toStdString().c_str());

    int btn = m_metaEnumMouseButtons.keyToValue(value.toStdString().c_str());

    if (key == -1 && btn == -1) {

        return { AT_INVALID, -1 };

    } else if (key != -1) {

        return { AT_KEY, key };

    } else {

        return { AT_MOUSE, btn };

    }

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
