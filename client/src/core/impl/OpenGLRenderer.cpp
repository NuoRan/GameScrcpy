#include "OpenGLRenderer.h"
#include "infra/FrameData.h"
#include "qyuvopenglwidget.h"

namespace qsc {
namespace core {

OpenGLRenderer::OpenGLRenderer(QYUVOpenGLWidget* widget)
    : m_widget(widget)
{
}

bool OpenGLRenderer::initialize()
{
    if (!m_widget) {
        return false;
    }

    // Widget 的 OpenGL 上下文在 show() 时自动初始化
    m_initialized = true;
    return true;
}

void OpenGLRenderer::setFrameSize(int width, int height)
{
    m_frameSize = QSize(width, height);
    if (m_widget) {
        m_widget->setFrameSize(m_frameSize);
    }
}

void OpenGLRenderer::renderFrame(const FrameData& frame)
{
    if (!m_widget || !frame.isValid()) {
        return;
    }

    // 更新帧尺寸（如果变化）
    if (frame.width != m_frameSize.width() || frame.height != m_frameSize.height()) {
        setFrameSize(frame.width, frame.height);
    }

    // 更新纹理
    m_widget->updateTextures(
        frame.dataY, frame.dataU, frame.dataV,
        frame.linesizeY, frame.linesizeU, frame.linesizeV
    );
}

void OpenGLRenderer::updateTextures(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                                    int linesizeY, int linesizeU, int linesizeV)
{
    if (!m_widget) {
        return;
    }

    m_widget->updateTextures(
        const_cast<quint8*>(y),
        const_cast<quint8*>(u),
        const_cast<quint8*>(v),
        static_cast<quint32>(linesizeY),
        static_cast<quint32>(linesizeU),
        static_cast<quint32>(linesizeV)
    );
}

bool OpenGLRenderer::grabFrame(uint8_t* outData, int* outWidth, int* outHeight)
{
    if (!m_widget) {
        return false;
    }

    QImage image = m_widget->grabCurrentFrame();
    if (image.isNull()) {
        return false;
    }

    // 转换为 RGB32 格式
    QImage rgb32 = image.convertToFormat(QImage::Format_RGB32);

    if (outWidth) *outWidth = rgb32.width();
    if (outHeight) *outHeight = rgb32.height();

    if (outData) {
        memcpy(outData, rgb32.constBits(), rgb32.sizeInBytes());
    }

    return true;
}

bool OpenGLRenderer::isPBOEnabled() const
{
    return m_widget ? m_widget->isPBOEnabled() : false;
}

void OpenGLRenderer::setPBOEnabled(bool enable)
{
    if (m_widget) {
        m_widget->setPBOEnabled(enable);
    }
}

} // namespace core
} // namespace qsc
