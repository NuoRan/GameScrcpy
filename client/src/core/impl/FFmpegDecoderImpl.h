#ifndef CORE_FFMPEGDECODERIMPL_H
#define CORE_FFMPEGDECODERIMPL_H

#include "interfaces/IDecoder.h"
#include <memory>
#include <QString>

class Decoder;
struct AVPacket;

namespace qsc {
namespace core {

/**
 * @brief FFmpeg 解码器适配器 / FFmpeg Decoder Adapter
 *
 * 将现有的 Decoder 类适配为 IDecoder 接口。
 * Adapts the existing Decoder class to the IDecoder interface.
 * 支持 H.264 和 H.265/HEVC，优先使用硬件加速。
 * Supports H.264 and H.265/HEVC with hardware acceleration preferred.
 */
class FFmpegDecoderImpl : public IDecoder {
public:
    FFmpegDecoderImpl();
    ~FFmpegDecoderImpl() override;

    // IDecoder 实现
    bool open(int codecId) override;
    void close() override;
    bool decode(const uint8_t* data, int size, int64_t pts) override;
    void setFrameCallback(FrameCallback callback) override;
    bool isHardwareAccelerated() const override;
    const char* name() const override { return "FFmpeg"; }

    // 获取底层 Decoder（用于兼容旧代码）
    Decoder* decoder() const { return m_decoder.get(); }

    // 截图回调
    using ScreenshotCallback = std::function<void(int width, int height, uint8_t* dataRGB32)>;
    void peekFrame(ScreenshotCallback callback);

private:
    std::unique_ptr<Decoder> m_decoder;
    FrameCallback m_frameCallback;
    bool m_isOpen = false;
    mutable QString m_hwName;
    AVPacket* m_packet = nullptr;  // 复用 AVPacket，避免每帧 alloc/free
};

} // namespace core
} // namespace qsc

#endif // CORE_FFMPEGDECODERIMPL_H
