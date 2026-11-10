#pragma once

#include <QObject>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace gs::app {

class ItemOrder : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit ItemOrder(QObject* parent = nullptr);

    Q_INVOKABLE void placeBefore(QQuickItem* item, QQuickItem* sibling) const;
};

}
