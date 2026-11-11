#include "TestFiles.h"

#include "gs/ext/Archive.h"
#include "gs/ext/Package.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using namespace gs::ext;
using namespace gs::ext::test;
namespace fs = std::filesystem;

namespace {

constexpr std::string_view kSetting =
    R"({"key": "strength", "type": "number", "title": "Strength", "min": 0, "max": 1, "default": 0.5})";

const std::string kManifest = R"({
    "schemaVersion": 1,
    "id": "alice.crt",
    "version": "1.2.0",
    "name": "CRT",
    "license": "MIT",
    "requires": {"filter": "^1.0"},
    "contributes": {"filters": [{"name": "Scanlines", "file": "shaders/crt.hlsl"}]},
    "settings": [)" + std::string(kSetting) + R"(]
})";

constexpr std::string_view kShader = R"(//! PARAM strength float 0.5 0 1 0.05
//! PASS scanlines
float4 scanlines(float2 uv)
{
    return Source.Sample(Linear, uv) * (1.0 - strength);
}
)";

std::string replaced(std::string_view text, std::string_view from, std::string_view to)
{
    std::string result(text);
    const std::size_t at = result.find(from);
    REQUIRE(at != std::string::npos);
    result.replace(at, from.size(), to);
    return result;
}

void writeExtension(const fs::path& folder, std::string_view manifest = kManifest, std::string_view shader = kShader)
{
    writeFile(folder / "manifest.json", manifest);
    writeFile(folder / "shaders/crt.hlsl", shader);
}

fs::path packed(const Scratch& scratch, std::string_view name, std::string_view manifest = kManifest,
                std::string_view shader = kShader)
{
    const fs::path folder = scratch / name;
    writeExtension(folder, manifest, shader);
    const fs::path archive = scratch / (std::string(name) + ".gsext");
    REQUIRE(packDirectory(folder, archive).empty());
    return archive;
}

bool emptyOrMissing(const fs::path& folder)
{
    std::error_code error;
    return !fs::exists(folder, error) || fs::is_empty(folder, error);
}

}

TEST_CASE("the example extensions pass the package check as official extensions, as a folder and as a package")
{
    Scratch scratch;
    std::size_t count = 0;
    std::size_t reserved = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(fs::path(GS_EXAMPLES_DIR) / "extensions")) {
        CAPTURE(entry.path().string());
        if (fs::exists(entry.path() / "CMakeLists.txt")) {
            continue;
        }
        ++count;
        const PackageCheck folder = checkDirectory(entry.path(), {.official = true});
        INFO(listed(folder.diagnostics));
        CHECK(folder.ok());
        const PackageCheck developer = checkDirectory(entry.path());
        INFO(listed(developer.diagnostics));
        if (folder.manifest && folder.manifest->id.starts_with(kReservedIdPrefix)) {
            ++reserved;
            CHECK(reports(developer.diagnostics, Code::IdReserved));
        } else {
            CHECK(developer.ok());
        }
        const fs::path archive = scratch / (entry.path().filename().string() + ".gsext");
        REQUIRE(packDirectory(entry.path(), archive).empty());
        const PackageCheck package = checkPackage(archive, {.official = true});
        CHECK(package.ok());
        CHECK(package.manifest == folder.manifest);
    }
    CHECK(count >= 1);
    CHECK(reserved >= 1);
}

TEST_CASE("a folder and its package report the same problems at the same places")
{
    struct Case {
        const char* label;
        std::string manifest;
        std::string shader;
        Code code;
        std::string path;
    };
    const std::string shader(kShader);
    const std::vector<Case> cases{
        {"missing filter file", replaced(kManifest, "shaders/crt.hlsl", "shaders/gone.hlsl"), shader,
         Code::FileMissing, "$.contributes.filters[0].file"},
        {"file name in another case", replaced(kManifest, "shaders/crt.hlsl", "shaders/CRT.hlsl"), shader,
         Code::FileMissing, "$.contributes.filters[0].file"},
        {"bad directive", kManifest, replaced(kShader, "//! PASS scanlines", "//! PASS"),
         Code::FilterDirectiveInvalid, "shaders/crt.hlsl:2"},
        {"missing pass", kManifest, replaced(kShader, "//! PASS scanlines", ""), Code::FilterPassMissing,
         "shaders/crt.hlsl"},
        {"missing texture", kManifest, "//! TEXTURE Noise textures/noise.png\n" + shader, Code::FileMissing,
         "shaders/crt.hlsl:1"},
        {"setting of another type",
         replaced(kManifest, kSetting, R"({"key": "strength", "type": "boolean", "title": "Strength", "default": true})"),
         shader, Code::FilterSettingMismatch, "$.settings[0].type"},
    };
    for (const Case& item : cases) {
        CAPTURE(item.label);
        Scratch scratch;
        writeExtension(scratch / "folder", item.manifest, item.shader);
        const PackageCheck folder = checkDirectory(scratch / "folder");
        INFO(listed(folder.diagnostics));
        CHECK_FALSE(folder.ok());
        CHECK(reports(folder.diagnostics, item.code, item.path));
        const fs::path archive = scratch / "package.gsext";
        REQUIRE(packDirectory(scratch / "folder", archive).empty());
        CHECK(checkPackage(archive).diagnostics == folder.diagnostics);
    }
}

TEST_CASE("a native extension needs its DLL in the folder and in the package, with the name in the right case "
          "(ADR-0097)")
{
    const std::string manifest = R"({
        "schemaVersion": 1,
        "id": "alice.clock",
        "version": "1.0.0",
        "name": "Clock",
        "license": "MIT",
        "requires": {"native": "^1.0"},
        "contributes": {"native": {"dll": "bin/clock.dll", "nodes": [{"name": "now", "returns": "string"}]}}
    })";
    Scratch scratch;
    writeFile(scratch / "folder/manifest.json", manifest);
    PackageCheck folder = checkDirectory(scratch / "folder");
    INFO(listed(folder.diagnostics));
    CHECK_FALSE(folder.ok());
    CHECK(reports(folder.diagnostics, Code::FileMissing, "$.contributes.native.dll"));
    REQUIRE(packDirectory(scratch / "folder", scratch / "missing.gsext").empty());
    CHECK(checkPackage(scratch / "missing.gsext").diagnostics == folder.diagnostics);

    writeFile(scratch / "folder/bin/Clock.dll", "MZ");
    CHECK(reports(checkDirectory(scratch / "folder").diagnostics, Code::FileMissing, "$.contributes.native.dll"));

    fs::remove(scratch / "folder/bin/Clock.dll");
    writeFile(scratch / "folder/bin/clock.dll", "MZ");
    folder = checkDirectory(scratch / "folder");
    CHECK(folder.ok());
    REQUIRE(packDirectory(scratch / "folder", scratch / "clock.gsext").empty());
    const PackageCheck package = checkPackage(scratch / "clock.gsext");
    CHECK(package.ok());
    REQUIRE(package.manifest.has_value());
    REQUIRE(package.manifest->contributes.native.has_value());
    CHECK(package.manifest->contributes.native->dll == "bin/clock.dll");
}

TEST_CASE("an extension's scripts need their entry files in the folder and in the package (ADR-0105)")
{
    const std::string manifest = R"({
        "schemaVersion": 1,
        "id": "alice.helper",
        "version": "1.0.0",
        "name": "Helper",
        "license": "MIT",
        "requires": {"script": "^1.0"},
        "contributes": {"scripts": [{"name": "helper", "entry": "scripts/helper.ts", "run": "session"},
                                    {"name": "tools", "entry": "tools.js", "run": "app"}]}
    })";
    Scratch scratch;
    writeFile(scratch / "folder/manifest.json", manifest);
    writeFile(scratch / "folder/tools.js", "export default {};");
    PackageCheck folder = checkDirectory(scratch / "folder");
    INFO(listed(folder.diagnostics));
    CHECK_FALSE(folder.ok());
    CHECK(reports(folder.diagnostics, Code::FileMissing, "$.contributes.scripts[0].entry"));
    CHECK_FALSE(reports(folder.diagnostics, Code::FileMissing, "$.contributes.scripts[1].entry"));
    REQUIRE(packDirectory(scratch / "folder", scratch / "missing.gsext").empty());
    CHECK(checkPackage(scratch / "missing.gsext").diagnostics == folder.diagnostics);

    writeFile(scratch / "folder/scripts/helper.ts", "export default async function () {}");
    folder = checkDirectory(scratch / "folder");
    CHECK(folder.ok());
    REQUIRE(folder.manifest.has_value());
    CHECK(folder.manifest->contributes.scripts.size() == 2);
}

TEST_CASE("ADR-0112: an extension's converters need their script files in the folder and in the package")
{
    const std::string manifest = R"({
        "schemaVersion": 1,
        "id": "alice.formats",
        "version": "1.0.0",
        "name": "Formats",
        "license": "MIT",
        "requires": {"script": "^1.0"},
        "contributes": {"converters": [{"name": "xyz-in", "title": "XYZ", "from": "xyz", "to": "profile",
                                        "script": "convert/xyz.ts"},
                                       {"name": "xyz-out", "title": "XYZ", "from": "profile", "to": "xyz",
                                        "script": "out.js"}]}
    })";
    Scratch scratch;
    writeFile(scratch / "folder/manifest.json", manifest);
    writeFile(scratch / "folder/out.js", "export default (input) => input.text;");
    PackageCheck folder = checkDirectory(scratch / "folder");
    INFO(listed(folder.diagnostics));
    CHECK_FALSE(folder.ok());
    CHECK(reports(folder.diagnostics, Code::FileMissing, "$.contributes.converters[0].script"));
    CHECK_FALSE(reports(folder.diagnostics, Code::FileMissing, "$.contributes.converters[1].script"));
    REQUIRE(packDirectory(scratch / "folder", scratch / "missing.gsext").empty());
    CHECK(checkPackage(scratch / "missing.gsext").diagnostics == folder.diagnostics);

    writeFile(scratch / "folder/convert/xyz.ts", "export function convert(input) { return input.text; }");
    folder = checkDirectory(scratch / "folder");
    CHECK(folder.ok());
    REQUIRE(folder.manifest.has_value());
    CHECK(folder.manifest->contributes.converters.size() == 2);
}

TEST_CASE("a package without a manifest is reported at the manifest file")
{
    Scratch scratch;
    writeFile(scratch / "folder/shaders/crt.hlsl", kShader);
    CHECK(reports(checkDirectory(scratch / "folder").diagnostics, Code::ManifestMissing, "manifest.json"));
    REQUIRE(packDirectory(scratch / "folder", scratch / "package.gsext").empty());
    CHECK(reports(checkPackage(scratch / "package.gsext").diagnostics, Code::ManifestMissing, "manifest.json"));
    CHECK(reports(checkDirectory(scratch / "nowhere").diagnostics, Code::ArchiveUnreadable));
}

TEST_CASE("files and folders whose names start with a dot are neither packed nor checked")
{
    Scratch scratch;
    writeExtension(scratch / "folder");
    writeFile(scratch / "folder/.git/config", "[core]");
    writeFile(scratch / "folder/.hidden", "x");
    writeFile(scratch / "folder/shaders/.cache/x.bin", "x");
    const ArchiveListing listing = inspectDirectory(scratch / "folder");
    CHECK(listing.ok());
    CHECK(listing.files ==
          std::vector<ArchiveEntry>{{"manifest.json", kManifest.size()}, {"shaders/crt.hlsl", kShader.size()}});
    REQUIRE(packDirectory(scratch / "folder", scratch / "package.gsext").empty());
    CHECK(inspectArchive(scratch / "package.gsext").files == listing.files);
}

TEST_CASE("installing puts the extension under its id and version and replaces the same version")
{
    Scratch scratch;
    const fs::path root = scratch / "extensions";
    const InstalledExtension first = installPackage(packed(scratch, "first"), root);
    INFO(listed(first.diagnostics));
    REQUIRE(first.ok());
    CHECK(first.directory == root / "alice.crt" / "1.2.0");
    CHECK(first.directory == installedDirectory(root, "alice.crt", *Version::parse("1.2.0")));
    CHECK(first.manifest->name == "CRT");
    CHECK(readFile(first.directory / "shaders/crt.hlsl") == kShader);
    CHECK(emptyOrMissing(root / ".staging"));

    const std::string brighter = replaced(kShader, "1.0 - strength", "1.0 + strength");
    const InstalledExtension second = installPackage(packed(scratch, "second", kManifest, brighter), root);
    REQUIRE(second.ok());
    CHECK(second.directory == first.directory);
    CHECK(readFile(second.directory / "shaders/crt.hlsl") == brighter);
    CHECK(emptyOrMissing(root / ".staging"));
}

TEST_CASE("a failed install leaves the installed version as it was")
{
    Scratch scratch;
    const fs::path root = scratch / "extensions";
    REQUIRE(installPackage(packed(scratch, "good"), root).ok());

    const InstalledExtension broken =
        installPackage(packed(scratch, "broken", kManifest, replaced(kShader, "//! PASS scanlines", "//! PASS")), root);
    CHECK_FALSE(broken.ok());
    CHECK(reports(broken.diagnostics, Code::FilterDirectiveInvalid, "shaders/crt.hlsl:2"));
    CHECK(broken.directory.empty());
    CHECK(readFile(root / "alice.crt/1.2.0/shaders/crt.hlsl") == kShader);
    CHECK(emptyOrMissing(root / ".staging"));

    CHECK(reports(installPackage(scratch / "missing.gsext", root).diagnostics, Code::ArchiveUnreadable));
    CHECK(emptyOrMissing(root / ".staging"));
}

TEST_CASE("a package with an official id installs only from an official source")
{
    Scratch scratch;
    const fs::path root = scratch / "extensions";
    const fs::path archive = packed(scratch, "official", replaced(kManifest, "alice.crt", "gamescrcpy.crt"));
    CHECK(reports(installPackage(archive, root).diagnostics, Code::IdReserved));
    CHECK_FALSE(fs::exists(root / "gamescrcpy.crt"));
    CHECK(installPackage(archive, root, {.official = true}).ok());
    CHECK(fs::exists(root / "gamescrcpy.crt/1.2.0/manifest.json"));
}

TEST_CASE("the installed list covers every version folder and flags folders that do not match their manifest")
{
    Scratch scratch;
    const fs::path root = scratch / "extensions";
    REQUIRE(installPackage(packed(scratch, "one"), root).ok());
    writeExtension(root / "alice.crt/9.9.9");
    writeFile(root / "bob.empty/1.0.0/readme.txt", "no manifest");
    writeFile(root / ".staging/leftover/manifest.json", "{}");
    writeFile(root / "notes.txt", "not a folder");

    const std::vector<InstalledExtension> installed = scanInstalled(root);
    REQUIRE(installed.size() == 3);
    CHECK(installed[0].directory == root / "alice.crt" / "1.2.0");
    CHECK(installed[0].ok());
    CHECK(installed[1].directory == root / "alice.crt" / "9.9.9");
    CHECK(reports(installed[1].diagnostics, Code::InstallMismatch, "alice.crt/9.9.9"));
    CHECK_FALSE(installed[1].ok());
    CHECK(installed[2].directory == root / "bob.empty" / "1.0.0");
    CHECK(reports(installed[2].diagnostics, Code::ManifestMissing, "manifest.json"));
    CHECK(scanInstalled(scratch / "nowhere").empty());
}

TEST_CASE("removing an installed version deletes its folder, and the id folder once it is empty")
{
    Scratch scratch;
    const fs::path root = scratch / "extensions";
    REQUIRE(installPackage(packed(scratch, "one"), root).ok());
    const Version version = *Version::parse("1.2.0");
    CHECK(removeInstalled(root, "alice.crt", version));
    CHECK_FALSE(fs::exists(root / "alice.crt"));
    CHECK_FALSE(removeInstalled(root, "alice.crt", version));

    for (const char* id : {"..", ".", "a/b", "a\\b", ".staging", "", "con"}) {
        CAPTURE(id);
        CHECK(installedDirectory(root, id, version).empty());
        CHECK_FALSE(removeInstalled(root, id, version));
    }
    CHECK(fs::exists(root));
}

TEST_CASE("leftovers of interrupted installs are cleared")
{
    Scratch scratch;
    const fs::path root = scratch / "extensions";
    writeFile(root / ".staging/0123/manifest.json", "{}");
    REQUIRE(installPackage(packed(scratch, "kept"), root).ok());
    clearStaging(root);
    CHECK_FALSE(fs::exists(root / ".staging"));
    CHECK(fs::exists(root / "alice.crt/1.2.0/manifest.json"));
}

TEST_CASE("an update killed after moving the old version out gets the old version back at the next start")
{
    Scratch scratch;
    const fs::path root = scratch / "extensions";
    REQUIRE(installPackage(packed(scratch, "old"), root).ok());
    fs::create_directories(root / ".staging");
    fs::rename(root / "alice.crt/1.2.0", root / ".staging/0123456789abcdef");
    writeFile(root / ".staging/0123456789abcdef.restore", "alice.crt/1.2.0");
    writeFile(root / ".staging/fedcba9876543210/manifest.json", "{}");
    clearStaging(root);
    CHECK_FALSE(fs::exists(root / ".staging"));
    CHECK(readFile(root / "alice.crt/1.2.0/shaders/crt.hlsl") == kShader);
    CHECK(scanInstalled(root).size() == 1);
}

TEST_CASE("a restore record never replaces an installed version or points outside its id and version")
{
    Scratch scratch;
    const fs::path root = scratch / "extensions";
    REQUIRE(installPackage(packed(scratch, "new"), root).ok());
    writeFile(root / ".staging/aaaa/manifest.json", "{\"stale\": true}");
    writeFile(root / ".staging/aaaa.restore", "alice.crt/1.2.0");
    writeFile(root / ".staging/bbbb/manifest.json", "{}");
    writeFile(root / ".staging/bbbb.restore", "../outside/1.0.0");
    writeFile(root / ".staging/cccc/manifest.json", "{}");
    writeFile(root / ".staging/cccc.restore", ".staging/x");
    writeFile(root / ".staging/dddd.restore", "bob.gone/1.0.0");
    clearStaging(root);
    CHECK_FALSE(fs::exists(root / ".staging"));
    CHECK(readFile(root / "alice.crt/1.2.0/manifest.json") == kManifest);
    CHECK_FALSE(fs::exists(scratch / "outside"));
    CHECK_FALSE(fs::exists(root / "bob.gone"));
    CHECK(scanInstalled(root).size() == 1);
}
