#ifndef CORE_IRENDERER_H
#define CORE_IRENDERER_H

#include <cstdint>

namespace qsc {
namespace core {

// 前向声明
struct FrameData;

/**
 * @brief 视频渲染器接口 / Video Renderer Interface
 *
 * 定义视频渲染器的通用接口，支持多种实现 / Generic renderer interface, multiple implementations:
 * - OpenGLRenderer: OpenGL 渲染 (当前) / OpenGL rendering (current)
 * - VulkanRenderer: Vulkan 渲染 (未来) / Vulkan rendering (future)
 * - SoftwareRenderer: 软件渲染 (兼容) / Software rendering (compatibility)
 */
class IRenderer {
public:
    virtual ~IRenderer() = default;

    /**
     * @brief 初始化渲染器
     * @return 成功返回 true
     */
    virtual bool initialize() = 0;

    /**
     * @brief 设置帧尺寸
     * @param width 帧宽度
     * @param height 帧高度
     */
    virtual void setFrameSize(int width, int height) = 0;

    /**
     * @brief 渲染一帧（主接口）
     * @param frame 帧数据引用 (YUV420P)
     *
     * 符合 REFACTOR_ANCHOR.md 3.4 节定义：
     * virtual void renderFrame(const FrameData& frame) = 0;
     */
    virtual void renderFrame(const FrameData& frame) = 0;

    /**
     * @brief 渲染一帧（指针版本，兼容）
     * @param frame 帧数据指针
     */
    virtual void renderFrame(const FrameData* frame) {
        if (frame) renderFrame(*frame);
    }

    /**
     * @brief 直接更新纹理数据 (兼容旧接口)
     * @deprecated 使用 renderFrame(const FrameData&) 替代
     */
    virtual void updateTextures(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                                int linesizeY, int linesizeU, int linesizeV) = 0;

    /**
     * @brief 抓取当前帧为图像
     * @return RGB 图像数据
     *
     * 符合 REFACTOR_ANCHOR.md 3.4 节定义：
     * virtual QImage grabCurrentFrame() = 0;
     *
     * 注意：此处使用 void 指针输出以避免 Qt 依赖
     */
    virtual bool grabFrame(uint8_t* outData, int* outWidth, int* outHeight) = 0;

    /**
     * @brief 获取渲染器名称 (用于调试)
     */
    virtual const char* name() const = 0;
};

} // namespace core
} // namespace qsc

#endif // CORE_IRENDERER_H
