#include "ZeroCopyRenderer.h"
#include <QDebug>
#include <cstring>

// GPU 直通帧需要释放 AVFrame 引用
extern "C" {
#include "libavutil/frame.h"
}

namespace qsc {
namespace core {

ZeroCopyRenderer::ZeroCopyRenderer(QWidget* parent)
    : QYUVOpenGLWidget(parent)
{
    qInfo("[ZeroCopyRenderer] Created (event-driven mode)");
}

ZeroCopyRenderer::~ZeroCopyRenderer()
{
    // 归还当前帧
    if (m_currentFrame && m_frameQueue) {
        // 释放 GPU 直通帧的 AVFrame 引用
        if (m_currentFrame->hwAVFrame) {
            auto* avFrame = static_cast<AVFrame*>(m_currentFrame->hwAVFrame);
            av_frame_free(&avFrame);
            m_currentFrame->hwAVFrame = nullptr;
        }
        m_frameQueue->releaseFrame(m_currentFrame);
        m_currentFrame = nullptr;
    }
}

void ZeroCopyRenderer::setFrameQueue(FrameQueue* queue)
{
    m_frameQueue = queue;
}

void ZeroCopyRenderer::setFrameSize(int width, int height)
{
    QYUVOpenGLWidget::setFrameSize(QSize(width, height));
    m_ready = true;
}

void ZeroCopyRenderer::renderFrame(const FrameData& frame)
{
    if (!frame.isValid() && !frame.isGPUDirect) return;

    // 确保尺寸正确
    if (frameSize() != QSize(frame.width, frame.height)) {
        setFrameSize(frame.width, frame.height);
    }

    // GPU 直通路径
    if (frame.isGPUDirect) {
        // GPU 直通帧: D3D11 纹理直接映射到 GL 纹理
        // 需要 D3D11GLInterop 支持，当前以 NV12 格式渲染
        // TODO: 在 QYUVOpenGLWidget 中实现 renderGPUDirectFrame(frame)
        //       调用 D3D11GLInterop::lock() → bind NV12 textures → draw → unlock()
        qDebug("[ZeroCopyRenderer] GPU direct frame: %dx%d, tex=%p, idx=%d",
               frame.width, frame.height,
               frame.d3d11Texture, frame.d3d11TextureIndex);

        // 回退: 如果 GL interop 未就绪，标记帧已消费但不渲染
        // 渲染器完成 interop 集成后移除此回退
        return;
    }

    // 始终使用 YUV420P 渲染路径（NV12 已在解码端转为 YUV420P）
    if (yuvFormat() != YUVFormat::YUV420P) {
        setYUVFormat(YUVFormat::YUV420P);
    }
    QYUVOpenGLWidget::updateTextures(
        frame.dataY, frame.dataU, frame.dataV,
        frame.linesizeY, frame.linesizeU, frame.linesizeV
    );
}

QImage ZeroCopyRenderer::grabCurrentFrame()
{
    return QYUVOpenGLWidget::grabCurrentFrame();
}

void ZeroCopyRenderer::updateTextures(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                                      int linesizeY, int linesizeU, int linesizeV)
{
    QYUVOpenGLWidget::updateTextures(
        const_cast<quint8*>(y), const_cast<quint8*>(u), const_cast<quint8*>(v),
        linesizeY, linesizeU, linesizeV
    );
}

bool ZeroCopyRenderer::grabFrame(uint8_t* outData, int* outWidth, int* outHeight)
{
    QImage img = QYUVOpenGLWidget::grabCurrentFrame();
    if (img.isNull()) {
        return false;
    }

    if (outWidth) *outWidth = img.width();
    if (outHeight) *outHeight = img.height();

    if (outData) {
        // 转换为 RGB32
        QImage rgb32 = img.convertToFormat(QImage::Format_RGB32);
        memcpy(outData, rgb32.bits(), rgb32.sizeInBytes());
    }

    return true;
}

void ZeroCopyRenderer::onFrameReady()
{
    consumeAndRender();
}

void ZeroCopyRenderer::consumeAndRender()
{
    if (!m_frameQueue) return;

    // 归还上一帧
    if (m_currentFrame) {
        // 释放 GPU 直通帧的 AVFrame 引用
        if (m_currentFrame->hwAVFrame) {
            auto* avFrame = static_cast<AVFrame*>(m_currentFrame->hwAVFrame);
            av_frame_free(&avFrame);
            m_currentFrame->hwAVFrame = nullptr;
        }
        m_frameQueue->releaseFrame(m_currentFrame);
        m_currentFrame = nullptr;
    }

    // 跳帧到最新：丢弃中间积压帧，只渲染最新帧
    FrameData* latest = nullptr;
    FrameData* frame = nullptr;
    int droppedFrames = 0;

    while ((frame = m_frameQueue->popFrame()) != nullptr) {
        if (latest) {
            // 被跳过的 GPU 直通帧需释放 AVFrame 引用
            if (latest->hwAVFrame) {
                auto* avFrame = static_cast<AVFrame*>(latest->hwAVFrame);
                av_frame_free(&avFrame);
                latest->hwAVFrame = nullptr;
            }
            m_frameQueue->releaseFrame(latest);
            droppedFrames++;
        }
        latest = frame;
    }

    if (droppedFrames > 0) {
        qDebug("[ZeroCopyRenderer] Skipped %d stale frame(s) to reduce latency", droppedFrames);
    }

    m_currentFrame = latest;

    if (m_currentFrame && m_currentFrame->isValid()) {
        renderFrame(*m_currentFrame);
        emit frameConsumed();
    }
}

} // namespace core
} // namespace qsc
