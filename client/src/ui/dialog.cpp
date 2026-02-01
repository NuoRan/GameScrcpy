#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QKeyEvent>
#include <QRandomGenerator>
#include <QTime>
#include <QTimer>

#include "config.h"
#include "dialog.h"
#include "ui_dialog.h"
#include "videoform.h"
#include "settingsdialog.h"
#include "terminaldialog.h"

#ifdef Q_OS_WIN32
#include "winutils.h"
#endif

QString s_keyMapPath = "";

const QString &getKeyMapPath()
{
    if (s_keyMapPath.isEmpty()) {
        s_keyMapPath = QString::fromLocal8Bit(qgetenv("QTSCRCPY_KEYMAP_PATH"));
        QFileInfo fileInfo(s_keyMapPath);
        if (s_keyMapPath.isEmpty() || !fileInfo.isDir()) {
            s_keyMapPath = QCoreApplication::applicationDirPath() + "/keymap";
        }
    }
    return s_keyMapPath;
}

// ---------------------------------------------------------
// 构造函数
// ---------------------------------------------------------
Dialog::Dialog(QWidget *parent) : QWidget(parent), ui(new Ui::Widget)
{
    ui->setupUi(this);
    initUI();
    applyModernStyle();

    updateBootConfig(true);
    on_updateDevice_clicked();

    // 自动刷新定时器
    connect(&m_autoUpdatetimer, &QTimer::timeout, this, &Dialog::on_updateDevice_clicked);
    if (ui->autoUpdatecheckBox->isChecked()) {
        m_autoUpdatetimer.start(5000);
    }

    // ADB 进程结果处理
    connect(&m_adb, &qsc::AdbProcess::adbProcessResult, this, [this](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
        QString log = "";
        bool newLine = true;
        QStringList args = m_adb.arguments();

        switch (processResult) {
        case qsc::AdbProcess::AER_ERROR_START:
            break;
        case qsc::AdbProcess::AER_SUCCESS_START:
            log = "adb run";
            newLine = false;
            break;
        case qsc::AdbProcess::AER_ERROR_EXEC:
            if (args.contains("ifconfig") && args.contains("wlan0")) {
                getIPbyIp();
            }
            break;
        case qsc::AdbProcess::AER_ERROR_MISSING_BINARY:
            log = "adb not found";
            break;
        case qsc::AdbProcess::AER_SUCCESS_EXEC:
            if (args.contains("devices")) {
                QStringList devices = m_adb.getDevicesSerialFromStdOut();
                ui->connectedPhoneList->clear();

                // 同步到设置对话框
                if (m_settingsDialog) {
                    m_settingsDialog->setSerialList(devices);
                }

                for (auto &item : devices) {
                    QString nickName = Config::getInstance().getNickName(item);
                    QString displayName = nickName.isEmpty() ? item : nickName + " - " + item;
                    ui->connectedPhoneList->addItem(displayName);
                }
            } else if (args.contains("show") && args.contains("wlan0")) {
                QString ip = m_adb.getDeviceIPFromStdOut();
                if (ip.isEmpty()) {
                    log = "未找到IP";
                    break;
                }
                if (m_settingsDialog) {
                    m_settingsDialog->setDeviceIP(ip);
                }
            } else if (args.contains("ifconfig") && args.contains("wlan0")) {
                QString ip = m_adb.getDeviceIPFromStdOut();
                if (ip.isEmpty()) {
                    log = "未找到IP";
                    break;
                }
                if (m_settingsDialog) {
                    m_settingsDialog->setDeviceIP(ip);
                }
            } else if (args.contains("ip -o a")) {
                QString ip = m_adb.getDeviceIPByIpFromStdOut();
                if (ip.isEmpty()) {
                    log = "未找到IP";
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

    // 设备管理信号
    connect(&qsc::IDeviceManage::getInstance(), &qsc::IDeviceManage::deviceConnected, this, &Dialog::onDeviceConnected);
    connect(&qsc::IDeviceManage::getInstance(), &qsc::IDeviceManage::deviceDisconnected, this, &Dialog::onDeviceDisconnected);
}

Dialog::~Dialog()
{
    updateBootConfig(false);
    qsc::IDeviceManage::getInstance().disconnectAllDevice();
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
    )");
}

// ---------------------------------------------------------
// UI初始化
// ---------------------------------------------------------
void Dialog::initUI()
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("KzScrcpy");

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
    m_settingsDialog->setReverseConnect(config.reverseConnect);
    m_settingsDialog->setShowToolbar(config.showToolbar);
    m_settingsDialog->setFrameless(config.framelessWindow);
    m_settingsDialog->setShowFPS(config.showFPS);

    // 加载历史记录
    m_settingsDialog->setIpHistory(Config::getInstance().getIpHistory());
    m_settingsDialog->setPortHistory(Config::getInstance().getPortHistory());
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
// 设置对话框信号处理
// ---------------------------------------------------------
void Dialog::onStartServer()
{
    if (m_currentSerial.isEmpty()) {
        outLog("错误: 请先选择设备");
        return;
    }

    outLog("正在启动...", false);

    QString maxSizeText = QString::number(m_settingsDialog->getMaxSize());
    quint16 videoSize = maxSizeText.toUShort();

    qsc::DeviceParams params;
    params.serial = m_currentSerial;
    params.maxSize = videoSize;
    params.bitRate = m_settingsDialog->getBitRate();
    params.maxFps = static_cast<quint32>(Config::getInstance().getMaxFps());
    params.renderExpiredFrames = Config::getInstance().getRenderExpiredFrames();
    params.serverLocalPath = getServerPath();
    params.serverRemotePath = Config::getInstance().getServerPath();
    params.pushFilePath = Config::getInstance().getPushFilePath();
    params.gameScript = "";
    params.logLevel = Config::getInstance().getLogLevel();
    params.codecOptions = Config::getInstance().getCodecOptions();
    params.codecName = Config::getInstance().getCodecName();
    params.scid = QRandomGenerator::global()->bounded(1, 10000) & 0x7FFFFFFF;

    qsc::IDeviceManage::getInstance().connectDevice(params);
}

void Dialog::onStopServer()
{
    if (qsc::IDeviceManage::getInstance().disconnectDevice(m_currentSerial)) {
        outLog("已停止服务");
    }
}

void Dialog::onStopAllServers()
{
    qsc::IDeviceManage::getInstance().disconnectAllDevice();
    outLog("已停止所有服务");
}

void Dialog::onWirelessConnect()
{
    if (checkAdbRun()) return;

    QString ip = m_settingsDialog->getDeviceIP();
    QString port = m_settingsDialog->getDevicePort();

    if (ip.isEmpty()) {
        outLog("错误: 请输入IP地址");
        return;
    }

    QString addr = ip;
    if (!port.isEmpty()) {
        addr += ":" + port;
    } else {
        addr += ":5555";
    }

    // 保存历史
    Config::getInstance().saveIpHistory(ip);
    if (!port.isEmpty()) {
        Config::getInstance().savePortHistory(port);
    }

    outLog("正在连接 " + addr + "...", false);
    m_adb.execute("", QStringList() << "connect" << addr);
}

void Dialog::onWirelessDisconnect()
{
    if (checkAdbRun()) return;

    QString addr = m_settingsDialog->getDeviceIP();
    outLog("正在断开...", false);
    m_adb.execute("", QStringList() << "disconnect" << addr);
}

void Dialog::onGetDeviceIP()
{
    if (checkAdbRun()) return;

    outLog("正在获取IP...", false);
    m_adb.execute(m_settingsDialog->getSerial(), QStringList() << "shell" << "ifconfig" << "wlan0");
}

void Dialog::onStartAdbd()
{
    if (checkAdbRun()) return;

    outLog("正在开启ADBD...", false);
    m_adb.execute(m_settingsDialog->getSerial(), QStringList() << "tcpip" << "5555");
}

// ---------------------------------------------------------
// 终端对话框信号处理
// ---------------------------------------------------------
void Dialog::onExecuteCommand(const QString &cmd)
{
    if (checkAdbRun()) return;

    m_terminalDialog->appendOutput("$ adb " + cmd);

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    m_adb.execute(m_settingsDialog ? m_settingsDialog->getSerial() : "", cmd.split(" ", Qt::SkipEmptyParts));
#else
    m_adb.execute(m_settingsDialog ? m_settingsDialog->getSerial() : "", cmd.split(" ", QString::SkipEmptyParts));
#endif
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
            config.reverseConnect = m_settingsDialog->isReverseConnect();
            config.showFPS = m_settingsDialog->showFPS();
            config.framelessWindow = m_settingsDialog->isFrameless();
            config.showToolbar = m_settingsDialog->showToolbar();

            // 保存IP和端口历史
            QString ip = m_settingsDialog->getDeviceIP();
            QString port = m_settingsDialog->getDevicePort();
            if (!ip.isEmpty()) {
                Config::getInstance().saveIpHistory(ip);
            }
            if (!port.isEmpty()) {
                Config::getInstance().savePortHistory(port);
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
    if (checkAdbRun()) return;
    m_adb.execute("", QStringList() << "devices");
}

// ---------------------------------------------------------
// 快捷连接按钮
// ---------------------------------------------------------
void Dialog::on_usbConnectBtn_clicked()
{
    qsc::IDeviceManage::getInstance().disconnectAllDevice();
    delayMs(200);
    on_updateDevice_clicked();
    delayMs(200);

    int firstUsbDevice = findDeviceFromeSerialBox(false);
    if (-1 == firstUsbDevice) {
        outLog("未找到USB设备！");
        return;
    }

    if (m_settingsDialog) {
        m_settingsDialog->setCurrentSerial(ui->connectedPhoneList->item(firstUsbDevice)->text().split(" - ").last());
    }

    onStartServer();
}

void Dialog::on_wifiConnectBtn_clicked()
{
    qsc::IDeviceManage::getInstance().disconnectAllDevice();
    delayMs(200);
    on_updateDevice_clicked();
    delayMs(200);

    int firstUsbDevice = findDeviceFromeSerialBox(false);
    if (-1 == firstUsbDevice) {
        outLog("未找到USB设备！");
        return;
    }

    QString serial = "";
    if (ui->connectedPhoneList->count() > firstUsbDevice) {
        serial = ui->connectedPhoneList->item(firstUsbDevice)->text().split(" - ").last();
    }

    if (m_settingsDialog) {
        m_settingsDialog->setCurrentSerial(serial);
    }

    onGetDeviceIP();
    delayMs(200);

    onStartAdbd();
    delayMs(1000);

    onWirelessConnect();
    delayMs(2000);

    on_updateDevice_clicked();
    delayMs(200);

    int firstWifiDevice = findDeviceFromeSerialBox(true);
    if (-1 == firstWifiDevice) {
        outLog("未找到WiFi设备！");
        return;
    }

    if (m_settingsDialog && ui->connectedPhoneList->count() > firstWifiDevice) {
        m_settingsDialog->setCurrentSerial(ui->connectedPhoneList->item(firstWifiDevice)->text().split(" - ").last());
    }

    onStartServer();
}

int Dialog::findDeviceFromeSerialBox(bool wifi)
{
    QString regStr = "\\b(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\:([0-9]|[1-9]\\d|[1-9]\\d{2}|[1-9]\\d{3}|[1-5]\\d{4}|6[0-4]\\d{3}|65[0-4]\\d{2}|655[0-2]\\d|6553[0-5])\\b";
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QRegExp regIP(regStr);
#else
    QRegularExpression regIP(regStr);
#endif
    for (int i = 0; i < ui->connectedPhoneList->count(); ++i) {
        QString itemText = ui->connectedPhoneList->item(i)->text();
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        bool isWifi = regIP.exactMatch(itemText) || itemText.contains(QRegExp("\\d+\\.\\d+\\.\\d+\\.\\d+"));
#else
        bool isWifi = regIP.match(itemText).hasMatch() || itemText.contains(QRegularExpression("\\d+\\.\\d+\\.\\d+\\.\\d+"));
#endif
        bool found = wifi ? isWifi : !isWifi;
        if (found) {
            return i;
        }
    }
    return -1;
}

void Dialog::on_connectedPhoneList_itemDoubleClicked(QListWidgetItem *item)
{
    QString text = item->text();
    QString serial = text.contains(" - ") ? text.split(" - ").last() : text;

    m_currentSerial = serial;  // 存储当前选中的设备
    onStartServer();
}

// ---------------------------------------------------------
// 设备连接回调
// ---------------------------------------------------------
void Dialog::onDeviceConnected(bool success, const QString &serial, const QString &deviceName, const QSize &size)
{
    Q_UNUSED(deviceName);
    if (!success) return;

    bool frameless = m_settingsDialog ? m_settingsDialog->isFrameless() : false;
    bool showToolbar = m_settingsDialog ? m_settingsDialog->showToolbar() : true;
    bool showFPS = m_settingsDialog ? m_settingsDialog->showFPS() : false;

    auto videoForm = new VideoForm(frameless, Config::getInstance().getSkin(), showToolbar);
    videoForm->setSerial(serial);

    qsc::IDeviceManage::getInstance().getDevice(serial)->setUserData(static_cast<void*>(videoForm));
    qsc::IDeviceManage::getInstance().getDevice(serial)->registerDeviceObserver(videoForm);

    videoForm->showFPS(showFPS);

#ifndef Q_OS_WIN32
    videoForm->show();
#endif
    QString name = Config::getInstance().getNickName(serial);
    if (name.isEmpty()) name = "KzScrcpy";

    videoForm->setWindowTitle(name + " - " + serial);
    videoForm->updateShowSize(size);

    bool deviceVer = size.height() > size.width();
    QRect rc = Config::getInstance().getRect(serial);
    bool rcVer = rc.height() > rc.width();
    if (rc.isValid() && (deviceVer == rcVer)) {
        videoForm->resize(rc.size());
        videoForm->setGeometry(rc);
    }

#ifdef Q_OS_WIN32
    QTimer::singleShot(200, videoForm, [videoForm](){videoForm->show();});
#endif
}

void Dialog::onDeviceDisconnected(QString serial)
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(serial);
    if (!device) return;

    auto data = device->getUserData();
    if (data) {
        VideoForm* vf = static_cast<VideoForm*>(data);
        qsc::IDeviceManage::getInstance().getDevice(serial)->deRegisterDeviceObserver(vf);
        vf->close();
        vf->deleteLater();
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
        serverPath = QString::fromLocal8Bit(qgetenv("QTSCRCPY_SERVER_PATH"));
        QFileInfo fileInfo(serverPath);
        if (serverPath.isEmpty() || !fileInfo.isFile()) {
            serverPath = QCoreApplication::applicationDirPath() + "/scrcpy-server";
        }
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
        outLog("请等待当前命令执行完成");
    }
    return m_adb.isRuning();
}

void Dialog::getIPbyIp()
{
    if (checkAdbRun()) return;
    m_adb.execute(m_settingsDialog ? m_settingsDialog->getSerial() : "", QStringList() << "shell" << "ip -o a");
}

// 历史记录相关（保持兼容，但功能已转移到设置对话框）
void Dialog::loadIpHistory() {}
void Dialog::saveIpHistory(const QString &) {}
void Dialog::loadPortHistory() {}
void Dialog::savePortHistory(const QString &) {}
