#ifndef IVIDEORENDERER_H
#define IVIDEORENDERER_H

#include <QObject>
#include <QSize>
#include <QImage>
#include <functional>
#include <memory>
#include <vector>

#include "ErrorCode.h"

namespace qsc {

/**
 * @brief 渲染器状态 / Renderer State
 */
enum class RendererState
{
    Uninitialized,  // 未初始化 / Uninitialized
    Ready,          // 就绪 / Ready
    Rendering,      // 渲染中 / Rendering
    Paused,         // 暂停 / Paused
    Error           // 错误 / Error
};

/**
 * @brief 渲染器类型 / Renderer Type
 */
enum class RendererType
{
    OpenGL,         // OpenGL 渲染 / OpenGL rendering
    OpenGLES,       // OpenGL ES 渲染 / OpenGL ES rendering
    D3D11,          // Direct3D 11 渲染 / Direct3D 11 rendering
    Vulkan,         // Vulkan 渲染 / Vulkan rendering
    Software        // 软件渲染 / Software rendering
};

/**
 * @brief 像素格式 / Pixel Format
 */
enum class PixelFormat
{
    YUV420P,        // YUV 4:2:0 平面格式 / Planar format
    NV12,           // YUV 4:2:0 半平面格式 / Semi-planar format
    RGB24,          // RGB 24位 / 24-bit
    RGBA32,         // RGBA 32位 / 32-bit
    BGRA32          // BGRA 32位 / 32-bit
};

/**
 * @brief 渲染器配置
 */
struct RendererConfig
{
    RendererType preferredType = RendererType::OpenGL;
    bool enableVSync = true;            // 垂直同步
    bool enablePBO = true;              // 使用 PBO 异步上传
    int pboCount = 2;                   // PBO 数量（双缓冲）
    bool dirtyRegionUpdate = false;     // 脏区域更新（实验性）
};

/**
 * @brief YUV帧数据结构
 */
struct YUVFrame
{
    uint8_t* dataY = nullptr;
    uint8_t* dataU = nullptr;
    uint8_t* dataV = nullptr;
    int linesizeY = 0;
    int linesizeU = 0;
    int linesizeV = 0;
    int width = 0;
    int height = 0;
    int64_t timestamp = 0;              // 时间戳（微秒）
};

/**
 * @brief 渲染统计信息
 */
struct RenderStats
{
    quint32 fps = 0;                    // 当前帧率
    quint32 droppedFrames = 0;          // 丢帧数
    quint64 totalFrames = 0;            // 总帧数
    double avgRenderTime = 0;           // 平均渲染时间（毫秒）
    double avgUploadTime = 0;           // 平均上传时间（毫秒）
    bool pboEnabled = false;            // PBO 是否启用
};

/**
 * @brief 视频渲染器抽象接口
 *
 * 定义视频渲染的标准接口，支持 OpenGL、Direct3D 等多种后端。
 * 提供异步纹理上传（PBO）、脏区域更新等优化功能。
 */
class IVideoRenderer
{
public:
    virtual ~IVideoRenderer() = default;

    // =========================================================================
    // 生命周期管理
    // =========================================================================

    /**
     * @brief 初始化渲染器
     * @param config 渲染器配置
     * @return 操作结果
     */
    virtual VoidResult initialize(const RendererConfig& config = RendererConfig()) = 0;

    /**
     * @brief 销毁渲染器资源
     */
    virtual void destroy() = 0;

    /**
     * @brief 检查是否已初始化
     */
    virtual bool isInitialized() const = 0;

    // =========================================================================
    // 帧管理
    // =========================================================================

    /**
     * @brief 设置视频帧尺寸
     * @param size 帧尺寸
     */
    virtual void setFrameSize(const QSize& size) = 0;

    /**
     * @brief 获取当前帧尺寸
     */
    virtual QSize frameSize() const = 0;

    /**
     * @brief 更新 YUV 纹理数据
     * @param frame YUV帧数据
     * @return 是否成功
     */
    virtual bool updateTextures(const YUVFrame& frame) = 0;

    /**
     * @brief 使用传统方式更新纹理（兼容旧接口）
     */
    virtual void updateTextures(
        uint8_t* dataY, uint8_t* dataU, uint8_t* dataV,
        uint32_t linesizeY, uint32_t linesizeU, uint32_t linesizeV
    ) = 0;

    // =========================================================================
    // 帧获取
    // =========================================================================

    /**
     * @brief 获取当前帧的 RGB 图像
     * @return RGB 图像
     */
    virtual QImage grabCurrentFrame() = 0;

    /**
     * @brief 获取当前帧的灰度数据（用于图像识别）
     * @return 灰度数据
     */
    virtual std::vector<uint8_t> grabCurrentFrameGrayscale() = 0;

    // =========================================================================
    // 状态与统计
    // =========================================================================

    /**
     * @brief 获取渲染器状态
     */
    virtual RendererState state() const = 0;

    /**
     * @brief 获取渲染器类型
     */
    virtual RendererType type() const = 0;

    /**
     * @brief 获取渲染统计信息
     */
    virtual RenderStats stats() const = 0;

    /**
     * @brief 是否支持 PBO
     */
    virtual bool supportsPBO() const = 0;

    /**
     * @brief PBO 是否启用
     */
    virtual bool isPBOEnabled() const = 0;
};

/**
 * @brief 渲染器工厂接口
 */
class IVideoRendererFactory
{
public:
    virtual ~IVideoRendererFactory() = default;

    /**
     * @brief 创建渲染器实例
     * @param type 渲染器类型
     * @param parent 父 QWidget（对于 Qt 渲染器）
     * @return 渲染器实例
     */
    virtual std::unique_ptr<IVideoRenderer> createRenderer(
        RendererType type = RendererType::OpenGL,
        void* parent = nullptr
    ) = 0;

    /**
     * @brief 获取可用的渲染器类型列表
     */
    virtual std::vector<RendererType> availableRenderers() const = 0;

    /**
     * @brief 获取推荐的渲染器类型
     */
    virtual RendererType recommendedRenderer() const = 0;
};

} // namespace qsc

#endif // IVIDEORENDERER_H
