/**
 * @file VideoSettingsPopup.cpp
 * @brief 视频流实时设置弹窗 — 与主界面 SettingsPage 同风格
 *
 * 遮罩 + 居中卡片弹窗。
 * 码率/帧率/分辨率从 Config (userdata.ini) 读写，与主界面 SettingsPage 共享同一配置源。
 * 显示选项从 ConfigCenter 读写。
 */
#include "VideoSettingsPopup.h"
#include "FluentCard.h"
#include "FluentSlider.h"
#include "FluentToggle.h"
#include "SettingRow.h"
#include "ThemeManager.h"
#include "DesignTokens.h"
#include "MotionTokens.h"
#include "ConfigCenter.h"
#include "config.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QLabel>
#include <QFrame>
#include <QPainter>
#include <QKeyEvent>
#include <QIntValidator>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QPushButton>

using namespace Fluent;

// ── helpers (与 SettingsPage 同) ──

static QLabel* makeFormLabel(const QString& text = {})
{
    auto* l = new QLabel(text);
    l->setStyleSheet(QStringLiteral("background: transparent; padding: 0;"));
    return l;
}

static QLabel* makeSectionTitle(const QString& text, const ThemeManager& tm)
{
    auto* l = new QLabel(text);
    l->setStyleSheet(QStringLiteral(
        "font-size: 15px; font-weight: 700; color: %1; background: transparent; padding: 0;")
        .arg(tm.textPrimary()));
    return l;
}

static QFrame* makeSeparator()
{
    auto* f = new QFrame;
    f->setFrameShape(QFrame::HLine);
    f->setFixedHeight(1);
    return f;
}

// ================================================================

VideoSettingsPopup::VideoSettingsPopup(QWidget* parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);

    setupUI();
    applyStyle();
    syncFromConfig();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        applyStyle();
    });
}

void VideoSettingsPopup::setupUI()
{
    auto& tm = ThemeManager::instance();

    // 外层: 全尺寸遮罩，居中放 card
    auto* overlay = new QVBoxLayout(this);
    overlay->setAlignment(Qt::AlignCenter);
    overlay->setContentsMargins(40, 40, 40, 40);

    m_card = new QWidget(this);
    m_card->setObjectName(QStringLiteral("settingsCard"));
    m_card->setMinimumWidth(440);
    m_card->setMaximumWidth(540);

    auto* scrollArea = new QScrollArea(m_card);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget;
    auto* main = new QVBoxLayout(content);
    main->setContentsMargins(32, 28, 32, 20);
    main->setSpacing(20);

    // ═══════════ 视频参数 (全部可实时调整) ═══════════
    auto* videoTitle = makeSectionTitle(tr("视频参数"), tm);
    main->addWidget(videoTitle);

    auto* videoCard = new FluentCard;
    auto* vl = new QVBoxLayout(videoCard);
    vl->setContentsMargins(20, 16, 20, 16);
    vl->setSpacing(12);

    // 码率 | 帧率 | 分辨率 — 单行三列等宽布局
    auto* paramsRow = new QHBoxLayout;
    paramsRow->setSpacing(12);

    // 码率
    {
        auto* col = new QVBoxLayout;
        col->setSpacing(4);
        auto* bitrateLabel = makeFormLabel(tr("码率"));
        m_bitRateEdit = new QLineEdit(QStringLiteral("8"));
        m_bitRateEdit->setMinimumHeight(36);
        m_bitRateEdit->setAlignment(Qt::AlignCenter);
        m_bitRateEdit->setValidator(new QIntValidator(1, 99999, this));
        col->addWidget(bitrateLabel);
        col->addWidget(m_bitRateEdit);
        paramsRow->addLayout(col, 1);
    }

    // 帧率
    {
        auto* col = new QVBoxLayout;
        col->setSpacing(4);
        auto* fpsLabel = makeFormLabel(tr("帧率"));
        m_fpsBox = new Fluent::FluentComboBox;
        m_fpsBox->addItems({QStringLiteral("5"), QStringLiteral("10"), QStringLiteral("20"),
                            QStringLiteral("30"), QStringLiteral("60"), QStringLiteral("90"),
                            QStringLiteral("120"), QStringLiteral("144"), QStringLiteral("165"),
                            QStringLiteral("240")});
        m_fpsBox->addItem(tr("不限制"));
        m_fpsBox->setItemData(m_fpsBox->count() - 1, -1);
        m_fpsBox->setMinimumHeight(36);
        col->addWidget(fpsLabel);
        col->addWidget(m_fpsBox);
        paramsRow->addLayout(col, 1);
    }

    // 分辨率
    {
        auto* col = new QVBoxLayout;
        col->setSpacing(4);
        auto* sizeLabel = makeFormLabel(tr("分辨率"));
        m_maxSizeBox = new Fluent::FluentComboBox;
        m_maxSizeBox->addItems({QStringLiteral("320"), QStringLiteral("480"), QStringLiteral("640"),
                                QStringLiteral("720"), QStringLiteral("1080")});
        m_maxSizeBox->addItem(tr("原始"));
        m_maxSizeBox->setMinimumHeight(36);
        col->addWidget(sizeLabel);
        col->addWidget(m_maxSizeBox);
        paramsRow->addLayout(col, 1);
    }

    vl->addLayout(paramsRow);

    auto* noteLabel = new QLabel(tr("修改帧率或分辨率会短暂重启编码器 (~200ms)"));
    noteLabel->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 11px; background: transparent; padding: 4px 0 0 0;").arg(tm.textTertiary()));
    noteLabel->setWordWrap(true);
    vl->addWidget(noteLabel);

    main->addWidget(videoCard);

    // 码率回车 → 立即发送
    connect(m_bitRateEdit, &QLineEdit::editingFinished, this, [this]() {
        flushAndSaveVideoParams();
    });

    // 帧率 / 分辨率选择变化 → 立即发送
    connect(m_fpsBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        flushAndSaveVideoParams();
    });
    connect(m_maxSizeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        flushAndSaveVideoParams();
    });

    // ═══════════ 显示选项 ═══════════
    main->addWidget(makeSeparator());
    auto* optTitle = makeSectionTitle(tr("显示选项"), tm);
    main->addWidget(optTitle);

    auto* optCard = new FluentCard;
    auto* ol = new QVBoxLayout(optCard);
    ol->setContentsMargins(20, 12, 20, 12);
    ol->setSpacing(4);

    // 显示键位
    {
        m_overlayToggle = new FluentToggle;
        auto* sr = new SettingRow;
        sr->setTitle(tr("显示键位"));
        sr->setDescription(tr("在视频画面上显示键位提示"));
        sr->setWidget(m_overlayToggle);
        ol->addWidget(sr);

        connect(m_overlayToggle, &FluentToggle::toggled, this, [this](bool checked) {
            if (checked != m_lastOverlayVisible) {
                m_lastOverlayVisible = checked;
                qsc::ConfigCenter::instance().setKeyMapOverlayVisible(checked);
                emit overlayVisibleChanged(checked);
            }
        });
    }

    // 键位透明度
    {
        m_overlayOpacitySlider = new FluentSlider;
        m_overlayOpacitySlider->setRange(0, 100);
        m_overlayOpacitySlider->setSuffix(QStringLiteral("%"));
        auto* sr = new SettingRow;
        sr->setTitle(tr("键位透明度"));
        sr->setWidget(m_overlayOpacitySlider);
        ol->addWidget(sr);

        connect(m_overlayOpacitySlider, &FluentSlider::valueChanged, this, [this](int val) {
            if (val != m_lastOverlayOpacity) {
                m_lastOverlayOpacity = val;
                qsc::ConfigCenter::instance().setKeyMapOverlayOpacity(val);
                emit overlayOpacityChanged(val);
            }
        });
    }

    // 脚本提示透明度
    {
        m_tipOpacitySlider = new FluentSlider;
        m_tipOpacitySlider->setRange(0, 100);
        m_tipOpacitySlider->setSuffix(QStringLiteral("%"));
        auto* sr = new SettingRow;
        sr->setTitle(tr("提示透明度"));
        sr->setDescription(tr("脚本操作提示弹窗的透明度"));
        sr->setWidget(m_tipOpacitySlider);
        ol->addWidget(sr);

        connect(m_tipOpacitySlider, &FluentSlider::valueChanged, this, [this](int val) {
            if (val != m_lastTipOpacity) {
                m_lastTipOpacity = val;
                qsc::ConfigCenter::instance().setScriptTipOpacity(val);
                emit tipOpacityChanged(val);
            }
        });
    }

    // 画面锐化
    {
        m_sharpenSlider = new FluentSlider;
        m_sharpenSlider->setRange(0, 100);
        m_sharpenSlider->setSuffix(QStringLiteral("%"));
        auto* sr = new SettingRow;
        sr->setTitle(tr("画面锐化"));
        sr->setDescription(tr("增强画面边缘清晰度 (CAS 算法)"));
        sr->setWidget(m_sharpenSlider);
        ol->addWidget(sr);

        connect(m_sharpenSlider, &FluentSlider::valueChanged, this, [this](int val) {
            if (val != m_lastSharpenStrength) {
                m_lastSharpenStrength = val;
                qsc::ConfigCenter::instance().setSharpenStrength(val);
                emit sharpenStrengthChanged(val);
            }
        });
    }

    main->addWidget(optCard);

    // ═══════════ 设备控制 ═══════════
    main->addWidget(makeSeparator());
    auto* devTitle = makeSectionTitle(tr("设备控制"), tm);
    main->addWidget(devTitle);

    auto* devCard = new FluentCard;
    auto* dl = new QVBoxLayout(devCard);
    dl->setContentsMargins(20, 12, 20, 12);
    dl->setSpacing(4);

    {
        m_screenOffToggle = new FluentToggle;
        auto* sr = new SettingRow;
        sr->setTitle(tr("息屏模式"));
        sr->setDescription(tr("关闭设备屏幕，保持控制流，画面黑屏"));
        sr->setWidget(m_screenOffToggle);
        dl->addWidget(sr);

        connect(m_screenOffToggle, &FluentToggle::toggled, this, [this](bool checked) {
            if (checked != m_lastScreenOff) {
                m_lastScreenOff = checked;
                qsc::ConfigCenter::instance().setScreenOff(checked);
                emit screenOffChanged(checked);
            }
        });
    }

    {
        m_streamingToggle = new FluentToggle;
        m_streamingToggle->setChecked(true);
        auto* sr = new SettingRow;
        sr->setTitle(tr("视频传输"));
        sr->setDescription(tr("关闭后暂停视频流画面，仅保留控制，节省带宽"));
        sr->setWidget(m_streamingToggle);
        dl->addWidget(sr);

        connect(m_streamingToggle, &FluentToggle::toggled, this, [this](bool checked) {
            if (checked != m_lastStreaming) {
                m_lastStreaming = checked;
                qsc::ConfigCenter::instance().setVideoStreaming(checked);
                emit videoStreamingChanged(checked);
            }
        });
    }

    main->addWidget(devCard);

    main->addStretch();

    scrollArea->setWidget(content);

    // card 内部放 scrollArea + 关闭按钮行
    auto* cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);
    cardLayout->addWidget(scrollArea, 1);

    // 底部关闭按钮
    auto* btnLayout = new QHBoxLayout;
    btnLayout->setContentsMargins(20, 8, 20, 16);
    btnLayout->addStretch();
    auto* closeBtn = new QPushButton(tr("关闭"));
    closeBtn->setObjectName(QStringLiteral("settingsCloseBtn"));
    closeBtn->setMinimumWidth(80);
    btnLayout->addWidget(closeBtn);
    cardLayout->addLayout(btnLayout);

    connect(closeBtn, &QPushButton::clicked, this, [this]() {
        flushAndSaveVideoParams();   // 关闭前确保所有变更已发送
        accept();
    });

    overlay->addWidget(m_card);
}

// ── 核心: 将当前三参数写 Config 并发信号 ──
void VideoSettingsPopup::flushAndSaveVideoParams()
{
    quint32 mbps = m_bitRateEdit->text().toUInt();
    if (mbps == 0) mbps = 1;
    quint32 bps = mbps * 1000000;

    int fps = 0;
    if (m_fpsBox->currentData().toInt() == -1)
        fps = 0;  // 不限制
    else
        fps = m_fpsBox->currentText().toInt();

    quint16 maxSize = m_maxSizeBox->currentText().toUShort(); // "原始" → 0

    bool changed = false;
    if (bps != m_lastBitRate || fps != m_lastMaxFps || maxSize != (quint16)m_lastMaxSize) {
        changed = true;
    }

    if (!changed) return;

    m_lastBitRate = bps;
    m_lastMaxFps  = fps;
    m_lastMaxSize = maxSize;

    // 写 Config (与主界面共享)
    auto cfg = Config::getInstance().getUserBootConfig();
    cfg.bitRate = bps;
    cfg.maxFps  = fps;
    cfg.maxSizeIndex = m_maxSizeBox->currentIndex();
    Config::getInstance().setUserBootConfig(cfg);

    // 同步 ConfigCenter
    qsc::ConfigCenter::instance().setBitRate(bps);

    // 发信号给 VideoForm → DeviceSession → 服务端
    emit videoParamsChanged(bps, static_cast<quint16>(fps), maxSize);
}

void VideoSettingsPopup::applyStyle()
{
    auto& tm = ThemeManager::instance();

    m_card->setStyleSheet(QStringLiteral(
        "#settingsCard {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 16px;"
        "}")
        .arg(tm.card(), tm.border()));

    if (auto* btn = m_card->findChild<QPushButton*>(QStringLiteral("settingsCloseBtn"))) {
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; color: #fff; border: none; "
            "border-radius: %2px; padding: 8px 20px; font-size: 13px; font-weight: 500; }"
            "QPushButton:hover { background: %3; }")
            .arg(tm.accentPrimary())
            .arg(Radius::Medium)
            .arg(tm.accentHover()));
    }

    QString comboStyle = QStringLiteral(
        "QComboBox { background: %1; border: 1px solid %2; border-radius: 8px;"
        "  padding: 6px 10px; color: %3; font-size: 13px; }"
        "QComboBox:focus { border-color: %4; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: %1; color: %3; "
        "  selection-background-color: %5; }")
        .arg(tm.inputBg(), tm.inputBorder(), tm.textPrimary(),
             tm.inputFocusBorder(), tm.surface());

    QString editStyle = QStringLiteral(
        "QLineEdit { background: %1; border: 1px solid %2; border-radius: 8px;"
        "  padding: 6px 10px; color: %3; font-size: 13px; }"
        "QLineEdit:focus { border-color: %4; }")
        .arg(tm.inputBg(), tm.inputBorder(), tm.textPrimary(), tm.inputFocusBorder());

    if (m_fpsBox)      m_fpsBox->setStyleSheet(comboStyle);
    if (m_maxSizeBox)  m_maxSizeBox->setStyleSheet(comboStyle);
    if (m_bitRateEdit) m_bitRateEdit->setStyleSheet(editStyle);

    for (auto* sa : m_card->findChildren<QScrollArea*>()) {
        sa->setStyleSheet(QStringLiteral(
            "QScrollArea { background: transparent; border: none; }"
            "QScrollArea > QWidget > QWidget { background: transparent; }"));
    }
}

void VideoSettingsPopup::syncFromConfig()
{
    // 从 Config (userdata.ini) 读取，与主界面 SettingsPage 完全一致
    auto cfg = Config::getInstance().getUserBootConfig();

    // 码率
    m_lastBitRate = cfg.bitRate;
    quint32 mbps = qMax(1u, m_lastBitRate / 1000000);
    m_bitRateEdit->blockSignals(true);
    m_bitRateEdit->setText(QString::number(mbps));
    m_bitRateEdit->blockSignals(false);

    // 帧率
    m_lastMaxFps = cfg.maxFps;
    m_fpsBox->blockSignals(true);
    if (cfg.maxFps == 0) {
        for (int i = 0; i < m_fpsBox->count(); ++i) {
            if (m_fpsBox->itemData(i).toInt() == -1) {
                m_fpsBox->setCurrentIndex(i);
                break;
            }
        }
    } else {
        int idx = m_fpsBox->findText(QString::number(cfg.maxFps));
        if (idx >= 0) m_fpsBox->setCurrentIndex(idx);
        else m_fpsBox->setCurrentText(QString::number(cfg.maxFps));
    }
    m_fpsBox->blockSignals(false);

    // 分辨率
    m_lastMaxSize = 0;
    m_maxSizeBox->blockSignals(true);
    int sizeIdx = qBound(0, cfg.maxSizeIndex, m_maxSizeBox->count() - 1);
    m_maxSizeBox->setCurrentIndex(sizeIdx);
    m_lastMaxSize = m_maxSizeBox->currentText().toUShort();
    m_maxSizeBox->blockSignals(false);

    // 显示选项从 ConfigCenter 读取 (这些值不在 Config 的 UserBootConfig 中)
    auto& cc = qsc::ConfigCenter::instance();

    m_lastOverlayVisible = cc.keyMapOverlayVisible();
    m_overlayToggle->blockSignals(true);
    m_overlayToggle->setChecked(m_lastOverlayVisible);
    m_overlayToggle->blockSignals(false);

    m_lastOverlayOpacity = cc.keyMapOverlayOpacity();
    m_overlayOpacitySlider->blockSignals(true);
    m_overlayOpacitySlider->setValue(m_lastOverlayOpacity);
    m_overlayOpacitySlider->blockSignals(false);

    m_lastTipOpacity = cc.scriptTipOpacity();
    m_tipOpacitySlider->blockSignals(true);
    m_tipOpacitySlider->setValue(m_lastTipOpacity);
    m_tipOpacitySlider->blockSignals(false);

    m_lastSharpenStrength = cc.sharpenStrength();
    m_sharpenSlider->blockSignals(true);
    m_sharpenSlider->setValue(m_lastSharpenStrength);
    m_sharpenSlider->blockSignals(false);

    // 息屏 / 视频传输 (从 ConfigCenter 持久化读取)
    m_lastScreenOff = cc.screenOff();
    m_screenOffToggle->blockSignals(true);
    m_screenOffToggle->setChecked(m_lastScreenOff);
    m_screenOffToggle->blockSignals(false);

    m_lastStreaming = cc.videoStreaming();
    m_streamingToggle->blockSignals(true);
    m_streamingToggle->setChecked(m_lastStreaming);
    m_streamingToggle->blockSignals(false);
}

void VideoSettingsPopup::paintEvent(QPaintEvent*)
{
    // 透明背景，卡片自绘
}

void VideoSettingsPopup::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);

    // 精确覆盖父窗口区域，确保卡片居中
    if (parentWidget()) {
        QPoint topLeft = parentWidget()->mapToGlobal(QPoint(0, 0));
        move(topLeft);
        QSize parentSz = parentWidget()->size();
        resize(parentSz);

        // 窄窗口 (竖屏) 自适应边距：防止 card minWidth + margin*2 > 父宽度
        int hMargin = qMin(40, qMax(8, (parentSz.width() - 440) / 2));
        int vMargin = qMin(40, qMax(8, parentSz.height() / 20));
        layout()->setContentsMargins(hMargin, vMargin, hMargin, vMargin);

        // 动态更新卡片最小宽度，保证不超出可用区域
        if (m_card) {
            int available = parentSz.width() - hMargin * 2;
            m_card->setMinimumWidth(qMin(440, available));
        }
    }

    syncFromConfig();

    auto* effect = new QGraphicsOpacityEffect(m_card);
    m_card->setGraphicsEffect(effect);
    auto* fade = new QPropertyAnimation(effect, "opacity", this);
    fade->setDuration(Motion::duration(Motion::Enter));
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(Motion::enterCurve());
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void VideoSettingsPopup::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        flushAndSaveVideoParams();
        accept();
        return;
    }
    QDialog::keyPressEvent(event);
}
