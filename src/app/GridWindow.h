#pragma once

#include "GridCells.h"

#include <gs/input/Geometry.h>
#include <gs/input/MappingEngine.h>
#include <gs/input/Profile.h>
#include <gs/render/FrameSlot.h>

#include <QList>
#include <QPointF>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <vector>

class QQmlEngine;

namespace gs::render {
class GraphicsDevice;
class VideoItem;
}

namespace gs::app {

class MirrorInputItem;

class GridWindow final : public QQuickWindow
{
    Q_OBJECT

public:
    using HintItems = std::function<QVariantList(const input::Profile& profile, QSizeF picture)>;

    explicit GridWindow(const render::GraphicsDevice& graphics);
    ~GridWindow() override;

    void attachOverlay(QQmlEngine& engine);
    bool addCell(const QString& serial, const QString& name, double aspect);
    bool removeCell(const QString& serial);
    void setFrames(const QString& serial, std::shared_ptr<render::FrameSlot> frames);
    void setLabel(const QString& serial, const QString& name, const QString& tone, const QString& status);
    void setRate(const QString& serial, double framesPerSecond);
    void setSync(const QString& serial, const GridCells::Sync& sync);
    [[nodiscard]] QStringList serials() const { return m_cells.serials(); }
    [[nodiscard]] bool contains(const QString& serial) const { return m_cells.find(serial) != nullptr; }
    [[nodiscard]] bool holds(const QString& serial) const;
    [[nodiscard]] bool separate(const QString& serial) const;
    [[nodiscard]] const QString& target() const noexcept { return m_cells.target(); }
    void setTarget(const QString& serial);
    [[nodiscard]] QRectF pictureShare(const QString& serial) const;
    void setKeymap(bool on);
    void setCameraLocked(bool locked);
    void setCursorPeek(bool on);
    void setTextInput(bool on);
    void setForeground(bool front);
    void setProfile(std::shared_ptr<const input::Profile> profile);
    void setLiveControls(const QString& serial, const QList<bool>& live, quintptr profile);
    void setHintItems(HintItems items);
    void setAimingControls(const QString& serial, const QList<bool>& aiming, quintptr profile);
    void setActionWheel(const QString& serial, const QVariantMap& wheel);
    void setCrop(const QString& serial, const QRectF& crop);
    void showToast(const QString& text, const QString& icon);
    void retranslate() { updateTitle(); }

signals:
    void closedByUser();
    void targetChosen(const QString& serial);
    void ownWindowWanted(const QString& serial);
    void backToGridWanted(const QString& serial);
    void picturesChanged();
    void cursorTouched(const QString& serial, gs::input::CursorPhase phase, gs::input::Vec2 position);
    void cursorScrolled(const QString& serial, gs::input::Vec2 position, int horizontal, int vertical);
    void cursorKey(const QString& serial, int keycode, int metaState, bool down, int repeat);
    void cursorTyped(const QString& serial, const QString& text);
    void textInputFinished();

public slots:
    void releaseTarget();

protected:
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Fit {
        std::size_t cells = 0;
        std::size_t known = 0;

        friend bool operator==(const Fit&, const Fit&) = default;
    };

    struct Pane {
        QPointer<render::VideoItem> video;
        QPointer<MirrorInputItem> pointer;
        quintptr liveProfile = 0;
        QList<bool> live;
        quintptr aimingProfile = 0;
        QList<bool> aiming;
        QRectF crop{0.0, 0.0, 1.0, 1.0};
    };

    [[nodiscard]] bool routePress(const QMouseEvent& mouse, bool doubleClick);
    [[nodiscard]] bool popupOpen() const;
    [[nodiscard]] bool overlayTakes(QPointF at) const;
    void layoutCells();
    void scheduleLayout();
    [[nodiscard]] Fit fitWanted() const;
    void scheduleFit();
    void fitToCells();
    void followFrame(const QString& serial);
    void targetChanged();
    void focusTarget();
    void applyPointer();
    void keepClip();
    void applyClaims();
    void centerCursor();
    void publishCells();
    void updateTitle();
    void updateHints();
    void pushHintFlags();
    void pushWheel();
    [[nodiscard]] MirrorInputItem* targetPointer() const;
    [[nodiscard]] QRectF pictureOf(const QString& serial) const;

    GridCells m_cells;
    std::map<QString, Pane> m_panes;
    QPointer<QQuickItem> m_overlay;
    QPointer<QQuickItem> m_hints;
    HintItems m_hintItems;
    QVariantList m_shownHints;
    QString m_wheelSerial;
    QVariantMap m_wheel;
    GridCells::Metrics m_metrics;
    int m_top = 40;
    bool m_layoutQueued = false;
    Fit m_fitted;
    QSize m_fitSize;
    bool m_fitQueued = false;
    bool m_userSized = false;
    bool m_placed = false;
    bool m_forwarded = true;
    Qt::MouseButtons m_swallowed;
    bool m_keymap = false;
    bool m_locked = false;
    bool m_peek = false;
    bool m_textInput = false;
    bool m_foreground = false;
    bool m_clipped = false;
    QTimer m_clipGuard;
    std::shared_ptr<const input::Profile> m_profile;
    std::vector<input::CursorClaim> m_cursorClaims;
    QVariantList m_published;
};

}
