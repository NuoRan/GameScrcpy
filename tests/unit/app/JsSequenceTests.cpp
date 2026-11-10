#include "JsSequence.h"

#include <QCoreApplication>
#include <QJSEngine>
#include <QJSValue>
#include <QMetaType>
#include <QSequentialIterable>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <doctest/doctest.h>

#include <memory>
#include <vector>

namespace {

struct Application {
    Application()
    {
        if (QCoreApplication::instance() == nullptr) {
            app = std::make_unique<QCoreApplication>(argc, argv.data());
        }
    }

    int argc = 1;
    std::vector<char*> argv{const_cast<char*>("gs_app_tests"), nullptr};
    std::unique_ptr<QCoreApplication> app;
};

constexpr int kMostSteps = 1000;

QVariantList walk(const QJSValue& value)
{
    const QVariant held = QVariant::fromValue(value);
    QSequentialIterable iterable;
    REQUIRE(QMetaType::convert(held.metaType(), held.constData(), QMetaType::fromType<QSequentialIterable>(), &iterable));
    QVariantList seen;
    for (auto it = iterable.constBegin(), end = iterable.constEnd(); it != end && seen.size() < kMostSteps; ++it) {
        seen.push_back(*it);
    }
    return seen;
}

}

TEST_CASE("a JavaScript object held in a QVariant walks as an empty sequence, so marking a compiled QML frame ends")
{
    const Application application;
    QJSEngine engine;
    gs::app::installJsSequence();

    CHECK(walk(engine.evaluate(QStringLiteral("({ keyword: '#fff', nested: { deeper: [1, 2] } })"))).isEmpty());
    CHECK(walk(engine.evaluate(QStringLiteral("'text'"))).isEmpty());

    const QVariantList array = walk(engine.evaluate(QStringLiteral("[1, 'two', { three: 3 }]")));
    REQUIRE(array.size() == 3);
    CHECK(array.at(0).toInt() == 1);
    CHECK(array.at(1).toString() == QStringLiteral("two"));
    CHECK(array.at(2).toMap().value(QStringLiteral("three")).toInt() == 3);
    CHECK(walk(engine.evaluate(QStringLiteral("[]"))).isEmpty());
}
