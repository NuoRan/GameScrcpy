/**
 * @file AoaHidBackend.h
 * @brief AOA HID 触控后端 / AOA HID Touch Backend
 *
 * 将 FastMsg 触摸事件转换为 AOA HID 触摸屏报告，
 * 管理多点触控的 contactId 分配和活跃点补发。
 */
#ifndef AOA_HID_BACKEND_H
#define AOA_HID_BACKEND_H

#ifdef HAVE_AOA_HID

#include "ITouchBackend.h"
#include <QObject>
#include <QHash>
#include <QQueue>
#include <QMutex>
#include <cstdint>

class AoaHidDevice;

class AoaHidBackend : public QObject, public ITouchBackend {
    Q_OBJECT
public:
    explicit AoaHidBackend(QObject* parent = nullptr);
    ~AoaHidBackend() override;

    // ITouchBackend
    bool open() override;
    void close() override;
    bool isConnected() const override;
    std::string statusText() const override;
    bool sendTouch(TouchAction action, uint8_t touchId,
                   uint16_t x, uint16_t y) override;
    void resetAllTouch() override;
    bool supportsKeys() const override { return true; }
    bool sendKey(HidKeyAction action, int32_t androidKeycode) override;

    /// 设置设备序列号 (在 open() 前调用)
    void setSerial(const QString& serial) { m_serial = serial; }

    /// 设置显示旋转角度 (0/90/180/270)，用于坐标变换
    void setDisplayRotation(int rotation) { m_displayRotation = rotation; }

signals:
    void connected();
    void disconnected();
    void statusChanged(const QString& text);

private:
    /// FastMsg 坐标 (0-65535) → HID 坐标 (0-32767)
    uint16_t toHidCoord(uint16_t val) {
        return static_cast<uint16_t>(static_cast<uint32_t>(val) * 32767 / 65535);
    }

    /// FastMsg seqId (1-255) → contactId (0-15)
    uint8_t mapContactId(uint8_t seqId);
    void releaseContactId(uint8_t seqId);

    /// 构造 7 字节触摸报告
    void buildTouchReport(uint8_t* buf, bool tipSwitch, uint8_t contactId,
                          uint16_t x, uint16_t y, uint8_t contactCount);

    /// 补发所有活跃触摸点 (Android多点触控需要)
    void resendActiveContacts();

    AoaHidDevice*  m_device = nullptr;
    QString        m_serial;
    int            m_displayRotation = 0;  // 0/90/180/270
    bool           m_touchRegistered = false;
    bool           m_keyboardRegistered = false;

    // 多点触控管理
    struct ActiveContact {
        uint16_t x, y;       // HID 坐标
        uint8_t  contactId;  // 0-15
    };
    QHash<uint8_t, ActiveContact> m_activeContacts;  // key = FastMsg seqId
    QQueue<uint8_t>               m_freeContactIds;  // 可用 contactId 池
    QMutex                        m_contactMutex;
};

#endif // HAVE_AOA_HID
#endif // AOA_HID_BACKEND_H
