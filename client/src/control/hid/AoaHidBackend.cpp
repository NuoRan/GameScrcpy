/**
 * @file AoaHidBackend.cpp
 * @brief AOA HID 触控后端实现
 */
#ifdef HAVE_AOA_HID

#define LOG_TAG "AoaHidBackend"
#include "Logger.h"
#include "AoaHidBackend.h"
#include "AoaHidDevice.h"
#include "HidReportDescriptor.h"

AoaHidBackend::AoaHidBackend(QObject* parent)
    : QObject(parent)
{
    // 初始化 contactId 池: 0-15
    for (uint8_t i = 0; i < HID_TOUCH_MAX_CONTACTS; ++i) {
        m_freeContactIds.enqueue(i);
    }
}

AoaHidBackend::~AoaHidBackend()
{
    close();
}

bool AoaHidBackend::open()
{
    if (m_device && m_device->isOpen()) return true;

    m_device = new AoaHidDevice(this);
    connect(m_device, &AoaHidDevice::connected, this, &AoaHidBackend::connected);
    connect(m_device, &AoaHidDevice::disconnected, this, [this]{
        m_touchRegistered = false;
        m_keyboardRegistered = false;
        emit disconnected();
        emit statusChanged("AOA 设备已断开");
    });
    connect(m_device, &AoaHidDevice::error, this, [this](const QString& msg){
        emit statusChanged(msg);
    });

    emit statusChanged("正在连接 AOA 设备...");
    LOGI() << "Opening AOA device, serial='" << m_serial.toStdString() << "'";

    if (!m_device->open(m_serial)) {
        LOGE() << "AoaHidDevice::open() failed";
        delete m_device;
        m_device = nullptr;
        return false;
    }

    // 注册触摸屏 HID 设备
    emit statusChanged("正在注册 HID 触摸屏...");
    if (!m_device->registerHid(HID_ACCESSORY_ID_TOUCH,
                                HID_TOUCH_SCREEN_REPORT_DESC,
                                HID_TOUCH_REPORT_DESC_SIZE)) {
        emit statusChanged("注册 HID 触摸屏失败");
        m_device->close();
        delete m_device;
        m_device = nullptr;
        return false;
    }
    m_touchRegistered = true;

    // 注册键盘 HID (可选, 用于纯 AOA 模式)
    if (m_device->registerHid(HID_ACCESSORY_ID_KEYBOARD,
                               HID_KEYBOARD_REPORT_DESC,
                               HID_KEYBOARD_REPORT_DESC_SIZE)) {
        m_keyboardRegistered = true;
        LOGI() << "AOA HID keyboard registered";
    }

    emit statusChanged("AOA HID 已就绪");
    LOGI() << "AOA HID backend opened: serial=" << m_serial.toStdString();
    return true;
}

void AoaHidBackend::close()
{
    if (!m_device) return;

    resetAllTouch();

    if (m_touchRegistered) {
        m_device->unregisterHid(HID_ACCESSORY_ID_TOUCH);
        m_touchRegistered = false;
    }
    if (m_keyboardRegistered) {
        m_device->unregisterHid(HID_ACCESSORY_ID_KEYBOARD);
        m_keyboardRegistered = false;
    }

    m_device->close();
    delete m_device;
    m_device = nullptr;

    emit statusChanged("AOA 已断开");
}

bool AoaHidBackend::isConnected() const
{
    return m_device && m_device->isOpen() && m_touchRegistered;
}

std::string AoaHidBackend::statusText() const
{
    if (!m_device || !m_device->isOpen()) return "未连接";
    if (!m_touchRegistered) return "触摸屏未注册";
    return "AOA HID 已连接";
}

bool AoaHidBackend::sendTouch(TouchAction action, uint8_t touchId,
                               uint16_t x, uint16_t y)
{
    if (!isConnected()) return false;

    QMutexLocker lock(&m_contactMutex);

    // 通知伴侣 App 光标位置 (旋转前 = 显示方向坐标系)
    if (m_touchPosCb) {
        m_touchPosCb(x / 65535.0f, y / 65535.0f);
    }

    // 横屏旋转: 显示坐标系 → 物理触摸板坐标系
    uint16_t rx = x, ry = y;
    switch (m_displayRotation) {
    case 90:
        rx = y;
        ry = 65535 - x;
        break;
    case 270:
        rx = 65535 - y;
        ry = x;
        break;
    case 180:
        rx = 65535 - x;
        ry = 65535 - y;
        break;
    default:
        break;
    }

    uint16_t hx = toHidCoord(rx);
    uint16_t hy = toHidCoord(ry);

    uint8_t report[HID_TOUCH_REPORT_SIZE];

    switch (action) {
    case TouchAction::Down: {
        uint8_t cid = mapContactId(touchId);
        m_activeContacts[touchId] = {hx, hy, cid};
        uint8_t count = static_cast<uint8_t>(m_activeContacts.size());
        buildTouchReport(report, true, cid, hx, hy, count);
        m_device->sendHidEvent(HID_ACCESSORY_ID_TOUCH, report, HID_TOUCH_REPORT_SIZE);
        resendActiveContacts();
        break;
    }
    case TouchAction::Move: {
        auto it = m_activeContacts.find(touchId);
        if (it == m_activeContacts.end()) return false;
        it->x = hx;
        it->y = hy;
        uint8_t count = static_cast<uint8_t>(m_activeContacts.size());
        buildTouchReport(report, true, it->contactId, hx, hy, count);
        m_device->sendHidEvent(HID_ACCESSORY_ID_TOUCH, report, HID_TOUCH_REPORT_SIZE);
        break;
    }
    case TouchAction::Up: {
        auto it = m_activeContacts.find(touchId);
        if (it == m_activeContacts.end()) return false;
        uint8_t cid = it->contactId;
        releaseContactId(touchId);
        m_activeContacts.erase(it);
        uint8_t count = static_cast<uint8_t>(m_activeContacts.size());
        buildTouchReport(report, false, cid, hx, hy, count);
        m_device->sendHidEvent(HID_ACCESSORY_ID_TOUCH, report, HID_TOUCH_REPORT_SIZE);
        resendActiveContacts();
        break;
    }
    }
    return true;
}

void AoaHidBackend::resetAllTouch()
{
    QMutexLocker lock(&m_contactMutex);
    if (m_activeContacts.isEmpty()) return;

    // 发送所有活跃点的 UP
    uint8_t report[HID_TOUCH_REPORT_SIZE];
    for (auto it = m_activeContacts.begin(); it != m_activeContacts.end(); ++it) {
        buildTouchReport(report, false, it->contactId, it->x, it->y, 0);
        if (m_device) {
            m_device->sendHidEvent(HID_ACCESSORY_ID_TOUCH, report, HID_TOUCH_REPORT_SIZE);
        }
    }

    // 重置 contactId 池
    m_activeContacts.clear();
    m_freeContactIds.clear();
    for (uint8_t i = 0; i < HID_TOUCH_MAX_CONTACTS; ++i) {
        m_freeContactIds.enqueue(i);
    }
}

bool AoaHidBackend::sendKey(HidKeyAction action, int32_t androidKeycode)
{
    if (!m_keyboardRegistered || !m_device) return false;

    // 简单映射: 将常用 Android keycode 转为 HID Usage ID
    // 这里只处理基础键, 复杂映射后续扩展
    uint8_t hidKeycode = 0;
    switch (androidKeycode) {
    case 4:   hidKeycode = 0x29; break; // BACK → Escape
    case 3:   hidKeycode = 0x4A; break; // HOME → Home
    case 24:  hidKeycode = 0x80; break; // VOLUME_UP
    case 25:  hidKeycode = 0x81; break; // VOLUME_DOWN
    case 26:  hidKeycode = 0x66; break; // POWER → Power
    default: return false;
    }

    uint8_t report[HID_KEYBOARD_REPORT_SIZE] = {0};
    if (action == HidKeyAction::Down) {
        report[2] = hidKeycode;  // key slot 0
    }
    // UP = 全零报告 (释放所有键)

    m_device->sendHidEvent(HID_ACCESSORY_ID_KEYBOARD, report, HID_KEYBOARD_REPORT_SIZE);
    return true;
}

// ---- 内部辅助 ----

uint8_t AoaHidBackend::mapContactId(uint8_t seqId)
{
    // 已分配?
    auto it = m_activeContacts.find(seqId);
    if (it != m_activeContacts.end()) {
        return it->contactId;
    }
    // 从池中取
    if (!m_freeContactIds.isEmpty()) {
        return m_freeContactIds.dequeue();
    }
    // 池空, 用 seqId mod 16 作为 fallback
    return seqId & 0x0F;
}

void AoaHidBackend::releaseContactId(uint8_t seqId)
{
    auto it = m_activeContacts.find(seqId);
    if (it != m_activeContacts.end()) {
        m_freeContactIds.enqueue(it->contactId);
    }
}

void AoaHidBackend::buildTouchReport(uint8_t* buf, bool tipSwitch,
                                      uint8_t contactId,
                                      uint16_t x, uint16_t y,
                                      uint8_t contactCount)
{
    buf[0] = tipSwitch ? 0x01 : 0x00;
    buf[1] = contactId;
    buf[2] = static_cast<uint8_t>(x & 0xFF);
    buf[3] = static_cast<uint8_t>((x >> 8) & 0xFF);
    buf[4] = static_cast<uint8_t>(y & 0xFF);
    buf[5] = static_cast<uint8_t>((y >> 8) & 0xFF);
    buf[6] = contactCount;
}

void AoaHidBackend::resendActiveContacts()
{
    // DOWN/UP 后需要补发所有其他活跃点的位置
    // Android 多点触控协议要求每次报告包含所有活跃触摸点
    if (m_activeContacts.size() <= 1) return;

    uint8_t report[HID_TOUCH_REPORT_SIZE];
    uint8_t count = static_cast<uint8_t>(m_activeContacts.size());

    for (auto it = m_activeContacts.begin(); it != m_activeContacts.end(); ++it) {
        buildTouchReport(report, true, it->contactId, it->x, it->y, count);
        m_device->sendHidEvent(HID_ACCESSORY_ID_TOUCH, report, HID_TOUCH_REPORT_SIZE);
    }
}

#endif // HAVE_AOA_HID
