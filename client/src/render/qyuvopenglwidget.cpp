#include <QCoreApplication>
#include <QOpenGLTexture>
#include <QSurfaceFormat>
#include <QOpenGLContext>
#include <QDebug>
#include <QDateTime>
#include <QThread>
#include <QTimer>
#include <QMetaObject>

#include "qyuvopenglwidget.h"
#include "PerformanceMonitor.h"

// 调试日志
#define RENDER_LOG(msg) qDebug() << "[Render]" << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") \
                                 << "[" << QThread::currentThreadId() << "]" << msg

// OpenGL PBO 扩展常量
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_WRITE_ONLY
#define GL_WRITE_ONLY 0x88B9
#endif

// ---------------------------------------------------------
// 顶点坐标和纹理坐标定义
// 用于绘制全屏矩形 (两个三角形组成)
// ---------------------------------------------------------
static const GLfloat coordinate[] = {
    // 顶点坐标 (x, y, z) - 范围 [-1, 1]
    -1.0f, -1.0f, 0.0f,
    1.0f, -1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f,
    1.0f,  1.0f, 0.0f,

    // 纹理坐标 (u, v) - 范围 [0, 1]
    0.0f, 1.0f,
    1.0f, 1.0f,
    0.0f, 0.0f,
    1.0f, 0.0f
};

// ---------------------------------------------------------
// 顶点着色器
// 传递顶点位置和纹理坐标
// ---------------------------------------------------------
static const QString s_vertShader = R"(
    attribute vec3 vertexIn;    // xyz顶点坐标
    attribute vec2 textureIn;   // xy纹理坐标
    varying vec2 textureOut;    // 传递给片段着色器的纹理坐标
    void main(void)
    {
        gl_Position = vec4(vertexIn, 1.0);
        textureOut = textureIn;
    }
)";

// ---------------------------------------------------------
// 片段着色器 - YUV420P 格式
// 执行 YUV 到 RGB 的颜色空间转换 (BT.709)
// ---------------------------------------------------------
static QString s_fragShader = R"(
    varying vec2 textureOut;        // 纹理坐标
    uniform sampler2D textureY;     // Y分量纹理
    uniform sampler2D textureU;     // U分量纹理
    uniform sampler2D textureV;     // V分量纹理
    void main(void)
    {
        vec3 yuv;
        vec3 rgb;

        // BT709 转换系数
        const vec3 Rcoeff = vec3(1.1644,  0.000,  1.7927);
        const vec3 Gcoeff = vec3(1.1644, -0.2132, -0.5329);
        const vec3 Bcoeff = vec3(1.1644,  2.1124,  0.000);

        // 采样 YUV 数据
        yuv.x = texture2D(textureY, textureOut).r;
        yuv.y = texture2D(textureU, textureOut).r - 0.5;
        yuv.z = texture2D(textureV, textureOut).r - 0.5;

        // 亮度调整与颜色转换
        yuv.x = yuv.x - 0.0625;
        rgb.r = dot(yuv, Rcoeff);
        rgb.g = dot(yuv, Gcoeff);
        rgb.b = dot(yuv, Bcoeff);

        gl_FragColor = vec4(rgb, 1.0);
    }
)";

// ---------------------------------------------------------
// NV12: 片段着色器 - NV12 格式 (直接渲染，避免格式转换)
// UV 交织存储在单个纹理中 (RG 通道)
// ---------------------------------------------------------
static QString s_fragShaderNV12 = R"(
    varying vec2 textureOut;        // 纹理坐标
    uniform sampler2D textureY;     // Y分量纹理
    uniform sampler2D textureUV;    // UV交织纹理 (NV12)
    void main(void)
    {
        vec3 yuv;
        vec3 rgb;

        // BT709 转换系数
        const vec3 Rcoeff = vec3(1.1644,  0.000,  1.7927);
        const vec3 Gcoeff = vec3(1.1644, -0.2132, -0.5329);
        const vec3 Bcoeff = vec3(1.1644,  2.1124,  0.000);

        // 采样 Y 数据
        yuv.x = texture2D(textureY, textureOut).r;

        // NV12: 从 UV 交织纹理采样 (R=U, G=V 或 A=V)
        vec2 uv = texture2D(textureUV, textureOut).rg;
        yuv.y = uv.r - 0.5;
        yuv.z = uv.g - 0.5;

        // 亮度调整与颜色转换
        yuv.x = yuv.x - 0.0625;
        rgb.r = dot(yuv, Rcoeff);
        rgb.g = dot(yuv, Gcoeff);
        rgb.b = dot(yuv, Bcoeff);

        gl_FragColor = vec4(rgb, 1.0);
    }
)";

QYUVOpenGLWidget::QYUVOpenGLWidget(QWidget *parent) : QOpenGLWidget(parent)
{
    m_statsTimer.start();

    // 创建后台刷新定时器，用于在窗口不可见时保持帧更新
    m_backgroundRefreshTimer = new QTimer(this);
    m_backgroundRefreshTimer->setInterval(16); // ~60fps
    connect(m_backgroundRefreshTimer, &QTimer::timeout, this, [this]() {
        // 检查是否正在销毁
        if (m_isDestroying.load(std::memory_order_acquire)) {
            m_backgroundRefreshTimer->stop();
            return;
        }
        // 窗口不可见时仍然处理待渲染的帧，避免卡顿
        if (m_hasPendingFrame.load(std::memory_order_acquire) && !m_isDestroying.load()) {
            // 直接调用 repaint 而不是 update，确保即使窗口不可见也能更新
            repaint();
        }
    });
}

QYUVOpenGLWidget::~QYUVOpenGLWidget()
{
    // 标记为正在销毁，阻止所有渲染操作
    m_isDestroying.store(true, std::memory_order_release);
    m_hasPendingFrame.store(false, std::memory_order_release);

    // 释放持有的直接指针帧
    {
        QMutexLocker locker(&m_yuvMutex);
        if (m_directFrameReleaseCallback) {
            m_directFrameReleaseCallback();
            m_directFrameReleaseCallback = nullptr;
            m_directDataY = nullptr;
        }
    }

    // 立即停止并删除定时器（必须在创建它的线程中执行）
    if (m_backgroundRefreshTimer) {
        if (QThread::currentThread() == m_backgroundRefreshTimer->thread()) {
            m_backgroundRefreshTimer->stop();
            delete m_backgroundRefreshTimer;
        } else {
            // 跨线程：投递到正确线程中停止
            QMetaObject::invokeMethod(m_backgroundRefreshTimer, &QTimer::stop);
            m_backgroundRefreshTimer->deleteLater();
        }
        m_backgroundRefreshTimer = nullptr;
    }

    // 等待一小段时间确保没有正在进行的渲染
    QThread::msleep(50);

    // 清理OpenGL资源
    if (context()) {
        makeCurrent();
        deInitPBO();
        m_vbo.destroy();
        deInitTextures();
        doneCurrent();
    }
}

QSize QYUVOpenGLWidget::minimumSizeHint() const
{
    return QSize(50, 50);
}

QSize QYUVOpenGLWidget::sizeHint() const
{
    return size();
}

// ---------------------------------------------------------
// 视频帧尺寸变化处理
// ---------------------------------------------------------
void QYUVOpenGLWidget::setFrameSize(const QSize &frameSize)
{
    if (m_frameSize != frameSize) {
        m_frameSize = frameSize;
        m_needUpdate = true;

        // 重新初始化 PBO
        if (m_pboInited) {
            makeCurrent();
            deInitPBO();
            initPBO();
            doneCurrent();
        }

        // 重置脏区域检测缓存
        m_prevFrameY.clear();

        repaint();
    }
}

const QSize &QYUVOpenGLWidget::frameSize()
{
    return m_frameSize;
}

// ---------------------------------------------------------
// 更新YUV纹理数据
// ---------------------------------------------------------
void QYUVOpenGLWidget::updateTextures(quint8 *dataY, quint8 *dataU, quint8 *dataV, quint32 linesizeY, quint32 linesizeU, quint32 linesizeV)
{
    // 如果正在销毁，直接返回
    if (m_isDestroying.load(std::memory_order_acquire)) {
        return;
    }

    m_uploadTimer.start();
    m_totalFrames++;

    // 缓存 YUV 数据
    {
        QMutexLocker locker(&m_yuvMutex);
        if (m_frameSize.isValid() && dataY && dataU && dataV) {
            int w = m_frameSize.width();
            int h = m_frameSize.height();

            m_yuvDataY.resize(w * h);
            for (int y = 0; y < h; ++y) {
                memcpy(m_yuvDataY.data() + y * w, dataY + y * linesizeY, w);
            }

            int uvW = w / 2;
            int uvH = h / 2;
            m_yuvDataU.resize(uvW * uvH);
            m_yuvDataV.resize(uvW * uvH);
            for (int y = 0; y < uvH; ++y) {
                memcpy(m_yuvDataU.data() + y * uvW, dataU + y * linesizeU, uvW);
                memcpy(m_yuvDataV.data() + y * uvW, dataV + y * linesizeV, uvW);
            }

            m_linesizeY = w;
            m_linesizeU = uvW;
            m_linesizeV = uvW;
        }
    }

    // 只在 GUI 线程执行 OpenGL 上传
    m_hasPendingFrame.store(true, std::memory_order_release);
    QMetaObject::invokeMethod(this, "repaint", Qt::QueuedConnection);

    m_totalUploadTime += m_uploadTimer.nsecsElapsed() / 1000000.0;

    if (m_statsTimer.elapsed() >= 1000) {
        RenderStatistics stats = statistics();
        emit statisticsUpdated(stats);
        m_statsTimer.restart();
    }
}

// ---------------------------------------------------------
// 零拷贝帧提交（优化跨线程渲染）
// 利用 QByteArray 的隐式共享避免额外拷贝
// ---------------------------------------------------------
void QYUVOpenGLWidget::submitFrame(QByteArray frameData, int width, int height, int linesizeY, int linesizeU, int linesizeV)
{
    // 如果正在销毁，直接返回
    if (m_isDestroying.load(std::memory_order_acquire)) {
        return;
    }

    m_uploadTimer.start();
    m_totalFrames++;

    {
        QMutexLocker locker(&m_yuvMutex);
        // 直接移动 QByteArray，利用隐式共享，无额外拷贝
        m_zeroCopyFrame = std::move(frameData);
        m_zcFrameWidth = width;
        m_zcFrameHeight = height;
        m_zcLinesizeY = linesizeY;
        m_zcLinesizeU = linesizeU;
        m_zcLinesizeV = linesizeV;
        m_useZeroCopyFrame = true;

        // 更新帧尺寸（用于帧获取）
        if (m_frameSize.width() != width || m_frameSize.height() != height) {
            m_frameSize = QSize(width, height);
            m_needUpdate = true;
        }
    }

    // 触发渲染
    m_hasPendingFrame.store(true, std::memory_order_release);
    QMetaObject::invokeMethod(this, "repaint", Qt::QueuedConnection);

    m_totalUploadTime += m_uploadTimer.nsecsElapsed() / 1000000.0;

    if (m_statsTimer.elapsed() >= 1000) {
        RenderStatistics stats = statistics();
        emit statisticsUpdated(stats);
        m_statsTimer.restart();
    }
}

// ---------------------------------------------------------
// 零拷贝帧提交 - 直接指针版本（完全零拷贝）
// 渲染完成后自动调用回调释放帧资源
// ---------------------------------------------------------
void QYUVOpenGLWidget::submitFrameDirect(uint8_t* dataY, uint8_t* dataU, uint8_t* dataV,
                                         int width, int height,
                                         int linesizeY, int linesizeU, int linesizeV,
                                         std::function<void()> releaseCallback)
{
    if (m_isDestroying.load(std::memory_order_acquire)) {
        if (releaseCallback) releaseCallback();
        return;
    }

    m_uploadTimer.start();
    m_totalFrames++;

    {
        QMutexLocker locker(&m_yuvMutex);

        // 先释放上一帧（如果有）——不管 m_useDirectFrame 标志
        // 因为 paintGL 设置 m_useDirectFrame=false 但保留帧供截图
        if (m_directFrameReleaseCallback) {
            m_directFrameReleaseCallback();
            m_directFrameReleaseCallback = nullptr;
            m_directDataY = nullptr;
        }

        // 存储直接指针
        m_directDataY = dataY;
        m_directDataU = dataU;
        m_directDataV = dataV;
        m_directWidth = width;
        m_directHeight = height;
        m_directLinesizeY = linesizeY;
        m_directLinesizeU = linesizeU;
        m_directLinesizeV = linesizeV;
        m_directFrameReleaseCallback = std::move(releaseCallback);
        m_useDirectFrame = true;
        m_useZeroCopyFrame = false;  // 优先使用直接指针模式
        m_grabDataStale = true;  // 标记截图数据可用，无需等待 paintGL

        if (m_frameSize.width() != width || m_frameSize.height() != height) {
            m_frameSize = QSize(width, height);
            m_needUpdate = true;
        }
    }

    m_hasPendingFrame.store(true, std::memory_order_release);
    // 使用 repaint() 代替 update()：
    // update() 会被 Qt 合并（多帧到达同一事件循环 tick → 只触发一次 paintGL → 丢帧）
    // repaint() 立即触发 paintGL，确保每帧都被渲染
    QMetaObject::invokeMethod(this, "repaint", Qt::QueuedConnection);

    m_totalUploadTime += m_uploadTimer.nsecsElapsed() / 1000000.0;
}

// ---------------------------------------------------------
// 释放当前持有的直接指针帧（窗口关闭时调用）
// ---------------------------------------------------------
void QYUVOpenGLWidget::discardPendingFrame()
{
    QMutexLocker locker(&m_yuvMutex);
    if (m_directFrameReleaseCallback) {
        m_directFrameReleaseCallback();
        m_directFrameReleaseCallback = nullptr;
        m_directDataY = nullptr;
        m_useDirectFrame = false;
    }
    m_hasPendingFrame.store(false, std::memory_order_release);
}

// ---------------------------------------------------------
// NV12: 更新 NV12 格式纹理数据 (直接渲染，避免格式转换)
// ---------------------------------------------------------
void QYUVOpenGLWidget::updateTexturesNV12(quint8 *dataY, quint8 *dataUV, quint32 linesizeY, quint32 linesizeUV)
{
    // 如果正在销毁，直接返回
    if (m_isDestroying.load(std::memory_order_acquire)) {
        return;
    }

    m_uploadTimer.start();
    m_totalFrames++;

    // 缓存 NV12 数据
    {
        QMutexLocker locker(&m_yuvMutex);
        if (m_frameSize.isValid() && dataY && dataUV) {
            int w = m_frameSize.width();
            int h = m_frameSize.height();

            // Y 分量
            m_yuvDataY.resize(w * h);
            for (int y = 0; y < h; ++y) {
                memcpy(m_yuvDataY.data() + y * w, dataY + y * linesizeY, w);
            }

            // UV 交织分量 (宽度为 w，高度为 h/2)
            int uvH = h / 2;
            m_yuvDataUV.resize(w * uvH);
            for (int y = 0; y < uvH; ++y) {
                memcpy(m_yuvDataUV.data() + y * w, dataUV + y * linesizeUV, w);
            }

            m_linesizeY = w;
            m_linesizeUV = w;
        }
    }

    // 只在 GUI 线程执行 OpenGL 上传
    m_hasPendingFrame.store(true, std::memory_order_release);
    QMetaObject::invokeMethod(this, "repaint", Qt::QueuedConnection);

    m_totalUploadTime += m_uploadTimer.nsecsElapsed() / 1000000.0;

    if (m_statsTimer.elapsed() >= 1000) {
        RenderStatistics stats = statistics();
        emit statisticsUpdated(stats);
        m_statsTimer.restart();
    }
}

// ---------------------------------------------------------
// NV12: 设置 YUV 格式
// ---------------------------------------------------------
void QYUVOpenGLWidget::setYUVFormat(YUVFormat format)
{
    if (m_yuvFormat != format) {
        m_yuvFormat = format;
        m_needUpdate = true;

        // 需要重新初始化着色器和纹理
        if (context()) {
            makeCurrent();
            deInitTextures();
            if (format == YUVFormat::NV12) {
                initShaderNV12();
                initTexturesNV12();
            } else {
                initShader();
                initTextures();
            }
            doneCurrent();
        }

        qInfo() << "YUV format changed to:" << (format == YUVFormat::NV12 ? "NV12" : "YUV420P");
    }
}

// ---------------------------------------------------------
// NV12: 初始化 NV12 专用着色器
// ---------------------------------------------------------
void QYUVOpenGLWidget::initShaderNV12()
{
    // 编译 NV12 着色器
    if (!m_shaderProgramNV12.addShaderFromSourceCode(QOpenGLShader::Vertex, s_vertShader)) {
        qCritical() << "NV12 Vertex shader compile error:" << m_shaderProgramNV12.log();
        return;
    }

    if (!m_shaderProgramNV12.addShaderFromSourceCode(QOpenGLShader::Fragment, s_fragShaderNV12)) {
        qCritical() << "NV12 Fragment shader compile error:" << m_shaderProgramNV12.log();
        return;
    }

    // 绑定属性位置
    m_shaderProgramNV12.bindAttributeLocation("vertexIn", 0);
    m_shaderProgramNV12.bindAttributeLocation("textureIn", 1);

    if (!m_shaderProgramNV12.link()) {
        qCritical() << "NV12 Shader link error:" << m_shaderProgramNV12.log();
        return;
    }

    qInfo() << "NV12 shader initialized successfully";
}

// ---------------------------------------------------------
// NV12: 初始化 NV12 纹理 (Y + UV)
// ---------------------------------------------------------
void QYUVOpenGLWidget::initTexturesNV12()
{
    if (!m_frameSize.isValid()) {
        return;
    }

    // 生成 2 个纹理 (Y 和 UV)
    glGenTextures(2, m_textureNV12);

    // Y 纹理 (全分辨率，单通道)
    glBindTexture(GL_TEXTURE_2D, m_textureNV12[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, m_frameSize.width(), m_frameSize.height(),
                 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);

    // UV 纹理 (半分辨率，双通道 - LUMINANCE_ALPHA 或 RG)
    glBindTexture(GL_TEXTURE_2D, m_textureNV12[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // 使用 LUMINANCE_ALPHA 存储 UV (R=U, A=V)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, m_frameSize.width() / 2, m_frameSize.height() / 2,
                 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, 0);

    qInfo() << "NV12 textures initialized for frame size:" << m_frameSize;
}

// ---------------------------------------------------------
// 获取当前帧的 RGB 图像
// 将 YUV420P 转换为 RGB
// ---------------------------------------------------------
QImage QYUVOpenGLWidget::grabCurrentFrame()
{
    int w, h;

    // 第一阶段：仅在互斥锁内做快速数据拷贝
    {
        QMutexLocker locker(&m_yuvMutex);

        // 懒拷贝：仅在截图时才从当前帧同步到 YUV 缓存
        if (m_grabDataStale && m_frameSize.isValid()) {
            // 优先从直接指针帧读取（submitFrameDirect 路径）
            if (m_directDataY) {
                w = m_directWidth;
                h = m_directHeight;
                int uvH = h / 2;
                int uvW = w / 2;

                m_yuvDataY.resize(w * h);
                m_yuvDataU.resize(uvW * uvH);
                m_yuvDataV.resize(uvW * uvH);
                for (int y = 0; y < h; ++y)
                    memcpy(m_yuvDataY.data() + y * w, m_directDataY + y * m_directLinesizeY, w);
                for (int y = 0; y < uvH; ++y) {
                    memcpy(m_yuvDataU.data() + y * uvW, m_directDataU + y * m_directLinesizeU, uvW);
                    memcpy(m_yuvDataV.data() + y * uvW, m_directDataV + y * m_directLinesizeV, uvW);
                }
                m_grabDataStale = false;
            }
            // 零拷贝 QByteArray 路径
            else if (!m_zeroCopyFrame.isEmpty()) {
                w = m_zcFrameWidth > 0 ? m_zcFrameWidth : m_frameSize.width();
                h = m_zcFrameHeight > 0 ? m_zcFrameHeight : m_frameSize.height();
                int uvH = h / 2;
                int uvW = w / 2;
                int ySize = m_zcLinesizeY * h;
                int uSize = m_zcLinesizeU * uvH;
                const uint8_t* srcY = reinterpret_cast<const uint8_t*>(m_zeroCopyFrame.constData());
                const uint8_t* srcU = srcY + ySize;
                const uint8_t* srcV = srcU + uSize;

                m_yuvDataY.resize(w * h);
                m_yuvDataU.resize(uvW * uvH);
                m_yuvDataV.resize(uvW * uvH);
                for (int y = 0; y < h; ++y)
                    memcpy(m_yuvDataY.data() + y * w, srcY + y * m_zcLinesizeY, w);
                for (int y = 0; y < uvH; ++y) {
                    memcpy(m_yuvDataU.data() + y * uvW, srcU + y * m_zcLinesizeU, uvW);
                    memcpy(m_yuvDataV.data() + y * uvW, srcV + y * m_zcLinesizeV, uvW);
                }
                m_grabDataStale = false;
            }
        }

        if (m_yuvDataY.empty() || !m_frameSize.isValid()) {
            return QImage();
        }

        w = m_frameSize.width();
        h = m_frameSize.height();

        // 验证数据大小
        size_t expectedYSize = static_cast<size_t>(w) * h;
        size_t expectedUVSize = static_cast<size_t>(w / 2) * (h / 2);
        if (m_yuvDataY.size() < expectedYSize ||
            m_yuvDataU.size() < expectedUVSize ||
            m_yuvDataV.size() < expectedUVSize) {
            return QImage();
        }
    }
    // mutex 已释放，submitFrameDirect 不再被阻塞

    // 第二阶段：CPU 密集的 YUV→RGB 转换（无需持锁）
    // m_yuvDataY/U/V 的所有权由本线程独占（只有 grabCurrentFrame 会写入）
    QImage image(w, h, QImage::Format_RGB888);
    int uvW = w / 2;

    for (int y = 0; y < h; ++y) {
        uchar* rgb = image.scanLine(y);
        for (int x = 0; x < w; ++x) {
            int yIdx = y * w + x;
            int uvIdx = (y / 2) * uvW + (x / 2);

            int Y = m_yuvDataY[yIdx];
            int U = m_yuvDataU[uvIdx] - 128;
            int V = m_yuvDataV[uvIdx] - 128;

            int R = qBound(0, (int)(Y + 1.5748 * V), 255);
            int G = qBound(0, (int)(Y - 0.1873 * U - 0.4681 * V), 255);
            int B = qBound(0, (int)(Y + 1.8556 * U), 255);

            rgb[x * 3 + 0] = R;
            rgb[x * 3 + 1] = G;
            rgb[x * 3 + 2] = B;
        }
    }

    return image;
}

// ---------------------------------------------------------
// 获取当前帧的灰度数据 (直接使用 Y 分量)
// 用于模板匹配，比 RGB 转换更高效
// ---------------------------------------------------------
std::vector<uint8_t> QYUVOpenGLWidget::grabCurrentFrameGrayscale()
{
    QMutexLocker locker(&m_yuvMutex);
    return m_yuvDataY;  // 返回 Y 分量的副本
}

// ---------------------------------------------------------
// PBO 相关方法
// ---------------------------------------------------------

void QYUVOpenGLWidget::setPBOEnabled(bool enable)
{
    if (m_pboEnabled != enable) {
        m_pboEnabled = enable;
        qInfo() << "PBO" << (enable ? "enabled" : "disabled");
    }
}

RenderStatistics QYUVOpenGLWidget::statistics() const
{
    RenderStatistics stats;
    stats.totalFrames = m_totalFrames.load();
    stats.droppedFrames = m_droppedFrames.load();
    stats.pboEnabled = isPBOEnabled();

    if (stats.totalFrames > 0) {
        stats.avgUploadTimeMs = m_totalUploadTime / stats.totalFrames;
        stats.avgRenderTimeMs = m_totalRenderTime / stats.totalFrames;
    }

    return stats;
}

void QYUVOpenGLWidget::resetStatistics()
{
    m_totalFrames = 0;
    m_droppedFrames = 0;
    m_totalUploadTime = 0;
    m_totalRenderTime = 0;
}

bool QYUVOpenGLWidget::checkPBOSupport()
{
    // 检查 OpenGL 版本和扩展
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return false;

    // OpenGL 2.1+ 或 OpenGL ES 3.0+ 支持 PBO
    auto version = ctx->format().version();
    bool isES = ctx->isOpenGLES();

    if (isES) {
        // OpenGL ES 3.0+
        m_pboSupported = (version.first >= 3);
    } else {
        // OpenGL 2.1+ (ARB_pixel_buffer_object 是核心功能)
        m_pboSupported = (version.first > 2 || (version.first == 2 && version.second >= 1));
    }

    if (!m_pboSupported) {
        qWarning() << "PBO not supported. OpenGL version:" << version.first << "." << version.second
                   << (isES ? "(ES)" : "");
    } else {
        qInfo() << "PBO supported. OpenGL version:" << version.first << "." << version.second
                << (isES ? "(ES)" : "");
    }

    return m_pboSupported;
}

void QYUVOpenGLWidget::initPBO()
{
    if (!m_pboSupported || !m_frameSize.isValid() || m_pboInited) {
        return;
    }

    int ySize = m_frameSize.width() * m_frameSize.height();
    int uvSize = (m_frameSize.width() / 2) * (m_frameSize.height() / 2);

    // 生成 PBO
    glGenBuffers(PBO_COUNT, m_pboY.data());
    glGenBuffers(PBO_COUNT, m_pboU.data());
    glGenBuffers(PBO_COUNT, m_pboV.data());

    // 初始化 Y 分量 PBO
    for (int i = 0; i < PBO_COUNT; ++i) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pboY[i]);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, ySize, nullptr, GL_STREAM_DRAW);
    }

    // 初始化 U 分量 PBO
    for (int i = 0; i < PBO_COUNT; ++i) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pboU[i]);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, uvSize, nullptr, GL_STREAM_DRAW);
    }

    // 初始化 V 分量 PBO
    for (int i = 0; i < PBO_COUNT; ++i) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pboV[i]);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, uvSize, nullptr, GL_STREAM_DRAW);
    }

    // 解绑
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    m_pboInited = true;
    m_pboIndex = 0;

    qInfo() << "PBO initialized for frame size:" << m_frameSize;
}

void QYUVOpenGLWidget::deInitPBO()
{
    if (!m_pboInited) {
        return;
    }

    glDeleteBuffers(PBO_COUNT, m_pboY.data());
    glDeleteBuffers(PBO_COUNT, m_pboU.data());
    glDeleteBuffers(PBO_COUNT, m_pboV.data());

    m_pboY.fill(0);
    m_pboU.fill(0);
    m_pboV.fill(0);

    m_pboInited = false;

    qInfo() << "PBO deinitialized";
}

// 不带 makeCurrent/doneCurrent 的 PBO 纹理更新（用于批量更新）
void QYUVOpenGLWidget::updateTextureWithPBONoContext(GLuint texture, quint32 textureType, quint8 *pixels, quint32 stride)
{
    if (!pixels || !m_pboInited) return;

    QSize size = (textureType == 0) ? m_frameSize : m_frameSize / 2;
    int dataSize = size.width() * size.height();

    // 选择对应的 PBO 数组
    GLuint* pboArray = nullptr;
    switch (textureType) {
        case 0: pboArray = m_pboY.data(); break;
        case 1: pboArray = m_pboU.data(); break;
        case 2: pboArray = m_pboV.data(); break;
        default: return;
    }

    int writeIndex = m_pboIndex;
    int readIndex = (m_pboIndex + 1) % PBO_COUNT;

    // 步骤1: 从上一帧的 PBO 上传到纹理 (异步，DMA传输)
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboArray[readIndex]);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, size.width());
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size.width(), size.height(),
                    GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);

    // 步骤2: 将新数据写入当前 PBO (CPU 端)
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboArray[writeIndex]);

    if (stride != static_cast<quint32>(size.width())) {
        if (m_pboTempBuffer.size() < static_cast<size_t>(dataSize)) {
            m_pboTempBuffer.resize(dataSize);
        }
        for (int y = 0; y < size.height(); ++y) {
            memcpy(m_pboTempBuffer.data() + y * size.width(), pixels + y * stride, size.width());
        }
        glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, dataSize, m_pboTempBuffer.data());
    } else {
        glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, dataSize, pixels);
    }

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

void QYUVOpenGLWidget::updateTextureWithPBO(GLuint texture, quint32 textureType, quint8 *pixels, quint32 stride)
{
    if (!pixels || !m_pboInited) return;

    QSize size = (textureType == 0) ? m_frameSize : m_frameSize / 2;
    int dataSize = size.width() * size.height();

    // 选择对应的 PBO 数组
    GLuint* pboArray = nullptr;
    switch (textureType) {
        case 0: pboArray = m_pboY.data(); break;
        case 1: pboArray = m_pboU.data(); break;
        case 2: pboArray = m_pboV.data(); break;
        default: return;
    }

    int writeIndex = m_pboIndex;
    int readIndex = (m_pboIndex + 1) % PBO_COUNT;

    makeCurrent();

    // 步骤1: 从上一帧的 PBO 上传到纹理 (异步，DMA传输)
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboArray[readIndex]);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, size.width());  // PBO 中数据已经是紧凑的
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size.width(), size.height(),
                    GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);  // nullptr 表示从 PBO 读取

    // 步骤2: 将新数据写入当前 PBO (CPU 端)
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboArray[writeIndex]);

    // 使用 glMapBuffer 或 glBufferSubData
    // glMapBuffer 可以实现真正的异步，但需要更复杂的同步
    // 这里使用 glBufferSubData，简单且足够高效

    // 如果数据有 padding，需要逐行拷贝到复用缓冲区
    if (stride != static_cast<quint32>(size.width())) {
        if (m_pboTempBuffer.size() < static_cast<size_t>(dataSize)) {
            m_pboTempBuffer.resize(dataSize);
        }
        for (int y = 0; y < size.height(); ++y) {
            memcpy(m_pboTempBuffer.data() + y * size.width(), pixels + y * stride, size.width());
        }
        glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, dataSize, m_pboTempBuffer.data());
    } else {
        // 无 padding，直接上传
        glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, dataSize, pixels);
    }

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    doneCurrent();
}

bool QYUVOpenGLWidget::isRegionDirty(const quint8* newData, const quint8* oldData, size_t size, int sampleStep)
{
    if (!oldData || !newData) return true;

    // 采样比较，而不是完整比较
    for (size_t i = 0; i < size; i += sampleStep) {
        if (newData[i] != oldData[i]) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------
// OpenGL 初始化
// 创建VBO，编译着色器，检查PBO支持
// ---------------------------------------------------------
void QYUVOpenGLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glDisable(GL_DEPTH_TEST);

    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(coordinate, sizeof(coordinate));
    initShader();
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    // 检查并初始化 PBO
    checkPBOSupport();
}

// ---------------------------------------------------------
// 渲染循环
// ---------------------------------------------------------
void QYUVOpenGLWidget::paintGL()
{
    // 如果正在销毁，立即返回
    if (m_isDestroying.load(std::memory_order_acquire)) {
        return;
    }

    m_renderTimer.start();
    m_shaderProgram.bind();

    if (m_needUpdate) {
        deInitPBO();
        deInitTextures();
        initTextures();
        if (m_pboSupported && m_pboEnabled) {
            initPBO();
        }
        m_needUpdate = false;
    }

    if (m_textureInited) {
        if (m_hasPendingFrame.load(std::memory_order_acquire)) {
            QMutexLocker locker(&m_yuvMutex);

            // 【最优】直接指针模式（完全零拷贝）
            if (m_useDirectFrame && m_directDataY) {
                const int h = m_directHeight;
                const int uvH = h / 2;
                const int w = m_directWidth;

                if (isPBOEnabled() && m_pboInited) {
                    updateTextureWithPBONoContext(m_texture[0], 0, m_directDataY, m_directLinesizeY);
                    updateTextureWithPBONoContext(m_texture[1], 1, m_directDataU, m_directLinesizeU);
                    updateTextureWithPBONoContext(m_texture[2], 2, m_directDataV, m_directLinesizeV);
                    m_pboIndex = (m_pboIndex + 1) % PBO_COUNT;
                } else {
                    updateTextureNoContext(m_texture[0], 0, m_directDataY, m_directLinesizeY);
                    updateTextureNoContext(m_texture[1], 1, m_directDataU, m_directLinesizeU);
                    updateTextureNoContext(m_texture[2], 2, m_directDataV, m_directLinesizeV);
                }

                // 标记截图缓存过期，仅在实际截图时才拷贝（每帧省 ~1.4MB memcpy）
                m_grabDataStale = true;
                m_linesizeY = w;
                m_linesizeU = w / 2;
                m_linesizeV = w / 2;

                // 【重要】不释放帧，保留指针供 grabCurrentFrame() 懒截图使用
                // 帧将在下次 submitFrameDirect() 调用时由新帧替换并释放
                m_useDirectFrame = false;  // 仅标记不再需要上传纹理
                m_hasPendingFrame.store(false, std::memory_order_release);
            }
            // 零拷贝帧（QByteArray 模式）
            else if (m_useZeroCopyFrame && !m_zeroCopyFrame.isEmpty()) {
                const int h = m_zcFrameHeight;
                const int uvH = h / 2;
                const int ySize = m_zcLinesizeY * h;
                const int uSize = m_zcLinesizeU * uvH;

                const uint8_t* dataY = reinterpret_cast<const uint8_t*>(m_zeroCopyFrame.constData());
                const uint8_t* dataU = dataY + ySize;
                const uint8_t* dataV = dataU + uSize;

                if (isPBOEnabled() && m_pboInited) {
                    updateTextureWithPBONoContext(m_texture[0], 0, const_cast<quint8*>(dataY), m_zcLinesizeY);
                    updateTextureWithPBONoContext(m_texture[1], 1, const_cast<quint8*>(dataU), m_zcLinesizeU);
                    updateTextureWithPBONoContext(m_texture[2], 2, const_cast<quint8*>(dataV), m_zcLinesizeV);
                    m_pboIndex = (m_pboIndex + 1) % PBO_COUNT;
                } else {
                    updateTextureNoContext(m_texture[0], 0, const_cast<quint8*>(dataY), m_zcLinesizeY);
                    updateTextureNoContext(m_texture[1], 1, const_cast<quint8*>(dataU), m_zcLinesizeU);
                    updateTextureNoContext(m_texture[2], 2, const_cast<quint8*>(dataV), m_zcLinesizeV);
                }

                // 标记截图缓存过期，仅在实际截图时才拷贝
                m_grabDataStale = true;
                m_linesizeY = m_zcFrameWidth;
                m_linesizeU = m_zcFrameWidth / 2;
                m_linesizeV = m_zcFrameWidth / 2;

                m_useZeroCopyFrame = false;
                // 保留 m_zeroCopyFrame 数据用于懒截图，QByteArray 隐式共享无额外内存开销
                m_hasPendingFrame.store(false, std::memory_order_release);
            }
            else if (!m_yuvDataY.empty() && m_frameSize.isValid()) {
                // 回退到旧路径
                if (isPBOEnabled() && m_pboInited) {
                    updateTextureWithPBONoContext(m_texture[0], 0, m_yuvDataY.data(), m_linesizeY);
                    updateTextureWithPBONoContext(m_texture[1], 1, m_yuvDataU.data(), m_linesizeU);
                    updateTextureWithPBONoContext(m_texture[2], 2, m_yuvDataV.data(), m_linesizeV);
                    m_pboIndex = (m_pboIndex + 1) % PBO_COUNT;
                } else {
                    updateTextureNoContext(m_texture[0], 0, m_yuvDataY.data(), m_linesizeY);
                    updateTextureNoContext(m_texture[1], 1, m_yuvDataU.data(), m_linesizeU);
                    updateTextureNoContext(m_texture[2], 2, m_yuvDataV.data(), m_linesizeV);
                }
                m_hasPendingFrame.store(false, std::memory_order_release);
            }
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_texture[0]);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texture[1]);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_texture[2]);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    m_shaderProgram.release();

    double renderTimeMs = m_renderTimer.nsecsElapsed() / 1000000.0;
    m_totalRenderTime += renderTimeMs;

    // 报告渲染延迟
    qsc::PerformanceMonitor::instance().reportRenderLatency(renderTimeMs);

    glFlush();
}

void QYUVOpenGLWidget::resizeGL(int width, int height)
{
    glViewport(0, 0, width, height);
    repaint();
}

void QYUVOpenGLWidget::showEvent(QShowEvent *event)
{
    QOpenGLWidget::showEvent(event);
    // 窗口可见时停止后台刷新定时器
    if (m_backgroundRefreshTimer && m_backgroundRefreshTimer->isActive()) {
        m_backgroundRefreshTimer->stop();
    }
}

void QYUVOpenGLWidget::hideEvent(QHideEvent *event)
{
    QOpenGLWidget::hideEvent(event);
    // 窗口不可见时启动后台刷新定时器，保持帧更新
    if (!m_isDestroying.load(std::memory_order_acquire) && m_backgroundRefreshTimer && !m_backgroundRefreshTimer->isActive()) {
        m_backgroundRefreshTimer->start();
    }
}

void QYUVOpenGLWidget::closeEvent(QCloseEvent *event)
{
    // 窗口关闭时，标记为销毁并停止所有操作
    m_isDestroying.store(true, std::memory_order_release);
    m_hasPendingFrame.store(false, std::memory_order_release);

    // 停止定时器
    if (m_backgroundRefreshTimer && m_backgroundRefreshTimer->isActive()) {
        m_backgroundRefreshTimer->stop();
    }

    QOpenGLWidget::closeEvent(event);
}

// ---------------------------------------------------------
// 初始化着色器
// 配置属性指针和Uniform变量
// ---------------------------------------------------------
void QYUVOpenGLWidget::initShader()
{
    if (QCoreApplication::testAttribute(Qt::AA_UseOpenGLES)) {
        s_fragShader.prepend(R"(
                             precision mediump int;
                             precision mediump float;
                             )");
    }
    m_shaderProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, s_vertShader);
    m_shaderProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, s_fragShader);
    m_shaderProgram.link();
    m_shaderProgram.bind();

    m_shaderProgram.setAttributeBuffer("vertexIn", GL_FLOAT, 0, 3, 3 * sizeof(float));
    m_shaderProgram.enableAttributeArray("vertexIn");

    m_shaderProgram.setAttributeBuffer("textureIn", GL_FLOAT, 12 * sizeof(float), 2, 2 * sizeof(float));
    m_shaderProgram.enableAttributeArray("textureIn");

    m_shaderProgram.setUniformValue("textureY", 0);
    m_shaderProgram.setUniformValue("textureU", 1);
    m_shaderProgram.setUniformValue("textureV", 2);
}

// ---------------------------------------------------------
// 纹理初始化与更新
// 配置纹理参数（过滤、环绕方式）并上传像素数据
// ---------------------------------------------------------
void QYUVOpenGLWidget::initTextures()
{
    glGenTextures(1, &m_texture[0]);
    glBindTexture(GL_TEXTURE_2D, m_texture[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, m_frameSize.width(), m_frameSize.height(), 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &m_texture[1]);
    glBindTexture(GL_TEXTURE_2D, m_texture[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, m_frameSize.width() / 2, m_frameSize.height() / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &m_texture[2]);
    glBindTexture(GL_TEXTURE_2D, m_texture[2]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, m_frameSize.width() / 2, m_frameSize.height() / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);

    m_textureInited = true;
}

void QYUVOpenGLWidget::deInitTextures()
{
    if (QOpenGLFunctions::isInitialized(QOpenGLFunctions::d_ptr)) {
        glDeleteTextures(3, m_texture);
    }
    memset(m_texture, 0, sizeof(m_texture));
    m_textureInited = false;
}

// 不带 makeCurrent/doneCurrent 的纹理更新（用于批量更新）
void QYUVOpenGLWidget::updateTextureNoContext(GLuint texture, quint32 textureType, quint8 *pixels, quint32 stride)
{
    if (!pixels) return;
    QSize size = 0 == textureType ? m_frameSize : m_frameSize / 2;

    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(stride));
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size.width(), size.height(), GL_LUMINANCE, GL_UNSIGNED_BYTE, pixels);
}

void QYUVOpenGLWidget::updateTexture(GLuint texture, quint32 textureType, quint8 *pixels, quint32 stride)
{
    if (!pixels) return;
    QSize size = 0 == textureType ? m_frameSize : m_frameSize / 2;

    makeCurrent();

    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(stride));
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size.width(), size.height(), GL_LUMINANCE, GL_UNSIGNED_BYTE, pixels);

    doneCurrent();
}
