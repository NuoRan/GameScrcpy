#ifndef IDECODER_H
#define IDECODER_H

#include <QObject>
#include <QSize>
#include <functional>
#include <memory>

#include "ErrorCode.h"

// Forward declarations
struct AVPacket;
struct AVFrame;

namespace qsc {

/**
 * @brief 解码器状态枚举
 */
enum class DecoderState
{
    Idle,           // 空闲
    Opening,        // 正在打开
    Ready,          // 就绪
    Decoding,       // 解码中
    Error,          // 错误
    Closed          // 已关闭
};

/**
 * @brief 解码器类型
 */
enum class DecoderType
{
    Software,       // 软件解码
    Hardware,       // 硬件解码 (自动选择)
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
struct DecoderConfig
{
    DecoderType preferredType = DecoderType::Hardware;
    bool allowFallback = true;          // 允许回退到软件解码
    int threadCount = 0;                // 解码线程数，0为自动
    bool lowLatency = true;             // 低延迟模式
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
 * @brief 解码器抽象接口
 *
 * 定义解码器的标准接口，支持软件解码和硬件加速解码。
 * 实现类需要处理 AVPacket 输入并输出 YUV 帧数据。
 */
class IDecoder : public QObject
{
    Q_OBJECT

public:
    explicit IDecoder(QObject* parent = nullptr) : QObject(parent) {}
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
    virtual QString hardwareDecoderName() const = 0;

    /**
     * @brief 获取当前解码分辨率
     */
    virtual QSize frameSize() const = 0;

signals:
    /**
     * @brief 状态变化信号
     */
    void stateChanged(DecoderState newState, DecoderState oldState);

    /**
     * @brief FPS 更新信号
     */
    void fpsUpdated(quint32 fps);

    /**
     * @brief 硬件解码器回退信号
     * @param reason 回退原因
     */
    void hardwareDecoderFallback(const QString& reason);

    /**
     * @brief 解码错误信号
     */
    void decoderError(ErrorCode code, const QString& message);
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
        FrameCallback callback,
        QObject* parent = nullptr
    ) = 0;

    /**
     * @brief 获取可用的硬件解码器列表
     */
    virtual QStringList availableHardwareDecoders() const = 0;
};

} // namespace qsc

#endif // IDECODER_H
