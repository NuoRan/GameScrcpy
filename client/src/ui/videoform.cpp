#include "videoform.h"
#include "ui_videoform.h"
#include "toolform.h"
#include "VideoBottomBar.h"
#include "KeyMapSidePanel.h"
#include "VideoSettingsPopup.h"
#include "D3D11VideoWidget.h"
#include "iconhelper.h"
#include "AudioStreamManager.h"
#include "config.h"
#include "mousetap.h"
#include "keepratiowidget.h"
#include "KeyMapItems.h"
#include "KeyMapOverlay.h"
#include "ConfigCenter.h"
#include "ScriptTipWidget.h"
#include "StringUtils.h"
#include "ThreadDispatcher.h"
#include <QLabel>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QDebug>
#include <QMap>
#include <QEventLoop>
#include <QMetaEnum>
#include <QByteArray>
#include "FluentDialog.h"
#include "ThemeManager.h"
#include "OnboardingOverlay.h"
#include <QThread>
#include <iostream>
#include <fstream>
#include <QStyleOption>

#if defined(Q_OS_WIN32)
#include <Windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#endif

// 新架构：直接使用 DeviceSession（不再使用 IDevice/DeviceObserver）
#include "service/DeviceSession.h"
#include "infra/FrameData.h"
#include "GameScrcpyCore.h"  // IDeviceManage
#include "InputEvent.h"

// OpenCV: UI→Core 边界的 QImage→cv::Mat 转换
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

// === Qt → InputEvent 转换辅助函数 ===
namespace {

InputEvent fromQMouseEvent(const QMouseEvent* e, const QWidget* videoWidget, const QWidget* parent)
{
    InputEvent ev{};

    switch (e->type()) {
    case QEvent::MouseButtonPress:   ev.type = InputEventType::MousePress;   break;
    case QEvent::MouseButtonRelease: ev.type = InputEventType::MouseRelease; break;
    case QEvent::MouseMove:          ev.type = InputEventType::MouseMove;    break;
    case QEvent::MouseButtonDblClick: ev.type = InputEventType::MousePress;  break;   // remap dblclick → press
    default:                         ev.type = InputEventType::MouseMove;    break;
    }

    // 坐标：转换到 videoWidget 本地坐标
    QPointF localPos;
    QPointF globalPos;
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    localPos = videoWidget->mapFrom(parent, e->pos());
    globalPos = e->globalPos();
#else
    localPos = videoWidget->mapFrom(parent, e->position().toPoint());
    globalPos = e->globalPosition();
#endif
    ev.localX = localPos.x();
    ev.localY = localPos.y();
    ev.globalX = globalPos.x();
    ev.globalY = globalPos.y();

    ev.button = static_cast<uint32_t>(e->button());
    ev.buttons = static_cast<uint32_t>(e->buttons());
    ev.modifiers = static_cast<uint32_t>(e->modifiers());
    ev.key = 0;
    ev.isAutoRepeat = false;
    ev.wheelDelta = 0;

    return ev;
}

InputEvent fromQMouseEventDirect(const QMouseEvent* e)
{
    InputEvent ev{};

    switch (e->type()) {
    case QEvent::MouseButtonPress:   ev.type = InputEventType::MousePress;   break;
    case QEvent::MouseButtonRelease: ev.type = InputEventType::MouseRelease; break;
    case QEvent::MouseMove:          ev.type = InputEventType::MouseMove;    break;
    default:                         ev.type = InputEventType::MouseMove;    break;
    }

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    ev.localX = e->localPos().x();
    ev.localY = e->localPos().y();
    ev.globalX = e->globalPos().x();
    ev.globalY = e->globalPos().y();
#else
    ev.localX = e->position().x();
    ev.localY = e->position().y();
    ev.globalX = e->globalPosition().x();
    ev.globalY = e->globalPosition().y();
#endif

    ev.button = static_cast<uint32_t>(e->button());
    ev.buttons = static_cast<uint32_t>(e->buttons());
    ev.modifiers = static_cast<uint32_t>(e->modifiers());
    ev.key = 0;
    ev.isAutoRepeat = false;
    ev.wheelDelta = 0;

    return ev;
}

InputEvent fromQWheelEvent(const QWheelEvent* e, const QWidget* videoWidget, const QWidget* parent)
{
    InputEvent ev{};
    ev.type = InputEventType::MouseWheel;

    QPointF localPos;
    QPointF globalPos;
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    localPos = videoWidget->mapFrom(parent, e->position().toPoint());
    globalPos = e->globalPosition();
#else
    localPos = videoWidget->mapFrom(parent, e->pos());
    globalPos = e->globalPosF();
#endif
    ev.localX = localPos.x();
    ev.localY = localPos.y();
    ev.globalX = globalPos.x();
    ev.globalY = globalPos.y();

    ev.wheelDelta = e->angleDelta().y();
    ev.button = 0;
    ev.buttons = static_cast<uint32_t>(e->buttons());
    ev.modifiers = static_cast<uint32_t>(e->modifiers());
    ev.key = 0;
    ev.isAutoRepeat = false;

    return ev;
}

InputEvent fromQKeyEvent(const QKeyEvent* e)
{
    InputEvent ev{};
    ev.type = (e->type() == QEvent::KeyPress) ? InputEventType::KeyPress : InputEventType::KeyRelease;
    ev.key = e->key();
    ev.modifiers = static_cast<uint32_t>(e->modifiers());
    ev.isAutoRepeat = e->isAutoRepeat();
    ev.localX = ev.localY = ev.globalX = ev.globalY = 0;
    ev.button = 0;
    ev.buttons = 0;
    ev.wheelDelta = 0;

    return ev;
}

InputEvent makeMouseEvent(InputEventType type, float lx, float ly, uint32_t button, uint32_t buttons)
{
    InputEvent ev{};
    ev.type = type;
    ev.localX = lx;
    ev.localY = ly;
    ev.globalX = lx;
    ev.globalY = ly;
    ev.button = button;
    ev.buttons = buttons;
    ev.modifiers = InputModifier::None;
    ev.key = 0;
    ev.isAutoRepeat = false;
    ev.wheelDelta = 0;
    return ev;
}

InputEvent makeKeyEvent(InputEventType type, int qtKey)
{
    InputEvent ev{};
    ev.type = type;
    ev.key = qtKey;
    ev.modifiers = InputModifier::None;
    ev.isAutoRepeat = false;
    ev.localX = ev.localY = ev.globalX = ev.globalY = 0;
    ev.button = 0;
    ev.buttons = 0;
    ev.wheelDelta = 0;
    return ev;
}

} // anonymous namespace

// =======================================================
// VideoForm 实现
// =======================================================

// ---------------------------------------------------------
// 构造与析构
// ---------------------------------------------------------
VideoForm::VideoForm(bool framelessWindow, bool skin, bool showToolbar, QWidget *parent) : QWidget(parent), ui(new Ui::videoForm), m_skin(skin) {
    ui->setupUi(this);
    initUI();
    updateShowSize(size());
    this->show_toolbar = showToolbar;

    // 如果启用皮肤，根据宽高比加载样式
    if (m_skin) {
        updateStyleSheet(size().height() > size().width());
    } else {
        // 无皮肤模式：应用深色样式
        applyDarkStyle();
    }
    // 设置无边框窗口
    if (framelessWindow) {
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    }
    qInfo("[VideoForm] Created (frameless=%d, skin=%d, toolbar=%d)", framelessWindow, skin, showToolbar);
}

// 应用深色样式（与主界面一致）
void VideoForm::applyDarkStyle() {
    setStyleSheet(R"(
        QWidget#videoForm {
            background-color: #09090b;
            border: 1px solid #27272a;
            border-radius: 8px;
        }
    )");
    layout()->setContentsMargins(2, 2, 2, 2);
}

// 设置 Windows 标题栏主题（跟随当前主题）
void VideoForm::setDarkTitleBar() {
#if defined(Q_OS_WIN32)
    HWND hwnd = reinterpret_cast<HWND>(winId());
    BOOL darkMode = Fluent::ThemeManager::instance().isDarkMode() ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
#endif
}

VideoForm::~VideoForm() {
    // Signal<> connections are automatically disconnected
    // (DeviceSession signals use disconnectAll() on destruction)
    delete ui;
}

// ---------------------------------------------------------
// 会话绑定（UI 解耦核心）
// 通过此方法绑定 DeviceSession，使用纯信号槽交互
// ---------------------------------------------------------
void VideoForm::bindSession(qsc::core::DeviceSession* session) {
    // 断开旧会话的信号 (Signal<> 使用 disconnectAll)
    if (m_session) {
        m_session->frameAvailable.disconnectAll();
        m_session->fpsUpdated.disconnectAll();
        m_session->cursorGrabChanged.disconnectAll();
        m_session->scriptTip.disconnectAll();
        m_session->keyMapOverlayUpdated.disconnectAll();
        m_session->setFrameGrabCallback(nullptr);
        m_frameNotifyPending.store(false, std::memory_order_release);
    }

    m_session = session;

    if (m_session) {
        // === 所有信号回调都通过 dispatch::postToMain 安全投递到 GUI 线程 ===
        // 这些信号可能从 Demuxer 线程或脚本线程触发，禁止直接访问 Qt widget

        m_session->frameAvailable.connect([this]() {
            // 原子标志合并高频帧通知
            if (!m_frameNotifyPending.exchange(true, std::memory_order_acq_rel)) {
                dispatch::postToMain([this]() {
                    m_frameNotifyPending.store(false, std::memory_order_release);
                    onSessionFrameAvailable();
                });
            }
        });

        // FPS 更新信号（从 Demuxer 线程触发）
        m_session->fpsUpdated.connect([this](quint32 fps) {
            dispatch::postToMain([this, fps]() {
                onSessionFpsUpdated(fps);
            });
        });

        // 光标状态信号（从 InputManager 工作线程触发）
        m_session->cursorGrabChanged.connect([this](bool grabbed) {
            dispatch::postToMain([this, grabbed]() {
                onSessionCursorGrabChanged(grabbed);
            });
        });

        // 脚本提示信号（从脚本线程触发）
        m_session->scriptTip.connect([this](const std::string& msg, int keyId, int durationMs) {
            QString qMsg = strutil::toQ(msg);
            dispatch::postToMain([this, qMsg, keyId, durationMs]() {
                onSessionScriptTip(qMsg, keyId, durationMs);
            });
        });

        // 键位覆盖层更新信号
        m_session->keyMapOverlayUpdated.connect([this]() {
            dispatch::postToMain([this]() {
                onSessionKeyMapOverlayUpdated();
            });
        });

        // 设置帧获取回调（用于脚本图像识别）
        // 注意：回调的生命周期由 ScriptEngine 的互斥锁机制管理
        // 在 closeEvent 中会先清除回调，再停止脚本
        // UI 边界：QImage → cv::Mat 转换
        m_session->setFrameGrabCallback([this]() -> cv::Mat {
            QImage img = grabCurrentFrame();
            if (img.isNull()) return cv::Mat();
            QImage converted = img.convertToFormat(QImage::Format_RGB888);
            cv::Mat mat(converted.height(), converted.width(), CV_8UC3,
                        const_cast<uchar*>(converted.bits()), converted.bytesPerLine());
            cv::Mat result;
            cv::cvtColor(mat, result, cv::COLOR_RGB2BGR);
            return result;
        });

        // 设置灰度帧获取回调（用于脚本图像匹配零拷贝管线）
        m_session->setGrayFrameGrabCallback([this]() -> GrayFrame {
            if (m_videoWidget) {
                return m_videoWidget->grabGrayFrame();
            }
            return {};
        });


        // 音频按钮可见性: 仅当会话有 AudioStreamManager 时显示
        if (m_bottomBar) {
            bool hasAudio = (m_session->audioManager() != nullptr);
            m_bottomBar->setAudioVisible(hasAudio);
            if (hasAudio) {
                // 同步初始状态 (音频启用时默认播放)
                m_bottomBar->setAudioMuted(false);
            }
        }
    }

}

// ---------------------------------------------------------
// DeviceSession 信号槽实现
// ---------------------------------------------------------
void VideoForm::onSessionFrameAvailable() {
    // 从 FrameQueue 消费帧
    // 此方法通过 dispatch::postToMain 在 GUI 线程执行

    if (!m_session || m_closing) return;

    // 防御性检查: m_videoWidget 可能已被销毁 (QPointer 自动置 null)
    if (!m_videoWidget) return;

    // drain 队列，只保留最新帧，避免积压导致延迟累积
    qsc::core::FrameData* frame = nullptr;
    qsc::core::FrameData* latest = nullptr;
    int drained = 0;
    while ((frame = m_session->consumeFrame()) != nullptr) {
        if (latest) {
            m_session->releaseFrame(latest);  // 释放旧帧
        }
        latest = frame;
        drained++;
    }
    frame = latest;
    if (!frame || !frame->isValid()) {
        if (frame) m_session->releaseFrame(frame);
        return;
    }

    const int w = frame->width;
    const int h = frame->height;



    // 使用 submitFrameDirect，直接传指针给渲染器
    // 渲染完成后通过回调归还帧，生命周期由 FramePool 引用计数管理
    m_session->retainFrame(frame);  // 增加引用计数，确保跨线程安全

    // 首帧处理和窗口尺寸更新（已在 GUI 线程，直接调用）
    if (!m_firstFrameReceived || QSize(w, h) != m_videoWidget->frameSize()) {
        if (m_videoWidget->isHidden()) {
            m_videoWidget->show();
        }
        updateShowSize(QSize(w, h));
        m_videoWidget->setFrameSize(QSize(w, h));
        // Sync device pixel ratio for DPI-aware cursor positioning
        m_session->setDevicePixelRatio(m_videoWidget->devicePixelRatioF());
        if (!m_firstFrameReceived) {
            m_firstFrameReceived = true;
            if (!m_currentKeyMapFile.isEmpty() && m_session) {
                m_session->runAutoStartScripts();
            }
        }
    }

    m_videoWidget->submitFrameDirect(
        frame->dataY, frame->dataU, frame->dataV,
        w, h,
        frame->linesizeY, frame->linesizeU, frame->linesizeV,
        [session = m_session, frame]() {
            // paintGL 完成后归还帧（在 GUI 线程执行）
            if (session) {
                session->releaseFrame(frame);  // retain 的引用
                session->releaseFrame(frame);  // consumeFrame 的引用
            }
        }
    );


}

void VideoForm::onSessionFpsUpdated(quint32 fps) {
    if (m_fpsLabel) {
        m_fpsLabel->setText(QString("FPS:%1").arg(fps));
    }
}

void VideoForm::onSessionCursorGrabChanged(bool grabbed) {
    // 启用/禁用鼠标捕获
    QRect rc = getGrabCursorRect();
    MouseTap::getInstance()->enableMouseEventTap(rc, grabbed);

    // 同步设置 tip 弹窗的游戏模式
    // 游戏模式下弹窗对鼠标透明，不会干扰视角控制
    ScriptTipWidget::instance()->setGameMode(grabbed);
}

void VideoForm::onSessionScriptTip(const QString& msg, int keyId, int durationMs) {
    ScriptTipWidget* tipWidget = ScriptTipWidget::instance();
    tipWidget->setParentVideoWidget(this);
    tipWidget->addMessage(msg, durationMs, keyId);
}

void VideoForm::onSessionKeyMapOverlayUpdated() {
    if (m_keyMapOverlay) {
        m_keyMapOverlay->scheduleUpdate();
    }
}

// ---------------------------------------------------------
// 初始化UI
// 设置OpenGL渲染控件、FPS显示标签以及键位编辑视图
// ---------------------------------------------------------
void VideoForm::initUI() {

    // 加载手机皮肤资源
    if (m_skin) {
        QPixmap phone;
        if (phone.load(":/res/phone.png")) {
            m_widthHeightRatio = 1.0f * phone.width() / phone.height();
        }
#ifndef Q_OS_OSX
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
#endif
    }


    // 初始化视频渲染控件 (D3D11)
    m_videoWidget = new D3D11VideoWidget();
    m_videoWidget->hide();


    // 设置保持比例容器
    ui->keepRatioWidget->setWidget(m_videoWidget);
    ui->keepRatioWidget->setWidthHeightRatio(m_widthHeightRatio);


    // v21: 监听 videoWidget 的 resize/move 事件来同步 overlay 位置
    m_videoWidget->installEventFilter(this);

    // FPS 显示标签 (v25: 顶层窗口, WA_TranslucentBackground 实现真透明)
    m_fpsLabel = new QLabel(this);  // parent=this, 但设为顶层 Tool 窗口
    m_fpsLabel->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint
                               | Qt::WindowTransparentForInput
                               | Qt::WindowDoesNotAcceptFocus);
    m_fpsLabel->setAttribute(Qt::WA_TranslucentBackground);
    m_fpsLabel->setAttribute(Qt::WA_ShowWithoutActivating);
    QFont ft;
    ft.setPointSize(15);
    ft.setWeight(QFont::Light);
    ft.setBold(true);
    m_fpsLabel->setFont(ft);
    m_fpsLabel->setMinimumWidth(100);
    m_fpsLabel->setStyleSheet("QLabel{color:#00FF00; background: rgba(0,0,0,128); padding: 2px 4px;}");

    // 开启鼠标追踪
    setMouseTracking(true);
    m_videoWidget->setMouseTracking(true);
    ui->keepRatioWidget->setMouseTracking(true);


    // 初始化键位编辑覆盖层 (v28: 顶层窗口, viewport 透明, drawBackground 半透明遮罩)
    m_keyMapEditView = new KeyMapEditView(this);
    m_keyMapEditView->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    m_keyMapEditView->setAttribute(Qt::WA_TranslucentBackground);
#ifdef Q_OS_WIN
    // 使编辑层成为 VideoForm 的原生子窗口，移动父窗口时子窗口自动跟随，消除延迟
    {
        HWND overlayHwnd = (HWND)m_keyMapEditView->winId();
        HWND parentHwnd  = (HWND)this->winId();
        LONG style = ::GetWindowLong(overlayHwnd, GWL_STYLE);
        style = (style & ~WS_POPUP) | WS_CHILD | WS_CLIPSIBLINGS;
        ::SetWindowLong(overlayHwnd, GWL_STYLE, style);
        ::SetParent(overlayHwnd, parentHwnd);
    }
#endif
    m_keyMapEditView->hide();


    // 初始化键位提示层 (v25: 顶层窗口, WA_TranslucentBackground 实现真透明)
    m_keyMapOverlay = new KeyMapOverlay(this);  // parent=this, Tool 窗口
    m_keyMapOverlay->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint
                                    | Qt::WindowTransparentForInput
                                    | Qt::WindowDoesNotAcceptFocus);
    m_keyMapOverlay->setAttribute(Qt::WA_TranslucentBackground);
    m_keyMapOverlay->setAttribute(Qt::WA_ShowWithoutActivating);
    m_keyMapOverlay->setOpacity(qsc::ConfigCenter::instance().keyMapOverlayOpacity() / 100.0);
    m_keyMapOverlay->hide();

    // 根据保存的设置决定是否显示
    if (qsc::ConfigCenter::instance().keyMapOverlayVisible()) {
        // 延迟显示，等 loadKeyMap 完成后再更新
        QTimer::singleShot(500, this, [this]() {
            setKeyMapOverlayVisible(true);
        });
    } else {
        m_keyMapOverlay->hide();
    }

    // === Fluent UI: 右侧操作栏 ===
    m_bottomBar = new VideoBottomBar(this);
    m_bottomBar->setAudioVisible(false); // 默认隐藏音频按钮，bindSession 时根据 audioManager 显示


    // === Fluent UI: 侧边键位面板 ===
    m_sidePanel = new KeyMapSidePanel(this);
    m_sidePanel->setVisible(false);


    // 将操作栏放在 keepRatioWidget 右侧（插入到 verticalLayout 的 wrapper 中）
    {
        // 创建水平包裹：[视频区域] [侧面板] [右侧栏]
        auto* hWrapper = new QHBoxLayout();
        hWrapper->setContentsMargins(0, 0, 0, 0);
        hWrapper->setSpacing(0);

        // 从 verticalLayout 中取出 keepRatioWidget
        ui->verticalLayout->removeWidget(ui->keepRatioWidget);
        hWrapper->addWidget(ui->keepRatioWidget, 1);
        hWrapper->addWidget(m_sidePanel);    // 面板在视频和操作栏之间
        hWrapper->addWidget(m_bottomBar);

        ui->verticalLayout->insertLayout(0, hWrapper, 1);
    }
    m_bottomBar->setVisible(false); // 等 showToolForm 再显示

    connect(m_bottomBar, &VideoBottomBar::goBack, this, [this]() {
        if (m_session) m_session->postGoBack();
    });
    connect(m_bottomBar, &VideoBottomBar::goHome, this, [this]() {
        if (m_session) m_session->postGoHome();
    });
    connect(m_bottomBar, &VideoBottomBar::appSwitch, this, [this]() {
        if (m_session) m_session->postAppSwitch();
    });
    connect(m_bottomBar, &VideoBottomBar::fullScreen, this, [this]() {
        switchFullScreen();
    });
    connect(m_bottomBar, &VideoBottomBar::keyMapToggled, this, [this](bool active) {
        // 展开/折叠侧边键位面板
        if (m_sidePanel) {
            m_sidePanel->setExpanded(active);
            if (active) m_sidePanel->refreshConfigList();
            // 拓宽/收窄窗口以容纳面板，不遮挡视频
            int delta = active ? 260 : -260;
            if (!isFullScreen()) {
                resize(width() + delta, height());
            }
        }
        onKeyMapEditModeToggled(active);
        // 切换配置时加载
        if (m_sidePanel && active) {
            emit m_sidePanel->configChanged(m_sidePanel->currentConfig());
        }
    });

    connect(m_sidePanel, &KeyMapSidePanel::configChanged, this, [this](const QString& filename) {
        loadKeyMap(filename, true);
    });
    connect(m_sidePanel, &KeyMapSidePanel::saveRequested, this, &VideoForm::saveKeyMap);
    connect(m_sidePanel, &KeyMapSidePanel::overlayToggled, this, &VideoForm::setKeyMapOverlayVisible);
    connect(m_sidePanel, &KeyMapSidePanel::overlayOpacityChanged, this, [this](int opacity) {
        if (m_keyMapOverlay) m_keyMapOverlay->setOpacity(opacity / 100.0);
    });
    connect(m_sidePanel, &KeyMapSidePanel::scriptTipOpacityChanged, this, [this](int opacity) {
        ScriptTipWidget::instance()->setOpacityLevel(opacity);
    });
    connect(m_sidePanel, &KeyMapSidePanel::editModeChanged, this, [this](bool active) {
        onKeyMapEditModeToggled(active);
    });
    connect(m_sidePanel, &KeyMapSidePanel::closeRequested, this, [this]() {
        // 同步底部栏的 keymap 按钮状态 (取消激活)
        if (m_bottomBar) m_bottomBar->setKeyMapMode(false);
        // 收窄窗口
        if (!isFullScreen()) {
            resize(width() - 260, height());
        }
    });

    // === 音频切换 (关闭音频 = 停止服务端采集，释放手机音频) ===
    connect(m_bottomBar, &VideoBottomBar::audioToggled, this, [this](bool muted) {
        if (m_session && m_session->audioManager()) {
            if (muted) {
                // 关闭音频: 断开 TCP socket，服务端检测到断开后停止 AudioRecord，
                // 手机音频恢复正常 (不再被 scrcpy 占用)
                m_session->audioManager()->stopStream();
                qDebug() << "[VideoForm] Audio stopped (server capture released)";
                // 按钮变为不可点击 (重新启用需要重新连接)
                if (m_bottomBar) {
                    m_bottomBar->setAudioVisible(false);
                }
            } else {
                m_session->audioManager()->setMuted(false);
                qDebug() << "[VideoForm] Audio unmuted";
            }
        }
    });

    // === 实时设置弹窗 ===
    m_settingsPopup = new VideoSettingsPopup(this);
    m_settingsPopup->hide();

    connect(m_bottomBar, &VideoBottomBar::settingsClicked, this, [this]() {
        if (!m_settingsPopup) return;
        // 模态弹窗，每次打开同步配置
        m_settingsPopup->syncFromConfig();
        m_settingsPopup->exec();
    });

    // 视频参数 → DeviceSession (实时发送到设备)
    connect(m_settingsPopup, &VideoSettingsPopup::videoParamsChanged, this,
            [this](quint32 bitrate, quint16 maxFps, quint16 maxSize) {
        if (m_session) m_session->setVideoParams(bitrate, maxFps, maxSize);
    });

    // 视频传输控制 → DeviceSession (暂停/恢复视频流)
    connect(m_settingsPopup, &VideoSettingsPopup::videoStreamingChanged, this, [this](bool on) {
        if (m_session) m_session->setVideoStreaming(on);
    });

    // 显示键位 → 覆盖层 + 侧边面板同步
    connect(m_settingsPopup, &VideoSettingsPopup::overlayVisibleChanged, this, [this](bool visible) {
        setKeyMapOverlayVisible(visible);
        if (m_sidePanel) m_sidePanel->setOverlayChecked(visible);
    });

    // 键位透明度 → 覆盖层 + 侧边面板同步
    connect(m_settingsPopup, &VideoSettingsPopup::overlayOpacityChanged, this, [this](int opacity) {
        if (m_keyMapOverlay) m_keyMapOverlay->setOpacity(opacity / 100.0);
        if (m_sidePanel) m_sidePanel->setOverlayOpacity(opacity);
    });

    // 提示透明度 → ScriptTipWidget + 侧边面板同步
    connect(m_settingsPopup, &VideoSettingsPopup::tipOpacityChanged, this, [this](int opacity) {
        ScriptTipWidget::instance()->setOpacityLevel(opacity);
        if (m_sidePanel) m_sidePanel->setTipOpacity(opacity);
    });

    // 息屏 → DeviceSession (关闭/开启设备屏幕)
    connect(m_settingsPopup, &VideoSettingsPopup::screenOffChanged, this, [this](bool off) {
        if (m_session) m_session->setDisplayPower(!off);
    });
}

// ---------------------------------------------------------
// 触摸与按键事件发送
// 将本地坐标/事件转换为 Android 设备指令
// ---------------------------------------------------------
void VideoForm::sendTouchDown(int id, float x, float y) {
    if (!m_session) return;
    (void)id;

    float lx = x * m_videoWidget->width();
    float ly = y * m_videoWidget->height();
    auto ev = makeMouseEvent(InputEventType::MousePress, lx, ly, InputButton::Left, InputButton::Left);
    m_session->mouseEvent(ev, typeconv::fromQ(m_frameSize), typeconv::fromQ(m_videoWidget->size()));
}

void VideoForm::sendTouchUp(int id, float x, float y) {
    if (!m_session) return;
    (void)id;

    float lx = x * m_videoWidget->width();
    float ly = y * m_videoWidget->height();
    auto ev = makeMouseEvent(InputEventType::MouseRelease, lx, ly, InputButton::Left, InputButton::None);
    m_session->mouseEvent(ev, typeconv::fromQ(m_frameSize), typeconv::fromQ(m_videoWidget->size()));
}

void VideoForm::sendTouchMove(int id, float x, float y) {
    if (!m_session) return;
    (void)id;

    float lx = x * m_videoWidget->width();
    float ly = y * m_videoWidget->height();
    auto ev = makeMouseEvent(InputEventType::MouseMove, lx, ly, InputButton::Left, InputButton::Left);
    m_session->mouseEvent(ev, typeconv::fromQ(m_frameSize), typeconv::fromQ(m_videoWidget->size()));
}

void VideoForm::sendKeyClick(int qtKey) {
    if (!m_session) return;

    auto e1 = makeKeyEvent(InputEventType::KeyPress, qtKey);
    m_session->keyEvent(e1, typeconv::fromQ(m_frameSize), typeconv::fromQ(m_videoWidget->size()));
    auto e2 = makeKeyEvent(InputEventType::KeyRelease, qtKey);
    m_session->keyEvent(e2, typeconv::fromQ(m_frameSize), typeconv::fromQ(m_videoWidget->size()));
}

// ---------------------------------------------------------
// 获取当前视频帧 (用于图像识别)
// ---------------------------------------------------------
QImage VideoForm::grabCurrentFrame() {
    if (m_videoWidget) {
        return m_videoWidget->grabCurrentFrame();
    }
    return QImage();
}

// ---------------------------------------------------------
// 键位映射加载逻辑
// 读取JSON文件，更新内存配置，并下发脚本到 Core 库
// runAutoStart: 是否执行自动启动脚本（初始加载时为 false，等视频流准备好后再执行）
// ---------------------------------------------------------
void VideoForm::loadKeyMap(const QString& filename, bool runAutoStart) {
    if (filename.isEmpty()) return;

    // 0. 清除脚本设置的 UI 位置覆盖，恢复到键位配置的原始位置
    KeyMapOverlay::clearAllOverrides();

    if (m_keyMapEditView && m_keyMapEditView->scene()) {
        m_keyMapEditView->clearEditingState();
        // 必须先清空撤销栈，再清空场景。否则撤销栈中的命令会持有已销毁对象的悬空指针
        m_keyMapEditView->undoStack()->clear();
        m_keyMapEditView->scene()->clear();
    }

    std::ifstream ifs(("keymap/" + filename).toStdString(), std::ios::binary);
    if (!ifs) return;

    std::string data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    // 1. 保存当前文件名
    m_currentKeyMapFile = filename;

    // 2. 判断是否在编辑模式
    bool isInEditMode = m_keyMapEditView && m_keyMapEditView->isVisible();

    // 3. 将脚本更新到底层设备实例
    // 编辑模式下跳过 updateScript，避免 SessionContext 反复销毁重建导致崩溃
    // 键位配置会在退出编辑模式时统一应用
    if (m_session && !isInEditMode) {
        m_session->updateScript(data, runAutoStart);
    }

    // 3. 记录配置，下次启动自动加载
    Config::getInstance().setKeyMap(strutil::fromQ(m_serial), strutil::fromQ(filename));

    // 4. 同步工具栏UI状态
    if (m_toolForm) {
        m_toolForm->setCurrentKeyMap(filename);
    }
    // Fluent UI: 同步侧边面板配置
    if (m_sidePanel) {
        m_sidePanel->setCurrentConfig(filename);
    }

    // 5. 解析 JSON 并在 UI 上绘制可视化键位
    nlohmann::json root;
    try { root = nlohmann::json::parse(data); } catch (...) { return; }
    m_currentConfigBase = root;

    KeyMapFactoryImpl factory;
    QSize sz = m_videoWidget->size().isEmpty() ? QSize(100,100) : m_videoWidget->size();

    if (root.contains("keyMapNodes") && root["keyMapNodes"].is_array()) {
        for (const auto& node : root["keyMapNodes"]) {
            if (!node.is_object()) continue;

            QString typeStr = QString::fromStdString(node.value("type", ""));
            KeyMapType type = KeyMapHelper::getTypeFromString(typeStr);

            if (type == KMT_STEER_WHEEL || type == KMT_SCRIPT || type == KMT_CAMERA_MOVE || type == KMT_FREE_LOOK) {
                KeyMapItemBase* item = factory.createItem(type);
                if (item) {
                    item->fromJson(node);
                    double x = 0, y = 0;
                    if (node.contains("pos") && node["pos"].is_object()) {
                        x = node["pos"].value("x", 0.0);
                        y = node["pos"].value("y", 0.0);
                    } else if (node.contains("centerPos") && node["centerPos"].is_object()) {
                        x = node["centerPos"].value("x", 0.0);
                        y = node["centerPos"].value("y", 0.0);
                    }

                    m_keyMapEditView->scene()->addItem(item);
                    item->setNormalizedPos(QPointF(x, y), sz);

                    if (auto* w = dynamic_cast<KeyMapItemSteerWheel*>(item)) w->updateSubItemsPos();
                }
            }
        }
    }

    // 6. 确保场景矩形与定位尺寸一致
    //    loadKeyMap 可能在 videoWidget 尚无尺寸时使用 100×100 回退值定位项目。
    //    如果不设置 sceneRect，后续 KeyMapEditView::updateSize() 发现 oldSize 为空
    //    会直接 return，导致键位项永远停留在 100 基准的像素坐标，
    //    overlay 的归一化计算会把它们都画到左上角。
    if (m_keyMapEditView && m_keyMapEditView->scene()) {
        m_keyMapEditView->scene()->setSceneRect(0, 0, sz.width(), sz.height());
    }

    // 7. 更新键位提示层
    if (m_keyMapOverlay && m_keyMapOverlay->isVisible()) {
        updateKeyMapOverlay();
    }
}

// ---------------------------------------------------------
// 键位映射保存逻辑
// 检测按键冲突，生成 JSON 并写入文件
// ---------------------------------------------------------
void VideoForm::saveKeyMap() {
    if (m_currentKeyMapFile.isEmpty()) m_currentKeyMapFile = "default.json";
    if (!m_keyMapEditView || !m_keyMapEditView->scene()) return;

    QList<QGraphicsItem*> items = m_keyMapEditView->scene()->items();

    // 1. 重置冲突状态
    for (auto i : items) {
        if(auto* b = dynamic_cast<KeyMapItemBase*>(i)) {
            b->setConflicted(false);
            if(auto* w = dynamic_cast<KeyMapItemSteerWheel*>(b)) {
                for(int j=0; j<4; j++) w->setSubItemConflicted(j, false);
            }
        }
    }

    // 2. 检测按键冲突
    QMap<QString, int> keyCount;
    QMap<QString, QList<QPair<KeyMapItemBase*, int>>> owners;

    for (auto g : items) {
        if (auto* item = dynamic_cast<KeyMapItemBase*>(g)) {
            if (auto* w = dynamic_cast<KeyMapItemSteerWheel*>(item)) {
                QString keys[] = {w->getUpKey(), w->getDownKey(), w->getLeftKey(), w->getRightKey()};
                for(int i=0; i<4; i++) {
                    if(!keys[i].isEmpty()) {
                        keyCount[keys[i]]++;
                        owners[keys[i]].append({item, i});
                    }
                }
            } else if (auto* s = dynamic_cast<KeyMapItemScript*>(item)) {
                QString k = s->getKey();
                if (!k.isEmpty()) {
                    keyCount[k]++;
                    owners[k].append({item, -1});
                }
            } else if (auto* c = dynamic_cast<KeyMapItemCamera*>(item)) {
                QString k = c->getKey();
                if (!k.isEmpty()) {
                    keyCount[k]++;
                    owners[k].append({item, -1});
                }
            } else if (auto* fl = dynamic_cast<KeyMapItemFreeLook*>(item)) {
                QString k = fl->getKey();
                if (!k.isEmpty()) {
                    keyCount[k]++;
                    owners[k].append({item, -1});
                }
            }
        }
    }

    bool conflict = false;
    for (auto it = keyCount.begin(); it != keyCount.end(); ++it) {
        if (it.value() > 1) {
            conflict = true;
            for (auto o : owners[it.key()]) {
                if (o.second == -1) o.first->setConflicted(true);
                else dynamic_cast<KeyMapItemSteerWheel*>(o.first)->setSubItemConflicted(o.second, true);
            }
        }
    }

    if (conflict) {
        Fluent::FluentDialog::warning(this, tr("警告"), tr("按键设置冲突，请修改红色标记的按键！"));
        return;
    }

    // 3. 检测空热键
    bool hasEmptyKey = false;
    for (auto g : items) {
        if (auto* item = dynamic_cast<KeyMapItemBase*>(g)) {
            if (auto* w = dynamic_cast<KeyMapItemSteerWheel*>(item)) {
                if (w->getUpKey().isEmpty() || w->getDownKey().isEmpty() ||
                    w->getLeftKey().isEmpty() || w->getRightKey().isEmpty()) {
                    hasEmptyKey = true;
                    w->setConflicted(true);  // 用红色标记
                }
            } else if (auto* s = dynamic_cast<KeyMapItemScript*>(item)) {
                if (s->getKey().isEmpty()) {
                    hasEmptyKey = true;
                    s->setConflicted(true);
                }
            } else if (auto* c = dynamic_cast<KeyMapItemCamera*>(item)) {
                if (c->getKey().isEmpty()) {
                    hasEmptyKey = true;
                    c->setConflicted(true);
                }
            } else if (auto* fl = dynamic_cast<KeyMapItemFreeLook*>(item)) {
                if (fl->getKey().isEmpty()) {
                    hasEmptyKey = true;
                    fl->setConflicted(true);
                }
            }
        }
    }

    if (hasEmptyKey) {
        Fluent::FluentDialog::warning(this, tr("警告"), tr("存在未设置热键的组件，请为红色标记的组件设置热键！"));
        return;
    }

    // 3. 序列化并保存
    nlohmann::json root = m_currentConfigBase;
    nlohmann::json nodes = nlohmann::json::array();
    nlohmann::json mouseMoveMap = nlohmann::json::object();

    for (auto g : items) {
        if (auto* item = dynamic_cast<KeyMapItemBase*>(g)) {
            nlohmann::json d = item->toJson();
            nodes.push_back(d);
        }
    }

    root["keyMapNodes"] = nodes;
    root["mouseMoveMap"] = mouseMoveMap;

    std::ofstream ofs(("keymap/" + m_currentKeyMapFile).toStdString(), std::ios::binary);
    if (ofs) {
        std::string jsonData = root.dump(4);
        ofs.write(jsonData.c_str(), jsonData.size());
        ofs.close();

        // 保存后重新加载以应用更改
        loadKeyMap(m_currentKeyMapFile);
    }
}

// ---------------------------------------------------------
// 鼠标交互事件处理
// 负责处理窗口拖拽、点击操作映射到手机
// ---------------------------------------------------------
void VideoForm::mousePressEvent(QMouseEvent *e) {
    if (m_keyMapEditView && m_keyMapEditView->isVisible()) return;

    // 记录按下状态（仅用于窗口拖拽判断，不阻止事件）
    m_pressedButtons |= e->button();

    // 快速判断是否在视频区域内
    QRect videoRect = m_videoWidget->geometry();
    if (videoRect.contains(e->pos())) {
        if(!m_session) return;
        auto ev = fromQMouseEvent(e, m_videoWidget, this);
        m_session->mouseEvent(ev, typeconv::fromQ(m_frameSize), typeconv::fromQ(m_videoWidget->size()));
    } else {
        // 在视频区域外：处理窗口拖拽
        if (e->button()==Qt::LeftButton) {
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
            m_dragPosition = e->globalPos() - frameGeometry().topLeft();
#else
            m_dragPosition = e->globalPosition().toPoint() - frameGeometry().topLeft();
#endif
            e->accept();
        }
    }
}

void VideoForm::mouseReleaseEvent(QMouseEvent *e) {
    if (m_keyMapEditView && m_keyMapEditView->isVisible()) return;

    // 清除按下状态（仅用于窗口拖拽判断，不阻止事件）
    m_pressedButtons &= ~e->button();

    if(m_dragPosition.isNull()){
        if(!m_session) return;
        auto ev = fromQMouseEvent(e, m_videoWidget, this);
        m_session->mouseEvent(ev, typeconv::fromQ(m_frameSize), typeconv::fromQ(m_videoWidget->size()));
    } else {
        m_dragPosition = QPoint(0,0);
    }
}

void VideoForm::mouseMoveEvent(QMouseEvent *e) {
    if (m_keyMapEditView && m_keyMapEditView->isVisible()) return;

    QRect videoRect = m_videoWidget->geometry();
    if(videoRect.contains(e->pos())){
        if(!m_session) return;
        auto ev = fromQMouseEvent(e, m_videoWidget, this);
        m_session->mouseEvent(ev, typeconv::fromQ(m_frameSize), typeconv::fromQ(m_videoWidget->size()));
    } else if(!m_dragPosition.isNull() && (e->buttons() & Qt::LeftButton)){
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        move(e->globalPos() - m_dragPosition);
#else
        move(e->globalPosition().toPoint() - m_dragPosition);
#endif
        e->accept();
    }
}

void VideoForm::mouseDoubleClickEvent(QMouseEvent *e) {
    if (m_keyMapEditView && m_keyMapEditView->isVisible()) return;

    // 双击非视频区域去除黑边
    if(e->button()==Qt::LeftButton && !m_videoWidget->geometry().contains(e->pos())) {
        if(!isMaximized()) removeBlackRect();
        return;  // 已处理，不再转发
    }
    // 右键双击熄屏/亮屏
    if(e->button()==Qt::RightButton && m_session && !m_session->isCurrentCustomKeymap()) {
        m_session->postPower();  // 双击视为亮屏/熄屏
        return;  // 已处理，不再转发
    }
    // 这导致快速连击时大约一半的点击被"吃掉"，严重影响响应速度
    // 解决方案：将双击事件当作 Press 事件处理
    if(m_videoWidget->geometry().contains(e->pos())){
        if(!m_session) return;
        // 转换事件类型：MouseButtonDblClick -> MouseButtonPress (fromQMouseEvent handles this)
        auto ev = fromQMouseEvent(e, m_videoWidget, this);
        m_session->mouseEvent(ev, typeconv::fromQ(m_frameSize), typeconv::fromQ(m_videoWidget->size()));
    }
}

void VideoForm::wheelEvent(QWheelEvent *e) {
    if (m_keyMapEditView && m_keyMapEditView->isVisible()) return;
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    if(m_videoWidget->geometry().contains(e->position().toPoint())) {
        if(!m_session) return;
        auto ev = fromQWheelEvent(e, m_videoWidget, this);
        m_session->wheelEvent(ev, typeconv::fromQ(m_frameSize), typeconv::fromQ(m_videoWidget->size()));
    }
#else
    if(m_videoWidget->geometry().contains(e->pos())) {
        if(!m_session) return;
        auto ev = fromQWheelEvent(e, m_videoWidget, this);
        m_session->wheelEvent(ev, typeconv::fromQ(m_frameSize), typeconv::fromQ(m_videoWidget->size()));
    }
#endif
}

// ---------------------------------------------------------
// 键盘事件处理
// 转发按键到手机，处理全屏退出
// ---------------------------------------------------------
void VideoForm::keyPressEvent(QKeyEvent *e) {
    if (!m_session) return;

    if (Qt::Key_Escape == e->key() && !e->isAutoRepeat() && isFullScreen()) {
        switchFullScreen();
    }

    auto ev = fromQKeyEvent(e);
    m_session->keyEvent(ev, typeconv::fromQ(m_frameSize), typeconv::fromQ(m_videoWidget->size()));
}

void VideoForm::keyReleaseEvent(QKeyEvent *e) {
    if (!m_session) return;
    auto ev = fromQKeyEvent(e);
    m_session->keyEvent(ev, typeconv::fromQ(m_frameSize), typeconv::fromQ(m_videoWidget->size()));
}

// ---------------------------------------------------------
// 辅助功能函数
// ---------------------------------------------------------

// 切换键位编辑模式
void VideoForm::onKeyMapEditModeToggled(bool active) {
    if (m_keyMapEditView) {
        if (!active) {
            m_keyMapEditView->clearEditingState();
        }
        if (active) {
            // v28: D3D11 保持显示, 编辑视图作为顶层半透明窗口覆盖其上
            syncOverlaysToVideo();
            m_keyMapEditView->show();
            m_keyMapEditView->raise();
        } else {
            m_keyMapEditView->hide();
        }
    }

    // 退出编辑时，应用键位配置到底层并获取焦点
    if (!active) {
        // 编辑模式下保存时跳过了 updateScript，退出时统一应用
        if (!m_currentKeyMapFile.isEmpty()) {
            loadKeyMap(m_currentKeyMapFile, true);
        }
        this->setFocus();
        this->activateWindow();
    }

    // 编辑模式首次引导
    if (active && !Config::getInstance().getOnboardingCompleted(Config::OB_EDIT_MODE)) {
        QTimer::singleShot(500, this, [this]() {
            startEditModeOnboarding();
        });
    }
}

// 获取光标抓取区域
QRect VideoForm::getGrabCursorRect() {
#if defined(Q_OS_WIN32)
    QRect rc = QRect(ui->keepRatioWidget->mapToGlobal(m_videoWidget->pos()), m_videoWidget->size());
    rc.setTopLeft(rc.topLeft() * m_videoWidget->devicePixelRatioF());
    rc.setBottomRight(rc.bottomRight() * m_videoWidget->devicePixelRatioF());
#else
    QRect rc = m_videoWidget->geometry();
#endif
    return rc.adjusted(10, 10, -20, -20);
}

const QSize &VideoForm::frameSize() { return m_frameSize; }

void VideoForm::resizeSquare() {
    resize(ui->keepRatioWidget->goodSize());
}

void VideoForm::removeBlackRect() {
    resize(ui->keepRatioWidget->goodSize());
}

void VideoForm::showFPS(bool show) {
    if (m_fpsLabel) {
        m_fpsLabel->setVisible(show);
        if (show) syncOverlaysToVideo();
    }
}

// 更新渲染画面
void VideoForm::updateRender(int w, int h, uint8_t* y, uint8_t* u, uint8_t* v, int ly, int lu, int lv) {
    if (m_videoWidget->isHidden()) m_videoWidget->show();
    updateShowSize(QSize(w, h));
    m_videoWidget->setFrameSize(QSize(w, h));
    m_videoWidget->updateTextures(y, u, v, ly, lu, lv);

    // 收到第一帧后执行自动启动脚本
    if (!m_firstFrameReceived) {
        m_firstFrameReceived = true;
        // 重新加载当前键位并执行自动启动脚本
        if (!m_currentKeyMapFile.isEmpty() && m_session) {
            m_session->runAutoStartScripts();
        }
    }
}

// 设置设备序列号并加载上次的键位配置
void VideoForm::setSerial(const QString &s) {
    m_serial = s;
    m_firstFrameReceived = false;  // 重置状态，等待视频流准备好

    QString savedKeyMap = strutil::toQ(Config::getInstance().getKeyMap(strutil::fromQ(m_serial)));
    if (!savedKeyMap.isEmpty()) {
        // 加载键位但不执行自动启动脚本，等视频流准备好后再执行
        loadKeyMap(savedKeyMap, false);
    }
}

// 显示/隐藏工具栏 (Fluent UI: 使用底部栏替代浮动 ToolForm)
void VideoForm::showToolForm(bool s) {
    // Fluent UI: 显示/隐藏底部操作栏
    if (m_bottomBar) {
        m_bottomBar->setVisible(s);
    }

    // 保留旧 ToolForm 的初始化逻辑用于向后兼容
    // 当有键位需要同步时使用
    if (!m_toolForm) {
        m_toolForm = new ToolForm(this, ToolForm::AP_OUTSIDE_RIGHT);
        m_toolForm->setSerial(m_serial);
        connect(m_toolForm, &ToolForm::keyMapEditModeToggled, this, &VideoForm::onKeyMapEditModeToggled);
        connect(m_toolForm, &ToolForm::keyMapChanged, this, [this](const QString& keyMapPath) {
            loadKeyMap(keyMapPath, true);  // 用户切换按键映射时，执行自动启动脚本
        });
        connect(m_toolForm, &ToolForm::keyMapSaveRequested, this, &VideoForm::saveKeyMap);
        connect(m_toolForm, &ToolForm::keyMapOverlayToggled, this, &VideoForm::setKeyMapOverlayVisible);
        connect(m_toolForm, &ToolForm::keyMapOverlayOpacityChanged, this, [this](int opacity) {
            if (m_keyMapOverlay) {
                m_keyMapOverlay->setOpacity(opacity / 100.0);
            }
        });
        connect(m_toolForm, &ToolForm::scriptTipOpacityChanged, this, [this](int opacity) {
            ScriptTipWidget::instance()->setOpacityLevel(opacity);
        });

        if (!m_currentKeyMapFile.isEmpty()) {
            m_toolForm->setCurrentKeyMap(m_currentKeyMapFile);
        }
    }
    // 隐藏旧 ToolForm（Fluent UI 模式下不再使用浮动工具栏）
    m_toolForm->setVisible(false);

    // 同步键位文件到侧边面板
    if (m_sidePanel && !m_currentKeyMapFile.isEmpty()) {
        m_sidePanel->setCurrentConfig(m_currentKeyMapFile);
    }
}

void VideoForm::moveCenter() {
    move(QApplication::primaryScreen()->availableGeometry().center() - rect().center());
}

QRect VideoForm::getScreenRect() {
    return QApplication::primaryScreen()->availableGeometry();
}

void VideoForm::updateStyleSheet(bool v) {
    setStyleSheet(v ? R"(#videoForm{border-image:url(:/image/videoform/phone-v.png) 150px 65px 85px 65px;border-width:150px 65px 85px 65px;})"
                    : R"(#videoForm{border-image:url(:/image/videoform/phone-h.png) 65px 85px 65px 150px;border-width:65px 85px 65px 150px;})");
    layout()->setContentsMargins(getMargins(v));
}

QMargins VideoForm::getMargins(bool v) {
    return v ? QMargins(10, 68, 12, 62) : QMargins(68, 12, 62, 10);
}

void VideoForm::updateShowSize(const QSize &s) {
    if (s.width() <= 0 || s.height() <= 0) return;
    if (m_frameSize != s) {
        m_frameSize = s;
        m_widthHeightRatio = 1.0f * s.width() / s.height();
        ui->keepRatioWidget->setWidthHeightRatio(m_widthHeightRatio);

        bool v = m_widthHeightRatio < 1.0f;

        // 如果已有用户设置的窗口位置，不强制 resize（保持用户设置的大小）
        if (m_hasUserGeometry) {
            // 只更新样式（横竖屏切换）和键位提示层
            if (m_skin) updateStyleSheet(v);
        } else {
            QSize ss = s;
            if (m_skin) {
                ss.setWidth(ss.width() + getMargins(v).left() + getMargins(v).right());
                ss.setHeight(ss.height() + getMargins(v).top() + getMargins(v).bottom());
            }
            // 确保窗口不超出屏幕可用区域（平板等宽屏设备）
            QRect screenRect = getScreenRect();
            int maxW = screenRect.width() * 9 / 10;   // 最大占屏幕 90%
            int maxH = screenRect.height() * 9 / 10;
            if (ss.width() > maxW || ss.height() > maxH) {
                float scale = qMin((float)maxW / ss.width(), (float)maxH / ss.height());
                ss = QSize((int)(ss.width() * scale), (int)(ss.height() * scale));
            }
            if (ss != size()) resize(ss);
        }

        // 更新键位提示层（处理旋转）
        // 使用延时确保 videoWidget 大小已更新，合并多次快速变化
        if (!m_pendingOverlaySync) {
            m_pendingOverlaySync = true;
            QTimer::singleShot(100, this, [this]() {
                m_pendingOverlaySync = false;
                syncOverlaysToVideo();
            });
        }
    }
}

// 切换全屏模式
void VideoForm::switchFullScreen() {
    if (isFullScreen()) {
        // 退出全屏：恢复 Fit 模式
        ui->keepRatioWidget->setScaleMode(KeepRatioWidget::FitMode);
        if (m_widthHeightRatio > 1.0f) ui->keepRatioWidget->setWidthHeightRatio(m_widthHeightRatio);
        showNormal();
        resize(m_normalSize);
        move(m_fullScreenBeforePos);
        if (m_skin) updateStyleSheet(m_frameSize.height() > m_frameSize.width());
        showToolForm(this->show_toolbar);
        // Fluent UI: 恢复底部栏
        if (m_bottomBar) m_bottomBar->setVisible(this->show_toolbar);
#ifdef Q_OS_WIN32
        ::SetThreadExecutionState(ES_CONTINUOUS);
#endif
    } else {
        // 进入全屏：使用 Fit 模式（保持比例，可能有黑边）
        m_normalSize = size();
        m_fullScreenBeforePos = pos();
        showToolForm(false);
        // Fluent UI: 全屏时隐藏底部栏和侧边面板
        if (m_bottomBar) m_bottomBar->setVisible(false);
        if (m_sidePanel && m_sidePanel->isExpanded()) m_sidePanel->setExpanded(false);
        if (m_skin) layout()->setContentsMargins(0, 0, 0, 0);
        ui->keepRatioWidget->setScaleMode(KeepRatioWidget::FitMode);
        showFullScreen();
#ifdef Q_OS_WIN32
        ::SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
#endif
    }
}

bool VideoForm::isHost() {
    return m_toolForm ? m_toolForm->isHost() : false;
}

// 旧的 DeviceObserver 接口方法已删除，使用 onSessionXxx 信号槽替代

void VideoForm::staysOnTop(bool top) {
    bool needShow = false;
    if (isVisible()) needShow = true;
    setWindowFlag(Qt::WindowStaysOnTopHint, top);
    if (m_toolForm) m_toolForm->setWindowFlag(Qt::WindowStaysOnTopHint, top);
    if (needShow) show();
}

void VideoForm::paintEvent(QPaintEvent *e) {
    Q_UNUSED(e);
    QStyleOption o;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    o.initFrom(this);
#else
    o.init(this);
#endif
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &o, &p, this);
}

void VideoForm::showEvent(QShowEvent *e) {
    Q_UNUSED(e);
    // 窗口首次显示后，允许保存窗口位置
    if (m_initializing) {
        m_initializing = false;
        // 窗口首次显示时设置深色标题栏（确保 HWND 有效）
        setDarkTitleBar();

        // 投屏窗口首次引导
        if (!Config::getInstance().getOnboardingCompleted(Config::OB_VIDEO_FORM)) {
            QTimer::singleShot(800, this, [this]() {
                startVideoFormOnboarding();
            });
        }
    }
    if (!isFullScreen() && show_toolbar) {
        QTimer::singleShot(500, this, [this](){ showToolForm(show_toolbar); });
    }
}

void VideoForm::resizeEvent(QResizeEvent *e) {
    Q_UNUSED(e);
    saveWindowGeometry();
    syncOverlaysToVideo();
    // 侧边面板由布局管理，不需要手动定位
}

// v21: 跟踪 m_videoWidget 在 keepRatioWidget 中的位置/大小变化
bool VideoForm::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_videoWidget &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        syncOverlaysToVideo();
    }
    return QWidget::eventFilter(watched, event);
}

// v28: 顶层窗口同步到 videoWidget 的屏幕坐标
void VideoForm::syncOverlaysToVideo()
{
    if (!m_videoWidget || !m_videoWidget->isVisible()) return;
    QRect vg = m_videoWidget->geometry();  // 在 keepRatioWidget 坐标系中
    QPoint globalPos = ui->keepRatioWidget->mapToGlobal(vg.topLeft());
    QSize sz = vg.size();

    if (m_keyMapOverlay) {
        m_keyMapOverlay->setGeometry(globalPos.x(), globalPos.y(), sz.width(), sz.height());
        if (m_keyMapOverlay->isVisible()) {
            m_keyMapOverlay->raise();
            updateKeyMapOverlay();
        }
    }
    if (m_keyMapEditView) {
        // 编辑层已通过 SetParent 成为原生子窗口，使用客户区相对坐标
        QPoint relPos = ui->keepRatioWidget->mapTo(this, vg.topLeft());
        m_keyMapEditView->setGeometry(relPos.x(), relPos.y(), sz.width(), sz.height());
        if (m_keyMapEditView->isVisible()) {
            m_keyMapEditView->updateSize(sz);
            m_keyMapEditView->raise();
        }
    }
    if (m_fpsLabel && m_fpsLabel->isVisible()) {
        m_fpsLabel->move(globalPos.x() + 5, globalPos.y() + 15);
        m_fpsLabel->raise();
    }
}

void VideoForm::moveEvent(QMoveEvent *e) {
    Q_UNUSED(e);
    saveWindowGeometry();
    syncOverlaysToVideo();  // v25: 顶层 overlay 需要跟随窗口移动
}

void VideoForm::saveWindowGeometry() {
    if (m_serial.isEmpty() || isFullScreen() || m_restoringGeometry || m_initializing) return;
    QRect qrc = geometry();
    Config::getInstance().setRect(strutil::fromQ(m_serial), Rect(qrc.x(), qrc.y(), qrc.width(), qrc.height()));
}

void VideoForm::restoreWindowGeometry() {
    if (m_serial.isEmpty()) return;
    Rect rc = Config::getInstance().getRect(strutil::fromQ(m_serial));
    if (rc.isValid()) {
        m_restoringGeometry = true;
        setGeometry(QRect(rc.x, rc.y, rc.width, rc.height));
        m_restoringGeometry = false;
        m_hasUserGeometry = true;  // 标记有用户设置的窗口位置
    }
}

void VideoForm::closeEvent(QCloseEvent *e) {
    Q_UNUSED(e);

    // 防止重复处理
    if (m_closing) {
        return;
    }
    m_closing = true;

    // 先释放渲染器持有的帧，此时 m_session 还有效
    // 避免析构时回调访问已空的 m_session
    if (m_videoWidget) {
        m_videoWidget->discardPendingFrame();
    }

    if (m_session) {
        m_session->setFrameGrabCallback(nullptr);
        m_session->resetScriptState();
        m_session->resetAllTouchPoints();
        bindSession(nullptr);
    }
    if (!m_serial.isEmpty()) {
        qsc::IDeviceManage::getInstance().disconnectDevice(strutil::fromQ(m_serial));
    }

    // 标记自己需要删除
    deleteLater();
}

void VideoForm::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);

    if (event->type() == QEvent::ActivationChange) {
        if (!isActiveWindow() && m_session) {
            // 窗口失去焦点，通知底层重置输入状态
            m_session->onWindowFocusLost();
            // 同时释放全部触摸点（防止脚本触摸点泄漏）
            m_session->resetAllTouchPoints();
        }
    }
}

// ---------------------------------------------------------
// 键位提示层控制
// ---------------------------------------------------------
void VideoForm::setKeyMapOverlayVisible(bool visible) {
    if (m_keyMapOverlay) {
        if (visible) {
            updateKeyMapOverlay();
            syncOverlaysToVideo();
            m_keyMapOverlay->show();
            m_keyMapOverlay->raise();
        } else {
            m_keyMapOverlay->hide();
        }
        // 保存显示状态
        qsc::ConfigCenter::instance().setKeyMapOverlayVisible(visible);
    }
}

bool VideoForm::isKeyMapOverlayVisible() const {
    return m_keyMapOverlay && m_keyMapOverlay->isVisible();
}

void VideoForm::updateKeyMapOverlay() {
    if (!m_keyMapOverlay || !m_keyMapEditView || !m_keyMapEditView->scene()) return;

    QList<KeyMapOverlay::KeyInfo> infos;
    QList<QGraphicsItem*> items = m_keyMapEditView->scene()->items();

    // 使用场景矩形尺寸做归一化——场景坐标系与项目像素坐标一致，
    // 避免 videoWidget 尺寸与项目定位尺寸不一致时所有键位挤到左上角。
    QSizeF sceneSize = m_keyMapEditView->scene()->sceneRect().size();
    QSize sz = (sceneSize.isEmpty() || sceneSize.width() <= 0)
                   ? m_videoWidget->size()
                   : sceneSize.toSize();

    for (auto item : items) {
        auto* base = dynamic_cast<KeyMapItemBase*>(item);
        if (!base) continue;

        KeyMapOverlay::KeyInfo info;
        QPointF normPos = base->getNormalizedPos(sz);
        info.pos = normPos;

        // 根据类型设置信息
        if (auto* script = dynamic_cast<KeyMapItemScript*>(base)) {
            info.type = "script";
            info.label = script->getKey();
        } else if (auto* wheel = dynamic_cast<KeyMapItemSteerWheel*>(base)) {
            info.type = "steerWheel";
            info.label = "";
            // 获取轮盘半径
            nlohmann::json json = wheel->toJson();
            double leftDist = json.value("leftOffset", 0.1);
            info.size = QSizeF(leftDist * 2, leftDist * 2);
            // 获取 WASD 子按键
            KeyMapOverlay::KeyInfo up, down, left, right;
            up.type = "up"; up.label = wheel->getUpKey();
            down.type = "down"; down.label = wheel->getDownKey();
            left.type = "left"; left.label = wheel->getLeftKey();
            right.type = "right"; right.label = wheel->getRightKey();
            info.subKeys << up << down << left << right;
        } else if (auto* camera = dynamic_cast<KeyMapItemCamera*>(base)) {
            info.type = "camera";
            info.label = "视角";
        } else if (auto* freeLook = dynamic_cast<KeyMapItemFreeLook*>(base)) {
            info.type = "freeLook";
            info.label = freeLook->getKey();
        } else {
            continue;  // 跳过未知类型
        }

        infos.append(info);
    }

    m_keyMapOverlay->setKeyInfos(infos);
}

// ═══════════════════════════════════════════════════════════
// 场景引导
// ═══════════════════════════════════════════════════════════

void VideoForm::startVideoFormOnboarding()
{
    if (m_onboarding) {
        m_onboarding->deleteLater();
        m_onboarding = nullptr;
    }

    using Fluent::OnboardingStep;
    using Fluent::OnboardingOverlay;

    m_onboarding = new OnboardingOverlay(this);

    // D3D11VideoWidget 使用 DirectX 渲染，普通子控件的半透明遮罩会完全遮住画面导致黑屏。
    // 将引导层提升为顶层透明窗口，使 D3D11 可在下层正常渲染。
    m_onboarding->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    m_onboarding->setAttribute(Qt::WA_TranslucentBackground);

    QVector<OnboardingStep> steps;

    // ── 步骤 1: 欢迎 ──
    steps.append({nullptr,
        tr("投屏窗口"),
        tr("设备投屏窗口，显示安卓设备实时画面。\n"
           "可直接鼠标操控、使用键位映射、运行脚本。"),
        QStringLiteral("*")});

    // ── 步骤 2: 视频画面 ──
    if (m_videoWidget) {
        steps.append({m_videoWidget,
            tr("投屏画面区域"),
            tr("显示设备实时画面。\n"
               "鼠标点击/拖拽模拟触摸，滚轮模拟滑动。\n"
               "开启键位映射后按配置模拟触摸。\n"
               "按 ~ 键切换鼠标捕获模式。"),
            QStringLiteral(">")});
    }

    // ── 步骤 3: 侧边工具栏 ──
    if (m_bottomBar) {
        steps.append({m_bottomBar,
            tr("操作工具栏"),
            tr("右侧工具栏提供快捷操作：\n"
               "返回、主页、多任务、全屏、键位面板。"),
            QStringLiteral(">")});
    }

    // ── 步骤 4: 侧边面板 ──
    if (m_sidePanel) {
        steps.append({m_sidePanel,
            tr("键位配置面板"),
            tr("点击工具栏「键位」按钮展开。\n"
               "可选择/新建配置、进入编辑模式、\n"
               "调整拟人化参数和显示设置。"),
            QStringLiteral(">")});
    }

    m_onboarding->setSteps(steps);

    connect(m_onboarding, &OnboardingOverlay::finished, this, [this]() {
        Config::getInstance().setOnboardingCompleted(Config::OB_VIDEO_FORM, true);
        m_onboarding->deleteLater();
        m_onboarding = nullptr;
    });

    m_onboarding->start();

    // start() 内部用 parentWidget()->rect() 设定几何，对顶层窗口不正确。
    // 覆盖为 VideoForm 的全局坐标，使引导层精确覆盖投屏窗口。
    QPoint globalPos = mapToGlobal(QPoint(0, 0));
    m_onboarding->setGeometry(globalPos.x(), globalPos.y(), width(), height());
    m_onboarding->raise();
}

void VideoForm::restartOnboarding()
{
    if (isVisible()) {
        QTimer::singleShot(500, this, [this]() {
            startVideoFormOnboarding();
        });
    }
}

void VideoForm::startEditModeOnboarding()
{
    if (m_onboarding) {
        m_onboarding->deleteLater();
        m_onboarding = nullptr;
    }

    using Fluent::OnboardingStep;
    using Fluent::OnboardingOverlay;

    m_onboarding = new OnboardingOverlay(this);

    // KeyMapEditView 是独立的 Qt::Tool 顶层窗口，普通子控件无法覆盖它。
    // 将引导层也提升为顶层窗口并置顶，确保盖在编辑视图之上。
    m_onboarding->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    m_onboarding->setAttribute(Qt::WA_TranslucentBackground);

    QVector<OnboardingStep> steps;

    // ── 步骤 1: 欢迎 ──
    steps.append({nullptr,
        tr("键位编辑模式"),
        tr("可视化配置键盘/鼠标到屏幕触摸的映射关系。"),
        QStringLiteral("*")});

    // ── 步骤 2: 编辑画布 ──
    if (m_keyMapEditView) {
        steps.append({m_keyMapEditView,
            tr("键位编辑画布"),
            tr("叠加在投屏画面上的编辑区域。\n"
               "从右侧拖拽组件放置，拖拽调整位置，\n"
               "双击修改绑定按键，右键删除。"),
            QStringLiteral(">")});
    }

    // ── 步骤 3: 侧边面板组件 ──
    if (m_sidePanel) {
        steps.append({m_sidePanel,
            tr("可拖拽组件面板"),
            tr("包含各种键位组件类型：\n"
               "点击、长按、摇杆、视角、脚本、自由视角。\n"
               "拖拽到画布上对应位置即可。"),
            QStringLiteral(">")});
    }

    // ── 步骤 4: 保存与退出 ──
    steps.append({nullptr,
        tr("保存与退出编辑"),
        tr("点击面板中的保存按钮存储配置。\n"
           "点击退出编辑按钮自动应用配置。\n"
           "支持创建多套配置随时切换。"),
        QStringLiteral("!")});

    m_onboarding->setSteps(steps);

    connect(m_onboarding, &OnboardingOverlay::finished, this, [this]() {
        Config::getInstance().setOnboardingCompleted(Config::OB_EDIT_MODE, true);
        m_onboarding->deleteLater();
        m_onboarding = nullptr;
    });

    m_onboarding->start();

    // start() 内部用 parentWidget()->rect() 设定几何，对顶层窗口不正确（会放到屏幕左上角）。
    // 覆盖为 VideoForm 的全局坐标，使引导层精确覆盖投屏窗口。
    QPoint globalPos = mapToGlobal(QPoint(0, 0));
    m_onboarding->setGeometry(globalPos.x(), globalPos.y(), width(), height());
    m_onboarding->raise();
}
