#include "ScriptEngine.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

namespace qsc {
namespace core {

ScriptEngine::ScriptEngine(QObject* parent)
    : QObject(parent)
{
    qInfo("[ScriptEngine] Created");
}

ScriptEngine::~ScriptEngine()
{
    qInfo("[ScriptEngine] Destroyed");
}

void ScriptEngine::loadScript(const QString& jsonConfig, bool runAutoStart)
{
    m_currentScript = jsonConfig;

    // 解析 JSON 判断是否为游戏键位
    QJsonDocument doc = QJsonDocument::fromJson(jsonConfig.toUtf8());
    if (!doc.isNull() && doc.isObject()) {
        QJsonObject root = doc.object();
        // 如果有 keyMapNodes，则为游戏键位模式
        m_customKeymapActive = root.contains("keyMapNodes");
    } else {
        m_customKeymapActive = false;
    }

    qInfo("[ScriptEngine] Script loaded, customKeymap: %s", m_customKeymapActive ? "yes" : "no");

    emit scriptLoaded(jsonConfig);
    emit keyMapOverlayUpdated();

    if (m_keyMapOverlayCallback) {
        m_keyMapOverlayCallback();
    }

    if (runAutoStart) {
        runAutoStartScripts();
    }
}

void ScriptEngine::resetState()
{
    qInfo("[ScriptEngine] State reset");
    // 重置脚本执行状态（如定时器、变量等）
}

void ScriptEngine::runAutoStartScripts()
{
    qInfo("[ScriptEngine] Running auto-start scripts");
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
