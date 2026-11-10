#pragma once

#include "KeyForwarding.h"

#include <gs/input/Geometry.h>
#include <gs/input/MappingEngine.h>

#include <QList>
#include <QPointF>
#include <QQuickItem>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVariant>

#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace gs::app {

class MirrorInputItem final : public QQuickItem
{
    Q_OBJECT

public:
    explicit MirrorInputItem(QQuickItem* parent = nullptr);

    void setContentRect(const QRectF& rect) { m_content = rect; }
    void setCapturing(bool on);
    [[nodiscard]] bool capturing() const noexcept { return m_capturing; }
    void setTyping(bool on);
    [[nodiscard]] bool typing() const noexcept { return m_typing; }
    void setRelativeMouse(bool on, QSizeF target = {});
    [[nodiscard]] bool relativeMouse() const noexcept { return m_relative; }

    [[nodiscard]] static std::optional<input::Vec2> normalized(const QRectF& content, QPointF position, bool clamped);
    [[nodiscard]] static std::int16_t scrollUnits(int angleDelta) noexcept;
    [[nodiscard]] static int mouseButtonBit(Qt::MouseButton button) noexcept;
    void setButtonKeys(std::map<Qt::MouseButton, std::uint32_t> keys);
    [[nodiscard]] std::optional<std::uint32_t> keycodeOf(Qt::MouseButton button) const;
    void setClaimedInputs(std::vector<input::Chord> chords) { m_claimed = std::move(chords); }
    [[nodiscard]] bool claimed(input::InputSource source, std::uint8_t modifiers = 0) const;
    [[nodiscard]] static std::vector<input::Chord> liveClaims(const input::Profile& profile,
                                                              const std::vector<input::CursorClaim>& claims,
                                                              const QList<bool>& live);

    [[nodiscard]] QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

signals:
    void touched(gs::input::CursorPhase phase, gs::input::Vec2 position);
    void scrolled(gs::input::Vec2 position, int horizontal, int vertical);
    void androidKey(int keycode, int metaState, bool down, int repeat);
    void typed(const QString& text);
    void mouseMoved(int dx, int dy);
    void mouseButtonsChanged(int buttons);

protected:
    bool event(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void hoverEnterEvent(QHoverEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    struct HeldKey {
        ForwardedKey key;
        int repeat = 0;
    };

    void cancel();
    void releaseKeys();
    void follow(QPointF position);
    [[nodiscard]] bool pressRelative(QMouseEvent* event, bool down);

    QRectF m_content;
    QPointF m_lastPress;
    bool m_capturing = false;
    bool m_typing = false;
    bool m_touching = false;
    bool m_relative = false;
    QSizeF m_target;
    std::optional<QPointF> m_lastPointer;
    QPointF m_rest;
    int m_buttons = 0;
    Qt::MouseButtons m_keysHeld;
    std::map<Qt::MouseButton, std::uint32_t> m_buttonKeys;
    std::map<int, HeldKey> m_heldKeys;
    std::vector<input::Chord> m_claimed;
};

}
