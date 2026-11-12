#include "AppSettings.h"
#include "ExtensionHost.h"
#include "SettingsCatalog.h"

#include "unit/ext/TestFiles.h"

#include <gs/ext/Version.h>

#include <doctest/doctest.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

using gs::app::AppSettings;
using gs::app::ExtensionHost;
using gs::app::extensionSettingPath;
using gs::app::SettingsCatalog;
using namespace gs::ext::test;
namespace fs = std::filesystem;

namespace {

std::string manifest(std::string_view id, std::string_view name, std::string_view settings, std::string_view more)
{
    return R"({"schemaVersion": 1, "id": ")" + std::string(id) + R"(", "version": "1.0.0", "name": ")" +
           std::string(name) + R"(", "license": "MIT", "requires": {"theme": "^1.0"},
           "contributes": {"themes": [{"name": "Plain", "dark": {"bgBase": "#101010"}}]},
           "settings": [)" +
           std::string(settings) + "]" + std::string(more) + "}";
}

struct Fixture {
    Scratch scratch;
    fs::path data = scratch / "data";
    fs::path user = scratch / "user";
    fs::path builtin = scratch / "builtin";

    void add(std::string_view id, std::string_view name, std::string_view settings, std::string_view more = {}) const
    {
        writeFile(user / pathOf(id) / "1.0.0" / "manifest.json", manifest(id, name, settings, more));
    }

    void chooseLanguage(std::string_view language) const
    {
        writeFile(data / "settings.json", R"({"format": "gamescrcpy.settings", "version": 1, "general": {"language": ")" +
                                              std::string(language) + R"("}})");
    }
};

void startUsable(ExtensionHost& host, std::size_t count)
{
    host.start(false);
    REQUIRE(host.extensions().size() == count);
    for (const ExtensionHost::Extension& extension : host.extensions()) {
        INFO(listed(extension.diagnostics));
        REQUIRE(extension.usable());
    }
}

QVariantMap entryOf(const SettingsCatalog& catalog, std::string_view id, std::string_view key)
{
    return catalog.entries().value(extensionSettingPath(id, key)).toMap();
}

}

TEST_CASE("each extension setting gets the control that suits its type and range, with choices named in the interface language")
{
    const Fixture fixture;
    fixture.add("alice.knobs", "Knobs",
                R"({"key": "glow", "type": "boolean", "title": "Glow", "default": true},
                   {"key": "mode", "type": "choice", "title": "Mode", "choices": ["soft", "hard", "auto"], "default": "soft"},
                   {"key": "strength", "type": "number", "title": "Strength", "min": 0, "max": 1, "step": 0.05, "default": 0.5},
                   {"key": "fine", "type": "number", "title": "Fine", "min": 0, "max": 1000, "step": 0.1, "default": 1},
                   {"key": "stops", "type": "integer", "title": "Stops", "min": 0, "max": 200, "default": 0},
                   {"key": "many", "type": "integer", "title": "Many", "min": 0, "max": 201, "default": 0},
                   {"key": "count", "type": "integer", "title": "Count", "min": 1, "default": 3},
                   {"key": "label", "type": "text", "title": "Label", "maxLength": 20, "default": "hi"})",
                R"(, "l10n": {"zh_CN": {"settings.mode.choices.soft": "柔和", "settings.mode.choices.hard": "强烈"}})");
    fixture.chooseLanguage("zh_CN");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    startUsable(host, 1);
    SettingsCatalog catalog;
    const auto entry = [&catalog](std::string_view key) { return entryOf(catalog, "alice.knobs", key); };
    const auto control = [&entry](std::string_view key) { return entry(key).value(QStringLiteral("control")).toString(); };

    CHECK(control("glow") == "switch");
    CHECK(control("mode") == "select");
    CHECK(control("strength") == "slider");
    CHECK(control("fine") == "number");
    CHECK(control("stops") == "slider");
    CHECK(control("many") == "number");
    CHECK(control("count") == "number");
    CHECK(control("label") == "text");

    const QVariantMap mode = entry("mode");
    CHECK(mode.value(QStringLiteral("kind")).toString() == "choice");
    CHECK(mode.value(QStringLiteral("choices")).toStringList() ==
          QStringList{QStringLiteral("soft"), QStringLiteral("hard"), QStringLiteral("auto")});
    const QVariantMap labels = mode.value(QStringLiteral("choiceLabels")).toMap();
    CHECK(labels.size() == 3);
    CHECK(labels.value(QStringLiteral("soft")).toString() == QStringLiteral("柔和"));
    CHECK(labels.value(QStringLiteral("hard")).toString() == QStringLiteral("强烈"));
    CHECK(labels.value(QStringLiteral("auto")).toString() == QStringLiteral("auto"));

    const QVariantMap count = entry("count");
    CHECK(count.value(QStringLiteral("kind")).toString() == "integer");
    REQUIRE(count.contains(QStringLiteral("minimum")));
    CHECK(count.value(QStringLiteral("minimum")).toDouble() == 1.0);
    CHECK_FALSE(count.contains(QStringLiteral("maximum")));
    CHECK(entry("label").value(QStringLiteral("maxLength")).toLongLong() == 20);
    CHECK_FALSE(entry("label").contains(QStringLiteral("minimum")));

    REQUIRE(catalog.extensionGroups().size() == 1);
    const QVariantList rows = catalog.extensionGroups().first().toMap().value(QStringLiteral("settings")).toList();
    REQUIRE(rows.size() == 8);
    CHECK(rows.first().toMap().value(QStringLiteral("path")).toString() == extensionSettingPath("alice.knobs", "glow"));
    for (const QVariant& row : rows) {
        const QVariantMap fields = row.toMap();
        CHECK(fields.value(QStringLiteral("control")) == catalog.entries()
                                                              .value(fields.value(QStringLiteral("path")).toString())
                                                              .toMap()
                                                              .value(QStringLiteral("control")));
    }
}

TEST_CASE("a number setting shows as many decimals as its step and bounds need, at most four, and a missing step follows them")
{
    const Fixture fixture;
    fixture.add("alice.places", "Places",
                R"({"key": "stepped", "type": "number", "title": "Stepped", "min": 0, "max": 1, "step": 0.05, "default": 0.5},
                   {"key": "eighths", "type": "number", "title": "Eighths", "min": 0.125, "max": 1, "default": 0.5},
                   {"key": "plain", "type": "number", "title": "Plain", "default": 1.5},
                   {"key": "whole", "type": "integer", "title": "Whole", "min": 0, "max": 10, "default": 5},
                   {"key": "tiny", "type": "number", "title": "Tiny", "step": 0.00001, "default": 0})");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    startUsable(host, 1);
    SettingsCatalog catalog;
    const auto decimals = [&catalog](std::string_view key) {
        return entryOf(catalog, "alice.places", key).value(QStringLiteral("decimals")).toInt();
    };
    const auto step = [&catalog](std::string_view key) {
        return entryOf(catalog, "alice.places", key).value(QStringLiteral("step")).toDouble();
    };

    CHECK(decimals("stepped") == 2);
    CHECK(step("stepped") == doctest::Approx(0.05).epsilon(1e-9));
    CHECK(decimals("eighths") == 3);
    CHECK(step("eighths") == doctest::Approx(0.001).epsilon(1e-9));
    CHECK(decimals("plain") == 2);
    CHECK(step("plain") == doctest::Approx(0.01).epsilon(1e-9));
    CHECK(decimals("whole") == 0);
    CHECK(step("whole") == 1.0);
    CHECK(decimals("tiny") == 4);
    CHECK(step("tiny") == doctest::Approx(0.0001).epsilon(1e-9));
}

TEST_CASE("the extensions page is offered per device once an extension declares a setting with device scope")
{
    const Fixture fixture;
    fixture.add("alice.flat", "Flat", R"({"key": "glow", "type": "boolean", "title": "Glow", "default": true})");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    startUsable(host, 1);
    SettingsCatalog catalog;
    REQUIRE(catalog.entries().contains(extensionSettingPath("alice.flat", "glow")));
    CHECK_FALSE(entryOf(catalog, "alice.flat", "glow").value(QStringLiteral("perDevice")).toBool());
    CHECK_FALSE(catalog.perDevicePages().contains(QStringLiteral("extensions")));

    fixture.add("bob.near", "Near",
                R"({"key": "level", "type": "integer", "title": "Level", "min": 0, "max": 9, "default": 1, "scope": "device"},
                   {"key": "tint", "type": "boolean", "title": "Tint", "default": false, "scope": "device"})");
    startUsable(host, 2);
    CHECK(entryOf(catalog, "bob.near", "level").value(QStringLiteral("perDevice")).toBool());
    CHECK(entryOf(catalog, "bob.near", "tint").value(QStringLiteral("perDevice")).toBool());
    CHECK(catalog.perDevicePages().count(QStringLiteral("extensions")) == 1);
}

TEST_CASE("extension setting groups are listed by extension name in the interface language")
{
    const Fixture fixture;
    fixture.add("alice.first", "Alpha", R"({"key": "glow", "type": "boolean", "title": "Glow", "default": true})",
                R"(, "l10n": {"zh_CN": {"name": "Zulu"}})");
    fixture.add("bob.second", "Bravo", R"({"key": "glow", "type": "boolean", "title": "Glow", "default": true})",
                R"(, "l10n": {"zh_CN": {"name": "Echo"}})");
    fixture.chooseLanguage("zh_CN");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    startUsable(host, 2);
    SettingsCatalog catalog;
    const auto order = [&catalog] {
        const QVariantList groups = catalog.extensionGroups();
        QStringList ids;
        for (const QVariant& group : groups) {
            ids.append(group.toMap().value(QStringLiteral("id")).toString());
        }
        return ids;
    };

    CHECK(order() == QStringList{QStringLiteral("bob.second"), QStringLiteral("alice.first")});
    CHECK(catalog.extensionGroups().first().toMap().value(QStringLiteral("name")) == QVariant(QStringLiteral("Echo")));

    REQUIRE(settings.setValue(QStringLiteral("general.language"), QVariant(QStringLiteral("en"))));
    catalog.retranslate();
    CHECK(order() == QStringList{QStringLiteral("alice.first"), QStringLiteral("bob.second")});
    CHECK(catalog.extensionGroups().first().toMap().value(QStringLiteral("name")) == QVariant(QStringLiteral("Alpha")));
}

TEST_CASE("ADR-0112: no extension brings back the connection page or the ESP32 settings, not even one with the old "
          "built-in id")
{
    const Fixture fixture;
    writeFile(fixture.builtin / "gamescrcpy.esp32" / "1.0.0" / "manifest.json",
              manifest("gamescrcpy.esp32", "ESP32 touch box", "", ""));
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, fixture.builtin});
    host.start(false);
    SettingsCatalog catalog;
    catalog.setQuery(QStringLiteral("esp32"));
    CHECK_FALSE(catalog.entries().contains(QStringLiteral("esp32.port")));
    CHECK(catalog.hits().isEmpty());
    CHECK_FALSE(catalog.pageHits().contains(QStringLiteral("connection")));
    CHECK_FALSE(catalog.perDevicePages().contains(QStringLiteral("connection")));
}

TEST_CASE("an extension's settings leave the settings catalog when the extension is uninstalled or removed")
{
    const Fixture fixture;
    fixture.add("alice.gone", "Gone", R"({"key": "sparkle", "type": "boolean", "title": "Sparkle", "default": true})");
    fixture.add("bob.gone", "Bye", R"({"key": "shimmer", "type": "boolean", "title": "Shimmer", "default": false})");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    startUsable(host, 2);
    SettingsCatalog catalog;
    const QString sparkle = extensionSettingPath("alice.gone", "sparkle");
    const QString shimmer = extensionSettingPath("bob.gone", "shimmer");
    REQUIRE(catalog.entries().contains(sparkle));
    REQUIRE(catalog.entries().contains(shimmer));
    REQUIRE(catalog.extensionGroups().size() == 2);
    catalog.setQuery(QStringLiteral("Sparkle"));
    REQUIRE(catalog.hits().contains(sparkle));

    int changes = 0;
    QObject::connect(&catalog, &SettingsCatalog::entriesChanged, [&changes] { ++changes; });
    REQUIRE(host.uninstall("alice.gone", *gs::ext::Version::parse("1.0.0")));
    CHECK(changes > 0);
    CHECK_FALSE(catalog.entries().contains(sparkle));
    CHECK(catalog.entries().contains(shimmer));
    CHECK_FALSE(catalog.hits().contains(sparkle));
    REQUIRE(catalog.extensionGroups().size() == 1);
    CHECK(catalog.extensionGroups().first().toMap().value(QStringLiteral("id")) == QVariant(QStringLiteral("bob.gone")));

    REQUIRE(host.remove(QStringLiteral("bob.gone"), QStringLiteral("1.0.0")));
    CHECK_FALSE(catalog.entries().contains(shimmer));
    CHECK(catalog.extensionGroups().isEmpty());
    CHECK(catalog.entries().contains(QStringLiteral("video.maxFps")));
}
