/**
 * @file Esp32HidBackend.h
 * @brief ESP32 HID 串口触控后端 / ESP32 HID Serial Touch Backend
 *
 * 通过串口与 ESP32-S3 HID Touch 固件通信，
 * 将触摸事件转发到 ESP32，由其作为 USB HID 设备发送到手机。
 *
 * 串口协议 (基于 QineTouch v3.2):
 *   Header(1) + TipSwitch(1) + ContactID(1) + X(4 LE) + Y(4 LE) + Count(1) = 12 bytes
 *   Header = 0xF4
 *   TipSwitch: 1 = DOWN/MOVE, 0 = UP
 *   X/Y: uint32 LE, 范围 0-32767
 *   Count: 当前活跃触点数
 */
#ifndef ESP32_HID_BACKEND_H
#define ESP32_HID_BACKEND_H

#include "ITouchBackend.h"
#include <QObject>
#include <QThread>
#include <QMutex>
#include <QSerialPort>
#include <map>

class Esp32HidBackend : public QObject, public ITouchBackend {
    Q_OBJECT
public:
    explicit Esp32HidBackend(QObject* parent = nullptr);
    ~Esp32HidBackend() override;

    /// 设置串口名 (如 "COM3")
    void setPortName(const std::string& portName) { m_portName = portName; }
    void setBaudRate(int baud) { m_baudRate = baud; }

    /// 设置显示旋转角度 (0/90/180/270)，用于坐标变换
    void setDisplayRotation(int rotation) { m_displayRotation = rotation; }

    // ITouchBackend
    bool open() override;
    void close() override;
    bool isConnected() const override;
    std::string statusText() const override;
    bool sendTouch(TouchAction action, uint8_t touchId, uint16_t x, uint16_t y) override;
    void resetAllTouch() override;

signals:
    void connected();
    void disconnected();
    void error(const QString& msg);

private:
    static constexpr uint8_t  MAGIC_HEADER   = 0xF4;
    static constexpr uint16_t HID_COORD_MAX  = 32767;
    static constexpr int      PACKET_SIZE    = 12;

    void sendPacket(uint8_t tipSwitch, uint8_t contactId,
                    uint32_t hx, uint32_t hy, uint8_t count);

    std::string m_portName;
    int         m_baudRate = 921600;
    int         m_displayRotation = 0;
    QSerialPort* m_serial = nullptr;
    bool        m_connected = false;

    // 活跃触点管理 (与 QineTouch protocol 一致)
    struct ActivePoint { uint32_t x; uint32_t y; };
    std::map<uint8_t, ActivePoint> m_activeContacts;
    QMutex m_mutex;
};

#endif // ESP32_HID_BACKEND_H
