#ifndef CURSORHANDLER_H
#define CURSORHANDLER_H

#include "IInputHandler.h"
#include "GameTypes.h"

class Controller;
class SessionContext;

/**
 * @brief 光标模式处理器 / Cursor Mode Handler
 *
 * 处理光标显示时的鼠标点击 / Handles mouse clicks when cursor is visible:
 * - 左键按下/释放/移动 → 触摸事件 / Left button down/up/move → touch events
 * - 中键和右键不响应 / Middle and right buttons are filtered
 * - 使用 FastMsg 协议发送触摸 / Uses FastMsg protocol for sending touches
 */
class CursorHandler : public IInputHandler
{
public:
    CursorHandler();
    ~CursorHandler();

    void init(Controller* controller, SessionContext* context) override;

    // IInputHandler interface
    bool handleKeyEvent(const InputEvent& event, const Size& frameSize, const Size& showSize) override;
    bool handleMouseEvent(const InputEvent& event, const Size& frameSize, const Size& showSize) override;
    bool handleWheelEvent(const InputEvent& event, const Size& frameSize, const Size& showSize) override;
    void onFocusLost() override;
    void reset() override;
    int priority() const override { return 50; }  // 光标模式优先级较低
    std::string name() const override { return "CursorHandler"; }

    // ========== 光标模式核心接口（由 SessionContext 调用）==========

    // 处理鼠标事件（光标显示模式）
    void processMouseEvent(const InputEvent& event, const Size& showSize);

    // 获取当前光标位置（供脚本 API 使用）
    PointF lastPos() const { return m_state.lastPos; }

    // 是否正在触摸
    bool isTouching() const { return m_state.touching; }

private:
    void sendFastTouch(uint8_t action, const PointF& pos);

    Size m_showSize;

    // 光标模式状态（与原版 m_ctrlCursor 完全一致）
    struct {
        bool touching = false;             // 是否正在触摸
        PointF lastPos;                   // 最后触摸位置（归一化坐标）
        uint32_t fastTouchSeqId = 0;        // FastMsg 序列 ID
    } m_state;
};

#endif // CURSORHANDLER_H
