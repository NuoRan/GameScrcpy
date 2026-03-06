/**
 * @file DeviceDetailPage.cpp
 * @brief 设备详情页实现
 */
#include "DeviceDetailPage.h"
#include "FluentCard.h"
#include "FluentButton.h"
#include "FluentToggle.h"
#include "FluentSlider.h"
#include "FluentBadge.h"
#include "SettingRow.h"
#include "ThemeManager.h"
#include "DesignTokens.h"
#include "ConfigCenter.h"
#include "config.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QIntValidator>
#include <filesystem>
#include <fstream>
#include <windows.h>
#include <shellapi.h>
#include <QEvent>
#include <QMouseEvent>
#include "FluentDialog.h"

using namespace Fluent;

// helper
static QFrame* makeSep() {
    auto* f = new QFrame; f->setFrameShape(QFrame::HLine); f->setFixedHeight(1); return f;
}
static QLabel* makeTitle(const QString& text, const ThemeManager& tm) {
    auto* l = new QLabel(text);
    l->setStyleSheet(QStringLiteral(
        "font-size: 15px; font-weight: 700; color: %1; background: transparent;").arg(tm.textPrimary()));
    return l;
}

DeviceDetailPage::DeviceDetailPage(QWidget* parent) : QWidget(parent)
{
    setupUI();
    retranslateUi();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() { update(); });
}

void DeviceDetailPage::setupUI()
{
    auto& tm = ThemeManager::instance();

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget;
    auto* main = new QVBoxLayout(content);
    main->setContentsMargins(32, 20, 32, 20);
    main->setSpacing(16);

    // ═══════════ 顶部: 返回 + 设备信息 ═══════════
    {
        auto* topRow = new QHBoxLayout;
        m_backLabel = new QLabel;
        m_backLabel->setCursor(Qt::PointingHandCursor);
        m_backLabel->setStyleSheet(QStringLiteral(
            "font-size: 13px; color: %1; background: transparent;").arg(tm.accentPrimary()));
        m_backLabel->installEventFilter(this);
        topRow->addWidget(m_backLabel);
        topRow->addStretch();
        main->addLayout(topRow);
    }

    auto* infoCard = new FluentCard;
    auto* infoLayout = new QHBoxLayout(infoCard);
    infoLayout->setContentsMargins(20, 16, 20, 16);
    infoLayout->setSpacing(16);

    // 设备图标
    auto* devIcon = new QLabel(QStringLiteral("\xF0\x9F\x93\xB1"));
    devIcon->setStyleSheet("font-size: 32px; background: transparent;");
    infoLayout->addWidget(devIcon);

    auto* infoText = new QVBoxLayout;
    infoText->setSpacing(4);
    m_deviceNameLabel = new QLabel;
    m_deviceNameLabel->setStyleSheet(QStringLiteral(
        "font-size: 20px; font-weight: 600; color: %1; background: transparent;").arg(tm.textPrimary()));
    m_serialLabel = new QLabel;
    m_serialLabel->setStyleSheet(QStringLiteral(
        "font-size: 12px; color: %1; background: transparent;").arg(tm.textTertiary()));
    m_connectionLabel = new QLabel;
    m_connectionLabel->setStyleSheet(QStringLiteral(
        "font-size: 12px; color: %1; background: transparent;").arg(tm.textSecondary()));
    infoText->addWidget(m_deviceNameLabel);
    infoText->addWidget(m_serialLabel);
    infoText->addWidget(m_connectionLabel);
    infoLayout->addLayout(infoText, 1);

    m_statusBadge = new FluentBadge;
    m_statusBadge->setStatus(FluentBadge::Online);
    infoLayout->addWidget(m_statusBadge);
    main->addWidget(infoCard);

    // ═══════════ 投屏控制 ═══════════
    main->addWidget(makeSep());
    m_streamTitle = makeTitle(QString(), tm);
    main->addWidget(m_streamTitle);

    auto* streamCard = new FluentCard;
    auto* sl = new QVBoxLayout(streamCard);
    sl->setContentsMargins(20, 16, 20, 16);
    sl->setSpacing(12);

    // 码率行
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        m_bitrateLabel = new QLabel;
        m_bitrateLabel->setFixedWidth(50);
        m_bitRateEdit = new QLineEdit("8");
        m_bitRateEdit->setFixedWidth(70);
        m_bitRateEdit->setMinimumHeight(36);
        m_bitRateEdit->setAlignment(Qt::AlignCenter);
        m_bitRateEdit->setValidator(new QIntValidator(1, 99999, this));
        m_bitRateUnit = new QComboBox;
        m_bitRateUnit->addItems({"Mbps", "Kbps"});
        m_bitRateUnit->setMinimumSize(85, 36);
        row->addWidget(m_bitrateLabel);
        row->addWidget(m_bitRateEdit);
        row->addWidget(m_bitRateUnit);
        row->addStretch();
        sl->addLayout(row);
    }
    // 帧率 + 分辨率 + 编码
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(12);
        m_fpsLabel = new QLabel;
        m_fpsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_fpsSpinBox = new QSpinBox;
        m_fpsSpinBox->setRange(0, 999);
        m_fpsSpinBox->setValue(60);
        m_fpsSpinBox->setMinimumSize(85, 36);

        m_sizeLabel = new QLabel;
        m_sizeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_maxSizeBox = new QComboBox;
        m_maxSizeBox->addItems({"320", "480", "640", "720", "1080"});
        m_maxSizeBox->setMinimumSize(90, 36);

        m_codecLabel = new QLabel;
        m_codecLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_codecBox = new QComboBox;
        m_codecBox->addItems({"H.264"});
        m_codecBox->setMinimumSize(90, 36);

        row->addWidget(m_fpsLabel);
        row->addWidget(m_fpsSpinBox);
        row->addSpacing(8);
        row->addWidget(m_sizeLabel);
        row->addWidget(m_maxSizeBox);
        row->addSpacing(8);
        row->addWidget(m_codecLabel);
        row->addWidget(m_codecBox);
        row->addStretch();
        sl->addLayout(row);
    }
    main->addWidget(streamCard);

    // ═══════════ 显示选项 ═══════════
    m_optionsTitle = makeTitle(QString(), tm);
    main->addWidget(m_optionsTitle);

    auto* optCard = new FluentCard;
    auto* ol = new QVBoxLayout(optCard);
    ol->setContentsMargins(20, 12, 20, 12);
    ol->setSpacing(4);

    auto addToggle = [&](FluentToggle*& toggle, const QString& role) -> SettingRow* {
        toggle = new FluentToggle;
        auto* sr = new SettingRow;
        sr->setWidget(toggle);
        sr->setProperty("_role", role);
        ol->addWidget(sr);
        return sr;
    };

    addToggle(m_reverseToggle,  "reverse");
    addToggle(m_toolbarToggle,  "toolbar");
    addToggle(m_framelessToggle,"frameless");
    addToggle(m_fpsToggle,      "fps");
    m_reverseToggle->setChecked(true);
    m_toolbarToggle->setChecked(true);
    main->addWidget(optCard);

    // ═══════════ 操作按钮 ═══════════
    {
        auto* btnRow = new QHBoxLayout;
        btnRow->setSpacing(16);
        m_startBtn = new FluentButton(QString(), FluentButton::Primary);
        m_startBtn->setMinimumSize(140, 48);
        m_stopBtn = new FluentButton(QString(), FluentButton::Danger);
        m_stopBtn->setMinimumSize(100, 48);
        btnRow->addWidget(m_startBtn);
        btnRow->addWidget(m_stopBtn);
        btnRow->addStretch();
        main->addLayout(btnRow);
    }

    connect(m_startBtn, &QPushButton::clicked, this, [this]() { emit startStreaming(m_serial); });
    connect(m_stopBtn,  &QPushButton::clicked, this, [this]() { emit stopStreaming(m_serial); });

    // ═══════════ 键位配置 ═══════════
    main->addWidget(makeSep());
    m_keymapTitle = makeTitle(QString(), tm);
    main->addWidget(m_keymapTitle);

    auto* kmCard = new FluentCard;
    auto* kl = new QVBoxLayout(kmCard);
    kl->setContentsMargins(20, 16, 20, 16);
    kl->setSpacing(10);

    {
        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        auto* cfgLabel = new QLabel(tr("配置"));
        cfgLabel->setFixedWidth(40);
        m_keymapCombo = new QComboBox;
        m_keymapCombo->setMinimumSize(160, 36);
        m_keymapNewBtn = new QPushButton("+");
        m_keymapNewBtn->setFixedSize(36, 36);
        m_keymapNewBtn->setToolTip(tr("新建配置"));
        m_keymapRefreshBtn = new QPushButton(QStringLiteral("\xE2\x86\xBB")); // ↻
        m_keymapRefreshBtn->setFixedSize(36, 36);
        m_keymapFolderBtn = new QPushButton(QStringLiteral("\xF0\x9F\x93\x81")); // 📁
        m_keymapFolderBtn->setFixedSize(36, 36);
        m_keymapSaveBtn = new FluentButton(QString(), FluentButton::Primary);
        m_keymapSaveBtn->setMinimumSize(70, 36);

        row->addWidget(cfgLabel);
        row->addWidget(m_keymapCombo, 1);
        row->addWidget(m_keymapNewBtn);
        row->addWidget(m_keymapRefreshBtn);
        row->addWidget(m_keymapFolderBtn);
        row->addWidget(m_keymapSaveBtn);
        kl->addLayout(row);
    }

    connect(m_keymapCombo, &QComboBox::currentTextChanged, this, [this](const QString& t) {
        if (!t.isEmpty()) emit keyMapChanged(t);
    });
    connect(m_keymapRefreshBtn, &QPushButton::clicked, this, &DeviceDetailPage::refreshKeyMapList);
    connect(m_keymapFolderBtn, &QPushButton::clicked, this, []() {
        namespace fs = std::filesystem;
        fs::path dirPath("keymap");
        if (!fs::exists(dirPath)) fs::create_directories(dirPath);
        ShellExecuteW(nullptr, L"open", fs::absolute(dirPath).wstring().c_str(), nullptr, nullptr, SW_SHOW);
    });
    connect(m_keymapNewBtn, &QPushButton::clicked, this, [this]() {
        QString name = FluentDialog::input(this, tr("新建配置"), tr("文件名:"), "new_config");
        if (!name.isEmpty()) {
            if (!name.endsWith(".json")) name += ".json";
            namespace fs = std::filesystem;
            fs::path dirPath("keymap");
            if (!fs::exists(dirPath)) fs::create_directories(dirPath);
            std::ofstream f((dirPath / name.toStdString()).string());
            if (f) { f << "{}"; f.close(); }
            refreshKeyMapList();
            m_keymapCombo->setCurrentText(name);
            emit keyMapNewRequested(name);
        }
    });
    connect(m_keymapSaveBtn, &QPushButton::clicked, this, [this]() { emit keyMapSaveRequested(); });

    main->addWidget(kmCard);

    // ═══════════ 拟人参数 ═══════════
    main->addWidget(makeSep());
    m_humanTitle = makeTitle(QString(), tm);
    main->addWidget(m_humanTitle);

    auto* humanCard = new FluentCard;
    auto* hl = new QVBoxLayout(humanCard);
    hl->setContentsMargins(20, 16, 20, 16);
    hl->setSpacing(10);

    auto addSlider = [&](FluentSlider*& slider, int defaultVal) {
        slider = new FluentSlider;
        slider->setRange(0, 100);
        slider->setValue(defaultVal);
        hl->addWidget(slider);
    };

    auto& cc = qsc::ConfigCenter::instance();
    addSlider(m_randomOffsetSlider, cc.randomOffset());
    addSlider(m_steerSmoothSlider,  cc.steerWheelSmooth());
    addSlider(m_steerCurveSlider,   cc.steerWheelCurve());
    addSlider(m_slideCurveSlider,   cc.slideCurve());

    // Save slider values on change
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

    main->addWidget(humanCard);
    main->addStretch();

    scrollArea->setWidget(content);
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);

    // Load params from config
    auto cfg = Config::getInstance().getUserBootConfig();
    if (cfg.bitRate % 1000000 == 0) {
        m_bitRateEdit->setText(QString::number(cfg.bitRate / 1000000));
        m_bitRateUnit->setCurrentText("Mbps");
    } else {
        m_bitRateEdit->setText(QString::number(cfg.bitRate / 1000));
        m_bitRateUnit->setCurrentText("Kbps");
    }
    m_fpsSpinBox->setValue(cfg.maxFps);
    m_maxSizeBox->setCurrentIndex(cfg.maxSizeIndex);
    m_reverseToggle->setChecked(cfg.reverseConnect);
    m_toolbarToggle->setChecked(cfg.showToolbar);
    m_framelessToggle->setChecked(cfg.framelessWindow);
    m_fpsToggle->setChecked(cfg.showFPS);

    // Handle back click
    connect(m_backLabel, &QLabel::linkActivated, this, [this]() { emit goBack(); });
}

bool DeviceDetailPage::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_backLabel && event->type() == QEvent::MouseButtonRelease) {
        emit goBack();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void DeviceDetailPage::setSerial(const QString& serial) { m_serial = serial; m_serialLabel->setText(serial); }
void DeviceDetailPage::setDeviceName(const QString& name) { m_deviceNameLabel->setText(name); }
void DeviceDetailPage::setConnectionType(const QString& type) { m_connectionLabel->setText(type); }
void DeviceDetailPage::setStreamingState(bool streaming)
{
    m_statusBadge->setStatus(streaming ? FluentBadge::Streaming : FluentBadge::Online);
    m_startBtn->setEnabled(!streaming);
    m_stopBtn->setEnabled(streaming);
}

quint32 DeviceDetailPage::getBitRate() const {
    quint32 v = m_bitRateEdit->text().toUInt();
    return v * (m_bitRateUnit->currentText() == "Mbps" ? 1000000 : 1000);
}
quint16 DeviceDetailPage::getMaxSize() const { return m_maxSizeBox->currentText().toUShort(); }
int DeviceDetailPage::getMaxFps() const { return m_fpsSpinBox->value(); }
QString DeviceDetailPage::getVideoCodecName() const { return "h264"; }
bool DeviceDetailPage::isReverseConnect() const { return m_reverseToggle->isChecked(); }
bool DeviceDetailPage::showToolbar() const { return m_toolbarToggle->isChecked(); }
bool DeviceDetailPage::isFrameless() const { return m_framelessToggle->isChecked(); }
bool DeviceDetailPage::showFPS() const { return m_fpsToggle->isChecked(); }

void DeviceDetailPage::refreshKeyMapList()
{
    if (!m_keymapCombo) return;
    QString current = m_keymapCombo->currentText();
    m_keymapCombo->blockSignals(true);
    m_keymapCombo->clear();
    namespace fs = std::filesystem;
    fs::path dirPath("keymap");
    if (!fs::exists(dirPath)) fs::create_directories(dirPath);
    QStringList files;
    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
            files << QString::fromStdString(entry.path().filename().string());
    }
    files.sort();
    if (files.isEmpty()) m_keymapCombo->addItem("default.json");
    else m_keymapCombo->addItems(files);
    int idx = m_keymapCombo->findText(current);
    if (idx >= 0) m_keymapCombo->setCurrentIndex(idx);
    m_keymapCombo->blockSignals(false);
}

QString DeviceDetailPage::currentKeyMapFile() const {
    return m_keymapCombo ? m_keymapCombo->currentText() : "default.json";
}

void DeviceDetailPage::setCurrentKeyMap(const QString& filename)
{
    if (!m_keymapCombo) return;
    refreshKeyMapList();
    int idx = m_keymapCombo->findText(filename);
    if (idx >= 0) {
        m_keymapCombo->blockSignals(true);
        m_keymapCombo->setCurrentIndex(idx);
        m_keymapCombo->blockSignals(false);
    }
}

void DeviceDetailPage::retranslateUi()
{
    m_backLabel->setText(QStringLiteral("\xE2\x86\x90 ") + tr("返回"));
    m_streamTitle->setText(tr("投屏控制"));
    m_bitrateLabel->setText(tr("码率"));
    m_fpsLabel->setText(tr("帧率"));
    m_sizeLabel->setText(tr("分辨率"));
    m_codecLabel->setText(tr("编码"));

    m_optionsTitle->setText(tr("显示选项"));
    for (auto* sr : findChildren<SettingRow*>()) {
        QString role = sr->property("_role").toString();
        if (role == "reverse")   sr->setTitle(tr("反向连接"));
        if (role == "toolbar")   sr->setTitle(tr("工具栏"));
        if (role == "frameless") sr->setTitle(tr("无边框"));
        if (role == "fps")       sr->setTitle(tr("显示FPS"));
    }

    m_startBtn->setText(QStringLiteral("\xE2\x96\xB6 ") + tr("开始投屏"));
    m_stopBtn->setText(QStringLiteral("\xE2\x96\xA0 ") + tr("停止"));

    m_keymapTitle->setText(tr("键位配置"));
    m_keymapSaveBtn->setText(tr("保存"));

    m_humanTitle->setText(tr("拟人参数"));
    m_randomOffsetSlider->setLabel(tr("随机偏移"));
    m_steerSmoothSlider->setLabel(tr("轮盘平滑"));
    m_steerCurveSlider->setLabel(tr("轮盘曲线"));
    m_slideCurveSlider->setLabel(tr("滑动曲线"));
}

void DeviceDetailPage::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) retranslateUi();
    QWidget::changeEvent(event);
}
