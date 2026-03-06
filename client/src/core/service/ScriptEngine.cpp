#include "ScriptEngine.h"
#define LOG_TAG "CoreScriptEngine"
#include "Logger.h"
#include <nlohmann/json.hpp>

namespace qsc {
namespace core {

ScriptEngine::ScriptEngine()
{
    LOG_I("[ScriptEngine] Created");
}

ScriptEngine::~ScriptEngine()
{
    LOG_I("[ScriptEngine] Destroyed");
}

void ScriptEngine::loadScript(const std::string& jsonConfig, bool runAutoStart)
{
    m_currentScript = jsonConfig;

    // 解析 JSON 判断是否为游戏键位
    try {
        auto root = nlohmann::json::parse(jsonConfig);
        m_customKeymapActive = root.is_object() && root.contains("keyMapNodes");
    } catch (...) {
        m_customKeymapActive = false;
    }

    LOG_I("[ScriptEngine] Script loaded, customKeymap: %s", m_customKeymapActive ? "yes" : "no");

    if (m_keyMapOverlayCallback) {
        m_keyMapOverlayCallback();
    }

    if (runAutoStart) {
        runAutoStartScripts();
    }
}

void ScriptEngine::resetState()
{
    LOG_I("[ScriptEngine] State reset");
    // 重置脚本执行状态（如定时器、变量等）
}

void ScriptEngine::runAutoStartScripts()
{
    LOG_I("[ScriptEngine] Running auto-start scripts");
    // 执行自动启动脚本
    // 当前实现：此功能通过 Controller 的 QJSEngine 执行
}

bool ScriptEngine::isCustomKeymapActive() const
{
    return m_customKeymapActive;
}

void ScriptEngine::setFrameGrabCallback(FrameGrabCallback callback)
{
    m_frameGrabCallback = std::move(callback);
}

void ScriptEngine::setScriptTipCallback(ScriptTipCallback callback)
{
    m_scriptTipCallback = std::move(callback);
}

void ScriptEngine::setKeyMapOverlayCallback(KeyMapOverlayCallback callback)
{
    m_keyMapOverlayCallback = std::move(callback);
}

} // namespace core
} // namespace qsc
