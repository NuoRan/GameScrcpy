#ifndef KEYBOARDHANDLER_H
#define KEYBOARDHANDLER_H

#include "IInputHandler.h"
#include "keymap.h"  // 需要 KeyMap::KeyMapNode
#include "input.h"  // AndroidKeyeventAction, AndroidKeycode

class Controller;
class SessionContext;

/**
 * @brief 键盘按键处理器 / Keyboard Key Handler
 *
 * 处理键盘按键映射 / Handles keyboard key mapping:
 * - KMT_ANDROID_KEY：映射到指定的 Android 按键 / Maps to specified Android key
 * - 默认转发：未映射的按键尝试转换为 Android 按键 / Default forward: unmapped keys converted to Android keys
 *
 * 使用 FastMsg 协议发送按键事件。/ Uses FastMsg protocol for sending key events.
 */
class KeyboardHandler : public IInputHandler
{
public:
    KeyboardHandler();
    ~KeyboardHandler();

    void init(Controller* controller, SessionContext* context) override;

    // IInputHandler interface
    bool handleKeyEvent(const InputEvent& event, const Size& frameSize, const Size& showSize) override;
    bool handleMouseEvent(const InputEvent& event, const Size& frameSize, const Size& showSize) override;
    bool handleWheelEvent(const InputEvent& event, const Size& frameSize, const Size& showSize) override;
    void onFocusLost() override;
    void reset() override;
    int priority() const override { return 200; }  // 最低优先级，作为默认处理
    std::string name() const override { return "KeyboardHandler"; }

    void setKeyMap(KeyMap* keyMap) { m_keyMap = keyMap; }

    // ========== 键盘处理核心接口（由 SessionContext 调用）==========

    // 处理映射的 Android 按键
    void processAndroidKey(AndroidKeycode androidKey, const InputEvent& event);

    // 处理默认按键转发（未映射的按键）
    void processDefaultKey(const InputEvent& event);

private:
    void sendKeyEvent(AndroidKeyeventAction action, AndroidKeycode keyCode);
    AndroidKeycode convertKeyCode(int key, uint32_t modifiers);

    KeyMap* m_keyMap = nullptr;
};

#endif // KEYBOARDHANDLER_H
