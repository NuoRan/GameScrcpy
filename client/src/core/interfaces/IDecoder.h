#ifndef CORE_IDECODER_H
#define CORE_IDECODER_H

#include <functional>
#include <cstdint>

namespace qsc {
namespace core {

// 前向声明
struct FrameData;

/**
 * @brief 解码器接口 / Decoder Interface
 *
 * 定义视频解码器的通用接口，支持多种实现。
 * Defines a generic video decoder interface with multiple implementations.
 * - FFmpegDecoder: 软解 (FFmpeg) / Software decoding
 * - HardwareDecoder: 硬解 (MediaCodec/VideoToolbox) / Hardware decoding
 */
class IDecoder {
public:
    virtual ~IDecoder() = default;

    /**
     * @brief 帧回调函数类型 / Frame callback type
     * 解码出一帧后调用 / Called after decoding a frame
     */
    using FrameCallback = std::function<void(FrameData* frame)>;

    /**
     * @brief 打开解码器
     * @param codecId 编码格式 ID (使用 FFmpeg 的 AVCodecID 值)
     * @return 成功返回 true
     */
    virtual bool open(int codecId) = 0;

    /**
     * @brief 关闭解码器
     */
    virtual void close() = 0;

    /**
     * @brief 解码一帧数据
     * @param data 编码数据
     * @param size 数据大小
     * @param pts 时间戳 (微秒)
     * @return 成功返回 true
     */
    virtual bool decode(const uint8_t* data, int size, int64_t pts, int flags = 0) = 0;

    /**
     * @brief 设置帧回调
     * @param callback 解码完成后的回调函数
     */
    virtual void setFrameCallback(FrameCallback callback) = 0;

    /**
     * @brief 是否使用硬件加速
     */
    virtual bool isHardwareAccelerated() const = 0;

    /**
     * @brief 获取解码器名称 (用于调试)
     */
    virtual const char* name() const = 0;
};

} // namespace core
} // namespace qsc

#endif // CORE_IDECODER_H
