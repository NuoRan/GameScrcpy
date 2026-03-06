#ifndef DECODER_H
#define DECODER_H

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavutil/hwcontext.h"
#include "libavutil/pixdesc.h"
}

#include <functional>
#include <cstdint>
#include <string>

class VideoBuffer;
// ---------------------------------------------------------
// 视频解码器 / Video Decoder (纯 C++，无 QObject)
// 基于 FFmpeg 的 H.264 解码，支持硬件加速
// FFmpeg-based H.264 decoding with hardware acceleration support
// ---------------------------------------------------------
class Decoder
{
public:
    using FrameCallback = std::function<void(int width, int height,
        uint8_t* dataY, uint8_t* dataU, uint8_t* dataV,
        int linesizeY, int linesizeU, int linesizeV)>;

    explicit Decoder(FrameCallback onFrame);
    ~Decoder();

    bool open(int codecId = 0);
    void close();
    bool push(const AVPacket *packet);
    void peekFrame(std::function<void(int width, int height, uint8_t* dataRGB32)> onFrame);

    // 硬解状态
    bool isHardwareAccelerated() const { return m_hwDeviceCtx != nullptr; }
    std::string hwDecoderName() const { return m_hwDecoderName; }

    /// 轮询 FPS（GUI 层每秒调用一次）
    uint32_t pollFps();
    uint32_t lastFps() const;

private:
    void pushFrame();
    void processFrame();
    bool initHardwareDecoder(const AVCodec* codec);
    static enum AVPixelFormat getHwFormat(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts);
    bool transferHwFrame(AVFrame* hwFrame, AVFrame* swFrame);

private:
    VideoBuffer *m_vb = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
    AVBufferRef *m_hwDeviceCtx = nullptr;
    AVFrame *m_hwFrame = nullptr;       // 硬件帧（GPU内存）
    AVFrame *m_swFrame = nullptr;       // 软件帧（CPU内存，用于转换）
    enum AVPixelFormat m_hwPixFmt = AV_PIX_FMT_NONE;
    bool m_isCodecCtxOpen = false;
    std::string m_hwDecoderName;
    FrameCallback m_onFrame;
};

#endif // DECODER_H
