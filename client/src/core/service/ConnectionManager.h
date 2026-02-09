#ifndef CORE_CONNECTIONMANAGER_H
#define CORE_CONNECTIONMANAGER_H

#include <QObject>
#include <QString>
#include <QSize>
#include <memory>

class Server;
class VideoSocket;
class KcpVideoSocket;
class KcpControlSocket;

namespace qsc {
namespace core {

class IVideoChannel;
class IControlChannel;

/**
 * @brief 连接状态 / Connection State
 */
enum class ConnectionState {
    Disconnected,   // 未连接 / Not connected
    Connecting,     // 正在连接 / Connecting
    Connected,      // 已连接 / Connected
    Error           // 错误 / Error
};

/**
 * @brief 连接管理器 / Connection Manager
 *
 * 管理与 Android 设备的连接生命周期 / Manages connection lifecycle with Android device:
 * - 启动 Server（ADB 推送 scrcpy-server 并启动）/ Start server (ADB push scrcpy-server and launch)
 * - 建立 TCP/KCP 视频和控制通道 / Establish TCP/KCP video and control channels
 * - 管理连接状态 / Manage connection state
 *
 * 单一职责 - 只负责连接管理，不涉及解码/渲染/输入
 * Single responsibility - connection management only, no decoding/rendering/input
 */
class ConnectionManager : public QObject
{
    Q_OBJECT

public:
    explicit ConnectionManager(QObject* parent = nullptr);
    ~ConnectionManager() override;

    // 禁止拷贝
    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;

    /**
     * @brief 开始连接设备
     * @param serial 设备序列号（IP:端口 或 ADB 序列号）
     * @param localPort 本地端口（用于 TCP 转发）
     * @param maxWidth 最大宽度
     * @param maxHeight 最大高度
     * @param bitRate 码率
     * @param maxFps 最大帧率
     * @return 成功启动返回 true
     */
    bool connectDevice(const QString& serial,
                      quint16 localPort = 27183,
                      int maxWidth = 720,
                      int maxHeight = 1280,
                      quint32 bitRate = 8000000,
                      quint32 maxFps = 60);

    /**
     * @brief 断开连接
     */
    void disconnectDevice();

    /**
     * @brief 获取当前状态
     */
    ConnectionState state() const { return m_state; }

    /**
     * @brief 是否已连接
     */
    bool isConnected() const { return m_state == ConnectionState::Connected; }

    /**
     * @brief 是否使用 KCP 模式
     */
    bool isKcpMode() const { return m_useKcp; }

    /**
     * @brief 获取视频 Socket（TCP 模式）
     */
    VideoSocket* videoSocket() const { return m_videoSocket; }

    /**
     * @brief 获取 KCP 视频 Socket
     */
    KcpVideoSocket* kcpVideoSocket() const { return m_kcpVideoSocket; }

    /**
     * @brief 获取 KCP 控制 Socket
     */
    KcpControlSocket* kcpControlSocket() const { return m_kcpControlSocket; }

    /**
     * @brief 获取帧尺寸（连接成功后有效）
     */
    QSize frameSize() const { return m_frameSize; }

signals:
    /**
     * @brief 连接状态变化
     */
    void stateChanged(ConnectionState state);

    /**
     * @brief 连接成功
     * @param size 视频帧尺寸
     */
    void connected(const QSize& size);

    /**
     * @brief 断开连接
     */
    void disconnected();

    /**
     * @brief 视频 Socket 就绪（TCP 模式）
     */
    void videoSocketReady(VideoSocket* socket);

    /**
     * @brief KCP 视频 Socket 就绪
     */
    void kcpVideoSocketReady(KcpVideoSocket* socket);

    /**
     * @brief KCP 控制 Socket 就绪
     */
    void kcpControlSocketReady(KcpControlSocket* socket);

    /**
     * @brief 连接错误
     */
    void error(const QString& message);

private slots:
    void onServerStarted(bool success, const QString& deviceName, const QSize& size);
    void onServerStopped();

private:
    void setState(ConnectionState state);
    void cleanup();

private:
    Server* m_server = nullptr;
    QString m_serial;
    ConnectionState m_state = ConnectionState::Disconnected;
    bool m_useKcp = false;

    // Socket 引用（由 Server 拥有）
    VideoSocket* m_videoSocket = nullptr;
    KcpVideoSocket* m_kcpVideoSocket = nullptr;
    KcpControlSocket* m_kcpControlSocket = nullptr;

    QSize m_frameSize;
};

} // namespace core
} // namespace qsc

#endif // CORE_CONNECTIONMANAGER_H
