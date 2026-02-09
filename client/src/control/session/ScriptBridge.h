#ifndef SCRIPT_BRIDGE_H
#define SCRIPT_BRIDGE_H

#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QSize>
#include <QImage>
#include <QVariantMap>
#include <functional>

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
class ScriptBridge : public QObject
{
    Q_OBJECT
public:
    explicit ScriptBridge(QPointer<Controller> controller, SessionVars* vars, QObject* parent = nullptr);
    ~ScriptBridge();

    // ========== 脚本引擎访问 ==========

    ScriptEngine* scriptEngine() const { return m_scriptEngine; }

    // 设置 SessionContext（在 SessionContext 构造完成后调用）
    void setSessionContext(SessionContext* ctx);

    // ========== 脚本基础路径 ==========

    void setScriptBasePath(const QString& path);

    // ========== 视频尺寸（用于脚本坐标计算）==========

    void setVideoSize(const QSize& size);

    // ========== 帧获取回调 ==========

    void setFrameGrabCallback(std::function<QImage()> callback);
    QImage grabFrame() const;

    // ========== 信号连接 ==========

    void connectScriptTipSignal(std::function<void(const QString&, int, int)> callback);
    void connectKeyMapOverlayUpdateSignal(std::function<void()> callback);

    // ========== 脚本管理 ==========

    void stopAll();
    void reset();
    void runAutoStartScripts(KeyMap* keyMap);

    // ========== 脚本执行 ==========

    void runInlineScript(const QString& script, int keyId, const QPointF& pos, bool isPress);

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
    QPointF script_getMousePos(bool cursorCaptured);
    void script_setGameMapMode(bool enter, bool& cursorCaptured, std::function<void()> toggleCallback);
    int script_getKeyState(int qtKey, const QHash<int, bool>& keyStates);
    int script_getKeyStateByName(const QString& displayName, KeyMap* keyMap, const QHash<int, bool>& keyStates);
    QVariantMap script_getKeyPos(int qtKey, KeyMap* keyMap);
    QVariantMap script_getKeyPosByName(const QString& displayName, KeyMap* keyMap);

signals:
    void grabCursor(bool grab);

private slots:
    void onTipRequested(const QString& msg, int durationMs, int keyId);
    void onKeyMapOverlayUpdateRequested();

private:
    void setupConnections();

    QPointer<Controller> m_controller;
    SessionVars* m_vars = nullptr;
    ScriptEngine* m_scriptEngine = nullptr;

    // Handler 引用（不持有所有权）
    SteerWheelHandler* m_steerWheelHandler = nullptr;
    ViewportHandler* m_viewportHandler = nullptr;
    FreeLookHandler* m_freeLookHandler = nullptr;
    CursorHandler* m_cursorHandler = nullptr;
    KeyboardHandler* m_keyboardHandler = nullptr;

    // 帧获取回调
    std::function<QImage()> m_frameGrabCallback;

    // 【修复】信号回调（避免 lambda 捕获问题）
    std::function<void(const QString&, int, int)> m_tipCallback;
    std::function<void()> m_overlayUpdateCallback;
};

#endif // SCRIPT_BRIDGE_H
