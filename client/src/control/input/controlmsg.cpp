#include <QDebug>

#include "bufferutil.h"
#include "controlmsg.h"

ControlMsg::ControlMsg(ControlMsgType controlMsgType) : QScrcpyEvent(Control)
{
    m_data.type = controlMsgType;
}

ControlMsg::~ControlMsg()
{
    cleanup();
}

void ControlMsg::cleanup()
{
    m_data.type = CMT_NULL;
}

void ControlMsg::resetType(ControlMsgType newType)
{
    cleanup();
    m_data.type = newType;
}

void ControlMsg::setInjectKeycodeMsgData(AndroidKeyeventAction action, AndroidKeycode keycode, quint32 repeat, AndroidMetastate metastate)
{
    m_data.injectKeycode.action = action;
    m_data.injectKeycode.keycode = keycode;
    m_data.injectKeycode.repeat = repeat;
    m_data.injectKeycode.metastate = metastate;
}

void ControlMsg::setInjectTouchMsgData(
    quint64 id,
    AndroidMotioneventAction action,
    AndroidMotioneventButtons actionButtons,
    AndroidMotioneventButtons buttons,
    QRect position,
    float pressure)
{
    m_data.injectTouch.id = id;
    m_data.injectTouch.action = action;
    m_data.injectTouch.actionButtons = actionButtons;
    m_data.injectTouch.buttons = buttons;
    m_data.injectTouch.position = position;
    m_data.injectTouch.pressure = pressure;
}

void ControlMsg::setBackOrScreenOnData(bool down)
{
    m_data.backOrScreenOn.action = down ? AKEY_EVENT_ACTION_DOWN : AKEY_EVENT_ACTION_UP;
}

void ControlMsg::writePosition(QBuffer &buffer, const QRect &value)
{
    BufferUtil::write32(buffer, value.left());
    BufferUtil::write32(buffer, value.top());
    BufferUtil::write16(buffer, value.width());
    BufferUtil::write16(buffer, value.height());
}

quint16 ControlMsg::floatToU16fp(float f)
{
    Q_ASSERT(f >= 0.0f && f <= 1.0f);
    quint32 u = f * 0x1p16f; // 2^16
    if (u >= 0xffff) {
        u = 0xffff;
    }
    return (quint16)u;
}

QByteArray ControlMsg::serializeData()
{
    QByteArray byteArray;
    QBuffer buffer(&byteArray);
    buffer.open(QBuffer::WriteOnly);
    buffer.putChar(m_data.type);

    switch (m_data.type) {
    case CMT_INJECT_KEYCODE:
        buffer.putChar(m_data.injectKeycode.action);
        BufferUtil::write32(buffer, m_data.injectKeycode.keycode);
        BufferUtil::write32(buffer, m_data.injectKeycode.repeat);
        BufferUtil::write32(buffer, m_data.injectKeycode.metastate);
        break;
    case CMT_INJECT_TOUCH: {
        buffer.putChar(m_data.injectTouch.action);
        BufferUtil::write64(buffer, m_data.injectTouch.id);
        writePosition(buffer, m_data.injectTouch.position);
        quint16 pressure = floatToU16fp(m_data.injectTouch.pressure);
        BufferUtil::write16(buffer, pressure);
        BufferUtil::write32(buffer, m_data.injectTouch.actionButtons);
        BufferUtil::write32(buffer, m_data.injectTouch.buttons);
    } break;
    case CMT_BACK_OR_SCREEN_ON:
        buffer.putChar(m_data.backOrScreenOn.action);
        break;
    case CMT_DISCONNECT:
        // 断开消息不需要额外数据，只需要类型字节
        break;
    default:
        break;
    }
    buffer.close();
    return byteArray;
}
