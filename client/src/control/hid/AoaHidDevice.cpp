/**
 * @file AoaHidDevice.cpp
 * @brief AOA HID USB 底层通信实现
 */
#ifdef HAVE_AOA_HID

#define LOG_TAG "AoaHidDevice"
#include "Logger.h"
#include "AoaHidDevice.h"

#include <libusb.h>

AoaHidDevice::AoaHidDevice(QObject* parent)
    : QObject(parent)
{
}

AoaHidDevice::~AoaHidDevice()
{
    close();
}

bool AoaHidDevice::open(const QString& serial)
{
    if (m_handle) {
        LOGW() << "Already open";
        return true;
    }

    int ret = libusb_init(&m_ctx);
    if (ret < 0) {
        LOGE() << "libusb_init failed: " << libusb_strerror(static_cast<libusb_error>(ret));
        emit error(QString("libusb 初始化失败: %1").arg(libusb_strerror(static_cast<libusb_error>(ret))));
        return false;
    }
    LOGI() << "libusb initialized, looking for serial='" << serial.toStdString() << "'";

    // 枚举 USB 设备
    libusb_device** devList = nullptr;
    ssize_t cnt = libusb_get_device_list(m_ctx, &devList);
    if (cnt < 0) {
        LOGE() << "libusb_get_device_list failed";
        emit error("枚举 USB 设备失败");
        libusb_exit(m_ctx);
        m_ctx = nullptr;
        return false;
    }
    LOGI() << "USB device count: " << cnt;

    // 常见 Android 手机厂商 VID
    auto isAndroidVid = [](uint16_t vid) {
        return vid == 0x18D1 /* Google */  || vid == 0x2717 /* Xiaomi */
            || vid == 0x04E8 /* Samsung */ || vid == 0x22B8 /* Motorola */
            || vid == 0x0BB4 /* HTC */     || vid == 0x12D1 /* Huawei */
            || vid == 0x2A70 /* OnePlus */ || vid == 0x1004 /* LG */
            || vid == 0x0FCE /* Sony */    || vid == 0x2916 /* Yota */
            || vid == 0x1949 /* Honor */   || vid == 0x05C6 /* Qualcomm */
            || vid == 0x2C7C /* Quectel */ || vid == 0x19D2 /* ZTE */
            || vid == 0x1782 /* Spreadtrum */;
    };

    libusb_device_handle* foundHandle = nullptr;
    QString foundSerial;
    int androidDevCount = 0;     // 已知 Android VID 但无法打开 或 AOA 不可用
    int aoaFailedCount = 0;      // 可以打开但 AOA 查询失败

    for (ssize_t i = 0; i < cnt; i++) {
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(devList[i], &desc) != 0) continue;

        libusb_device_handle* handle = nullptr;
        int openRet = libusb_open(devList[i], &handle);
        if (openRet != 0) {
            if (isAndroidVid(desc.idVendor)) {
                LOGW() << "Cannot open Android device VID:PID="
                       << QString("%1:%2").arg(desc.idVendor, 4, 16, QChar('0'))
                              .arg(desc.idProduct, 4, 16, QChar('0')).toStdString()
                       << " error=" << libusb_strerror(static_cast<libusb_error>(openRet))
                       << " (需要安装 WinUSB 驱动, 请用 Zadig 工具)";
                androidDevCount++;
            }
            continue;
        }

        // 检查 AOA 协议版本
        uint16_t aoaVersion = 0;
        bool aoaOk = aoaGetProtocolWith(handle, &aoaVersion);
        bool knownAndroid = isAndroidVid(desc.idVendor);

        LOGI() << "USB device [" << i << "] VID:PID="
               << QString("%1:%2").arg(desc.idVendor, 4, 16, QChar('0'))
                      .arg(desc.idProduct, 4, 16, QChar('0')).toStdString()
               << (knownAndroid ? " (Android)" : "")
               << " AOA query " << (aoaOk ? "ok" : "fail") << " version=" << aoaVersion;

        if (!aoaOk || aoaVersion < 2) {
            if (knownAndroid) {
                androidDevCount++;
                aoaFailedCount++;
                LOGW() << "  -> Android device detected but AOA protocol unavailable."
                       << " Likely needs WinUSB driver (use Zadig).";
            }
            libusb_close(handle);
            continue;
        }
        androidDevCount++;

        // 读取序列号
        unsigned char serialBuf[256] = {0};
        if (desc.iSerialNumber > 0) {
            libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber,
                                                serialBuf, sizeof(serialBuf));
        }
        QString devSerial = QString::fromLatin1(reinterpret_cast<char*>(serialBuf));

        // 如果指定了序列号，匹配；否则取第一个
        if (!serial.isEmpty() && devSerial != serial) {
            LOGI() << "Serial mismatch: want='" << serial.toStdString()
                   << "' got='" << devSerial.toStdString() << "', skip";
            libusb_close(handle);
            continue;
        }

        foundHandle = handle;
        foundSerial = devSerial;
        LOGI() << "Found AOA device: serial=" << devSerial.toStdString()
               << " VID:PID=" << QString("%1:%2")
                      .arg(desc.idVendor, 4, 16, QChar('0'))
                      .arg(desc.idProduct, 4, 16, QChar('0')).toStdString()
               << " AOA v" << aoaVersion;
        break;
    }

    libusb_free_device_list(devList, 1);

    if (!foundHandle) {
        QString reason;
        if (cnt == 0) {
            reason = "没有检测到任何 USB 设备。";
        } else if (androidDevCount == 0) {
            reason = QString("检测到 %1 个 USB 设备，但没有 Android 手机。\n"
                             "请确保手机通过 USB 数据线连接到电脑。").arg(cnt);
        } else if (aoaFailedCount > 0) {
            reason = QString("检测到 Android 手机，但 AOA 协议不可用。\n\n"
                             "这通常是 USB 驱动问题，请按以下步骤操作:\n"
                             "1. 下载 Zadig (zadig.akeo.ie)\n"
                             "2. 菜单 Options → List All Devices\n"
                             "3. 选择你的 Android 手机\n"
                             "4. 将驱动替换为 WinUSB\n"
                             "5. 点击 Replace Driver 或 Install Driver\n\n"
                             "注意: 替换驱动后 ADB 可能失效，"
                             "需要时可在 Zadig 恢复原驱动。");
        } else {
            reason = QString("检测到 Android 设备但无法打开。\n"
                             "请尝试使用 Zadig 安装 WinUSB 驱动，\n"
                             "或确保手机未被其他程序独占。");
        }
        LOGE() << "AOA open failed: " << reason.toStdString();
        emit error(reason);
        libusb_exit(m_ctx);
        m_ctx = nullptr;
        return false;
    }

    m_handle = foundHandle;
    m_serial = foundSerial;

    startSenderThread();
    emit connected();
    return true;
}

void AoaHidDevice::close()
{
    stopSenderThread();

    if (m_handle) {
        libusb_close(m_handle);
        m_handle = nullptr;
    }
    if (m_ctx) {
        libusb_exit(m_ctx);
        m_ctx = nullptr;
    }
    m_serial.clear();
}

bool AoaHidDevice::isOpen() const
{
    return m_handle != nullptr;
}

// ---- AOA 协议操作 ----

bool AoaHidDevice::aoaGetProtocol(uint16_t* version)
{
    return aoaGetProtocolWith(m_handle, version);
}

// 静态辅助：用指定 handle 查询 AOA 版本
bool AoaHidDevice::aoaGetProtocolWith(libusb_device_handle* handle, uint16_t* version)
{
    uint8_t buf[2] = {0};
    int ret = libusb_control_transfer(handle,
        LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR,
        AOA_GET_PROTOCOL,
        0, 0,
        buf, 2,
        USB_TIMEOUT_MS);
    if (ret == 2) {
        *version = static_cast<uint16_t>(buf[0] | (buf[1] << 8));
        return true;
    }
    return false;
}

bool AoaHidDevice::aoaRegisterHid(uint16_t id, uint16_t descSize)
{
    int ret = libusb_control_transfer(m_handle,
        LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR,
        AOA_REGISTER_HID,
        id,         // wValue = accessory ID
        descSize,   // wIndex = report desc size
        nullptr, 0,
        USB_TIMEOUT_MS);
    return !checkDisconnected(ret) && ret >= 0;
}

bool AoaHidDevice::aoaSetReportDesc(uint16_t id, const uint8_t* desc, uint16_t size)
{
    int ret = libusb_control_transfer(m_handle,
        LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR,
        AOA_SET_HID_REPORT_DESC,
        id,    // wValue = accessory ID
        0,     // wIndex = offset (always 0)
        const_cast<uint8_t*>(desc), size,
        USB_TIMEOUT_MS);
    return !checkDisconnected(ret) && ret == size;
}

bool AoaHidDevice::aoaSendHidEvent(uint16_t id, const uint8_t* data, uint16_t size)
{
    int ret = libusb_control_transfer(m_handle,
        LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR,
        AOA_SEND_HID_EVENT,
        id,    // wValue = accessory ID
        0,     // wIndex = 0
        const_cast<uint8_t*>(data), size,
        USB_TIMEOUT_MS);
    return !checkDisconnected(ret) && ret == size;
}

bool AoaHidDevice::aoaUnregisterHid(uint16_t id)
{
    int ret = libusb_control_transfer(m_handle,
        LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR,
        AOA_UNREGISTER_HID,
        id,    // wValue = accessory ID
        0,
        nullptr, 0,
        USB_TIMEOUT_MS);
    return ret >= 0;
}

bool AoaHidDevice::checkDisconnected(int result)
{
    if (result == LIBUSB_ERROR_NO_DEVICE || result == LIBUSB_ERROR_NOT_FOUND) {
        LOGW() << "USB device disconnected";
        emit disconnected();
        return true;
    }
    return false;
}

// ---- 高级操作 ----

bool AoaHidDevice::registerHid(uint16_t accessoryId, const uint8_t* reportDesc, uint16_t descSize)
{
    if (!m_handle) return false;

    if (!aoaRegisterHid(accessoryId, descSize)) {
        LOGE() << "AOA REGISTER_HID failed for ID " << accessoryId;
        return false;
    }

    if (!aoaSetReportDesc(accessoryId, reportDesc, descSize)) {
        LOGE() << "AOA SET_HID_REPORT_DESC failed for ID " << accessoryId;
        aoaUnregisterHid(accessoryId);
        return false;
    }

    LOGI() << "AOA HID registered: ID=" << accessoryId << " descSize=" << descSize;
    return true;
}

bool AoaHidDevice::unregisterHid(uint16_t accessoryId)
{
    if (!m_handle) return false;
    bool ok = aoaUnregisterHid(accessoryId);
    LOGI() << "AOA HID unregistered: ID=" << accessoryId << " ok=" << ok;
    return ok;
}

void AoaHidDevice::sendHidEvent(uint16_t accessoryId, const uint8_t* data, uint16_t size)
{
    if (!m_running) return;

    QMutexLocker lock(&m_queueMutex);

    // 队列满时丢弃最旧事件 (MOVE 可丢弃)
    if (m_eventQueue.size() >= MAX_QUEUE_SIZE) {
        m_eventQueue.dequeue();
    }

    HidEvent evt;
    evt.accessoryId = accessoryId;
    evt.size = (size > sizeof(evt.data)) ? sizeof(evt.data) : size;
    memcpy(evt.data, data, evt.size);
    m_eventQueue.enqueue(evt);
    m_queueCond.wakeOne();
}

// ---- 发送线程 ----

void AoaHidDevice::startSenderThread()
{
    m_running = true;
    m_senderThread = QThread::create([this]{ senderLoop(); });
    m_senderThread->setObjectName("aoa-hid-sender");
    m_senderThread->start();
}

void AoaHidDevice::stopSenderThread()
{
    if (!m_senderThread) return;

    m_running = false;
    m_queueCond.wakeAll();
    m_senderThread->wait(3000);
    delete m_senderThread;
    m_senderThread = nullptr;

    m_queueMutex.lock();
    m_eventQueue.clear();
    m_queueMutex.unlock();
}

void AoaHidDevice::senderLoop()
{
    LOGI() << "AOA HID sender thread started";

    while (m_running) {
        HidEvent evt;

        {
            QMutexLocker lock(&m_queueMutex);
            while (m_eventQueue.isEmpty() && m_running) {
                m_queueCond.wait(&m_queueMutex, 100);
            }
            if (!m_running) break;
            evt = m_eventQueue.dequeue();
        }

        if (!m_handle) break;

        if (!aoaSendHidEvent(evt.accessoryId, evt.data, evt.size)) {
            LOGW() << "AOA SEND_HID_EVENT failed";
        }
    }

    LOGI() << "AOA HID sender thread stopped";
}

#endif // HAVE_AOA_HID
