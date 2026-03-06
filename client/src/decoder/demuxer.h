#ifndef DEMUXER_H
#define DEMUXER_H


#include <cstdint>
#include <string>
#include <functional>
#include <atomic>
#include <thread>

#include "GameSignal.h"
#include "GameTypes.h"

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
class Demuxer
{
public:
    Demuxer();
    ~Demuxer();

public:
    static bool init();
    static void deInit();

    // KCP 模式 (WiFi，低延迟视频传输)
    void installKcpVideoSocket(KcpVideoSocket* kcpVideoSocket);

    // TCP 模式 (USB，通过 adb forward)
    void installVideoSocket(VideoSocket* videoSocket);

    // 通过 IVideoChannel 接口安装视频通道
    void installVideoChannel(qsc::core::IVideoChannel* channel);

    void setFrameSize(const Size &frameSize);
    void setVideoCodec(const std::string &codec);
    bool startDecode();
    void stopDecode();

    // 信号 / Signals
    Signal<> onStreamStop;
    Signal<AVPacket*> getFrame;
    Signal<AVPacket*> getConfigFrame;

private:
    void run();
    bool recvPacket(AVPacket *packet);
    bool pushPacket(AVPacket *packet);
    bool processConfigPacket(AVPacket *packet);
    bool parse(AVPacket *packet);
    bool processFrame(AVPacket *packet);
    int32_t recvData(uint8_t *buf, int32_t bufSize);

private:
    KcpVideoSocket* m_kcpVideoSocket = nullptr;
    VideoSocket* m_videoSocket = nullptr;
    qsc::core::IVideoChannel* m_videoChannel = nullptr;  // 新架构接口

    Size m_frameSize;
    std::string m_videoCodec = "h264";

    AVCodecContext* m_codecCtx = nullptr;
    AVCodecParserContext* m_parser = nullptr;
    AVPacket* m_pending = nullptr; // 暂存包，用于处理 Config 包拼接
    bool m_mustMergeConfig = true;   // H.264/H.265 需要合并 config

    // 停止标志 - 用于线程安全地通知停止
    std::atomic<bool> m_stopRequested{false};

    // 工作线程 (替代 QThread 继承)
    std::thread m_thread;
};

#endif // DEMUXER_H
