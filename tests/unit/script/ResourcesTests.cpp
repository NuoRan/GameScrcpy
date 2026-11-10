#include "ScriptFixture.h"

#include "gs/script/Resources.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using gs::script::characterCount;
using gs::script::colorText;
using gs::script::loadResources;
using gs::script::MatchMode;
using gs::script::parseColor;
using gs::script::parseResources;
using gs::script::PictureSize;
using gs::script::Point;
using gs::script::Region;
using gs::script::ResourceFile;
using gs::script::resourceFile;
using gs::script::resourceName;
using gs::script::Resources;
using gs::script::Rgb;
using gs::script::saveResources;
using gs::script::TemplateResource;
using gs::script::writeResources;
using gs::script::test::ScriptFolder;

namespace {

constexpr std::string_view kExample = R"({"format": "gamescrcpy.resources", "version": 1,
 "templates": [{"name": "开始按钮", "file": "开始按钮.png", "threshold": 0.9, "match": "color", "region": "右下角",
                "source": {"width": 1080, "height": 2400}}],
 "regions": [{"name": "右下角", "x": 0.5, "y": 0.6, "width": 0.5, "height": 0.4}],
 "points": [{"name": "活动入口", "x": 0.12, "y": 0.08}],
 "paths": [{"name": "拖动地图", "points": [[0.1, 0.8], [0.3, 0.75]], "durationMs": 400}],
 "colors": [{"name": "活动横幅", "color": "#F5C542", "tolerance": 0.1, "region": "右下角"}]})";

std::string withList(std::string_view list, std::string_view entries)
{
    return std::string(R"({"format": "gamescrcpy.resources", "version": 1, ")") + std::string(list) + "\": ["
         + std::string(entries) + "]}";
}

}

TEST_CASE("resources: the workbench's file is read with every kind and written back the same (VIS-004)")
{
    const auto parsed = parseResources(kExample);
    REQUIRE(parsed);
    const Resources& resources = *parsed;
    REQUIRE(resources.templates.size() == 1);
    const TemplateResource& start = resources.templates.front();
    CHECK(start.name == "开始按钮");
    CHECK(start.file == "开始按钮.png");
    CHECK(start.threshold == 0.9);
    CHECK(start.match == MatchMode::Color);
    CHECK(start.region == "右下角");
    CHECK(start.source == PictureSize{1080, 2400});
    REQUIRE(resources.findRegion("右下角") != nullptr);
    CHECK(resources.findRegion("右下角")->area == Region{0.5, 0.6, 0.5, 0.4});
    REQUIRE(resources.findPoint("活动入口") != nullptr);
    CHECK(resources.findPoint("活动入口")->at == Point{0.12, 0.08});
    REQUIRE(resources.findPath("拖动地图") != nullptr);
    CHECK(resources.findPath("拖动地图")->points == std::vector<Point>{{0.1, 0.8}, {0.3, 0.75}});
    CHECK(resources.findPath("拖动地图")->durationMs == 400.0);
    REQUIRE(resources.findColor("活动横幅") != nullptr);
    CHECK(resources.findColor("活动横幅")->color == Rgb{0xF5, 0xC5, 0x42});
    CHECK(resources.findColor("活动横幅")->tolerance == 0.1);
    CHECK(resources.findColor("活动横幅")->region == "右下角");
    CHECK(resources.findTemplate("missing") == nullptr);
    CHECK(resources.findPoint("右下角") == nullptr);

    const std::string written = writeResources(resources);
    CHECK(written.starts_with("{\n  \"format\": \"gamescrcpy.resources\",\n  \"version\": 1,\n  \"templates\": [\n"));
    CHECK(written.find("\"color\": \"#F5C542\"") != std::string::npos);
    CHECK(written.find("\"name\": \"开始按钮\"") != std::string::npos);
    CHECK(written.ends_with("}\n"));
    const auto again = parseResources(written);
    REQUIRE(again);
    CHECK(*again == resources);

    const auto defaults = parseResources(R"({"format": "gamescrcpy.resources", "version": 1,
        "templates": [{"name": "chest", "file": "chest.png"}], "paths": [{"name": "drag", "points": [[0, 0], [1, 1]]}],
        "colors": [{"name": "gold", "color": "#ffd700"}]})");
    REQUIRE(defaults);
    CHECK(defaults->templates.front().threshold == 0.8);
    CHECK(defaults->templates.front().match == MatchMode::Grey);
    CHECK(defaults->templates.front().region.empty());
    CHECK(defaults->templates.front().source == PictureSize{});
    CHECK(defaults->paths.front().durationMs == 300.0);
    CHECK(defaults->colors.front().color == Rgb{255, 215, 0});
    CHECK(defaults->colors.front().tolerance == 0.1);
    const auto empty = parseResources(R"({"format": "gamescrcpy.resources", "version": 1})");
    REQUIRE(empty);
    CHECK(empty->empty());
    const auto shared = parseResources(R"({"format": "gamescrcpy.resources", "version": 1,
        "regions": [{"name": "same", "x": 0, "y": 0, "width": 1, "height": 1}],
        "points": [{"name": "same", "x": 0.5, "y": 0.5}]})");
    CHECK(shared);
}

TEST_CASE("resources: names are 1 to 64 characters without / \\ or : and never look like a picture path")
{
    CHECK(resourceName("开始按钮"));
    CHECK(resourceName("a"));
    CHECK(resourceName("Template 1"));
    CHECK(resourceName("v1.2 #3"));
    CHECK(resourceName("x.png2"));
    std::string longest;
    for (int i = 0; i < 64; ++i) {
        longest += "按";
    }
    CHECK(characterCount(longest) == 64);
    CHECK(resourceName(longest));
    CHECK_FALSE(resourceName(longest + "a"));
    CHECK_FALSE(resourceName(""));
    for (const char* bad : {"a/b", "a\\b", "c:d", "x.png", "X.PNG", "y.jpeg", "z.Jpg", "w.bmp", "tab\there", "\x7F"}) {
        CAPTURE(bad);
        CHECK_FALSE(resourceName(bad));
    }
    CHECK(resourceFile("开始按钮.png"));
    CHECK(resourceFile("Template 1.PNG"));
    for (const char* bad : {"", ".png", "a.jpg", "a/b.png", "a\\b.png", "con.png", "COM1.png", "Nul.png", "a?.png",
                            "a\".png", ".hidden.png", "a:b.png"}) {
        CAPTURE(bad);
        CHECK_FALSE(resourceFile(bad));
    }
    CHECK(parseColor("#F5C542") == Rgb{0xF5, 0xC5, 0x42});
    CHECK(parseColor("#00ff7f") == Rgb{0, 255, 127});
    for (const char* bad : {"F5C542", "#F5C54", "#F5C5421", "#G5C542", "", "#"}) {
        CAPTURE(bad);
        CHECK_FALSE(parseColor(bad));
    }
    CHECK(colorText({0xF5, 0xC5, 0x42}) == "#F5C542");
    CHECK(colorText({0, 10, 255}) == "#000AFF");
}

TEST_CASE("resources: problems name the place in the file, and every problem is reported")
{
    const struct {
        std::string text;
        const char* problem;
    } broken[] = {
        {"not json", "$: the file is not a JSON object"},
        {R"({"format": "other", "version": 1})", "$.format: must be \"gamescrcpy.resources\""},
        {R"({"format": "gamescrcpy.resources", "version": 2})", "$.version: must be 1"},
        {R"({"format": "gamescrcpy.resources", "version": 1, "extra": 1})", "$.extra: is not a known field"},
        {R"({"format": "gamescrcpy.resources", "version": 1, "templates": {}})", "$.templates: must be a list"},
        {withList("templates", R"({"name": "a", "file": "a.png", "zoom": 2})"), "$.templates[0].zoom: is not a known field"},
        {withList("templates", R"({"name": "a"})"), "$.templates[0].file: is missing"},
        {withList("templates", R"({"name": "a", "file": "a.png", "threshold": "high"})"),
         "$.templates[0].threshold: must be a number"},
        {withList("templates", R"({"name": "a", "file": "a.png", "match": "sepia"})"),
         "$.templates[0].match: must be \"color\" or \"grey\""},
        {withList("templates", R"({"name": "a", "file": "a.png", "source": {"width": 0, "height": 10}})"),
         "$.templates[0].source.width: must be a whole number of pixels from 1 to 65535"},
        {withList("templates", R"({"name": "a", "file": "a.png", "threshold": 1.5})"),
         "$.templates[0].threshold: must be a number from 0 to 1"},
        {withList("templates", R"({"name": "a", "file": "a.png", "region": "nowhere"})"),
         "$.templates[0].region: there is no region named \"nowhere\""},
        {withList("templates", R"({"name": "a.png", "file": "a.png"})"), "$.templates[0].name: must be 1 to 64 characters"},
        {withList("templates", R"({"name": "a", "file": "a.png"}, {"name": "b", "file": "A.png"})"),
         "$.templates[1].file: \"A.png\" belongs to another template"},
        {withList("templates", R"({"name": "a", "file": "a.jpg"})"),
         "$.templates[0].file: must be the name of a .png file in res/"},
        {withList("points", R"({"name": "p", "x": 0.1, "y": 0.1}, {"name": "p", "x": 0.2, "y": 0.2})"),
         "$.points[1].name: \"p\" is used twice"},
        {withList("regions", R"({"name": "r", "x": 0.6, "y": 0, "width": 0.5, "height": 1})"),
         "$.regions[0]: must lie inside the picture"},
        {withList("regions", R"({"name": "r", "x": 0, "y": 0, "width": 0, "height": 1})"),
         "$.regions[0]: must lie inside the picture"},
        {withList("points", R"({"name": "p", "x": 1.5, "y": 0})"), "$.points[0]: x and y must be ratios from 0 to 1"},
        {withList("points", R"({"name": "p", "x": 0.5})"), "$.points[0].y: is missing"},
        {withList("paths", R"({"name": "q", "points": [[0.1, 0.1]]})"), "$.paths[0].points: must hold 2 to 64 points"},
        {withList("paths", R"({"name": "q", "points": [[0.1, 0.1], [0.2]]})"),
         "$.paths[0].points[1]: must be an [x, y] pair of numbers"},
        {withList("paths", R"({"name": "q", "points": [[0.1, 0.1], [0.2, 0.2]], "durationMs": 0})"),
         "$.paths[0].durationMs: must be more than 0"},
        {withList("colors", R"({"name": "c", "color": "#12345"})"), "$.colors[0].color: must be written \"#RRGGBB\""},
        {withList("colors", R"({"name": "c", "color": "#123456", "tolerance": -0.1})"),
         "$.colors[0].tolerance: must be a number from 0 to 1"},
        {withList("colors", R"({"name": "c", "color": "#123456", "region": "r"})"),
         "$.colors[0].region: there is no region named \"r\""},
    };
    for (const auto& entry : broken) {
        CAPTURE(entry.text);
        const auto parsed = parseResources(entry.text);
        REQUIRE_FALSE(parsed);
        REQUIRE_FALSE(parsed.error().empty());
        CHECK(parsed.error().front().starts_with(entry.problem));
    }
    const auto both = parseResources(withList("points", R"({"name": "p", "x": 2, "y": 0}, {"name": "", "x": 0, "y": 0})"));
    REQUIRE_FALSE(both);
    CHECK(both.error().size() == 2);
}

TEST_CASE("resources: saving writes the whole file at once, and a model with problems is not saved")
{
    const ScriptFolder folder;
    const std::filesystem::path file = folder.path() / "res" / "resources.json";
    const auto missing = loadResources(file);
    REQUIRE(missing);
    CHECK(missing->empty());
    const Resources resources = *parseResources(kExample);
    CHECK_FALSE(static_cast<bool>(saveResources(file, resources)));
    const auto loaded = loadResources(file);
    REQUIRE(loaded);
    CHECK(*loaded == resources);
    Resources wrong = resources;
    wrong.points.front().at.x = 2.0;
    CHECK(saveResources(file, wrong) == std::make_error_code(std::errc::invalid_argument));
    const auto kept = loadResources(file);
    REQUIRE(kept);
    CHECK(*kept == resources);
    folder.write("res/resources.json", "{");
    const auto broken = loadResources(file);
    REQUIRE_FALSE(broken);
    CHECK(broken.error().front() == "$: the file is not a JSON object");
}

TEST_CASE("resources: the file is read again when its time or size changes")
{
    const ScriptFolder folder;
    ResourceFile watched(folder.path() / "res" / "resources.json");
    REQUIRE(watched.current());
    CHECK(watched.current()->empty());
    folder.write("res/resources.json",
                 R"({"format": "gamescrcpy.resources", "version": 1, "points": [{"name": "a", "x": 0.5, "y": 0.5}]})");
    REQUIRE(watched.current());
    CHECK(watched.current()->findPoint("a") != nullptr);
    folder.write("res/resources.json",
                 R"({"format": "gamescrcpy.resources", "version": 1, "points": [{"name": "bb", "x": 0.25, "y": 0.5}]})");
    REQUIRE(watched.current());
    CHECK(watched.current()->findPoint("bb") != nullptr);
    CHECK(watched.current()->findPoint("a") == nullptr);
    folder.write("res/resources.json", "[]");
    CHECK_FALSE(watched.current());
    std::error_code error;
    std::filesystem::remove(folder.path() / "res" / "resources.json", error);
    REQUIRE(watched.current());
    CHECK(watched.current()->empty());
}
