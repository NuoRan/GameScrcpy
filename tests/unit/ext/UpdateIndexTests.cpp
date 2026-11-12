#include "gs/ext/UpdateIndex.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

using namespace gs::ext;
using Json = nlohmann::json;

namespace {

constexpr std::string_view kHash = "0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789ABCDEF";

std::string publisherText(std::uint8_t number)
{
    Ed25519Seed seed{};
    seed.fill(number);
    MinisignKeyId id{};
    id.fill(number);
    return minisignKeyText(minisignPublicKey(makeMinisignKey(seed, id)));
}

Json release(std::string_view version)
{
    return {{"version", version},
            {"url", "https://alice.example/feel-" + std::string(version) + ".gsext"},
            {"sha256", kHash},
            {"size", 4096}};
}

Json listOf(Json extensions)
{
    return {{"format", "gamescrcpy.extension-index"}, {"version", 1}, {"extensions", std::move(extensions)}};
}

Json single(Json releases)
{
    return listOf(Json::array({{{"id", "alice.feel"}, {"versions", std::move(releases)}}}));
}

ReadIndex read(const Json& json)
{
    return readUpdateIndex(json.dump());
}

bool refusedAt(const Json& json, std::string_view path)
{
    const ReadIndex result = read(json);
    return !result.index && result.problem.starts_with(std::string(path) + ":");
}

HostApi hostWith(ExtensionPoint point, std::string_view version)
{
    HostApi host;
    host.versions[point] = {*Version::parse(version)};
    return host;
}

}

TEST_CASE("ADR-0112: an update list reads every version of every extension, with hashes in lowercase")
{
    Json newest = release("1.2.0");
    newest["publisher"] = publisherText(7);
    newest["requires"] = {{"script", "^1.0"}, {"panel", ">=1.0 <2"}};
    newest["released"] = "2026-11-12";
    newest["notes"] = "Smoother aiming.";
    const Json json = listOf(Json::array({{{"id", "alice.feel"}, {"versions", Json::array({release("1.1.0"), newest})}},
                                          {{"id", "bob.tools"}, {"versions", Json::array({release("0.1.0-beta.2")})}}}));
    const ReadIndex result = read(json);
    REQUIRE_MESSAGE(result.index.has_value(), result.problem);
    CHECK(result.problem.empty());
    REQUIRE(result.index->extensions.size() == 2);

    const IndexedExtension* feel = result.index->find("alice.feel");
    REQUIRE(feel != nullptr);
    REQUIRE(feel->releases.size() == 2);
    const IndexedRelease& listed = feel->releases[1];
    CHECK(listed.version == *Version::parse("1.2.0"));
    CHECK(listed.url == "https://alice.example/feel-1.2.0.gsext");
    CHECK(listed.sha256 == "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    CHECK(listed.size == 4096);
    REQUIRE(listed.publisher.has_value());
    CHECK(minisignKeyText(*listed.publisher) == publisherText(7));
    CHECK(listed.requirements.size() == 2);
    CHECK(listed.requirements.at(ExtensionPoint::Script).text() == "^1.0");
    CHECK_FALSE(listed.needsUnknown);
    CHECK(listed.released == "2026-11-12");
    CHECK(listed.notes == "Smoother aiming.");
    CHECK_FALSE(feel->releases[0].publisher.has_value());
    CHECK(feel->releases[0].requirements.empty());

    REQUIRE(result.index->find("bob.tools") != nullptr);
    CHECK(result.index->find("bob.tools")->releases.front().version.prerelease == "beta.2");
    CHECK(result.index->find("carol.none") == nullptr);

    const ReadIndex empty = read(listOf(Json::array()));
    REQUIRE(empty.index.has_value());
    CHECK(empty.index->extensions.empty());
}

TEST_CASE("ADR-0112: a list that is not an update list of this format is refused as a whole")
{
    CHECK(readUpdateIndex("{\"format\":").problem == "$: not JSON");
    CHECK(readUpdateIndex("[]").problem == "$: not an object");
    CHECK_FALSE(readUpdateIndex(std::string(kMostUpdateIndexBytes + 1, ' ')).index.has_value());
    CHECK(readUpdateIndex(std::string(kMostUpdateIndexBytes + 1, ' ')).problem == "$: larger than 1 MiB");

    Json json = listOf(Json::array());
    json["format"] = "gamescrcpy.extension-catalog";
    CHECK(refusedAt(json, "$.format"));
    json = listOf(Json::array());
    json["version"] = 2;
    CHECK(read(json).problem == "$.version: a newer format than this GameScrcpy reads");
    json["version"] = 0;
    CHECK(refusedAt(json, "$.version"));
    json["version"] = "1";
    CHECK(refusedAt(json, "$.version"));
    json = listOf(Json::array());
    json.erase("extensions");
    CHECK(refusedAt(json, "$.extensions"));
    json = listOf(Json::object());
    CHECK(refusedAt(json, "$.extensions"));
    json = listOf(Json::array());
    json["mirror"] = "https://elsewhere.example/";
    CHECK(refusedAt(json, "$[\"mirror\"]"));

    std::string deep = R"({"format":"gamescrcpy.extension-index","version":1,"extensions":[],"x":)";
    deep += std::string(40, '[') + std::string(40, ']') + "}";
    CHECK(readUpdateIndex(deep).problem == "$: nested too deeply");

    const std::string twice =
        R"({"format":"gamescrcpy.extension-index","version":1,"extensions":[],"extensions":[]})";
    CHECK(readUpdateIndex(twice).problem == "$: an object names the same field twice");
    const std::string twiceInside = R"({"format":"gamescrcpy.extension-index","version":1,"extensions":[)"
                                    R"({"id":"alice.feel","id":"alice.feel","versions":[]}]})";
    CHECK(readUpdateIndex(twiceInside).problem == "$: an object names the same field twice");

    Json many = Json::array();
    for (std::size_t index = 0; index <= kMostIndexedExtensions; ++index) {
        many.push_back({{"id", "alice.x" + std::to_string(index)}, {"versions", Json::array({release("1.0.0")})}});
    }
    CHECK(refusedAt(listOf(many), "$.extensions"));
}

TEST_CASE("ADR-0112: a wrong type or value in an update list refuses the list and says where")
{
    const auto withRelease = [](const char* field, const Json& value) {
        Json entry = release("1.0.0");
        entry[field] = value;
        return single(Json::array({entry}));
    };
    const auto without = [](const char* field) {
        Json entry = release("1.0.0");
        entry.erase(field);
        return single(Json::array({entry}));
    };
    const std::string at = "$.extensions[0].versions[0]";

    CHECK(refusedAt(withRelease("version", 1), at + ".version"));
    CHECK(refusedAt(withRelease("version", "1.0"), at + ".version"));
    CHECK(refusedAt(without("version"), at + ".version"));
    CHECK(refusedAt(withRelease("url", "http://alice.example/feel.gsext"), at + ".url"));
    CHECK(refusedAt(withRelease("url", "https://bob@alice.example/feel.gsext"), at + ".url"));
    CHECK(refusedAt(withRelease("url", "https://alice.example/" + std::string(2048, 'a')), at + ".url"));
    CHECK(refusedAt(without("url"), at + ".url"));
    CHECK(refusedAt(withRelease("sha256", std::string(63, 'a')), at + ".sha256"));
    CHECK(refusedAt(withRelease("sha256", std::string(63, 'a') + "g"), at + ".sha256"));
    CHECK(refusedAt(without("sha256"), at + ".sha256"));
    CHECK(refusedAt(withRelease("size", 0), at + ".size"));
    CHECK(refusedAt(withRelease("size", -5), at + ".size"));
    CHECK(refusedAt(withRelease("size", 1.5), at + ".size"));
    CHECK(refusedAt(withRelease("size", "4096"), at + ".size"));
    CHECK(refusedAt(withRelease("size", kMostUpdatePackageBytes + 1), at + ".size"));
    CHECK(refusedAt(without("size"), at + ".size"));
    CHECK(refusedAt(withRelease("publisher", "RWQ-not-a-key"), at + ".publisher"));
    CHECK(refusedAt(withRelease("publisher", 7), at + ".publisher"));
    CHECK(refusedAt(withRelease("released", "2026-02-30"), at + ".released"));
    CHECK(refusedAt(withRelease("released", "12.11.2026"), at + ".released"));
    CHECK(refusedAt(withRelease("notes", std::string(kMostReleaseNotesBytes + 1, 'n')), at + ".notes"));
    CHECK(refusedAt(withRelease("requires", "^1.0"), at + ".requires"));
    CHECK(refusedAt(withRelease("requires", Json{{"script", "one"}}), at + ".requires[\"script\"]"));
    CHECK(refusedAt(withRelease("requires", Json{{"script", 1}}), at + ".requires[\"script\"]"));
    CHECK(refusedAt(withRelease("channel", "beta"), at + "[\"channel\"]"));
    CHECK(refusedAt(single(Json::array({"1.0.0"})), at));

    CHECK(refusedAt(single(Json::array()), "$.extensions[0].versions"));
    CHECK(refusedAt(single(Json::object()), "$.extensions[0].versions"));
    Json oddId = single(Json::array({release("1.0.0")}));
    oddId["extensions"][0]["id"] = "Alice";
    CHECK(refusedAt(oddId, "$.extensions[0].id"));
    Json extra = single(Json::array({release("1.0.0")}));
    extra["extensions"][0]["homepage"] = "https://alice.example/";
    CHECK(refusedAt(extra, "$.extensions[0][\"homepage\"]"));
    CHECK(refusedAt(listOf(Json::array({"alice.feel"})), "$.extensions[0]"));

    const ReadIndex unknown = read(withRelease("requires", Json{{"teleport", "^1.0"}, {"script", "^1.0"}}));
    REQUIRE_MESSAGE(unknown.index.has_value(), unknown.problem);
    CHECK(unknown.index->extensions[0].releases[0].needsUnknown);
    CHECK(unknown.index->extensions[0].releases[0].requirements.size() == 1);
}

TEST_CASE("ADR-0112: an extension or a version listed twice refuses the whole list")
{
    Json twiceVersion = single(Json::array({release("1.0.0"), release("1.1.0"), release("1.0.0")}));
    CHECK(read(twiceVersion).problem == "$.extensions[0].versions[2].version: listed twice");

    const Json entry = {{"id", "alice.feel"}, {"versions", Json::array({release("1.0.0")})}};
    CHECK(read(listOf(Json::array({entry, entry}))).problem == "$.extensions[1].id: listed twice");
}

TEST_CASE("ADR-0112: the version offered is the newest one above the installed one that runs here and is not skipped")
{
    Json needsNewer = release("1.4.0");
    needsNewer["requires"] = {{"script", "^2.0"}};
    Json fits = release("1.2.0");
    fits["requires"] = {{"script", "^1.0"}};
    Json unknownPoint = release("1.3.0");
    unknownPoint["requires"] = {{"teleport", "^1.0"}};
    const ReadIndex result = read(single(
        Json::array({release("0.9.0"), release("1.1.0"), fits, unknownPoint, needsNewer, release("1.5.0-beta.1")})));
    REQUIRE_MESSAGE(result.index.has_value(), result.problem);
    const IndexedExtension& feel = result.index->extensions.front();
    const HostApi host = hostWith(ExtensionPoint::Script, "1.0.0");

    const IndexedRelease* offered = offeredRelease(feel, *Version::parse("1.0.0"), host);
    REQUIRE(offered != nullptr);
    CHECK(offered->version.text() == "1.2.0");

    CHECK(offeredRelease(feel, *Version::parse("1.2.0"), host) == nullptr);
    CHECK(offeredRelease(feel, *Version::parse("2.0.0"), host) == nullptr);

    offered = offeredRelease(feel, *Version::parse("1.0.0"), host, *Version::parse("1.1.0"));
    REQUIRE(offered != nullptr);
    CHECK(offered->version.text() == "1.2.0");
    CHECK(offeredRelease(feel, *Version::parse("1.0.0"), host, *Version::parse("1.2.0")) == nullptr);

    const HostApi newer = hostWith(ExtensionPoint::Script, "2.1.0");
    offered = offeredRelease(feel, *Version::parse("1.0.0"), newer);
    REQUIRE(offered != nullptr);
    CHECK(offered->version.text() == "1.4.0");

    offered = offeredRelease(feel, *Version::parse("1.0.0-beta.3"), host);
    REQUIRE(offered != nullptr);
    CHECK(offered->version.text() == "1.5.0-beta.1");

    CHECK_FALSE(runsOn(feel.releases[3], newer));
    CHECK(runsOn(feel.releases[1], HostApi{}));
    CHECK_FALSE(runsOn(feel.releases[2], HostApi{}));
}

TEST_CASE("ADR-0112: update addresses are https with a host and without a user name")
{
    CHECK(validUpdateUrl("https://alice.example/index.json"));
    CHECK(validUpdateUrl("https://alice.example:8443/a/b?c=d"));
    CHECK(validUpdateUrl("https://[2001:db8::1]/index.json"));
    CHECK_FALSE(validUpdateUrl("http://alice.example/index.json"));
    CHECK_FALSE(validUpdateUrl("HTTPS://alice.example/index.json"));
    CHECK_FALSE(validUpdateUrl("https://"));
    CHECK_FALSE(validUpdateUrl("https:///index.json"));
    CHECK_FALSE(validUpdateUrl("https://:8443/index.json"));
    CHECK_FALSE(validUpdateUrl("https://user:secret@alice.example/index.json"));
    CHECK_FALSE(validUpdateUrl("https://alice.example/an index.json"));
    CHECK_FALSE(validUpdateUrl("https://alice.example/\xE6\x9B\xB4\xE6\x96\xB0.json"));
    CHECK_FALSE(validUpdateUrl("https://alice.example/" + std::string(2048, 'a')));
}
