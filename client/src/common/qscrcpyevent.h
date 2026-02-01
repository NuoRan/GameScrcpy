#ifndef QSCRCPYEVENT_H
#define QSCRCPYEVENT_H
#include <QEvent>

class QScrcpyEvent : public QEvent
{
public:
    enum Type
    {
        Control = QEvent::User + 1,
    };
    QScrcpyEvent(Type type) : QEvent(QEvent::Type(type)) {}
};

#endif // QSCRCPYEVENT_H
