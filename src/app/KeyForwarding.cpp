#include "KeyForwarding.h"

#include <QKeyEvent>
#include <QKeySequence>

#include <algorithm>
#include <array>
#include <utility>

namespace gs::app {

namespace {

constexpr std::uint32_t kMetaShift = 0x41;
constexpr std::uint32_t kMetaAlt = 0x12;
constexpr std::uint32_t kMetaCtrl = 0x3000;
constexpr std::uint32_t kMetaMeta = 0x30000;

constexpr std::uint32_t kKeycodeA = 29;
constexpr std::uint32_t kKeycode0 = 7;
constexpr std::uint32_t kKeycodeF1 = 131;

constexpr std::array<std::pair<int, std::uint32_t>, 19> kNamedKeys{{
    {Qt::Key_Return, 66},
    {Qt::Key_Enter, 66},
    {Qt::Key_Backspace, 67},
    {Qt::Key_Delete, 112},
    {Qt::Key_Tab, 61},
    {Qt::Key_Backtab, 61},
    {Qt::Key_Escape, 111},
    {Qt::Key_Left, 21},
    {Qt::Key_Right, 22},
    {Qt::Key_Up, 19},
    {Qt::Key_Down, 20},
    {Qt::Key_Home, 122},
    {Qt::Key_End, 123},
    {Qt::Key_PageUp, 92},
    {Qt::Key_PageDown, 93},
    {Qt::Key_Insert, 124},
    {Qt::Key_Space, 62},
    {Qt::Key_Menu, 82},
    {Qt::Key_Pause, 121},
}};

constexpr std::array kEditingKeys{
    QKeySequence::Copy,           QKeySequence::Cut,
    QKeySequence::Paste,          QKeySequence::Undo,
    QKeySequence::Redo,           QKeySequence::SelectAll,
    QKeySequence::MoveToNextWord, QKeySequence::MoveToPreviousWord,
    QKeySequence::SelectNextWord, QKeySequence::SelectPreviousWord,
    QKeySequence::DeleteEndOfWord, QKeySequence::DeleteStartOfWord,
    QKeySequence::MoveToStartOfLine, QKeySequence::MoveToEndOfLine,
    QKeySequence::SelectStartOfLine, QKeySequence::SelectEndOfLine,
};

std::uint32_t metaStateOf(Qt::KeyboardModifiers modifiers) noexcept
{
    std::uint32_t meta = 0;
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        meta |= kMetaShift;
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        meta |= kMetaAlt;
    }
    if (modifiers.testFlag(Qt::ControlModifier)) {
        meta |= kMetaCtrl;
    }
    if (modifiers.testFlag(Qt::MetaModifier)) {
        meta |= kMetaMeta;
    }
    return meta;
}

}

bool typesText(const QString& text, Qt::KeyboardModifiers modifiers) noexcept
{
    const Qt::KeyboardModifiers chord = modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const bool altGr = chord == (Qt::ControlModifier | Qt::AltModifier);
    if (text.isEmpty() || (chord != Qt::NoModifier && !altGr)) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](QChar c) { return c.isPrint(); });
}

std::optional<ForwardedKey> androidKeyOf(int key, Qt::KeyboardModifiers modifiers) noexcept
{
    const std::uint32_t meta = metaStateOf(modifiers);
    const auto named = std::find_if(kNamedKeys.begin(), kNamedKeys.end(),
                                    [key](const std::pair<int, std::uint32_t>& entry) { return entry.first == key; });
    if (named != kNamedKeys.end()) {
        const std::uint32_t shift = key == Qt::Key_Backtab ? kMetaShift : 0;
        return ForwardedKey{.keycode = named->second, .metaState = meta | shift};
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        return ForwardedKey{.keycode = kKeycodeF1 + static_cast<std::uint32_t>(key - Qt::Key_F1), .metaState = meta};
    }
    const bool chorded = modifiers.testAnyFlags(Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (chorded && key >= Qt::Key_A && key <= Qt::Key_Z) {
        return ForwardedKey{.keycode = kKeycodeA + static_cast<std::uint32_t>(key - Qt::Key_A), .metaState = meta};
    }
    if (chorded && key >= Qt::Key_0 && key <= Qt::Key_9) {
        return ForwardedKey{.keycode = kKeycode0 + static_cast<std::uint32_t>(key - Qt::Key_0), .metaState = meta};
    }
    return std::nullopt;
}

bool textBoxWants(const QKeyEvent& event)
{
    if (std::any_of(kEditingKeys.begin(), kEditingKeys.end(),
                    [&event](QKeySequence::StandardKey standard) { return event.matches(standard); })) {
        return true;
    }
    const Qt::KeyboardModifiers modifiers = event.modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
    const int key = event.key();
    if (modifiers == Qt::NoModifier && key < Qt::Key_Escape) {
        return true;
    }
    return modifiers == Qt::NoModifier && (key == Qt::Key_Delete || key == Qt::Key_Home || key == Qt::Key_End ||
                                           key == Qt::Key_Backspace || key == Qt::Key_Left || key == Qt::Key_Right);
}

}
