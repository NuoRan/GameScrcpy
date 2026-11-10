#include "TestFiles.h"

#include "gs/ext/Archive.h"
#include "gs/ext/Bundle.h"
#include "gs/ext/Package.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using namespace gs::ext;
using namespace gs::ext::test;
namespace fs = std::filesystem;

namespace {

constexpr std::string_view kShader = R"(//! PARAM strength float 0.5 0 1 0.05
//! PASS scanlines
float4 scanlines(float2 uv)
{
    return Source.Sample(Linear, uv) * (1.0 - strength);
}
)";

std::string manifest(std::string_view id, std::string_view version)
{
    return R"({"schemaVersion": 1, "id": ")" + std::string(id) + R"(", "version": ")" + std::string(version) +
           R"(", "name": "CRT", "license": "MIT", "requires": {"filter": "^1.0"},
           "contributes": {"filters": [{"name": "Scanlines", "file": "shaders/crt.hlsl"}]},
           "settings": [{"key": "strength", "type": "number", "title": "Strength", "min": 0, "max": 1, "default": 0.5}]})";
}

BundleEntry entry(std::string id, std::string_view version, bool builtin = false)
{
    BundleEntry made;
    made.id = std::move(id);
    made.version = *Version::parse(version);
    made.builtin = builtin;
    return made;
}

void stagePackage(const Scratch& scratch, const fs::path& staging, const BundleEntry& listed)
{
    const fs::path folder = scratch / ("source-" + listed.id);
    writeFile(folder / "manifest.json", manifest(listed.id, listed.version.text()));
    writeFile(folder / "shaders/crt.hlsl", kShader);
    const fs::path package = staging / pathOf(bundlePackagePath(listed));
    fs::create_directories(package.parent_path());
    REQUIRE(packDirectory(folder, package).empty());
}

}

TEST_CASE("a bundle index survives a save and a load with built-in entries and values of every kind")
{
    Bundle bundle;
    bundle.name = "Retro night";
    bundle.description = "Scanlines and a dark theme.";
    BundleEntry crt = entry("alice.crt", "1.2.0");
    crt.values.emplace("strength", 0.35);
    crt.values.emplace("lines", std::int64_t{3});
    crt.values.emplace("soft", true);
    crt.values.emplace("mode", std::string("aperture"));
    bundle.extensions.push_back(crt);
    BundleEntry theme = entry("gamescrcpy.slate-theme", "1.0.0", true);
    theme.enabled = false;
    bundle.extensions.push_back(theme);

    const LoadedBundle loaded = loadBundle(saveBundle(bundle));
    INFO(listed(loaded.diagnostics));
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    CHECK(loaded.bundle == bundle);
    CHECK(bundlePackagePath(crt) == "extensions/alice.crt-1.2.0.gsext");
}

TEST_CASE("problems in a bundle index are reported with their paths")
{
    const LoadedBundle loaded = loadBundle(R"({
        "schemaVersion": 2,
        "extensions": [
            {"id": "Not An Id", "version": "1.0.0"},
            {"id": "alice.crt", "version": "one"},
            {"id": "bob.glow", "version": "1.0.0", "builtin": "yes", "values": {"strength": [1], "": 2}, "colour": 1},
            {"id": "bob.glow", "version": "2.0.0"},
            7
        ],
        "profiles": []
    })");
    INFO(listed(loaded.diagnostics));
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded.diagnostics, Code::SchemaVersion, "$.schemaVersion"));
    CHECK(reports(loaded.diagnostics, Code::MissingField, "$.name"));
    CHECK(reports(loaded.diagnostics, Code::IdInvalid, "$.extensions[0].id"));
    CHECK(reports(loaded.diagnostics, Code::VersionInvalid, "$.extensions[1].version"));
    CHECK(reports(loaded.diagnostics, Code::WrongType, "$.extensions[2].builtin"));
    CHECK(reports(loaded.diagnostics, Code::WrongType, R"($.extensions[2].values["strength"])"));
    CHECK(reports(loaded.diagnostics, Code::SettingKeyInvalid, R"($.extensions[2].values[""])"));
    CHECK(reports(loaded.diagnostics, Code::UnknownField, R"($.extensions[2]["colour"])"));
    CHECK(reports(loaded.diagnostics, Code::Duplicate, "$.extensions[3].id"));
    CHECK(reports(loaded.diagnostics, Code::NotAnObject, "$.extensions[4]"));
    CHECK(reports(loaded.diagnostics, Code::UnknownField, R"($["profiles"])"));
    REQUIRE(loaded.bundle.extensions.size() == 1);
    CHECK(loaded.bundle.extensions.front().id == "bob.glow");

    CHECK(reports(loadBundle("not json").diagnostics, Code::MalformedJson, "$"));
    CHECK(reports(loadBundle("[]").diagnostics, Code::NotAnObject, "$"));
    CHECK(reports(loadBundle(R"({"schemaVersion": 1, "name": "x"})").diagnostics, Code::MissingField, "$.extensions"));
}

TEST_CASE("checking a bundle finds its index and a package for every extension that is not built in")
{
    const Scratch scratch;
    const fs::path staging = scratch / "staging";
    Bundle bundle;
    bundle.name = "Retro night";
    const BundleEntry crt = entry("alice.crt", "1.2.0");
    stagePackage(scratch, staging, crt);
    bundle.extensions = {crt, entry("bob.glow", "1.0.0"), entry("gamescrcpy.slate-theme", "1.0.0", true)};
    const fs::path archive = scratch / "retro.gsbundle";
    REQUIRE(packBundle(bundle, staging, archive).empty());

    const BundleCheck check = checkBundle(archive);
    INFO(listed(check.diagnostics));
    REQUIRE(check.bundle.has_value());
    CHECK(*check.bundle == bundle);
    CHECK_FALSE(check.ok());
    CHECK(reports(check.diagnostics, Code::BundlePackageMissing, "extensions/bob.glow-1.0.0.gsext"));
    CHECK_FALSE(reports(check.diagnostics, Code::BundlePackageMissing, "extensions/gamescrcpy.slate-theme-1.0.0.gsext"));

    const fs::path inner = scratch / "out/alice.crt-1.2.0.gsext";
    REQUIRE(extractBundlePackage(archive, crt, inner).empty());
    const PackageCheck package = checkPackage(inner);
    INFO(listed(package.diagnostics));
    CHECK(package.ok());
    CHECK(package.manifest->id == "alice.crt");
    CHECK(reports(extractBundlePackage(archive, bundle.extensions[1], scratch / "out/bob.gsext"),
                  Code::BundlePackageMissing));
}

TEST_CASE("an archive without a bundle index is not a bundle")
{
    const Scratch scratch;
    writeFile(scratch / "plain/readme.txt", "hello");
    const fs::path archive = scratch / "plain.gsbundle";
    REQUIRE(packDirectory(scratch / "plain", archive).empty());
    const BundleCheck check = checkBundle(archive);
    CHECK_FALSE(check.bundle.has_value());
    CHECK(reports(check.diagnostics, Code::BundleIndexMissing, "$"));
}
