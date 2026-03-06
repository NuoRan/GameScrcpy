#include "ScriptEngine.h"
#include "ScriptSandbox.h"
#include "controller.h"
#include "SessionContext.h"
#include "KeyMapOverlay.h"
#include "StringUtils.h"

#define LOG_TAG "ScriptEngine"
#include "Logger.h"
#include <mutex>
#include "ElapsedTimer.h"
#include <regex>
#include <sstream>
#include <thread>
#include <chrono>

// 静态成员初始化
FrameGrabCallback ScriptEngine::s_frameGrabCallback;
GrayFrameGrabCallback ScriptEngine::s_grayFrameGrabCallback;
std::mutex ScriptEngine::s_frameGrabMutex;
ScriptEngine* ScriptEngine::s_activeEngine = nullptr;
std::atomic<int> ScriptEngine::s_callInProgress{0};

ScriptEngine::ScriptEngine(Controller* controller, SessionContext* ctx)
    : m_controller(controller)
    , m_sessionContext(ctx)
{
    std::lock_guard<std::mutex> locker(s_frameGrabMutex);
    s_activeEngine = this;
}

void ScriptEngine::setFrameGrabCallback(FrameGrabCallback callback)
{
    if (callback) {
        std::lock_guard<std::mutex> locker(s_frameGrabMutex);
        m_frameGrabCallback = callback;
        s_frameGrabCallback = callback;
        s_activeEngine = this;
    } else {
        {
            std::lock_guard<std::mutex> locker(s_frameGrabMutex);
            m_frameGrabCallback = nullptr;
            if (s_activeEngine == this) {
                s_frameGrabCallback = nullptr;
            }
        }

        // 等待正在进行的回调调用完成
        int waitCount = 0;
        while (s_callInProgress.load() > 0 && waitCount < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            waitCount++;
        }
    }
}

cv::Mat ScriptEngine::grabCurrentFrame()
{
    FrameGrabCallback callback;
    {
        std::lock_guard<std::mutex> locker(s_frameGrabMutex);
        callback = s_frameGrabCallback;
        if (callback) {
            s_callInProgress.fetch_add(1);
        }
    }

    if (callback) {
        cv::Mat result = callback();
        s_callInProgress.fetch_sub(1);
        return result;
    }
    return cv::Mat();
}

void ScriptEngine::setGrayFrameGrabCallback(GrayFrameGrabCallback callback)
{
    std::lock_guard<std::mutex> locker(s_frameGrabMutex);
    if (callback) {
        s_grayFrameGrabCallback = std::move(callback);
        s_activeEngine = this;
    } else {
        if (s_activeEngine == this) {
            s_grayFrameGrabCallback = nullptr;
        }
    }
}

GrayFrame ScriptEngine::grabCurrentGrayFrame()
{
    GrayFrameGrabCallback callback;
    {
        std::lock_guard<std::mutex> locker(s_frameGrabMutex);
        callback = s_grayFrameGrabCallback;
        if (callback) {
            s_callInProgress.fetch_add(1);
        }
    }

    if (callback) {
        GrayFrame result = callback();
        s_callInProgress.fetch_sub(1);
        return result;
    }
    return {};
}

void ScriptEngine::setSessionContext(SessionContext* ctx)
{
    m_sessionContext = ctx;

    // 如果 ctx 为 nullptr，通知所有沙箱清除引用（防止访问已销毁的对象）
    if (!ctx) {
        std::lock_guard<std::mutex> locker(m_sandboxMutex);
        for (auto& [id, sandbox] : m_sandboxes) {
            sandbox->clearSessionContext();
        }
    }
}

ScriptEngine::~ScriptEngine()
{
    stopAll();

    {
        std::lock_guard<std::mutex> locker(s_frameGrabMutex);
        if (s_activeEngine == this) {
            s_frameGrabCallback = nullptr;
            s_grayFrameGrabCallback = nullptr;
            s_activeEngine = nullptr;
        }
        m_frameGrabCallback = nullptr;
    }

    std::lock_guard<std::mutex> sandboxLocker(m_sandboxMutex);
    for (auto& [id, sb] : m_sandboxes) delete sb;
    m_sandboxes.clear();
}

int ScriptEngine::runScript(const std::string& scriptPath, int keyId, const PointF& anchorPos, bool isPress)
{
    return createSandbox(scriptPath, keyId, anchorPos, isPress, false);
}

int ScriptEngine::runInlineScript(const std::string& script, int keyId, const PointF& anchorPos, bool isPress)
{
    return createSandbox(script, keyId, anchorPos, isPress, true);
}

void ScriptEngine::runAutoStartScript(const std::string& script)
{
    // 自动启动脚本使用递减的 keyId，避免与普通按键冲突
    int keyId = m_autoStartKeyIdCounter--;
    createSandbox(script, keyId, PointF(0.5, 0.5), true, true);
}

bool ScriptEngine::isAutoStartScript(const std::string& script)
{
    // 检查脚本是否包含自动启动标记
    // 支持: // @autoStart 或 // @自动启动
    // 逐行匹配 (std::regex 无 multiline 支持)
    static std::regex autoStartPattern(
        R"(^\s*//\s*@(autoStart)\s*$)",
        std::regex_constants::icase
    );
    std::istringstream stream(script);
    std::string line;
    while (std::getline(stream, line)) {
        // Trim trailing \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Trim whitespace for comparison
        size_t start = line.find_first_not_of(" \t");
        std::string trimmed = (start == std::string::npos) ? "" : line.substr(start);
        size_t end = trimmed.find_last_not_of(" \t");
        if (end != std::string::npos) trimmed = trimmed.substr(0, end + 1);

        if (std::regex_match(trimmed, autoStartPattern))
            return true;
        // 中文标记直接字符串匹配
        if (trimmed.rfind("//", 0) == 0 && trimmed.find("@\xe8\x87\xaa\xe5\x8a\xa8\xe5\x90\xaf\xe5\x8a\xa8") != std::string::npos)
            return true;
    }
    return false;
}

int ScriptEngine::createSandbox(const std::string& scriptOrPath, int keyId, const PointF& anchorPos,
                                 bool isPress, bool isInline)
{
    int sandboxId = m_nextSandboxId.fetch_add(1);

    ScriptSandbox* sandbox = new ScriptSandbox(sandboxId, m_controller, m_sessionContext);

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
        std::lock_guard<std::mutex> locker(m_sandboxMutex);
        m_sandboxes[sandboxId] = sandbox;
    }

    sandbox->start();

    return sandboxId;
}

void ScriptEngine::connectSandbox(ScriptSandbox* sandbox)
{
    // Signal<> 直接连接（替代 QObject::connect）
    sandbox->finished.connect(
        [this](int id) { onSandboxFinished(id); });

    sandbox->touchRequested.connect(
        [this](uint32_t seqId, uint8_t action, uint16_t x, uint16_t y) {
            touchRequested.fire(seqId, action, x, y);
        });
    sandbox->keyRequested.connect(
        [this](uint8_t action, uint16_t keycode) {
            keyRequested.fire(action, keycode);
        });
    sandbox->tipRequested.connect(
        [this](const std::string& msg, int durationMs, int keyId) {
            tipRequested.fire(msg, durationMs, keyId);
        });
    sandbox->shotmodeRequested.connect(
        [this](bool gameMode) {
            shotmodeRequested.fire(gameMode);
        });
    sandbox->radialParamRequested.connect(
        [this](double up, double down, double left, double right) {
            radialParamRequested.fire(up, down, left, right);
        });
    sandbox->resetviewRequested.connect(
        [this]() { resetviewRequested.fire(); });
    sandbox->resetWheelRequested.connect(
        [this]() { resetWheelRequested.fire(); });
    sandbox->simulateKeyRequested.connect(
        [this](const std::string& keyName, bool press) {
            simulateKeyRequested.fire(keyName, press);
        });
    sandbox->scriptError.connect(
        [this](const std::string& error) {
            scriptError.fire(error);
        });
    sandbox->keyUIPosRequested.connect(
        [this](const std::string& keyName, double x, double y) {
            onKeyUIPosRequested(keyName, x, y);
        });
}

void ScriptEngine::stopSandbox(int sandboxId)
{
    std::lock_guard<std::mutex> locker(m_sandboxMutex);
    if (m_sandboxes.count(sandboxId)) {
        m_sandboxes[sandboxId]->stop();
    }
}

void ScriptEngine::stopAll()
{
    {
        std::lock_guard<std::mutex> locker(m_sandboxMutex);
        for (auto& [id, sandbox] : m_sandboxes) {
            sandbox->stop();
        }
    }

    ElapsedTimer timer;
    timer.start();
    const int maxWaitMs = 3000;

    while (timer.elapsed() < maxWaitMs) {
        bool allStopped = true;
        {
            std::lock_guard<std::mutex> locker(m_sandboxMutex);
            for (auto& [id, sandbox] : m_sandboxes) {
                if (sandbox->isRunning()) {
                    allStopped = false;
                    break;
                }
            }
        }

        if (allStopped) return;

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    {
        std::lock_guard<std::mutex> locker(m_sandboxMutex);
        for (auto& [id, sandbox] : m_sandboxes) {
            if (sandbox->isRunning()) {
                sandbox->forceTerminate();
            }
        }
    }
}

bool ScriptEngine::hasRunningSandboxes() const
{
    std::lock_guard<std::mutex> locker(m_sandboxMutex);
    for (auto& [id, sandbox] : m_sandboxes) {
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

void ScriptEngine::setKeyUIPos(const std::string& keyName, double x, double y)
{
    // 直接使用显示名称作为键（支持 "Ctrl+J", "Tab", "=" 等组合键）
    KeyMapOverlay::setKeyPosOverride(strutil::toQ(keyName), x, y);
    keyMapOverlayUpdateRequested.fire();
}

void ScriptEngine::onSandboxFinished(int sandboxId)
{
    std::lock_guard<std::mutex> locker(m_sandboxMutex);
    auto it = m_sandboxes.find(sandboxId);
    if (it != m_sandboxes.end()) {
        ScriptSandbox* sandbox = it->second;
        m_sandboxes.erase(it);
        delete sandbox;
    }
}

// ---------------------------------------------------------
// 信号转发槽函数（P-KCP: 大部分已改为直接信号→信号连接，仅保留有额外逻辑的槽）
// ---------------------------------------------------------

void ScriptEngine::onKeyUIPosRequested(const std::string& keyName, double x, double y)
{
    // 设置位置并触发更新
    setKeyUIPos(keyName, x, y);
}
