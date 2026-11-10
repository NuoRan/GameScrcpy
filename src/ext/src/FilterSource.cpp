#include "gs/ext/FilterSource.h"

#include "gs/ext/Archive.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <span>
#include <system_error>
#include <utility>

namespace gs::ext {

namespace {

constexpr std::string_view kDirective = "//!";
constexpr std::size_t kMaxNameLength = 64;

bool identifierStart(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool identifierChar(char c) noexcept
{
    return identifierStart(c) || (c >= '0' && c <= '9');
}

bool validIdentifier(std::string_view text) noexcept
{
    return !text.empty() && text.size() <= kMaxNameLength && identifierStart(text.front()) &&
           std::ranges::all_of(text, identifierChar);
}

bool reservedName(std::string_view name) noexcept
{
    return std::ranges::find(kFilterBuiltins, name) != kFilterBuiltins.end() || name.starts_with("Gs");
}

std::string_view trimmed(std::string_view text) noexcept
{
    const auto blank = [](char c) { return c == ' ' || c == '\t'; };
    while (!text.empty() && blank(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && blank(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

std::vector<std::string_view> words(std::string_view text)
{
    std::vector<std::string_view> result;
    std::size_t at = 0;
    while (at < text.size()) {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) {
            ++at;
        }
        const std::size_t start = at;
        while (at < text.size() && text[at] != ' ' && text[at] != '\t') {
            ++at;
        }
        if (at > start) {
            result.push_back(text.substr(start, at - start));
        }
    }
    return result;
}

std::optional<double> decimal(std::string_view text)
{
    double value = 0.0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> valueOf(ParamType type, std::string_view text)
{
    if (type == ParamType::Bool) {
        if (text == "true" || text == "1") {
            return 1.0;
        }
        if (text == "false" || text == "0") {
            return 0.0;
        }
        return std::nullopt;
    }
    const std::optional<double> value = decimal(text);
    if (!value || (type == ParamType::Int && (std::floor(*value) != *value || std::abs(*value) > 2147483647.0))) {
        return std::nullopt;
    }
    return value;
}

bool validTextureFile(std::string_view file)
{
    if (!safeArchivePath(file) || file.ends_with('/')) {
        return false;
    }
    std::string lower(file);
    std::ranges::transform(lower, lower.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; });
    return lower.ends_with(".png");
}

const char* hlslType(ParamType type) noexcept
{
    switch (type) {
    case ParamType::Float:
        return "float";
    case ParamType::Int:
        return "int";
    case ParamType::Bool:
        return "bool";
    }
    return "float";
}

std::string lineName(std::string_view fileName)
{
    std::string name(fileName);
    std::ranges::replace(name, '\\', '/');
    std::erase(name, '"');
    return name;
}

class Parser
{
public:
    explicit Parser(FilterSource& source) : m_source(source) {}

    void read(std::size_t at, std::string_view text)
    {
        std::string_view rest = trimmed(text);
        if (!rest.starts_with(kDirective)) {
            return;
        }
        rest.remove_prefix(kDirective.size());
        const std::vector<std::string_view> parts = words(rest);
        if (parts.empty()) {
            invalid(at, "empty directive");
            return;
        }
        const std::span<const std::string_view> arguments(parts.begin() + 1, parts.end());
        if (parts.front() == "PARAM") {
            param(at, arguments);
        } else if (parts.front() == "PASS") {
            pass(at, arguments);
        } else if (parts.front() == "TEXTURE") {
            texture(at, arguments);
        } else {
            invalid(at, "unknown directive " + std::string(parts.front()));
        }
    }

private:
    void param(std::size_t at, std::span<const std::string_view> arguments)
    {
        if (arguments.size() != 3 && arguments.size() != 5 && arguments.size() != 6) {
            invalid(at, "write //! PARAM <name> <float|int|bool> <default> [<min> <max> [<step>]]");
            return;
        }
        if (!claim(at, arguments[0])) {
            return;
        }
        FilterParam declared{.name = std::string(arguments[0]), .line = at};
        if (arguments[1] == "float") {
            declared.type = ParamType::Float;
        } else if (arguments[1] == "int") {
            declared.type = ParamType::Int;
        } else if (arguments[1] == "bool") {
            declared.type = ParamType::Bool;
        } else {
            invalid(at, "the type is float, int or bool");
            return;
        }
        if (declared.type == ParamType::Bool && arguments.size() != 3) {
            invalid(at, "a bool parameter has no range");
            return;
        }
        const std::optional<double> fallback = valueOf(declared.type, arguments[2]);
        if (!fallback) {
            invalid(at, "the default is not a " + std::string(hlslType(declared.type)));
            return;
        }
        declared.defaultValue = *fallback;
        if (arguments.size() >= 5) {
            const std::optional<double> low = valueOf(declared.type, arguments[3]);
            const std::optional<double> high = valueOf(declared.type, arguments[4]);
            if (!low || !high || *low > *high) {
                invalid(at, "the range is <min> <max> with min at most max");
                return;
            }
            if (declared.defaultValue < *low || declared.defaultValue > *high) {
                invalid(at, "the default is outside the range");
                return;
            }
            declared.min = low;
            declared.max = high;
        }
        if (arguments.size() == 6) {
            const std::optional<double> step = valueOf(declared.type, arguments[5]);
            if (!step || *step <= 0.0) {
                invalid(at, "the step is a positive number");
                return;
            }
            declared.step = step;
        }
        if (m_source.params.size() == kMaxFilterParams) {
            report(Code::TooMany, at, std::to_string(kMaxFilterParams) + " parameters at most");
            return;
        }
        m_source.params.push_back(std::move(declared));
    }

    void pass(std::size_t at, std::span<const std::string_view> arguments)
    {
        if (arguments.empty()) {
            invalid(at, "write //! PASS <name> [scale=<ratio>] [format=rgba8|rgba16f]");
            return;
        }
        if (!claim(at, arguments[0])) {
            return;
        }
        FilterPass declared{.name = std::string(arguments[0]), .line = at};
        for (const std::string_view option : arguments.subspan(1)) {
            const std::size_t equals = option.find('=');
            const std::string_view key = option.substr(0, equals);
            const std::string_view value =
                equals == std::string_view::npos ? std::string_view{} : option.substr(equals + 1);
            if (key == "scale") {
                const std::optional<double> scale = decimal(value);
                if (!scale || *scale <= 0.0 || *scale > kMaxPassScale) {
                    invalid(at, "scale is a ratio above 0 and at most 4");
                    return;
                }
                declared.scale = *scale;
            } else if (key == "format" && (value == "rgba8" || value == "rgba16f")) {
                declared.format = value == "rgba8" ? PassFormat::Rgba8 : PassFormat::Rgba16f;
            } else {
                invalid(at, "unknown pass option " + std::string(option));
                return;
            }
        }
        if (m_source.passes.size() == kMaxFilterPasses) {
            report(Code::TooMany, at, std::to_string(kMaxFilterPasses) + " passes at most");
            return;
        }
        m_source.passes.push_back(std::move(declared));
    }

    void texture(std::size_t at, std::span<const std::string_view> arguments)
    {
        if (arguments.size() != 2) {
            invalid(at, "write //! TEXTURE <name> <file.png>");
            return;
        }
        if (!claim(at, arguments[0])) {
            return;
        }
        if (!validTextureFile(arguments[1])) {
            report(Code::PathInvalid, at, std::string(arguments[1]));
            return;
        }
        if (m_source.textures.size() == kMaxFilterTextures) {
            report(Code::TooMany, at, std::to_string(kMaxFilterTextures) + " textures at most");
            return;
        }
        m_source.textures.push_back({std::string(arguments[0]), std::string(arguments[1]), at});
    }

    bool claim(std::size_t at, std::string_view name)
    {
        if (!validIdentifier(name)) {
            invalid(at, "not an HLSL identifier: " + std::string(name));
            return false;
        }
        if (reservedName(name)) {
            report(Code::FilterNameReserved, at, std::string(name));
            return false;
        }
        if (!m_names.emplace(name).second) {
            report(Code::Duplicate, at, std::string(name));
            return false;
        }
        return true;
    }

    void invalid(std::size_t at, std::string detail) { report(Code::FilterDirectiveInvalid, at, std::move(detail)); }

    void report(Code code, std::size_t at, std::string detail)
    {
        m_source.diagnostics.push_back({Severity::Error, code, "line " + std::to_string(at), std::move(detail)});
    }

    FilterSource& m_source;
    std::set<std::string, std::less<>> m_names;
};

}

FilterSource parseFilterSource(std::string_view hlsl)
{
    FilterSource source;
    if (hlsl.size() > kMaxFilterSourceBytes) {
        source.diagnostics.push_back({Severity::Error, Code::TooLarge, "$", "1 MiB at most"});
        return source;
    }
    source.body = std::string(hlsl);
    Parser parser(source);
    std::size_t line = 0;
    std::size_t start = 0;
    while (start <= hlsl.size()) {
        const std::size_t end = std::min(hlsl.find('\n', start), hlsl.size());
        std::string_view text = hlsl.substr(start, end - start);
        if (text.ends_with('\r')) {
            text.remove_suffix(1);
        }
        parser.read(++line, text);
        start = end + 1;
    }
    if (source.passes.empty()) {
        source.diagnostics.push_back({Severity::Error, Code::FilterPassMissing, "$", "declare at least one //! PASS"});
    }
    return source;
}

std::size_t constantBufferSize(const std::vector<FilterParam>& params) noexcept
{
    return std::max<std::size_t>(16, (params.size() * 4 + 15) / 16 * 16);
}

std::string passEntryPoint(const FilterPass& pass)
{
    return "GsPass_" + pass.name;
}

std::string filterShaderText(const FilterSource& source, std::string_view fileName)
{
    std::string text = "cbuffer GsFrame : register(b0)\n{\n    float4 SourceSize;\n    float4 OriginalSize;\n"
                       "    float4 OutputSize;\n    uint Frame;\n    uint3 GsPad;\n};\n";
    if (!source.params.empty()) {
        text += "cbuffer GsParams : register(b1)\n{\n";
        for (const FilterParam& param : source.params) {
            text += "    " + std::string(hlslType(param.type)) + " " + param.name + ";\n";
        }
        text += "};\n";
    }
    text += "SamplerState Linear : register(s0);\nSamplerState Point : register(s1);\n"
            "Texture2D Original : register(t0);\nTexture2D Source : register(t1);\n";
    for (std::size_t i = 0; i < source.textures.size(); ++i) {
        text += "Texture2D " + source.textures[i].name + " : register(t" + std::to_string(i + 2) + ");\n";
    }
    text += "#line 1 \"" + lineName(fileName) + "\"\n" + source.body;
    if (!text.ends_with('\n')) {
        text += '\n';
    }
    text += "#line 1 \"gs-entry-points\"\n";
    for (const FilterPass& pass : source.passes) {
        text += "float4 " + passEntryPoint(pass) + "(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n{\n"
                "    return " + pass.name + "(uv);\n}\n";
    }
    return text;
}

}
