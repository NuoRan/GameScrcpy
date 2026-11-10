#pragma once

#include <QQuickItem>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace gs::app {

class KeyCapture : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit KeyCapture(QQuickItem* parent = nullptr);

signals:
    void captured(const QString& keys);
    void cancelled();
    void cleared();

protected:
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
};

}
