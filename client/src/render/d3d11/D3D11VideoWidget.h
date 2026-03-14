#ifndef D3D11VIDEOWIDGET_H
#define D3D11VIDEOWIDGET_H

/**
 * @file D3D11VideoWidget.h
 * @brief D3D11 视频渲染控件 / D3D11 Video Rendering Widget
 *
 * 将 D3D11Renderer 嵌入 QWidget, 替代 QYUVOpenGLWidget。
 * Embeds D3D11Renderer into a QWidget, replacing QYUVOpenGLWidget.
 *
 * API 与 QYUVOpenGLWidget 兼容, VideoForm 可直接切换使用。
 * API compatible with QYUVOpenGLWidget, VideoForm can switch directly.
 *
 * 帧提交使用无锁原子邮箱模式 (与 QYUVOpenGLWidget 相同):
 *   解码线程 → atomic exchange → GUI 线程消费 → D3D11 渲染
 */

#include <QWidget>
#include <QSize>
#include <QImage>
#include <QEvent>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>
#include <memory>

#include "GrayFrame.h"

namespace qsc { class D3D11Renderer; }

/**
 * @brief 帧数据槽 (无锁帧传递)
 *
 * 通过 atomic exchange 在解码线程和 GUI 线程之间传递帧数据。
 */
struct D3DFrameSlot {
    uint8_t* dataY = nullptr;
    uint8_t* dataU = nullptr;
    uint8_t* dataV = nullptr;
    int width  = 0;
    int height = 0;
    int linesizeY = 0;
    int linesizeU = 0;
    int linesizeV = 0;
    std::function<void()> releaseCallback;
};

class D3D11VideoWidget : public QWidget
{
    Q_OBJECT
public:
    explicit D3D11VideoWidget(QWidget* parent = nullptr);
    ~D3D11VideoWidget() override;

    // =========================================================================
    // 帧尺寸 (与 QYUVOpenGLWidget 兼容)
    // =========================================================================
    void setFrameSize(const QSize& size);
    const QSize& frameSize() const { return m_frameSize; }

    // =========================================================================
    // 帧提交 (与 QYUVOpenGLWidget 兼容)
    // =========================================================================

    /**
     * @brief YUV420P 帧提交 (同步, 旧路径)
     */
    void updateTextures(uint8_t* dataY, uint8_t* dataU, uint8_t* dataV,
                        uint32_t linesizeY, uint32_t linesizeU, uint32_t linesizeV);

    /**
     * @brief 零拷贝帧提交 (主路径, 与 QYUVOpenGLWidget::submitFrameDirect 兼容)
     *
     * 使用原子指针交换将帧传递到 GUI 线程, 无锁无额外拷贝。
     * 渲染完成后自动调用 releaseCallback 释放帧资源。
     */
    void submitFrameDirect(uint8_t* dataY, uint8_t* dataU, uint8_t* dataV,
                           int width, int height,
                           int linesizeY, int linesizeU, int linesizeV,
                           std::function<void()> releaseCallback);

    /**
     * @brief 释放当前持有的帧 (窗口关闭时调用)
     */
    void discardPendingFrame();

    // =========================================================================
    // 帧抓取 (与 QYUVOpenGLWidget 兼容)
    // =========================================================================

    /**
     * @brief 获取当前帧 RGB 图像
     */
    QImage grabCurrentFrame();

    /**
     * @brief 获取当前帧灰度数据
     */
    GrayFrame grabGrayFrame();

    // =========================================================================
    // 状态
    // =========================================================================
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

protected:
    // QWidget 事件重写
    QPaintEngine* paintEngine() const override { return nullptr; }
    void paintEvent(QPaintEvent*) override;     // v17b: 恢复 D3D11 内容 (WM_PAINT 后重新呈现)
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool event(QEvent* event) override;
#ifdef _WIN32
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif

private:
    void ensureRenderer();
    void consumeAndRenderFrame();
    void cacheFramePlanes(uint8_t* dataY, uint8_t* dataU, uint8_t* dataV,
                          int width, int height,
                          int linesizeY, int linesizeU, int linesizeV);

private:
    // D3D11 渲染器
    std::unique_ptr<qsc::D3D11Renderer> m_renderer;
    bool m_rendererInitialized = false;

    // 帧尺寸
    QSize m_frameSize = { -1, -1 };

    // 无锁帧邮箱 (与 QYUVOpenGLWidget 相同模式)
    std::atomic<D3DFrameSlot*> m_pendingFrame{nullptr};
    D3DFrameSlot* m_renderedFrame = nullptr;    // 仅 GUI 线程访问

    // 高优先级渲染事件
    static constexpr int RenderEventType = QEvent::User + 43;
    std::atomic<bool> m_renderEventPending{false};

    // 销毁标志
    std::atomic<bool> m_isDestroying{false};

    // 原始帧缓存（用于原始尺寸抓帧）
    std::mutex m_yuvMutex;
    std::vector<uint8_t> m_yuvDataY;
    std::vector<uint8_t> m_yuvDataU;
    std::vector<uint8_t> m_yuvDataV;
    int m_cachedFrameWidth = 0;
    int m_cachedFrameHeight = 0;

    // 统计
    std::atomic<uint64_t> m_totalFrames{0};
    std::atomic<uint64_t> m_droppedFrames{0};
};

#endif // D3D11VIDEOWIDGET_H
