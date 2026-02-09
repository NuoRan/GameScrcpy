#ifndef CORE_OPENGLRENDERER_H
#define CORE_OPENGLRENDERER_H

#include "interfaces/IRenderer.h"
#include <QSize>
#include <QImage>

class QYUVOpenGLWidget;

namespace qsc {
namespace core {

/**
 * @brief OpenGL 渲染器适配器 / OpenGL Renderer Adapter
 *
 * 将 QYUVOpenGLWidget 适配为 IRenderer 接口。
 * Adapts QYUVOpenGLWidget to the IRenderer interface.
 * 支持 PBO 异步上传和 NV12 格式。
 * Supports PBO async upload and NV12 format.
 */
class OpenGLRenderer : public IRenderer {
public:
    /**
     * @brief 构造函数
     * @param widget 外部提供的 OpenGL Widget（生命周期由调用者管理）
     */
    explicit OpenGLRenderer(QYUVOpenGLWidget* widget);
    ~OpenGLRenderer() override = default;

    // IRenderer 实现
    bool initialize() override;
    void setFrameSize(int width, int height) override;
    void renderFrame(const FrameData& frame) override;
    using IRenderer::renderFrame;  // 引入指针版本
    void updateTextures(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                       int linesizeY, int linesizeU, int linesizeV) override;
    bool grabFrame(uint8_t* outData, int* outWidth, int* outHeight) override;
    const char* name() const override { return "OpenGL"; }

    // OpenGL 特有功能
    bool isPBOEnabled() const;
    void setPBOEnabled(bool enable);

    // 获取底层 Widget（用于 UI 集成）
    QYUVOpenGLWidget* widget() const { return m_widget; }

private:
    QYUVOpenGLWidget* m_widget;
    QSize m_frameSize;
    bool m_initialized = false;
};

} // namespace core
} // namespace qsc

#endif // CORE_OPENGLRENDERER_H
