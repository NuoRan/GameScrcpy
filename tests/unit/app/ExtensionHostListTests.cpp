#include "AppSettings.h"
#include "ExtensionHost.h"

#include "unit/ext/TestFiles.h"

#include <gs/ext/Archive.h>
#include <gs/ext/Package.h>

#include <doctest/doctest.h>

#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

using gs::app::AppSettings;
using gs::app::ExtensionHost;
using namespace gs::ext::test;
namespace fs = std::filesystem;

namespace {

constexpr std::string_view kShader = R"(//! PARAM strength float 0.5 0 1 0.05
//! PARAM lines float 3 1 8
//! PARAM steps int 2 1 4
//! PASS glow
float4 glow(float2 uv)
{
    return Source.Sample(Linear, uv) * (strength + lines + steps);
}
)";

constexpr std::string_view kSettings =
    R"({"key": "strength", "type": "number", "title": "Strength", "min": 0, "max": 1, "default": 0.5, "scope": "device"},
       {"key": "steps", "type": "integer", "title": "Steps", "min": 1, "max": 4, "default": 3})";

std::string manifest(std::string_view id, std::string_view version, std::string_view name, std::string_view more)
{
    return R"({"schemaVersion": 1, "id": ")" + std::string(id) + R"(", "version": ")" + std::string(version) +
           R"(", "name": ")" + std::string(name) + R"(", "license": "MIT", "requires": {"filter": "^1.0"},
           "contributes": {"filters": [{"name": "Glow", "file": "shaders/glow.hlsl"}]},
           "settings": [)" +
           std::string(kSettings) + "]" + std::string(more) + "}";
}

void writeExtension(const fs::path& folder, const std::string& manifestText)
{
    writeFile(folder / "manifest.json", manifestText);
    writeFile(folder / "shaders/glow.hlsl", kShader);
}

QString textOf(const fs::path& path)
{
    return QString::fromStdWString(path.wstring());
}

QString urlOf(const fs::path& path)
{
    return QUrl::fromLocalFile(textOf(path)).toString();
}

QVariantMap itemWith(const QVariantList& items, const QString& field, const QString& value)
{
    for (const QVariant& item : items) {
        QVariantMap map = item.toMap();
        if (map.value(field).toString() == value) {
            return map;
        }
    }
    return {};
}

QVariantMap entryOf(const ExtensionHost& host, const QString& id)
{
    return itemWith(host.entries(), QStringLiteral("id"), id);
}

QStringList idsOf(const ExtensionHost& host)
{
    QStringList ids;
    const QVariantList entries = host.entries();
    for (const QVariant& entry : entries) {
        ids.append(entry.toMap().value(QStringLiteral("id")).toString());
    }
    return ids;
}

struct Fixture {
    Scratch scratch;
    fs::path data = scratch / "data";
    fs::path user = scratch / "user";
    fs::path builtin = scratch / "builtin";

    void add(const fs::path& root, std::string_view id, std::string_view version, std::string_view name = "Glow",
             std::string_view more = {}) const
    {
        writeExtension(root / pathOf(id) / pathOf(version), manifest(id, version, name, more));
    }

    [[nodiscard]] fs::path pack(std::string_view id, std::string_view version, std::string_view more) const
    {
        const std::string stem = std::string(id) + "-" + std::string(version);
        const fs::path folder = scratch / ("package-" + stem);
        writeExtension(folder, manifest(id, version, "Glow", more));
        const fs::path archive = scratch / (stem + ".gsext");
        REQUIRE(gs::ext::packDirectory(folder, archive).empty());
        return archive;
    }
};

}

TEST_CASE("the extension list shows built-in extensions first, then the others by name, each off until enabled")
{
    const Fixture fixture;
    fixture.add(fixture.builtin, "gamescrcpy.glow", "1.0.0", "Zenith");
    fixture.add(fixture.user, "alice.glow", "2.0.0", "Glow", R"(, "description": "Soft light", "authors": ["Alice"])");
    fixture.add(fixture.user, "bob.aura", "1.0.0", "Aura");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, fixture.builtin});
    host.start(false);

    const QStringList order{QStringLiteral("gamescrcpy.glow"), QStringLiteral("bob.aura"), QStringLiteral("alice.glow")};
    CHECK(idsOf(host) == order);
    const QVariantList entries = host.entries();
    QSet<QString> keys;
    for (const QVariant& entry : entries) {
        keys.insert(entry.toMap().value(QStringLiteral("key")).toString());
    }
    CHECK(keys.size() == entries.size());
    CHECK_FALSE(keys.contains(QString()));

    const QVariantMap builtin = entryOf(host, QStringLiteral("gamescrcpy.glow"));
    CHECK(builtin.value(QStringLiteral("builtin")).toBool());
    CHECK(builtin.value(QStringLiteral("usable")).toBool());
    CHECK_FALSE(builtin.value(QStringLiteral("enabled")).toBool());
    CHECK_FALSE(builtin.value(QStringLiteral("active")).toBool());

    const QVariantMap glow = entryOf(host, QStringLiteral("alice.glow"));
    CHECK(glow.value(QStringLiteral("version")).toString() == "2.0.0");
    CHECK(glow.value(QStringLiteral("name")).toString() == "Glow");
    CHECK(glow.value(QStringLiteral("description")).toString() == "Soft light");
    CHECK(glow.value(QStringLiteral("authors")).toStringList() == QStringList{QStringLiteral("Alice")});
    CHECK(glow.value(QStringLiteral("license")).toString() == "MIT");
    CHECK_FALSE(glow.value(QStringLiteral("builtin")).toBool());
    CHECK(glow.value(QStringLiteral("usable")).toBool());
    CHECK_FALSE(glow.value(QStringLiteral("enabled")).toBool());
    CHECK_FALSE(glow.value(QStringLiteral("active")).toBool());
    CHECK(glow.value(QStringLiteral("points")).toStringList().contains(QStringLiteral("filter")));
    CHECK(glow.value(QStringLiteral("hasSettings")).toBool());
    CHECK(glow.value(QStringLiteral("permissions")).toList().isEmpty());

    REQUIRE(host.enable(QStringLiteral("alice.glow"), true));
    CHECK(entryOf(host, QStringLiteral("alice.glow")).value(QStringLiteral("enabled")).toBool());
    CHECK(entryOf(host, QStringLiteral("alice.glow")).value(QStringLiteral("active")).toBool());
    CHECK_FALSE(entryOf(host, QStringLiteral("bob.aura")).value(QStringLiteral("enabled")).toBool());
    CHECK(host.filters("").size() == 1);

    REQUIRE(host.enable(QStringLiteral("alice.glow"), false));
    CHECK_FALSE(entryOf(host, QStringLiteral("alice.glow")).value(QStringLiteral("enabled")).toBool());
    CHECK_FALSE(entryOf(host, QStringLiteral("alice.glow")).value(QStringLiteral("active")).toBool());
    CHECK(host.filters("").empty());
    CHECK_FALSE(host.enable(QStringLiteral("nobody.here"), true));
}

TEST_CASE("an unusable user extension folder is listed with a reason and a hint, and can be removed")
{
    const Fixture fixture;
    const fs::path folder = fixture.user / "alice.broken/1.0.0";
    QString code;
    SUBCASE("the manifest is missing")
    {
        writeFile(folder / "shaders/glow.hlsl", kShader);
        code = QStringLiteral("manifest-missing");
    }
    SUBCASE("the manifest is not valid JSON")
    {
        writeExtension(folder, R"({"schemaVersion": 1, "id": )");
        code = QStringLiteral("malformed-json");
    }
    SUBCASE("the manifest names a filter file that is missing")
    {
        writeFile(folder / "manifest.json", manifest("alice.broken", "1.0.0", "Broken", ""));
        code = QStringLiteral("file-missing");
    }
    writeFile(fixture.data / "settings.json",
              R"({"format": "gamescrcpy.settings", "version": 1, "extensions": {"alice.broken": {"enabled": true}}})");
    writeFile(fixture.user / ".consent/alice.broken.json", "{}");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);

    const QVariantList entries = host.entries();
    REQUIRE(entries.size() == 1);
    const QVariantMap entry = entries.first().toMap();
    CHECK(entry.value(QStringLiteral("id")).toString() == "alice.broken");
    CHECK(entry.value(QStringLiteral("version")).toString() == "1.0.0");
    CHECK_FALSE(entry.value(QStringLiteral("usable")).toBool());
    CHECK_FALSE(entry.value(QStringLiteral("builtin")).toBool());
    CHECK_FALSE(entry.value(QStringLiteral("active")).toBool());
    const QVariantMap problem = itemWith(entry.value(QStringLiteral("problems")).toList(), QStringLiteral("code"), code);
    CHECK(problem.value(QStringLiteral("severity")).toString() == "error");
    CHECK_FALSE(problem.value(QStringLiteral("hint")).toString().isEmpty());
    CHECK_FALSE(host.enable(QStringLiteral("alice.broken"), true));
    CHECK(entry.value(QStringLiteral("folder")).toString() == "alice.broken/1.0.0");

    CHECK_FALSE(host.remove(QStringLiteral("alice.broken"), QStringLiteral("1.0.0")));
    CHECK(fs::exists(folder));
    CHECK(host.removeFolder(entry.value(QStringLiteral("folder")).toString()));
    CHECK_FALSE(fs::exists(fixture.user / "alice.broken"));
    CHECK(host.entries().isEmpty());
    CHECK_FALSE(settings.extensions().contains("alice.broken"));
    CHECK_FALSE(fs::exists(fixture.user / ".consent/alice.broken.json"));
}

TEST_CASE("a package is inspected from a path or a file URL before installing and then reports the installed version")
{
    const Fixture fixture;
    const fs::path archive =
        fixture.pack("alice.glow", "1.0.0",
                     R"(, "description": "Soft light", "authors": ["Alice", "Bob"],
                        "dependencies": {"bob.base": "^1.0", "carol.extra": {"version": "^2.0", "optional": true}})");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);

    const QVariantMap before = host.inspect(textOf(archive));
    CHECK(before.value(QStringLiteral("ok")).toBool());
    CHECK(before.value(QStringLiteral("id")).toString() == "alice.glow");
    CHECK(before.value(QStringLiteral("version")).toString() == "1.0.0");
    CHECK(before.value(QStringLiteral("name")).toString() == "Glow");
    CHECK(before.value(QStringLiteral("description")).toString() == "Soft light");
    const QStringList authors{QStringLiteral("Alice"), QStringLiteral("Bob")};
    CHECK(before.value(QStringLiteral("authors")).toStringList() == authors);
    CHECK(before.value(QStringLiteral("permissions")).toList().isEmpty());
    CHECK(before.value(QStringLiteral("installedVersion")).toString().isEmpty());
    const QVariantList dependencies = before.value(QStringLiteral("dependencies")).toList();
    CHECK(dependencies.size() == 2);
    const QVariantMap base = itemWith(dependencies, QStringLiteral("id"), QStringLiteral("bob.base"));
    CHECK(base.value(QStringLiteral("range")).toString() == "^1.0");
    CHECK_FALSE(base.value(QStringLiteral("optional")).toBool());
    const QVariantMap extra = itemWith(dependencies, QStringLiteral("id"), QStringLiteral("carol.extra"));
    CHECK(extra.value(QStringLiteral("range")).toString() == "^2.0");
    CHECK(extra.value(QStringLiteral("optional")).toBool());

    const QVariantMap fromUrl = host.inspect(urlOf(archive));
    CHECK(fromUrl.value(QStringLiteral("ok")).toBool());
    CHECK(fromUrl.value(QStringLiteral("id")).toString() == "alice.glow");
    CHECK(fromUrl.value(QStringLiteral("version")).toString() == "1.0.0");

    const gs::ext::InstalledExtension installed = host.install(archive);
    INFO(listed(installed.diagnostics));
    REQUIRE(installed.ok());
    CHECK(host.inspect(textOf(archive)).value(QStringLiteral("installedVersion")).toString() == "1.0.0");

    const QVariantMap missing = host.inspect(textOf(fixture.scratch / "missing.gsext"));
    CHECK_FALSE(missing.value(QStringLiteral("ok")).toBool());
    CHECK_FALSE(missing.value(QStringLiteral("problems")).toList().isEmpty());
}

TEST_CASE("removing deletes a user extension from disk and from the list but refuses built-in ones and unknown versions")
{
    const Fixture fixture;
    fixture.add(fixture.builtin, "gamescrcpy.glow", "1.0.0");
    fixture.add(fixture.user, "alice.glow", "2.0.0");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, fixture.builtin});
    host.start(false);
    REQUIRE(host.enable(QStringLiteral("alice.glow"), true));

    CHECK_FALSE(host.remove(QStringLiteral("alice.glow"), QStringLiteral("two")));
    CHECK_FALSE(host.remove(QStringLiteral("alice.glow"), QStringLiteral("1.0.0")));
    CHECK_FALSE(host.remove(QStringLiteral("nobody.here"), QStringLiteral("1.0.0")));
    CHECK_FALSE(host.remove(QStringLiteral("gamescrcpy.glow"), QStringLiteral("1.0.0")));
    CHECK(fs::exists(fixture.builtin / "gamescrcpy.glow/1.0.0/manifest.json"));
    CHECK(fs::exists(fixture.user / "alice.glow/2.0.0/manifest.json"));
    CHECK(idsOf(host).size() == 2);

    CHECK(host.remove(QStringLiteral("alice.glow"), QStringLiteral("2.0.0")));
    CHECK_FALSE(fs::exists(fixture.user / "alice.glow"));
    CHECK(entryOf(host, QStringLiteral("alice.glow")).isEmpty());
    CHECK(idsOf(host) == QStringList{QStringLiteral("gamescrcpy.glow")});
    CHECK_FALSE(settings.extensions().contains("alice.glow"));
    CHECK(host.filters("").empty());
}

TEST_CASE("an installed extension is exported to a path or a file URL as a package that passes the package check")
{
    const Fixture fixture;
    fixture.add(fixture.user, "alice.glow", "2.0.0");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);

    const fs::path exported = fixture.scratch / "alice.glow-2.0.0.gsext";
    REQUIRE(host.exportTo(QStringLiteral("alice.glow"), QStringLiteral("2.0.0"), textOf(exported)));
    const gs::ext::PackageCheck check = gs::ext::checkPackage(exported);
    INFO(listed(check.diagnostics));
    REQUIRE(check.ok());
    CHECK(check.manifest->id == "alice.glow");
    CHECK(check.manifest->version.text() == "2.0.0");
    CHECK(gs::ext::readArchiveFile(exported, "shaders/glow.hlsl", std::uint64_t{1} << 20) == std::string(kShader));

    const fs::path copy = fixture.scratch / "copy.gsext";
    CHECK(host.exportTo(QStringLiteral("alice.glow"), QStringLiteral("2.0.0"), urlOf(copy)));
    CHECK(gs::ext::checkPackage(copy).ok());

    const fs::path none = fixture.scratch / "none.gsext";
    CHECK_FALSE(host.exportTo(QStringLiteral("alice.glow"), QStringLiteral("9.0.0"), textOf(none)));
    CHECK_FALSE(host.exportTo(QStringLiteral("nobody.here"), QStringLiteral("1.0.0"), textOf(none)));
    CHECK_FALSE(fs::exists(none));
}

TEST_CASE("only files on this computer are opened: file URLs with a host, UNC paths and other schemes are refused")
{
    const Fixture fixture;
    fixture.add(fixture.user, "alice.glow", "2.0.0");
    const fs::path archive = fixture.pack("alice.glow", "1.0.0", {});
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);

    CHECK(host.isLocalFile(textOf(archive)));
    CHECK(host.isLocalFile(urlOf(archive)));
    CHECK(host.inspect(textOf(archive)).value(QStringLiteral("ok")).toBool());
    CHECK(host.inspect(urlOf(archive)).value(QStringLiteral("ok")).toBool());
    CHECK(host.exportTo(QStringLiteral("alice.glow"), QStringLiteral("2.0.0"), urlOf(fixture.scratch / "copy.gsext")));
    CHECK(fs::exists(fixture.scratch / "copy.gsext"));

    const QStringList remote{QStringLiteral("file://gs-test.invalid/share/alice.glow-1.0.0.gsext"),
                             QStringLiteral("file:////gs-test.invalid/share/alice.glow-1.0.0.gsext"),
                             QStringLiteral("\\\\gs-test.invalid\\share\\alice.glow-1.0.0.gsext"),
                             QStringLiteral("//gs-test.invalid/share/alice.glow-1.0.0.gsext"),
                             QStringLiteral("http://gs-test.invalid/alice.glow-1.0.0.gsext")};
    for (const QString& where : remote) {
        INFO(where.toStdString());
        CHECK_FALSE(host.isLocalFile(where));
        const QVariantMap info = host.inspect(where);
        CHECK_FALSE(info.value(QStringLiteral("ok")).toBool());
        CHECK(info.value(QStringLiteral("id")).toString().isEmpty());
        const QVariantList problems = info.value(QStringLiteral("problems")).toList();
        REQUIRE(problems.size() == 1);
        const QVariantMap problem = problems.first().toMap();
        CHECK(problem.value(QStringLiteral("code")).toString() == "not-local-file");
        CHECK(problem.value(QStringLiteral("severity")).toString() == "error");
        CHECK(problem.value(QStringLiteral("detail")).toString() == where);
        CHECK_FALSE(problem.value(QStringLiteral("hint")).toString().isEmpty());

        const QVariantMap bundle = host.inspectBundle(where);
        CHECK_FALSE(bundle.value(QStringLiteral("ok")).toBool());
        CHECK(itemWith(bundle.value(QStringLiteral("problems")).toList(), QStringLiteral("code"),
                       QStringLiteral("not-local-file"))
                  .value(QStringLiteral("severity"))
                  .toString() == "error");
        CHECK_FALSE(host.exportTo(QStringLiteral("alice.glow"), QStringLiteral("2.0.0"), where));
        CHECK_FALSE(host.exportBundle({QStringLiteral("alice.glow")}, QStringLiteral("Glow"), where));
    }
}

TEST_CASE("a user copy of a built-in version is listed as not used and can be removed while the built-in one stays")
{
    const Fixture fixture;
    fixture.add(fixture.builtin, "alice.glow", "1.0.0");
    fixture.add(fixture.user, "alice.glow", "1.0.0");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, fixture.builtin});
    host.start(false);
    REQUIRE(host.enable(QStringLiteral("alice.glow"), true));

    const QVariantList entries = host.entries();
    REQUIRE(entries.size() == 2);
    const QVariantMap builtin = entries.at(0).toMap();
    CHECK(builtin.value(QStringLiteral("builtin")).toBool());
    CHECK(builtin.value(QStringLiteral("usable")).toBool());
    CHECK(builtin.value(QStringLiteral("active")).toBool());
    CHECK_FALSE(builtin.value(QStringLiteral("superseded")).toBool());
    CHECK(builtin.value(QStringLiteral("folder")).toString().isEmpty());
    const QVariantMap copy = entries.at(1).toMap();
    CHECK(copy.value(QStringLiteral("id")).toString() == "alice.glow");
    CHECK(copy.value(QStringLiteral("version")).toString() == "1.0.0");
    CHECK_FALSE(copy.value(QStringLiteral("builtin")).toBool());
    CHECK_FALSE(copy.value(QStringLiteral("usable")).toBool());
    CHECK_FALSE(copy.value(QStringLiteral("enabled")).toBool());
    CHECK_FALSE(copy.value(QStringLiteral("active")).toBool());
    CHECK(copy.value(QStringLiteral("superseded")).toBool());
    CHECK(copy.value(QStringLiteral("folder")).toString() == "alice.glow/1.0.0");
    CHECK(copy.value(QStringLiteral("key")).toString() != builtin.value(QStringLiteral("key")).toString());
    const QVariantMap problem = itemWith(copy.value(QStringLiteral("problems")).toList(), QStringLiteral("code"),
                                         QStringLiteral("superseded-by-builtin"));
    CHECK(problem.value(QStringLiteral("severity")).toString() == "warning");
    CHECK_FALSE(problem.value(QStringLiteral("hint")).toString().isEmpty());
    CHECK(host.filters("").size() == 1);

    CHECK_FALSE(host.remove(QStringLiteral("alice.glow"), QStringLiteral("1.0.0")));
    CHECK(host.removeFolder(copy.value(QStringLiteral("folder")).toString()));
    CHECK_FALSE(fs::exists(fixture.user / "alice.glow"));
    CHECK(fs::exists(fixture.builtin / "alice.glow/1.0.0/manifest.json"));
    REQUIRE(host.entries().size() == 1);
    CHECK(host.entries().first().toMap().value(QStringLiteral("builtin")).toBool());
    CHECK(settings.extensions().at("alice.glow").enabled);
    CHECK(host.filters("").size() == 1);
}

TEST_CASE("an unusable folder is removed by its folder, exactly that one, and folders that leave the user root are refused")
{
    const Fixture fixture;
    writeFile(fixture.user / "alice.glow/1.0.0/manifest.json", manifest("alice.glow", "1.0.0", "Glow", ""));
    writeExtension(fixture.user / "alice.glow/9.9.9", manifest("alice.glow", "1.0.0", "Glow", ""));
    writeExtension(fixture.scratch / "outside/1.0.0", manifest("bob.glow", "1.0.0", "Glow", ""));
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);

    const QVariantList entries = host.entries();
    REQUIRE(entries.size() == 2);
    QStringList folders;
    for (const QVariant& entry : entries) {
        const QVariantMap row = entry.toMap();
        CHECK_FALSE(row.value(QStringLiteral("usable")).toBool());
        CHECK(row.value(QStringLiteral("id")).toString() == "alice.glow");
        CHECK(row.value(QStringLiteral("version")).toString() == "1.0.0");
        folders.append(row.value(QStringLiteral("folder")).toString());
    }
    folders.sort();
    CHECK(folders == QStringList{QStringLiteral("alice.glow/1.0.0"), QStringLiteral("alice.glow/9.9.9")});

    const QStringList refused{QString(),
                              QStringLiteral("alice.glow"),
                              QStringLiteral("alice.glow/9.9.9/shaders"),
                              QStringLiteral("alice.glow/.."),
                              QStringLiteral("../outside"),
                              QStringLiteral("..\\outside"),
                              QStringLiteral("alice.glow/../../outside/1.0.0"),
                              QStringLiteral("/alice.glow/9.9.9"),
                              QStringLiteral("C:alice.glow/9.9.9"),
                              textOf(fixture.user / "alice.glow/9.9.9"),
                              textOf(fixture.scratch / "outside/1.0.0")};
    for (const QString& folder : refused) {
        INFO(folder.toStdString());
        CHECK_FALSE(host.removeFolder(folder));
    }
    CHECK_FALSE(host.remove(QStringLiteral("alice.glow"), QStringLiteral("1.0.0")));
    CHECK(fs::exists(fixture.user / "alice.glow/1.0.0/manifest.json"));
    CHECK(fs::exists(fixture.user / "alice.glow/9.9.9/manifest.json"));
    CHECK(fs::exists(fixture.scratch / "outside/1.0.0/manifest.json"));
    CHECK(host.entries().size() == 2);

    CHECK(host.removeFolder(QStringLiteral("alice.glow/9.9.9")));
    CHECK_FALSE(fs::exists(fixture.user / "alice.glow/9.9.9"));
    CHECK(fs::exists(fixture.user / "alice.glow/1.0.0/manifest.json"));
    REQUIRE(host.entries().size() == 1);
    CHECK(host.entries().first().toMap().value(QStringLiteral("folder")).toString() == "alice.glow/1.0.0");
    CHECK_FALSE(host.removeFolder(QStringLiteral("alice.glow/9.9.9")));

    CHECK(host.removeFolder(QStringLiteral("alice.glow\\1.0.0")));
    CHECK_FALSE(fs::exists(fixture.user / "alice.glow"));
    CHECK(host.entries().isEmpty());
    CHECK(fs::exists(fixture.scratch / "outside/1.0.0/manifest.json"));
}

TEST_CASE("the mode name says whether extensions run normally, safe mode was asked for, or the last run ended with them loaded")
{
    const Fixture fixture;
    fixture.add(fixture.user, "alice.glow", "1.0.0");
    const fs::path marker = fixture.data / "extensions.running";
    AppSettings settings(fixture.data);
    {
        ExtensionHost host(settings, {fixture.user, {}});
        host.start(false, marker);
        CHECK(host.modeName() == "normal");
        CHECK(host.property("mode").toString() == "normal");
        host.start(true, marker);
        CHECK(host.modeName() == "safeRequested");
        CHECK(host.property("mode").toString() == "safeRequested");
        host.start(false, marker);
        REQUIRE(host.enable(QStringLiteral("alice.glow"), true));
        CHECK(fs::exists(marker));
    }
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false, marker);
    CHECK(host.modeName() == "safeAfterCrash");
    CHECK(host.property("mode").toString() == "safeAfterCrash");
}

TEST_CASE("ADR-0112: the extension list says which panels have the wrong kind of script and draws their own icons")
{
    const Fixture fixture;
    const fs::path folder = fixture.user / "alice.places/1.0.0";
    writeFile(folder / "manifest.json", R"({"schemaVersion": 1, "id": "alice.places", "version": "1.0.0",
        "name": "Places", "license": "MIT", "requires": {"panel": "^1.0", "script": "^1.0"},
        "contributes": {
            "scripts": [{"name": "app", "entry": "app.js", "run": "app"},
                        {"name": "session", "entry": "session.js", "run": "session"}],
            "panels": [
                {"name": "options", "title": "Options", "icon": "svg:icons/board.svg", "where": "settings",
                 "script": "app"},
                {"name": "late", "title": "Late", "where": "extension", "script": "session"},
                {"name": "quick", "title": "Quick", "icon": "zap", "where": "toolbar", "script": "app"},
                {"name": "more", "title": "More", "where": "contextMenu", "script": "app", "on": ["device"]}]}})");
    writeFile(folder / "app.js", "export {};\n");
    writeFile(folder / "session.js", "export {};\n");
    writeFile(folder / "icons/board.svg", R"(<svg viewBox="0 0 24 24"><path d="M2 2 L22 22"/></svg>)");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);

    const QVariantMap entry = entryOf(host, QStringLiteral("alice.places"));
    INFO(entry.value(QStringLiteral("problems")).toList().size());
    REQUIRE(entry.value(QStringLiteral("usable")).toBool());
    const QVariantList panels = entry.value(QStringLiteral("panels")).toList();
    REQUIRE(panels.size() == 4);
    const QVariantMap options = panels[0].toMap();
    CHECK(options.value(QStringLiteral("where")).toString() == "settings");
    CHECK(options.value(QStringLiteral("problem")).toString().isEmpty());
    CHECK(options.value(QStringLiteral("iconPath")).toString() == "M2 2 L22 22");
    CHECK(panels[1].toMap().value(QStringLiteral("problem")).toString() ==
          "This place needs a script that runs once for the app, and session runs for each phone");
    CHECK(panels[2].toMap().value(QStringLiteral("problem")).toString() ==
          "This place needs a script that runs for each phone, and app runs once for the app");
    CHECK_FALSE(panels[2].toMap().contains(QStringLiteral("iconPath")));
    CHECK(panels[3].toMap().value(QStringLiteral("where")).toString() == "contextMenu");
    CHECK(panels[3].toMap().value(QStringLiteral("problem")).toString().isEmpty());
}
