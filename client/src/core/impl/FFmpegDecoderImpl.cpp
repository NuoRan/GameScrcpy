#include "FFmpegDecoderImpl.h"
#include "infra/FrameData.h"
#include "decoder.h"

extern "C" {
#include "libavcodec/avcodec.h"
}

namespace qsc {
namespace core {

FFmpegDecoderImpl::FFmpegDecoderImpl()
{
}

FFmpegDecoderImpl::~FFmpegDecoderImpl()
{
    close();
}

bool FFmpegDecoderImpl::open(int codecId)
{
    if (m_isOpen) {
        close();
    }

    // 创建 Decoder 实例，传入帧回调
    // Decoder 的回调是 YUV 格式的，我们需要转换为 FrameData
    auto yuvCallback = [this](int width, int height,
                              uint8_t* dataY, uint8_t* dataU, uint8_t* dataV,
                              int linesizeY, int linesizeU, int linesizeV) {
        if (m_frameCallback) {
            // 创建 FrameData 并调用回调
            FrameData frame;
            frame.width = width;
            frame.height = height;
            frame.dataY = dataY;
            frame.dataU = dataU;
            frame.dataV = dataV;
            frame.linesizeY = linesizeY;
            frame.linesizeU = linesizeU;
            frame.linesizeV = linesizeV;
            m_frameCallback(&frame);
        }
    };

    m_decoder = std::make_unique<Decoder>(yuvCallback);

    // 旧版解码器只支持 H.264
    AVCodecID avCodecId = static_cast<AVCodecID>(codecId);
    if (avCodecId != AV_CODEC_ID_H264) {
        qWarning("[FFmpegDecoderImpl] Only H.264 is supported, requested codec: %d", codecId);
        m_decoder.reset();
        return false;
    }

    if (m_decoder->open()) {
        m_isOpen = true;
        return true;
    }

    m_decoder.reset();
    return false;
}

void FFmpegDecoderImpl::close()
{
    if (m_packet) {
        av_packet_free(&m_packet);
        m_packet = nullptr;
    }
    if (m_decoder) {
        m_decoder->close();
        m_decoder.reset();
    }
    m_isOpen = false;
}

bool FFmpegDecoderImpl::decode(const uint8_t* data, int size, int64_t pts, int flags)
{
    Q_UNUSED(pts)
    Q_UNUSED(flags)

    if (!m_decoder || !m_isOpen) {
        return false;
    }

    // 复用 AVPacket，避免每帧 av_packet_alloc/free
    if (!m_packet) {
        m_packet = av_packet_alloc();
        if (!m_packet) return false;
    }

    // 注意：这里不复制数据，直接引用
    m_packet->data = const_cast<uint8_t*>(data);
    m_packet->size = size;

    bool result = m_decoder->push(m_packet);

    // 清理引用（不释放 packet 结构本身）
    m_packet->data = nullptr;
    m_packet->size = 0;

    return result;
}

void FFmpegDecoderImpl::setFrameCallback(FrameCallback callback)
{
    m_frameCallback = std::move(callback);
}

bool FFmpegDecoderImpl::isHardwareAccelerated() const
{
    return m_decoder ? m_decoder->isHardwareAccelerated() : false;
}

void FFmpegDecoderImpl::peekFrame(ScreenshotCallback callback)
{
    if (m_decoder) {
        m_decoder->peekFrame(callback);
    }
}

} // namespace core
} // namespace qsc
