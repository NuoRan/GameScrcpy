#ifndef COMMON_INPUTEVENT_H
#define COMMON_INPUTEVENT_H

#include <cstdint>

/**
 * @brief 输入事件类型 — 替代 QEvent::Type 中的输入事件类型
 *
 * Input event types — replaces QEvent::Type for input events.
 */
enum class InputEventType : uint8_t {
    KeyPress,
    KeyRelease,
    MouseMove,
    MousePress,
    MouseRelease,
    MouseWheel,
};

/**
 * @brief 修饰键标志 — 值兼容 Qt::KeyboardModifier
 *
 * Modifier flags — value-compatible with Qt::KeyboardModifier.
 */
namespace InputModifier {
    constexpr uint32_t None    = 0x00000000;
    constexpr uint32_t Shift   = 0x02000000;  // Qt::ShiftModifier
    constexpr uint32_t Ctrl    = 0x04000000;  // Qt::ControlModifier
    constexpr uint32_t Alt     = 0x08000000;  // Qt::AltModifier
    constexpr uint32_t Meta    = 0x10000000;  // Qt::MetaModifier
}

/**
 * @brief 鼠标按键标志 — 值兼容 Qt::MouseButton
 *
 * Mouse button flags — value-compatible with Qt::MouseButton.
 */
namespace InputButton {
    constexpr uint32_t None     = 0x00000000;
    constexpr uint32_t Left     = 0x00000001;  // Qt::LeftButton
    constexpr uint32_t Right    = 0x00000002;  // Qt::RightButton
    constexpr uint32_t Middle   = 0x00000004;  // Qt::MiddleButton
    constexpr uint32_t Back     = 0x00000008;  // Qt::BackButton
    constexpr uint32_t Forward  = 0x00000010;  // Qt::ForwardButton
}

/**
 * @brief 轻量级输入事件 — 替代 QKeyEvent / QMouseEvent / QWheelEvent
 *
 * POD 结构体，栈分配，无虚函数，无堆分配开销。
 * 整数值与 Qt::Key / Qt::KeyboardModifier / Qt::MouseButton 兼容，
 * 但不依赖 QObject 或 Qt 事件系统。
 *
 * Lightweight POD input event — replaces QKeyEvent/QMouseEvent/QWheelEvent.
 * Integer values are compatible with Qt::Key, Qt::KeyboardModifier, Qt::MouseButton.
 */
struct InputEvent {
    InputEventType type = InputEventType::KeyPress;

    // === 键盘数据 / Key data ===
    int32_t key = 0;                // 键码 (Qt::Key 兼容)
    uint32_t modifiers = 0;         // 修饰键标志 (InputModifier::*)
    bool isAutoRepeat = false;      // 自动重复

    // === 鼠标数据 / Mouse data ===
    double localX = 0.0;            // 控件相对 X
    double localY = 0.0;            // 控件相对 Y
    double globalX = 0.0;           // 屏幕全局 X
    double globalY = 0.0;           // 屏幕全局 Y
    uint32_t button = 0;            // 触发按键 (InputButton::*)
    uint32_t buttons = 0;           // 当前按住的按键 (InputButton flags)

    // === 滚轮数据 / Wheel data ===
    int32_t wheelDelta = 0;         // angleDelta.y()

    // === 便捷判断 / Convenience ===
    bool isPress() const {
        return type == InputEventType::KeyPress || type == InputEventType::MousePress;
    }
    bool isRelease() const {
        return type == InputEventType::KeyRelease || type == InputEventType::MouseRelease;
    }
    bool isMouseEvent() const {
        return type == InputEventType::MouseMove ||
               type == InputEventType::MousePress ||
               type == InputEventType::MouseRelease;
    }
    bool isKeyEvent() const {
        return type == InputEventType::KeyPress || type == InputEventType::KeyRelease;
    }
};

#endif // COMMON_INPUTEVENT_H
