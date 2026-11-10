#include "KeyCapture.h"

#include "Commands.h"

#include <QKeyEvent>

namespace gs::app {

namespace {

bool navigation(const QKeyEvent& event)
{
    return (event.key() == Qt::Key_Tab || event.key() == Qt::Key_Backtab) &&
           !event.modifiers().testAnyFlags(Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
}

}

KeyCapture::KeyCapture(QQuickItem* parent) : QQuickItem(parent)
{
}

bool KeyCapture::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        event->setAccepted(!navigation(*static_cast<QKeyEvent*>(event)));
        return true;
    }
    return QQuickItem::event(event);
}

void KeyCapture::keyPressEvent(QKeyEvent* event)
{
    if (navigation(*event)) {
        event->ignore();
        return;
    }
    event->accept();
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
        emit cancelled();
        return;
    }
    if (event->key() == Qt::Key_Backspace && event->modifiers() == Qt::NoModifier) {
        emit cleared();
        return;
    }
    const QString keys = Commands::portableKeys(Commands::keysOf(*event));
    if (!keys.isEmpty()) {
        emit captured(keys);
    }
}

}
