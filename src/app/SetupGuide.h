#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace gs::app {

class SetupGuide : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList vendors READ vendors CONSTANT FINAL)

public:
    explicit SetupGuide(QObject* parent = nullptr);

    [[nodiscard]] QVariantList vendors() const;
    Q_INVOKABLE QString vendorFor(const QString& manufacturer) const;
    Q_INVOKABLE void open(int page, const QString& vendor = {}, const QString& step = {});

signals:
    void openRequested(int page, const QString& vendor, const QString& step);
};

}
