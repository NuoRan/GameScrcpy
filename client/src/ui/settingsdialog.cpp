#include "settingsdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QFrame>
#include <QIntValidator>
#include <QSpacerItem>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    applyStyle();
}

void SettingsDialog::setupUI()
{
    setWindowTitle("设置");
    setMinimumWidth(480);
    setModal(true);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(20);
    mainLayout->setContentsMargins(28, 24, 28, 24);

    // ==================== 视频参数区 ====================
    QLabel *videoTitle = new QLabel("视频参数");
    videoTitle->setObjectName("sectionTitle");
    videoTitle->setAlignment(Qt::AlignCenter);

    QHBoxLayout *videoRow = new QHBoxLayout();
    videoRow->setSpacing(12);

    QLabel *bitrateLabel = new QLabel("码率");
    bitrateLabel->setFixedWidth(50);
    bitrateLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_bitRateEdit = new QLineEdit("8");
    m_bitRateEdit->setMinimumHeight(38);
    m_bitRateEdit->setFixedWidth(70);
    m_bitRateEdit->setAlignment(Qt::AlignCenter);
    m_bitRateEdit->setValidator(new QIntValidator(1, 99999, this));

    m_bitRateUnit = new QComboBox();
    m_bitRateUnit->addItems({"Mbps", "Kbps"});
    m_bitRateUnit->setMinimumSize(85, 38);

    QLabel *sizeLabel = new QLabel("分辨率");
    sizeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_maxSizeBox = new QComboBox();
    m_maxSizeBox->addItems({"320", "640", "720", "1080", "1280", "1920", "原始"});
    m_maxSizeBox->setMinimumSize(90, 38);

    videoRow->addWidget(bitrateLabel);
    videoRow->addWidget(m_bitRateEdit);
    videoRow->addWidget(m_bitRateUnit);
    videoRow->addSpacing(16);
    videoRow->addWidget(sizeLabel);
    videoRow->addWidget(m_maxSizeBox);
    videoRow->addStretch(1);

    // ==================== 显示选项区 ====================
    QLabel *optionsTitle = new QLabel("显示选项");
    optionsTitle->setObjectName("sectionTitle");
    optionsTitle->setAlignment(Qt::AlignCenter);

    QHBoxLayout *optionsRow = new QHBoxLayout();
    optionsRow->setSpacing(24);

    m_reverseCheck = new QCheckBox("反向连接");
    m_reverseCheck->setChecked(true);
    m_toolbarCheck = new QCheckBox("工具栏");
    m_framelessCheck = new QCheckBox("无边框");
    m_fpsCheck = new QCheckBox("显示FPS");

    optionsRow->addStretch(1);
    optionsRow->addWidget(m_reverseCheck);
    optionsRow->addWidget(m_toolbarCheck);
    optionsRow->addWidget(m_framelessCheck);
    optionsRow->addWidget(m_fpsCheck);
    optionsRow->addStretch(1);

    // ==================== 无线连接区 ====================
    QLabel *wifiTitle = new QLabel("无线连接");
    wifiTitle->setObjectName("sectionTitle");
    wifiTitle->setAlignment(Qt::AlignCenter);

    // 地址行 - 与设备行对齐
    QHBoxLayout *wifiRow = new QHBoxLayout();
    wifiRow->setSpacing(12);

    QLabel *ipLabel = new QLabel("地址");
    ipLabel->setFixedWidth(50);
    ipLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

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
    m_portEdit->setFixedSize(80, 38);
    if (m_portEdit->lineEdit()) {
        m_portEdit->lineEdit()->setPlaceholderText("5555");
        m_portEdit->lineEdit()->setAlignment(Qt::AlignCenter);
    }

    wifiRow->addWidget(ipLabel);
    wifiRow->addWidget(m_ipEdit, 1);
    wifiRow->addWidget(colonLabel);
    wifiRow->addWidget(m_portEdit);

    // 连接/断开按钮行 - 居中
    QHBoxLayout *connectBtnRow = new QHBoxLayout();
    connectBtnRow->setSpacing(12);

    m_connectBtn = new QPushButton("连接");
    m_connectBtn->setObjectName("primaryBtn");
    m_connectBtn->setMinimumSize(90, 38);

    m_disconnectBtn = new QPushButton("断开");
    m_disconnectBtn->setMinimumSize(90, 38);

    connectBtnRow->addStretch(1);
    connectBtnRow->addWidget(m_connectBtn);
    connectBtnRow->addWidget(m_disconnectBtn);
    connectBtnRow->addStretch(1);

    // ==================== 工具按钮行 - 居中 ====================
    QHBoxLayout *toolRow = new QHBoxLayout();
    toolRow->setSpacing(12);

    m_getIpBtn = new QPushButton("获取设备IP");
    m_getIpBtn->setMinimumSize(100, 38);

    m_adbdBtn = new QPushButton("开启ADBD");
    m_adbdBtn->setMinimumSize(100, 38);

    toolRow->addStretch(1);
    toolRow->addWidget(m_getIpBtn);
    toolRow->addWidget(m_adbdBtn);
    toolRow->addStretch(1);

    // ==================== 组装主布局 ====================
    mainLayout->addWidget(videoTitle);
    mainLayout->addLayout(videoRow);

    mainLayout->addSpacing(4);
    mainLayout->addWidget(optionsTitle);
    mainLayout->addLayout(optionsRow);

    mainLayout->addSpacing(4);
    mainLayout->addWidget(wifiTitle);
    mainLayout->addLayout(wifiRow);
    mainLayout->addLayout(connectBtnRow);

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
void SettingsDialog::setReverseConnect(bool checked) { m_reverseCheck->setChecked(checked); }
void SettingsDialog::setShowToolbar(bool checked) { m_toolbarCheck->setChecked(checked); }
void SettingsDialog::setFrameless(bool checked) { m_framelessCheck->setChecked(checked); }
void SettingsDialog::setShowFPS(bool checked) { m_fpsCheck->setChecked(checked); }
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
