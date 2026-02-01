#pragma once
#include <QPointer>
#include <QMouseEvent>
#include <QImage>
#include <functional>

#include "QtScrcpyCoreDef.h"

namespace qsc {

class DeviceObserver {
protected:
    DeviceObserver() {

    }
    virtual ~DeviceObserver() {

    }

public:
    virtual void onFrame(int width, int height, uint8_t* dataY, uint8_t* dataU, uint8_t* dataV, int linesizeY, int linesizeU, int linesizeV) {
        Q_UNUSED(width);
        Q_UNUSED(height);
        Q_UNUSED(dataY);
        Q_UNUSED(dataU);
        Q_UNUSED(dataV);
        Q_UNUSED(linesizeY);
        Q_UNUSED(linesizeU);
        Q_UNUSED(linesizeV);
    }
    virtual void updateFPS(quint32 fps) { Q_UNUSED(fps); }
    virtual void grabCursor(bool grab) {Q_UNUSED(grab);}

    virtual void mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize) {
        Q_UNUSED(from);
        Q_UNUSED(frameSize);
        Q_UNUSED(showSize);
    }
    virtual void wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize) {
        Q_UNUSED(from);
        Q_UNUSED(frameSize);
        Q_UNUSED(showSize);
    }
    virtual void keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize) {
        Q_UNUSED(from);
        Q_UNUSED(frameSize);
        Q_UNUSED(showSize);
    }

    virtual void postGoBack() {}
    virtual void postGoHome() {}
    virtual void postGoMenu() {}
    virtual void postAppSwitch() {}
    virtual void postPower() {}
    virtual void postVolumeUp() {}
    virtual void postVolumeDown() {}
    virtual void postBackOrScreenOn(bool down) { Q_UNUSED(down); }
    virtual void pushFileRequest(const QString &file, const QString &devicePath) {
        Q_UNUSED(file);
        Q_UNUSED(devicePath);
    }
    virtual void installApkRequest(const QString &apkFile) { Q_UNUSED(apkFile); }
    virtual void screenshot() {}
    virtual void showTouch(bool show) { Q_UNUSED(show); }
};

class IDevice : public QObject {
    Q_OBJECT
public:
    IDevice(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~IDevice(){}

signals:
    void deviceConnected(bool success, const QString& serial, const QString& deviceName, const QSize& size);
    void deviceDisconnected(QString serial);

public:
    virtual void setUserData(void* data) = 0;
    virtual void* getUserData() = 0;
    virtual void registerDeviceObserver(DeviceObserver* observer) = 0;
    virtual void deRegisterDeviceObserver(DeviceObserver* observer) = 0;

    virtual bool connectDevice() = 0;
    virtual void disconnectDevice() = 0;

    virtual void mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize) = 0;
    virtual void wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize) = 0;
    virtual void keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize) = 0;

    virtual void postGoBack() = 0;
    virtual void postGoHome() = 0;
    virtual void postGoMenu() = 0;
    virtual void postAppSwitch() = 0;
    virtual void postPower() = 0;
    virtual void postVolumeUp() = 0;
    virtual void postVolumeDown() = 0;
    virtual void postBackOrScreenOn(bool down) = 0;
    virtual void pushFileRequest(const QString &file, const QString &devicePath = "") = 0;
    virtual void installApkRequest(const QString &apkFile) = 0;

    virtual void screenshot() = 0;
    virtual void showTouch(bool show) = 0;

    virtual bool isReversePort(quint16 port) = 0;
    virtual const QString &getSerial() = 0;

    virtual void updateScript(QString script) = 0;
    virtual bool isCurrentCustomKeymap() = 0;

    virtual QSize getMobileSize() = 0;
    virtual void updateMobileSize() = 0;

    // [新增] 设置帧获取回调 (用于脚本图像识别)
    virtual void setFrameGrabCallback(std::function<QImage()> callback) = 0;
};

class IDeviceManage : public QObject {
    Q_OBJECT
public:
    static IDeviceManage& getInstance();
    virtual bool connectDevice(DeviceParams params) = 0;
    virtual bool disconnectDevice(const QString &serial) = 0;
    virtual void disconnectAllDevice() = 0;
    virtual QPointer<IDevice> getDevice(const QString& serial) = 0;

signals:
    void deviceConnected(bool success, const QString& serial, const QString& deviceName, const QSize& size);
    void deviceDisconnected(QString serial);
};

}
