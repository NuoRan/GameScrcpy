/**
 * @file ScriptEngine.h
 * @brief JavaScript 脚本引擎 / JavaScript Script Engine
 *
 * Copyright (C) 2019-2026 Rankun
 * Licensed under the Apache License, Version 2.0
 *
 * 功能 / Features:
 * - JavaScript 脚本执行 / JavaScript script execution
 * - 沙箱隔离与超时保护 / Sandbox isolation and timeout protection
 * - 触摸、按键、图像识别等 API / Touch, key press, image recognition APIs
 * - 自动启动脚本支持 / Auto-start script support
 */

#ifndef SCRIPTENGINE_H
#define SCRIPTENGINE_H

#include <QObject>
#include <QHash>
#include <QMutex>
#include <QPointF>
#include <QSize>
#include <QImage>
#include <atomic>
#include <functional>

class Controller;
class SessionContext;
class ScriptSandbox;

/// 帧获取回调类型 (用于图像识别) / Frame grab callback type (for image recognition)
using FrameGrabCallback = std::function<QImage()>;

/**
 * @brief JavaScript 脚本引擎 / JavaScript Script Engine
 *
 * 管理多个脚本沙箱的执行，提供 / Manages multiple script sandbox execution, providing:
 * - 沙箱隔离: 每个脚本在独立沙箱中执行 / Sandbox isolation: each script runs independently
 * - 超时保护: 自动检测并终止死循环脚本 / Timeout protection: auto-detect and terminate runaway scripts
 * - 会话绑定: 通过 SessionContext 管理状态 / Session binding: state managed via SessionContext
 * - API 兼容: 完全兼容旧版 mapi 接口 / API compatible: fully compatible with legacy mapi interface
 */
class ScriptEngine : public QObject
{
    Q_OBJECT
public:
    explicit ScriptEngine(Controller* controller, SessionContext* ctx, QObject* parent = nullptr);
    ~ScriptEngine();

    // 获取/设置会话上下文
    SessionContext* sessionContext() const { return m_sessionContext; }
    void setSessionContext(SessionContext* ctx);  // 实现在 cpp 中，需要通知所有沙箱

    // 设置脚本基础路径
    void setScriptBasePath(const QString& path) { m_scriptBasePath = path; }
    QString scriptBasePath() const { return m_scriptBasePath; }

    // 设置视频尺寸（用于脚本坐标计算）
    void setVideoSize(const QSize& size) { m_videoSize = size; }
    QSize videoSize() const { return m_videoSize; }

    void setFrameGrabCallback(FrameGrabCallback callback);
    static QImage grabCurrentFrame();

    // 执行脚本文件（返回沙箱 ID）
    int runScript(const QString& scriptPath, int keyId, const QPointF& anchorPos, bool isPress);

    // 执行内联脚本（返回沙箱 ID）
    int runInlineScript(const QString& script, int keyId, const QPointF& anchorPos, bool isPress);

    // 执行自动启动脚本（keyId 从 -1000 开始递减）
    void runAutoStartScript(const QString& script);

    // 检查脚本是否包含自动启动标记
    static bool isAutoStartScript(const QString& script);

    // 停止指定沙箱
    void stopSandbox(int sandboxId);

    // 停止所有沙箱
    void stopAll();

    // 重置所有状态（等同于 stopAll）
    void reset() { stopAll(); }

    // 检查是否有正在运行的沙箱
    bool hasRunningSandboxes() const;

    // 设置最大触摸点数（静态，影响所有沙箱）
    static void setMaxTouchPoints(int max);

    // 设置热键 UI 显示位置
    void setKeyUIPos(const QString& keyName, double x, double y);

signals:
    // 触摸请求信号（转发到主线程处理）
    void touchRequested(quint32 seqId, quint8 action, quint16 x, quint16 y);
    void keyRequested(quint8 action, quint16 keycode);

    // UI 信号
    void tipRequested(const QString& msg, int durationMs, int keyId);
    void shotmodeRequested(bool gameMode);

    // 参数调整信号
    void radialParamRequested(double up, double down, double left, double right);
    void resetviewRequested();
    void resetWheelRequested();

    // 按键模拟信号
    void simulateKeyRequested(const QString& keyName, bool press);
    void keyUIPosRequested(const QString& keyName, double x, double y);

    // 键位覆盖层更新信号
    void keyMapOverlayUpdateRequested();

    // 脚本错误信号
    void scriptError(const QString& error);

private slots:
    void onSandboxFinished(int sandboxId);

    // P-KCP: 仅保留有额外逻辑的槽（其余已改为直接信号→信号连接）
    void onKeyUIPosRequested(const QString& keyName, double x, double y);

private:
    int createSandbox(const QString& scriptOrPath, int keyId, const QPointF& anchorPos,
                      bool isPress, bool isInline);
    void connectSandbox(ScriptSandbox* sandbox);

    Controller* m_controller = nullptr;
    SessionContext* m_sessionContext = nullptr;

    QString m_scriptBasePath;
    QSize m_videoSize = QSize(1920, 1080);
    FrameGrabCallback m_frameGrabCallback;

    static FrameGrabCallback s_frameGrabCallback;
    static QMutex s_frameGrabMutex;  // 保护静态回调的互斥锁
    static ScriptEngine* s_activeEngine;  // 当前活跃的引擎（用于防止旧引擎清除新回调）
    static std::atomic<int> s_callInProgress;  // 正在进行的回调调用计数

    // 沙箱管理
    QHash<int, ScriptSandbox*> m_sandboxes;
    mutable QMutex m_sandboxMutex;
    std::atomic<int> m_nextSandboxId{1};

    // 自动启动脚本的 keyId 计数器（从 -1000 开始递减）
    int m_autoStartKeyIdCounter = -1000;
};

#endif // SCRIPTENGINE_H
