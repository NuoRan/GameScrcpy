#pragma once

#include "MirrorInputItem.h"
#include "ProfileEditor.h"

#include <gs/input/Profile.h>
#include <gs/protocol/Messages.h>
#include <gs/render/FrameSlot.h>
#include <gs/render/VideoFilter.h>

#include <QAbstractNativeEventFilter>
#include <QList>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QVariantList>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QStringList>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

class QQmlEngine;

namespace gs::render {
class GraphicsDevice;
class VideoItem;
}

namespace gs::app {

class DiagnosticsOverlay;

class MirrorWindow final : public QQuickWindow, private QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    MirrorWindow(const render::GraphicsDevice& graphics, std::shared_ptr<render::FrameSlot> frames,
                 const QString& serial, const QString& title, bool lowLatency);
    ~MirrorWindow() override;

    [[nodiscard]] const QString& serial() const noexcept { return m_serial; }
    void setDiagnostics(const QStringList& lines);
    void setSharpness(qreal sharpness);
    void setFilters(std::vector<render::VideoFilter> filters);
    [[nodiscard]] const std::vector<render::FilterStatus>& filterStatus() const;
    void attachOverlay(QQmlEngine& engine);
    void setFullScreen(bool on);
    [[nodiscard]] bool fullScreen() const noexcept { return m_fullScreen; }
    void setMini(bool on);
    [[nodiscard]] bool mini() const noexcept { return m_mini; }
    void setEditor(ProfileEditor* editor);
    [[nodiscard]] ProfileEditor* editor() const noexcept { return m_editor; }
    [[nodiscard]] bool editing() const noexcept { return m_editor != nullptr; }
    [[nodiscard]] bool trying() const noexcept { return m_editor != nullptr && m_editor->trying(); }
    [[nodiscard]] bool playing() const noexcept { return m_playing; }
    void setPictureFrozen(bool frozen);
    void zoomPicture(qreal zoom, QPointF anchor);
    void scrollPicture(QPointF delta);
    void setToolbarPinned(bool pinned);
    [[nodiscard]] bool toolbarPinned() const noexcept { return m_toolbarPinned; }
    void setTitleBarFixed(bool fixed);
    void toggleToolbar();
    void setStatus(double framesPerSecond, double latencyMs, const QString& transport, const QString& problem);
    void setLatencyTest(const QVariantMap& state);
    void setRecording(const QVariantMap& state);
    void showRecording(const QVariantMap& info);
    void setVolume(double volume);
    void setPhoneStatus(const QVariantMap& state);
    void showToast(const QString& text, const QString& icon, const QString& actionText = {},
                   const QString& secondActionText = {});
    void showPrompt(quint64 request, const QVariantMap& prompt);
    void withdrawPrompt(quint64 request);
    void draw(quint64 id, const QVariantMap& drawing);
    void erase(quint64 id);
    void clearDrawings();
    void setScriptProgress(const QString& source, const QString& title, double value);
    void clearScriptProgress(const QString& source);
    void changeHint(const QString& id, const QVariantMap& change);
    void clearHintChanges();
    void forceHints(const QString& source, const QVariantMap& change);
    void setKeymap(bool on);
    [[nodiscard]] bool keymap() const noexcept { return m_keymap; }
    void setCameraLocked(bool locked);
    void setCursorPeek(bool on);
    void setActionWheel(const QVariantMap& wheel);
    [[nodiscard]] bool cameraLocked() const noexcept { return m_locked; }
    void setForeground(bool front);
    void setTextInput(bool on);
    void setProfile(std::shared_ptr<const input::Profile> profile, std::optional<input::Chord> lockKey,
                    const QString& id, const QString& name);
    void setLiveControls(const QList<bool>& live, quintptr profile);
    void setAimingControls(const QList<bool>& aiming, quintptr profile);
    void setForegroundApp(const QString& package);
    void setStartOffer(const QString& package);
    void setNoPicture(QSize canvas, int turn);
    [[nodiscard]] bool noPicture() const noexcept { return m_noPicture; }
    void setSync(const QString& role, int tone, int followers, const QString& leader);
    void setCrop(const QRectF& crop);
    void setCaptureCard(int turn, bool manualCrop);
    void setCropEditing(bool on, const QRectF& box);
    [[nodiscard]] const protocol::CursorState& phoneCursor() const noexcept { return m_phoneCursor; }
    void lendVideo(bool lent);
    [[nodiscard]] bool videoLent() const noexcept { return m_videoLent; }
    [[nodiscard]] const QRectF& pictureShare() const noexcept { return m_pictureShare; }
    [[nodiscard]] QSize pictureSize() const;
    [[nodiscard]] QSize pictureChrome() const noexcept;
    void setKeptOnClose(bool kept) noexcept { m_keptOnClose = kept; }
    void setTypingFollowsPhone(bool on);
    void setCompanionQr(const QVariantMap& qr);
    void setExtensionNotice(const QString& notice);
    [[nodiscard]] const protocol::KeyHintLayout& phoneHints() const noexcept { return m_phoneHints; }
    [[nodiscard]] const protocol::KeyHintState& phoneHintState() const noexcept { return m_phoneHintState; }

signals:
    void closedByUser();
    void filterStatusChanged();
    void fullScreenChanged();
    void miniChanged();
    void pictureAreaChanged(const QRectF& share);
    void cursorTouched(gs::input::CursorPhase phase, gs::input::Vec2 position);
    void cursorScrolled(gs::input::Vec2 position, int horizontal, int vertical);
    void cursorKey(int keycode, int metaState, bool down, int repeat);
    void cursorTyped(const QString& text);
    void mouseMoved(int dx, int dy);
    void mouseButtonsChanged(int buttons);
    void textInputFinished();
    void playingChanged();
    void phoneCursorChanged(const gs::protocol::CursorState& cursor);
    void phoneHintsChanged(const gs::protocol::KeyHintLayout& layout);
    void phoneHintStateChanged(const gs::protocol::KeyHintState& state);

public slots:
    void focusPicture();

private slots:
    void followPinnedBar();
    void followPhoneHints();

protected:
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;
    void restOnTitle(bool resting);
    void titleRested();
    void fitToPicture();
    [[nodiscard]] QRect miniGeometry() const;
    void layoutContent();
    [[nodiscard]] int reservedTop() const noexcept;
    [[nodiscard]] int reservedRight() const noexcept;
    [[nodiscard]] int reservedBottom() const noexcept;
    [[nodiscard]] QRectF pictureInWindow() const;
    [[nodiscard]] QRectF pictureView() const;
    [[nodiscard]] QRectF videoArea(const QRectF& view);
    [[nodiscard]] bool arranging() const noexcept { return m_editor != nullptr && !m_editor->trying(); }
    [[nodiscard]] bool unsaved() const;
    void followTrying();
    void followLogic();
    void followFocus();
    void enablePointer();
    void followWindowState();
    void applyPointer();
    void keepClip();
    [[nodiscard]] bool pinCursor(std::uintptr_t window) const;
    void applyClaims();
    [[nodiscard]] std::vector<input::Chord> liveClaims() const;
    void centerCursor();
    void reportPhoneCursor();
    void reportPhoneHints();
    void updateHints();
    void updateCrosshair();

    QString m_serial;
    render::VideoItem* m_video;
    DiagnosticsOverlay* m_diagnostics;
    MirrorInputItem* m_pointer;
    QPointer<QQuickItem> m_overlay;
    qreal m_aspect = 0;
    bool m_fullScreen = false;
    bool m_toolbarPinned = false;
    int m_pinnedBar = 0;
    bool m_titleFixed = true;
    int m_titleHeight = 40;
    bool m_restingOnTitle = false;
    QTimer m_titleRest;
    QTimer m_clipGuard;
    bool m_restoreMaximized = false;
    QRect m_normalGeometry;
    bool m_mini = false;
    Qt::WindowStates m_miniStates;
    QRect m_miniRestore;
    std::optional<QPoint> m_miniPosition;
    QPointer<ProfileEditor> m_editor;
    int m_editTop = 0;
    int m_editRight = 0;
    int m_editBottom = 0;
    QRect m_editRestore;
    qreal m_editAspect = 0;
    bool m_logicShown = false;
    QRect m_logicRestore;
    bool m_playing = false;
    qreal m_zoom = 1;
    QPointF m_scroll;
    bool m_keymap = false;
    bool m_locked = false;
    bool m_peek = false;
    bool m_textInput = false;
    bool m_foreground = false;
    bool m_clipped = false;
    QRectF m_pictureShare;
    std::shared_ptr<const input::Profile> m_profile;
    std::optional<input::Chord> m_lockKey;
    std::map<QString, QVariantMap> m_hintChanges;
    std::set<QString> m_hintFlashes;
    int m_hintFlash = 0;
    std::set<QString> m_hintReveals;
    std::vector<input::CursorClaim> m_cursorClaims;
    QList<bool> m_liveControls;
    QString m_profileId;
    QString m_profileName;
    QString m_foregroundApp;
    QString m_startOffer;
    bool m_noPicture = false;
    QSize m_canvas;
    int m_turn = 0;
    QVariantMap m_recording;
    QVariantMap m_phoneStatus;
    std::optional<QPointF> m_hover;
    bool m_hoverPressed = false;
    protocol::CursorState m_phoneCursor;
    QString m_syncRole;
    int m_syncTone = -1;
    int m_syncFollowers = 0;
    QString m_syncLeader;
    std::shared_ptr<render::FrameSlot> m_frames;
    bool m_videoLent = false;
    bool m_keptOnClose = false;
    bool m_typingFollowsPhone = false;
    QVariantMap m_companionQr;
    QVariantList m_hintItems;
    QSizeF m_hintPicture;
    protocol::KeyHintLayout m_phoneHints;
    protocol::KeyHintState m_phoneHintState;
    QRectF m_crop{0.0, 0.0, 1.0, 1.0};
    bool m_captureCard = false;
    bool m_manualCrop = false;
    bool m_cropEditing = false;
    QRectF m_cropBox{0.0, 0.0, 1.0, 1.0};
    QString m_extensionNotice;
};

}
