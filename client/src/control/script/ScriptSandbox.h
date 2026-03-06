#ifndef SCRIPTSANDBOX_H
#define SCRIPTSANDBOX_H

#include "GameSignal.h"
#include "GameTypes.h"
#include <string>
#include <atomic>
#include <thread>

class Controller;
class SessionContext;
class ScriptWatchdog;
class JsEngine;

/**
 * @brief 脚本沙箱 - 单个脚本的独立执行环境
 *
 * 特性：
 * - std::thread 独立线程执行
 * - QuickJS JsEngine
 * - 超时看门狗保护
 * - 通过 SessionContext 访问共享状态
 * - 11 个 Signal<> 替代 Qt 信号
 */
class ScriptSandbox
{
public:
    ScriptSandbox(int sandboxId, Controller* controller, SessionContext* ctx);
    ~ScriptSandbox();

    // 禁止拷贝/移动
    ScriptSandbox(const ScriptSandbox&) = delete;
    ScriptSandbox& operator=(const ScriptSandbox&) = delete;

    // 设置脚本内容
    void setScript(const std::string& script);
    void setScriptPath(const std::string& path);
    void setScriptBasePath(const std::string& basePath) { m_scriptBasePath = basePath; }

    // 设置按键参数
    void setKeyId(int keyId) { m_keyId = keyId; }
    void setAnchorPos(const PointF& pos) { m_anchorPos = pos; }
    void setIsPress(bool isPress) { m_isPress = isPress; }

    // 设置超时时间
    void setTimeoutMs(int ms);

    // 启动执行
    void start();

    // 停止执行（优雅停止：设置中断标志）
    void stop();

    // 强制终止
    void forceTerminate();

    // 检查状态
    bool isRunning() const { return m_running.load(); }
    int sandboxId() const { return m_sandboxId; }

    // 清除 SessionContext 引用
    void clearSessionContext();

    // 设置最大触摸点数
    static void setMaxTouchPoints(int max);
    static int maxTouchPoints();

    // ---- 信号 (Signal<>) ----
    Signal<uint32_t, uint8_t, uint16_t, uint16_t> touchRequested;
    Signal<uint8_t, uint16_t> keyRequested;
    Signal<std::string, int, int> tipRequested;
    Signal<bool> shotmodeRequested;
    Signal<double, double, double, double> radialParamRequested;
    Signal<> resetviewRequested;
    Signal<> resetWheelRequested;
    Signal<std::string, bool> simulateKeyRequested;
    Signal<std::string, double, double> keyUIPosRequested;
    Signal<std::string> scriptError;
    Signal<int> finished;

private:
    friend class SandboxScriptApi;
    void onSoftTimeout();
    void onHardTimeout();
    void runScript();
    std::string resolveModulePath(const std::string& modulePath);

    int m_sandboxId;
    Controller* m_controller = nullptr;
    std::atomic<SessionContext*> m_sessionContext{nullptr};

    std::thread m_thread;
    std::atomic<JsEngine*> m_jsEngine{nullptr};
    ScriptWatchdog* m_watchdog = nullptr;

    std::string m_script;
    std::string m_scriptPath;
    std::string m_scriptBasePath;
    bool m_isInlineScript = false;

    int m_keyId = -1;
    PointF m_anchorPos;
    bool m_isPress = true;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};

    static int s_maxTouchPoints;
};

/**
 * @brief 沙箱内的脚本 API
 *
 * 暴露给 JS 的方法，与原 WorkerScriptApi 功能完全一致
 */
class SandboxScriptApi
{
public:
    explicit SandboxScriptApi(ScriptSandbox* sandbox);
    ~SandboxScriptApi() = default;

    void setScriptBasePath(const std::string& path) { m_scriptBasePath = path; }
    void setKeyId(int keyId) { m_keyId = keyId; }
    void setAnchorPos(const PointF& pos) { m_anchorPos = pos; }
    void setIsPress(bool isPress) { m_isPress = isPress; }
    void setSessionContext(SessionContext* ctx) { m_sessionContext.store(ctx); }
    void clearSessionContext() { m_sessionContext.store(nullptr); }

    bool wasRadialParamModified() const { return m_radialParamModified; }
    int keyId() const { return m_keyId; }

    // ---------------------------------------------------------
    // 28 个 API 方法（由 JsBindings 注册到 QuickJS）
    // ---------------------------------------------------------

    void click(double x = -1, double y = -1);
    void holdpress(double x = -1, double y = -1);
    void release();
    void slide(double sx, double sy, double ex, double ey, int delayMs, int num);
    void pinch(double centerX, double centerY, double scale, int durationMs = 300, int steps = 10);

    bool isPress() { return m_isPress; }
    void key(const std::string& keyName, int durationMs = 50);
    void releaseAll();
    void sleep(int ms);
    bool isInterrupted();
    void stop();
    void toast(const std::string& msg, int durationMs = 3000);

    void setGlobal(const std::string& key, const ScriptValue& value);
    ScriptValue getGlobal(const std::string& key);
    void log(const std::string& msg);

    void shotmode(bool gameMode);
    void setRadialParam(double up, double down, double left, double right);
    void resetview();
    void resetwheel();
    PosResult getmousepos();
    KeyPosResult getkeypos(const std::string& keyName);
    ButtonPosResult getbuttonpos(int buttonId);
    int getKeyState(const std::string& keyName);
    void setKeyUIPos(const std::string& keyName, double x, double y, double xoffset = 0, double yoffset = 0);
    FindImageResult findImage(const std::string& imageName,
                           double x1 = 0, double y1 = 0,
                           double x2 = 1, double y2 = 1,
                           double threshold = 0.8);
    FindImageResult findImageByRegion(const std::string& imageName,
                                  int regionId,
                                  double threshold = 0.8);
    void swipeById(int swipeId, int durationMs = 200, int steps = 10);

    // 公开给 JsBindings 使用
    std::string resolveModulePath(const std::string& modulePath);

private:
    void normalizePos(double x, double y, uint16_t& outX, uint16_t& outY);
    int getAndroidKeyCode(const std::string& keyName);
    int getQtKey(const std::string& keyName);
    PointF applyRandomOffset(double x, double y);
    std::vector<PointF> generateSmoothPath(double sx, double sy, double ex, double ey, int steps);

    ScriptSandbox* m_sandbox = nullptr;
    std::atomic<SessionContext*> m_sessionContext{nullptr};
    std::string m_scriptBasePath;

    int m_keyId = -1;
    PointF m_anchorPos;
    bool m_isPress = true;
    bool m_radialParamModified = false;
};

#endif // SCRIPTSANDBOX_H
