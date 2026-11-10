#include "SettingVariant.h"

#include <doctest/doctest.h>

#include <QMetaType>
#include <QString>

#include <cstdint>
#include <limits>
#include <string>

using gs::app::fromVariant;
using gs::app::toVariant;
namespace settings = gs::app::settings;

namespace {

const settings::Entry& entryAt(const char* path)
{
    const settings::Entry* entry = settings::findSetting(path);
    REQUIRE(entry != nullptr);
    return *entry;
}

}

TEST_CASE("setting values reach QML as booleans, 64-bit integers and strings")
{
    CHECK(toVariant(settings::Value{true}).typeId() == QMetaType::Bool);
    const QVariant integer = toVariant(settings::Value{std::int64_t{8000}});
    CHECK(integer.typeId() == QMetaType::LongLong);
    CHECK(integer.toLongLong() == 8000);
    const QVariant text = toVariant(settings::Value{std::string("h265")});
    CHECK(text.typeId() == QMetaType::QString);
    CHECK(text.toString() == QStringLiteral("h265"));
}

TEST_CASE("a value from QML is taken only in its setting's own kind")
{
    const settings::Entry& sound = entryAt("audio.enabled");
    CHECK(fromVariant(sound, QVariant(true)) == settings::Value{true});
    CHECK_FALSE(fromVariant(sound, QVariant(1)));
    CHECK_FALSE(fromVariant(sound, QVariant(QStringLiteral("true"))));

    const settings::Entry& codec = entryAt("video.codec");
    CHECK(fromVariant(codec, QVariant(QStringLiteral("h265"))) == settings::Value{std::string("h265")});
    CHECK_FALSE(fromVariant(codec, QVariant(1)));
}

TEST_CASE("integers from QML may come as whole doubles, but not as fractions, text or beyond 64 bits")
{
    const settings::Entry& fps = entryAt("video.maxFps");
    CHECK(fromVariant(fps, QVariant(60)) == settings::Value{std::int64_t{60}});
    CHECK(fromVariant(fps, QVariant(qint64{90})) == settings::Value{std::int64_t{90}});
    CHECK(fromVariant(fps, QVariant(120.0)) == settings::Value{std::int64_t{120}});
    CHECK(fromVariant(fps, QVariant(30U)) == settings::Value{std::int64_t{30}});
    CHECK_FALSE(fromVariant(fps, QVariant(60.5)));
    CHECK_FALSE(fromVariant(fps, QVariant(std::numeric_limits<double>::quiet_NaN())));
    CHECK_FALSE(fromVariant(fps, QVariant(std::numeric_limits<double>::infinity())));
    CHECK_FALSE(fromVariant(fps, QVariant(1e19)));
    CHECK_FALSE(fromVariant(fps, QVariant(std::numeric_limits<qulonglong>::max())));
    CHECK_FALSE(fromVariant(fps, QVariant(true)));
    CHECK_FALSE(fromVariant(fps, QVariant(QStringLiteral("60"))));
}

TEST_CASE("every shown setting's default survives the way to QML and back")
{
    for (const settings::Entry& entry : settings::catalog()) {
        CAPTURE(entry.path);
        CHECK(fromVariant(entry, toVariant(entry.defaultValue)) == entry.defaultValue);
    }
}
