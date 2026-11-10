#include "OfflineNetwork.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QString>
#include <QUrl>

namespace gs::app {

namespace {

class OfflineAccessManager final : public QNetworkAccessManager
{
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request, QIODevice* data) override
    {
        const QUrl url = request.url();
        if ((url.isLocalFile() && url.host().isEmpty()) || url.scheme() == QStringLiteral("qrc") ||
            url.scheme() == QStringLiteral("data")) {
            return QNetworkAccessManager::createRequest(operation, request, data);
        }
        QNetworkRequest refused(request);
        refused.setUrl(QUrl(QStringLiteral("blocked:")));
        return QNetworkAccessManager::createRequest(operation, refused, data);
    }
};

}

QNetworkAccessManager* OfflineNetworkFactory::create(QObject* parent)
{
    return new OfflineAccessManager(parent);
}

}
