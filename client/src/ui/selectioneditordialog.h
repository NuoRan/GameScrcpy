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
#include <functional>

#ifdef Q_OS_WIN
#include "winutils.h"
#endif

#include "selectionregionmanager.h"
#include "imagecapturedialog.h"

// 帧获取回调类型 / Frame grab callback type
using FrameGrabFunc = std::function<QImage()>;

// 创建模式 / Create mode
enum class CreateMode {
    None,           // 无 (普通浏览/编辑) / None (browse/edit)
    CreateRegion,   // 新建选区 / Create region
    CreateImage,    // 新建图片 / Create image
    GetPosition     // 获取位置 (点击获取坐标) / Get position (click for coords)
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

        // 创建图片模式和获取位置模式时隐藏选区
        bool hideRegions = (m_createMode == CreateMode::CreateImage || m_createMode == CreateMode::GetPosition);

        // 绘制所有选区 (创建图片/获取位置模式时不显示)
        if (!hideRegions) {
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

        // 绘制获取位置标记
        if (m_hasPositionMarker) {
            QPointF wp = normToWidget(m_positionMarker.x(), m_positionMarker.y());
            painter.setRenderHint(QPainter::Antialiasing, true);

            // 十字线
            QPen crossPen(QColor(255, 60, 60), 1, Qt::DashLine);
            painter.setPen(crossPen);
            painter.drawLine(QPointF(wp.x(), 0), QPointF(wp.x(), height()));
            painter.drawLine(QPointF(0, wp.y()), QPointF(width(), wp.y()));

            // 外圈
            painter.setPen(QPen(QColor(255, 60, 60), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(wp, 12, 12);

            // 中心点
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 60, 60));
            painter.drawEllipse(wp, 3, 3);

            // 坐标文字
            painter.setPen(Qt::white);
            painter.setFont(QFont("Consolas", 10, QFont::Bold));
            QString coordText = QString("(%1, %2)")
                .arg(m_positionMarker.x(), 0, 'f', 4)
                .arg(m_positionMarker.y(), 0, 'f', 4);
            QPointF textPos = wp + QPointF(16, -8);
            // 背景
            QFontMetrics fm(painter.font());
            QRectF textRect = fm.boundingRect(coordText);
            textRect.moveTopLeft(textPos + QPointF(-2, -fm.ascent()));
            textRect.adjust(-4, -2, 4, 2);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 180));
            painter.drawRoundedRect(textRect, 3, 3);
            painter.setPen(Qt::white);
            painter.drawText(textPos, coordText);

            painter.setRenderHint(QPainter::Antialiasing, false);
        }
    }

private:
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
        QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
        leftLayout->setContentsMargins(12, 12, 12, 12);
        leftLayout->setSpacing(8);

        QLabel* opLabel = new QLabel(tr("操作"), this);
        opLabel->setStyleSheet("color: #a1a1aa; font-size: 9pt; font-weight: bold;");
        leftLayout->addWidget(opLabel);

        // 第一行: 新建图片 + 新建选区
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

        // 第二行: 导入选区 + 打开文件夹
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

        // 第三行: 获取位置
        m_btnGetPos = new QPushButton(tr("获取位置"), this);
        styleActionButton(m_btnGetPos);
        connect(m_btnGetPos, &QPushButton::clicked, this, &SelectionEditorDialog::onToggleGetPositionMode);
        leftLayout->addWidget(m_btnGetPos);

        QFrame* separator = new QFrame(this);
        separator->setFrameShape(QFrame::HLine);
        separator->setStyleSheet("color: #3f3f46;");
        leftLayout->addWidget(separator);

        QLabel* listLabel = new QLabel(tr("选区列表"), this);
        listLabel->setStyleSheet("color: #a1a1aa; font-size: 9pt; font-weight: bold;");
        leftLayout->addWidget(listLabel);

        m_listWidget = new QListWidget(this);
        m_listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
        m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
        connect(m_listWidget, &QListWidget::customContextMenuRequested,
                this, &SelectionEditorDialog::onContextMenu);
        connect(m_listWidget, &QListWidget::currentRowChanged,
                this, &SelectionEditorDialog::onSelectionChanged);
        leftLayout->addWidget(m_listWidget, 1);

        m_infoLabel = new QLabel(tr("共 0 个选区"), this);
        m_infoLabel->setStyleSheet("color: #71717a; font-size: 9pt;");
        leftLayout->addWidget(m_infoLabel);

        splitter->addWidget(leftPanel);

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

        auto& mgr = SelectionRegionManager::instance();
        const auto& regions = mgr.regions();
        if (row >= 0 && row < regions.size()) {
            m_preview->setHighlightId(regions[row].id);
        } else {
            m_preview->clearHighlight();
        }
        updateHint();
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

        // GetPosition 待确认时仍允许点击重选
        if (m_pendingConfirm && m_currentCreateMode == CreateMode::GetPosition) {
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

        if (m_preview->highlightId() < 0) {
            m_preview->setCursor(Qt::ArrowCursor);
            return;
        }
        Handle h = hitTestHighlightHandle(pos);
        if (h != Handle::None) {
            setCursorForHandle(h);
            return;
        }
        QRectF wr = getHighlightWidgetRect();
        if (wr.isValid() && wr.contains(pos)) {
            m_preview->setCursor(Qt::OpenHandCursor);
        } else {
            m_preview->setCursor(Qt::ArrowCursor);
        }
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

        // 待确认: GetPosition 模式允许重新点击选取
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

        // 创建模式: 开始框选 / 获取位置
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
        m_preview->clearHighlight();
        m_preview->setCursor(Qt::CrossCursor);
        if (mode == CreateMode::CreateRegion) {
            m_btnNew->setText(tr("取消创建"));
        } else if (mode == CreateMode::CreateImage) {
            m_btnCaptureImage->setText(tr("取消截图"));
        } else if (mode == CreateMode::GetPosition) {
            m_btnGetPos->setText(tr("取消获取"));
            m_preview->clearPositionMarker();
        }
        updateHint();
    }

    void exitCreateMode() {
        m_currentCreateMode = CreateMode::None;
        m_selecting = false;
        m_preview->setCreateMode(CreateMode::None);
        m_preview->clearCreatingRect();
        m_preview->clearPositionMarker();
        m_preview->setCursor(Qt::ArrowCursor);
        m_btnNew->setText(tr("新建选区"));
        m_btnCaptureImage->setText(tr("新建图片"));
        m_btnGetPos->setText(tr("获取位置"));
        updateHint();
    }

    // =========================================================
    // 浮动确认栏位置
    // =========================================================
    void updateConfirmBarPosition() {
        if (!m_confirmBar || !m_preview || !m_scrollArea) return;

        // GetPosition mode: 将确认栏定位到标记点附近
        if (m_currentCreateMode == CreateMode::GetPosition && m_preview->hasPositionMarker()) {
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
            } else {
                m_hintLabel->setText(tr("拖拽手柄微调区域 | 确定提交 | 取消放弃 | ESC取消"));
            }
        } else if (m_currentCreateMode == CreateMode::CreateImage) {
            m_hintLabel->setText(tr("在画布上拖动框选截图区域 | ESC取消 | 滚轮缩放"));
        } else if (m_currentCreateMode == CreateMode::CreateRegion) {
            m_hintLabel->setText(tr("在画布上拖动框选新选区 | ESC取消 | 滚轮缩放"));
        } else if (m_currentCreateMode == CreateMode::GetPosition) {
            m_hintLabel->setText(tr("点击画布选取坐标位置 | ESC取消 | 滚轮缩放"));
        } else if (m_preview && m_preview->highlightId() >= 0) {
            m_hintLabel->setText(tr("拖拽移动选区，拖拽手柄调整大小 | Ctrl+C复制 | Del删除"));
        } else {
            m_hintLabel->setText(tr("滚轮缩放 | 选中左侧选区后可拖拽调整"));
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
    // 成员变量
    // =========================================================
    QPushButton* m_btnCaptureImage = nullptr;
    QPushButton* m_btnNew = nullptr;
    QPushButton* m_btnGetPos = nullptr;
    QListWidget* m_listWidget = nullptr;
    QLabel* m_infoLabel = nullptr;
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
