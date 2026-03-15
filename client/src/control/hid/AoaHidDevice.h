/**
 * @file AoaHidDevice.h
 * @brief AOA HID USB 底层通信 / AOA HID USB Low-Level Communication
 *
 * 封装 libusb AOA 协议，管理 HID 设备的注册/注销和事件发送。
 * 所有 USB 控制传输在专用发送线程中执行。
 */
#ifndef AOA_HID_DEVICE_H
#define AOA_HID_DEVICE_H

#ifdef HAVE_AOA_HID

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QQueue>
#include <QWaitCondition>
#include <cstdint>
#include <string>

struct libusb_device_handle;
struct libusb_context;

class AoaHidDevice : public QObject {
    Q_OBJECT
public:
    explicit AoaHidDevice(QObject* parent = nullptr);
    ~AoaHidDevice();

    /// 打开 USB 设备并验证 AOA 支持
    bool open(const QString& serial = QString());
    void close();
    bool isOpen() const;

    /// 注册 HID 设备 (AOA REGISTER_HID + SET_HID_REPORT_DESC)
    bool registerHid(uint16_t accessoryId, const uint8_t* reportDesc, uint16_t descSize);
    bool unregisterHid(uint16_t accessoryId);

    /// 发送 HID 事件 (加入异步队列)
    void sendHidEvent(uint16_t accessoryId, const uint8_t* data, uint16_t size);

    /// 设备信息
    QString deviceSerial() const { return m_serial; }

signals:
    void connected();
    void disconnected();
    void error(const QString& message);

private:
    // AOA USB control transfers
    bool aoaGetProtocol(uint16_t* version);
    static bool aoaGetProtocolWith(libusb_device_handle* handle, uint16_t* version);
    bool aoaRegisterHid(uint16_t id, uint16_t descSize);
    bool aoaSetReportDesc(uint16_t id, const uint8_t* desc, uint16_t size);
    bool aoaSendHidEvent(uint16_t id, const uint8_t* data, uint16_t size);
    bool aoaUnregisterHid(uint16_t id);
    bool checkDisconnected(int result);

    // 发送线程
    void startSenderThread();
    void stopSenderThread();
    void senderLoop();

    struct HidEvent {
        uint16_t accessoryId;
        uint8_t  data[16];
        uint16_t size;
    };

    libusb_context*        m_ctx = nullptr;
    libusb_device_handle*  m_handle = nullptr;
    QString                m_serial;

    // 异步发送线程
    QThread*               m_senderThread = nullptr;
    QMutex                 m_queueMutex;
    QWaitCondition         m_queueCond;
    QQueue<HidEvent>       m_eventQueue;
    volatile bool          m_running = false;

    static constexpr int   MAX_QUEUE_SIZE = 64;
    static constexpr int   USB_TIMEOUT_MS = 1000;

    // AOA protocol constants
    static constexpr uint8_t AOA_GET_PROTOCOL        = 51;
    static constexpr uint8_t AOA_REGISTER_HID        = 54;
    static constexpr uint8_t AOA_UNREGISTER_HID      = 55;
    static constexpr uint8_t AOA_SET_HID_REPORT_DESC = 56;
    static constexpr uint8_t AOA_SEND_HID_EVENT      = 57;
};

#endif // HAVE_AOA_HID
#endif // AOA_HID_DEVICE_H
