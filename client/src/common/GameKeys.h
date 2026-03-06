#pragma once
/**
 * @file GameKeys.h
 * @brief 自定义键码/鼠标/修饰符枚举，替代 Qt::Key / Qt::MouseButton / Qt::KeyboardModifier
 *
 * 数值与 Qt 完全一致，以保持配置文件兼容性。
 * 所有值来源于 Qt 6 头文件 (qnamespace.h)。
 */
#include <cstdint>

// ============================================================================
// Key codes  (matching Qt::Key values)
// ============================================================================
namespace GameKey {

    // Special
    constexpr int Key_unknown       = 0x00000000;

    // ----- Function / navigation keys (0x0100xxxx range) --------------------
    constexpr int Key_Escape        = 0x01000000;
    constexpr int Key_Tab           = 0x01000001;
    constexpr int Key_Backtab       = 0x01000002;
    constexpr int Key_Backspace     = 0x01000003;
    constexpr int Key_Return        = 0x01000004;
    constexpr int Key_Enter         = 0x01000005;   // numpad enter
    constexpr int Key_Insert        = 0x01000006;
    constexpr int Key_Delete        = 0x01000007;
    constexpr int Key_Pause         = 0x01000008;
    constexpr int Key_Print         = 0x01000009;
    constexpr int Key_SysReq        = 0x0100000a;
    constexpr int Key_Clear         = 0x0100000b;

    constexpr int Key_Home          = 0x01000010;
    constexpr int Key_End           = 0x01000011;
    constexpr int Key_Left          = 0x01000012;
    constexpr int Key_Up            = 0x01000013;
    constexpr int Key_Right         = 0x01000014;
    constexpr int Key_Down          = 0x01000015;
    constexpr int Key_PageUp        = 0x01000016;
    constexpr int Key_PageDown      = 0x01000017;

    // Modifier keys
    constexpr int Key_Shift         = 0x01000020;
    constexpr int Key_Control       = 0x01000021;
    constexpr int Key_Meta          = 0x01000022;
    constexpr int Key_Alt           = 0x01000023;
    constexpr int Key_CapsLock      = 0x01000024;
    constexpr int Key_NumLock       = 0x01000025;
    constexpr int Key_ScrollLock    = 0x01000026;

    // Function keys  (F1 = 0x01000030 … F24 = 0x01000047)
    constexpr int Key_F1            = 0x01000030;
    constexpr int Key_F2            = 0x01000031;
    constexpr int Key_F3            = 0x01000032;
    constexpr int Key_F4            = 0x01000033;
    constexpr int Key_F5            = 0x01000034;
    constexpr int Key_F6            = 0x01000035;
    constexpr int Key_F7            = 0x01000036;
    constexpr int Key_F8            = 0x01000037;
    constexpr int Key_F9            = 0x01000038;
    constexpr int Key_F10           = 0x01000039;
    constexpr int Key_F11           = 0x0100003a;
    constexpr int Key_F12           = 0x0100003b;
    constexpr int Key_F13           = 0x0100003c;
    constexpr int Key_F14           = 0x0100003d;
    constexpr int Key_F15           = 0x0100003e;
    constexpr int Key_F16           = 0x0100003f;
    constexpr int Key_F17           = 0x01000040;
    constexpr int Key_F18           = 0x01000041;
    constexpr int Key_F19           = 0x01000042;
    constexpr int Key_F20           = 0x01000043;
    constexpr int Key_F21           = 0x01000044;
    constexpr int Key_F22           = 0x01000045;
    constexpr int Key_F23           = 0x01000046;
    constexpr int Key_F24           = 0x01000047;

    // Super / Menu
    constexpr int Key_Super_L       = 0x01000053;
    constexpr int Key_Super_R       = 0x01000054;
    constexpr int Key_Menu          = 0x01000055;

    // ----- ASCII-range keys (value == ASCII code) ---------------------------
    constexpr int Key_Space         = 0x20;
    constexpr int Key_Exclam        = 0x21;  // !
    constexpr int Key_QuoteDbl      = 0x22;  // "
    constexpr int Key_NumberSign    = 0x23;  // #
    constexpr int Key_Dollar        = 0x24;  // $
    constexpr int Key_Percent       = 0x25;  // %
    constexpr int Key_Ampersand     = 0x26;  // &
    constexpr int Key_Apostrophe    = 0x27;  // '
    constexpr int Key_ParenLeft     = 0x28;  // (
    constexpr int Key_ParenRight    = 0x29;  // )
    constexpr int Key_Asterisk      = 0x2a;  // *
    constexpr int Key_Plus          = 0x2b;  // +
    constexpr int Key_Comma         = 0x2c;  // ,
    constexpr int Key_Minus         = 0x2d;  // -
    constexpr int Key_Period        = 0x2e;  // .
    constexpr int Key_Slash         = 0x2f;  // /

    // 0‒9
    constexpr int Key_0             = 0x30;
    constexpr int Key_1             = 0x31;
    constexpr int Key_2             = 0x32;
    constexpr int Key_3             = 0x33;
    constexpr int Key_4             = 0x34;
    constexpr int Key_5             = 0x35;
    constexpr int Key_6             = 0x36;
    constexpr int Key_7             = 0x37;
    constexpr int Key_8             = 0x38;
    constexpr int Key_9             = 0x39;

    constexpr int Key_Colon         = 0x3a;  // :
    constexpr int Key_Semicolon     = 0x3b;  // ;
    constexpr int Key_Less          = 0x3c;  // <
    constexpr int Key_Equal         = 0x3d;  // =
    constexpr int Key_Greater       = 0x3e;  // >
    constexpr int Key_Question      = 0x3f;  // ?
    constexpr int Key_At            = 0x40;  // @

    // A‒Z  (upper-case ASCII)
    constexpr int Key_A             = 0x41;
    constexpr int Key_B             = 0x42;
    constexpr int Key_C             = 0x43;
    constexpr int Key_D             = 0x44;
    constexpr int Key_E             = 0x45;
    constexpr int Key_F             = 0x46;
    constexpr int Key_G             = 0x47;
    constexpr int Key_H             = 0x48;
    constexpr int Key_I             = 0x49;
    constexpr int Key_J             = 0x4a;
    constexpr int Key_K             = 0x4b;
    constexpr int Key_L             = 0x4c;
    constexpr int Key_M             = 0x4d;
    constexpr int Key_N             = 0x4e;
    constexpr int Key_O             = 0x4f;
    constexpr int Key_P             = 0x50;
    constexpr int Key_Q             = 0x51;
    constexpr int Key_R             = 0x52;
    constexpr int Key_S             = 0x53;
    constexpr int Key_T             = 0x54;
    constexpr int Key_U             = 0x55;
    constexpr int Key_V             = 0x56;
    constexpr int Key_W             = 0x57;
    constexpr int Key_X             = 0x58;
    constexpr int Key_Y             = 0x59;
    constexpr int Key_Z             = 0x5a;

    constexpr int Key_BracketLeft   = 0x5b;  // [
    constexpr int Key_Backslash     = 0x5c;  /* \ */
    constexpr int Key_BracketRight  = 0x5d;  // ]
    constexpr int Key_AsciiCircum   = 0x5e;  // ^
    constexpr int Key_Underscore    = 0x5f;  // _
    constexpr int Key_QuoteLeft     = 0x60;  // `

    constexpr int Key_BraceLeft     = 0x7b;  // {
    constexpr int Key_Bar           = 0x7c;  // |
    constexpr int Key_BraceRight    = 0x7d;  // }
    constexpr int Key_AsciiTilde    = 0x7e;  // ~

} // namespace GameKey

// ============================================================================
// Mouse buttons  (matching Qt::MouseButton values)
// ============================================================================
namespace GameMouse {

    constexpr uint32_t NoButton       = 0x00000000;
    constexpr uint32_t LeftButton     = 0x00000001;
    constexpr uint32_t RightButton    = 0x00000002;
    constexpr uint32_t MiddleButton   = 0x00000004;
    constexpr uint32_t BackButton     = 0x00000008;
    constexpr uint32_t XButton1       = 0x00000008;   // alias for BackButton
    constexpr uint32_t ForwardButton  = 0x00000010;
    constexpr uint32_t XButton2       = 0x00000010;   // alias for ForwardButton

} // namespace GameMouse

// ============================================================================
// Keyboard modifiers  (bitmask, matching Qt::KeyboardModifier values)
// ============================================================================
namespace GameMod {

    constexpr uint32_t NoModifier       = 0x00000000;
    constexpr uint32_t ShiftModifier    = 0x02000000;
    constexpr uint32_t ControlModifier  = 0x04000000;
    constexpr uint32_t AltModifier      = 0x08000000;
    constexpr uint32_t MetaModifier     = 0x10000000;
    constexpr uint32_t KeypadModifier   = 0x20000000;

} // namespace GameMod
