#ifndef SCRIPT_BRIDGE_H
#define SCRIPT_BRIDGE_H

#include "GameTypes.h"
#include <opencv2/core.hpp>
#include <functional>
#include <string>
#include <unordered_map>
#include "GameSignal.h"
#include "GrayFrame.h"

class Controller;
class SessionContext;
class ScriptEngine;
class SessionVars;
class KeyMap;
class SteerWheelHandler;
class ViewportHandler;
class FreeLookHandler;
class CursorHandler;
class KeyboardHandler;

/**
 * @brief 脚本 API 桥接器 / Script API Bridge
 *
 * 负责 / Responsible for：
 * - 管理 ScriptEngine 的生命周期 / Managing ScriptEngine lifecycle
 * - 提供所有 script_* API 方法 / Providing all script_* API methods
 * - 连接脚本引擎信号到主线程 / Connecting script engine signals to main thread
 * - 管理帧获取回调 / Managing frame grab callbacks
 *
 * 从 SessionContext 拆分出来，专注于脚本相关功能。
 * Split from SessionContext, focused on script-related functionality.
 */
class ScriptBridge
{
public:
    explicit ScriptBridge(Controller* controller, SessionVars* vars);
    ~ScriptBridge();

    // ========== 脚本引擎访问 ==========

    ScriptEngine* scriptEngine() const { return m_scriptEngine; }

    // 设置 SessionContext（在 SessionContext 构造完成后调用）
    void setSessionContext(SessionContext* ctx);

    // ========== 脚本基础路径 ==========

    void setScriptBasePath(const std::string& path);

    // ========== 视频尺寸（用于脚本坐标计算）==========

    void setVideoSize(const Size& size);

    // ========== 帧获取回调 ==========

    void setFrameGrabCallback(std::function<cv::Mat()> callback);
    void setGrayFrameGrabCallback(GrayFrameGrabCallback callback);
    cv::Mat grabFrame() const;

    // ========== 信号连接 ==========

    void connectScriptTipSignal(std::function<void(const std::string&, int, int)> callback);
    void connectKeyMapOverlayUpdateSignal(std::function<void()> callback);

    // ========== 脚本管理 ==========

    void stopAll();
    void reset();
    void releaseAllScriptTouches();
    void runAutoStartScripts(KeyMap* keyMap);

    // ========== 脚本执行 ==========

    void runInlineScript(const std::string& script, int keyId, const PointF& pos, bool isPress);

    // ========== Handler 设置（用于脚本 API）==========

    void setHandlers(SteerWheelHandler* steerWheel,
                     ViewportHandler* viewport,
                     FreeLookHandler* freeLook,
                     CursorHandler* cursor,
                     KeyboardHandler* keyboard);

    // ========== 脚本 API 方法 ==========

    void script_resetView();
    void script_setSteerWheelCoefficient(double up, double down, double left, double right);
    void script_resetSteerWheelCoefficient();
    void script_resetWheel();
    PointF script_getMousePos(bool cursorCaptured);
    void script_setGameMapMode(bool enter, bool& cursorCaptured, std::function<void()> toggleCallback);
    int script_getKeyState(int qtKey, const std::unordered_map<int, bool>& keyStates);
    int script_getKeyStateByName(const std::string& displayName, KeyMap* keyMap, const std::unordered_map<int, bool>& keyStates);
    KeyPosResult script_getKeyPos(int qtKey, KeyMap* keyMap);
    KeyPosResult script_getKeyPosByName(const std::string& displayName, KeyMap* keyMap);

    Signal<bool> grabCursor;

private:
    void onTipRequested(const std::string& msg, int durationMs, int keyId);
    void onKeyMapOverlayUpdateRequested();

private:
    void setupConnections();

    Controller* m_controller = nullptr;
    SessionVars* m_vars = nullptr;
    ScriptEngine* m_scriptEngine = nullptr;

    // Handler 引用（不持有所有权）
    SteerWheelHandler* m_steerWheelHandler = nullptr;
    ViewportHandler* m_viewportHandler = nullptr;
    FreeLookHandler* m_freeLookHandler = nullptr;
    CursorHandler* m_cursorHandler = nullptr;
    KeyboardHandler* m_keyboardHandler = nullptr;

    // 帧获取回调
    std::function<cv::Mat()> m_frameGrabCallback;

    // 信号回调（避免 lambda 捕获问题）
    std::function<void(const std::string&, int, int)> m_tipCallback;
    std::function<void()> m_overlayUpdateCallback;
};

#endif // SCRIPT_BRIDGE_H
