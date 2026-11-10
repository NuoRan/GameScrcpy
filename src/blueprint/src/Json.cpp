#include "Model.h"

#include <algorithm>
#include <utility>

namespace gs::blueprint {

using Ordered = nlohmann::ordered_json;
using syntax::Kind;
using syntax::Node;
namespace flag = syntax::flag;

namespace {

constexpr std::string_view kHidden[] = {"setup", "event", "finger", "bind", "props", "inputs", "hint", "id", "name", "type"};

GraphPin pinOf(const Pin& pin)
{
    GraphPin made;
    made.name = pin.name;
    made.label = pin.label;
    made.type = pinTypeName(pin.type);
    made.connected = pin.connected;
    made.optional = pin.optional;
    made.value = pin.value;
    made.editor = pin.editor;
    made.raw = &pin.raw;
    made.choices = pin.choices;
    made.preview = pin.preview;
    made.min = pin.min;
    made.max = pin.max;
    made.step = pin.step;
    made.unit = pin.unit;
    return made;
}

GraphNode nodeOf(const Item& item, std::span<const GraphPin> inputs, std::span<const GraphPin> outputs)
{
    GraphNode made;
    made.key = item.key;
    made.kind = item.kind;
    made.tone = item.tone;
    made.icon = item.icon;
    made.title = item.title;
    made.badge = item.badge;
    made.subtitle = item.subtitle;
    made.note = item.note;
    made.x = item.x;
    made.y = item.y;
    made.inputs = inputs;
    made.outputs = outputs;
    made.code = item.code;
    made.floating = item.floating;
    made.breakpoint = item.breakpoint;
    made.breakpointReason = item.breakpointReason;
    made.problem = item.problem;
    made.fix = item.fix;
    made.fixControl = item.fixControl;
    made.start = item.start;
    return made;
}

GraphWire wireOf(const Wire& wire)
{
    return {wire.key, wire.from, wire.fromPin, wire.to, wire.toPin, wire.type, wire.label, wire.active, wire.dashed};
}

Ordered pinJson(const GraphPin& pin)
{
    Ordered choices = Ordered::array();
    for (const GraphChoice& choice : pin.choices) {
        choices.push_back({{"value", choice.value}, {"text", choice.text}});
    }
    Ordered json{{"name", pin.name},
                 {"label", pin.label},
                 {"type", pin.type},
                 {"connected", pin.connected},
                 {"optional", pin.optional},
                 {"value", pin.value},
                 {"editor", pin.editor},
                 {"raw", pin.raw != nullptr ? Ordered(*pin.raw) : Ordered()},
                 {"choices", std::move(choices)},
                 {"preview", pin.preview}};
    if (pin.min) {
        json["min"] = *pin.min;
    }
    if (pin.max) {
        json["max"] = *pin.max;
    }
    if (pin.step) {
        json["step"] = *pin.step;
    }
    if (!pin.unit.empty()) {
        json["unit"] = pin.unit;
    }
    return json;
}

Ordered nodeJson(const GraphNode& node)
{
    Ordered inputs = Ordered::array();
    for (const GraphPin& pin : node.inputs) {
        inputs.push_back(pinJson(pin));
    }
    Ordered outputs = Ordered::array();
    for (const GraphPin& pin : node.outputs) {
        outputs.push_back(pinJson(pin));
    }
    return {{"key", node.key},
            {"kind", node.kind},
            {"tone", node.tone},
            {"icon", node.icon},
            {"title", node.title},
            {"badge", node.badge},
            {"subtitle", node.subtitle},
            {"note", node.note},
            {"x", node.x},
            {"y", node.y},
            {"inputs", std::move(inputs)},
            {"outputs", std::move(outputs)},
            {"code", node.code},
            {"floating", node.floating},
            {"breakpoint", node.breakpoint},
            {"breakpointReason", node.breakpointReason},
            {"problem", node.problem},
            {"fix", node.fix},
            {"fixControl", node.fixControl},
            {"start", node.start},
            {"active", node.active},
            {"activeSince", node.activeSince},
            {"visited", node.visited}};
}

Ordered wireJson(const GraphWire& wire)
{
    return {{"key", wire.key},         {"from", wire.from},   {"fromPin", wire.fromPin},
            {"to", wire.to},           {"toPin", wire.toPin}, {"type", wire.type},
            {"label", wire.label},     {"active", wire.active}, {"dashed", wire.dashed}};
}

Ordered field(std::string name, std::string label, std::string kind, Ordered value)
{
    return {{"name", std::move(name)},
            {"label", std::move(label)},
            {"kind", std::move(kind)},
            {"value", std::move(value)},
            {"caption", ""},
            {"info", ""},
            {"unit", ""},
            {"choices", Ordered::array()},
            {"items", Ordered::array()},
            {"entries", Ordered::array()}};
}

std::string memberKind(const Api& api, const ApiMember& member)
{
    if (isEventMember(api, member)) {
        return "event";
    }
    if (!member.property) {
        return "function";
    }
    if (!member.readonly || settingMember(member.name)) {
        return "property";
    }
    return "state";
}

std::string iconOfShape(std::string_view shape)
{
    if (shape == "Tap") {
        return "mouse-pointer-click";
    }
    if (shape == "Turbo") {
        return "zap";
    }
    if (shape == "Wasd" || shape == "PadStick" || shape == "RightWalk") {
        return "joystick-ring";
    }
    if (shape == "Camera" || shape == "PadCamera" || shape == "FreeLook") {
        return "crosshair";
    }
    if (shape == "Swipe") {
        return "swipe-path";
    }
    if (shape == "Skill") {
        return "target";
    }
    if (shape == "Dial") {
        return "dial-cells";
    }
    if (shape == "MultiPress") {
        return "multi-touch";
    }
    if (shape == "TempCursor") {
        return "cursor-arrow";
    }
    if (shape == "ActionWheel") {
        return "wheel-slices";
    }
    if (shape == "Pinch") {
        return "maximize-2";
    }
    return "pointer";
}

std::string iconOfVariable(std::string_view kind)
{
    if (kind == "toggle") {
        return "power";
    }
    if (kind == "integer" || kind == "decimal") {
        return "gauge";
    }
    if (kind == "text") {
        return "type";
    }
    if (kind == "point") {
        return "pin";
    }
    if (kind == "region") {
        return "square";
    }
    if (kind == "color") {
        return "palette";
    }
    if (kind == "image") {
        return "image";
    }
    if (kind == "key") {
        return "keyboard";
    }
    if (kind == "duration") {
        return "timer";
    }
    if (kind == "component") {
        return "pointer";
    }
    if (kind == "class") {
        return "puzzle";
    }
    return "sliders-horizontal";
}

std::string variableTypeText(const Say& say, const VariableInfo& info)
{
    const std::string type = say("variable:type:" + info.kind);
    const std::string element = type.empty() ? info.kind : type;
    if (info.container == "list") {
        const std::string pattern = say("variable:listOf");
        return fill(pattern.empty() ? std::string("%1 list") : pattern, element);
    }
    if (info.container == "dict") {
        const std::string pattern = say("variable:mapOf");
        return fill(pattern.empty() ? std::string("%1, by name") : pattern, element);
    }
    if (info.container == "set") {
        const std::string pattern = say("variable:setOf");
        return fill(pattern.empty() ? std::string("%1 set") : pattern, element);
    }
    return element;
}

std::string detailKind(const Pin& pin)
{
    if (pin.editor == "number" || pin.editor == "duration" || pin.editor == "text" || pin.editor == "toggle"
        || pin.editor == "choice" || pin.editor == "key") {
        return pin.editor.str();
    }
    if (pin.editor == "component") {
        return "choice";
    }
    if (pin.editor == "code") {
        return "text";
    }
    return "readonly";
}

Ordered pinField(const Pin& pin)
{
    Ordered json = field(pin.name, pin.label.empty() ? pin.name : pin.label, detailKind(pin),
                         pin.editor == "code" || detailKind(pin) == "readonly" ? Ordered(pin.value) : Ordered(pin.raw));
    if (pin.raw.is_null() && detailKind(pin) != "readonly") {
        json["value"] = pin.editor == "toggle" ? Ordered(false) : Ordered("");
        if (pin.optional && !pin.connected) {
            json["isDefault"] = true;
            if (!pin.fallback.is_null()) {
                json["value"] = Ordered(pin.fallback);
            }
        }
    }
    if (!pin.unit.empty()) {
        json["unit"] = pin.unit;
    }
    if (pin.min) {
        json["min"] = *pin.min;
    }
    if (pin.max) {
        json["max"] = *pin.max;
    }
    if (pin.step) {
        json["step"] = *pin.step;
    }
    Ordered choices = Ordered::array();
    for (const Choice& choice : pin.choices) {
        choices.push_back({{"value", choice.value}, {"text", choice.text}});
    }
    json["choices"] = std::move(choices);
    return json;
}

const Node* argumentAt(const Item& item, std::size_t index)
{
    if (item.expression == nullptr || item.expression->kind != Kind::Call || index >= item.expression->items.size()) {
        return nullptr;
    }
    return item.expression->items[index];
}

std::string againOf(const Item& item)
{
    const Node* options = syntax::unparen(argumentAt(item, item.optionsArgument));
    if (options == nullptr || options->kind != Kind::Object) {
        return "ignore";
    }
    for (const Node* entry : options->items) {
        if (entry->kind == Kind::Property && entry->target != nullptr && entry->target->text == "again") {
            if (const std::optional<std::string> value = stringValue(entry->value)) {
                return *value;
            }
        }
    }
    return "ignore";
}

std::string casesText(const Document::Impl& d, const Item& item)
{
    std::string text;
    if (item.statement == nullptr || item.source == nullptr) {
        return text;
    }
    for (const Node* each : item.statement->items) {
        if (each->kind != Kind::Case || each->test == nullptr) {
            continue;
        }
        text += (text.empty() ? "" : ", ") + d.text(*item.source, each->test);
    }
    return text;
}

}

Document::Document(std::string source, const Environment& environment, Layout layout, const Document* previous,
                   const Sizes& sizes)
    : m_impl(std::make_unique<Impl>())
{
    Impl& d = *m_impl;
    d.environment = environment;
    d.say = Say(environment.labels);
    d.layout = std::move(layout);
    d.sizes = sizes;
    Source& file = d.sources.emplace_back();
    file.text = std::move(source);
    if (d.environment.api == nullptr) {
        d.environment.api = loadApi({}, {});
    }
    build(d, previous);
}

Document::~Document() = default;
Document::Document(Document&&) noexcept = default;
Document& Document::operator=(Document&&) noexcept = default;

const std::string& Document::source() const noexcept
{
    return m_impl->file().text;
}

const Layout& Document::layout() const noexcept
{
    return m_impl->layout;
}

bool Document::ok() const noexcept
{
    return m_impl->ok;
}

const std::string& Document::problem() const noexcept
{
    return m_impl->problem;
}

std::uint32_t Document::problemLine() const noexcept
{
    return m_impl->problemLine;
}

bool Document::hasPage(std::string_view page) const
{
    return m_impl->page(page) != nullptr;
}

std::string Document::pageOf(std::string_view key) const
{
    if (const Item* item = m_impl->item(key)) {
        return item->page;
    }
    for (const auto& [anchor, page] : m_impl->layout.pages) {
        for (const CommentBox& box : page.comments) {
            if (box.key == key) {
                for (const Page& candidate : m_impl->pages) {
                    if (candidate.anchor == anchor) {
                        return candidate.key;
                    }
                }
            }
        }
    }
    return {};
}

std::string Document::find(std::string_view signature) const
{
    for (const Item& item : m_impl->items) {
        if (signatureOf(item) == signature) {
            return item.key;
        }
    }
    return {};
}

std::vector<std::string> Document::keys(std::string_view page) const
{
    std::vector<std::string> list;
    if (const Page* found = m_impl->page(page)) {
        for (const Item* item : found->items) {
            list.push_back(item->key);
        }
    }
    return list;
}

Ordered Document::pages() const
{
    Ordered list = Ordered::array();
    for (const Page& page : m_impl->pages) {
        if (page.listed) {
            list.push_back({{"key", page.key}, {"title", page.title}, {"icon", page.icon}, {"kind", page.kind}});
        }
    }
    return list;
}

Ordered Document::crumbs(std::string_view key) const
{
    std::vector<const Page*> chain;
    const Page* page = m_impl->page(key);
    while (page != nullptr && chain.size() < 16) {
        chain.push_back(page);
        if (page->listed) {
            break;
        }
        page = m_impl->page(page->parent);
    }
    Ordered list = Ordered::array();
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        list.push_back({{"key", (*it)->key}, {"title", (*it)->title}});
    }
    return list;
}

Graph Document::view(std::string_view key) const
{
    const Impl& d = *m_impl;
    const Page* page = d.page(key);
    Graph graph;
    graph.key = std::string(key);
    graph.kind = page != nullptr ? page->kind : std::string("events");
    graph.title = page != nullptr ? page->title : d.mainTitle;
    graph.readOnly = page == nullptr || page->readOnly || !d.ok;
    graph.problem = d.problem;
    graph.problemLine = d.problemLine;
    if (page == nullptr) {
        return graph;
    }
    const auto layout = d.layout.pages.find(page->anchor);
    std::vector<const Reroute*> reroutes;
    if (layout != d.layout.pages.end()) {
        for (const Reroute& reroute : layout->second.reroutes) {
            reroutes.push_back(&reroute);
        }
        graph.comments = layout->second.comments;
        graph.view = layout->second.view;
    }
    std::size_t pinCount = reroutes.size() * 2;
    for (const Item* item : page->items) {
        pinCount += item->inputs.size() + item->outputs.size();
    }
    graph.pins.reserve(pinCount);
    graph.nodes.reserve(page->items.size() + reroutes.size());
    for (const Item* item : page->items) {
        const std::size_t first = graph.pins.size();
        for (const Pin& pin : item->inputs) {
            graph.pins.push_back(pinOf(pin));
        }
        const std::size_t middle = graph.pins.size();
        for (const Pin& pin : item->outputs) {
            graph.pins.push_back(pinOf(pin));
        }
        const std::span<const GraphPin> all(graph.pins);
        graph.nodes.push_back(nodeOf(*item, all.subspan(first, middle - first), all.subspan(middle, graph.pins.size() - middle)));
    }
    graph.wires.reserve(page->wires.size() + reroutes.size());
    for (const Wire& wire : page->wires) {
        const auto through = std::ranges::find_if(reroutes, [&wire](const Reroute* reroute) {
            return reroute->from == wire.from && reroute->fromPin == wire.fromPin && reroute->to == wire.to
                && reroute->toPin == wire.toPin;
        });
        if (through == reroutes.end()) {
            graph.wires.push_back(wireOf(wire));
            continue;
        }
        const Reroute& reroute = **through;
        GraphPin in;
        in.name = "in";
        in.type = wire.type;
        in.connected = true;
        GraphPin out = in;
        out.name = "out";
        const std::size_t first = graph.pins.size();
        graph.pins.push_back(in);
        graph.pins.push_back(out);
        const std::span<const GraphPin> all(graph.pins);
        GraphNode dot;
        dot.key = reroute.key;
        dot.kind = "reroute";
        dot.tone = wire.type == "exec" ? "flow" : "pure";
        dot.x = reroute.x;
        dot.y = reroute.y;
        dot.inputs = all.subspan(first, 1);
        dot.outputs = all.subspan(first + 1, 1);
        graph.nodes.push_back(dot);
        GraphWire firstHalf = wireOf(wire);
        firstHalf.to = reroute.key;
        firstHalf.toPin = "in";
        firstHalf.key = graph.strings.emplace_back(std::string(wire.key) + "~1");
        GraphWire secondHalf = wireOf(wire);
        secondHalf.from = reroute.key;
        secondHalf.fromPin = "out";
        secondHalf.key = graph.strings.emplace_back(std::string(wire.key) + "~2");
        graph.wires.push_back(firstHalf);
        graph.wires.push_back(secondHalf);
    }
    return graph;
}

Ordered graphJson(const Graph& graph)
{
    Ordered nodes = Ordered::array();
    for (const GraphNode& node : graph.nodes) {
        nodes.push_back(nodeJson(node));
    }
    Ordered wires = Ordered::array();
    for (const GraphWire& wire : graph.wires) {
        wires.push_back(wireJson(wire));
    }
    Ordered comments = Ordered::array();
    for (const CommentBox& box : graph.comments) {
        comments.push_back({{"key", box.key},
                            {"x", box.x},
                            {"y", box.y},
                            {"width", box.width},
                            {"height", box.height},
                            {"title", box.title},
                            {"color", box.color}});
    }
    return {{"key", graph.key},
            {"kind", graph.kind},
            {"title", graph.title},
            {"readOnly", graph.readOnly},
            {"nodes", std::move(nodes)},
            {"wires", std::move(wires)},
            {"comments", std::move(comments)},
            {"problem", graph.problem},
            {"problemLine", graph.problemLine}};
}

Ordered Document::graph(std::string_view key) const
{
    return graphJson(view(key));
}

Ordered Document::sidebar(std::string_view page) const
{
    const Impl& d = *m_impl;
    const Api& api = d.api();
    const auto section = [](std::string key, std::string title, bool addable, Ordered items) {
        const std::size_t count = items.size();
        return Ordered{{"key", std::move(key)},
                       {"title", std::move(title)},
                       {"count", std::to_string(count)},
                       {"addable", addable},
                       {"items", std::move(items)}};
    };
    const auto entry = [](std::string key, std::string icon, std::string title, std::string badge, std::string dot) {
        return Ordered{{"key", std::move(key)},
                       {"icon", std::move(icon)},
                       {"title", std::move(title)},
                       {"badge", std::move(badge)},
                       {"dot", std::move(dot)},
                       {"category", ""},
                       {"description", ""},
                       {"members", Ordered::array()}};
    };
    const auto variableEntry = [](std::string key, const VariableInfo& info, std::string badge, std::string dot) {
        Ordered item = Ordered{{"key", std::move(key)},
                               {"icon", iconOfVariable(info.kind)},
                               {"title", info.name},
                               {"badge", std::move(badge)},
                               {"dot", std::move(dot)},
                               {"category", info.category},
                               {"description", info.description},
                               {"kind", info.kind},
                               {"container", info.container},
                               {"members", Ordered::array()}};
        return item;
    };
    Ordered sections = Ordered::array();
    const ClassInfo* own = d.defaultClass();
    const auto typeSections = [&]() {
        Ordered list = Ordered::array();
        Ordered structs = Ordered::array();
        for (const StructInfo& info : d.types.structs) {
            Ordered item = entry("def:" + info.name, "square", info.name, d.say("badge:struct"), "");
            for (const StructFieldInfo& field : info.fields) {
                item["members"].push_back({{"key", field.name},
                                           {"title", field.name + (field.optional ? "?" : "")},
                                           {"kind", "field"},
                                           {"badge", field.type}});
            }
            structs.push_back(std::move(item));
        }
        list.push_back(section("structs", d.say("section:structs"), true, std::move(structs)));
        Ordered enums = Ordered::array();
        for (const EnumInfo& info : d.types.enums) {
            Ordered item = entry("def:" + info.name, "layers", info.name, d.say("badge:enum"), "");
            for (const std::string& option : info.options) {
                item["members"].push_back({{"key", "enum:" + info.name + "." + option},
                                           {"title", option},
                                           {"kind", "option"},
                                           {"badge", ""}});
            }
            enums.push_back(std::move(item));
        }
        list.push_back(section("enums", d.say("section:enums"), true, std::move(enums)));
        Ordered classes = Ordered::array();
        for (const ClassInfo& info : d.classes) {
            if (!info.plain) {
                continue;
            }
            Ordered item = entry("def:" + info.name, "square", info.name, info.base.empty() ? std::string() : "extends " + info.base,
                                 "");
            for (const std::string& name : info.fields) {
                const bool priv = std::ranges::find(info.privates, name) != info.privates.end();
                item["members"].push_back({{"key", "field:" + name}, {"title", name}, {"kind", "field"},
                                           {"badge", priv ? std::string("private") : std::string()}});
            }
            for (const std::string& name : info.methods) {
                const bool priv = std::ranges::find(info.privates, name) != info.privates.end();
                item["members"].push_back({{"key", "fn:" + info.name + "." + name}, {"title", name}, {"kind", "function"},
                                           {"badge", priv ? std::string("private") : std::string()}});
            }
            for (const std::string& name : info.events) {
                item["members"].push_back({{"key", "event:" + name}, {"title", name}, {"kind", "event"}, {"badge", ""}});
            }
            classes.push_back(std::move(item));
        }
        list.push_back(section("classes", d.say("section:classes"), true, std::move(classes)));
        return list;
    };
    if (own != nullptr && (own->component || own->task)) {
        Ordered props = Ordered::array();
        for (const auto& [name, kind] : own->props) {
            const auto label = own->propLabels.find(name);
            const std::string title = label != own->propLabels.end() && !label->second.empty() ? label->second : name;
            props.push_back(entry("prop:" + name, kind == "key" ? "keyboard" : kind == "position" ? "pin" : "sliders-horizontal",
                                  title, d.say("kind:" + kind), ""));
        }
        sections.push_back(section(own->task ? "params" : "props", d.say(own->task ? "section:params" : "section:props"),
                                   true, std::move(props)));
        Ordered events = Ordered::array();
        for (const std::string& name : own->events) {
            events.push_back(entry("event:" + name, "zap", name, "", "event"));
        }
        sections.push_back(section("events", d.say("section:ownEvents"), true, std::move(events)));
        Ordered functions = Ordered::array();
        for (const std::string& name : own->methods) {
            if (d.page("fn:" + own->name + "." + name) != nullptr) {
                functions.push_back(entry("fn:" + own->name + "." + name, "puzzle", name, "", ""));
            }
        }
        sections.push_back(section("functions", d.say("section:ownFunctions"), true, std::move(functions)));
        Ordered fields = Ordered::array();
        for (const std::string& name : own->fields) {
            const VariableInfo* info = d.declaredVariable("", name);
            if (info != nullptr) {
                fields.push_back(variableEntry("field:" + name, *info, variableTypeText(d.say, *info), "var"));
            } else {
                fields.push_back(entry("field:" + name, "sliders-horizontal", name, "", "var"));
            }
        }
        sections.push_back(section("vars", d.say("section:vars"), true, std::move(fields)));
        for (Ordered& extra : typeSections()) {
            sections.push_back(std::move(extra));
        }
        return sections;
    }
    Ordered controls = Ordered::array();
    for (const ControlEntry& control : d.environment.controls) {
        Ordered item = entry("control:" + control.id, iconOfShape(control.type), controlPageTitle(control),
                             d.say("type:" + control.type), "");
        const std::string shape = api.interfaces.contains(control.type) ? control.type : "Component";
        for (const ApiMember* member : api.members(shape)) {
            if (std::ranges::find(kHidden, member->name) != std::end(kHidden) || hiddenBlock(shape, member->label)) {
                continue;
            }
            const std::string kind = memberKind(api, *member);
            std::string title = d.say.optional(member->label);
            if (title.empty()) {
                title = member->name;
            }
            item["members"].push_back(
                {{"key", member->name}, {"title", title}, {"kind", kind}, {"badge", d.say("members:" + kind)}});
        }
        controls.push_back(std::move(item));
    }
    sections.push_back(section("controls", d.say("section:controls"), false, std::move(controls)));
    Ordered vars = Ordered::array();
    for (const VariableInfo& info : d.variables) {
        if (info.inClass || info.local || !info.page.empty()) {
            continue;
        }
        vars.push_back(variableEntry("variable:" + info.name, info, variableTypeText(d.say, info), "variable"));
    }
    for (const VariableEntry& variable : d.environment.vars) {
        vars.push_back(entry("var:" + variable.id, "sliders-horizontal", variable.name.empty() ? variable.id : variable.name,
                             d.say("kind:" + variable.kind), "var"));
    }
    for (const std::string& name : d.globalNames) {
        vars.push_back(entry("global:" + name, "layers", name, d.say("badge:global"), "global"));
    }
    for (const VariableInfo& info : d.variables) {
        if (!info.local || info.page != page) {
            continue;
        }
        vars.push_back(variableEntry("local:" + info.name, info, variableTypeText(d.say, info), "local"));
    }
    sections.push_back(section("vars", d.say("section:vars"), true, std::move(vars)));
    for (Ordered& extra : typeSections()) {
        sections.push_back(std::move(extra));
    }
    sections.push_back(section("events", d.say("section:events"), true, Ordered::array()));
    sections.push_back(section("interfaces", d.say("section:interfaces"), true, Ordered::array()));
    Ordered functions = Ordered::array();
    for (const Page& listed : d.pages) {
        if (listed.kind == "function" && listed.listed) {
            functions.push_back(entry(listed.key, "puzzle", listed.title, "", ""));
        }
    }
    sections.push_back(section("functions", d.say("section:functions"), true, std::move(functions)));
    if (!d.environment.libraryImport.empty()) {
        Ordered shared = Ordered::array();
        for (const LibraryFunction& function : d.environment.library) {
            shared.push_back(entry("lib:" + function.name, "puzzle", function.name, "", ""));
        }
        sections.push_back(section("library", d.say("section:library"), true, std::move(shared)));
    }
    Ordered files = Ordered::array();
    std::vector<std::string> paths = d.environment.files;
    std::ranges::stable_partition(paths, [](const std::string& path) { return path == "logic.ts"; });
    for (const std::string& path : paths) {
        constexpr std::string_view kFolder = "controls/";
        const bool component = path.starts_with(kFolder) && path.ends_with(".ts") && !path.ends_with(".d.ts");
        const ControlEntry* control =
            component ? d.control(std::string_view(path).substr(kFolder.size(), path.size() - kFolder.size() - 3)) : nullptr;
        if (control != nullptr) {
            files.push_back(entry("file:" + path, iconOfShape(control->type), controlPageTitle(*control), "", ""));
        } else {
            files.push_back(entry("file:" + path, path == kLibraryFile ? "layers" : "code-xml", mainTitle(d.say, path), "", ""));
        }
    }
    sections.push_back(section("pages", d.say("section:pages"), true, std::move(files)));
    return sections;
}

Ordered Document::typeInfo(std::string_view name) const
{
    const Impl& d = *m_impl;
    if (const StructInfo* info = structNamed(d.types, name)) {
        Ordered fields = Ordered::array();
        for (const StructFieldInfo& field : info->fields) {
            fields.push_back({{"name", field.name}, {"type", field.type}, {"optional", field.optional}});
        }
        return {{"name", info->name}, {"kind", "struct"}, {"fields", std::move(fields)}, {"options", Ordered::array()},
                {"members", Ordered::array()}, {"params", Ordered::array()}, {"bases", Ordered::array()}};
    }
    if (const EnumInfo* info = enumNamed(d.types, name)) {
        Ordered options = Ordered::array();
        for (const std::string& option : info->options) {
            options.push_back(option);
        }
        return {{"name", info->name}, {"kind", "enum"}, {"fields", Ordered::array()}, {"options", std::move(options)},
                {"members", Ordered::array()}, {"params", Ordered::array()}, {"bases", Ordered::array()}};
    }
    Ordered bases = Ordered::array();
    for (const ClassInfo& candidate : d.classes) {
        if (candidate.plain) {
            bases.push_back(candidate.name);
        }
    }
    for (const ClassInfo& info : d.classes) {
        if (info.name != name || !info.plain) {
            continue;
        }
        Ordered members = Ordered::array();
        for (const std::string& field : info.fields) {
            const bool priv = std::ranges::find(info.privates, field) != info.privates.end();
            members.push_back({{"name", field}, {"type", ""}, {"private", priv}, {"kind", "field"}, {"value", ""}});
        }
        for (const std::string& method : info.methods) {
            const bool priv = std::ranges::find(info.privates, method) != info.privates.end();
            members.push_back({{"name", method}, {"type", ""}, {"private", priv}, {"kind", "function"}, {"value", ""}});
        }
        for (const std::string& event : info.events) {
            const bool priv = std::ranges::find(info.privates, event) != info.privates.end();
            members.push_back({{"name", event}, {"type", ""}, {"private", priv}, {"kind", "event"}, {"value", ""}});
        }
        Ordered params = Ordered::array();
        if (info.constructor != nullptr) {
            for (const syntax::Node* param : info.constructor->items) {
                const syntax::Node* id = param->kind == Kind::AssignPattern ? param->target : param;
                if (id != nullptr && id->kind == Kind::Identifier) {
                    params.push_back({{"name", std::string(id->text)}, {"type", ""}});
                }
            }
        }
        return {{"name", info.name},
                {"kind", "class"},
                {"fields", Ordered::array()},
                {"options", Ordered::array()},
                {"members", std::move(members)},
                {"params", std::move(params)},
                {"base", info.base},
                {"bases", std::move(bases)}};
    }
    return {{"name", std::string(name)}, {"kind", ""}, {"fields", Ordered::array()}, {"options", Ordered::array()},
            {"members", Ordered::array()}, {"params", Ordered::array()}, {"bases", std::move(bases)}};
}

Ordered Document::details(std::string_view page, std::string_view key, const Ordered& log) const
{
    const Impl& d = *m_impl;
    Ordered groups = Ordered::array();
    std::string icon;
    std::string tone;
    std::string title;
    std::string subtitle;
    std::string codeText;
    std::uint32_t codeLine = 0;
    const Page* current = d.page(page);
    const auto pushGroup = [&groups](std::string groupTitle, Ordered fields) {
        if (!fields.empty()) {
            groups.push_back({{"title", std::move(groupTitle)}, {"fields", std::move(fields)}});
        }
    };
    const Item* item = d.item(key);
    if (item == nullptr) {
        if (current != nullptr) {
            const auto layout = d.layout.pages.find(current->anchor);
            if (layout != d.layout.pages.end()) {
                for (const CommentBox& box : layout->second.comments) {
                    if (box.key != key) {
                        continue;
                    }
                    Ordered colors = Ordered::array();
                    for (const std::string_view color : {"neutral", "blue", "green", "amber", "red", "purple"}) {
                        colors.push_back({{"value", color}, {"text", d.say("color:" + std::string(color))}});
                    }
                    Ordered color = field("color", d.say("detail:color"), "choice", box.color);
                    color["choices"] = std::move(colors);
                    pushGroup({}, Ordered::array({field("title", d.say("detail:title"), "text", box.title), color}));
                    return {{"icon", "square"},
                            {"tone", "neutral"},
                            {"title", box.title.empty() ? d.say("node:comment") : box.title},
                            {"subtitle", d.say("node:comment")},
                            {"groups", std::move(groups)},
                            {"code", ""},
                            {"codeLine", 0}};
                }
            }
        }
        return {{"icon", ""}, {"tone", ""}, {"title", ""}, {"subtitle", ""}, {"groups", Ordered::array()}, {"code", ""}, {"codeLine", 0}};
    }
    icon = item->icon;
    tone = item->tone;
    title = item->title;
    subtitle = (current != nullptr ? current->title : std::string()) + (item->badge.empty() ? "" : " \xC2\xB7 " + item->badge.str());
    if (item->statement != nullptr && item->source != nullptr && item->source->id.empty()) {
        const Source& source = *item->source;
        const Node* last = item->last != nullptr ? item->last : item->statement;
        std::size_t lineStart = item->statement->start;
        while (lineStart > 0 && source.text[lineStart - 1] != '\n' && source.text[lineStart - 1] != '\r') {
            --lineStart;
        }
        codeText = dedent(d.text(source, static_cast<std::uint32_t>(lineStart), last->end));
        codeLine = source.lines->lineOf(item->statement->start);
    }
    Ordered note = Ordered::array({field("note", d.say("detail:note"), "text", item->note)});
    if (item->role == Role::Event && !item->keyName.empty()) {
        title = d.say("details:keyEvent");
        subtitle = (current != nullptr ? current->title + " \xC2\xB7 " : std::string()) + item->title + " " + item->badge.str();
        Ordered when = field("when", d.say("detail:when"), "segmented", item->member);
        for (const std::string_view method : {"onPress", "onRelease", "whileHeld", "onTap", "onLongPress", "onDoubleTap"}) {
            when["choices"].push_back({{"value", method}, {"text", d.say("when:" + std::string(method))}});
        }
        Ordered first = Ordered::array({field("key", d.say("detail:key"), "key", item->keyName), when});
        if (item->member == "onLongPress") {
            const std::optional<double> ms = numberValue(argumentAt(*item, item->badgeArgument));
            Ordered hold = field("holdMs", d.say("detail:holdFor"), "duration", ms ? *ms : 500.0);
            hold["unit"] = d.say("unit:s");
            hold["min"] = 100;
            hold["max"] = 10000;
            hold["step"] = 100;
            first.push_back(std::move(hold));
        }
        pushGroup({}, std::move(first));
    } else if (item->role == Role::Event && item->badgeArgument != kNone) {
        const std::optional<double> ms = numberValue(argumentAt(*item, item->badgeArgument));
        Ordered every = field("badgeMs", d.say("detail:interval"), "duration", ms ? *ms : 1000.0);
        every["unit"] = d.say("unit:s");
        every["min"] = 0;
        pushGroup({}, Ordered::array({every}));
    }
    if (item->role == Role::Event && item->optionsArgument != kNone) {
        Ordered again = field("again", d.say("detail:again"), "choice", againOf(*item));
        for (const std::string_view mode : {"ignore", "restart", "stop", "queue", "parallel"}) {
            again["choices"].push_back({{"value", mode}, {"text", d.say("again:" + std::string(mode))}});
        }
        again["caption"] = d.say("detail:againCaption");
        pushGroup({}, Ordered::array({again}));
    }
    if (takesKey(d, *item)) {
        Ordered take = field("consume", d.say("detail:consume"), "choice", consumesKey(*item) ? "logic" : "both");
        for (const std::string_view mode : {"logic", "both"}) {
            take["choices"].push_back({{"value", mode}, {"text", d.say("consume:" + std::string(mode))}});
        }
        take["caption"] = d.say("detail:consumeCaption");
        pushGroup({}, Ordered::array({take}));
    }
    if (item->token.starts_with("enum:") && item->expression != nullptr && item->expression->kind == Kind::Member
        && item->expression->value != nullptr) {
        const EnumInfo* info = enumNamed(d.types, std::string_view(item->token).substr(5));
        Ordered option = field("option", d.say("detail:option"), "choice",
                               std::string(item->expression->value->text));
        if (info != nullptr) {
            for (const std::string& name : info->options) {
                option["choices"].push_back({{"value", name}, {"text", name}});
            }
        }
        pushGroup({}, Ordered::array({option}));
    } else if (item->role == Role::Code) {
        pushGroup({}, Ordered::array({field("code", d.say("detail:code"), "text", item->code)}));
    } else if (item->role == Role::State) {
        Ordered enter = field("enter", d.say("detail:enter"), "list", nullptr);
        if (const Page* statePage = d.page(item->target)) {
            for (const Item* inner : statePage->items) {
                if (inner->chain != nullptr && inner->chain->head == statePage->entry) {
                    enter["items"].push_back({{"icon", inner->icon}, {"text", inner->title}, {"badge", inner->badge}, {"tone", inner->tone}});
                }
            }
        }
        Ordered leave = field("leave", d.say("detail:leave"), "list", nullptr);
        if (current != nullptr) {
            for (const Wire& wire : current->wires) {
                if (wire.from == item->key) {
                    const Item* to = d.item(wire.to);
                    leave["items"].push_back({{"icon", "chevron-right"},
                                              {"text", wire.label.empty() ? d.say("detail:always") : wire.label.str()},
                                              {"badge", to != nullptr ? to->title : std::string(wire.to)},
                                              {"tone", "neutral"}});
                }
            }
        }
        Ordered entries = field("log", d.say("detail:log"), "log", nullptr);
        entries["entries"] = log.is_array() ? log : Ordered::array();
        pushGroup({}, Ordered::array({enter, leave, entries}));
    } else if (item->role == Role::Machine) {
        Ordered name = field("name", d.say("detail:name"), "text", item->stateName);
        Ordered start = field("start", d.say("detail:start"), "choice", "");
        if (const Page* machinePage = d.page(item->target)) {
            for (const Item* state : machinePage->items) {
                if (state->role == Role::State) {
                    start["choices"].push_back({{"value", state->stateName}, {"text", state->title}});
                    if (state->start) {
                        start["value"] = state->stateName;
                    }
                }
            }
        }
        pushGroup({}, Ordered::array({name, start}));
    } else if (item->role != Role::Event) {
        Ordered fields = Ordered::array();
        for (const Pin& pin : item->inputs) {
            if (pin.type != PinType::Exec) {
                fields.push_back(pinField(pin));
            }
        }
        pushGroup({}, std::move(fields));
    }
    if (item->role != Role::Pure && item->role != Role::Entry) {
        pushGroup({}, std::move(note));
    }
    if (item->role == Role::Switch) {
        Ordered cases = field("cases", d.say("detail:cases"), "text", casesText(d, *item));
        cases["caption"] = d.say("detail:casesCaption");
        pushGroup({}, Ordered::array({std::move(cases)}));
    }
    return {{"icon", icon},
            {"tone", tone},
            {"title", title},
            {"subtitle", subtitle},
            {"groups", std::move(groups)},
            {"code", codeText},
            {"codeLine", codeLine}};
}

std::string Document::open(std::string_view node) const
{
    const Item* item = m_impl->item(node);
    if (item == nullptr) {
        return {};
    }
    if (!item->target.empty() && (m_impl->page(item->target) != nullptr || item->target.view().starts_with("file:"))) {
        return item->target;
    }
    return {};
}

Target Document::jumpTarget(std::string_view node) const
{
    const Item* item = m_impl->item(node);
    if (item == nullptr || item->source == nullptr || !item->source->id.empty()) {
        return {};
    }
    const Node* at = item->statement != nullptr ? item->statement : item->expression;
    if (at == nullptr && item->function != nullptr) {
        at = item->function;
    }
    if (at == nullptr) {
        return {};
    }
    const Place place = item->source->lines->place(at->start);
    return {place.line, place.column};
}

std::string Document::nodeAt(std::string_view position) const
{
    const Impl& d = *m_impl;
    const std::optional<Place> place = parsePosition(position);
    if (!place || d.sources.empty() || d.file().lines == nullptr) {
        return {};
    }
    const std::size_t offset = d.file().lines->offset(*place);
    const Item* fallback = nullptr;
    for (const Item& item : d.items) {
        if (item.source != &d.file() || item.statement == nullptr || item.statement->start != offset) {
            continue;
        }
        if (item.role != Role::Pure && item.chain != nullptr) {
            return item.key;
        }
        if (fallback == nullptr) {
            fallback = &item;
        }
    }
    return fallback != nullptr ? fallback->key : std::string();
}

std::string Document::wireInto(std::string_view node, std::string_view from) const
{
    const Item* item = m_impl->item(node);
    if (item == nullptr) {
        return {};
    }
    const Page* page = m_impl->page(item->page);
    if (page == nullptr) {
        return {};
    }
    for (const Wire& wire : page->wires) {
        if (wire.to == node && (wire.type == "exec" || wire.type == "transition") && (from.empty() || wire.from == from)) {
            return wire.key;
        }
    }
    return {};
}

Ordered Document::breakpointPositions() const
{
    Ordered list = Ordered::array();
    for (const Breakpoint& breakpoint : m_impl->layout.breakpoints) {
        const Item* item = m_impl->item(breakpoint.anchor);
        if (item == nullptr || item->breakpoint == "invalid" || item->breakpoint.empty()) {
            continue;
        }
        const std::string position = positionOf(*item);
        if (position.empty()) {
            continue;
        }
        list.push_back({{"position", position}, {"enabled", breakpoint.enabled}});
    }
    return list;
}

std::string Document::localProducer(std::string_view position, std::string_view name) const
{
    const Impl& d = *m_impl;
    const std::optional<Place> place = parsePosition(position);
    if (!place || d.sources.empty() || d.file().scopes == nullptr) {
        return {};
    }
    const Source& file = d.file();
    const std::size_t offset = file.lines->offset(*place);
    const syntax::Binding* binding = file.scopes->lookup(name, static_cast<std::uint32_t>(offset));
    if (binding == nullptr) {
        return {};
    }
    const auto found = d.producers.find(binding);
    if (found == d.producers.end()) {
        return {};
    }
    return found->second.first->key + "\n" + found->second.second;
}

}
