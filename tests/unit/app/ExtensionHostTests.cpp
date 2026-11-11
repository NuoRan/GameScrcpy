#include "AppSettings.h"
#include "ExtensionHealth.h"
#include "ExtensionHost.h"
#include "SettingsCatalog.h"
#include "ThemeTokens.h"

#include "unit/ext/TestFiles.h"

#include <gs/ext/Archive.h>
#include <gs/ext/Manifest.h>
#include <gs/input/InputSource.h>
#include <gs/input/Profile.h>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <windows.h>

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using gs::app::AppSettings;
using gs::app::ExtensionHost;
using namespace gs::ext::test;
namespace fs = std::filesystem;
namespace settings = gs::app::settings;

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

std::string manifest(std::string_view id, std::string_view version, std::string_view more = {})
{
    return R"({"schemaVersion": 1, "id": ")" + std::string(id) + R"(", "version": ")" + std::string(version) +
           R"(", "name": "Glow", "license": "MIT", "requires": {"filter": "^1.0"},
           "contributes": {"filters": [{"name": "Glow", "file": "shaders/glow.hlsl"}]},
           "settings": [)" +
           std::string(kSettings) + "]" + std::string(more) + "}";
}

void writeExtension(const fs::path& folder, const std::string& manifestText, std::string_view shader = kShader)
{
    writeFile(folder / "manifest.json", manifestText);
    writeFile(folder / "shaders/glow.hlsl", shader);
}

struct Fixture {
    Scratch scratch;
    fs::path data = scratch / "data";
    fs::path user = scratch / "user";
    fs::path builtin = scratch / "builtin";
    fs::path marker = scratch / "data/extensions.running";

    void add(const fs::path& root, std::string_view id, std::string_view version, std::string_view more = {},
             std::string_view shader = kShader) const
    {
        writeExtension(root / pathOf(id) / pathOf(version), manifest(id, version, more), shader);
    }
};

constexpr std::string_view kStandardProfile = R"({"format": "gamescrcpy.profile", "version": 3,
    "meta": {"name": "Standard", "app": "com.example.shooter"},
    "controls": [{"id": "fire", "type": "hold", "inputs": {"press": "Mouse.Left"}},
                 {"id": "jump", "type": "tap", "inputs": {"press": "Space"}}]})";

std::string pressOf(const QByteArray& json, std::string_view id)
{
    const gs::input::LoadResult loaded =
        gs::input::loadProfile(std::string_view(json.constData(), static_cast<std::size_t>(json.size())));
    REQUIRE(loaded.ok());
    const auto found = std::ranges::find(loaded.profile.controls, id, &gs::input::Control::id);
    if (found == loaded.profile.controls.end()) {
        return {};
    }
    const auto* button = std::get_if<gs::input::HoldControl>(&found->kind);
    return button != nullptr && button->press ? gs::input::chordName(button->press->input) : std::string();
}

bool activeVersion(const ExtensionHost& host, std::string_view id, std::string_view version)
{
    return std::ranges::any_of(host.extensions(), [&](const ExtensionHost::Extension& extension) {
        return extension.active && extension.manifest->id == id && extension.manifest->version.text() == version;
    });
}

}

TEST_CASE("extensions in both roots start switched off and give their filters once enabled")
{
    const Fixture fixture;
    fixture.add(fixture.builtin, "gamescrcpy.glow", "1.0.0");
    fixture.add(fixture.user, "alice.glow", "2.0.0");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, fixture.builtin});
    host.start(false, fixture.marker);

    CHECK(host.mode() == ExtensionHost::Mode::Normal);
    REQUIRE(host.extensions().size() == 2);
    for (const ExtensionHost::Extension& extension : host.extensions()) {
        INFO(listed(extension.diagnostics));
        CHECK(extension.usable());
        CHECK_FALSE(extension.active);
    }
    CHECK(host.extensions()[0].builtin);
    CHECK(host.extensions()[0].manifest->id == "gamescrcpy.glow");
    CHECK_FALSE(host.extensions()[1].builtin);
    CHECK(host.filters("").empty());
    CHECK_FALSE(fs::exists(fixture.marker));

    int changes = 0;
    QObject::connect(&host, &ExtensionHost::filtersChanged, [&changes] { ++changes; });
    REQUIRE(host.setEnabled("gamescrcpy.glow", true));
    CHECK(changes == 1);
    const std::vector<gs::render::VideoFilter> filters = host.filters("");
    REQUIRE(filters.size() == 1);
    CHECK(filters[0].id == "gamescrcpy.glow/Glow");
    CHECK(filters[0].fileName == "gamescrcpy.glow/shaders/glow.hlsl");
    CHECK(filters[0].source == kShader);
    CHECK(filters[0].values ==
          std::map<std::string, double, std::less<>>{{"lines", 3.0}, {"steps", 3.0}, {"strength", 0.5}});
    CHECK(fs::exists(fixture.marker));
    CHECK(settings.extensions().at("gamescrcpy.glow").enabled);

    REQUIRE(host.setEnabled("gamescrcpy.glow", false));
    CHECK(host.filters("").empty());
    CHECK_FALSE(fs::exists(fixture.marker));
    CHECK(settings.extensions().empty());
    CHECK_FALSE(host.setEnabled("nobody.here", true));
}

TEST_CASE("filter values come from the device, then the global value, then the setting's default, then the source")
{
    const Fixture fixture;
    fixture.add(fixture.user, "alice.glow", "1.0.0");
    {
        AppSettings settings(fixture.data);
        ExtensionHost host(settings, {fixture.user, {}});
        host.start(false);
        REQUIRE(host.setEnabled("alice.glow", true));
        CHECK(host.filters("phone").at(0).values.at("strength") == 0.5);

        CHECK(host.setValue("alice.glow", "strength", 0.25));
        CHECK(host.setValue("alice.glow", "steps", std::int64_t{4}));
        CHECK(host.setDeviceValue("phone", "alice.glow", "strength", 0.75));
        CHECK(host.filters("phone").at(0).values.at("strength") == 0.75);
        CHECK(host.filters("tablet").at(0).values.at("strength") == 0.25);
        CHECK(host.filters("").at(0).values.at("strength") == 0.25);
        CHECK(host.filters("phone").at(0).values.at("steps") == 4.0);

        CHECK_FALSE(host.setValue("alice.glow", "strength", 2.0));
        CHECK_FALSE(host.setValue("alice.glow", "strength", std::string("loud")));
        CHECK_FALSE(host.setValue("alice.glow", "steps", 2.5));
        CHECK_FALSE(host.setValue("alice.glow", "lines", 4.0));
        CHECK_FALSE(host.setValue("bob.none", "strength", 0.5));
        CHECK_FALSE(host.setDeviceValue("phone", "alice.glow", "steps", std::int64_t{2}));
        CHECK_FALSE(host.setDeviceValue("", "alice.glow", "strength", 0.5));
    }
    {
        AppSettings settings(fixture.data);
        ExtensionHost host(settings, {fixture.user, {}});
        host.start(false);
        CHECK(host.filters("phone").at(0).values.at("strength") == 0.75);
        CHECK(host.filters("tablet").at(0).values.at("strength") == 0.25);
        CHECK(host.resetDeviceValue("phone", "alice.glow", "strength"));
        CHECK_FALSE(host.resetDeviceValue("phone", "alice.glow", "strength"));
        CHECK(host.filters("phone").at(0).values.at("strength") == 0.25);
        CHECK(settings.device("phone") == settings::DeviceSettings{});

        settings::ExtensionStates states = settings.extensions();
        states["alice.glow"].values["strength"] = std::string("loud");
        settings.setExtensions(states);
        CHECK(host.filters("").at(0).values.at("strength") == 0.5);
    }
}

TEST_CASE("an extension whose dependency is missing or whose extension point the host lacks stays off with a reason")
{
    const Fixture fixture;
    fixture.add(fixture.user, "alice.needs", "1.0.0", R"(, "dependencies": {"bob.base": "^1.0"})");
    writeFile(fixture.user / "alice.script/1.0.0/manifest.json",
              R"({"schemaVersion": 1, "id": "alice.script", "version": "1.0.0", "name": "Script", "license": "MIT",
                  "requires": {"command": "^1.0"},
                  "contributes": {"commands": [{"name": "helper"}]}})");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    REQUIRE(host.setEnabled("alice.needs", true));
    REQUIRE(host.setEnabled("alice.script", true));
    INFO(listed(host.problems()));
    CHECK(host.filters("").empty());
    CHECK(std::ranges::none_of(host.extensions(), &ExtensionHost::Extension::active));
    CHECK(reports(host.problemsOf("alice.needs"), gs::ext::Code::DependencyMissing));
    CHECK(reports(host.problemsOf("alice.script"), gs::ext::Code::HostApiMissing,
                  R"($["alice.script"].requires["command"])"));

    fixture.add(fixture.user, "bob.base", "1.2.0");
    host.start(false);
    CHECK(host.problemsOf("alice.needs").empty());
    const std::vector<gs::render::VideoFilter> filters = host.filters("");
    REQUIRE(filters.size() == 2);
    CHECK(filters[0].id == "bob.base/Glow");
    CHECK(filters[1].id == "alice.needs/Glow");
}

TEST_CASE("every permission an extension can ask for is shown by its own title rather than its name")
{
    const Fixture fixture;
    std::string permissions;
    for (const std::string_view name : gs::ext::kPermissionNames) {
        permissions += std::string(permissions.empty() ? "" : ", ") + R"({"name": ")" + std::string(name) +
                       R"(", "reason": "Needed.")" + (name == "network" ? R"(, "hosts": ["api.example.com"])" : "") +
                       (name == "files.paths" ? R"(, "paths": ["%USERPROFILE%/Presets"])" : "") + "}";
    }
    writeFile(fixture.user / "alice.script/1.0.0/manifest.json",
              R"({"schemaVersion": 1, "id": "alice.script", "version": "1.0.0", "name": "Script", "license": "MIT",
                  "requires": {"script": "^1.0"},
                  "contributes": {"scripts": [{"name": "helper", "entry": "helper.ts", "run": "app"}]},
                  "permissions": [)" +
                  permissions + "]}");
    writeFile(fixture.user / "alice.script/1.0.0/helper.ts", "export default async function () {}\n");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    REQUIRE(host.entries().size() == 1);
    const QVariantList shown = host.entries()[0].toMap()[QStringLiteral("permissions")].toList();
    REQUIRE(shown.size() == static_cast<qsizetype>(gs::ext::kPermissionNames.size()));
    QStringList titles;
    for (const QVariant& item : shown) {
        const QVariantMap permission = item.toMap();
        INFO(permission[QStringLiteral("name")].toString().toStdString());
        CHECK_FALSE(permission[QStringLiteral("title")].toString().isEmpty());
        CHECK(permission[QStringLiteral("title")] != permission[QStringLiteral("name")]);
        titles.append(permission[QStringLiteral("title")].toString());
    }
    titles.removeDuplicates();
    CHECK(titles.size() == shown.size());
}

TEST_CASE("extensions offer read-only profiles and patches that stack in load order and leave with them (PRF-006)")
{
    const Fixture fixture;
    writeFile(fixture.user / "alice.pack/1.0.0/manifest.json",
              R"({"schemaVersion": 1, "id": "alice.pack", "version": "1.0.0", "name": "Alice pack", "license": "MIT",
                  "requires": {"profile": "^1.0"},
                  "contributes": {"profiles": [{"name": "standard", "file": "profiles/standard.json"},
                                               {"name": "broken", "file": "profiles/broken.json"}]}})");
    writeFile(fixture.user / "alice.pack/1.0.0/profiles/standard.json", kStandardProfile);
    writeFile(fixture.user / "alice.pack/1.0.0/profiles/broken.json", "{");
    const auto addPatch = [&fixture](std::string_view id, std::string_view name, std::string_view target,
                                     std::string_view body) {
        writeFile(fixture.user / pathOf(id) / "1.0.0/manifest.json",
                  R"({"schemaVersion": 1, "id": ")" + std::string(id) + R"(", "version": "1.0.0", "name": ")" +
                      std::string(name) + R"(", "license": "MIT", "requires": {"profile": "^1.0"},
                      "contributes": {"profilePatches": [{"target": ")" +
                      std::string(target) + R"(", "file": "patch.json"}]}})");
        writeFile(fixture.user / pathOf(id) / "1.0.0/patch.json",
                  R"({"format": "gamescrcpy.profile-patch", "version": 1, )" + std::string(body) + "}");
    };
    addPatch("bob.lean", "Bob lean", "alice.pack/standard",
             R"("controls": {"change": {"fire": {"inputs": {"press": "KeyF"}}},
                             "add": [{"id": "lean", "type": "hold", "inputs": {"press": "KeyQ"}}]})");
    addPatch("carol.keys", "Carol keys", "alice.pack/standard",
             R"("controls": {"change": {"fire": {"inputs": {"press": "KeyG"}}, "ghost": {"tapMs": 40}}})");
    addPatch("dave.other", "Dave other", "erin.pack/standard", R"("meta": {"name": "Other"})");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    CHECK(host.profiles().empty());
    int changes = 0;
    QObject::connect(&host, &ExtensionHost::profilesChanged, [&changes] { ++changes; });
    for (const char* id : {"alice.pack", "bob.lean", "carol.keys", "dave.other"}) {
        REQUIRE(host.setEnabled(id, true));
    }
    INFO(listed(host.problems()));
    CHECK(changes > 0);
    REQUIRE(host.profiles().size() == 2);
    const auto standard = [&host]() -> const gs::app::ExtensionProfile& {
        const auto found = std::ranges::find(host.profiles(), QStringLiteral("alice.pack/standard"),
                                             &gs::app::ExtensionProfile::id);
        REQUIRE(found != host.profiles().end());
        return *found;
    };
    CHECK(standard().extensionName == QStringLiteral("Alice pack"));
    CHECK(standard().version == QStringLiteral("1.0.0"));
    CHECK(standard().patchedBy == QStringList{QStringLiteral("Bob lean"), QStringLiteral("Carol keys")});
    REQUIRE(standard().conflicts.size() == 1);
    const QVariantMap conflict = standard().conflicts[0].toMap();
    CHECK(conflict[QStringLiteral("id")].toString() == QStringLiteral("fire"));
    CHECK(conflict[QStringLiteral("by")].toStringList() == QStringList{QStringLiteral("Bob lean"), QStringLiteral("Carol keys")});
    CHECK(pressOf(standard().json, "fire") == "KeyG");
    CHECK(pressOf(standard().json, "lean") == "KeyQ");
    CHECK(reports(host.problemsOf("carol.keys"), gs::ext::Code::PatchTargetMissing,
                  R"($["carol.keys"].contributes.profilePatches[0].file)"));
    CHECK(reports(host.problemsOf("dave.other"), gs::ext::Code::PatchTargetMissing,
                  R"($["dave.other"].contributes.profilePatches[0].target)"));
    CHECK(reports(host.problemsOf("alice.pack"), gs::ext::Code::ProfileInvalid,
                  R"($["alice.pack"].contributes.profiles[1].file)"));
    CHECK_FALSE(gs::ext::hasErrors(host.problems()));

    REQUIRE(host.setEnabled("carol.keys", false));
    CHECK(standard().patchedBy == QStringList{QStringLiteral("Bob lean")});
    CHECK(standard().conflicts.isEmpty());
    CHECK(pressOf(standard().json, "fire") == "KeyF");

    REQUIRE(host.setEnabled("bob.lean", false));
    CHECK(standard().patchedBy.isEmpty());
    CHECK(standard().json == QByteArray(kStandardProfile.data(), static_cast<qsizetype>(kStandardProfile.size())));
    CHECK(pressOf(standard().json, "fire") == "Mouse.Left");
    CHECK(pressOf(standard().json, "lean").empty());

    REQUIRE(host.setEnabled("alice.pack", false));
    CHECK(host.profiles().empty());
    CHECK(reports(host.problemsOf("dave.other"), gs::ext::Code::PatchTargetMissing));
}

TEST_CASE("a locked version is used even when a newer one is installed")
{
    const Fixture fixture;
    const std::string older = std::string(kShader) + "\n";
    fixture.add(fixture.user, "alice.glow", "1.0.0", {}, older);
    fixture.add(fixture.user, "alice.glow", "2.0.0");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    REQUIRE(host.setEnabled("alice.glow", true));
    CHECK(host.filters("").at(0).source == kShader);

    settings::ExtensionStates states = settings.extensions();
    states["alice.glow"].version = "1.0.0";
    settings.setExtensions(states);
    host.start(false);
    CHECK(host.filters("").at(0).source == older);
    CHECK(activeVersion(host, "alice.glow", "1.0.0"));

    states["alice.glow"].version = "3.0.0";
    settings.setExtensions(states);
    host.start(false);
    CHECK(host.filters("").empty());
    CHECK(reports(host.problemsOf("alice.glow"), gs::ext::Code::DependencyMissing));
}

TEST_CASE("safe mode loads nothing, when asked and after a run that ended while extensions were loaded")
{
    const Fixture fixture;
    fixture.add(fixture.user, "alice.glow", "1.0.0");
    AppSettings settings(fixture.data);
    {
        ExtensionHost host(settings, {fixture.user, {}});
        host.start(false, fixture.marker);
        REQUIRE(host.setEnabled("alice.glow", true));
        CHECK(fs::exists(fixture.marker));
    }
    {
        ExtensionHost host(settings, {fixture.user, {}});
        host.start(false, fixture.marker);
        CHECK(host.mode() == ExtensionHost::Mode::SafeAfterCrash);
        CHECK(host.filters("").empty());
        REQUIRE(host.extensions().size() == 1);
        CHECK_FALSE(host.extensions()[0].active);
        CHECK_FALSE(fs::exists(fixture.marker));
        CHECK(settings.extensions().at("alice.glow").enabled);
        host.finish();
    }
    {
        ExtensionHost host(settings, {fixture.user, {}});
        host.start(false, fixture.marker);
        CHECK(host.mode() == ExtensionHost::Mode::Normal);
        CHECK(host.filters("").size() == 1);
        host.finish();
        CHECK_FALSE(fs::exists(fixture.marker));
    }
    {
        ExtensionHost host(settings, {fixture.user, {}});
        host.start(true, fixture.marker);
        CHECK(host.mode() == ExtensionHost::Mode::SafeRequested);
        CHECK(host.filters("").empty());
        CHECK_FALSE(fs::exists(fixture.marker));
    }
}

TEST_CASE("installing through the host replaces older versions, and an update that adds a dependency waits for consent")
{
    const Fixture fixture;
    const auto packed = [&fixture](std::string_view version, std::string_view more = {}) {
        const fs::path folder = fixture.scratch / ("package-" + std::string(version));
        writeExtension(folder, manifest("alice.glow", version, more));
        const fs::path archive = fixture.scratch / ("alice.glow-" + std::string(version) + ".gsext");
        REQUIRE(gs::ext::packDirectory(folder, archive).empty());
        return archive;
    };
    fixture.add(fixture.builtin, "gamescrcpy.glow", "1.0.0");
    fixture.add(fixture.user, "bob.base", "1.0.0");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, fixture.builtin});
    host.start(false);

    const gs::ext::InstalledExtension first = host.install(packed("1.0.0"));
    INFO(listed(first.diagnostics));
    REQUIRE(first.ok());
    REQUIRE(host.setEnabled("alice.glow", true));
    CHECK(activeVersion(host, "alice.glow", "1.0.0"));

    REQUIRE(host.install(packed("1.1.0")).ok());
    CHECK_FALSE(fs::exists(fixture.user / "alice.glow/1.0.0"));
    CHECK(settings.extensions().at("alice.glow").enabled);
    CHECK(activeVersion(host, "alice.glow", "1.1.0"));
    CHECK(host.filters("").size() == 1);

    REQUIRE(host.install(packed("2.0.0", R"(, "dependencies": {"bob.base": "^1.0"})")).ok());
    CHECK_FALSE(activeVersion(host, "alice.glow", "2.0.0"));
    CHECK(host.filters("").empty());
    CHECK(reports(host.problemsOf("alice.glow"), gs::ext::Code::ConsentRequired));
    REQUIRE(host.setEnabled("alice.glow", true));
    CHECK(activeVersion(host, "alice.glow", "2.0.0"));
    CHECK_FALSE(reports(host.problemsOf("alice.glow"), gs::ext::Code::ConsentRequired));

    CHECK_FALSE(host.install(fixture.scratch / "missing.gsext").ok());
    CHECK(host.uninstall("alice.glow", *gs::ext::Version::parse("2.0.0")));
    CHECK_FALSE(fs::exists(fixture.user / "alice.glow"));
    CHECK_FALSE(settings.extensions().contains("alice.glow"));
    CHECK_FALSE(fs::exists(fixture.user / ".consent/alice.glow.json"));
    CHECK_FALSE(host.uninstall("gamescrcpy.glow", *gs::ext::Version::parse("1.0.0")));
    CHECK(fs::exists(fixture.builtin / "gamescrcpy.glow/1.0.0/manifest.json"));
}

TEST_CASE("a version that asks for more than was allowed stays off until the extension is turned on again")
{
    const Fixture fixture;
    constexpr std::string_view kNeedsBase = R"(, "dependencies": {"bob.base": "^1.0"})";
    fixture.add(fixture.user, "alice.glow", "1.0.0");
    fixture.add(fixture.user, "bob.base", "1.0.0");
    AppSettings settings(fixture.data);
    {
        ExtensionHost host(settings, {fixture.user, {}});
        host.start(false);
        REQUIRE(host.setEnabled("alice.glow", true));
        CHECK(activeVersion(host, "alice.glow", "1.0.0"));
        CHECK(fs::exists(fixture.user / ".consent/alice.glow.json"));
    }
    std::string newest;
    std::string meanwhile;
    SUBCASE("a newer version was written while the program was not running")
    {
        fixture.add(fixture.user, "alice.glow", "2.0.0", kNeedsBase);
        newest = "2.0.0";
        meanwhile = "1.0.0";
    }
    SUBCASE("the same version was rewritten with a new dependency")
    {
        fixture.add(fixture.user, "alice.glow", "1.0.0", kNeedsBase);
        newest = "1.0.0";
    }
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    CHECK(settings.extensions().at("alice.glow").enabled);
    CHECK_FALSE(activeVersion(host, "alice.glow", newest));
    CHECK(meanwhile.empty() == !activeVersion(host, "alice.glow", "1.0.0"));
    CHECK(reports(host.problemsOf("alice.glow"), gs::ext::Code::ConsentRequired));

    REQUIRE(host.setEnabled("alice.glow", true));
    CHECK(activeVersion(host, "alice.glow", newest));
    CHECK(activeVersion(host, "bob.base", "1.0.0"));
    CHECK_FALSE(reports(host.problemsOf("alice.glow"), gs::ext::Code::ConsentRequired));
}

TEST_CASE("an extension turned on while another program holds its consent file runs now, and the consent is saved at exit")
{
    const Fixture fixture;
    fixture.add(fixture.user, "alice.glow", "1.0.0", R"(, "dependencies": {"bob.base": "^1.0"})");
    fixture.add(fixture.user, "bob.base", "1.0.0");
    const fs::path consent = fixture.user / ".consent/alice.glow.json";
    writeFile(consent, "{}");
    AppSettings settings(fixture.data);
    {
        ExtensionHost host(settings, {fixture.user, {}});
        host.start(false);
        int busy = 0;
        QObject::connect(&host, &ExtensionHost::saveBusy, [&busy] { ++busy; });
        HANDLE reader = ::CreateFileW(consent.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        REQUIRE(reader != INVALID_HANDLE_VALUE);
        REQUIRE(host.setEnabled("alice.glow", true));
        CHECK(busy == 1);
        CHECK(activeVersion(host, "alice.glow", "1.0.0"));
        CHECK(activeVersion(host, "bob.base", "1.0.0"));
        CHECK_FALSE(reports(host.problemsOf("alice.glow"), gs::ext::Code::ConsentRequired));
        CHECK(host.setEnabled("alice.glow", true));
        CHECK(busy == 1);
        ::CloseHandle(reader);
        CHECK(readFile(consent) == "{}");
        host.finish();
        CHECK(busy == 1);
    }
    const gs::ext::LoadedManifest saved = gs::ext::loadManifest(readFile(consent), {.official = true});
    CHECK(saved.usable);
    CHECK(saved.manifest.id == "alice.glow");
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    CHECK(activeVersion(host, "alice.glow", "1.0.0"));
    CHECK_FALSE(reports(host.problemsOf("alice.glow"), gs::ext::Code::ConsentRequired));
}

TEST_CASE("finding the extension behind a problem loads halves of the enabled ones without touching what the user enabled")
{
    const Fixture fixture;
    const fs::path bisectFile = fixture.data / "extensions-bisect.json";
    for (const std::string_view id : {"alice.one", "bob.two", "carol.three"}) {
        fixture.add(fixture.user, id, "1.0.0");
    }
    AppSettings settings(fixture.data);
    const auto enabledIds = [&settings] {
        std::vector<std::string> ids;
        for (const auto& [id, state] : settings.extensions()) {
            if (state.enabled) {
                ids.push_back(id);
            }
        }
        return ids;
    };
    const auto activeIds = [](const ExtensionHost& host) {
        std::vector<std::string> ids;
        for (const ExtensionHost::Extension& extension : host.extensions()) {
            if (extension.active) {
                ids.push_back(extension.manifest->id);
            }
        }
        std::ranges::sort(ids);
        return ids;
    };
    const std::vector<std::string> all{"alice.one", "bob.two", "carol.three"};
    {
        ExtensionHost host(settings, {fixture.user, {}});
        host.start(false, fixture.marker, bisectFile);
        for (const std::string& id : all) {
            REQUIRE(host.setEnabled(id, true));
        }
        REQUIRE(host.startBisect());
        CHECK_FALSE(host.startBisect());
        CHECK(enabledIds() == all);
        CHECK(activeIds(host).empty());
        CHECK(fs::exists(bisectFile));
        CHECK_FALSE(host.setEnabled("carol.three", false));
        QVariantMap state = host.bisectState();
        CHECK(state.value(QStringLiteral("active")).toBool());
        CHECK(state.value(QStringLiteral("step")).toInt() == 1);
        CHECK(state.value(QStringLiteral("steps")).toInt() == 3);
        CHECK(state.value(QStringLiteral("loaded")).toStringList().isEmpty());

        host.answerBisect(false);
        CHECK(activeIds(host) == std::vector<std::string>{"alice.one", "bob.two"});
        state = host.bisectState();
        CHECK(state.value(QStringLiteral("loaded")).toStringList() == QStringList{QStringLiteral("Glow"), QStringLiteral("Glow")});
        CHECK(fs::exists(fixture.marker));
    }

    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false, fixture.marker, bisectFile);
    CHECK(host.mode() == ExtensionHost::Mode::Normal);
    CHECK(activeIds(host) == std::vector<std::string>{"alice.one"});
    host.answerBisect(false);
    CHECK(activeIds(host).empty());
    const QVariantMap state = host.bisectState();
    CHECK(state.value(QStringLiteral("outcome")).toString() == "found");
    CHECK(state.value(QStringLiteral("found")).toMap().value(QStringLiteral("id")).toString() == "bob.two");

    SUBCASE("turning the found one off ends the search and keeps the rest")
    {
        host.concludeBisect(true);
        CHECK(enabledIds() == std::vector<std::string>{"alice.one", "carol.three"});
        CHECK(activeIds(host) == std::vector<std::string>{"alice.one", "carol.three"});
    }
    SUBCASE("stopping restores everything that was switched on")
    {
        host.stopBisect();
        CHECK(enabledIds() == all);
        CHECK(activeIds(host) == all);
    }
    CHECK_FALSE(host.bisect().has_value());
    CHECK_FALSE(host.bisectState().value(QStringLiteral("active")).toBool());
    CHECK_FALSE(fs::exists(bisectFile));
}

TEST_CASE("extension settings are read and written from QML by path, with values converted to the declared type")
{
    const Fixture fixture;
    fixture.add(fixture.user, "alice.glow", "1.0.0");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    REQUIRE(host.setEnabled("alice.glow", true));
    const QString strength = QStringLiteral("ext:alice.glow/strength");
    const QString steps = QStringLiteral("ext:alice.glow/steps");

    int changes = 0;
    QObject::connect(&host, &ExtensionHost::valuesChanged, [&changes] { ++changes; });
    CHECK(host.values().value(strength) == QVariant(0.5));
    CHECK(host.values().value(steps) == QVariant::fromValue(qint64{3}));
    CHECK(host.setValue(strength, QVariant(0.3)));
    CHECK(changes == 1);
    CHECK(host.values().value(strength) == QVariant(0.3));
    CHECK(host.setValue(steps, QVariant(2)));
    CHECK(host.setValue(steps, QVariant(4.0)));
    CHECK(host.values().value(steps) == QVariant::fromValue(qint64{4}));
    CHECK(host.setValue(strength, QVariant(1)));
    CHECK(host.values().value(strength) == QVariant(1.0));

    CHECK_FALSE(host.setValue(steps, QVariant(2.5)));
    CHECK_FALSE(host.setValue(strength, QVariant(QStringLiteral("0.3"))));
    CHECK_FALSE(host.setValue(strength, QVariant(true)));
    CHECK_FALSE(host.setValue(QStringLiteral("ext:alice.glow"), QVariant(0.3)));
    CHECK_FALSE(host.setValue(QStringLiteral("video.maxFps"), QVariant(30)));
    CHECK_FALSE(host.setValue(QStringLiteral("ext:bob.none/strength"), QVariant(0.3)));

    CHECK(host.setDeviceValue(QStringLiteral("phone"), strength, QVariant(0.9)));
    CHECK_FALSE(host.setDeviceValue(QStringLiteral("phone"), steps, QVariant(2)));
    CHECK(host.overrides().value(QStringLiteral("phone")).toMap().value(strength) == QVariant(0.9));
    CHECK(host.filters("phone").at(0).values.at("strength") == 0.9);
    host.resetDeviceValue(QStringLiteral("phone"), strength);
    CHECK(host.overrides().isEmpty());
    CHECK(host.filters("phone").at(0).values.at("strength") == 1.0);
}

TEST_CASE("extension settings join the settings catalog in the interface language and can be searched")
{
    const Fixture fixture;
    fixture.add(fixture.user, "alice.glow", "1.0.0",
                R"(, "l10n": {"zh_CN": {"name": "光晕", "settings.strength.title": "强度",
                                        "settings.strength.keywords": "亮度"}})");
    writeFile(fixture.data / "settings.json",
              R"({"format": "gamescrcpy.settings", "version": 1, "general": {"language": "zh_CN"}})");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    gs::app::SettingsCatalog catalog;
    const QString strength = QStringLiteral("ext:alice.glow/strength");

    const QVariantMap entry = catalog.entries().value(strength).toMap();
    CHECK(entry.value(QStringLiteral("title")).toString() == QStringLiteral("强度"));
    CHECK(entry.value(QStringLiteral("kind")).toString() == "number");
    CHECK(entry.value(QStringLiteral("control")).toString() == "slider");
    CHECK(entry.value(QStringLiteral("decimals")).toInt() == 2);
    CHECK(entry.value(QStringLiteral("minimum")).toDouble() == 0.0);
    CHECK(entry.value(QStringLiteral("maximum")).toDouble() == 1.0);
    CHECK(entry.value(QStringLiteral("perDevice")).toBool());
    CHECK(entry.value(QStringLiteral("page")).toString() == "extensions");
    CHECK(entry.value(QStringLiteral("extensionName")).toString() == QStringLiteral("光晕"));
    CHECK(catalog.entries().value(QStringLiteral("ext:alice.glow/steps")).toMap().value(QStringLiteral("title")) ==
          QVariant(QStringLiteral("Steps")));
    REQUIRE(catalog.extensionGroups().size() == 1);
    const QVariantMap group = catalog.extensionGroups().first().toMap();
    CHECK(group.value(QStringLiteral("name")).toString() == QStringLiteral("光晕"));
    CHECK(group.value(QStringLiteral("settings")).toList().size() == 2);
    CHECK(catalog.perDevicePages().contains(QStringLiteral("extensions")));

    catalog.setQuery(QStringLiteral("亮度"));
    CHECK(catalog.hits().contains(strength));
    CHECK(catalog.pageHits().value(QStringLiteral("extensions")).toInt() == 1);

    REQUIRE(settings.setValue(QStringLiteral("general.language"), QVariant(QStringLiteral("en"))));
    catalog.retranslate();
    CHECK(catalog.entries().value(strength).toMap().value(QStringLiteral("title")) == QVariant(QStringLiteral("Strength")));
    CHECK(catalog.extensionGroups().first().toMap().value(QStringLiteral("name")) == QVariant(QStringLiteral("Glow")));
}

TEST_CASE("a theme extension offers its palettes while it is loaded, named in the interface language")
{
    const Fixture fixture;
    writeFile(fixture.user / "alice.slate/1.0.0/manifest.json",
              R"({"schemaVersion": 1, "id": "alice.slate", "version": "1.0.0", "name": "Slate", "license": "MIT",
                  "requires": {"theme": "^1.0"},
                  "contributes": {"themes": [{"name": "Slate", "dark": {"bgBase": "#0E1116", "bgSurface": "#000000"},
                                              "light": {"bgBase": "#F2F4F8"}}]},
                  "l10n": {"zh_CN": {"themes.Slate.name": "石板"}}})");
    writeFile(fixture.data / "settings.json",
              R"({"format": "gamescrcpy.settings", "version": 1, "general": {"language": "zh_CN"}})");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    CHECK(host.themes().isEmpty());
    REQUIRE(host.setEnabled("alice.slate", true));
    INFO(listed(host.problems()));
    const std::vector<gs::ext::Diagnostic> problems = host.problemsOf("alice.slate");
    CHECK_FALSE(gs::ext::hasErrors(problems));
    CHECK(problems.size() == 1);
    CHECK(reports(problems, gs::ext::Code::ThemeTokenInvalid, R"($["alice.slate"].contributes.themes[0].dark["bgSurface"])"));
    REQUIRE(host.themes().size() == 1);
    const QVariantMap theme = host.themes().first().toMap();
    CHECK(theme.value(QStringLiteral("key")) == QVariant(QStringLiteral("alice.slate/Slate")));
    CHECK(theme.value(QStringLiteral("name")) == QVariant(QStringLiteral("石板")));
    CHECK(theme.value(QStringLiteral("dark")).toMap().value(QStringLiteral("bgBase")) == QVariant(QStringLiteral("#0E1116")));
    CHECK(theme.value(QStringLiteral("light")).toMap().value(QStringLiteral("bgBase")) ==
          QVariant(QStringLiteral("#F2F4F8")));
    REQUIRE(host.setEnabled("alice.slate", false));
    CHECK(host.themes().isEmpty());
}

TEST_CASE("the theme token names are the colors that ColorTokens.qml lets a theme override")
{
    std::ifstream in(GS_COLOR_TOKENS_FILE);
    REQUIRE(in);
    const std::regex declaration(R"(readonly property color (\w+): (.*))");
    std::vector<std::string> declared;
    for (std::string line; std::getline(in, line);) {
        std::smatch match;
        if (std::regex_search(line, match, declaration)) {
            declared.push_back(match[1].str());
            CHECK(match[2].str().starts_with("overrides." + match[1].str() + " ?? "));
        }
    }
    const std::vector<std::string> names(gs::app::kThemeTokenNames.begin(), gs::app::kThemeTokenNames.end());
    CHECK(declared == names);
}

TEST_CASE("a filter's textures are loaded as pictures, and one that is not a picture keeps the filter off with a reason")
{
    const Fixture fixture;
    const std::string shader = "//! TEXTURE Noise textures/noise.png\n" + std::string(kShader);
    fixture.add(fixture.user, "alice.glow", "1.0.0", {}, shader);
    fixture.add(fixture.user, "bob.glow", "1.0.0", {}, shader);
    fs::create_directories(fixture.user / "alice.glow/1.0.0/textures");
    QImage noise(4, 2, QImage::Format_RGBA8888);
    noise.fill(Qt::red);
    REQUIRE(noise.save(QString::fromStdWString((fixture.user / "alice.glow/1.0.0/textures/noise.png").wstring()),
                       "PNG"));
    writeFile(fixture.user / "bob.glow/1.0.0/textures/noise.png", "not a picture");

    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    REQUIRE(host.setEnabled("alice.glow", true));
    REQUIRE(host.setEnabled("bob.glow", true));
    const std::vector<gs::render::VideoFilter> filters = host.filters("");
    REQUIRE(filters.size() == 1);
    CHECK(filters[0].id == "alice.glow/Glow");
    REQUIRE(filters[0].textures.contains("Noise"));
    CHECK(filters[0].textures.at("Noise").size() == QSize(4, 2));
    CHECK(reports(host.problemsOf("bob.glow"), gs::ext::Code::TextureUnreadable,
                  R"($["bob.glow"].contributes.filters[0].file)"));
}

TEST_CASE("ADR-0112: why an extension was turned off is still shown after GameScrcpy starts again, until it is turned on")
{
    const Fixture fixture;
    writeFile(fixture.user / "alice.slate/1.0.0/manifest.json",
              R"({"schemaVersion": 1, "id": "alice.slate", "version": "1.0.0", "name": "Slate", "license": "MIT",
                  "requires": {"theme": "^1.0"},
                  "contributes": {"themes": [{"name": "Slate", "dark": {"bgBase": "#0E1116"}}]}})");
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    writeFile(fixture.data / "extension-health.json",
              R"({"format": "gamescrcpy.extension-health", "version": 1, "extensions": {"alice.slate": {"off": "stopped",
                  "events": [{"at": ")" + gs::app::ExtensionHealth::timeText(now - std::chrono::hours(1)) +
                  R"(", "version": "1.0.0", "kind": "crashed", "code": 3221225477, "report": "gs-crash-1"},
                             {"at": ")" + gs::app::ExtensionHealth::timeText(now - std::chrono::minutes(30)) +
                  R"(", "version": "1.0.0", "kind": "stopped"}]}}})");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {.user = fixture.user, .builtin = {}, .hostProgram = {}, .data = fixture.data,
                                  .log = {}});
    host.start(false);
    const auto health = [&host] {
        const QVariantList entries = host.entries();
        return entries.isEmpty() ? QVariantMap() : entries[0].toMap().value(QStringLiteral("health")).toMap();
    };
    QVariantMap shown = health();
    CHECK(shown.value(QStringLiteral("off")).toString() == "stopped");
    CHECK(shown.value(QStringLiteral("recent")).toInt() == 1);
    const QVariantList events = shown.value(QStringLiteral("events")).toList();
    REQUIRE(events.size() == 2);
    CHECK(events[0].toMap().value(QStringLiteral("kind")).toString() == "stopped");
    CHECK(events[1].toMap().value(QStringLiteral("code")).toString() == "0xC0000005");
    CHECK(events[1].toMap().value(QStringLiteral("report")).toString() == "gs-crash-1");
    CHECK(reports(host.problemsOf("alice.slate"), gs::ext::Code::NativeCrashed));

    REQUIRE(host.setEnabled("alice.slate", true));
    shown = health();
    CHECK(shown.value(QStringLiteral("off")).toString().isEmpty());
    CHECK(shown.value(QStringLiteral("events")).toList().size() == 2);
    CHECK_FALSE(reports(host.problemsOf("alice.slate"), gs::ext::Code::NativeCrashed));
    host.clearHealth(QStringLiteral("alice.slate"));
    CHECK(health().isEmpty());
    CHECK(gs::app::ExtensionHealth(fixture.data / "extension-health.json").find("alice.slate") == nullptr);
    nlohmann::json diagnostics = nlohmann::json::parse(host.diagnosticsText());
    REQUIRE(diagnostics["extensions"].size() == 1);
    CHECK(diagnostics["extensions"][0]["id"] == "alice.slate");
    CHECK(diagnostics["extensions"][0]["enabled"] == true);
    CHECK(diagnostics["health"].empty());
}

TEST_CASE("ADR-0112: the tours of an extension that is on come with their text in the language of the interface")
{
    const Fixture fixture;
    writeFile(fixture.user / "alice.tours/1.0.0/manifest.json",
              R"({"schemaVersion": 1, "id": "alice.tours", "version": "1.0.0", "name": "Tours", "license": "MIT",
                  "requires": {"tour": "^1.0"},
                  "contributes": {"tours": [{"id": "intro", "title": "Getting started", "start": "manual",
                                             "steps": [{"title": "Welcome", "text": "What it adds."},
                                                       {"target": "card", "title": "Its card",
                                                        "text": "Turn it on here."}]}]},
                  "l10n": {"zh_CN": {"tours.intro.title": "上手", "tours.intro.1.text": "在这里打开它。"}}})");
    writeFile(fixture.data / "settings.json",
              R"({"format": "gamescrcpy.settings", "version": 1, "general": {"language": "zh_CN"}})");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    INFO(listed(host.problems()));
    CHECK(host.tours().isEmpty());
    REQUIRE(host.setEnabled("alice.tours", true));
    const QVariantList tours = host.tours();
    REQUIRE(tours.size() == 1);
    const QVariantMap tour = tours[0].toMap();
    CHECK(tour.value(QStringLiteral("key")).toString() == "ext/alice.tours/intro");
    CHECK(tour.value(QStringLiteral("extension")).toString() == "alice.tours");
    CHECK(tour.value(QStringLiteral("extensionName")).toString() == "Tours");
    CHECK(tour.value(QStringLiteral("title")).toString() == QStringLiteral("上手"));
    CHECK(tour.value(QStringLiteral("start")).toString() == "manual");
    const QVariantList steps = tour.value(QStringLiteral("steps")).toList();
    REQUIRE(steps.size() == 2);
    CHECK(steps[0].toMap().value(QStringLiteral("target")).toString().isEmpty());
    CHECK(steps[0].toMap().value(QStringLiteral("text")).toString() == "What it adds.");
    CHECK(steps[1].toMap().value(QStringLiteral("target")).toString() == "card");
    CHECK(steps[1].toMap().value(QStringLiteral("title")).toString() == "Its card");
    CHECK(steps[1].toMap().value(QStringLiteral("text")).toString() == QStringLiteral("在这里打开它。"));
    REQUIRE(host.setEnabled("alice.tours", false));
    CHECK(host.tours().isEmpty());
}

TEST_CASE("ADR-0112: local connections of an extension follow its permission and its own switch in the details")
{
    const Fixture fixture;
    writeFile(fixture.user / "alice.deck/1.0.0/manifest.json",
              R"({"schemaVersion": 1, "id": "alice.deck", "version": "1.0.0", "name": "Deck", "license": "MIT",
                  "requires": {"script": "^1.0"},
                  "contributes": {"scripts": [{"name": "app", "entry": "app.js", "run": "app"}]},
                  "permissions": [{"name": "network.listen", "reason": "Stream Deck buttons."}]})");
    writeFile(fixture.user / "alice.deck/1.0.0/app.js", "log.info(\"deck\");\n");
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    INFO(listed(host.problems()));
    const auto entry = [&host] {
        const QVariantList entries = host.entries();
        return entries.isEmpty() ? QVariantMap() : entries[0].toMap();
    };
    CHECK_FALSE(host.localConnections("alice.deck"));
    CHECK_FALSE(host.setLocalConnections(QStringLiteral("alice.deck"), true));
    REQUIRE(host.setEnabled("alice.deck", true));
    CHECK(host.localConnections("alice.deck"));
    CHECK(entry().value(QStringLiteral("localConnections")).toBool());
    int changes = 0;
    QObject::connect(&host, &ExtensionHost::extensionsChanged, [&changes] { ++changes; });
    REQUIRE(host.setLocalConnections(QStringLiteral("alice.deck"), false));
    CHECK(changes == 1);
    CHECK_FALSE(host.localConnections("alice.deck"));
    CHECK(entry().contains(QStringLiteral("localConnections")));
    CHECK_FALSE(entry().value(QStringLiteral("localConnections")).toBool());
    CHECK(host.setLocalConnections(QStringLiteral("alice.deck"), false));
    CHECK(changes == 1);
    REQUIRE(host.setLocalConnections(QStringLiteral("alice.deck"), true));
    CHECK(host.localConnections("alice.deck"));
    CHECK_FALSE(host.setLocalConnections(QStringLiteral("alice.other"), true));
}
