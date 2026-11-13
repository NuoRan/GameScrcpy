#include "AppSettings.h"
#include "ComponentLibrary.h"
#include "ExtensionExport.h"
#include "ExtensionHost.h"
#include "ProfileLibrary.h"
#include "TaskLibrary.h"

#include "unit/ext/TestFiles.h"

#include <gs/base/BuildInfo.h>
#include <gs/ext/Archive.h>
#include <gs/ext/Manifest.h>
#include <gs/ext/Package.h>
#include <gs/input/Profile.h>

#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using gs::app::AppSettings;
using gs::app::ComponentEntry;
using gs::app::ComponentLibrary;
using gs::app::ExtensionHost;
using gs::app::ProfileLibrary;
using gs::app::TaskLibrary;
using namespace gs::ext::test;
namespace fs = std::filesystem;

namespace {

constexpr const char* kComponentDemo = R"({"format":"gamescrcpy.profile","version":3,
    "meta":{"name":"Component demo"},
    "controls":[{"id":"combo","type":"component","component":"mine/combo","inputs":{"key":"KeyQ"}},
                {"id":"burst","type":"component","component":"examples/burst","inputs":{"key":"KeyB"}}]})";

QString textOf(const fs::path& path)
{
    return QString::fromStdWString(path.wstring());
}

std::string releasedVersionText()
{
    const std::string_view version = gs::buildInfo().version;
    const std::size_t last = version.rfind('.');
    return std::string(last == std::string_view::npos ? version : version.substr(0, last)) + ".0";
}

std::vector<std::string> archived(const fs::path& archive)
{
    const gs::ext::ArchiveListing listing = gs::ext::inspectArchive(archive);
    REQUIRE(listing.ok());
    std::vector<std::string> names;
    for (const gs::ext::ArchiveEntry& file : listing.files) {
        names.push_back(file.path);
    }
    return names;
}

std::string member(const fs::path& archive, std::string_view path)
{
    const std::optional<std::string> text = gs::ext::readArchiveFile(archive, path, std::uint64_t{1} << 20);
    REQUIRE(text.has_value());
    return *text;
}

gs::ext::Manifest validated(const fs::path& archive)
{
    const gs::ext::PackageCheck check = gs::ext::checkPackage(archive);
    INFO(listed(check.diagnostics));
    REQUIRE(check.ok());
    return *check.manifest;
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

}

TEST_CASE("an extension id for an export comes from a safe name, with a fallback for names without letters it can use")
{
    CHECK(gs::app::exportedExtensionId(QStringLiteral("Combo Tap!"), "component") == "my.combo-tap");
    CHECK(gs::app::exportedExtensionId(QStringLiteral("  --Daily__Check-in--  "), "task") == "my.daily-check-in");
    const std::string fallback = gs::app::exportedExtensionId(QStringLiteral("连击"), "component");
    CHECK(fallback.starts_with("my.component-"));
    CHECK(fallback.size() == std::string("my.component-").size() + 8);
    CHECK(gs::ext::validExtensionId(fallback));
    CHECK(gs::ext::validExtensionId(gs::app::exportedExtensionId(QString(200, QChar(u'a')), "profile")));
    CHECK_FALSE(gs::app::exportTarget(QStringLiteral("relative/out.gsext")).has_value());
    CHECK(gs::app::exportTarget(QStringLiteral("file:///C:/exports/out.gsext")).has_value());
}

TEST_CASE("a component of mine exported as an extension package validates, installs back read-only and copies to mine")
{
    const Scratch scratch;
    const fs::path archive = scratch / "combo.gsext";
    {
        ComponentLibrary components(scratch / "sender/examples", scratch / "sender/components");
        const QString ref = components.add(QStringLiteral("Combo"));
        REQUIRE(ref == QStringLiteral("mine/combo"));
        REQUIRE(components.setInfo(ref, QStringLiteral("description"), QStringLiteral("Taps twice")));
        CHECK_FALSE(components.exportPackage(QStringLiteral("mine/missing"), textOf(archive)));
        REQUIRE(components.exportPackage(ref, textOf(archive)));
    }
    const gs::ext::Manifest manifest = validated(archive);
    CHECK(manifest.id == "my.combo");
    CHECK(manifest.version.text() == "1.0.0");
    REQUIRE(manifest.gamescrcpy.has_value());
    CHECK(manifest.gamescrcpy->text() == releasedVersionText());
    CHECK(manifest.name == "Combo");
    CHECK(manifest.description == "Taps twice");
    CHECK(manifest.declares(gs::ext::ExtensionPoint::Component));
    CHECK(manifest.requirements.at(gs::ext::ExtensionPoint::Component).text() == "^1.1");
    CHECK(manifest.contributes.components ==
          std::vector<gs::ext::ComponentContribution>{{.name = "combo", .folder = "components/combo"}});
    CHECK(archived(archive) == std::vector<std::string>{"components/combo/component.json",
                                                        "components/combo/component.ts",
                                                        "components/combo/tsconfig.json", "manifest.json"});

    AppSettings settings(scratch / "data");
    ExtensionHost host(settings, {scratch / "user", {}});
    host.start(false);
    const gs::ext::InstalledExtension installed = host.install(archive);
    INFO(listed(installed.diagnostics));
    REQUIRE(installed.ok());
    CHECK(host.components().empty());
    REQUIRE(host.enable(QStringLiteral("my.combo"), true));
    REQUIRE(host.components().size() == 1);
    const gs::app::ExtensionComponent contributed = host.components().front();
    CHECK(contributed.ref == QStringLiteral("examples/my.combo~combo"));
    CHECK(contributed.extensionName == QStringLiteral("Combo"));
    CHECK(fs::exists(contributed.folder / "component.ts"));

    ComponentLibrary library(scratch / "receiver/examples", scratch / "receiver/components");
    library.setExtensionComponents(host.components());
    const ComponentEntry* entry = library.find(contributed.ref);
    REQUIRE(entry != nullptr);
    CHECK(entry->example);
    CHECK(entry->name == QStringLiteral("Combo"));
    CHECK(entry->description == QStringLiteral("Taps twice"));
    CHECK(entry->extensionName == QStringLiteral("Combo"));
    REQUIRE(library.examples().size() == 1);
    CHECK(library.examples().first().toMap().value(QStringLiteral("extension")).toString() == QStringLiteral("Combo"));
    CHECK(library.mine().isEmpty());
    CHECK_FALSE(library.setInfo(contributed.ref, QStringLiteral("name"), QStringLiteral("Renamed")));
    CHECK_FALSE(library.remove(contributed.ref));
    CHECK_FALSE(library.exportPackage(contributed.ref, textOf(scratch / "again.gsext")));

    const QString copied = library.copyToMine(contributed.ref);
    CHECK(copied == QStringLiteral("mine/combo-2"));
    REQUIRE(library.find(copied) != nullptr);
    CHECK_FALSE(library.find(copied)->example);
    CHECK(library.find(copied)->name == QStringLiteral("Combo 2"));

    REQUIRE(host.enable(QStringLiteral("my.combo"), false));
    CHECK(host.components().empty());
    library.setExtensionComponents(host.components());
    CHECK(library.find(contributed.ref) == nullptr);
    CHECK(library.find(copied) != nullptr);
}

TEST_CASE("a component built on another of mine takes its base along, and both copy back still linked")
{
    const Scratch scratch;
    const fs::path archive = scratch / "child.gsext";
    {
        ComponentLibrary components(scratch / "sender/examples", scratch / "sender/components");
        const QString base = components.add(QStringLiteral("Base"));
        REQUIRE(base == QStringLiteral("mine/base"));
        const QString child = components.add(QStringLiteral("Child"), base);
        REQUIRE(child == QStringLiteral("mine/child"));
        REQUIRE(components.find(child)->parent == base);
        REQUIRE(components.exportPackage(child, textOf(archive)));
    }
    const gs::ext::Manifest manifest = validated(archive);
    CHECK(manifest.id == "my.child");
    CHECK(manifest.contributes.components == std::vector<gs::ext::ComponentContribution>{
                                                 {.name = "child", .folder = "components/child"},
                                                 {.name = "base", .folder = "components/base"}});
    const std::string code = member(archive, "components/child/component.ts");
    CHECK(code.find("\"../my.child~base/component.ts\"") != std::string::npos);
    CHECK(code.find("\"../base/component.ts\"") == std::string::npos);

    AppSettings settings(scratch / "data");
    ExtensionHost host(settings, {scratch / "user", {}});
    host.start(false);
    REQUIRE(host.install(archive).ok());
    REQUIRE(host.enable(QStringLiteral("my.child"), true));
    REQUIRE(host.components().size() == 2);

    ComponentLibrary library(scratch / "receiver/examples", scratch / "receiver/components");
    library.setExtensionComponents(host.components());
    const ComponentEntry* entry = library.find(QStringLiteral("examples/my.child~child"));
    REQUIRE(entry != nullptr);
    CHECK(entry->parent == QStringLiteral("examples/my.child~base"));
    REQUIRE(library.find(QStringLiteral("examples/my.child~base")) != nullptr);

    const QString copied = library.copyToMine(QStringLiteral("examples/my.child~child"));
    CHECK(copied == QStringLiteral("mine/child-2"));
    const ComponentEntry* mine = library.find(copied);
    REQUIRE(mine != nullptr);
    CHECK(mine->parent == QStringLiteral("mine/base-2"));
    REQUIRE(library.find(QStringLiteral("mine/base-2")) != nullptr);
    CHECK(readFile(scratch / "receiver/components/child-2/component.ts").find("\"../base-2/component.ts\"") !=
          std::string::npos);
}

TEST_CASE("a task exported as an extension package leaves phones and schedule behind and copies back from its read-only entry")
{
    const Scratch scratch;
    const fs::path archive = scratch / "daily.gsext";
    {
        TaskLibrary tasks(scratch / "sender/tasks");
        const QString id = tasks.add(QStringLiteral("Daily check-in"));
        REQUIRE(id == QStringLiteral("daily-check-in"));
        REQUIRE(tasks.setPhones(id, {QStringLiteral("serial-1")}));
        REQUIRE(tasks.setSchedule(id, QStringLiteral("daily"), QStringLiteral("22:00")));
        REQUIRE(tasks.setValue(id, QStringLiteral("rounds"), 5));
        CHECK_FALSE(tasks.exportPackage(QStringLiteral("missing"), textOf(archive)));
        REQUIRE(tasks.exportPackage(id, textOf(archive)));
    }
    const gs::ext::Manifest manifest = validated(archive);
    CHECK(manifest.id == "my.daily-check-in");
    CHECK(manifest.name == "Daily check-in");
    CHECK(manifest.declares(gs::ext::ExtensionPoint::Task));
    CHECK(manifest.contributes.tasks ==
          std::vector<gs::ext::TaskContribution>{{.name = "daily-check-in", .folder = "tasks/daily-check-in"}});
    const QJsonObject settingsFile =
        QJsonDocument::fromJson(QByteArray::fromStdString(member(archive, "tasks/daily-check-in/task.json"))).object();
    CHECK(settingsFile.value(QStringLiteral("phones")).toArray().isEmpty());
    CHECK(settingsFile.value(QStringLiteral("schedule")).toObject().value(QStringLiteral("mode")).toString() ==
          QStringLiteral("manual"));
    CHECK_FALSE(settingsFile.contains(QStringLiteral("lastScheduled")));
    CHECK(settingsFile.value(QStringLiteral("values")).toObject().value(QStringLiteral("rounds")).toDouble() == 5.0);
    CHECK(archived(archive) == std::vector<std::string>{"manifest.json", "tasks/daily-check-in/task.json",
                                                        "tasks/daily-check-in/task.ts",
                                                        "tasks/daily-check-in/tsconfig.json"});

    AppSettings settings(scratch / "data");
    ExtensionHost host(settings, {scratch / "user", {}});
    host.start(false);
    REQUIRE(host.install(archive).ok());
    REQUIRE(host.enable(QStringLiteral("my.daily-check-in"), true));
    REQUIRE(host.tasks().size() == 1);
    const QString contributed = host.tasks().front().id;
    CHECK(contributed == QStringLiteral("my.daily-check-in/daily-check-in"));

    TaskLibrary library(scratch / "receiver/tasks");
    library.setExtensionTasks(host.tasks());
    const gs::app::TaskEntry* entry = library.find(contributed);
    REQUIRE(entry != nullptr);
    CHECK(entry->readOnly);
    CHECK(entry->name == QStringLiteral("Daily check-in"));
    CHECK(entry->extensionName == QStringLiteral("Daily check-in"));
    CHECK(entry->schedule == QStringLiteral("manual"));
    CHECK(entry->phones.isEmpty());
    CHECK(library.paramsJson(contributed) == QStringLiteral(R"({"rounds":5})"));
    REQUIRE(library.tasks().size() == 1);
    CHECK(library.tasks().first().toMap().value(QStringLiteral("readOnly")).toBool());
    CHECK_FALSE(library.setValue(contributed, QStringLiteral("rounds"), 2));
    CHECK_FALSE(library.rename(contributed, QStringLiteral("Renamed")));
    CHECK_FALSE(library.setPhones(contributed, {QStringLiteral("serial-2")}));
    CHECK_FALSE(library.setSchedule(contributed, QStringLiteral("daily"), QStringLiteral("07:00")));
    CHECK_FALSE(library.remove(contributed));
    CHECK_FALSE(library.exportPackage(contributed, textOf(scratch / "again.gsext")));

    const QString copied = library.copyToMine(contributed);
    CHECK(copied == QStringLiteral("daily-check-in"));
    const gs::app::TaskEntry* mine = library.find(copied);
    REQUIRE(mine != nullptr);
    CHECK_FALSE(mine->readOnly);
    CHECK(fs::exists(scratch / "receiver/tasks/daily-check-in/task.ts"));
    CHECK(library.paramsJson(copied) == QStringLiteral(R"({"rounds":5})"));
    CHECK(library.setValue(copied, QStringLiteral("rounds"), 2));
    CHECK(library.paramsJson(copied) == QStringLiteral(R"({"rounds":2})"));
    CHECK(library.tasks().size() == 2);
}

TEST_CASE("a profile with its logic and my components exported as an extension package installs back and copies with its logic")
{
    const ApplicationScope application;
    const Scratch scratch;
    const fs::path archive = scratch / "demo.gsext";
    {
        ComponentLibrary components(scratch / "sender/examples", scratch / "sender/components");
        REQUIRE(components.add(QStringLiteral("Combo")) == QStringLiteral("mine/combo"));
        writeFile(scratch / "sender/profiles/demo/profile.json", kComponentDemo);
        writeFile(scratch / "sender/profiles/demo/logic.ts", "export function go(): void {}\n");
        writeFile(scratch / "sender/profiles/demo/node_modules/pkg/index.js", "module.exports = 1;\n");
        ProfileLibrary library(scratch / "sender/profiles");
        REQUIRE(library.find(QStringLiteral("demo")) != nullptr);
        CHECK_FALSE(library.exportPackage(QStringLiteral("missing"), textOf(archive)));
        REQUIRE(library.exportPackage(QStringLiteral("demo"), textOf(archive)));
    }
    const gs::ext::Manifest manifest = validated(archive);
    CHECK(manifest.id == "my.component-demo");
    CHECK(manifest.name == "Component demo");
    CHECK(manifest.declares(gs::ext::ExtensionPoint::Profile));
    CHECK(manifest.declares(gs::ext::ExtensionPoint::Component));
    CHECK(manifest.contributes.profiles ==
          std::vector<gs::ext::ProfileContribution>{{.name = "demo", .file = "profiles/demo/profile.json"}});
    CHECK(manifest.contributes.components ==
          std::vector<gs::ext::ComponentContribution>{{.name = "combo", .folder = "components/combo"}});
    CHECK(archived(archive) == std::vector<std::string>{"components/combo/component.json",
                                                        "components/combo/component.ts",
                                                        "components/combo/tsconfig.json", "manifest.json",
                                                        "profiles/demo/logic.ts", "profiles/demo/profile.json"});
    const gs::input::LoadResult exported = gs::input::loadProfile(member(archive, "profiles/demo/profile.json"));
    REQUIRE(exported.ok());
    REQUIRE(exported.profile.controls.size() == 2);
    CHECK(std::get<gs::input::ComponentControl>(exported.profile.controls[0].kind).component ==
          "examples/my.component-demo~combo");
    CHECK(std::get<gs::input::ComponentControl>(exported.profile.controls[1].kind).component == "examples/burst");

    AppSettings settings(scratch / "data");
    ExtensionHost host(settings, {scratch / "user", {}});
    host.start(false);
    REQUIRE(host.install(archive).ok());
    REQUIRE(host.enable(QStringLiteral("my.component-demo"), true));
    REQUIRE(host.profiles().size() == 1);
    CHECK(host.profiles().front().id == QStringLiteral("my.component-demo/demo"));
    CHECK(fs::exists(host.profiles().front().folder / "logic.ts"));
    REQUIRE(host.components().size() == 1);
    CHECK(host.components().front().ref == QStringLiteral("examples/my.component-demo~combo"));

    ProfileLibrary library(scratch / "receiver/profiles");
    library.setExtensionProfiles(host.profiles());
    const gs::app::ProfileEntry* entry = library.find(QStringLiteral("my.component-demo/demo"));
    REQUIRE(entry != nullptr);
    CHECK(entry->readOnly());
    CHECK(entry->logic);
    CHECK_FALSE(library.exportPackage(entry->id, textOf(scratch / "again.gsext")));
    const QString copied = library.duplicate(entry->id);
    CHECK(copied == QStringLiteral("demo-copy"));
    const gs::app::ProfileEntry* mine = library.find(copied);
    REQUIRE(mine != nullptr);
    CHECK_FALSE(mine->readOnly());
    CHECK(mine->logic);
    CHECK(fs::exists(scratch / "receiver/profiles/demo-copy/logic.ts"));
}
