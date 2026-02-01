#include "inputconvertbase.h"
#include "controller.h"

InputConvertBase::InputConvertBase(Controller *controller) : QObject(controller), m_controller(controller)
{
    Q_ASSERT(controller);
}

InputConvertBase::~InputConvertBase() {}

// ---------------------------------------------------------
// 发送控制消息
// 将封装好的 ControlMsg 发送给 Controller 进行处理
// ---------------------------------------------------------
void InputConvertBase::sendControlMsg(ControlMsg *msg)
{
    if (msg && m_controller) {
        m_controller->postControlMsg(msg);
    }
}
