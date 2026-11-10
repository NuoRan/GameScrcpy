#include "Model.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace gs::blueprint {

using Ordered = nlohmann::ordered_json;
using syntax::Kind;
using syntax::Node;
using syntax::Kind;
using syntax::Node;

namespace {

constexpr std::string_view kDot = " \xC2\xB7 ";

constexpr std::string_view kGroups[] = {"events", "components", "touch", "vision", "flow",      "wait",       "vars",
                                        "types",  "device",     "ui",    "mapping", "math",      "text",       "list",
                                        "dict",   "set",        "log",   "functions", "extensions", "other"};

enum class EntryKind : std::uint8_t { Statement, Event, Pure };

struct Entry {
    std::string id;
    std::string title;
    std::string english;
    std::string subtitle;
    std::string group;
    std::string tone;
    std::string icon;
    std::string keywords;
    EntryKind kind = EntryKind::Statement;
    std::vector<PinType> inputs;
    std::optional<PinType> output;
    std::string control;
    std::string memberKind;
    std::string groupTitle;
    std::string api;
};

std::size_t groupIndex(std::string_view group)
{
    for (std::size_t i = 0; i < std::size(kGroups); ++i) {
        if (kGroups[i] == group) {
            return i;
        }
    }
    return std::size(kGroups);
}

std::string groupOf(std::string_view owner, std::string_view name)
{
    if (owner.starts_with("ext.") || owner.starts_with("ext[")) {
        return "extensions";
    }
    if (owner == "touch") {
        return "touch";
    }
    if (owner == "vision" || owner == "res") {
        return "vision";
    }
    if (owner == "device" || owner == "input") {
        return "device";
    }
    if (owner == "ui" || owner == "hints") {
        return "ui";
    }
    if (owner == "mapping" || owner == "profile") {
        return "mapping";
    }
    if (owner == "math") {
        return "math";
    }
    if (owner == "text" || owner == "list" || owner == "dict" || owner == "set") {
        return std::string(owner);
    }
    if (owner == "log") {
        return "log";
    }
    if (owner == "store") {
        return "vars";
    }
    if (owner.empty()) {
        if (name == "sleep" || name == "nextFrame" || name == "waitUntil" || name == "withTimeout" || name == "retry") {
            return "wait";
        }
        if (name == "every" || name == "after") {
            return "events";
        }
        if (name == "point") {
            return "math";
        }
        return "flow";
    }
    return "other";
}

std::string lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool compatible(PinType from, PinType to)
{
    if (from == to || from == PinType::Any || to == PinType::Any) {
        return true;
    }
    if (to == PinType::Text && (from == PinType::Number || from == PinType::Bool || from == PinType::Duration)) {
        return true;
    }
    if (to == PinType::Number && (from == PinType::Text || from == PinType::Duration)) {
        return true;
    }
    if (to == PinType::Duration && from == PinType::Number) {
        return true;
    }
    return to == PinType::Bool;
}

bool skippedMember(std::string_view name)
{
    static constexpr std::string_view kSkipped[] = {"setup", "event", "bind", "props", "inputs", "hint", "finger", "id", "type"};
    return std::ranges::find(kSkipped, name) != std::end(kSkipped);
}

struct HiddenBlock {
    std::string_view shape;
    std::string_view label;
};

constexpr HiddenBlock kHiddenBlocks[] = {
    {"*", "Component.child"},
    {"*", "Component.enable"},
    {"*", "Component.disable"},
    {"*", "Component.toggle"},
    {"*", "Component.touching"},
    {"Tap", "Component.trigger"},
    {"Hold", "Component.hold"},
    {"Hold", "Hold.onPressed"},
    {"Hold", "Hold.onReleased"},
    {"LongPress", "Component.hold"},
    {"LongPress", "Component.trigger"},
    {"LongPress", "LongPress.onStart"},
    {"LongPress", "LongPress.onEnd"},
    {"Turbo", "Component.hold"},
    {"Turbo", "Component.trigger"},
    {"Swipe", "Component.hold"},
    {"Swipe", "Component.trigger"},
    {"Swipe", "Component.press"},
    {"Swipe", "Component.release"},
    {"Swipe", "Swipe.onStart"},
    {"Swipe", "Swipe.onEnd"},
    {"Pinch", "Component.hold"},
    {"Pinch", "Component.trigger"},
    {"Pinch", "Component.press"},
    {"Pinch", "Component.release"},
    {"Wasd", "Component.hold"},
    {"Wasd", "Component.trigger"},
    {"Wasd", "Component.press"},
    {"PadStick", "Component.hold"},
    {"PadStick", "Component.trigger"},
    {"PadStick", "Component.press"},
    {"PadStick", "Component.active"},
    {"PadStick", "Component.pressedAt"},
    {"PadStick", "Component.heldMs"},
    {"PadStick", "Component.onPress"},
    {"PadStick", "Component.onRelease"},
    {"PadStick", "Component.whileHeld"},
    {"RightWalk", "Component.hold"},
    {"RightWalk", "Component.trigger"},
    {"RightWalk", "Component.press"},
    {"RightWalk", "Component.release"},
    {"Camera", "Component.hold"},
    {"Camera", "Component.trigger"},
    {"Camera", "Component.press"},
    {"Camera", "Component.release"},
    {"Camera", "Component.active"},
    {"Camera", "Component.key"},
    {"Camera", "Component.pressedAt"},
    {"Camera", "Component.heldMs"},
    {"Camera", "Component.onPress"},
    {"Camera", "Component.onRelease"},
    {"Camera", "Component.whileHeld"},
    {"Camera", "Component.onTouchStart"},
    {"Camera", "Component.onTouchEnd"},
    {"PadCamera", "Component.hold"},
    {"PadCamera", "Component.trigger"},
    {"PadCamera", "Component.press"},
    {"PadCamera", "Component.release"},
    {"PadCamera", "Component.active"},
    {"PadCamera", "Component.key"},
    {"PadCamera", "Component.pressedAt"},
    {"PadCamera", "Component.heldMs"},
    {"PadCamera", "Component.onPress"},
    {"PadCamera", "Component.onRelease"},
    {"PadCamera", "Component.whileHeld"},
    {"PadCamera", "Component.onTouchStart"},
    {"PadCamera", "Component.onTouchEnd"},
    {"FreeLook", "Component.hold"},
    {"FreeLook", "Component.trigger"},
    {"FreeLook", "Component.press"},
    {"FreeLook", "Component.release"},
    {"FreeLook", "FreeLook.onStart"},
    {"FreeLook", "FreeLook.onEnd"},
    {"Skill", "Component.hold"},
    {"Skill", "Component.trigger"},
    {"Skill", "Component.press"},
    {"Skill", "Component.release"},
    {"Dial", "Component.hold"},
    {"Dial", "Component.trigger"},
    {"Dial", "Component.press"},
    {"Dial", "Component.release"},
    {"Dial", "Component.onTouchStart"},
    {"Dial", "Component.onTouchEnd"},
    {"MultiPress", "Component.hold"},
    {"MultiPress", "MultiPress.onPressed"},
    {"MultiPress", "MultiPress.onReleased"},
    {"TempCursor", "Component.trigger"},
    {"TempCursor", "Component.onTouchStart"},
    {"TempCursor", "Component.onTouchEnd"},
    {"ActionWheel", "Component.hold"},
    {"ActionWheel", "Component.trigger"},
    {"ActionWheel", "Component.press"},
    {"ActionWheel", "Component.release"},
    {"ActionWheel", "Component.onTouchStart"},
    {"ActionWheel", "Component.onTouchEnd"},
};

bool valueSpace(std::string_view owner) noexcept
{
    return owner == "list" || owner == "dict" || owner == "set" || owner == "text";
}

class Catalog
{
public:
    Catalog(const Document::Impl& impl, std::string_view page) : d(impl), api(impl.api()), m_page(page) {}

    std::vector<Entry> entries;

    void build(const Item* from)
    {
        flow();
        operators();
        globals();
        spaces();
        keyEvents();
        controls();
        vars();
        declared(from);
        types();
        functions();
        library();
        if (from != nullptr) {
            locals(*from);
        }
        own();
    }

private:
    const Document::Impl& d;
    const Api& api;
    std::string_view m_page;

    [[nodiscard]] PinType typeOf(const TypeRef& type, std::string_view name) const
    {
        TypeRef current = withoutEmpty(type);
        for (int depth = 0; depth < 8 && current != nullptr; ++depth) {
            if (current->kind == TypeNode::Kind::Name) {
                const std::string& n = current->name;
                if (n == "number") {
                    return name.ends_with("Ms") || name == "ms" ? PinType::Duration : PinType::Number;
                }
                if (n == "boolean") {
                    return PinType::Bool;
                }
                if (n == "string") {
                    return PinType::Text;
                }
                if (n == "InputName") {
                    return PinType::Key;
                }
                if (n == "PicturePoint" || n == "Found") {
                    return PinType::Point;
                }
                if (n == "Region" || n == "RegionName") {
                    return PinType::Region;
                }
                if (n == "TemplateName" || n == "ImagePath") {
                    return PinType::Image;
                }
                if (n == "ColorName" || n == "HexColor") {
                    return PinType::Color;
                }
                if (api.interfaces.contains(n) && api.derives(n, "Component")) {
                    return PinType::Component;
                }
                const TypeRef resolved = api.resolve(current);
                if (resolved == current) {
                    return PinType::Any;
                }
                current = withoutEmpty(resolved);
                continue;
            }
            if (current->kind == TypeNode::Kind::Union) {
                for (const TypeRef& item : current->items) {
                    if (isName(item, "PicturePoint")) {
                        return PinType::Point;
                    }
                    if (isName(item, "TemplateName") || isName(item, "ImagePath")) {
                        return PinType::Image;
                    }
                    if (isName(item, "RegionName") || isName(item, "Region")) {
                        return PinType::Region;
                    }
                    if (isName(item, "ColorName") || isName(item, "HexColor")) {
                        return PinType::Color;
                    }
                }
                const bool literals = std::ranges::all_of(current->items, [](const TypeRef& item) {
                    return item != nullptr && item->kind == TypeNode::Kind::Literal;
                });
                return literals ? PinType::Text : PinType::Any;
            }
            if (current->kind == TypeNode::Kind::Array || current->kind == TypeNode::Kind::Tuple) {
                return PinType::List;
            }
            if (current->kind == TypeNode::Kind::Object) {
                const auto has = [&current](std::string_view field) {
                    return std::ranges::any_of(current->props, [field](const TypeProp& prop) { return prop.name == field; });
                };
                if (has("x") && has("y")) {
                    return has("width") ? PinType::Region : PinType::Point;
                }
            }
            return PinType::Any;
        }
        return PinType::Any;
    }

    [[nodiscard]] std::vector<PinType> inputsOf(const ApiMember& member) const
    {
        std::vector<PinType> types;
        for (const TypeParam& param : member.params) {
            if (isFunctionType(api, param.type) || param.name == "options") {
                continue;
            }
            types.push_back(typeOf(param.type, param.name));
        }
        return types;
    }

    [[nodiscard]] std::optional<PinType> outputOf(const ApiMember& member) const
    {
        const TypeRef returns = withoutEmpty(awaited(member.returns));
        if (returns == nullptr || isName(returns, "void") || isName(member.returns, "Subscription")) {
            return std::nullopt;
        }
        return typeOf(member.returns != nullptr && awaitable(member.returns) ? awaited(member.returns) : member.returns, "");
    }

    [[nodiscard]] std::string label(std::string_view key, std::string_view fallback) const
    {
        std::string text = d.say.optional(key);
        return text.empty() ? std::string(fallback) : text;
    }

    Entry& add(std::string id, std::string title, std::string english, std::string subtitle, std::string group,
               std::string tone, std::string icon, EntryKind kind)
    {
        Entry& entry = entries.emplace_back();
        entry.id = std::move(id);
        entry.title = std::move(title);
        entry.english = std::move(english);
        entry.subtitle = std::move(subtitle);
        entry.group = std::move(group);
        entry.tone = std::move(tone);
        entry.icon = std::move(icon);
        entry.kind = kind;
        entry.keywords = entry.subtitle + " " + entry.english;
        return entry;
    }

    void flow()
    {
        struct Flow {
            std::string_view id;
            std::string_view title;
            std::string_view icon;
            std::vector<PinType> inputs;
        };
        const Flow flows[] = {
            {"flow:if", "node:if", "workflow", {PinType::Bool}},
            {"flow:switch", "node:switch", "workflow", {PinType::Any}},
            {"flow:sequence", "node:sequence", "layers", {}},
            {"flow:multiGate",
             "node:multiGate",
             "dial-cells",
             {PinType::Text, PinType::Number, PinType::Bool, PinType::Bool}},
            {"flow:forOf", "node:forOf", "refresh-cw", {PinType::List}},
            {"flow:forEachIndex", "node:forEachIndex", "refresh-cw", {PinType::List}},
            {"flow:repeat", "node:repeat", "refresh-cw", {PinType::Number}},
            {"flow:forRange", "node:forRange", "refresh-cw", {PinType::Number, PinType::Number}},
            {"flow:while", "node:while", "rotate-cw", {PinType::Bool}},
            {"flow:break", "node:break", "x", {}},
            {"flow:continue", "node:continue", "chevron-right", {}},
            {"flow:return", "node:return", "arrow-left", {PinType::Any}},
            {"flow:machine", "node:machine", "workflow", {}},
            {"flow:code", "node:code", "code-xml", {}},
        };
        for (const Flow& item : flows) {
            Entry& entry = add(std::string(item.id), d.say(item.title), d.say.english(item.title), std::string(item.id.substr(5)),
                               "flow", "flow", std::string(item.icon), EntryKind::Statement);
            entry.inputs = item.inputs;
            if (item.id == "flow:code") {
                entry.tone = "code";
            }
        }
    }

    void operators()
    {
        struct Operator {
            std::string_view op;
            PinType input;
            PinType output;
        };
        constexpr Operator ops[] = {
            {"+", PinType::Number, PinType::Number},  {"-", PinType::Number, PinType::Number},
            {"*", PinType::Number, PinType::Number},  {"/", PinType::Number, PinType::Number},
            {"%", PinType::Number, PinType::Number},  {"===", PinType::Any, PinType::Bool},
            {"!==", PinType::Any, PinType::Bool},     {"<", PinType::Number, PinType::Bool},
            {"<=", PinType::Number, PinType::Bool},   {">", PinType::Number, PinType::Bool},
            {">=", PinType::Number, PinType::Bool},   {"&&", PinType::Bool, PinType::Bool},
            {"||", PinType::Bool, PinType::Bool},     {"!", PinType::Bool, PinType::Bool},
            {"??", PinType::Any, PinType::Any},       {"?:", PinType::Any, PinType::Any},
        };
        for (const Operator& item : ops) {
            const std::string key = "op:" + std::string(item.op);
            Entry& entry = add(key, d.say(key), d.say.english(key), std::string(item.op), "math", "pure", "", EntryKind::Pure);
            entry.inputs = {item.input};
            entry.output = item.output;
        }
    }

    void member(const ApiMember& member, std::string group)
    {
        const std::string title = label(member.label, member.label);
        const std::string english = d.say.english(member.label);
        if (isEventMember(api, member)) {
            Entry& entry = add("event:" + member.label, title, english, member.label, "events", "event",
                               member.owner.empty() ? "timer" : "zap", EntryKind::Event);
            entry.inputs = inputsOf(member);
            entry.api = member.label;
            return;
        }
        const std::optional<PinType> output = outputOf(member);
        const bool pure = output && !awaitable(member.returns)
                       && (!isName(member.returns, "boolean") || valueSpace(member.owner) || member.label == "doN");
        Entry& entry = add((pure ? "pure:" : "call:") + member.label, title, english, member.label, std::move(group),
                           pure ? "pure" : (member.owner == "vision" ? "vision" : "action"), "zap",
                           pure ? EntryKind::Pure : EntryKind::Statement);
        entry.inputs = inputsOf(member);
        entry.output = output;
        entry.api = member.label;
        if (member.label == "sleep") {
            entry.title = d.say("node:wait");
            entry.tone = "wait";
            entry.icon = "timer";
        } else if (member.label == "vision.find" || member.label == "vision.waitFor") {
            entry.title = d.say(member.label == "vision.find" ? "node:find" : "node:waitFor");
            entry.icon = "search";
        } else if (entry.group == "extensions") {
            entry.title = member.name;
            entry.groupTitle = extensionTitle(member.owner);
            entry.icon = "plug";
            entry.keywords += " " + entry.groupTitle + " ext extension";
        }
    }

    [[nodiscard]] std::string extensionTitle(std::string_view owner) const
    {
        for (const ExtensionEntry& extension : d.environment.extensions) {
            if (extension.owner == owner) {
                return extension.name;
            }
        }
        return std::string(owner);
    }

    void globals()
    {
        for (const ApiMember& global : api.globals) {
            if (global.name == "sequence" || global.name == "multiGate") {
                continue;
            }
            member(global, groupOf("", global.name));
        }
    }

    void spaces()
    {
        for (const ApiNamespace& space : api.namespaces) {
            if (space.name == "prop" || space.name == "param") {
                continue;
            }
            for (const ApiMember& function : space.functions) {
                member(function, groupOf(space.name, function.name));
            }
        }
    }

    void keyEvents()
    {
        for (const ApiMember* key : api.members("Key")) {
            if (!isEventMember(api, *key)) {
                continue;
            }
            const std::string title = d.say("node:keyEvent") + std::string(kDot) + d.say("badge:" + key->label);
            Entry& entry = add("event:keys." + key->name, fill(title, numberText(0.5)), d.say.english("badge:" + key->label),
                               "keys." + key->name, "events", "event", "keyboard", EntryKind::Event);
            entry.keywords += " key keys " + key->name;
        }
    }

    void controls()
    {
        for (const ControlEntry& control : d.environment.controls) {
            const std::string shape = api.interfaces.contains(control.type) ? control.type : "Component";
            const std::string name = control.name.empty() ? control.id : control.name;
            for (const ApiMember* member : api.members(shape)) {
                if (skippedMember(member->name) || member->construct || hiddenBlock(shape, member->label)) {
                    continue;
                }
                const std::string memberTitle = label(member->label, member->name);
                const std::string path = "controls." + control.id + "." + member->name;
                const std::string english = d.say.english(member->label);
                if (isEventMember(api, *member)) {
                    Entry& entry = add("event:" + path, name + std::string(kDot) + memberTitle, english, path, "events",
                                       "event", "pointer", EntryKind::Event);
                    entry.control = control.id;
                    entry.memberKind = "event";
                    entry.keywords += " " + control.id + " " + control.name;
                    entry.api = member->label;
                    continue;
                }
                if (!member->property) {
                    Entry& entry = add("call:" + path, name + std::string(kDot) + memberTitle, english, path, "components",
                                       "action", "pointer", EntryKind::Statement);
                    entry.inputs = inputsOf(*member);
                    entry.output = outputOf(*member);
                    entry.control = control.id;
                    entry.memberKind = "function";
                    entry.keywords += " " + control.id + " " + control.name;
                    entry.api = member->label;
                    continue;
                }
                const bool state = member->readonly && !settingMember(member->name);
                Entry& getter = add("get:" + path, name + std::string(kDot) + memberTitle, english, path, "components",
                                    "pure", "pointer", EntryKind::Pure);
                getter.output = typeOf(member->type, member->name);
                getter.control = control.id;
                getter.memberKind = state ? "state" : "property";
                getter.keywords += " " + control.id + " " + control.name;
                getter.api = member->label;
                if (!member->readonly) {
                    Entry& setter = add("set:" + path, d.say.format("node:set", name + std::string(kDot) + memberTitle),
                                        english, path, "components", "action", "pencil", EntryKind::Statement);
                    setter.inputs = {typeOf(member->type, member->name)};
                    setter.control = control.id;
                    setter.api = member->label;
                    setter.memberKind = "property";
                    setter.keywords += " " + control.id + " " + control.name;
                }
            }
        }
    }

    void vars()
    {
        for (const VariableEntry& variable : d.environment.vars) {
            const std::string name = variable.name.empty() ? variable.id : variable.name;
            Entry& getter = add("get:vars." + variable.id, d.say.format("node:varGet", name), {}, "vars." + variable.id,
                                "vars", "pure", "sliders-horizontal", EntryKind::Pure);
            getter.output = PinType::Any;
            Entry& setter = add("set:vars." + variable.id, d.say.format("node:setVar", name), {}, "vars." + variable.id,
                                "vars", "action", "sliders-horizontal", EntryKind::Statement);
            setter.inputs = {PinType::Any};
            add("event:vars." + variable.id + ".onChange", name + std::string(kDot) + label("Variable.onChange", "onChange"),
                d.say.english("Variable.onChange"), "vars." + variable.id + ".onChange", "events", "event",
                "sliders-horizontal", EntryKind::Event);
        }
    }

    PinType variableType(const VariableInfo& info) const
    {
        if (!info.container.empty()) {
            return PinType::List;
        }
        if (info.kind == "toggle") {
            return PinType::Bool;
        }
        if (info.kind == "integer" || info.kind == "decimal") {
            return PinType::Number;
        }
        if (info.kind == "duration") {
            return PinType::Duration;
        }
        if (info.kind == "text") {
            return PinType::Text;
        }
        if (info.kind == "point") {
            return PinType::Point;
        }
        if (info.kind == "region") {
            return PinType::Region;
        }
        if (info.kind == "color") {
            return PinType::Color;
        }
        if (info.kind == "image") {
            return PinType::Image;
        }
        if (info.kind == "key") {
            return PinType::Key;
        }
        if (info.kind == "component" || info.kind == "class") {
            return PinType::Component;
        }
        return PinType::Any;
    }

    void declared(const Item* from)
    {
        for (const VariableInfo& info : d.variables) {
            if (info.inClass) {
                continue;
            }
            if (info.local && (from != nullptr || info.page != m_page)) {
                continue;
            }
            if (!info.local && !info.page.empty()) {
                continue;
            }
            const std::string id = (info.local ? "local:" : "variable:") + info.name;
            const std::string path = (info.local ? "local." : "variable.") + info.name;
            Entry& getter = add("get:" + id, d.say.format("node:get", info.name), {}, path, "vars", "pure", "",
                                EntryKind::Pure);
            getter.output = variableType(info);
            if (!info.constant) {
                Entry& setter = add("set:" + id, d.say.format("node:set", info.name), {}, path, "vars", "action",
                                    "pencil", EntryKind::Statement);
                setter.inputs = {variableType(info)};
            }
        }
    }

    void types()
    {
        for (const StructInfo& info : d.types.structs) {
            Entry& make = add("make:" + info.name, d.say.format("node:make", info.name), info.name, "make:" + info.name,
                              "types", "pure", "square", EntryKind::Pure);
            for (const StructFieldInfo& field : info.fields) {
                make.inputs.push_back(typeOfName(d, field.type));
            }
            make.output = PinType::Struct;
            make.keywords += " make struct " + info.name;
            for (const StructFieldInfo& field : info.fields) {
                Entry& entry = add("break:" + info.name + "." + field.name,
                                   d.say.format("node:breakStruct", info.name) + std::string(kDot) + field.name,
                                   info.name + "." + field.name, "break:" + info.name, "types", "pure", "square",
                                   EntryKind::Pure);
                entry.inputs = {PinType::Struct};
                entry.output = typeOfName(d, field.type);
                entry.keywords += " break struct " + info.name;
            }
        }
        for (const EnumInfo& info : d.types.enums) {
            Entry& value = add("enum:" + info.name, info.name, info.name, "enum:" + info.name, "types", "pure", "layers",
                               EntryKind::Pure);
            value.output = PinType::Enum;
            value.keywords += " enum " + info.name;
            Entry& branch = add("switch:" + info.name, d.say.format("node:switchOn", info.name), info.name,
                                "switch:" + info.name, "types", "flow", "workflow", EntryKind::Statement);
            branch.inputs = {PinType::Enum};
            branch.keywords += " switch enum " + info.name;
        }
        for (const ClassInfo& info : d.classes) {
            if (!info.plain) {
                continue;
            }
            Entry& create = add("new:" + info.name, d.say.format("node:new", info.name), info.name, "new:" + info.name,
                                "types", "action", "square", EntryKind::Statement);
            if (info.constructor != nullptr) {
                for (const Node* param : info.constructor->items) {
                    const Node* id = param->kind == Kind::AssignPattern ? param->target : param;
                    if (id != nullptr && id->kind == Kind::Identifier) {
                        create.inputs.push_back(PinType::Any);
                    }
                }
            }
            create.output = PinType::Object;
            create.keywords += " new class " + info.name;
            Entry& cast = add("cast:" + info.name, d.say.format("node:cast", info.name), info.name, "cast:" + info.name,
                              "types", "flow", "square", EntryKind::Statement);
            cast.inputs = {PinType::Object};
            cast.output = PinType::Object;
            cast.keywords += " cast instanceof class " + info.name;
            for (const std::string& name : info.fields) {
                const bool priv = std::ranges::find(info.privates, name) != info.privates.end();
                Entry& getter = add("getobject:" + info.name + "." + name,
                                    info.name + std::string(kDot) + (priv ? "#" + name : name), name,
                                    "getobject:" + info.name + "." + name, "types", "pure", "sliders-horizontal",
                                    EntryKind::Pure);
                getter.inputs = {PinType::Object};
                getter.output = PinType::Any;
                getter.keywords += " member " + info.name;
                Entry& setter = add("setobject:" + info.name + "." + name,
                                    d.say.format("node:set", info.name + std::string(kDot) + (priv ? "#" + name : name)),
                                    name, "setobject:" + info.name + "." + name, "types", "action", "pencil",
                                    EntryKind::Statement);
                setter.inputs = {PinType::Object, PinType::Any};
                setter.keywords += " member " + info.name;
            }
            for (const std::string& name : info.methods) {
                const bool priv = std::ranges::find(info.privates, name) != info.privates.end();
                Entry& call = add("callobject:" + info.name + "." + name,
                                  info.name + std::string(kDot) + (priv ? "#" + name : name), name,
                                  "callobject:" + info.name + "." + name, "types", "action", "puzzle",
                                  EntryKind::Statement);
                call.inputs = {PinType::Object};
                call.output = PinType::Any;
                call.keywords += " method " + info.name;
            }
            for (const std::string& name : info.events) {
                Entry& emit = add("emit:" + info.name + "." + name,
                                  d.say.format("node:emit", info.name + std::string(kDot) + name), name,
                                  "emit:" + info.name + "." + name, "types", "action", "zap", EntryKind::Statement);
                emit.inputs = {PinType::Object, PinType::Any};
                emit.keywords += " event dispatcher " + info.name;
                Entry& bind = add("bind:" + info.name + "." + name,
                                  d.say.format("node:bind", info.name + std::string(kDot) + name), name,
                                  "bind:" + info.name + "." + name, "types", "action", "link", EntryKind::Statement);
                bind.inputs = {PinType::Object};
                bind.keywords += " event dispatcher " + info.name;
                Entry& unbind = add("unbind:" + info.name + "." + name,
                                    d.say.format("node:unbind", info.name + std::string(kDot) + name), name,
                                    "unbind:" + info.name + "." + name, "types", "action", "link",
                                    EntryKind::Statement);
                unbind.inputs = {PinType::Any};
                unbind.keywords += " event dispatcher " + info.name;
            }
        }
    }

    void functions()
    {
        for (const Page& page : d.pages) {
            if (page.kind != "function" || page.function == nullptr) {
                continue;
            }
            const std::string name = page.key.substr(3);
            Entry& entry = add("call:fn:" + name, page.title, {}, name, "functions", "custom", "puzzle",
                               EntryKind::Statement);
            entry.inputs.assign(page.function->items.size(), PinType::Any);
            entry.output = PinType::Any;
        }
    }

    void library()
    {
        if (d.environment.libraryImport.empty()) {
            return;
        }
        for (const LibraryFunction& function : d.environment.library) {
            if (d.functions.contains(function.name)) {
                continue;
            }
            Entry& entry = add("call:lib:" + function.name, function.name, {}, function.name, "functions", "custom",
                               "puzzle", EntryKind::Statement);
            entry.inputs.assign(function.params.size(), PinType::Any);
            entry.output = PinType::Any;
            entry.keywords += " library " + d.say("section:library");
        }
    }

    void locals(const Item& from)
    {
        const Source* source = from.source;
        if (source == nullptr || source->scopes == nullptr || from.statement == nullptr) {
            return;
        }
        for (const syntax::Binding* binding : source->scopes->visible(from.statement->end)) {
            if (binding->kind == syntax::BindingKind::Import || binding->kind == syntax::BindingKind::Function
                || binding->kind == syntax::BindingKind::Class || binding->scope == source->scopes->root()) {
                continue;
            }
            if (binding->ready > from.statement->end) {
                continue;
            }
            const std::string name(binding->name);
            Entry& getter = add("get:local:" + name, d.say.format("node:get", name), {}, name, "vars", "pure", "",
                                EntryKind::Pure);
            getter.output = PinType::Any;
            if (binding->kind != syntax::BindingKind::Const) {
                Entry& setter = add("set:local:" + name, d.say.format("node:set", name), {}, name, "vars", "action",
                                    "pencil", EntryKind::Statement);
                setter.inputs = {PinType::Any};
            }
        }
    }

    void own()
    {
        const ClassInfo* info = d.defaultClass();
        if (info == nullptr) {
            return;
        }
        for (const std::string& method : info->methods) {
            if (d.page("fn:" + info->name + "." + method) == nullptr) {
                continue;
            }
            add("call:this." + method, method, {}, "this." + method, "functions", "custom", "puzzle",
                EntryKind::Statement);
        }
        for (const std::string& event : info->events) {
            add("call:this." + event + ".emit", event + std::string(kDot) + label("ComponentEvent.emit", "emit"),
                d.say.english("ComponentEvent.emit"), "this." + event + ".emit", "events", "action", "zap",
                EntryKind::Statement);
            Entry& bind = add("bind:this." + event, d.say.format("node:bind", event), event, "bind:this." + event, "types",
                              "action", "link", EntryKind::Statement);
            bind.output = PinType::Any;
            Entry& unbind = add("unbind:this." + event, d.say.format("node:unbind", event), event,
                                "unbind:this." + event, "types", "action", "link", EntryKind::Statement);
            unbind.inputs = {PinType::Any};
        }
        for (const std::string& name : info->fields) {
            Entry& getter = add("get:this." + name, d.say.format("node:get", name), {}, "this." + name, "vars", "pure", "",
                                EntryKind::Pure);
            getter.output = PinType::Any;
            Entry& setter = add("set:this." + name, d.say.format("node:set", name), {}, "this." + name, "vars", "action",
                                "pencil", EntryKind::Statement);
            setter.inputs = {PinType::Any};
        }
        for (const auto& [name, kind] : info->props) {
            const auto title = info->propLabels.find(name);
            Entry& getter = add("get:this.props." + name,
                                title != info->propLabels.end() && !title->second.empty() ? title->second : name, {},
                                "this.props." + name, "components", "pure", "pin", EntryKind::Pure);
            getter.output = kind == "position" ? PinType::Point
                          : kind == "number"   ? PinType::Number
                          : kind == "toggle"   ? PinType::Bool
                          : kind == "text"     ? PinType::Text
                                               : PinType::Any;
        }
    }
};

const Pin* findPin(const std::vector<Pin>& pins, std::string_view name)
{
    const auto found = std::ranges::find(pins, name, &Pin::name);
    return found == pins.end() ? nullptr : &*found;
}

}

bool hiddenBlock(std::string_view shape, std::string_view label)
{
    return std::ranges::any_of(kHiddenBlocks, [shape, label](const HiddenBlock& hidden) {
        return (hidden.shape == "*" || hidden.shape == shape) && hidden.label == label;
    });
}

Ordered Document::catalog(std::string_view page, std::string_view node, std::string_view pin, std::string_view query) const
{
    const Impl& d = *m_impl;
    const Item* from = d.item(node);
    Catalog catalog(d, page);
    catalog.build(from);
    const std::string& pageControl = d.environment.ownControl;
    const Pin* output = from != nullptr ? findPin(from->outputs, pin) : nullptr;
    const Pin* input = from != nullptr && output == nullptr ? findPin(from->inputs, pin) : nullptr;
    std::string control;
    if (from != nullptr && ((output != nullptr && output->type == PinType::Component)
                            || (input != nullptr && input->type == PinType::Component))) {
        if (from->callee.starts_with("controls.")) {
            control = from->callee.substr(9);
        } else if (input != nullptr && input->raw.is_string()) {
            control = input->raw.get<std::string>();
        }
    }
    const std::string needle = lower(query);
    std::vector<const Entry*> chosen;
    for (const Entry& entry : catalog.entries) {
        if (!pageControl.empty() && control.empty() && !entry.control.empty() && entry.control != pageControl) {
            continue;
        }
        if (!control.empty()) {
            if (entry.control != control || entry.memberKind.empty()) {
                continue;
            }
        } else if (output != nullptr) {
            if (output->type == PinType::Exec) {
                if (entry.kind != EntryKind::Statement) {
                    continue;
                }
            } else if (!std::ranges::any_of(entry.inputs, [&output](PinType type) { return compatible(output->type, type); })) {
                continue;
            }
        } else if (input != nullptr) {
            if (input->type == PinType::Exec) {
                if (entry.kind != EntryKind::Statement) {
                    continue;
                }
            } else if (!entry.output || !compatible(*entry.output, input->type)) {
                continue;
            }
        }
        if (!needle.empty()) {
            const bool hit = lower(entry.title).find(needle) != std::string::npos
                          || lower(entry.english).find(needle) != std::string::npos
                          || lower(entry.subtitle).find(needle) != std::string::npos
                          || lower(entry.keywords).find(needle) != std::string::npos;
            if (!hit) {
                continue;
            }
        }
        chosen.push_back(&entry);
    }
    std::ranges::stable_sort(chosen, [&control](const Entry* a, const Entry* b) {
        if (!control.empty()) {
            static constexpr std::string_view kKinds[] = {"property", "state", "event", "function"};
            const auto rank = [](std::string_view kind) {
                return static_cast<std::size_t>(std::ranges::find(kKinds, kind) - std::begin(kKinds));
            };
            return rank(a->memberKind) < rank(b->memberKind);
        }
        return groupIndex(a->group) < groupIndex(b->group);
    });
    Ordered list = Ordered::array();
    for (const Entry* entry : chosen) {
        const std::string group = !control.empty()             ? d.say("members:" + entry->memberKind)
                                  : !entry->groupTitle.empty() ? entry->groupTitle
                                                               : d.say("group:" + entry->group);
        const ControlEntry* owner = entry->control.empty() ? nullptr : d.control(entry->control);
        list.push_back({{"id", entry->id},
                        {"title", entry->title},
                        {"english", entry->english},
                        {"subtitle", entry->subtitle},
                        {"group", group},
                        {"tone", entry->tone},
                        {"icon", entry->icon},
                        {"keywords", entry->keywords},
                        {"control", entry->control},
                        {"controlTitle", owner != nullptr ? controlPageTitle(*owner) : std::string()},
                        {"api", entry->api}});
    }
    return list;
}

}
