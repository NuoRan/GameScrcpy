#include "ScriptEngine.h"
#include "ScriptSandbox.h"
#include "controller.h"
#include "SessionContext.h"
#include "KeyMapOverlay.h"

#include <QDebug>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QThread>

// 静态成员初始化
FrameGrabCallback ScriptEngine::s_frameGrabCallback;
QMutex ScriptEngine::s_frameGrabMutex;
ScriptEngine* ScriptEngine::s_activeEngine = nullptr;
std::atomic<int> ScriptEngine::s_callInProgress{0};

ScriptEngine::ScriptEngine(Controller* controller, SessionContext* ctx, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_sessionContext(ctx)
{
    QMutexLocker locker(&s_frameGrabMutex);
    s_activeEngine = this;
}

void ScriptEngine::setFrameGrabCallback(FrameGrabCallback callback)
{
    if (callback) {
        QMutexLocker locker(&s_frameGrabMutex);
        m_frameGrabCallback = callback;
        s_frameGrabCallback = callback;
        s_activeEngine = this;
    } else {
        {
            QMutexLocker locker(&s_frameGrabMutex);
            m_frameGrabCallback = nullptr;
            if (s_activeEngine == this) {
                s_frameGrabCallback = nullptr;
            }
        }

        // 等待正在进行的回调调用完成
        int waitCount = 0;
        while (s_callInProgress.load() > 0 && waitCount < 100) {
            QThread::msleep(10);
            waitCount++;
        }
    }
}

QImage ScriptEngine::grabCurrentFrame()
{
    FrameGrabCallback callback;
    {
        QMutexLocker locker(&s_frameGrabMutex);
        callback = s_frameGrabCallback;
        if (callback) {
            s_callInProgress.fetch_add(1);
        }
    }

    if (callback) {
        QImage result = callback();
        s_callInProgress.fetch_sub(1);
        return result;
    }
    return QImage();
}

void ScriptEngine::setSessionContext(SessionContext* ctx)
{
    m_sessionContext = ctx;

    // 如果 ctx 为 nullptr，通知所有沙箱清除引用（防止访问已销毁的对象）
    if (!ctx) {
        QMutexLocker locker(&m_sandboxMutex);
        for (ScriptSandbox* sandbox : m_sandboxes) {
            sandbox->clearSessionContext();
        }
    }
}

ScriptEngine::~ScriptEngine()
{
    stopAll();
    disconnect();

    {
        QMutexLocker locker(&s_frameGrabMutex);
        if (s_activeEngine == this) {
            s_frameGrabCallback = nullptr;
            s_activeEngine = nullptr;
        }
        m_frameGrabCallback = nullptr;
    }

    QMutexLocker sandboxLocker(&m_sandboxMutex);
    qDeleteAll(m_sandboxes);
    m_sandboxes.clear();
}

int ScriptEngine::runScript(const QString& scriptPath, int keyId, const QPointF& anchorPos, bool isPress)
{
    return createSandbox(scriptPath, keyId, anchorPos, isPress, false);
}

int ScriptEngine::runInlineScript(const QString& script, int keyId, const QPointF& anchorPos, bool isPress)
{
    return createSandbox(script, keyId, anchorPos, isPress, true);
}

void ScriptEngine::runAutoStartScript(const QString& script)
{
    // 自动启动脚本使用递减的 keyId，避免与普通按键冲突
    int keyId = m_autoStartKeyIdCounter--;
    createSandbox(script, keyId, QPointF(0.5, 0.5), true, true);
}

bool ScriptEngine::isAutoStartScript(const QString& script)
{
    // 检查脚本是否包含自动启动标记
    // 支持: // @autoStart 或 // @自动启动
    static QRegularExpression autoStartPattern(
        R"(^\s*//\s*@(autoStart|自动启动)\s*$)",
        QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption
    );
    return autoStartPattern.match(script).hasMatch();
}

int ScriptEngine::createSandbox(const QString& scriptOrPath, int keyId, const QPointF& anchorPos,
                                 bool isPress, bool isInline)
{
    int sandboxId = m_nextSandboxId.fetch_add(1);

    ScriptSandbox* sandbox = new ScriptSandbox(sandboxId, m_controller, m_sessionContext, this);

    if (isInline) {
        sandbox->setScript(scriptOrPath);
    } else {
        sandbox->setScriptPath(scriptOrPath);
    }

    sandbox->setScriptBasePath(m_scriptBasePath);
    sandbox->setKeyId(keyId);
    sandbox->setAnchorPos(anchorPos);
    sandbox->setIsPress(isPress);

    connectSandbox(sandbox);

    {
        QMutexLocker locker(&m_sandboxMutex);
        m_sandboxes.insert(sandboxId, sandbox);
    }

    sandbox->start();

    return sandboxId;
}

void ScriptEngine::connectSandbox(ScriptSandbox* sandbox)
{
    // 完成信号
    connect(sandbox, &ScriptSandbox::finished, this, &ScriptEngine::onSandboxFinished);

    // P-KCP: 直接信号→信号连接，跳过转发槽函数的开销
    connect(sandbox, &ScriptSandbox::touchRequested, this, &ScriptEngine::touchRequested);
    connect(sandbox, &ScriptSandbox::keyRequested, this, &ScriptEngine::keyRequested);
    connect(sandbox, &ScriptSandbox::tipRequested, this, &ScriptEngine::tipRequested);
    connect(sandbox, &ScriptSandbox::shotmodeRequested, this, &ScriptEngine::shotmodeRequested);
    connect(sandbox, &ScriptSandbox::radialParamRequested, this, &ScriptEngine::radialParamRequested);
    connect(sandbox, &ScriptSandbox::resetviewRequested, this, &ScriptEngine::resetviewRequested);
    connect(sandbox, &ScriptSandbox::resetWheelRequested, this, &ScriptEngine::resetWheelRequested);
    connect(sandbox, &ScriptSandbox::simulateKeyRequested, this, &ScriptEngine::simulateKeyRequested);
    connect(sandbox, &ScriptSandbox::scriptError, this, &ScriptEngine::scriptError);

    // keyUIPosRequested 有额外逻辑，保留槽转发
    connect(sandbox, &ScriptSandbox::keyUIPosRequested, this, &ScriptEngine::onKeyUIPosRequested);
}

void ScriptEngine::stopSandbox(int sandboxId)
{
    QMutexLocker locker(&m_sandboxMutex);
    if (m_sandboxes.contains(sandboxId)) {
        m_sandboxes[sandboxId]->stop();
    }
}

void ScriptEngine::stopAll()
{
    {
        QMutexLocker locker(&m_sandboxMutex);
        for (ScriptSandbox* sandbox : m_sandboxes) {
            sandbox->stop();
        }
    }

    QElapsedTimer timer;
    timer.start();
    const int maxWaitMs = 3000;

    while (timer.elapsed() < maxWaitMs) {
        bool allStopped = true;
        {
            QMutexLocker locker(&m_sandboxMutex);
            for (const ScriptSandbox* sandbox : m_sandboxes) {
                if (sandbox->isRunning()) {
                    allStopped = false;
                    break;
                }
            }
        }

        if (allStopped) return;

        QThread::msleep(30);
    }

    {
        QMutexLocker locker(&m_sandboxMutex);
        for (ScriptSandbox* sandbox : m_sandboxes) {
            if (sandbox->isRunning()) {
                sandbox->forceTerminate();
            }
        }
    }
}

bool ScriptEngine::hasRunningSandboxes() const
{
    QMutexLocker locker(&m_sandboxMutex);
    for (const ScriptSandbox* sandbox : m_sandboxes) {
        if (sandbox->isRunning()) {
            return true;
        }
    }
    return false;
}

void ScriptEngine::setMaxTouchPoints(int max)
{
    ScriptSandbox::setMaxTouchPoints(max);
}

void ScriptEngine::setKeyUIPos(const QString& keyName, double x, double y)
{
    // 直接使用显示名称作为键（支持 "Ctrl+J", "Tab", "=" 等组合键）
    KeyMapOverlay::setKeyPosOverride(keyName, x, y);
    emit keyMapOverlayUpdateRequested();
}

void ScriptEngine::onSandboxFinished(int sandboxId)
{
    QMutexLocker locker(&m_sandboxMutex);
    if (m_sandboxes.contains(sandboxId)) {
        ScriptSandbox* sandbox = m_sandboxes.take(sandboxId);
        sandbox->deleteLater();
    }
}

// ---------------------------------------------------------
// 信号转发槽函数（P-KCP: 大部分已改为直接信号→信号连接，仅保留有额外逻辑的槽）
// ---------------------------------------------------------

void ScriptEngine::onKeyUIPosRequested(const QString& keyName, double x, double y)
{
    // 设置位置并触发更新
    setKeyUIPos(keyName, x, y);
}
