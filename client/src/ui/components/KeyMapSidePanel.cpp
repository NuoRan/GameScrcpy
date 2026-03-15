/**
 * @file KeyMapSidePanel.cpp
 * @brief 侧边键位面板实现 — 替代 ToolForm 的全部键位编辑功能
 *
 * 从 VideoForm 右侧滑入（宽度从 0→260px 动画），包含配置管理、
 * 可拖拽键位组件、显示设置 以及 拟人化参数滑块。
 */
#include "KeyMapSidePanel.h"
#include "FluentButton.h"
#include "FluentSlider.h"
#include "FluentToggle.h"
#include "FluentCard.h"
#include "ThemeManager.h"
#include "DesignTokens.h"
#include "MotionTokens.h"
#include "ConfigCenter.h"
#include "FluentDialog.h"

// 复用 ToolForm 中已有的 DraggableLabel & KeyMapType
#include "toolform.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <filesystem>
#include <fstream>
#include <QLabel>
#include <QPainter>
#include <windows.h>
#include <shellapi.h>

using namespace Fluent;

// ============================================================
// 构造
// ============================================================
KeyMapSidePanel::KeyMapSidePanel(QWidget* parent)
    : QWidget(parent)
{
    // 面板本身不可见 (宽度 0)
    setFixedWidth(0);
    setMinimumHeight(200);

    // 展开 / 折叠动画
    m_expandAnim = new QPropertyAnimation(this, "panelWidth", this);
    m_expandAnim->setDuration(Motion::Slow);
    m_expandAnim->setEasingCurve(Motion::defaultCurve());

    setupUI();
}

// ============================================================
// 展开 / 折叠
// ============================================================
void KeyMapSidePanel::setExpanded(bool expanded)
{
    if (m_expanded == expanded) return;
    m_expanded = expanded;

    // 直接切换，不使用动画
    setPanelWidth(expanded ? m_expandedWidth : 0);

    emit editModeChanged(expanded);
}

void KeyMapSidePanel::setPanelWidth(int w)
{
    m_currentWidth = w;
    setFixedWidth(w);
    // 当宽度 > 一定阈值时才显示内容
    setVisible(w > 4);
}

// ============================================================
// 配置列表
// ============================================================
void KeyMapSidePanel::refreshConfigList()
{
    if (!m_configCombo) return;
    QString current = m_configCombo->currentText();
    m_configCombo->blockSignals(true);
    m_configCombo->clear();
    namespace fs = std::filesystem;
    fs::path dirPath("keymap");
    if (!fs::exists(dirPath)) fs::create_directories(dirPath);
    QStringList files;
    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
            files << QString::fromStdString(entry.path().filename().string());
    }
    files.sort();
    if (files.isEmpty())
        m_configCombo->addItem("default.json");
    else
        m_configCombo->addItems(files);
    int idx = m_configCombo->findText(current);
    if (idx >= 0)
        m_configCombo->setCurrentIndex(idx);
    m_configCombo->blockSignals(false);
}

QString KeyMapSidePanel::currentConfig() const
{
    return m_configCombo ? m_configCombo->currentText() : "default.json";
}

void KeyMapSidePanel::setCurrentConfig(const QString& filename)
{
    if (!m_configCombo) return;
    refreshConfigList();
    int idx = m_configCombo->findText(filename);
    if (idx >= 0) {
        m_configCombo->blockSignals(true);
        m_configCombo->setCurrentIndex(idx);
        m_configCombo->blockSignals(false);
    }
}

void KeyMapSidePanel::setOverlayButtonState(bool checked)
{
    if (m_overlayToggle) {
        m_overlayToggle->blockSignals(true);
        m_overlayToggle->setChecked(checked);
        m_overlayToggle->blockSignals(false);
    }
}

void KeyMapSidePanel::setOverlayChecked(bool checked)
{
    setOverlayButtonState(checked);
}

void KeyMapSidePanel::setOverlayOpacity(int value)
{
    if (m_overlayOpacitySlider) {
        m_overlayOpacitySlider->blockSignals(true);
        m_overlayOpacitySlider->setValue(value);
        m_overlayOpacitySlider->blockSignals(false);
    }
}

void KeyMapSidePanel::setTipOpacity(int value)
{
    if (m_tipOpacitySlider) {
        m_tipOpacitySlider->blockSignals(true);
        m_tipOpacitySlider->setValue(value);
        m_tipOpacitySlider->blockSignals(false);
    }
}

// ============================================================
// 绘制背景
// ============================================================
void KeyMapSidePanel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    auto& tm = ThemeManager::instance();
    // 背景
    p.fillRect(rect(), QColor(tm.surface()));
    // 左边线
    p.setPen(QPen(QColor(tm.border()), 1));
    p.drawLine(0, 0, 0, height());
}

// ============================================================
// UI 构建
// ============================================================
void KeyMapSidePanel::setupUI()
{
    auto& tm = ThemeManager::instance();

    // 整体滚动区
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet(QString("QScrollArea{background:transparent;border:none;}"
                               "QScrollBar:vertical{width:4px;background:transparent;}"
                               "QScrollBar::handle:vertical{background:%1;border-radius:2px;}"
                               "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}").arg(tm.scrollThumb()));

    auto* content = new QWidget();
    auto* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(6);

    // === 辅助 lambda: 分组卡片容器 ===
    auto makeGroupCard = [&tm](QLayout* innerLayout) -> QWidget* {
        auto* card = new QWidget();
        card->setStyleSheet(QString("background:%1;border:1px solid %2;border-radius:8px;")
            .arg(tm.card(), tm.borderSoft()));
        auto* wrapper = new QVBoxLayout(card);
        wrapper->setContentsMargins(8, 6, 8, 6);
        wrapper->setSpacing(4);
        if (innerLayout) wrapper->addLayout(innerLayout);
        return card;
    };

    auto makeSectionLabel = [&tm](const QString& text) -> QLabel* {
        auto* lbl = new QLabel(text);
        lbl->setStyleSheet(QString("color:%1;font-size:10px;font-weight:600;background:transparent;letter-spacing:1px;")
            .arg(tm.textTertiary()));
        return lbl;
    };

    // ---- 标题行：键位配置 + 关闭按钮 ----
    auto* titleBar = new QHBoxLayout();
    auto* titleLabel = new QLabel(tr("键位配置"));
    titleLabel->setStyleSheet(QString("color:%1;font-size:12px;font-weight:700;background:transparent;").arg(tm.textPrimary()));
    titleBar->addWidget(titleLabel);
    titleBar->addStretch();
    auto* closeBtn = new FluentButton(QStringLiteral("x"), FluentButton::Ghost, nullptr);
    closeBtn->setFixedSize(24, 24);
    connect(closeBtn, &FluentButton::clicked, this, [this]() {
        setExpanded(false);
        emit closeRequested();
    });
    titleBar->addWidget(closeBtn);
    mainLayout->addLayout(titleBar);

    // ---- 配置选择（卡片分组）----
    mainLayout->addWidget(makeSectionLabel(tr("配置")));

    auto* configInner = new QVBoxLayout();
    configInner->setSpacing(4);

    m_configCombo = new QComboBox();
    m_configCombo->setMinimumHeight(26);
    m_configCombo->setStyleSheet(
        QString("QComboBox{background:%1;color:%2;border:1px solid %3;border-radius:5px;"
        "padding:2px 6px;font-size:11px;}"
        "QComboBox:hover{border-color:%4;}"
        "QComboBox::drop-down{border:none;width:18px;}"
        "QComboBox::down-arrow{image:none;width:0;height:0;border-style:solid;"
        "border-width:4px 3px 0 3px;border-color:%5 transparent transparent transparent;}"
        "QComboBox QAbstractItemView{background:%1;border:1px solid %3;border-radius:6px;padding:4px;}"
        "QComboBox QAbstractItemView::item{padding:4px 6px;border-radius:4px;color:%2;font-size:11px;}"
        "QComboBox QAbstractItemView::item:hover{background:%6;}"
        ).arg(tm.inputBg(), tm.textPrimary(), tm.inputBorder(),
              tm.accentPrimary(), tm.textTertiary(), tm.navHover())
    );
    connect(m_configCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        if (!text.isEmpty()) emit configChanged(text);
    });
    configInner->addWidget(m_configCombo);

    // 按钮行：新建 | 刷新 | 文件夹
    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(3);
    QString btnStyle = QString(
        "QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:5px;font-size:12px;padding:2px 0;}"
        "QPushButton:hover{background:%4;border-color:%5;}")
        .arg(tm.inputBg(), tm.textPrimary(), tm.inputBorder(), tm.navHover(), tm.accentPrimary());

    m_newBtn = new QPushButton("+");
    m_newBtn->setFixedHeight(24);
    m_newBtn->setToolTip(tr("新建配置"));
    m_newBtn->setStyleSheet(btnStyle);

    m_refreshBtn = new QPushButton("↻");
    m_refreshBtn->setFixedHeight(24);
    m_refreshBtn->setToolTip(tr("刷新配置列表"));
    m_refreshBtn->setStyleSheet(btnStyle);

    m_folderBtn = new QPushButton("📁");
    m_folderBtn->setFixedHeight(24);
    m_folderBtn->setToolTip(tr("打开配置文件夹"));
    m_folderBtn->setStyleSheet(QString(
        "QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:5px;font-size:10px;padding:2px 0;}"
        "QPushButton:hover{background:%4;border-color:%5;}")
        .arg(tm.inputBg(), tm.textPrimary(), tm.inputBorder(), tm.navHover(), tm.accentPrimary()));

    btnRow->addWidget(m_newBtn, 1);
    btnRow->addWidget(m_refreshBtn, 1);
    btnRow->addWidget(m_folderBtn, 1);
    configInner->addLayout(btnRow);

    mainLayout->addWidget(makeGroupCard(configInner));

    // 新建按钮 → 弹窗输入名称后创建空配置文件
    connect(m_newBtn, &QPushButton::clicked, this, [this]() {
        QString name = FluentDialog::input(this, tr("新建键位配置"), tr("请输入配置名称"), "new_config");
        if (name.isEmpty()) return;
        if (!name.endsWith(".json")) name += ".json";
        namespace fs = std::filesystem;
        fs::path dirPath("keymap");
        if (!fs::exists(dirPath)) fs::create_directories(dirPath);
        fs::path filePath = dirPath / name.toStdString();
        if (fs::exists(filePath)) {
            FluentDialog::info(this, tr("提示"), tr("配置文件 \"%1\" 已存在").arg(name));
            return;
        }
        std::ofstream file(filePath.string());
        if (file) {
            file << "{}"; file.close();
            refreshConfigList();
            m_configCombo->setCurrentText(name);
        }
    });

    connect(m_refreshBtn, &QPushButton::clicked, this, [this]() {
        emit configChanged(currentConfig());
    });

    connect(m_folderBtn, &QPushButton::clicked, this, []() {
        namespace fs = std::filesystem;
        fs::path dirPath("keymap");
        if (!fs::exists(dirPath)) fs::create_directories(dirPath);
        ShellExecuteW(nullptr, L"open", fs::absolute(dirPath).wstring().c_str(), nullptr, nullptr, SW_SHOW);
    });

    // 保存按钮
    m_saveBtn = new FluentButton(tr("保存"), FluentButton::Primary, nullptr);
    m_saveBtn->setFixedHeight(28);
    connect(m_saveBtn, &FluentButton::clicked, this, &KeyMapSidePanel::saveRequested);
    mainLayout->addWidget(m_saveBtn);

    // ---- 显示键位 Toggle ----
    mainLayout->addSpacing(2);
    mainLayout->addWidget(makeSectionLabel(tr("显示")));

    auto* overlayCard = new QWidget();
    overlayCard->setStyleSheet(QString("background:%1;border:1px solid %2;border-radius:8px;")
        .arg(tm.card(), tm.borderSoft()));
    auto* overlayCardLayout = new QHBoxLayout(overlayCard);
    overlayCardLayout->setContentsMargins(8, 5, 8, 5);
    auto* overlayLabel = new QLabel(tr("显示键位"));
    overlayLabel->setStyleSheet(QString("color:%1;font-size:11px;background:transparent;border:none;").arg(tm.textPrimary()));
    m_overlayToggle = new FluentToggle();
    m_overlayToggle->setChecked(qsc::ConfigCenter::instance().keyMapOverlayVisible());
    connect(m_overlayToggle, &FluentToggle::toggled, this, &KeyMapSidePanel::overlayToggled);
    overlayCardLayout->addWidget(overlayLabel);
    overlayCardLayout->addStretch();
    overlayCardLayout->addWidget(m_overlayToggle);
    mainLayout->addWidget(overlayCard);

    // ---- 可拖拽键位组件 ----
    mainLayout->addSpacing(2);
    mainLayout->addWidget(makeSectionLabel(tr("组件")));

    auto* dragTitle = new QLabel(tr("拖拽到视频窗口添加"));
    dragTitle->setStyleSheet(QString("color:%1;font-size:10px;background:transparent;").arg(tm.textTertiary()));
    mainLayout->addWidget(dragTitle);

    // 两列布局（卡片内）
    auto* dragInner = new QVBoxLayout();
    dragInner->setSpacing(4);
    auto* dragGrid = new QHBoxLayout();
    auto* col1 = new QVBoxLayout();
    auto* col2 = new QVBoxLayout();
    col1->setSpacing(4);
    col2->setSpacing(4);

    m_clickLabel   = new DraggableLabel(KMT_SCRIPT, tr("点击"), content, "click");
    m_holdLabel    = new DraggableLabel(KMT_SCRIPT, tr("长按"), content, "hold");
    m_scriptLabel  = new DraggableLabel(KMT_SCRIPT, tr("脚本"), content);
    m_steerLabel   = new DraggableLabel(KMT_STEER_WHEEL, tr("轮盘"), content);
    m_cameraLabel  = new DraggableLabel(KMT_CAMERA_MOVE, tr("视角"), content);
    m_freeLookLabel = new DraggableLabel(KMT_FREE_LOOK, tr("小眼睛"), content);

    col1->addWidget(m_clickLabel, 0, Qt::AlignHCenter);
    col1->addWidget(m_scriptLabel, 0, Qt::AlignHCenter);
    col1->addWidget(m_cameraLabel, 0, Qt::AlignHCenter);

    col2->addWidget(m_holdLabel, 0, Qt::AlignHCenter);
    col2->addWidget(m_steerLabel, 0, Qt::AlignHCenter);
    col2->addWidget(m_freeLookLabel, 0, Qt::AlignHCenter);

    dragGrid->addLayout(col1);
    dragGrid->addLayout(col2);
    dragInner->addLayout(dragGrid);
    mainLayout->addWidget(makeGroupCard(dragInner));

    // ---- 拟人化参数 ----
    mainLayout->addSpacing(2);
    mainLayout->addWidget(makeSectionLabel(tr("参数")));

    auto* paramLayout = new QVBoxLayout();
    paramLayout->setSpacing(3);

    auto addSlider = [&](const QString& label, const QString& desc, int initial) -> FluentSlider* {
        auto* sl = new FluentSlider();
        sl->setLabel(label);
        sl->setRange(0, 100);
        sl->setValue(initial);
        sl->setShowValue(true);
        sl->setFixedHeight(24);
        paramLayout->addWidget(sl);
        if (!desc.isEmpty()) {
            auto* d = new QLabel(desc);
            d->setStyleSheet(QString("color:%1;font-size:9px;background:transparent;border:none;").arg(tm.textTertiary()));
            d->setWordWrap(true);
            paramLayout->addWidget(d);
        }
        return sl;
    };

    m_randomOffsetSlider = addSlider(tr("随机偏移"), QString(),
                                      qsc::ConfigCenter::instance().randomOffset());
    m_steerSmoothSlider  = addSlider(tr("轮盘平滑"), tr("0=瞬间移动, 100=高平滑缓动"),
                                      qsc::ConfigCenter::instance().steerWheelSmooth());
    m_steerCurveSlider   = addSlider(tr("轮盘曲线"), tr("0=直线移动, 100=最大弧度"),
                                      qsc::ConfigCenter::instance().steerWheelCurve());
    m_slideCurveSlider   = addSlider(tr("滑动曲线"), tr("脚本 slide 等 API 的轨迹曲线"),
                                      qsc::ConfigCenter::instance().slideCurve());

    mainLayout->addWidget(makeGroupCard(paramLayout));

    // 自动保存到 ConfigCenter
    connect(m_randomOffsetSlider, &FluentSlider::valueChanged, [](int v) {
        qsc::ConfigCenter::instance().setRandomOffset(v);
    });
    connect(m_steerSmoothSlider, &FluentSlider::valueChanged, [](int v) {
        qsc::ConfigCenter::instance().setSteerWheelSmooth(v);
    });
    connect(m_steerCurveSlider, &FluentSlider::valueChanged, [](int v) {
        qsc::ConfigCenter::instance().setSteerWheelCurve(v);
    });
    connect(m_slideCurveSlider, &FluentSlider::valueChanged, [](int v) {
        qsc::ConfigCenter::instance().setSlideCurve(v);
    });

    // ---- 透明度 ----
    mainLayout->addSpacing(2);
    mainLayout->addWidget(makeSectionLabel(tr("透明度")));

    auto* opacityLayout = new QVBoxLayout();
    opacityLayout->setSpacing(3);

    auto addOpSlider = [&](const QString& label, const QString& desc, int initial) -> FluentSlider* {
        auto* sl = new FluentSlider();
        sl->setLabel(label);
        sl->setRange(0, 100);
        sl->setValue(initial);
        sl->setShowValue(true);
        sl->setFixedHeight(24);
        opacityLayout->addWidget(sl);
        if (!desc.isEmpty()) {
            auto* d = new QLabel(desc);
            d->setStyleSheet(QString("color:%1;font-size:9px;background:transparent;border:none;").arg(tm.textTertiary()));
            d->setWordWrap(true);
            opacityLayout->addWidget(d);
        }
        return sl;
    };

    m_overlayOpacitySlider = addOpSlider(tr("键位提示"), tr("0=全透明, 100=不透明"),
                                        qsc::ConfigCenter::instance().keyMapOverlayOpacity());
    m_tipOpacitySlider     = addOpSlider(tr("脚本弹窗"), tr("0=全透明, 100=不透明"),
                                        qsc::ConfigCenter::instance().scriptTipOpacity());

    mainLayout->addWidget(makeGroupCard(opacityLayout));

    connect(m_overlayOpacitySlider, &FluentSlider::valueChanged, this, [this](int v) {
        qsc::ConfigCenter::instance().setKeyMapOverlayOpacity(v);
        emit overlayOpacityChanged(v);
    });
    connect(m_tipOpacitySlider, &FluentSlider::valueChanged, this, [this](int v) {
        qsc::ConfigCenter::instance().setScriptTipOpacity(v);
        emit scriptTipOpacityChanged(v);
    });

    // 弹簧
    mainLayout->addStretch();

    scrollArea->setWidget(content);

    // 面板的布局
    auto* panelLayout = new QVBoxLayout(this);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->addWidget(scrollArea);
}
