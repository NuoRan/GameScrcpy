#ifndef CONTROLMSG_H
#define CONTROLMSG_H

#include <QBuffer>
#include <QRect>
#include <QString>

#include "input.h"
#include "keycodes.h"
#include "qscrcpyevent.h"

#define CONTROL_MSG_MAX_SIZE (1 << 18) // 256k

#define CONTROL_MSG_INJECT_TEXT_MAX_LENGTH 300
// type: 1 byte; sequence: 8 bytes; paste flag: 1 byte; length: 4 bytes
#define CONTROL_MSG_CLIPBOARD_TEXT_MAX_LENGTH \
    (CONTROL_MSG_MAX_SIZE - 14)

#define POINTER_ID_MOUSE static_cast<quint64>(-1)
#define POINTER_ID_GENERIC_FINGER static_cast<quint64>(-2)

// Used for injecting an additional virtual pointer for pinch-to-zoom
#define POINTER_ID_VIRTUAL_MOUSE static_cast<quint64>(-3)
#define POINTER_ID_VIRTUAL_FINGER static_cast<quint64>(-4)

/**
 * 游戏投屏控制消息 - 极简版本
 * 只保留按键、触摸和返回键功能
 */
class ControlMsg : public QScrcpyEvent
{
public:
    // 核心消息类型（保持原值以兼容服务端）
    enum ControlMsgType
    {
        CMT_NULL = -1,
        CMT_INJECT_KEYCODE = 0,
        CMT_INJECT_TOUCH = 2,
        CMT_BACK_OR_SCREEN_ON = 4,
        CMT_DISCONNECT = 200  // 断开连接消息，通知服务端退出
    };

    ControlMsg(ControlMsgType controlMsgType);
    virtual ~ControlMsg();

    // 对象池支持方法
    void resetType(ControlMsgType newType);
    void cleanup();

    void setInjectKeycodeMsgData(AndroidKeyeventAction action, AndroidKeycode keycode, quint32 repeat, AndroidMetastate metastate);
    // id 代表一个触摸点，最多支持10个触摸点[0,9]
    // action 只能是AMOTION_EVENT_ACTION_DOWN，AMOTION_EVENT_ACTION_UP，AMOTION_EVENT_ACTION_MOVE
    void setInjectTouchMsgData(
        quint64 id,
        AndroidMotioneventAction action,
        AndroidMotioneventButtons actionButtons,
        AndroidMotioneventButtons buttons,
        QRect position,
        float pressure);
    void setBackOrScreenOnData(bool down);

    QByteArray serializeData();

private:
    void writePosition(QBuffer &buffer, const QRect &value);
    quint16 floatToU16fp(float f);  // C-I08: 修复拼写错误
    qint16 flostToI16fp(float f);

private:
    struct ControlMsgData
    {
        ControlMsgType type = CMT_NULL;
        union
        {
            struct
            {
                AndroidKeyeventAction action;
                AndroidKeycode keycode;
                quint32 repeat;
                AndroidMetastate metastate;
            } injectKeycode;
            struct
            {
                quint64 id;
                AndroidMotioneventAction action;
                AndroidMotioneventButtons actionButtons;
                AndroidMotioneventButtons buttons;
                QRect position;
                float pressure;
            } injectTouch;
            struct
            {
                AndroidKeyeventAction action;
            } backOrScreenOn;
        };

        ControlMsgData() {}
        ~ControlMsgData() {}
    };

    ControlMsgData m_data;
};

#endif // CONTROLMSG_H
