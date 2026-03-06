#include "SettingsPage.h"
#include "FluentCard.h"
#include "FluentButton.h"
#include "FluentToggle.h"
#include "SettingRow.h"
#include "ThemeManager.h"
#include "DesignTokens.h"
#include "config.h"
#include "ConfigCenter.h"
#include "StringUtils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QIntValidator>
#include <QFrame>
#include <QSet>
#include <QRegularExpression>

using namespace Fluent;

// helper: transparent label used in forms (no background)
static QLabel *makeFormLabel(const QString &text = {})
{
    auto *l = new QLabel(text);
    l->setStyleSheet(QStringLiteral("background: transparent; padding: 0;"));
    return l;
}

// helper: section title label
static QLabel *makeSectionTitle(const QString &text, const ThemeManager &tm)
{
    auto *l = new QLabel(text);
    l->setStyleSheet(QStringLiteral(
        "font-size: 15px; font-weight: 700; color: %1; background: transparent; padding: 0;")
        .arg(tm.textPrimary()));
    return l;
}

// horizontal separator
static QFrame *makeSeparator()
{
    auto *f = new QFrame;
    f->setFrameShape(QFrame::HLine);
    f->setFixedHeight(1);
    return f;
}

static QStringList normalizeHistoryItems(const QStringList& rawItems)
{
    QStringList result;
    QSet<QString> seen;
    static const QRegularExpression splitRe(QStringLiteral("[,;\\r\\n]+"));

    for (const QString& raw : rawItems) {
        const auto parts = raw.split(splitRe, Qt::SkipEmptyParts);
        if (parts.isEmpty()) {
            const QString item = raw.trimmed();
            if (!item.isEmpty() && !seen.contains(item)) {
                seen.insert(item);
                result.push_back(item);
            }
            continue;
        }

        for (const QString& part : parts) {
            const QString item = part.trimmed();
            if (item.isEmpty() || seen.contains(item)) continue;
            seen.insert(item);
            result.push_back(item);
        }
    }

    return result;
}

SettingsPage::SettingsPage(QWidget *parent) : QWidget(parent)
{
    setupUI();
    retranslateUi();
    syncFromConfig();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        update();
    });
}

void SettingsPage::setupUI()
{
    auto &tm = ThemeManager::instance();

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget;
    auto *main = new QVBoxLayout(content);
    main->setContentsMargins(32, 28, 32, 20);
    main->setSpacing(20);

    // ═══════════ 视频参数 ═══════════
    m_videoTitle = makeSectionTitle(QString(), tm);
    main->addWidget(m_videoTitle);

    auto *videoCard = new FluentCard;
    auto *vl = new QVBoxLayout(videoCard);
    vl->setContentsMargins(20, 16, 20, 16);
    vl->setSpacing(12);

    // 2×2 网格布局: Row1=[帧率+下拉, 码率+输入+Mbps], Row2=[分辨率+下拉, 编码+下拉]
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(10);

    // Row 0: 帧率 | 码率
    m_fpsLabel = makeFormLabel();
    m_fpsLabel->setFixedWidth(50);
    m_fpsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_fpsBox = new QComboBox;
    m_fpsBox->addItems({"5", "10", "20", "30", "60", "90", "120", "144", "165", "240"});
    m_fpsBox->setMinimumSize(85, 36);
    m_fpsBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_fpsBox->setMinimumContentsLength(6);

    m_bitrateLabel = makeFormLabel();
    m_bitrateLabel->setFixedWidth(50);
    m_bitrateLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_bitRateEdit = new QLineEdit("8");
    m_bitRateEdit->setFixedWidth(70);
    m_bitRateEdit->setMinimumHeight(36);
    m_bitRateEdit->setAlignment(Qt::AlignCenter);
    m_bitRateEdit->setValidator(new QIntValidator(1, 99999, this));
    m_bitRateUnitLabel = makeFormLabel("");
    m_bitRateUnitLabel->setFixedWidth(0);
    m_bitRateUnitLabel->hide();

    auto *fpsRow = new QHBoxLayout;
    fpsRow->setSpacing(6);
    fpsRow->addWidget(m_fpsLabel);
    fpsRow->addWidget(m_fpsBox);

    auto *brRow = new QHBoxLayout;
    brRow->setSpacing(6);
    brRow->addWidget(m_bitrateLabel);
    brRow->addWidget(m_bitRateEdit);
    brRow->addWidget(m_bitRateUnitLabel);

    grid->addLayout(fpsRow, 0, 0);
    grid->addLayout(brRow,  0, 1);

    // Row 1: 分辨率 | 编码
    m_sizeLabel = makeFormLabel();
    m_sizeLabel->setFixedWidth(50);
    m_sizeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_maxSizeBox = new QComboBox;
    m_maxSizeBox->addItems({"320", "480", "640", "720", "1080"});
    m_maxSizeBox->setMinimumSize(90, 36);
    m_maxSizeBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_maxSizeBox->setMinimumContentsLength(6);

    m_codecLabel = makeFormLabel();
    m_codecLabel->setFixedWidth(50);
    m_codecLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_codecBox = new QComboBox;
    m_codecBox->addItems({"H.264", "H.265"});
    m_codecBox->setMinimumSize(90, 36);

    auto *sizeRow = new QHBoxLayout;
    sizeRow->setSpacing(6);
    sizeRow->addWidget(m_sizeLabel);
    sizeRow->addWidget(m_maxSizeBox);

    auto *codecRow = new QHBoxLayout;
    codecRow->setSpacing(6);
    codecRow->addWidget(m_codecLabel);
    codecRow->addWidget(m_codecBox);

    grid->addLayout(sizeRow,  1, 0);
    grid->addLayout(codecRow, 1, 1);

    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);

    vl->addLayout(grid);
    main->addWidget(videoCard);

    // ═══════════ 显示选项 ═══════════
    main->addWidget(makeSeparator());
    m_optionsTitle = makeSectionTitle(QString(), tm);
    main->addWidget(m_optionsTitle);

    auto *optCard = new FluentCard;
    auto *ol = new QVBoxLayout(optCard);
    ol->setContentsMargins(20, 12, 20, 12);
    ol->setSpacing(4);

    auto addToggleRow = [&](FluentToggle *&toggle, const QString &placeholder = {}) -> SettingRow* {
        toggle = new FluentToggle;
        auto *sr = new SettingRow;
        sr->setWidget(toggle);
        ol->addWidget(sr);
        return sr;
    };

    auto *reverseRow  = addToggleRow(m_reverseToggle);   reverseRow->setProperty("_role", "reverse");
    auto *toolbarRow  = addToggleRow(m_toolbarToggle);   toolbarRow->setProperty("_role", "toolbar");
    auto *framelessRow = addToggleRow(m_framelessToggle); framelessRow->setProperty("_role", "frameless");
    auto *fpsToggleRow  = addToggleRow(m_fpsToggle);       fpsToggleRow->setProperty("_role", "fps");

    m_reverseToggle->setChecked(true);
    main->addWidget(optCard);

    // ═══════════ 通道控制 ═══════════
    main->addWidget(makeSeparator());
    m_channelTitle = makeSectionTitle(QString(), tm);
    main->addWidget(m_channelTitle);

    auto *chCard = new FluentCard;
    auto *cl = new QVBoxLayout(chCard);
    cl->setContentsMargins(20, 12, 20, 12);
    cl->setSpacing(4);

    auto addChToggle = [&](FluentToggle *&toggle) -> SettingRow* {
        toggle = new FluentToggle;
        auto *sr = new SettingRow;
        sr->setWidget(toggle);
        cl->addWidget(sr);
        return sr;
    };

    auto *videoChRow   = addChToggle(m_videoChannelToggle);   videoChRow->setProperty("_role", "videoChannel");
    auto *audioChRow   = addChToggle(m_audioChannelToggle);   audioChRow->setProperty("_role", "audioChannel");
    auto *controlChRow = addChToggle(m_controlChannelToggle); controlChRow->setProperty("_role", "controlChannel");
    auto *auxChRow     = addChToggle(m_auxChannelToggle);     auxChRow->setProperty("_role", "auxChannel");

    m_videoChannelToggle->setChecked(true);
    m_audioChannelToggle->setChecked(false);    // 音频默认关闭
    m_controlChannelToggle->setChecked(true);
    m_auxChannelToggle->setChecked(true);

    // 通道开关持久化
    connect(m_videoChannelToggle, &FluentToggle::toggled, this, [](bool on) {
        qsc::ConfigCenter::instance().set("user/videoChannelEnabled", on);
    });
    connect(m_audioChannelToggle, &FluentToggle::toggled, this, [](bool on) {
        qsc::ConfigCenter::instance().set("user/audioChannelEnabled", on);
    });
    connect(m_controlChannelToggle, &FluentToggle::toggled, this, [](bool on) {
        qsc::ConfigCenter::instance().set("user/controlChannelEnabled", on);
    });
    connect(m_auxChannelToggle, &FluentToggle::toggled, this, [](bool on) {
        qsc::ConfigCenter::instance().set("user/auxChannelEnabled", on);
    });
    main->addWidget(chCard);

    // ═══════════ 外观 ═══════════
    main->addWidget(makeSeparator());
    m_appearTitle = makeSectionTitle(QString(), tm);
    main->addWidget(m_appearTitle);

    auto *appearCard = new FluentCard;
    auto *al = new QVBoxLayout(appearCard);
    al->setContentsMargins(20, 16, 20, 16);
    al->setSpacing(12);

    // 主题 + 强调色 合并为同一行
    {
        auto *row = new QHBoxLayout;
        row->setSpacing(12);
        auto *themeLabel = makeFormLabel();
        themeLabel->setProperty("_role", "themeLabel");
        themeLabel->setFixedWidth(48);
        m_themeBox = new QComboBox;
        m_themeBox->setMinimumSize(110, 36);

        auto *accentLabel = makeFormLabel();
        accentLabel->setProperty("_role", "accentLabel");
        accentLabel->setFixedWidth(48);
        m_accentBox = new QComboBox;
        m_accentBox->setMinimumSize(110, 36);

        row->addWidget(themeLabel);
        row->addWidget(m_themeBox);
        row->addSpacing(12);
        row->addWidget(accentLabel);
        row->addWidget(m_accentBox);
        row->addStretch();
        al->addLayout(row);
    }
    main->addWidget(appearCard);

    // 主题切换信号
    connect(m_themeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [](int idx) {
        auto &t = ThemeManager::instance();
        if (idx == 0) t.setTheme(Theme::Dark);
        else if (idx == 1) t.setTheme(Theme::Light);
        else t.setTheme(Theme::System);
    });
    connect(m_accentBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [](int idx) {
        ThemeManager::instance().setAccentColor(static_cast<AccentColor>(idx));
    });

    // ═══════════ 无线连接 ═══════════
    main->addWidget(makeSeparator());
    m_wifiTitle = makeSectionTitle(QString(), tm);
    main->addWidget(m_wifiTitle);

    auto *wifiCard = new FluentCard;
    auto *wl = new QVBoxLayout(wifiCard);
    wl->setContentsMargins(20, 16, 20, 16);
    wl->setSpacing(12);

    {
        // 地址行
        auto *row = new QHBoxLayout;
        row->setSpacing(8);
        m_ipLabel = new QLabel;
        m_ipLabel->setFixedWidth(40);
        m_ipLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_ipEdit = new QComboBox;
        m_ipEdit->setEditable(true);
        m_ipEdit->setMinimumHeight(36);
        m_ipEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_ipEdit->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_ipEdit->setMinimumContentsLength(14);
        if (m_ipEdit->lineEdit()) {
            m_ipEdit->lineEdit()->setPlaceholderText("192.168.1.100");
            m_ipEdit->lineEdit()->setAlignment(Qt::AlignCenter);
        }

        auto *colon = new QLabel(":");
        colon->setFixedWidth(8);
        colon->setAlignment(Qt::AlignCenter);

        m_portEdit = new QComboBox;
        m_portEdit->setEditable(true);
        m_portEdit->setFixedSize(96, 36);
        m_portEdit->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_portEdit->setMinimumContentsLength(5);
        if (m_portEdit->lineEdit()) {
            m_portEdit->lineEdit()->setPlaceholderText("5555");
            m_portEdit->lineEdit()->setAlignment(Qt::AlignCenter);
        }

        m_connectBtn = new FluentButton(QString(), FluentButton::Primary);
        m_connectBtn->setMinimumSize(70, 36);

        m_disconnectBtn = new FluentButton(QString(), FluentButton::Secondary);
        m_disconnectBtn->setMinimumSize(70, 36);

        row->addWidget(m_ipLabel);
        row->addWidget(m_ipEdit, 1);
        row->addWidget(colon);
        row->addWidget(m_portEdit);
        row->addSpacing(8);
        row->addWidget(m_connectBtn);
        row->addWidget(m_disconnectBtn);
        wl->addLayout(row);
    }
    {
        // 工具按钮行
        auto *row = new QHBoxLayout;
        row->setSpacing(12);
        m_getIpBtn = new QPushButton;
        m_getIpBtn->setMinimumSize(100, 36);
        m_adbdBtn = new QPushButton;
        m_adbdBtn->setMinimumSize(100, 36);
        row->addStretch();
        row->addWidget(m_getIpBtn);
        row->addWidget(m_adbdBtn);
        row->addStretch();
        wl->addLayout(row);
    }
    main->addWidget(wifiCard);

    // ---- Section 5: 其他 ----
    m_otherTitle = makeSectionTitle(QString(), tm);
    main->addWidget(m_otherTitle);

    auto *otherCard = new FluentCard;
    auto *otherL = new QVBoxLayout(otherCard);
    {
        auto *row = new QHBoxLayout;
        m_onboardingBtn = new FluentButton(QString(), FluentButton::Secondary);
        m_onboardingBtn->setMinimumSize(140, 36);
        row->addWidget(m_onboardingBtn);
        row->addStretch();
        otherL->addLayout(row);
    }
    main->addWidget(otherCard);

    main->addStretch();

    scrollArea->setWidget(content);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);

    // ---- 信号 ----
    connect(m_connectBtn, &QPushButton::clicked, this, [this]() {
        QString ip = m_ipEdit->currentText().trimmed();
        QString port = m_portEdit->currentText().trimmed();
        if (port.isEmpty()) port = "5555";
        emit wirelessConnect(ip + ":" + port);
    });
    connect(m_disconnectBtn, &QPushButton::clicked, this, &SettingsPage::wirelessDisconnect);
    connect(m_getIpBtn, &QPushButton::clicked, this, &SettingsPage::requestDeviceIP);
    connect(m_adbdBtn, &QPushButton::clicked, this, &SettingsPage::startAdbd);
    connect(m_onboardingBtn, &QPushButton::clicked, this, &SettingsPage::restartOnboarding);

    // 加载 ip/port 历史
    m_ipEdit->clear();
    m_ipEdit->addItems(normalizeHistoryItems(strutil::toQList(Config::getInstance().getIpHistory())));
    m_portEdit->clear();
    m_portEdit->addItems(normalizeHistoryItems(strutil::toQList(Config::getInstance().getPortHistory())));
}

void SettingsPage::retranslateUi()
{
    m_videoTitle->setText(tr("视频参数"));
    m_bitrateLabel->setText(tr("码率"));
    m_fpsLabel->setText(tr("帧率"));
    m_sizeLabel->setText(tr("分辨率"));
    m_codecLabel->setText(tr("编码"));

    // 帧率下拉最后追加"不限制"项（避免重复）
    const QString unlimitedText = tr("不限制");
    int lastIdx = m_fpsBox->count() - 1;
    bool hasUnlimited = (lastIdx >= 0 && m_fpsBox->itemData(lastIdx).toInt() == -1);
    if (!hasUnlimited) {
        m_fpsBox->addItem(unlimitedText);
        m_fpsBox->setItemData(m_fpsBox->count() - 1, -1);
    } else {
        m_fpsBox->setItemText(lastIdx, unlimitedText);
    }

    int lastSizeIdx = m_maxSizeBox->count() - 1;
    if (lastSizeIdx >= 0 && m_maxSizeBox->itemText(lastSizeIdx).toUShort() == 0)
        m_maxSizeBox->setItemText(lastSizeIdx, tr("原始"));
    else
        m_maxSizeBox->addItem(tr("原始"));

    m_optionsTitle->setText(tr("显示选项"));
    // Toggle rows
    for (auto *sr : findChildren<SettingRow *>()) {
        QString role = sr->property("_role").toString();
        if (role == "reverse")   sr->setTitle(tr("反向连接"));
        if (role == "toolbar")   sr->setTitle(tr("工具栏"));
        if (role == "frameless") sr->setTitle(tr("无边框"));
        if (role == "fps")       sr->setTitle(tr("显示FPS"));
        if (role == "videoChannel")   { sr->setTitle(tr("视频通道")); sr->setDescription(tr("启用视频流传输通道")); }
        if (role == "audioChannel")   { sr->setTitle(tr("音频通道")); sr->setDescription(tr("启用音频流 (需重新连接生效)")); }
        if (role == "controlChannel") { sr->setTitle(tr("控制通道")); sr->setDescription(tr("启用触控命令通道")); }
        if (role == "auxChannel")     { sr->setTitle(tr("辅助通道")); sr->setDescription(tr("启用辅助数据通道 (实时参数/剪切板)")); }
    }

    m_channelTitle->setText(tr("通道控制"));

    m_appearTitle->setText(tr("外观"));
    // 主题下拉
    m_themeBox->clear();
    m_themeBox->addItems({tr("深色"), tr("浅色"), tr("跟随系统")});
    m_themeBox->setCurrentIndex(0); // default dark
    // 强调色下拉
    m_accentBox->clear();
    m_accentBox->addItems({tr("靛蓝"), tr("蓝色"), tr("紫罗兰"), tr("玫瑰"), tr("翡翠"), tr("琥珀")});

    for (auto *l : findChildren<QLabel *>()) {
        QString role = l->property("_role").toString();
        if (role == "themeLabel")  l->setText(tr("主题"));
        if (role == "accentLabel") l->setText(tr("强调色"));
    }

    m_wifiTitle->setText(tr("无线连接"));
    m_ipLabel->setText(tr("地址"));
    m_connectBtn->setText(tr("连接"));
    m_disconnectBtn->setText(tr("断开"));
    m_getIpBtn->setText(tr("获取设备IP"));
    m_adbdBtn->setText(tr("开启ADBD"));

    m_otherTitle->setText(tr("其他"));
    m_onboardingBtn->setText(tr("重新引导"));
}

void SettingsPage::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}

void SettingsPage::saveToConfig()
{
    auto cfg = Config::getInstance().getUserBootConfig();
    cfg.videoCodecIndex    = getVideoCodecIndex();
    cfg.maxFps             = getMaxFps();
    cfg.bitRate            = getBitRate();
    cfg.reverseConnect     = isReverseConnect();
    cfg.showToolbar        = showToolbar();
    cfg.framelessWindow    = isFrameless();
    cfg.showFPS            = showFPS();
    Config::getInstance().setUserBootConfig(cfg);
}

void SettingsPage::syncFromConfig()
{
    auto cfg = Config::getInstance().getUserBootConfig();
    // 码率：统一用 Mbps
    m_bitRateEdit->setText(QString::number(qMax(1u, cfg.bitRate / 1000000)));
    // 帧率：查找匹配项
    if (cfg.maxFps == 0) {
        // 0 表示不限制
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
    m_maxSizeBox->setCurrentIndex(cfg.maxSizeIndex);
    m_codecBox->setCurrentIndex(qBound(0, cfg.videoCodecIndex, m_codecBox->count() - 1));
    m_reverseToggle->setChecked(cfg.reverseConnect);
    m_toolbarToggle->setChecked(cfg.showToolbar);
    m_framelessToggle->setChecked(cfg.framelessWindow);
    m_fpsToggle->setChecked(cfg.showFPS);

    // 通道开关
    auto& cc = qsc::ConfigCenter::instance();
    m_videoChannelToggle->setChecked(cc.get<bool>("user/videoChannelEnabled", true));
    m_audioChannelToggle->setChecked(cc.get<bool>("user/audioChannelEnabled", false));
    m_controlChannelToggle->setChecked(cc.get<bool>("user/controlChannelEnabled", true));
    m_auxChannelToggle->setChecked(cc.get<bool>("user/auxChannelEnabled", true));
}

// ═══════════ Getters ═══════════

quint32 SettingsPage::getBitRate() const
{
    quint32 v = m_bitRateEdit->text().toUInt();
    return v * 1000000; // 统一 Mbps
}

quint16 SettingsPage::getMaxSize() const { return m_maxSizeBox->currentText().toUShort(); }

int SettingsPage::getMaxFps() const
{
    // "不限制" 项的 data == -1
    if (m_fpsBox->currentData().toInt() == -1) return 0;
    return m_fpsBox->currentText().toInt();
}
QString SettingsPage::getVideoCodecName() const {
    int idx = m_codecBox ? m_codecBox->currentIndex() : 0;
    switch (idx) {
        case 1:  return "h265";
        default: return "h264";
    }
}
int SettingsPage::getVideoCodecIndex() const {
    return m_codecBox ? m_codecBox->currentIndex() : 0;
}
bool    SettingsPage::isReverseConnect() const { return m_reverseToggle->isChecked(); }
bool    SettingsPage::showToolbar() const { return m_toolbarToggle->isChecked(); }
bool    SettingsPage::isFrameless() const { return m_framelessToggle->isChecked(); }
bool    SettingsPage::showFPS() const { return m_fpsToggle->isChecked(); }
QString SettingsPage::getDeviceIP() const { return m_ipEdit->currentText().trimmed(); }
QString SettingsPage::getDevicePort() const { return m_portEdit->currentText().trimmed(); }

void SettingsPage::setDeviceIP(const QString &ip)
{
    m_ipEdit->setCurrentText(ip);
}
