/**
 * @file UhidTouchBackend.cpp
 * @brief UHID 触控后端实现
 *
 * 通过控制通道发送 UHID 协议消息到服务端，
 * 服务端打开 /dev/uhid 注册虚拟触摸屏。
 */
#define LOG_TAG "UhidTouchBackend"
#include "Logger.h"
#include "UhidTouchBackend.h"
#include "HidReportDescriptor.h"

#include <cstring>
#include <QMutexLocker>

UhidTouchBackend::UhidTouchBackend(QObject* parent)
    : QObject(parent)
{
    for (uint8_t i = 0; i < HID_TOUCH_MAX_CONTACTS; ++i) {
        m_freeContactIds.enqueue(i);
    }
}

UhidTouchBackend::~UhidTouchBackend()
{
    close();
}

bool UhidTouchBackend::open()
{
    if (m_opened) return true;

    if (!m_sendFunc) {
        LOGE() << "No send function configured";
        return false;
    }

    if (!sendUhidCreate()) {
        LOGE() << "Failed to send UHID_CREATE";
        return false;
    }

    m_opened = true;
    LOGI() << "UHID touch backend opened (id=" << UHID_TOUCH_ID << ")";
    emit statusChanged("UHID 触摸屏已注册");
    return true;
}

void UhidTouchBackend::close()
{
    if (!m_opened) return;

    resetAllTouch();
    sendUhidDestroy();
    m_opened = false;

    LOGI() << "UHID touch backend closed";
    emit statusChanged("UHID 已断开");
}

std::string UhidTouchBackend::statusText() const
{
    return m_opened ? "UHID connected" : "UHID disconnected";
}

bool UhidTouchBackend::sendTouch(TouchAction action, uint8_t touchId,
                                  uint16_t x, uint16_t y)
{
    if (!m_opened) return false;

    uint16_t hx = toHidCoord(x);
    uint16_t hy = toHidCoord(y);

    uint8_t report[HID_TOUCH_REPORT_SIZE];

    switch (action) {
    case TouchAction::Down: {
        uint8_t cid = mapContactId(touchId);
        {
            QMutexLocker lock(&m_contactMutex);
            ActiveContact ac;
            ac.x = hx;
            ac.y = hy;
            ac.contactId = cid;
            m_activeContacts[touchId] = ac;
        }
        // 发送新触摸点
        uint8_t count = static_cast<uint8_t>(m_activeContacts.size());
        buildTouchReport(report, true, cid, hx, hy, count);
        sendUhidInput(report, HID_TOUCH_REPORT_SIZE);
        // 补发其他活跃点
        resendActiveContacts();
        break;
    }
    case TouchAction::Move: {
        QMutexLocker lock(&m_contactMutex);
        auto it = m_activeContacts.find(touchId);
        if (it == m_activeContacts.end()) return false;
        it->x = hx;
        it->y = hy;
        uint8_t cid = it->contactId;
        uint8_t count = static_cast<uint8_t>(m_activeContacts.size());
        lock.unlock();

        buildTouchReport(report, true, cid, hx, hy, count);
        sendUhidInput(report, HID_TOUCH_REPORT_SIZE);
        resendActiveContacts();
        break;
    }
    case TouchAction::Up: {
        QMutexLocker lock(&m_contactMutex);
        auto it = m_activeContacts.find(touchId);
        if (it == m_activeContacts.end()) return false;
        uint8_t cid = it->contactId;
        m_activeContacts.erase(it);
        m_freeContactIds.enqueue(cid);  // 归还 contactId 到池
        uint8_t count = static_cast<uint8_t>(m_activeContacts.size());
        lock.unlock();

        // 发送 UP (tipSwitch=false)
        buildTouchReport(report, false, cid, hx, hy, count);
        sendUhidInput(report, HID_TOUCH_REPORT_SIZE);
        // 补发剩余活跃点
        resendActiveContacts();
        break;
    }
    }
    return true;
}

void UhidTouchBackend::resetAllTouch()
{
    QMutexLocker lock(&m_contactMutex);
    uint8_t report[HID_TOUCH_REPORT_SIZE];
    for (auto it = m_activeContacts.begin(); it != m_activeContacts.end(); ++it) {
        buildTouchReport(report, false, it->contactId, it->x, it->y, 0);
        sendUhidInput(report, HID_TOUCH_REPORT_SIZE);
    }
    m_activeContacts.clear();
    m_freeContactIds.clear();
    for (uint8_t i = 0; i < HID_TOUCH_MAX_CONTACTS; ++i) {
        m_freeContactIds.enqueue(i);
    }
}

// ---- UHID 消息序列化 ----

bool UhidTouchBackend::sendUhidCreate()
{
    // 格式: type(1) + id(2) + vendorId(2) + productId(2) + nameLen(1) + name(N) + descLen(2) + desc(M)
    const char* name = "gamescrcpy-touch";
    uint8_t nameLen = static_cast<uint8_t>(strlen(name));

    int totalLen = 1 + 2 + 2 + 2 + 1 + nameLen + 2 + HID_TOUCH_REPORT_DESC_SIZE;
    std::vector<char> buf(totalLen);
    int idx = 0;

    buf[idx++] = FMT_UHID_CREATE;
    // id (big-endian)
    buf[idx++] = static_cast<char>((UHID_TOUCH_ID >> 8) & 0xFF);
    buf[idx++] = static_cast<char>(UHID_TOUCH_ID & 0xFF);
    // vendorId
    buf[idx++] = 0;
    buf[idx++] = 0;
    // productId
    buf[idx++] = 0;
    buf[idx++] = 0;
    // name
    buf[idx++] = static_cast<char>(nameLen);
    memcpy(&buf[idx], name, nameLen);
    idx += nameLen;
    // report descriptor
    buf[idx++] = static_cast<char>((HID_TOUCH_REPORT_DESC_SIZE >> 8) & 0xFF);
    buf[idx++] = static_cast<char>(HID_TOUCH_REPORT_DESC_SIZE & 0xFF);
    memcpy(&buf[idx], HID_TOUCH_SCREEN_REPORT_DESC, HID_TOUCH_REPORT_DESC_SIZE);
    idx += HID_TOUCH_REPORT_DESC_SIZE;

    return sendRaw(buf.data(), idx);
}

bool UhidTouchBackend::sendUhidInput(const uint8_t* report, uint16_t size)
{
    // 格式: type(1) + id(2) + dataLen(2) + data(N)
    int totalLen = 1 + 2 + 2 + size;
    char buf[32]; // 报告最大 7 字节, 总共不超过 12
    int idx = 0;

    buf[idx++] = FMT_UHID_INPUT;
    buf[idx++] = static_cast<char>((UHID_TOUCH_ID >> 8) & 0xFF);
    buf[idx++] = static_cast<char>(UHID_TOUCH_ID & 0xFF);
    buf[idx++] = static_cast<char>((size >> 8) & 0xFF);
    buf[idx++] = static_cast<char>(size & 0xFF);
    memcpy(&buf[idx], report, size);
    idx += size;

    return sendRaw(buf, idx);
}

bool UhidTouchBackend::sendUhidDestroy()
{
    // 格式: type(1) + id(2)
    char buf[3];
    buf[0] = FMT_UHID_DESTROY;
    buf[1] = static_cast<char>((UHID_TOUCH_ID >> 8) & 0xFF);
    buf[2] = static_cast<char>(UHID_TOUCH_ID & 0xFF);
    return sendRaw(buf, 3);
}

bool UhidTouchBackend::sendRaw(const char* data, int len)
{
    if (!m_sendFunc) return false;
    int64_t ret = m_sendFunc(data, len);
    return ret >= 0;
}

// ---- 多点触控管理 ----

uint8_t UhidTouchBackend::mapContactId(uint8_t seqId)
{
    QMutexLocker lock(&m_contactMutex);
    auto it = m_activeContacts.find(seqId);
    if (it != m_activeContacts.end()) {
        return it->contactId;
    }
    if (m_freeContactIds.isEmpty()) {
        return 0;
    }
    return m_freeContactIds.dequeue();
}

void UhidTouchBackend::buildTouchReport(uint8_t* buf, bool tipSwitch, uint8_t contactId,
                                         uint16_t x, uint16_t y, uint8_t contactCount)
{
    buf[0] = tipSwitch ? 0x01 : 0x00;  // Tip Switch (bit 0) + 7 bits padding
    buf[1] = contactId;                  // Contact ID
    buf[2] = static_cast<uint8_t>(x & 0xFF);        // X low byte
    buf[3] = static_cast<uint8_t>((x >> 8) & 0xFF); // X high byte
    buf[4] = static_cast<uint8_t>(y & 0xFF);        // Y low byte
    buf[5] = static_cast<uint8_t>((y >> 8) & 0xFF); // Y high byte
    buf[6] = contactCount;               // Contact Count
}

void UhidTouchBackend::resendActiveContacts()
{
    QMutexLocker lock(&m_contactMutex);
    uint8_t count = static_cast<uint8_t>(m_activeContacts.size());
    uint8_t report[HID_TOUCH_REPORT_SIZE];
    for (auto it = m_activeContacts.begin(); it != m_activeContacts.end(); ++it) {
        buildTouchReport(report, true, it->contactId, it->x, it->y, count);
        sendUhidInput(report, HID_TOUCH_REPORT_SIZE);
    }
}
