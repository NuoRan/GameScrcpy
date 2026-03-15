/**
 * @file TouchRouter.cpp
 * @brief 触控路由器实现 / Touch Event Router Implementation
 */
#include "TouchRouter.h"
#include "ITouchBackend.h"
#include "controlsender.h"
#include "fastmsg.h"

#include <QDebug>

TouchRouter::TouchRouter(QObject* parent)
    : QObject(parent)
{
}

TouchRouter::~TouchRouter() = default;

void TouchRouter::setMethod(TouchMethod method)
{
    if (m_method == method) return;
    m_method = method;
    emit methodChanged(method);
    qDebug() << "[TouchRouter] method changed to" << static_cast<int>(method);
}

void TouchRouter::setScrcpySender(ControlSender* sender)
{
    m_scrcpySender = sender;
}

void TouchRouter::setHidBackend(ITouchBackend* backend)
{
    m_hidBackend = backend;
}

bool TouchRouter::isHidConnected() const
{
    return m_hidBackend && m_hidBackend->isConnected();
}

void TouchRouter::routeFastMsg(const char* data, int len)
{
    if (len < 1) return;

    uint8_t type = static_cast<uint8_t>(data[0]);

    switch (type) {
    // 触摸消息
    case FMT_TOUCH_DOWN:
    case FMT_TOUCH_UP:
    case FMT_TOUCH_MOVE:
    case FMT_TOUCH_RESET:
        routeTouchMsg(type, data, len);
        break;

    // 按键消息
    case FMT_KEY_DOWN:
    case FMT_KEY_UP:
        routeKeyMsg(data, len);
        break;

    // 其他消息 (视频码率、电源、断开等) 始终走 scrcpy
    default:
        sendToScrcpy(data, len);
        break;
    }
}

void TouchRouter::routeTouchMsg(uint8_t type, const char* data, int len)
{
    if (methodUsesScrcpyTouch(m_method)) {
        sendToScrcpy(data, len);
        return;
    }

    // HID 模式 (UHID / AOA / ESP32)
    if (!m_hidBackend || !m_hidBackend->isConnected()) {
        // 后端不可用，丢弃触摸事件（不回退 scrcpy）
        static bool warned = false;
        if (!warned) {
            qWarning() << "[TouchRouter] HID backend unavailable, touch events dropped (no fallback)";
            emit hidStatusChanged(tr("HID 后端不可用，触控已禁用"));
            warned = true;
        }
        return;
    }

    if (type == FMT_TOUCH_RESET) {
        m_hidBackend->resetAllTouch();
        return;
    }

    // 解析 FastMsg 触摸: type(1) + seqId(1) + x(2 BE) + y(2 BE) = 6B
    if (len < 6) return;

    uint8_t seqId = static_cast<uint8_t>(data[1]);
    uint16_t x = (static_cast<uint8_t>(data[2]) << 8) | static_cast<uint8_t>(data[3]);
    uint16_t y = (static_cast<uint8_t>(data[4]) << 8) | static_cast<uint8_t>(data[5]);

    TouchAction action;
    switch (type) {
    case FMT_TOUCH_DOWN: action = TouchAction::Down; break;
    case FMT_TOUCH_UP:   action = TouchAction::Up;   break;
    case FMT_TOUCH_MOVE: action = TouchAction::Move;  break;
    default: return;
    }

    m_hidBackend->sendTouch(action, seqId, x, y);
}

void TouchRouter::routeKeyMsg(const char* data, int len)
{
    // 按键: type(1) + keycode(2 BE) = 3B
    if (len < 3) return;

    if (methodUsesScrcpyTouch(m_method)) {
        sendToScrcpy(data, len);
        return;
    }

    // HID 模式: 如果后端支持按键则用 HID 发送, 否则走 scrcpy
    uint8_t type = static_cast<uint8_t>(data[0]);
    uint16_t keycode = (static_cast<uint8_t>(data[1]) << 8) | static_cast<uint8_t>(data[2]);

    HidKeyAction action = (type == FMT_KEY_DOWN) ? HidKeyAction::Down : HidKeyAction::Up;

    if (m_hidBackend && m_hidBackend->isConnected() && m_hidBackend->supportsKeys()) {
        m_hidBackend->sendKey(action, keycode);
    } else if (m_scrcpyAvailable) {
        sendToScrcpy(data, len);
    }
}

void TouchRouter::sendToScrcpy(const char* data, int len)
{
    if (m_scrcpySender) {
        m_scrcpySender->send(data, len);
    }
}
