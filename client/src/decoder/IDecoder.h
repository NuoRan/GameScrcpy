#ifndef IDECODER_H
#define IDECODER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ErrorCode.h"
#include "GameSignal.h"
#include "GameTypes.h"

// Forward declarations
struct AVPacket;
struct AVFrame;

namespace qsc {

/**
 * @brief 解码器状态枚举 / Decoder State Enumeration
 */
enum class DecoderState
{
    Idle,           // 空闲 / Idle
    Opening,        // 正在打开 / Opening
    Ready,          // 就绪 / Ready
    Decoding,       // 解码中 / Decoding
    Error,          // 错误 / Error
    Closed          // 已关闭 / Closed
};

/**
 * @brief 解码器类型 / Decoder Type
 */
enum class DecoderType
{
    Software,       // 软件解码 / Software decoding
    Hardware,       // 硬件解码 (自动选择) / Hardware (auto-select)
    DXVA2,          // Windows DXVA2
    D3D11VA,        // Windows D3D11
    VAAPI,          // Linux VA-API
    VDPAU,          // Linux VDPAU
    VideoToolbox,   // macOS VideoToolbox
    CUDA,           // NVIDIA CUDA
    QSV             // Intel Quick Sync
};

/**
 * @brief 解码器配置
 */
/**
 * @brief 解码器配置 / Decoder Configuration
 */
struct DecoderConfig
{
    DecoderType preferredType = DecoderType::Hardware;
    bool allowFallback = true;          // 允许回退到软件解码 / Allow fallback to software decoding
    int threadCount = 0;                // 解码线程数，0为自动 / Decode threads, 0=auto
    bool lowLatency = true;             // 低延迟模式 / Low-latency mode
};

/**
 * @brief 帧回调函数类型
 * @param width 帧宽度
 * @param height 帧高度
 * @param dataY Y分量数据
 * @param dataU U分量数据
 * @param dataV V分量数据
 * @param linesizeY Y分量行大小
 * @param linesizeU U分量行大小
 * @param linesizeV V分量行大小
 */
using FrameCallback = std::function<void(
    int width, int height,
    uint8_t* dataY, uint8_t* dataU, uint8_t* dataV,
    int linesizeY, int linesizeU, int linesizeV
)>;

/**
 * @brief RGB帧回调函数类型
 */
using RGBFrameCallback = std::function<void(int width, int height, uint8_t* dataRGB32)>;

/**
 * @brief 解码器抽象接口 / Decoder Abstract Interface
 *
 * 定义解码器的标准接口，支持软件解码和硬件加速解码。
 * Standard decoder interface supporting software and hardware-accelerated decoding.
 */
class IDecoder
{
public:
    IDecoder() = default;
    virtual ~IDecoder() = default;

    // =========================================================================
    // 生命周期管理
    // =========================================================================

    /**
     * @brief 打开解码器
     * @param config 解码器配置
     * @return 操作结果
     */
    virtual VoidResult open(const DecoderConfig& config = DecoderConfig()) = 0;

    /**
     * @brief 关闭解码器
     */
    virtual void close() = 0;

    /**
     * @brief 检查解码器是否已打开
     */
    virtual bool isOpen() const = 0;

    // =========================================================================
    // 解码操作
    // =========================================================================

    /**
     * @brief 推送数据包进行解码
     * @param packet 编码数据包
     * @return 是否成功
     */
    virtual bool push(const AVPacket* packet) = 0;

    /**
     * @brief 刷新解码器（用于获取缓存的帧）
     */
    virtual void flush() = 0;

    // =========================================================================
    // 回调设置
    // =========================================================================

    /**
     * @brief 设置帧回调函数
     */
    virtual void setFrameCallback(FrameCallback callback) = 0;

    /**
     * @brief 获取当前帧（用于截图等）
     */
    virtual void peekFrame(RGBFrameCallback callback) = 0;

    // =========================================================================
    // 状态查询
    // =========================================================================

    /**
     * @brief 获取当前状态
     */
    virtual DecoderState state() const = 0;

    /**
     * @brief 是否使用硬件加速
     */
    virtual bool isHardwareAccelerated() const = 0;

    /**
     * @brief 获取硬件解码器名称
     */
    virtual std::string hardwareDecoderName() const = 0;

    /**
     * @brief 获取当前解码分辨率
     */
    virtual Size frameSize() const = 0;

    // 信号 / Signals
    Signal<DecoderState, DecoderState> stateChanged;
    Signal<uint32_t> fpsUpdated;
    Signal<const std::string&> hardwareDecoderFallback;
    Signal<ErrorCode, const std::string&> decoderError;
};

/**
 * @brief 解码器工厂接口
 */
class IDecoderFactory
{
public:
    virtual ~IDecoderFactory() = default;

    /**
     * @brief 创建解码器实例
     * @param callback 帧回调函数
     * @param parent 父对象
     * @return 解码器实例
     */
    virtual std::unique_ptr<IDecoder> createDecoder(
        FrameCallback callback
    ) = 0;

    /**
     * @brief 获取可用的硬件解码器列表
     */
    virtual std::vector<std::string> availableHardwareDecoders() const = 0;
};

} // namespace qsc

#endif // IDECODER_H
