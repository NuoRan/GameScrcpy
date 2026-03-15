#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QKeyEvent>
#include <QRandomGenerator>
#include <QTime>
#include <QTimer>
#include <filesystem>
#include <fstream>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <regex>

#include "config.h"
#include "StringUtils.h"
#include "dialog.h"
#include "ui_dialog.h"
#include "videoform.h"
#include "settingsdialog.h"
#include "terminaldialog.h"
#include "ScriptEngine.h"
#include "service/DeviceSession.h"
#include "ConfigCenter.h"
#include "hid/TouchRouter.h"

#ifdef Q_OS_WIN32
#include "winutils.h"
#endif

// 来自 main.cpp 的语言切换函数
extern void installTranslator(const QString &langOverride);

QString s_keyMapPath = "";

const QString &getKeyMapPath()
{
    if (s_keyMapPath.isEmpty()) {
        s_keyMapPath = QString::fromLocal8Bit(qgetenv("KZSCRCPY_KEYMAP_PATH"));
        if (s_keyMapPath.isEmpty() || !std::filesystem::is_directory(s_keyMapPath.toStdString())) {
            s_keyMapPath = strutil::toQ(strutil::appDirPath() + "/keymap");
        }
    }
    return s_keyMapPath;
}

Dialog::Dialog(QWidget *parent) : QWidget(parent), ui(new Ui::Widget)
{
    ui->setupUi(this);
    initUI();
    applyModernStyle();

    updateBootConfig(true);

    // 自动刷新定时器
    connect(&m_autoUpdatetimer, &QTimer::timeout, this, &Dialog::on_updateDevice_clicked);
    if (ui->autoUpdatecheckBox->isChecked()) {
        m_autoUpdatetimer.start(5000);
    }

    // 常驻设备监听：不依赖“自动刷新”勾选，确保USB/WiFi状态即时更新
    connect(&m_deviceWatchTimer, &QTimer::timeout, this, [this]() {
        requestDeviceRefresh(false);
    });
    m_deviceWatchTimer.start(1500);

    // ADB 进程结果处理
    m_adb.adbProcessResult.connect([this](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
        QString log = "";
        bool newLine = true;
        // Convert std::vector<std::string> / std::string → Qt types at UI boundary
        auto rawArgs = m_adb.arguments();
        QStringList args;
        for (const auto& a : rawArgs) args << strutil::toQ(a);
        const QString stdOut = strutil::toQ(m_adb.getStdOut());
        const QString errOut = strutil::toQ(m_adb.getErrorOut());

        switch (processResult) {
        case qsc::AdbProcess::AER_ERROR_START:
            break;
        case qsc::AdbProcess::AER_SUCCESS_START:
            log = "adb run";
            newLine = false;
            break;
        case qsc::AdbProcess::AER_ERROR_EXEC:
            if (args.contains("connect")) {
                if (m_wifiConnectMode == WifiConnectMode::Manual) {
                    const QString detail = errOut.isEmpty() ? tr("连接失败") : errOut;
                    hideStatusBar();
                    showStatusDialog(tr("WiFi连接失败"), tr("设备地址: %1\n%2").arg(m_currentWifiAddr, detail), QMessageBox::Critical);
                    m_wifiConnectMode = WifiConnectMode::None;
                    m_currentWifiAddr.clear();
                } else if (m_wifiConnectMode == WifiConnectMode::AutoReconnect) {
                    tryNextAutoReconnect();
                }
                requestDeviceRefresh(true);
                break;
            }
            if (args.contains("ifconfig") && args.contains("wlan0")) {
                getIPbyIp();
            }
            break;
        case qsc::AdbProcess::AER_ERROR_MISSING_BINARY:
            log = "adb not found";
            showStatusDialog(tr("ADB不可用"), tr("未找到 adb 可执行文件，请检查环境配置。"), QMessageBox::Critical);
            break;
        case qsc::AdbProcess::AER_SUCCESS_EXEC:
            if (args.contains("devices")) {
                auto rawDevices = m_adb.getDevicesSerialFromStdOut();
                QStringList devices;
                for (const auto& d : rawDevices) devices << strutil::toQ(d);
                ui->connectedPhoneList->clear();

                // 同步到设置对话框
                if (m_settingsDialog) {
                    m_settingsDialog->setSerialList(devices);
                }

                for (auto &item : devices) {
                    QString nickName = strutil::toQ(Config::getInstance().getNickName(strutil::fromQ(item)));
                    QString displayName = nickName.isEmpty() ? item : nickName + " - " + item;
                    ui->connectedPhoneList->addItem(displayName);
                }

                // 手动 WiFi 连接成功后：自动连接对应设备会话
                if (m_pendingManualWifiStart) {
                    const QString wifiSerial = findWifiSerialByAddr(m_currentWifiAddr, devices);
                    if (!wifiSerial.isEmpty()) {
                        m_pendingManualWifiStart = false;
                        m_pendingManualWifiRefreshRetry = 0;
                        m_wifiConnectMode = WifiConnectMode::None;
                        m_currentWifiAddr.clear();
                        if (!m_videoForms.contains(wifiSerial)) {
                            tryStartServerForSerial(wifiSerial);
                        } else {
                            hideStatusBar();
                        }
                    } else {
                        ++m_pendingManualWifiRefreshRetry;
                        if (m_pendingManualWifiRefreshRetry <= 5) {
                            updateStatusBar(tr("正在查找设备... (重试 %1/5)").arg(m_pendingManualWifiRefreshRetry));
                            QTimer::singleShot(300, this, [this]() { requestDeviceRefresh(true); });
                        } else {
                            m_pendingManualWifiStart = false;
                            m_pendingManualWifiRefreshRetry = 0;
                            hideStatusBar();
                            showStatusDialog(tr("WiFi连接失败"), tr("设备已连接到 adb，但未在设备列表中出现: %1").arg(m_currentWifiAddr), QMessageBox::Critical);
                            m_wifiConnectMode = WifiConnectMode::None;
                            m_currentWifiAddr.clear();
                        }
                    }
                }

                // USB 插入即时连接（无需开启自动刷新）
                QString newUsbSerial;
                for (const auto &serial : devices) {
                    if (!isWifiSerial(serial) && !m_lastDeviceSerials.contains(serial)) {
                        newUsbSerial = serial;
                        break;
                    }
                }

                // 手动 USB 连接按钮触发后的刷新回调
                if (m_pendingUsbConnect) {
                    m_pendingUsbConnect = false;
                    int firstUsbDevice = findDeviceFromeSerialBox(false);
                    if (-1 == firstUsbDevice) {
                        hideStatusBar();
                        outLog(tr("未找到USB设备！"));
                        showStatusDialog(tr("USB连接失败"), tr("未检测到可用的 USB 设备。"), QMessageBox::Warning);
                    } else {
                        const QString serial = extractSerialFromItemText(ui->connectedPhoneList->item(firstUsbDevice)->text());
                        tryStartServerForSerial(serial);
                    }
                } else if (!newUsbSerial.isEmpty() && !m_videoForms.contains(newUsbSerial)) {
                    // 常驻监听发现新 USB，自动建立连接
                    tryStartServerForSerial(newUsbSerial);
                }

                // 首次扫描后自动尝试历史 WiFi 回连
                if (!m_hasInitialDeviceScan) {
                    m_hasInitialDeviceScan = true;
                    startAutoReconnectFromHistory();
                }

                m_lastDeviceSerials = devices;
            } else if (args.contains("connect")) {
                const bool connectedOk = stdOut.contains("connected to", Qt::CaseInsensitive)
                                         || stdOut.contains("already connected", Qt::CaseInsensitive);

                if (m_wifiConnectMode == WifiConnectMode::Manual) {
                    if (connectedOk) {
                        updateStatusBar(tr("已连接，正在查找设备..."));
                        m_pendingManualWifiStart = true;
                        m_pendingManualWifiRefreshRetry = 0;
                    } else {
                        const QString detail = errOut.isEmpty() ? stdOut : errOut;
                        hideStatusBar();
                        showStatusDialog(tr("WiFi连接失败"), tr("设备地址: %1\n%2").arg(m_currentWifiAddr, detail), QMessageBox::Critical);
                        m_wifiConnectMode = WifiConnectMode::None;
                        m_currentWifiAddr.clear();
                    }
                    requestDeviceRefresh(true);
                } else if (m_wifiConnectMode == WifiConnectMode::AutoReconnect) {
                    if (connectedOk) {
                        hideStatusBar();
                        outLog(tr("已自动回连 WiFi 设备: %1").arg(m_currentWifiAddr));
                        m_wifiConnectMode = WifiConnectMode::None;
                        m_currentWifiAddr.clear();
                        requestDeviceRefresh(true);
                    } else {
                        tryNextAutoReconnect();
                    }
                }
            } else if (args.contains("disconnect")) {
                // 断开成功不弹窗，按需仅日志
                outLog(tr("WiFi设备已断开"));
            } else if (args.contains("show") && args.contains("wlan0")) {
                QString ip = strutil::toQ(m_adb.getDeviceIPFromStdOut());
                if (ip.isEmpty()) {
                    log = tr("未找到IP");
                    break;
                }
                if (m_settingsDialog) {
                    m_settingsDialog->setDeviceIP(ip);
                }
            } else if (args.contains("ifconfig") && args.contains("wlan0")) {
                QString ip = strutil::toQ(m_adb.getDeviceIPFromStdOut());
                if (ip.isEmpty()) {
                    log = tr("未找到IP");
                    break;
                }
                if (m_settingsDialog) {
                    m_settingsDialog->setDeviceIP(ip);
                }
            } else if (args.contains("ip -o a")) {
                QString ip = strutil::toQ(m_adb.getDeviceIPByIpFromStdOut());
                if (ip.isEmpty()) {
                    log = tr("未找到IP");
                    break;
                }
                if (m_settingsDialog) {
                    m_settingsDialog->setDeviceIP(ip);
                }
            }
            break;
        }
        if (!log.isEmpty()) {
            outLog(log, newLine);
        }
    });

    // 系统托盘
    m_hideIcon = new QSystemTrayIcon(this);
    m_hideIcon->setIcon(QIcon(":/image/tray/logo.png"));
    m_menu = new QMenu(this);
    m_quit = new QAction(this);
    m_showWindow = new QAction(this);
    m_showWindow->setText(tr("显示"));
    m_quit->setText(tr("退出"));
    m_menu->addAction(m_showWindow);
    m_menu->addAction(m_quit);
    m_hideIcon->setContextMenu(m_menu);
    m_hideIcon->show();
    connect(m_showWindow, &QAction::triggered, this, &Dialog::show);
    connect(m_quit, &QAction::triggered, this, [this]() {
        m_hideIcon->hide();
        qApp->quit();
    });
    connect(m_hideIcon, &QSystemTrayIcon::activated, this, &Dialog::slotActivated);

    // 设备管理回调 (替代原 Qt signal/slot)
    m_deviceConnectedListenerId = qsc::IDeviceManage::getInstance().addDeviceConnectedListener(
        [this](bool success, const std::string& serial, const std::string& deviceName, const Size& size) {
            onDeviceConnected(success, strutil::toQ(serial), strutil::toQ(deviceName), typeconv::toQ(size));
        });
    m_deviceDisconnectedListenerId = qsc::IDeviceManage::getInstance().addDeviceDisconnectedListener(
        [this](const std::string& serial) {
            onDeviceDisconnected(strutil::toQ(serial));
        });

    // 首次启动立即刷新一次设备列表（用于后续自动回连）
    QTimer::singleShot(0, this, [this]() {
        requestDeviceRefresh(true);
    });
}

Dialog::~Dialog()
{
    updateBootConfig(false);
    qsc::IDeviceManage::getInstance().removeDeviceListener(m_deviceConnectedListenerId);
    qsc::IDeviceManage::getInstance().removeDeviceListener(m_deviceDisconnectedListenerId);
    // disconnectAllDevice 已在 main() 退出前调用，此处为防御性调用
    qsc::IDeviceManage::getInstance().disconnectAllDevice();
    // 清空 VideoForm 映射（指针已由各自的 deleteLater 销毁，不再二次操作）
    m_videoForms.clear();
    delete ui;
}

// ---------------------------------------------------------
// 应用现代样式
// ---------------------------------------------------------
void Dialog::applyModernStyle()
{
    setStyleSheet(R"(
        QWidget {
            background-color: #09090b;
            font-family: "Microsoft YaHei", "SF Pro Display", -apple-system, sans-serif;
        }
        QLabel {
            color: #a1a1aa;
            background: transparent;
        }
        #logoLabel {
            color: #fafafa;
            font-size: 22px;
            font-weight: 700;
        }
        #connectTitle, #deviceTitle {
            color: #fafafa;
            font-size: 15px;
            font-weight: 600;
        }
        #deviceHint {
            color: #52525b;
            font-size: 12px;
        }
        QFrame#connectCard, QFrame#deviceCard {
            background-color: #18181b;
            border: 1px solid #27272a;
            border-radius: 12px;
        }
        QFrame#toolbarFrame {
            background-color: #0f0f12;
            border-top: 1px solid #27272a;
        }
        QPushButton {
            background-color: #27272a;
            border: 1px solid #3f3f46;
            border-radius: 10px;
            padding: 10px 20px;
            color: #fafafa;
            font-size: 14px;
            font-weight: 500;
        }
        QPushButton:hover {
            background-color: #3f3f46;
            border-color: #52525b;
        }
        QPushButton:pressed {
            background-color: #52525b;
        }
        QPushButton#usbConnectBtn {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #3b82f6, stop:1 #1d4ed8);
            border: none;
            font-size: 15px;
            font-weight: 600;
        }
        QPushButton#usbConnectBtn:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #60a5fa, stop:1 #2563eb);
        }
        QPushButton#wifiConnectBtn {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #22c55e, stop:1 #15803d);
            border: none;
            font-size: 15px;
            font-weight: 600;
        }
        QPushButton#wifiConnectBtn:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #4ade80, stop:1 #16a34a);
        }
        QPushButton#settingsBtn, QPushButton#terminalBtn {
            background-color: transparent;
            border: 1px solid #3f3f46;
            border-radius: 8px;
            color: #a1a1aa;
            font-size: 13px;
        }
        QPushButton#settingsBtn:hover, QPushButton#terminalBtn:hover {
            background-color: #27272a;
            color: #fafafa;
        }
        QPushButton#langBtn {
            background-color: transparent;
            border: 1px solid #3f3f46;
            border-radius: 8px;
            color: #a1a1aa;
            font-size: 13px;
        }
        QPushButton#langBtn:hover {
            background-color: #27272a;
            color: #fafafa;
        }
        QPushButton#updateDevice {
            background-color: #27272a;
            border-radius: 6px;
            padding: 6px 14px;
            font-size: 12px;
        }
        QListWidget {
            background-color: #09090b;
            border: 1px solid #27272a;
            border-radius: 8px;
            padding: 4px;
            outline: none;
        }
        QListWidget::item {
            color: #a1a1aa;
            padding: 10px 12px;
            border-radius: 6px;
            margin: 2px 0;
        }
        QListWidget::item:hover {
            background-color: #27272a;
            color: #fafafa;
        }
        QListWidget::item:selected {
            background-color: #3f3f46;
            color: #fafafa;
        }
        QCheckBox {
            color: #71717a;
            font-size: 13px;
            spacing: 8px;
            background: transparent;
            background-color: transparent;
        }
        QCheckBox:hover {
            color: #a1a1aa;
            background: transparent;
        }
        QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border-radius: 4px;
            border: 2px solid #52525b;
        }
        QCheckBox::indicator:checked {
            background-color: #6366f1;
            border-color: #6366f1;
        }
        QScrollBar:vertical {
            background-color: transparent;
            width: 6px;
            margin: 4px 0;
        }
        QScrollBar::handle:vertical {
            background-color: #3f3f46;
            border-radius: 3px;
            min-height: 30px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
        QFrame#statusBarFrame {
            background-color: #0f0f12;
            border-top: 1px solid #27272a;
        }
        QFrame#statusBarFrame QLabel {
            color: #a1a1aa;
            font-size: 12px;
            background: transparent;
        }
        QFrame#statusBarFrame QProgressBar {
            background-color: transparent;
            border: none;
            border-radius: 8px;
        }
        QFrame#statusBarFrame QProgressBar::chunk {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #6366f1, stop:1 #818cf8);
            border-radius: 8px;
        }
    )");
}

// ---------------------------------------------------------
// UI初始化
// ---------------------------------------------------------
void Dialog::initUI()
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("GameScrcpy");

    // 确保自动刷新复选框无背景填充
    ui->autoUpdatecheckBox->setAutoFillBackground(false);
    ui->autoUpdatecheckBox->setAttribute(Qt::WA_TranslucentBackground);

#ifdef Q_OS_LINUX
    if (!qApp->windowIcon().isNull()) {
        setWindowIcon(qApp->windowIcon());
    }
#endif

#ifdef Q_OS_WIN32
    WinUtils::setDarkBorderToWindow((HWND)this->winId(), true);
#endif

    // 创建设置对话框（延迟创建）
    m_settingsDialog = new SettingsDialog(this);

    // 连接设置对话框信号
    connect(m_settingsDialog, &SettingsDialog::wirelessConnect, this, &Dialog::onWirelessConnect);
    connect(m_settingsDialog, &SettingsDialog::wirelessDisconnect, this, &Dialog::onWirelessDisconnect);
    connect(m_settingsDialog, &SettingsDialog::requestDeviceIP, this, &Dialog::onGetDeviceIP);
    connect(m_settingsDialog, &SettingsDialog::startAdbd, this, &Dialog::onStartAdbd);
    // 设置对话框关闭时保存配置
    connect(m_settingsDialog, &QDialog::finished, this, [this]() {
        updateBootConfig(false);
    });

    // 创建终端对话框
    m_terminalDialog = new TerminalDialog(this);
    connect(m_terminalDialog, &TerminalDialog::executeCommand, this, &Dialog::onExecuteCommand);
    connect(m_terminalDialog, &TerminalDialog::stopCommand, this, &Dialog::onStopCommand);

    // 初始化设置对话框数据
    syncSettingsToDialog();

    // 初始化语言按钮显示
    updateLangBtnText();

    // 初始化底部状态栏
    m_statusBarFrame = ui->statusBarFrame;
    m_statusBarFrame->setFixedHeight(0);
    m_statusBarFrame->setVisible(false);

    auto *statusLayout = new QHBoxLayout(m_statusBarFrame);
    statusLayout->setContentsMargins(16, 6, 16, 6);
    statusLayout->setSpacing(10);

    m_statusProgress = new QProgressBar(m_statusBarFrame);
    m_statusProgress->setFixedSize(16, 16);
    m_statusProgress->setRange(0, 0);  // indeterminate
    m_statusProgress->setTextVisible(false);
    statusLayout->addWidget(m_statusProgress);

    m_statusLabel = new QLabel(m_statusBarFrame);
    m_statusLabel->setText("");
    statusLayout->addWidget(m_statusLabel, 1);

    m_statusHideTimer.setSingleShot(true);
    connect(&m_statusHideTimer, &QTimer::timeout, this, &Dialog::hideStatusBar);
}

// ---------------------------------------------------------
// 同步设置到对话框
// ---------------------------------------------------------
void Dialog::syncSettingsToDialog()
{
    if (!m_settingsDialog) return;

    UserBootConfig config = Config::getInstance().getUserBootConfig();

    if (config.bitRate > 0) {
        m_settingsDialog->setBitRate(config.bitRate);
    }
    m_settingsDialog->setMaxSizeIndex(config.maxSizeIndex);
    m_settingsDialog->setMaxFps(config.maxFps);
    m_settingsDialog->setReverseConnect(config.reverseConnect);
    m_settingsDialog->setShowToolbar(config.showToolbar);
    m_settingsDialog->setFrameless(config.framelessWindow);
    m_settingsDialog->setShowFPS(config.showFPS);
    m_settingsDialog->setVideoCodecIndex(config.videoCodecIndex);

    // 加载历史记录
    m_settingsDialog->setIpHistory(strutil::toQList(Config::getInstance().getIpHistory()));
    m_settingsDialog->setPortHistory(strutil::toQList(Config::getInstance().getPortHistory()));
}

// ---------------------------------------------------------
// 底部工具栏按钮
// ---------------------------------------------------------
void Dialog::on_settingsBtn_clicked()
{
    syncSettingsToDialog();
    m_settingsDialog->exec();
}

void Dialog::on_terminalBtn_clicked()
{
    m_terminalDialog->show();
    m_terminalDialog->raise();
    m_terminalDialog->activateWindow();
}

// ---------------------------------------------------------
// 语言切换按钮
// ---------------------------------------------------------
void Dialog::on_langBtn_clicked()
{
    // 当前语言判断：如果配置是 zh_CN 或 Auto 且系统是中文，则切换到英文，反之切换到中文
    QString current = strutil::toQ(Config::getInstance().getLanguage());
    QString next;
    if (current == "en_US") {
        next = "zh_CN";
    } else if (current == "zh_CN") {
        next = "en_US";
    } else {
        // Auto 或其他 — 检测当前实际语言
        QLocale locale;
        if (locale.language() == QLocale::Chinese) {
            next = "en_US";
        } else {
            next = "zh_CN";
        }
    }

    Config::getInstance().setLanguage(strutil::fromQ(next));
    installTranslator(next);
    // QEvent::LanguageChange 会自动触发 changeEvent
}

void Dialog::updateLangBtnText()
{
    QString lang = strutil::toQ(Config::getInstance().getLanguage());
    if (lang == "zh_CN" || (lang == "Auto" && QLocale().language() == QLocale::Chinese)) {
        ui->langBtn->setText(QString::fromUtf8("🌐 EN"));
    } else {
        ui->langBtn->setText(QString::fromUtf8("🌐 中文"));
    }
}

void Dialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QWidget::changeEvent(event);
}

void Dialog::retranslateUi()
{
    // dialog.ui 中的字符串
    ui->connectTitle->setText(tr("快速连接"));
    ui->usbConnectBtn->setText(tr("USB 连接"));
    ui->wifiConnectBtn->setText(tr("WiFi 连接"));
    ui->deviceTitle->setText(tr("设备列表"));
    ui->deviceHint->setText(tr("双击连接"));
    ui->autoUpdatecheckBox->setText(tr("自动刷新"));
    ui->updateDevice->setText(tr("刷新"));
    ui->settingsBtn->setText(tr("⚙  设置"));
    ui->terminalBtn->setText(tr("⌨  终端"));

    // 系统托盘
    m_showWindow->setText(tr("显示"));
    m_quit->setText(tr("退出"));

    // 更新语言按钮显示
    updateLangBtnText();
}

// ---------------------------------------------------------
// 设置对话框信号处理
// ---------------------------------------------------------
void Dialog::onStartServer()
{
    if (m_currentSerial.isEmpty()) {
        outLog(tr("错误: 请先选择设备"));
        return;
    }

    outLog(tr("正在启动..."), false);

    QString maxSizeText = QString::number(m_settingsDialog->getMaxSize());
    quint16 videoSize = maxSizeText.toUShort();

    qsc::DeviceParams params;
    params.serial = strutil::fromQ(m_currentSerial);
    params.maxSize = videoSize;
    params.bitRate = m_settingsDialog->getBitRate();
    params.maxFps = static_cast<quint32>(m_settingsDialog->getMaxFps());
    params.renderExpiredFrames = Config::getInstance().getRenderExpiredFrames();
    params.serverLocalPath = strutil::fromQ(getServerPath());
    params.serverRemotePath = Config::getInstance().getServerPath();
    params.gameScript = "";
    params.logLevel = Config::getInstance().getLogLevel();
    params.codecOptions = Config::getInstance().getCodecOptions();
    params.codecName = Config::getInstance().getCodecName();
    params.videoCodec = strutil::fromQ(m_settingsDialog->getVideoCodecName());
    params.scid = QRandomGenerator::global()->bounded(1, 10000) & 0x7FFFFFFF;

    qsc::IDeviceManage::getInstance().connectDevice(params);
}

void Dialog::onStopServer()
{
    if (qsc::IDeviceManage::getInstance().disconnectDevice(strutil::fromQ(m_currentSerial))) {
        outLog(tr("已停止服务"));
    }
}

void Dialog::onStopAllServers()
{
    qsc::IDeviceManage::getInstance().disconnectAllDevice();
    outLog(tr("已停止所有服务"));
}

void Dialog::onWirelessConnect()
{
    if (checkAdbRun()) return;

    QString ip = m_settingsDialog->getDeviceIP();
    QString port = m_settingsDialog->getDevicePort();

    if (ip.isEmpty()) {
        outLog(tr("错误: 请输入IP地址"));
        return;
    }

    QString addr = ip;
    if (!port.isEmpty()) {
        addr += ":" + port;
    } else {
        addr += ":5555";
    }

    // 保存历史
    Config::getInstance().saveIpHistory(strutil::fromQ(ip));
    if (!port.isEmpty()) {
        Config::getInstance().savePortHistory(strutil::fromQ(port));
    }

    m_wifiConnectMode = WifiConnectMode::Manual;
    m_currentWifiAddr = addr;

    updateStatusBar(tr("WiFi 连接中: %1...").arg(addr));
    outLog(tr("正在连接 %1...").arg(addr), false);
    m_adb.execute("", {"connect", strutil::fromQ(addr)});
}

void Dialog::onWirelessDisconnect()
{
    if (checkAdbRun()) return;

    QString addr = m_settingsDialog->getDeviceIP();
    outLog(tr("正在断开..."), false);
    m_adb.execute("", {"disconnect", strutil::fromQ(addr)});
    QTimer::singleShot(300, this, [this]() { requestDeviceRefresh(true); });
}

void Dialog::onGetDeviceIP()
{
    if (checkAdbRun()) return;

    outLog(tr("正在获取IP..."), false);
    m_adb.execute(strutil::fromQ(m_settingsDialog->getSerial()), {"shell", "ifconfig", "wlan0"});
}

void Dialog::onStartAdbd()
{
    if (checkAdbRun()) return;

    outLog(tr("正在开启ADBD..."), false);
    m_adb.execute(strutil::fromQ(m_settingsDialog->getSerial()), {"tcpip", "5555"});
}

// ---------------------------------------------------------
// 终端对话框信号处理
// ---------------------------------------------------------
void Dialog::onExecuteCommand(const QString &cmd)
{
    if (checkAdbRun()) return;

    m_terminalDialog->appendOutput("$ adb " + cmd);

    {
        std::string serial = strutil::fromQ(m_settingsDialog ? m_settingsDialog->getSerial() : QString());
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        auto parts = cmd.split(" ", Qt::SkipEmptyParts);
#else
        auto parts = cmd.split(" ", QString::SkipEmptyParts);
#endif
        std::vector<std::string> stdParts;
        for (const auto& p : parts) stdParts.push_back(strutil::fromQ(p));
        m_adb.execute(serial, stdParts);
    }
}

void Dialog::onStopCommand()
{
    m_adb.kill();
}

// ---------------------------------------------------------
// 更新启动配置
// ---------------------------------------------------------
void Dialog::updateBootConfig(bool toView)
{
    if (toView) {
        // 加载配置到界面
        UserBootConfig config = Config::getInstance().getUserBootConfig();
        ui->autoUpdatecheckBox->setChecked(config.autoUpdateDevice);

        // 同步配置到设置对话框
        syncSettingsToDialog();
    } else {
        // 保存配置 - 先获取当前配置，避免覆盖其他字段
        UserBootConfig config = Config::getInstance().getUserBootConfig();

        if (m_settingsDialog) {
            config.bitRate = m_settingsDialog->getBitRate();
            config.maxSizeIndex = m_settingsDialog->getMaxSizeIndex();
            config.maxFps = m_settingsDialog->getMaxFps();
            config.reverseConnect = m_settingsDialog->isReverseConnect();
            config.showFPS = m_settingsDialog->showFPS();
            config.framelessWindow = m_settingsDialog->isFrameless();
            config.showToolbar = m_settingsDialog->showToolbar();
            config.videoCodecIndex = m_settingsDialog->getVideoCodecIndex();

            // 保存IP和端口历史
            QString ip = m_settingsDialog->getDeviceIP();
            QString port = m_settingsDialog->getDevicePort();
            if (!ip.isEmpty()) {
                Config::getInstance().saveIpHistory(strutil::fromQ(ip));
            }
            if (!port.isEmpty()) {
                Config::getInstance().savePortHistory(strutil::fromQ(port));
            }
        }
        config.autoUpdateDevice = ui->autoUpdatecheckBox->isChecked();

        Config::getInstance().setUserBootConfig(config);
    }
}

void Dialog::execAdbCmd()
{
    // 已移动到 onExecuteCommand
}

void Dialog::delayMs(int ms)
{
    QTime dieTime = QTime::currentTime().addMSecs(ms);
    while (QTime::currentTime() < dieTime) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }
}

bool Dialog::isWifiSerial(const QString &serial) const
{
    static const std::string regStr = "\\b(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\:([0-9]|[1-9]\\d|[1-9]\\d{2}|[1-9]\\d{3}|[1-5]\\d{4}|6[0-4]\\d{3}|65[0-4]\\d{2}|655[0-2]\\d|6553[0-5])\\b";
    std::string serialStd = serial.toStdString();
    std::regex regIP(regStr);
    return std::regex_match(serialStd, regIP) || std::regex_search(serialStd, std::regex("\\d+\\.\\d+\\.\\d+\\.\\d+"));
}

QString Dialog::extractSerialFromItemText(const QString &text) const
{
    return text.contains(" - ") ? text.split(" - ").last() : text;
}

void Dialog::showStatusDialog(const QString &title, const QString &message, QMessageBox::Icon icon)
{
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(title);
    msgBox.setText(message);
    msgBox.setIcon(icon);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setStyleSheet(R"(
        QMessageBox {
            background-color: #18181b;
        }
        QMessageBox QLabel {
            color: #fafafa;
            font-size: 13px;
            min-width: 340px;
        }
        QMessageBox QPushButton {
            background-color: #27272a;
            border: 1px solid #3f3f46;
            border-radius: 8px;
            padding: 8px 18px;
            color: #fafafa;
            font-size: 13px;
            min-width: 90px;
        }
        QMessageBox QPushButton:hover {
            background-color: #3f3f46;
        }
    )");
    msgBox.exec();
}

void Dialog::updateStatusBar(const QString &text, bool showProgress)
{
    m_statusHideTimer.stop();

    if (m_statusBarFrame) {
        m_statusLabel->setText(text);
        m_statusProgress->setVisible(showProgress);
        if (!m_statusBarFrame->isVisible() || m_statusBarFrame->maximumHeight() == 0) {
            m_statusBarFrame->setVisible(true);
            m_statusBarFrame->setFixedHeight(32);
        }
    }
}

void Dialog::hideStatusBar()
{
    if (m_statusBarFrame) {
        m_statusBarFrame->setFixedHeight(0);
        m_statusBarFrame->setVisible(false);
        m_statusLabel->clear();
    }
}

void Dialog::requestDeviceRefresh(bool force)
{
    if (!force && m_adb.isRuning()) {
        return;
    }
    if (force && checkAdbRun()) {
        return;
    }
    m_adb.execute("", {"devices"});
}

void Dialog::tryStartServerForSerial(const QString &serial)
{
    if (serial.isEmpty()) {
        return;
    }
    m_currentSerial = serial;
    if (m_settingsDialog) {
        m_settingsDialog->setCurrentSerial(serial);
    }
    updateStatusBar(tr("正在启动会话: %1...").arg(serial));
    outLog(tr("正在连接设备: %1").arg(serial));
    onStartServer();
}

QString Dialog::findWifiSerialByAddr(const QString &addr, const QStringList &devices) const
{
    if (addr.isEmpty()) {
        return QString();
    }

    const QString ip = addr.section(':', 0, 0);
    for (const auto &serial : devices) {
        if (!isWifiSerial(serial)) {
            continue;
        }
        if (serial == addr) {
            return serial;
        }
        if (!ip.isEmpty() && serial.startsWith(ip + ":")) {
            return serial;
        }
    }
    return QString();
}

void Dialog::startAutoReconnectFromHistory()
{
    if (m_hasTriedAutoReconnect) {
        return;
    }
    m_hasTriedAutoReconnect = true;

    // 已有 WiFi 设备时不重复回连
    for (const auto &serial : m_lastDeviceSerials) {
        if (isWifiSerial(serial)) {
            return;
        }
    }

    const auto ips = Config::getInstance().getIpHistory();
    if (ips.empty()) {
        return;
    }

    const auto ports = Config::getInstance().getPortHistory();
    const QString port = ports.empty() ? QStringLiteral("5555") : strutil::toQ(ports.front());

    m_autoReconnectQueue.clear();
    for (const auto &ip : ips) {
        if (!ip.empty()) {
            m_autoReconnectQueue << (strutil::toQ(ip) + ":" + port);
        }
        if (m_autoReconnectQueue.size() >= 3) {
            break;
        }
    }

    if (m_autoReconnectQueue.isEmpty()) {
        return;
    }

    m_autoReconnectIndex = 0;
    tryNextAutoReconnect();
}

void Dialog::tryNextAutoReconnect()
{
    if (m_adb.isRuning()) {
        QTimer::singleShot(200, this, [this]() { tryNextAutoReconnect(); });
        return;
    }

    if (m_autoReconnectIndex >= m_autoReconnectQueue.size()) {
        m_wifiConnectMode = WifiConnectMode::None;
        m_currentWifiAddr.clear();
        hideStatusBar();
        return;
    }

    m_wifiConnectMode = WifiConnectMode::AutoReconnect;
    m_currentWifiAddr = m_autoReconnectQueue[m_autoReconnectIndex++];
    updateStatusBar(tr("自动回连: %1...").arg(m_currentWifiAddr));
    outLog(tr("自动回连WiFi设备: %1").arg(m_currentWifiAddr));
    m_adb.execute("", {"connect", strutil::fromQ(m_currentWifiAddr)});
}

void Dialog::slotActivated(QSystemTrayIcon::ActivationReason reason)
{
    switch (reason) {
    case QSystemTrayIcon::Trigger:
#ifdef Q_OS_WIN32
        this->show();
#endif
        break;
    default:
        break;
    }
}

void Dialog::on_updateDevice_clicked()
{
    requestDeviceRefresh(true);
}

// ---------------------------------------------------------
// 快捷连接按钮
// ---------------------------------------------------------
void Dialog::on_usbConnectBtn_clicked()
{
    m_pendingUsbConnect = true;
    updateStatusBar(tr("正在搜索 USB 设备..."));
    requestDeviceRefresh(true);
}

void Dialog::on_wifiConnectBtn_clicked()
{
    // 按用户预期：WiFi按钮只做“设备连接（adb connect）”，不直接启动投屏会话
    onWirelessConnect();
}

int Dialog::findDeviceFromeSerialBox(bool wifi)
{
    for (int i = 0; i < ui->connectedPhoneList->count(); ++i) {
        const QString itemText = ui->connectedPhoneList->item(i)->text();
        const QString serial = extractSerialFromItemText(itemText);
        const bool isWifi = this->isWifiSerial(serial);
        bool found = wifi ? isWifi : !isWifi;
        if (found) {
            return i;
        }
    }
    return -1;
}

void Dialog::on_connectedPhoneList_itemDoubleClicked(QListWidgetItem *item)
{
    QString serial = extractSerialFromItemText(item->text());

    m_currentSerial = serial;  // 存储当前选中的设备
    updateStatusBar(tr("正在启动会话: %1...").arg(serial));
    onStartServer();
}

// ---------------------------------------------------------
// 设备连接回调
// ---------------------------------------------------------
void Dialog::onDeviceConnected(bool success, const QString &serial, const QString &deviceName, const QSize &size)
{
    Q_UNUSED(deviceName);
    if (!success) {
        hideStatusBar();
        showStatusDialog(tr("连接失败"), tr("设备 %1 连接失败，请检查设备授权与连接状态。").arg(serial), QMessageBox::Critical);
        return;
    }

    // 连接成功，状态栏短暂显示后自动隐藏
    updateStatusBar(tr("设备已连接: %1").arg(serial), false);
    m_statusHideTimer.start(2000);

    bool frameless = m_settingsDialog ? m_settingsDialog->isFrameless() : false;
    bool showToolbar = m_settingsDialog ? m_settingsDialog->showToolbar() : true;
    bool showFPS = m_settingsDialog ? m_settingsDialog->showFPS() : false;

    auto videoForm = new VideoForm(frameless, Config::getInstance().getSkin(), showToolbar);

    // UI 完全解耦：直接获取 DeviceSession，使用纯信号槽交互
    // 必须先 bindSession 后 setSerial，否则 loadKeyMap 时 m_session 为空
    auto* session = qsc::IDeviceManage::getInstance().getSession(strutil::fromQ(serial));
    if (session) {
        videoForm->bindSession(session);
    }

    // 记录映射关系
    m_videoForms[serial] = videoForm;

    // 设置序列号并加载键位配置（此时 m_session 已绑定）
    videoForm->setSerial(serial);

    videoForm->showFPS(showFPS);

    QString name = strutil::toQ(Config::getInstance().getNickName(strutil::fromQ(serial)));
    if (name.isEmpty()) name = "GameScrcpy";

    videoForm->setWindowTitle(name + " - " + serial);
    videoForm->updateShowSize(size);

    // 纯触控模式: 视频关闭 且 触控方式不需 server → 启用虚拟画布
    int touchMethodIdx = qsc::ConfigCenter::instance().get<int>("user/touchMode", 0);
    auto touchMethod = static_cast<TouchMethod>(touchMethodIdx);
    bool videoEnabled = qsc::ConfigCenter::instance().get<bool>("user/videoChannelEnabled", true);
    if (!videoEnabled && !methodNeedsServer(touchMethod)) {
        videoForm->enableVirtualCanvas(QSize(size.width(), size.height()));
    }

    // 恢复窗口位置和大小（必须在 show 之前调用）
    videoForm->restoreWindowGeometry();

#ifdef Q_OS_WIN32
    QTimer::singleShot(200, videoForm, [videoForm](){videoForm->show();});
#else
    videoForm->show();
#endif
}

void Dialog::onDeviceDisconnected(QString serial)
{
    // 从映射中找到对应的 VideoForm
    auto it = m_videoForms.find(serial);
    if (it == m_videoForms.end()) {
        return;
    }

    VideoForm* vf = it.value();
    m_videoForms.erase(it);

    if (vf) {
        // UI 解耦：bindSession(nullptr) 会断开所有信号连接
        vf->bindSession(nullptr);
        // close() 会触发 closeEvent，但因为 m_closing 标志，不会重复处理
        vf->close();
    }
}

void Dialog::on_autoUpdatecheckBox_toggled(bool checked)
{
    if (checked) {
        m_autoUpdatetimer.start(5000);
    } else {
        m_autoUpdatetimer.stop();
    }
}

quint32 Dialog::getBitRate()
{
    return m_settingsDialog ? m_settingsDialog->getBitRate() : 8000000;
}

const QString &Dialog::getServerPath()
{
    static QString serverPath;
    if (serverPath.isEmpty()) {
        // 1. 首先检查环境变量
        serverPath = QString::fromLocal8Bit(qgetenv("KZSCRCPY_SERVER_PATH"));
        if (!serverPath.isEmpty() && std::filesystem::is_regular_file(serverPath.toStdString())) {
            return serverPath;
        }

        // 2. 检查应用目录下的外部文件
        std::string externalStd = strutil::appDirPath() + "/scrcpy-server";
        if (std::filesystem::exists(externalStd)) {
            serverPath = strutil::toQ(externalStd);
            return serverPath;
        }

        // 3. 从内嵌资源提取到临时目录
        std::string tempDir = std::filesystem::temp_directory_path().string();
        std::string extractedStd = tempDir + "/kzscrcpy-server";
        QString extractedPath = QString::fromStdString(extractedStd);

        // 检查是否需要重新提取（比较文件大小）
        QFile resourceFile(":/scrcpy-server");
        bool needExtract = true;

        if (std::filesystem::exists(extractedStd) && resourceFile.open(QIODevice::ReadOnly)) {
            qint64 resourceSize = resourceFile.size();
            resourceFile.close();
            if (static_cast<uintmax_t>(resourceSize) == std::filesystem::file_size(extractedStd)) {
                needExtract = false;  // 文件已存在且大小相同，无需重新提取
            }
        }

        if (needExtract) {
            if (resourceFile.open(QIODevice::ReadOnly)) {
                QByteArray data = resourceFile.readAll();
                resourceFile.close();

                std::ofstream ofs(extractedStd, std::ios::binary);
                if (ofs) {
                    ofs.write(data.constData(), data.size());
                    qDebug() << "Extracted scrcpy-server to:" << extractedPath;
                }
            }
        }

        serverPath = extractedPath;
    }
    return serverPath;
}

void Dialog::outLog(const QString &log, bool newLine)
{
    if (m_terminalDialog) {
        m_terminalDialog->appendOutput(log);
        if (newLine) {
            m_terminalDialog->appendOutput("");
        }
    }
}

bool Dialog::filterLog(const QString &log)
{
    if (log.contains("app_proces")) return true;
    if (log.contains("Unable to set geometry")) return true;
    return false;
}

bool Dialog::checkAdbRun()
{
    if (m_adb.isRuning()) {
        outLog(tr("请等待当前命令执行完成"));
    }
    return m_adb.isRuning();
}

void Dialog::getIPbyIp()
{
    if (checkAdbRun()) return;
    m_adb.execute(strutil::fromQ(m_settingsDialog ? m_settingsDialog->getSerial() : QString()), {"shell", "ip -o a"});
}

// 历史记录相关（保持兼容，但功能已转移到设置对话框）
void Dialog::loadIpHistory() {}
void Dialog::saveIpHistory(const QString &) {}
void Dialog::loadPortHistory() {}
void Dialog::savePortHistory(const QString &) {}
