#include <QCoreApplication>
#include <QOpenGLTexture>
#include <QSurfaceFormat>
#include <QOpenGLContext>
#include <QDebug>
#include <QDateTime>
#include <QThread>
#include <QTimer>
#include <QMetaObject>

// 渲染线程优先级提升
#ifdef Q_OS_WIN
#include <windows.h>
#include <avrt.h>   // [超低延迟优化] MMCSS 实时调度
#endif

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
// glMapBufferRange 所需的常量
#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT 0x0002
#endif
#ifndef GL_MAP_INVALIDATE_BUFFER_BIT
#define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008
#endif
#ifndef GL_MAP_UNSYNCHRONIZED_BIT
#define GL_MAP_UNSYNCHRONIZED_BIT 0x0020
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

        // NV12: 从 UV 交织纹理采样 (GL_LUMINANCE_ALPHA: R=U, A=V)
        vec2 uv = texture2D(textureUV, textureOut).ra;
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
    // 关闭 VSync
    // 默认 swapInterval=1 表示等待 VSync，设为 0 表示立即交换
    QSurfaceFormat fmt = format();
    fmt.setSwapInterval(0);
    setFormat(fmt);

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

    // 清理无锁帧槽
    DirectFrameSlot* pending = m_pendingDirectFrame.exchange(nullptr, std::memory_order_acq_rel);
    if (pending) {
        if (pending->releaseCallback) pending->releaseCallback();
        delete pending;
    }
    if (m_renderedFrame) {
        if (m_renderedFrame->releaseCallback) m_renderedFrame->releaseCallback();
        delete m_renderedFrame;
        m_renderedFrame = nullptr;
    }

    // 释放旧路径持有的直接指针帧
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
// 帧提交 - 直接指针版本
// 使用原子指针交换代替 QMutex，
// 消除解码线程与渲染线程之间的锁竞争（节省 0.5-2ms/帧）
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

    // 无锁帧提交：
    // 创建新的帧槽，通过原子 exchange 投入邮箱
    // 如果旧帧还没被渲染线程取走（被跳过），立即释放
    auto* newSlot = new DirectFrameSlot{
        dataY, dataU, dataV,
        width, height,
        linesizeY, linesizeU, linesizeV,
        std::move(releaseCallback)
    };

    DirectFrameSlot* old = m_pendingDirectFrame.exchange(newSlot, std::memory_order_acq_rel);
    if (old) {
        // 旧帧被跳过（渲染来不及），立即释放
        if (old->releaseCallback) old->releaseCallback();
        delete old;
    }

    // 更新帧尺寸（需在 GUI 线程检查，但写入是安全的：只有 paintGL 读取 m_frameSize）
    if (m_frameSize.width() != width || m_frameSize.height() != height) {
        QMutexLocker locker(&m_yuvMutex);
        m_frameSize = QSize(width, height);
        m_needUpdate = true;
    }

    m_hasPendingFrame.store(true, std::memory_order_release);

    // [超低延迟优化] 帧提交触发渲染
    // 使用 repaint() 代替 update()：
    // update() 会被 Qt 合并（多帧到达同一事件循环 tick → 只触发一次 paintGL → 丢帧）
    // repaint() 立即触发 paintGL，确保每帧都被渲染
    // 注：如果当前在 GUI 线程则直接 repaint()，否则通过 QueuedConnection 投递
    if (QThread::currentThread() == this->thread()) {
        repaint();
    } else {
        QMetaObject::invokeMethod(this, "repaint", Qt::QueuedConnection);
    }

    m_totalUploadTime += m_uploadTimer.nsecsElapsed() / 1000000.0;
}

// ---------------------------------------------------------
// 释放当前持有的直接指针帧（窗口关闭时调用）
// ---------------------------------------------------------
void QYUVOpenGLWidget::discardPendingFrame()
{
    // 清理无锁路径的帧
    DirectFrameSlot* pending = m_pendingDirectFrame.exchange(nullptr, std::memory_order_acq_rel);
    if (pending) {
        if (pending->releaseCallback) pending->releaseCallback();
        delete pending;
    }
    if (m_renderedFrame) {
        if (m_renderedFrame->releaseCallback) m_renderedFrame->releaseCallback();
        delete m_renderedFrame;
        m_renderedFrame = nullptr;
    }

    // 旧路径兼容
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

    // 设置纹理采样器 uniform
    m_shaderProgramNV12.bind();
    m_shaderProgramNV12.setUniformValue("textureY", 0);
    m_shaderProgramNV12.setUniformValue("textureUV", 1);
    m_shaderProgramNV12.release();

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
            // 优先从无锁渲染帧读取
            if (m_renderedFrame && m_renderedFrame->dataY) {
                w = m_renderedFrame->width;
                h = m_renderedFrame->height;
                int uvH = h / 2;
                int uvW = w / 2;

                m_yuvDataY.resize(w * h);
                m_yuvDataU.resize(uvW * uvH);
                m_yuvDataV.resize(uvW * uvH);
                for (int y = 0; y < h; ++y)
                    memcpy(m_yuvDataY.data() + y * w, m_renderedFrame->dataY + y * m_renderedFrame->linesizeY, w);
                for (int y = 0; y < uvH; ++y) {
                    memcpy(m_yuvDataU.data() + y * uvW, m_renderedFrame->dataU + y * m_renderedFrame->linesizeU, uvW);
                    memcpy(m_yuvDataV.data() + y * uvW, m_renderedFrame->dataV + y * m_renderedFrame->linesizeV, uvW);
                }
                m_grabDataStale = false;
            }
            // 旧路径：从直接指针帧读取
            else if (m_directDataY) {
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

    // 步骤2: 将新数据写入当前 PBO
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboArray[writeIndex]);

    // glMapBufferRange 异步 DMA 传输
    // GL_MAP_INVALIDATE_BUFFER_BIT: 告诉驱动不需保留旧数据，可以直接分配新内存
    // GL_MAP_UNSYNCHRONIZED_BIT: CPU 写入与 GPU 读取完全异步，不等待
    typedef void* (*PFNGLMAPBUFFERRANGEPROC)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
    typedef GLboolean (*PFNGLUNMAPBUFFERPROC)(GLenum);
    static PFNGLMAPBUFFERRANGEPROC s_glMapBufferRange = nullptr;
    static PFNGLUNMAPBUFFERPROC s_glUnmapBuffer = nullptr;
    static bool s_resolved = false;

    if (!s_resolved) {
        s_resolved = true;
        QOpenGLContext* ctx = QOpenGLContext::currentContext();
        if (ctx) {
            s_glMapBufferRange = reinterpret_cast<PFNGLMAPBUFFERRANGEPROC>(
                ctx->getProcAddress("glMapBufferRange"));
            s_glUnmapBuffer = reinterpret_cast<PFNGLUNMAPBUFFERPROC>(
                ctx->getProcAddress("glUnmapBuffer"));
        }
    }

    const uint8_t* srcData = pixels;
    bool needStrideCopy = (stride != static_cast<quint32>(size.width()));

    if (needStrideCopy) {
        // stride 不匹配时先拷贝到临时缓冲区
        if (m_pboTempBuffer.size() < static_cast<size_t>(dataSize)) {
            m_pboTempBuffer.resize(dataSize);
        }
        for (int y = 0; y < size.height(); ++y) {
            memcpy(m_pboTempBuffer.data() + y * size.width(), pixels + y * stride, size.width());
        }
        srcData = m_pboTempBuffer.data();
    }

    if (s_glMapBufferRange && s_glUnmapBuffer) {
        // 优选路径: glMapBufferRange 真正异步 DMA
        void* ptr = s_glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, dataSize,
            GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
        if (ptr) {
            memcpy(ptr, srcData, dataSize);
            s_glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
        } else {
            // 回退到 glBufferSubData
            glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, dataSize, srcData);
        }
    } else {
        // 回退: glBufferSubData
        glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, dataSize, srcData);
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

    // glMapBufferRange 异步 DMA
    typedef void* (*PFNGLMAPBUFFERRANGEPROC)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
    typedef GLboolean (*PFNGLUNMAPBUFFERPROC)(GLenum);
    static PFNGLMAPBUFFERRANGEPROC s_glMapBufferRange2 = nullptr;
    static PFNGLUNMAPBUFFERPROC s_glUnmapBuffer2 = nullptr;
    static bool s_resolved2 = false;

    if (!s_resolved2) {
        s_resolved2 = true;
        QOpenGLContext* ctx = QOpenGLContext::currentContext();
        if (ctx) {
            s_glMapBufferRange2 = reinterpret_cast<PFNGLMAPBUFFERRANGEPROC>(
                ctx->getProcAddress("glMapBufferRange"));
            s_glUnmapBuffer2 = reinterpret_cast<PFNGLUNMAPBUFFERPROC>(
                ctx->getProcAddress("glUnmapBuffer"));
        }
    }

    const uint8_t* srcData = pixels;
    bool needStrideCopy = (stride != static_cast<quint32>(size.width()));

    if (needStrideCopy) {
        if (m_pboTempBuffer.size() < static_cast<size_t>(dataSize)) {
            m_pboTempBuffer.resize(dataSize);
        }
        for (int y = 0; y < size.height(); ++y) {
            memcpy(m_pboTempBuffer.data() + y * size.width(), pixels + y * stride, size.width());
        }
        srcData = m_pboTempBuffer.data();
    }

    if (s_glMapBufferRange2 && s_glUnmapBuffer2) {
        void* ptr = s_glMapBufferRange2(GL_PIXEL_UNPACK_BUFFER, 0, dataSize,
            GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
        if (ptr) {
            memcpy(ptr, srcData, dataSize);
            s_glUnmapBuffer2(GL_PIXEL_UNPACK_BUFFER);
        } else {
            glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, dataSize, srcData);
        }
    } else {
        glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, dataSize, srcData);
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

    // 提升渲染线程优先级
#ifdef Q_OS_WIN
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    // [超低延迟优化] MMCSS 实时调度：注册为 "Playback" 获得内核级优先级提升
    {
        DWORD taskIndex = 0;
        HANDLE hTask = AvSetMmThreadCharacteristicsA("Playback", &taskIndex);
        if (hTask) {
            AvSetMmThreadPriority(hTask, AVRT_PRIORITY_HIGH);
            RENDER_LOG("MMCSS registered: Playback, index=" + QString::number(taskIndex));
        }
    }
    RENDER_LOG("Render thread priority boosted to ABOVE_NORMAL + MMCSS");
#else
    QThread::currentThread()->setPriority(QThread::HighPriority);
    RENDER_LOG("Render thread priority set to HighPriority");
#endif
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
            // 无锁路径：从原子邮箱取帧
            DirectFrameSlot* directFrame = m_pendingDirectFrame.exchange(nullptr, std::memory_order_acq_rel);

            if (directFrame) {
                // 释放上一帧渲染的帧
                if (m_renderedFrame) {
                    if (m_renderedFrame->releaseCallback) m_renderedFrame->releaseCallback();
                    delete m_renderedFrame;
                }

                const int h = directFrame->height;
                const int w = directFrame->width;

                if (m_frameSize.width() != w || m_frameSize.height() != h) {
                    m_frameSize = QSize(w, h);
                    m_needUpdate = true;
                    // 需要重新初始化纹理/PBO
                    deInitPBO();
                    deInitTextures();
                    initTextures();
                    if (m_pboSupported && m_pboEnabled) {
                        initPBO();
                    }
                    m_needUpdate = false;
                }

                if (isPBOEnabled() && m_pboInited) {
                    updateTextureWithPBONoContext(m_texture[0], 0, directFrame->dataY, directFrame->linesizeY);
                    updateTextureWithPBONoContext(m_texture[1], 1, directFrame->dataU, directFrame->linesizeU);
                    updateTextureWithPBONoContext(m_texture[2], 2, directFrame->dataV, directFrame->linesizeV);
                    m_pboIndex = (m_pboIndex + 1) % PBO_COUNT;
                } else {
                    updateTextureNoContext(m_texture[0], 0, directFrame->dataY, directFrame->linesizeY);
                    updateTextureNoContext(m_texture[1], 1, directFrame->dataU, directFrame->linesizeU);
                    updateTextureNoContext(m_texture[2], 2, directFrame->dataV, directFrame->linesizeV);
                }

                m_grabDataStale = true;
                m_linesizeY = w;
                m_linesizeU = w / 2;
                m_linesizeV = w / 2;

                // 保留帧引用用于截图
                m_renderedFrame = directFrame;
                m_hasPendingFrame.store(false, std::memory_order_release);
            }
            // 回退：非直接帧路径仍用 mutex
            else {
                QMutexLocker locker(&m_yuvMutex);
                // 零拷贝帧（QByteArray 模式）
                if (m_useZeroCopyFrame && !m_zeroCopyFrame.isEmpty()) {
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

                m_grabDataStale = true;
                m_linesizeY = m_zcFrameWidth;
                m_linesizeU = m_zcFrameWidth / 2;
                m_linesizeV = m_zcFrameWidth / 2;

                m_useZeroCopyFrame = false;
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
            } // end else (non-direct fallback path)
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_texture[0]);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texture[1]);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_texture[2]);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // [超低延迟优化] 立即提交 GPU 命令，不等待 buffer swap
        // 确保 Draw Call 尽快送入 GPU 驱动队列
        glFlush();
    }

    m_shaderProgram.release();

    double renderTimeMs = m_renderTimer.nsecsElapsed() / 1000000.0;
    m_totalRenderTime += renderTimeMs;

    // 报告渲染延迟
    qsc::PerformanceMonitor::instance().reportRenderLatency(renderTimeMs);
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
