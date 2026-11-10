#include "gs/ext/Resolution.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
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
