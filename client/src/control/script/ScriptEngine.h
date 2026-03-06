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

#include <unordered_map>
#include <mutex>
#include <string>
#include "GameTypes.h"
#include <atomic>
#include <functional>
#include <opencv2/core.hpp>
#include "GrayFrame.h"
#include "GameSignal.h"

class Controller;
class SessionContext;
class ScriptSandbox;

/// 帧获取回调类型 (用于图像识别) / Frame grab callback type (for image recognition)
using FrameGrabCallback = std::function<cv::Mat()>;

/**
 * @brief JavaScript 脚本引擎 / JavaScript Script Engine
 *
 * 管理多个脚本沙箱的执行，提供 / Manages multiple script sandbox execution, providing:
 * - 沙箱隔离: 每个脚本在独立沙箱中执行 / Sandbox isolation: each script runs independently
 * - 超时保护: 自动检测并终止死循环脚本 / Timeout protection: auto-detect and terminate runaway scripts
 * - 会话绑定: 通过 SessionContext 管理状态 / Session binding: state managed via SessionContext
 * - API 兼容: 完全兼容旧版 mapi 接口 / API compatible: fully compatible with legacy mapi interface
 */
class ScriptEngine
{
public:
    ScriptEngine(Controller* controller, SessionContext* ctx);
    ~ScriptEngine();

    // 获取/设置会话上下文
    SessionContext* sessionContext() const { return m_sessionContext; }
    void setSessionContext(SessionContext* ctx);  // 实现在 cpp 中，需要通知所有沙箱

    // 设置脚本基础路径
    void setScriptBasePath(const std::string& path) { m_scriptBasePath = path; }
    std::string scriptBasePath() const { return m_scriptBasePath; }

    // 设置视频尺寸（用于脚本坐标计算）
    void setVideoSize(const Size& size) { m_videoSize = size; }
    Size videoSize() const { return m_videoSize; }

    void setFrameGrabCallback(FrameGrabCallback callback);
    static cv::Mat grabCurrentFrame();

    void setGrayFrameGrabCallback(GrayFrameGrabCallback callback);
    static GrayFrame grabCurrentGrayFrame();

    // 执行脚本文件（返回沙箱 ID）
    int runScript(const std::string& scriptPath, int keyId, const PointF& anchorPos, bool isPress);

    // 执行内联脚本（返回沙箱 ID）
    int runInlineScript(const std::string& script, int keyId, const PointF& anchorPos, bool isPress);

    // 执行自动启动脚本（keyId 从 -1000 开始递减）
    void runAutoStartScript(const std::string& script);

    // 检查脚本是否包含自动启动标记
    static bool isAutoStartScript(const std::string& script);

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
    void setKeyUIPos(const std::string& keyName, double x, double y);

    // Signals (Signal<>)
    Signal<uint32_t, uint8_t, uint16_t, uint16_t> touchRequested;
    Signal<uint8_t, uint16_t> keyRequested;
    Signal<const std::string&, int, int> tipRequested;
    Signal<bool> shotmodeRequested;
    Signal<double, double, double, double> radialParamRequested;
    Signal<> resetviewRequested;
    Signal<> resetWheelRequested;
    Signal<const std::string&, bool> simulateKeyRequested;
    Signal<const std::string&, double, double> keyUIPosRequested;
    Signal<> keyMapOverlayUpdateRequested;
    Signal<const std::string&> scriptError;

private:
    void onSandboxFinished(int sandboxId);
    void onKeyUIPosRequested(const std::string& keyName, double x, double y);

private:
    int createSandbox(const std::string& scriptOrPath, int keyId, const PointF& anchorPos,
                      bool isPress, bool isInline);
    void connectSandbox(ScriptSandbox* sandbox);

    Controller* m_controller = nullptr;
    SessionContext* m_sessionContext = nullptr;

    std::string m_scriptBasePath;
    Size m_videoSize = Size(1920, 1080);
    FrameGrabCallback m_frameGrabCallback;

    static FrameGrabCallback s_frameGrabCallback;
    static GrayFrameGrabCallback s_grayFrameGrabCallback;
    static std::mutex s_frameGrabMutex;  // 保护静态回调的互斥锁
    static ScriptEngine* s_activeEngine;  // 当前活跃的引擎（用于防止旧引擎清除新回调）
    static std::atomic<int> s_callInProgress;  // 正在进行的回调调用计数

    // 沙箱管理
    std::unordered_map<int, ScriptSandbox*> m_sandboxes;
    mutable std::mutex m_sandboxMutex;
    std::atomic<int> m_nextSandboxId{1};

    // 自动启动脚本的 keyId 计数器（从 -1000 开始递减）
    int m_autoStartKeyIdCounter = -1000;
};

#endif // SCRIPTENGINE_H
