#include "GridWindow.h"

#include "MirrorInputItem.h"

#include <gs/platform/Cursor.h>
#include <gs/platform/Foreground.h>
#include <gs/render/GraphicsDevice.h>
#include <gs/render/VideoItem.h>

#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QResizeEvent>
#include <QScreen>
#include <QStyleHints>
#include <QVariant>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace gs::app {

namespace {

constexpr qreal kOverlayZ = -1;
constexpr qreal kVideoZ = 0;
constexpr qreal kHintsZ = 0.5;
constexpr qreal kPointerZ = 1;
constexpr qreal kScreenShare = 0.85;
constexpr double kFitShare = 0.8;
constexpr int kSizeSlack = 2;
constexpr int kLeastWidth = 480;
constexpr int kLeastHeight = 320;
constexpr std::chrono::milliseconds kClipCheck{100};

void setChanged(QObject& item, const char* name, const QVariant& value)
{
    if (item.property(name) != value) {
        item.setProperty(name, value);
    }
}

}

GridWindow::GridWindow(const render::GraphicsDevice& graphics)
{
    graphics.share(*this);
    setColor(Qt::black);
    setMinimumSize(QSize(kLeastWidth, kLeastHeight));
    if (const QStyleHints* hints = QGuiApplication::styleHints(); hints != nullptr) {
        m_cells.setDoubleClick(hints->mouseDoubleClickInterval(), hints->mouseDoubleClickDistance());
    }
    if (const QScreen* primary = QGuiApplication::primaryScreen(); primary != nullptr) {
        const QRect area = primary->availableGeometry();
        const QSize wanted(qRound(area.width() * kScreenShare), qRound(area.height() * kScreenShare));
        setGeometry(QRect(area.center() - QPoint(wanted.width() / 2, wanted.height() / 2), wanted));
    }
    m_clipGuard.setInterval(kClipCheck);
    connect(&m_clipGuard, &QTimer::timeout, this, &GridWindow::keepClip);
    updateTitle();
}

GridWindow::~GridWindow()
{
    if (m_clipped) {
        platform::releaseCursorClip();
    }
}

void GridWindow::attachOverlay(QQmlEngine& engine)
{
    if (m_overlay != nullptr) {
        return;
    }
    QQmlComponent component(&engine, QStringLiteral("GameScrcpy.App"), QStringLiteral("GridOverlay"));
    QObject* created = component.create();
    auto* overlay = qobject_cast<QQuickItem*>(created);
    if (overlay == nullptr) {
        delete created;
        qWarning("grid overlay not created: %s", qUtf8Printable(component.errorString()));
        return;
    }
    overlay->setParent(contentItem());
    overlay->setParentItem(contentItem());
    overlay->setZ(kOverlayZ);
    overlay->setSize(size());
    m_overlay = overlay;
    const auto metric = [overlay](const char* name, double fallback) {
        const QVariant value = overlay->property(name);
        return value.isValid() ? value.toDouble() : fallback;
    };
    m_top = qRound(metric("topInset", m_top));
    m_metrics.header = metric("headerHeight", m_metrics.header);
    m_metrics.gap = metric("gap", m_metrics.gap);
    m_metrics.margin = metric("margin", m_metrics.margin);
    m_metrics.inset = metric("inset", m_metrics.inset);
    m_metrics.menu = metric("menuWidth", m_metrics.menu);
    connect(overlay, SIGNAL(releaseWanted()), this, SLOT(releaseTarget()));
    QQmlComponent hintComponent(&engine, QStringLiteral("GameScrcpy.App"), QStringLiteral("GridHintLayer"));
    QObject* hintObject = hintComponent.create();
    if (auto* hints = qobject_cast<QQuickItem*>(hintObject); hints != nullptr) {
        hints->setParent(contentItem());
        hints->setParentItem(contentItem());
        hints->setZ(kHintsZ);
        hints->setSize(size());
        m_hints = hints;
    } else {
        delete hintObject;
        qWarning("grid hint layer not created: %s", qUtf8Printable(hintComponent.errorString()));
    }
    m_published.clear();
    layoutCells();
}

bool GridWindow::addCell(const QString& serial, const QString& name, double aspect)
{
    if (!m_cells.add(serial, name, aspect, true)) {
        return false;
    }
    auto* pointer = new MirrorInputItem(contentItem());
    pointer->setZ(kPointerZ);
    pointer->setEnabled(false);
    pointer->setVisible(false);
    pointer->setTyping(true);
    connect(pointer, &MirrorInputItem::touched, this, [this, serial](input::CursorPhase phase, input::Vec2 position) {
        emit cursorTouched(serial, phase, position);
    });
    connect(pointer, &MirrorInputItem::scrolled, this,
            [this, serial](input::Vec2 position, int horizontal, int vertical) {
                emit cursorScrolled(serial, position, horizontal, vertical);
            });
    connect(pointer, &MirrorInputItem::androidKey, this,
            [this, serial](int keycode, int metaState, bool down, int repeat) {
                emit cursorKey(serial, keycode, metaState, down, repeat);
            });
    connect(pointer, &MirrorInputItem::typed, this,
            [this, serial](const QString& text) { emit cursorTyped(serial, text); });
    m_panes[serial].pointer = pointer;
    layoutCells();
    return true;
}

bool GridWindow::removeCell(const QString& serial)
{
    if (const auto pane = m_panes.find(serial); pane != m_panes.end()) {
        if (MirrorInputItem* pointer = pane->second.pointer; pointer != nullptr && pointer->hasActiveFocus()) {
            contentItem()->forceActiveFocus(Qt::OtherFocusReason);
        }
        delete pane->second.video.data();
        delete pane->second.pointer.data();
        m_panes.erase(pane);
    }
    if (m_wheelSerial == serial) {
        m_wheelSerial.clear();
        m_wheel.clear();
    }
    if (!m_cells.remove(serial)) {
        return false;
    }
    layoutCells();
    return true;
}

void GridWindow::setFrames(const QString& serial, std::shared_ptr<render::FrameSlot> frames)
{
    const auto pane = m_panes.find(serial);
    if (pane == m_panes.end()) {
        return;
    }
    if (frames == nullptr) {
        delete pane->second.video.data();
        m_cells.setSeparate(serial, true);
    } else {
        render::VideoItem* video = pane->second.video;
        if (video == nullptr) {
            video = new render::VideoItem(contentItem());
            video->setZ(kVideoZ);
            video->setCrop(pane->second.crop);
            connect(video, &render::VideoItem::contentRectChanged, this, [this, serial] { followFrame(serial); });
            pane->second.video = video;
        }
        video->setSource(std::move(frames));
        m_cells.setSeparate(serial, false);
    }
    layoutCells();
}

void GridWindow::setLabel(const QString& serial, const QString& name, const QString& tone, const QString& status)
{
    if (m_cells.setLabel(serial, name, tone, status)) {
        publishCells();
    }
}

void GridWindow::setRate(const QString& serial, double framesPerSecond)
{
    if (m_cells.setRate(serial, framesPerSecond)) {
        publishCells();
    }
}

void GridWindow::setSync(const QString& serial, const GridCells::Sync& sync)
{
    if (m_cells.setSync(serial, sync)) {
        publishCells();
    }
}

bool GridWindow::holds(const QString& serial) const
{
    const GridCells::Cell* cell = m_cells.find(serial);
    return cell != nullptr && !cell->separate;
}

bool GridWindow::separate(const QString& serial) const
{
    const GridCells::Cell* cell = m_cells.find(serial);
    return cell != nullptr && cell->separate;
}

void GridWindow::setTarget(const QString& serial)
{
    if (m_cells.setTarget(serial)) {
        targetChanged();
    }
}

void GridWindow::releaseTarget()
{
    if (!m_cells.setTarget(QString())) {
        return;
    }
    targetChanged();
    emit targetChosen(QString());
}

QRectF GridWindow::pictureShare(const QString& serial) const
{
    const QRectF picture = pictureOf(serial);
    if (picture.isEmpty() || width() <= 0 || height() <= 0) {
        return {};
    }
    const double wide = width();
    const double high = height();
    return {picture.x() / wide, picture.y() / high, picture.width() / wide, picture.height() / high};
}

void GridWindow::setKeymap(bool on)
{
    if (on == m_keymap) {
        return;
    }
    m_keymap = on;
    m_textInput = m_textInput && on;
    applyPointer();
    updateHints();
}

void GridWindow::setCameraLocked(bool locked)
{
    if (locked == m_locked) {
        return;
    }
    m_locked = locked;
    applyPointer();
    if (!locked) {
        centerCursor();
    }
}

void GridWindow::setCursorPeek(bool on)
{
    if (on == m_peek) {
        return;
    }
    m_peek = on;
    applyPointer();
    if (on) {
        centerCursor();
    }
}

void GridWindow::setTextInput(bool on)
{
    if (on == m_textInput) {
        return;
    }
    m_textInput = on;
    if (on) {
        showToast(QCoreApplication::translate("gs::app::MirrorWindow",
                                              "Typing on the phone: Enter sends, Esc goes back to the game"),
                  QStringLiteral("keyboard"));
    }
    applyPointer();
    updateHints();
}

void GridWindow::setForeground(bool front)
{
    if (front == m_foreground) {
        return;
    }
    m_foreground = front;
    if (!front) {
        m_wheelSerial.clear();
        m_wheel.clear();
    }
    applyPointer();
    pushWheel();
}

void GridWindow::setProfile(std::shared_ptr<const input::Profile> profile)
{
    if (profile == m_profile) {
        return;
    }
    m_profile = std::move(profile);
    m_cursorClaims = m_profile != nullptr ? input::cursorClaimChords(*m_profile) : std::vector<input::CursorClaim>();
    applyPointer();
    updateHints();
}

void GridWindow::setLiveControls(const QString& serial, const QList<bool>& live, quintptr profile)
{
    const auto pane = m_panes.find(serial);
    if (pane == m_panes.end()) {
        return;
    }
    pane->second.liveProfile = profile;
    pane->second.live = live;
    if (serial == target()) {
        applyClaims();
        pushHintFlags();
    }
}

void GridWindow::setHintItems(HintItems items)
{
    m_hintItems = std::move(items);
    updateHints();
}

void GridWindow::setAimingControls(const QString& serial, const QList<bool>& aiming, quintptr profile)
{
    const auto pane = m_panes.find(serial);
    if (pane == m_panes.end()) {
        return;
    }
    pane->second.aimingProfile = profile;
    pane->second.aiming = aiming;
    if (serial == target()) {
        pushHintFlags();
    }
}

void GridWindow::setActionWheel(const QString& serial, const QVariantMap& wheel)
{
    m_wheelSerial = serial;
    m_wheel = wheel;
    pushWheel();
}

void GridWindow::setCrop(const QString& serial, const QRectF& crop)
{
    const auto pane = m_panes.find(serial);
    if (pane == m_panes.end()) {
        return;
    }
    pane->second.crop = crop;
    if (render::VideoItem* video = pane->second.video; video != nullptr) {
        video->setCrop(crop);
    }
}

void GridWindow::showToast(const QString& text, const QString& icon)
{
    if (m_overlay != nullptr) {
        QMetaObject::invokeMethod(m_overlay.data(), "toast", Q_ARG(QVariant, QVariant(text)),
                                  Q_ARG(QVariant, QVariant(icon)));
    }
}

bool GridWindow::event(QEvent* event)
{
    const QEvent::Type type = event->type();
    if (type == QEvent::Close) {
        emit closedByUser();
    } else if (type == QEvent::Show) {
        fitToCells();
    } else if (type == QEvent::WindowStateChange) {
        scheduleFit();
    } else if (type == QEvent::Move) {
        applyPointer();
    } else if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick) {
        if (routePress(*static_cast<const QMouseEvent*>(event), type == QEvent::MouseButtonDblClick)) {
            event->accept();
            return true;
        }
    } else if (type == QEvent::MouseButtonRelease) {
        const auto* mouse = static_cast<const QMouseEvent*>(event);
        if (m_swallowed.testFlag(mouse->button())) {
            m_swallowed.setFlag(mouse->button(), false);
            event->accept();
            return true;
        }
    } else if (type == QEvent::MouseMove) {
        m_swallowed &= static_cast<const QMouseEvent*>(event)->buttons();
        if (m_swallowed != Qt::NoButton) {
            event->accept();
            return true;
        }
    }
    return QQuickWindow::event(event);
}

bool GridWindow::routePress(const QMouseEvent& mouse, bool doubleClick)
{
    if (overlayTakes(mouse.position())) {
        m_forwarded = true;
        return false;
    }
    if (doubleClick) {
        return !m_forwarded;
    }
    m_swallowed &= mouse.buttons();
    const GridCells::Press press =
        m_cells.press(mouse.position(), mouse.button(), static_cast<std::int64_t>(mouse.timestamp()));
    m_forwarded = press.action == GridCells::Action::Outside || press.action == GridCells::Action::Forward ||
                  press.action == GridCells::Action::Menu;
    switch (press.action) {
    case GridCells::Action::Outside:
    case GridCells::Action::Forward:
    case GridCells::Action::Menu:
        return false;
    case GridCells::Action::None:
        break;
    case GridCells::Action::Select:
        targetChanged();
        emit targetChosen(press.serial);
        break;
    case GridCells::Action::OwnWindow:
        emit ownWindowWanted(press.serial);
        break;
    case GridCells::Action::BackToGrid:
        emit backToGridWanted(press.serial);
        break;
    }
    m_swallowed.setFlag(mouse.button());
    return true;
}

bool GridWindow::popupOpen() const
{
    return m_overlay != nullptr && m_overlay->property("popupOpen").toBool();
}

bool GridWindow::overlayTakes(QPointF at) const
{
    return popupOpen() || (m_overlay != nullptr && m_overlay->property("noticeArea").toRectF().contains(at));
}

void GridWindow::keyPressEvent(QKeyEvent* event)
{
    if (m_textInput && !event->isAutoRepeat()) {
        const int key = event->key();
        if (key == Qt::Key_Escape) {
            event->accept();
            emit textInputFinished();
            return;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            QQuickWindow::keyPressEvent(event);
            emit textInputFinished();
            return;
        }
    }
    QQuickWindow::keyPressEvent(event);
}

void GridWindow::resizeEvent(QResizeEvent* event)
{
    QQuickWindow::resizeEvent(event);
    if (m_overlay != nullptr) {
        m_overlay->setSize(size());
    }
    if (m_hints != nullptr) {
        m_hints->setSize(size());
    }
    layoutCells();
}

void GridWindow::layoutCells()
{
    m_layoutQueued = false;
    m_cells.layout(QRectF(0.0, m_top, width(), std::max(0, height() - m_top)), m_metrics);
    for (const GridCells::Cell& cell : m_cells.cells()) {
        const auto pane = m_panes.find(cell.serial);
        if (pane == m_panes.end()) {
            continue;
        }
        render::VideoItem* video = pane->second.video;
        if (video != nullptr) {
            video->setPosition(cell.view.topLeft());
            video->setSize(cell.view.size());
        }
        if (MirrorInputItem* pointer = pane->second.pointer; pointer != nullptr) {
            pointer->setPosition(cell.view.topLeft());
            pointer->setSize(cell.view.size());
            pointer->setContentRect(video != nullptr ? video->contentRect() : QRectF());
        }
    }
    focusTarget();
    applyPointer();
    publishCells();
    updateHints();
    emit picturesChanged();
    scheduleFit();
}

void GridWindow::scheduleLayout()
{
    if (std::exchange(m_layoutQueued, true)) {
        return;
    }
    QMetaObject::invokeMethod(this, &GridWindow::layoutCells, Qt::QueuedConnection);
}

GridWindow::Fit GridWindow::fitWanted() const
{
    return {.cells = m_cells.cells().size(), .known = m_cells.knownAspects()};
}

void GridWindow::scheduleFit()
{
    if (m_userSized || m_fitQueued || !isVisible() || fitWanted() == m_fitted) {
        return;
    }
    m_fitQueued = true;
    QMetaObject::invokeMethod(this, &GridWindow::fitToCells, Qt::QueuedConnection);
}

void GridWindow::fitToCells()
{
    m_fitQueued = false;
    const Fit key = fitWanted();
    if (m_userSized || key == m_fitted ||
        windowStates().testAnyFlags(Qt::WindowMinimized | Qt::WindowMaximized | Qt::WindowFullScreen)) {
        return;
    }
    if (!m_fitSize.isEmpty() && (std::abs(width() - m_fitSize.width()) > kSizeSlack ||
                                 std::abs(height() - m_fitSize.height()) > kSizeSlack)) {
        m_userSized = true;
        return;
    }
    const QScreen* where = screen();
    if (where == nullptr) {
        return;
    }
    const QRect area = where->availableGeometry();
    const QSize grid = m_cells.windowSize(area.size(), m_top, m_metrics, kFitShare);
    m_fitted = key;
    if (grid.isEmpty()) {
        return;
    }
    const QSize chosen = grid.expandedTo(minimumSize()).boundedTo(area.size());
    QRect placed(QPoint(0, 0), chosen);
    placed.moveCenter(m_placed ? geometry().center() : area.center());
    placed.moveLeft(std::clamp(placed.left(), area.left(), std::max(area.left(), area.right() + 1 - placed.width())));
    placed.moveTop(std::clamp(placed.top(), area.top(), std::max(area.top(), area.bottom() + 1 - placed.height())));
    m_placed = true;
    setGeometry(placed);
    m_fitSize = size();
}

void GridWindow::followFrame(const QString& serial)
{
    const auto pane = m_panes.find(serial);
    if (pane == m_panes.end() || pane->second.video == nullptr) {
        return;
    }
    const render::VideoItem* video = pane->second.video;
    const QSize frame = video->frameSize();
    if (!frame.isEmpty() && m_cells.setAspect(serial, static_cast<double>(frame.width()) / frame.height())) {
        scheduleLayout();
        return;
    }
    if (MirrorInputItem* pointer = pane->second.pointer; pointer != nullptr) {
        pointer->setContentRect(video->contentRect());
    }
    if (serial == target()) {
        applyPointer();
        updateHints();
    }
    emit picturesChanged();
}

void GridWindow::targetChanged()
{
    if (m_wheelSerial != target()) {
        m_wheelSerial.clear();
        m_wheel.clear();
    }
    focusTarget();
    applyPointer();
    publishCells();
    updateHints();
}

void GridWindow::focusTarget()
{
    MirrorInputItem* focus = targetPointer();
    const bool popup = popupOpen();
    if (focus != nullptr) {
        focus->setVisible(true);
        focus->setEnabled(true);
        if (!popup && !focus->hasActiveFocus()) {
            focus->forceActiveFocus(Qt::OtherFocusReason);
        }
    } else if (!popup && qobject_cast<MirrorInputItem*>(activeFocusItem()) != nullptr) {
        contentItem()->forceActiveFocus(Qt::OtherFocusReason);
    }
    for (auto& entry : m_panes) {
        MirrorInputItem* pointer = entry.second.pointer;
        if (pointer != nullptr && pointer != focus) {
            pointer->setEnabled(false);
            pointer->setVisible(false);
        }
    }
}

void GridWindow::applyPointer()
{
    MirrorInputItem* focus = targetPointer();
    const bool captured = focus != nullptr && m_keymap && m_locked && !m_peek && !m_textInput;
    for (auto& entry : m_panes) {
        if (MirrorInputItem* pointer = entry.second.pointer; pointer != nullptr) {
            pointer->setTyping(!m_keymap || m_textInput);
            pointer->setCapturing(pointer == focus && captured);
        }
    }
    applyClaims();
    if (captured && m_foreground && !windowStates().testFlag(Qt::WindowMinimized)) {
        const auto window = static_cast<std::uintptr_t>(winId());
        const QRectF share = pictureShare(target());
        if (!share.isEmpty() && platform::isInFront(window)) {
            setCursor(Qt::BlankCursor);
            m_clipped = platform::pinCursorInClientArea(window, share.center().x(), share.center().y());
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

void GridWindow::keepClip()
{
    const auto window = static_cast<std::uintptr_t>(winId());
    const QRectF share = pictureShare(target());
    if (share.isEmpty() || !platform::isInFront(window)) {
        applyPointer();
        return;
    }
    m_clipped = platform::pinCursorInClientArea(window, share.center().x(), share.center().y());
}

void GridWindow::applyClaims()
{
    MirrorInputItem* focus = targetPointer();
    std::vector<input::Chord> claims;
    if (focus != nullptr && m_profile != nullptr && m_keymap && !m_textInput) {
        const auto pane = m_panes.find(target());
        const bool fresh =
            pane != m_panes.end() && pane->second.liveProfile == reinterpret_cast<quintptr>(m_profile.get());
        claims = MirrorInputItem::liveClaims(*m_profile, m_cursorClaims, fresh ? pane->second.live : QList<bool>());
    }
    for (auto& entry : m_panes) {
        if (MirrorInputItem* pointer = entry.second.pointer; pointer != nullptr) {
            pointer->setClaimedInputs(pointer == focus ? claims : std::vector<input::Chord>());
        }
    }
}

void GridWindow::centerCursor()
{
    const QRectF picture = pictureOf(target());
    if (!m_foreground || picture.isEmpty() || !platform::isInFront(static_cast<std::uintptr_t>(winId()))) {
        return;
    }
    QCursor::setPos(screen(), mapToGlobal(picture.center().toPoint()));
}

void GridWindow::publishCells()
{
    updateTitle();
    QVariantList cells;
    cells.reserve(static_cast<qsizetype>(m_cells.cells().size()));
    for (const GridCells::Cell& cell : m_cells.cells()) {
        cells.append(QVariantMap{{QStringLiteral("serial"), cell.serial},
                                 {QStringLiteral("name"), cell.name},
                                 {QStringLiteral("tone"), cell.tone},
                                 {QStringLiteral("status"), cell.status},
                                 {QStringLiteral("rate"), cell.rate},
                                 {QStringLiteral("syncRole"), cell.sync.role},
                                 {QStringLiteral("syncTone"), cell.sync.tone},
                                 {QStringLiteral("syncCount"), cell.sync.followers},
                                 {QStringLiteral("syncLeader"), cell.sync.leader},
                                 {QStringLiteral("separate"), cell.separate},
                                 {QStringLiteral("target"), cell.serial == target()},
                                 {QStringLiteral("x"), cell.area.x()},
                                 {QStringLiteral("y"), cell.area.y()},
                                 {QStringLiteral("width"), cell.area.width()},
                                 {QStringLiteral("height"), cell.area.height()}});
    }
    if (cells == m_published) {
        return;
    }
    m_published = cells;
    if (m_overlay != nullptr) {
        m_overlay->setProperty("cells", cells);
    }
}

void GridWindow::updateTitle()
{
    const QString wanted = tr("Grid · %n phone(s)", "", static_cast<int>(m_cells.cells().size()));
    if (wanted != title()) {
        setTitle(wanted);
    }
}

void GridWindow::updateHints()
{
    if (m_hints == nullptr) {
        return;
    }
    const GridHints shown = gridHints(m_cells, m_keymap, m_textInput, m_foreground, m_wheelSerial);
    const QRectF picture = shown.serial.isEmpty() ? QRectF() : pictureOf(shown.serial);
    QVariantList items;
    if (m_hintItems && m_profile != nullptr && !picture.isEmpty()) {
        items = m_hintItems(*m_profile, picture.size());
    }
    const auto identity = reinterpret_cast<quintptr>(m_profile.get());
    setChanged(*m_hints, "pictureRect", picture);
    setChanged(*m_hints, "hintsAllowed", shown.keyHints);
    setChanged(*m_hints, "profileKey", identity != 0 ? QString::number(identity, 16) : QString());
    if (items != m_shownHints) {
        m_shownHints = items;
        m_hints->setProperty("keyHints", items);
    }
    pushHintFlags();
    pushWheel();
}

void GridWindow::pushHintFlags()
{
    if (m_hints == nullptr) {
        return;
    }
    const auto identity = reinterpret_cast<quintptr>(m_profile.get());
    QVariantList live;
    QVariantList aiming;
    if (const auto pane = m_panes.find(target()); pane != m_panes.end()) {
        if (pane->second.liveProfile == identity) {
            for (const bool flag : pane->second.live) {
                live.append(flag);
            }
        }
        if (pane->second.aimingProfile == identity) {
            for (const bool flag : pane->second.aiming) {
                aiming.append(flag);
            }
        }
    }
    m_hints->setProperty("liveControls", live);
    m_hints->setProperty("aimingControls", aiming);
}

void GridWindow::pushWheel()
{
    if (m_hints == nullptr) {
        return;
    }
    const bool shown = gridHints(m_cells, m_keymap, m_textInput, m_foreground, m_wheelSerial).wheel;
    m_hints->setProperty("actionWheel", shown ? m_wheel : QVariantMap());
}

MirrorInputItem* GridWindow::targetPointer() const
{
    const auto pane = m_panes.find(target());
    return pane != m_panes.end() && pane->second.video != nullptr ? pane->second.pointer.data() : nullptr;
}

QRectF GridWindow::pictureOf(const QString& serial) const
{
    const auto pane = m_panes.find(serial);
    if (pane == m_panes.end() || pane->second.video == nullptr) {
        return {};
    }
    const render::VideoItem* video = pane->second.video;
    return video->contentRect().translated(video->position());
}

}
