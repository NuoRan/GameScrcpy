#include "Model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace gs::blueprint {

using syntax::Kind;
using syntax::Node;

std::string_view pinTypeName(PinType type) noexcept
{
    switch (type) {
    case PinType::Exec:
        return "exec";
    case PinType::Bool:
        return "bool";
    case PinType::Number:
        return "number";
    case PinType::Text:
        return "text";
    case PinType::Point:
        return "point";
    case PinType::Region:
        return "region";
    case PinType::Image:
        return "image";
    case PinType::Color:
        return "color";
    case PinType::Component:
        return "component";
    case PinType::Key:
        return "key";
    case PinType::Duration:
        return "duration";
    case PinType::List:
        return "list";
    case PinType::Transition:
        return "transition";
    case PinType::Struct:
        return "struct";
    case PinType::Enum:
        return "enum";
    case PinType::Object:
        return "object";
    case PinType::Any:
        break;
    }
    return "any";
}

PinType typeOfName(const Document::Impl& d, std::string_view type)
{
    if (type == "number") {
        return PinType::Number;
    }
    if (type == "boolean") {
        return PinType::Bool;
    }
    if (type == "string") {
        return PinType::Text;
    }
    if (type == "InputName") {
        return PinType::Key;
    }
    if (type == "PicturePoint" || type == "Point") {
        return PinType::Point;
    }
    if (type == "Region" || type == "RegionName") {
        return PinType::Region;
    }
    if (type == "TemplateName" || type == "ImagePath") {
        return PinType::Image;
    }
    if (type == "ColorName" || type == "HexColor" || type == "Rgb") {
        return PinType::Color;
    }
    if (type.ends_with("[]")) {
        return PinType::List;
    }
    if (structNamed(d.types, type) != nullptr) {
        return PinType::Struct;
    }
    if (enumNamed(d.types, type) != nullptr) {
        return PinType::Enum;
    }
    if (std::ranges::find(d.classes, type, &ClassInfo::name) != d.classes.end()) {
        return PinType::Object;
    }
    return PinType::Any;
}

std::string defaultValueOfType(std::string_view type)
{
    if (type == "number") {
        return "0";
    }
    if (type == "boolean") {
        return "false";
    }
    if (type == "string") {
        return "\"\"";
    }
    if (type == "InputName") {
        return "\"F\"";
    }
    if (type == "PicturePoint" || type == "Point") {
        return "{ x: 0.5, y: 0.5 }";
    }
    if (type == "Region" || type == "RegionName") {
        return "{ x: 0, y: 0, width: 1, height: 1 }";
    }
    if (type == "ColorName" || type == "HexColor" || type == "Rgb") {
        return "\"#FFFFFF\"";
    }
    if (type.ends_with("[]")) {
        return "[]";
    }
    return "undefined";
}

nlohmann::json declaredDefault(std::string_view type, std::string_view name)
{
    struct Known {
        std::string_view type;
        std::string_view name;
        double number = 0;
        std::string_view text;
    };
    static constexpr Known kDefaults[] = {
        {"UntilOptions", "timeoutMs", 5000, {}},     {"UntilOptions", "retry", 0, {}},
        {"FindOptions", "max", 16, {}},              {"FindOptions", "timeoutMs", 2000, {}},
        {"WaitOptions", "max", 16, {}},              {"WaitOptions", "timeoutMs", 10000, {}},
        {"WaitOptions", "intervalMs", 250, {}},      {"WatchOptions", "max", 16, {}},
        {"WatchOptions", "timeoutMs", 2000, {}},     {"WatchOptions", "intervalMs", 500, {}},
        {"ColorOptions", "timeoutMs", 2000, {}},     {"PixelOptions", "timeoutMs", 2000, {}},
        {"ChangeOptions", "threshold", 0.05, {}},    {"ChangeOptions", "timeoutMs", 2000, {}},
        {"OnChangeOptions", "threshold", 0.05, {}},  {"OnChangeOptions", "intervalMs", 500, {}},
        {"ReadTextOptions", "timeoutMs", 2000, {}},  {"PixelMatchOptions", "timeoutMs", 2000, {}},
        {"BoxOptions", "color", 0, "accent"},        {"TextOptions", "color", 0, "accent"},
        {"MarkOptions", "color", 0, "accent"},
    };
    for (const Known& known : kDefaults) {
        if (known.type == type && known.name == name) {
            return known.text.empty() ? nlohmann::json(known.number) : nlohmann::json(std::string(known.text));
        }
    }
    return nullptr;
}

std::string fill(std::string text, std::string_view first, std::string_view second)
{
    if (const std::size_t at = text.find("%1"); at != std::string::npos) {
        text.replace(at, 2, first);
    }
    if (const std::size_t at = text.find("%2"); at != std::string::npos) {
        text.replace(at, 2, second);
    }
    return text;
}

std::string numberText(double value)
{
    if (!std::isfinite(value)) {
        return "0";
    }
    if (std::floor(value) == value && std::fabs(value) < 1e15) {
        return std::to_string(static_cast<long long>(value));
    }
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.6g", value);
    std::string text = buffer;
    if (text.find('e') == std::string::npos && text.find('.') != std::string::npos) {
        while (text.back() == '0') {
            text.pop_back();
        }
        if (text.back() == '.') {
            text.pop_back();
        }
    }
    return text;
}

std::string percentText(double ratio)
{
    return numberText(std::round(ratio * 1000.0) / 10.0) + "%";
}

const std::string& Say::optional(std::string_view key) const
{
    static const std::string none;
    if (m_labels == nullptr) {
        return none;
    }
    if (const auto found = m_optional.find(key); found != m_optional.end()) {
        return found->second;
    }
    std::string text = m_labels->text(key);
    if (text.empty()) {
        text = m_labels->english(key);
    }
    return m_optional.emplace(std::string(key), std::move(text)).first->second;
}

const std::string& Say::operator()(std::string_view key) const
{
    const std::string& text = optional(key);
    if (!text.empty()) {
        return text;
    }
    if (const auto found = m_shown.find(key); found != m_shown.end()) {
        return found->second;
    }
    const std::size_t colon = key.find(':');
    return m_shown.emplace(std::string(key), std::string(colon == std::string_view::npos ? key : key.substr(colon + 1)))
        .first->second;
}

std::string Say::english(std::string_view key) const
{
    return m_labels != nullptr ? m_labels->english(key) : std::string();
}

std::string Say::format(std::string_view key, std::string_view first, std::string_view second) const
{
    std::string text = optional(key);
    if (text.empty()) {
        text = second.empty() ? "%1" : "%1 %2";
    }
    return fill(std::move(text), first, second);
}

std::string Say::key(std::string_view name) const
{
    if (m_labels != nullptr) {
        if (const auto found = m_keys.find(name); found != m_keys.end()) {
            return found->second;
        }
        std::string text = m_labels->keyName(name);
        if (!text.empty()) {
            return m_keys.emplace(std::string(name), std::move(text)).first->second;
        }
    }
    return std::string(name);
}

std::string Say::seconds(double milliseconds) const
{
    return format("unit:seconds", numberText(milliseconds / 1000.0));
}

const std::string& signatureOf(const Item& item)
{
    if (item.signature.empty()) {
        std::string text = item.token;
        if (item.source != nullptr && item.sigEnd > item.sigStart && item.sigEnd <= item.source->text.size()) {
            text += "|";
            text += compact(std::string_view(item.source->text).substr(item.sigStart, item.sigEnd - item.sigStart));
        }
        item.signature = std::move(text);
    }
    return item.signature.str();
}

std::string positionOf(const Item& item)
{
    if (item.source == nullptr || !item.source->id.empty() || item.statement == nullptr || item.source->lines == nullptr) {
        return {};
    }
    return item.source->lines->position(item.statement->start);
}

std::span<const Choice> Document::Impl::keep(std::span<const Choice> list)
{
    if (list.empty()) {
        return {};
    }
    return choiceLists.emplace_back(list.begin(), list.end());
}

std::span<const Choice> Document::Impl::shared(PinType type)
{
    if (!choicesReady) {
        choicesReady = true;
        std::vector<Choice> images;
        for (const ImageEntry& image : environment.images) {
            images.push_back({image.name, image.name});
        }
        imageChoices = keep(std::move(images));
        std::vector<Choice> controls;
        for (const ControlEntry& control : environment.controls) {
            controls.push_back({control.id, control.name.empty() ? control.id : control.name});
        }
        controlChoices = keep(std::move(controls));
        const auto named = [this](const std::vector<std::string>& names) {
            std::vector<Choice> made;
            for (const std::string& name : names) {
                made.push_back({name, name});
            }
            return keep(std::move(made));
        };
        regionChoices = named(environment.regions);
        pointChoices = named(environment.points);
    }
    switch (type) {
    case PinType::Image:
        return imageChoices;
    case PinType::Component:
        return controlChoices;
    case PinType::Region:
        return regionChoices;
    case PinType::Point:
        return pointChoices;
    default:
        return {};
    }
}

bool floatingHead(const Item& item) noexcept
{
    if (item.source == nullptr || item.source->id.empty() || item.consumer != nullptr) {
        return false;
    }
    if (item.chain == nullptr) {
        return item.role == Role::Pure || item.role == Role::Code;
    }
    return item.chain->shape == Shape::Floating && !item.chain->items.empty() && item.chain->items.front() == &item;
}

bool takesKey(const Document::Impl& d, const Item& item)
{
    constexpr std::string_view kPrefix = "controls.";
    if (item.role != Role::Event || !item.callee.starts_with(kPrefix)) {
        return false;
    }
    const std::string_view rest = std::string_view(item.callee).substr(kPrefix.size());
    const std::size_t dot = rest.rfind('.');
    if (dot == std::string_view::npos) {
        return false;
    }
    const std::string_view event = rest.substr(dot + 1);
    if (event != "onPress" && event != "onRelease" && event != "whileHeld") {
        return false;
    }
    const ControlEntry* control = d.control(rest.substr(0, dot));
    if (control == nullptr) {
        return false;
    }
    constexpr std::string_view kTypes[] = {"Tap", "Hold", "LongPress", "Turbo", "Swipe", "MultiPress"};
    return std::ranges::any_of(kTypes, [control](std::string_view type) { return type == control->type; });
}

bool consumesKey(const Item& item)
{
    if (item.expression == nullptr || item.optionsArgument >= item.expression->items.size()) {
        return true;
    }
    const Node* options = syntax::unparen(item.expression->items[item.optionsArgument]);
    if (options == nullptr || options->kind != Kind::Object) {
        return true;
    }
    for (const Node* entry : options->items) {
        if (entry->kind == Kind::Property && entry->target != nullptr && entry->target->text == "consume") {
            return entry->value == nullptr || syntax::unparen(entry->value)->kind != Kind::False;
        }
    }
    return true;
}

bool settingMember(std::string_view name) noexcept
{
    static constexpr std::string_view kSettings[] = {
        "id",        "name",        "type",          "key",           "inputs",  "props",  "position",
        "hint",      "finger",      "tapMs",         "rateHz",        "holdMs",  "durationMs", "radius",
        "walkScale", "sprintScale", "walkMsPerUnit", "stopOnRelease", "enabled",
    };
    return std::ranges::find(kSettings, name) != std::end(kSettings);
}

[[nodiscard]] bool parallelArrow(const syntax::Node* node)
{
    const Node* function = syntax::unparen(node);
    return function != nullptr && syntax::isFunction(function) && !function->has(syntax::flag::kExpressionBody)
        && function->items.empty() && function->body != nullptr && function->body->kind == Kind::Block;
}

[[nodiscard]] const syntax::Node* parallelFan(const Chain& chain, std::size_t at)
{
    if (chain.source == nullptr || at >= chain.statements.size() || at + 1 != chain.statements.size()) {
        return nullptr;
    }
    const Node* statement = chain.statements[at];
    if (statement == nullptr || statement->kind != Kind::ExpressionStatement) {
        return nullptr;
    }
    const Node* expression = syntax::unparen(statement->target);
    if (expression != nullptr && expression->kind == Kind::Await) {
        expression = syntax::unparen(expression->target);
    }
    if (expression == nullptr || expression->kind != Kind::Call || expression->items.size() < 2) {
        return nullptr;
    }
    const Node* callee = syntax::unparen(expression->target);
    if (callee == nullptr || callee->kind != Kind::Identifier || callee->text != "parallel") {
        return nullptr;
    }
    if (chain.source->scopes != nullptr) {
        const syntax::Binding* binding = chain.source->scopes->lookup(callee->text, callee->start);
        if (binding != nullptr && binding->kind != syntax::BindingKind::Import) {
            return nullptr;
        }
    }
    for (const Node* argument : expression->items) {
        if (!parallelArrow(argument)) {
            return nullptr;
        }
    }
    return expression;
}

std::string compact(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    char quote = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quote != 0) {
            out.push_back(c);
            if (c == '\\' && i + 1 < text.size()) {
                out.push_back(text[++i]);
            } else if (c == quote) {
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'' || c == '`') {
            quote = c;
            out.push_back(c);
            continue;
        }
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != ';') {
            out.push_back(c);
        }
    }
    return out;
}

std::string dedent(std::string_view text)
{
    std::vector<std::string_view> lines;
    std::size_t at = 0;
    while (at <= text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(at, end - at);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        at = end + 1;
    }
    std::size_t least = std::string_view::npos;
    for (const std::string_view line : lines) {
        const std::size_t indent = line.find_first_not_of(" \t");
        if (indent != std::string_view::npos) {
            least = std::min(least, indent);
        }
    }
    if (least == std::string_view::npos) {
        least = 0;
    }
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string_view line = lines[i];
        if (i > 0) {
            out.push_back('\n');
        }
        out.append(line.size() >= least ? line.substr(least) : std::string_view());
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ' || out.back() == '\t')) {
        out.pop_back();
    }
    return out;
}

std::string noteText(std::string_view comment)
{
    std::string_view body = comment;
    if (body.starts_with("//")) {
        body.remove_prefix(2);
        const std::size_t start = body.find_first_not_of(" \t");
        body = start == std::string_view::npos ? std::string_view() : body.substr(start);
        while (!body.empty() && (body.back() == ' ' || body.back() == '\t' || body.back() == '\r')) {
            body.remove_suffix(1);
        }
        return std::string(body);
    }
    if (body.starts_with("/*")) {
        body.remove_prefix(2);
    }
    if (body.ends_with("*/")) {
        body.remove_suffix(2);
    }
    std::string out;
    std::size_t at = 0;
    while (at <= body.size()) {
        std::size_t end = body.find('\n', at);
        if (end == std::string_view::npos) {
            end = body.size();
        }
        std::string_view line = body.substr(at, end - at);
        const std::size_t first = line.find_first_not_of(" \t\r*");
        line = first == std::string_view::npos ? std::string_view() : line.substr(first);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
            line.remove_suffix(1);
        }
        if (!line.empty()) {
            if (!out.empty()) {
                out.push_back('\n');
            }
            out.append(line);
        }
        at = end + 1;
    }
    if (comment.starts_with("/**")) {
        std::size_t cut = out.size();
        for (const std::string_view tag : {"@category", "@decimal", "@duration", "@keep", "@editable"}) {
            for (std::size_t found = out.find(tag); found != std::string::npos; found = out.find(tag, found + 1)) {
                const std::size_t next = found + tag.size();
                const bool startsWord = found == 0 || out[found - 1] == ' ' || out[found - 1] == '\n';
                const bool endsWord = next >= out.size() || out[next] == ' ' || out[next] == '\n';
                if (startsWord && endsWord) {
                    cut = std::min(cut, found);
                    break;
                }
            }
        }
        out.resize(cut);
        while (!out.empty() && (out.back() == ' ' || out.back() == '\n')) {
            out.pop_back();
        }
    }
    return out;
}

std::string unquote(std::string_view literal)
{
    if (literal.size() < 2) {
        return std::string(literal);
    }
    const char quote = literal.front();
    if ((quote != '"' && quote != '\'' && quote != '`') || literal.back() != quote) {
        return std::string(literal);
    }
    std::string out;
    for (std::size_t i = 1; i + 1 < literal.size(); ++i) {
        const char c = literal[i];
        if (c != '\\' || i + 2 >= literal.size()) {
            out.push_back(c);
            continue;
        }
        const char next = literal[++i];
        switch (next) {
        case 'n':
            out.push_back('\n');
            break;
        case 't':
            out.push_back('\t');
            break;
        case 'r':
            out.push_back('\r');
            break;
        default:
            out.push_back(next);
            break;
        }
    }
    return out;
}

bool identifierText(std::string_view text)
{
    if (text.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || c >= 0x80;
        const bool digit = c >= '0' && c <= '9';
        if (!(letter || (digit && i > 0))) {
            return false;
        }
    }
    static constexpr std::string_view kWords[] = {
        "break",  "case",   "catch",  "class",      "const",   "continue", "debugger", "default", "delete",
        "do",     "else",   "enum",   "export",     "extends", "false",    "finally",  "for",     "function",
        "if",     "import", "in",     "instanceof", "new",     "null",     "return",   "super",   "switch",
        "this",   "throw",  "true",   "try",        "typeof",  "var",      "void",     "while",   "with",
        "let",    "static", "yield",  "await",      "undefined",
    };
    return std::ranges::find(kWords, text) == std::end(kWords);
}

std::optional<double> numberValue(const Node* node)
{
    node = syntax::unparen(node);
    if (node == nullptr) {
        return std::nullopt;
    }
    if (node->kind == Kind::Number) {
        std::string digits;
        for (const char c : node->text) {
            if (c != '_' && c != 'n') {
                digits.push_back(c);
            }
        }
        if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
            return static_cast<double>(std::strtoll(digits.c_str() + 2, nullptr, 16));
        }
        return std::strtod(digits.c_str(), nullptr);
    }
    if (node->kind == Kind::Unary && node->text == "-") {
        if (const std::optional<double> inner = numberValue(node->target)) {
            return -*inner;
        }
    }
    return std::nullopt;
}

std::optional<std::string> stringValue(const Node* node)
{
    node = syntax::unparen(node);
    if (node == nullptr) {
        return std::nullopt;
    }
    if (node->kind == Kind::String) {
        return unquote(node->text);
    }
    return std::nullopt;
}

void visit(const Node* node, const std::function<void(const Node*)>& visitor)
{
    if (node == nullptr) {
        return;
    }
    std::vector<const Node*> pending{node};
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current == nullptr) {
            continue;
        }
        visitor(current);
        for (std::size_t i = current->items.size(); i > 0; --i) {
            pending.push_back(current->items[i - 1]);
        }
        pending.push_back(current->other);
        pending.push_back(current->body);
        pending.push_back(current->test);
        if (current->value != current->target) {
            pending.push_back(current->value);
        }
        pending.push_back(current->target);
    }
}

std::string mainTitle(const Say& say, std::string_view fileName)
{
    if (fileName == kLibraryFile) {
        return say("page:library");
    }
    std::string_view name = fileName;
    if (const std::size_t slash = name.find_last_of("/\\"); slash != std::string_view::npos) {
        name.remove_prefix(slash + 1);
    }
    if (name == "logic.ts" || name.empty()) {
        return say("page:main");
    }
    if (name.ends_with(".ts")) {
        name.remove_suffix(3);
    }
    return std::string(name);
}

std::string controlPageTitle(const ControlEntry& control)
{
    const std::string& name = control.name.empty() ? control.id : control.name;
    if (control.key.empty()) {
        return name;
    }
    constexpr std::string_view kDot = " \xC2\xB7 ";
    return name + std::string(kDot) + control.key;
}

void Document::setOpenPage(std::string_view page)
{
    m_impl->layout.open = std::string(page);
}

void detectStyle(Document::Impl& impl)
{
    const Source& file = impl.file();
    const std::string& text = file.text;
    Style& style = impl.style;
    style.lineBreak = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::size_t least = 0;
    bool tabs = false;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::size_t indent = at;
        while (indent < end && (text[indent] == ' ' || text[indent] == '\t')) {
            ++indent;
        }
        if (indent > at && indent < end && text[indent] != '\r' && text[indent] != '*') {
            if (text[at] == '\t') {
                tabs = true;
                break;
            }
            const std::size_t width = indent - at;
            least = least == 0 ? width : std::min(least, width);
        }
        at = end + 1;
    }
    if (tabs) {
        style.indent = "\t";
    } else if (least >= 2) {
        style.indent = std::string(std::min<std::size_t>(least, 8), ' ');
    } else {
        style.indent = "  ";
    }
    int withSemicolon = 0;
    int withoutSemicolon = 0;
    int doubles = 0;
    int singles = 0;
    visit(file.tree.program, [&](const Node* node) {
        switch (node->kind) {
        case Kind::ExpressionStatement:
        case Kind::VarDecl:
        case Kind::Return:
        case Kind::Break:
        case Kind::Continue:
        case Kind::Throw:
            if (node->end > node->start && node->end <= text.size()) {
                (text[node->end - 1] == ';' ? withSemicolon : withoutSemicolon) += 1;
            }
            break;
        case Kind::String:
            if (!node->text.empty()) {
                (node->text.front() == '\'' ? singles : doubles) += 1;
            }
            break;
        default:
            break;
        }
    });
    style.semicolons = withSemicolon >= withoutSemicolon;
    style.quote = singles > doubles ? '\'' : '"';
}

Page* Document::Impl::page(std::string_view key)
{
    const auto found = std::ranges::find(pages, key, &Page::key);
    return found == pages.end() ? nullptr : &*found;
}

const Page* Document::Impl::page(std::string_view key) const
{
    const auto found = std::ranges::find(pages, key, &Page::key);
    return found == pages.end() ? nullptr : &*found;
}

Item* Document::Impl::item(std::string_view key) const
{
    const auto found = byKey.find(key);
    return found == byKey.end() ? nullptr : found->second;
}

PageLayout* Document::Impl::pageLayout(const Page& target)
{
    return &layout.pages[target.anchor];
}

std::string Document::Impl::text(const Source& source, const Node* node) const
{
    return node == nullptr ? std::string() : text(source, node->start, node->end);
}

std::string Document::Impl::text(const Source& source, std::uint32_t start, std::uint32_t end) const
{
    if (start >= source.text.size() || end <= start) {
        return {};
    }
    return source.text.substr(start, std::min<std::size_t>(end, source.text.size()) - start);
}

Size Document::Impl::sizeOf(const Item& item) const
{
    if (const auto found = sizes.find(item.key); found != sizes.end() && found->second.width > 0) {
        return found->second;
    }
    const auto shown = [](const std::vector<Pin>& pins) {
        return static_cast<double>(std::ranges::count_if(pins, [](const Pin& pin) {
            return !pin.optional || pin.connected || !pin.value.empty();
        }));
    };
    if (item.kind == "pure") {
        return {150, 28 + 24 * shown(item.inputs)};
    }
    if (item.kind == "reroute") {
        return {10, 10};
    }
    return {176, 30 + 24 * std::max(shown(item.inputs), shown(item.outputs)) + 14};
}

const ControlEntry* Document::Impl::control(std::string_view id) const
{
    const auto found = std::ranges::find(environment.controls, id, &ControlEntry::id);
    return found == environment.controls.end() ? nullptr : &*found;
}

const VariableEntry* Document::Impl::variable(std::string_view id) const
{
    const auto found = std::ranges::find(environment.vars, id, &VariableEntry::id);
    return found == environment.vars.end() ? nullptr : &*found;
}

const VariableInfo* Document::Impl::declaredVariable(std::string_view page, std::string_view name) const
{
    const auto found = std::ranges::find_if(variables, [page, name](const VariableInfo& info) {
        return info.page == page && info.name == name;
    });
    return found == variables.end() ? nullptr : &*found;
}

const ClassInfo* Document::Impl::classNamed(std::string_view name) const
{
    const auto found = std::ranges::find(classes, name, &ClassInfo::name);
    return found == classes.end() ? nullptr : &*found;
}

const ClassInfo* Document::Impl::defaultClass() const
{
    const auto found = std::ranges::find_if(classes, [](const ClassInfo& info) { return info.exported; });
    return found == classes.end() ? nullptr : &*found;
}

}
