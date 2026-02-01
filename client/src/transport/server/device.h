#ifndef DEVICE_H
#define DEVICE_H

#include <set>
#include <functional>
#include <QElapsedTimer>
#include <QPointer>
#include <QTime>
#include <QImage>

#include "QtScrcpyCore.h"
#include "adbprocess.h"

// 前向声明
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class Recorder;
class Server;
class VideoBuffer;
class Decoder;
class FileHandler;
class Demuxer;
class VideoForm;
class Controller;
struct AVFrame;

namespace qsc {

    // ---------------------------------------------------------
    // 设备管理具体实现类
    // 实现了 IDevice 接口，聚合了所有子模块
    // ---------------------------------------------------------
    class Device : public IDevice
    {
        Q_OBJECT
    public:
        explicit Device(DeviceParams params, QObject *parent = nullptr);
        virtual ~Device();

        void setUserData(void* data) override;
        void* getUserData() override;

        void registerDeviceObserver(DeviceObserver* observer) override;
        void deRegisterDeviceObserver(DeviceObserver* observer) override;

        bool connectDevice() override;
        void disconnectDevice() override;

        // 事件转发接口
        void mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize) override;
        void wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize) override;
        void keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize) override;

        // 控制指令接口
        void postGoBack() override;
        void postGoHome() override;
        void postGoMenu() override;
        void postAppSwitch() override;
        void postPower() override;
        void postVolumeUp() override;
        void postVolumeDown() override;
        void postBackOrScreenOn(bool down) override;

        void screenshot() override;
        void showTouch(bool show) override;

        bool isReversePort(quint16 port) override;
        const QString &getSerial() override;

        void updateScript(QString script) override;
        bool isCurrentCustomKeymap() override;

        QSize getMobileSize() override;
        void updateMobileSize() override;

        void pushFileRequest(const QString &file, const QString &devicePath = "") override;
        void installApkRequest(const QString &apkFile) override;

        // [新增] 设置帧获取回调 (用于脚本图像识别)
        void setFrameGrabCallback(std::function<QImage()> callback) override;

    private slots:
        // [新增] 处理ADB执行结果的槽函数
        void onAdbSizeResult(qsc::AdbProcess::ADB_EXEC_RESULT processResult);

    private:
        void initSignals();
        bool saveFrame(int width, int height, uint8_t* dataRGB32);

    private:
        // 子模块指针
        QPointer<Server> m_server;
        bool m_serverStartSuccess = false;
        QPointer<Decoder> m_decoder;
        QPointer<Controller> m_controller;
        QPointer<FileHandler> m_fileHandler;
        QPointer<Demuxer> m_stream;
        QPointer<Recorder> m_recorder;

        QElapsedTimer m_startTimeCount;
        DeviceParams m_params;
        std::set<DeviceObserver*> m_deviceObservers;
        void* m_userData = nullptr;

        // [新增] 存储手机实际分辨率
        QSize m_mobileSize;
        // [新增] 用于执行查询命令的ADB进程
        qsc::AdbProcess *m_adbSizeProcess = nullptr;
        // [新增] 帧获取回调 (用于脚本图像识别)
        std::function<QImage()> m_frameGrabCallback;
    };

}

#endif // DEVICE_H
