#ifndef STREAM_H
#define STREAM_H

#include <QPointer>
#include <QSize>
#include <QThread>
#include <QElapsedTimer>
#include <functional>
#include <atomic>

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
}

class KcpVideoSocket;
class VideoSocket;

// 前向声明 IVideoChannel 接口
namespace qsc { namespace core { class IVideoChannel; } }

// ---------------------------------------------------------
// 解复用器 (Demuxer) / Video Demuxer
// 负责从网络读取 Scrcpy 协议流，解析出 H.264 数据包
// Reads Scrcpy protocol stream from network, parses H.264 data packets.
// 支持两种传输模式 / Supports two transport modes:
// - KCP (KcpVideoSocket) - WiFi 模式，低延迟 / WiFi mode, low latency
// - TCP (VideoSocket) - USB 模式 / USB mode via adb forward
// ---------------------------------------------------------
class Demuxer : public QThread
{
    Q_OBJECT
public:
    Demuxer(QObject *parent = Q_NULLPTR);
    virtual ~Demuxer();

public:
    static bool init();
    static void deInit();

    // KCP 模式 (WiFi，低延迟视频传输)
    void installKcpVideoSocket(KcpVideoSocket* kcpVideoSocket);

    // TCP 模式 (USB，通过 adb forward)
    void installVideoSocket(VideoSocket* videoSocket);

    // 【新架构】通过 IVideoChannel 接口安装视频通道
    void installVideoChannel(qsc::core::IVideoChannel* channel);

    void setFrameSize(const QSize &frameSize);
    bool startDecode();
    void stopDecode();

signals:
    void onStreamStop();
    // 发出完整的数据包给 Decoder
    void getFrame(AVPacket* packet);
    void getConfigFrame(AVPacket* packet);

protected:
    void run();
    bool recvPacket(AVPacket *packet);
    bool pushPacket(AVPacket *packet);
    bool processConfigPacket(AVPacket *packet);
    bool parse(AVPacket *packet);
    bool processFrame(AVPacket *packet);
    qint32 recvData(quint8 *buf, qint32 bufSize);

private:
    QPointer<KcpVideoSocket> m_kcpVideoSocket;
    QPointer<VideoSocket> m_videoSocket;
    qsc::core::IVideoChannel* m_videoChannel = nullptr;  // 新架构接口

    QSize m_frameSize;
    QElapsedTimer m_debugTimer;

    AVCodecContext *m_codecCtx = Q_NULLPTR;
    AVCodecParserContext *m_parser = Q_NULLPTR;
    AVPacket* m_pending = Q_NULLPTR; // 暂存包，用于处理 Config 包拼接

    // 停止标志 - 用于线程安全地通知停止
    std::atomic<bool> m_stopRequested{false};
};

#endif // STREAM_H
