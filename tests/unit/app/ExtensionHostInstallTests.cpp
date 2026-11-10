#include "AppSettings.h"
#include "ExtensionHost.h"

#include "unit/ext/TestFiles.h"

#include <gs/ext/Archive.h>

#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

using gs::app::AppSettings;
using gs::app::ExtensionHost;
using namespace gs::ext::test;
namespace fs = std::filesystem;

namespace {

constexpr auto kInstallTimeout = std::chrono::seconds(10);

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

std::string manifest(std::string_view id, std::string_view version, std::string_view name,
                     std::string_view more = {})
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

std::string codesOf(const QVariantMap& result)
{
    std::string codes;
    const QVariantList problems = result.value(QStringLiteral("problems")).toList();
    for (const QVariant& problem : problems) {
        codes += problem.toMap().value(QStringLiteral("code")).toString().toStdString() + " ";
    }
    return codes;
}

class ApplicationScope
{
public:
    ApplicationScope()
    {
        if (QCoreApplication::instance() == nullptr) {
            m_application = std::make_unique<QCoreApplication>(m_argc, m_argv.data());
        }
    }

    ApplicationScope(const ApplicationScope&) = delete;
    ApplicationScope& operator=(const ApplicationScope&) = delete;

private:
    int m_argc = 1;
    std::string m_name = "gs_app_tests";
    std::array<char*, 2> m_argv{m_name.data(), nullptr};
    std::unique_ptr<QCoreApplication> m_application;
};

class InstallWatch
{
public:
    explicit InstallWatch(ExtensionHost& host)
    {
        QObject::connect(&host, &ExtensionHost::installFinished, &m_context,
                         [this](const QVariantMap& result) { m_results.append(result); });
        QObject::connect(&host, &ExtensionHost::bundleFinished, &m_context,
                         [this](const QVariantMap& result) { m_results.append(result); });
        QObject::connect(&host, &ExtensionHost::installingChanged, &m_context, [this] { ++m_changes; });
    }

    InstallWatch(const InstallWatch&) = delete;
    InstallWatch& operator=(const InstallWatch&) = delete;

    bool waitFor(qsizetype count)
    {
        const auto deadline = std::chrono::steady_clock::now() + kInstallTimeout;
        while (m_results.size() < count && std::chrono::steady_clock::now() < deadline) {
            QCoreApplication::processEvents();
            std::this_thread::yield();
        }
        return m_results.size() >= count;
    }

    [[nodiscard]] const QList<QVariantMap>& results() const noexcept { return m_results; }
    [[nodiscard]] int changes() const noexcept { return m_changes; }

private:
    QList<QVariantMap> m_results;
    int m_changes = 0;
    QObject m_context;
};

struct Fixture {
    Scratch scratch;
    fs::path data = scratch / "data";
    fs::path user = scratch / "user";
    fs::path builtin = scratch / "builtin";

    void add(const fs::path& root, std::string_view id, std::string_view version, std::string_view name) const
    {
        writeExtension(root / pathOf(id) / pathOf(version), manifest(id, version, name));
    }

    [[nodiscard]] fs::path pack(std::string_view id, std::string_view version, std::string_view more = {}) const
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

TEST_CASE("a bundle of the switched-on extensions brings back their versions, switches and values elsewhere")
{
    ApplicationScope application;
    const Fixture source;
    source.add(source.user, "alice.glow", "1.0.0", "Glow");
    source.add(source.user, "carol.idle", "1.0.0", "Idle");
    source.add(source.builtin, "gamescrcpy.base", "1.0.0", "Base");
    const fs::path bundle = source.scratch / "night.gsbundle";
    {
        AppSettings settings(source.data);
        ExtensionHost host(settings, {source.user, source.builtin});
        host.start(false);
        REQUIRE(host.enable(QStringLiteral("alice.glow"), true));
        REQUIRE(host.enable(QStringLiteral("gamescrcpy.base"), true));
        REQUIRE(host.setValue(gs::app::extensionSettingPath("alice.glow", "strength"), QVariant(0.2)));
        CHECK_FALSE(host.exportBundle({}, QStringLiteral("Empty"), textOf(source.scratch / "empty.gsbundle")));
        REQUIRE(host.exportBundle({QStringLiteral("alice.glow"), QStringLiteral("gamescrcpy.base")},
                                  QStringLiteral("Night"), textOf(bundle)));
    }

    const Fixture target;
    target.add(target.builtin, "gamescrcpy.base", "1.0.0", "Base");
    AppSettings settings(target.data);
    ExtensionHost host(settings, {target.user, target.builtin});
    host.start(false);
    const QVariantMap info = host.inspectBundle(urlOf(bundle));
    INFO(codesOf(info));
    CHECK(info.value(QStringLiteral("ok")).toBool());
    CHECK(info.value(QStringLiteral("name")).toString() == "Night");
    const QVariantList rows = info.value(QStringLiteral("extensions")).toList();
    REQUIRE(rows.size() == 2);
    CHECK(rows.at(0).toMap().value(QStringLiteral("id")).toString() == "alice.glow");
    CHECK(rows.at(0).toMap().value(QStringLiteral("status")).toString() == "new");
    CHECK(rows.at(1).toMap().value(QStringLiteral("status")).toString() == "builtin");

    InstallWatch watch(host);
    host.installBundle(textOf(bundle));
    CHECK(host.installing());
    REQUIRE(watch.waitFor(1));
    const QVariantMap done = watch.results().first();
    INFO(codesOf(done));
    CHECK(done.value(QStringLiteral("ok")).toBool());
    CHECK(done.value(QStringLiteral("installed")).toStringList().size() == 2);
    CHECK_FALSE(host.installing());
    CHECK(fs::exists(target.user / "alice.glow/1.0.0/manifest.json"));
    CHECK_FALSE(fs::exists(target.user / "carol.idle"));
    CHECK(settings.extensions().at("alice.glow").enabled);
    CHECK(settings.extensions().at("gamescrcpy.base").enabled);
    CHECK(fs::exists(target.user / ".consent/alice.glow.json"));
    CHECK(host.values().value(gs::app::extensionSettingPath("alice.glow", "strength")).toDouble() ==
          doctest::Approx(0.2));
    CHECK(idsOf(host).contains(QStringLiteral("alice.glow")));
}

TEST_CASE("a bundle that needs a built-in extension this installation lacks cannot be installed")
{
    ApplicationScope application;
    const Fixture source;
    source.add(source.builtin, "gamescrcpy.base", "1.0.0", "Base");
    const fs::path bundle = source.scratch / "base.gsbundle";
    {
        AppSettings settings(source.data);
        ExtensionHost host(settings, {source.user, source.builtin});
        host.start(false);
        REQUIRE(host.enable(QStringLiteral("gamescrcpy.base"), true));
        REQUIRE(host.exportBundle({QStringLiteral("gamescrcpy.base")}, QStringLiteral("Base"), textOf(bundle)));
    }

    const Fixture target;
    AppSettings settings(target.data);
    ExtensionHost host(settings, {target.user, {}});
    host.start(false);
    const QVariantMap info = host.inspectBundle(textOf(bundle));
    CHECK_FALSE(info.value(QStringLiteral("ok")).toBool());
    CHECK(codesOf(info).find("bundle-builtin-missing") != std::string::npos);

    InstallWatch watch(host);
    host.installBundle(textOf(bundle));
    REQUIRE(watch.waitFor(1));
    const QVariantMap done = watch.results().first();
    CHECK_FALSE(done.value(QStringLiteral("ok")).toBool());
    CHECK(codesOf(done).find("bundle-builtin-missing") != std::string::npos);
    CHECK_FALSE(settings.extensions().contains("gamescrcpy.base"));
}

TEST_CASE("an installed update that needs more than was allowed says that it waits for consent")
{
    ApplicationScope application;
    const Fixture fixture;
    fixture.add(fixture.user, "alice.glow", "1.0.0", "Glow");
    fixture.add(fixture.user, "bob.base", "1.0.0", "Base");
    const fs::path update = fixture.pack("alice.glow", "2.0.0", R"(, "dependencies": {"bob.base": "^1.0"})");
    const fs::path same = fixture.pack("bob.base", "1.1.0");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    REQUIRE(host.enable(QStringLiteral("alice.glow"), true));
    REQUIRE(host.enable(QStringLiteral("bob.base"), true));
    InstallWatch watch(host);

    host.installPackage(textOf(update));
    REQUIRE(watch.waitFor(1));
    const QVariantMap waiting = watch.results().at(0);
    INFO(codesOf(waiting));
    CHECK(waiting.value(QStringLiteral("ok")).toBool());
    CHECK(waiting.value(QStringLiteral("consentRequired")).toBool());

    host.installPackage(textOf(same));
    REQUIRE(watch.waitFor(2));
    const QVariantMap plain = watch.results().at(1);
    CHECK(plain.value(QStringLiteral("ok")).toBool());
    CHECK_FALSE(plain.value(QStringLiteral("consentRequired")).toBool());
}

TEST_CASE("installing a package from a path runs in the background and reports the installed extension when it finishes")
{
    ApplicationScope application;
    const Fixture fixture;
    const fs::path archive = fixture.pack("alice.glow", "1.0.0");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    InstallWatch watch(host);

    host.installPackage(textOf(archive));
    CHECK(host.installing());
    CHECK(host.property("installing").toBool());
    CHECK(watch.changes() == 1);
    CHECK(watch.results().isEmpty());

    REQUIRE(watch.waitFor(1));
    REQUIRE(watch.results().size() == 1);
    const QVariantMap result = watch.results().first();
    INFO(codesOf(result));
    CHECK(result.value(QStringLiteral("ok")).toBool());
    CHECK(result.value(QStringLiteral("id")).toString() == "alice.glow");
    CHECK(result.value(QStringLiteral("version")).toString() == "1.0.0");
    CHECK(result.value(QStringLiteral("problems")).toList().isEmpty());
    CHECK_FALSE(host.installing());
    CHECK_FALSE(host.property("installing").toBool());
    CHECK(watch.changes() == 2);

    const QVariantMap entry = entryOf(host, QStringLiteral("alice.glow"));
    CHECK(entry.value(QStringLiteral("version")).toString() == "1.0.0");
    CHECK(entry.value(QStringLiteral("usable")).toBool());
    CHECK(fs::exists(fixture.user / "alice.glow/1.0.0/manifest.json"));
    CHECK(host.inspect(textOf(archive)).value(QStringLiteral("installedVersion")).toString() == "1.0.0");
}

TEST_CASE("a package given as a file URL installs the same way as one given as a path")
{
    ApplicationScope application;
    const Fixture fixture;
    const fs::path archive = fixture.pack("alice.glow", "2.0.0");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    InstallWatch watch(host);

    host.installPackage(urlOf(archive));
    CHECK(host.installing());

    REQUIRE(watch.waitFor(1));
    const QVariantMap result = watch.results().first();
    INFO(codesOf(result));
    CHECK(result.value(QStringLiteral("ok")).toBool());
    CHECK(result.value(QStringLiteral("id")).toString() == "alice.glow");
    CHECK(result.value(QStringLiteral("version")).toString() == "2.0.0");
    CHECK(result.value(QStringLiteral("problems")).toList().isEmpty());
    CHECK_FALSE(host.installing());
    CHECK(entryOf(host, QStringLiteral("alice.glow")).value(QStringLiteral("version")).toString() == "2.0.0");
    CHECK(fs::exists(fixture.user / "alice.glow/2.0.0/manifest.json"));
}

TEST_CASE("a second install while one is running is refused at once as busy and the first one still finishes")
{
    ApplicationScope application;
    const Fixture fixture;
    const fs::path first = fixture.pack("alice.glow", "1.0.0");
    const fs::path second = fixture.pack("bob.aura", "1.0.0");
    fixture.add(fixture.user, "carol.base", "1.0.0", "Base");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    InstallWatch watch(host);

    host.installPackage(textOf(first));
    REQUIRE(host.installing());
    host.installPackage(textOf(second));

    REQUIRE(watch.results().size() == 1);
    const QVariantMap busy = watch.results().first();
    CHECK_FALSE(busy.value(QStringLiteral("ok")).toBool());
    CHECK(busy.value(QStringLiteral("id")).toString().isEmpty());
    const QVariantList problems = busy.value(QStringLiteral("problems")).toList();
    REQUIRE_FALSE(problems.isEmpty());
    const QVariantMap problem = problems.first().toMap();
    CHECK(problem.value(QStringLiteral("code")).toString() == "install-busy");
    CHECK(problem.value(QStringLiteral("severity")).toString() == "error");
    CHECK(problem.value(QStringLiteral("detail")).toString() == textOf(second));
    CHECK(host.installing());
    CHECK(watch.changes() == 1);
    CHECK_FALSE(host.enable(QStringLiteral("carol.base"), true));
    CHECK_FALSE(host.remove(QStringLiteral("carol.base"), QStringLiteral("1.0.0")));

    REQUIRE(watch.waitFor(2));
    CHECK(watch.results().size() == 2);
    const QVariantMap done = watch.results().at(1);
    INFO(codesOf(done));
    CHECK(done.value(QStringLiteral("ok")).toBool());
    CHECK(done.value(QStringLiteral("id")).toString() == "alice.glow");
    CHECK(done.value(QStringLiteral("version")).toString() == "1.0.0");
    CHECK(done.value(QStringLiteral("problems")).toList().isEmpty());
    CHECK_FALSE(host.installing());
    CHECK(watch.changes() == 2);
    CHECK(fs::exists(fixture.user / "alice.glow/1.0.0/manifest.json"));
    CHECK_FALSE(fs::exists(fixture.user / "bob.aura"));
    const QStringList ids = idsOf(host);
    CHECK(ids.size() == 2);
    CHECK(ids.contains(QStringLiteral("alice.glow")));
    CHECK(ids.contains(QStringLiteral("carol.base")));
    CHECK(host.enable(QStringLiteral("carol.base"), true));
}

TEST_CASE("installing from a file URL with a host or from a UNC path is refused at once as not a local file")
{
    ApplicationScope application;
    const Fixture fixture;
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    InstallWatch watch(host);

    host.installPackage(QStringLiteral("file://gs-test.invalid/share/alice.glow-1.0.0.gsext"));
    host.installPackage(QStringLiteral("\\\\gs-test.invalid\\share\\alice.glow-1.0.0.gsext"));
    host.installBundle(QStringLiteral("file://gs-test.invalid/share/night.gsbundle"));
    host.installBundle(QStringLiteral("\\\\gs-test.invalid\\share\\night.gsbundle"));

    REQUIRE(watch.results().size() == 4);
    for (const QVariantMap& result : watch.results()) {
        INFO(codesOf(result));
        CHECK_FALSE(result.value(QStringLiteral("ok")).toBool());
        CHECK(codesOf(result) == "not-local-file ");
    }
    CHECK_FALSE(host.installing());
    CHECK(watch.changes() == 0);
    CHECK(host.entries().isEmpty());
}

TEST_CASE("a package of a version that is already built in is refused when inspected and when installed")
{
    ApplicationScope application;
    const Fixture fixture;
    fixture.add(fixture.builtin, "alice.glow", "1.0.0", "Glow");
    const fs::path same = fixture.pack("alice.glow", "1.0.0");
    const fs::path newer = fixture.pack("alice.glow", "2.0.0");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, fixture.builtin});
    host.start(false);

    const QVariantMap info = host.inspect(textOf(same));
    INFO(codesOf(info));
    CHECK_FALSE(info.value(QStringLiteral("ok")).toBool());
    CHECK(info.value(QStringLiteral("id")).toString() == "alice.glow");
    CHECK(info.value(QStringLiteral("version")).toString() == "1.0.0");
    CHECK(info.value(QStringLiteral("builtinVersion")).toString() == "1.0.0");
    CHECK(info.value(QStringLiteral("installedVersion")).toString().isEmpty());
    const QVariantMap problem = info.value(QStringLiteral("problems")).toList().value(0).toMap();
    CHECK(problem.value(QStringLiteral("code")).toString() == "builtin-version");
    CHECK(problem.value(QStringLiteral("severity")).toString() == "error");
    CHECK_FALSE(problem.value(QStringLiteral("hint")).toString().isEmpty());

    const QVariantMap update = host.inspect(textOf(newer));
    CHECK(update.value(QStringLiteral("ok")).toBool());
    CHECK(update.value(QStringLiteral("builtinVersion")).toString() == "1.0.0");
    CHECK(update.value(QStringLiteral("installedVersion")).toString().isEmpty());

    InstallWatch watch(host);
    host.installPackage(textOf(same));
    CHECK(host.installing());
    REQUIRE(watch.waitFor(1));
    const QVariantMap refused = watch.results().first();
    CHECK_FALSE(refused.value(QStringLiteral("ok")).toBool());
    CHECK(refused.value(QStringLiteral("id")).toString() == "alice.glow");
    CHECK(codesOf(refused) == "builtin-version ");
    CHECK_FALSE(host.installing());
    CHECK(watch.changes() == 2);
    CHECK_FALSE(fs::exists(fixture.user / "alice.glow"));
    CHECK(host.entries().size() == 1);

    host.installPackage(textOf(newer));
    REQUIRE(watch.waitFor(2));
    const QVariantMap installed = watch.results().at(1);
    INFO(codesOf(installed));
    CHECK(installed.value(QStringLiteral("ok")).toBool());
    CHECK(fs::exists(fixture.user / "alice.glow/2.0.0/manifest.json"));
    CHECK(host.inspect(textOf(same)).value(QStringLiteral("installedVersion")).toString() == "2.0.0");
}

TEST_CASE("a bundle entry that this installation has built in at the same version is not installed again")
{
    ApplicationScope application;
    const Fixture source;
    source.add(source.user, "alice.glow", "1.0.0", "Glow");
    const fs::path bundle = source.scratch / "glow.gsbundle";
    {
        AppSettings settings(source.data);
        ExtensionHost host(settings, {source.user, source.builtin});
        host.start(false);
        REQUIRE(host.enable(QStringLiteral("alice.glow"), true));
        REQUIRE(host.exportBundle({QStringLiteral("alice.glow")}, QStringLiteral("Glow"), textOf(bundle)));
    }

    const Fixture target;
    target.add(target.builtin, "alice.glow", "1.0.0", "Glow");
    AppSettings settings(target.data);
    ExtensionHost host(settings, {target.user, target.builtin});
    host.start(false);
    const QVariantMap info = host.inspectBundle(textOf(bundle));
    INFO(codesOf(info));
    CHECK(info.value(QStringLiteral("ok")).toBool());
    const QVariantList rows = info.value(QStringLiteral("extensions")).toList();
    REQUIRE(rows.size() == 1);
    CHECK(rows.first().toMap().value(QStringLiteral("status")).toString() == "builtin");

    InstallWatch watch(host);
    host.installBundle(textOf(bundle));
    REQUIRE(watch.waitFor(1));
    const QVariantMap done = watch.results().first();
    INFO(codesOf(done));
    CHECK(done.value(QStringLiteral("ok")).toBool());
    CHECK_FALSE(fs::exists(target.user / "alice.glow"));
    CHECK(settings.extensions().at("alice.glow").enabled);
    REQUIRE(host.entries().size() == 1);
    CHECK(host.entries().first().toMap().value(QStringLiteral("active")).toBool());
}

TEST_CASE("a damaged package fails to install with problems and adds nothing to the extension list")
{
    ApplicationScope application;
    const Fixture fixture;
    fixture.add(fixture.user, "bob.aura", "1.0.0", "Aura");
    const fs::path broken = fixture.scratch / "broken.gsext";
    writeFile(broken, "not a package at all");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    InstallWatch watch(host);
    const QStringList before = idsOf(host);
    REQUIRE(before == QStringList{QStringLiteral("bob.aura")});

    host.installPackage(textOf(broken));
    CHECK(host.installing());

    REQUIRE(watch.waitFor(1));
    REQUIRE(watch.results().size() == 1);
    const QVariantMap result = watch.results().first();
    INFO(codesOf(result));
    CHECK_FALSE(result.value(QStringLiteral("ok")).toBool());
    CHECK(result.value(QStringLiteral("id")).toString().isEmpty());
    CHECK_FALSE(result.value(QStringLiteral("problems")).toList().isEmpty());
    CHECK_FALSE(host.installing());
    CHECK(watch.changes() == 2);
    CHECK(idsOf(host) == before);
}
