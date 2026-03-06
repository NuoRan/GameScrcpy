/**
 * @file SessionContext.h
 * @brief 设备会话上下文 / Device Session Context
 *
 * Copyright (C) 2019-2026 Rankun
 * Licensed under the Apache License, Version 2.0
 *
 * 会话上下文是设备连接的核心协调器，负责 / Session context is the core coordinator, responsible for:
 * - 协调控制器、输入处理器、脚本引擎等组件 / Coordinating controller, input processor, script engine
 * - 管理键位映射和会话变量 / Managing key mappings and session variables
 * - 处理键鼠事件分发 / Handling keyboard/mouse event dispatch
 */

#ifndef CONTROL_SESSIONCONTEXT_H
#define CONTROL_SESSIONCONTEXT_H

#include "GameTypes.h"
#include <opencv2/core.hpp>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

#include "GameSignal.h"
#include "keymap.h"
#include "GrayFrame.h"

// 前向声明
class Controller;
class SessionVars;
class ScriptBridge;
class InputDispatcher;
class ScriptEngine;
class HandlerChain;
class SteerWheelHandler;
class ViewportHandler;
class FreeLookHandler;
class CursorHandler;
class KeyboardHandler;
struct InputEvent;

/**
 * @brief 设备会话上下文 (门面/协调器) / Device Session Context (Facade/Coordinator)
 *
 * 职责 / Responsibilities:
 * - 作为会话的门面类，协调各子组件 / Acts as session facade, coordinating sub-components
 * - 持有 SessionVars (会话变量) / Holds SessionVars (session variables)
 * - 持有 ScriptBridge (脚本 API) / Holds ScriptBridge (script API)
 * - 持有 InputDispatcher (输入分发) / Holds InputDispatcher (input dispatch)
 * - 持有 HandlerChain 和各个 Handler / Holds HandlerChain and individual handlers
 *
 * 设计原则 / Design principles:
 * - 每个设备一个独立实例 / One independent instance per device
 * - 使用裸指针访问 Controller / Uses raw pointer for Controller access (生命周期由 SessionContext 管理)
 * - 组件化设计，单一职责 / Component-based design, single responsibility
 */
class SessionContext {
public:
    /**
     * @brief 构造函数
     * @param deviceId 设备 ID
     * @param controller 控制器（生命周期由 SessionContext 管理）
     */
    explicit SessionContext(const std::string& deviceId, Controller* controller);
    ~SessionContext();

    // 禁止拷贝
    SessionContext(const SessionContext&) = delete;
    SessionContext& operator=(const SessionContext&) = delete;

    // ========== 设备信息 ==========

    std::string deviceId() const { return m_deviceId; }
    Controller* controller() const { return m_controller; }

    // ========== 子组件访问 ==========

    SessionVars* vars() const { return m_vars; }
    ScriptBridge* scriptBridge() const { return m_scriptBridge; }
    InputDispatcher* inputDispatcher() const { return m_inputDispatcher; }

    // ========== Handler 访问 ==========

    HandlerChain* handlerChain() const { return m_handlerChain; }
    SteerWheelHandler* steerWheelHandler() const { return m_steerWheelHandler; }
    ViewportHandler* viewportHandler() const { return m_viewportHandler; }
    FreeLookHandler* freeLookHandler() const { return m_freeLookHandler; }
    CursorHandler* cursorHandler() const { return m_cursorHandler; }
    KeyboardHandler* keyboardHandler() const { return m_keyboardHandler; }
    ScriptEngine* scriptEngine() const;

    // ========== 事件处理（委托给 InputDispatcher）==========

    void mouseEvent(const InputEvent& from, const Size& frameSize, const Size& showSize);
    void wheelEvent(const InputEvent& from, const Size& frameSize, const Size& showSize);
    void keyEvent(const InputEvent& from, const Size& frameSize, const Size& showSize);

    // ========== 窗口焦点 ==========

    void onWindowFocusLost();

    // ========== 脚本管理 ==========

    void resetScriptState();
    void runAutoStartScripts();
    bool isCurrentCustomKeymap() const { return true; }

    // ========== KeyMap 管理 ==========

    void loadKeyMap(const std::string& json, bool runAutoStartScripts = true);
    KeyMap* keyMap() { return &m_keyMap; }
    const KeyMap* keyMap() const { return &m_keyMap; }

    // ========== 帧获取回调 ==========

    void setFrameGrabCallback(std::function<cv::Mat()> callback);
    void setGrayFrameGrabCallback(GrayFrameGrabCallback callback);
    cv::Mat grabFrame() const;

    // ========== 信号连接 ==========

    void connectScriptTipSignal(std::function<void(const std::string&, int, int)> callback);
    void connectKeyMapOverlayUpdateSignal(std::function<void()> callback);

    // ========== 尺寸信息 ==========

    void setFrameSize(const Size& size);
    void setShowSize(const Size& size);
    void setMobileSize(const Size& size);
    void setDevicePixelRatio(double dpr);
    Size frameSize() const;
    Size showSize() const;
    Size mobileSize() const;

    // ========== 光标状态（委托给 InputDispatcher）==========

    bool isCursorCaptured() const;
    bool toggleCursorCaptured();
    void setCursorCaptured(bool captured);

    // ========== 脚本 API（委托给 ScriptBridge）==========

    void script_resetView();
    void script_setSteerWheelCoefficient(double up, double down, double left, double right);
    void script_resetSteerWheelCoefficient();
    PointF script_getMousePos();
    void script_setGameMapMode(bool enter);
    void script_resetWheel();
    int script_getKeyState(int qtKey);
    int script_getKeyStateByName(const std::string& displayName);
    KeyPosResult script_getKeyPos(int qtKey);
    KeyPosResult script_getKeyPosByName(const std::string& displayName);
    void script_simulateKey(const std::string& keyName, bool press);

    // ========== 会话变量（委托给 SessionVars）==========

    ScriptValue getVar(const std::string& key, const ScriptValue& defaultValue = ScriptValue()) const;
    void setVar(const std::string& key, const ScriptValue& value);
    bool hasVar(const std::string& key) const;
    void removeVar(const std::string& key);
    void clearVars();

    // ========== 触摸序列 ID（委托给 SessionVars）==========

    void addTouchSeq(int keyId, uint32_t seqId);
    std::vector<uint32_t> takeTouchSeqs(int keyId);
    int touchSeqCount(int keyId) const;
    bool hasTouchSeqs(int keyId) const;
    void clearTouchSeqs();

    // ========== 辅助参数存储（委托给 SessionVars）==========

    void setRadialParamKeyId(const std::string& keyId);
    std::string radialParamKeyId() const;

    // ========== 工具函数 ==========

    PointF calcFrameAbsolutePos(PointF relativePos) const;
    PointF calcScreenAbsolutePos(PointF relativePos) const;
    void sendKeyEvent(int action, int keyCode);

    Signal<bool> grabCursor;
    Signal<const std::string&, int, int> scriptTipRequested;
    Signal<> keyMapOverlayUpdateRequested;

private:
    void initComponents();
    int keyNameToQtKey(const std::string& keyName);

    std::string m_deviceId;
    Controller* m_controller = nullptr;  // 裸指针，生命周期由 SessionContext 管理

    // ===== 子组件 =====
    SessionVars* m_vars = nullptr;
    ScriptBridge* m_scriptBridge = nullptr;
    InputDispatcher* m_inputDispatcher = nullptr;

    // ===== Handler 组件 =====
    HandlerChain* m_handlerChain = nullptr;
    SteerWheelHandler* m_steerWheelHandler = nullptr;
    ViewportHandler* m_viewportHandler = nullptr;
    FreeLookHandler* m_freeLookHandler = nullptr;
    CursorHandler* m_cursorHandler = nullptr;
    KeyboardHandler* m_keyboardHandler = nullptr;

    // ===== KeyMap =====
    KeyMap m_keyMap;
};

#endif // CONTROL_SESSIONCONTEXT_H
