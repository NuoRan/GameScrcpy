#include <QDebug>

#include "compat.h"
#include "decoder.h"
#include "videobuffer.h"

extern "C" {
#include "libavutil/imgutils.h"
}

// 硬件加速类型优先级（Windows: D3D11VA > DXVA2 > CUDA）
static const AVHWDeviceType hwDeviceTypes[] = {
#ifdef _WIN32
    AV_HWDEVICE_TYPE_D3D11VA,
    AV_HWDEVICE_TYPE_DXVA2,
    AV_HWDEVICE_TYPE_CUDA,
#elif defined(__APPLE__)
    AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
#elif defined(__linux__)
    AV_HWDEVICE_TYPE_VAAPI,
    AV_HWDEVICE_TYPE_VDPAU,
    AV_HWDEVICE_TYPE_CUDA,
#endif
    AV_HWDEVICE_TYPE_NONE
};

// 静态成员：存储当前解码器的硬件像素格式
static AVPixelFormat s_hwPixFmt = AV_PIX_FMT_NONE;

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

// 硬件格式回调
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

// 初始化硬件解码器
bool Decoder::initHardwareDecoder(const AVCodec* codec)
{
    for (int i = 0; hwDeviceTypes[i] != AV_HWDEVICE_TYPE_NONE; i++) {
        AVHWDeviceType type = hwDeviceTypes[i];

        // 检查编解码器是否支持此硬件类型
        for (int j = 0;; j++) {
            const AVCodecHWConfig *config = avcodec_get_hw_config(codec, j);
            if (!config) {
                break;
            }
            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == type) {

                // 尝试创建硬件设备上下文
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
// 初始化解码器
// ---------------------------------------------------------
bool Decoder::open()
{
    // 查找 H.264 解码器
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        qCritical("H.264 decoder not found");
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
        qInfo("Hardware decoding not available, using software decoder");
    }

    // 低延迟设置
    m_codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    m_codecCtx->flags2 |= AV_CODEC_FLAG2_FAST;

    // 打开解码器
    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        qCritical("Could not open H.264 codec");
        return false;
    }

    m_isCodecCtxOpen = true;

    if (hwEnabled) {
        qInfo("Decoder opened with hardware acceleration: %s", qPrintable(m_hwDecoderName));
    } else {
        qInfo("Decoder opened with software decoding");
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

#ifdef KZSCRCPY_LAVF_HAS_NEW_ENCODING_DECODING_API
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
        m_onFrame(frame->width, frame->height,
                  frame->data[0], frame->data[1], frame->data[2],
                  frame->linesize[0], frame->linesize[1], frame->linesize[2]);
    m_vb->unLock();
}
