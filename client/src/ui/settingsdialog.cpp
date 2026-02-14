#include "settingsdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QFrame>
#include <QIntValidator>
#include <QSpacerItem>
#include <QSpinBox>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    applyStyle();
    retranslateUi();
}

void SettingsDialog::setupUI()
{
    setMinimumWidth(480);
    setModal(true);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(20);
    mainLayout->setContentsMargins(28, 24, 28, 24);

    // ==================== 视频参数区 ====================
    m_videoTitle = new QLabel();
    m_videoTitle->setObjectName("sectionTitle");
    m_videoTitle->setAlignment(Qt::AlignCenter);

    QHBoxLayout *videoRow = new QHBoxLayout();
    videoRow->setSpacing(12);

    m_bitrateLabel = new QLabel();
    m_bitrateLabel->setFixedWidth(50);
    m_bitrateLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_bitRateEdit = new QLineEdit("8");
    m_bitRateEdit->setMinimumHeight(38);
    m_bitRateEdit->setFixedWidth(70);
    m_bitRateEdit->setAlignment(Qt::AlignCenter);
    m_bitRateEdit->setValidator(new QIntValidator(1, 99999, this));

    m_bitRateUnit = new QComboBox();
    m_bitRateUnit->addItems({"Mbps", "Kbps"});
    m_bitRateUnit->setMinimumSize(85, 38);

    m_fpsLabel = new QLabel();
    m_fpsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_fpsSpinBox = new QSpinBox();
    m_fpsSpinBox->setRange(0, 999);
    m_fpsSpinBox->setValue(60);
    m_fpsSpinBox->setMinimumSize(85, 38);
    m_fpsSpinBox->setAlignment(Qt::AlignCenter);

    m_sizeLabel = new QLabel();
    m_sizeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_maxSizeBox = new QComboBox();
    m_maxSizeBox->addItems({"320", "640", "720", "1080", "1280", "1920"});
    // "原始" will be added in retranslateUi()
    m_maxSizeBox->setMinimumSize(90, 38);

    m_touchLabel = new QLabel();
    m_touchLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_touchPointsSpinBox = new QSpinBox();
    m_touchPointsSpinBox->setRange(1, 50);
    m_touchPointsSpinBox->setValue(10);
    m_touchPointsSpinBox->setMinimumSize(85, 38);
    m_touchPointsSpinBox->setAlignment(Qt::AlignCenter);

    m_codecLabel = new QLabel();
    m_codecLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_codecBox = new QComboBox();
    m_codecBox->addItems({"H.264"});
    m_codecBox->setMinimumSize(90, 38);

    videoRow->addWidget(m_bitrateLabel);
    videoRow->addWidget(m_bitRateEdit);
    videoRow->addWidget(m_bitRateUnit);
    videoRow->addSpacing(16);
    videoRow->addWidget(m_fpsLabel);
    videoRow->addWidget(m_fpsSpinBox);
    videoRow->addSpacing(16);
    videoRow->addWidget(m_sizeLabel);
    videoRow->addWidget(m_maxSizeBox);
    videoRow->addSpacing(16);
    videoRow->addWidget(m_touchLabel);
    videoRow->addWidget(m_touchPointsSpinBox);
    videoRow->addSpacing(16);
    videoRow->addWidget(m_codecLabel);
    videoRow->addWidget(m_codecBox);
    videoRow->addStretch(1);

    // ==================== 显示选项区 ====================
    m_optionsTitle = new QLabel();
    m_optionsTitle->setObjectName("sectionTitle");
    m_optionsTitle->setAlignment(Qt::AlignCenter);

    QHBoxLayout *optionsRow = new QHBoxLayout();
    optionsRow->setSpacing(24);

    m_reverseCheck = new QCheckBox();
    m_reverseCheck->setChecked(true);
    m_toolbarCheck = new QCheckBox();
    m_framelessCheck = new QCheckBox();
    m_fpsCheck = new QCheckBox();

    optionsRow->addStretch(1);
    optionsRow->addWidget(m_reverseCheck);
    optionsRow->addWidget(m_toolbarCheck);
    optionsRow->addWidget(m_framelessCheck);
    optionsRow->addWidget(m_fpsCheck);
    optionsRow->addStretch(1);

    // ==================== 无线连接区 ====================
    m_wifiTitle = new QLabel();
    m_wifiTitle->setObjectName("sectionTitle");
    m_wifiTitle->setAlignment(Qt::AlignCenter);

    // 地址行
    QHBoxLayout *wifiRow = new QHBoxLayout();
    wifiRow->setSpacing(12);

    m_ipLabel = new QLabel();
    m_ipLabel->setFixedWidth(50);
    m_ipLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_ipEdit = new QComboBox();
    m_ipEdit->setEditable(true);
    m_ipEdit->setMinimumHeight(38);
    m_ipEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (m_ipEdit->lineEdit()) {
        m_ipEdit->lineEdit()->setPlaceholderText("192.168.1.100");
        m_ipEdit->lineEdit()->setAlignment(Qt::AlignCenter);
    }

    QLabel *colonLabel = new QLabel(":");
    colonLabel->setFixedWidth(10);
    colonLabel->setAlignment(Qt::AlignCenter);

    m_portEdit = new QComboBox();
    m_portEdit->setEditable(true);
    m_portEdit->setFixedSize(96, 38);
    if (m_portEdit->lineEdit()) {
        m_portEdit->lineEdit()->setPlaceholderText("5555");
        m_portEdit->lineEdit()->setAlignment(Qt::AlignCenter);
    }

    m_connectBtn = new QPushButton();
    m_connectBtn->setObjectName("primaryBtn");
    m_connectBtn->setMinimumSize(70, 38);

    m_disconnectBtn = new QPushButton();
    m_disconnectBtn->setMinimumSize(70, 38);

    wifiRow->addWidget(m_ipLabel);
    wifiRow->addWidget(m_ipEdit, 1);
    wifiRow->addWidget(colonLabel);
    wifiRow->addWidget(m_portEdit);
    wifiRow->addSpacing(12);
    wifiRow->addWidget(m_connectBtn);
    wifiRow->addWidget(m_disconnectBtn);

    // ==================== 工具按钮行 ====================
    QHBoxLayout *toolRow = new QHBoxLayout();
    toolRow->setSpacing(12);

    m_getIpBtn = new QPushButton();
    m_getIpBtn->setMinimumSize(100, 38);

    m_adbdBtn = new QPushButton();
    m_adbdBtn->setMinimumSize(100, 38);

    toolRow->addStretch(1);
    toolRow->addWidget(m_getIpBtn);
    toolRow->addWidget(m_adbdBtn);
    toolRow->addStretch(1);

    // ==================== 组装主布局 ====================
    mainLayout->addWidget(m_videoTitle);
    mainLayout->addLayout(videoRow);

    mainLayout->addSpacing(4);
    mainLayout->addWidget(m_optionsTitle);
    mainLayout->addLayout(optionsRow);

    mainLayout->addSpacing(4);
    mainLayout->addWidget(m_wifiTitle);
    mainLayout->addLayout(wifiRow);

    mainLayout->addSpacing(8);
    mainLayout->addLayout(toolRow);

    mainLayout->addStretch(1);

    // ==================== 信号连接 ====================
    connect(m_connectBtn, &QPushButton::clicked, this, &SettingsDialog::wirelessConnect);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &SettingsDialog::wirelessDisconnect);
    connect(m_getIpBtn, &QPushButton::clicked, this, &SettingsDialog::requestDeviceIP);
    connect(m_adbdBtn, &QPushButton::clicked, this, &SettingsDialog::startAdbd);

    adjustSize();
}

// ---------------------------------------------------------
// 翻译所有UI文本 / Translate all UI text
// ---------------------------------------------------------
void SettingsDialog::retranslateUi()
{
    setWindowTitle(tr("设置"));

    m_videoTitle->setText(tr("视频参数"));
    m_bitrateLabel->setText(tr("码率"));
    m_fpsLabel->setText(tr("帧率"));
    m_sizeLabel->setText(tr("分辨率"));
    m_touchLabel->setText(tr("触摸点"));
    m_codecLabel->setText(tr("编码"));

    m_fpsSpinBox->setSpecialValueText(tr("不限制"));
    m_fpsSpinBox->setToolTip(tr("0 = 不限制帧率, 1-999 = 限制最大帧率"));
    m_touchPointsSpinBox->setToolTip(tr("脚本宏可同时按下的最大触摸点数（1-10）"));

    // 更新 "原始" 项
    int lastIdx = m_maxSizeBox->count() - 1;
    if (lastIdx >= 0 && m_maxSizeBox->itemText(lastIdx).toUShort() == 0) {
        m_maxSizeBox->setItemText(lastIdx, tr("原始"));
    } else {
        m_maxSizeBox->addItem(tr("原始"));
    }

    m_optionsTitle->setText(tr("显示选项"));
    m_reverseCheck->setText(tr("反向连接"));
    m_toolbarCheck->setText(tr("工具栏"));
    m_framelessCheck->setText(tr("无边框"));
    m_fpsCheck->setText(tr("显示FPS"));

    m_wifiTitle->setText(tr("无线连接"));
    m_ipLabel->setText(tr("地址"));
    m_connectBtn->setText(tr("连接"));
    m_disconnectBtn->setText(tr("断开"));
    m_getIpBtn->setText(tr("获取设备IP"));
    m_adbdBtn->setText(tr("开启ADBD"));
}

void SettingsDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QDialog::changeEvent(event);
}

void SettingsDialog::applyStyle()
{
    setStyleSheet(R"(
        QDialog {
            background-color: #18181b;
        }
        QLabel {
            color: #a1a1aa;
            font-size: 13px;
            background: transparent;
        }
        QLabel#sectionTitle {
            color: #fafafa;
            font-size: 14px;
            font-weight: 600;
            padding: 6px 0;
        }
        QLineEdit {
            background-color: #27272a;
            border: 1px solid #3f3f46;
            border-radius: 8px;
            padding: 0 12px;
            color: #fafafa;
            font-size: 13px;
            selection-background-color: #6366f1;
        }
        QLineEdit:focus {
            border-color: #6366f1;
            background-color: #1f1f23;
        }
        QSpinBox {
            background-color: #27272a;
            border: 1px solid #3f3f46;
            border-radius: 8px;
            padding: 0 12px;
            color: #fafafa;
            font-size: 13px;
        }
        QSpinBox:focus {
            border-color: #6366f1;
            background-color: #1f1f23;
        }
        QSpinBox::up-button, QSpinBox::down-button {
            width: 20px;
            border: none;
            background: transparent;
        }
        QSpinBox::up-arrow {
            image: none;
            width: 0px; height: 0px;
            border-style: solid;
            border-width: 0 5px 6px 5px;
            border-color: transparent transparent #71717a transparent;
        }
        QSpinBox::down-arrow {
            image: none;
            width: 0px; height: 0px;
            border-style: solid;
            border-width: 6px 5px 0 5px;
            border-color: #71717a transparent transparent transparent;
        }
        QSpinBox::up-arrow:hover { border-color: transparent transparent #a1a1aa transparent; }
        QSpinBox::down-arrow:hover { border-color: #a1a1aa transparent transparent transparent; }
        QComboBox {
            background-color: #27272a;
            border: 1px solid #3f3f46;
            border-radius: 8px;
            padding: 0 12px;
            color: #fafafa;
            font-size: 13px;
        }
        QComboBox:focus, QComboBox:on {
            border-color: #6366f1;
        }
        QComboBox::drop-down {
            border: none;
            width: 24px;
            subcontrol-position: center right;
            subcontrol-origin: padding;
            right: 6px;
        }
        QComboBox::down-arrow {
            image: none;
            width: 0px;
            height: 0px;
            border-style: solid;
            border-width: 6px 5px 0 5px;
            border-color: #71717a transparent transparent transparent;
        }
        QComboBox::down-arrow:on, QComboBox::down-arrow:hover {
            border-color: #a1a1aa transparent transparent transparent;
        }
        QComboBox QAbstractItemView {
            background-color: #27272a;
            border: 1px solid #3f3f46;
            border-radius: 8px;
            padding: 6px;
            selection-background-color: #3f3f46;
            outline: none;
        }
        QComboBox QAbstractItemView::item {
            color: #fafafa;
            padding: 8px 12px;
            border-radius: 4px;
            min-height: 24px;
        }
        QComboBox QAbstractItemView::item:hover {
            background-color: #3f3f46;
        }
        QComboBox QAbstractItemView::item:selected {
            background-color: #6366f1;
        }
        QPushButton {
            background-color: #27272a;
            border: 1px solid #3f3f46;
            border-radius: 8px;
            padding: 8px 16px;
            color: #fafafa;
            font-size: 13px;
            font-weight: 500;
        }
        QPushButton:hover {
            background-color: #3f3f46;
            border-color: #52525b;
        }
        QPushButton#primaryBtn {
            background-color: #6366f1;
            border: none;
            color: white;
            font-weight: 600;
        }
        QPushButton#primaryBtn:hover {
            background-color: #818cf8;
        }
        QCheckBox {
            color: #a1a1aa;
            font-size: 13px;
            spacing: 8px;
            background: transparent;
        }
        QCheckBox:hover {
            color: #fafafa;
        }
        QCheckBox::indicator {
            width: 18px;
            height: 18px;
            border-radius: 4px;
            border: 2px solid #52525b;
            background-color: transparent;
        }
        QCheckBox::indicator:unchecked {
            background-color: transparent;
        }
        QCheckBox::indicator:hover {
            border-color: #71717a;
        }
        QCheckBox::indicator:checked {
            background-color: #6366f1;
            border-color: #6366f1;
        }
    )");
}

// ==================== Getter 方法 ====================
QString SettingsDialog::getSerial() const { return QString(); }

quint32 SettingsDialog::getBitRate() const {
    quint32 value = m_bitRateEdit->text().toUInt();
    return value * (m_bitRateUnit->currentText() == "Mbps" ? 1000000 : 1000);
}

quint16 SettingsDialog::getMaxSize() const { return m_maxSizeBox->currentText().toUShort(); }
int SettingsDialog::getMaxSizeIndex() const { return m_maxSizeBox->currentIndex(); }
int SettingsDialog::getMaxFps() const { return m_fpsSpinBox->value(); }
int SettingsDialog::getMaxTouchPoints() const { return m_touchPointsSpinBox->value(); }
int SettingsDialog::getVideoCodecIndex() const { return m_codecBox->currentIndex(); }
QString SettingsDialog::getVideoCodecName() const {
    return "h264";
}
bool SettingsDialog::isReverseConnect() const { return m_reverseCheck->isChecked(); }
bool SettingsDialog::showToolbar() const { return m_toolbarCheck->isChecked(); }
bool SettingsDialog::isFrameless() const { return m_framelessCheck->isChecked(); }
bool SettingsDialog::showFPS() const { return m_fpsCheck->isChecked(); }
QString SettingsDialog::getDeviceIP() const { return m_ipEdit->currentText().trimmed(); }
QString SettingsDialog::getDevicePort() const { return m_portEdit->currentText().trimmed(); }

// ==================== Setter 方法 ====================
void SettingsDialog::setSerialList(const QStringList &serials) {
    Q_UNUSED(serials);
}

void SettingsDialog::setCurrentSerial(const QString &serial) { Q_UNUSED(serial); }

void SettingsDialog::setBitRate(quint32 bitRate) {
    if (bitRate % 1000000 == 0) {
        m_bitRateEdit->setText(QString::number(bitRate / 1000000));
        m_bitRateUnit->setCurrentText("Mbps");
    } else {
        m_bitRateEdit->setText(QString::number(bitRate / 1000));
        m_bitRateUnit->setCurrentText("Kbps");
    }
}

void SettingsDialog::setMaxSizeIndex(int index) { m_maxSizeBox->setCurrentIndex(index); }
void SettingsDialog::setMaxFps(int fps) { m_fpsSpinBox->setValue(fps); }
void SettingsDialog::setMaxTouchPoints(int points) { m_touchPointsSpinBox->setValue(points); }
void SettingsDialog::setReverseConnect(bool checked) { m_reverseCheck->setChecked(checked); }
void SettingsDialog::setShowToolbar(bool checked) { m_toolbarCheck->setChecked(checked); }
void SettingsDialog::setFrameless(bool checked) { m_framelessCheck->setChecked(checked); }
void SettingsDialog::setShowFPS(bool checked) { m_fpsCheck->setChecked(checked); }
void SettingsDialog::setVideoCodecIndex(int index) { m_codecBox->setCurrentIndex(qBound(0, index, 2)); }
void SettingsDialog::setDeviceIP(const QString &ip) { m_ipEdit->setCurrentText(ip); }
void SettingsDialog::setDevicePort(const QString &port) { m_portEdit->setCurrentText(port); }

void SettingsDialog::setIpHistory(const QStringList &ips) {
    m_ipEdit->clear();
    m_ipEdit->addItems(ips);
}

void SettingsDialog::setPortHistory(const QStringList &ports) {
    m_portEdit->clear();
    m_portEdit->addItems(ports);
}
