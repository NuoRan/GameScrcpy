#include "ShortcutDispatcher.h"

#include "Commands.h"
#include "DeviceCommands.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>

#include <utility>

namespace gs::app {

namespace {

bool insidePopup(const QQuickItem* item)
{
    for (; item != nullptr; item = item->parentItem()) {
        if (item->inherits("QQuickPopupItem")) {
            return true;
        }
    }
    return false;
}

bool listOpen(const QQuickItem* item)
{
    if (item == nullptr || !item->inherits("QQuickComboBox")) {
        return false;
    }
    const auto* popup = item->property("popup").value<QObject*>();
    return popup != nullptr && popup->property("visible").toBool();
}

bool modalOpen(const QQuickWindow& window)
{
    for (const QQuickItem* layer : window.contentItem()->childItems()) {
        if (!layer->inherits("QQuickOverlay")) {
            continue;
        }
        for (const QQuickItem* item : layer->childItems()) {
            const QObject* popup = item->inherits("QQuickPopupItem") ? item->parent() : nullptr;
            if (popup != nullptr && item->isVisible() && popup->property("modal").toBool()) {
                return true;
            }
        }
    }
    return false;
}

bool typesText(const QQuickItem* focus, const QKeyEvent& key)
{
    constexpr Qt::KeyboardModifiers altGr = Qt::ControlModifier | Qt::AltModifier;
    const QString text = key.text();
    return focus != nullptr && focus->flags().testFlag(QQuickItem::ItemAcceptsInputMethod) &&
           (key.modifiers() & altGr) == altGr && !text.isEmpty() && text.front().isPrint();
}

bool focusWants(QQuickWindow& window, const QKeyEvent& key)
{
    QKeyEvent query(QEvent::ShortcutOverride, key.key(), key.modifiers(), key.nativeScanCode(),
                    key.nativeVirtualKey(), key.nativeModifiers(), key.text(), key.isAutoRepeat(), key.count());
    query.ignore();
    QCoreApplication::sendEvent(&window, &query);
    return query.isAccepted();
}

quint32 keyCode(const QKeyEvent& key)
{
    return key.nativeVirtualKey() != 0 ? key.nativeVirtualKey() : static_cast<quint32>(key.key());
}

}

ShortcutDispatcher::ShortcutDispatcher(Commands& commands, MirrorOf mirrorOf, QObject* parent)
    : QObject(parent), m_commands(commands), m_mirrorOf(std::move(mirrorOf))
{
}

bool ShortcutDispatcher::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type type = event->type();
    if (type == QEvent::ApplicationStateChange &&
        static_cast<const QApplicationStateChangeEvent*>(event)->applicationState() != Qt::ApplicationActive) {
        m_held.clear();
        return false;
    }
    if (type != QEvent::KeyPress && type != QEvent::KeyRelease) {
        return false;
    }
    auto* window = qobject_cast<QQuickWindow*>(watched);
    if (window == nullptr) {
        return false;
    }
    const auto* key = static_cast<const QKeyEvent*>(event);
    const quint32 code = keyCode(*key);
    if (type == QEvent::KeyRelease) {
        return key->isAutoRepeat() ? m_held.contains(code) : m_held.remove(code);
    }
    if (key->isAutoRepeat()) {
        return m_held.contains(code);
    }
    m_held.remove(code);
    const std::optional<QString> mirror = m_mirrorOf ? m_mirrorOf(*window) : std::nullopt;
    const QString id = m_commands.match(Commands::keysOf(*key), mirror ? CommandScope::Mirror : CommandScope::Hub);
    if (id.isEmpty()) {
        return false;
    }
    const QString serial = mirror.value_or(QString());
    if (mirror && m_blocked && m_blocked(serial, *key)) {
        return false;
    }
    const QString target = id == u"app.help" ? (m_topicOf ? m_topicOf(*window) : QString()) : serial;
    const QQuickItem* focus = window->activeFocusItem();
    const bool reserved = mirror && passesTyping(id);
    if (!m_commands.isEnabled(id, target) || insidePopup(focus) || listOpen(focus) || modalOpen(*window) ||
        (!reserved && (typesText(focus, *key) || focusWants(*window, *key))) || !m_commands.trigger(id, target)) {
        return false;
    }
    m_held.insert(code);
    return true;
}

}
