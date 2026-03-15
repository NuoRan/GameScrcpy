/**
 * @file Esp32HidBackend.cpp
 * @brief ESP32 HID 串口触控后端实现
 *
 * 串口协议 (源自 QineTouch v3.2):
 *   [0xF4] [tipSwitch] [contactId] [X_LE32] [Y_LE32] [count]
 *   共 12 字节。ESP32 收到后去掉 header，将 11 字节 body 作为 HID report 发给手机。
 */
#include "Esp32HidBackend.h"
#include <QDebug>
#include <cstring>

Esp32HidBackend::Esp32HidBackend(QObject* parent)
    : QObject(parent)
{
}

Esp32HidBackend::~Esp32HidBackend()
{
    close();
}

bool Esp32HidBackend::open()
{
    QMutexLocker lock(&m_mutex);

    if (m_serial) {
        close();
    }

    if (m_portName.empty()) {
        emit error(tr("未设置串口名"));
        return false;
    }

    m_serial = new QSerialPort(this);
    m_serial->setPortName(QString::fromStdString(m_portName));
    m_serial->setBaudRate(m_baudRate);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        QString err = m_serial->errorString();
        delete m_serial;
        m_serial = nullptr;
        emit error(tr("串口打开失败: %1").arg(err));
        return false;
    }

    m_connected = true;
    m_activeContacts.clear();

    qDebug() << "[Esp32Hid] Opened" << QString::fromStdString(m_portName) << "@" << m_baudRate;
    emit connected();
    return true;
}

void Esp32HidBackend::close()
{
    QMutexLocker lock(&m_mutex);

    if (m_serial) {
        if (m_serial->isOpen()) {
            m_serial->close();
        }
        delete m_serial;
        m_serial = nullptr;
    }
    m_connected = false;
    m_activeContacts.clear();
    emit disconnected();
}

bool Esp32HidBackend::isConnected() const
{
    return m_connected && m_serial && m_serial->isOpen();
}

std::string Esp32HidBackend::statusText() const
{
    if (isConnected())
        return "ESP32 connected (" + m_portName + ")";
    return "ESP32 disconnected";
}

bool Esp32HidBackend::sendTouch(TouchAction action, uint8_t touchId,
                                 uint16_t x, uint16_t y)
{
    QMutexLocker lock(&m_mutex);
    if (!isConnected()) return false;

    // 横屏旋转: 显示坐标系 → 物理触摸板坐标系
    uint16_t rx = x, ry = y;
    switch (m_displayRotation) {
    case 90:
        rx = 65535 - y;
        ry = x;
        break;
    case 270:
        rx = y;
        ry = 65535 - x;
        break;
    case 180:
        rx = 65535 - x;
        ry = 65535 - y;
        break;
    default:
        break;
    }

    // FastMsg 坐标 0-65535 → HID 坐标 0-32767
    uint32_t hx = static_cast<uint32_t>(rx) * HID_COORD_MAX / 65535;
    uint32_t hy = static_cast<uint32_t>(ry) * HID_COORD_MAX / 65535;

    switch (action) {
    case TouchAction::Down:
        m_activeContacts[touchId] = {hx, hy};
        break;
    case TouchAction::Move:
        m_activeContacts[touchId] = {hx, hy};
        break;
    case TouchAction::Up:
        m_activeContacts.erase(touchId);
        break;
    }

    uint8_t count = static_cast<uint8_t>(m_activeContacts.size());

    // 发送主事件
    uint8_t tipSwitch = (action == TouchAction::Up) ? 0 : 1;
    sendPacket(tipSwitch, touchId, hx, hy, count);

    // DOWN/UP 时需要重发所有其余活跃触点 (Android multi-touch 要求)
    if (action == TouchAction::Down || action == TouchAction::Up) {
        for (auto& [id, pt] : m_activeContacts) {
            if (id != touchId) {
                sendPacket(1, id, pt.x, pt.y, count);
            }
        }
    }

    return true;
}

void Esp32HidBackend::resetAllTouch()
{
    QMutexLocker lock(&m_mutex);
    if (!isConnected()) return;

    // 逐个释放所有触点
    auto contacts = m_activeContacts;  // copy
    for (auto& [id, pt] : contacts) {
        m_activeContacts.erase(id);
        uint8_t count = static_cast<uint8_t>(m_activeContacts.size());
        sendPacket(0, id, pt.x, pt.y, count);
    }
    m_activeContacts.clear();
}

void Esp32HidBackend::sendPacket(uint8_t tipSwitch, uint8_t contactId,
                                  uint32_t hx, uint32_t hy, uint8_t count)
{
    if (!m_serial || !m_serial->isOpen()) return;

    uint8_t buf[PACKET_SIZE];
    buf[0] = MAGIC_HEADER;
    buf[1] = tipSwitch & 0x01;
    buf[2] = contactId;
    // X - little endian uint32
    buf[3]  = static_cast<uint8_t>(hx);
    buf[4]  = static_cast<uint8_t>(hx >> 8);
    buf[5]  = static_cast<uint8_t>(hx >> 16);
    buf[6]  = static_cast<uint8_t>(hx >> 24);
    // Y - little endian uint32
    buf[7]  = static_cast<uint8_t>(hy);
    buf[8]  = static_cast<uint8_t>(hy >> 8);
    buf[9]  = static_cast<uint8_t>(hy >> 16);
    buf[10] = static_cast<uint8_t>(hy >> 24);
    buf[11] = count;

    m_serial->write(reinterpret_cast<const char*>(buf), PACKET_SIZE);
}
