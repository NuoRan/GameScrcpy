#include "gs/ext/Manifest.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <variant>

using namespace gs::ext;
using Json = nlohmann::ordered_json;

namespace {

Json base()
{
    return Json::parse(R"({
        "schemaVersion": 1,
        "id": "alice.crt-filter",
        "version": "1.2.0",
        "name": "CRT scanlines",
        "description": "Old monitor look.",
        "authors": ["Alice"],
        "license": "MIT",
        "repository": "https://github.com/alice/crt-filter",
        "requires": {"filter": "^1.0"},
        "contributes": {"filters": [{"name": "scanlines", "file": "shaders/crt.hlsl"}]}
    })");
}

LoadedManifest load(const Json& json, LoadOptions options = {})
{
    return loadManifest(json.dump(), options);
}

bool reports(const LoadedManifest& loaded, Code code, std::string_view path)
{
    return std::ranges::any_of(loaded.diagnostics,
                               [&](const Diagnostic& diagnostic) { return diagnostic.code == code && diagnostic.path == path; });
}

std::string listed(const LoadedManifest& loaded)
{
    std::string text;
    for (const Diagnostic& diagnostic : loaded.diagnostics) {
        text += std::string(codeName(diagnostic.code)) + " " + diagnostic.path + "; ";
    }
    return text;
}

bool contains(const Json& array, const Json& value)
{
    return std::find(array.begin(), array.end(), value) != array.end();
}

}

TEST_CASE("a complete manifest loads without diagnostics and survives a save and a load")
{
    Json json = base();
    json["aiAssisted"] = true;
    json["requires"]["script"] = "~1.0.0";
    json["requires"]["theme"] = "1.0.0";
    json["dependencies"] = {{"bob.shader-lib", "^2.1"}, {"carol.extras", {{"version", ">=1.0 <3"}, {"optional", true}}}};
    json["contributes"]["themes"] =
        Json::array({{{"name", "Amber"}, {"dark", {{"accent", "#FFB000"}}}, {"light", {{"accent", "#CC8800FF"}}}}});
    json["contributes"]["scripts"] = Json::array({{{"name", "main"}, {"entry", "main.ts"}, {"run", "app"}}});
    json["settings"] = Json::array(
        {{{"key", "strength"}, {"type", "number"}, {"title", "Strength"}, {"min", 0}, {"max", 1}, {"step", 0.05},
          {"default", 0.5}, {"live", true}},
         {{"key", "lines"}, {"type", "integer"}, {"title", "Lines"}, {"min", 100}, {"max", 2000}, {"default", 480},
          {"scope", "device"}},
         {{"key", "mode"}, {"type", "choice"}, {"title", "Mode"}, {"choices", {"soft", "hard"}}, {"default", "soft"}},
         {{"key", "enabled"}, {"type", "boolean"}, {"title", "On"}, {"default", true}},
         {{"key", "label"}, {"type", "text"}, {"title", "Label"}, {"maxLength", 10}, {"default", "CRT"}}});
    json["permissions"] = Json::array(
        {{{"name", "network"},
          {"reason", "Downloads the shader presets."},
          {"hosts", {"api.example.com", "*.cdn.example.com:8443"}}},
         {{"name", "devices.other"}, {"reason", "Shows the look on the other phones too."}, {"maxDevices", 4}},
         {{"name", "files.paths"},
          {"reason", "Reads your presets folder."},
          {"folders", Json::array({{{"name", "presets"}, {"purpose", "Where your presets are kept."}}})}}});
    json["l10n"] = {{"zh_CN", {{"name", "CRT 扫描线"}, {"settings.strength.title", "强度"}}}};

    const LoadedManifest loaded = load(json);
    CHECK_MESSAGE(loaded.usable, listed(loaded));
    CHECK_MESSAGE(loaded.diagnostics.empty(), listed(loaded));
    const Manifest& manifest = loaded.manifest;
    CHECK(manifest.id == "alice.crt-filter");
    CHECK(manifest.version == Version{1, 2, 0, {}});
    CHECK(manifest.aiAssisted);
    CHECK(manifest.requirements.at(ExtensionPoint::Filter).text() == "^1.0");
    CHECK(manifest.declares(ExtensionPoint::Script));
    CHECK_FALSE(manifest.dependencies.at("bob.shader-lib").optional);
    CHECK(manifest.dependencies.at("carol.extras").optional);
    CHECK(manifest.contributes.filters.front().file == "shaders/crt.hlsl");
    CHECK(manifest.contributes.themes.front().light.at("accent") == "#CC8800FF");
    REQUIRE(manifest.contributes.scripts.size() == 1);
    CHECK(manifest.contributes.scripts.front() == ScriptContribution{.name = "main", .entry = "main.ts", .run = ScriptRun::App});
    REQUIRE(manifest.settings.size() == 5);
    CHECK(std::get<double>(manifest.settings[0].defaultValue) == 0.5);
    CHECK(std::get<std::int64_t>(manifest.settings[1].defaultValue) == 480);
    CHECK(manifest.settings[1].scope == SettingScope::Device);
    CHECK(manifest.settings[2].choices == std::vector<std::string>{"soft", "hard"});
    CHECK(manifest.permissions[1].maxDevices == 4);
    CHECK(manifest.permissions[2].folders ==
          std::vector<PermissionFolder>{{.name = "presets", .purpose = "Where your presets are kept."}});
    CHECK(manifest.l10n.at("zh_CN").at("settings.strength.title") == "强度");

    const LoadedManifest again = loadManifest(saveManifest(manifest));
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == manifest);
}

TEST_CASE("required fields, the schema version, the id and the version are checked with their paths")
{
    Json json = base();
    json.erase("name");
    json.erase("license");
    json["schemaVersion"] = 2;
    json["id"] = "Alice.Filter";
    json["version"] = "1.2";
    json["repository"] = "http://example.com";
    json["extra"] = 1;
    const LoadedManifest loaded = load(json);
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::MissingField, "$.name"));
    CHECK(reports(loaded, Code::MissingField, "$.license"));
    CHECK(reports(loaded, Code::SchemaVersion, "$.schemaVersion"));
    CHECK(reports(loaded, Code::IdInvalid, "$.id"));
    CHECK(reports(loaded, Code::VersionInvalid, "$.version"));
    CHECK(reports(loaded, Code::UrlInvalid, "$.repository"));
    CHECK(reports(loaded, Code::UnknownField, "$.extra"));

    for (const char* bad : {"alice", "alice.", ".alice", "alice..x", "alice.-x", "alice.x-", "alice.x--y", "alice.x_y",
                            "nul.tools", "com1.x"}) {
        json = base();
        json["id"] = bad;
        CHECK_MESSAGE(reports(load(json), Code::IdInvalid, "$.id"), bad);
    }
    json = base();
    json["id"] = std::string(60, 'a') + ".bcde";
    CHECK(reports(load(json), Code::TooLong, "$.id"));
    json = base();
    json["id"] = "gamescrcpy.basics";
    CHECK(reports(load(json), Code::IdReserved, "$.id"));
    CHECK(load(json, {.official = true}).usable);
}

TEST_CASE("extension points: unknown names, bad ranges and contributions that requires does not declare")
{
    Json json = base();
    json["requires"] = {{"filter", "^1.0"}, {"shader", "^1"}, {"theme", "one"}};
    json["contributes"]["themes"] = Json::array({{{"name", "x"}}});
    json["contributes"]["presets"] = Json::array({{{"name", "p"}}});
    const LoadedManifest loaded = load(json);
    CHECK(reports(loaded, Code::ExtensionPointUnknown, R"($.requires["shader"])"));
    CHECK(reports(loaded, Code::RangeInvalid, R"($.requires["theme"])"));
    CHECK(reports(loaded, Code::ContributionUndeclared, "$.contributes.themes"));
    CHECK(reports(loaded, Code::ContributionUndeclared, "$.contributes.presets"));
    CHECK_FALSE(reports(loaded, Code::ContributionUndeclared, "$.contributes.filters"));

    json = base();
    json["requires"] = Json::object();
    CHECK(reports(load(json), Code::MissingField, "$.requires"));
    json = base();
    json.erase("requires");
    CHECK(reports(load(json), Code::MissingField, "$.requires"));
}

TEST_CASE("filters point at .hlsl files inside the package, and themes give colors for token names")
{
    Json json = base();
    json["requires"]["theme"] = "^1.0";
    json["contributes"]["filters"] = Json::array({{{"name", "a"}, {"file", "../evil.hlsl"}},
                                                  {{"name", "b"}, {"file", "C:/x.hlsl"}},
                                                  {{"name", "c"}, {"file", "shaders/crt.glsl"}},
                                                  {{"name", "c"}, {"file", "/abs.hlsl"}},
                                                  {{"name", "d"}, {"file", "shaders\\crt.hlsl"}},
                                                  {{"name", "e"}, {"file", "shaders/con.hlsl"}}});
    json["contributes"]["themes"] =
        Json::array({{{"name", "t"}, {"dark", {{"Accent", "#FFB000"}, {"bgSurface", "red"}, {"accent", "#FFF"}}}}});
    const LoadedManifest loaded = load(json);
    CHECK(reports(loaded, Code::PathInvalid, "$.contributes.filters[0].file"));
    CHECK(reports(loaded, Code::PathInvalid, "$.contributes.filters[1].file"));
    CHECK(reports(loaded, Code::FilterFileInvalid, "$.contributes.filters[2].file"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.filters[3].name"));
    CHECK(reports(loaded, Code::PathInvalid, "$.contributes.filters[3].file"));
    CHECK(reports(loaded, Code::PathInvalid, "$.contributes.filters[4].file"));
    CHECK(reports(loaded, Code::PathInvalid, "$.contributes.filters[5].file"));
    CHECK(reports(loaded, Code::ThemeTokenInvalid, R"($.contributes.themes[0].dark["Accent"])"));
    CHECK(reports(loaded, Code::ThemeColorInvalid, R"($.contributes.themes[0].dark["bgSurface"])"));
    CHECK(reports(loaded, Code::ThemeColorInvalid, R"($.contributes.themes[0].dark["accent"])"));
}

TEST_CASE("settings: key format, duplicates, choices, ranges, unused fields and defaults that fit the type")
{
    Json json = base();
    json["settings"] = Json::array(
        {{{"key", "1st"}, {"type", "boolean"}, {"title", "A"}, {"default", true}},
         {{"key", "mode"}, {"type", "choice"}, {"title", "B"}, {"default", "x"}},
         {{"key", "mode"}, {"type", "choice"}, {"title", "C"}, {"choices", {"a"}}, {"default", "b"}},
         {{"key", "n"}, {"type", "integer"}, {"title", "D"}, {"min", 5}, {"max", 1}, {"default", 3}},
         {{"key", "m"}, {"type", "integer"}, {"title", "E"}, {"min", 0.5}, {"default", 1.5}},
         {{"key", "s"}, {"type", "number"}, {"title", "F"}, {"step", 0}, {"default", "1"}},
         {{"key", "t"}, {"type", "text"}, {"title", "G"}, {"maxLength", 2}, {"default", "abc"}},
         {{"key", "u"}, {"type", "boolean"}, {"title", "H"}, {"min", 1}, {"default", false}},
         {{"key", "v"}, {"type", "slider"}, {"title", "I"}, {"default", 1}},
         {{"key", "w"}, {"type", "boolean"}, {"title", "J"}}});
    const LoadedManifest loaded = load(json);
    CHECK(reports(loaded, Code::SettingKeyInvalid, "$.settings[0].key"));
    CHECK(reports(loaded, Code::SettingChoicesMissing, "$.settings[1].choices"));
    CHECK(reports(loaded, Code::SettingDefaultInvalid, "$.settings[1].default"));
    CHECK(reports(loaded, Code::Duplicate, "$.settings[2].key"));
    CHECK(reports(loaded, Code::SettingDefaultInvalid, "$.settings[2].default"));
    CHECK(reports(loaded, Code::SettingRangeInvalid, "$.settings[3].min"));
    CHECK(reports(loaded, Code::SettingRangeInvalid, "$.settings[4].min"));
    CHECK(reports(loaded, Code::SettingDefaultInvalid, "$.settings[4].default"));
    CHECK(reports(loaded, Code::SettingRangeInvalid, "$.settings[5].step"));
    CHECK(reports(loaded, Code::SettingDefaultInvalid, "$.settings[5].default"));
    CHECK(reports(loaded, Code::SettingDefaultInvalid, "$.settings[6].default"));
    CHECK(reports(loaded, Code::SettingFieldUnused, "$.settings[7].min"));
    CHECK(reports(loaded, Code::OutOfRange, "$.settings[8].type"));
    CHECK(reports(loaded, Code::MissingField, "$.settings[9].default"));
    const auto unused = std::ranges::find(loaded.diagnostics, Code::SettingFieldUnused, &Diagnostic::code);
    REQUIRE(unused != loaded.diagnostics.end());
    CHECK(unused->severity == Severity::Warning);
}

TEST_CASE("permissions: adb.shell never, known names only, a reason each, hosts and paths, and scripts only")
{
    Json json = base();
    json["requires"]["script"] = "^1.0";
    json["permissions"] = Json::array(
        {{{"name", "adb.shell"}, {"reason", "Runs commands."}},
         {{"name", "camera"}, {"reason", "Takes photos."}},
         {{"name", "notify"}},
         {{"name", "mapping"}, {"reason", std::string(121, 'x')}},
         {{"name", "network"}, {"reason", "Talks to a server."}},
         {{"name", "network"}, {"reason", "Again."}, {"hosts", {"*", "bad host", "-x.com", "a.com:99999"}}},
         {{"name", "files.paths"}, {"reason", "Reads files."}},
         {{"name", "notify"}, {"reason", "Tells you."}, {"hosts", {"a.com"}}}});
    const LoadedManifest loaded = load(json);
    CHECK(reports(loaded, Code::PermissionNeverGranted, "$.permissions[0].name"));
    CHECK(reports(loaded, Code::PermissionUnknown, "$.permissions[1].name"));
    CHECK(reports(loaded, Code::PermissionReasonMissing, "$.permissions[2].reason"));
    CHECK(reports(loaded, Code::TooLong, "$.permissions[3].reason"));
    CHECK(reports(loaded, Code::PermissionHostsMissing, "$.permissions[4].hosts"));
    CHECK(reports(loaded, Code::Duplicate, "$.permissions[5].name"));
    CHECK(reports(loaded, Code::PermissionAnyHost, "$.permissions[5].hosts[0]"));
    CHECK(reports(loaded, Code::PermissionHostInvalid, "$.permissions[5].hosts[1]"));
    CHECK(reports(loaded, Code::PermissionHostInvalid, "$.permissions[5].hosts[2]"));
    CHECK(reports(loaded, Code::PermissionHostInvalid, "$.permissions[5].hosts[3]"));
    CHECK(reports(loaded, Code::PermissionPathsMissing, "$.permissions[6].folders"));
    CHECK(reports(loaded, Code::Duplicate, "$.permissions[7].name"));
    CHECK(reports(loaded, Code::PermissionFieldUnused, "$.permissions[7].hosts"));
    const auto anyHost = std::ranges::find(loaded.diagnostics, Code::PermissionAnyHost, &Diagnostic::code);
    REQUIRE(anyHost != loaded.diagnostics.end());
    CHECK(anyHost->severity == Severity::Warning);
    CHECK(std::string(fixHint(Code::PermissionNeverGranted)).find("never") != std::string::npos);
    CHECK(codeName(Code::PermissionUnknown) == "permission-unknown");

    json = base();
    json["permissions"] = Json::array({{{"name", "notify"}, {"reason", "Tells you."}}});
    CHECK(reports(load(json), Code::PermissionNotAllowed, "$.permissions"));
}

TEST_CASE("scripts: a name of lowercase letters, digits and hyphens, a .ts or .js entry, session or app, eight at most")
{
    Json json = base();
    json["requires"]["script"] = "^1.0";
    json["contributes"]["scripts"] =
        Json::array({{{"name", "helper"}, {"entry", "scripts/helper.ts"}, {"run", "session"}},
                     {{"name", "auto-pick"}, {"entry", "pick.js"}, {"run", "app"}}});
    LoadedManifest loaded = load(json);
    CHECK_MESSAGE(loaded.usable, listed(loaded));
    CHECK_MESSAGE(loaded.diagnostics.empty(), listed(loaded));
    REQUIRE(loaded.manifest.contributes.scripts.size() == 2);
    CHECK(loaded.manifest.contributes.scripts[0].run == ScriptRun::Session);
    CHECK(loaded.manifest.contributes.scripts[1].name == "auto-pick");
    CHECK(loaded.manifest.contributes.scripts[1].run == ScriptRun::App);
    CHECK(loadManifest(saveManifest(loaded.manifest)).manifest == loaded.manifest);

    json["contributes"]["scripts"] = Json::array({{{"name", "Helper"}, {"entry", "a.ts"}, {"run", "app"}},
                                                  {{"name", std::string(33, 'a')}, {"entry", "b.ts"}, {"run", "app"}},
                                                  {{"name", "c"}, {"entry", "c.mjs"}, {"run", "app"}},
                                                  {{"name", "d"}, {"entry", "types/d.d.ts"}, {"run", "app"}},
                                                  {{"name", "e"}, {"entry", "../e.ts"}, {"run", "app"}},
                                                  {{"name", "f"}, {"entry", "f.ts"}, {"run", "always"}},
                                                  {{"name", "g"}, {"entry", "g.ts"}},
                                                  {{"name", "c"}, {"entry", "h.ts"}, {"run", "app"}}});
    loaded = load(json);
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::ScriptNameInvalid, "$.contributes.scripts[0].name"));
    CHECK(reports(loaded, Code::TooLong, "$.contributes.scripts[1].name"));
    CHECK(reports(loaded, Code::ScriptEntryInvalid, "$.contributes.scripts[2].entry"));
    CHECK(reports(loaded, Code::ScriptEntryInvalid, "$.contributes.scripts[3].entry"));
    CHECK(reports(loaded, Code::PathInvalid, "$.contributes.scripts[4].entry"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.scripts[5].run"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.scripts[6].run"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.scripts[7].name"));
    CHECK(codeName(Code::ScriptNameInvalid) == "script-name-invalid");
    CHECK(codeName(Code::ScriptEntryInvalid) == "script-entry-invalid");
    CHECK_FALSE(fixHint(Code::ScriptEntryInvalid).empty());

    Json many = Json::array();
    for (int index = 0; index < 9; ++index) {
        many.push_back({{"name", "s" + std::to_string(index)}, {"entry", "s.ts"}, {"run", "app"}});
    }
    json["contributes"]["scripts"] = many;
    CHECK(reports(load(json), Code::TooMany, "$.contributes.scripts"));

    json = base();
    json["contributes"]["scripts"] = Json::array({{{"name", "helper"}, {"entry", "helper.ts"}, {"run", "app"}}});
    CHECK(reports(load(json), Code::ContributionUndeclared, "$.contributes.scripts"));
}

TEST_CASE("broken documents: not JSON, not an object, too large, too deep, and fields of the wrong type")
{
    CHECK(reports(loadManifest("{"), Code::MalformedJson, "$"));
    CHECK(reports(loadManifest("[]"), Code::NotAnObject, "$"));
    CHECK(reports(loadManifest(std::string(kMaxManifestBytes + 1, ' ')), Code::TooLarge, "$"));
    CHECK(reports(loadManifest(std::string(40, '[') + std::string(40, ']')), Code::TooLarge, "$"));
    CHECK_FALSE(loadManifest("{").usable);

    Json json = base();
    json["name"] = 3;
    json["authors"] = "Alice";
    json["contributes"]["filters"] = Json::object();
    json["settings"] = Json::array({1});
    json["dependencies"] = {{"bob.lib", 2}, {"Bad", "^1"}, {"carol.x", "nope"}, {"dave.y", {{"optional", true}}}};
    const LoadedManifest loaded = load(json);
    CHECK(reports(loaded, Code::WrongType, "$.name"));
    CHECK(reports(loaded, Code::WrongType, "$.authors"));
    CHECK(reports(loaded, Code::WrongType, "$.contributes.filters"));
    CHECK(reports(loaded, Code::NotAnObject, "$.settings[0]"));
    CHECK(reports(loaded, Code::WrongType, R"($.dependencies["bob.lib"])"));
    CHECK(reports(loaded, Code::IdInvalid, R"($.dependencies["Bad"])"));
    CHECK(reports(loaded, Code::RangeInvalid, R"($.dependencies["carol.x"])"));
    CHECK(reports(loaded, Code::MissingField, R"($.dependencies["dave.y"].version)"));
}

TEST_CASE("an extension cannot depend on itself")
{
    Json json = base();
    json["dependencies"] = {{"alice.crt-filter", "^1"}};
    CHECK(reports(load(json), Code::DependencyCycle, R"($.dependencies["alice.crt-filter"])"));
}

TEST_CASE("very many problems of one kind are cut to fifty and the rest are counted in one more")
{
    Json manifest = base();
    for (int index = 0; index < 200; ++index) {
        manifest["extra" + std::to_string(index)] = index;
    }
    const LoadedManifest loaded = load(manifest);
    CHECK(loaded.usable);
    CHECK(std::ranges::count(loaded.diagnostics, Code::UnknownField, &Diagnostic::code) == 51);
    CHECK(reports(loaded, Code::UnknownField, "$.extra49"));
    CHECK_FALSE(reports(loaded, Code::UnknownField, "$.extra50"));
    const auto more = std::ranges::find(loaded.diagnostics, std::string("$"), &Diagnostic::path);
    REQUIRE(more != loaded.diagnostics.end());
    CHECK(more->code == Code::UnknownField);
    CHECK(more->severity == Severity::Warning);
    CHECK(more->detail == "150 more");
}

TEST_CASE("an unknown key that is long or not a plain name is quoted and cut short in the problem path")
{
    Json manifest = base();
    manifest[std::string(100, 'k')] = 1;
    manifest[std::string(63, 'a') + "\u77F3b"] = 1;
    manifest["<img src=x>"] = 1;
    const LoadedManifest loaded = load(manifest);
    INFO(listed(loaded));
    CHECK(reports(loaded, Code::UnknownField, "$[\"" + std::string(64, 'k') + "\u2026\"]"));
    CHECK(reports(loaded, Code::UnknownField, "$[\"" + std::string(63, 'a') + "\u2026\"]"));
    CHECK(reports(loaded, Code::UnknownField, R"($["<img src=x>"])"));
}

TEST_CASE("the JSON Schema comes from the same declarations as the reader")
{
    Json schema = Json::parse(manifestJsonSchema());
    CHECK(schema["$schema"] == "https://json-schema.org/draft/2020-12/schema");
    CHECK(schema["type"] == "object");
    CHECK(schema["additionalProperties"] == false);
    for (const char* field : {"schemaVersion", "id", "version", "name", "license", "requires"}) {
        CHECK_MESSAGE(contains(schema["required"], field), field);
    }
    CHECK_FALSE(contains(schema["required"], "description"));
    CHECK(schema["properties"]["schemaVersion"]["const"] == kSchemaVersion);
    CHECK(schema["properties"]["id"]["maxLength"] == kMaxIdLength);
    CHECK(schema["properties"]["requires"]["propertyNames"]["enum"].size() == kExtensionPointNames.size());

    Json permission = schema["properties"]["permissions"]["items"];
    CHECK(permission["required"] == Json::array({"name", "reason"}));
    CHECK(permission["properties"]["reason"]["maxLength"] == kMaxReasonLength);
    CHECK(contains(permission["properties"]["name"]["enum"], "network"));
    CHECK_FALSE(contains(permission["properties"]["name"]["enum"], "adb.shell"));

    Json setting = schema["properties"]["settings"]["items"];
    CHECK(setting["properties"]["type"]["enum"].size() == kSettingTypeNames.size());
    CHECK(contains(setting["required"], "default"));
    CHECK(schema["properties"]["contributes"]["properties"]["themes"]["items"]["properties"]["dark"]
                ["additionalProperties"]["pattern"] == "^#([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$");
    Json patch = schema["properties"]["contributes"]["properties"]["profilePatches"]["items"];
    CHECK(patch["required"] == Json::array({"target", "file"}));
    CHECK(patch["properties"]["target"]["pattern"].is_string());
}

TEST_CASE("profile contributions name their profiles, patches name a target, and both need the profile point (PRF-006)")
{
    Json json = base();
    json["requires"] = {{"profile", "^1.0"}};
    json["contributes"] = {
        {"profiles", Json::array({{{"name", "standard"}, {"file", "profiles/standard.json"}},
                                  {{"name", "lean_2-hands"}, {"file", "profiles/lean.json"}}})},
        {"profilePatches", Json::array({{{"target", "bob.shooter-pack/standard"}, {"file", "patches/keys.json"}}})}};
    LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    REQUIRE(loaded.manifest.contributes.profiles.size() == 2);
    CHECK(loaded.manifest.contributes.profiles[1].name == "lean_2-hands");
    REQUIRE(loaded.manifest.contributes.profilePatches.size() == 1);
    CHECK(loaded.manifest.contributes.profilePatches[0].target == "bob.shooter-pack/standard");
    CHECK(loadManifest(saveManifest(loaded.manifest)).manifest == loaded.manifest);

    json["contributes"]["profiles"] = Json::array({{{"name", "bad name"}, {"file", "profiles/a.json"}},
                                                   {{"name", "same"}, {"file", "profiles/b.json"}},
                                                   {{"name", "same"}, {"file", "profiles/c.json"}},
                                                   {{"name", "far"}, {"file", "../d.json"}}});
    json["contributes"]["profilePatches"] = Json::array({{{"target", "standard"}, {"file", "patches/a.json"}},
                                                         {{"target", "Bob.Pack/standard"}, {"file", "patches/b.json"}},
                                                         {{"target", "bob.pack/x y"}, {"file", "patches/c.json"}},
                                                         {{"file", "patches/d.json"}}});
    loaded = load(json);
    INFO(listed(loaded));
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::ProfileNameInvalid, "$.contributes.profiles[0].name"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.profiles[2].name"));
    CHECK(reports(loaded, Code::PathInvalid, "$.contributes.profiles[3].file"));
    CHECK(reports(loaded, Code::ProfileTargetInvalid, "$.contributes.profilePatches[0].target"));
    CHECK(reports(loaded, Code::ProfileTargetInvalid, "$.contributes.profilePatches[1].target"));
    CHECK(reports(loaded, Code::ProfileTargetInvalid, "$.contributes.profilePatches[2].target"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.profilePatches[3].target"));

    json = base();
    json["contributes"]["profilePatches"] =
        Json::array({{{"target", "bob.shooter-pack/standard"}, {"file", "patches/keys.json"}}});
    CHECK(reports(load(json), Code::ContributionUndeclared, "$.contributes.profilePatches"));
    CHECK(std::string(codeName(Code::PatchTargetMissing)) == "patch-target-missing");
    CHECK_FALSE(std::string(fixHint(Code::ProfileTargetInvalid)).empty());
}

namespace {

Json nativeBase()
{
    Json json = base();
    json["requires"] = {{"native", "^1.0"}};
    json["contributes"] = {
        {"native",
         {{"dll", "bin/clock.dll"},
          {"touchMethods",
           Json::array({{{"name", "board"}, {"title", "Touch board"}, {"description", "Touches through a USB board."}}})},
          {"nodes", Json::array({{{"name", "now"}, {"returns", "string"}, {"description", "The time as text."}},
                                 {{"name", "beep"},
                                  {"params", Json::array({{{"name", "at"}, {"type", "point"}},
                                                          {{"name", "lengthMs"}, {"type", "duration"}}})},
                                  {"permissions", {"notify"}}}})}}}};
    json["permissions"] = Json::array({{{"name", "notify"}, {"reason", "Shows when it beeps."}}});
    return json;
}

}

TEST_CASE("a native contribution with a DLL, touch methods and nodes loads and survives a save and a load (ADR-0097)")
{
    const LoadedManifest loaded = load(nativeBase());
    INFO(listed(loaded));
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    CHECK(loaded.manifest.declares(ExtensionPoint::Native));
    REQUIRE(loaded.manifest.contributes.native.has_value());
    const NativeContribution& native = *loaded.manifest.contributes.native;
    CHECK(native.dll == "bin/clock.dll");
    REQUIRE(native.touchMethods.size() == 1);
    CHECK(native.touchMethods[0].name == "board");
    CHECK(native.touchMethods[0].title == "Touch board");
    REQUIRE(native.nodes.size() == 2);
    CHECK(native.nodes[0].name == "now");
    CHECK(native.nodes[0].returns == "string");
    CHECK(native.nodes[0].params.empty());
    CHECK(native.nodes[1].returns.empty());
    REQUIRE(native.nodes[1].params.size() == 2);
    CHECK(native.nodes[1].params[0] == NativeParam{"at", "point"});
    CHECK(native.nodes[1].params[1] == NativeParam{"lengthMs", "duration"});
    CHECK(native.nodes[1].permissions == std::vector<std::string>{"notify"});

    const LoadedManifest again = loadManifest(saveManifest(loaded.manifest));
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == loaded.manifest);
    CHECK(Json::parse(saveManifest(loaded.manifest))["contributes"]["native"]["nodes"][1]["params"][0]["type"] == "point");

    Json plain = base();
    CHECK_FALSE(load(plain).manifest.contributes.native.has_value());
    CHECK_FALSE(Json::parse(saveManifest(load(plain).manifest))["contributes"].contains("native"));
}

TEST_CASE("a native contribution needs a DLL inside the package, known types, plain names and the native point "
          "(ADR-0097)")
{
    Json json = nativeBase();
    json["contributes"]["native"].erase("dll");
    LoadedManifest loaded = load(json);
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::MissingField, "$.contributes.native.dll"));

    for (const char* bad : {"bin/clock.exe", "clock"}) {
        json = nativeBase();
        json["contributes"]["native"]["dll"] = bad;
        CHECK_MESSAGE(reports(load(json), Code::NativeDllInvalid, "$.contributes.native.dll"), bad);
    }
    json = nativeBase();
    json["contributes"]["native"]["dll"] = "../clock.dll";
    CHECK(reports(load(json), Code::PathInvalid, "$.contributes.native.dll"));
    json["contributes"]["native"]["dll"] = "BIN/Clock.DLL";
    CHECK(load(json).usable);

    json = nativeBase();
    json["contributes"]["native"]["nodes"][1]["params"][0]["type"] = "int";
    json["contributes"]["native"]["nodes"][1]["params"][1]["name"] = "length-ms";
    json["contributes"]["native"]["nodes"][0]["returns"] = "void";
    json["contributes"]["native"]["nodes"].push_back({{"name", "Now"}});
    json["contributes"]["native"]["nodes"].push_back({{"name", "now"}});
    json["contributes"]["native"]["touchMethods"][0].erase("title");
    json["contributes"]["native"]["touchMethods"].push_back({{"name", "2fast"}, {"title", "Fast"}});
    loaded = load(json);
    INFO(listed(loaded));
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::NativeTypeInvalid, "$.contributes.native.nodes[1].params[0].type"));
    CHECK(reports(loaded, Code::NativeNameInvalid, "$.contributes.native.nodes[1].params[1].name"));
    CHECK(reports(loaded, Code::NativeTypeInvalid, "$.contributes.native.nodes[0].returns"));
    CHECK(reports(loaded, Code::NativeNameInvalid, "$.contributes.native.nodes[2].name"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.native.nodes[3].name"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.native.touchMethods[0].title"));
    CHECK(reports(loaded, Code::NativeNameInvalid, "$.contributes.native.touchMethods[1].name"));

    json = nativeBase();
    json["requires"] = {{"filter", "^1.0"}};
    loaded = load(json);
    CHECK(reports(loaded, Code::ContributionUndeclared, "$.contributes.native"));
    CHECK(reports(loaded, Code::PermissionNotAllowed, "$.permissions"));

    json = nativeBase();
    json["contributes"]["native"] = Json::array();
    CHECK(reports(load(json), Code::NotAnObject, "$.contributes.native"));

    CHECK(codeName(Code::NativeTypeInvalid) == "native-type-invalid");
    CHECK(std::string(fixHint(Code::NativeTypeInvalid)).find("duration") != std::string::npos);
}

TEST_CASE("the permissions a native node needs are known ones that the manifest asks for (ADR-0097)")
{
    Json json = nativeBase();
    json["contributes"]["native"]["nodes"][1]["permissions"] = {"network", "camera", "adb.shell", "notify"};
    const LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::NativePermissionUndeclared, "$.contributes.native.nodes[1].permissions[0]"));
    CHECK(reports(loaded, Code::PermissionUnknown, "$.contributes.native.nodes[1].permissions[1]"));
    CHECK(reports(loaded, Code::PermissionNeverGranted, "$.contributes.native.nodes[1].permissions[2]"));
    CHECK_FALSE(reports(loaded, Code::NativePermissionUndeclared, "$.contributes.native.nodes[1].permissions[3]"));
    CHECK_FALSE(reports(loaded, Code::PermissionNotAllowed, "$.permissions"));

    Json schema = Json::parse(manifestJsonSchema());
    Json native = schema["properties"]["contributes"]["properties"]["native"];
    CHECK(native["required"] == Json::array({"dll"}));
    CHECK(native["properties"]["nodes"]["items"]["properties"]["params"]["items"]["properties"]["type"]["enum"].size() ==
          kNativeTypeNames.size());
    CHECK(native["properties"]["nodes"]["items"]["properties"]["name"]["pattern"] == "^[a-z][A-Za-z0-9]*$");
    CHECK(native["properties"]["touchMethods"]["items"]["required"] == Json::array({"name", "title"}));
}

TEST_CASE("ADR-0112: a native DLL is one path or one path per processor, both forms survive a save and a load, and "
          "the one for this processor is picked")
{
    Json json = nativeBase();
    json["contributes"]["native"]["dll"] = {{"x64", "bin/x64/clock.dll"}, {"arm64", "bin/arm64/clock.dll"}};
    LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    REQUIRE(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    const NativeContribution& native = *loaded.manifest.contributes.native;
    CHECK(native.dll.empty());
    CHECK(native.dlls == TextMap{{"arm64", "bin/arm64/clock.dll"}, {"x64", "bin/x64/clock.dll"}});
    CHECK(nativeDllFor(native, "x64") == "bin/x64/clock.dll");
    CHECK(nativeDllFor(native, "arm64") == "bin/arm64/clock.dll");
    CHECK(nativeDllFor(native, "x86").empty());
    CHECK(nativeDlls(native) == std::vector<std::pair<std::string, std::string>>{
                                    {"arm64", "bin/arm64/clock.dll"}, {"x64", "bin/x64/clock.dll"}});
    CHECK(nativeDllField("arm64") == R"($.contributes.native.dll["arm64"])");
    CHECK(nativeDllField("") == "$.contributes.native.dll");
    const LoadedManifest again = loadManifest(saveManifest(loaded.manifest));
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == loaded.manifest);
    CHECK(Json::parse(saveManifest(loaded.manifest))["contributes"]["native"]["dll"]["x64"] == "bin/x64/clock.dll");

    const LoadedManifest plain = load(nativeBase());
    REQUIRE(plain.usable);
    CHECK(nativeDllFor(*plain.manifest.contributes.native, "x64") == "bin/clock.dll");
    CHECK(nativeDllFor(*plain.manifest.contributes.native, "arm64") == "bin/clock.dll");
    CHECK(nativeDlls(*plain.manifest.contributes.native) ==
          std::vector<std::pair<std::string, std::string>>{{"", "bin/clock.dll"}});
    CHECK(Json::parse(saveManifest(plain.manifest))["contributes"]["native"]["dll"] == "bin/clock.dll");
    CHECK(std::ranges::find(kNativeArchitectureNames, hostArchitecture()) != kNativeArchitectureNames.end());

    json["contributes"]["native"]["dll"] = {{"x64", "bin/x64/clock.dll"}, {"riscv64", "bin/riscv64/clock.dll"}};
    loaded = load(json);
    CHECK(loaded.usable);
    CHECK(reports(loaded, Code::NativeArchitectureUnknown, R"($.contributes.native.dll["riscv64"])"));

    json["contributes"]["native"]["dll"] = {{"X64", "bin/x64/clock.dll"}};
    CHECK(reports(load(json), Code::NativeNameInvalid, R"($.contributes.native.dll["X64"])"));
    json["contributes"]["native"]["dll"] = {{"x64", "bin/x64/clock.exe"}};
    CHECK(reports(load(json), Code::NativeDllInvalid, R"($.contributes.native.dll["x64"])"));
    json["contributes"]["native"]["dll"] = {{"x64", "../clock.dll"}};
    CHECK(reports(load(json), Code::PathInvalid, R"($.contributes.native.dll["x64"])"));
    json["contributes"]["native"]["dll"] = Json::object();
    CHECK(reports(load(json), Code::MissingField, "$.contributes.native.dll"));
    json["contributes"]["native"]["dll"] = Json::array({"bin/clock.dll"});
    CHECK(reports(load(json), Code::WrongType, "$.contributes.native.dll"));

    const Json schema = Json::parse(manifestJsonSchema());
    const Json dll = schema["properties"]["contributes"]["properties"]["native"]["properties"]["dll"];
    REQUIRE(dll["oneOf"].size() == 2);
    CHECK(dll["oneOf"][0]["type"] == "string");
    CHECK(dll["oneOf"][1]["type"] == "object");
    CHECK(dll["oneOf"][1]["minProperties"] == 1);
    CHECK(dll["oneOf"][1]["propertyNames"]["pattern"] == "^[a-z][A-Za-z0-9]*$");
    CHECK(schema["properties"]["contributes"]["properties"]["native"]["required"] == Json::array({"dll"}));
    for (const Code code : {Code::NativeArchitectureUnknown, Code::NativeArchitectureMissing, Code::NativeMachineMismatch,
                            Code::NativeEntryMissing, Code::SignatureInvalid, Code::SignatureMissing,
                            Code::PublisherChanged, Code::SigningKeyMismatch}) {
        CHECK(codeName(code) != "unknown");
        CHECK_FALSE(fixHint(code).empty());
    }
    CHECK(codeName(Code::NativeArchitectureMissing) == "native-architecture-missing");
    CHECK(codeName(Code::PublisherChanged) == "publisher-changed");
}

TEST_CASE("ADR-0112: native jobs load with their arguments, result and running limit, survive a save and a load, "
          "and are checked like nodes")
{
    Json json = nativeBase();
    json["contributes"]["native"]["jobs"] =
        Json::array({{{"name", "flash"},
                      {"title", "Write firmware"},
                      {"params", Json::array({{{"name", "port"}, {"type", "string"}}})},
                      {"returns", "json"},
                      {"description", "Writes the firmware."},
                      {"permissions", {"notify"}},
                      {"most", 1}},
                     {{"name", "scan"}, {"title", "Scan"}}});
    LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    REQUIRE(loaded.manifest.contributes.native.has_value());
    const std::vector<NativeJob>& jobs = loaded.manifest.contributes.native->jobs;
    REQUIRE(jobs.size() == 2);
    CHECK(jobs[0].name == "flash");
    CHECK(jobs[0].title == "Write firmware");
    CHECK(jobs[0].params == std::vector<NativeParam>{{"port", "string"}});
    CHECK(jobs[0].returns == "json");
    CHECK(jobs[0].description == "Writes the firmware.");
    CHECK(jobs[0].permissions == std::vector<std::string>{"notify"});
    CHECK(jobs[0].most == 1);
    CHECK(jobs[1].params.empty());
    CHECK(jobs[1].returns.empty());
    CHECK_FALSE(jobs[1].most.has_value());
    const LoadedManifest again = loadManifest(saveManifest(loaded.manifest));
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == loaded.manifest);

    json["contributes"]["native"]["jobs"][0]["most"] = 5;
    json["contributes"]["native"]["jobs"][0]["params"][0]["type"] = "int";
    json["contributes"]["native"]["jobs"][1].erase("title");
    json["contributes"]["native"]["jobs"].push_back({{"name", "Flash"}, {"title", "Bad"}});
    json["contributes"]["native"]["jobs"].push_back({{"name", "flash"}, {"title", "Again"}});
    json["contributes"]["native"]["jobs"].push_back({{"name", "now"}, {"title", "Same as a node"}});
    json["contributes"]["native"]["jobs"].push_back({{"name", "send"}, {"title", "Send"}, {"permissions", {"network"}}});
    loaded = load(json);
    INFO(listed(loaded));
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.jobs[0].most"));
    CHECK(reports(loaded, Code::NativeTypeInvalid, "$.contributes.native.jobs[0].params[0].type"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.native.jobs[1].title"));
    CHECK(reports(loaded, Code::NativeNameInvalid, "$.contributes.native.jobs[2].name"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.native.jobs[3].name"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.native.jobs[4].name"));
    CHECK(reports(loaded, Code::NativePermissionUndeclared, "$.contributes.native.jobs[5].permissions[0]"));

    const Json schema = Json::parse(manifestJsonSchema());
    const Json job = schema["properties"]["contributes"]["properties"]["native"]["properties"]["jobs"]["items"];
    CHECK(job["required"] == Json::array({"name", "title"}));
    CHECK(job["properties"]["most"]["maximum"] == kMostRunningNativeJobs);
}

TEST_CASE("ADR-0112: native device providers load with their kinds and need the permission devices.provide")
{
    Json json = nativeBase();
    json["contributes"]["native"]["deviceProviders"] =
        Json::array({{{"name", "boards"}, {"title", "Touch boxes"}, {"kinds", {"touchBox"}}}});
    json["permissions"].push_back({{"name", "devices.provide"}, {"reason", "Lists the boards."}});
    LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    REQUIRE(loaded.manifest.contributes.native.has_value());
    CHECK(loaded.manifest.contributes.native->deviceProviders ==
          std::vector<NativeDeviceProvider>{{.name = "boards", .title = "Touch boxes", .kinds = {"touchBox"}}});
    const LoadedManifest again = loadManifest(saveManifest(loaded.manifest));
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == loaded.manifest);

    json["permissions"] = Json::array({{{"name", "notify"}, {"reason", "Shows when it beeps."}}});
    json["contributes"]["native"]["deviceProviders"].push_back(
        {{"name", "boards"}, {"title", "Again"}, {"kinds", {"phone"}}});
    json["contributes"]["native"]["deviceProviders"].push_back({{"name", "more"}, {"title", "More"}});
    loaded = load(json);
    INFO(listed(loaded));
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::NativePermissionUndeclared, "$.contributes.native.deviceProviders"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.native.deviceProviders[1].name"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.deviceProviders[1].kinds[0]"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.native.deviceProviders[2].kinds"));
}

TEST_CASE("ADR-0112: a speech engine names jobs and a node of the extension that take what GameScrcpy passes")
{
    Json json = nativeBase();
    json["contributes"]["native"]["jobs"] = Json::array(
        {{{"name", "speak"},
          {"title", "Speak"},
          {"params",
           {{{"name", "text"}, {"type", "string"}},
            {{"name", "voice"}, {"type", "string"}},
            {{"name", "rate"}, {"type", "number"}},
            {{"name", "volume"}, {"type", "number"}}}}},
         {{"name", "hear"},
          {"title", "Hear"},
          {"params",
           {{{"name", "confidence"}, {"type", "number"}},
            {{"name", "phrases"}, {"type", "json"}},
            {{"name", "language"}, {"type", "string"}}}}},
         {{"name", "count"}, {"title", "Count"}, {"params", {{{"name", "text"}, {"type", "string"}}}}}});
    json["contributes"]["native"]["nodes"].push_back({{"name", "voices"}, {"returns", "json"}});
    json["contributes"]["native"]["speechEngines"] =
        Json::array({{{"name", "robot"},
                      {"title", "Robot voice"},
                      {"say", "speak"},
                      {"voices", "voices"},
                      {"listen", "hear"},
                      {"dictation", true},
                      {"languages", {"en-US", "zh-CN"}}},
                     {{"name", "ears"}, {"title", "Ears"}, {"listen", "hear"}}});
    LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    REQUIRE(loaded.manifest.contributes.native.has_value());
    const std::vector<NativeSpeechEngine>& engines = loaded.manifest.contributes.native->speechEngines;
    REQUIRE(engines.size() == 2);
    CHECK(engines[0] == NativeSpeechEngine{.name = "robot",
                                           .title = "Robot voice",
                                           .say = "speak",
                                           .voices = "voices",
                                           .listen = "hear",
                                           .dictation = true,
                                           .languages = {"en-US", "zh-CN"}});
    CHECK(engines[1].say.empty());
    CHECK_FALSE(engines[1].dictation);
    const LoadedManifest again = loadManifest(saveManifest(loaded.manifest));
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == loaded.manifest);

    json["contributes"]["native"]["speechEngines"] = Json::array(
        {{{"name", "robot"}, {"title", "Again"}, {"say", "count"}, {"listen", "speak"}, {"voices", "now"}},
         {{"name", "robot"}, {"title", "Twice"}, {"say", "missing"}, {"voices", "voices"}},
         {{"name", "mute"}, {"title", "Mute"}, {"languages", {"zh_CN!"}}}});
    loaded = load(json);
    INFO(listed(loaded));
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::SpeechEngineInvalid, "$.contributes.native.speechEngines[0].say"));
    CHECK(reports(loaded, Code::SpeechEngineInvalid, "$.contributes.native.speechEngines[0].listen"));
    CHECK(reports(loaded, Code::SpeechEngineInvalid, "$.contributes.native.speechEngines[0].voices"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.native.speechEngines[1].name"));
    CHECK(reports(loaded, Code::SpeechEngineInvalid, "$.contributes.native.speechEngines[1].say"));
    CHECK_FALSE(reports(loaded, Code::SpeechEngineInvalid, "$.contributes.native.speechEngines[1].voices"));
    CHECK(reports(loaded, Code::SpeechEngineInvalid, "$.contributes.native.speechEngines[2]"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.speechEngines[2].languages[0]"));
    CHECK(codeName(Code::SpeechEngineInvalid) == "speech-engine-invalid");
    CHECK_FALSE(fixHint(Code::SpeechEngineInvalid).empty());
}

TEST_CASE("ADR-0112: an AI provider names a chat job taking one json request and a models node without parameters, "
          "both returning json, and needs the permission ai.provide")
{
    Json json = nativeBase();
    json["contributes"]["native"]["jobs"] =
        Json::array({{{"name", "chat"},
                      {"title", "Chat"},
                      {"params", {{{"name", "request"}, {"type", "json"}}}},
                      {"returns", "json"}},
                     {{"name", "count"}, {"title", "Count"}, {"params", {{{"name", "request"}, {"type", "string"}}}},
                      {"returns", "json"}},
                     {{"name", "quiet"}, {"title", "Quiet"}, {"params", {{{"name", "request"}, {"type", "json"}}}}},
                     {{"name", "wide"},
                      {"title", "Wide"},
                      {"params", {{{"name", "request"}, {"type", "json"}}, {{"name", "more"}, {"type", "json"}}}},
                      {"returns", "json"}}});
    json["contributes"]["native"]["nodes"].push_back({{"name", "models"}, {"returns", "json"}});
    json["contributes"]["native"]["nodes"].push_back(
        {{"name", "pick"}, {"params", {{{"name", "kind"}, {"type", "string"}}}}, {"returns", "json"}});
    json["contributes"]["native"]["aiProviders"] =
        Json::array({{{"name", "cloud"}, {"title", "Cloud AI"}, {"models", "models"}, {"chat", "chat"}},
                     {{"name", "local"}, {"title", "Local AI"}, {"models", "models"}, {"chat", "chat"}}});
    json["permissions"].push_back({{"name", "ai.provide"}, {"reason", "Answers the assistant with its models."}});
    LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    REQUIRE(loaded.manifest.contributes.native.has_value());
    const std::vector<NativeAiProvider>& providers = loaded.manifest.contributes.native->aiProviders;
    REQUIRE(providers.size() == 2);
    CHECK(providers[0] == NativeAiProvider{.name = "cloud", .title = "Cloud AI", .models = "models", .chat = "chat"});
    CHECK(providers[1].name == "local");
    const LoadedManifest again = loadManifest(saveManifest(loaded.manifest));
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == loaded.manifest);
    CHECK(std::ranges::find(kPermissionNames, kAiProvidePermission) != kPermissionNames.end());

    json["permissions"] = Json::array({{{"name", "notify"}, {"reason", "Shows when it beeps."}}});
    json["contributes"]["native"]["aiProviders"] = Json::array(
        {{{"name", "a"}, {"title", "String request"}, {"models", "now"}, {"chat", "count"}},
         {{"name", "b"}, {"title", "No result"}, {"models", "pick"}, {"chat", "quiet"}},
         {{"name", "c"}, {"title", "Two parameters"}, {"models", "missing"}, {"chat", "wide"}},
         {{"name", "d"}, {"title", "Not a job"}, {"models", "models"}, {"chat", "models"}}});
    loaded = load(json);
    INFO(listed(loaded));
    CHECK_FALSE(loaded.usable);
    CHECK_FALSE(reports(loaded, Code::TooMany, "$.contributes.native.aiProviders"));
    CHECK(reports(loaded, Code::NativePermissionUndeclared, "$.contributes.native.aiProviders"));
    CHECK(reports(loaded, Code::AiProviderInvalid, "$.contributes.native.aiProviders[0].chat"));
    CHECK(reports(loaded, Code::AiProviderInvalid, "$.contributes.native.aiProviders[0].models"));
    CHECK(reports(loaded, Code::AiProviderInvalid, "$.contributes.native.aiProviders[1].chat"));
    CHECK(reports(loaded, Code::AiProviderInvalid, "$.contributes.native.aiProviders[1].models"));
    CHECK(reports(loaded, Code::AiProviderInvalid, "$.contributes.native.aiProviders[2].chat"));
    CHECK(reports(loaded, Code::AiProviderInvalid, "$.contributes.native.aiProviders[2].models"));
    CHECK(reports(loaded, Code::AiProviderInvalid, "$.contributes.native.aiProviders[3].chat"));
    CHECK_FALSE(reports(loaded, Code::AiProviderInvalid, "$.contributes.native.aiProviders[3].models"));

    json["contributes"]["native"]["aiProviders"] =
        Json::array({{{"name", "a"}, {"title", "First"}, {"models", "models"}, {"chat", "chat"}},
                     {{"name", "a"}, {"title", "Again"}, {"models", "models"}, {"chat", "chat"}},
                     {{"name", "Bad"}, {"title", "Capital"}, {"models", "models"}, {"chat", "chat"}},
                     {{"name", "e"}, {"title", "Nothing named"}}});
    loaded = load(json);
    INFO(listed(loaded));
    CHECK_FALSE(loaded.usable);
    CHECK_FALSE(reports(loaded, Code::TooMany, "$.contributes.native.aiProviders"));
    CHECK_FALSE(reports(loaded, Code::AiProviderInvalid, "$.contributes.native.aiProviders[0].chat"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.native.aiProviders[1].name"));
    CHECK_FALSE(reports(loaded, Code::AiProviderInvalid, "$.contributes.native.aiProviders[1].chat"));
    CHECK(reports(loaded, Code::NativeNameInvalid, "$.contributes.native.aiProviders[2].name"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.native.aiProviders[3].models"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.native.aiProviders[3].chat"));
    CHECK(codeName(Code::AiProviderInvalid) == "ai-provider-invalid");
    CHECK_FALSE(fixHint(Code::AiProviderInvalid).empty());

    json["contributes"]["native"]["aiProviders"] = Json::array(
        {{{"name", "a"}, {"title", "One"}, {"models", "models"}, {"chat", "chat"}},
         {{"name", "b"}, {"title", "Two"}, {"models", "models"}, {"chat", "chat"}},
         {{"name", "c"}, {"title", "Three"}, {"models", "models"}, {"chat", "chat"}},
         {{"name", "d"}, {"title", "Four"}, {"models", "models"}, {"chat", "chat"}},
         {{"name", "e"}, {"title", "Five"}, {"models", "models"}, {"chat", "chat"}}});
    json["permissions"].push_back({{"name", "ai.provide"}, {"reason", "Answers the assistant with its models."}});
    loaded = load(json);
    INFO(listed(loaded));
    CHECK(reports(loaded, Code::TooMany, "$.contributes.native.aiProviders"));

    const Json schema = Json::parse(manifestJsonSchema());
    const Json provider =
        schema["properties"]["contributes"]["properties"]["native"]["properties"]["aiProviders"]["items"];
    CHECK(provider["required"] == Json::array({"name", "title", "models", "chat"}));
}

TEST_CASE("ADR-0112: vision engines load with their kinds and languages, survive a save and a load, and name known "
          "kinds once each")
{
    Json json = nativeBase();
    json["contributes"]["native"]["visionEngines"] =
        Json::array({{{"name", "reader"},
                      {"title", "Fast reader"},
                      {"kinds", {"ocr", "classify"}},
                      {"languages", {"en-US", "zh-Hans"}}},
                     {{"name", "finder"}, {"title", "Boss finder"}, {"kinds", {"detect"}}}});
    LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    REQUIRE(loaded.manifest.contributes.native.has_value());
    const std::vector<NativeVisionEngine>& engines = loaded.manifest.contributes.native->visionEngines;
    REQUIRE(engines.size() == 2);
    CHECK(engines[0] == NativeVisionEngine{.name = "reader",
                                           .title = "Fast reader",
                                           .kinds = {"ocr", "classify"},
                                           .languages = {"en-US", "zh-Hans"}});
    CHECK(engines[1].kinds == std::vector<std::string>{"detect"});
    CHECK(engines[1].languages.empty());
    const LoadedManifest again = loadManifest(saveManifest(loaded.manifest));
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == loaded.manifest);

    json["contributes"]["native"]["visionEngines"] =
        Json::array({{{"name", "reader"}, {"title", "Again"}, {"kinds", {"ocr", "faces", "ocr"}}},
                     {{"name", "reader"}, {"title", "Twice"}, {"kinds", Json::array()}},
                     {{"name", "blind"}, {"title", "Blind"}, {"kinds", {"classify"}}, {"languages", {"x", "zh_CN!"}}},
                     {{"name", "fourth"}, {"title", "Fourth"}, {"kinds", {"detect"}}},
                     {{"name", "fifth"}, {"title", "Fifth"}, {"kinds", {"detect"}}}});
    loaded = load(json);
    INFO(listed(loaded));
    CHECK_FALSE(loaded.usable);
    CHECK_FALSE(reports(loaded, Code::VisionEngineInvalid, "$.contributes.native.visionEngines[0].kinds[0]"));
    CHECK(reports(loaded, Code::VisionEngineInvalid, "$.contributes.native.visionEngines[0].kinds[1]"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.native.visionEngines[0].kinds[2]"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.native.visionEngines[1].name"));
    CHECK(reports(loaded, Code::VisionEngineInvalid, "$.contributes.native.visionEngines[1].kinds"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.visionEngines[2].languages[0]"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.visionEngines[2].languages[1]"));
    CHECK(reports(loaded, Code::TooMany, "$.contributes.native.visionEngines"));
    CHECK(codeName(Code::VisionEngineInvalid) == "vision-engine-invalid");
    CHECK_FALSE(fixHint(Code::VisionEngineInvalid).empty());
}

TEST_CASE("ADR-0112: native input sources load with their slots, survive a save and a load, and need names that "
          "bindings can use, kinds that exist, at least one slot and the permission input.provide")
{
    Json json = nativeBase();
    json["contributes"]["native"]["inputSources"] =
        Json::array({{{"name", "pedal2"},
                      {"title", "Foot pedal"},
                      {"slots", Json::array({{{"name", "left"}, {"kind", "button"}, {"title", "Left"}},
                                             {{"name", "Press_2-x"}, {"kind", "axis"}},
                                             {{"name", "tilt"}, {"kind", "stick"}, {"title", "Tilt"}}})}}});
    json["permissions"].push_back({{"name", "input.provide"}, {"reason", "Sends the pedal to the key mapping."}});
    LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    REQUIRE(loaded.manifest.contributes.native.has_value());
    const std::vector<NativeInputSource>& sources = loaded.manifest.contributes.native->inputSources;
    REQUIRE(sources.size() == 1);
    CHECK(sources[0].name == "pedal2");
    CHECK(sources[0].title == "Foot pedal");
    CHECK(sources[0].slotList ==
          std::vector<NativeInputSlot>{{.name = "left", .kind = InputSlotKind::Button, .title = "Left"},
                                       {.name = "Press_2-x", .kind = InputSlotKind::Axis, .title = {}},
                                       {.name = "tilt", .kind = InputSlotKind::Stick, .title = "Tilt"}});
    const LoadedManifest again = loadManifest(saveManifest(loaded.manifest));
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == loaded.manifest);
    CHECK(Json::parse(saveManifest(loaded.manifest))["contributes"]["native"]["inputSources"][0]["slots"][0]["kind"] ==
          "button");

    json["permissions"] = Json::array({{{"name", "notify"}, {"reason", "Shows when it beeps."}}});
    Json& declared = json["contributes"]["native"]["inputSources"];
    declared[0]["slots"][1]["kind"] = "trigger";
    declared[0]["slots"].push_back({{"name", "left"}, {"kind", "button"}});
    declared[0]["slots"].push_back({{"name", "a.b"}, {"kind", "button"}});
    const Json oneSlot = Json::array({{{"name", "x"}, {"kind", "button"}}});
    declared.push_back({{"name", "Pedal"}, {"title", "Capital"}, {"slots", oneSlot}});
    declared.push_back({{"name", "foot-pedal"}, {"title", "Hyphen"}, {"slots", oneSlot}});
    declared.push_back({{"name", "2pedal"}, {"title", "Digit first"}, {"slots", oneSlot}});
    declared.push_back({{"name", "pedal2"}, {"title", "Again"}, {"slots", oneSlot}});
    declared.push_back({{"name", "empty"}, {"title", "No slots"}});
    declared.push_back({{"name", "abcdefghijklmnopqrstuvwxyzabcdefg"}, {"title", "Long"}, {"slots", oneSlot}});
    loaded = load(json);
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::NativePermissionUndeclared, "$.contributes.native.inputSources"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.inputSources[0].slots[1].kind"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.native.inputSources[0].slots[3].name"));
    CHECK(reports(loaded, Code::InputNameInvalid, "$.contributes.native.inputSources[0].slots[4].name"));
    CHECK(reports(loaded, Code::InputNameInvalid, "$.contributes.native.inputSources[1].name"));
    CHECK(reports(loaded, Code::InputNameInvalid, "$.contributes.native.inputSources[2].name"));
    CHECK(reports(loaded, Code::InputNameInvalid, "$.contributes.native.inputSources[3].name"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.native.inputSources[4].name"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.native.inputSources[5].slots"));
    CHECK(reports(loaded, Code::TooLong, "$.contributes.native.inputSources[6].name"));

    json = nativeBase();
    json["permissions"].push_back({{"name", "input.provide"}, {"reason", "Sends the pads to the key mapping."}});
    Json many = Json::array();
    for (int index = 0; index < 9; ++index) {
        many.push_back({{"name", "pad" + std::to_string(index)}, {"title", "Pad"}, {"slots", oneSlot}});
    }
    Json slots = Json::array();
    for (int index = 0; index < 65; ++index) {
        slots.push_back({{"name", "s" + std::to_string(index)}, {"kind", "button"}});
    }
    many[0]["slots"] = slots;
    json["contributes"]["native"]["inputSources"] = many;
    loaded = load(json);
    CHECK(reports(loaded, Code::TooMany, "$.contributes.native.inputSources"));
    CHECK(reports(loaded, Code::TooMany, "$.contributes.native.inputSources[0].slots"));

    const Json schema = Json::parse(manifestJsonSchema());
    const Json source = schema["properties"]["contributes"]["properties"]["native"]["properties"]["inputSources"];
    CHECK(source["maxItems"] == kMostInputSources);
    CHECK(source["items"]["required"] == Json::array({"name", "title"}));
    CHECK(source["items"]["properties"]["name"]["pattern"] == "^[a-z][a-z0-9]{0,31}$");
    const Json slot = source["items"]["properties"]["slots"];
    CHECK(slot["maxItems"] == kMostInputSlots);
    CHECK(slot["items"]["required"] == Json::array({"name", "kind"}));
    CHECK(slot["items"]["properties"]["kind"]["enum"] == Json::array({"button", "axis", "stick"}));
    CHECK(slot["items"]["properties"]["name"]["pattern"] == "^[A-Za-z0-9_-]{1,32}$");
    CHECK(codeName(Code::InputNameInvalid) == "input-name-invalid");
    CHECK(std::string(fixHint(Code::InputNameInvalid)).find("Ext.<extension id>.<source>.<slot>") != std::string::npos);
}

TEST_CASE("ADR-0112: native transports load with their titles and icons, survive a save and a load, and need the "
          "permission transport")
{
    Json json = nativeBase();
    json["contributes"]["native"]["transports"] =
        Json::array({{{"name", "relay"}, {"title", "Relay server"}, {"icon", "svg:icons/relay.svg"}},
                     {{"name", "lan"}, {"title", "LAN"}}});
    json["permissions"].push_back({{"name", "transport"}, {"reason", "Reaches phones through the relay."}});
    json["permissions"].push_back(
        {{"name", "network"}, {"reason", "Talks to the relay."}, {"hosts", Json::array({"relay.example.com"})}});
    LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    REQUIRE(loaded.manifest.contributes.native.has_value());
    CHECK(loaded.manifest.contributes.native->transports ==
          std::vector<NativeTransport>{{.name = "relay", .title = "Relay server", .icon = "svg:icons/relay.svg"},
                                       {.name = "lan", .title = "LAN"}});
    const LoadedManifest again = loadManifest(saveManifest(loaded.manifest));
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == loaded.manifest);

    json["permissions"] = Json::array({{{"name", "notify"}, {"reason", "Shows when it beeps."}}});
    json["contributes"]["native"]["transports"].push_back({{"name", "relay"}, {"title", "Again"}});
    json["contributes"]["native"]["transports"].push_back({{"name", "Relay"}, {"title", "Bad name"}});
    json["contributes"]["native"]["transports"].push_back({{"name", "untitled"}});
    loaded = load(json);
    INFO(listed(loaded));
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::NativePermissionUndeclared, "$.contributes.native.transports"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.native.transports[2].name"));
    CHECK(reports(loaded, Code::NativeNameInvalid, "$.contributes.native.transports[3].name"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.native.transports[4].title"));

    const Json schema = Json::parse(manifestJsonSchema());
    const Json transport =
        schema["properties"]["contributes"]["properties"]["native"]["properties"]["transports"]["items"];
    CHECK(transport["required"] == Json::array({"name", "title"}));
    CHECK(transport["properties"]["name"]["pattern"] == "^[a-z][A-Za-z0-9]*$");
}

TEST_CASE("ADR-0112: native frame taps load with their rate and size, survive a save and a load, are checked and "
          "need the permission media.read")
{
    Json json = nativeBase();
    json["contributes"]["native"]["frameTaps"] =
        Json::array({{{"name", "detector"}, {"title", "Find enemies"}, {"fps", 60}, {"size", 3840}},
                     {{"name", "reader"}, {"title", "Read the score"}}});
    json["permissions"].push_back({{"name", "media.read"}, {"reason", "Finds enemies in the picture."}});
    LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    REQUIRE(loaded.manifest.contributes.native.has_value());
    const std::vector<NativeFrameTap>& taps = loaded.manifest.contributes.native->frameTaps;
    REQUIRE(taps.size() == 2);
    CHECK(taps[0] == NativeFrameTap{.name = "detector", .title = "Find enemies", .fps = 60, .size = 3840});
    CHECK(taps[1].name == "reader");
    CHECK_FALSE(taps[1].fps.has_value());
    CHECK_FALSE(taps[1].size.has_value());
    const LoadedManifest again = loadManifest(saveManifest(loaded.manifest));
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == loaded.manifest);

    json["permissions"] = Json::array({{{"name", "notify"}, {"reason", "Shows when it beeps."}}});
    json["contributes"]["native"]["frameTaps"][0]["fps"] = 0;
    json["contributes"]["native"]["frameTaps"][1]["size"] = 4000;
    json["contributes"]["native"]["frameTaps"].push_back({{"name", "detector"}, {"title", "Again"}});
    json["contributes"]["native"]["frameTaps"].push_back({{"name", "Bad"}, {"title", "Bad"}, {"fps", 61}, {"size", 63}});
    json["contributes"]["native"]["frameTaps"].push_back({{"name", "slow"}, {"fps", 2.5}});
    loaded = load(json);
    INFO(listed(loaded));
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::NativePermissionUndeclared, "$.contributes.native.frameTaps"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.frameTaps[0].fps"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.frameTaps[1].size"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.native.frameTaps[2].name"));
    CHECK(reports(loaded, Code::NativeNameInvalid, "$.contributes.native.frameTaps[3].name"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.frameTaps[3].fps"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.frameTaps[3].size"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.native.frameTaps[4].title"));
    CHECK(reports(loaded, Code::WrongType, "$.contributes.native.frameTaps[4].fps"));

    json = nativeBase();
    json["contributes"]["native"]["frameTaps"] = Json::array();
    for (int index = 0; index < 9; ++index) {
        json["contributes"]["native"]["frameTaps"].push_back(
            {{"name", "tap" + std::to_string(index)}, {"title", "Tap"}});
    }
    json["permissions"].push_back({{"name", "media.read"}, {"reason", "Reads the picture."}});
    loaded = load(json);
    CHECK(reports(loaded, Code::TooMany, "$.contributes.native.frameTaps"));

    const Json schema = Json::parse(manifestJsonSchema());
    const Json tap = schema["properties"]["contributes"]["properties"]["native"]["properties"]["frameTaps"]["items"];
    CHECK(tap["required"] == Json::array({"name", "title"}));
    CHECK(tap["properties"]["fps"]["minimum"] == 1);
    CHECK(tap["properties"]["fps"]["maximum"] == kMostTapFps);
    CHECK(tap["properties"]["size"]["minimum"] == kLeastTapSize);
    CHECK(tap["properties"]["size"]["maximum"] == kMostTapSize);
    CHECK(std::ranges::find(kPermissionNames, std::string_view("media.read")) != kPermissionNames.end());
}

namespace {

Json panelBase()
{
    Json json = base();
    json["id"] = "alice.stats-panel";
    json["requires"] = {{"panel", "^1.0"}, {"script", "^1.0"}};
    json["contributes"] = {
        {"scripts", Json::array({{{"name", "session"}, {"entry", "session.ts"}, {"run", "session"}},
                                 {{"name", "app"}, {"entry", "app.ts"}, {"run", "app"}}})},
        {"panels", Json::array({{{"name", "stats"},
                                 {"title", "Stats"},
                                 {"icon", "activity"},
                                 {"where", "mirror"},
                                 {"script", "session"}},
                                {{"name", "menu"}, {"title", "Stats tools"}, {"where", "menu"}, {"script", "session"}},
                                {{"name", "fps"}, {"title", "Frame rate"}, {"where", "status"}, {"script", "session"}},
                                {{"name", "history"},
                                 {"title", "History"},
                                 {"icon", "history"},
                                 {"where", "page"},
                                 {"script", "app"}}})}};
    json["l10n"] = {{"zh_CN", {{"panels.stats.title", "统计"}}}};
    return json;
}

bool undeclared(const LoadedManifest& loaded, std::string_view path, std::string_view point)
{
    return std::ranges::any_of(loaded.diagnostics, [&](const Diagnostic& diagnostic) {
        return diagnostic.code == Code::ContributionUndeclared && diagnostic.path == path && diagnostic.detail == point;
    });
}

Json feelBase()
{
    Json json = nativeBase();
    json["contributes"]["native"]["attachments"] = Json::array(
        {{{"name", "deadZone"},
          {"title", "Dead zone"},
          {"description", "Ignores small stick moves."},
          {"appliesTo", {"wasd", "padStick"}},
          {"params", Json::array({{{"key", "size"},
                                   {"type", "number"},
                                   {"title", "Size"},
                                   {"default", 0.1},
                                   {"min", 0},
                                   {"max", 0.5},
                                   {"step", 0.01},
                                   {"unit", "%"}},
                                  {{"key", "curve"},
                                   {"type", "curve"},
                                   {"title", "Curve"},
                                   {"default", Json::array({Json::array({0, 0}), Json::array({1, 1})})}}})}},
         {{"name", "randomLanding"},
          {"title", "Random landing"},
          {"appliesTo", {"*"}},
          {"params",
           Json::array({{{"key", "radius"}, {"type", "number"}, {"title", "Radius"}, {"default", 0.05},
                         {"min", 0.001}, {"max", 1}, {"log", true}},
                        {{"key", "taps"}, {"type", "integer"}, {"title", "Taps"}, {"default", 3}, {"min", 1},
                         {"max", 9}},
                        {{"key", "mode"}, {"type", "choice"}, {"title", "Mode"}, {"options", {"even", "near"}},
                         {"default", "near"}},
                        {{"key", "on"}, {"type", "toggle"}, {"title", "On"}, {"default", true}},
                        {{"key", "key"}, {"type", "key"}, {"title", "Key"}, {"default", "KeyQ"}},
                        {{"key", "spot"}, {"type", "point"}, {"title", "Spot"}, {"default", {0.5, -0.5}}},
                        {{"key", "area"}, {"type", "region"}, {"title", "Area"}, {"shape", "rect"},
                         {"default", {0, 0, 0.5, 0.5}}},
                        {{"key", "tint"}, {"type", "color"}, {"title", "Tint"}, {"default", "#F5C542"}}})}}});
    json["contributes"]["native"]["behaviors"] = Json::array(
        {{{"name", "circle"},
          {"title", "Circle"},
          {"slots", Json::array({{{"name", "press"}, {"kind", "key"}, {"title", "Hold"}}})},
          {"fingers", 1},
          {"frames", true},
          {"params", Json::array({{{"key", "radius"}, {"type", "number"}, {"title", "Radius"}, {"default", 0.3},
                                   {"min", 0}, {"max", 2}},
                                  {{"key", "period"}, {"type", "integer"}, {"title", "Period"}, {"default", 800},
                                   {"min", 100}, {"max", 10000}, {"unit", "ms"}}})}}});
    return json;
}

Json named(std::string_view prefix, int count, const Json& extra = Json::object())
{
    Json list = Json::array();
    for (int index = 0; index < count; ++index) {
        Json item = extra;
        item[prefix == "key" ? "key" : "name"] = "n" + std::to_string(index);
        list.push_back(std::move(item));
    }
    return list;
}

}

TEST_CASE("panel contributions name a place, a script and a title, load and survive a save and a load (ADR-0105, "
          "SCR-025)")
{
    const LoadedManifest loaded = load(panelBase());
    INFO(listed(loaded));
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    CHECK(loaded.manifest.declares(ExtensionPoint::Panel));
    CHECK(extensionPointNamed("panel") == ExtensionPoint::Panel);
    CHECK(nameOf(ExtensionPoint::Panel) == "panel");
    const std::vector<PanelContribution>& panels = loaded.manifest.contributes.panels;
    REQUIRE(panels.size() == 4);
    CHECK(panels[0] == PanelContribution{"stats", "Stats", "activity", PanelPlace::Mirror, "session"});
    CHECK(panels[1].where == PanelPlace::Menu);
    CHECK(panels[1].icon.empty());
    CHECK(panels[2].where == PanelPlace::Status);
    CHECK(panels[3].where == PanelPlace::Page);
    CHECK(panels[3].script == "app");

    const LoadedManifest again = loadManifest(saveManifest(loaded.manifest));
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == loaded.manifest);
    CHECK(Json::parse(saveManifest(loaded.manifest))["contributes"]["panels"][2]["where"] == "status");

    Json schema = Json::parse(manifestJsonSchema());
    Json panelSchema = schema["properties"]["contributes"]["properties"]["panels"];
    CHECK(panelSchema["maxItems"] == kMaxPanels);
    CHECK(panelSchema["items"]["required"] == Json::array({"name", "title", "where", "script"}));
    CHECK(panelSchema["items"]["properties"]["name"]["pattern"] == "^[a-z0-9-]{1,32}$");
    CHECK(panelSchema["items"]["properties"]["where"]["enum"] ==
          Json::array({"mirror", "menu", "status", "page", "device", "settings", "extension", "toolbar", "contextMenu",
                       "tray"}));
    CHECK(panelSchema["items"]["properties"]["title"]["maxLength"] == 40);
    const Json points = schema["properties"]["requires"]["propertyNames"]["enum"];
    CHECK(std::find(points.begin(), points.end(), Json("panel")) != points.end());
    CHECK(std::find(points.begin(), points.end(), Json("help")) != points.end());
    CHECK(std::find(points.begin(), points.end(), Json("tour")) != points.end());
}

TEST_CASE("panel contributions are checked: names, places, titles, at most eight, one menu and both points "
          "(ADR-0105)")
{
    Json json = panelBase();
    json["contributes"]["panels"][0]["name"] = "Stats";
    json["contributes"]["panels"][2]["where"] = "sidebar";
    json["contributes"]["panels"][3].erase("script");
    json["contributes"]["panels"][3].erase("title");
    json["contributes"]["panels"].push_back(
        {{"name", "menu"}, {"title", "Again"}, {"where", "menu"}, {"script", "session"}});
    LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::PanelNameInvalid, "$.contributes.panels[0].name"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.panels[2].where"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.panels[3].script"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.panels[3].title"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.panels[4].name"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.panels[4].where"));

    for (const char* bad : {"stats panel", "stats_panel", "图表", "Stats"}) {
        json = panelBase();
        json["contributes"]["panels"][0]["name"] = bad;
        CHECK_MESSAGE(reports(load(json), Code::PanelNameInvalid, "$.contributes.panels[0].name"), bad);
    }
    json = panelBase();
    json["contributes"]["panels"][0]["name"] = "";
    CHECK(reports(load(json), Code::MissingField, "$.contributes.panels[0].name"));
    json["contributes"]["panels"][0]["name"] = std::string(33, 'a');
    CHECK(reports(load(json), Code::TooLong, "$.contributes.panels[0].name"));
    json["contributes"]["panels"][0]["name"] = std::string(32, 'a');
    json["contributes"]["panels"][0]["title"] = std::string(41, 'x');
    loaded = load(json);
    CHECK(reports(loaded, Code::TooLong, "$.contributes.panels[0].title"));
    CHECK_FALSE(reports(loaded, Code::TooLong, "$.contributes.panels[0].name"));

    json = panelBase();
    json["contributes"]["panels"][0]["name"] = "menu";
    json["contributes"]["panels"][1]["name"] = "tools";
    loaded = load(json);
    CHECK(reports(loaded, Code::PanelNameInvalid, "$.contributes.panels[0].name"));
    CHECK_FALSE(reports(loaded, Code::PanelNameInvalid, "$.contributes.panels[1].name"));

    json = panelBase();
    for (int index = 0; index < 6; ++index) {
        json["contributes"]["panels"].push_back({{"name", "extra-" + std::to_string(index)},
                                                 {"title", "Extra"},
                                                 {"where", "mirror"},
                                                 {"script", "session"}});
    }
    loaded = load(json);
    CHECK(reports(loaded, Code::TooMany, "$.contributes.panels"));
    CHECK(loaded.manifest.contributes.panels.size() == kMaxPanels);

    json = panelBase();
    json["requires"] = {{"script", "^1.0"}};
    loaded = load(json);
    CHECK_FALSE(loaded.usable);
    CHECK(undeclared(loaded, "$.contributes.panels", "panel"));
    CHECK_FALSE(undeclared(loaded, "$.contributes.panels", "script"));
    json["requires"] = {{"panel", "^1.0"}};
    loaded = load(json);
    CHECK_FALSE(loaded.usable);
    CHECK(undeclared(loaded, "$.contributes.panels", "script"));
    CHECK_FALSE(undeclared(loaded, "$.contributes.panels", "panel"));

    json = panelBase();
    json["contributes"]["panels"] = Json::object();
    CHECK(reports(load(json), Code::WrongType, "$.contributes.panels"));

    CHECK(codeName(Code::PanelNameInvalid) == "panel-name-invalid");
    CHECK(std::string(fixHint(Code::PanelNameInvalid)).find("ui.panel") != std::string::npos);
}

TEST_CASE("native attachments and behaviors with every kind of parameter load and survive a save and a load "
          "(ADR-0105)")
{
    const LoadedManifest loaded = load(feelBase());
    INFO(listed(loaded));
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    REQUIRE(loaded.manifest.contributes.native.has_value());
    const NativeContribution& native = *loaded.manifest.contributes.native;
    REQUIRE(native.attachments.size() == 2);
    const NativeAttachment& deadZone = native.attachments[0];
    CHECK(deadZone.name == "deadZone");
    CHECK(deadZone.title == "Dead zone");
    CHECK(deadZone.description == "Ignores small stick moves.");
    CHECK(deadZone.appliesTo == std::vector<std::string>{"wasd", "padStick"});
    REQUIRE(deadZone.params.size() == 2);
    CHECK(deadZone.params[0].key == "size");
    CHECK(deadZone.params[0].type == DeclaredParamType::Number);
    CHECK(deadZone.params[0].defaultValue == "0.1");
    CHECK(deadZone.params[0].min == 0.0);
    CHECK(deadZone.params[0].max == 0.5);
    CHECK(deadZone.params[0].step == 0.01);
    CHECK(deadZone.params[0].unit == "%");
    CHECK(deadZone.params[1].type == DeclaredParamType::Curve);
    CHECK(deadZone.params[1].defaultValue == "[[0,0],[1,1]]");

    const NativeAttachment& landing = native.attachments[1];
    CHECK(landing.appliesTo == std::vector<std::string>{"*"});
    REQUIRE(landing.params.size() == 8);
    CHECK(landing.params[0].log);
    CHECK(landing.params[1].type == DeclaredParamType::Integer);
    CHECK(landing.params[1].defaultValue == "3");
    CHECK(landing.params[2].options == std::vector<std::string>{"even", "near"});
    CHECK(landing.params[2].defaultValue == "\"near\"");
    CHECK(landing.params[3].defaultValue == "true");
    CHECK(landing.params[4].type == DeclaredParamType::Key);
    CHECK(landing.params[5].defaultValue == "[0.5,-0.5]");
    CHECK(landing.params[6].shape == RegionShape::Rect);
    CHECK(landing.params[6].defaultValue == "[0,0,0.5,0.5]");
    CHECK(landing.params[7].defaultValue == "\"#F5C542\"");

    REQUIRE(native.behaviors.size() == 1);
    const NativeBehavior& circle = native.behaviors[0];
    CHECK(circle.name == "circle");
    CHECK(circle.title == "Circle");
    REQUIRE(circle.slotList.size() == 1);
    CHECK(circle.slotList[0] == DeclaredSlot{"press", NativeSlotKind::Key, "Hold"});
    CHECK(circle.fingers == 1);
    CHECK(circle.frames);
    REQUIRE(circle.params.size() == 2);
    CHECK(circle.params[1].unit == "ms");

    const LoadedManifest again = loadManifest(saveManifest(loaded.manifest));
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == loaded.manifest);
    const Json saved = Json::parse(saveManifest(loaded.manifest));
    CHECK(saved["contributes"]["native"]["attachments"][1]["params"][6]["shape"] == "rect");
    CHECK_FALSE(saved["contributes"]["native"]["attachments"][1]["params"][5].contains("shape"));
    CHECK(saved["contributes"]["native"]["behaviors"][0]["frames"] == true);

    CHECK(declaredDefault(deadZone.params[0]) == "0.1");
    CHECK(declaredProblem(deadZone.params[0], "0.2").empty());
    CHECK(declaredProblem(deadZone.params[0], "0.6") == "outside min and max");
    CHECK(declaredProblem(deadZone.params[0], "\"wide\"") == "not a number");
    CHECK(declaredProblem(deadZone.params[0], "{") == "not JSON");
    CHECK(declaredProblem(deadZone.params[1], "[[0,0],[0.5,0.2],[1,1]]").empty());
    CHECK(declaredProblem(deadZone.params[1], "[[0,0]]") == "not 2 to 16 points");
    CHECK(declaredProblem(landing.params[2], "\"far\"") == "not one of the options");
    CHECK(declaredProblem(landing.params[6], "[0,0,1,1]").empty());
    CHECK(declaredProblem(landing.params[6], "[0,0,1]") == "not [x, y, w, h]");

    CHECK(knownControlType("behavior"));
    CHECK(knownControlType("wasd"));
    CHECK_FALSE(knownControlType("*"));
    CHECK_FALSE(knownControlType("joystick"));
}

TEST_CASE("a declared parameter without a valid default starts from its type's default (ADR-0105)")
{
    DeclaredParam param;
    param.key = "value";
    param.type = DeclaredParamType::Number;
    param.min = 0.25;
    CHECK(declaredDefault(param) == "0.25");
    param.defaultValue = "\"x\"";
    CHECK(declaredDefault(param) == "0.25");
    param.defaultValue = "0.5";
    CHECK(declaredDefault(param) == "0.5");

    param.defaultValue.clear();
    param.type = DeclaredParamType::Integer;
    param.min = 2.0;
    param.max = 9.0;
    CHECK(declaredDefault(param) == "2");
    param.min.reset();
    param.max = -3.0;
    CHECK(declaredDefault(param) == "-3");

    param.min.reset();
    param.max.reset();
    param.type = DeclaredParamType::Toggle;
    CHECK(declaredDefault(param) == "false");
    param.type = DeclaredParamType::Choice;
    param.options = {"soft", "hard"};
    CHECK(declaredDefault(param) == "\"soft\"");
    param.type = DeclaredParamType::Key;
    CHECK(declaredDefault(param) == "\"\"");
    param.type = DeclaredParamType::Point;
    CHECK(declaredDefault(param) == "[0,0]");
    param.type = DeclaredParamType::Region;
    CHECK(declaredDefault(param) == "[0,0,0.1]");
    param.shape = RegionShape::Rect;
    CHECK(declaredDefault(param) == "[0,0,0.2,0.2]");
    param.type = DeclaredParamType::Curve;
    CHECK(declaredDefault(param) == "[[0,0],[1,1]]");
    param.type = DeclaredParamType::Color;
    CHECK(declaredDefault(param) == "\"#FFFFFF\"");
    CHECK(declaredProblem(param, "\"#ffcc00\"").empty());
    CHECK(declaredProblem(param, "\"#FFCC0\"") == "not #RRGGBB");
}

TEST_CASE("native attachments and behaviors are checked for limits, names, defaults, fields that do not apply and "
          "the components they fit (ADR-0105)")
{
    Json json = feelBase();
    json["contributes"]["native"]["attachments"] = named("name", 33, {{"title", "A"}});
    LoadedManifest loaded = load(json);
    CHECK(reports(loaded, Code::TooMany, "$.contributes.native.attachments"));
    CHECK_FALSE(loaded.usable);

    json = feelBase();
    json["contributes"]["native"]["behaviors"] = named("name", 33, {{"title", "B"}});
    CHECK(reports(load(json), Code::TooMany, "$.contributes.native.behaviors"));

    json = feelBase();
    json["contributes"]["native"]["attachments"][0]["params"] = named("key", 25, {{"type", "toggle"}, {"title", "T"}});
    CHECK(reports(load(json), Code::TooMany, "$.contributes.native.attachments[0].params"));

    json = feelBase();
    json["contributes"]["native"]["behaviors"][0]["slots"] = named("name", 9, {{"kind", "key"}});
    CHECK(reports(load(json), Code::TooMany, "$.contributes.native.behaviors[0].slots"));

    json = feelBase();
    json["contributes"]["native"]["attachments"].push_back({{"name", "deadZone"}, {"title", "Again"}});
    json["contributes"]["native"]["attachments"][0]["params"].push_back(
        {{"key", "size"}, {"type", "number"}, {"title", "Size"}});
    json["contributes"]["native"]["behaviors"][0]["slots"].push_back({{"name", "press"}, {"kind", "axis2"}});
    json["contributes"]["native"]["attachments"][1].erase("title");
    json["contributes"]["native"]["behaviors"][0]["fingers"] = 11;
    json["contributes"]["native"]["behaviors"][0]["slots"][0]["kind"] = "stick";
    json["contributes"]["native"]["attachments"][1]["params"][0]["type"] = "slider";
    json["contributes"]["native"]["attachments"][1]["params"][1]["key"] = "two-words";
    loaded = load(json);
    INFO(listed(loaded));
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.native.attachments[2].name"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.native.attachments[0].params[2].key"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.native.behaviors[0].slots[1].name"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.native.attachments[1].title"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.behaviors[0].fingers"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.behaviors[0].slots[0].kind"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.attachments[1].params[0].type"));
    CHECK(reports(loaded, Code::NativeNameInvalid, "$.contributes.native.attachments[1].params[1].key"));

    json = feelBase();
    Json& params = json["contributes"]["native"]["attachments"][1]["params"];
    params[0]["default"] = 2;
    params[1]["default"] = 2.5;
    params[2]["default"] = "far";
    params[3]["default"] = "yes";
    params[4]["default"] = 7;
    params[5]["default"] = {3, 0};
    params[6]["default"] = {0, 0, 5, 1};
    params[7]["default"] = "red";
    json["contributes"]["native"]["attachments"][0]["params"][1]["default"] =
        Json::array({Json::array({0, 0}), Json::array({0, 1})});
    loaded = load(json);
    INFO(listed(loaded));
    CHECK_FALSE(loaded.usable);
    for (int index = 0; index < 8; ++index) {
        CHECK_MESSAGE(reports(loaded, Code::NativeParamInvalid,
                              "$.contributes.native.attachments[1].params[" + std::to_string(index) + "].default"),
                      index);
    }
    CHECK(reports(loaded, Code::NativeParamInvalid, "$.contributes.native.attachments[0].params[1].default"));

    json = feelBase();
    Json& sized = json["contributes"]["native"]["attachments"][0]["params"][0];
    sized["min"] = 0.6;
    sized["step"] = 0;
    json["contributes"]["native"]["attachments"][1]["params"][0]["min"] = 0;
    json["contributes"]["native"]["attachments"][1]["params"][1]["max"] = 9.5;
    json["contributes"]["native"]["attachments"][1]["params"][2].erase("options");
    json["contributes"]["native"]["attachments"][1]["params"][2].erase("default");
    loaded = load(json);
    INFO(listed(loaded));
    CHECK(reports(loaded, Code::NativeParamInvalid, "$.contributes.native.attachments[0].params[0].min"));
    CHECK(reports(loaded, Code::NativeParamInvalid, "$.contributes.native.attachments[0].params[0].step"));
    CHECK(reports(loaded, Code::NativeParamInvalid, "$.contributes.native.attachments[1].params[0].log"));
    CHECK(reports(loaded, Code::NativeParamInvalid, "$.contributes.native.attachments[1].params[1].max"));
    CHECK(reports(loaded, Code::NativeParamInvalid, "$.contributes.native.attachments[1].params[2].options"));

    json = feelBase();
    json["contributes"]["native"]["attachments"][1]["params"][3]["min"] = 1;
    json["contributes"]["native"]["attachments"][1]["params"][0]["options"] = {"a", "b"};
    json["contributes"]["native"]["attachments"][1]["params"][5]["shape"] = "rect";
    json["contributes"]["native"]["attachments"][1]["params"][7]["unit"] = "px";
    json["contributes"]["native"]["attachments"][0]["appliesTo"] = {"wasd", "joystick"};
    loaded = load(json);
    INFO(listed(loaded));
    CHECK(loaded.usable);
    CHECK(reports(loaded, Code::NativeParamUnused, "$.contributes.native.attachments[1].params[3].min"));
    CHECK(reports(loaded, Code::NativeParamUnused, "$.contributes.native.attachments[1].params[0].options"));
    CHECK(reports(loaded, Code::NativeParamUnused, "$.contributes.native.attachments[1].params[5].shape"));
    CHECK(reports(loaded, Code::NativeParamUnused, "$.contributes.native.attachments[1].params[7].unit"));
    CHECK(reports(loaded, Code::NativeTargetUnknown, "$.contributes.native.attachments[0].appliesTo[1]"));
    CHECK_FALSE(reports(loaded, Code::NativeTargetUnknown, "$.contributes.native.attachments[0].appliesTo[0]"));

    CHECK(codeName(Code::NativeParamInvalid) == "native-param-invalid");
    CHECK(codeName(Code::NativeParamUnused) == "native-param-unused");
    CHECK(codeName(Code::NativeTargetUnknown) == "native-target-unknown");
    CHECK_FALSE(std::string(fixHint(Code::NativeParamInvalid)).empty());
    CHECK(std::string(fixHint(Code::NativeMismatch)).find("behaviors") != std::string::npos);
}

TEST_CASE("the manifest schema describes native attachments, behaviors, their slots and their parameters (ADR-0105)")
{
    const Json schema = Json::parse(manifestJsonSchema());
    const Json native = schema["properties"]["contributes"]["properties"]["native"]["properties"];
    const Json attachment = native["attachments"]["items"];
    CHECK(native["attachments"]["maxItems"] == kMostNativeAttachments);
    CHECK(attachment["required"] == Json::array({"name", "title"}));
    const Json param = attachment["properties"]["params"]["items"];
    CHECK(attachment["properties"]["params"]["maxItems"] == kMostDeclaredParams);
    CHECK(param["required"] == Json::array({"key", "type", "title"}));
    CHECK(param["properties"]["type"]["enum"].size() == kParamTypeNames.size());
    CHECK(param["properties"]["shape"]["enum"] == Json::array({"circle", "rect"}));
    CHECK(contains(param["properties"]["default"]["type"], "array"));
    const Json behavior = native["behaviors"]["items"];
    CHECK(native["behaviors"]["maxItems"] == kMostNativeBehaviors);
    CHECK(behavior["required"] == Json::array({"name", "title"}));
    CHECK(behavior["properties"]["slots"]["maxItems"] == kMostDeclaredSlots);
    CHECK(behavior["properties"]["slots"]["items"]["required"] == Json::array({"name", "kind"}));
    CHECK(behavior["properties"]["slots"]["items"]["properties"]["kind"]["enum"] ==
          Json::array({"key", "axis2", "axis1"}));
    CHECK(behavior["properties"]["fingers"]["maximum"] == 10);
}

TEST_CASE("ADR-0112: panels show for a device, in Settings, in the extension's settings window, on the toolbar, in "
          "card menus and in the tray, and only a card menu names the cards it shows on")
{
    Json json = panelBase();
    json["contributes"]["panels"] = Json::array(
        {{{"name", "board"}, {"title", "Board settings"}, {"where", "device"}, {"script", "app"}},
         {{"name", "options"}, {"title", "Options"}, {"where", "settings"}, {"script", "app"}},
         {{"name", "main"}, {"title", "Setup"}, {"icon", "svg:icons/board.svg"}, {"where", "extension"}, {"script", "app"}},
         {{"name", "quick"}, {"title", "Quick"}, {"where", "toolbar"}, {"script", "session"}},
         {{"name", "more"}, {"title", "More"}, {"where", "contextMenu"}, {"script", "app"}, {"on", {"device", "profile"}}},
         {{"name", "tray"}, {"title", "Tray"}, {"where", "tray"}, {"script", "app"}}});
    LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK(loaded.usable);
    const std::vector<PanelContribution>& panels = loaded.manifest.contributes.panels;
    REQUIRE(panels.size() == 6);
    CHECK(panels[0].where == PanelPlace::Device);
    CHECK(panels[1].where == PanelPlace::Settings);
    CHECK(panels[2].where == PanelPlace::Extension);
    CHECK(panels[2].icon == "svg:icons/board.svg");
    CHECK(panels[3].where == PanelPlace::Toolbar);
    CHECK(panels[4].where == PanelPlace::ContextMenu);
    CHECK(panels[4].on == std::vector<std::string>{"device", "profile"});
    CHECK(panels[5].where == PanelPlace::Tray);
    const Json saved = Json::parse(saveManifest(loaded.manifest))["contributes"]["panels"];
    CHECK(saved[3]["where"] == "toolbar");
    CHECK(saved[4]["where"] == "contextMenu");
    CHECK(saved[4]["on"] == Json::array({"device", "profile"}));
    CHECK_FALSE(saved[1].contains("on"));
    for (const PanelPlace place : {PanelPlace::Page, PanelPlace::Device, PanelPlace::Settings, PanelPlace::Extension,
                                   PanelPlace::ContextMenu, PanelPlace::Tray}) {
        CHECK(panelForApp(place));
    }
    for (const PanelPlace place : {PanelPlace::Mirror, PanelPlace::Menu, PanelPlace::Status, PanelPlace::Toolbar}) {
        CHECK_FALSE(panelForApp(place));
    }

    json["contributes"]["panels"][4].erase("on");
    json["contributes"]["panels"][5]["on"] = {"device"};
    loaded = load(json);
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::MissingField, "$.contributes.panels[4].on"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.panels[5].on"));

    json["contributes"]["panels"][4]["on"] = {"profile", "profile"};
    json["contributes"]["panels"][5].erase("on");
    loaded = load(json);
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.panels[4].on[1]"));
    CHECK_FALSE(reports(loaded, Code::OutOfRange, "$.contributes.panels[4].on[0]"));
    json["contributes"]["panels"][4]["on"] = {"window"};
    CHECK(reports(load(json), Code::OutOfRange, "$.contributes.panels[4].on[0]"));
}

TEST_CASE("ADR-0112: an extension adds help articles in Markdown, a file for each language, under the help point")
{
    Json json = base();
    json["id"] = "alice.helpful";
    json["requires"] = {{"help", "^1.0"}};
    json["contributes"] = {
        {"help", Json::array({{{"id", "setup"},
                               {"title", "Setting up"},
                               {"file", "help/setup.md"},
                               {"locales", {{"zh_CN", "help/setup.zh_CN.md"}}},
                               {"icon", "svg:icons/board.svg"}},
                              {{"id", "faq"}, {"title", "Questions"}, {"file", "help/faq.MD"}}})}};
    LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK(loaded.usable);
    REQUIRE(loaded.manifest.contributes.help.size() == 2);
    const HelpContribution& setup = loaded.manifest.contributes.help[0];
    CHECK(setup.id == "setup");
    CHECK(setup.file == "help/setup.md");
    CHECK(setup.locales.at("zh_CN") == "help/setup.zh_CN.md");
    CHECK(setup.icon == "svg:icons/board.svg");
    CHECK(loaded.manifest.declares(ExtensionPoint::Help));
    CHECK(extensionPointNamed("help") == ExtensionPoint::Help);
    CHECK(nameOf(ExtensionPoint::Help) == "help");
    const Json saved = Json::parse(saveManifest(loaded.manifest))["contributes"]["help"];
    CHECK(saved[0]["locales"]["zh_CN"] == "help/setup.zh_CN.md");
    CHECK_FALSE(saved[1].contains("locales"));

    json["contributes"]["help"][1]["id"] = "setup";
    json["contributes"]["help"][0]["file"] = "help/setup.txt";
    json["contributes"]["help"][0]["locales"] = {{"chinese", "help/a.md"}, {"ja", "help/ja.txt"}};
    loaded = load(json);
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.help[1].id"));
    CHECK(reports(loaded, Code::PathInvalid, "$.contributes.help[0].file"));
    CHECK(reports(loaded, Code::OutOfRange, R"($.contributes.help[0].locales["chinese"])"));
    CHECK(reports(loaded, Code::PathInvalid, R"($.contributes.help[0].locales["ja"])"));

    json = base();
    json["requires"] = {{"script", "^1.0"}};
    json["contributes"] = {{"help", Json::array({{{"id", "setup"}, {"title", "Setting up"}, {"file", "a.md"}}})}};
    loaded = load(json);
    CHECK_FALSE(loaded.usable);
    CHECK(undeclared(loaded, "$.contributes.help", "help"));
}

TEST_CASE("ADR-0112: an extension adds tours whose steps point at its card, its devices or its own panels")
{
    const auto step = [](const char* target, const char* title) {
        Json made = {{"title", title}, {"text", std::string(title) + " text."}};
        if (target[0] != '\0') {
            made["target"] = target;
        }
        return made;
    };
    Json json = base();
    json["id"] = "alice.tours";
    json["requires"] = {{"tour", "^1.0"}, {"script", "^1.0"}, {"panel", "^1.0"}};
    json["contributes"] = {
        {"scripts", Json::array({{{"name", "app"}, {"entry", "app.js"}, {"run", "app"}}})},
        {"panels", Json::array({{{"name", "home"}, {"title", "Home"}, {"where", "page"}, {"script", "app"}},
                                {{"name", "options"}, {"title", "Options"}, {"where", "settings"}, {"script", "app"}}})},
        {"tours", Json::array({{{"id", "intro"},
                                {"title", "Getting started"},
                                {"steps", Json::array({step("", "Welcome"), step("card", "Its card"),
                                                       step("page:home", "Its page"),
                                                       step("settings:options", "Its settings")})}},
                               {{"id", "later"},
                                {"title", "Later"},
                                {"start", "manual"},
                                {"steps", Json::array({step("", "One"), step("", "Two")})}}})}};
    LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK(loaded.usable);
    REQUIRE(loaded.manifest.contributes.tours.size() == 2);
    const TourContribution& intro = loaded.manifest.contributes.tours[0];
    CHECK(intro.start == TourStart::Enabled);
    REQUIRE(intro.steps.size() == 4);
    CHECK(intro.steps[0].target.empty());
    CHECK(intro.steps[2].target == "page:home");
    CHECK(intro.steps[3].text == "Its settings text.");
    CHECK(loaded.manifest.contributes.tours[1].start == TourStart::Manual);
    CHECK(extensionPointNamed("tour") == ExtensionPoint::Tour);
    const Json saved = Json::parse(saveManifest(loaded.manifest))["contributes"]["tours"];
    CHECK(saved[1]["start"] == "manual");
    CHECK(saved[0]["steps"][1]["target"] == "card");

    json["contributes"]["tours"][0]["steps"][1]["target"] = "devices";
    json["contributes"]["tours"][0]["steps"][2]["target"] = "page:options";
    json["contributes"]["tours"][0]["steps"][3]["target"] = "device";
    json["contributes"]["tours"][1]["id"] = "intro";
    json["contributes"]["tours"][1]["steps"] = Json::array({step("", "Only")});
    loaded = load(json);
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::TourTargetInvalid, "$.contributes.tours[0].steps[1].target"));
    CHECK(reports(loaded, Code::TourTargetInvalid, "$.contributes.tours[0].steps[2].target"));
    CHECK(reports(loaded, Code::TourTargetInvalid, "$.contributes.tours[0].steps[3].target"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.tours[1].id"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.tours[1].steps"));
    CHECK(std::ranges::count_if(loaded.diagnostics, [](const Diagnostic& each) {
              return each.path == "$.contributes.tours[0].steps[1].target";
          }) == 1);

    json = base();
    json["requires"] = {{"script", "^1.0"}};
    json["contributes"] = {
        {"tours", Json::array({{{"id", "intro"}, {"title", "Intro"}, {"steps", Json::array({step("", "A"), step("", "B")})}}})}};
    loaded = load(json);
    CHECK_FALSE(loaded.usable);
    CHECK(undeclared(loaded, "$.contributes.tours", "tour"));
}

TEST_CASE("ADR-0112: services load with their schemas and the app script or node that answers them, and survive a "
          "save and a load")
{
    Json json = nativeBase();
    json["id"] = "alice.tools";
    json["requires"] = {{"native", "^1.0"}, {"script", "^1.0"}};
    json["contributes"]["scripts"] = Json::array({{{"name", "app"}, {"entry", "app.ts"}, {"run", "app"}},
                                                 {{"name", "phone"}, {"entry", "phone.ts"}, {"run", "session"}}});
    json["contributes"]["native"]["nodes"].push_back(
        {{"name", "sum"}, {"params", Json::array({{{"name", "args"}, {"type", "json"}}})}, {"returns", "json"}});
    json["contributes"]["services"] = Json::array(
        {{{"name", "add"},
          {"title", "Add"},
          {"description", "Adds two numbers."},
          {"input",
           {{"type", "object"},
            {"properties", {{"a", {{"type", "number"}}}, {"b", {{"type", "number"}}}}},
            {"required", {"a", "b"}}}},
          {"output", {{"type", "number"}}},
          {"script", "app"}},
         {{"name", "sum"}, {"description", "Adds up a list."}, {"node", "sum"}}});
    const LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    const std::vector<ServiceContribution>& services = loaded.manifest.contributes.services;
    REQUIRE(services.size() == 2);
    CHECK(services[0].name == "add");
    CHECK(services[0].title == "Add");
    CHECK(services[0].description == "Adds two numbers.");
    CHECK(services[0].script == "app");
    CHECK(services[0].node.empty());
    CHECK(Json::parse(services[0].input)["required"] == Json::array({"a", "b"}));
    CHECK(services[0].output == R"({"type":"number"})");
    CHECK(services[1].node == "sum");
    CHECK(services[1].title.empty());
    CHECK(services[1].input.empty());
    CHECK(services[1].output.empty());
    const LoadedManifest again = loadManifest(saveManifest(loaded.manifest));
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == loaded.manifest);
    const Json saved = Json::parse(saveManifest(loaded.manifest))["contributes"]["services"];
    CHECK(saved[0]["input"]["properties"]["b"]["type"] == "number");
    CHECK_FALSE(saved[1].contains("input"));
    CHECK_FALSE(saved[1].contains("script"));

    const Json schema = Json::parse(manifestJsonSchema())["properties"]["contributes"]["properties"]["services"];
    CHECK(schema["maxItems"] == kMaxServices);
    CHECK(schema["items"]["required"] == Json::array({"name", "description"}));
}

TEST_CASE("ADR-0112: a service is checked for its name, its schemas, and an app script or a json node that answers it, "
          "under the script or native point")
{
    Json json = nativeBase();
    json["id"] = "alice.tools";
    json["requires"] = {{"native", "^1.0"}, {"script", "^1.0"}};
    json["contributes"]["scripts"] = Json::array({{{"name", "app"}, {"entry", "app.ts"}, {"run", "app"}},
                                                 {{"name", "phone"}, {"entry", "phone.ts"}, {"run", "session"}}});
    json["contributes"]["native"]["nodes"].push_back(
        {{"name", "sum"}, {"params", Json::array({{{"name", "args"}, {"type", "json"}}})}, {"returns", "json"}});
    json["contributes"]["services"] = Json::array(
        {{{"name", "add"},
          {"description", "Adds."},
          {"input", {{"type", "object"}, {"pattern", "x"}}},
          {"output", "number"},
          {"script", "phone"}},
         {{"name", "clock"}, {"description", "The time."}, {"node", "now"}},
         {{"name", "both"}, {"description", "Both."}, {"script", "app"}, {"node", "sum"}},
         {{"name", "neither"}, {"description", "Neither."}},
         {{"name", "lost"}, {"description", "Lost."}, {"script", "gone"}},
         {{"name", "add"}, {"description", "Again."}, {"node", "sum"}},
         {{"name", "Bad"}, {"description", "Bad."}, {"node", "sum"}},
         {{"name", "quiet"}, {"node", "missing"}},
         {{"name", "wide"}, {"description", "Wide."}, {"node", "sum"}, {"output", {{"minimum", 2}, {"maximum", 1}}}}});
    LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::ServiceTargetInvalid, "$.contributes.services[0].script"));
    CHECK(reports(loaded, Code::ServiceSchemaInvalid, "$.contributes.services[0].input"));
    CHECK(reports(loaded, Code::WrongType, "$.contributes.services[0].output"));
    CHECK(reports(loaded, Code::ServiceTargetInvalid, "$.contributes.services[1].node"));
    CHECK(reports(loaded, Code::ServiceTargetInvalid, "$.contributes.services[2].script"));
    CHECK(reports(loaded, Code::ServiceTargetInvalid, "$.contributes.services[3].script"));
    CHECK(reports(loaded, Code::ServiceTargetInvalid, "$.contributes.services[4].script"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.services[5].name"));
    CHECK(reports(loaded, Code::NativeNameInvalid, "$.contributes.services[6].name"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.services[7].description"));
    CHECK(reports(loaded, Code::ServiceTargetInvalid, "$.contributes.services[7].node"));
    CHECK(reports(loaded, Code::ServiceSchemaInvalid, "$.contributes.services[8].output"));
    const auto detail = std::ranges::find_if(loaded.diagnostics, [](const Diagnostic& each) {
        return each.path == "$.contributes.services[0].input";
    });
    REQUIRE(detail != loaded.diagnostics.end());
    CHECK(detail->detail.find("$.pattern") == 0);
    CHECK(codeName(Code::ServiceSchemaInvalid) == "service-schema-invalid");
    CHECK(codeName(Code::ServiceTargetInvalid) == "service-target-invalid");
    CHECK_FALSE(fixHint(Code::ServiceTargetInvalid).empty());

    json["contributes"]["services"] = Json::array();
    for (std::size_t index = 0; index <= kMaxServices; ++index) {
        json["contributes"]["services"].push_back(
            {{"name", "sum" + std::to_string(index)}, {"description", "Sums."}, {"node", "sum"}});
    }
    loaded = load(json);
    CHECK(reports(loaded, Code::TooMany, "$.contributes.services"));

    json = base();
    json["contributes"]["services"] = Json::array({{{"name", "add"}, {"description", "Adds."}, {"script", "app"}},
                                                  {{"name", "sum"}, {"description", "Sums."}, {"node", "sum"}}});
    loaded = load(json);
    CHECK_FALSE(loaded.usable);
    CHECK(undeclared(loaded, "$.contributes.services[0].script", "script"));
    CHECK(undeclared(loaded, "$.contributes.services[1].node", "native"));
}

namespace {

Json mediaBase()
{
    Json json = nativeBase();
    json["contributes"]["native"]["videoSources"] =
        Json::array({{{"name", "camera"}, {"title", "Camera"}, {"kinds", {"phone", "touchBox"}}},
                     {{"name", "screen"}, {"title", "Screen"}}});
    json["contributes"]["native"]["audioSources"] = Json::array({{{"name", "microphone"}, {"title", "Microphone"}}});
    json["contributes"]["native"]["mediaSinks"] =
        Json::array({{{"name", "camera"},
                      {"title", "Virtual camera"},
                      {"video", {{"fps", 30}, {"width", 1280}, {"height", 720}}}},
                     {{"name", "stream"},
                      {"title", "Live stream"},
                      {"video", {{"fps", 60}, {"width", 1920}, {"height", 1080}}},
                      {"audio", true}},
                     {{"name", "recorder"}, {"title", "Sound recorder"}, {"audio", true}}});
    json["contributes"]["native"]["recordingExporters"] =
        Json::array({{{"name", "gif"}, {"title", "GIF animation"}, {"extension", "gif"}}});
    json["permissions"].push_back({{"name", "media.capture"}, {"reason", "Shows the camera."}});
    json["permissions"].push_back({{"name", "media.output"}, {"reason", "Streams the window."}});
    return json;
}

}

TEST_CASE("ADR-0112: video and audio sources, media sinks and recording exporters load, survive a save and a load, and "
          "need media.capture and media.output")
{
    const LoadedManifest loaded = load(mediaBase());
    INFO(listed(loaded));
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    REQUIRE(loaded.manifest.contributes.native.has_value());
    const NativeContribution& native = *loaded.manifest.contributes.native;
    CHECK(native.videoSources == std::vector<NativeVideoSource>{{.name = "camera", .title = "Camera",
                                                                 .kinds = {"phone", "touchBox"}},
                                                                {.name = "screen", .title = "Screen", .kinds = {}}});
    CHECK(native.audioSources == std::vector<NativeAudioSource>{{.name = "microphone", .title = "Microphone"}});
    REQUIRE(native.mediaSinks.size() == 3);
    CHECK(native.mediaSinks[0].video == SinkVideo{.fps = 30, .width = 1280, .height = 720});
    CHECK_FALSE(native.mediaSinks[0].audio);
    CHECK(native.mediaSinks[1].audio);
    CHECK_FALSE(native.mediaSinks[2].video.has_value());
    CHECK(native.recordingExporters ==
          std::vector<NativeRecordingExporter>{{.name = "gif", .title = "GIF animation", .extension = "gif"}});
    const LoadedManifest again = loadManifest(saveManifest(loaded.manifest));
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == loaded.manifest);
    CHECK(std::ranges::find(kPermissionNames, "media.capture") != kPermissionNames.end());
    CHECK(std::ranges::find(kPermissionNames, "media.output") != kPermissionNames.end());

    Json json = mediaBase();
    json["permissions"] = Json::array({{{"name", "notify"}, {"reason", "Shows when it beeps."}}});
    const LoadedManifest bare = load(json);
    INFO(listed(bare));
    CHECK_FALSE(bare.usable);
    CHECK(reports(bare, Code::NativePermissionUndeclared, "$.contributes.native.videoSources"));
    CHECK(reports(bare, Code::NativePermissionUndeclared, "$.contributes.native.audioSources"));
    CHECK(reports(bare, Code::NativePermissionUndeclared, "$.contributes.native.mediaSinks"));
    CHECK(reports(bare, Code::NativePermissionUndeclared, "$.contributes.native.recordingExporters"));
    const auto said = [&bare](std::string_view path) {
        return std::ranges::find_if(bare.diagnostics, [path](const Diagnostic& each) { return each.path == path; });
    };
    REQUIRE(said("$.contributes.native.videoSources") != bare.diagnostics.end());
    CHECK(said("$.contributes.native.videoSources")->detail == "media.capture");
    REQUIRE(said("$.contributes.native.mediaSinks") != bare.diagnostics.end());
    CHECK(said("$.contributes.native.mediaSinks")->detail == "media.output");

    const Json schema = Json::parse(manifestJsonSchema());
    const Json declared = schema["properties"]["contributes"]["properties"]["native"]["properties"];
    CHECK(declared["mediaSinks"]["items"]["properties"]["video"]["properties"]["fps"]["maximum"] == kMostSinkFps);
    CHECK(declared["recordingExporters"]["items"]["required"] == Json::array({"name", "title", "extension"}));
    CHECK(declared["videoSources"]["maxItems"] == kMostMediaSources);
}

TEST_CASE("ADR-0112: a media sink with a bad frame rate or size, or that takes nothing, and an exporter with a bad "
          "extension do not load")
{
    Json json = mediaBase();
    Json& native = json["contributes"]["native"];
    native["videoSources"][0]["kinds"] = {"phone", "camera"};
    native["videoSources"][1]["kinds"] = {"phone", "phone"};
    native["mediaSinks"][0]["video"]["fps"] = 0;
    native["mediaSinks"][1]["video"]["fps"] = 61;
    native["mediaSinks"][1]["video"]["width"] = 8;
    native["mediaSinks"][2].erase("audio");
    native["mediaSinks"].push_back({{"name", "huge"}, {"title", "Huge"}, {"video", {{"fps", 30}, {"width", 4096},
                                                                                     {"height", 4096}}}});
    native["mediaSinks"].push_back({{"name", "half"}, {"title", "Half"}, {"video", {{"fps", 30}}}});
    native["mediaSinks"].push_back({{"name", "camera"}, {"title", "Again"}, {"audio", true}});
    native["recordingExporters"].push_back({{"name", "dotted"}, {"title", "Dotted"}, {"extension", ".mp4"}});
    native["recordingExporters"].push_back({{"name", "ours"}, {"title", "Ours"}, {"extension", "gsrec"}});
    native["recordingExporters"].push_back({{"name", "plain"}, {"title", "Plain"}});
    const LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.videoSources[0].kinds[1]"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.native.videoSources[1].kinds[1]"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.mediaSinks[0].video.fps"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.mediaSinks[1].video.fps"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.mediaSinks[1].video.width"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.native.mediaSinks[2].video"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.mediaSinks[3].video.width"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.native.mediaSinks[4].video.width"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.native.mediaSinks[4].video.height"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.native.mediaSinks[5].name"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.recordingExporters[1].extension"));
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.recordingExporters[2].extension"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.native.recordingExporters[3].extension"));
}

TEST_CASE("ADR-0112: command-line commands name one of the native jobs and its parameters, with defaults of their "
          "type, and survive a save and a load")
{
    Json json = nativeBase();
    json["contributes"]["native"]["jobs"] = Json::array(
        {{{"name", "flash"},
          {"title", "Write firmware"},
          {"params", Json::array({{{"name", "port"}, {"type", "string"}}, {{"name", "baud"}, {"type", "number"}}})}}});
    json["contributes"]["cli"] =
        Json::array({{{"name", "flash"},
                      {"description", "Writes firmware to a board."},
                      {"job", "flash"},
                      {"params", Json::array({{{"name", "port"}, {"description", "The serial port"}},
                                              {{"name", "baud"}, {"default", 921600}}})}}});
    LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    REQUIRE(loaded.manifest.contributes.cli.size() == 1);
    const CliContribution& command = loaded.manifest.contributes.cli[0];
    CHECK(command.name == "flash");
    CHECK(command.description == "Writes firmware to a board.");
    CHECK(command.job == "flash");
    REQUIRE(command.params.size() == 2);
    CHECK(command.params[0].name == "port");
    CHECK(command.params[0].description == "The serial port");
    CHECK(command.params[0].defaultValue.empty());
    CHECK(command.params[1].defaultValue == "921600");
    const LoadedManifest again = loadManifest(saveManifest(loaded.manifest));
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == loaded.manifest);

    json["contributes"]["cli"][0]["params"][1]["default"] = "fast";
    json["contributes"]["cli"].push_back({{"name", "Bad Name"}, {"job", "flash"}});
    json["contributes"]["cli"].push_back({{"name", "scan"}, {"job", "scan"}});
    json["contributes"]["cli"].push_back(
        {{"name", "erase"}, {"job", "flash"}, {"params", Json::array({{{"name", "speed"}}})}});
    json["contributes"]["cli"].push_back({{"name", "flash"}, {"job", "flash"}});
    loaded = load(json);
    INFO(listed(loaded));
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::CliCommandInvalid, "$.contributes.cli[0].params[1].default"));
    CHECK(reports(loaded, Code::CliCommandInvalid, "$.contributes.cli[1].name"));
    CHECK(reports(loaded, Code::CliCommandInvalid, "$.contributes.cli[2].job"));
    CHECK(reports(loaded, Code::CliCommandInvalid, "$.contributes.cli[3].params[0].name"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.cli[4].name"));
    CHECK(codeName(Code::CliCommandInvalid) == "cli-command-invalid");
    CHECK_FALSE(fixHint(Code::CliCommandInvalid).empty());

    Json reserved = nativeBase();
    reserved["contributes"]["native"]["jobs"] = Json::array(
        {{{"name", "dump"}, {"title", "Dump"}, {"params", Json::array({{{"name", "json"}, {"type", "boolean"}}})}}});
    reserved["contributes"]["cli"] = Json::array({{{"name", "dump"}, {"job", "dump"}}});
    CHECK(reports(load(reserved), Code::CliCommandInvalid, "$.contributes.cli[0].job"));

    Json alone = base();
    alone["contributes"]["cli"] = Json::array({{{"name", "dump"}, {"job", "dump"}}});
    CHECK(reports(load(alone), Code::CliCommandInvalid, "$.contributes.cli[0].job"));

    Json many = nativeBase();
    many["contributes"]["native"]["jobs"] = Json::array({{{"name", "scan"}, {"title", "Scan"}}});
    for (int index = 0; index <= static_cast<int>(kMaxCliCommands); ++index) {
        many["contributes"]["cli"].push_back({{"name", "scan-" + std::to_string(index)}, {"job", "scan"}});
    }
    CHECK(reports(load(many), Code::TooMany, "$.contributes.cli"));

    const Json schema = Json::parse(manifestJsonSchema());
    const Json cli = schema["properties"]["contributes"]["properties"]["cli"];
    CHECK(cli["maxItems"] == kMaxCliCommands);
    CHECK(cli["items"]["required"] == Json::array({"name", "job"}));

    Json hears = nativeBase();
    hears["contributes"]["native"]["jobs"] = Json::array(
        {{{"name", "hear"}, {"title", "Hear"}, {"params", Json::array({{{"name", "phrases"}, {"type", "json"}}})}}});
    hears["contributes"]["cli"] = Json::array(
        {{{"name", "hear"}, {"job", "hear"}, {"params", Json::array({{{"name", "phrases"}, {"default", "[1, 2]"}}})}}});
    const LoadedManifest hearable = load(hears);
    INFO(listed(hearable));
    CHECK(hearable.usable);
}

namespace {

Json converterOf(const char* name, const char* from, const char* to)
{
    return Json{{"name", name}, {"title", "Convert"}, {"from", from}, {"to", to}, {"script", "convert.ts"}};
}

}

TEST_CASE("ADR-0112: converters name their formats, the file extensions they read and their script, load under the "
          "script point and survive a save and a load")
{
    Json json = base();
    json["id"] = "alice.formats";
    json["requires"] = {{"script", "^1.0"}};
    json["contributes"] = {
        {"converters", Json::array({{{"name", "xyz-in"},
                                     {"title", "XYZ profiles"},
                                     {"from", "xyz"},
                                     {"to", "profile"},
                                     {"script", "convert/xyz.ts"},
                                     {"extensions", Json::array({"xyz", "xyzz"})}},
                                    {{"name", "xyz-out"},
                                     {"title", "XYZ profile"},
                                     {"from", "profile"},
                                     {"to", "xyz"},
                                     {"script", "convert/xyz.js"}},
                                    {{"name", "macro"},
                                     {"title", "Macro"},
                                     {"from", "recording"},
                                     {"to", "ahk"},
                                     {"script", "convert/macro.ts"}}})}};
    const LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    const std::vector<ConverterContribution>& converters = loaded.manifest.contributes.converters;
    REQUIRE(converters.size() == 3);
    CHECK(converters[0].name == "xyz-in");
    CHECK(converters[0].title == "XYZ profiles");
    CHECK(converters[0].from == "xyz");
    CHECK(converters[0].to == "profile");
    CHECK(converters[0].script == "convert/xyz.ts");
    CHECK(converters[0].extensions == std::vector<std::string>{"xyz", "xyzz"});
    CHECK(converters[1].extensions.empty());
    CHECK(converters[2].from == "recording");
    CHECK(converters[2].to == "ahk");
    CHECK(appFormat("profile"));
    CHECK(appFormat("task"));
    CHECK(appFormat("recording"));
    CHECK_FALSE(appFormat("xyz"));
    CHECK(fileFormat("xyz"));
    CHECK(fileFormat("mp4"));
    CHECK_FALSE(fileFormat("profile"));
    CHECK_FALSE(fileFormat(".xyz"));
    CHECK_FALSE(fileFormat("XYZ"));
    CHECK_FALSE(fileFormat(""));
    CHECK_FALSE(fileFormat("abcdefghijklmnopq"));
    const LoadedManifest again = loadManifest(saveManifest(loaded.manifest));
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == loaded.manifest);
    const Json saved = Json::parse(saveManifest(loaded.manifest))["contributes"]["converters"];
    CHECK(saved[0]["extensions"] == Json::array({"xyz", "xyzz"}));
    CHECK_FALSE(saved[1].contains("extensions"));
    const Json schema = Json::parse(manifestJsonSchema())["properties"]["contributes"]["properties"]["converters"];
    CHECK(schema["maxItems"] == kMaxConverters);
    CHECK(schema["items"]["required"] == Json::array({"name", "title", "from", "to", "script"}));
}

TEST_CASE("ADR-0112: a converter is checked for its formats, the file extensions it reads and its script, sixteen at "
          "most and only under the script point")
{
    Json json = base();
    json["id"] = "alice.formats";
    json["requires"] = {{"script", "^1.0"}};
    Json list = Json::array({converterOf("both", "profile", "task"), converterOf("neither", "xyz", "abc"),
                             converterOf("upper", "XYZ", "profile"), converterOf("dotted", "profile", ".xyz"),
                             converterOf("long", "abcdefghijklmnopq", "profile"), converterOf("both", "xyz", "profile")});
    list.push_back(converterOf("reads", "xyz", "profile"));
    list.back()["extensions"] = Json::array({"xyz", ".abc", "xyz", "A b"});
    list.push_back(converterOf("styled", "profile", "css"));
    list.back()["script"] = "convert/style.css";
    list.push_back(converterOf("typed", "profile", "xyz"));
    list.back()["script"] = "convert/types.d.ts";
    list.push_back(converterOf("Bad Name", "profile", "xyz"));
    list.push_back(Json{{"name", "bare"}, {"from", "xyz"}, {"to", "profile"}});
    json["contributes"] = {{"converters", list}};
    LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::ConverterInvalid, "$.contributes.converters[0]"));
    CHECK(reports(loaded, Code::ConverterInvalid, "$.contributes.converters[1]"));
    CHECK(reports(loaded, Code::ConverterInvalid, "$.contributes.converters[2].from"));
    CHECK_FALSE(reports(loaded, Code::ConverterInvalid, "$.contributes.converters[2]"));
    CHECK(reports(loaded, Code::ConverterInvalid, "$.contributes.converters[3].to"));
    CHECK(reports(loaded, Code::TooLong, "$.contributes.converters[4].from"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.converters[5].name"));
    CHECK_FALSE(reports(loaded, Code::ConverterInvalid, "$.contributes.converters[6].extensions[0]"));
    CHECK(reports(loaded, Code::ConverterInvalid, "$.contributes.converters[6].extensions[1]"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.converters[6].extensions[2]"));
    CHECK(reports(loaded, Code::ConverterInvalid, "$.contributes.converters[6].extensions[3]"));
    CHECK(reports(loaded, Code::ScriptEntryInvalid, "$.contributes.converters[7].script"));
    CHECK(reports(loaded, Code::ScriptEntryInvalid, "$.contributes.converters[8].script"));
    CHECK(reports(loaded, Code::PanelNameInvalid, "$.contributes.converters[9].name"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.converters[10].title"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.converters[10].script"));
    CHECK(codeName(Code::ConverterInvalid) == "converter-invalid");
    CHECK_FALSE(fixHint(Code::ConverterInvalid).empty());

    json["contributes"]["converters"] = Json::array();
    for (std::size_t index = 0; index <= kMaxConverters; ++index) {
        json["contributes"]["converters"].push_back(converterOf(("xyz" + std::to_string(index)).c_str(), "xyz", "profile"));
    }
    loaded = load(json);
    CHECK(reports(loaded, Code::TooMany, "$.contributes.converters"));

    json = base();
    json["contributes"]["converters"] = Json::array({converterOf("xyz-in", "xyz", "profile")});
    loaded = load(json);
    CHECK_FALSE(loaded.usable);
    CHECK(undeclared(loaded, "$.contributes.converters", "script"));
}

TEST_CASE("ADR-0112: a language pack names its language, its own name, a compiled translation and an optional help "
          "folder under the locale point, and survives a save and a load")
{
    Json json = base();
    json["id"] = "alice.japanese";
    json["requires"] = {{"locale", "^1.0"}};
    json["contributes"] = {
        {"locales", Json::array({{{"locale", "ja"}, {"name", "日本語"}, {"translations", "i18n/ja.qm"}, {"help", "help/ja"}},
                                 {{"locale", "zh_TW"}, {"name", "繁體中文"}, {"translations", "i18n/zh_TW.QM"}}})}};
    const LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    REQUIRE(loaded.manifest.contributes.locales.size() == 2);
    const LocaleContribution& japanese = loaded.manifest.contributes.locales[0];
    CHECK(japanese.locale == "ja");
    CHECK(japanese.name == "日本語");
    CHECK(japanese.translations == "i18n/ja.qm");
    CHECK(japanese.help == "help/ja");
    CHECK(loaded.manifest.contributes.locales[1].locale == "zh_TW");
    CHECK(loaded.manifest.contributes.locales[1].help.empty());
    CHECK(loaded.manifest.declares(ExtensionPoint::Locale));
    CHECK(extensionPointNamed("locale") == ExtensionPoint::Locale);
    CHECK(nameOf(ExtensionPoint::Locale) == "locale");

    const LoadedManifest again = loadManifest(saveManifest(loaded.manifest));
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == loaded.manifest);
    const Json saved = Json::parse(saveManifest(loaded.manifest))["contributes"]["locales"];
    CHECK(saved[0]["help"] == "help/ja");
    CHECK_FALSE(saved[1].contains("help"));

    const Json schema = Json::parse(manifestJsonSchema());
    const Json localeSchema = schema["properties"]["contributes"]["properties"]["locales"];
    CHECK(localeSchema["maxItems"] == kMaxLocales);
    CHECK(localeSchema["items"]["required"] == Json::array({"locale", "name", "translations"}));
    const Json points = schema["properties"]["requires"]["propertyNames"]["enum"];
    CHECK(std::find(points.begin(), points.end(), Json("locale")) != points.end());
}

TEST_CASE("ADR-0112: a language pack is checked for its tag, the languages GameScrcpy has itself, a .qm file, its "
          "point and that it asks for no permission")
{
    Json json = base();
    json["id"] = "alice.japanese";
    json["requires"] = {{"locale", "^1.0"}};
    json["contributes"] = {
        {"locales", Json::array({{{"locale", "Japanese"}, {"name", "日本語"}, {"translations", "i18n/ja.qm"}},
                                 {{"locale", "en"}, {"name", "English"}, {"translations", "i18n/en.qm"}},
                                 {{"locale", "zh_CN"}, {"name", "简体中文"}, {"translations", "i18n/zh_CN.qm"}},
                                 {{"locale", "ja"}, {"name", "日本語"}, {"translations", "i18n/ja.ts"}},
                                 {{"locale", "ja"}, {"name", "日本語"}, {"translations", "i18n/ja.txt"}},
                                 {{"locale", "zh_tw"},
                                  {"name", std::string(kMaxLocaleNameLength + 1, 'x')},
                                  {"translations", "../ja.qm"},
                                  {"help", "/help"}},
                                 {{"locale", "ko"}}})}};
    LoadedManifest loaded = load(json);
    INFO(listed(loaded));
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::LocaleInvalid, "$.contributes.locales[0].locale"));
    CHECK(reports(loaded, Code::LocaleInvalid, "$.contributes.locales[1].locale"));
    CHECK(reports(loaded, Code::LocaleInvalid, "$.contributes.locales[2].locale"));
    CHECK(reports(loaded, Code::LocaleInvalid, "$.contributes.locales[3].translations"));
    CHECK(reports(loaded, Code::LocaleInvalid, "$.contributes.locales[4].translations"));
    CHECK(reports(loaded, Code::Duplicate, "$.contributes.locales[4].locale"));
    CHECK(reports(loaded, Code::LocaleInvalid, "$.contributes.locales[5].locale"));
    CHECK(reports(loaded, Code::TooLong, "$.contributes.locales[5].name"));
    CHECK(reports(loaded, Code::PathInvalid, "$.contributes.locales[5].translations"));
    CHECK(reports(loaded, Code::PathInvalid, "$.contributes.locales[5].help"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.locales[6].name"));
    CHECK(reports(loaded, Code::MissingField, "$.contributes.locales[6].translations"));
    CHECK_FALSE(reports(loaded, Code::LocaleInvalid, "$.contributes.locales[6].locale"));
    const auto reserved = std::ranges::find_if(loaded.diagnostics, [](const Diagnostic& each) {
        return each.path == "$.contributes.locales[1].locale";
    });
    REQUIRE(reserved != loaded.diagnostics.end());
    CHECK(reserved->detail == "en is built into GameScrcpy");
    const auto source = std::ranges::find_if(loaded.diagnostics, [](const Diagnostic& each) {
        return each.path == "$.contributes.locales[3].translations";
    });
    REQUIRE(source != loaded.diagnostics.end());
    CHECK(source->detail.find("lrelease") != std::string::npos);
    CHECK(codeName(Code::LocaleInvalid) == "locale-invalid");
    CHECK(fixHint(Code::LocaleInvalid).find("lrelease") != std::string_view::npos);

    json["contributes"]["locales"] = Json::array();
    for (std::size_t index = 0; index <= kMaxLocales; ++index) {
        json["contributes"]["locales"].push_back({{"locale", std::string{'a', static_cast<char>('a' + index)}},
                                                  {"name", "Language"},
                                                  {"translations", "i18n/language.qm"}});
    }
    loaded = load(json);
    CHECK(reports(loaded, Code::TooMany, "$.contributes.locales"));

    json["contributes"]["locales"] =
        Json::array({{{"locale", "ja"}, {"name", "日本語"}, {"translations", "i18n/ja.qm"}}});
    json["permissions"] =
        Json::array({{{"name", "network"}, {"reason", "Downloads new words."}, {"hosts", {"example.com"}}}});
    loaded = load(json);
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::PermissionNotAllowed, "$.permissions"));

    json.erase("permissions");
    json["requires"] = {{"theme", "^1.0"}};
    loaded = load(json);
    CHECK_FALSE(loaded.usable);
    CHECK(undeclared(loaded, "$.contributes.locales", "locale"));
}

TEST_CASE("ADR-0112: files.paths names its folders, which load, round-trip and fail their checks")
{
    Json json = base();
    json["requires"]["script"] = "^1.0";
    json["permissions"] = Json::array(
        {{{"name", "files.paths"},
          {"reason", "Keeps your exports."},
          {"folders", Json::array({{{"name", "exports"}, {"purpose", "Where it saves your recordings."}},
                                   {{"name", "saves-2"}, {"purpose", "Where it keeps its saves."}}})}},
         {{"name", "files.pick"}, {"reason", "Opens a file you choose."}}});
    LoadedManifest loaded = load(json);
    CHECK_MESSAGE(loaded.usable, listed(loaded));
    CHECK_MESSAGE(loaded.diagnostics.empty(), listed(loaded));
    REQUIRE(loaded.manifest.permissions.size() == 2);
    REQUIRE(loaded.manifest.permissions[0].folders.size() == 2);
    CHECK(loaded.manifest.permissions[0].folders[1].name == "saves-2");
    CHECK(loaded.manifest.permissions[0].folders[1].purpose == "Where it keeps its saves.");
    CHECK(saveManifest(loaded.manifest).find("\"folders\"") != std::string::npos);
    const LoadedManifest again = loadManifest(saveManifest(loaded.manifest));
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == loaded.manifest);

    json["permissions"] = Json::array(
        {{{"name", "files.paths"},
          {"reason", "Keeps files."},
          {"folders", Json::array({{{"name", "Exports"}, {"purpose", "A."}},
                                   {{"name", "same"}, {"purpose", "B."}},
                                   {{"name", "same"}, {"purpose", "C."}},
                                   {{"name", "quiet"}},
                                   {{"name", "long"}, {"purpose", std::string(121, 'x')}}})}},
         {{"name", "files.pick"},
          {"reason", "Opens a file."},
          {"folders", Json::array({{{"name", "x"}, {"purpose", "Y."}}})}},
         {{"name", "network"}, {"reason", "Talks."}, {"hosts", {"a.com"}}, {"paths", {"C:/x"}}}});
    loaded = load(json);
    INFO(listed(loaded));
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::PanelNameInvalid, "$.permissions[0].folders[0].name"));
    CHECK(reports(loaded, Code::Duplicate, "$.permissions[0].folders[2].name"));
    CHECK(reports(loaded, Code::MissingField, "$.permissions[0].folders[3].purpose"));
    CHECK(reports(loaded, Code::TooLong, "$.permissions[0].folders[4].purpose"));
    CHECK(reports(loaded, Code::PermissionFieldUnused, "$.permissions[1].folders"));
    CHECK(reports(loaded, Code::PermissionFieldUnused, "$.permissions[2].paths"));

    json["permissions"] =
        Json::array({{{"name", "files.paths"}, {"reason", "Old style."}, {"paths", {"%USERPROFILE%/Presets"}}}});
    loaded = load(json);
    CHECK_MESSAGE(loaded.usable, listed(loaded));
    CHECK(reports(loaded, Code::PermissionPathsFixed, "$.permissions[0].paths"));
    const auto fixed = std::ranges::find(loaded.diagnostics, Code::PermissionPathsFixed, &Diagnostic::code);
    REQUIRE(fixed != loaded.diagnostics.end());
    CHECK(fixed->severity == Severity::Warning);
    CHECK(codeName(Code::PermissionPathsFixed) == "permission-paths-fixed");
    CHECK(std::string(fixHint(Code::PermissionPathsFixed)).find("folders") != std::string::npos);

    json["permissions"] = Json::array({{{"name", "files.paths"}, {"reason", "Nothing."}}});
    CHECK(reports(load(json), Code::PermissionPathsMissing, "$.permissions[0].folders"));

    Json many = Json::array();
    for (std::size_t index = 0; index <= kMaxPermissionFolders; ++index) {
        many.push_back({{"name", "f" + std::to_string(index)}, {"purpose", "A folder."}});
    }
    json["permissions"] = Json::array({{{"name", "files.paths"}, {"reason", "Many."}, {"folders", many}}});
    CHECK(reports(load(json), Code::TooMany, "$.permissions[0].folders"));
}

TEST_CASE("ADR-0112: the hosts of the permission network are matched by name, by wildcard and by port")
{
    const std::vector<std::string> hosts{"api.example.com", "*.cdn.example.com", "Media.Example.org:8443"};
    CHECK(hostListed(hosts, "api.example.com", 443));
    CHECK(hostListed(hosts, "API.Example.com", 443));
    CHECK_FALSE(hostListed(hosts, "api.example.com", 8443));
    CHECK_FALSE(hostListed(hosts, "example.com", 443));
    CHECK_FALSE(hostListed(hosts, "evilapi.example.com", 443));
    CHECK(hostListed(hosts, "a.cdn.example.com", 443));
    CHECK(hostListed(hosts, "a.b.cdn.example.com", 443));
    CHECK_FALSE(hostListed(hosts, "cdn.example.com", 443));
    CHECK_FALSE(hostListed(hosts, "a.cdn.example.com.evil.net", 443));
    CHECK(hostListed(hosts, "media.example.org", 8443));
    CHECK_FALSE(hostListed(hosts, "media.example.org", 443));
    CHECK_FALSE(hostListed(hosts, "", 443));
    CHECK_FALSE(hostListed({}, "api.example.com", 443));
    CHECK(hostListed({"*"}, "anything.example.net", 9000));
    CHECK_FALSE(hostListed(hosts, "api.example.com.", 443));
    CHECK_FALSE(hostListed(hosts, "a.cdn.example.com.", 443));
    CHECK_FALSE(hostListed(hosts, "::1", 443));
    CHECK_FALSE(hostListed({"[::1]"}, "::1", 443));
    CHECK(hostListed({"*"}, "::1", 443));
}

TEST_CASE("ADR-0112: a native DLL runs with full rights unless its manifest says restricted, which survives a save "
          "and a load")
{
    Json json = nativeBase();
    LoadedManifest loaded = load(json);
    REQUIRE(loaded.manifest.contributes.native.has_value());
    CHECK(loaded.manifest.contributes.native->sandbox == NativeSandbox::Full);
    CHECK_FALSE(Json::parse(saveManifest(loaded.manifest))["contributes"]["native"].contains("sandbox"));

    json["contributes"]["native"]["sandbox"] = "restricted";
    loaded = load(json);
    INFO(listed(loaded));
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    REQUIRE(loaded.manifest.contributes.native.has_value());
    CHECK(loaded.manifest.contributes.native->sandbox == NativeSandbox::Restricted);
    const Json saved = Json::parse(saveManifest(loaded.manifest));
    CHECK(saved["contributes"]["native"]["sandbox"] == "restricted");
    const LoadedManifest again = loadManifest(saved.dump());
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == loaded.manifest);

    json["contributes"]["native"]["sandbox"] = "full";
    loaded = load(json);
    CHECK(loaded.usable);
    REQUIRE(loaded.manifest.contributes.native.has_value());
    CHECK(loaded.manifest.contributes.native->sandbox == NativeSandbox::Full);

    json["contributes"]["native"]["sandbox"] = "sandboxed";
    loaded = load(json);
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::OutOfRange, "$.contributes.native.sandbox"));
    json["contributes"]["native"]["sandbox"] = true;
    loaded = load(json);
    CHECK_FALSE(loaded.usable);
    CHECK(reports(loaded, Code::WrongType, "$.contributes.native.sandbox"));

    Json schema = Json::parse(manifestJsonSchema());
    CHECK(schema["properties"]["contributes"]["properties"]["native"]["properties"]["sandbox"]["enum"] ==
          Json::array({"full", "restricted"}));
    CHECK_FALSE(contains(schema["properties"]["contributes"]["properties"]["native"]["required"], "sandbox"));
}

TEST_CASE("ADR-0112: a manifest names its update list with an https address of printable ASCII that has a host and "
          "no user name")
{
    Json json = base();
    json["updates"] = {{"index", "https://alice.example/gamescrcpy/index.json"}};
    LoadedManifest loaded = load(json);
    CHECK_MESSAGE(loaded.diagnostics.empty(), listed(loaded));
    REQUIRE(loaded.manifest.updates.has_value());
    CHECK(loaded.manifest.updates->index == "https://alice.example/gamescrcpy/index.json");
    const LoadedManifest again = loadManifest(saveManifest(loaded.manifest));
    CHECK_MESSAGE(again.diagnostics.empty(), listed(again));
    CHECK(again.manifest == loaded.manifest);

    CHECK_FALSE(load(base()).manifest.updates.has_value());

    for (const char* bad : {"http://alice.example/index.json", "https://", "https:///index.json",
                            "https://:443/index.json", "https://user@alice.example/index.json",
                            "https://alice.example/my index.json", "ftp://alice.example/index.json",
                            "https://alice.example/caf\xC3\xA9.json", "https://alice.example/\x01.json"}) {
        json["updates"] = {{"index", bad}};
        loaded = load(json);
        CHECK_FALSE_MESSAGE(loaded.usable, bad);
        CHECK_MESSAGE(reports(loaded, Code::UrlInvalid, "$.updates.index"), bad);
    }

    json["updates"] = {{"index", "https://alice.example/" + std::string(2048, 'a')}};
    CHECK(reports(load(json), Code::TooLong, "$.updates.index"));
    json["updates"] = Json::object();
    CHECK(reports(load(json), Code::MissingField, "$.updates.index"));
    json["updates"] = {{"index", "https://alice.example/index.json"}, {"channel", "beta"}};
    CHECK(reports(load(json), Code::UnknownField, "$.updates.channel"));
    json["updates"] = "https://alice.example/index.json";
    CHECK(reports(load(json), Code::NotAnObject, "$.updates"));

    Json schema = Json::parse(manifestJsonSchema());
    CHECK(schema["properties"]["updates"]["required"] == Json::array({"index"}));
    CHECK(schema["properties"]["updates"]["properties"]["index"]["maxLength"] == kMaxUpdateIndexLength);
    CHECK_FALSE(contains(schema["required"], "updates"));
}
