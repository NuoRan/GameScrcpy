#ifndef CORE_GAMEINPUTPROCESSOR_H
#define CORE_GAMEINPUTPROCESSOR_H

#include "interfaces/IInputProcessor.h"
#include <memory>

class SessionContext;
class Controller;

namespace qsc {
namespace core {

/**
 * @brief 游戏输入处理器适配器 / Game Input Processor Adapter
 *
 * 将 SessionContext 适配为 IInputProcessor 接口。
 * Adapts SessionContext to the IInputProcessor interface.
 * 支持键盘映射、脚本系统、轮盘控制等。
 * Supports key mapping, script system, steer wheel control, etc.
 */
class GameInputProcessor : public IInputProcessor {
public:
    /**
     * @brief 构造函数
     * @param controller 控制器实例（用于发送命令）
     */
    explicit GameInputProcessor(Controller* controller);
    ~GameInputProcessor() override;

    // IInputProcessor 实现
    void processKeyEvent(const InputEvent& event,
                        const Size& frameSize,
                        const Size& showSize) override;
    void processMouseEvent(const InputEvent& event,
                          const Size& frameSize,
                          const Size& showSize) override;
    void processWheelEvent(const InputEvent& event,
                          const Size& frameSize,
                          const Size& showSize) override;
    void loadKeyMap(const std::string& json, bool runAutoStart = true) override;
    void onWindowFocusLost() override;
    void resetState() override;
    void releaseAllTouchPoints() override;
    void setTouchCallback(TouchCallback callback) override;
    void setKeyCallback(KeyCallback callback) override;
    void setCursorGrabCallback(CursorGrabCallback callback) override;
    void setFrameGrabCallback(FrameGrabCallback callback) override;
    void setScriptTipCallback(ScriptTipCallback callback) override;
    void setKeyMapOverlayCallback(KeyMapOverlayCallback callback) override;
    void runAutoStartScripts() override;
    void resetScriptState() override;
    bool isGameMode() const override;
    const char* name() const override { return "GameInput"; }

    // 获取底层 SessionContext（总是从 Controller 获取最新指针）
    SessionContext* sessionContext() const;

private:
    // 安全获取当前 SessionContext（不缓存，防止 updateScript 后悬挂指针）
    SessionContext* currentContext() const;

    Controller* m_controller = nullptr;
    TouchCallback m_touchCallback;
    KeyCallback m_keyCallback;
    CursorGrabCallback m_cursorGrabCallback;
};

} // namespace core
} // namespace qsc

#endif // CORE_GAMEINPUTPROCESSOR_H
