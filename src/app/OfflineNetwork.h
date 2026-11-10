#pragma once

#include <QQmlNetworkAccessManagerFactory>

class QNetworkAccessManager;
class QObject;

namespace gs::app {

class OfflineNetworkFactory final : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager* create(QObject* parent) override;
};

}
