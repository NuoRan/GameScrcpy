#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace gs::app {

class AppInfo : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString version READ version CONSTANT FINAL)
    Q_PROPERTY(QString revision READ revision CONSTANT FINAL)
    Q_PROPERTY(QString build READ build CONSTANT FINAL)
    Q_PROPERTY(QVariantList licenses READ licenses CONSTANT FINAL)
    Q_PROPERTY(QString sponsorUrl READ sponsorUrl CONSTANT FINAL)

public:
    explicit AppInfo(QObject* parent = nullptr);

    [[nodiscard]] QString version() const;
    [[nodiscard]] QString revision() const;
    [[nodiscard]] QString build() const;
    [[nodiscard]] QVariantList licenses() const;
    [[nodiscard]] QString sponsorUrl() const;
};

}
