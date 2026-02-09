#ifndef CORE_GAMEINPUTPROCESSOR_H
#define CORE_GAMEINPUTPROCESSOR_H

#include "interfaces/IInputProcessor.h"
#include <memory>
#include <QImage>

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
    void processKeyEvent(const QKeyEvent* event,
                        const QSize& frameSize,
                        const QSize& showSize) override;
    void processMouseEvent(const QMouseEvent* event,
                          const QSize& frameSize,
                          const QSize& showSize) override;
    void processWheelEvent(const QWheelEvent* event,
                          const QSize& frameSize,
                          const QSize& showSize) override;
    void loadKeyMap(const QString& json, bool runAutoStart = true) override;
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

    // 获取底层 SessionContext
    SessionContext* sessionContext() const { return m_sessionContext; }

private:
    Controller* m_controller = nullptr;
    SessionContext* m_sessionContext = nullptr;  // 由 Controller 持有
    TouchCallback m_touchCallback;
    KeyCallback m_keyCallback;
    CursorGrabCallback m_cursorGrabCallback;
};

} // namespace core
} // namespace qsc

#endif // CORE_GAMEINPUTPROCESSOR_H
