#ifndef CORE_SCRIPTENGINE_H
#define CORE_SCRIPTENGINE_H

#include <QObject>
#include <QString>
#include <QImage>
#include <functional>
#include <memory>

namespace qsc {
namespace core {

/**
 * @brief 脚本引擎服务 / Script Engine Service
 *
 * 管理键位脚本的加载、执行和状态 / Manages key mapping script loading, execution and state:
 * - 加载/解析键位配置 JSON / Load/parse key binding config JSON
 * - 执行按键映射脚本 / Execute key mapping scripts
 * - 管理自动启动脚本 / Manage auto-start scripts
 * - 提供图像识别 API / Provide image recognition API
 */
class ScriptEngine : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 帧获取回调类型
     */
    using FrameGrabCallback = std::function<QImage()>;

    /**
     * @brief 脚本提示回调类型
     */
    using ScriptTipCallback = std::function<void(const QString& msg, int durationMs, int keyId)>;

    /**
     * @brief 键位覆盖层更新回调类型
     */
    using KeyMapOverlayCallback = std::function<void()>;

    explicit ScriptEngine(QObject* parent = nullptr);
    ~ScriptEngine() override;

    // 禁止拷贝
    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    /**
     * @brief 加载脚本配置
     * @param jsonConfig 键位配置 JSON 字符串
     * @param runAutoStart 是否执行自动启动脚本
     */
    void loadScript(const QString& jsonConfig, bool runAutoStart = true);

    /**
     * @brief 重置脚本状态
     */
    void resetState();

    /**
     * @brief 执行自动启动脚本
     */
    void runAutoStartScripts();

    /**
     * @brief 是否处于游戏键位模式
     */
    bool isCustomKeymapActive() const;

    /**
     * @brief 获取当前脚本配置
     */
    QString currentScript() const { return m_currentScript; }

    /**
     * @brief 设置帧获取回调（用于图像识别）
     */
    void setFrameGrabCallback(FrameGrabCallback callback);

    /**
     * @brief 设置脚本提示回调
     */
    void setScriptTipCallback(ScriptTipCallback callback);

    /**
     * @brief 设置键位覆盖层更新回调
     */
    void setKeyMapOverlayCallback(KeyMapOverlayCallback callback);

signals:
    /**
     * @brief 脚本加载完成
     */
    void scriptLoaded(const QString& scriptName);

    /**
     * @brief 脚本执行错误
     */
    void scriptError(const QString& message);

    /**
     * @brief 脚本提示
     */
    void scriptTip(const QString& msg, int durationMs, int keyId);

    /**
     * @brief 键位覆盖层需要更新
     */
    void keyMapOverlayUpdated();

private:
    QString m_currentScript;
    FrameGrabCallback m_frameGrabCallback;
    ScriptTipCallback m_scriptTipCallback;
    KeyMapOverlayCallback m_keyMapOverlayCallback;
    bool m_customKeymapActive = false;
};

} // namespace core
} // namespace qsc

#endif // CORE_SCRIPTENGINE_H
