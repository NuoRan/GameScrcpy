#include "gs/ext/Resolution.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace gs::ext;

namespace {

Manifest extension(const char* id, const char* version)
{
    Manifest manifest;
    manifest.id = id;
    manifest.version = *Version::parse(version);
    manifest.name = id;
    manifest.license = "MIT";
    manifest.requirements.emplace(ExtensionPoint::Filter, *VersionRange::parse("^1"));
    return manifest;
}

void needs(Manifest& manifest, const char* id, const char* range, bool optional = false)
{
    manifest.dependencies.insert_or_assign(id, Dependency{*VersionRange::parse(range), optional});
}

std::string order(const Resolution& resolution)
{
    std::string text;
    for (const Selected& selected : resolution.order) {
        text += (text.empty() ? "" : " ") + selected.id + "@" + selected.version.text();
    }
    return text;
}

bool reports(const Resolution& resolution, Code code, std::string_view path)
{
    return std::ranges::any_of(resolution.problems, [&](const Diagnostic& diagnostic) {
        return diagnostic.code == code && diagnostic.path == path;
    });
}

std::vector<Diagnostic> against(const char* range, const char* offered)
{
    Manifest manifest = extension("alice.app", "1.0.0");
    manifest.requirements.clear();
    manifest.requirements.insert_or_assign(ExtensionPoint::Script, *VersionRange::parse(range));
    HostApi host;
    host.versions[ExtensionPoint::Script] = {*Version::parse(offered)};
    return checkCompatibility(manifest, host);
}

Code verdict(const char* range, const char* offered)
{
    const std::vector<Diagnostic> problems = against(range, offered);
    REQUIRE(problems.size() == 1);
    return problems.front().code;
}

}

TEST_CASE("dependencies load first, ties go in id order, and required dependencies are pulled in")
{
    Manifest app = extension("alice.app", "1.0.0");
    needs(app, "bob.lib", "^2.0");
    Manifest lib = extension("bob.lib", "2.3.0");
    needs(lib, "carol.base", "~1.4");
    const Manifest base = extension("carol.base", "1.4.2");
    const Manifest solo = extension("aaron.solo", "0.1.0");
    const Resolution resolution = resolveDependencies({app, lib, base, solo}, {"alice.app", "aaron.solo"});
    CHECK(resolution.ok());
    CHECK(resolution.problems.empty());
    CHECK(order(resolution) == "aaron.solo@0.1.0 carol.base@1.4.2 bob.lib@2.3.0 alice.app@1.0.0");
}

TEST_CASE("the newest installed version that every dependent accepts is chosen")
{
    Manifest app = extension("alice.app", "1.0.0");
    needs(app, "bob.lib", ">=2.0 <2.5");
    const Manifest old = extension("bob.lib", "2.1.0");
    const Manifest middle = extension("bob.lib", "2.4.9");
    const Manifest newest = extension("bob.lib", "3.0.0");
    const Resolution resolution = resolveDependencies({old, newest, app, middle}, {"alice.app"});
    CHECK(resolution.ok());
    CHECK(order(resolution) == "bob.lib@2.4.9 alice.app@1.0.0");
}

TEST_CASE("missing, unsatisfiable, conflicting and cyclic dependencies are reported, and keep their dependents out")
{
    Manifest app = extension("alice.app", "1.0.0");
    needs(app, "bob.lib", "^2.0");
    Manifest viewer = extension("dan.viewer", "1.0.0");
    needs(viewer, "bob.lib", "^1.0");
    const Manifest lib1 = extension("bob.lib", "1.5.0");
    const Manifest lib2 = extension("bob.lib", "2.0.0");
    Manifest lonely = extension("erin.lonely", "1.0.0");
    needs(lonely, "zed.gone", "^1");
    Manifest picky = extension("fay.picky", "1.0.0");
    needs(picky, "bob.lib", "^9");
    Manifest ping = extension("gus.ping", "1.0.0");
    needs(ping, "hal.pong", "^1");
    Manifest pong = extension("hal.pong", "1.0.0");
    needs(pong, "gus.ping", "^1");
    Manifest user = extension("ivy.user", "1.0.0");
    needs(user, "erin.lonely", "^1");
    const Resolution resolution =
        resolveDependencies({app, viewer, lib1, lib2, lonely, picky, ping, pong, user},
                            {"alice.app", "dan.viewer", "erin.lonely", "fay.picky", "gus.ping", "ivy.user", "jan.absent"});
    CHECK_FALSE(resolution.ok());
    CHECK(reports(resolution, Code::DependencyMissing, R"($["jan.absent"])"));
    CHECK(reports(resolution, Code::DependencyMissing, R"($["erin.lonely"].dependencies["zed.gone"])"));
    CHECK(reports(resolution, Code::DependencyVersion, R"($["fay.picky"].dependencies["bob.lib"])"));
    CHECK(reports(resolution, Code::DependencyConflict, R"($["bob.lib"])"));
    CHECK(reports(resolution, Code::DependencyCycle, R"($["gus.ping"])"));
    CHECK(reports(resolution, Code::DependencyCycle, R"($["hal.pong"])"));
    CHECK(order(resolution) == "bob.lib@2.0.0 alice.app@1.0.0");
}

TEST_CASE("a missing optional dependency is only a notice, and an enabled one still loads first")
{
    Manifest app = extension("alice.app", "1.0.0");
    needs(app, "bob.extras", "^1", true);
    needs(app, "carol.colors", "^2", true);
    const Manifest colors = extension("carol.colors", "2.1.0");
    const Resolution resolution = resolveDependencies({app, colors}, {"alice.app", "carol.colors"});
    CHECK(resolution.ok());
    CHECK(reports(resolution, Code::DependencyOptionalMissing, R"($["alice.app"].dependencies["bob.extras"])"));
    REQUIRE(resolution.problems.size() == 1);
    CHECK(resolution.problems.front().severity == Severity::Notice);
    CHECK(order(resolution) == "carol.colors@2.1.0 alice.app@1.0.0");
    CHECK(order(resolveDependencies({app, colors}, {"alice.app"})) == "alice.app@1.0.0");
}

TEST_CASE("an extension needs every extension point it declares, in a version the host offers")
{
    Manifest manifest = extension("alice.app", "1.0.0");
    manifest.requirements.insert_or_assign(ExtensionPoint::Theme, *VersionRange::parse("^2.0"));
    manifest.requirements.insert_or_assign(ExtensionPoint::Script, *VersionRange::parse("^1.0"));
    HostApi host;
    host.versions[ExtensionPoint::Filter] = {*Version::parse("1.3.0")};
    host.versions[ExtensionPoint::Theme] = {*Version::parse("1.0.0"), *Version::parse("3.0.0")};
    const std::vector<Diagnostic> problems = checkCompatibility(manifest, host);
    REQUIRE(problems.size() == 2);
    CHECK(problems[0].code == Code::HostApiVersion);
    CHECK(problems[0].path == R"($.requires["theme"])");
    CHECK(problems[1].code == Code::HostApiMissing);
    CHECK(problems[1].path == R"($.requires["script"])");

    host.versions[ExtensionPoint::Theme].push_back(*Version::parse("2.4.0"));
    host.versions[ExtensionPoint::Script] = {*Version::parse("1.0.0")};
    CHECK(checkCompatibility(manifest, host).empty());
}

TEST_CASE("ADR-0113: a manifest made for an older GameScrcpy is outdated, one for a newer one needs a newer GameScrcpy")
{
    CHECK(verdict("^1.0", "2.0.0") == Code::HostApiOutdated);
    CHECK(verdict("~1.2", "1.3.0") == Code::HostApiOutdated);
    CHECK(verdict("1.2.3", "1.3.0") == Code::HostApiOutdated);
    CHECK(verdict(">=1.0 <2.0", "2.0.0") == Code::HostApiOutdated);
    CHECK(verdict(">=1.0 <2.0 <1.5", "1.6.0") == Code::HostApiOutdated);

    CHECK(verdict("^2.0", "1.0.0") == Code::HostApiVersion);
    CHECK(verdict("~1.2", "1.1.0") == Code::HostApiVersion);
    CHECK(verdict("1.2.3", "1.0.0") == Code::HostApiVersion);
    CHECK(verdict(">=2.0 <3", "1.0.0") == Code::HostApiVersion);
    CHECK(verdict(">=2.0", "1.0.0") == Code::HostApiVersion);
    CHECK(verdict("^1.0", "1.0.0-rc.1") == Code::HostApiVersion);

    CHECK(against("^1.0", "1.5.0").empty());
    CHECK(against(">=1.0", "9.0.0").empty());
    CHECK(against("*", "9.0.0").empty());
    CHECK(against("1.2.3", "1.2.3").empty());

    const std::vector<Diagnostic> problems = against("^1.0", "2.0.0");
    REQUIRE(problems.size() == 1);
    CHECK(problems.front().severity == Severity::Error);
    CHECK(problems.front().path == R"($.requires["script"])");
    CHECK(problems.front().detail == "^1.0 wanted, offered 2.0.0");
    CHECK(codeName(Code::HostApiOutdated) == "host-api-outdated");
    CHECK(std::string(fixHint(Code::HostApiOutdated)).find("\"native\": \"^2.0\"") != std::string::npos);
}

TEST_CASE("ADR-0113: the versions GameScrcpy offers")
{
    const HostApi& host = currentHostApi();
    const auto offered = [&host](ExtensionPoint point) {
        const auto found = host.versions.find(point);
        REQUIRE(found != host.versions.end());
        REQUIRE(found->second.size() == 1);
        return found->second.front().text();
    };
    CHECK(offered(ExtensionPoint::Filter) == "1.0.0");
    CHECK(offered(ExtensionPoint::Theme) == "1.0.0");
    CHECK(offered(ExtensionPoint::Profile) == "1.0.0");
    CHECK(offered(ExtensionPoint::Script) == "1.1.0");
    CHECK(offered(ExtensionPoint::Component) == "1.1.0");
    CHECK(offered(ExtensionPoint::Task) == "1.1.0");
    CHECK(offered(ExtensionPoint::Panel) == "1.1.0");
    CHECK(offered(ExtensionPoint::Native) == "2.0.0");
    CHECK(offered(ExtensionPoint::Help) == "1.0.0");
    CHECK(offered(ExtensionPoint::Tour) == "1.0.0");
    CHECK(offered(ExtensionPoint::Locale) == "1.0.0");
    CHECK(host.versions.size() == 11);
    CHECK_FALSE(host.versions.contains(ExtensionPoint::Command));
    CHECK_FALSE(host.versions.contains(ExtensionPoint::Preset));

    Manifest native = extension("bob.native", "1.0.0");
    native.requirements.clear();
    native.requirements.insert_or_assign(ExtensionPoint::Native, *VersionRange::parse("^1.0"));
    const std::vector<Diagnostic> problems = checkCompatibility(native, host);
    REQUIRE(problems.size() == 1);
    CHECK(problems.front().code == Code::HostApiOutdated);
    CHECK(problems.front().detail == "^1.0 wanted, offered 2.0.0");

    native.requirements.insert_or_assign(ExtensionPoint::Native, *VersionRange::parse("^2.0"));
    CHECK(checkCompatibility(native, host).empty());
}

TEST_CASE("ADR-0113: an author tool writes the version this GameScrcpy offers as a caret range")
{
    const std::optional<VersionRange> native = requirementFor(ExtensionPoint::Native);
    REQUIRE(native.has_value());
    CHECK(native->text() == "^2.0");
    CHECK(native->contains(*Version::parse("2.0.0")));
    CHECK_FALSE(native->contains(*Version::parse("3.0.0")));

    const std::optional<VersionRange> script = requirementFor(ExtensionPoint::Script);
    REQUIRE(script.has_value());
    CHECK(script->text() == "^1.1");

    const std::optional<VersionRange> help = requirementFor(ExtensionPoint::Help);
    REQUIRE(help.has_value());
    CHECK(help->text() == "^1.0");

    CHECK_FALSE(requirementFor(ExtensionPoint::Command).has_value());
    CHECK_FALSE(requirementFor(ExtensionPoint::Preset).has_value());
}

TEST_CASE("an update names the permissions it adds or widens and the dependencies it adds")
{
    Manifest before = extension("alice.app", "1.0.0");
    before.requirements.insert_or_assign(ExtensionPoint::Script, *VersionRange::parse("^1.0"));
    before.permissions = {Permission{.name = "network", .reason = "r", .hosts = {"*.example.com"}},
                          Permission{.name = "devices.other", .reason = "r", .maxDevices = 2},
                          Permission{.name = "notify", .reason = "r"}};
    needs(before, "bob.lib", "^1");
    needs(before, "carol.extras", "^1", true);

    Manifest after = before;
    after.version = *Version::parse("1.1.0");
    CHECK_FALSE(compareForUpdate(before, after).needsConsent());
    after.permissions[0].hosts = {"api.example.com", "*.example.com"};
    after.permissions[2].reason = "A new reason.";
    CHECK_FALSE(compareForUpdate(before, after).needsConsent());

    after.permissions[0].hosts.push_back("other.org");
    after.permissions[1].maxDevices = 5;
    after.permissions.push_back(Permission{.name = "files.pick", .reason = "r"});
    needs(after, "carol.extras", "^1");
    needs(after, "dan.new", "^1", true);
    needs(after, "erin.new", "^2");
    after.requirements.insert_or_assign(ExtensionPoint::Script, *VersionRange::parse("^2.0"));
    after.requirements.erase(ExtensionPoint::Filter);
    const UpdateChanges changes = compareForUpdate(before, after);
    CHECK(changes.needsConsent());
    std::vector<std::string> names;
    for (const Permission& permission : changes.addedPermissions) {
        names.push_back(permission.name);
    }
    CHECK(names == std::vector<std::string>{"network", "devices.other", "files.pick"});
    CHECK(changes.addedDependencies == std::vector<std::string>{"carol.extras", "erin.new"});
    CHECK(changes.changedRequirements == std::vector<ExtensionPoint>{ExtensionPoint::Filter, ExtensionPoint::Script});

    Manifest unlimited = before;
    unlimited.permissions[1].maxDevices.reset();
    CHECK(compareForUpdate(before, unlimited).addedPermissions.size() == 1);
}

TEST_CASE("ADR-0112: an update that names a new folder under files.paths asks again, other folder changes do not")
{
    Manifest before = extension("alice.app", "1.0.0");
    before.requirements.insert_or_assign(ExtensionPoint::Script, *VersionRange::parse("^1.0"));
    before.permissions = {Permission{.name = "files.paths",
                                     .reason = "r",
                                     .folders = {{.name = "exports", .purpose = "Where it saves."},
                                                 {.name = "saves", .purpose = "Where it keeps saves."}}}};

    Manifest after = before;
    after.version = *Version::parse("1.1.0");
    after.permissions[0].folders[0].purpose = "Where it saves your recordings.";
    std::swap(after.permissions[0].folders[0], after.permissions[0].folders[1]);
    CHECK_FALSE(compareForUpdate(before, after).needsConsent());
    after.permissions[0].folders.pop_back();
    CHECK_FALSE(compareForUpdate(before, after).needsConsent());

    after.permissions[0].folders.push_back({.name = "photos", .purpose = "Where it reads photos."});
    const UpdateChanges changes = compareForUpdate(before, after);
    CHECK(changes.needsConsent());
    REQUIRE(changes.addedPermissions.size() == 1);
    CHECK(changes.addedPermissions.front().name == "files.paths");
}

TEST_CASE("ADR-0112: an update also names the permissions and dependencies it drops, which need no consent")
{
    Manifest before = extension("alice.app", "1.0.0");
    before.permissions = {Permission{.name = "notify", .reason = "r"}, Permission{.name = "files.pick", .reason = "r"}};
    needs(before, "bob.lib", "^1");
    needs(before, "carol.extras", "^1");
    needs(before, "dan.optional", "^1", true);

    Manifest after = before;
    after.version = *Version::parse("2.0.0");
    after.permissions.erase(after.permissions.begin());
    after.dependencies.erase("bob.lib");
    needs(after, "carol.extras", "^1", true);
    after.dependencies.erase("dan.optional");
    const UpdateChanges changes = compareForUpdate(before, after);
    CHECK_FALSE(changes.needsConsent());
    CHECK(changes.addedPermissions.empty());
    REQUIRE(changes.removedPermissions.size() == 1);
    CHECK(changes.removedPermissions.front().name == "notify");
    CHECK(changes.addedDependencies.empty());
    CHECK(changes.removedDependencies == std::vector<std::string>{"bob.lib", "carol.extras"});
}
