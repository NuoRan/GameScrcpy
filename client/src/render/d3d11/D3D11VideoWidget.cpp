/**
 * @file D3D11VideoWidget.cpp
 * @brief D3D11 视频渲染控件实现
 *
 * 核心流程:
 *   1. showEvent → 获取 HWND → D3D11Renderer::initialize()
 *   2. submitFrameDirect → 原子邮箱 → postEvent(RenderEvent)
 *   3. event(RenderEvent) → consumeAndRenderFrame() → renderYUV420P + present
 *   4. resizeEvent → D3D11Renderer::resize()
 *   5. closeEvent/destructor → discardPendingFrame + shutdown
 */

#ifdef _WIN32

#include "D3D11VideoWidget.h"
#include "D3D11Renderer.h"
#include "Logger.h"

#include <QCoreApplication>
#include <QThread>
#include <QResizeEvent>
#include <QShowEvent>
#include <algorithm>

// =============================================================================
// 构造 / 析构
// =============================================================================

D3D11VideoWidget::D3D11VideoWidget(QWidget* parent)
    : QWidget(parent)
{
    // 告诉 Qt 不要管这个 widget 的绘制 — D3D11 直接渲染到 HWND
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    // 确保创建原生窗口句柄 (winId 会触发)
    setAttribute(Qt::WA_NativeWindow, true);

    setAutoFillBackground(false);
}

D3D11VideoWidget::~D3D11VideoWidget()
{
    m_isDestroying.store(true, std::memory_order_release);
    discardPendingFrame();

    if (m_renderer) {
        m_renderer->shutdown();
    }
}

// =============================================================================
// 帧尺寸
// =============================================================================

void D3D11VideoWidget::setFrameSize(const QSize& size)
{
    m_frameSize = size;
}

// =============================================================================
// 同步纹理更新 (旧路径)
// =============================================================================

void D3D11VideoWidget::updateTextures(
    uint8_t* dataY, uint8_t* dataU, uint8_t* dataV,
    uint32_t linesizeY, uint32_t linesizeU, uint32_t linesizeV)
{
    if (m_isDestroying.load(std::memory_order_acquire)) return;

    ensureRenderer();
    if (!m_rendererInitialized) return;

    int w = m_frameSize.width();
    int h = m_frameSize.height();
    if (w <= 0 || h <= 0) return;

    cacheFramePlanes(dataY, dataU, dataV, w, h, linesizeY, linesizeU, linesizeV);

    m_renderer->renderYUV420P(dataY, dataU, dataV,
                              linesizeY, linesizeU, linesizeV,
                              w, h);
    m_renderer->present(false);
}

// =============================================================================
// 零拷贝帧提交 (主路径)
// =============================================================================

void D3D11VideoWidget::submitFrameDirect(
    uint8_t* dataY, uint8_t* dataU, uint8_t* dataV,
    int width, int height,
    int linesizeY, int linesizeU, int linesizeV,
    std::function<void()> releaseCallback)
{
    if (m_isDestroying.load(std::memory_order_acquire)) {
        if (releaseCallback) releaseCallback();
        return;
    }

    m_totalFrames++;

    // 创建帧槽, 原子投入邮箱
    auto* newSlot = new D3DFrameSlot{
        dataY, dataU, dataV,
        width, height,
        linesizeY, linesizeU, linesizeV,
        std::move(releaseCallback)
    };

    D3DFrameSlot* old = m_pendingFrame.exchange(newSlot, std::memory_order_acq_rel);
    if (old) {
        // 旧帧被跳过 (渲染来不及), 立即释放
        m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
        if (old->releaseCallback) old->releaseCallback();
        delete old;
    }

    // 更新帧尺寸
    if (m_frameSize.width() != width || m_frameSize.height() != height) {
        m_frameSize = QSize(width, height);
    }

    // 触发渲染事件
    if (QThread::currentThread() == this->thread()) {
        // 同线程: 直接渲染
        consumeAndRenderFrame();
    } else {
        // 跨线程: 投递高优先级事件 (自动合并, 防止重复)
        if (!m_renderEventPending.exchange(true, std::memory_order_acq_rel)) {
            QCoreApplication::postEvent(this,
                new QEvent(static_cast<QEvent::Type>(RenderEventType)),
                Qt::HighEventPriority);
        }
    }
}

// =============================================================================
// 释放持有的帧
// =============================================================================

void D3D11VideoWidget::discardPendingFrame()
{
    // 清理无锁路径的帧
    D3DFrameSlot* pending = m_pendingFrame.exchange(nullptr, std::memory_order_acq_rel);
    if (pending) {
        if (pending->releaseCallback) pending->releaseCallback();
        delete pending;
    }

    // 清理上一帧
    if (m_renderedFrame) {
        if (m_renderedFrame->releaseCallback) m_renderedFrame->releaseCallback();
        delete m_renderedFrame;
        m_renderedFrame = nullptr;
    }
}

// =============================================================================
// 帧抓取
// =============================================================================

QImage D3D11VideoWidget::grabCurrentFrame()
{
    int w = 0;
    int h = 0;
    std::vector<uint8_t> yPlane;
    std::vector<uint8_t> uPlane;
    std::vector<uint8_t> vPlane;

    {
        std::lock_guard<std::mutex> lock(m_yuvMutex);
        if (m_cachedFrameWidth <= 0 || m_cachedFrameHeight <= 0 || m_yuvDataY.empty()) {
            return QImage();
        }

        w = m_cachedFrameWidth;
        h = m_cachedFrameHeight;
        yPlane = m_yuvDataY;
        uPlane = m_yuvDataU;
        vPlane = m_yuvDataV;
    }

    QImage image(w, h, QImage::Format_RGB888);
    const int uvW = w / 2;

    for (int y = 0; y < h; ++y) {
        uchar* rgb = image.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const int yIdx = y * w + x;
            const int uvIdx = (y / 2) * uvW + (x / 2);

            const int Y = yPlane[yIdx];
            const int U = uPlane[uvIdx] - 128;
            const int V = vPlane[uvIdx] - 128;

            rgb[x * 3 + 0] = static_cast<uchar>(qBound(0, static_cast<int>(Y + 1.5748 * V), 255));
            rgb[x * 3 + 1] = static_cast<uchar>(qBound(0, static_cast<int>(Y - 0.1873 * U - 0.4681 * V), 255));
            rgb[x * 3 + 2] = static_cast<uchar>(qBound(0, static_cast<int>(Y + 1.8556 * U), 255));
        }
    }

    return image;
}

GrayFrame D3D11VideoWidget::grabGrayFrame()
{
    GrayFrame frame;

    std::lock_guard<std::mutex> lock(m_yuvMutex);
    if (m_cachedFrameWidth <= 0 || m_cachedFrameHeight <= 0 || m_yuvDataY.empty()) {
        return frame;
    }

    frame.width = m_cachedFrameWidth;
    frame.height = m_cachedFrameHeight;
    frame.data = m_yuvDataY;
    return frame;
}

void D3D11VideoWidget::setSharpenStrength(int strength)
{
    m_pendingSharpenStrength = std::clamp(strength, 0, 100);
    if (m_renderer) {
        m_renderer->setSharpenStrength(m_pendingSharpenStrength / 100.0f);
    }
}

void D3D11VideoWidget::cacheFramePlanes(
    uint8_t* dataY, uint8_t* dataU, uint8_t* dataV,
    int width, int height,
    int linesizeY, int linesizeU, int linesizeV)
{
    if (!dataY || !dataU || !dataV || width <= 0 || height <= 0) {
        return;
    }

    const int uvW = width / 2;
    const int uvH = height / 2;

    std::lock_guard<std::mutex> lock(m_yuvMutex);
    m_yuvDataY.resize(static_cast<size_t>(width) * height);
    m_yuvDataU.resize(static_cast<size_t>(uvW) * uvH);
    m_yuvDataV.resize(static_cast<size_t>(uvW) * uvH);

    for (int row = 0; row < height; ++row) {
        memcpy(m_yuvDataY.data() + static_cast<size_t>(row) * width,
               dataY + static_cast<size_t>(row) * linesizeY,
               static_cast<size_t>(width));
    }

    for (int row = 0; row < uvH; ++row) {
        memcpy(m_yuvDataU.data() + static_cast<size_t>(row) * uvW,
               dataU + static_cast<size_t>(row) * linesizeU,
               static_cast<size_t>(uvW));
        memcpy(m_yuvDataV.data() + static_cast<size_t>(row) * uvW,
               dataV + static_cast<size_t>(row) * linesizeV,
               static_cast<size_t>(uvW));
    }

    m_cachedFrameWidth = width;
    m_cachedFrameHeight = height;
}

// =============================================================================
// QWidget 尺寸提示
// =============================================================================

QSize D3D11VideoWidget::minimumSizeHint() const
{
    return QSize(50, 50);
}

QSize D3D11VideoWidget::sizeHint() const
{
    // 不返回帧尺寸——尺寸由 KeepRatioWidget 的 setGeometry 控制
    // 返回帧尺寸会导致 Qt layout 与手动 geometry 冲突引发 resize 震荡
    return QSize(256, 256);
}

// =============================================================================
// QWidget 事件
// =============================================================================

void D3D11VideoWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // DPI 修正: 传递物理像素给 D3D11 (逻辑 × devicePixelRatio)
    qreal dpr = devicePixelRatio();
    int pw = qRound(event->size().width() * dpr);
    int ph = qRound(event->size().height() * dpr);

    static int resizeLogCount = 0;
    if (++resizeLogCount <= 3) {
        qInfo("[D3D11Widget] resizeEvent #%d: %dx%d -> %dx%d (physical: %dx%d, dpr=%.2f), rendererInit=%d",
              resizeLogCount,
              event->oldSize().width(), event->oldSize().height(),
              event->size().width(), event->size().height(),
              pw, ph, dpr,
              (int)m_rendererInitialized);
    }

    if (m_rendererInitialized && m_renderer) {
        m_renderer->resize(pw, ph);
    } else if (!m_rendererInitialized && isVisible()) {
        // v12: widget 可能在 showEvent 时太小，现在有了有效尺寸，重试初始化
        ensureRenderer();
        if (m_rendererInitialized) {
            consumeAndRenderFrame();
        }
    }
}

void D3D11VideoWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    ensureRenderer();
    // renderer 刚初始化成功时，立即渲染 mailbox 中缓存的帧
    if (m_rendererInitialized) {
        consumeAndRenderFrame();
    }
}

// =============================================================================
// v20: 仅拦截 WM_ERASEBKGND 防止系统擦除闪烁
// D3D11VideoWidget 不能有任何子控件 (会触发 GDI 导致 DXGI 内容消失)
// =============================================================================

bool D3D11VideoWidget::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    Q_UNUSED(eventType);
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_ERASEBKGND) {
        *result = 1;
        return true;
    }
    return QWidget::nativeEvent(eventType, message, result);
}

void D3D11VideoWidget::paintEvent(QPaintEvent*)
{
    // D3D11 直接渲染到 HWND，paintEngine() 返回 nullptr
    // 窗口从遮挡/最小化恢复时，交换链内容可能被系统清除，需重新呈现
    if (m_rendererInitialized && m_renderer) {
        m_renderer->rePresent();
    }
}

bool D3D11VideoWidget::event(QEvent* event)
{
    if (event->type() == static_cast<QEvent::Type>(RenderEventType)) {
        m_renderEventPending.store(false, std::memory_order_release);
        consumeAndRenderFrame();
        return true;
    }
    return QWidget::event(event);
}

// =============================================================================
// 渲染器初始化 (首次显示时懒加载)
// =============================================================================

void D3D11VideoWidget::ensureRenderer()
{
    if (m_rendererInitialized) return;
    if (!isVisible()) return;

    // 安全获取 HWND — 防止 winId() 返回无效句柄
    WId wid = 0;
    try {
        wid = winId();
    } catch (...) {
        LOG_E("[D3D11Widget] winId() threw exception");
        return;
    }

    HWND hwnd = reinterpret_cast<HWND>(wid);
    if (!hwnd || hwnd == INVALID_HANDLE_VALUE) {
        LOG_E("[D3D11Widget] winId() returned invalid HWND: %p", (void*)hwnd);
        return;
    }

    // 验证 HWND 有效性
    if (!::IsWindow(hwnd)) {
        LOG_E("[D3D11Widget] HWND %p is not a valid window", (void*)hwnd);
        return;
    }

    int w = width();
    int h = height();

    // DPI 修正: Qt 6 返回逻辑像素, D3D11 需要物理像素
    // 逻辑 × devicePixelRatio = 物理 (HWND client rect 大小)
    qreal dpr = devicePixelRatio();
    int pw = qRound(w * dpr);
    int ph = qRound(h * dpr);

    // v12: 等待 widget 完成布局后再初始化渲染器
    // showEvent 触发时 widget 可能还没有有效尺寸 (width=1)
    // 推迟到 widget 有合理尺寸时再创建 swap chain
    static constexpr int kMinRendererSize = 8;
    if (pw < kMinRendererSize || ph < kMinRendererSize) {
        LOG_I("[D3D11Widget] Deferring renderer init: widget too small (%dx%d, dpr=%.2f)", pw, ph, dpr);
        return;
    }

    if (!m_renderer) {
        m_renderer = std::make_unique<qsc::D3D11Renderer>();
    }

    if (m_renderer->initialize(hwnd, pw, ph)) {
        m_rendererInitialized = true;
        LOG_I("[D3D11Widget] Renderer initialized (%dx%d, logical=%dx%d, dpr=%.2f)", pw, ph, w, h, dpr);

        // 应用渲染器初始化前缓存的锐化强度
        if (m_pendingSharpenStrength > 0) {
            m_renderer->setSharpenStrength(m_pendingSharpenStrength / 100.0f);
        }
    } else {
        LOG_E("[D3D11Widget] Failed to initialize renderer");
        // 释放失败的 renderer 以便下次重试
        m_renderer.reset();
    }
}

// =============================================================================
// 消费帧并渲染
// =============================================================================

void D3D11VideoWidget::consumeAndRenderFrame()
{
    if (m_isDestroying.load(std::memory_order_acquire)) return;

    ensureRenderer();
    if (!m_rendererInitialized) {
        // 渲染器未就绪，帧保留在 mailbox，等 renderer 初始化后再渲染
        return;
    }

    // 取出最新帧
    D3DFrameSlot* slot = m_pendingFrame.exchange(nullptr, std::memory_order_acq_rel);
    if (!slot) return;

    // 释放上一帧
    if (m_renderedFrame) {
        if (m_renderedFrame->releaseCallback) m_renderedFrame->releaseCallback();
        delete m_renderedFrame;
    }
    m_renderedFrame = slot;

    cacheFramePlanes(
        slot->dataY, slot->dataU, slot->dataV,
        slot->width, slot->height,
        slot->linesizeY, slot->linesizeU, slot->linesizeV
    );

    // 渲染
    m_renderer->renderYUV420P(
        slot->dataY, slot->dataU, slot->dataV,
        slot->linesizeY, slot->linesizeU, slot->linesizeV,
        slot->width, slot->height
    );
    m_renderer->present(false);  // 不等 VSync (由应用控帧率)
}

#endif // _WIN32
