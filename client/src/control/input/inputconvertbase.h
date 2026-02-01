#ifndef INPUTCONVERTBASE_H
#define INPUTCONVERTBASE_H

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QWheelEvent>
#include <QSize>

#include "controlmsg.h"

class Controller;

class InputConvertBase : public QObject
{
    Q_OBJECT
public:
    InputConvertBase(Controller *controller);
    virtual ~InputConvertBase();

    virtual void mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize) = 0;
    virtual void wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize) = 0;
    virtual void keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize) = 0;

    virtual void setMobileSize(const QSize &size) { m_mobileSize = size; }

    virtual bool isCurrentCustomKeymap()
    {
        return false;
    }

    QSize getTargetSize(const QSize& frameSize, const QSize& showSize) {
        if (m_mobileSize.isValid()) {
            QSize target = m_mobileSize;

            QSize refSize = frameSize.isValid() ? frameSize : showSize;

            if (refSize.isValid()) {
                bool refLandscape = refSize.width() > refSize.height();
                bool mobileLandscape = target.width() > target.height();

                if (refLandscape != mobileLandscape) {
                    target.transpose();
                }
            }
            return target;
        }
        return frameSize;
    }

signals:
    void grabCursor(bool grab);

protected:
    void sendControlMsg(ControlMsg *msg);

    QPointer<Controller> m_controller;
    unsigned m_repeat = 0;

    QSize m_mobileSize;
};

#endif // INPUTCONVERTBASE_H
