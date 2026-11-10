#include "OfflineNetwork.h"

#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QUrl>

#include <memory>

namespace {

QUrl requested(QNetworkAccessManager& manager, const QUrl& url)
{
    const std::unique_ptr<QNetworkReply> reply(manager.get(QNetworkRequest(url)));
    REQUIRE(reply != nullptr);
    const QUrl sent = reply->request().url();
    reply->abort();
    return sent;
}

}

TEST_CASE("the network access manager given to QML refuses every address that is not a local file, a resource or "
          "inline data")
{
    int argc = 1;
    char name[] = "gs_app_tests";
    char* argv[] = {name, nullptr};
    std::unique_ptr<QCoreApplication> application;
    if (QCoreApplication::instance() == nullptr) {
        application = std::make_unique<QCoreApplication>(argc, argv);
    }
    gs::app::OfflineNetworkFactory factory;
    const std::unique_ptr<QNetworkAccessManager> manager(factory.create(nullptr));
    REQUIRE(manager != nullptr);

    CHECK(requested(*manager, QUrl(QStringLiteral("http://192.0.2.1/track.png"))).scheme() == QStringLiteral("blocked"));
    CHECK(requested(*manager, QUrl(QStringLiteral("https://example.com/a.png"))).scheme() == QStringLiteral("blocked"));
    CHECK(requested(*manager, QUrl(QStringLiteral("file://server/share/a.png"))).scheme() == QStringLiteral("blocked"));
    const QUrl local = QUrl::fromLocalFile(QStringLiteral("C:/Windows/win.ini"));
    CHECK(requested(*manager, local) == local);
    const QUrl resource(QStringLiteral("qrc:/i18n/gamescrcpy_zh_CN.qm"));
    CHECK(requested(*manager, resource) == resource);
    const QUrl embedded(QStringLiteral("data:image/png;base64,iVBORw0KGgo="));
    CHECK(requested(*manager, embedded) == embedded);
}
