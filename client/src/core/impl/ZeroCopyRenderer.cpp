#include "ZeroCopyRenderer.h"
#include <QDebug>
#include <cstring>

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
    if (!frame.isValid()) return;

    // 确保尺寸正确
    if (frameSize() != QSize(frame.width, frame.height)) {
        setFrameSize(frame.width, frame.height);
    }

    // 直接更新纹理（使用父类的 PBO 优化）
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
        m_frameQueue->releaseFrame(m_currentFrame);
        m_currentFrame = nullptr;
    }

    // 获取新帧
    m_currentFrame = m_frameQueue->popFrame();

    if (m_currentFrame && m_currentFrame->isValid()) {
        renderFrame(*m_currentFrame);  // 使用引用版本
        emit frameConsumed();
    }
}

} // namespace core
} // namespace qsc
