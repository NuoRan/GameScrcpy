#include "gs/script/ApiFiles.h"

#include "Api.h"
#include "NativeAbi.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::script {

namespace {

using detail::api::Failure;
using detail::api::Function;
using detail::api::Interface;
using detail::api::Member;
using detail::api::Namespace;
using detail::api::Param;
using detail::api::Type;
using detail::api::Value;

constexpr std::string_view kIndexTitle = "GameScrcpy script API";
constexpr std::string_view kIndexLead = "Positions are ratios of the phone picture: (0, 0) is the top left corner and "
                                        "(1, 1) the bottom right one, so scripts work at any resolution and "
                                        "orientation.";
constexpr std::string_view kGlobalsTitle = "Global functions and values";
constexpr std::string_view kObjectsTitle = "Objects";
constexpr std::string_view kTypesTitle = "Types";
constexpr std::string_view kMemberHeader = "Member";
constexpr std::string_view kParameterHeader = "Parameter";
constexpr std::string_view kTypeHeader = "Type";
constexpr std::string_view kDescriptionHeader = "Description";
constexpr std::string_view kPermissionNote = "Needs the permission %1.";
constexpr std::string_view kModuleDoc = "The script API as a module: import { touch, log } from \"gamescrcpy\" in a "
                                        "logic, task or component file gives the same functions and values as the "
                                        "globals.";

std::string said(const ApiTranslate& translate, std::string_view text)
{
    return translate ? translate(text) : std::string(text);
}

std::string filled(std::string text, std::string_view value)
{
    if (const std::size_t at = text.find("%1"); at != std::string::npos) {
        text.replace(at, 2, value);
    }
    return text;
}

constexpr std::string_view kCompilerOptions = R"json(  "compilerOptions": {
    "target": "es2023",
    "module": "esnext",
    "moduleDetection": "force",
    "moduleResolution": "bundler",
    "lib": ["es2023"],
    "strict": true,
    "noEmit": true,
    "erasableSyntaxOnly": true,
    "allowImportingTsExtensions": true,
    "verbatimModuleSyntax": true
  },
)json";

constexpr std::string_view kScriptInclude = R"json(  "include": ["src", "types"]
}
)json";

constexpr std::string_view kProfileInclude = R"json(  "include": ["**/*.ts", "**/*.mts"],
  "exclude": ["node_modules"]
}
)json";

std::string parameterText(const Param& param)
{
    if (param.type.starts_with("...")) {
        return "..." + std::string(param.name) + ": " + std::string(param.type.substr(3));
    }
    return std::string(param.name) + (param.optional ? "?: " : ": ") + std::string(param.type);
}

std::string parametersText(const Function& function)
{
    std::string text;
    for (const Param& param : function.params) {
        if (!text.empty()) {
            text += ", ";
        }
        text += parameterText(param);
    }
    return text;
}

std::string signature(std::string_view owner, const Function& function)
{
    std::string text = owner.empty() ? std::string() : std::string(owner) + ".";
    text += std::string(function.name) + "(" + parametersText(function) + "): " + std::string(function.returns);
    return text;
}

std::string callText(std::string_view owner, const Function& function)
{
    std::string names;
    for (const Param& param : function.params) {
        if (!names.empty()) {
            names += ", ";
        }
        names += std::string(param.name) + (param.optional ? "?" : "");
    }
    return (owner.empty() ? std::string() : std::string(owner) + ".") + std::string(function.name) + "(" + names
         + ")";
}

void docBlock(std::string& out, std::string_view indent, const Function& function)
{
    out += std::string(indent) + "/**\n";
    out += std::string(indent) + " * " + std::string(function.doc) + "\n";
    for (const Param& param : function.params) {
        out += std::string(indent) + " * @param " + std::string(param.name) + " " + std::string(param.doc) + "\n";
    }
    for (const Failure& failure : function.failures) {
        out += std::string(indent) + " * @throws " + std::string(failure.code) + " when " + std::string(failure.when)
             + ". " + std::string(failure.fix) + "\n";
    }
    if (!function.permission.empty()) {
        out += std::string(indent) + " * @permission " + std::string(function.permission) + "\n";
    }
    out += std::string(indent) + " * @since " + std::string(function.since) + " ("
         + std::string(detail::api::stabilityName(function.stability)) + ")\n";
    out += std::string(indent) + " * @example " + std::string(function.example) + "\n";
    out += std::string(indent) + " */\n";
}

void memberBlock(std::string& out, const Member& member)
{
    out += "    /**\n     * " + std::string(member.doc) + "\n";
    if (!member.example.empty()) {
        out += "     * @example " + std::string(member.example) + "\n";
    }
    out += "     */\n    " + std::string(member.declaration) + ";\n";
}

bool overloaded(std::span<const Function> functions, std::string_view name)
{
    return std::ranges::any_of(functions, [name](const Function& function) {
        return function.name == name && !function.overload.empty();
    });
}

const Function* baseOf(std::span<const Function> functions, std::string_view name)
{
    const auto found = std::ranges::find_if(functions, [name](const Function& function) {
        return function.name == name && function.overload.empty();
    });
    return found != functions.end() ? &*found : nullptr;
}

void functionBlock(std::string& out, std::string_view indent, const Function& function)
{
    if (!function.overload.empty()) {
        out += std::string(indent) + "function " + std::string(function.overload) + ";\n";
        return;
    }
    out += std::string(indent) + "function " + std::string(function.name) + "(" + parametersText(function) + "): "
         + std::string(function.returns) + ";\n";
}

std::string declarations()
{
    std::string out;
    for (const Type& type : detail::api::types()) {
        out += "/** " + std::string(type.doc) + " */\n";
        out += "type " + std::string(type.name) + " = " + std::string(type.definition) + ";\n\n";
    }
    for (const Interface& shape : detail::api::interfaces()) {
        out += "/** " + std::string(shape.doc) + " */\n";
        const std::string head = "interface " + std::string(shape.name) +
                                 (shape.base.empty() ? std::string() : " extends " + std::string(shape.base));
        if (shape.members.empty()) {
            out += head + " {}\n\n";
            continue;
        }
        out += head + " {\n";
        bool first = true;
        for (const Member& member : shape.members) {
            if (!first) {
                out += "\n";
            }
            first = false;
            memberBlock(out, member);
        }
        out += "}\n\n";
    }
    for (const Function& function : detail::api::globals()) {
        if (!function.overload.empty()) {
            out += "declare function " + std::string(function.overload) + ";\n\n";
            continue;
        }
        if (overloaded(detail::api::globals(), function.name)) {
            continue;
        }
        docBlock(out, "", function);
        out += "declare function " + std::string(function.name) + "(" + parametersText(function)
             + "): " + std::string(function.returns) + ";\n\n";
    }
    for (const Value& value : detail::api::values()) {
        out += "/**\n * " + std::string(value.doc) + "\n * @example " + std::string(value.example) + "\n */\n";
        out += "declare const " + std::string(value.name) + ": " + std::string(value.type) + ";\n\n";
    }
    for (const Namespace& space : detail::api::namespaces()) {
        out += "/** " + std::string(space.doc) + " */\n";
        out += "declare namespace " + std::string(space.name) + " {\n";
        bool first = true;
        std::set<std::string_view, std::less<>> described;
        for (const Function& function : space.functions) {
            const bool base = function.overload.empty();
            if (base && overloaded(space.functions, function.name)) {
                continue;
            }
            if (!first) {
                out += "\n";
            }
            first = false;
            const Function* source = base ? &function : baseOf(space.functions, function.name);
            if (source != nullptr && (base || described.insert(function.name).second)) {
                docBlock(out, "    ", *source);
            }
            functionBlock(out, "    ", function);
        }
        out += "}\n\n";
    }
    for (const Namespace& space : detail::api::attached()) {
        out += "/** " + std::string(space.doc) + " */\n";
        out += "declare namespace " + std::string(space.name) + " {\n";
        for (std::size_t index = 0; index < space.functions.size(); ++index) {
            const Function& function = space.functions[index];
            out += index == 0 ? "" : "\n";
            docBlock(out, "    ", function);
            out += "    function " + std::string(function.name) + "(" + parametersText(function)
                 + "): " + std::string(function.returns) + ";\n";
        }
        out += "}\n\n";
    }
    out += "/** " + std::string(kModuleDoc) + " */\n";
    out += "declare module \"" + std::string(detail::api::kModuleName) + "\" {\n";
    for (const std::string_view name : detail::api::moduleExports()) {
        out += "    export import " + std::string(name) + " = globalThis." + std::string(name) + ";\n";
    }
    out += "}\n\n";
    out.pop_back();
    return out;
}

std::string cell(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '|') {
            out += '\\';
        }
        out += c;
    }
    return out;
}

void functionPage(std::string& out, std::string_view owner, const Function& function, const ApiTranslate& translate)
{
    out += "## " + callText(owner, function) + "\n\n";
    out += said(translate, function.doc) + "\n\n";
    out += "```ts\n" + signature(owner, function) + "\n```\n\n";
    if (!function.params.empty()) {
        out += "| " + said(translate, kParameterHeader) + " | " + said(translate, kTypeHeader) + " | "
             + said(translate, kDescriptionHeader) + " |\n|---|---|---|\n";
        for (const Param& param : function.params) {
            out += "| `" + std::string(param.name) + "`" + (param.optional ? " (optional)" : "") + " | `"
                 + cell(param.type) + "` | " + cell(said(translate, param.doc)) + " |\n";
        }
        out += "\n";
    }
    out += "Returns `" + std::string(function.returns) + "`. "
         + std::string(detail::api::stabilityName(function.stability)) + " since API "
         + std::string(function.since) + ".";
    if (!function.permission.empty()) {
        out += " " + filled(said(translate, kPermissionNote), "`" + std::string(function.permission) + "`");
    }
    out += "\n\n";
    if (!function.failures.empty()) {
        out += "Errors:\n\n";
        for (const Failure& failure : function.failures) {
            out += "- `" + std::string(failure.code) + "` when " + said(translate, failure.when) + ". "
                 + said(translate, failure.fix) + "\n";
        }
        out += "\n";
    }
    out += "```ts\n" + std::string(function.example) + "\n```\n\n";
}

std::string trimmed(std::string text)
{
    while (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }
    return text + "\n";
}

std::string indexPage(const ApiTranslate& translate)
{
    std::string out = "# " + said(translate, kIndexTitle) + "\n\n";
    out += said(translate, kIndexLead) + "\n\n";
    out += "- [" + said(translate, kGlobalsTitle) + "](globals.md)\n";
    for (const Namespace& space : detail::api::namespaces()) {
        out += "- [`" + std::string(space.name) + "`](" + std::string(space.name) + ".md): "
             + said(translate, space.doc) + "\n";
    }
    out += "\n## " + said(translate, kObjectsTitle) + "\n\n";
    for (const Interface& shape : detail::api::interfaces()) {
        out += "### " + std::string(shape.name) +
               (shape.base.empty() ? std::string() : " extends " + std::string(shape.base)) + "\n\n" +
               said(translate, shape.doc) + "\n\n";
        if (shape.members.empty()) {
            continue;
        }
        out += "| " + said(translate, kMemberHeader) + " | " + said(translate, kDescriptionHeader) + " |\n|---|---|\n";
        for (const Member& member : shape.members) {
            out += "| `" + cell(member.declaration) + "` | " + cell(said(translate, member.doc)) + " |\n";
        }
        out += "\n";
    }
    out += "## " + said(translate, kTypesTitle) + "\n\n";
    for (const Type& type : detail::api::types()) {
        out += "### " + std::string(type.name) + "\n\n" + said(translate, type.doc) + "\n\n```ts\ntype "
             + std::string(type.name) + " = " + std::string(type.definition) + ";\n```\n\n";
    }
    return trimmed(out);
}

std::string globalsPage(const ApiTranslate& translate)
{
    std::string out = "# " + said(translate, kGlobalsTitle) + "\n\n";
    for (const Function& function : detail::api::globals()) {
        if (function.overload.empty()) {
            functionPage(out, "", function, translate);
        }
    }
    for (const Value& value : detail::api::values()) {
        out += "## " + std::string(value.name) + "\n\n" + said(translate, value.doc) + "\n\n```ts\ndeclare const "
             + std::string(value.name) + ": " + std::string(value.type) + ";\n```\n\n```ts\n"
             + std::string(value.example) + "\n```\n\n";
    }
    return trimmed(out);
}

std::string namespacePage(const Namespace& space, const ApiTranslate& translate)
{
    std::string out = "# " + std::string(space.name) + "\n\n" + said(translate, space.doc) + "\n\n";
    for (const Function& function : space.functions) {
        if (function.overload.empty()) {
            functionPage(out, space.name, function, translate);
        }
    }
    for (const Namespace& extra : detail::api::attached()) {
        if (extra.name.starts_with(std::string(space.name) + ".")) {
            for (const Function& function : extra.functions) {
                if (function.overload.empty()) {
                    functionPage(out, extra.name, function, translate);
                }
            }
        }
    }
    return trimmed(out);
}

nlohmann::ordered_json completion(std::string_view owner, const Function& function)
{
    std::string snippet = (owner.empty() ? std::string() : std::string(owner) + ".") + std::string(function.name) + "(";
    int slot = 0;
    for (const Param& param : function.params) {
        if (param.optional || param.type.starts_with("...")) {
            continue;
        }
        snippet += (slot > 0 ? ", ${" : "${") + std::to_string(slot + 1) + ":" + std::string(param.name) + "}";
        ++slot;
    }
    snippet += ")";
    return {{"label", (owner.empty() ? std::string() : std::string(owner) + ".") + std::string(function.name)},
            {"kind", "function"},
            {"detail", signature(owner, function)},
            {"documentation", std::string(function.doc)},
            {"insertText", snippet}};
}

std::string completions()
{
    nlohmann::ordered_json list = nlohmann::ordered_json::array();
    for (const Function& function : detail::api::globals()) {
        if (function.overload.empty()) {
            list.push_back(completion("", function));
        }
    }
    for (const Value& value : detail::api::values()) {
        list.push_back({{"label", std::string(value.name)},
                        {"kind", "variable"},
                        {"detail", std::string(value.name) + ": " + std::string(value.type)},
                        {"documentation", std::string(value.doc)},
                        {"insertText", std::string(value.name)}});
    }
    for (const Namespace& space : detail::api::namespaces()) {
        list.push_back({{"label", std::string(space.name)},
                        {"kind", "namespace"},
                        {"detail", std::string(space.name)},
                        {"documentation", std::string(space.doc)},
                        {"insertText", std::string(space.name)}});
        for (const Function& function : space.functions) {
            if (function.overload.empty()) {
                list.push_back(completion(space.name, function));
            }
        }
    }
    for (const Namespace& space : detail::api::attached()) {
        for (const Function& function : space.functions) {
            if (function.overload.empty()) {
                list.push_back(completion(space.name, function));
            }
        }
    }
    for (const Interface& shape : detail::api::interfaces()) {
        for (const Member& member : shape.members) {
            const bool method = member.declaration.find('(') != std::string_view::npos;
            list.push_back({{"label", std::string(shape.name) + "." + std::string(member.name)},
                            {"kind", method ? "method" : "property"},
                            {"detail", std::string(member.declaration)},
                            {"documentation", std::string(member.doc)},
                            {"insertText", std::string(member.name) + (method ? "(${1})" : "")}});
        }
    }
    for (const Type& type : detail::api::types()) {
        list.push_back({{"label", std::string(type.name)},
                        {"kind", "type"},
                        {"detail", "type " + std::string(type.name)},
                        {"documentation", std::string(type.doc)},
                        {"insertText", std::string(type.name)}});
    }
    return list.dump(2) + "\n";
}

nlohmann::ordered_json reportEntry(std::string_view owner, const Function& function)
{
    nlohmann::ordered_json errors = nlohmann::ordered_json::array();
    for (const Failure& failure : function.failures) {
        errors.push_back(std::string(failure.code));
    }
    return {{"name", std::string(function.name)},
            {"signature", signature(owner, function)},
            {"stability", std::string(detail::api::stabilityName(function.stability))},
            {"since", std::string(function.since)},
            {"permission", std::string(function.permission)},
            {"errors", errors}};
}

std::string report()
{
    nlohmann::ordered_json globals = nlohmann::ordered_json::array();
    for (const Function& function : detail::api::globals()) {
        if (function.overload.empty()) {
            globals.push_back(reportEntry("", function));
        }
    }
    nlohmann::ordered_json values = nlohmann::ordered_json::array();
    for (const Value& value : detail::api::values()) {
        values.push_back({{"name", std::string(value.name)}, {"type", std::string(value.type)}});
    }
    nlohmann::ordered_json spaces = nlohmann::ordered_json::array();
    for (const Namespace& space : detail::api::namespaces()) {
        nlohmann::ordered_json functions = nlohmann::ordered_json::array();
        for (const Function& function : space.functions) {
            if (function.overload.empty()) {
                functions.push_back(reportEntry(space.name, function));
            }
        }
        spaces.push_back({{"name", std::string(space.name)}, {"functions", functions}});
    }
    nlohmann::ordered_json shapes = nlohmann::ordered_json::array();
    for (const Interface& shape : detail::api::interfaces()) {
        nlohmann::ordered_json members = nlohmann::ordered_json::array();
        for (const Member& member : shape.members) {
            members.push_back(std::string(member.declaration));
        }
        shapes.push_back({{"name", std::string(shape.name)}, {"members", members}});
    }
    nlohmann::ordered_json types = nlohmann::ordered_json::array();
    for (const Type& type : detail::api::types()) {
        types.push_back({{"name", std::string(type.name)}, {"definition", std::string(type.definition)}});
    }
    const nlohmann::ordered_json root{{"api", 1},           {"globals", globals}, {"values", values},
                                      {"namespaces", spaces}, {"interfaces", shapes}, {"types", types}};
    return root.dump(2) + "\n";
}

bool plainIdentifier(std::string_view name) noexcept
{
    if (name.empty()) {
        return false;
    }
    const auto leading = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$'; };
    const auto rest = [&leading](char c) { return leading(c) || (c >= '0' && c <= '9'); };
    return leading(name.front()) && std::all_of(name.begin() + 1, name.end(), rest);
}

std::string propertyName(std::string_view id)
{
    return plainIdentifier(id) ? std::string(id) : nlohmann::json(std::string(id)).dump();
}

std::string docText(std::string text)
{
    for (std::size_t at = text.find("*/"); at != std::string::npos; at = text.find("*/", at + 2)) {
        text.replace(at, 2, "*\\/");
    }
    std::ranges::replace(text, '\n', ' ');
    std::ranges::replace(text, '\r', ' ');
    return text;
}

template <typename Item, typename Describe>
void nameList(std::string& out, std::string_view shape, const std::vector<Item>& items, Describe describe)
{
    if (items.empty()) {
        return;
    }
    out += "\ninterface " + std::string(shape) + " {\n";
    for (const Item& item : items) {
        out += "    /** " + docText(describe(item)) + " */\n";
        out += "    readonly " + propertyName(item.name) + ": true;\n";
    }
    out += "}\n";
}

std::string translationSource()
{
    std::string out;
    for (const std::string& text : apiTexts()) {
        out += "QT_TRANSLATE_NOOP(\"" + std::string(kApiTextContext) + "\", " + nlohmann::json(text).dump() + ");\n";
    }
    return out;
}

std::string interfaceOf(std::string_view type)
{
    static constexpr std::string_view kTypes[] = {"tap",       "hold",      "longPress", "turbo",    "swipe",
                                                  "pinch",     "wasd",      "rightWalk", "padStick", "camera",
                                                  "padCamera", "freeLook",  "skill",     "dial",     "multiPress",
                                                  "tempCursor", "actionWheel"};
    if (std::ranges::find(kTypes, type) == std::end(kTypes)) {
        return "Component";
    }
    std::string name(type);
    name.front() = static_cast<char>(name.front() - 'a' + 'A');
    return name;
}

std::string commentLine(std::string_view indent, std::string_view text)
{
    return std::string(indent) + "/* " + docText(std::string(text)) + " */\n";
}

std::string apiTypeDoc(std::string_view name)
{
    for (const Type& type : detail::api::types()) {
        if (type.name == name) {
            return std::string(type.doc);
        }
    }
    return {};
}

std::string nativeHeader()
{
    namespace native = detail::native;
    std::string out = commentLine("", native::headerDoc());
    out += "#ifndef GAMESCRCPY_EXT_H\n#define GAMESCRCPY_EXT_H\n\n#include <stddef.h>\n#include <stdint.h>\n\n";
    out += "#if defined(_WIN32)\n#define GS_EXT_EXPORT __declspec(dllexport)\n#else\n"
           "#define GS_EXT_EXPORT __attribute__((visibility(\"default\")))\n#endif\n\n";
    out += "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n";
    for (const native::Constant& constant : native::constants()) {
        out += commentLine("", constant.doc);
        out += "#define " + std::string(constant.name) + " " + std::string(constant.value) + "\n\n";
    }
    for (const native::Enumeration& enumeration : native::enumerations()) {
        out += commentLine("", enumeration.doc);
        out += "enum " + std::string(enumeration.name) + " {\n";
        for (const native::Enumerator& value : enumeration.values) {
            std::string doc(value.doc);
            if (const std::string more = value.apiType.empty() ? std::string() : apiTypeDoc(value.apiType);
                !more.empty()) {
                doc += " " + more;
            }
            out += commentLine("    ", doc);
            out += "    " + std::string(value.name) + " = " + std::to_string(value.value) + ",\n";
        }
        out += "};\n\n";
    }
    for (const native::Structure& structure : native::structures()) {
        out += "typedef struct " + std::string(structure.name) + " " + std::string(structure.name) + ";\n";
    }
    out += "\n";
    for (const native::Structure& structure : native::structures()) {
        out += commentLine("", structure.doc);
        out += "struct " + std::string(structure.name) + " {\n";
        for (const native::Field& field : structure.fields) {
            out += commentLine("    ", field.doc);
            out += "    " + std::string(field.declaration) + ";\n";
        }
        out += "};\n\n";
    }
    const native::Entry& entry = native::entry();
    out += commentLine("", entry.doc);
    out += "typedef " + std::string(entry.returns) + " (*" + std::string(entry.name) + "_function)(" +
           std::string(entry.parameters) + ");\n";
    out += "GS_EXT_EXPORT " + std::string(entry.returns) + " " + std::string(entry.name) + "(" +
           std::string(entry.parameters) + ");\n\n";
    out += "#ifdef __cplusplus\n}\n#endif\n\n#endif\n";
    return out;
}

struct NodeLevel {
    std::string doc;
    std::map<std::string, std::unique_ptr<NodeLevel>, std::less<>> children;
    std::vector<const NodeInfo*> nodes;
};

void writeLevel(std::string& out, const NodeLevel& level, const std::string& indent)
{
    for (const NodeInfo* node : level.nodes) {
        if (level.children.contains(node->name)) {
            continue;
        }
        if (!node->description.empty()) {
            out += indent + "/** " + docText(node->description) + " */\n";
        }
        out += indent + nodeSignature(*node) + ";\n";
    }
    for (const auto& [name, child] : level.children) {
        if (!child->doc.empty()) {
            out += indent + "/** " + docText(child->doc) + " */\n";
        }
        out += indent + "readonly " + propertyName(name) + ": {\n";
        writeLevel(out, *child, indent + "    ");
        out += indent + "};\n";
    }
}

}

std::vector<ApiFile> apiDocs(const ApiTranslate& translate)
{
    std::vector<ApiFile> docs;
    docs.push_back({"docs/README.md", indexPage(translate)});
    docs.push_back({"docs/globals.md", globalsPage(translate)});
    for (const Namespace& space : detail::api::namespaces()) {
        docs.push_back({"docs/" + std::string(space.name) + ".md", namespacePage(space, translate)});
    }
    return docs;
}

std::vector<std::string> apiTexts()
{
    std::vector<std::string> texts;
    std::set<std::string, std::less<>> seen;
    static_cast<void>(apiDocs([&texts, &seen](std::string_view text) {
        if (seen.insert(std::string(text)).second) {
            texts.emplace_back(text);
        }
        return std::string(text);
    }));
    return texts;
}

std::vector<ApiFile> apiFiles()
{
    std::vector<ApiFile> files;
    files.push_back({"types/gamescrcpy.d.ts", declarations()});
    for (ApiFile& page : apiDocs({})) {
        files.push_back(std::move(page));
    }
    files.push_back({"completions.json", completions()});
    files.push_back({"api-report.json", report()});
    files.push_back({"i18n/api-texts.js", translationSource()});
    files.push_back({std::string(detail::native::kHeaderPath), nativeHeader()});
    return files;
}

std::string extensionTypes(const std::vector<NodeInfo>& nodes)
{
    NodeLevel root;
    for (const NodeInfo& node : nodes) {
        NodeLevel* level = &root;
        for (const std::string& segment : nodeOwner(node.extension)) {
            std::unique_ptr<NodeLevel>& child = level->children[segment];
            if (child == nullptr) {
                child = std::make_unique<NodeLevel>();
            }
            level = child.get();
        }
        if (level->doc.empty()) {
            level->doc = node.extensionName.empty() ? node.extension : node.extensionName + " (" + node.extension + ")";
        }
        level->nodes.push_back(&node);
    }
    for (const char* taken : {"call", "provide"}) {
        if (const auto found = root.children.find(taken); found != root.children.end()) {
            root.children.erase(found);
        }
    }
    if (root.children.empty()) {
        return "interface Extensions {}\n";
    }
    std::string out = "interface Extensions {\n";
    writeLevel(out, root, "    ");
    return out + "}\n";
}

std::string profileTypes(const std::vector<ControlInfo>& controls, const std::vector<StateInfo>& states,
                         const Resources& resources)
{
    std::string out = "interface Controls {\n";
    for (const ControlInfo& control : controls) {
        const std::string label = control.name.empty() ? control.type : control.name + " · " + control.type;
        out += "    /** " + docText(label) + " */\n";
        out += "    readonly " + propertyName(control.id) + ": " + interfaceOf(control.type)
             + " & { readonly position: PicturePoint };\n";
    }
    out += "}\n\ninterface Vars {\n";
    for (const StateInfo& state : states) {
        if (!state.name.empty()) {
            out += "    /** " + docText(state.name) + " */\n";
        }
        out += "    readonly " + propertyName(state.id) + ": Variable;\n";
    }
    out += "}\n";
    nameList(out, "TemplateNames", resources.templates, [](const TemplateResource& entry) {
        return std::format("res/{} · threshold {} · {}", entry.file, entry.threshold,
                           entry.match == MatchMode::Color ? "color" : "grey");
    });
    nameList(out, "RegionNames", resources.regions, [](const RegionResource& entry) {
        return std::format("x {} y {} · {} × {}", entry.area.x, entry.area.y, entry.area.width, entry.area.height);
    });
    nameList(out, "PointNames", resources.points,
             [](const PointResource& entry) { return std::format("x {} y {}", entry.at.x, entry.at.y); });
    nameList(out, "PathNames", resources.paths, [](const PathResource& entry) {
        return std::format("{} points · {} ms", entry.points.size(), entry.durationMs);
    });
    nameList(out, "ColorNames", resources.colors, [](const ColorResource& entry) {
        return std::format("{} · tolerance {}", colorText(entry.color), entry.tolerance);
    });
    return out;
}

std::string scriptTsconfig()
{
    return "{\n" + std::string(kCompilerOptions) + std::string(kScriptInclude);
}

std::string profileTsconfig()
{
    return "{\n" + std::string(kCompilerOptions) + std::string(kProfileInclude);
}

}
