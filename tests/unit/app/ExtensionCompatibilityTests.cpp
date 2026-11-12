#include "AppSettings.h"
#include "ExtensionHost.h"

#include "unit/ext/TestFiles.h"

#include <gs/ext/Archive.h>

#include <doctest/doctest.h>

#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <filesystem>
#include <string>
#include <string_view>

using gs::app::AppSettings;
using gs::app::ExtensionHost;
using namespace gs::app::settings;
using namespace gs::ext::test;
namespace fs = std::filesystem;

namespace {

constexpr std::string_view kShader = R"(//! PARAM strength float 0.5 0 1 0.05
//! PASS glow
float4 glow(float2 uv)
{
    return Source.Sample(Linear, uv) * strength;
}
)";

std::string manifest(std::string_view id, std::string_view version, std::string_view wanted,
                     std::string_view more = {})
{
    return R"({"schemaVersion": 1, "id": ")" + std::string(id) + R"(", "version": ")" + std::string(version) +
           R"(", "name": "Glow", "license": "MIT", "requires": {)" + std::string(wanted) +
           R"(}, "contributes": {"filters": [{"name": "Glow", "file": "shaders/glow.hlsl"}]})" + std::string(more) +
           "}";
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

QVariantMap rowOf(const QVariantList& rows, const QString& id)
{
    for (const QVariant& row : rows) {
        QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("id")).toString() == id) {
            return map;
        }
    }
    return {};
}

struct Fixture {
    Scratch scratch;
    fs::path data = scratch / "data";
    fs::path user = scratch / "user";
    fs::path builtin = scratch / "builtin";

    void add(const fs::path& root, std::string_view id, std::string_view version, std::string_view wanted,
             std::string_view more = {}) const
    {
        writeExtension(root / pathOf(id) / pathOf(version), manifest(id, version, wanted, more));
    }

    [[nodiscard]] fs::path pack(std::string_view id, std::string_view version, std::string_view wanted) const
    {
        const std::string stem = std::string(id) + "-" + std::string(version);
        const fs::path folder = scratch / ("package-" + stem);
        writeExtension(folder, manifest(id, version, wanted));
        const fs::path archive = scratch / (stem + ".gsext");
        REQUIRE(gs::ext::packDirectory(folder, archive).empty());
        return archive;
    }
};

}

TEST_CASE("ADR-0113: the notice is due when this GameScrcpy differs from the version that last finished starting")
{
    using gs::app::reviewExtensionsAfterStart;
    CHECK_FALSE(reviewExtensionsAfterStart(QStringLiteral("2.1.0"), QStringLiteral("2.1.0"), true));
    CHECK_FALSE(reviewExtensionsAfterStart(QStringLiteral("2.1.0"), QStringLiteral("2.1.0"), false));
    CHECK(reviewExtensionsAfterStart(QStringLiteral("2.0.0"), QStringLiteral("2.1.0"), true));
    CHECK(reviewExtensionsAfterStart(QStringLiteral("2.0.0"), QStringLiteral("2.1.0"), false));
    CHECK(reviewExtensionsAfterStart(QStringLiteral("2.2.0"), QStringLiteral("2.1.0"), true));
    CHECK(reviewExtensionsAfterStart(QString(), QStringLiteral("2.1.0"), true));
    CHECK_FALSE(reviewExtensionsAfterStart(QString(), QStringLiteral("2.1.0"), false));
    CHECK_FALSE(reviewExtensionsAfterStart(QStringLiteral("2.1.0"), QString(), true));
}

TEST_CASE("ADR-0113: only the extensions the user turned on and their version holds back are listed, with the reason")
{
    const Fixture fixture;
    fixture.add(fixture.user, "alice.older", "1.0.0", R"("filter": "^0.9")",
                R"(, "updates": {"index": "https://example.com/glow/index.json"})");
    fixture.add(fixture.user, "bob.newer", "2.0.0", R"("filter": "^3.0")");
    fixture.add(fixture.user, "carol.fine", "1.0.0", R"("filter": "^1.0")");
    fixture.add(fixture.user, "dave.off", "1.0.0", R"("filter": "^3.0")");

    AppSettings settings(fixture.data);
    ExtensionStates states;
    states["alice.older"].enabled = true;
    states["bob.newer"].enabled = true;
    states["carol.fine"].enabled = true;
    settings.setExtensions(states);

    ExtensionHost host(settings, {fixture.user, fixture.builtin});
    host.start(false);
    const QVariantList blocked = host.versionBlockedExtensions();
    CHECK(blocked.size() == 2);

    const QVariantMap older = rowOf(blocked, QStringLiteral("alice.older"));
    CHECK(older.value(QStringLiteral("reason")).toString() == "host-api-outdated");
    CHECK(older.value(QStringLiteral("name")).toString() == "Glow");
    CHECK(older.value(QStringLiteral("version")).toString() == "1.0.0");
    CHECK(older.value(QStringLiteral("updates")).toBool());

    const QVariantMap newer = rowOf(blocked, QStringLiteral("bob.newer"));
    CHECK(newer.value(QStringLiteral("reason")).toString() == "host-api-version");
    CHECK(newer.value(QStringLiteral("version")).toString() == "2.0.0");
    CHECK_FALSE(newer.value(QStringLiteral("updates")).toBool());

    CHECK(rowOf(blocked, QStringLiteral("carol.fine")).isEmpty());
    CHECK(rowOf(blocked, QStringLiteral("dave.off")).isEmpty());
    CHECK(host.property("versionBlocked").toList().size() == 2);
}

TEST_CASE("ADR-0113: the version that last finished starting starts empty, stays out of the settings page and is kept")
{
    const Scratch scratch;
    const fs::path data = scratch / "data";
    {
        AppSettings settings(data);
        CHECK(settings.lastVersion().isEmpty());
        CHECK_FALSE(settings.settingsFileExisted());
        CHECK(gs::app::settings::findSetting("general.lastVersion") == nullptr);
        CHECK_FALSE(settings.setValue(QStringLiteral("general.lastVersion"), QStringLiteral("2.1.0")));
        CHECK_FALSE(settings.values().contains(QStringLiteral("general.lastVersion")));

        settings.setLastVersion(QStringLiteral("2.1.0"));
        CHECK(settings.lastVersion() == QStringLiteral("2.1.0"));
        settings.setLastVersion(QString(static_cast<int>(General::kMaxVersionLength) + 1, QLatin1Char('x')));
        CHECK(settings.lastVersion() == QStringLiteral("2.1.0"));
    }
    CHECK(readFile(data / "settings.json").find("\"lastVersion\": \"2.1.0\"") != std::string::npos);
    {
        AppSettings settings(data);
        CHECK(settings.settingsFileExisted());
        CHECK(settings.lastVersion() == QStringLiteral("2.1.0"));
    }
}

TEST_CASE("ADR-0113: a package that wants an interface newer than this GameScrcpy has cannot be installed")
{
    const Fixture fixture;
    const fs::path newest = fixture.pack("alice.glow", "1.0.0", R"("filter": "^3.0")");
    const fs::path fits = fixture.pack("bob.glow", "1.0.0", R"("filter": "^1.0")");

    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);

    const QVariantMap refused = host.inspect(textOf(newest));
    CHECK_FALSE(refused.value(QStringLiteral("ok")).toBool());
    bool reported = false;
    for (const QVariant& problem : refused.value(QStringLiteral("problems")).toList()) {
        reported = reported || problem.toMap().value(QStringLiteral("code")).toString() == "host-api-version";
    }
    CHECK(reported);
    CHECK(host.inspect(textOf(fits)).value(QStringLiteral("ok")).toBool());
}
