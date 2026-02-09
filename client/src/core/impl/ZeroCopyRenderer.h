#ifndef CORE_ZEROCOPYRENDERER_H
#define CORE_ZEROCOPYRENDERER_H

#include "../interfaces/IRenderer.h"
#include "../infra/FrameQueue.h"
#include "qyuvopenglwidget.h"
#include <memory>
#include <atomic>

namespace qsc {
namespace core {

/**
 * @brief 零拷贝渲染器 / Zero-Copy Renderer
 *
 * 直接从 FrameQueue 消费帧并渲染，无需中间缓存拷贝。
 * Directly consumes frames from FrameQueue and renders without intermediate buffer copy.
 *
 * 特性 / Features:
 * - 继承 QYUVOpenGLWidget 获取 OpenGL 渲染能力 / Inherits OpenGL rendering
 * - 事件驱动：外部调用 onFrameReady() 触发 / Event-driven rendering
 * - 自动归还帧到 FramePool / Auto-returns frames to pool
 * - 支持 PBO 双缓冲 / PBO double-buffering support
 */
class ZeroCopyRenderer : public QYUVOpenGLWidget, public IRenderer {
    Q_OBJECT

public:
    explicit ZeroCopyRenderer(QWidget* parent = nullptr);
    ~ZeroCopyRenderer() override;

    // IRenderer 接口实现
    bool initialize() override { return true; }
    void setFrameSize(int width, int height) override;
    void renderFrame(const FrameData& frame) override;
    using IRenderer::renderFrame;  // 引入指针版本
    void updateTextures(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                       int linesizeY, int linesizeU, int linesizeV) override;
    bool grabFrame(uint8_t* outData, int* outWidth, int* outHeight) override;
    const char* name() const override { return "ZeroCopyRenderer"; }

    // 额外方法
    QImage grabCurrentFrame();
    bool isReady() const { return m_ready; }

    /**
     * @brief 设置帧队列（消费端）
     * @param queue 帧队列指针
     */
    void setFrameQueue(FrameQueue* queue);

public slots:
    /**
     * @brief 新帧可用时调用（事件驱动）
     *
     * 替代 QTimer 轮询，当 Decoder 有新帧时直接调用此方法。
     * 从 FrameQueue 取帧并渲染。
     */
    void onFrameReady();

signals:
    /**
     * @brief 帧消费完成信号
     */
    void frameConsumed();

private:
    void consumeAndRender();

private:
    FrameQueue* m_frameQueue = nullptr;
    FrameData* m_currentFrame = nullptr;  // 当前正在渲染的帧

    std::atomic<bool> m_ready{false};
};

} // namespace core
} // namespace qsc

#endif // CORE_ZEROCOPYRENDERER_H
