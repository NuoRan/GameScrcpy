#ifndef QSCRCPYEVENT_H
#define QSCRCPYEVENT_H
#include <QEvent>

/**
 * @brief 自定义 Qt 事件类型 / Custom Qt Event Type
 *
 * 用于跨线程发送控制命令事件。
 * Used for cross-thread control command event delivery.
 */
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
