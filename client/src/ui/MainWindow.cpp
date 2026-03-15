/**
 * @file MainWindow.cpp
 * @brief 方案 A 主窗口实现 — NavigationView + Page 切换
 *
 * 从旧 Dialog 迁移全部核心逻辑:
 * - ADB 设备管理、信号路由
 * - USB 即插即连、WiFi 自动回连
 * - 系统托盘
 * - 日志输出路由到 TerminalPage
 */

#include "MainWindow.h"

// Fluent 组件
#include "NavigationView.h"
#include "FluentDialog.h"
#include "FluentInfoBar.h"
#include "FluentButton.h"
#include "FluentCard.h"
#include "OnboardingOverlay.h"
#include "HelpDialog.h"
#include "ThemeManager.h"
#include "DesignTokens.h"
#include "MotionTokens.h"

// 页面
#include "HomePage.h"
#include "SettingsPage.h"
#include "TerminalPage.h"

// 旧 UI (VideoForm 暂保留)
#include "videoform.h"

// 核心
#include "config.h"
#include "ScriptEngine.h"
#include "service/DeviceSession.h"
#include "StringUtils.h"
#include "ConfigCenter.h"
#include "hid/TouchRouter.h"  // TouchMethod, methodNeedsServer
#ifdef Q_OS_WIN32
#include "winutils.h"
#endif

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QFile>
#include <QListWidget>
#include <QRandomGenerator>
#include <filesystem>
#include <fstream>
#include <regex>

// 来自 main.cpp 的语言切换函数
extern void installTranslator(const QString &langOverride);

QString s_keyMapPath_mw = "";
static const QString &getKeyMapPath_mw()
{
    if (s_keyMapPath_mw.isEmpty()) {
        s_keyMapPath_mw = QString::fromLocal8Bit(qgetenv("KZSCRCPY_KEYMAP_PATH"));
        if (s_keyMapPath_mw.isEmpty() || !std::filesystem::is_directory(s_keyMapPath_mw.toStdString())) {
            s_keyMapPath_mw = strutil::toQ(strutil::appDirPath() + "/keymap");
        }
    }
    return s_keyMapPath_mw;
}

// ═══════════════════════════════════════════════════════════
// 构造 / 析构
// ═══════════════════════════════════════════════════════════

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    setupNavigation();
    setupPages();
    setupTray();
    setupDeviceWatcher();
    connectSignals();
    applyStyle();

#ifdef Q_OS_WIN32
    WinUtils::setDarkBorderToWindow((HWND)this->winId(), Fluent::ThemeManager::instance().isDarkMode());
#endif

    // 主题切换时更新标题栏
    connect(&Fluent::ThemeManager::instance(), &Fluent::ThemeManager::themeChanged, this, [this](bool isDark) {
        applyStyle();
#ifdef Q_OS_WIN32
        WinUtils::setDarkBorderToWindow((HWND)this->winId(), isDark);
#endif
    });

    // 首次启动立即刷新设备列表
    QTimer::singleShot(0, this, [this]() {
        requestDeviceRefresh(true);
    });

    // 首次使用引导（主界面场景）
    if (!Config::getInstance().getOnboardingCompleted(Config::OB_MAIN_WINDOW)) {
        QTimer::singleShot(500, this, [this]() {
            startOnboarding();
        });
    }
}

MainWindow::~MainWindow()
{
    // 保存配置（从 SettingsPage 同步到 Config）
    UserBootConfig config = Config::getInstance().getUserBootConfig();
    if (m_settingsPage) {
        config.videoCodecIndex = m_settingsPage->getVideoCodecIndex();
        config.maxFps = m_settingsPage->getMaxFps();
        config.bitRate = m_settingsPage->getBitRate();
        config.reverseConnect = m_settingsPage->isReverseConnect();
        config.showToolbar = m_settingsPage->showToolbar();
        config.framelessWindow = m_settingsPage->isFrameless();
        config.showFPS = m_settingsPage->showFPS();
    }
    Config::getInstance().setUserBootConfig(config);

    qsc::IDeviceManage::getInstance().removeDeviceListener(m_deviceConnectedListenerId);
    qsc::IDeviceManage::getInstance().removeDeviceListener(m_deviceDisconnectedListenerId);
    qsc::IDeviceManage::getInstance().disconnectAllDevice();
    m_videoForms.clear();
}

// ═══════════════════════════════════════════════════════════
// UI 构建
// ═══════════════════════════════════════════════════════════

void MainWindow::setupUI()
{
    setWindowTitle(QStringLiteral("GameScrcpy"));
    resize(800, 560);
    setMinimumSize(700, 480);
    setAttribute(Qt::WA_DeleteOnClose);

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 左侧导航
    m_navView = new Fluent::NavigationView(this);
    mainLayout->addWidget(m_navView);

    // 右侧页面容器
    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("pageStack"));
    mainLayout->addWidget(m_stack, 1);
}

void MainWindow::setupNavigation()
{
    using namespace Fluent;

    m_navView->addItem({QStringLiteral("home"),     QStringLiteral(":/icons/home.svg"),     tr("首页")});
    m_navView->addSeparator();
    m_navView->addItem({QStringLiteral("settings"), QStringLiteral(":/icons/settings.svg"), tr("设置"), true});
    m_navView->addItem({QStringLiteral("terminal"), QStringLiteral(":/icons/terminal.svg"), tr("终端"), true});
    m_navView->addItem({QStringLiteral("help"),     QStringLiteral(":/icons/book-open.svg"), tr("帮助"), true});

    connect(m_navView, &NavigationView::itemClicked, this, &MainWindow::onNavItemClicked);
}

void MainWindow::setupPages()
{
    m_homePage = new HomePage(this);
    m_stack->addWidget(m_homePage);

    m_settingsPage = new SettingsPage(this);
    m_stack->addWidget(m_settingsPage);

    m_terminalPage = new TerminalPage(this);
    m_stack->addWidget(m_terminalPage);

    m_stack->setCurrentWidget(m_homePage);
}

void MainWindow::setupTray()
{
}

void MainWindow::setupDeviceWatcher()
{
    // 常驻设备监听 (1.5秒)
    connect(&m_deviceWatchTimer, &QTimer::timeout, this, [this]() {
        requestDeviceRefresh(false);
    });
    m_deviceWatchTimer.start(1500);

    // 自动刷新 (5秒)
    connect(&m_autoUpdateTimer, &QTimer::timeout, this, [this]() {
        requestDeviceRefresh(true);
    });
}

void MainWindow::connectSignals()
{
    // 设备管理回调 (替代原 Qt signal/slot)
    // CRITICAL: connected.fire() runs synchronously inside KcpServer timer callback.
    // Creating & showing widgets (FluentInfoBar, VideoForm) during that synchronous
    // chain triggers Qt text-layout which crashes (NULL font engine at Qt6Gui+0x35B093).
    // Fix: Defer widget creation to the next event-loop iteration via QTimer::singleShot.
    m_deviceConnectedListenerId = qsc::IDeviceManage::getInstance().addDeviceConnectedListener(
        [this](bool success, const std::string& serial, const std::string& deviceName, const Size& size) {
            // Capture by value — the Signal::fire() call stack will unwind before we use these.
            QString qSerial = strutil::toQ(serial);
            QString qDeviceName = strutil::toQ(deviceName);
            QSize  qSize = typeconv::toQ(size);
            QTimer::singleShot(0, this, [this, success, qSerial, qDeviceName, qSize]() {
                onDeviceConnected(success, qSerial, qDeviceName, qSize);
            });
        });
    m_deviceDisconnectedListenerId = qsc::IDeviceManage::getInstance().addDeviceDisconnectedListener(
        [this](const std::string& serial) {
            QString qSerial = strutil::toQ(serial);
            QTimer::singleShot(0, this, [this, qSerial]() {
                onDeviceDisconnected(qSerial);
            });
        });

    // WiFi 设置 ADB 进程结果 (USB→WiFi 切换流程)
    m_wifiSetupAdb.adbProcessResult.connect([this](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
        onWifiSetupResult(processResult);
    });

    // ADB 进程结果
    m_adb.adbProcessResult.connect([this](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
        auto rawArgs = m_adb.arguments();
        QStringList args;
        for (const auto& a : rawArgs) args << strutil::toQ(a);
        const QString stdOut = strutil::toQ(m_adb.getStdOut());
        const QString errOut = strutil::toQ(m_adb.getErrorOut());

        switch (processResult) {
        case qsc::AdbProcess::AER_ERROR_START:
            break;
        case qsc::AdbProcess::AER_SUCCESS_START:
            break;
        case qsc::AdbProcess::AER_ERROR_EXEC:
            if (args.contains("connect")) {
                if (m_wifiConnectMode == WifiConnectMode::Manual) {
                    const QString detail = errOut.isEmpty() ? tr("连接失败") : errOut;
                    Fluent::FluentDialog::error(this, tr("WiFi连接失败"),
                        tr("设备地址: %1\n%2").arg(m_currentWifiAddr, detail));
                    m_wifiConnectMode = WifiConnectMode::None;
                    m_currentWifiAddr.clear();
                } else if (m_wifiConnectMode == WifiConnectMode::AutoReconnect) {
                    tryNextAutoReconnect();
                }
                requestDeviceRefresh(true);
            }
            if (args.contains("ifconfig") && args.contains("wlan0")) {
                // fallback to ip command
                if (!checkAdbRun()) {
                    m_adb.execute("", {"shell", "ip -o a"});
                }
            }
            break;
        case qsc::AdbProcess::AER_ERROR_MISSING_BINARY:
            Fluent::FluentDialog::error(this, tr("ADB不可用"), tr("未找到 adb 可执行文件，请检查环境配置。"));
            break;
        case qsc::AdbProcess::AER_SUCCESS_EXEC:
            if (args.contains("devices")) {
                auto rawDevices = m_adb.getDevicesSerialFromStdOut();
                QStringList devices;
                for (const auto& d : rawDevices) devices << strutil::toQ(d);

                // 通知 HomePage 更新设备列表
                if (m_homePage) {
                    m_homePage->updateDeviceList(devices);
                }
                emit deviceListUpdated(devices);

                // 手动 WiFi 连接后查找设备
                if (m_pendingManualWifiStart) {
                    const QString wifiSerial = findWifiSerialByAddr(m_currentWifiAddr, devices);
                    if (!wifiSerial.isEmpty()) {
                        m_pendingManualWifiStart = false;
                        m_pendingManualWifiRefreshRetry = 0;
                        m_wifiConnectMode = WifiConnectMode::None;
                        m_currentWifiAddr.clear();
                        if (!m_videoForms.contains(wifiSerial)) {
                            tryStartServerForSerial(wifiSerial);
                        }
                    } else {
                        ++m_pendingManualWifiRefreshRetry;
                        if (m_pendingManualWifiRefreshRetry <= 5) {
                            Fluent::FluentInfoBar::info(this,
                                tr("正在查找设备... (重试 %1/5)").arg(m_pendingManualWifiRefreshRetry));
                            QTimer::singleShot(300, this, [this]() { requestDeviceRefresh(true); });
                        } else {
                            m_pendingManualWifiStart = false;
                            m_pendingManualWifiRefreshRetry = 0;
                            Fluent::FluentDialog::error(this, tr("WiFi连接失败"),
                                tr("设备已连接到 adb，但未在设备列表中出现: %1").arg(m_currentWifiAddr));
                            m_wifiConnectMode = WifiConnectMode::None;
                            m_currentWifiAddr.clear();
                        }
                    }
                }

                // USB 手动连接 (不再自动即插即连)
                if (m_pendingUsbConnect) {
                    m_pendingUsbConnect = false;
                    bool found = false;
                    for (const auto& serial : devices) {
                        if (!isWifiSerial(serial)) {
                            tryStartServerForSerial(serial);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        Fluent::FluentDialog::warning(this, tr("USB连接失败"), tr("未检测到可用的 USB 设备。"));
                    }
                }

                if (!m_hasInitialDeviceScan) {
                    m_hasInitialDeviceScan = true;
                    // 不再自动回连历史 WiFi 设备
                }
                m_lastDeviceSerials = devices;

            } else if (args.contains("connect")) {
                const bool connectedOk = stdOut.contains("connected to", Qt::CaseInsensitive)
                                         || stdOut.contains("already connected", Qt::CaseInsensitive);
                if (m_wifiConnectMode == WifiConnectMode::Manual) {
                    if (connectedOk) {
                        Fluent::FluentInfoBar::info(this, tr("已连接，正在查找设备..."));
                        m_pendingManualWifiStart = true;
                        m_pendingManualWifiRefreshRetry = 0;
                    } else {
                        const QString detail = errOut.isEmpty() ? stdOut : errOut;
                        Fluent::FluentDialog::error(this, tr("WiFi连接失败"),
                            tr("设备地址: %1\n%2").arg(m_currentWifiAddr, detail));
                        m_wifiConnectMode = WifiConnectMode::None;
                        m_currentWifiAddr.clear();
                    }
                    requestDeviceRefresh(true);
                } else if (m_wifiConnectMode == WifiConnectMode::AutoReconnect) {
                    if (connectedOk) {
                        outLog(tr("已自动回连 WiFi 设备: %1").arg(m_currentWifiAddr));
                        Fluent::FluentInfoBar::success(this, tr("WiFi设备已回连: %1").arg(m_currentWifiAddr));
                        m_wifiConnectMode = WifiConnectMode::None;
                        m_currentWifiAddr.clear();
                        requestDeviceRefresh(true);
                    } else {
                        tryNextAutoReconnect();
                    }
                }
            } else if (args.contains("disconnect")) {
                outLog(tr("WiFi设备已断开"));
            } else if ((args.contains("show") || args.contains("ifconfig")) && args.contains("wlan0")) {
                QString ip = strutil::toQ(m_adb.getDeviceIPFromStdOut());
                if (!ip.isEmpty() && m_homePage) {
                    m_homePage->setDeviceIP(ip);
                }
            } else if (args.contains("ip -o a")) {
                QString ip = strutil::toQ(m_adb.getDeviceIPByIpFromStdOut());
                if (!ip.isEmpty() && m_homePage) {
                    m_homePage->setDeviceIP(ip);
                }
            }
            break;
        }
    });

    // HomePage 信号
    connect(m_homePage, &HomePage::requestUsbConnect, this, &MainWindow::onRequestUsbConnect);
    connect(m_homePage, &HomePage::requestWifiConnect, this, &MainWindow::onRequestWifiConnect);
    connect(m_homePage, &HomePage::requestRefresh, this, &MainWindow::onRequestRefresh);
    connect(m_homePage, &HomePage::requestDeviceConnect, this, &MainWindow::onRequestDeviceConnect);

    // TerminalPage 信号
    connect(m_terminalPage, &TerminalPage::executeCommand, this, &MainWindow::onExecuteCommand);
    connect(m_terminalPage, &TerminalPage::stopCommand, this, &MainWindow::onStopCommand);

    // SettingsPage 信号
    connect(m_settingsPage, &SettingsPage::wirelessConnect, this, &MainWindow::onRequestWifiConnect);
    connect(m_settingsPage, &SettingsPage::wirelessDisconnect, this, [this]() {
        m_adb.execute("", {"disconnect"});
    });
    connect(m_settingsPage, &SettingsPage::requestDeviceIP, this, [this]() {
        if (!checkAdbRun()) {
            m_adb.execute("", {"shell", "ifconfig", "wlan0"});
        }
    });
    connect(m_settingsPage, &SettingsPage::startAdbd, this, &MainWindow::onRequestStartAdbd);
    connect(m_settingsPage, &SettingsPage::touchModeChanged, this, [this](int mode) {
        // 通知首页动态更新直连条目
        if (m_homePage) m_homePage->onTouchMethodChanged(mode);
    });
    connect(m_homePage, &HomePage::requestDirectConnect, this, &MainWindow::onRequestDirectConnect);

    // 初始化首页直连条目状态
    int initTouchMethod = qsc::ConfigCenter::instance().get<int>("user/touchMode", 0);
    m_homePage->onTouchMethodChanged(initTouchMethod);
    connect(m_settingsPage, &SettingsPage::restartOnboarding, this, [this]() {
        Config::getInstance().resetAllOnboarding();
        m_stack->setCurrentWidget(m_homePage);   // 回到首页再启动引导
        startOnboarding();

        // 已打开的投屏窗口也重新引导
        for (auto* vf : m_videoForms) {
            if (vf) vf->restartOnboarding();
        }
    });
}

void MainWindow::applyStyle()
{
    auto& tm = Fluent::ThemeManager::instance();
    setStyleSheet(QStringLiteral("#pageStack { background-color: %1; }").arg(tm.base()));
}

// ═══════════════════════════════════════════════════════════
// 导航
// ═══════════════════════════════════════════════════════════

void MainWindow::onNavItemClicked(const QString& id)
{
    if (id == "home")     m_stack->setCurrentWidget(m_homePage);
    else if (id == "settings") {
        if (m_settingsPage) {
            // 先同步外部变更（如 VideoSettingsPopup），再保存当前 UI 状态
            m_settingsPage->syncFromConfig();
            m_settingsPage->saveToConfig();
        }
        m_stack->setCurrentWidget(m_settingsPage);
    }
    else if (id == "terminal") m_stack->setCurrentWidget(m_terminalPage);
    else if (id == "help") {
        // 打开综合帮助中心对话框
        if (!m_helpDialog) {
            m_helpDialog = new HelpDialog(this);
            connect(m_helpDialog, &QDialog::destroyed, this, [this]() { m_helpDialog = nullptr; });
        }
        m_helpDialog->show();
        m_helpDialog->raise();
        m_helpDialog->activateWindow();
    }
}

// ═══════════════════════════════════════════════════════════
// 设备操作 (来自 HomePage)
// ═══════════════════════════════════════════════════════════

void MainWindow::onRequestUsbConnect()
{
    m_pendingUsbConnect = true;
    Fluent::FluentInfoBar::info(this, tr("正在搜索 USB 设备..."));
    requestDeviceRefresh(true);
}

void MainWindow::onRequestWifiConnect(const QString& address)
{
    if (m_adb.isRuning() || m_wifiSetupAdb.isRuning()) {
        Fluent::FluentInfoBar::warning(this, tr("请等待当前命令完成"));
        return;
    }

    // 如果没有指定地址，且当前有 USB 设备 → 启动 USB→WiFi 切换流程
    if (address.isEmpty() && !m_lastDeviceSerials.isEmpty()) {
        // 优先使用选中的 USB 设备，否则取第一个 USB 设备
        QString usbSerial;
        for (const auto& serial : m_lastDeviceSerials) {
            if (!isWifiSerial(serial)) {
                usbSerial = serial;
                break;
            }
        }
        if (!usbSerial.isEmpty()) {
            startUsbToWifiFlow(usbSerial);
            return;
        }
    }

    // 没有 USB 设备或指定了地址 → 手动输入 IP 的原有流程
    QString addr = address;
    if (addr.isEmpty() || addr == QLatin1String("manual_input")) {
        // WiFi 按钮点击 — 弹出输入框
        addr = Fluent::FluentDialog::input(this, tr("WiFi 连接"), tr("请输入设备IP地址 (例: 192.168.1.100)"),
                                           Config::getInstance().getIpHistory().empty() ? QString() : strutil::toQ(Config::getInstance().getIpHistory().front()));
        if (addr.isEmpty()) return;
    }
    if (!addr.contains(':')) {
        addr += ":5555";
    }

    // 保存历史
    QString ip = addr.section(':', 0, 0);
    QString port = addr.section(':', 1);
    Config::getInstance().saveIpHistory(strutil::fromQ(ip));
    if (!port.isEmpty()) {
        Config::getInstance().savePortHistory(strutil::fromQ(port));
    }

    m_wifiConnectMode = WifiConnectMode::Manual;
    m_currentWifiAddr = addr;

    Fluent::FluentInfoBar::info(this, tr("WiFi 连接中: %1...").arg(addr));
    outLog(tr("正在连接 %1...").arg(addr), false);
    m_adb.execute("", {"connect", strutil::fromQ(addr)});
}

void MainWindow::onRequestRefresh()
{
    requestDeviceRefresh(true);
}

void MainWindow::onRequestDeviceConnect(const QString& serial)
{
    tryStartServerForSerial(serial);
}

void MainWindow::onRequestDirectConnect()
{
    // AOA/ESP32 直连: 不需要 ADB, 用虚拟 serial 走纯触控路径
    int touchMethodIdx = qsc::ConfigCenter::instance().get<int>("user/touchMode", 0);
    auto tm = static_cast<TouchMethod>(touchMethodIdx);

    QString pseudoSerial;
    if (methodUsesAoa(tm))
        pseudoSerial = QStringLiteral("AOA-DIRECT");
    else if (methodUsesEsp32(tm))
        pseudoSerial = QStringLiteral("ESP32-DIRECT");
    else
        return;

    if (m_videoForms.contains(pseudoSerial)) {
        Fluent::FluentInfoBar::warning(this, tr("已有活跃的直连会话"));
        return;
    }

    // 直连 = 纯触控，不管视频开关都跳过 server（没有 ADB 就没法传视频）
    Fluent::FluentInfoBar::info(this, tr("正在启动直连会话 (纯触控)..."));
    m_currentSerial = pseudoSerial;

    qsc::DeviceParams params;
    params.serial = strutil::fromQ(pseudoSerial);
    params.maxSize = 0;
    params.bitRate = 0;
    params.maxFps = 0;
    params.renderExpiredFrames = false;
    params.serverLocalPath = "";
    params.serverRemotePath = "";
    params.gameScript = "";
    params.logLevel = "";
    params.codecOptions = "";
    params.codecName = "";
    params.videoCodec = "";
    params.scid = 0;
    // 临时关闭视频通道，确保走纯触控路径
    bool savedVideo = qsc::ConfigCenter::instance().get<bool>("user/videoChannelEnabled", true);
    qsc::ConfigCenter::instance().set("user/videoChannelEnabled", false);
    qsc::IDeviceManage::getInstance().connectDevice(params);
    // 恢复视频通道设置（不影响用户配置）
    qsc::ConfigCenter::instance().set("user/videoChannelEnabled", savedVideo);
}

void MainWindow::onRequestWifiDisconnect()
{
    if (checkAdbRun()) return;
    // TODO: get address from homePage
    outLog(tr("正在断开..."), false);
    m_adb.execute("", {"disconnect"});
    QTimer::singleShot(300, this, [this]() { requestDeviceRefresh(true); });
}

void MainWindow::onRequestGetDeviceIP()
{
    if (checkAdbRun()) return;
    outLog(tr("正在获取IP..."), false);
    m_adb.execute(strutil::fromQ(m_currentSerial), {"shell", "ifconfig", "wlan0"});
}

void MainWindow::onRequestStartAdbd()
{
    if (checkAdbRun()) return;
    outLog(tr("正在开启ADBD..."), false);
    m_adb.execute(strutil::fromQ(m_currentSerial), {"tcpip", "5555"});
}

// ═══════════════════════════════════════════════════════════
// 终端部分
// ═══════════════════════════════════════════════════════════

void MainWindow::onExecuteCommand(const QString& cmd)
{
    if (checkAdbRun()) return;
    if (m_terminalPage) {
        m_terminalPage->appendOutput("$ adb " + cmd);
    }
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    auto parts = cmd.split(" ", Qt::SkipEmptyParts);
#else
    auto parts = cmd.split(" ", QString::SkipEmptyParts);
#endif
    std::vector<std::string> cmdArgs;
    for (const auto& p : parts) cmdArgs.push_back(strutil::fromQ(p));
    m_adb.execute(strutil::fromQ(m_currentSerial), cmdArgs);
}

void MainWindow::onStopCommand()
{
    m_adb.kill();
}

// ═══════════════════════════════════════════════════════════
// 设备连接回调
// ═══════════════════════════════════════════════════════════

void MainWindow::onDeviceConnected(bool success, const QString& serial, const QString& deviceName, const QSize& size)
{
    Q_UNUSED(deviceName);
    qInfo("[MainWindow] onDeviceConnected: success=%d, serial=%s, size=%dx%d",
          success, qPrintable(serial), size.width(), size.height());
    if (!success) {
        Fluent::FluentDialog::error(this, tr("连接失败"),
            tr("设备 %1 连接失败，请检查设备授权与连接状态。").arg(serial));
        return;
    }

    // 防止竞态：server 在 connected.fire() 后立刻崩溃导致 session 已被销毁
    auto* sessionCheck = qsc::IDeviceManage::getInstance().getSession(strutil::fromQ(serial));
    if (!sessionCheck) {
        qWarning("[MainWindow] Device %s already disconnected, skipping VideoForm creation", qPrintable(serial));
        return;
    }

    Fluent::FluentInfoBar::success(this, tr("设备已连接: %1").arg(serial));

    UserBootConfig bootCfg = Config::getInstance().getUserBootConfig();
    bool frameless = bootCfg.framelessWindow;
    bool showToolbar = bootCfg.showToolbar;
    bool showFPS = bootCfg.showFPS;

    qInfo("[MainWindow] Creating VideoForm: frameless=%d, toolbar=%d, fps=%d",
          frameless, showToolbar, showFPS);
    auto videoForm = new VideoForm(frameless, Config::getInstance().getSkin(), showToolbar);

    auto* session = qsc::IDeviceManage::getInstance().getSession(strutil::fromQ(serial));
    if (session) {
        videoForm->bindSession(session);
    }

    m_videoForms[serial] = videoForm;
    videoForm->setSerial(serial);
    videoForm->showFPS(showFPS);

    QString name = strutil::toQ(Config::getInstance().getNickName(strutil::fromQ(serial)));
    if (name.isEmpty()) name = "GameScrcpy";

    videoForm->setWindowTitle(name + " - " + serial);
    videoForm->updateShowSize(size);
    videoForm->restoreWindowGeometry();

    // 纯触控模式: 视频关闭 且 触控方式不需 server → 启用虚拟画布
    int touchMethodIdx = qsc::ConfigCenter::instance().get<int>("user/touchMode", 0);
    auto touchMethod = static_cast<TouchMethod>(touchMethodIdx);
    bool videoEnabled = qsc::ConfigCenter::instance().get<bool>("user/videoChannelEnabled", true);
    if (!videoEnabled && !methodNeedsServer(touchMethod)) {
        videoForm->enableVirtualCanvas(QSize(size.width(), size.height()));
    }

    videoForm->show();
    qInfo("[MainWindow] onDeviceConnected done");
}

void MainWindow::onDeviceDisconnected(const QString& serial)
{
    auto it = m_videoForms.find(serial);
    if (it == m_videoForms.end()) return;

    VideoForm* vf = it.value();
    m_videoForms.erase(it);

    if (vf) {
        vf->bindSession(nullptr);
        vf->close();
    }

    Fluent::FluentInfoBar::warning(this, tr("设备已断开: %1").arg(serial));
}

// ═══════════════════════════════════════════════════════════
// 设备管理工具方法
// ═══════════════════════════════════════════════════════════

bool MainWindow::checkAdbRun()
{
    if (m_adb.isRuning()) {
        outLog(tr("请等待当前命令执行完成"));
    }
    return m_adb.isRuning();
}

void MainWindow::requestDeviceRefresh(bool force)
{
    if (!force && m_adb.isRuning()) return;
    if (force && m_adb.isRuning()) {
        // ADB 忙时延迟重试，而不是静默失败
        QTimer::singleShot(500, this, [this]() { requestDeviceRefresh(true); });
        return;
    }
    m_adb.execute("", {"devices"});
}

void MainWindow::tryStartServerForSerial(const QString& serial)
{
    if (serial.isEmpty()) return;
    m_currentSerial = serial;

    // 先从 Config 同步到设置页 UI（VideoSettingsPopup 可能已修改配置），再保存
    if (m_settingsPage) {
        m_settingsPage->syncFromConfig();
        m_settingsPage->saveToConfig();
    }

    Fluent::FluentInfoBar::info(this, tr("正在启动会话: %1...").arg(serial));
    outLog(tr("正在连接设备: %1").arg(serial));

    // 从配置和设置页获取参数
    UserBootConfig bootCfg = Config::getInstance().getUserBootConfig();

    qsc::DeviceParams params;
    params.serial = strutil::fromQ(serial);
    params.maxSize = m_settingsPage ? m_settingsPage->getMaxSize() : 0;
    params.bitRate = m_settingsPage ? m_settingsPage->getBitRate() : bootCfg.bitRate;
    params.maxFps = m_settingsPage ? m_settingsPage->getMaxFps() : bootCfg.maxFps;
    params.renderExpiredFrames = Config::getInstance().getRenderExpiredFrames();
    params.serverLocalPath = strutil::fromQ(getServerPath());
    params.serverRemotePath = Config::getInstance().getServerPath();
    params.gameScript = "";
    params.logLevel = Config::getInstance().getLogLevel();
    params.codecOptions = Config::getInstance().getCodecOptions();
    params.codecName = Config::getInstance().getCodecName();
    params.videoCodec = m_settingsPage ? strutil::fromQ(m_settingsPage->getVideoCodecName()) : std::string();
    params.scid = QRandomGenerator::global()->bounded(1, 10000) & 0x7FFFFFFF;

    qsc::IDeviceManage::getInstance().connectDevice(params);
}

bool MainWindow::isWifiSerial(const QString& serial) const
{
    static const std::regex regIP("\\b(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\:([0-9]|[1-9]\\d|[1-9]\\d{2}|[1-9]\\d{3}|[1-5]\\d{4}|6[0-4]\\d{3}|65[0-4]\\d{2}|655[0-2]\\d|6553[0-5])\\b");
    static const std::regex regSimpleIP("\\d+\\.\\d+\\.\\d+\\.\\d+");
    std::string serialStd = serial.toStdString();
    return std::regex_match(serialStd, regIP) || std::regex_search(serialStd, regSimpleIP);
}

QString MainWindow::extractSerialFromItemText(const QString& text) const
{
    return text.contains(" - ") ? text.split(" - ").last() : text;
}

QString MainWindow::findWifiSerialByAddr(const QString& addr, const QStringList& devices) const
{
    if (addr.isEmpty()) return {};
    const QString ip = addr.section(':', 0, 0);
    for (const auto& serial : devices) {
        if (!isWifiSerial(serial)) continue;
        if (serial == addr) return serial;
        if (!ip.isEmpty() && serial.startsWith(ip + ":")) return serial;
    }
    return {};
}

// ═══════════════════════════════════════════════════════════
// USB → WiFi 切换流程
// ═══════════════════════════════════════════════════════════

void MainWindow::startUsbToWifiFlow(const QString& usbSerial)
{
    m_wifiSetupStep = WifiSetupStep::TcpipSent;
    m_wifiSetupSerial = usbSerial;

    Fluent::FluentInfoBar::info(this, tr("正在为 %1 开启无线调试...").arg(usbSerial));
    outLog(tr("USB → WiFi: 正在开启 tcpip 5555 (%1)").arg(usbSerial));

    m_wifiSetupAdb.execute(strutil::fromQ(usbSerial), {"tcpip", "5555"});
}

void MainWindow::onWifiSetupResult(qsc::AdbProcess::ADB_EXEC_RESULT result)
{
    const QString stdOut = strutil::toQ(m_wifiSetupAdb.getStdOut());
    const QString errOut = strutil::toQ(m_wifiSetupAdb.getErrorOut());

    switch (m_wifiSetupStep) {
    case WifiSetupStep::TcpipSent: {
        if (result == qsc::AdbProcess::AER_SUCCESS_EXEC) {
            // tcpip 成功 → 等待 adbd 重启后再获取 IP
            // adb tcpip 5555 会重启 adbd，导致 USB 连接瞬断，延迟 2 秒等待设备重新上线
            outLog(tr("USB → WiFi: tcpip 已开启，等待设备重新上线..."));
            m_wifiSetupStep = WifiSetupStep::GettingIP;
            QTimer::singleShot(2000, this, [this]() {
                outLog(tr("USB → WiFi: 正在获取设备 IP..."));
                m_wifiSetupAdb.execute(strutil::fromQ(m_wifiSetupSerial), {"shell", "ifconfig", "wlan0"});
            });
        } else {
            const QString detail = errOut.isEmpty() ? stdOut : errOut;
            Fluent::FluentDialog::error(this, tr("WiFi切换失败"),
                tr("无法开启无线调试模式。\n设备: %1\n%2").arg(m_wifiSetupSerial, detail));
            m_wifiSetupStep = WifiSetupStep::None;
            m_wifiSetupSerial.clear();
        }
        break;
    }
    case WifiSetupStep::GettingIP: {
        if (result == qsc::AdbProcess::AER_SUCCESS_EXEC) {
            QString ip = strutil::toQ(m_wifiSetupAdb.getDeviceIPFromStdOut());
            if (!ip.isEmpty()) {
                // 成功获取 IP → 连接
                const QString addr = ip + ":5555";
                outLog(tr("USB → WiFi: 设备 IP = %1，正在连接...").arg(ip));
                Fluent::FluentInfoBar::info(this, tr("设备 IP: %1，WiFi 连接中...").arg(ip));

                Config::getInstance().saveIpHistory(strutil::fromQ(ip));

                m_wifiSetupStep = WifiSetupStep::Connecting;
                m_currentWifiAddr = addr;
                m_wifiSetupAdb.execute("", {"connect", strutil::fromQ(addr)});
            } else {
                // ifconfig 没拿到 → 回退到 ip 命令
                outLog(tr("USB → WiFi: ifconfig 未获取到 IP，尝试 ip 命令..."));
                m_wifiSetupStep = WifiSetupStep::GettingIPFallback;
                m_wifiSetupAdb.execute(strutil::fromQ(m_wifiSetupSerial), {"shell", "ip", "-o", "a"});
            }
        } else {
            // ifconfig 失败 → 回退到 ip 命令
            outLog(tr("USB → WiFi: ifconfig 失败，尝试 ip 命令..."));
            m_wifiSetupStep = WifiSetupStep::GettingIPFallback;
            m_wifiSetupAdb.execute(strutil::fromQ(m_wifiSetupSerial), {"shell", "ip", "-o", "a"});
        }
        break;
    }
    case WifiSetupStep::GettingIPFallback: {
        if (result == qsc::AdbProcess::AER_SUCCESS_EXEC) {
            QString ip = strutil::toQ(m_wifiSetupAdb.getDeviceIPByIpFromStdOut());
            if (!ip.isEmpty()) {
                const QString addr = ip + ":5555";
                outLog(tr("USB → WiFi: 设备 IP = %1，正在连接...").arg(ip));
                Fluent::FluentInfoBar::info(this, tr("设备 IP: %1，WiFi 连接中...").arg(ip));

                Config::getInstance().saveIpHistory(strutil::fromQ(ip));

                m_wifiSetupStep = WifiSetupStep::Connecting;
                m_currentWifiAddr = addr;
                m_wifiSetupAdb.execute("", {"connect", strutil::fromQ(addr)});
            } else {
                // 两种方式都没拿到 IP → 弹输入框让用户手动输入
                Fluent::FluentInfoBar::warning(this, tr("无法自动获取设备 IP，请手动输入"));
                m_wifiSetupStep = WifiSetupStep::None;
                m_wifiSetupSerial.clear();
                // 回退到手动输入流程
                onRequestWifiConnect(QString("manual_input"));
            }
        } else {
            Fluent::FluentInfoBar::warning(this, tr("无法自动获取设备 IP，请手动输入"));
            m_wifiSetupStep = WifiSetupStep::None;
            m_wifiSetupSerial.clear();
            onRequestWifiConnect(QString("manual_input"));
        }
        break;
    }
    case WifiSetupStep::Connecting: {
        if (result == qsc::AdbProcess::AER_SUCCESS_EXEC) {
            const bool connectedOk = stdOut.contains("connected to", Qt::CaseInsensitive)
                                     || stdOut.contains("already connected", Qt::CaseInsensitive);
            if (connectedOk) {
                Fluent::FluentInfoBar::success(this, tr("WiFi 已连接: %1").arg(m_currentWifiAddr));
                outLog(tr("USB → WiFi: 已成功连接 %1").arg(m_currentWifiAddr));

                // 刷新设备列表并自动启动会话
                m_wifiConnectMode = WifiConnectMode::Manual;
                m_pendingManualWifiStart = true;
                m_pendingManualWifiRefreshRetry = 0;
                requestDeviceRefresh(true);
            } else {
                const QString detail = errOut.isEmpty() ? stdOut : errOut;
                Fluent::FluentDialog::error(this, tr("WiFi连接失败"),
                    tr("设备 IP: %1\n%2").arg(m_currentWifiAddr, detail));
                m_currentWifiAddr.clear();
            }
        } else {
            const QString detail = errOut.isEmpty() ? stdOut : errOut;
            Fluent::FluentDialog::error(this, tr("WiFi连接失败"),
                tr("设备 IP: %1\n%2").arg(m_currentWifiAddr, detail));
            m_currentWifiAddr.clear();
        }
        m_wifiSetupStep = WifiSetupStep::None;
        m_wifiSetupSerial.clear();
        break;
    }
    default:
        break;
    }
}

// ═══════════════════════════════════════════════════════════
// WiFi 自动重连
// ═══════════════════════════════════════════════════════════

void MainWindow::startAutoReconnectFromHistory()
{
    if (m_hasTriedAutoReconnect) return;
    m_hasTriedAutoReconnect = true;

    for (const auto& serial : m_lastDeviceSerials) {
        if (isWifiSerial(serial)) return;
    }

    const auto ips = Config::getInstance().getIpHistory();
    if (ips.empty()) return;

    const auto ports = Config::getInstance().getPortHistory();
    const QString port = ports.empty() ? QStringLiteral("5555") : strutil::toQ(ports.front());

    m_autoReconnectQueue.clear();
    for (const auto& ip : ips) {
        if (!ip.empty()) {
            m_autoReconnectQueue << (strutil::toQ(ip) + ":" + port);
        }
        if (m_autoReconnectQueue.size() >= 3) break;
    }

    if (m_autoReconnectQueue.isEmpty()) return;
    m_autoReconnectIndex = 0;
    tryNextAutoReconnect();
}

void MainWindow::tryNextAutoReconnect()
{
    if (m_adb.isRuning()) {
        QTimer::singleShot(200, this, [this]() { tryNextAutoReconnect(); });
        return;
    }
    if (m_autoReconnectIndex >= m_autoReconnectQueue.size()) {
        m_wifiConnectMode = WifiConnectMode::None;
        m_currentWifiAddr.clear();
        return;
    }

    m_wifiConnectMode = WifiConnectMode::AutoReconnect;
    m_currentWifiAddr = m_autoReconnectQueue[m_autoReconnectIndex++];
    outLog(tr("自动回连WiFi设备: %1").arg(m_currentWifiAddr));
    m_adb.execute("", {"connect", strutil::fromQ(m_currentWifiAddr)});
}

quint32 MainWindow::getBitRate()
{
    return m_settingsPage ? m_settingsPage->getBitRate() : 8000000;
}

const QString& MainWindow::getServerPath()
{
    static QString serverPath;
    if (serverPath.isEmpty()) {
        serverPath = QString::fromLocal8Bit(qgetenv("KZSCRCPY_SERVER_PATH"));
        if (!serverPath.isEmpty() && std::filesystem::is_regular_file(serverPath.toStdString())) return serverPath;

        std::string externalStd = strutil::appDirPath() + "/scrcpy-server";
        if (std::filesystem::exists(externalStd)) {
            serverPath = strutil::toQ(externalStd);
            return serverPath;
        }

        std::string tempDir = std::filesystem::temp_directory_path().string();
        std::string extractedStd = tempDir + "/kzscrcpy-server";
        QString extractedPath = QString::fromStdString(extractedStd);

        QFile resourceFile(":/scrcpy-server");
        bool needExtract = true;
        if (std::filesystem::exists(extractedStd) && resourceFile.open(QIODevice::ReadOnly)) {
            qint64 resourceSize = resourceFile.size();
            resourceFile.close();
            if (static_cast<uintmax_t>(resourceSize) == std::filesystem::file_size(extractedStd)) needExtract = false;
        }
        if (needExtract) {
            if (resourceFile.open(QIODevice::ReadOnly)) {
                QByteArray data = resourceFile.readAll();
                resourceFile.close();
                std::ofstream ofs(extractedStd, std::ios::binary);
                if (ofs) {
                    ofs.write(data.constData(), data.size());
                    ofs.close();
                }
            }
        }
        serverPath = extractedPath;
    }
    return serverPath;
}

// ═══════════════════════════════════════════════════════════
// 日志 / i18n / 事件
// ═══════════════════════════════════════════════════════════

void MainWindow::outLog(const QString& log, bool newLine)
{
    if (m_terminalPage) {
        m_terminalPage->appendOutput(log);
        if (newLine) m_terminalPage->appendOutput("");
    }
}

bool MainWindow::filterLog(const QString& log)
{
    if (log.contains("app_proces")) return true;
    if (log.contains("Unable to set geometry")) return true;
    return false;
}

void MainWindow::retranslateUi()
{
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QWidget::changeEvent(event);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    qApp->quit();
}

// ═══════════════════════════════════════════════════════════
// 引导系统
// ═══════════════════════════════════════════════════════════

void MainWindow::startOnboarding()
{
    if (m_onboarding) {
        m_onboarding->deleteLater();
        m_onboarding = nullptr;
    }

    using Fluent::OnboardingStep;
    using Fluent::OnboardingOverlay;

    m_onboarding = new OnboardingOverlay(this);

    // ── 页面导航辅助 lambda ──
    auto goHome = [this]() {
        m_stack->setCurrentWidget(m_homePage);
        if (m_navView) m_navView->setCurrentItem(QStringLiteral("home"));
    };
    auto goSettings = [this]() {
        m_stack->setCurrentWidget(m_settingsPage);
        if (m_navView) m_navView->setCurrentItem(QStringLiteral("settings"));
    };
    auto goTerminal = [this]() {
        m_stack->setCurrentWidget(m_terminalPage);
        if (m_navView) m_navView->setCurrentItem(QStringLiteral("terminal"));
    };

    QVector<OnboardingStep> steps;

    // ── 步骤 1: 欢迎 ──
    steps.append({nullptr,
        tr("欢迎使用 GameScrcpy!"),
        tr("安卓投屏与游戏控制工具。\n"
           "支持键鼠映射、脚本自动化、图像识别。\n\n"
           "本引导将介绍主界面各项功能。"),
        QStringLiteral("*"),
        goHome});

    // ── 步骤 2: USB 连接按钮 ──
    if (m_homePage) {
        auto* usbBtn = m_homePage->findChild<Fluent::FluentButton*>();
        if (usbBtn) {
            steps.append({usbBtn,
                tr("USB 有线连接"),
                tr("通过 USB 数据线连接安卓设备。\n"
                   "需先在手机上开启 USB 调试。\n"
                   "连接成功后设备出现在下方列表。"),
                QStringLiteral(">"),
                goHome});
        }
    }

    // ── 步骤 3: WiFi 连接按钮 ──
    if (m_homePage) {
        auto buttons = m_homePage->findChildren<Fluent::FluentButton*>();
        if (buttons.size() >= 2) {
            steps.append({buttons[1],
                tr("WiFi 无线连接"),
                tr("通过 WiFi 无线方式连接设备。\n"
                   "已有 USB 时可一键切换为 WiFi；\n"
                   "无 USB 时需手动输入设备 IP。\n"
                   "手机和电脑需在同一局域网。"),
                QStringLiteral(">"),
                goHome});
        }
    }

    // ── 步骤 4: 设备列表 ──
    if (m_homePage) {
        auto* deviceList = m_homePage->findChild<QListWidget*>();
        if (deviceList) {
            steps.append({deviceList,
                tr("在线设备列表"),
                tr("已连接的设备显示在此列表。\n"
                   "双击设备即可启动投屏窗口。\n"
                   "支持同时连接多台设备。"),
                QStringLiteral(">"),
                goHome});
        }
    }

    // ── 步骤 5: 导航栏 ──
    if (m_navView) {
        steps.append({m_navView,
            tr("功能导航栏"),
            tr("切换不同功能页面：\n"
               "首页 — 设备连接与管理\n"
               "设置 — 投屏参数配置\n"
               "终端 — 日志与 ADB 命令\n"
               "帮助 — 使用文档"),
            QStringLiteral(">"),
            goHome});
    }

    // ── 步骤 6: 设置页 ──
    if (m_settingsPage) {
        QWidget* settingsTarget = m_settingsPage->findChild<QLineEdit*>();
        if (!settingsTarget) settingsTarget = m_settingsPage;

        steps.append({settingsTarget,
            tr("投屏参数设置"),
            tr("连接前可在此调整投屏参数：\n"
               "码率、分辨率、帧率、编码格式等。\n"
               "参数在下次连接时生效。"),
            QStringLiteral(">"),
            goSettings});
    }

    // ── 步骤 7: 终端页 ──
    if (m_terminalPage) {
        auto cards = m_terminalPage->findChildren<Fluent::FluentCard*>();
        QWidget* termTarget = (cards.size() >= 2) ? static_cast<QWidget*>(cards[1])
                                                   : static_cast<QWidget*>(m_terminalPage);

        steps.append({termTarget,
            tr("终端与日志"),
            tr("查看运行日志和脚本调试输出。\n"
               "可直接输入 ADB 命令操作设备。"),
            QStringLiteral(">"),
            goTerminal});
    }

    // ── 步骤 8: 整体工作流程说明 ──
    steps.append({nullptr,
        tr("推荐工作流程"),
        tr("连接设备 → 双击投屏 → 配置键位 → 编写脚本。\n"
           "各功能首次打开时有专属引导。"),
        QStringLiteral(">"),
        goHome});

    // ── 步骤 9: 完成 ──
    steps.append({nullptr,
        tr("主界面引导完成!"),
        tr("你已了解主界面的全部功能！\n"
           "试试连接设备开始使用吧。\n\n"
           "设置页可随时重新启动引导。"),
        QStringLiteral("!"),
        goHome});

    m_onboarding->setSteps(steps);

    connect(m_onboarding, &OnboardingOverlay::finished, this, [this, goHome]() {
        Config::getInstance().setOnboardingCompleted(Config::OB_MAIN_WINDOW, true);
        goHome();  // 引导结束后回到首页
        m_onboarding->deleteLater();
        m_onboarding = nullptr;
    });

    m_onboarding->start();
}
