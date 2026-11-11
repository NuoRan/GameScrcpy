#include "MirrorWindow.h"

#include "CompanionInput.h"
#include "CompanionLink.h"
#include "DiagnosticsOverlay.h"
#include "KeyHintModel.h"
#include "PictureZoom.h"
#include "ProfileEditor.h"
#include "ScriptWorkbench.h"

#include <gs/input/InputSource.h>
#include <gs/platform/Cursor.h>
#include <gs/platform/Foreground.h>
#include <gs/render/GraphicsDevice.h>
#include <gs/render/VideoItem.h>

#include <QCoreApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QResizeEvent>
#include <QScreen>
#include <QSurfaceFormat>
#include <QVariant>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <utility>

namespace gs::app {

namespace {

constexpr qreal kScreenShare = 0.85;
constexpr qreal kPointerZ = 1;
constexpr qreal kOverlayZ = 2;
constexpr int kPinnedBarHeight = 44;
constexpr int kMiniLongSide = 360;
constexpr int kMiniMargin = 16;
constexpr qreal kLeastZoom = 0.5;
constexpr qreal kMostZoom = 2.0;
constexpr std::chrono::milliseconds kTitleRest{2000};
constexpr std::chrono::milliseconds kClipCheck{100};

bool plain(Qt::WindowStates states)
{
    return !states.testAnyFlags(Qt::WindowMinimized | Qt::WindowMaximized | Qt::WindowFullScreen);
}

}

MirrorWindow::MirrorWindow(const render::GraphicsDevice& graphics, std::shared_ptr<render::FrameSlot> frames,
                           const QString& serial, const QString& title, bool lowLatency)
    : m_serial(serial), m_video(new render::VideoItem(contentItem())),
      m_diagnostics(new DiagnosticsOverlay(contentItem())), m_pointer(new MirrorInputItem(contentItem()))
{
    m_pinnedBar = kPinnedBarHeight;
    m_pointer->setZ(kPointerZ);
    m_pointer->setTyping(true);
    m_pointer->setFocus(true);
    connect(m_video, &render::VideoItem::contentRectChanged, m_pointer,
            [this] { m_pointer->setContentRect(m_video->contentRect()); });
    connect(m_pointer, &MirrorInputItem::touched, this, &MirrorWindow::cursorTouched);
    connect(m_pointer, &MirrorInputItem::scrolled, this, &MirrorWindow::cursorScrolled);
    connect(m_pointer, &MirrorInputItem::androidKey, this, &MirrorWindow::cursorKey);
    connect(m_pointer, &MirrorInputItem::typed, this, &MirrorWindow::cursorTyped);
    connect(m_pointer, &MirrorInputItem::mouseMoved, this, &MirrorWindow::mouseMoved);
    connect(m_pointer, &MirrorInputItem::mouseButtonsChanged, this, &MirrorWindow::mouseButtonsChanged);
    if (lowLatency) {
        QSurfaceFormat unthrottled = requestedFormat();
        unthrottled.setSwapInterval(0);
        setFormat(unthrottled);
    }
    m_video->setImmediate(lowLatency);
    graphics.share(*this);
    setColor(Qt::black);
    setTitle(title);
    m_frames = std::move(frames);
    m_video->setSource(m_frames);
    connect(m_video, &render::VideoItem::contentRectChanged, this, &MirrorWindow::fitToPicture);
    connect(m_video, &render::VideoItem::contentRectChanged, this, &MirrorWindow::updateHints);
    connect(m_video, &render::VideoItem::contentRectChanged, this, [this] {
        if (arranging() && !qFuzzyCompare(m_zoom, 1.0)) {
            layoutContent();
        }
    });
    connect(m_video, &render::VideoItem::filterStatusChanged, this, &MirrorWindow::filterStatusChanged);
    connect(this, &QWindow::windowStateChanged, this, &MirrorWindow::followWindowState);
    connect(this, &QQuickWindow::activeFocusItemChanged, this, &MirrorWindow::followFocus);
    m_titleRest.setSingleShot(true); // source-rule-allow R9: resting on the title bar is what shows the toolbar
    m_titleRest.setInterval(kTitleRest);
    connect(&m_titleRest, &QTimer::timeout, this, &MirrorWindow::titleRested);
    m_clipGuard.setInterval(kClipCheck);
    connect(&m_clipGuard, &QTimer::timeout, this, &MirrorWindow::keepClip);
    QCoreApplication::instance()->installNativeEventFilter(this);
    resize(360, 720);
}

MirrorWindow::~MirrorWindow()
{
    QCoreApplication::instance()->removeNativeEventFilter(this);
    if (m_clipped) {
        platform::releaseCursorClip();
    }
}

bool MirrorWindow::nativeEventFilter(const QByteArray& eventType, void* message, qintptr*)
{
    if (!m_titleFixed || handle() == nullptr || eventType != "windows_generic_MSG") {
        return false;
    }
    switch (platform::captionHoverOf(message, static_cast<std::uintptr_t>(winId()))) {
    case platform::CaptionHover::Over:
        restOnTitle(true);
        break;
    case platform::CaptionHover::Off:
        restOnTitle(false);
        break;
    case platform::CaptionHover::Unrelated:
        break;
    }
    return false;
}

void MirrorWindow::restOnTitle(bool resting)
{
    if (resting == m_restingOnTitle) {
        return;
    }
    m_restingOnTitle = resting;
    if (resting) {
        m_titleRest.start();
    } else {
        m_titleRest.stop();
    }
}

void MirrorWindow::titleRested()
{
    m_restingOnTitle = false;
    if (m_overlay != nullptr && handle() != nullptr && m_titleFixed && !m_fullScreen && !m_mini &&
        m_editor == nullptr && !m_locked && platform::pointerOnCaption(static_cast<std::uintptr_t>(winId()))) {
        QMetaObject::invokeMethod(m_overlay.data(), "revealFromTitle");
    }
}

void MirrorWindow::setDiagnostics(const QStringList& lines)
{
    m_diagnostics->setLines(lines);
}

void MirrorWindow::setSharpness(qreal sharpness)
{
    m_video->setSharpness(sharpness);
}

void MirrorWindow::setFilters(std::vector<render::VideoFilter> filters)
{
    m_video->setFilters(std::move(filters));
}

const std::vector<render::FilterStatus>& MirrorWindow::filterStatus() const
{
    return m_video->filterStatus();
}

void MirrorWindow::lendVideo(bool lent)
{
    if (lent == m_videoLent) {
        return;
    }
    m_videoLent = lent;
    m_video->setSource(lent ? nullptr : m_frames);
}

QSize MirrorWindow::pictureSize() const
{
    return m_video->frameSize();
}

QSize MirrorWindow::pictureChrome() const noexcept
{
    return {reservedRight(), reservedTop() + reservedBottom()};
}

void MirrorWindow::attachOverlay(QQmlEngine& engine)
{
    if (m_overlay != nullptr) {
        return;
    }
    QQmlComponent component(&engine, QStringLiteral("GameScrcpy.App"), QStringLiteral("MirrorOverlay"));
    QObject* created = component.createWithInitialProperties({{QStringLiteral("serial"), m_serial}});
    auto* overlay = qobject_cast<QQuickItem*>(created);
    if (overlay == nullptr) {
        delete created;
        qWarning("mirror overlay for %s not created: %s", qUtf8Printable(m_serial),
                 qUtf8Printable(component.errorString()));
        return;
    }
    overlay->setParent(contentItem());
    overlay->setParentItem(contentItem());
    overlay->setZ(kOverlayZ);
    overlay->setSize(size());
    overlay->setProperty("fullScreen", m_fullScreen);
    overlay->setProperty("mini", m_mini);
    if (const int title = overlay->property("fixedTitleHeight").toInt(); title > 0) {
        m_titleHeight = title;
    }
    overlay->setProperty("titleFixed", m_titleFixed);
    overlay->setProperty("reservedTop", reservedTop());
    overlay->setProperty("toolbarPinned", m_toolbarPinned);
    overlay->setProperty("keymap", m_keymap);
    overlay->setProperty("cameraLocked", m_locked);
    overlay->setProperty("textInput", m_textInput);
    overlay->setProperty("foregroundApp", m_foregroundApp);
    overlay->setProperty("startOffer", m_startOffer);
    overlay->setProperty("noPicture", m_noPicture);
    overlay->setProperty("controlTurn", m_turn);
    overlay->setProperty("recording", m_recording);
    overlay->setProperty("syncRole", m_syncRole);
    overlay->setProperty("syncTone", m_syncTone);
    overlay->setProperty("syncCount", m_syncFollowers);
    overlay->setProperty("syncLeader", m_syncLeader);
    overlay->setProperty("phoneStatus", m_phoneStatus);
    overlay->setProperty("companionQr", m_companionQr);
    overlay->setProperty("captureCard", m_captureCard);
    overlay->setProperty("manualCrop", m_manualCrop);
    overlay->setProperty("cropBox", m_cropBox);
    overlay->setProperty("cropEditing", m_cropEditing);
    overlay->setProperty("extensionNotice", m_extensionNotice);
    m_overlay = overlay;
    connect(overlay, SIGNAL(pinnedBarHeightChanged()), this, SLOT(followPinnedBar()));
    connect(overlay, SIGNAL(pictureFocusWanted()), this, SLOT(focusPicture()));
    connect(overlay, SIGNAL(companionHintLookChanged()), this, SLOT(followPhoneHints()));
    followPinnedBar();
    setProfile(m_profile, m_lockKey, m_profileId, m_profileName);
    followPhoneHints();
}

void MirrorWindow::setTypingFollowsPhone(bool on)
{
    m_typingFollowsPhone = on;
}

void MirrorWindow::setCompanionQr(const QVariantMap& qr)
{
    if (qr == m_companionQr) {
        return;
    }
    m_companionQr = qr;
    if (m_overlay != nullptr) {
        m_overlay->setProperty("companionQr", qr);
    }
}

void MirrorWindow::setExtensionNotice(const QString& notice)
{
    if (notice == m_extensionNotice) {
        return;
    }
    m_extensionNotice = notice;
    if (m_overlay != nullptr) {
        m_overlay->setProperty("extensionNotice", notice);
    }
}

void MirrorWindow::reportPhoneHints()
{
    protocol::KeyHintLayout layout = companionKeyHints(m_hintItems, m_hintPicture, m_liveControls);
    if (layout == m_phoneHints) {
        return;
    }
    m_phoneHints = std::move(layout);
    emit phoneHintsChanged(m_phoneHints);
}

void MirrorWindow::followPhoneHints()
{
    if (m_overlay == nullptr) {
        return;
    }
    const QVariantMap look = m_overlay->property("companionHintLook").toMap();
    const protocol::KeyHintState state =
        companionKeyHintState(look.value(QStringLiteral("shown")).toBool(),
                              look.value(QStringLiteral("faded"), true).toBool(),
                              look.value(QStringLiteral("opacity"), 1.0).toDouble());
    if (state == m_phoneHintState) {
        return;
    }
    m_phoneHintState = state;
    emit phoneHintStateChanged(state);
}

void MirrorWindow::setForegroundApp(const QString& package)
{
    m_foregroundApp = package;
    if (m_overlay != nullptr) {
        m_overlay->setProperty("foregroundApp", package);
    }
}

void MirrorWindow::setStartOffer(const QString& package)
{
    if (package == m_startOffer) {
        return;
    }
    m_startOffer = package;
    if (m_overlay != nullptr) {
        m_overlay->setProperty("startOffer", package);
    }
}

void MirrorWindow::setNoPicture(QSize canvas, int turn)
{
    m_noPicture = true;
    m_canvas = canvas;
    m_turn = turn;
    m_video->setPlaceholderSize(canvas);
    m_pointer->setRelativeMouse(true, QSizeF(canvas));
    if (m_overlay != nullptr) {
        m_overlay->setProperty("noPicture", true);
        m_overlay->setProperty("controlTurn", turn);
    }
}

void MirrorWindow::setSync(const QString& role, int tone, int followers, const QString& leader)
{
    if (role == m_syncRole && tone == m_syncTone && followers == m_syncFollowers && leader == m_syncLeader) {
        return;
    }
    m_syncRole = role;
    m_syncTone = tone;
    m_syncFollowers = followers;
    m_syncLeader = leader;
    if (m_overlay != nullptr) {
        m_overlay->setProperty("syncRole", role);
        m_overlay->setProperty("syncTone", tone);
        m_overlay->setProperty("syncCount", followers);
        m_overlay->setProperty("syncLeader", leader);
    }
}

void MirrorWindow::setCrop(const QRectF& crop)
{
    m_crop = crop;
    if (!m_cropEditing) {
        m_video->setCrop(crop);
    }
}

void MirrorWindow::setCaptureCard(int turn, bool manualCrop)
{
    m_captureCard = true;
    m_manualCrop = manualCrop;
    m_turn = turn;
    if (m_overlay != nullptr) {
        m_overlay->setProperty("captureCard", true);
        m_overlay->setProperty("manualCrop", manualCrop);
        m_overlay->setProperty("controlTurn", turn);
    }
}

void MirrorWindow::setCropEditing(bool on, const QRectF& box)
{
    m_cropEditing = on;
    if (on) {
        m_cropBox = box;
    }
    m_video->setCrop(on ? QRectF(0.0, 0.0, 1.0, 1.0) : m_crop);
    if (m_overlay != nullptr) {
        m_overlay->setProperty("cropBox", m_cropBox);
        m_overlay->setProperty("cropEditing", on);
    }
}

void MirrorWindow::focusPicture()
{
    if (!m_mini && (m_editor == nullptr || m_editor->trying())) {
        m_pointer->forceActiveFocus(Qt::OtherFocusReason);
    }
}

void MirrorWindow::setKeymap(bool on)
{
    if (on == m_keymap) {
        return;
    }
    m_keymap = on;
    m_textInput = m_textInput && on;
    m_pointer->setTyping(!on || m_textInput);
    if (m_overlay != nullptr) {
        m_overlay->setProperty("keymap", on);
        m_overlay->setProperty("textInput", m_textInput);
    }
    applyPointer();
}

void MirrorWindow::setCameraLocked(bool locked)
{
    if (locked == m_locked) {
        return;
    }
    m_locked = locked;
    if (m_overlay != nullptr) {
        m_overlay->setProperty("cameraLocked", locked && !m_peek);
    }
    applyPointer();
    if (!locked) {
        centerCursor();
    }
}

void MirrorWindow::setCursorPeek(bool on)
{
    if (on == m_peek) {
        return;
    }
    m_peek = on;
    if (m_overlay != nullptr) {
        m_overlay->setProperty("cameraLocked", m_locked && !on);
    }
    applyPointer();
    if (on) {
        centerCursor();
    }
}

void MirrorWindow::setActionWheel(const QVariantMap& wheel)
{
    if (m_overlay != nullptr) {
        m_overlay->setProperty("actionWheel", wheel);
    }
}

void MirrorWindow::setForeground(bool front)
{
    if (front == m_foreground) {
        return;
    }
    m_foreground = front;
    if (!front) {
        setActionWheel({});
    }
    applyPointer();
}

void MirrorWindow::setTextInput(bool on)
{
    if (on == m_textInput) {
        return;
    }
    m_textInput = on;
    m_pointer->setTyping(!m_keymap || on);
    if (m_overlay != nullptr) {
        m_overlay->setProperty("textInput", on);
    }
    if (on) {
        showToast(tr("Typing on the phone: Enter sends, Esc goes back to the game"), QStringLiteral("keyboard"));
    }
    applyPointer();
}

void MirrorWindow::setProfile(std::shared_ptr<const input::Profile> profile, std::optional<input::Chord> lockKey,
                              const QString& id, const QString& name)
{
    const bool same = profile == m_profile && id == m_profileId;
    if (profile != m_profile) {
        m_liveControls.clear();
    }
    m_profile = std::move(profile);
    m_lockKey = lockKey;
    m_profileId = id;
    m_profileName = name;
    m_cursorClaims = m_profile != nullptr ? input::cursorClaimChords(*m_profile) : std::vector<input::CursorClaim>();
    applyPointer();
    if (m_overlay == nullptr) {
        return;
    }
    m_overlay->setProperty("profileName", m_profile != nullptr ? m_profileName : QString());
    updateCrosshair();
    if (same && !m_overlay->property("profileKey").toString().isEmpty()) {
        return;
    }
    const auto identity = reinterpret_cast<quintptr>(m_profile.get());
    m_overlay->setProperty("profileId", m_profileId);
    m_overlay->setProperty("profileKey", identity != 0 ? QString::number(identity, 16) : QString());
    m_overlay->setProperty("liveControls", QVariantList());
    m_overlay->setProperty("aimingControls", QVariantList());
    updateHints();
}

void MirrorWindow::setLiveControls(const QList<bool>& live, quintptr profile)
{
    if (profile != reinterpret_cast<quintptr>(m_profile.get())) {
        return;
    }
    m_liveControls = live;
    applyClaims();
    reportPhoneHints();
    if (m_overlay == nullptr) {
        return;
    }
    QVariantList flags;
    flags.reserve(live.size());
    for (const bool flag : live) {
        flags.append(flag);
    }
    m_overlay->setProperty("liveControls", flags);
    updateCrosshair();
}

void MirrorWindow::updateCrosshair()
{
    if (m_overlay != nullptr) {
        m_overlay->setProperty("crosshair", m_profile != nullptr && crosshairWanted(*m_profile, m_liveControls));
    }
}

void MirrorWindow::setAimingControls(const QList<bool>& aiming, quintptr profile)
{
    if (m_overlay == nullptr || profile != reinterpret_cast<quintptr>(m_profile.get())) {
        return;
    }
    QVariantList flags;
    flags.reserve(aiming.size());
    for (const bool flag : aiming) {
        flags.append(flag);
    }
    m_overlay->setProperty("aimingControls", flags);
}

void MirrorWindow::updateHints()
{
    const QRectF picture = pictureInWindow();
    if (width() > 0 && height() > 0) {
        const QRectF share(picture.x() / width(), picture.y() / height(), picture.width() / width(),
                           picture.height() / height());
        if (share != m_pictureShare) {
            m_pictureShare = share;
            emit pictureAreaChanged(share);
            applyPointer();
        }
    }
    if (m_overlay == nullptr) {
        return;
    }
    m_overlay->setProperty("pictureRect", picture);
    QVariantList items =
        m_profile != nullptr ? keyHintItems(*m_profile, picture.size(), m_lockKey) : QVariantList();
    if ((!m_hintChanges.empty() || !m_hintFlashes.empty()) && m_profile != nullptr) {
        QVariantList changed;
        for (const QVariant& entry : std::as_const(items)) {
            QVariantMap item = entry.toMap();
            const auto index = static_cast<std::size_t>(item.value(QStringLiteral("control")).toInt());
            if (index >= m_profile->controls.size()) {
                changed.append(item);
                continue;
            }
            const QString id = QString::fromStdString(m_profile->controls[index].id);
            if (m_hintFlashes.contains(id)) {
                item.insert(QStringLiteral("flash"), m_hintFlash);
            }
            const auto found = m_hintChanges.find(id);
            if (found == m_hintChanges.end()) {
                changed.append(item);
                continue;
            }
            if (!found->second.value(QStringLiteral("visible"), true).toBool()) {
                continue;
            }
            const QVariant movedX = found->second.value(QStringLiteral("x"));
            const QVariant movedY = found->second.value(QStringLiteral("y"));
            if (movedX.isValid() && movedY.isValid()) {
                item.insert(QStringLiteral("x"), movedX.toDouble() * picture.width());
                item.insert(QStringLiteral("y"), movedY.toDouble() * picture.height());
            }
            if (const QVariant text = found->second.value(QStringLiteral("text")); text.isValid()) {
                const QVariantList keys = item.value(QStringLiteral("keys")).toList();
                QVariantMap first = keys.isEmpty() ? QVariantMap() : keys.front().toMap();
                first.insert(QStringLiteral("text"), text.toString());
                first.insert(QStringLiteral("mouse"), QString());
                first.insert(QStringLiteral("glyph"), QString());
                first.insert(QStringLiteral("prefix"), QString());
                item.insert(QStringLiteral("keys"), QVariantList{first});
            }
            changed.append(item);
        }
        items = changed;
    }
    m_hintFlashes.clear();
    m_overlay->setProperty("keyHints", items);
    m_hintItems = items;
    m_hintPicture = picture.size();
    reportPhoneHints();
}

void MirrorWindow::changeHint(const QString& id, const QVariantMap& change)
{
    if (change.value(QStringLiteral("reset")).toBool()) {
        m_hintChanges.erase(id);
    } else if (change.value(QStringLiteral("flash")).toBool()) {
        m_hintFlashes.insert(id);
        ++m_hintFlash;
    } else {
        QVariantMap& current = m_hintChanges[id];
        for (auto it = change.cbegin(); it != change.cend(); ++it) {
            if (it.key() != u"id") {
                current.insert(it.key(), it.value());
            }
        }
    }
    updateHints();
}

void MirrorWindow::clearHintChanges()
{
    m_hintFlashes.clear();
    if (!m_hintChanges.empty()) {
        m_hintChanges.clear();
        updateHints();
    }
    if (m_overlay != nullptr) {
        m_overlay->setProperty("hintsForced", -1);
        m_overlay->setProperty("hintOpacityForced", -1.0);
    }
}

void MirrorWindow::forceHints(const QString& source, const QVariantMap& change)
{
    if (m_overlay == nullptr) {
        return;
    }
    if (const QVariant visible = change.value(QStringLiteral("visible")); visible.isValid()) {
        int forced = 0;
        if (visible.toString() == u"toggle") {
            forced = m_overlay->property("hintsShown").toBool() ? 0 : 1;
        } else {
            forced = visible.toBool() ? 1 : 0;
        }
        m_overlay->setProperty("hintsForced", forced);
    }
    if (const QVariant opacity = change.value(QStringLiteral("opacity")); opacity.isValid()) {
        m_overlay->setProperty("hintOpacityForced", std::clamp(opacity.toDouble(), 0.0, 1.0));
    }
    if (const QVariant reveal = change.value(QStringLiteral("reveal")); reveal.isValid()) {
        if (reveal.toBool()) {
            m_hintReveals.insert(source);
        } else {
            m_hintReveals.erase(source);
        }
        m_overlay->setProperty("hintsRevealed", !m_hintReveals.empty());
    }
}

QRectF MirrorWindow::pictureInWindow() const
{
    return m_video->contentRect().translated(m_video->position());
}

int MirrorWindow::reservedTop() const noexcept
{
    if (m_editor != nullptr) {
        return m_editTop;
    }
    if (m_mini) {
        return 0;
    }
    if (m_toolbarPinned) {
        return m_pinnedBar;
    }
    return m_titleFixed && !m_fullScreen ? m_titleHeight : 0;
}

int MirrorWindow::reservedRight() const noexcept
{
    return m_editor != nullptr ? m_editRight : 0;
}

int MirrorWindow::reservedBottom() const noexcept
{
    return m_editor != nullptr ? m_editBottom : 0;
}

void MirrorWindow::setEditor(ProfileEditor* editor)
{
    if (editor == m_editor) {
        return;
    }
    const int oldTop = reservedTop();
    const int oldRight = reservedRight();
    const int oldBottom = reservedBottom();
    const bool entering = m_editor == nullptr;
    const bool resizable = isVisible() && plain(windowStates()) && !m_mini;
    const bool hadLogic = m_logicShown;
    if (entering) {
        m_editRestore = resizable ? geometry() : QRect();
        m_editAspect = m_aspect;
    }
    if (m_editor != nullptr) {
        disconnect(m_editor, nullptr, this, nullptr);
    }
    m_editor = editor;
    m_logicShown = false;
    m_logicRestore = QRect();
    m_zoom = 1;
    m_scroll = {};
    m_video->setFrozen(false);
    if (m_editor != nullptr) {
        connect(m_editor, &ProfileEditor::tryingChanged, this, &MirrorWindow::followTrying);
        connect(m_editor, &ProfileEditor::logicShownChanged, this, &MirrorWindow::followLogic);
    }
    if (m_overlay != nullptr) {
        m_overlay->setProperty("editor", QVariant::fromValue(editor));
        m_editTop = m_overlay->property("editingTop").toInt();
        m_editRight = m_overlay->property("editingRight").toInt();
        m_editBottom = m_overlay->property("editingBottom").toInt();
        m_overlay->setProperty("reservedTop", reservedTop());
    }
    enablePointer();
    if (!entering && resizable && m_editRestore.isValid() && std::abs(m_aspect - m_editAspect) < 0.01) {
        setGeometry(m_editRestore);
        layoutContent();
    } else if (resizable) {
        resize(width() + reservedRight() - oldRight,
               height() + reservedTop() - oldTop + reservedBottom() - oldBottom);
    } else {
        layoutContent();
    }
    if (hadLogic) {
        fitToPicture();
    }
    applyPointer();
    updateHints();
    if (m_editor == nullptr) {
        m_pointer->forceActiveFocus(Qt::OtherFocusReason);
    }
    followFocus();
}

bool MirrorWindow::unsaved() const
{
    if (m_editor == nullptr) {
        return false;
    }
    const auto* bench = qobject_cast<const ScriptWorkbench*>(m_editor->logic());
    return m_editor->dirty() || (bench != nullptr && bench->dirtyCount() > 0);
}

void MirrorWindow::followLogic()
{
    const bool shown = m_editor != nullptr && m_editor->logicShown();
    if (shown == m_logicShown) {
        return;
    }
    m_logicShown = shown;
    const bool resizable = isVisible() && plain(windowStates()) && !m_mini && screen() != nullptr;
    if (shown) {
        m_logicRestore = QRect();
        if (resizable && m_overlay != nullptr) {
            const QRect area = screen()->availableGeometry();
            const QSize wanted(
                std::min(std::max(width(), m_overlay->property("logicWidth").toInt()), area.width()),
                std::min(std::max(height(), m_overlay->property("logicHeight").toInt()), area.height()));
            if (wanted != size()) {
                m_logicRestore = geometry();
                QRect next(position(), wanted);
                next.moveLeft(std::clamp(next.left(), area.left(), std::max(area.left(), area.right() + 1 - next.width())));
                next.moveTop(std::clamp(next.top(), area.top(), std::max(area.top(), area.bottom() + 1 - next.height())));
                setGeometry(next);
            }
        }
    } else {
        if (resizable && m_logicRestore.isValid()) {
            setGeometry(m_logicRestore);
        }
        m_logicRestore = QRect();
        fitToPicture();
    }
    layoutContent();
    updateHints();
}

void MirrorWindow::setPictureFrozen(bool frozen)
{
    m_video->setFrozen(frozen && arranging());
}

void MirrorWindow::zoomPicture(qreal zoom, QPointF anchor)
{
    if (!arranging()) {
        return;
    }
    const qreal next = std::clamp(zoom, kLeastZoom, kMostZoom);
    const QSize frame = m_video->frameSize();
    if (!frame.isEmpty()) {
        m_scroll = scrollKeeping(pictureView(), QSizeF(frame), m_zoom, next, m_scroll, anchor);
    }
    m_zoom = next;
    layoutContent();
    updateHints();
}

void MirrorWindow::scrollPicture(QPointF delta)
{
    if (!arranging() || qFuzzyCompare(m_zoom, 1.0)) {
        return;
    }
    m_scroll += delta;
    layoutContent();
    updateHints();
}

void MirrorWindow::followTrying()
{
    m_video->setFrozen(false);
    enablePointer();
    layoutContent();
    applyPointer();
    updateHints();
    if (trying()) {
        m_pointer->forceActiveFocus(Qt::OtherFocusReason);
    }
    followFocus();
}

void MirrorWindow::followFocus()
{
    const bool now = trying() && m_pointer->hasActiveFocus();
    if (now != m_playing) {
        m_playing = now;
        emit playingChanged();
    }
}

void MirrorWindow::enablePointer()
{
    m_pointer->setEnabled((m_editor == nullptr || m_editor->trying()) && !m_mini);
}

void MirrorWindow::followPinnedBar()
{
    if (m_overlay == nullptr) {
        return;
    }
    const int bar = m_overlay->property("pinnedBarHeight").toInt();
    if (bar <= 0 || bar == m_pinnedBar) {
        return;
    }
    m_pinnedBar = bar;
    if (m_toolbarPinned) {
        m_overlay->setProperty("reservedTop", reservedTop());
        layoutContent();
        updateHints();
    }
}

void MirrorWindow::layoutContent()
{
    const QRectF area = videoArea(pictureView());
    m_video->setPosition(area.topLeft());
    m_video->setSize(area.size());
    m_pointer->setPosition(area.topLeft());
    m_pointer->setSize(area.size());
    m_diagnostics->setTopInset(reservedTop());
    if (m_overlay != nullptr) {
        m_overlay->setSize(size());
    }
}

QRectF MirrorWindow::pictureView() const
{
    const int top = reservedTop();
    return {0, static_cast<qreal>(top), static_cast<qreal>(std::max(0, width() - reservedRight())),
            static_cast<qreal>(std::max(0, height() - top - reservedBottom()))};
}

QRectF MirrorWindow::videoArea(const QRectF& view)
{
    const QSize frame = m_video->frameSize();
    if (!arranging() || qFuzzyCompare(m_zoom, 1.0) || frame.isEmpty()) {
        return view;
    }
    const ZoomedPicture zoomed = zoomedPicture(view, QSizeF(frame), m_zoom, m_scroll);
    m_scroll = zoomed.scroll;
    return zoomed.picture;
}

void MirrorWindow::setToolbarPinned(bool pinned)
{
    if (pinned == m_toolbarPinned) {
        return;
    }
    const int oldTop = reservedTop();
    m_toolbarPinned = pinned;
    if (m_overlay != nullptr) {
        m_overlay->setProperty("reservedTop", reservedTop());
        m_overlay->setProperty("toolbarPinned", pinned);
    }
    if (isVisible() && plain(windowStates()) && !m_mini && m_editor == nullptr) {
        resize(width(), height() + reservedTop() - oldTop);
    } else {
        layoutContent();
    }
    updateHints();
}

void MirrorWindow::setTitleBarFixed(bool fixed)
{
    if (fixed == m_titleFixed) {
        return;
    }
    const int oldTop = reservedTop();
    m_titleFixed = fixed;
    if (m_overlay != nullptr) {
        m_overlay->setProperty("reservedTop", reservedTop());
        m_overlay->setProperty("titleFixed", fixed);
    }
    const int change = reservedTop() - oldTop;
    if (change != 0 && isVisible() && plain(windowStates()) && !m_mini && m_editor == nullptr) {
        resize(width(), height() + change);
    } else {
        layoutContent();
    }
    updateHints();
}

void MirrorWindow::toggleToolbar()
{
    if (m_overlay != nullptr) {
        QMetaObject::invokeMethod(m_overlay.data(), "toggleToolbar");
    }
}

void MirrorWindow::centerCursor()
{
    const auto window = static_cast<std::uintptr_t>(winId());
    if (!m_foreground || !platform::isInFront(window)) {
        return;
    }
    QCursor::setPos(screen(), mapToGlobal(pictureInWindow().center().toPoint()));
}

void MirrorWindow::reportPhoneCursor()
{
    const std::optional<input::Vec2> position =
        m_hover ? MirrorInputItem::normalized(pictureInWindow(), *m_hover, false) : std::nullopt;
    const protocol::CursorState cursor = phoneCursorOf(position, m_hoverPressed, m_foreground, m_pointer->capturing());
    if (cursor == m_phoneCursor) {
        return;
    }
    m_phoneCursor = cursor;
    emit phoneCursorChanged(cursor);
}

void MirrorWindow::applyClaims()
{
    const bool mapping = m_keymap && !m_textInput && !m_mini && !arranging();
    std::vector<input::Chord> claims = mapping ? liveClaims() : std::vector<input::Chord>();
    if (m_overlay != nullptr) {
        const std::optional<input::InputSource> right = input::parseSource("Mouse.Right");
        const bool rightClaimed =
            right && std::ranges::any_of(claims, [&right](const input::Chord& chord) { return chord.source == *right; });
        m_overlay->setProperty("rightClaimed", rightClaimed);
    }
    m_pointer->setClaimedInputs(std::move(claims));
}

std::vector<input::Chord> MirrorWindow::liveClaims() const
{
    return m_profile != nullptr ? MirrorInputItem::liveClaims(*m_profile, m_cursorClaims, m_liveControls)
                                : std::vector<input::Chord>();
}

void MirrorWindow::applyPointer()
{
    const bool captured = m_keymap && m_locked && !m_peek && !m_textInput && !m_mini && !arranging();
    m_pointer->setCapturing(captured);
    reportPhoneCursor();
    applyClaims();
    if (captured && m_foreground && !windowStates().testFlag(Qt::WindowMinimized)) {
        const auto window = static_cast<std::uintptr_t>(winId());
        if (platform::isInFront(window)) {
            setCursor(Qt::BlankCursor);
            m_clipped = pinCursor(window);
            if (!m_clipGuard.isActive()) {
                m_clipGuard.start();
            }
            return;
        }
    }
    m_clipGuard.stop();
    unsetCursor();
    if (std::exchange(m_clipped, false)) {
        platform::releaseCursorClip();
    }
}

void MirrorWindow::keepClip()
{
    const auto window = static_cast<std::uintptr_t>(winId());
    if (!platform::isInFront(window)) {
        applyPointer();
        return;
    }
    m_clipped = pinCursor(window);
}

bool MirrorWindow::pinCursor(std::uintptr_t window) const
{
    const QPointF center = pictureInWindow().center();
    return width() > 0 && height() > 0 &&
           platform::pinCursorInClientArea(window, center.x() / width(), center.y() / height());
}

void MirrorWindow::setFullScreen(bool on)
{
    if (on == m_fullScreen) {
        return;
    }
    if (on) {
        m_restoreMaximized = windowStates().testFlag(Qt::WindowMaximized);
        m_normalGeometry = m_restoreMaximized ? QRect() : geometry();
        showFullScreen();
    } else if (m_restoreMaximized) {
        showMaximized();
    } else {
        showNormal();
    }
}

void MirrorWindow::setMini(bool on)
{
    if (on == m_mini) {
        return;
    }
    if (on) {
        const Qt::WindowStates states = windowStates();
        m_miniStates = states & (Qt::WindowMaximized | Qt::WindowFullScreen);
        m_miniRestore = m_fullScreen && m_normalGeometry.isValid() ? m_normalGeometry
                        : plain(states)                             ? geometry()
                                                                    : QRect();
        m_normalGeometry = QRect();
        m_mini = true;
        setFlag(Qt::WindowStaysOnTopHint, true);
        showNormal();
        setGeometry(miniGeometry());
    } else {
        m_miniPosition = position();
        m_mini = false;
        setFlag(Qt::WindowStaysOnTopHint, false);
        showNormal();
        if (m_miniRestore.isValid()) {
            setGeometry(m_miniRestore);
        } else if (screen() != nullptr && m_aspect > 0) {
            const QRect area = screen()->availableGeometry();
            const qreal height = std::min(area.height() * kScreenShare, area.width() * kScreenShare / m_aspect);
            const QSize fitted(qRound(height * m_aspect), qRound(height));
            setGeometry(QRect(area.center() - QPoint(fitted.width() / 2, fitted.height() / 2), fitted));
        }
        if (m_miniStates.testFlag(Qt::WindowFullScreen)) {
            setFullScreen(true);
        } else if (m_miniStates.testFlag(Qt::WindowMaximized)) {
            showMaximized();
        }
        raise();
        requestActivate();
    }
    enablePointer();
    if (m_overlay != nullptr) {
        m_overlay->setProperty("mini", on);
        m_overlay->setProperty("reservedTop", reservedTop());
    }
    layoutContent();
    applyPointer();
    updateHints();
    emit miniChanged();
}

QRect MirrorWindow::miniGeometry() const
{
    const QRectF content = m_video->contentRect();
    const qreal aspect = !content.isEmpty() ? content.width() / content.height() : m_aspect > 0 ? m_aspect : 0.5;
    const QSize size = aspect >= 1 ? QSize(kMiniLongSide, qRound(kMiniLongSide / aspect))
                                   : QSize(qRound(kMiniLongSide * aspect), kMiniLongSide);
    const QRect area = screen() != nullptr ? screen()->availableGeometry() : QRect(QPoint(), size);
    QPoint corner = m_miniPosition.value_or(
        QPoint(area.right() + 1 - kMiniMargin - size.width(), area.bottom() + 1 - kMiniMargin - size.height()));
    corner.setX(std::clamp(corner.x(), area.left(), std::max(area.left(), area.right() + 1 - size.width())));
    corner.setY(std::clamp(corner.y(), area.top(), std::max(area.top(), area.bottom() + 1 - size.height())));
    return {corner, size};
}

void MirrorWindow::setStatus(double framesPerSecond, double latencyMs, const QString& transport,
                             const QString& problem)
{
    if (m_overlay == nullptr) {
        return;
    }
    m_overlay->setProperty("framesPerSecond", framesPerSecond);
    m_overlay->setProperty("latencyMs", latencyMs);
    m_overlay->setProperty("transport", transport);
    m_overlay->setProperty("statusProblem", problem);
}

void MirrorWindow::setLatencyTest(const QVariantMap& state)
{
    if (m_overlay != nullptr) {
        m_overlay->setProperty("latencyTest", state);
    }
}

void MirrorWindow::setRecording(const QVariantMap& state)
{
    m_recording = state;
    if (m_overlay != nullptr) {
        m_overlay->setProperty("recording", state);
    }
}

void MirrorWindow::showRecording(const QVariantMap& info)
{
    if (m_overlay != nullptr) {
        QMetaObject::invokeMethod(m_overlay.data(), "showRecording", Q_ARG(QVariant, QVariant(info)));
    }
}

void MirrorWindow::setVolume(double volume)
{
    if (m_overlay != nullptr) {
        m_overlay->setProperty("volume", volume);
    }
}

void MirrorWindow::setPhoneStatus(const QVariantMap& state)
{
    if (state == m_phoneStatus) {
        return;
    }
    m_phoneStatus = state;
    if (m_overlay != nullptr) {
        m_overlay->setProperty("phoneStatus", m_phoneStatus);
    }
}

void MirrorWindow::showToast(const QString& text, const QString& icon, const QString& actionText,
                             const QString& secondActionText)
{
    if (m_overlay != nullptr) {
        QMetaObject::invokeMethod(m_overlay.data(), "toast", Q_ARG(QVariant, QVariant(text)),
                                  Q_ARG(QVariant, QVariant(icon)), Q_ARG(QVariant, QVariant(actionText)),
                                  Q_ARG(QVariant, QVariant(secondActionText)));
    }
}

void MirrorWindow::showPrompt(quint64 request, const QVariantMap& prompt)
{
    if (m_overlay != nullptr) {
        QMetaObject::invokeMethod(m_overlay.data(), "prompt", Q_ARG(QVariant, QVariant(QString::number(request))),
                                  Q_ARG(QVariant, QVariant(prompt)));
    }
}

void MirrorWindow::withdrawPrompt(quint64 request)
{
    if (m_overlay != nullptr) {
        QMetaObject::invokeMethod(m_overlay.data(), "withdrawPrompt",
                                  Q_ARG(QVariant, QVariant(QString::number(request))));
    }
}

void MirrorWindow::draw(quint64 id, const QVariantMap& drawing)
{
    if (m_overlay != nullptr) {
        QMetaObject::invokeMethod(m_overlay.data(), "draw", Q_ARG(QVariant, QVariant(QString::number(id))),
                                  Q_ARG(QVariant, QVariant(drawing)));
    }
}

void MirrorWindow::erase(quint64 id)
{
    if (m_overlay != nullptr) {
        QMetaObject::invokeMethod(m_overlay.data(), "erase", Q_ARG(QVariant, QVariant(QString::number(id))));
    }
}

void MirrorWindow::clearDrawings()
{
    if (m_overlay != nullptr) {
        QMetaObject::invokeMethod(m_overlay.data(), "clearDrawings");
    }
}

void MirrorWindow::setOverlay(const QString& group, const QVariantList& shapes)
{
    if (m_overlay != nullptr) {
        QMetaObject::invokeMethod(m_overlay.data(), "setOverlay", Q_ARG(QVariant, QVariant(group)),
                                  Q_ARG(QVariant, QVariant(shapes)));
    }
}

void MirrorWindow::setScriptProgress(const QString& source, const QString& title, double value)
{
    if (m_overlay != nullptr) {
        QMetaObject::invokeMethod(m_overlay.data(), "setProgress", Q_ARG(QVariant, QVariant(source)),
                                  Q_ARG(QVariant, QVariant(title)), Q_ARG(QVariant, QVariant(value)));
    }
}

void MirrorWindow::clearScriptProgress(const QString& source)
{
    if (m_overlay != nullptr) {
        QMetaObject::invokeMethod(m_overlay.data(), "clearProgress", Q_ARG(QVariant, QVariant(source)));
    }
}

bool MirrorWindow::event(QEvent* event)
{
    const QEvent::Type type = event->type();
    if (type == QEvent::Close) {
        if (m_editor != nullptr && m_overlay != nullptr && !m_overlay->property("closeChosen").toBool()) {
            event->ignore();
            QMetaObject::invokeMethod(m_overlay.data(), "closeRequested");
            return true;
        }
        emit closedByUser();
        if (m_keptOnClose) {
            event->ignore();
            hide();
            return true;
        }
    } else if (type == QEvent::NonClientAreaMouseMove && m_overlay != nullptr && !m_locked && !m_mini) {
        QMetaObject::invokeMethod(m_overlay.data(), "revealToolbar");
    } else if (type == QEvent::Move) {
        applyPointer();
    }
    if (type == QEvent::MouseMove || type == QEvent::MouseButtonPress || type == QEvent::MouseButtonRelease ||
        type == QEvent::MouseButtonDblClick) {
        const auto* mouse = static_cast<const QMouseEvent*>(event);
        m_hover = mouse->position();
        m_hoverPressed = mouse->buttons().testFlag(Qt::LeftButton);
        reportPhoneCursor();
    } else if (type == QEvent::Leave) {
        m_hover.reset();
        m_hoverPressed = false;
        reportPhoneCursor();
    }
    return QQuickWindow::event(event);
}

void MirrorWindow::keyPressEvent(QKeyEvent* event)
{
    if (m_textInput && !event->isAutoRepeat()) {
        const int key = event->key();
        if (key == Qt::Key_Escape) {
            event->accept();
            emit textInputFinished();
            return;
        }
        if ((key == Qt::Key_Return || key == Qt::Key_Enter) && !m_typingFollowsPhone) {
            QQuickWindow::keyPressEvent(event);
            emit textInputFinished();
            return;
        }
    }
    QQuickWindow::keyPressEvent(event);
}

void MirrorWindow::resizeEvent(QResizeEvent* event)
{
    QQuickWindow::resizeEvent(event);
    layoutContent();
    applyPointer();
    updateHints();
}

void MirrorWindow::followWindowState()
{
    const Qt::WindowStates states = windowStates();
    if (m_mini && states.testAnyFlags(Qt::WindowMaximized | Qt::WindowFullScreen)) {
        showNormal();
        setGeometry(miniGeometry());
        return;
    }
    const bool on = states.testFlag(Qt::WindowFullScreen);
    if (on != m_fullScreen) {
        m_fullScreen = on;
        if (!on && !states.testFlag(Qt::WindowMinimized)) {
            if (plain(states) && m_normalGeometry.isValid()) {
                setGeometry(m_normalGeometry);
            }
            m_normalGeometry = QRect();
            m_restoreMaximized = false;
        }
        if (m_overlay != nullptr) {
            m_overlay->setProperty("fullScreen", on);
            m_overlay->setProperty("reservedTop", reservedTop());
        }
        layoutContent();
        updateHints();
        emit fullScreenChanged();
    }
    fitToPicture();
}

void MirrorWindow::fitToPicture()
{
    const QRectF content = m_video->contentRect();
    if (content.isEmpty() || screen() == nullptr || !plain(windowStates()) || m_logicShown) {
        return;
    }
    if (m_mini) {
        const QSize target = miniGeometry().size();
        if (target != size()) {
            resize(target);
        }
        return;
    }
    const qreal aspect = content.width() / content.height();
    if (std::abs(aspect - m_aspect) < 0.01) {
        return;
    }
    m_aspect = aspect;
    const int top = reservedTop();
    const int right = reservedRight();
    const int bottom = reservedBottom();
    const QSizeF available = screen()->availableGeometry().size() * kScreenShare;
    const qreal height = std::min(available.height() - top - bottom, (available.width() - right) / aspect);
    resize(qRound(height * aspect) + right, qRound(height) + top + bottom);
}

}
