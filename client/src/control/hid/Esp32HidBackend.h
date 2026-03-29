/**
 * @file Esp32HidBackend.h
 * @brief ESP32 HID 触控后端 (串口 + WiFi TCP)
 *
 * 通过串口或 WiFi TCP 与 ESP32-S3 HID Touch 固件通信，
 * 将触摸事件转发到 ESP32，由其作为 USB HID 设备发送到手机。
 *
 * 连接模式自动判断:
 *   - 输入为 IP 地址 (如 "192.168.1.100") → TCP 模式 (端口 26760)
 *   - 输入为串口名 (如 "COM3") → 串口模式 (921600 波特率)
 *
 * 协议 (基于 QineTouch v3.3):
 *   Header(1) + TipSwitch(1) + ContactID(1) + X(4 LE) + Y(4 LE) + Count(1) = 12 bytes
 *   Header = 0xF4
 */
#ifndef ESP32_HID_BACKEND_H
#define ESP32_HID_BACKEND_H

#include "ITouchBackend.h"
#include <QObject>
#include <QThread>
#include <QMutex>
#include <QSerialPort>
#include <QTcpSocket>
#include <map>

class Esp32HidBackend : public QObject, public ITouchBackend {
    Q_OBJECT
public:
    explicit Esp32HidBackend(QObject* parent = nullptr);
    ~Esp32HidBackend() override;

    /// 设置连接目标: IP 地址则走 TCP，COM 口名则走串口
    void setPortName(const std::string& portName) { m_portName = portName; }
    void setBaudRate(int baud) { m_baudRate = baud; }

    /// 设置显示旋转角度 (0/90/180/270)，用于坐标变换
    void setDisplayRotation(int rotation) { m_displayRotation = rotation; }
    /// 是否交换 X/Y 轴（用于设备固件坐标轴定义与客户端不一致时）
    void setSwapXY(bool swap) { m_swapXY = swap; }

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
    static constexpr int      TCP_PORT       = 26760;

    bool isIpAddress(const std::string& addr) const;
    void sendPacket(uint8_t tipSwitch, uint8_t contactId,
                    uint32_t hx, uint32_t hy, uint8_t count);
    void writeBytes(const uint8_t* data, int len);

    enum class ConnMode { Serial, Tcp };

    std::string m_portName;
    int         m_baudRate = 921600;
    int         m_displayRotation = 0;
    bool        m_swapXY = false;
    ConnMode    m_connMode = ConnMode::Serial;

    QSerialPort* m_serial = nullptr;
    QTcpSocket*  m_tcpSocket = nullptr;
    bool         m_connected = false;

    // 活跃触点管理
    struct ActivePoint { uint32_t x; uint32_t y; };
    std::map<uint8_t, ActivePoint> m_activeContacts;
    QMutex m_mutex;
};

#endif // ESP32_HID_BACKEND_H
