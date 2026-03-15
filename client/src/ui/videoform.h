/**
 * @file videoform.h
 * @brief 视频显示窗口 / Video Display Window
 *
 * Copyright (C) 2019-2026 Rankun
 * Licensed under the Apache License, Version 2.0
 *
 * 功能 / Features:
 * - 显示设备投屏画面 / Display device screen mirroring
 * - 处理键鼠输入 / Handle keyboard/mouse input
 * - 键位映射覆盖层 / Key mapping overlay
 * - 悬浮工具栏 / Floating toolbar
 */

#ifndef VIDEOFORM_H
#define VIDEOFORM_H

#include <QWidget>
#include <QPointer>
#include "third_party/nlohmann/json.hpp"
#include <QVariant>
#include <QPointF>
#include <atomic>

#include "KeyMapEditView.h"
#include "KeyMapOverlay.h"

namespace Fluent { class OnboardingOverlay; }

// 前向声明
namespace qsc { namespace core { class DeviceSession; } }

namespace Ui { class videoForm; }
class ToolForm; class D3D11VideoWidget; class QLabel;
class VideoBottomBar; class KeyMapSidePanel; class VideoSettingsPopup;
class CompanionClient;

/**
 * @brief 视频显示窗口 / Video Display Window
 *
 * 负责显示设备画面、处理用户输入及键位映射
 * Displays device screen, handles user input and key mapping.
 *
 * 架构设计 / Architecture:
 * - UI 完全解耦，不继承 DeviceObserver / Fully decoupled UI, no DeviceObserver inheritance
 * - 通过 bindSession() 绑定 DeviceSession / Binds to DeviceSession via bindSession()
 * - 所有交互通过信号槽完成 / All interactions via signals/slots
 */
class VideoForm : public QWidget
{
    Q_OBJECT
public:
    explicit VideoForm(bool framelessWindow = false, bool skin = true, bool showToolBar = true, QWidget *parent = 0);
    ~VideoForm();

    // === 会话绑定（UI 解耦核心） ===
    // 通过此方法绑定 DeviceSession，使用纯信号槽交互
    void bindSession(qsc::core::DeviceSession* session);
    qsc::core::DeviceSession* session() const { return m_session; }

    // 窗口控制接口
    void staysOnTop(bool top = true);
    void updateShowSize(const QSize &newSize);
    void updateRender(int width, int height, uint8_t* dataY, uint8_t* dataU, uint8_t* dataV, int linesizeY, int linesizeU, int linesizeV);
    void setSerial(const QString& serial);
    QRect getGrabCursorRect();
    const QSize &frameSize();
    void resizeSquare();
    void removeBlackRect();
    void showFPS(bool show);
    void switchFullScreen();
    bool isHost();

    /// 纯触控模式: 无视频流，显示深色虚拟画布供键位映射使用
    void enableVirtualCanvas(const QSize& canvasSize);

    // 脚本控制接口：模拟触控和按键
    void sendTouchDown(int id, float x, float y);
    void sendTouchMove(int id, float x, float y);
    void sendTouchUp(int id, float x, float y);
    void sendKeyClick(int qtKey);

    // 获取当前视频帧 (用于图像识别)
    QImage grabCurrentFrame();

public slots:
    // 键位映射管理
    // runAutoStart: 是否执行自动启动脚本（默认 true，初始加载时为 false）
    void loadKeyMap(const QString& filename, bool runAutoStart = true);
    void saveKeyMap();

public slots:
    void setKeyMapOverlayVisible(bool visible);
    bool isKeyMapOverlayVisible() const;

private slots:
    void onKeyMapEditModeToggled(bool active);

    // === DeviceSession 信号槽 ===
    void onSessionFrameAvailable();  // 零拷贝模式：帧可用通知
    void onSessionFpsUpdated(quint32 fps);
    void onSessionCursorGrabChanged(bool grabbed);
    void onSessionScriptTip(const QString& msg, int keyId, int durationMs);
    void onSessionKeyMapOverlayUpdated();

private:
    // 内部辅助方法
    void updateStyleSheet(bool vertical);
    QMargins getMargins(bool vertical);
    void initUI();
    void showToolForm(bool show = true);
    void moveCenter();
    QRect getScreenRect();

protected:
    // 事件处理 override
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;  // 处理窗口激活状态变化
    bool eventFilter(QObject *watched, QEvent *event) override;  // v21: 跟踪 videoWidget 位置
#if defined(Q_OS_WIN32)
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#endif

public:
    void saveWindowGeometry();
    void restoreWindowGeometry();  // 恢复窗口位置

private:
    void applyDarkStyle();  // 应用深色样式
    void setDarkTitleBar(); // 设置 Windows 深色标题栏
    void showCompanionDialog(); // 手机伴侣连接对话框

private:
    Ui::videoForm *ui;
    QPointer<ToolForm> m_toolForm;
    QPointer<QWidget> m_loadingWidget;
    QPointer<D3D11VideoWidget> m_videoWidget;
    QPointer<QLabel> m_fpsLabel;
    KeyMapEditView* m_keyMapEditView = nullptr;
    KeyMapOverlay* m_keyMapOverlay = nullptr;
    void updateKeyMapOverlay();  // 从当前配置更新覆盖层
    void syncOverlaysToVideo();  // v21: 同步 overlay/FPS 到 videoWidget 位置

    // Fluent UI: 底部操作栏 + 侧边键位面板 + 设置弹窗
    VideoBottomBar* m_bottomBar = nullptr;
    KeyMapSidePanel* m_sidePanel = nullptr;
    VideoSettingsPopup* m_settingsPopup = nullptr;

    nlohmann::json m_currentConfigBase;
    QString m_currentKeyMapFile;

    QSize m_frameSize; QSize m_normalSize; QPoint m_dragPosition;
    float m_widthHeightRatio = 0.5f; bool m_skin = true;
    QPoint m_fullScreenBeforePos; QString m_serial; bool show_toolbar = true;
    bool m_restoringGeometry = false;  // 正在恢复窗口位置时不保存
    bool m_initializing = true;        // 初始化期间不保存窗口位置
    bool m_hasUserGeometry = false;    // 是否有用户设置的窗口位置（恢复成功时为 true）
    bool m_pendingOverlaySync = false; // 防止 overlay 同步重复排队

    // 手机伴侣
    CompanionClient* m_companionClient = nullptr;
    bool m_cursorGrabbed = false;  // 当前是否游戏模式（光标捕获）

    // 防止重复鼠标事件
    Qt::MouseButtons m_pressedButtons;

    // 是否已收到第一帧视频（用于延迟执行自动启动脚本）
    std::atomic<bool> m_firstFrameReceived{false};

    // 渲染更新节流（防止队列堆积导致UI卡死）
    std::atomic<bool> m_renderQueued{false};

    // Demuxer 线程帧通知合并标志
    // 避免从 Demuxer 线程直接调用 Qt widget 方法（非线程安全）
    std::atomic<bool> m_frameNotifyPending{false};

    // 防止 closeEvent 重复处理
    bool m_closing = false;

    // === UI 解耦 ===
    // 持有 DeviceSession 指针，通过信号槽交互
    qsc::core::DeviceSession* m_session = nullptr;

    // === 场景引导 ===
    Fluent::OnboardingOverlay* m_onboarding = nullptr;
    void startVideoFormOnboarding();
    void startEditModeOnboarding();

public:
    /// 重置引导状态后调用，重新启动投屏窗口引导
    void restartOnboarding();
};

#endif // VIDEOFORM_H
