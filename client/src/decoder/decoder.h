#ifndef DECODER_H
#define DECODER_H
#include <QObject>

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavutil/hwcontext.h"
#include "libavutil/pixdesc.h"
}

#include <functional>

class VideoBuffer;

/**
 * Decoder - 视频解码器
 *
 * 优化特性:
 * - H265: 支持 H.264 和 H.265/HEVC 编码
 * - HW: VideoToolbox/DXVA2 优先的硬解策略
 * - P-02: 缓存硬件解码器类型避免重复检测
 */
class Decoder : public QObject
{
    Q_OBJECT
public:
    Decoder(std::function<void(int width, int height, uint8_t* dataY, uint8_t* dataU, uint8_t* dataV, int linesizeY, int linesizeU, int linesizeV)> onFrame, QObject *parent = Q_NULLPTR);
    virtual ~Decoder();

    // H265: 支持指定编码类型
    bool open();                              // 默认 H.264
    bool open(AVCodecID codecId);             // 指定编码 (AV_CODEC_ID_H264 或 AV_CODEC_ID_HEVC)
    void close();
    bool push(const AVPacket *packet);
    void peekFrame(std::function<void(int width, int height, uint8_t* dataRGB32)> onFrame);

    // 硬解状态
    bool isHardwareAccelerated() const { return m_hwDeviceCtx != nullptr; }
    QString hwDecoderName() const { return m_hwDecoderName; }

    // H265: 获取当前编码类型
    AVCodecID codecId() const { return m_codecId; }
    bool isHEVC() const { return m_codecId == AV_CODEC_ID_HEVC; }

signals:
    void updateFPS(quint32 fps);

private slots:
    void onNewFrame();

signals:
    void newFrame();

private:
    void pushFrame();
    bool initHardwareDecoder(const AVCodec* codec);
    static enum AVPixelFormat getHwFormat(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts);
    bool transferHwFrame(AVFrame* hwFrame, AVFrame* swFrame);

private:
    VideoBuffer *m_vb = Q_NULLPTR;
    AVCodecContext *m_codecCtx = Q_NULLPTR;
    AVBufferRef *m_hwDeviceCtx = Q_NULLPTR;
    AVFrame *m_hwFrame = Q_NULLPTR;       // 硬件帧（GPU内存）
    AVFrame *m_swFrame = Q_NULLPTR;       // 软件帧（CPU内存，用于转换）
    enum AVPixelFormat m_hwPixFmt = AV_PIX_FMT_NONE;
    bool m_isCodecCtxOpen = false;
    QString m_hwDecoderName;
    AVCodecID m_codecId = AV_CODEC_ID_H264;  // H265: 当前编码类型
    std::function<void(int, int, uint8_t*, uint8_t*, uint8_t*, int, int, int)> m_onFrame = Q_NULLPTR;
};

#endif // DECODER_H

