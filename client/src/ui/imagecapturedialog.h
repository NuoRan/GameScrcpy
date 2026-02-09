#ifndef IMAGECAPTUREDIALOG_H
#define IMAGECAPTUREDIALOG_H

#include <QDialog>
#include <QImage>
#include <QRubberBand>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QInputDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QLineEdit>
#include <QDir>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QClipboard>
#include <QApplication>
#include "imagematcher.h"

#ifdef Q_OS_WIN
#include "winutils.h"
#endif

// ---------------------------------------------------------
// 图像截取模式 / Image Capture Mode
// ---------------------------------------------------------
enum class CaptureMode
{
    CaptureTemplate,   // 截取模板图片 / Capture template image
    SelectRegion,      // 选择搜索区域 / Select search region
    SelectPosition     // 选择位置点 / Select position point
};

// ---------------------------------------------------------
// 可缩放的图像显示控件 / Zoomable Image Display Widget
// ---------------------------------------------------------
class ZoomableImageWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ZoomableImageWidget(const QImage& frame, QWidget* parent = nullptr)
        : QWidget(parent), m_frame(frame), m_scale(1.0)
    {
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        // 确保 frame 有效后再更新
        if (!m_frame.isNull() && m_frame.width() > 0 && m_frame.height() > 0) {
            updateScaledImage();
        } else {
            setFixedSize(400, 300);  // 设置默认尺寸
        }
    }

    void setScale(double scale) {
        m_scale = qBound(0.25, scale, 4.0);
        updateScaledImage();
        update();
    }

    double scale() const { return m_scale; }

    QSize scaledSize() const { return m_scaledFrame.size(); }

    // 将控件坐标转换为原始图像坐标（使用四舍五入避免精度丢失）
    QPoint toOriginalCoord(const QPoint& widgetPos) const {
        return QPoint(
            qRound(widgetPos.x() / m_scale),
            qRound(widgetPos.y() / m_scale)
        );
    }

    // 将原始图像坐标转换为控件坐标（使用四舍五入避免精度丢失）
    QPoint toWidgetCoord(const QPoint& origPos) const {
        return QPoint(
            qRound(origPos.x() * m_scale),
            qRound(origPos.y() * m_scale)
        );
    }

    QRect toWidgetRect(const QRect& origRect) const {
        // 使用左上角和尺寸构造，避免 bottomRight 的 +1 问题
        return QRect(toWidgetCoord(origRect.topLeft()),
                     QSize(qRound(origRect.width() * m_scale),
                           qRound(origRect.height() * m_scale)));
    }

    QRect toOriginalRect(const QRect& widgetRect) const {
        // 使用左上角和尺寸构造，避免 QRect(topLeft, bottomRight) 的 +1 问题
        QPoint tl = toOriginalCoord(widgetRect.topLeft());
        int w = qRound(widgetRect.width() / m_scale);
        int h = qRound(widgetRect.height() / m_scale);
        return QRect(tl, QSize(w, h));
    }

    const QImage& frame() const { return m_frame; }

    // 选择框 (原始坐标系)
    void setSelectionRect(const QRect& rect) { m_selectionRect = rect; update(); }
    QRect selectionRect() const { return m_selectionRect; }

    // 当前绘制中的框 (控件坐标系)
    void setDrawingRect(const QRect& rect) { m_drawingRect = rect; update(); }
    QRect drawingRect() const { return m_drawingRect; }

    void setDrawing(bool d) { m_isDrawing = d; }
    bool isDrawing() const { return m_isDrawing; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);

        // 确保有有效的缩放图像
        if (m_scaledFrame.isNull()) {
            painter.fillRect(rect(), QColor(30, 30, 30));
            painter.setPen(Qt::white);
            painter.drawText(rect(), Qt::AlignCenter, "无可用图像");
            return;
        }

        painter.drawImage(0, 0, m_scaledFrame);

        QRect displayRect;
        if (m_isDrawing && m_drawingRect.isValid()) {
            displayRect = m_drawingRect.normalized();
        } else if (m_selectionRect.isValid()) {
            displayRect = toWidgetRect(m_selectionRect);
        }

        if (displayRect.isValid()) {
            // 遮罩区域
            QRegion outside = QRegion(rect()) - QRegion(displayRect);
            painter.setClipRegion(outside);
            painter.fillRect(rect(), QColor(0, 0, 0, 100));
            painter.setClipRect(rect());

            // 选择框
            painter.setPen(QPen(QColor(0, 200, 255), 2, m_isDrawing ? Qt::DashLine : Qt::SolidLine));
            painter.drawRect(displayRect);

            // 绘制8个调整手柄
            if (!m_isDrawing && m_selectionRect.isValid()) {
                painter.setBrush(QColor(0, 200, 255));
                int hs = 6; // 手柄大小
                QRect r = displayRect;
                // 使用 x+width 和 y+height 代替 right() 和 bottom()，避免 -1 问题
                int rRight = r.x() + r.width();
                int rBottom = r.y() + r.height();
                int cx = r.x() + r.width() / 2;
                int cy = r.y() + r.height() / 2;
                // 四角
                painter.drawRect(r.left() - hs/2, r.top() - hs/2, hs, hs);
                painter.drawRect(rRight - hs/2, r.top() - hs/2, hs, hs);
                painter.drawRect(r.left() - hs/2, rBottom - hs/2, hs, hs);
                painter.drawRect(rRight - hs/2, rBottom - hs/2, hs, hs);
                // 四边中点
                painter.drawRect(cx - hs/2, r.top() - hs/2, hs, hs);
                painter.drawRect(cx - hs/2, rBottom - hs/2, hs, hs);
                painter.drawRect(r.left() - hs/2, cy - hs/2, hs, hs);
                painter.drawRect(rRight - hs/2, cy - hs/2, hs, hs);
            }

            // 尺寸提示
            QRect origRect = m_isDrawing ? toOriginalRect(displayRect) : m_selectionRect;
            QString sizeText = QString("%1 x %2").arg(origRect.width()).arg(origRect.height());
            painter.setPen(Qt::white);
            painter.drawText(displayRect.topLeft() + QPoint(5, -5), sizeText);
        }
    }

private:
    void updateScaledImage() {
        if (m_frame.isNull() || m_frame.width() <= 0 || m_frame.height() <= 0) {
            m_scaledFrame = QImage();
            return;
        }
        QSize newSize(static_cast<int>(m_frame.width() * m_scale),
                      static_cast<int>(m_frame.height() * m_scale));
        if (newSize.width() <= 0 || newSize.height() <= 0) {
            return;
        }
        m_scaledFrame = m_frame.scaled(newSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        setFixedSize(m_scaledFrame.size());
    }

    QImage m_frame;
    QImage m_scaledFrame;
    double m_scale;
    QRect m_selectionRect;  // 原始坐标系
    QRect m_drawingRect;    // 控件坐标系 (绘制中)
    bool m_isDrawing = false;
};

// ---------------------------------------------------------
// 图像截取覆盖层 (支持缩放和微调)
// ---------------------------------------------------------
class ImageCaptureOverlay : public QDialog
{
    Q_OBJECT
public:
    explicit ImageCaptureOverlay(const QImage& frame, CaptureMode mode, QWidget* parent = nullptr)
        : QDialog(parent), m_frame(frame), m_mode(mode)
    {
        // 防护：检查 frame 是否有效
        if (frame.isNull() || frame.width() <= 0 || frame.height() <= 0) {
            QMessageBox::warning(parent, "错误", "无效的视频帧");
            QTimer::singleShot(0, this, &QDialog::reject);
            return;
        }

        setWindowTitle(mode == CaptureMode::CaptureTemplate ? "截取模板图片" : "选择搜索区域");
        setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
        resize(1024, 700);

        // 设置 Windows 深色标题栏
#ifdef Q_OS_WIN
        WinUtils::setDarkBorderToWindow(reinterpret_cast<HWND>(winId()), true);
#endif

        // 主布局
        QVBoxLayout* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);

        // 顶部工具栏
        QWidget* toolbar = new QWidget(this);
        toolbar->setStyleSheet("background-color: #18181b;");
        QHBoxLayout* toolbarLayout = new QHBoxLayout(toolbar);
        toolbarLayout->setContentsMargins(10, 5, 10, 5);

        m_hintLabel = new QLabel(this);
        m_hintLabel->setStyleSheet("color: #a1a1aa; font-size: 12px;");
        updateHint();
        toolbarLayout->addWidget(m_hintLabel);

        toolbarLayout->addStretch();

        m_scaleLabel = new QLabel("100%", this);
        m_scaleLabel->setStyleSheet("color: #a1a1aa; font-size: 12px;");
        toolbarLayout->addWidget(m_scaleLabel);

        QPushButton* btnZoomIn = new QPushButton("+", this);
        btnZoomIn->setFixedSize(28, 28);
        btnZoomIn->setStyleSheet("QPushButton { background: #27272a; color: #fafafa; border: 1px solid #3f3f46; border-radius: 6px; font-weight: bold; } QPushButton:hover { background: #3f3f46; border-color: #6366f1; }");
        connect(btnZoomIn, &QPushButton::clicked, [this]() { zoom(0.25); });
        toolbarLayout->addWidget(btnZoomIn);

        QPushButton* btnZoomOut = new QPushButton("-", this);
        btnZoomOut->setFixedSize(28, 28);
        btnZoomOut->setStyleSheet("QPushButton { background: #27272a; color: #fafafa; border: 1px solid #3f3f46; border-radius: 6px; font-weight: bold; } QPushButton:hover { background: #3f3f46; border-color: #6366f1; }");
        connect(btnZoomOut, &QPushButton::clicked, [this]() { zoom(-0.25); });
        toolbarLayout->addWidget(btnZoomOut);

        QPushButton* btnFit = new QPushButton("适应", this);
        btnFit->setFixedSize(50, 28);
        btnFit->setStyleSheet("QPushButton { background: #27272a; color: #fafafa; border: 1px solid #3f3f46; border-radius: 6px; } QPushButton:hover { background: #3f3f46; border-color: #6366f1; }");
        connect(btnFit, &QPushButton::clicked, [this]() { fitToWindow(); });
        toolbarLayout->addWidget(btnFit);

        mainLayout->addWidget(toolbar);

        // 滚动区域
        m_scrollArea = new QScrollArea(this);
        m_scrollArea->setStyleSheet("QScrollArea { background-color: #09090b; border: none; }");
        m_scrollArea->setWidgetResizable(false);
        m_scrollArea->setAlignment(Qt::AlignCenter);

        m_imageWidget = new ZoomableImageWidget(frame, this);
        m_scrollArea->setWidget(m_imageWidget);
        m_imageWidget->installEventFilter(this);
        m_scrollArea->installEventFilter(this);
        m_scrollArea->viewport()->installEventFilter(this);

        // 确保对话框可以接收键盘焦点
        setFocusPolicy(Qt::StrongFocus);
        m_scrollArea->setFocusPolicy(Qt::NoFocus);
        m_imageWidget->setFocusPolicy(Qt::NoFocus);

        mainLayout->addWidget(m_scrollArea, 1);

        // 底部按钮栏
        QWidget* bottomBar = new QWidget(this);
        bottomBar->setStyleSheet("background-color: #18181b;");
        QHBoxLayout* bottomLayout = new QHBoxLayout(bottomBar);
        bottomLayout->setContentsMargins(10, 8, 10, 8);

        bottomLayout->addStretch();

        m_btnConfirm = new QPushButton("确定", this);
        m_btnConfirm->setFixedSize(80, 30);
        m_btnConfirm->setEnabled(false);
        m_btnConfirm->setStyleSheet("QPushButton { background: #6366f1; color: white; border: none; border-radius: 6px; font-weight: bold; } QPushButton:hover { background: #818cf8; } QPushButton:disabled { background: #27272a; color: #71717a; }");
        connect(m_btnConfirm, &QPushButton::clicked, [this]() {
            m_selectedRect = m_imageWidget->selectionRect();
            emit selectionComplete(m_selectedRect);
        });
        bottomLayout->addWidget(m_btnConfirm);

        QPushButton* btnCancel = new QPushButton("取消", this);
        btnCancel->setFixedSize(80, 30);
        btnCancel->setStyleSheet("QPushButton { background: #27272a; color: #fafafa; border: 1px solid #3f3f46; border-radius: 6px; } QPushButton:hover { background: #3f3f46; }");
        connect(btnCancel, &QPushButton::clicked, [this]() {
            emit selectionCanceled();
        });
        bottomLayout->addWidget(btnCancel);

        mainLayout->addWidget(bottomBar);

        // 初始适应窗口
        QTimer::singleShot(0, this, &ImageCaptureOverlay::fitToWindow);
    }

    QRect getSelectedRect() const { return m_selectedRect; }

    QRect getOriginalRect() const { return m_selectedRect; }

    QRectF getNormalizedRect() const {
        if (m_selectedRect.isNull() || m_frame.isNull() || m_frame.width() <= 0 || m_frame.height() <= 0) return QRectF();
        return QRectF(
            static_cast<double>(m_selectedRect.x()) / m_frame.width(),
            static_cast<double>(m_selectedRect.y()) / m_frame.height(),
            static_cast<double>(m_selectedRect.width()) / m_frame.width(),
            static_cast<double>(m_selectedRect.height()) / m_frame.height()
        );
    }

signals:
    void selectionComplete(const QRect& rect);
    void selectionCanceled();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (!m_imageWidget || !m_scrollArea) return QDialog::eventFilter(obj, event);

        if (obj == m_imageWidget) {
            // 鼠标按下事件
            if (event->type() == QEvent::MouseButtonPress) {
                QMouseEvent* me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton) {
                    QPoint pos = me->pos();
                    QRect selRect = m_imageWidget->selectionRect();

                    if (selRect.isValid()) {
                        // 检查是否点击了调整手柄
                        m_resizeHandle = hitTestHandle(pos);
                        if (m_resizeHandle != Handle::None) {
                            m_resizing = true;
                            m_dragStart = pos;
                            // 保存原始坐标系的矩形，避免累积转换误差
                            m_originalRectInOrigCoord = selRect;
                            return true;
                        }

                        // 检查是否在选择框内 (拖动移动)
                        QRect widgetRect = m_imageWidget->toWidgetRect(selRect);
                        if (widgetRect.contains(pos)) {
                            m_dragging = true;
                            m_dragStart = pos;
                            m_originalRectInOrigCoord = selRect;
                            m_imageWidget->setCursor(Qt::ClosedHandCursor);
                            return true;
                        }
                    }

                    // 开始新的选择
                    m_selecting = true;
                    m_startPoint = pos;
                    m_imageWidget->setDrawing(true);
                    m_imageWidget->setDrawingRect(QRect(pos, QSize()));
                    return true;
                }
            }
            // 鼠标移动事件
            else if (event->type() == QEvent::MouseMove) {
                QMouseEvent* me = static_cast<QMouseEvent*>(event);
                QPoint pos = me->pos();

                if (m_selecting) {
                    m_imageWidget->setDrawingRect(QRect(m_startPoint, pos).normalized());
                    return true;
                }
                else if (m_dragging) {
                    // 在原始坐标系中计算位移
                    QPoint deltaWidget = pos - m_dragStart;
                    int deltaX = static_cast<int>(deltaWidget.x() / m_imageWidget->scale());
                    int deltaY = static_cast<int>(deltaWidget.y() / m_imageWidget->scale());

                    QRect newRect = m_originalRectInOrigCoord.translated(deltaX, deltaY);
                    // 边界检查 (原始坐标系)
                    int maxX = m_imageWidget->frame().width() - newRect.width();
                    int maxY = m_imageWidget->frame().height() - newRect.height();
                    newRect.moveLeft(qBound(0, newRect.left(), maxX));
                    newRect.moveTop(qBound(0, newRect.top(), maxY));
                    m_imageWidget->setSelectionRect(newRect);
                    return true;
                }
                else if (m_resizing) {
                    // 在原始坐标系中计算位移
                    QPoint deltaWidget = pos - m_dragStart;
                    int deltaX = static_cast<int>(deltaWidget.x() / m_imageWidget->scale());
                    int deltaY = static_cast<int>(deltaWidget.y() / m_imageWidget->scale());

                    // 使用原始坐标系的矩形边界
                    int left = m_originalRectInOrigCoord.left();
                    int top = m_originalRectInOrigCoord.top();
                    int right = m_originalRectInOrigCoord.left() + m_originalRectInOrigCoord.width();
                    int bottom = m_originalRectInOrigCoord.top() + m_originalRectInOrigCoord.height();

                    switch (m_resizeHandle) {
                        case Handle::TopLeft:
                            left += deltaX;
                            top += deltaY;
                            break;
                        case Handle::Top:
                            top += deltaY;
                            break;
                        case Handle::TopRight:
                            right += deltaX;
                            top += deltaY;
                            break;
                        case Handle::Right:
                            right += deltaX;
                            break;
                        case Handle::BottomRight:
                            right += deltaX;
                            bottom += deltaY;
                            break;
                        case Handle::Bottom:
                            bottom += deltaY;
                            break;
                        case Handle::BottomLeft:
                            left += deltaX;
                            bottom += deltaY;
                            break;
                        case Handle::Left:
                            left += deltaX;
                            break;
                        default: break;
                    }

                    // 确保 left < right, top < bottom
                    if (left > right) std::swap(left, right);
                    if (top > bottom) std::swap(top, bottom);

                    QRect newRect(left, top, right - left, bottom - top);
                    if (newRect.width() > 5 && newRect.height() > 5) {
                        m_imageWidget->setSelectionRect(newRect);
                    }
                    return true;
                }
                else {
                    // 更新鼠标光标
                    updateCursor(pos);
                }
            }
            // 鼠标释放事件
            else if (event->type() == QEvent::MouseButtonRelease) {
                QMouseEvent* me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton) {
                    if (m_selecting) {
                        m_selecting = false;
                        m_imageWidget->setDrawing(false);
                        QRect drawRect = m_imageWidget->drawingRect();
                        if (drawRect.width() > 5 && drawRect.height() > 5) {
                            m_imageWidget->setSelectionRect(m_imageWidget->toOriginalRect(drawRect));
                            m_btnConfirm->setEnabled(true);
                            updateHint();
                        }
                        m_imageWidget->setDrawingRect(QRect());
                    }
                    else if (m_dragging || m_resizing) {
                        m_dragging = false;
                        m_resizing = false;
                        m_resizeHandle = Handle::None;
                        m_imageWidget->setCursor(Qt::CrossCursor);
                    }
                    return true;
                }
            }
            else if (event->type() == QEvent::Wheel) {
                QWheelEvent* we = static_cast<QWheelEvent*>(event);
                // Ctrl + 滚轮 或 直接滚轮 都可以缩放
                double delta = we->angleDelta().y() > 0 ? 0.15 : -0.15;
                // 获取鼠标在视口中的位置
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
                QPoint mousePos = we->position().toPoint();
#else
                QPoint mousePos = we->pos();
#endif
                zoom(delta, mousePos);
                return true;
            }
        }
        return QDialog::eventFilter(obj, event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            emit selectionCanceled();
        } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (m_btnConfirm->isEnabled()) {
                m_btnConfirm->click();
            }
        } else {
            QDialog::keyPressEvent(event);
        }
    }

    void keyReleaseEvent(QKeyEvent* event) override {
        QDialog::keyReleaseEvent(event);
    }

    void wheelEvent(QWheelEvent* event) override {
        // 全局滚轮缩放（基于鼠标位置）
        double delta = event->angleDelta().y() > 0 ? 0.15 : -0.15;
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
        QPoint mousePos = m_imageWidget->mapFromGlobal(event->globalPosition().toPoint());
#else
        QPoint mousePos = m_imageWidget->mapFromGlobal(event->globalPos());
#endif
        zoom(delta, mousePos);
        event->accept();
    }

private:
    enum class Handle { None, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };

    Handle hitTestHandle(const QPoint& pos) {
        if (!m_imageWidget) return Handle::None;
        QRect selRect = m_imageWidget->selectionRect();
        if (!selRect.isValid()) return Handle::None;

        QRect r = m_imageWidget->toWidgetRect(selRect);
        int hs = 8; // 手柄点击范围
        // 使用 x+width 和 y+height 代替 right() 和 bottom()，避免 -1 问题
        int rRight = r.x() + r.width();
        int rBottom = r.y() + r.height();

        if (QRect(r.left() - hs, r.top() - hs, hs*2, hs*2).contains(pos)) return Handle::TopLeft;
        if (QRect(rRight - hs, r.top() - hs, hs*2, hs*2).contains(pos)) return Handle::TopRight;
        if (QRect(r.left() - hs, rBottom - hs, hs*2, hs*2).contains(pos)) return Handle::BottomLeft;
        if (QRect(rRight - hs, rBottom - hs, hs*2, hs*2).contains(pos)) return Handle::BottomRight;
        if (QRect(r.center().x() - hs, r.top() - hs, hs*2, hs*2).contains(pos)) return Handle::Top;
        if (QRect(r.center().x() - hs, rBottom - hs, hs*2, hs*2).contains(pos)) return Handle::Bottom;
        if (QRect(r.left() - hs, r.center().y() - hs, hs*2, hs*2).contains(pos)) return Handle::Left;
        if (QRect(rRight - hs, r.center().y() - hs, hs*2, hs*2).contains(pos)) return Handle::Right;

        return Handle::None;
    }

    void updateCursor(const QPoint& pos) {
        if (!m_imageWidget) return;
        Handle h = hitTestHandle(pos);
        QRect selRect = m_imageWidget->selectionRect();
        QRect widgetRect = selRect.isValid() ? m_imageWidget->toWidgetRect(selRect) : QRect();

        if (h == Handle::TopLeft || h == Handle::BottomRight) {
            m_imageWidget->setCursor(Qt::SizeFDiagCursor);
        } else if (h == Handle::TopRight || h == Handle::BottomLeft) {
            m_imageWidget->setCursor(Qt::SizeBDiagCursor);
        } else if (h == Handle::Top || h == Handle::Bottom) {
            m_imageWidget->setCursor(Qt::SizeVerCursor);
        } else if (h == Handle::Left || h == Handle::Right) {
            m_imageWidget->setCursor(Qt::SizeHorCursor);
        } else if (widgetRect.contains(pos)) {
            m_imageWidget->setCursor(Qt::OpenHandCursor);
        } else {
            m_imageWidget->setCursor(Qt::CrossCursor);
        }
    }

    void zoom(double delta, const QPoint& mousePos = QPoint()) {
        if (!m_imageWidget || !m_scrollArea || !m_scaleLabel) return;
        double oldScale = m_imageWidget->scale();
        double newScale = qBound(0.25, oldScale + delta, 4.0);

        if (qFuzzyCompare(oldScale, newScale)) return;

        // 如果提供了鼠标位置，基于鼠标位置缩放
        if (!mousePos.isNull()) {
            // mousePos 已经是相对于 m_imageWidget 的坐标
            // 获取当前滚动位置
            int oldScrollX = m_scrollArea->horizontalScrollBar()->value();
            int oldScrollY = m_scrollArea->verticalScrollBar()->value();

            // 计算鼠标位置对应的原始图像坐标
            double imageX = mousePos.x() / oldScale;
            double imageY = mousePos.y() / oldScale;

            // 应用新的缩放
            m_imageWidget->setScale(newScale);

            // 计算新缩放下鼠标位置对应的控件坐标
            double newWidgetX = imageX * newScale;
            double newWidgetY = imageY * newScale;

            // 计算鼠标在视口中的位置
            QPoint viewportMousePos = mousePos - QPoint(oldScrollX, oldScrollY);

            // 调整滚动条位置，使鼠标下的图像点保持不变
            int newScrollX = static_cast<int>(newWidgetX - viewportMousePos.x());
            int newScrollY = static_cast<int>(newWidgetY - viewportMousePos.y());

            m_scrollArea->horizontalScrollBar()->setValue(newScrollX);
            m_scrollArea->verticalScrollBar()->setValue(newScrollY);
        } else {
            m_imageWidget->setScale(newScale);
        }

        m_scaleLabel->setText(QString("%1%").arg(static_cast<int>(m_imageWidget->scale() * 100)));
    }

    void fitToWindow() {
        if (!m_scrollArea || !m_imageWidget || !m_scaleLabel || m_frame.isNull()) return;
        QSize viewSize = m_scrollArea->viewport()->size();
        QSize imgSize = m_frame.size();
        if (imgSize.width() <= 0 || imgSize.height() <= 0) return;
        double scaleW = static_cast<double>(viewSize.width() - 20) / imgSize.width();
        double scaleH = static_cast<double>(viewSize.height() - 20) / imgSize.height();
        double scale = qMin(scaleW, scaleH);
        m_imageWidget->setScale(scale);
        m_scaleLabel->setText(QString("%1%").arg(static_cast<int>(scale * 100)));
    }

    void updateHint() {
        if (!m_hintLabel) return;
        bool hasSelection = m_imageWidget && m_imageWidget->selectionRect().isValid();
        QString hint = m_mode == CaptureMode::CaptureTemplate
            ? "拖动鼠标框选模板区域"
            : "拖动鼠标框选搜索区域";
        if (hasSelection) {
            hint += " | 拖动选择框移动，拖动手柄调整大小";
        }
        hint += " | 滚轮缩放 | ESC取消";
        m_hintLabel->setText(hint);
    }

private:
    QImage m_frame;
    CaptureMode m_mode;
    QRect m_selectedRect;

    QScrollArea* m_scrollArea = nullptr;
    ZoomableImageWidget* m_imageWidget = nullptr;
    QLabel* m_hintLabel = nullptr;
    QLabel* m_scaleLabel = nullptr;
    QPushButton* m_btnConfirm = nullptr;

    bool m_selecting = false;
    bool m_dragging = false;
    bool m_resizing = false;
    QPoint m_startPoint;
    QPoint m_dragStart;
    QRect m_originalRectInOrigCoord;  // 在原始坐标系中的矩形，避免转换误差
    Handle m_resizeHandle = Handle::None;
};

// ---------------------------------------------------------
// 位置结果对话框
// 显示选取的位置坐标比例，提供复制功能
// ---------------------------------------------------------
class PositionResultDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PositionResultDialog(double xRatio, double yRatio, QWidget* parent = nullptr)
        : QDialog(parent), m_xRatio(xRatio), m_yRatio(yRatio)
    {
        setWindowTitle("位置坐标");
        setFixedSize(280, 150);
        setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

        // 设置 Windows 深色标题栏
#ifdef Q_OS_WIN
        WinUtils::setDarkBorderToWindow(reinterpret_cast<HWND>(winId()), true);
#endif

        QVBoxLayout* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(20, 20, 20, 15);
        mainLayout->setSpacing(15);

        // 坐标显示
        QLabel* titleLabel = new QLabel("获取的位置坐标 (比例值)", this);
        titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #fafafa;");
        titleLabel->setAlignment(Qt::AlignCenter);
        mainLayout->addWidget(titleLabel);

        // X, Y 坐标显示
        QString xStr = QString::number(xRatio, 'f', 4);
        QString yStr = QString::number(yRatio, 'f', 4);
        m_coordText = QString("x: %1,  y: %2").arg(xStr, yStr);

        QLabel* coordLabel = new QLabel(m_coordText, this);
        coordLabel->setStyleSheet(
            "font-size: 16px; font-family: 'Consolas', 'Monaco', monospace; "
            "color: #6366f1; background-color: #27272a; padding: 12px; "
            "border-radius: 6px; border: 1px solid #3f3f46;"
        );
        coordLabel->setAlignment(Qt::AlignCenter);
        coordLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        mainLayout->addWidget(coordLabel);

        mainLayout->addStretch();

        // 按钮栏
        QHBoxLayout* btnLayout = new QHBoxLayout();
        btnLayout->setSpacing(10);

        btnLayout->addStretch();

        QPushButton* btnCopy = new QPushButton("复制", this);
        btnCopy->setFixedSize(80, 32);
        btnCopy->setStyleSheet(
            "QPushButton { background-color: #6366f1; color: white; border: none; "
            "border-radius: 6px; font-weight: bold; font-size: 13px; }"
            "QPushButton:hover { background-color: #818cf8; }"
        );
        connect(btnCopy, &QPushButton::clicked, this, &PositionResultDialog::onCopy);
        btnLayout->addWidget(btnCopy);

        QPushButton* btnCancel = new QPushButton("关闭", this);
        btnCancel->setFixedSize(80, 32);
        btnCancel->setStyleSheet(
            "QPushButton { background-color: #27272a; color: #fafafa; "
            "border: 1px solid #3f3f46; border-radius: 6px; font-size: 13px; }"
            "QPushButton:hover { background-color: #3f3f46; }"
        );
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
        btnLayout->addWidget(btnCancel);

        mainLayout->addLayout(btnLayout);

        // 对话框样式
        setStyleSheet("QDialog { background-color: #18181b; }");
    }

    double xRatio() const { return m_xRatio; }
    double yRatio() const { return m_yRatio; }

private slots:
    void onCopy() {
        QString copyText = QString("%1, %2").arg(
            QString::number(m_xRatio, 'f', 4),
            QString::number(m_yRatio, 'f', 4)
        );
        QApplication::clipboard()->setText(copyText);
        accept();
    }

private:
    double m_xRatio;
    double m_yRatio;
    QString m_coordText;
};

// ---------------------------------------------------------
// 位置选择覆盖层
// 点击选择单个位置点
// ---------------------------------------------------------
class PositionSelectOverlay : public QDialog
{
    Q_OBJECT
public:
    explicit PositionSelectOverlay(const QImage& frame, QWidget* parent = nullptr)
        : QDialog(parent), m_frame(frame)
    {
        if (frame.isNull() || frame.width() <= 0 || frame.height() <= 0) {
            QMessageBox::warning(parent, "错误", "无效的视频帧");
            QTimer::singleShot(0, this, &QDialog::reject);
            return;
        }

        setWindowTitle("获取位置 - 点击选择位置");
        setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
        resize(1024, 700);

        // 设置 Windows 深色标题栏
#ifdef Q_OS_WIN
        WinUtils::setDarkBorderToWindow(reinterpret_cast<HWND>(winId()), true);
#endif

        QVBoxLayout* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);

        // 顶部工具栏
        QWidget* toolbar = new QWidget(this);
        toolbar->setStyleSheet("background-color: #18181b;");
        QHBoxLayout* toolbarLayout = new QHBoxLayout(toolbar);
        toolbarLayout->setContentsMargins(10, 5, 10, 5);

        m_hintLabel = new QLabel("点击图像选择位置 | 滚轮缩放 | ESC取消", this);
        m_hintLabel->setStyleSheet("color: #a1a1aa; font-size: 12px;");
        toolbarLayout->addWidget(m_hintLabel);

        toolbarLayout->addStretch();

        m_scaleLabel = new QLabel("100%", this);
        m_scaleLabel->setStyleSheet("color: #a1a1aa; font-size: 12px;");
        toolbarLayout->addWidget(m_scaleLabel);

        QPushButton* btnZoomIn = new QPushButton("+", this);
        btnZoomIn->setFixedSize(28, 28);
        btnZoomIn->setStyleSheet("QPushButton { background: #27272a; color: #fafafa; border: 1px solid #3f3f46; border-radius: 6px; font-weight: bold; } QPushButton:hover { background: #3f3f46; border-color: #6366f1; }");
        connect(btnZoomIn, &QPushButton::clicked, [this]() { zoom(0.25); });
        toolbarLayout->addWidget(btnZoomIn);

        QPushButton* btnZoomOut = new QPushButton("-", this);
        btnZoomOut->setFixedSize(28, 28);
        btnZoomOut->setStyleSheet("QPushButton { background: #27272a; color: #fafafa; border: 1px solid #3f3f46; border-radius: 6px; font-weight: bold; } QPushButton:hover { background: #3f3f46; border-color: #6366f1; }");
        connect(btnZoomOut, &QPushButton::clicked, [this]() { zoom(-0.25); });
        toolbarLayout->addWidget(btnZoomOut);

        QPushButton* btnFit = new QPushButton("适应", this);
        btnFit->setFixedSize(50, 28);
        btnFit->setStyleSheet("QPushButton { background: #27272a; color: #fafafa; border: 1px solid #3f3f46; border-radius: 6px; } QPushButton:hover { background: #3f3f46; border-color: #6366f1; }");
        connect(btnFit, &QPushButton::clicked, [this]() { fitToWindow(); });
        toolbarLayout->addWidget(btnFit);

        mainLayout->addWidget(toolbar);

        // 滚动区域
        m_scrollArea = new QScrollArea(this);
        m_scrollArea->setStyleSheet("QScrollArea { background-color: #09090b; border: none; }");
        m_scrollArea->setWidgetResizable(false);
        m_scrollArea->setAlignment(Qt::AlignCenter);

        m_imageWidget = new PositionImageWidget(frame, this);
        m_scrollArea->setWidget(m_imageWidget);
        m_imageWidget->installEventFilter(this);
        m_scrollArea->installEventFilter(this);
        m_scrollArea->viewport()->installEventFilter(this);

        setFocusPolicy(Qt::StrongFocus);
        m_scrollArea->setFocusPolicy(Qt::NoFocus);
        m_imageWidget->setFocusPolicy(Qt::NoFocus);

        mainLayout->addWidget(m_scrollArea, 1);

        // 底部按钮栏
        QWidget* bottomBar = new QWidget(this);
        bottomBar->setStyleSheet("background-color: #18181b;");
        QHBoxLayout* bottomLayout = new QHBoxLayout(bottomBar);
        bottomLayout->setContentsMargins(10, 8, 10, 8);

        m_coordLabel = new QLabel("未选择位置", this);
        m_coordLabel->setStyleSheet("color: #a1a1aa; font-size: 12px;");
        bottomLayout->addWidget(m_coordLabel);

        bottomLayout->addStretch();

        m_btnConfirm = new QPushButton("确定", this);
        m_btnConfirm->setFixedSize(80, 30);
        m_btnConfirm->setEnabled(false);
        m_btnConfirm->setStyleSheet("QPushButton { background: #6366f1; color: white; border: none; border-radius: 6px; font-weight: bold; } QPushButton:hover { background: #818cf8; } QPushButton:disabled { background: #27272a; color: #71717a; }");
        connect(m_btnConfirm, &QPushButton::clicked, [this]() {
            emit positionSelected(m_selectedPos);
        });
        bottomLayout->addWidget(m_btnConfirm);

        QPushButton* btnCancel = new QPushButton("取消", this);
        btnCancel->setFixedSize(80, 30);
        btnCancel->setStyleSheet("QPushButton { background: #27272a; color: #fafafa; border: 1px solid #3f3f46; border-radius: 6px; } QPushButton:hover { background: #3f3f46; }");
        connect(btnCancel, &QPushButton::clicked, [this]() {
            emit selectionCanceled();
        });
        bottomLayout->addWidget(btnCancel);

        mainLayout->addWidget(bottomBar);

        QTimer::singleShot(0, this, &PositionSelectOverlay::fitToWindow);
    }

    QPointF getSelectedPosition() const { return m_selectedPos; }

signals:
    void positionSelected(const QPointF& pos);
    void selectionCanceled();

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            emit selectionCanceled();
        } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (m_hasSelection) {
                emit positionSelected(m_selectedPos);
            }
        }
        QDialog::keyPressEvent(event);
    }

    bool eventFilter(QObject* obj, QEvent* event) override {
        if (!m_imageWidget || !m_scrollArea) return QDialog::eventFilter(obj, event);

        if (obj == m_imageWidget) {
            if (event->type() == QEvent::MouseButtonPress) {
                QMouseEvent* me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton) {
                    QPoint widgetPos = me->pos();
                    QPoint origPos = m_imageWidget->toOriginalCoord(widgetPos);

                    // 计算比例坐标
                    m_selectedPos = QPointF(
                        static_cast<double>(origPos.x()) / m_frame.width(),
                        static_cast<double>(origPos.y()) / m_frame.height()
                    );

                    // 限制在 0-1 范围内
                    m_selectedPos.setX(qBound(0.0, m_selectedPos.x(), 1.0));
                    m_selectedPos.setY(qBound(0.0, m_selectedPos.y(), 1.0));

                    m_hasSelection = true;
                    m_imageWidget->setMarkerPosition(origPos);
                    m_btnConfirm->setEnabled(true);

                    // 更新坐标显示
                    m_coordLabel->setText(QString("位置: x=%1, y=%2")
                        .arg(QString::number(m_selectedPos.x(), 'f', 4))
                        .arg(QString::number(m_selectedPos.y(), 'f', 4)));
                    m_coordLabel->setStyleSheet("color: #6366f1; font-size: 12px; font-weight: bold;");

                    return true;
                }
            }
            else if (event->type() == QEvent::Wheel) {
                QWheelEvent* we = static_cast<QWheelEvent*>(event);
                double delta = we->angleDelta().y() > 0 ? 0.15 : -0.15;
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
                QPoint mousePos = we->position().toPoint();
#else
                QPoint mousePos = we->pos();
#endif
                zoom(delta, mousePos);
                return true;
            }
        }
        return QDialog::eventFilter(obj, event);
    }

private:
    // 位置图像显示控件 (内部类)
    class PositionImageWidget : public QWidget
    {
    public:
        explicit PositionImageWidget(const QImage& frame, QWidget* parent = nullptr)
            : QWidget(parent), m_frame(frame), m_scale(1.0), m_hasMarker(false)
        {
            setMouseTracking(true);
            setCursor(Qt::CrossCursor);
            if (!m_frame.isNull() && m_frame.width() > 0 && m_frame.height() > 0) {
                updateScaledImage();
            } else {
                setFixedSize(400, 300);
            }
        }

        void setScale(double scale) {
            m_scale = qBound(0.25, scale, 4.0);
            updateScaledImage();
            update();
        }

        double scale() const { return m_scale; }

        QPoint toOriginalCoord(const QPoint& widgetPos) const {
            return QPoint(
                static_cast<int>(widgetPos.x() / m_scale),
                static_cast<int>(widgetPos.y() / m_scale)
            );
        }

        QPoint toWidgetCoord(const QPoint& origPos) const {
            return QPoint(
                static_cast<int>(origPos.x() * m_scale),
                static_cast<int>(origPos.y() * m_scale)
            );
        }

        void setMarkerPosition(const QPoint& pos) {
            m_markerPos = pos;
            m_hasMarker = true;
            update();
        }

        const QImage& frame() const { return m_frame; }

    protected:
        void paintEvent(QPaintEvent*) override {
            QPainter painter(this);

            if (m_scaledFrame.isNull()) {
                painter.fillRect(rect(), QColor(30, 30, 30));
                painter.setPen(Qt::white);
                painter.drawText(rect(), Qt::AlignCenter, "无可用图像");
                return;
            }

            painter.drawImage(0, 0, m_scaledFrame);

            // 绘制标记点
            if (m_hasMarker) {
                QPoint widgetPos = toWidgetCoord(m_markerPos);

                // 十字线
                painter.setPen(QPen(QColor(255, 100, 100), 2));
                painter.drawLine(widgetPos.x() - 15, widgetPos.y(), widgetPos.x() + 15, widgetPos.y());
                painter.drawLine(widgetPos.x(), widgetPos.y() - 15, widgetPos.x(), widgetPos.y() + 15);

                // 圆圈
                painter.setPen(QPen(QColor(99, 102, 241), 3));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(widgetPos, 12, 12);

                // 中心点
                painter.setBrush(QColor(99, 102, 241));
                painter.drawEllipse(widgetPos, 4, 4);
            }
        }

    private:
        void updateScaledImage() {
            if (m_frame.isNull() || m_frame.width() <= 0 || m_frame.height() <= 0) {
                m_scaledFrame = QImage();
                return;
            }
            QSize newSize(static_cast<int>(m_frame.width() * m_scale),
                          static_cast<int>(m_frame.height() * m_scale));
            if (newSize.width() <= 0 || newSize.height() <= 0) return;
            m_scaledFrame = m_frame.scaled(newSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            setFixedSize(m_scaledFrame.size());
        }

        QImage m_frame;
        QImage m_scaledFrame;
        double m_scale;
        QPoint m_markerPos;
        bool m_hasMarker;
    };

    void zoom(double delta, const QPoint& mousePos = QPoint()) {
        if (!m_imageWidget) return;

        double oldScale = m_imageWidget->scale();
        double newScale = qBound(0.25, oldScale + delta, 4.0);

        if (qFuzzyCompare(oldScale, newScale)) return;

        QPoint scrollPos = m_scrollArea->viewport()->mapFromGlobal(m_imageWidget->mapToGlobal(mousePos));
        QScrollBar* hBar = m_scrollArea->horizontalScrollBar();
        QScrollBar* vBar = m_scrollArea->verticalScrollBar();
        QPoint oldScrollValue(hBar->value(), vBar->value());

        m_imageWidget->setScale(newScale);
        m_scaleLabel->setText(QString("%1%").arg(static_cast<int>(newScale * 100)));

        if (!mousePos.isNull()) {
            double ratio = newScale / oldScale;
            int newHValue = static_cast<int>(mousePos.x() * ratio - scrollPos.x() + oldScrollValue.x() * ratio);
            int newVValue = static_cast<int>(mousePos.y() * ratio - scrollPos.y() + oldScrollValue.y() * ratio);
            hBar->setValue(newHValue);
            vBar->setValue(newVValue);
        }
    }

    void fitToWindow() {
        if (!m_imageWidget || !m_scrollArea) return;
        QSize viewSize = m_scrollArea->viewport()->size();
        QSize imgSize = m_imageWidget->frame().size();
        if (imgSize.isEmpty()) return;
        double scaleW = static_cast<double>(viewSize.width() - 20) / imgSize.width();
        double scaleH = static_cast<double>(viewSize.height() - 20) / imgSize.height();
        double scale = qMin(scaleW, scaleH);
        m_imageWidget->setScale(scale);
        m_scaleLabel->setText(QString("%1%").arg(static_cast<int>(scale * 100)));
    }

private:
    QImage m_frame;
    QPointF m_selectedPos;
    bool m_hasSelection = false;

    QScrollArea* m_scrollArea = nullptr;
    PositionImageWidget* m_imageWidget = nullptr;
    QLabel* m_hintLabel = nullptr;
    QLabel* m_scaleLabel = nullptr;
    QLabel* m_coordLabel = nullptr;
    QPushButton* m_btnConfirm = nullptr;
};

// ---------------------------------------------------------
// 图像截取对话框
// 管理截取模板和选择区域的完整流程
// ---------------------------------------------------------
class ImageCaptureDialog : public QObject
{
    Q_OBJECT
public:
    explicit ImageCaptureDialog(QWidget* parent = nullptr) : QObject(parent), m_parentWidget(parent) {}

    // =========================================================
    // 截取模板图片流程
    // =========================================================
    void captureTemplate(const QImage& currentFrame) {
        if (currentFrame.isNull()) {
            QMessageBox::warning(m_parentWidget, "错误", "当前没有可用的视频帧");
            return;
        }

        m_currentFrame = currentFrame;

        // 创建覆盖层
        ImageCaptureOverlay* overlay = new ImageCaptureOverlay(currentFrame, CaptureMode::CaptureTemplate);
        overlay->setWindowModality(Qt::ApplicationModal);

        connect(overlay, &ImageCaptureOverlay::selectionComplete, this, [this, overlay](const QRect&) {
            QRect originalRect = overlay->getOriginalRect();
            overlay->close();
            overlay->deleteLater();

            if (originalRect.isValid()) {
                // 裁剪图像
                QImage cropped = m_currentFrame.copy(originalRect);

                // 请求用户输入文件名
                saveTemplateWithName(cropped);
            }
        });

        connect(overlay, &ImageCaptureOverlay::selectionCanceled, overlay, [overlay]() {
            overlay->close();
            overlay->deleteLater();
        });

        overlay->show();
        overlay->activateWindow();
    }

    // =========================================================
    // 选择搜索区域流程
    // =========================================================
    void selectRegion(const QImage& currentFrame) {
        if (currentFrame.isNull()) {
            QMessageBox::warning(m_parentWidget, "错误", "当前没有可用的视频帧");
            return;
        }

        m_currentFrame = currentFrame;

        // 创建覆盖层
        ImageCaptureOverlay* overlay = new ImageCaptureOverlay(currentFrame, CaptureMode::SelectRegion);
        overlay->setWindowModality(Qt::ApplicationModal);

        connect(overlay, &ImageCaptureOverlay::selectionComplete, this, [this, overlay](const QRect&) {
            QRectF normalizedRect = overlay->getNormalizedRect();
            overlay->close();
            overlay->deleteLater();

            if (normalizedRect.isValid()) {
                // 显示选项对话框
                showRegionOptions(normalizedRect);
            }
        });

        connect(overlay, &ImageCaptureOverlay::selectionCanceled, overlay, [overlay]() {
            overlay->close();
            overlay->deleteLater();
        });

        overlay->show();
        overlay->activateWindow();
    }

    // =========================================================
    // 选择位置点流程
    // =========================================================
    void selectPosition(const QImage& currentFrame) {
        if (currentFrame.isNull()) {
            QMessageBox::warning(m_parentWidget, "错误", "当前没有可用的视频帧");
            return;
        }

        m_currentFrame = currentFrame;

        // 创建位置选择覆盖层
        PositionSelectOverlay* overlay = new PositionSelectOverlay(currentFrame);
        overlay->setWindowModality(Qt::ApplicationModal);

        connect(overlay, &PositionSelectOverlay::positionSelected, this, [this, overlay](const QPointF& pos) {
            overlay->close();
            overlay->deleteLater();

            // 显示位置结果对话框
            PositionResultDialog* resultDialog = new PositionResultDialog(pos.x(), pos.y(), m_parentWidget);
            resultDialog->setWindowModality(Qt::ApplicationModal);
            resultDialog->exec();
            resultDialog->deleteLater();
        });

        connect(overlay, &PositionSelectOverlay::selectionCanceled, overlay, [overlay]() {
            overlay->close();
            overlay->deleteLater();
        });

        overlay->show();
        overlay->activateWindow();
    }

signals:
    // 生成的代码
    void codeGenerated(const QString& code);

private:
    void saveTemplateWithName(const QImage& image) {
        bool ok;
        QString name = QInputDialog::getText(m_parentWidget, "保存模板图片",
            "请输入图片名称 (不含扩展名):", QLineEdit::Normal, "template", &ok);

        if (!ok || name.isEmpty()) return;

        // 添加扩展名
        if (!name.endsWith(".png", Qt::CaseInsensitive)) {
            name += ".png";
        }

        // 检查是否存在
        if (ImageMatcher::templateExists(name)) {
            QMessageBox::StandardButton btn = QMessageBox::question(
                m_parentWidget, "文件已存在",
                QString("图片 '%1' 已存在，是否覆盖？").arg(name),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel
            );

            if (btn == QMessageBox::Cancel) {
                return;
            } else if (btn == QMessageBox::No) {
                // 重新输入名称
                saveTemplateWithName(image);
                return;
            }
            // Yes: 继续覆盖
        }

        // 保存图片
        if (ImageMatcher::saveTemplateImage(image, name)) {
            QMessageBox::information(m_parentWidget, "成功",
                QString("模板图片已保存: %1").arg(name));
        } else {
            QMessageBox::warning(m_parentWidget, "错误", "保存图片失败");
        }
    }

    void showRegionOptions(const QRectF& region) {
        QMessageBox msgBox(m_parentWidget);
        msgBox.setWindowTitle("选择操作");
        msgBox.setText("请选择下一步操作:");

        QPushButton* btnSelectImage = msgBox.addButton("选择已有模板", QMessageBox::ActionRole);
        QPushButton* btnEmptyTemplate = msgBox.addButton("生成空模板代码", QMessageBox::ActionRole);
        msgBox.addButton(QMessageBox::Cancel);

        msgBox.exec();

        if (msgBox.clickedButton() == btnSelectImage) {
            // 选择已有图片
            QString imagesPath = ImageMatcher::getImagesPath();
            QString fileName = QFileDialog::getOpenFileName(
                m_parentWidget, "选择模板图片", imagesPath,
                "Images (*.png *.jpg *.bmp);;All Files (*)"
            );

            if (!fileName.isEmpty()) {
                QFileInfo fi(fileName);
                // 不带后缀的文件名（去掉所有后缀）
                QString imageName = fi.completeBaseName();

                QString code = generateFindImageCode(imageName, region);
                emit codeGenerated(code);
            }

        } else if (msgBox.clickedButton() == btnEmptyTemplate) {
            // 生成空模板代码
            QString code = generateFindImageCode("图片名", region);
            emit codeGenerated(code);
        }
    }

    QString generateFindImageCode(const QString& imageName, const QRectF& region) {
        // 格式化坐标 (保留3位小数)
        QString x1 = QString::number(region.left(), 'f', 3);
        QString y1 = QString::number(region.top(), 'f', 3);
        QString x2 = QString::number(region.right(), 'f', 3);
        QString y2 = QString::number(region.bottom(), 'f', 3);

        QString code = QString(
            "// 区域找图\n"
            "var result = mapi.findImage(\"%1\", %2, %3, %4, %5, 0.8);\n"
            "if (result.found) {\n"
            "    mapi.click(result.x, result.y);\n"
            "    mapi.toast(\"找到目标，置信度: \" + result.confidence.toFixed(2));\n"
            "} else {\n"
            "    mapi.toast(\"未找到目标\");\n"
            "}"
        ).arg(imageName, x1, y1, x2, y2);

        return code;
    }

private:
    QWidget* m_parentWidget;
    QImage m_currentFrame;
};

#endif // IMAGECAPTUREDIALOG_H
