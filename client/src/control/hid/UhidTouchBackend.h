/**
 * @file UhidTouchBackend.h
 * @brief UHID 触控后端 / UHID Touch Backend
 *
 * 通过已有的控制通道向服务端发送 UHID 消息，
 * 服务端写 /dev/uhid 创建虚拟触摸屏设备。
 * 无需 USB 驱动，支持 WiFi 模式。
 */
#ifndef UHID_TOUCH_BACKEND_H
#define UHID_TOUCH_BACKEND_H

#include "ITouchBackend.h"
#include <QObject>
#include <QHash>
#include <QQueue>
#include <QMutex>
#include <cstdint>
#include <functional>

using UhidSendFunc = std::function<int64_t(const char*, int)>;

class UhidTouchBackend : public QObject, public ITouchBackend {
    Q_OBJECT
public:
    explicit UhidTouchBackend(QObject* parent = nullptr);
    ~UhidTouchBackend() override;

    /// 设置控制通道发送函数 (必须在 open() 前调用)
    void setSendFunc(UhidSendFunc func) { m_sendFunc = func; }

    // ITouchBackend
    bool open() override;
    void close() override;
    bool isConnected() const override { return m_opened; }
    std::string statusText() const override;
    bool sendTouch(TouchAction action, uint8_t touchId,
                   uint16_t x, uint16_t y) override;
    void resetAllTouch() override;

signals:
    void statusChanged(const QString& text);

private:
    // UHID 消息类型 (与服务端 ControlMessage 一致)
    static constexpr uint8_t  FMT_UHID_CREATE  = 30;
    static constexpr uint8_t  FMT_UHID_INPUT   = 31;
    static constexpr uint8_t  FMT_UHID_DESTROY = 32;

    // UHID 设备 ID
    static constexpr uint16_t UHID_TOUCH_ID = 11;

    /// 发送 UHID_CREATE: 注册触摸屏 HID 设备
    bool sendUhidCreate();
    /// 发送 UHID_INPUT: 发送 HID 触摸报告
    bool sendUhidInput(const uint8_t* report, uint16_t size);
    /// 发送 UHID_DESTROY: 注销 HID 设备
    bool sendUhidDestroy();

    /// 发送原始字节到控制通道
    bool sendRaw(const char* data, int len);

    /// FastMsg 坐标 (0-65535) → HID 坐标 (0-32767)
    uint16_t toHidCoord(uint16_t val) {
        return static_cast<uint16_t>(static_cast<uint32_t>(val) * 32767 / 65535);
    }

    /// FastMsg seqId → contactId 映射
    uint8_t mapContactId(uint8_t seqId);

    /// 构造 7 字节触摸报告 (与 HidReportDescriptor.h 一致)
    void buildTouchReport(uint8_t* buf, bool tipSwitch, uint8_t contactId,
                          uint16_t x, uint16_t y, uint8_t contactCount);

    /// 补发所有活跃触摸点
    void resendActiveContacts();

    UhidSendFunc m_sendFunc;
    bool m_opened = false;

    // 多点触控管理
    struct ActiveContact {
        uint16_t x, y;       // HID 坐标
        uint8_t  contactId;  // 0-15
    };
    QHash<uint8_t, ActiveContact> m_activeContacts;  // key = FastMsg seqId
    QQueue<uint8_t>               m_freeContactIds;  // 可用 contactId 池
    QMutex                        m_contactMutex;
};

#endif // UHID_TOUCH_BACKEND_H
