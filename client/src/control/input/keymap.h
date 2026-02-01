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

        KMT_CAMERA_MOVE // 【新增】

    };

    Q_ENUM(KeyMapType)



    enum ActionType

    {

        AT_INVALID = -1,

        AT_KEY = 0,

        AT_MOUSE = 1,

    };

    Q_ENUM(ActionType)



    struct KeyNode

    {

        ActionType type = AT_INVALID;

        int key = Qt::Key_unknown;

        QPointF pos = QPointF(0, 0);

        QPointF extendPos = QPointF(0, 0);

        double extendOffset = 0.0;

        AndroidKeycode androidKey = AKEYCODE_UNKNOWN;



        KeyNode(

            ActionType type = AT_INVALID,

            int key = Qt::Key_unknown,

            QPointF pos = QPointF(0, 0),

            QPointF extendPos = QPointF(0, 0),

            double extendOffset = 0.0,

            AndroidKeycode androidKey = AKEYCODE_UNKNOWN)

            : type(type), key(key), pos(pos), extendPos(extendPos), extendOffset(extendOffset), androidKey(androidKey)

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

    const KeyMap::KeyMapNode &getKeyMapNodeKey(int key);

    const KeyMap::KeyMapNode &getKeyMapNodeMouse(int key);

    bool isSwitchOnKeyboard();

    int getSwitchKey();



    bool isValidMouseMoveMap();

    bool isValidSteerWheelMap();

    const KeyMap::KeyMapNode &getMouseMoveMap();



    bool updateSteerWheelOffset(double up, double down, double left, double right);



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



    QString getItemString(const QJsonObject &node, const QString &name);

    double getItemDouble(const QJsonObject &node, const QString &name);

    bool getItemBool(const QJsonObject &node, const QString &name);

    QJsonObject getItemObject(const QJsonObject &node, const QString &name);

    QPointF getItemPos(const QJsonObject &node, const QString &name);

    QPair<ActionType, int> getItemKey(const QJsonObject &node, const QString &name);

    KeyMapType getItemKeyMapType(const QJsonObject &node, const QString &name);



private:

    static QString s_keyMapPath;



    QVector<KeyMapNode> m_keyMapNodes;

    KeyNode m_switchKey = { AT_KEY, Qt::Key_QuoteLeft };



    KeyMapNode m_invalidNode;



    int m_idxSteerWheel = -1;

    int m_idxMouseMove = -1;



    QMetaEnum m_metaEnumKey = QMetaEnum::fromType<Qt::Key>();

    QMetaEnum m_metaEnumMouseButtons = QMetaEnum::fromType<Qt::MouseButtons>();

    QMetaEnum m_metaEnumKeyMapType = QMetaEnum::fromType<KeyMap::KeyMapType>();



    QMultiHash<int, KeyMapNode *> m_rmapKey;

    QMultiHash<int, KeyMapNode *> m_rmapMouse;

};



#endif // KEYMAP_H
