#include "Variables.h"

#include <algorithm>

namespace gs::blueprint {

namespace {

std::string_view trimmed(std::string_view text)
{
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string_view elementOfKind(std::string_view kind, std::string_view type)
{
    if (kind == "toggle") {
        return "boolean";
    }
    if (kind == "integer" || kind == "decimal" || kind == "duration") {
        return "number";
    }
    if (kind == "text") {
        return "string";
    }
    if (kind == "point") {
        return "PicturePoint";
    }
    if (kind == "region") {
        return "Region";
    }
    if (kind == "color") {
        return "HexColor";
    }
    if (kind == "image") {
        return "ImagePath";
    }
    if (kind == "key") {
        return "InputName";
    }
    if (kind == "component" || kind == "class") {
        return type.empty() ? std::string_view("Component") : type;
    }
    return type.empty() ? std::string_view("string") : type;
}

bool isTagName(std::string_view name)
{
    return name == "category" || name == "decimal" || name == "duration" || name == "keep" || name == "editable";
}

std::string_view tagAt(std::string_view text, std::size_t at)
{
    std::size_t end = at;
    while (end < text.size()) {
        const char c = text[end];
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!letter) {
            break;
        }
        ++end;
    }
    return text.substr(at, end - at);
}

}

const std::vector<VariableType>& variableTypes()
{
    static const std::vector<VariableType> made = {
        {"toggle"}, {"integer"}, {"decimal"}, {"text"},   {"point"},   {"region"},
        {"color"},  {"image"},   {"key"},     {"duration"}, {"component"}, {"class"},
    };
    return made;
}

std::vector<std::string> variableTypeNames()
{
    std::vector<std::string> names;
    for (const VariableType& type : variableTypes()) {
        names.push_back(type.kind);
    }
    return names;
}

std::string variableAnnotation(std::string_view kind, std::string_view type, std::string_view container,
                               std::string_view keyKind)
{
    const std::string_view element = elementOfKind(kind, type);
    if (container == "list") {
        return std::string(element) + "[]";
    }
    if (container == "dict") {
        return "Map<" + std::string(keyKind == "number" ? "number" : "string") + ", " + std::string(element) + ">";
    }
    if (container == "set") {
        return "Set<" + std::string(element) + ">";
    }
    return std::string(element);
}

std::string variableDefault(std::string_view kind, std::string_view type, std::string_view container,
                            std::string_view keyKind, char quote)
{
    const std::string text(1, quote);
    if (container == "list") {
        return "[]";
    }
    if (container == "dict" || container == "set") {
        return "new " + variableAnnotation(kind, type, container, keyKind) + "()";
    }
    if (kind == "toggle") {
        return "false";
    }
    if (kind == "integer") {
        return "0";
    }
    if (kind == "decimal") {
        return "0.5";
    }
    if (kind == "duration") {
        return "500";
    }
    if (kind == "text") {
        return text + text;
    }
    if (kind == "point") {
        return "{ x: 0.5, y: 0.5 }";
    }
    if (kind == "region") {
        return "{ x: 0, y: 0, width: 1, height: 1 }";
    }
    if (kind == "color") {
        return text + "#FFFFFF" + text;
    }
    if (kind == "image") {
        return text + text;
    }
    if (kind == "key") {
        return text + "F" + text;
    }
    return "undefined";
}

std::string variableInitial(const VariableSpec& spec, char quote)
{
    const std::string_view given = trimmed(spec.value);
    const std::string base = given.empty()
                               ? variableDefault(spec.kind, spec.type, spec.container, spec.keyKind, quote)
                               : std::string(given);
    if (!spec.keep) {
        return base;
    }
    const std::string annotation = variableAnnotation(spec.kind, spec.type, spec.container, spec.keyKind);
    return "(store.get(" + std::string(1, quote) + spec.name + std::string(1, quote) + ") as " + annotation + ") ?? "
         + base;
}

std::string variableDocComment(const VariableSpec& spec)
{
    std::string body = std::string(trimmed(spec.description));
    const auto append = [&body](std::string_view tag) {
        if (!body.empty()) {
            body += " ";
        }
        body += tag;
    };
    const std::string category(trimmed(spec.category));
    if (!category.empty()) {
        append("@category " + category);
    }
    if (spec.kind == "decimal") {
        append("@decimal");
    }
    if (spec.kind == "duration") {
        append("@duration");
    }
    if (spec.keep) {
        append("@keep");
    }
    if (spec.editable) {
        append("@editable");
    }
    return body.empty() ? std::string() : "/** " + body + " */";
}

std::string variableDeclaration(const VariableSpec& spec, char quote, bool semicolons)
{
    const std::string annotation = variableAnnotation(spec.kind, spec.type, spec.container, spec.keyKind);
    const std::string value = spec.editable ? "this.props." + spec.name : variableInitial(spec, quote);
    std::string keyword;
    if (spec.scope != "class") {
        keyword = spec.constant ? "const " : "let ";
    }
    std::string text;
    const std::string doc = variableDocComment(spec);
    if (!doc.empty()) {
        text = doc + "\n";
    }
    text += keyword + spec.name + ": " + annotation + " = " + value + (semicolons ? ";" : "");
    return text;
}

std::string_view variableElement(std::string_view annotation)
{
    const std::string_view text = trimmed(annotation);
    if (text.ends_with("[]")) {
        return trimmed(text.substr(0, text.size() - 2));
    }
    if (text.starts_with("Map<") && text.ends_with(">")) {
        const std::string_view inner = text.substr(4, text.size() - 5);
        int depth = 0;
        std::size_t at = 0;
        for (; at < inner.size(); ++at) {
            const char c = inner[at];
            if (c == '<' || c == '(' || c == '[' || c == '{') {
                ++depth;
            } else if (c == '>' || c == ')' || c == ']' || c == '}') {
                --depth;
            } else if (c == ',' && depth == 0) {
                break;
            }
        }
        return trimmed(inner.substr(std::min(at + 1, inner.size())));
    }
    if (text.starts_with("Set<") && text.ends_with(">")) {
        return trimmed(text.substr(4, text.size() - 5));
    }
    if (text.starts_with("Array<") && text.ends_with(">")) {
        return trimmed(text.substr(6, text.size() - 7));
    }
    if (text.starts_with("ReadonlyArray<") && text.ends_with(">")) {
        return trimmed(text.substr(14, text.size() - 15));
    }
    return text;
}

std::string variableKindOfName(std::string_view name)
{
    const std::string_view text = trimmed(name);
    if (text == "boolean") {
        return "toggle";
    }
    if (text == "number") {
        return "integer";
    }
    if (text == "string") {
        return "text";
    }
    if (text == "PicturePoint" || text == "Found") {
        return "point";
    }
    if (text == "Region" || text == "RegionName") {
        return "region";
    }
    if (text == "HexColor" || text == "ColorName") {
        return "color";
    }
    if (text == "ImagePath" || text == "TemplateName") {
        return "image";
    }
    if (text == "InputName") {
        return "key";
    }
    if (text == "Component") {
        return "component";
    }
    if (text.empty()) {
        return "text";
    }
    return "class";
}

bool variableEditableKind(std::string_view kind)
{
    return kind == "toggle" || kind == "integer" || kind == "decimal" || kind == "duration" || kind == "text"
        || kind == "point" || kind == "key" || kind == "component";
}

std::string variablePropKind(std::string_view kind)
{
    if (kind == "toggle") {
        return "toggle";
    }
    if (kind == "integer" || kind == "decimal" || kind == "duration") {
        return "number";
    }
    if (kind == "text") {
        return "text";
    }
    if (kind == "point") {
        return "position";
    }
    if (kind == "key") {
        return "key";
    }
    if (kind == "component") {
        return "component";
    }
    return {};
}

VariableDoc parseVariableDoc(std::string_view text, std::span<const syntax::Comment> comments, std::uint32_t start)
{
    VariableDoc doc;
    const syntax::Comment* found = nullptr;
    for (const syntax::Comment& comment : comments) {
        if (comment.end > start) {
            break;
        }
        if (!comment.block) {
            continue;
        }
        const std::string_view between = text.substr(comment.end, start - comment.end);
        if (between.find_first_not_of(" \t\r\n") != std::string_view::npos) {
            continue;
        }
        found = &comment;
    }
    if (found == nullptr || found->end > text.size()) {
        return doc;
    }
    const std::string_view raw = text.substr(found->start, found->end - found->start);
    if (!raw.starts_with("/**")) {
        return doc;
    }
    std::string joined;
    std::string_view inner = raw.substr(3);
    if (inner.ends_with("*/")) {
        inner = inner.substr(0, inner.size() - 2);
    }
    std::size_t at = 0;
    while (at <= inner.size()) {
        std::size_t end = inner.find('\n', at);
        if (end == std::string_view::npos) {
            end = inner.size();
        }
        std::string_view line = inner.substr(at, end - at);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        line = trimmed(line);
        if (line.starts_with("*")) {
            line = trimmed(line.substr(1));
        }
        if (!line.empty()) {
            if (!joined.empty()) {
                joined += " ";
            }
            joined += line;
        }
        at = end + 1;
    }
    std::size_t tagStart = joined.size();
    for (std::size_t i = 0; i < joined.size(); ++i) {
        if (joined[i] == '@' && isTagName(tagAt(joined, i + 1))) {
            tagStart = i;
            break;
        }
    }
    doc.description = std::string(trimmed(std::string_view(joined).substr(0, tagStart)));
    std::size_t i = tagStart;
    while (i < joined.size()) {
        if (joined[i] != '@') {
            ++i;
            continue;
        }
        const std::string_view tag = tagAt(joined, i + 1);
        if (!isTagName(tag)) {
            ++i;
            continue;
        }
        std::size_t end = joined.size();
        for (std::size_t j = i + 1 + tag.size(); j < joined.size(); ++j) {
            if (joined[j] == '@' && isTagName(tagAt(joined, j + 1))) {
                end = j;
                break;
            }
        }
        const std::string_view rest = std::string_view(joined).substr(i + 1 + tag.size(), end - i - 1 - tag.size());
        if (tag == "category") {
            doc.category = std::string(trimmed(rest));
        } else if (tag == "decimal") {
            doc.decimal = true;
        } else if (tag == "duration") {
            doc.duration = true;
        } else if (tag == "keep") {
            doc.keep = true;
        } else if (tag == "editable") {
            doc.editable = true;
        }
        i = end;
    }
    return doc;
}

}
