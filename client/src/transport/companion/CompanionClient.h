/**
 * @file CompanionClient.h
 * @brief 手机伴侣 App TCP 客户端
 *
 * 连接到手机上的 GS Companion App，发送光标位置、请求截屏。
 * 光标服务端口 26758:
 *   PC → Phone: 0x01 [float x][float y]  光标位置 (0.0-1.0)
 *   PC → Phone: 0x03                     隐藏光标
 * 截屏服务端口 26759:
 *   PC → Phone: 0x02                     请求截屏
 *   Phone → PC: 0x82 [int32 len][JPEG]   截屏响应 (连接自动关闭)
 */
#ifndef COMPANIONCLIENT_H
#define COMPANIONCLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QImage>

class CompanionClient : public QObject {
    Q_OBJECT
public:
    static constexpr quint16 CURSOR_PORT = 26758;
    static constexpr quint16 SCREENSHOT_PORT = 26759;

    explicit CompanionClient(QObject* parent = nullptr);
    ~CompanionClient() override;

    void connectToDevice(const QString& ip, quint16 port = CURSOR_PORT);
    void disconnectFromDevice();
    bool isConnected() const;

    /// 发送光标位置 (归一化 0.0-1.0，显示方向坐标系)
    void sendCursorPos(float nx, float ny);

    /// 请求手机截屏 (通过临时连接到截屏端口)
    void requestScreenshot();

    /// 隐藏手机上的光标
    void hideCursor();

signals:
    void connected();
    void disconnected();
    void screenshotReceived(const QImage& image);
    void screenshotFailed();
    void errorOccurred(const QString& msg);

private slots:
    void onConnected();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError err);

private:
    void handleScreenshotData();
    void cleanupScreenshotSocket();

    QTcpSocket* m_socket = nullptr;
    QString m_deviceIp;

    // 截屏临时连接
    QTcpSocket* m_screenshotSocket = nullptr;
    QByteArray m_screenshotBuf;
    int m_screenshotExpectedLen = -1;
};

#endif // COMPANIONCLIENT_H
