#pragma once

#include <gs/protocol/Messages.h>

#include <QList>
#include <QSizeF>
#include <QString>
#include <QVariantList>

#include <cstdint>
#include <optional>
#include <string>

namespace gs::app {

class InputController;

inline constexpr int kAndroidKeycodeEnter = 66;
inline constexpr int kAndroidKeycodeNumpadEnter = 160;
inline constexpr std::uint32_t kAndroidModifierMeta = 0x000770F3U;

enum class TypingChange : std::uint8_t { None, Enter, Leave };

class PhoneInputBox final
{
public:
    TypingChange changed(const protocol::InputBoxChanged& box);
    TypingChange lost();
    void left() noexcept { m_left = true; }

    [[nodiscard]] bool shown() const noexcept { return m_box && m_box->shown; }
    [[nodiscard]] bool typingWanted() const noexcept { return shown() && !m_left; }
    [[nodiscard]] bool commits() const noexcept { return shown(); }
    [[nodiscard]] const std::optional<protocol::InputBoxChanged>& box() const noexcept { return m_box; }

private:
    std::optional<protocol::InputBoxChanged> m_box;
    bool m_left = false;
};

void applyTyping(InputController& input, const QString& serial, TypingChange change);
[[nodiscard]] bool isPlainEnter(int keycode, std::uint32_t metaState) noexcept;
[[nodiscard]] std::string inputBoxJson(const protocol::InputBoxChanged& box);
[[nodiscard]] protocol::KeyHintLayout companionKeyHints(const QVariantList& items, QSizeF picture,
                                                        const QList<bool>& live);
[[nodiscard]] protocol::KeyHintState companionKeyHintState(bool shown, bool faded, double opacity) noexcept;

}
