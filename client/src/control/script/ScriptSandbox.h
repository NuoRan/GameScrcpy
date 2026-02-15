#ifndef SCRIPTSANDBOX_H
#define SCRIPTSANDBOX_H

#include <QObject>
#include <QThread>
#include <QJSEngine>
#include <QJSValue>
#include <QPointF>
#include <QMutex>
#include <QHash>
#include <QVariantMap>
#include <atomic>

class Controller;
class SessionContext;
class ScriptWatchdog;

/**
 * @brief 脚本沙箱 - 单个脚本的独立执行环境 / Script Sandbox - isolated execution environment for a single script
 *
 * 特性 / Features：
 * - 独立线程执行 / Independent thread execution
 * - 独立 QJSEngine / Independent QJSEngine
 * - 超时看门狗保护 / Timeout watchdog protection
 * - 通过 SessionContext 访问共享状态 / Accesses shared state via SessionContext
 */
class ScriptSandbox : public QObject
{
    Q_OBJECT
public:
    explicit ScriptSandbox(int sandboxId, Controller* controller, SessionContext* ctx,
                           QObject* parent = nullptr);
    ~ScriptSandbox();

    // 设置脚本内容
    void setScript(const QString& script);
    void setScriptPath(const QString& path);
    void setScriptBasePath(const QString& basePath) { m_scriptBasePath = basePath; }

    // 设置按键参数
    void setKeyId(int keyId) { m_keyId = keyId; }
    void setAnchorPos(const QPointF& pos) { m_anchorPos = pos; }
    void setIsPress(bool isPress) { m_isPress = isPress; }

    // 设置超时时间
    void setTimeoutMs(int ms);

    // 启动执行
    void start();

    // 停止执行（优雅停止：设置中断标志）
    void stop();

    // 强制终止（沙箱核心功能：立即终止线程）
    void forceTerminate();

    // 检查状态（真正检查线程是否在运行）
    bool isRunning() const { return m_thread && m_thread->isRunning(); }
    int sandboxId() const { return m_sandboxId; }

    // 清除 SessionContext 引用（SessionContext 销毁时调用）
    void clearSessionContext();

    // 设置最大触摸点数
    static void setMaxTouchPoints(int max);
    static int maxTouchPoints();

signals:
    // 请求主线程执行触摸操作
    void touchRequested(quint32 seqId, quint8 action, quint16 x, quint16 y);
    void keyRequested(quint8 action, quint16 keycode);

    // 请求显示提示
    void tipRequested(const QString& msg, int durationMs, int keyId);

    // 请求切换模式
    void shotmodeRequested(bool gameMode);

    // 请求调整轮盘参数
    void radialParamRequested(double up, double down, double left, double right);

    // 请求重置视角
    void resetviewRequested();

    // 请求重置轮盘
    void resetWheelRequested();

    // 请求模拟按键
    void simulateKeyRequested(const QString& keyName, bool press);

    // 请求设置按键 UI 位置
    void keyUIPosRequested(const QString& keyName, double x, double y);

    // 脚本错误
    void scriptError(const QString& error);

    // 脚本执行完成
    void finished(int sandboxId);

private slots:
    void onSoftTimeout();
    void onHardTimeout();
    void doRun();

private:
    friend class SandboxScriptApi;  // 允许 SandboxScriptApi 访问私有成员
    void runScript();
    QString resolveModulePath(const QString& modulePath);

    int m_sandboxId;
    Controller* m_controller = nullptr;
    std::atomic<SessionContext*> m_sessionContext{nullptr};

    QThread* m_thread = nullptr;
    std::atomic<QJSEngine*> m_jsEngine{nullptr};
    ScriptWatchdog* m_watchdog = nullptr;

    QString m_script;
    QString m_scriptPath;
    QString m_scriptBasePath;
    bool m_isInlineScript = false;

    int m_keyId = -1;
    QPointF m_anchorPos;
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
class SandboxScriptApi : public QObject
{
    Q_OBJECT
public:
    explicit SandboxScriptApi(ScriptSandbox* sandbox, QObject* parent = nullptr);

    void setJSEngine(QJSEngine* engine) { m_jsEngine = engine; }
    void setScriptBasePath(const QString& path) { m_scriptBasePath = path; }
    void setKeyId(int keyId) { m_keyId = keyId; }
    void setAnchorPos(const QPointF& pos) { m_anchorPos = pos; }
    void setIsPress(bool isPress) { m_isPress = isPress; }
    void setSessionContext(SessionContext* ctx) { m_sessionContext.store(ctx); }
    void clearSessionContext() { m_sessionContext.store(nullptr); }  // SessionContext 销毁时调用

    // 检查轮盘系数是否被修改过
    bool wasRadialParamModified() const { return m_radialParamModified; }
    int keyId() const { return m_keyId; }

    // ---------------------------------------------------------
    // 暴露给 JS 的方法（与 WorkerScriptApi 完全一致）
    // ---------------------------------------------------------

    Q_INVOKABLE void click(double x = -1, double y = -1);
    Q_INVOKABLE void holdpress(double x = -1, double y = -1);
    Q_INVOKABLE void release();
    Q_INVOKABLE void slide(double sx, double sy, double ex, double ey, int delayMs, int num);
    Q_INVOKABLE void pinch(double centerX, double centerY, double scale, int durationMs = 300, int steps = 10);

    Q_INVOKABLE bool isPress() { return m_isPress; }
    Q_INVOKABLE void key(const QString& keyName, int durationMs = 50);
    Q_INVOKABLE void releaseAll();
    Q_INVOKABLE void sleep(int ms);
    Q_INVOKABLE bool isInterrupted();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void toast(const QString& msg, int durationMs = 3000);

    Q_INVOKABLE void setGlobal(const QString& key, const QJSValue& value);
    Q_INVOKABLE QJSValue getGlobal(const QString& key);
    Q_INVOKABLE QJSValue loadModule(const QString& modulePath);
    Q_INVOKABLE void log(const QString& msg);

    Q_INVOKABLE void shotmode(bool gameMode);
    Q_INVOKABLE void setRadialParam(double up, double down, double left, double right);
    Q_INVOKABLE void resetview();
    Q_INVOKABLE void resetwheel();
    Q_INVOKABLE QVariantMap getmousepos();
    Q_INVOKABLE QVariantMap getkeypos(const QString& keyName);
    Q_INVOKABLE QVariantMap getbuttonpos(int buttonId);
    Q_INVOKABLE int getKeyState(const QString& keyName);
    Q_INVOKABLE void setKeyUIPos(const QString& keyName, double x, double y, double xoffset = 0, double yoffset = 0);
    Q_INVOKABLE QVariantMap findImage(const QString& imageName,
                                       double x1 = 0, double y1 = 0,
                                       double x2 = 1, double y2 = 1,
                                       double threshold = 0.8);

    // 按选区编号找图的重载
    Q_INVOKABLE QVariantMap findImageByRegion(const QString& imageName,
                                              int regionId,
                                              double threshold = 0.8);

    // 按滑动编号执行滑动
    Q_INVOKABLE void swipeById(int swipeId, int durationMs = 200, int steps = 10);

signals:
    void touchRequested(quint32 seqId, quint8 action, quint16 x, quint16 y);
    void keyRequested(quint8 action, quint16 keycode);
    void tipRequested(const QString& msg, int durationMs, int keyId);
    void shotmodeRequested(bool gameMode);
    void radialParamRequested(double up, double down, double left, double right);
    void resetviewRequested();
    void resetWheelRequested();
    void simulateKeyRequested(const QString& keyName, bool press);
    void keyUIPosRequested(const QString& keyName, double x, double y);

private:
    void normalizePos(double x, double y, quint16& outX, quint16& outY);
    int getAndroidKeyCode(const QString& keyName);
    int getQtKey(const QString& keyName);
    QString resolveModulePath(const QString& modulePath);
    QPointF applyRandomOffset(double x, double y);
    QVector<QPointF> generateSmoothPath(double sx, double sy, double ex, double ey, int steps);

    ScriptSandbox* m_sandbox = nullptr;
    std::atomic<SessionContext*> m_sessionContext{nullptr};
    QJSEngine* m_jsEngine = nullptr;
    QString m_scriptBasePath;

    int m_keyId = -1;
    QPointF m_anchorPos;
    bool m_isPress = true;
    bool m_radialParamModified = false;

    QHash<QString, QJSValue> m_moduleCache;
};

#endif // SCRIPTSANDBOX_H
