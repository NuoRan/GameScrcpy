#include "gs/ext/FilterSource.h"
#include "gs/ext/Manifest.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace gs::ext;

namespace {

bool reports(const FilterSource& source, Code code, std::string_view path)
{
    return std::ranges::any_of(source.diagnostics, [&](const Diagnostic& diagnostic) {
        return diagnostic.code == code && diagnostic.path == path;
    });
}

bool has(const std::string& text, std::string_view part)
{
    return text.find(part) != std::string::npos;
}

std::string readExample(const char* path)
{
    std::ifstream file(std::string(GS_EXAMPLES_DIR) + "/" + path, std::ios::binary);
    REQUIRE_MESSAGE(file.is_open(), path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

}

TEST_CASE("the example scanline extension has a clean manifest and filter")
{
    const LoadedManifest loaded = loadManifest(readExample("extensions/crt-scanlines/manifest.json"), {.official = true});
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    REQUIRE(loaded.manifest.contributes.filters.size() == 1);
    const FilterSource source = parseFilterSource(
        readExample(("extensions/crt-scanlines/" + loaded.manifest.contributes.filters.front().file).c_str()));
    CHECK(source.usable());
    CHECK(source.diagnostics.empty());
    REQUIRE(source.params.size() == 2);
    for (const FilterParam& param : source.params) {
        const auto setting = std::ranges::find(loaded.manifest.settings, param.name, &SettingDeclaration::key);
        CHECK_MESSAGE(setting != loaded.manifest.settings.end(), param.name);
    }
}

TEST_CASE("filter directives declare parameters, passes and textures, and the host interface wraps the source")
{
    const FilterSource source = parseFilterSource(R"(//! PARAM strength float 0.5 0 1 0.05
//! PARAM lines int 480 100 2000
//! PARAM glow bool true
//! TEXTURE mask textures/mask.png
//! PASS blur scale=0.5 format=rgba16f
float4 blur(float2 uv) { return Source.Sample(Linear, uv); }
//! PASS scanlines
float4 scanlines(float2 uv) { return Source.Sample(Linear, uv) * strength; }
)");
    CHECK(source.usable());
    CHECK(source.diagnostics.empty());
    REQUIRE(source.params.size() == 3);
    CHECK(source.params[0] == FilterParam{.name = "strength",
                                          .type = ParamType::Float,
                                          .defaultValue = 0.5,
                                          .min = 0.0,
                                          .max = 1.0,
                                          .step = 0.05,
                                          .line = 1});
    CHECK(source.params[1].type == ParamType::Int);
    CHECK(source.params[1].max == 2000.0);
    CHECK(source.params[2].defaultValue == 1.0);
    REQUIRE(source.passes.size() == 2);
    CHECK(source.passes[0].scale == 0.5);
    CHECK(source.passes[0].format == PassFormat::Rgba16f);
    CHECK(source.passes[1].format == PassFormat::Rgba8);
    CHECK(source.passes[1].line == 7);
    CHECK(source.textures == std::vector<FilterTexture>{{"mask", "textures/mask.png", 4}});
    CHECK(constantBufferSize(source.params) == 16);
    CHECK(passEntryPoint(source.passes[0]) == "GsPass_blur");

    const std::string text = filterShaderText(source, "shaders\\crt.hlsl");
    CHECK(has(text, "cbuffer GsFrame : register(b0)"));
    CHECK(has(text, "cbuffer GsParams : register(b1)\n{\n    float strength;\n    int lines;\n    bool glow;\n};"));
    CHECK(has(text, "Texture2D Source : register(t1);"));
    CHECK(has(text, "Texture2D mask : register(t2);"));
    CHECK(has(text, "#line 1 \"shaders/crt.hlsl\"\n//! PARAM strength"));
    CHECK(has(text, "float4 GsPass_scanlines(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n{\n"
                    "    return scanlines(uv);\n}"));
}

TEST_CASE("bad directives are reported with their line")
{
    const FilterSource source = parseFilterSource("//! PARAM 1x float 0\n"
                                                  "//! PARAM a float\n"
                                                  "//! PARAM b double 0\n"
                                                  "//! PARAM c int 1.5\n"
                                                  "//! PARAM d float 2 0 1\n"
                                                  "//! PARAM e float 0 1 0\n"
                                                  "//! PARAM f float 0 0 1 0\n"
                                                  "//! PARAM g bool true 0 1\n"
                                                  "//! PARAM Source float 0\n"
                                                  "//! PARAM GsThing float 0\n"
                                                  "//! PASS h scale=5\n"
                                                  "//! PASS i format=rgb565\n"
                                                  "//! PASS j blend=add\n"
                                                  "//! TEXTURE k ../x.png\n"
                                                  "//! TEXTURE l mask.jpg\n"
                                                  "//! SHADER m\n"
                                                  "//! PASS ok\n"
                                                  "//! PARAM ok float 0\n"
                                                  "//!\n");
    CHECK_FALSE(source.usable());
    for (const int line : {1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 13, 16, 19}) {
        CHECK_MESSAGE(reports(source, Code::FilterDirectiveInvalid, "line " + std::to_string(line)), line);
    }
    CHECK(reports(source, Code::FilterNameReserved, "line 9"));
    CHECK(reports(source, Code::FilterNameReserved, "line 10"));
    CHECK(reports(source, Code::PathInvalid, "line 14"));
    CHECK(reports(source, Code::PathInvalid, "line 15"));
    CHECK(reports(source, Code::Duplicate, "line 18"));
    CHECK(source.passes.size() == 1);
    CHECK(source.params.empty());
    CHECK_FALSE(reports(source, Code::FilterPassMissing, "$"));
}

TEST_CASE("a filter needs a pass, and CRLF line ends and indented directives read the same")
{
    CHECK(reports(parseFilterSource("float4 f(float2 uv) { return 0; }\n"), Code::FilterPassMissing, "$"));
    CHECK(reports(parseFilterSource(std::string(1024 * 1024 + 1, ' ')), Code::TooLarge, "$"));
    const FilterSource source = parseFilterSource("  //! PASS a\r\n\t//! PARAM k float 1\r\nfloat4 a(float2 uv) { return k; }");
    CHECK(source.usable());
    CHECK(source.passes.front().name == "a");
    CHECK(source.params.front().line == 2);
    CHECK(constantBufferSize({}) == 16);
    CHECK(constantBufferSize(std::vector<FilterParam>(5)) == 32);
    CHECK(filterShaderText(source, "a.hlsl").find("cbuffer GsParams") != std::string::npos);
    CHECK(filterShaderText(parseFilterSource("//! PASS a\n"), "a.hlsl").find("cbuffer GsParams") == std::string::npos);
}
