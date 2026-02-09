#ifndef KEYMAP_H

#define KEYMAP_H

#include <QJsonObject>

#include <QMetaEnum>

#include <QMultiHash>

#include <QObject>

#include <QPair>

#include <QPointF>

#include <QRectF>

#include <QVector>



#include "keycodes.h"

// 自定义滚轮事件值 / Custom wheel event values (no conflict with Qt key values)
constexpr int WHEEL_UP = 0x10000001;
constexpr int WHEEL_DOWN = 0x10000002;

/**
 * @brief 按键映射管理器 / Key Mapping Manager
 *
 * 解析脚本中的按键绑定配置，将键盘/鼠标事件映射为 Android 触摸/按键操作。
 * Parses key binding configs from scripts, maps keyboard/mouse events to Android touch/key actions.
 */
class KeyMap : public QObject

{

    Q_OBJECT

public:

    enum KeyMapType

    {

        KMT_INVALID = -1,

        // 移除了 KMT_CLICK, KMT_CLICK_TWICE, KMT_CLICK_MULTI, KMT_DRAG

        KMT_STEER_WHEEL = 4,

        KMT_MOUSE_MOVE,

        KMT_ANDROID_KEY,

        KMT_SCRIPT,

        KMT_CAMERA_MOVE,

        KMT_FREE_LOOK // 【新增】小眼睛自由视角

    };

    Q_ENUM(KeyMapType)



    enum ActionType

    {

        AT_INVALID = -1,

        AT_KEY = 0,

        AT_MOUSE = 1,

    };

    Q_ENUM(ActionType)



    // 【新增】解析按键的结果结构

    struct ParsedKey {

        ActionType type = AT_INVALID;

        int key = Qt::Key_unknown;

        Qt::KeyboardModifiers modifiers = Qt::NoModifier;

    };



    struct KeyNode

    {

        ActionType type = AT_INVALID;

        int key = Qt::Key_unknown;

        Qt::KeyboardModifiers modifiers = Qt::NoModifier;  // 【新增】支持组合键

        QPointF pos = QPointF(0, 0);

        QPointF extendPos = QPointF(0, 0);

        double extendOffset = 0.0;

        AndroidKeycode androidKey = AKEYCODE_UNKNOWN;



        KeyNode(

            ActionType type = AT_INVALID,

            int key = Qt::Key_unknown,

            Qt::KeyboardModifiers modifiers = Qt::NoModifier,

            QPointF pos = QPointF(0, 0),

            QPointF extendPos = QPointF(0, 0),

            double extendOffset = 0.0,

            AndroidKeycode androidKey = AKEYCODE_UNKNOWN)

            : type(type), key(key), modifiers(modifiers), pos(pos), extendPos(extendPos), extendOffset(extendOffset), androidKey(androidKey)

        {

        }

    };



    struct KeyMapNode

    {

        KeyMapType type = KMT_INVALID;

        QString script;



        union DATA

        {

            struct

            {

                QPointF centerPos = { 0.0, 0.0 };

                KeyNode left, right, up, down;

            } steerWheel;



            struct

            {

                QPointF startPos   = { 0.0, 0.0 };

                QPointF speedRatio = { 1.0, 1.0 };

            } mouseMove;



            struct

            {

                KeyNode keyNode;

            } androidKey;



            struct

            {

                KeyNode keyNode;

            } script;

            struct

            {

                KeyNode keyNode;        // 触发热键

                QPointF startPos = { 0.0, 0.0 };  // 起始位置

                QPointF speedRatio = { 1.0, 1.0 };  // 灵敏度

                bool resetViewOnRelease = false;  // 松开时是否重置视角

            } freeLook;

            DATA() {}

            ~DATA() {}

        } data;



        KeyMapNode() {}

        ~KeyMapNode() {}

    };



    KeyMap(QObject *parent = Q_NULLPTR);

    virtual ~KeyMap();



    void loadKeyMap(const QString &json);

    const KeyMap::KeyMapNode &getKeyMapNode(int key);

    const KeyMap::KeyMapNode &getKeyMapNodeKey(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

    const KeyMap::KeyMapNode &getKeyMapNodeMouse(int key);

    // 根据显示名称查找按键（支持 "LMB", "Tab", "=" 等）
    const KeyMap::KeyMapNode &getKeyMapNodeByDisplayName(const QString& displayName);

    // 获取所有键位节点（供自动启动脚本检测使用）
    const QVector<KeyMapNode>& getKeyMapNodes() const { return m_keyMapNodes; }

    bool isSwitchOnKeyboard();

    int getSwitchKey();



    bool isValidMouseMoveMap();

    bool isValidSteerWheelMap();

    const KeyMap::KeyMapNode &getMouseMoveMap();



    // 设置轮盘偏移系数（临时生效）
    // 默认 1,1,1,1，实际偏移 = 原值 * 系数
    void setSteerWheelCoefficient(double up, double down, double left, double right);

    // 重置轮盘偏移系数为默认值 1,1,1,1
    void resetSteerWheelCoefficient();

    // 获取应用系数后的轮盘偏移
    double getSteerWheelOffset(int direction) const; // 0=up, 1=down, 2=left, 3=right

    // 获取单独的轮盘系数（不乘以基础偏移）
    double getSteerWheelCoefficient(int direction) const; // 0=up, 1=down, 2=left, 3=right

    // 获取轮盘节点（用于即时更新）
    const KeyMapNode* getSteerWheelNode() const;

    // 检查系数是否变化（并重置标志）
    bool checkCoefficientChanged() {
        bool changed = m_coefficientChanged;
        m_coefficientChanged = false;
        return changed;
    }



private:

    void makeReverseMap();



    bool checkItemString(const QJsonObject &node, const QString &name);

    bool checkItemDouble(const QJsonObject &node, const QString &name);

    bool checkItemBool(const QJsonObject &node, const QString &name);

    bool checkItemObject(const QJsonObject &node, const QString &name);

    bool checkItemPos(const QJsonObject &node, const QString &name);



    bool checkForSteerWhell(const QJsonObject &node);

    bool checkForAndroidKey(const QJsonObject &node);

    bool checkForScript(const QJsonObject &node);

    bool checkForCamera(const QJsonObject &node); // 【新增】

    bool checkForFreeLook(const QJsonObject &node); // 【新增】小眼睛



    QString getItemString(const QJsonObject &node, const QString &name);

    double getItemDouble(const QJsonObject &node, const QString &name);

    bool getItemBool(const QJsonObject &node, const QString &name);

    QJsonObject getItemObject(const QJsonObject &node, const QString &name);

    QPointF getItemPos(const QJsonObject &node, const QString &name);

    ParsedKey getItemKey(const QJsonObject &node, const QString &name);

    KeyMapType getItemKeyMapType(const QJsonObject &node, const QString &name);



private:

    static QString s_keyMapPath;



    QVector<KeyMapNode> m_keyMapNodes;

    KeyNode m_switchKey = { AT_KEY, Qt::Key_QuoteLeft };



    KeyMapNode m_invalidNode;



    int m_idxSteerWheel = -1;

    // 轮盘偏移系数（临时生效）
    double m_steerWheelCoeff[4] = {1.0, 1.0, 1.0, 1.0}; // up, down, left, right
    bool m_coefficientChanged = false;

    int m_idxMouseMove = -1;



    QMetaEnum m_metaEnumKey = QMetaEnum::fromType<Qt::Key>();

    QMetaEnum m_metaEnumMouseButtons = QMetaEnum::fromType<Qt::MouseButtons>();

    QMetaEnum m_metaEnumKeyMapType = QMetaEnum::fromType<KeyMap::KeyMapType>();



    // 使用 qint64 作为键，低32位存储 key，高32位存储 modifiers

    QMultiHash<qint64, KeyMapNode *> m_rmapKey;

    QMultiHash<int, KeyMapNode *> m_rmapMouse;



    // 辅助函数：组合 key 和 modifiers 为查找键

    static qint64 makeKeyHash(int key, Qt::KeyboardModifiers modifiers) {

        return (static_cast<qint64>(modifiers) << 32) | static_cast<qint64>(key);

    }

};



#endif // KEYMAP_H
