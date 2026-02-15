#ifndef SELECTIONEDITORDIALOG_H
#define SELECTIONEDITORDIALOG_H

#include <QDialog>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QImage>
#include <QPainter>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QClipboard>
#include <QApplication>
#include <QScrollArea>
#include <QScrollBar>
#include <QDir>
#include <QTimer>
#include <QFrame>
#include <QToolButton>
#include <functional>
#include <cmath>

#ifdef Q_OS_WIN
#include "winutils.h"
#endif

#include "selectionregionmanager.h"
#include "scriptbuttonmanager.h"
#include "scriptswipemanager.h"
#include "imagecapturedialog.h"

// 帧获取回调类型 / Frame grab callback type
using FrameGrabFunc = std::function<QImage()>;

// 创建模式 / Create mode
enum class CreateMode {
    None,           // 无 (普通浏览/编辑) / None (browse/edit)
    CreateRegion,   // 新建选区 / Create region
    CreateImage,    // 新建图片 / Create image
    GetPosition,    // 获取位置 (点击获取坐标) / Get position (click for coords)
    CreateButton,   // 新建按钮 (点击放置) / Create button (click to place)
    CreateSwipe     // 新建滑动 (点击起点+终点) / Create swipe (click start + end)
};

// 图层可见性标志 / Layer visibility flags
enum class PreviewLayer {
    Buttons = 0x01,
    Swipes  = 0x02,
    Regions = 0x04,
    All     = 0x07
};

// ---------------------------------------------------------
// 选区预览控件 - 可缩放图像 + 选区叠加绘制
// Selection Preview Widget - Zoomable image + selection overlay
// ---------------------------------------------------------
class SelectionPreviewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SelectionPreviewWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedSize(200, 150);
    }

    void setFrame(const QImage& frame) {
        m_frame = frame;
        updateScaledImage();
    }

    const QImage& frame() const { return m_frame; }
    double scale() const { return m_scale; }

    void setScale(double s) {
        m_scale = qBound(0.1, s, 8.0);
        updateScaledImage();
    }

    // 高亮控制
    void setHighlightId(int id) { m_highlightId = id; update(); }
    void clearHighlight() { m_highlightId = -1; update(); }
    int highlightId() const { return m_highlightId; }

    // 覆盖坐标 (拖拽/缩放时实时预览)
    void setOverrideCoords(double x0, double y0, double x1, double y1) {
        m_hasOverride = true;
        m_ovX0 = x0; m_ovY0 = y0; m_ovX1 = x1; m_ovY1 = y1;
        update();
    }
    void clearOverride() { m_hasOverride = false; update(); }
    bool hasOverride() const { return m_hasOverride; }
    void getOverrideCoords(double& x0, double& y0, double& x1, double& y1) const {
        x0 = m_ovX0; y0 = m_ovY0; x1 = m_ovX1; y1 = m_ovY1;
    }

    // 创建模式控制
    void setCreateMode(CreateMode mode) { m_createMode = mode; update(); }
    CreateMode createMode() const { return m_createMode; }

    // 创建中的矩形
    void setCreatingRect(const QRectF& r) { m_creatingRect = r; update(); }
    QRectF creatingRect() const { return m_creatingRect; }
    void clearCreatingRect() { m_creatingRect = QRectF(); update(); }

    // 待确认的矩形 (框选完成后微调状态)
    void setPendingRect(const QRectF& r) { m_pendingRect = r; update(); }
    QRectF pendingRect() const { return m_pendingRect; }
    void clearPendingRect() { m_pendingRect = QRectF(); update(); }
    bool hasPendingRect() const { return m_pendingRect.isValid() && m_pendingRect.width() > 0.005; }

    // 获取位置模式: 标记点
    void setPositionMarker(const QPointF& normPos) { m_positionMarker = normPos; m_hasPositionMarker = true; update(); }
    void clearPositionMarker() { m_hasPositionMarker = false; update(); }
    bool hasPositionMarker() const { return m_hasPositionMarker; }
    QPointF positionMarker() const { return m_positionMarker; }

    // 图层可见性
    void setLayerVisible(PreviewLayer layer, bool visible) {
        int flag = static_cast<int>(layer);
        if (visible) m_visibleLayers |= flag;
        else m_visibleLayers &= ~flag;
        update();
    }
    bool isLayerVisible(PreviewLayer layer) const {
        return (m_visibleLayers & static_cast<int>(layer)) != 0;
    }

    // 按钮高亮
    void setHighlightButtonId(int id) { m_highlightButtonId = id; update(); }
    void clearHighlightButton() { m_highlightButtonId = -1; update(); }
    int highlightButtonId() const { return m_highlightButtonId; }

    // 滑动高亮
    void setHighlightSwipeId(int id) { m_highlightSwipeId = id; update(); }
    void clearHighlightSwipe() { m_highlightSwipeId = -1; update(); }
    int highlightSwipeId() const { return m_highlightSwipeId; }

    // 滑动创建: 起点标记
    void setSwipeStartMarker(const QPointF& p) { m_swipeStart = p; m_hasSwipeStart = true; update(); }
    void clearSwipeStartMarker() { m_hasSwipeStart = false; update(); }
    bool hasSwipeStartMarker() const { return m_hasSwipeStart; }
    QPointF swipeStartMarker() const { return m_swipeStart; }

    // 滑动创建: 终点标记
    void setSwipeEndMarker(const QPointF& p) { m_swipeEnd = p; m_hasSwipeEnd = true; update(); }
    void clearSwipeEndMarker() { m_hasSwipeEnd = false; update(); }
    bool hasSwipeEndMarker() const { return m_hasSwipeEnd; }
    QPointF swipeEndMarker() const { return m_swipeEnd; }

    // 按钮拖拽: 覆盖坐标
    void setOverrideButtonPos(double x, double y) { m_hasButtonOverride = true; m_ovBtnX = x; m_ovBtnY = y; update(); }
    void clearOverrideButton() { m_hasButtonOverride = false; update(); }
    bool hasOverrideButton() const { return m_hasButtonOverride; }
    void getOverrideButtonPos(double& x, double& y) const { x = m_ovBtnX; y = m_ovBtnY; }

    // 滑动拖拽: 覆盖坐标
    void setOverrideSwipeCoords(double x0, double y0, double x1, double y1) {
        m_hasSwipeOverride = true; m_ovSwX0 = x0; m_ovSwY0 = y0; m_ovSwX1 = x1; m_ovSwY1 = y1; update();
    }
    void clearOverrideSwipe() { m_hasSwipeOverride = false; update(); }
    bool hasOverrideSwipe() const { return m_hasSwipeOverride; }
    void getOverrideSwipeCoords(double& x0, double& y0, double& x1, double& y1) const {
        x0 = m_ovSwX0; y0 = m_ovSwY0; x1 = m_ovSwX1; y1 = m_ovSwY1;
    }

    // 根据区域 ID 生成确定性颜色
    static QColor colorForRegionId(int id) {
        int hue = (id * 137 + 60) % 360;
        return QColor::fromHsl(hue, 180, 140);
    }

    // 坐标转换
    QPointF widgetToNorm(const QPoint& wp) const {
        if (m_frame.isNull()) return QPointF(0, 0);
        return QPointF(
            wp.x() / (m_frame.width() * m_scale),
            wp.y() / (m_frame.height() * m_scale));
    }

    QPointF normToWidget(double nx, double ny) const {
        return QPointF(nx * m_frame.width() * m_scale, ny * m_frame.height() * m_scale);
    }

    QRectF normToWidgetRect(double x0, double y0, double x1, double y1) const {
        QPointF tl = normToWidget(x0, y0);
        QPointF br = normToWidget(x1, y1);
        return QRectF(tl, br).normalized();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);

        if (m_scaledFrame.isNull()) {
            painter.fillRect(rect(), QColor(0x09, 0x09, 0x0b));
            painter.setPen(QColor(0xa1, 0xa1, 0xaa));
            painter.drawText(rect(), Qt::AlignCenter, tr("无预览帧\n请先连接设备"));
            return;
        }

        painter.drawImage(0, 0, m_scaledFrame);

        // 创建模式时只隐藏不相关的图层
        bool hideRegions = (m_createMode == CreateMode::CreateImage || m_createMode == CreateMode::GetPosition
                           || m_createMode == CreateMode::CreateButton || m_createMode == CreateMode::CreateSwipe);

        // ---- 绘制选区图层 ----
        if (!hideRegions && isLayerVisible(PreviewLayer::Regions)) {
            auto& mgr = SelectionRegionManager::instance();
            for (const auto& r : mgr.regions()) {
                double rx0 = r.x0, ry0 = r.y0, rx1 = r.x1, ry1 = r.y1;
                if (r.id == m_highlightId && m_hasOverride) {
                    rx0 = m_ovX0; ry0 = m_ovY0; rx1 = m_ovX1; ry1 = m_ovY1;
                }

                QRectF wr = normToWidgetRect(rx0, ry0, rx1, ry1);
                QColor regionColor = colorForRegionId(r.id);

                if (r.id == m_highlightId) {
                    painter.fillRect(wr, QColor(regionColor.red(), regionColor.green(), regionColor.blue(), 30));
                    painter.setPen(QPen(regionColor, 2));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawRect(wr);

                    painter.setPen(Qt::white);
                    painter.setFont(QFont("Consolas", 10, QFont::Bold));
                    painter.drawText(wr.topLeft() + QPointF(4, -4),
                                     QString("#%1 %2").arg(r.id).arg(r.name));

                    drawHandles(painter, wr, regionColor);
                } else {
                    painter.setPen(QPen(QColor(regionColor.red(), regionColor.green(), regionColor.blue(), 80), 1, Qt::DashLine));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawRect(wr);

                    painter.setPen(QColor(regionColor.red(), regionColor.green(), regionColor.blue(), 160));
                    painter.setFont(QFont("Consolas", 8));
                    painter.drawText(wr.topLeft() + QPointF(3, 12), QString("#%1").arg(r.id));
                }
            }
        }

        // ---- 绘制按钮图层 ----
        if (!hideRegions && isLayerVisible(PreviewLayer::Buttons)) {
            painter.setRenderHint(QPainter::Antialiasing, true);
            auto& bmgr = ScriptButtonManager::instance();
            for (const auto& b : bmgr.buttons()) {
                double bx = b.x, by = b.y;
                if (b.id == m_highlightButtonId && m_hasButtonOverride) {
                    bx = m_ovBtnX; by = m_ovBtnY;
                }
                QPointF wp = normToWidget(bx, by);
                QColor btnColor = colorForRegionId(b.id + 100); // offset to differentiate from regions

                if (b.id == m_highlightButtonId) {
                    // 高亮按钮: 大圆 + 十字 + 标签
                    painter.setPen(QPen(btnColor, 2));
                    painter.setBrush(QColor(btnColor.red(), btnColor.green(), btnColor.blue(), 50));
                    painter.drawEllipse(wp, 14, 14);
                    painter.setPen(QPen(btnColor, 1, Qt::DashLine));
                    painter.drawLine(QPointF(wp.x() - 20, wp.y()), QPointF(wp.x() + 20, wp.y()));
                    painter.drawLine(QPointF(wp.x(), wp.y() - 20), QPointF(wp.x(), wp.y() + 20));
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(btnColor);
                    painter.drawEllipse(wp, 5, 5);

                    painter.setPen(Qt::white);
                    painter.setFont(QFont("Consolas", 10, QFont::Bold));
                    drawTextWithBg(painter, wp + QPointF(18, -6),
                                   QString("#%1 %2").arg(b.id).arg(b.name));
                } else {
                    // 非高亮按钮: 小圆点 + ID
                    painter.setPen(QPen(btnColor, 1.5));
                    painter.setBrush(QColor(btnColor.red(), btnColor.green(), btnColor.blue(), 80));
                    painter.drawEllipse(wp, 8, 8);
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(btnColor);
                    painter.drawEllipse(wp, 3, 3);

                    painter.setPen(QColor(btnColor.red(), btnColor.green(), btnColor.blue(), 200));
                    painter.setFont(QFont("Consolas", 8));
                    painter.drawText(wp + QPointF(10, -2), QString("#%1").arg(b.id));
                }
            }
            painter.setRenderHint(QPainter::Antialiasing, false);
        }

        // ---- 绘制滑动图层 ----
        if (!hideRegions && isLayerVisible(PreviewLayer::Swipes)) {
            painter.setRenderHint(QPainter::Antialiasing, true);
            auto& smgr = ScriptSwipeManager::instance();
            for (const auto& s : smgr.swipes()) {
                double sx0 = s.x0, sy0 = s.y0, sx1 = s.x1, sy1 = s.y1;
                if (s.id == m_highlightSwipeId && m_hasSwipeOverride) {
                    sx0 = m_ovSwX0; sy0 = m_ovSwY0; sx1 = m_ovSwX1; sy1 = m_ovSwY1;
                }
                QPointF wpStart = normToWidget(sx0, sy0);
                QPointF wpEnd = normToWidget(sx1, sy1);
                QColor swColor = colorForRegionId(s.id + 200); // offset

                if (s.id == m_highlightSwipeId) {
                    // 高亮滑动: 粗线 + 箭头 + 端点圆 + 标签
                    drawArrowLine(painter, wpStart, wpEnd, swColor, 2.5, true);

                    painter.setPen(Qt::white);
                    painter.setFont(QFont("Consolas", 10, QFont::Bold));
                    QPointF mid = (wpStart + wpEnd) / 2.0;
                    drawTextWithBg(painter, mid + QPointF(8, -10),
                                   QString("#%1 %2").arg(s.id).arg(s.name));
                } else {
                    // 非高亮: 细线 + 小箭头
                    drawArrowLine(painter, wpStart, wpEnd, swColor, 1.5, false);

                    painter.setPen(QColor(swColor.red(), swColor.green(), swColor.blue(), 200));
                    painter.setFont(QFont("Consolas", 8));
                    QPointF mid = (wpStart + wpEnd) / 2.0;
                    painter.drawText(mid + QPointF(6, -4), QString("#%1").arg(s.id));
                }
            }
            painter.setRenderHint(QPainter::Antialiasing, false);
        }

        // 绘制正在框选的矩形 (遮罩 + 虚线框)
        if (m_creatingRect.isValid()) {
            QRectF cr = normToWidgetRect(
                m_creatingRect.left(), m_creatingRect.top(),
                m_creatingRect.right(), m_creatingRect.bottom());

            QRegion outside = QRegion(rect()) - QRegion(cr.toRect());
            painter.save();
            painter.setClipRegion(outside);
            painter.fillRect(rect(), QColor(0, 0, 0, 80));
            painter.restore();

            QColor lineColor = (m_createMode == CreateMode::CreateImage)
                ? QColor(0, 200, 100) : QColor(0, 200, 255);
            painter.setPen(QPen(lineColor, 2, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(cr);
        }

        // 绘制待确认的矩形 (实线 + 手柄 + 半透明遮罩)
        if (m_pendingRect.isValid() && m_pendingRect.width() > 0.005) {
            QRectF pr = normToWidgetRect(
                m_pendingRect.left(), m_pendingRect.top(),
                m_pendingRect.right(), m_pendingRect.bottom());

            QRegion outside = QRegion(rect()) - QRegion(pr.toRect());
            painter.save();
            painter.setClipRegion(outside);
            painter.fillRect(rect(), QColor(0, 0, 0, 60));
            painter.restore();

            QColor borderColor = (m_createMode == CreateMode::CreateImage)
                ? QColor(0, 200, 100) : QColor(0, 200, 255);
            painter.setPen(QPen(borderColor, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(pr);

            drawHandles(painter, pr);
        }

        // ---- 绘制按钮/滑动创建标记 ----
        painter.setRenderHint(QPainter::Antialiasing, true);

        // 滑动创建: 起点+终点标记
        if (m_hasSwipeStart) {
            QPointF wp = normToWidget(m_swipeStart.x(), m_swipeStart.y());
            drawPointMarker(painter, wp, QColor(0, 200, 255), "A");
            if (m_hasSwipeEnd) {
                QPointF wpEnd = normToWidget(m_swipeEnd.x(), m_swipeEnd.y());
                drawArrowLine(painter, wp, wpEnd, QColor(0, 200, 255), 2.5, true);
                drawPointMarker(painter, wpEnd, QColor(255, 120, 0), "B");
            }
        }

        // 获取位置/按钮创建标记
        if (m_hasPositionMarker) {
            QPointF wp = normToWidget(m_positionMarker.x(), m_positionMarker.y());

            if (m_createMode == CreateMode::CreateButton) {
                // 按钮创建: 绿色圆点标记
                painter.setPen(QPen(QColor(0, 220, 120), 2));
                painter.setBrush(QColor(0, 220, 120, 40));
                painter.drawEllipse(wp, 14, 14);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0, 220, 120));
                painter.drawEllipse(wp, 5, 5);

                painter.setPen(Qt::white);
                painter.setFont(QFont("Consolas", 10, QFont::Bold));
                QString coordText = QString("(%1, %2)")
                    .arg(m_positionMarker.x(), 0, 'f', 4)
                    .arg(m_positionMarker.y(), 0, 'f', 4);
                drawTextWithBg(painter, wp + QPointF(18, -6), coordText);
            } else {
                // 获取位置: 红色十字标记 (原始样式)
                QPen crossPen(QColor(255, 60, 60), 1, Qt::DashLine);
                painter.setPen(crossPen);
                painter.drawLine(QPointF(wp.x(), 0), QPointF(wp.x(), height()));
                painter.drawLine(QPointF(0, wp.y()), QPointF(width(), wp.y()));

                painter.setPen(QPen(QColor(255, 60, 60), 2));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(wp, 12, 12);

                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(255, 60, 60));
                painter.drawEllipse(wp, 3, 3);

                painter.setPen(Qt::white);
                painter.setFont(QFont("Consolas", 10, QFont::Bold));
                QString coordText = QString("(%1, %2)")
                    .arg(m_positionMarker.x(), 0, 'f', 4)
                    .arg(m_positionMarker.y(), 0, 'f', 4);
                QPointF textPos = wp + QPointF(16, -8);
                QFontMetrics fm(painter.font());
                QRectF textRect = fm.boundingRect(coordText);
                textRect.moveTopLeft(textPos + QPointF(-2, -fm.ascent()));
                textRect.adjust(-4, -2, 4, 2);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0, 0, 0, 180));
                painter.drawRoundedRect(textRect, 3, 3);
                painter.setPen(Qt::white);
                painter.drawText(textPos, coordText);
            }
        }

        painter.setRenderHint(QPainter::Antialiasing, false);
    }

private:
    // 绘制带背景的文字
    void drawTextWithBg(QPainter& painter, const QPointF& pos, const QString& text) const {
        QFontMetrics fm(painter.font());
        QRectF textRect = fm.boundingRect(text);
        textRect.moveTopLeft(pos + QPointF(-2, -fm.ascent()));
        textRect.adjust(-4, -2, 4, 2);
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 180));
        painter.drawRoundedRect(textRect, 3, 3);
        painter.setPen(Qt::white);
        painter.drawText(pos, text);
        painter.restore();
    }

    // 绘制带字母标识的标记点
    void drawPointMarker(QPainter& painter, const QPointF& wp, const QColor& color, const QString& label) const {
        painter.setPen(QPen(color, 2));
        painter.setBrush(QColor(color.red(), color.green(), color.blue(), 40));
        painter.drawEllipse(wp, 12, 12);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(wp, 4, 4);

        if (!label.isEmpty()) {
            painter.setFont(QFont("Consolas", 9, QFont::Bold));
            painter.setPen(Qt::white);
            drawTextWithBg(painter, wp + QPointF(14, -8), label);
        }
    }

    // 绘制带箭头的线段
    void drawArrowLine(QPainter& painter, const QPointF& from, const QPointF& to,
                       const QColor& color, double lineWidth, bool highlight) const {
        painter.setPen(QPen(color, lineWidth));
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(from, to);

        // 起点圆点
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(from, highlight ? 5.0 : 3.0, highlight ? 5.0 : 3.0);

        // 箭头
        double dx = to.x() - from.x();
        double dy = to.y() - from.y();
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.0) return;

        double ux = dx / len, uy = dy / len;
        double arrowLen = highlight ? 12.0 : 8.0;
        double arrowW = highlight ? 6.0 : 4.0;

        QPointF tip = to;
        QPointF base = tip - QPointF(ux * arrowLen, uy * arrowLen);
        QPointF left = base + QPointF(-uy * arrowW, ux * arrowW);
        QPointF right = base + QPointF(uy * arrowW, -ux * arrowW);

        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        QPolygonF arrow;
        arrow << tip << left << right;
        painter.drawPolygon(arrow);
    }

    void updateScaledImage() {
        if (m_frame.isNull()) {
            m_scaledFrame = QImage();
            setFixedSize(200, 150);
            update();
            return;
        }
        int w = qRound(m_frame.width() * m_scale);
        int h = qRound(m_frame.height() * m_scale);
        if (w <= 0 || h <= 0) return;
        m_scaledFrame = m_frame.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        setFixedSize(m_scaledFrame.size());
        update();
    }

    void drawHandles(QPainter& painter, const QRectF& r, const QColor& color = QColor(99, 102, 241)) const {
        painter.setBrush(color);
        painter.setPen(QPen(Qt::white, 1));
        const double hs = 6.0;
        double cx = r.center().x(), cy = r.center().y();
        auto dh = [&](double x, double y) {
            painter.drawRect(QRectF(x - hs / 2, y - hs / 2, hs, hs));
        };
        dh(r.left(), r.top()); dh(r.right(), r.top());
        dh(r.left(), r.bottom()); dh(r.right(), r.bottom());
        dh(cx, r.top()); dh(cx, r.bottom());
        dh(r.left(), cy); dh(r.right(), cy);
    }

    QImage m_frame;
    QImage m_scaledFrame;
    double m_scale = 1.0;
    int m_highlightId = -1;
    bool m_hasOverride = false;
    double m_ovX0 = 0, m_ovY0 = 0, m_ovX1 = 0, m_ovY1 = 0;
    CreateMode m_createMode = CreateMode::None;
    QRectF m_creatingRect;
    QRectF m_pendingRect;
    bool m_hasPositionMarker = false;
    QPointF m_positionMarker;
    int m_visibleLayers = static_cast<int>(PreviewLayer::All);
    int m_highlightButtonId = -1;
    int m_highlightSwipeId = -1;
    bool m_hasSwipeStart = false;
    QPointF m_swipeStart;
    bool m_hasSwipeEnd = false;
    QPointF m_swipeEnd;
    // 按钮拖拽覆盖
    bool m_hasButtonOverride = false;
    double m_ovBtnX = 0, m_ovBtnY = 0;
    // 滑动拖拽覆盖
    bool m_hasSwipeOverride = false;
    double m_ovSwX0 = 0, m_ovSwY0 = 0, m_ovSwX1 = 0, m_ovSwY1 = 0;
};

// ---------------------------------------------------------
// 选区编辑器对话框
// 左侧: 操作按钮 + 选区列表
// 右侧: 工具栏(缩放) + 可缩放预览 (支持拖拽/手柄/框选)
// 预览中框选完成后显示带手柄的待确认矩形 + 浮动配置栏
// ---------------------------------------------------------
class SelectionEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SelectionEditorDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(tr("自定义选区管理"));
        setWindowFlags(Qt::Window | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
        resize(900, 600);
        setMinimumSize(700, 450);

#ifdef Q_OS_WIN
        WinUtils::setDarkBorderToWindow(reinterpret_cast<HWND>(winId()), true);
#endif

        setStyleSheet(
            "QDialog { background-color: #18181b; }"
            "QWidget { background-color: #18181b; }"
            "QLabel { color: #fafafa; background: transparent; }"
            "QListWidget {"
            "  background-color: #1e1e1e;"
            "  color: #e4e4e7;"
            "  border: 1px solid #3f3f46;"
            "  border-radius: 6px;"
            "  font-family: 'Consolas', 'Monaco', monospace;"
            "  font-size: 10pt;"
            "  outline: none;"
            "}"
            "QListWidget::item {"
            "  padding: 8px 10px;"
            "  border-bottom: 1px solid #27272a;"
            "}"
            "QListWidget::item:selected {"
            "  background-color: #6366f1;"
            "  color: #ffffff;"
            "}"
            "QListWidget::item:hover:!selected {"
            "  background-color: #27272a;"
            "}"
            "QMenu {"
            "  background-color: #18181b;"
            "  color: #fafafa;"
            "  border: 1px solid #3f3f46;"
            "  border-radius: 6px;"
            "  padding: 4px;"
            "}"
            "QMenu::item {"
            "  padding: 8px 16px;"
            "  border-radius: 4px;"
            "}"
            "QMenu::item:selected {"
            "  background-color: #6366f1;"
            "}"
            "QMenu::separator {"
            "  height: 1px;"
            "  background-color: #3f3f46;"
            "  margin: 4px 8px;"
            "}"
            "QMessageBox { background-color: #18181b; color: #fafafa; }"
            "QMessageBox QLabel { color: #fafafa; }"
            "QMessageBox QPushButton {"
            "  background-color: #27272a;"
            "  color: #fafafa;"
            "  border: 1px solid #3f3f46;"
            "  border-radius: 6px;"
            "  padding: 6px 16px;"
            "}"
            "QMessageBox QPushButton:hover { background-color: #3f3f46; }"
        );

        // --- 主布局 ---
        QHBoxLayout* mainLayout = new QHBoxLayout(this);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);

        QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
        splitter->setStyleSheet(
            "QSplitter::handle { background-color: #3f3f46; width: 2px; }");
        splitter->setChildrenCollapsible(false);

        // =========================================================
        // 左侧面板
        // =========================================================
        QWidget* leftPanel = new QWidget(this);
        leftPanel->setMinimumWidth(250);
        leftPanel->setMaximumWidth(400);

        QScrollArea* leftScroll = new QScrollArea(this);
        leftScroll->setWidgetResizable(true);
        leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        leftScroll->setStyleSheet("QScrollArea { border: none; background-color: #18181b; }"
                                  "QScrollBar:vertical { background: #18181b; width: 6px; }"
                                  "QScrollBar::handle:vertical { background: #3f3f46; border-radius: 3px; min-height: 30px; }"
                                  "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");

        QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
        leftLayout->setContentsMargins(12, 12, 12, 12);
        leftLayout->setSpacing(8);

        QLabel* opLabel = new QLabel(tr("操作"), this);
        opLabel->setStyleSheet("color: #a1a1aa; font-size: 9pt; font-weight: bold;");
        leftLayout->addWidget(opLabel);

        // 第一行 (NEW): 新建按钮 + 新建滑动
        QHBoxLayout* row0 = new QHBoxLayout();
        row0->setSpacing(6);
        m_btnCreateButton = new QPushButton(tr("新建按钮"), this);
        styleActionButton(m_btnCreateButton);
        connect(m_btnCreateButton, &QPushButton::clicked, this, &SelectionEditorDialog::onToggleCreateButtonMode);
        row0->addWidget(m_btnCreateButton);

        m_btnCreateSwipe = new QPushButton(tr("新建滑动"), this);
        styleActionButton(m_btnCreateSwipe);
        connect(m_btnCreateSwipe, &QPushButton::clicked, this, &SelectionEditorDialog::onToggleCreateSwipeMode);
        row0->addWidget(m_btnCreateSwipe);
        leftLayout->addLayout(row0);

        // 第二行: 新建图片 + 新建选区
        QHBoxLayout* row1 = new QHBoxLayout();
        row1->setSpacing(6);
        m_btnCaptureImage = new QPushButton(tr("新建图片"), this);
        styleActionButton(m_btnCaptureImage);
        connect(m_btnCaptureImage, &QPushButton::clicked, this, &SelectionEditorDialog::onToggleCreateImageMode);
        row1->addWidget(m_btnCaptureImage);

        m_btnNew = new QPushButton(tr("新建选区"), this);
        styleActionButton(m_btnNew);
        connect(m_btnNew, &QPushButton::clicked, this, &SelectionEditorDialog::onToggleCreateMode);
        row1->addWidget(m_btnNew);
        leftLayout->addLayout(row1);

        // 第三行: 导入选区 + 打开文件夹
        QHBoxLayout* row2 = new QHBoxLayout();
        row2->setSpacing(6);
        QPushButton* btnImport = new QPushButton(tr("导入选区"), this);
        styleActionButton(btnImport);
        connect(btnImport, &QPushButton::clicked, this, &SelectionEditorDialog::onImportRegion);
        row2->addWidget(btnImport);

        QPushButton* btnOpenDir = new QPushButton(tr("打开文件夹"), this);
        styleActionButton(btnOpenDir);
        connect(btnOpenDir, &QPushButton::clicked, []() {
            QString dir = SelectionRegionManager::configDir();
            QDir d(dir);
            if (!d.exists()) d.mkpath(".");
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        });
        row2->addWidget(btnOpenDir);
        leftLayout->addLayout(row2);

        // 第四行: 获取位置
        m_btnGetPos = new QPushButton(tr("获取位置"), this);
        styleActionButton(m_btnGetPos);
        connect(m_btnGetPos, &QPushButton::clicked, this, &SelectionEditorDialog::onToggleGetPositionMode);
        leftLayout->addWidget(m_btnGetPos);

        QFrame* separator = new QFrame(this);
        separator->setFrameShape(QFrame::HLine);
        separator->setStyleSheet("color: #3f3f46;");
        leftLayout->addWidget(separator);

        // ---- 按钮列表 (可折叠) ----
        auto createSectionHeader = [this](const QString& title, bool expanded = true) -> QToolButton* {
            QToolButton* toggle = new QToolButton(this);
            toggle->setText(title);
            toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            toggle->setCheckable(true);
            toggle->setChecked(expanded);
            toggle->setStyleSheet(
                "QToolButton {"
                "  color: #a1a1aa; font-size: 9pt; font-weight: bold;"
                "  background: transparent; border: none; padding: 2px 0px;"
                "}"
                "QToolButton:hover { color: #fafafa; }"
            );
            return toggle;
        };

        QToolButton* btnSectionToggle = createSectionHeader(tr("按钮列表"));
        leftLayout->addWidget(btnSectionToggle);

        m_buttonListContainer = new QWidget(this);
        QVBoxLayout* btnContainerLayout = new QVBoxLayout(m_buttonListContainer);
        btnContainerLayout->setContentsMargins(0, 0, 0, 0);
        btnContainerLayout->setSpacing(4);

        m_buttonListWidget = new QListWidget(this);
        m_buttonListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
        m_buttonListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
        m_buttonListWidget->setMaximumHeight(120);
        connect(m_buttonListWidget, &QListWidget::customContextMenuRequested,
                this, &SelectionEditorDialog::onButtonContextMenu);
        connect(m_buttonListWidget, &QListWidget::currentRowChanged,
                this, &SelectionEditorDialog::onButtonSelectionChanged);
        btnContainerLayout->addWidget(m_buttonListWidget);

        m_buttonInfoLabel = new QLabel(tr("共 0 个按钮"), this);
        m_buttonInfoLabel->setStyleSheet("color: #71717a; font-size: 9pt;");
        btnContainerLayout->addWidget(m_buttonInfoLabel);

        leftLayout->addWidget(m_buttonListContainer);

        connect(btnSectionToggle, &QToolButton::toggled, [this, btnSectionToggle](bool checked) {
            m_buttonListContainer->setVisible(checked);
            btnSectionToggle->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
        });

        // ---- 滑动列表 (可折叠) ----
        QToolButton* swipeSectionToggle = createSectionHeader(tr("滑动列表"));
        leftLayout->addWidget(swipeSectionToggle);

        m_swipeListContainer = new QWidget(this);
        QVBoxLayout* swipeContainerLayout = new QVBoxLayout(m_swipeListContainer);
        swipeContainerLayout->setContentsMargins(0, 0, 0, 0);
        swipeContainerLayout->setSpacing(4);

        m_swipeListWidget = new QListWidget(this);
        m_swipeListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
        m_swipeListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
        m_swipeListWidget->setMaximumHeight(120);
        connect(m_swipeListWidget, &QListWidget::customContextMenuRequested,
                this, &SelectionEditorDialog::onSwipeContextMenu);
        connect(m_swipeListWidget, &QListWidget::currentRowChanged,
                this, &SelectionEditorDialog::onSwipeSelectionChanged);
        swipeContainerLayout->addWidget(m_swipeListWidget);

        m_swipeInfoLabel = new QLabel(tr("共 0 个滑动"), this);
        m_swipeInfoLabel->setStyleSheet("color: #71717a; font-size: 9pt;");
        swipeContainerLayout->addWidget(m_swipeInfoLabel);

        leftLayout->addWidget(m_swipeListContainer);

        connect(swipeSectionToggle, &QToolButton::toggled, [this, swipeSectionToggle](bool checked) {
            m_swipeListContainer->setVisible(checked);
            swipeSectionToggle->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
        });

        // ---- 选区列表 (可折叠) ----
        QToolButton* regionSectionToggle = createSectionHeader(tr("选区列表"));
        leftLayout->addWidget(regionSectionToggle);

        m_regionListContainer = new QWidget(this);
        QVBoxLayout* regionContainerLayout = new QVBoxLayout(m_regionListContainer);
        regionContainerLayout->setContentsMargins(0, 0, 0, 0);
        regionContainerLayout->setSpacing(4);

        m_listWidget = new QListWidget(this);
        m_listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
        m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
        connect(m_listWidget, &QListWidget::customContextMenuRequested,
                this, &SelectionEditorDialog::onContextMenu);
        connect(m_listWidget, &QListWidget::currentRowChanged,
                this, &SelectionEditorDialog::onSelectionChanged);
        regionContainerLayout->addWidget(m_listWidget);

        m_infoLabel = new QLabel(tr("共 0 个选区"), this);
        m_infoLabel->setStyleSheet("color: #71717a; font-size: 9pt;");
        regionContainerLayout->addWidget(m_infoLabel);

        leftLayout->addWidget(m_regionListContainer, 1);

        connect(regionSectionToggle, &QToolButton::toggled, [this, regionSectionToggle](bool checked) {
            m_regionListContainer->setVisible(checked);
            regionSectionToggle->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
        });

        leftLayout->addStretch();

        leftScroll->setWidget(leftPanel);
        splitter->addWidget(leftScroll);

        // =========================================================
        // 右侧面板: 工具栏 + 滚动预览区
        // =========================================================
        QWidget* rightPanel = new QWidget(this);
        QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
        rightLayout->setContentsMargins(0, 0, 0, 0);
        rightLayout->setSpacing(0);

        // 工具栏
        QWidget* toolbar = new QWidget(this);
        toolbar->setStyleSheet("background-color: #18181b;");
        QHBoxLayout* toolbarLayout = new QHBoxLayout(toolbar);
        toolbarLayout->setContentsMargins(12, 5, 12, 5);

        m_hintLabel = new QLabel(this);
        m_hintLabel->setStyleSheet("color: #a1a1aa; font-size: 11px;");
        toolbarLayout->addWidget(m_hintLabel);

        toolbarLayout->addStretch();

        m_scaleLabel = new QLabel("100%", this);
        m_scaleLabel->setStyleSheet("color: #a1a1aa; font-size: 11px;");
        toolbarLayout->addWidget(m_scaleLabel);

        auto makeToolBtn = [&](const QString& text, int w = 28) {
            QPushButton* btn = new QPushButton(text, this);
            btn->setFixedSize(w, 26);
            btn->setStyleSheet(
                "QPushButton { background: #27272a; color: #fafafa; border: 1px solid #3f3f46;"
                "  border-radius: 6px; font-weight: bold; }"
                "QPushButton:hover { background: #3f3f46; border-color: #6366f1; }");
            return btn;
        };

        QPushButton* btnRefresh = makeToolBtn("⟳", 32);
        btnRefresh->setToolTip(tr("刷新帧"));
        connect(btnRefresh, &QPushButton::clicked, this, &SelectionEditorDialog::refreshFrame);
        toolbarLayout->addWidget(btnRefresh);

        QPushButton* btnZoomIn = makeToolBtn("+");
        connect(btnZoomIn, &QPushButton::clicked, [this]() { zoom(0.25); });
        toolbarLayout->addWidget(btnZoomIn);

        QPushButton* btnZoomOut = makeToolBtn("-");
        connect(btnZoomOut, &QPushButton::clicked, [this]() { zoom(-0.25); });
        toolbarLayout->addWidget(btnZoomOut);

        QPushButton* btnFit = makeToolBtn(tr("适应"), 42);
        connect(btnFit, &QPushButton::clicked, this, &SelectionEditorDialog::fitToWindow);
        toolbarLayout->addWidget(btnFit);

        // 图层切换按钮
        auto makeLayerToggle = [&](const QString& text, PreviewLayer layer, int w = 36) {
            QPushButton* btn = new QPushButton(text, this);
            btn->setFixedSize(w, 26);
            btn->setCheckable(true);
            btn->setChecked(true);
            btn->setToolTip(tr("显示/隐藏图层"));
            btn->setStyleSheet(
                "QPushButton { background: #27272a; color: #a1a1aa; border: 1px solid #3f3f46;"
                "  border-radius: 6px; font-size: 9pt; }"
                "QPushButton:hover { background: #3f3f46; border-color: #6366f1; color: #fafafa; }"
                "QPushButton:checked { background: #3f3f46; color: #fafafa; border-color: #6366f1; }");
            connect(btn, &QPushButton::toggled, [this, layer](bool checked) {
                if (m_preview) m_preview->setLayerVisible(layer, checked);
            });
            return btn;
        };

        // 分隔
        QFrame* tbSep = new QFrame(this);
        tbSep->setFrameShape(QFrame::VLine);
        tbSep->setFixedSize(1, 20);
        tbSep->setStyleSheet("background: #3f3f46;");
        toolbarLayout->addWidget(tbSep);

        toolbarLayout->addWidget(makeLayerToggle(tr("按钮"), PreviewLayer::Buttons, 40));
        toolbarLayout->addWidget(makeLayerToggle(tr("滑动"), PreviewLayer::Swipes, 40));
        toolbarLayout->addWidget(makeLayerToggle(tr("选区"), PreviewLayer::Regions, 40));

        rightLayout->addWidget(toolbar);

        // 滚动预览区
        m_scrollArea = new QScrollArea(this);
        m_scrollArea->setStyleSheet("QScrollArea { background-color: #09090b; border: none; }");
        m_scrollArea->setWidgetResizable(false);
        m_scrollArea->setAlignment(Qt::AlignCenter);

        m_preview = new SelectionPreviewWidget(this);
        m_scrollArea->setWidget(m_preview);

        m_preview->installEventFilter(this);
        m_scrollArea->viewport()->installEventFilter(this);

        setFocusPolicy(Qt::StrongFocus);
        m_scrollArea->setFocusPolicy(Qt::NoFocus);
        m_preview->setFocusPolicy(Qt::NoFocus);

        rightLayout->addWidget(m_scrollArea, 1);

        splitter->addWidget(rightPanel);

        splitter->setStretchFactor(0, 3);
        splitter->setStretchFactor(1, 7);

        mainLayout->addWidget(splitter);

        // --- 浮动确认栏 (覆盖在 scrollArea 之上，初始隐藏) ---
        m_confirmBar = new QWidget(m_scrollArea->viewport());
        m_confirmBar->setStyleSheet(
            "QWidget#confirmBar {"
            "  background-color: rgba(24, 24, 27, 220);"
            "  border: 1px solid #6366f1;"
            "  border-radius: 6px;"
            "}"
        );
        m_confirmBar->setObjectName("confirmBar");
        QHBoxLayout* confirmLayout = new QHBoxLayout(m_confirmBar);
        confirmLayout->setContentsMargins(8, 4, 8, 4);
        confirmLayout->setSpacing(6);

        m_confirmHintLabel = new QLabel("", m_confirmBar);
        m_confirmHintLabel->setStyleSheet("color: #a1a1aa; font-size: 11px; background: transparent;");
        confirmLayout->addWidget(m_confirmHintLabel);

        confirmLayout->addStretch();

        m_btnConfirmCancel = new QPushButton(tr("取消"), m_confirmBar);
        m_btnConfirmCancel->setFixedSize(50, 24);
        m_btnConfirmCancel->setStyleSheet(
            "QPushButton { background: #3f3f46; color: #fafafa; border: 1px solid #52525b;"
            "  border-radius: 4px; font-size: 10pt; }"
            "QPushButton:hover { background: #52525b; }");
        connect(m_btnConfirmCancel, &QPushButton::clicked, this, &SelectionEditorDialog::onPendingCancel);
        confirmLayout->addWidget(m_btnConfirmCancel);

        m_btnConfirmOk = new QPushButton(tr("确定"), m_confirmBar);
        m_btnConfirmOk->setFixedSize(50, 24);
        m_btnConfirmOk->setStyleSheet(
            "QPushButton { background: #6366f1; color: #ffffff; border: none;"
            "  border-radius: 4px; font-size: 10pt; }"
            "QPushButton:hover { background: #818cf8; }");
        connect(m_btnConfirmOk, &QPushButton::clicked, this, &SelectionEditorDialog::onPendingConfirm);
        confirmLayout->addWidget(m_btnConfirmOk);

        m_confirmBar->hide();

        refreshList();
        refreshButtonList();
        refreshSwipeList();
        updateHint();
    }

    void setFrameGrabCallback(FrameGrabFunc callback) {
        m_frameGrabCallback = callback;
        refreshFrame();
        QTimer::singleShot(50, this, &SelectionEditorDialog::fitToWindow);
    }

signals:
    void codeSnippetGenerated(const QString& code);

protected:
    // =========================================================
    // 事件过滤器 — 处理预览区鼠标交互和滚轮缩放
    // =========================================================
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (!m_preview || !m_scrollArea)
            return QDialog::eventFilter(obj, event);

        // 滚轮缩放
        if ((obj == m_preview || obj == m_scrollArea->viewport())
            && event->type() == QEvent::Wheel) {
            QWheelEvent* we = static_cast<QWheelEvent*>(event);
            double delta = we->angleDelta().y() > 0 ? 0.15 : -0.15;
            QPoint mousePos;
            if (obj == m_preview) {
                mousePos = we->position().toPoint();
            } else {
                mousePos = m_preview->mapFromGlobal(we->globalPosition().toPoint());
            }
            zoom(delta, mousePos);
            return true;
        }

        // 鼠标事件 (只在 preview 上)
        if (obj == m_preview) {
            switch (event->type()) {
                case QEvent::MouseButtonPress:
                    return handleMousePress(static_cast<QMouseEvent*>(event));
                case QEvent::MouseMove:
                    return handleMouseMove(static_cast<QMouseEvent*>(event));
                case QEvent::MouseButtonRelease:
                    return handleMouseRelease(static_cast<QMouseEvent*>(event));
                default: break;
            }
        }

        return QDialog::eventFilter(obj, event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            if (m_pendingConfirm) {
                onPendingCancel();
            } else if (m_currentCreateMode != CreateMode::None) {
                exitCreateMode();
            } else {
                close();
            }
        }
        else if (event->key() == Qt::Key_Delete) {
            deleteCurrentRegion(false);
        }
        else if (event->key() == Qt::Key_C && (event->modifiers() & Qt::ControlModifier)) {
            copyCurrentRegionValue();
        }
        else {
            QDialog::keyPressEvent(event);
        }
    }

    void wheelEvent(QWheelEvent* event) override {
        double delta = event->angleDelta().y() > 0 ? 0.15 : -0.15;
        QPoint mousePos = m_preview->mapFromGlobal(event->globalPosition().toPoint());
        zoom(delta, mousePos);
        event->accept();
    }

private slots:
    void onToggleCreateMode() {
        if (m_pendingConfirm) return; // 有待确认的操作时忽略
        if (m_currentCreateMode == CreateMode::CreateRegion) {
            exitCreateMode();
        } else {
            if (m_currentCreateMode != CreateMode::None) exitCreateMode();
            if (!ensureFrame()) return;
            enterCreateMode(CreateMode::CreateRegion);
        }
    }

    void onToggleCreateImageMode() {
        if (m_pendingConfirm) return;
        if (m_currentCreateMode == CreateMode::CreateImage) {
            exitCreateMode();
        } else {
            if (m_currentCreateMode != CreateMode::None) exitCreateMode();
            if (!ensureFrame()) return;
            enterCreateMode(CreateMode::CreateImage);
        }
    }

    void onToggleGetPositionMode() {
        if (m_pendingConfirm) return;
        if (m_currentCreateMode == CreateMode::GetPosition) {
            exitCreateMode();
        } else {
            if (m_currentCreateMode != CreateMode::None) exitCreateMode();
            if (!ensureFrame()) return;
            enterCreateMode(CreateMode::GetPosition);
        }
    }

    void onToggleCreateButtonMode() {
        if (m_pendingConfirm) return;
        if (m_currentCreateMode == CreateMode::CreateButton) {
            exitCreateMode();
        } else {
            if (m_currentCreateMode != CreateMode::None) exitCreateMode();
            if (!ensureFrame()) return;
            enterCreateMode(CreateMode::CreateButton);
        }
    }

    void onToggleCreateSwipeMode() {
        if (m_pendingConfirm) return;
        if (m_currentCreateMode == CreateMode::CreateSwipe) {
            exitCreateMode();
        } else {
            if (m_currentCreateMode != CreateMode::None) exitCreateMode();
            if (!ensureFrame()) return;
            enterCreateMode(CreateMode::CreateSwipe);
        }
    }

    void onImportRegion() {
        QString filePath = QFileDialog::getOpenFileName(
            this, tr("导入选区配置"),
            SelectionRegionManager::configDir(),
            "JSON Files (*.json);;All Files (*)");
        if (filePath.isEmpty()) return;

        auto& mgr = SelectionRegionManager::instance();
        int count = mgr.importFromFile(filePath);
        if (count > 0) {
            refreshList();
            QMessageBox::information(this, tr("导入成功"),
                QString(tr("成功导入 %1 个选区")).arg(count));
        } else {
            QMessageBox::warning(this, tr("导入失败"), tr("未找到有效的选区数据"));
        }
    }

    void onContextMenu(const QPoint& pos) {
        QListWidgetItem* item = m_listWidget->itemAt(pos);
        if (!item) return;

        QMenu menu(this);

        QAction* actRename = menu.addAction(tr("重命名"));
        QAction* actCopy = menu.addAction(tr("复制选区值"));
        actCopy->setShortcut(QKeySequence("Ctrl+C"));
        menu.addSeparator();
        QAction* actFindSpecific = menu.addAction(tr("创建找图（指定）"));
        QAction* actFindEmpty = menu.addAction(tr("创建找图（空）"));
        menu.addSeparator();
        QAction* actDelete = menu.addAction(tr("删除"));
        actDelete->setShortcut(QKeySequence::Delete);

        QAction* selected = menu.exec(m_listWidget->mapToGlobal(pos));
        if (!selected) return;

        if (selected == actRename) {
            renameCurrentRegion();
        } else if (selected == actCopy) {
            copyCurrentRegionValue();
        } else if (selected == actFindSpecific) {
            onCreateFindImage(true);
        } else if (selected == actFindEmpty) {
            onCreateFindImage(false);
        } else if (selected == actDelete) {
            deleteCurrentRegion(true);
        }
    }

    void onSelectionChanged(int row) {
        m_dragging = false;
        m_resizing = false;
        m_resizeHandle = Handle::None;

        // 清除其它列表的选中
        m_buttonListWidget->clearSelection();
        m_swipeListWidget->clearSelection();
        m_preview->clearHighlightButton();
        m_preview->clearHighlightSwipe();

        auto& mgr = SelectionRegionManager::instance();
        const auto& regions = mgr.regions();
        if (row >= 0 && row < regions.size()) {
            m_preview->setHighlightId(regions[row].id);
        } else {
            m_preview->clearHighlight();
        }
        updateHint();
    }

    void onButtonSelectionChanged(int row) {
        // 清除其它列表的选中
        m_listWidget->clearSelection();
        m_swipeListWidget->clearSelection();
        m_preview->clearHighlight();
        m_preview->clearHighlightSwipe();

        auto& bmgr = ScriptButtonManager::instance();
        const auto& buttons = bmgr.buttons();
        if (row >= 0 && row < buttons.size()) {
            m_preview->setHighlightButtonId(buttons[row].id);
        } else {
            m_preview->clearHighlightButton();
        }
        updateHint();
    }

    void onSwipeSelectionChanged(int row) {
        // 清除其它列表的选中
        m_listWidget->clearSelection();
        m_buttonListWidget->clearSelection();
        m_preview->clearHighlight();
        m_preview->clearHighlightButton();

        auto& smgr = ScriptSwipeManager::instance();
        const auto& swipes = smgr.swipes();
        if (row >= 0 && row < swipes.size()) {
            m_preview->setHighlightSwipeId(swipes[row].id);
        } else {
            m_preview->clearHighlightSwipe();
        }
        updateHint();
    }

    void onButtonContextMenu(const QPoint& pos) {
        QListWidgetItem* item = m_buttonListWidget->itemAt(pos);
        if (!item) return;
        int id = item->data(Qt::UserRole).toInt();

        QMenu menu(this);
        QAction* actRename = menu.addAction(tr("重命名"));
        QAction* actCopy = menu.addAction(tr("复制坐标值"));
        menu.addSeparator();
        QAction* actGenClick = menu.addAction(tr("生成点击代码"));
        QAction* actGenHold = menu.addAction(tr("生成长按代码"));
        menu.addSeparator();
        QAction* actDelete = menu.addAction(tr("删除"));

        QAction* selected = menu.exec(m_buttonListWidget->mapToGlobal(pos));
        if (!selected) return;

        ScriptButton btn;
        if (!ScriptButtonManager::instance().findById(id, btn)) return;

        if (selected == actRename) {
            renameButton(id);
        } else if (selected == actCopy) {
            QApplication::clipboard()->setText(btn.coordString());
        } else if (selected == actGenClick) {
            emit codeSnippetGenerated(QString("// 按钮 #%1: %2\nmapi.click(%3);")
                .arg(id).arg(btn.name).arg(btn.coordString()));
        } else if (selected == actGenHold) {
            emit codeSnippetGenerated(QString("// 按钮 #%1: %2\nmapi.holdpress(%3);")
                .arg(id).arg(btn.name).arg(btn.coordString()));
        } else if (selected == actDelete) {
            deleteButton(id, true);
        }
    }

    void onSwipeContextMenu(const QPoint& pos) {
        QListWidgetItem* item = m_swipeListWidget->itemAt(pos);
        if (!item) return;
        int id = item->data(Qt::UserRole).toInt();

        QMenu menu(this);
        QAction* actRename = menu.addAction(tr("重命名"));
        QAction* actCopy = menu.addAction(tr("复制坐标值"));
        menu.addSeparator();
        QAction* actGen = menu.addAction(tr("生成滑动代码"));
        menu.addSeparator();
        QAction* actDelete = menu.addAction(tr("删除"));

        QAction* selected = menu.exec(m_swipeListWidget->mapToGlobal(pos));
        if (!selected) return;

        ScriptSwipe sw;
        if (!ScriptSwipeManager::instance().findById(id, sw)) return;

        if (selected == actRename) {
            renameSwipe(id);
        } else if (selected == actCopy) {
            QApplication::clipboard()->setText(sw.coordString());
        } else if (selected == actGen) {
            emit codeSnippetGenerated(QString("// 滑动 #%1: %2\nmapi.slide(%3, 200, 10);")
                .arg(id).arg(sw.name).arg(sw.coordString()));
        } else if (selected == actDelete) {
            deleteSwipe(id, true);
        }
    }

    // 待确认栏: 取消
    void onPendingCancel() {
        m_pendingConfirm = false;
        m_preview->clearPendingRect();
        m_confirmBar->hide();
        exitCreateMode();
    }

    // 待确认栏: 确定
    void onPendingConfirm() {
        // GetPosition mode: 弹出坐标操作对话框
        if (m_currentCreateMode == CreateMode::GetPosition) {
            if (m_preview->hasPositionMarker()) {
                QPointF p = m_preview->positionMarker();
                showPositionResultDialog(p.x(), p.y());
            }
            m_pendingConfirm = false;
            m_confirmBar->hide();
            exitCreateMode();
            return;
        }

        // CreateButton mode: 创建虚拟按钮
        if (m_currentCreateMode == CreateMode::CreateButton) {
            if (m_preview->hasPositionMarker()) {
                QPointF p = m_preview->positionMarker();
                bool ok;
                QString name = QInputDialog::getText(this, tr("新建按钮"),
                    tr("请输入按钮名称:"), QLineEdit::Normal, "", &ok);
                if (ok && !name.trimmed().isEmpty()) {
                    name = name.trimmed();
                    auto& bmgr = ScriptButtonManager::instance();
                    ScriptButton btn;
                    btn.id = bmgr.nextId();
                    btn.name = name;
                    btn.x = p.x();
                    btn.y = p.y();
                    bmgr.add(btn);
                    refreshButtonList();
                    m_buttonListWidget->setCurrentRow(m_buttonListWidget->count() - 1);
                } else {
                    return; // 保留待确认状态
                }
            }
            m_pendingConfirm = false;
            m_confirmBar->hide();
            exitCreateMode();
            return;
        }

        // CreateSwipe mode: 创建滑动路径
        if (m_currentCreateMode == CreateMode::CreateSwipe) {
            if (m_preview->hasSwipeStartMarker() && m_preview->hasSwipeEndMarker()) {
                QPointF start = m_preview->swipeStartMarker();
                QPointF end = m_preview->swipeEndMarker();
                bool ok;
                QString name = QInputDialog::getText(this, tr("新建滑动"),
                    tr("请输入滑动名称:"), QLineEdit::Normal, "", &ok);
                if (ok && !name.trimmed().isEmpty()) {
                    name = name.trimmed();
                    auto& smgr = ScriptSwipeManager::instance();
                    ScriptSwipe sw;
                    sw.id = smgr.nextId();
                    sw.name = name;
                    sw.x0 = start.x();
                    sw.y0 = start.y();
                    sw.x1 = end.x();
                    sw.y1 = end.y();
                    smgr.add(sw);
                    refreshSwipeList();
                    m_swipeListWidget->setCurrentRow(m_swipeListWidget->count() - 1);
                } else {
                    return; // 保留待确认状态
                }
            }
            m_pendingConfirm = false;
            m_confirmBar->hide();
            exitCreateMode();
            return;
        }

        QRectF nr = m_preview->pendingRect();
        if (!nr.isValid() || nr.width() < 0.005 || nr.height() < 0.005) {
            onPendingCancel();
            return;
        }

        if (m_currentCreateMode == CreateMode::CreateRegion) {
            // 新建选区: 输入名称
            bool ok;
            QString name = QInputDialog::getText(this, tr("新建选区"),
                tr("请输入选区备注名称:"), QLineEdit::Normal, "", &ok);
            if (ok && !name.trimmed().isEmpty()) {
                name = name.trimmed();
                auto& mgr = SelectionRegionManager::instance();
                if (mgr.nameExists(name)) {
                    QMessageBox::warning(this, tr("名称冲突"),
                        QString(tr("名称 \"%1\" 已存在")).arg(name));
                    return; // 保留待确认状态让用户重试
                }
                SelectionRegion region;
                region.id = mgr.nextId();
                region.name = name;
                region.x0 = nr.left();
                region.y0 = nr.top();
                region.x1 = nr.right();
                region.y1 = nr.bottom();
                mgr.add(region);
                refreshList();
                m_listWidget->setCurrentRow(m_listWidget->count() - 1);
            } else {
                return; // 取消输入名称时保留待确认状态
            }
        } else if (m_currentCreateMode == CreateMode::CreateImage) {
            // 新建图片: 从帧裁剪并保存
            if (!m_preview->frame().isNull()) {
                int imgW = m_preview->frame().width();
                int imgH = m_preview->frame().height();
                QRect cropRect(
                    qRound(nr.left() * imgW), qRound(nr.top() * imgH),
                    qRound(nr.width() * imgW), qRound(nr.height() * imgH));
                cropRect = cropRect.intersected(QRect(0, 0, imgW, imgH));
                if (cropRect.isValid() && cropRect.width() > 0 && cropRect.height() > 0) {
                    QImage cropped = m_preview->frame().copy(cropRect);
                    saveTemplateImage(cropped);
                }
            }
        }

        // 完成后清理
        m_pendingConfirm = false;
        m_preview->clearPendingRect();
        m_confirmBar->hide();
        exitCreateMode();
    }

    // placeholder - 后续步骤添加 private 方法

private:
    // =========================================================
    // 手柄枚举与查找
    // =========================================================
    enum class Handle { None, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };

    void getHighlightCoords(double& x0, double& y0, double& x1, double& y1) const {
        SelectionRegion r;
        if (!SelectionRegionManager::instance().findById(m_preview->highlightId(), r)) {
            x0 = y0 = x1 = y1 = 0; return;
        }
        x0 = r.x0; y0 = r.y0; x1 = r.x1; y1 = r.y1;
    }

    QRectF getHighlightWidgetRect() const {
        double x0, y0, x1, y1;
        if (m_preview->hasOverride()) {
            m_preview->getOverrideCoords(x0, y0, x1, y1);
        } else {
            getHighlightCoords(x0, y0, x1, y1);
        }
        return m_preview->normToWidgetRect(x0, y0, x1, y1);
    }

    // 获取待确认矩形的 widget 坐标
    QRectF getPendingWidgetRect() const {
        QRectF pr = m_preview->pendingRect();
        if (!pr.isValid()) return QRectF();
        return m_preview->normToWidgetRect(pr.left(), pr.top(), pr.right(), pr.bottom());
    }

    Handle hitTestHandle(const QPoint& pos, const QRectF& wr) const {
        if (!wr.isValid()) return Handle::None;
        const double hs = 8.0;
        double l = wr.left(), t = wr.top(), r = wr.right(), b = wr.bottom();
        double cx = wr.center().x(), cy = wr.center().y();

        auto hitNear = [&](double px, double py) {
            return QRectF(px - hs, py - hs, hs * 2, hs * 2).contains(pos);
        };

        if (hitNear(l, t)) return Handle::TopLeft;
        if (hitNear(r, t)) return Handle::TopRight;
        if (hitNear(l, b)) return Handle::BottomLeft;
        if (hitNear(r, b)) return Handle::BottomRight;
        if (hitNear(cx, t)) return Handle::Top;
        if (hitNear(cx, b)) return Handle::Bottom;
        if (hitNear(l, cy)) return Handle::Left;
        if (hitNear(r, cy)) return Handle::Right;

        return Handle::None;
    }

    Handle hitTestHighlightHandle(const QPoint& pos) const {
        if (m_preview->highlightId() < 0) return Handle::None;
        return hitTestHandle(pos, getHighlightWidgetRect());
    }

    Handle hitTestPendingHandle(const QPoint& pos) const {
        return hitTestHandle(pos, getPendingWidgetRect());
    }

    void updateCursorAt(const QPoint& pos) {
        if (m_currentCreateMode != CreateMode::None && !m_pendingConfirm) {
            m_preview->setCursor(Qt::CrossCursor);
            return;
        }

        // GetPosition / CreateButton / CreateSwipe 待确认时仍允许点击重选
        if (m_pendingConfirm && (m_currentCreateMode == CreateMode::GetPosition
            || m_currentCreateMode == CreateMode::CreateButton
            || m_currentCreateMode == CreateMode::CreateSwipe)) {
            m_preview->setCursor(Qt::CrossCursor);
            return;
        }

        // 有待确认矩形时检测其手柄
        if (m_pendingConfirm) {
            Handle h = hitTestPendingHandle(pos);
            if (h != Handle::None) {
                setCursorForHandle(h);
                return;
            }
            QRectF wr = getPendingWidgetRect();
            if (wr.isValid() && wr.contains(pos)) {
                m_preview->setCursor(Qt::OpenHandCursor);
                return;
            }
            m_preview->setCursor(Qt::ArrowCursor);
            return;
        }

        if (m_preview->highlightId() >= 0) {
            Handle h = hitTestHighlightHandle(pos);
            if (h != Handle::None) {
                setCursorForHandle(h);
                return;
            }
            QRectF wr = getHighlightWidgetRect();
            if (wr.isValid() && wr.contains(pos)) {
                m_preview->setCursor(Qt::OpenHandCursor);
                return;
            }
        }

        // 按钮高亮: 检查悬停
        if (m_preview->highlightButtonId() >= 0) {
            ScriptButton btn;
            if (ScriptButtonManager::instance().findById(m_preview->highlightButtonId(), btn)) {
                QPointF wp = m_preview->normToWidget(btn.x, btn.y);
                if (QLineF(QPointF(pos), wp).length() <= 18.0) {
                    m_preview->setCursor(Qt::OpenHandCursor);
                    return;
                }
            }
        }

        // 滑动高亮: 检查悬停
        if (m_preview->highlightSwipeId() >= 0) {
            ScriptSwipe sw;
            if (ScriptSwipeManager::instance().findById(m_preview->highlightSwipeId(), sw)) {
                QPointF wpS = m_preview->normToWidget(sw.x0, sw.y0);
                QPointF wpE = m_preview->normToWidget(sw.x1, sw.y1);
                if (QLineF(QPointF(pos), wpS).length() <= 14.0 ||
                    QLineF(QPointF(pos), wpE).length() <= 14.0) {
                    m_preview->setCursor(Qt::OpenHandCursor);
                    return;
                }
                if (distToSegment(QPointF(pos), wpS, wpE) <= 10.0) {
                    m_preview->setCursor(Qt::OpenHandCursor);
                    return;
                }
            }
        }

        m_preview->setCursor(Qt::ArrowCursor);
    }

    void setCursorForHandle(Handle h) {
        switch (h) {
            case Handle::TopLeft: case Handle::BottomRight:
                m_preview->setCursor(Qt::SizeFDiagCursor); return;
            case Handle::TopRight: case Handle::BottomLeft:
                m_preview->setCursor(Qt::SizeBDiagCursor); return;
            case Handle::Top: case Handle::Bottom:
                m_preview->setCursor(Qt::SizeVerCursor); return;
            case Handle::Left: case Handle::Right:
                m_preview->setCursor(Qt::SizeHorCursor); return;
            default: m_preview->setCursor(Qt::ArrowCursor); return;
        }
    }

    // =========================================================
    // 鼠标事件处理
    // =========================================================
    bool handleMousePress(QMouseEvent* me) {
        if (me->button() != Qt::LeftButton) return false;
        QPoint pos = me->pos();

        // 待确认: GetPosition / CreateButton / CreateSwipe 模式允许重新点击选取
        if (m_pendingConfirm) {
            if (m_currentCreateMode == CreateMode::GetPosition) {
                QPointF normPos = m_preview->widgetToNorm(pos);
                normPos.setX(qBound(0.0, normPos.x(), 1.0));
                normPos.setY(qBound(0.0, normPos.y(), 1.0));
                m_preview->setPositionMarker(normPos);
                m_confirmHintLabel->setText(QString(tr("位置: %1, %2"))
                    .arg(normPos.x(), 0, 'f', 4)
                    .arg(normPos.y(), 0, 'f', 4));
                updateConfirmBarPosition();
                updateHint();
                return true;
            }

            if (m_currentCreateMode == CreateMode::CreateButton) {
                QPointF normPos = m_preview->widgetToNorm(pos);
                normPos.setX(qBound(0.0, normPos.x(), 1.0));
                normPos.setY(qBound(0.0, normPos.y(), 1.0));
                m_preview->setPositionMarker(normPos);
                m_confirmHintLabel->setText(QString(tr("按钮位置: %1, %2"))
                    .arg(normPos.x(), 0, 'f', 4)
                    .arg(normPos.y(), 0, 'f', 4));
                updateConfirmBarPosition();
                updateHint();
                return true;
            }

            if (m_currentCreateMode == CreateMode::CreateSwipe) {
                // 允许重新选择起点/终点
                QPointF normPos = m_preview->widgetToNorm(pos);
                normPos.setX(qBound(0.0, normPos.x(), 1.0));
                normPos.setY(qBound(0.0, normPos.y(), 1.0));
                m_preview->setSwipeEndMarker(normPos);
                m_confirmHintLabel->setText(QString(tr("滑动: A→B")));
                updateConfirmBarPosition();
                updateHint();
                return true;
            }

            // 待确认矩形: 手柄缩放或拖拽
            Handle h = hitTestPendingHandle(pos);
            if (h != Handle::None) {
                m_pendingResizing = true;
                m_dragStart = pos;
                m_pendingResizeHandle = h;
                QRectF pr = m_preview->pendingRect();
                m_pendingOrigX0 = pr.left(); m_pendingOrigY0 = pr.top();
                m_pendingOrigX1 = pr.right(); m_pendingOrigY1 = pr.bottom();
                return true;
            }
            QRectF wr = getPendingWidgetRect();
            if (wr.isValid() && wr.contains(pos)) {
                m_pendingDragging = true;
                m_dragStart = pos;
                QRectF pr = m_preview->pendingRect();
                m_pendingOrigX0 = pr.left(); m_pendingOrigY0 = pr.top();
                m_pendingOrigX1 = pr.right(); m_pendingOrigY1 = pr.bottom();
                m_preview->setCursor(Qt::ClosedHandCursor);
                return true;
            }
            return false;
        }

        // 创建模式: 开始框选 / 获取位置 / 按钮 / 滑动
        if (m_currentCreateMode != CreateMode::None) {
            if (m_currentCreateMode == CreateMode::GetPosition) {
                // 获取位置模式: 设置标记并显示确认栏
                QPointF normPos = m_preview->widgetToNorm(pos);
                normPos.setX(qBound(0.0, normPos.x(), 1.0));
                normPos.setY(qBound(0.0, normPos.y(), 1.0));
                m_preview->setPositionMarker(normPos);

                m_pendingConfirm = true;
                m_confirmHintLabel->setText(QString(tr("位置: %1, %2"))
                    .arg(normPos.x(), 0, 'f', 4)
                    .arg(normPos.y(), 0, 'f', 4));
                updateConfirmBarPosition();
                m_confirmBar->show();

                updateHint();
                return true;
            }

            if (m_currentCreateMode == CreateMode::CreateButton) {
                // 按钮创建: 点击放置标记
                QPointF normPos = m_preview->widgetToNorm(pos);
                normPos.setX(qBound(0.0, normPos.x(), 1.0));
                normPos.setY(qBound(0.0, normPos.y(), 1.0));
                m_preview->setPositionMarker(normPos);

                m_pendingConfirm = true;
                m_confirmHintLabel->setText(QString(tr("按钮位置: %1, %2"))
                    .arg(normPos.x(), 0, 'f', 4)
                    .arg(normPos.y(), 0, 'f', 4));
                updateConfirmBarPosition();
                m_confirmBar->show();
                updateHint();
                return true;
            }

            if (m_currentCreateMode == CreateMode::CreateSwipe) {
                QPointF normPos = m_preview->widgetToNorm(pos);
                normPos.setX(qBound(0.0, normPos.x(), 1.0));
                normPos.setY(qBound(0.0, normPos.y(), 1.0));

                if (!m_preview->hasSwipeStartMarker()) {
                    // 第一次点击: 设置起点
                    m_preview->setSwipeStartMarker(normPos);
                    updateHint();
                    return true;
                } else if (!m_preview->hasSwipeEndMarker()) {
                    // 第二次点击: 设置终点并显示确认栏
                    m_preview->setSwipeEndMarker(normPos);
                    m_pendingConfirm = true;
                    m_confirmHintLabel->setText(tr("滑动: A→B"));
                    updateConfirmBarPosition();
                    m_confirmBar->show();
                    updateHint();
                    return true;
                }
                return true;
            }

            m_selecting = true;
            m_selectStart = pos;
            m_preview->setCreatingRect(QRectF());
            return true;
        }

        // 编辑模式: 手柄缩放
        if (m_preview->highlightId() >= 0) {
            Handle h = hitTestHighlightHandle(pos);
            if (h != Handle::None) {
                m_resizing = true;
                m_dragStart = pos;
                m_resizeHandle = h;
                getHighlightCoords(m_origX0, m_origY0, m_origX1, m_origY1);
                return true;
            }

            // 编辑模式: 拖拽移动
            QRectF wr = getHighlightWidgetRect();
            if (wr.isValid() && wr.contains(pos)) {
                m_dragging = true;
                m_dragStart = pos;
                getHighlightCoords(m_origX0, m_origY0, m_origX1, m_origY1);
                m_preview->setCursor(Qt::ClosedHandCursor);
                return true;
            }
        }

        // 编辑模式: 拖拽按钮
        if (m_preview->highlightButtonId() >= 0) {
            ScriptButton btn;
            if (ScriptButtonManager::instance().findById(m_preview->highlightButtonId(), btn)) {
                double bx = btn.x, by = btn.y;
                if (m_preview->hasOverrideButton()) m_preview->getOverrideButtonPos(bx, by);
                QPointF wp = m_preview->normToWidget(bx, by);
                double dist = QLineF(QPointF(pos), wp).length();
                if (dist <= 18.0) {
                    m_draggingButton = true;
                    m_dragStart = pos;
                    m_origX0 = bx; m_origY0 = by;
                    m_preview->setCursor(Qt::ClosedHandCursor);
                    return true;
                }
            }
        }

        // 编辑模式: 拖拽滑动端点/整体
        if (m_preview->highlightSwipeId() >= 0) {
            ScriptSwipe sw;
            if (ScriptSwipeManager::instance().findById(m_preview->highlightSwipeId(), sw)) {
                double sx0 = sw.x0, sy0 = sw.y0, sx1 = sw.x1, sy1 = sw.y1;
                if (m_preview->hasOverrideSwipe()) m_preview->getOverrideSwipeCoords(sx0, sy0, sx1, sy1);
                QPointF wpStart = m_preview->normToWidget(sx0, sy0);
                QPointF wpEnd = m_preview->normToWidget(sx1, sy1);
                double distS = QLineF(QPointF(pos), wpStart).length();
                double distE = QLineF(QPointF(pos), wpEnd).length();

                if (distS <= 14.0) {
                    m_draggingSwipe = true;
                    m_swipeDragEndpoint = 1; // 起点
                    m_dragStart = pos;
                    m_origX0 = sx0; m_origY0 = sy0; m_origX1 = sx1; m_origY1 = sy1;
                    m_preview->setCursor(Qt::ClosedHandCursor);
                    return true;
                }
                if (distE <= 14.0) {
                    m_draggingSwipe = true;
                    m_swipeDragEndpoint = 2; // 终点
                    m_dragStart = pos;
                    m_origX0 = sx0; m_origY0 = sy0; m_origX1 = sx1; m_origY1 = sy1;
                    m_preview->setCursor(Qt::ClosedHandCursor);
                    return true;
                }
                // 检测是否点击在线段附近 (整体拖拽)
                double lineLen = QLineF(wpStart, wpEnd).length();
                if (lineLen > 1.0) {
                    double d = distToSegment(QPointF(pos), wpStart, wpEnd);
                    if (d <= 10.0) {
                        m_draggingSwipe = true;
                        m_swipeDragEndpoint = 0; // 整体
                        m_dragStart = pos;
                        m_origX0 = sx0; m_origY0 = sy0; m_origX1 = sx1; m_origY1 = sy1;
                        m_preview->setCursor(Qt::ClosedHandCursor);
                        return true;
                    }
                }
            }
        }

        return false;
    }

    bool handleMouseMove(QMouseEvent* me) {
        QPoint pos = me->pos();

        // 待确认矩形: 拖拽
        if (m_pendingDragging) {
            double dw = m_preview->frame().width() * m_preview->scale();
            double dh = m_preview->frame().height() * m_preview->scale();
            if (dw <= 0 || dh <= 0) return true;

            QPoint delta = pos - m_dragStart;
            double dnx = delta.x() / dw, dny = delta.y() / dh;

            double nx0 = m_pendingOrigX0 + dnx, ny0 = m_pendingOrigY0 + dny;
            double nx1 = m_pendingOrigX1 + dnx, ny1 = m_pendingOrigY1 + dny;

            double rw = nx1 - nx0, rh = ny1 - ny0;
            if (nx0 < 0) { nx0 = 0; nx1 = rw; }
            if (ny0 < 0) { ny0 = 0; ny1 = rh; }
            if (nx1 > 1.0) { nx1 = 1.0; nx0 = 1.0 - rw; }
            if (ny1 > 1.0) { ny1 = 1.0; ny0 = 1.0 - rh; }

            m_preview->setPendingRect(QRectF(QPointF(nx0, ny0), QPointF(nx1, ny1)));
            updateConfirmBarPosition();
            return true;
        }

        // 待确认矩形: 手柄缩放
        if (m_pendingResizing) {
            double dw = m_preview->frame().width() * m_preview->scale();
            double dh = m_preview->frame().height() * m_preview->scale();
            if (dw <= 0 || dh <= 0) return true;

            QPoint delta = pos - m_dragStart;
            double dnx = delta.x() / dw, dny = delta.y() / dh;

            double l = m_pendingOrigX0, t = m_pendingOrigY0;
            double r = m_pendingOrigX1, b = m_pendingOrigY1;
            applyHandleDelta(m_pendingResizeHandle, dnx, dny, l, t, r, b);

            if ((r - l) > 0.01 && (b - t) > 0.01) {
                m_preview->setPendingRect(QRectF(QPointF(l, t), QPointF(r, b)));
                updateConfirmBarPosition();
            }
            return true;
        }

        // 创建模式: 更新框选矩形
        if (m_selecting) {
            QPointF sn = m_preview->widgetToNorm(m_selectStart);
            QPointF cn = m_preview->widgetToNorm(pos);
            m_preview->setCreatingRect(QRectF(
                qMin(sn.x(), cn.x()), qMin(sn.y(), cn.y()),
                qAbs(cn.x() - sn.x()), qAbs(cn.y() - sn.y())));
            return true;
        }

        // 拖拽移动高亮选区
        if (m_dragging) {
            double dw = m_preview->frame().width() * m_preview->scale();
            double dh = m_preview->frame().height() * m_preview->scale();
            if (dw <= 0 || dh <= 0) return true;

            QPoint delta = pos - m_dragStart;
            double dnx = delta.x() / dw, dny = delta.y() / dh;

            double nx0 = m_origX0 + dnx, ny0 = m_origY0 + dny;
            double nx1 = m_origX1 + dnx, ny1 = m_origY1 + dny;

            double rw = nx1 - nx0, rh = ny1 - ny0;
            if (nx0 < 0) { nx0 = 0; nx1 = rw; }
            if (ny0 < 0) { ny0 = 0; ny1 = rh; }
            if (nx1 > 1.0) { nx1 = 1.0; nx0 = 1.0 - rw; }
            if (ny1 > 1.0) { ny1 = 1.0; ny0 = 1.0 - rh; }

            m_preview->setOverrideCoords(nx0, ny0, nx1, ny1);
            return true;
        }

        // 手柄缩放高亮选区
        if (m_resizing) {
            double dw = m_preview->frame().width() * m_preview->scale();
            double dh = m_preview->frame().height() * m_preview->scale();
            if (dw <= 0 || dh <= 0) return true;

            QPoint delta = pos - m_dragStart;
            double dnx = delta.x() / dw, dny = delta.y() / dh;

            double l = m_origX0, t = m_origY0, r = m_origX1, b = m_origY1;
            applyHandleDelta(m_resizeHandle, dnx, dny, l, t, r, b);

            if ((r - l) > 0.01 && (b - t) > 0.01) {
                m_preview->setOverrideCoords(l, t, r, b);
            }
            return true;
        }

        // 拖拽按钮
        if (m_draggingButton) {
            double dw = m_preview->frame().width() * m_preview->scale();
            double dh = m_preview->frame().height() * m_preview->scale();
            if (dw <= 0 || dh <= 0) return true;

            QPoint delta = pos - m_dragStart;
            double nx = m_origX0 + delta.x() / dw;
            double ny = m_origY0 + delta.y() / dh;
            nx = qBound(0.0, nx, 1.0);
            ny = qBound(0.0, ny, 1.0);
            m_preview->setOverrideButtonPos(nx, ny);
            return true;
        }

        // 拖拽滑动
        if (m_draggingSwipe) {
            double dw = m_preview->frame().width() * m_preview->scale();
            double dh = m_preview->frame().height() * m_preview->scale();
            if (dw <= 0 || dh <= 0) return true;

            QPoint delta = pos - m_dragStart;
            double dnx = delta.x() / dw, dny = delta.y() / dh;

            double nx0 = m_origX0, ny0 = m_origY0, nx1 = m_origX1, ny1 = m_origY1;
            if (m_swipeDragEndpoint == 0) {
                // 整体移动
                nx0 += dnx; ny0 += dny; nx1 += dnx; ny1 += dny;
                double sw = nx1 - nx0, sh = ny1 - ny0;
                if (nx0 < 0) { nx0 = 0; nx1 = sw; }
                if (ny0 < 0) { ny0 = 0; ny1 = sh; }
                if (nx1 > 1.0) { nx1 = 1.0; nx0 = 1.0 - sw; }
                if (ny1 > 1.0) { ny1 = 1.0; ny0 = 1.0 - sh; }
            } else if (m_swipeDragEndpoint == 1) {
                nx0 += dnx; ny0 += dny;
                nx0 = qBound(0.0, nx0, 1.0);
                ny0 = qBound(0.0, ny0, 1.0);
            } else {
                nx1 += dnx; ny1 += dny;
                nx1 = qBound(0.0, nx1, 1.0);
                ny1 = qBound(0.0, ny1, 1.0);
            }
            m_preview->setOverrideSwipeCoords(nx0, ny0, nx1, ny1);
            return true;
        }

        // 空闲状态: 更新光标
        updateCursorAt(pos);
        return false;
    }

    bool handleMouseRelease(QMouseEvent* me) {
        if (me->button() != Qt::LeftButton) return false;

        // 待确认矩形拖拽/缩放完成
        if (m_pendingDragging || m_pendingResizing) {
            m_pendingDragging = false;
            m_pendingResizing = false;
            m_pendingResizeHandle = Handle::None;
            updateCursorAt(me->pos());
            updateConfirmBarPosition();
            return true;
        }

        // 创建框选完成 → 进入待确认状态
        if (m_selecting) {
            m_selecting = false;
            QRectF nr = m_preview->creatingRect();
            m_preview->clearCreatingRect();

            if (nr.isValid() && nr.width() > 0.01 && nr.height() > 0.01) {
                // 设置待确认矩形
                m_preview->setPendingRect(nr);
                m_pendingConfirm = true;

                // 更新确认栏
                if (m_currentCreateMode == CreateMode::CreateImage) {
                    m_confirmHintLabel->setText(tr("拖拽调整截图区域"));
                } else {
                    m_confirmHintLabel->setText(tr("拖拽调整选区范围"));
                }
                updateConfirmBarPosition();
                m_confirmBar->show();
                m_preview->setCursor(Qt::ArrowCursor);
                updateHint();
            }
            return true;
        }

        // 拖拽/缩放完成 → 提交到管理器
        if (m_dragging || m_resizing) {
            if (m_preview->hasOverride()) {
                double x0, y0, x1, y1;
                m_preview->getOverrideCoords(x0, y0, x1, y1);
                SelectionRegionManager::instance().updateCoords(
                    m_preview->highlightId(), x0, y0, x1, y1);
                m_preview->clearOverride();
            }
            m_dragging = false;
            m_resizing = false;
            m_resizeHandle = Handle::None;
            updateCursorAt(me->pos());
            return true;
        }

        // 按钮拖拽完成 → 提交到管理器
        if (m_draggingButton) {
            if (m_preview->hasOverrideButton()) {
                double x, y;
                m_preview->getOverrideButtonPos(x, y);
                ScriptButtonManager::instance().updateCoords(
                    m_preview->highlightButtonId(), x, y);
                m_preview->clearOverrideButton();
            }
            m_draggingButton = false;
            updateCursorAt(me->pos());
            return true;
        }

        // 滑动拖拽完成 → 提交到管理器
        if (m_draggingSwipe) {
            if (m_preview->hasOverrideSwipe()) {
                double x0, y0, x1, y1;
                m_preview->getOverrideSwipeCoords(x0, y0, x1, y1);
                ScriptSwipeManager::instance().updateCoords(
                    m_preview->highlightSwipeId(), x0, y0, x1, y1);
                m_preview->clearOverrideSwipe();
            }
            m_draggingSwipe = false;
            m_swipeDragEndpoint = 0;
            updateCursorAt(me->pos());
            return true;
        }

        return false;
    }

    void applyHandleDelta(Handle h, double dnx, double dny,
                          double& l, double& t, double& r, double& b) {
        switch (h) {
            case Handle::TopLeft:     l += dnx; t += dny; break;
            case Handle::Top:                   t += dny; break;
            case Handle::TopRight:    r += dnx; t += dny; break;
            case Handle::Right:       r += dnx;           break;
            case Handle::BottomRight: r += dnx; b += dny; break;
            case Handle::Bottom:                b += dny; break;
            case Handle::BottomLeft:  l += dnx; b += dny; break;
            case Handle::Left:        l += dnx;           break;
            default: break;
        }
        if (l > r) std::swap(l, r);
        if (t > b) std::swap(t, b);
        l = qBound(0.0, l, 1.0); t = qBound(0.0, t, 1.0);
        r = qBound(0.0, r, 1.0); b = qBound(0.0, b, 1.0);
    }

    // 点到线段的距离
    static double distToSegment(const QPointF& p, const QPointF& a, const QPointF& b) {
        double dx = b.x() - a.x(), dy = b.y() - a.y();
        double lenSq = dx * dx + dy * dy;
        if (lenSq < 1e-6) return QLineF(p, a).length();
        double t = qBound(0.0, ((p.x() - a.x()) * dx + (p.y() - a.y()) * dy) / lenSq, 1.0);
        QPointF proj(a.x() + t * dx, a.y() + t * dy);
        return QLineF(p, proj).length();
    }

    // =========================================================
    // 创建模式
    // =========================================================
    bool ensureFrame() {
        if (m_preview->frame().isNull()) {
            refreshFrame();
            if (m_preview->frame().isNull()) {
                QMessageBox::warning(this, tr("错误"), tr("无法获取视频帧，请先连接设备"));
                return false;
            }
        }
        return true;
    }

    void enterCreateMode(CreateMode mode) {
        m_currentCreateMode = mode;
        m_preview->setCreateMode(mode);
        m_listWidget->clearSelection();
        m_buttonListWidget->clearSelection();
        m_swipeListWidget->clearSelection();
        m_preview->clearHighlight();
        m_preview->clearHighlightButton();
        m_preview->clearHighlightSwipe();
        m_preview->setCursor(Qt::CrossCursor);
        if (mode == CreateMode::CreateRegion) {
            m_btnNew->setText(tr("取消创建"));
        } else if (mode == CreateMode::CreateImage) {
            m_btnCaptureImage->setText(tr("取消截图"));
        } else if (mode == CreateMode::GetPosition) {
            m_btnGetPos->setText(tr("取消获取"));
            m_preview->clearPositionMarker();
        } else if (mode == CreateMode::CreateButton) {
            m_btnCreateButton->setText(tr("取消创建"));
            m_preview->clearPositionMarker();
        } else if (mode == CreateMode::CreateSwipe) {
            m_btnCreateSwipe->setText(tr("取消创建"));
            m_preview->clearSwipeStartMarker();
            m_preview->clearSwipeEndMarker();
        }
        updateHint();
    }

    void exitCreateMode() {
        m_currentCreateMode = CreateMode::None;
        m_selecting = false;
        m_preview->setCreateMode(CreateMode::None);
        m_preview->clearCreatingRect();
        m_preview->clearPositionMarker();
        m_preview->clearSwipeStartMarker();
        m_preview->clearSwipeEndMarker();
        m_preview->setCursor(Qt::ArrowCursor);
        m_btnNew->setText(tr("新建选区"));
        m_btnCaptureImage->setText(tr("新建图片"));
        m_btnGetPos->setText(tr("获取位置"));
        m_btnCreateButton->setText(tr("新建按钮"));
        m_btnCreateSwipe->setText(tr("新建滑动"));
        updateHint();
    }

    // =========================================================
    // 浮动确认栏位置
    // =========================================================
    void updateConfirmBarPosition() {
        if (!m_confirmBar || !m_preview || !m_scrollArea) return;

        // GetPosition / CreateButton mode: 将确认栏定位到标记点附近
        if ((m_currentCreateMode == CreateMode::GetPosition || m_currentCreateMode == CreateMode::CreateButton)
            && m_preview->hasPositionMarker()) {
            QPointF marker = m_preview->positionMarker();
            QPointF wp = m_preview->normToWidget(marker.x(), marker.y());

            QPoint previewPos(int(wp.x()) - 100, int(wp.y()) - 50);
            QPoint vpPos = m_preview->mapTo(m_scrollArea->viewport(), previewPos);

            int barW = 240;
            int barH = 32;

            if (vpPos.y() < 2) {
                QPoint belowPos(int(wp.x()) - 100, int(wp.y()) + 20);
                vpPos = m_preview->mapTo(m_scrollArea->viewport(), belowPos);
            }

            vpPos.setX(qBound(2, vpPos.x(), m_scrollArea->viewport()->width() - barW - 2));
            vpPos.setY(qBound(2, vpPos.y(), m_scrollArea->viewport()->height() - barH - 2));

            m_confirmBar->setGeometry(vpPos.x(), vpPos.y(), barW, barH);
            return;
        }

        // CreateSwipe mode: 确认栏定位到线段中点附近
        if (m_currentCreateMode == CreateMode::CreateSwipe
            && m_preview->hasSwipeStartMarker() && m_preview->hasSwipeEndMarker()) {
            QPointF start = m_preview->swipeStartMarker();
            QPointF end = m_preview->swipeEndMarker();
            QPointF mid((start.x() + end.x()) / 2.0, (start.y() + end.y()) / 2.0);
            QPointF wp = m_preview->normToWidget(mid.x(), mid.y());

            QPoint previewPos(int(wp.x()) - 100, int(wp.y()) - 50);
            QPoint vpPos = m_preview->mapTo(m_scrollArea->viewport(), previewPos);

            int barW = 200;
            int barH = 32;

            if (vpPos.y() < 2) {
                QPoint belowPos(int(wp.x()) - 80, int(wp.y()) + 20);
                vpPos = m_preview->mapTo(m_scrollArea->viewport(), belowPos);
            }

            vpPos.setX(qBound(2, vpPos.x(), m_scrollArea->viewport()->width() - barW - 2));
            vpPos.setY(qBound(2, vpPos.y(), m_scrollArea->viewport()->height() - barH - 2));

            m_confirmBar->setGeometry(vpPos.x(), vpPos.y(), barW, barH);
            return;
        }

        QRectF pr = m_preview->pendingRect();
        if (!pr.isValid()) return;

        QRectF wr = m_preview->normToWidgetRect(pr.left(), pr.top(), pr.right(), pr.bottom());

        // 在 preview widget 坐标系中矩形上边正上方
        QPoint previewPos(int(wr.left()), int(wr.top()) - 36);

        // 转换为 viewport 坐标
        QPoint vpPos = m_preview->mapTo(m_scrollArea->viewport(), previewPos);

        // 宽度跟随矩形宽度，但有最小/最大限制
        int barW = qBound(180, int(wr.width()), m_scrollArea->viewport()->width() - 20);
        int barH = 32;

        // 如果上方放不下，放到矩形下方
        if (vpPos.y() < 2) {
            QPoint belowPos(int(wr.left()), int(wr.bottom()) + 4);
            vpPos = m_preview->mapTo(m_scrollArea->viewport(), belowPos);
        }

        // 限制在 viewport 范围内
        vpPos.setX(qBound(2, vpPos.x(), m_scrollArea->viewport()->width() - barW - 2));
        vpPos.setY(qBound(2, vpPos.y(), m_scrollArea->viewport()->height() - barH - 2));

        m_confirmBar->setGeometry(vpPos.x(), vpPos.y(), barW, barH);
    }

    // =========================================================
    // 缩放
    // =========================================================
    void zoom(double delta, const QPoint& mousePos = QPoint()) {
        if (!m_preview || !m_scrollArea || !m_scaleLabel) return;
        double oldScale = m_preview->scale();
        double newScale = qBound(0.1, oldScale + delta, 8.0);
        if (qFuzzyCompare(oldScale, newScale)) return;

        if (!mousePos.isNull()) {
            int oldSX = m_scrollArea->horizontalScrollBar()->value();
            int oldSY = m_scrollArea->verticalScrollBar()->value();

            double imgX = mousePos.x() / oldScale;
            double imgY = mousePos.y() / oldScale;

            m_preview->setScale(newScale);

            double newWX = imgX * newScale;
            double newWY = imgY * newScale;

            QPoint vpMouse = mousePos - QPoint(oldSX, oldSY);
            m_scrollArea->horizontalScrollBar()->setValue(int(newWX - vpMouse.x()));
            m_scrollArea->verticalScrollBar()->setValue(int(newWY - vpMouse.y()));
        } else {
            m_preview->setScale(newScale);
        }
        m_scaleLabel->setText(QString("%1%").arg(int(m_preview->scale() * 100)));

        // 缩放后更新确认栏位置
        if (m_pendingConfirm) {
            QTimer::singleShot(0, this, [this]() { updateConfirmBarPosition(); });
        }
    }

    void fitToWindow() {
        if (!m_scrollArea || !m_preview || m_preview->frame().isNull()) return;
        QSize vs = m_scrollArea->viewport()->size();
        QSize is = m_preview->frame().size();
        if (is.isEmpty()) return;
        double sw = double(vs.width() - 20) / is.width();
        double sh = double(vs.height() - 20) / is.height();
        double s = qMin(sw, sh);
        m_preview->setScale(s);
        if (m_scaleLabel) m_scaleLabel->setText(QString("%1%").arg(int(s * 100)));

        if (m_pendingConfirm) {
            QTimer::singleShot(0, this, [this]() { updateConfirmBarPosition(); });
        }
    }

    // =========================================================
    // 辅助方法
    // =========================================================
    void updateHint() {
        if (!m_hintLabel) return;
        if (m_pendingConfirm) {
            if (m_currentCreateMode == CreateMode::GetPosition && m_preview && m_preview->hasPositionMarker()) {
                QPointF p = m_preview->positionMarker();
                m_hintLabel->setText(QString(tr("位置: %1, %2 | 点击重新选取 | 确定继续 | ESC取消"))
                    .arg(p.x(), 0, 'f', 4).arg(p.y(), 0, 'f', 4));
            } else if (m_currentCreateMode == CreateMode::CreateButton && m_preview && m_preview->hasPositionMarker()) {
                QPointF p = m_preview->positionMarker();
                m_hintLabel->setText(QString(tr("按钮: %1, %2 | 点击重新选取 | 确定创建 | ESC取消"))
                    .arg(p.x(), 0, 'f', 4).arg(p.y(), 0, 'f', 4));
            } else if (m_currentCreateMode == CreateMode::CreateSwipe) {
                m_hintLabel->setText(tr("滑动路径已设定 | 点击调整终点 | 确定创建 | ESC取消"));
            } else {
                m_hintLabel->setText(tr("拖拽手柄微调区域 | 确定提交 | 取消放弃 | ESC取消"));
            }
        } else if (m_currentCreateMode == CreateMode::CreateImage) {
            m_hintLabel->setText(tr("在画布上拖动框选截图区域 | ESC取消 | 滚轮缩放"));
        } else if (m_currentCreateMode == CreateMode::CreateRegion) {
            m_hintLabel->setText(tr("在画布上拖动框选新选区 | ESC取消 | 滚轮缩放"));
        } else if (m_currentCreateMode == CreateMode::GetPosition) {
            m_hintLabel->setText(tr("点击画布选取坐标位置 | ESC取消 | 滚轮缩放"));
        } else if (m_currentCreateMode == CreateMode::CreateButton) {
            m_hintLabel->setText(tr("点击画布放置虚拟按钮 | ESC取消 | 滚轮缩放"));
        } else if (m_currentCreateMode == CreateMode::CreateSwipe) {
            if (m_preview && m_preview->hasSwipeStartMarker()) {
                m_hintLabel->setText(tr("点击画布设置滑动终点 (B) | ESC取消"));
            } else {
                m_hintLabel->setText(tr("点击画布设置滑动起点 (A) | ESC取消 | 滚轮缩放"));
            }
        } else if (m_preview && m_preview->highlightId() >= 0) {
            m_hintLabel->setText(tr("拖拽移动选区，拖拽手柄调整大小 | Ctrl+C复制 | Del删除"));
        } else if (m_preview && m_preview->highlightButtonId() >= 0) {
            m_hintLabel->setText(tr("已选中按钮 | 右键菜单操作"));
        } else if (m_preview && m_preview->highlightSwipeId() >= 0) {
            m_hintLabel->setText(tr("已选中滑动 | 右键菜单操作"));
        } else {
            m_hintLabel->setText(tr("滚轮缩放 | 选中左侧列表项后可操作"));
        }
    }

    void refreshList() {
        m_listWidget->clear();
        auto& mgr = SelectionRegionManager::instance();
        mgr.load();
        const auto allRegions = mgr.regions();
        for (const auto& r : allRegions) {
            QListWidgetItem* item = new QListWidgetItem(
                QString("%1 | #%2").arg(r.name).arg(r.id), m_listWidget);
            item->setData(Qt::UserRole, r.id);
        }
        m_infoLabel->setText(QString(tr("共 %1 个选区")).arg(allRegions.size()));
        m_preview->update();
    }

    void refreshButtonList() {
        m_buttonListWidget->clear();
        auto& bmgr = ScriptButtonManager::instance();
        bmgr.load();
        const auto allButtons = bmgr.buttons();
        for (const auto& b : allButtons) {
            QListWidgetItem* item = new QListWidgetItem(
                QString("%1 | #%2").arg(b.name).arg(b.id), m_buttonListWidget);
            item->setData(Qt::UserRole, b.id);
        }
        m_buttonInfoLabel->setText(QString(tr("共 %1 个按钮")).arg(allButtons.size()));
        m_preview->update();
    }

    void refreshSwipeList() {
        m_swipeListWidget->clear();
        auto& smgr = ScriptSwipeManager::instance();
        smgr.load();
        const auto allSwipes = smgr.swipes();
        for (const auto& s : allSwipes) {
            QListWidgetItem* item = new QListWidgetItem(
                QString("%1 | #%2").arg(s.name).arg(s.id), m_swipeListWidget);
            item->setData(Qt::UserRole, s.id);
        }
        m_swipeInfoLabel->setText(QString(tr("共 %1 个滑动")).arg(allSwipes.size()));
        m_preview->update();
    }

    void refreshFrame() {
        if (m_frameGrabCallback) {
            QImage frame = m_frameGrabCallback();
            if (!frame.isNull()) {
                m_preview->setFrame(frame);
            }
        }
    }

    void deleteCurrentRegion(bool confirm) {
        int row = m_listWidget->currentRow();
        if (row < 0) return;
        int id = m_listWidget->item(row)->data(Qt::UserRole).toInt();

        if (confirm) {
            SelectionRegion r;
            QString name = SelectionRegionManager::instance().findById(id, r) ? r.name : tr("未知");
            if (QMessageBox::question(this, tr("确认删除"),
                    QString(tr("确定要删除选区 \"%1\" (#%2) 吗？")).arg(name).arg(id))
                != QMessageBox::Yes) return;
        }

        SelectionRegionManager::instance().remove(id);
        refreshList();
    }

    void renameCurrentRegion() {
        int row = m_listWidget->currentRow();
        if (row < 0) return;
        int id = m_listWidget->item(row)->data(Qt::UserRole).toInt();

        auto& mgr = SelectionRegionManager::instance();
        SelectionRegion region;
        if (!mgr.findById(id, region)) return;

        bool ok;
        QString newName = QInputDialog::getText(this, tr("重命名选区"),
            tr("请输入新名称:"), QLineEdit::Normal, region.name, &ok);
        if (!ok || newName.trimmed().isEmpty()) return;
        newName = newName.trimmed();
        if (newName == region.name) return;

        if (mgr.nameExists(newName, id)) {
            QMessageBox::warning(this, tr("名称冲突"),
                QString(tr("名称 \"%1\" 已被其他选区使用")).arg(newName));
            return;
        }

        mgr.rename(id, newName);
        refreshList();
        m_listWidget->setCurrentRow(row);
    }

    void copyCurrentRegionValue() {
        int row = m_listWidget->currentRow();
        if (row < 0) return;
        int id = m_listWidget->item(row)->data(Qt::UserRole).toInt();
        SelectionRegion r;
        if (!SelectionRegionManager::instance().findById(id, r)) return;
        QApplication::clipboard()->setText(r.coordString());
    }

    void onCreateFindImage(bool selectTemplate) {
        int row = m_listWidget->currentRow();
        if (row < 0) return;
        int id = m_listWidget->item(row)->data(Qt::UserRole).toInt();
        SelectionRegion region;
        if (!SelectionRegionManager::instance().findById(id, region)) return;

        QString imageName = tr("模板图片");
        if (selectTemplate) {
            QString imagesPath = ImageMatcher::getImagesPath();
            QString fileName = QFileDialog::getOpenFileName(
                this, tr("选择模板图片"), imagesPath,
                "Images (*.png *.jpg *.bmp);;All Files (*)");
            if (fileName.isEmpty()) return;
            QFileInfo fi(fileName);
            imageName = fi.completeBaseName();
        }

        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("代码格式"));
        msgBox.setText(tr("选择生成的代码格式:"));
        QPushButton* btnCoord = msgBox.addButton(tr("使用坐标"), QMessageBox::ActionRole);
        QPushButton* btnRegion = msgBox.addButton(tr("使用选区编号"), QMessageBox::ActionRole);
        msgBox.addButton(QMessageBox::Cancel);
        msgBox.exec();

        QString code;
        if (msgBox.clickedButton() == btnCoord) {
            QString x0 = QString::number(region.x0, 'f', 3);
            QString y0 = QString::number(region.y0, 'f', 3);
            QString x1 = QString::number(region.x1, 'f', 3);
            QString y1 = QString::number(region.y1, 'f', 3);
            code = QString(
                "// 区域找图\n"
                "var result = mapi.findImage(\"%1\", %2, %3, %4, %5, 0.8);\n"
                "if (result.found) {\n"
                "    mapi.click(result.x, result.y);\n"
                "    mapi.toast(\"找到目标，置信度: \" + result.confidence.toFixed(2));\n"
                "} else {\n"
                "    mapi.toast(\"未找到目标\");\n"
                "}"
            ).arg(imageName, x0, y0, x1, y1);
        } else if (msgBox.clickedButton() == btnRegion) {
            code = QString(
                "// 按选区编号找图 (选区: %1)\n"
                "var result = mapi.findImageByRegion(\"%2\", %3, 0.8);\n"
                "if (result.found) {\n"
                "    mapi.click(result.x, result.y);\n"
                "    mapi.toast(\"找到目标，置信度: \" + result.confidence.toFixed(2));\n"
                "} else {\n"
                "    mapi.toast(\"未找到目标\");\n"
                "}"
            ).arg(region.name).arg(imageName).arg(id);
        }

        if (!code.isEmpty()) {
            emit codeSnippetGenerated(code);
        }
    }

    void saveTemplateImage(const QImage& image) {
        bool ok;
        QString name = QInputDialog::getText(this, tr("保存模板图片"),
            tr("请输入图片名称 (不含扩展名):"), QLineEdit::Normal, "template", &ok);
        if (!ok || name.isEmpty()) return;

        if (!name.endsWith(".png", Qt::CaseInsensitive)) {
            name += ".png";
        }

        if (ImageMatcher::templateExists(name)) {
            auto btn = QMessageBox::question(this, tr("文件已存在"),
                QString(tr("图片 '%1' 已存在，是否覆盖？")).arg(name),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            if (btn == QMessageBox::Cancel) return;
            if (btn == QMessageBox::No) {
                saveTemplateImage(image);
                return;
            }
        }

        if (ImageMatcher::saveTemplateImage(image, name)) {
            QMessageBox::information(this, tr("成功"),
                QString(tr("模板图片已保存: %1")).arg(name));
        } else {
            QMessageBox::warning(this, tr("错误"), tr("保存图片失败"));
        }
    }

    // =========================================================
    // 获取位置: 结果对话框
    // =========================================================
    void showPositionResultDialog(double x, double y) {
        QString coordStr = QString("%1, %2").arg(x, 0, 'f', 4).arg(y, 0, 'f', 4);

        QDialog dlg(this);
        dlg.setWindowTitle(tr("坐标操作"));
        dlg.setFixedSize(300, 130);
        dlg.setStyleSheet(
            "QDialog { background-color: #18181b; }"
            "QLabel { color: #fafafa; background: transparent; }"
        );
#ifdef Q_OS_WIN
        WinUtils::setDarkBorderToWindow(reinterpret_cast<HWND>(dlg.winId()), true);
#endif

        QVBoxLayout* layout = new QVBoxLayout(&dlg);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(12);

        QLabel* label = new QLabel(QString(tr("坐标: (%1)")).arg(coordStr), &dlg);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet("font-size: 13pt; font-weight: bold; color: #fafafa;");
        layout->addWidget(label);

        QHBoxLayout* btnLayout = new QHBoxLayout();
        btnLayout->setSpacing(8);

        auto styleDlgBtn = [](QPushButton* btn, bool primary = false) {
            if (primary) {
                btn->setStyleSheet(
                    "QPushButton { background: #6366f1; color: #fff; border: none;"
                    "  border-radius: 6px; padding: 8px 16px; font-size: 10pt; }"
                    "QPushButton:hover { background: #818cf8; }"
                    "QPushButton:pressed { background: #4f46e5; }");
            } else {
                btn->setStyleSheet(
                    "QPushButton { background: #27272a; color: #fafafa; border: 1px solid #3f3f46;"
                    "  border-radius: 6px; padding: 8px 16px; font-size: 10pt; }"
                    "QPushButton:hover { background: #3f3f46; border-color: #52525b; }"
                    "QPushButton:pressed { background: #52525b; }");
            }
            btn->setCursor(Qt::PointingHandCursor);
        };

        QPushButton* btnCopy = new QPushButton(tr("复制值"), &dlg);
        styleDlgBtn(btnCopy, true);
        btnLayout->addWidget(btnCopy);

        QPushButton* btnGenerate = new QPushButton(tr("生成"), &dlg);
        styleDlgBtn(btnGenerate);
        btnLayout->addWidget(btnGenerate);

        QPushButton* btnCancel = new QPushButton(tr("取消"), &dlg);
        styleDlgBtn(btnCancel);
        btnLayout->addWidget(btnCancel);

        layout->addLayout(btnLayout);

        connect(btnCopy, &QPushButton::clicked, [&]() {
            QApplication::clipboard()->setText(coordStr);
            dlg.accept();
        });

        connect(btnGenerate, &QPushButton::clicked, [this, &dlg, x, y]() {
            if (showPositionCodeDialog(x, y)) {
                dlg.accept();
            }
        });

        connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);

        dlg.exec();
    }

    // 获取位置: 生成代码子对话框
    bool showPositionCodeDialog(double x, double y) {
        QString coordStr = QString("%1, %2").arg(x, 0, 'f', 4).arg(y, 0, 'f', 4);

        QDialog dlg(this);
        dlg.setWindowTitle(tr("生成代码"));
        dlg.setFixedSize(280, 130);
        dlg.setStyleSheet(
            "QDialog { background-color: #18181b; }"
            "QLabel { color: #fafafa; background: transparent; }"
        );
#ifdef Q_OS_WIN
        WinUtils::setDarkBorderToWindow(reinterpret_cast<HWND>(dlg.winId()), true);
#endif

        QVBoxLayout* layout = new QVBoxLayout(&dlg);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(12);

        QLabel* label = new QLabel(tr("选择生成的操作类型:"), &dlg);
        label->setStyleSheet("font-size: 10pt; color: #a1a1aa;");
        layout->addWidget(label);

        QHBoxLayout* btnLayout = new QHBoxLayout();
        btnLayout->setSpacing(8);

        auto styleDlgBtn = [](QPushButton* btn, bool primary = false) {
            if (primary) {
                btn->setStyleSheet(
                    "QPushButton { background: #6366f1; color: #fff; border: none;"
                    "  border-radius: 6px; padding: 8px 16px; font-size: 10pt; }"
                    "QPushButton:hover { background: #818cf8; }"
                    "QPushButton:pressed { background: #4f46e5; }");
            } else {
                btn->setStyleSheet(
                    "QPushButton { background: #27272a; color: #fafafa; border: 1px solid #3f3f46;"
                    "  border-radius: 6px; padding: 8px 16px; font-size: 10pt; }"
                    "QPushButton:hover { background: #3f3f46; border-color: #52525b; }"
                    "QPushButton:pressed { background: #52525b; }");
            }
            btn->setCursor(Qt::PointingHandCursor);
        };

        QPushButton* btnClick = new QPushButton(tr("点击"), &dlg);
        styleDlgBtn(btnClick, true);
        btnLayout->addWidget(btnClick);

        QPushButton* btnHold = new QPushButton(tr("长按"), &dlg);
        styleDlgBtn(btnHold, true);
        btnLayout->addWidget(btnHold);

        QPushButton* btnBack = new QPushButton(tr("返回"), &dlg);
        styleDlgBtn(btnBack);
        btnLayout->addWidget(btnBack);

        layout->addLayout(btnLayout);

        bool codeGenerated = false;

        connect(btnClick, &QPushButton::clicked, [this, &dlg, &codeGenerated, coordStr]() {
            emit codeSnippetGenerated(QString("mapi.click(%1);").arg(coordStr));
            codeGenerated = true;
            dlg.accept();
        });

        connect(btnHold, &QPushButton::clicked, [this, &dlg, &codeGenerated, coordStr]() {
            emit codeSnippetGenerated(QString("mapi.holdpress(%1);").arg(coordStr));
            codeGenerated = true;
            dlg.accept();
        });

        connect(btnBack, &QPushButton::clicked, &dlg, &QDialog::reject);

        dlg.exec();
        return codeGenerated;
    }

    void styleActionButton(QPushButton* btn) {
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton {"
            "  background-color: #27272a; color: #fafafa;"
            "  border: 1px solid #3f3f46; border-radius: 6px;"
            "  padding: 7px 14px; font-size: 10pt; text-align: left;"
            "}"
            "QPushButton:hover {"
            "  background-color: #3f3f46; border-color: #6366f1;"
            "}"
            "QPushButton:pressed { background-color: #52525b; }"
        );
    }

    // =========================================================
    // 按钮/滑动操作辅助方法
    // =========================================================
    void renameButton(int id) {
        auto& bmgr = ScriptButtonManager::instance();
        ScriptButton btn;
        if (!bmgr.findById(id, btn)) return;

        bool ok;
        QString newName = QInputDialog::getText(this, tr("重命名按钮"),
            tr("请输入新名称:"), QLineEdit::Normal, btn.name, &ok);
        if (!ok || newName.trimmed().isEmpty()) return;
        newName = newName.trimmed();
        if (newName == btn.name) return;

        bmgr.rename(id, newName);
        int row = m_buttonListWidget->currentRow();
        refreshButtonList();
        if (row >= 0 && row < m_buttonListWidget->count())
            m_buttonListWidget->setCurrentRow(row);
    }

    void deleteButton(int id, bool confirm) {
        if (confirm) {
            ScriptButton btn;
            QString name = ScriptButtonManager::instance().findById(id, btn) ? btn.name : tr("未知");
            if (QMessageBox::question(this, tr("确认删除"),
                    QString(tr("确定要删除按钮 \"%1\" (#%2) 吗？")).arg(name).arg(id))
                != QMessageBox::Yes) return;
        }
        ScriptButtonManager::instance().remove(id);
        refreshButtonList();
    }

    void renameSwipe(int id) {
        auto& smgr = ScriptSwipeManager::instance();
        ScriptSwipe sw;
        if (!smgr.findById(id, sw)) return;

        bool ok;
        QString newName = QInputDialog::getText(this, tr("重命名滑动"),
            tr("请输入新名称:"), QLineEdit::Normal, sw.name, &ok);
        if (!ok || newName.trimmed().isEmpty()) return;
        newName = newName.trimmed();
        if (newName == sw.name) return;

        smgr.rename(id, newName);
        int row = m_swipeListWidget->currentRow();
        refreshSwipeList();
        if (row >= 0 && row < m_swipeListWidget->count())
            m_swipeListWidget->setCurrentRow(row);
    }

    void deleteSwipe(int id, bool confirm) {
        if (confirm) {
            ScriptSwipe sw;
            QString name = ScriptSwipeManager::instance().findById(id, sw) ? sw.name : tr("未知");
            if (QMessageBox::question(this, tr("确认删除"),
                    QString(tr("确定要删除滑动 \"%1\" (#%2) 吗？")).arg(name).arg(id))
                != QMessageBox::Yes) return;
        }
        ScriptSwipeManager::instance().remove(id);
        refreshSwipeList();
    }

    // =========================================================
    // 成员变量
    // =========================================================
    QPushButton* m_btnCaptureImage = nullptr;
    QPushButton* m_btnNew = nullptr;
    QPushButton* m_btnGetPos = nullptr;
    QPushButton* m_btnCreateButton = nullptr;
    QPushButton* m_btnCreateSwipe = nullptr;
    QListWidget* m_listWidget = nullptr;
    QListWidget* m_buttonListWidget = nullptr;
    QListWidget* m_swipeListWidget = nullptr;
    QWidget* m_buttonListContainer = nullptr;
    QWidget* m_swipeListContainer = nullptr;
    QWidget* m_regionListContainer = nullptr;
    QLabel* m_infoLabel = nullptr;
    QLabel* m_buttonInfoLabel = nullptr;
    QLabel* m_swipeInfoLabel = nullptr;
    QLabel* m_hintLabel = nullptr;
    QLabel* m_scaleLabel = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    SelectionPreviewWidget* m_preview = nullptr;
    FrameGrabFunc m_frameGrabCallback;

    // 浮动确认栏
    QWidget* m_confirmBar = nullptr;
    QLabel* m_confirmHintLabel = nullptr;
    QPushButton* m_btnConfirmCancel = nullptr;
    QPushButton* m_btnConfirmOk = nullptr;

    // 交互状态
    CreateMode m_currentCreateMode = CreateMode::None;
    bool m_selecting = false;       // 创建模式: 框选中
    bool m_pendingConfirm = false;  // 框选完成, 待确认微调
    bool m_dragging = false;        // 拖拽高亮选区
    bool m_resizing = false;        // 缩放高亮选区
    bool m_draggingButton = false;  // 拖拽高亮按钮
    bool m_draggingSwipe = false;   // 拖拽高亮滑动
    int  m_swipeDragEndpoint = 0;   // 0=整体, 1=起点, 2=终点
    QPoint m_selectStart;
    QPoint m_dragStart;
    double m_origX0 = 0, m_origY0 = 0, m_origX1 = 0, m_origY1 = 0;
    Handle m_resizeHandle = Handle::None;

    // 待确认矩形的拖拽/缩放
    bool m_pendingDragging = false;
    bool m_pendingResizing = false;
    Handle m_pendingResizeHandle = Handle::None;
    double m_pendingOrigX0 = 0, m_pendingOrigY0 = 0, m_pendingOrigX1 = 0, m_pendingOrigY1 = 0;
};

#endif // SELECTIONEDITORDIALOG_H
