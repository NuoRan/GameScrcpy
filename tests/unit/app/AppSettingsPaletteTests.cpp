#include "AppSettings.h"

#include "unit/ext/TestFiles.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <QChar>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <filesystem>
#include <string>

using gs::app::AppSettings;
using namespace gs::ext::test;
namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

constexpr std::size_t kLimit = gs::app::settings::Appearance::kMaxPaletteLength;

const QString kSlate = QStringLiteral("alice.slate/Slate");

struct Fixture {
    Scratch scratch;
    fs::path data = scratch / "data";
};

void countAppearance(AppSettings& settings, int& counter)
{
    QObject::connect(&settings, &AppSettings::appearanceChanged, &settings, [&counter] { ++counter; });
}

QString repeated(std::size_t bytes)
{
    return QString(static_cast<qsizetype>(bytes), QLatin1Char('a'));
}

Json savedAppearance(const fs::path& data)
{
    const Json saved = Json::parse(readFile(data / "settings.json"), nullptr, false);
    const auto found = saved.find("appearance");
    return found != saved.end() && found->is_object() ? *found : Json();
}

}

TEST_CASE("a chosen palette is read back and reported once, and choosing it again reports nothing")
{
    const Fixture fixture;
    int changed = 0;
    AppSettings settings(fixture.data);
    countAppearance(settings, changed);
    CHECK(settings.palette().isEmpty());

    settings.setPalette(kSlate);
    CHECK(settings.palette() == kSlate);
    CHECK(changed == 1);

    settings.setPalette(kSlate);
    CHECK(settings.palette() == kSlate);
    CHECK(changed == 1);
}

TEST_CASE("a palette key longer than the limit in UTF-8 bytes is refused while one exactly at the limit is kept")
{
    const Fixture fixture;
    const QString longest = repeated(kLimit);
    const QString overlong = repeated(kLimit + 1);
    const qsizetype wide = static_cast<qsizetype>((kLimit - 1) / 3);
    const QString wideLongest = QString(wide, QChar(u'\u77F3')) + repeated(kLimit - 3 * static_cast<std::size_t>(wide));
    const QString wideOverlong = wideLongest + QStringLiteral("b");
    REQUIRE(longest.toStdString().size() == kLimit);
    REQUIRE(overlong.toStdString().size() == kLimit + 1);
    REQUIRE(wideLongest.toStdString().size() == kLimit);
    REQUIRE(wideOverlong.toStdString().size() == kLimit + 1);
    {
        int changed = 0;
        AppSettings settings(fixture.data);
        countAppearance(settings, changed);
        settings.setPalette(kSlate);
        REQUIRE(changed == 1);

        settings.setPalette(overlong);
        CHECK(settings.palette() == kSlate);
        CHECK(changed == 1);

        settings.setPalette(longest);
        CHECK(settings.palette() == longest);
        CHECK(changed == 2);

        settings.setPalette(wideOverlong);
        CHECK(settings.palette() == longest);
        CHECK(changed == 2);

        settings.setPalette(wideLongest);
        CHECK(settings.palette() == wideLongest);
        CHECK(changed == 3);
    }
    AppSettings reopened(fixture.data);
    CHECK(reopened.palette() == wideLongest);
}

TEST_CASE("the chosen palette is saved under appearance and is still chosen after a restart")
{
    const Fixture fixture;
    {
        AppSettings settings(fixture.data);
        settings.setPalette(kSlate);
    }
    const Json appearance = savedAppearance(fixture.data);
    REQUIRE(appearance.contains("palette"));
    CHECK(appearance.at("palette") == Json(kSlate.toStdString()));

    AppSettings settings(fixture.data);
    CHECK(settings.palette() == kSlate);
}

TEST_CASE("clearing the palette brings back the built-in colors and removes it from the saved file")
{
    const Fixture fixture;
    {
        AppSettings settings(fixture.data);
        settings.setTheme(QStringLiteral("light"));
        settings.setPalette(kSlate);
    }
    REQUIRE(savedAppearance(fixture.data).contains("palette"));
    {
        int changed = 0;
        AppSettings settings(fixture.data);
        countAppearance(settings, changed);
        REQUIRE(settings.palette() == kSlate);

        settings.setPalette(QString());
        CHECK(settings.palette().isEmpty());
        CHECK(changed == 1);
    }
    const Json appearance = savedAppearance(fixture.data);
    REQUIRE(appearance.is_object());
    CHECK(appearance.contains("theme"));
    CHECK_FALSE(appearance.contains("palette"));

    AppSettings settings(fixture.data);
    CHECK(settings.palette().isEmpty());
    CHECK(settings.theme() == QStringLiteral("light"));
}

TEST_CASE("the interface language is the chosen one, and following Windows resolves to Chinese or English")
{
    const Fixture fixture;
    writeFile(fixture.data / "settings.json",
              R"({"format": "gamescrcpy.settings", "version": 1, "general": {"language": "zh_CN"}})");
    AppSettings settings(fixture.data);
    CHECK(settings.language() == QStringLiteral("zh_CN"));
    CHECK(settings.interfaceLanguage() == QStringLiteral("zh_CN"));

    REQUIRE(settings.setValue(QStringLiteral("general.language"), QStringLiteral("en")));
    CHECK(settings.interfaceLanguage() == QStringLiteral("en"));

    REQUIRE(settings.setValue(QStringLiteral("general.language"), QStringLiteral("system")));
    CHECK(settings.language() == QStringLiteral("system"));
    const QStringList resolved{QStringLiteral("zh_CN"), QStringLiteral("en")};
    CHECK(resolved.contains(settings.interfaceLanguage()));
}
