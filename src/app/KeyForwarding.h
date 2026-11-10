#pragma once

#include <QString>

#include <cstdint>
#include <optional>

class QKeyEvent;

namespace gs::app {

struct ForwardedKey {
    std::uint32_t keycode = 0;
    std::uint32_t metaState = 0;

    friend bool operator==(const ForwardedKey&, const ForwardedKey&) = default;
};

[[nodiscard]] bool typesText(const QString& text, Qt::KeyboardModifiers modifiers) noexcept;
[[nodiscard]] std::optional<ForwardedKey> androidKeyOf(int key, Qt::KeyboardModifiers modifiers) noexcept;
[[nodiscard]] bool textBoxWants(const QKeyEvent& event);

}
