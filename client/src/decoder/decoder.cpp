#include <QDebug>
#include <QDateTime>
#include <QThread>
#include <QMutex>

#include "compat.h"
#include "decoder.h"
#include "videobuffer.h"

extern "C" {
#include "libavutil/imgutils.h"
}

// 调试日志
#define DECODER_LOG(msg) qDebug() << "[Decoder]" << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") \
                                  << "[" << QThread::currentThreadId() << "]" << msg

/**
 * HW 优化: 硬件加速类型优先级
 * - Windows: D3D11VA > DXVA2 > CUDA (D3D11VA 延迟最低)
 * - macOS: VideoToolbox (唯一选择，性能最佳)
 * - Linux: VAAPI > VDPAU > CUDA
 */
static const AVHWDeviceType hwDeviceTypes[] = {
#ifdef _WIN32
    AV_HWDEVICE_TYPE_D3D11VA,   // HW: 优先 D3D11VA (Windows 8+)
    AV_HWDEVICE_TYPE_DXVA2,     // HW: 次选 DXVA2 (兼容 Windows 7)
    AV_HWDEVICE_TYPE_CUDA,
#elif defined(__APPLE__)
    AV_HWDEVICE_TYPE_VIDEOTOOLBOX,  // HW: macOS 唯一选择
#elif defined(__linux__)
    AV_HWDEVICE_TYPE_VAAPI,    // HW: 优先 VAAPI (Intel/AMD)
    AV_HWDEVICE_TYPE_VDPAU,
    AV_HWDEVICE_TYPE_CUDA,
#endif
    AV_HWDEVICE_TYPE_NONE
};

/**
 * P-02: 硬件解码器缓存
 * 避免每次创建 Decoder 时重复检测硬件能力
 */
struct HwDecoderCache {
    QMutex mutex;
    bool initialized = false;
    AVHWDeviceType cachedType = AV_HWDEVICE_TYPE_NONE;
    AVPixelFormat cachedPixFmt = AV_PIX_FMT_NONE;
    QString cachedName;

    // P-02: 检测并缓存可用的硬件解码器
    void detectOnce(AVCodecID codecId) {
        QMutexLocker locker(&mutex);
        if (initialized) return;
        initialized = true;

        const AVCodec* codec = avcodec_find_decoder(codecId);
        if (!codec) return;

        for (int i = 0; hwDeviceTypes[i] != AV_HWDEVICE_TYPE_NONE; i++) {
            AVHWDeviceType type = hwDeviceTypes[i];

            for (int j = 0;; j++) {
                const AVCodecHWConfig *config = avcodec_get_hw_config(codec, j);
                if (!config) break;

                if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                    config->device_type == type) {

                    AVBufferRef *testCtx = nullptr;
                    int ret = av_hwdevice_ctx_create(&testCtx, type, nullptr, nullptr, 0);
                    if (ret >= 0) {
                        cachedType = type;
                        cachedPixFmt = config->pix_fmt;
                        cachedName = QString::fromUtf8(av_hwdevice_get_type_name(type));
                        av_buffer_unref(&testCtx);
                        qInfo("P-02: Cached hardware decoder: %s for codec %s",
                              qPrintable(cachedName), avcodec_get_name(codecId));
                        return;
                    }
                }
            }
        }
        qInfo("P-02: No hardware decoder available for codec %s", avcodec_get_name(codecId));
    }
};

// P-02: 全局缓存 (H.264 和 H.265 分开缓存)
static HwDecoderCache s_h264Cache;
static HwDecoderCache s_h265Cache;

// 静态成员：存储当前解码器的硬件像素格式
static AVPixelFormat s_hwPixFmt = AV_PIX_FMT_NONE;

// ---------------------------------------------------------
// 构造与析构
// ---------------------------------------------------------
Decoder::Decoder(std::function<void(int, int, uint8_t*, uint8_t*, uint8_t*, int, int, int)> onFrame, QObject *parent)
    : QObject(parent)
    , m_vb(new VideoBuffer())
    , m_onFrame(onFrame)
{
    m_vb->init();
    connect(this, &Decoder::newFrame, this, &Decoder::onNewFrame, Qt::QueuedConnection);
    connect(m_vb, &VideoBuffer::updateFPS, this, &Decoder::updateFPS);
}

Decoder::~Decoder() {
    close();
    m_vb->deInit();
    delete m_vb;
}

// ---------------------------------------------------------
// 硬件格式回调
// ---------------------------------------------------------
enum AVPixelFormat Decoder::getHwFormat(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
    Q_UNUSED(ctx);
    for (const enum AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == s_hwPixFmt) {
            return *p;
        }
    }
    qWarning("Failed to get HW surface format, falling back to software");
    return AV_PIX_FMT_NONE;
}

// ---------------------------------------------------------
// P-02: 初始化硬件解码器 (使用缓存)
// ---------------------------------------------------------
bool Decoder::initHardwareDecoder(const AVCodec* codec)
{
    // P-02: 根据编码类型选择缓存
    HwDecoderCache *cache = nullptr;
    if (codec->id == AV_CODEC_ID_H264) {
        cache = &s_h264Cache;
    } else if (codec->id == AV_CODEC_ID_HEVC) {
        cache = &s_h265Cache;
    }

    // P-02: 使用缓存的硬件解码器信息
    if (cache && cache->initialized && cache->cachedType != AV_HWDEVICE_TYPE_NONE) {
        int ret = av_hwdevice_ctx_create(&m_hwDeviceCtx, cache->cachedType, nullptr, nullptr, 0);
        if (ret >= 0) {
            m_hwPixFmt = cache->cachedPixFmt;
            s_hwPixFmt = m_hwPixFmt;
            m_hwDecoderName = cache->cachedName;
            qInfo("P-02: Using cached hardware decoder: %s (format: %s)",
                  qPrintable(m_hwDecoderName),
                  av_get_pix_fmt_name(m_hwPixFmt));
            return true;
        }
    }

    // 回退到完整检测
    for (int i = 0; hwDeviceTypes[i] != AV_HWDEVICE_TYPE_NONE; i++) {
        AVHWDeviceType type = hwDeviceTypes[i];

        for (int j = 0;; j++) {
            const AVCodecHWConfig *config = avcodec_get_hw_config(codec, j);
            if (!config) {
                break;
            }
            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == type) {

                int ret = av_hwdevice_ctx_create(&m_hwDeviceCtx, type, nullptr, nullptr, 0);
                if (ret >= 0) {
                    m_hwPixFmt = config->pix_fmt;
                    s_hwPixFmt = m_hwPixFmt;
                    m_hwDecoderName = QString::fromUtf8(av_hwdevice_get_type_name(type));
                    qInfo("Hardware decoder initialized: %s (format: %s)",
                          qPrintable(m_hwDecoderName),
                          av_get_pix_fmt_name(m_hwPixFmt));
                    return true;
                }
            }
        }
    }
    return false;
}

// ---------------------------------------------------------
// 硬件帧转软件帧
// ---------------------------------------------------------
bool Decoder::transferHwFrame(AVFrame* hwFrame, AVFrame* swFrame)
{
    if (!hwFrame || !swFrame) {
        return false;
    }

    // 从 GPU 内存复制到 CPU 内存
    int ret = av_hwframe_transfer_data(swFrame, hwFrame, 0);
    if (ret < 0) {
        char errorbuf[256];
        av_strerror(ret, errorbuf, sizeof(errorbuf));
        qWarning("Error transferring HW frame: %s", errorbuf);
        return false;
    }

    // 复制时间戳等元数据
    swFrame->pts = hwFrame->pts;
    swFrame->pkt_dts = hwFrame->pkt_dts;

    return true;
}

// ---------------------------------------------------------
// H265: 初始化解码器 (支持 H.264 和 H.265)
// ---------------------------------------------------------
bool Decoder::open()
{
    return open(AV_CODEC_ID_H264);  // 默认 H.264
}

bool Decoder::open(AVCodecID codecId)
{
    // H265: 支持 H.264 和 H.265
    const char* codecName = (codecId == AV_CODEC_ID_HEVC) ? "H.265/HEVC" : "H.264";

    // P-02: 预先检测并缓存硬件解码器
    if (codecId == AV_CODEC_ID_H264) {
        s_h264Cache.detectOnce(codecId);
    } else if (codecId == AV_CODEC_ID_HEVC) {
        s_h265Cache.detectOnce(codecId);
    }

    // 查找解码器
    const AVCodec* codec = avcodec_find_decoder(codecId);
    if (!codec) {
        qCritical("%s decoder not found!", codecName);
        return false;
    }

    // 分配上下文
    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        qCritical("Could not allocate decoder context");
        return false;
    }

    // 尝试初始化硬件解码
    bool hwEnabled = initHardwareDecoder(codec);
    if (hwEnabled) {
        m_codecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
        m_codecCtx->get_format = getHwFormat;

        // 分配硬件帧和软件帧
        m_hwFrame = av_frame_alloc();
        m_swFrame = av_frame_alloc();
        if (!m_hwFrame || !m_swFrame) {
            qCritical("Could not allocate HW/SW frames");
            return false;
        }
    } else {
        qInfo("Hardware decoding not available for %s, using software decoder", codecName);
    }

    // 低延迟设置
    m_codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    m_codecCtx->flags2 |= AV_CODEC_FLAG2_FAST;

    // H265: 额外的低延迟选项
    if (codecId == AV_CODEC_ID_HEVC) {
        // HEVC 特定优化
        m_codecCtx->thread_count = 4;  // 多线程解码
    }

    // 打开解码器
    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        qCritical("Could not open %s codec", codecName);
        return false;
    }

    m_isCodecCtxOpen = true;
    m_codecId = codecId;

    if (hwEnabled) {
        qInfo("Decoder opened with hardware acceleration: %s (%s)",
              qPrintable(m_hwDecoderName), codecName);
    } else {
        qInfo("Decoder opened with software decoding (%s)", codecName);
    }

    return true;
}

// ---------------------------------------------------------
// 关闭解码器
// ---------------------------------------------------------
void Decoder::close()
{
    if (m_vb) {
        m_vb->interrupt();
    }

    if (m_hwFrame) {
        av_frame_free(&m_hwFrame);
        m_hwFrame = nullptr;
    }
    if (m_swFrame) {
        av_frame_free(&m_swFrame);
        m_swFrame = nullptr;
    }
    if (m_hwDeviceCtx) {
        av_buffer_unref(&m_hwDeviceCtx);
        m_hwDeviceCtx = nullptr;
    }

    if (m_codecCtx) {
        if (m_isCodecCtxOpen) {
            avcodec_close(m_codecCtx);
        }
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }

    m_isCodecCtxOpen = false;
    m_hwPixFmt = AV_PIX_FMT_NONE;
    s_hwPixFmt = AV_PIX_FMT_NONE;
}

// ---------------------------------------------------------
// 推送数据包进行解码
// ---------------------------------------------------------
bool Decoder::push(const AVPacket *packet)
{
    if (!m_codecCtx || !m_vb) {
        return false;
    }

#ifdef QTSCRCPY_LAVF_HAS_NEW_ENCODING_DECODING_API
    int ret = avcodec_send_packet(m_codecCtx, packet);
    if (ret < 0) {
        char errorbuf[256];
        av_strerror(ret, errorbuf, sizeof(errorbuf));
        qCritical("Could not send video packet: %s", errorbuf);
        return false;
    }

    // 根据是否使用硬解选择不同的接收帧
    AVFrame *receiveFrame = m_hwDeviceCtx ? m_hwFrame : m_vb->decodingFrame();
    if (!receiveFrame) {
        return false;
    }

    ret = avcodec_receive_frame(m_codecCtx, receiveFrame);
    if (ret == 0) {
        // 成功解码一帧
        if (m_hwDeviceCtx) {
            // 硬解：需要从 GPU 转换到 CPU
            AVFrame *decodingFrame = m_vb->decodingFrame();
            if (decodingFrame && transferHwFrame(m_hwFrame, decodingFrame)) {
                pushFrame();
            }
            av_frame_unref(m_hwFrame);
        } else {
            // 软解：直接使用
            pushFrame();
        }
    } else if (ret != AVERROR(EAGAIN)) {
        char errorbuf[256];
        av_strerror(ret, errorbuf, sizeof(errorbuf));
        qCritical("Could not receive video frame: %s", errorbuf);
        return false;
    }
#else
    // 旧版 FFmpeg API（不支持硬解）
    AVFrame *decodingFrame = m_vb->decodingFrame();
    int gotPicture = 0;
    int len = -1;
    if (decodingFrame) {
        len = avcodec_decode_video2(m_codecCtx, decodingFrame, &gotPicture, packet);
    }
    if (len < 0) {
        qCritical("Could not decode video packet: %d", len);
        return false;
    }
    if (gotPicture) {
        pushFrame();
    }
#endif
    return true;
}

// ---------------------------------------------------------
// 帧处理逻辑
// ---------------------------------------------------------
void Decoder::peekFrame(std::function<void (int, int, uint8_t *)> onFrame)
{
    if (!m_vb) {
        return;
    }
    m_vb->peekRenderedFrame(onFrame);
}

void Decoder::pushFrame()
{
    if (!m_vb) {
        return;
    }
    bool previousFrameSkipped = true;
    m_vb->offerDecodedFrame(previousFrameSkipped);
    if (previousFrameSkipped) {
        return;
    }
    emit newFrame();
}

void Decoder::onNewFrame() {
    if (!m_onFrame) {
        return;
    }

    m_vb->lock();
    const AVFrame *frame = m_vb->consumeRenderedFrame();
    if (frame) {
        m_onFrame(frame->width, frame->height,
                  frame->data[0], frame->data[1], frame->data[2],
                  frame->linesize[0], frame->linesize[1], frame->linesize[2]);
    }
    m_vb->unLock();
}
