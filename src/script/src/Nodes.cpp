#include "gs/script/Services.h"

#include <algorithm>
#include <array>

namespace gs::script {

namespace {

constexpr std::array<std::string_view, 9> kNodeTypeNames{"",       "boolean", "number", "string",  "point",
                                                         "region", "color",   "duration", "json"};

bool identifierStart(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}

constexpr std::array<std::string_view, 48> kReservedWords{
    "arguments", "await",      "break",     "case",  "catch",    "class",  "const",      "continue",
    "debugger",  "default",    "delete",    "do",    "else",     "enum",   "eval",       "export",
    "extends",   "false",      "finally",   "for",   "function", "if",     "implements", "import",
    "in",        "instanceof", "interface", "let",   "new",      "null",   "package",    "private",
    "protected", "public",     "return",    "static", "super",   "switch", "this",       "throw",
    "true",      "try",        "typeof",    "var",   "void",     "while",  "with",       "yield"};

std::string parameterName(const std::string& name)
{
    return std::ranges::find(kReservedWords, name) != kReservedWords.end() ? name + "_" : name;
}

}

std::optional<NodeType> nodeTypeNamed(std::string_view name) noexcept
{
    for (std::size_t index = 0; index < kNodeTypeNames.size(); ++index) {
        if (kNodeTypeNames[index] == name) {
            return static_cast<NodeType>(index);
        }
    }
    return std::nullopt;
}

std::string_view nodeTypeName(NodeType type) noexcept
{
    const auto index = static_cast<std::size_t>(type);
    return index < kNodeTypeNames.size() ? kNodeTypeNames[index] : std::string_view();
}

std::string_view nodeScriptType(NodeType type) noexcept
{
    switch (type) {
    case NodeType::None:
        return "void";
    case NodeType::Boolean:
        return "boolean";
    case NodeType::Number:
    case NodeType::Duration:
        return "number";
    case NodeType::String:
        return "string";
    case NodeType::Point:
        return "PicturePoint";
    case NodeType::Region:
        return "Region";
    case NodeType::Color:
        return "HexColor";
    case NodeType::Json:
        return "StoreValue";
    }
    return "unknown";
}

std::string nodeSignature(const NodeInfo& node)
{
    std::string params;
    for (const NodeParam& param : node.params) {
        if (!params.empty()) {
            params += ", ";
        }
        params += parameterName(param.name) + ": " + std::string(nodeScriptType(param.type));
    }
    return node.name + "(" + params + "): " + std::string(nodeScriptType(node.returns));
}

std::string nodeSegment(std::string_view idPart)
{
    std::string out;
    out.reserve(idPart.size());
    bool afterHyphen = false;
    for (const char c : idPart) {
        if (c == '-') {
            afterHyphen = true;
            continue;
        }
        if (afterHyphen && c >= 'a' && c <= 'z') {
            out.push_back(static_cast<char>(c - 'a' + 'A'));
        } else {
            if (afterHyphen) {
                out.push_back('_');
            }
            out.push_back(c);
        }
        afterHyphen = false;
    }
    return out;
}

std::vector<std::string> nodeOwner(std::string_view extension)
{
    std::vector<std::string> segments;
    std::size_t start = 0;
    while (start <= extension.size()) {
        const std::size_t dot = extension.find('.', start);
        const std::size_t end = dot == std::string_view::npos ? extension.size() : dot;
        segments.push_back(nodeSegment(extension.substr(start, end - start)));
        if (dot == std::string_view::npos) {
            break;
        }
        start = dot + 1;
    }
    return segments;
}

std::string nodePath(std::string_view extension, std::string_view node)
{
    std::string path = "ext";
    for (const std::string& segment : nodeOwner(extension)) {
        if (!segment.empty() && identifierStart(segment.front())) {
            path += "." + segment;
        } else {
            path += "[\"" + segment + "\"]";
        }
    }
    return path + "." + std::string(node);
}

}
