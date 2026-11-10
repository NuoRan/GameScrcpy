#include "Model.h"

#include <gs/tsstrip/Strip.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <utility>

namespace gs::blueprint {

using syntax::Binding;
using syntax::BindingKind;
using syntax::Kind;
using syntax::Node;
using syntax::unparen;
namespace flag = syntax::flag;

namespace {

constexpr std::string_view kDot = " \xC2\xB7 ";

struct Names {
    std::array<std::string_view, 12> items{};
    std::size_t count = 0;

    [[nodiscard]] std::size_t size() const noexcept { return count; }
    [[nodiscard]] bool empty() const noexcept { return count == 0; }
    [[nodiscard]] std::string_view operator[](std::size_t index) const noexcept { return items[index]; }
    [[nodiscard]] std::string_view front() const noexcept { return items[0]; }
    [[nodiscard]] std::string_view back() const noexcept { return items[count - 1]; }
    [[nodiscard]] const std::string_view* begin() const noexcept { return items.data(); }
    [[nodiscard]] const std::string_view* end() const noexcept { return items.data() + count; }
};

struct Path {
    bool ok = false;
    Kind root = Kind::Identifier;
    std::string_view rootName;
    Names names;
    std::unique_ptr<std::deque<std::string>> owned;
};

Path pathOf(const Node* node)
{
    Path path;
    Names names;
    const Node* current = unparen(node);
    while (current != nullptr && current->kind == Kind::Member) {
        if (names.count == names.items.size()) {
            return path;
        }
        if (current->has(flag::kComputed)) {
            std::optional<std::string> key = stringValue(current->value);
            if (!key) {
                return path;
            }
            if (path.owned == nullptr) {
                path.owned = std::make_unique<std::deque<std::string>>();
            }
            names.items[names.count++] = path.owned->emplace_back(std::move(*key));
        } else {
            names.items[names.count++] = current->value->text;
        }
        current = unparen(current->target);
    }
    if (current == nullptr) {
        return path;
    }
    if (current->kind == Kind::Identifier) {
        path.rootName = current->text;
    } else if (current->kind != Kind::This && current->kind != Kind::Super) {
        return path;
    }
    path.root = current->kind;
    std::reverse(names.items.begin(), names.items.begin() + static_cast<std::ptrdiff_t>(names.count));
    path.names = names;
    path.ok = true;
    return path;
}

struct Resolved {
    bool known = false;
    const ApiMember* member = nullptr;
    std::string shape;
    std::string owner;
    std::string callee;
    std::string object;
    Lazy keyName;
    Lazy control;
    Lazy variable;
    Lazy custom;
    Lazy page;
    Lazy prop;
    Lazy propKind;
    Lazy field;
    const LibraryFunction* library = nullptr;
    bool self = false;
    bool superCall = false;
};

bool pointParam(const TypeRef& type)
{
    if (type == nullptr || type->kind != TypeNode::Kind::Union) {
        return false;
    }
    bool point = false;
    bool number = false;
    for (const TypeRef& item : type->items) {
        point = point || isName(item, "PicturePoint");
        number = number || isName(item, "number");
    }
    return point && number;
}

bool numberType(const TypeParam& param)
{
    return isName(withoutEmpty(param.type), "number");
}

bool undefinedNode(const Node* node)
{
    node = unparen(node);
    return node != nullptr && node->kind == Kind::Identifier && node->text == "undefined";
}

const LibraryFunction* importedFromLibrary(const Environment& environment, const Binding& binding, std::string_view name)
{
    if (environment.libraryImport.empty() || binding.owner == nullptr || binding.owner->kind != Kind::Import) {
        return nullptr;
    }
    const std::optional<std::string> from = stringValue(binding.owner->value);
    const std::string_view wanted = environment.libraryImport;
    if (!from || (*from != wanted && *from != wanted.substr(0, wanted.size() - 3))) {
        return nullptr;
    }
    const auto found = std::ranges::find(environment.library, name, &LibraryFunction::name);
    return found != environment.library.end() ? &*found : nullptr;
}

bool durationName(std::string_view name)
{
    return name.ends_with("Ms") || name == "ms";
}

std::string_view trimmedView(std::string_view text)
{
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

struct VariableParts {
    std::string container;
    std::string element;
    std::string keyKind = "text";
};

VariableParts splitAnnotation(std::string_view annotation)
{
    VariableParts made;
    const std::string_view text = trimmedView(annotation);
    if (text.ends_with("[]")) {
        made.container = "list";
        made.element = std::string(trimmedView(text.substr(0, text.size() - 2)));
        return made;
    }
    if (text.starts_with("Map<") && text.ends_with(">")) {
        made.container = "dict";
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
        made.keyKind = trimmedView(inner.substr(0, at)) == "number" ? "number" : "text";
        made.element = std::string(trimmedView(inner.substr(std::min(at + 1, inner.size()))));
        return made;
    }
    if (text.starts_with("Set<") && text.ends_with(">")) {
        made.container = "set";
        made.element = std::string(trimmedView(text.substr(4, text.size() - 5)));
        return made;
    }
    if (text.starts_with("Array<") && text.ends_with(">")) {
        made.container = "list";
        made.element = std::string(trimmedView(text.substr(6, text.size() - 7)));
        return made;
    }
    made.element = std::string(text);
    return made;
}

void inferVariableKind(VariableInfo& info, const Node* value)
{
    info.container.clear();
    info.kind = "text";
    info.type = "string";
    const Node* node = value != nullptr ? unparen(value) : nullptr;
    if (node == nullptr) {
        return;
    }
    if (node->kind == Kind::True || node->kind == Kind::False) {
        info.kind = "toggle";
        info.type = "boolean";
        return;
    }
    if (node->kind == Kind::Number) {
        info.kind = node->text.find('.') == std::string_view::npos ? "integer" : "decimal";
        info.type = "number";
        return;
    }
    if (node->kind == Kind::String) {
        return;
    }
    if (node->kind == Kind::Array) {
        info.container = "list";
        return;
    }
    if (node->kind == Kind::New) {
        const Node* callee = unparen(node->target);
        if (callee != nullptr && callee->kind == Kind::Identifier && callee->text == "Map") {
            info.container = "dict";
        } else if (callee != nullptr && callee->kind == Kind::Identifier && callee->text == "Set") {
            info.container = "set";
        }
        return;
    }
    if (node->kind == Kind::Object) {
        info.kind = "point";
        info.type = "PicturePoint";
    }
}

VariableInfo describeVariable(const Source& source, const Api& api, const Node* declaration, const Node* declarator,
                              std::string page, bool inClass, char quote)
{
    VariableInfo info;
    info.page = std::move(page);
    info.inClass = inClass;
    info.declaration = declaration;
    info.declarator = declarator;
    if (declaration != nullptr) {
        info.constant = declaration->text == "const";
    }
    const Node* id = declarator != nullptr ? declarator->target : nullptr;
    if (id == nullptr || id->kind != Kind::Identifier) {
        return info;
    }
    info.id = id;
    info.name = std::string(id->text);
    const Node* value = declarator->value;
    const std::uint32_t to = value != nullptr ? value->start : declarator->end;
    bool annotated = false;
    if (to > id->end && to <= source.text.size()) {
        const std::string_view between(source.text.data() + id->end, to - id->end);
        const std::size_t colon = between.find(':');
        if (colon != std::string_view::npos) {
            std::string_view rest = between.substr(colon + 1);
            const std::size_t equals = rest.find('=');
            if (equals != std::string_view::npos) {
                rest = rest.substr(0, equals);
            }
            const VariableParts parts = splitAnnotation(rest);
            info.container = parts.container;
            info.keyKind = parts.keyKind;
            if (!parts.element.empty()) {
                std::string kind = variableKindOfName(parts.element);
                if (kind == "class" && api.interfaces.contains(parts.element)
                    && api.derives(parts.element, "Component")) {
                    kind = "component";
                }
                info.kind = kind;
                info.type = parts.element;
                annotated = true;
            }
        }
    }
    const VariableDoc doc = declaration != nullptr
                              ? parseVariableDoc(source.text, source.tree.comments, declaration->start)
                              : VariableDoc{};
    info.category = doc.category;
    info.description = doc.description;
    info.editable = doc.editable;
    if (!annotated) {
        inferVariableKind(info, value);
    }
    if (info.kind == "integer") {
        info.kind = doc.duration ? "duration" : doc.decimal ? "decimal" : "integer";
    }
    if (value != nullptr) {
        info.initial = std::string(trimmedView(source.text.substr(value->start, value->end - value->start)));
    } else {
        info.initial = variableDefault(info.kind, info.type, info.container, info.keyKind, quote);
    }
    if (doc.keep || std::string_view(info.initial).find("store.get(") != std::string_view::npos) {
        info.keep = true;
        const std::string_view initial(info.initial);
        const std::size_t at = initial.rfind("??");
        if (at != std::string_view::npos) {
            info.initial = std::string(trimmedView(initial.substr(at + 2)));
        }
    }
    info.start = declaration != nullptr ? declaration->start : 0;
    info.end = declaration != nullptr ? declaration->end : 0;
    return info;
}

bool numeric(const Node* node)
{
    node = unparen(node);
    if (node == nullptr) {
        return false;
    }
    if (node->kind == Kind::Number) {
        return true;
    }
    if (node->kind == Kind::Unary && node->text == "-") {
        return numeric(node->target);
    }
    return node->kind == Kind::Binary && node->text != "+";
}

const Node* property(const Node* object, std::string_view name)
{
    object = unparen(object);
    if (object == nullptr || object->kind != Kind::Object) {
        return nullptr;
    }
    for (const Node* item : object->items) {
        if (item->kind != Kind::Property || item->has(flag::kSpread) || item->has(flag::kComputed)) {
            continue;
        }
        const Node* key = item->target;
        const std::string keyName = key->kind == Kind::String ? unquote(key->text) : std::string(key->text);
        if (keyName == name) {
            return item;
        }
    }
    return nullptr;
}

const Node* propertyValue(const Node* object, std::string_view name)
{
    const Node* found = property(object, name);
    return found == nullptr ? nullptr : found->value;
}

bool pointExpression(const Node* node)
{
    node = unparen(node);
    if (node == nullptr) {
        return false;
    }
    if (node->kind == Kind::Object && node->items.size() == 2 && numberValue(propertyValue(node, "x"))
        && numberValue(propertyValue(node, "y"))) {
        return true;
    }
    if (node->kind != Kind::Call || node->target == nullptr) {
        return false;
    }
    const Node* callee = unparen(node->target);
    if (callee == nullptr) {
        return false;
    }
    if (callee->kind == Kind::Identifier && callee->text == "point" && node->items.size() == 2) {
        return numberValue(node->items[0]) && numberValue(node->items[1]);
    }
    if (callee->kind != Kind::Member || node->items.size() != 1) {
        return false;
    }
    const Path path = pathOf(callee);
    const std::optional<std::string> name = stringValue(unparen(node->items[0]));
    return name.has_value() && path.ok && path.root == Kind::Identifier && path.rootName == "res"
        && path.names.size() == 1 && path.names[0] == "point";
}

struct Fusion {
    std::string_view callee;
    std::string_view title;
    std::string_view found;
    std::string_view missing;
};

constexpr Fusion kFusions[] = {
    {"vision.find", "node:find", "pin:found", "pin:notFound"},
    {"vision.waitFor", "node:waitFor", "pin:appeared", "pin:timedOut"},
};

const Fusion* fusionOf(std::string_view callee)
{
    for (const Fusion& fusion : kFusions) {
        if (fusion.callee == callee) {
            return &fusion;
        }
    }
    return nullptr;
}

struct ObjectShape {
    const StructInfo* info = nullptr;
    std::size_t fields = 0;
};

ObjectShape structOfObject(const Document::Impl& d, const Node* object)
{
    object = unparen(object);
    ObjectShape best;
    if (object == nullptr || object->kind != Kind::Object || object->items.empty()) {
        return best;
    }
    for (const StructInfo& info : d.types.structs) {
        bool ok = true;
        std::size_t matched = 0;
        for (const Node* entry : object->items) {
            if (entry->kind != Kind::Property || entry->has(flag::kSpread) || entry->has(flag::kComputed)
                || entry->target == nullptr) {
                ok = false;
                break;
            }
            const std::string key = entry->target->kind == Kind::String ? unquote(entry->target->text)
                                                                        : std::string(entry->target->text);
            if (fieldNamed(info, key) == nullptr) {
                ok = false;
                break;
            }
            ++matched;
        }
        if (!ok) {
            continue;
        }
        for (const StructFieldInfo& field : info.fields) {
            if (!field.optional && property(object, field.name) == nullptr) {
                ok = false;
                break;
            }
        }
        if (ok && (best.info == nullptr || matched > best.fields)) {
            best.info = &info;
            best.fields = matched;
        }
    }
    return best;
}

const EnumInfo* enumOfValue(const Document::Impl& d, const Node* value)
{
    const Path path = pathOf(value);
    if (path.ok && path.root == Kind::Identifier && path.names.size() == 1) {
        const EnumInfo* info = enumNamed(d.types, path.rootName);
        if (info != nullptr && std::ranges::find(info->options, path.names[0]) != info->options.end()) {
            return info;
        }
    }
    if (const std::optional<std::string> text = stringValue(value)) {
        for (const EnumInfo& info : d.types.enums) {
            if (std::ranges::find(info.options, *text) != info.options.end()) {
                return &info;
            }
        }
    }
    return nullptr;
}

const EnumInfo* enumOfSwitch(const Document::Impl& d, const Node* statement)
{
    if (const EnumInfo* info = enumOfValue(d, statement->test)) {
        return info;
    }
    for (const Node* caseNode : statement->items) {
        if (caseNode->kind == Kind::Case && caseNode->test != nullptr) {
            if (const EnumInfo* info = enumOfValue(d, caseNode->test)) {
                return info;
            }
        }
    }
    return nullptr;
}

std::string optionOfCase(const Node* caseNode)
{
    const Path path = pathOf(caseNode->test);
    if (path.ok && path.names.size() == 1) {
        return std::string(path.names[0]);
    }
    if (const std::optional<std::string> text = stringValue(caseNode->test)) {
        return *text;
    }
    return {};
}

const ClassInfo* classOfNew(const Document::Impl& d, const Node* expression)
{
    expression = unparen(expression);
    if (expression == nullptr || expression->kind != Kind::New) {
        return nullptr;
    }
    const Path path = pathOf(expression->target);
    if (!path.ok || path.root != Kind::Identifier || !path.names.empty()) {
        return nullptr;
    }
    const auto found = std::ranges::find(d.classes, path.rootName, &ClassInfo::name);
    return found == d.classes.end() ? nullptr : &*found;
}

bool declaresEnum(const Document::Impl& d, const Node* statement)
{
    return statement != nullptr && statement->kind == Kind::VarDecl
        && std::ranges::any_of(d.types.enums, [statement](const EnumInfo& info) {
               return statement->start >= info.start && statement->start < info.end;
           });
}

const ClassInfo* classOfCast(const Document::Impl& d, const Node* test)
{
    test = unparen(test);
    if (test == nullptr || test->kind != Kind::Binary || test->text != "instanceof") {
        return nullptr;
    }
    const Path path = pathOf(test->value);
    if (!path.ok || path.root != Kind::Identifier || !path.names.empty()) {
        return nullptr;
    }
    const auto found = std::ranges::find(d.classes, path.rootName, &ClassInfo::name);
    return found == d.classes.end() ? nullptr : &*found;
}

const ClassInfo* classOfObjectItem(const Document::Impl& d, const Item* item)
{
    if (item == nullptr || item->typeName.empty()) {
        return nullptr;
    }
    const auto found = std::ranges::find(d.classes, item->typeName, &ClassInfo::name);
    return found == d.classes.end() ? nullptr : &*found;
}

bool extensionSpace(std::string_view space) noexcept
{
    return space.starts_with("ext.") || space.starts_with("ext[");
}

Literal groupIcon(std::string_view space)
{
    if (space == "touch") {
        return "mouse-pointer-click";
    }
    if (space == "vision") {
        return "search";
    }
    if (space == "device" || space == "input") {
        return "smartphone";
    }
    if (space == "ui" || space == "hints") {
        return "panel-top";
    }
    if (space == "log") {
        return "terminal";
    }
    if (space == "math") {
        return "sliders-horizontal";
    }
    if (space == "text") {
        return "type";
    }
    if (space == "list" || space == "dict") {
        return "layers";
    }
    if (space == "set") {
        return "circle-check";
    }
    if (space == "mapping" || space == "profile") {
        return "layout-grid";
    }
    if (space == "store") {
        return "layers";
    }
    if (space == "time") {
        return "timer";
    }
    if (extensionSpace(space)) {
        return "plug";
    }
    return "zap";
}

Literal controlIcon(std::string_view shape)
{
    if (shape == "Tap") {
        return "mouse-pointer-click";
    }
    if (shape == "Turbo") {
        return "zap";
    }
    if (shape == "Wasd" || shape == "PadStick" || shape == "RightWalk" || shape == "Stick") {
        return "joystick-ring";
    }
    if (shape == "Camera" || shape == "PadCamera" || shape == "FreeLook" || shape == "Look") {
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
    if (shape == "Key") {
        return "keyboard";
    }
    if (shape == "Variable") {
        return "sliders-horizontal";
    }
    if (shape == "Globals") {
        return "layers";
    }
    return "pointer";
}

Literal toneOf(std::string_view label)
{
    if (label.starts_with("vision.")) {
        return "vision";
    }
    if (label == "sleep" || label == "nextFrame" || label == "waitUntil" || label == "withTimeout" || label == "retry") {
        return "wait";
    }
    if (label == "parallel" || label == "race" || label == "doOnce" || label == "resetOnce" || label == "gate"
        || label == "flipFlop" || label == "sequence" || label == "multiGate" || label == "doN"
        || label == "resetDoN") {
        return "flow";
    }
    return "action";
}

bool terminal(Role role) noexcept
{
    return role == Role::Break || role == Role::Continue || role == Role::Return || role == Role::Goto
        || role == Role::Finish;
}

bool structure(Role role) noexcept
{
    return role == Role::Branch || role == Role::Fused || role == Role::ForEach || role == Role::Repeat
        || role == Role::While || role == Role::Switch || role == Role::Sequence || role == Role::MultiGate
        || role == Role::ForRange || role == Role::Job;
}

bool repeatShape(const Node* loop)
{
    const Node* init = loop->target;
    if (init == nullptr || init->kind != Kind::VarDecl || init->text != "let" || init->items.size() != 1) {
        return false;
    }
    const Node* declarator = init->items[0];
    if (declarator->target->kind != Kind::Identifier || !numberValue(declarator->value)
        || *numberValue(declarator->value) != 0) {
        return false;
    }
    const std::string_view name = declarator->target->text;
    const Node* test = unparen(loop->test);
    if (test == nullptr || test->kind != Kind::Binary || test->text != "<" || unparen(test->target)->kind != Kind::Identifier
        || unparen(test->target)->text != name) {
        return false;
    }
    const Node* update = unparen(loop->other);
    return update != nullptr && update->kind == Kind::Update && update->text == "++"
        && unparen(update->target)->kind == Kind::Identifier && unparen(update->target)->text == name;
}

bool forOfShape(const Node* loop)
{
    const Node* left = loop->target;
    return !loop->has(flag::kAwait) && left != nullptr && left->kind == Kind::VarDecl && left->items.size() == 1
        && left->items[0]->target->kind == Kind::Identifier && left->items[0]->value == nullptr;
}

bool rangeShape(const Node* loop)
{
    const Node* init = loop->target;
    if (init == nullptr || init->kind != Kind::VarDecl || init->text != "let" || init->items.size() != 1) {
        return false;
    }
    const Node* declarator = init->items[0];
    if (declarator->target->kind != Kind::Identifier || !numberValue(declarator->value)) {
        return false;
    }
    const std::string_view name = declarator->target->text;
    const Node* test = unparen(loop->test);
    if (test == nullptr || test->kind != Kind::Binary || (test->text != "<" && test->text != "<=")) {
        return false;
    }
    const Node* counted = unparen(test->target);
    if (counted == nullptr || counted->kind != Kind::Identifier || counted->text != name) {
        return false;
    }
    const Node* update = unparen(loop->other);
    return update != nullptr && update->kind == Kind::Update && update->text == "++" && unparen(update->target) != nullptr
        && unparen(update->target)->kind == Kind::Identifier && unparen(update->target)->text == name;
}

bool entriesShape(const Node* loop)
{
    const Node* left = loop->target;
    if (loop->has(flag::kAwait) || left == nullptr || left->kind != Kind::VarDecl || left->items.size() != 1
        || left->items[0]->value != nullptr) {
        return false;
    }
    const Node* pattern = left->items[0]->target;
    if (pattern == nullptr || pattern->kind != Kind::ArrayPattern || pattern->items.size() != 2) {
        return false;
    }
    for (const Node* each : pattern->items) {
        if (each == nullptr || each->kind != Kind::Identifier) {
            return false;
        }
    }
    const Node* value = unparen(loop->value);
    if (value == nullptr || value->kind != Kind::Call || !value->items.empty()) {
        return false;
    }
    const Node* callee = unparen(value->target);
    return callee != nullptr && callee->kind == Kind::Member && callee->value != nullptr && callee->value->text == "entries";
}

struct Link {
    Item* from = nullptr;
    std::size_t fromPin = kNone;
    Item* to = nullptr;
    std::size_t toPin = kNone;
    bool dashed = false;
};

std::size_t pinIndex(const std::vector<Pin>& pins, std::string_view name)
{
    for (std::size_t i = pins.size(); i > 0; --i) {
        if (pins[i - 1].name == name) {
            return i - 1;
        }
    }
    return kNone;
}

using Ordinals = std::map<std::string, int, std::less<>>;

class Builder
{
public:
    Builder(Document::Impl& impl, Source& source, std::vector<Link>& links)
        : d(impl), api(impl.api()), m_source(source), m_links(links)
    {
    }

    void file();
    void floating(const Page& page, const FloatingCode& code);
    void broken(const Page& page, const FloatingCode& code);

    const ClassInfo* m_class = nullptr;

private:
    Document::Impl& d;
    const Api& api;
    Source& m_source;
    std::vector<Link>& m_links;
    Ordinals m_events;
    mutable std::string m_scratch;
    mutable std::map<std::string, std::string, std::less<>> m_keyTitles;
    mutable std::vector<Choice> m_choices;
    mutable std::map<std::string, Resolved, std::less<>> m_resolved;
    mutable std::deque<Resolved> m_computed;
    std::string m_state;
    std::string m_stateParam;
    std::map<const Chain*, std::size_t> m_fans;

    [[nodiscard]] const std::string& say(std::string_view key) const { return d.say(key); }
    [[nodiscard]] std::string text(const Node* node) const { return d.text(m_source, node); }

    Item& make(std::string key, Role role, Literal kind, std::string page);
    Chain& chain(Shape shape, const Node* owner, std::vector<const Node*> statements, Item* head, std::string pin,
                 const Node* function, bool async, bool loop, std::string prefix, std::string page);
    Chain* body(Item& head, std::string pin, const Node* function, const Node* bodyNode, bool loop, const Chain* outer,
                const Node* parent);
    void setBranch(Item& item, std::string_view pin, Chain* target);
    void note(Item& item, const Node* first, const Node* last);
    void finish(Item& item, const Node* first, const Node* last);
    void link(Item* from, std::string_view fromPin, Item* to, std::string_view toPin, bool dashed = false)
    {
        if (from == nullptr || to == nullptr) {
            return;
        }
        m_links.push_back({from, pinIndex(from->outputs, fromPin), to, pinIndex(to->inputs, toPin), dashed});
    }

    [[nodiscard]] const Resolved& resolve(const Node* node) const;
    [[nodiscard]] Resolved compute(const Node* node) const;
    [[nodiscard]] Resolved walkShape(Resolved resolved, const Path& path, std::size_t from) const;
    [[nodiscard]] bool settable(const Node* target) const;
    [[nodiscard]] bool codeStatement(const Chain& target, std::size_t at) const;
    [[nodiscard]] bool registration(const Node* statement) const;

    PinType typeOf(const TypeRef& type, std::string_view name, std::vector<Choice>* choices = nullptr) const;
    void express(Pin& pin, PinType type) const;
    Pin& addInput(Item& item, std::string name, std::string label, PinType type, bool optional) const;
    static Pin& execIn(Item& item);
    static Pin& execOut(Item& item, std::string name = "exec", std::string label = {}, bool optional = false);
    void code(Pin& pin, const Node* expression) const;
    static void pointValue(Pin& pin, double x, double y);
    void literal(Pin& pin, const Node* expression) const;
    Item* pure(std::string key, std::string page, Item* consumer, Pin* pin, const Node* expression, const Chain* context);
    void valueOf(Item& item, Pin& pin, const Node* expression, const Chain* context);
    void apiPins(Item& item, const Node* call, const ApiMember& member, const Chain* context,
                 std::span<const std::size_t> skipped);
    void customPins(Item& item, const Node* call, const Node* declaration, const Chain* context);
    void argumentPins(Item& item, const Node* call, std::span<const LibraryParam> params, const Chain* context);
    void genericPins(Item& item, const Node* call) const;
    void callOutput(Item& item, const ApiMember* member, bool waited, const Node* declarator);
    void produce(Item& item, const Node* id, std::string label, PinType type);

    void fill(Chain& target);
    void fanOut(Chain& target, std::size_t at);
    Item& statementBase(Chain& target, std::size_t index, Role role, std::string token, Literal kind,
                        Ordinals& ordinals);
    Item* statementItem(Chain& target, std::size_t& index, Ordinals& ordinals);
    Item* callItem(Chain& target, std::size_t index, const Node* statement, const Node* call, bool waited,
                   const Node* declarator, Ordinals& ordinals);
    Item* setItem(Chain& target, std::size_t index, const Node* statement, const Node* assigned, const Node* value,
                  const Node* declarator, const Node* owner, Ordinals& ordinals);
    Item* fusedItem(Chain& target, std::size_t index, const Node* statement, const Node* call, const Node* declarator,
                    const Fusion& fusion, const ApiMember& member, const std::string& callee, Ordinals& ordinals);
    Item* newItem(Chain& target, std::size_t index, const Node* statement, const Node* call, const ClassInfo& info,
                  const Node* declarator, Ordinals& ordinals);
    Item* structureItem(Chain& target, std::size_t index, const Node* statement, Ordinals& ordinals);
    Item* switchItem(Chain& target, std::size_t index, const Node* statement, Ordinals& ordinals);
    Chain* caseBody(Item& head, const std::string& pin, const Node* each, const Chain& outer);
    [[nodiscard]] bool workCall(const Node* call, std::string_view name) const;
    Item* workItem(Chain& target, std::size_t index, const Node* statement, const Node* call, Role role, Ordinals& ordinals);
    [[nodiscard]] const Node* jobOf(const Node* call) const;
    Item* jobItem(Chain& target, std::size_t index, const Node* statement, const Node* call, const Node* job,
                  Ordinals& ordinals);
    Item* transitionItem(Chain& target, std::size_t index, const Node* statement, Ordinals& ordinals);
    [[nodiscard]] bool machineCall(const Node* call) const;
    Item* machineItem(Chain& target, std::size_t index, const Node* statement, const Node* call,
                      const Node* declarator, Ordinals& ordinals);
    void machinePage(Item& machineNode, const Node* call);
    Item& codeItem(Chain& target, std::size_t first, std::size_t last, Ordinals& ordinals);

    bool event(const Node* statement, const std::string& prefix);
    void handlerOutputs(Item& item, const Node* handler, const TypeNode* type, bool several);
    Item& synthetic(Role role, const std::string& key, std::string title, Literal icon, std::string badge,
                    const Node* function, const Node* owner);
    void functionPage(const Node* function, const std::string& name, const std::string& title, const Node* owner);
    void classPages(const Node* classNode);
    void registrationScope(const std::vector<const Node*>& statements, const std::string& prefix,
                           std::vector<const Node*>& rest, bool topLevel, std::vector<const Node*>& events);
};

Item& Builder::make(std::string key, Role role, Literal kind, std::string page)
{
    Item& item = d.items.emplace_back();
    item.serial = d.items.size() - 1;
    item.key = std::move(key);
    item.role = role;
    item.kind = kind;
    item.page = std::move(page);
    item.source = &m_source;
    item.floating = !m_source.id.empty();
    if (role == Role::Pure) {
        item.outputs.reserve(1);
    } else {
        item.inputs.reserve(4);
        item.outputs.reserve(3);
    }
    d.byKey[item.key] = &item;
    if (Page* target = d.page(item.page)) {
        target->items.push_back(&item);
    }
    return item;
}

Chain& Builder::chain(Shape shape, const Node* owner, std::vector<const Node*> statements, Item* head, std::string pin,
                      const Node* function, bool async, bool loop, std::string prefix, std::string page)
{
    Chain& made = d.chains.emplace_back();
    made.source = &m_source;
    made.shape = shape;
    made.owner = owner;
    made.statements = std::move(statements);
    made.head = head;
    made.pin = std::move(pin);
    made.function = function;
    made.async = async;
    made.loop = loop;
    made.prefix = std::move(prefix);
    made.page = std::move(page);
    return made;
}

Chain* Builder::body(Item& head, std::string pin, const Node* function, const Node* bodyNode, bool loop,
                     const Chain* outer, const Node* parent)
{
    const bool async = function != nullptr ? function->has(flag::kAsync) : (outer != nullptr && outer->async);
    const Node* scope = function != nullptr ? function : (outer != nullptr ? outer->function : nullptr);
    const bool inLoop = loop || (function == nullptr && outer != nullptr && outer->loop);
    std::string prefix = head.key + "/" + pin;
    Chain* made = nullptr;
    if (bodyNode == nullptr) {
        made = &chain(Shape::Missing, nullptr, {}, &head, pin, scope, async, inLoop, std::move(prefix), head.page);
    } else if (function != nullptr && function->has(flag::kExpressionBody)) {
        made = &chain(Shape::Expression, function, {bodyNode}, &head, pin, scope, async, inLoop, std::move(prefix),
                      head.page);
    } else if (bodyNode->kind == Kind::Block) {
        std::vector<const Node*> statements(bodyNode->items.begin(), bodyNode->items.end());
        made = &chain(Shape::Braced, bodyNode, std::move(statements), &head, pin, scope, async, inLoop,
                      std::move(prefix), head.page);
    } else {
        made = &chain(Shape::Single, bodyNode, {bodyNode}, &head, pin, scope, async, inLoop, std::move(prefix), head.page);
    }
    made->parent = parent;
    made->state = outer != nullptr ? outer->state : m_state;
    setBranch(head, made->pin, made);
    fill(*made);
    return made;
}

void Builder::setBranch(Item& item, std::string_view pin, Chain* target)
{
    for (Pin& output : item.outputs) {
        if (output.name == pin) {
            output.branch = target;
            return;
        }
    }
}

void Builder::note(Item& item, const Node* first, const Node* last)
{
    const std::vector<syntax::Comment>& comments = m_source.tree.comments;
    const std::string& source = m_source.text;
    const auto adjacentAbove = [&](std::vector<syntax::Comment>::const_iterator it, std::uint32_t boundary) {
        if (it->end > boundary) {
            return false;
        }
        const std::string_view gap(source.data() + it->end, boundary - it->end);
        if (gap.find_first_not_of(" \t\r\n") != std::string_view::npos || std::ranges::count(gap, '\n') > 1) {
            return false;
        }
        std::size_t lineStart = it->start;
        while (lineStart > 0 && source[lineStart - 1] != '\n' && source[lineStart - 1] != '\r') {
            --lineStart;
        }
        const std::string_view before(source.data() + lineStart, it->start - lineStart);
        return before.find_first_not_of(" \t") == std::string_view::npos;
    };
    const auto below = std::ranges::lower_bound(comments, first->start, {}, &syntax::Comment::start);
    const auto after = std::ranges::lower_bound(comments, last->end, {}, &syntax::Comment::start);
    bool trailing = false;
    if (after != comments.end() && after->start >= last->end) {
        const std::string_view gap(source.data() + last->end, after->start - last->end);
        trailing = gap.find_first_not_of(" \t;,") == std::string_view::npos;
    }
    bool above = false;
    auto it = below;
    while (it != comments.begin()) {
        --it;
        if (it->end > first->start) {
            continue;
        }
        above = adjacentAbove(it, first->start);
        break;
    }
    if (!above && !trailing) {
        return;
    }
    std::vector<std::string> parts;
    if (above) {
        it = below;
        std::uint32_t boundary = first->start;
        std::vector<std::string> lines;
        while (it != comments.begin()) {
            --it;
            if (it->end > boundary) {
                continue;
            }
            if (!adjacentAbove(it, boundary)) {
                break;
            }
            lines.push_back(noteText(std::string_view(source.data() + it->start, it->end - it->start)));
            boundary = it->start;
        }
        std::ranges::reverse(lines);
        for (std::string& line : lines) {
            if (!line.empty()) {
                parts.push_back(std::move(line));
            }
        }
    }
    if (trailing) {
        std::string text = noteText(std::string_view(source.data() + after->start, after->end - after->start));
        if (!text.empty()) {
            parts.push_back(std::move(text));
        }
    }
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            item.note += "\n";
        }
        item.note += parts[i];
    }
}

void Builder::finish(Item& item, const Node* first, const Node* last)
{
    item.statement = first;
    item.last = last;
    if (first == nullptr) {
        return;
    }
    if (!m_source.tree.comments.empty()) {
        note(item, first, last);
    }
    item.sigStart = first->start;
    item.sigEnd = last->end;
}

Resolved Builder::walkShape(Resolved resolved, const Path& path, std::size_t from) const
{
    for (std::size_t i = from; i < path.names.size(); ++i) {
        const ApiMember* member = api.member(resolved.shape, path.names[i]);
        if (member == nullptr) {
            resolved.known = false;
            resolved.member = nullptr;
            return resolved;
        }
        if (i + 1 == path.names.size()) {
            resolved.member = member;
            resolved.callee = join(resolved.object, ".", path.names[i]);
            return resolved;
        }
        const TypeRef type = withoutEmpty(member->type);
        if (!member->property || type == nullptr || type->kind != TypeNode::Kind::Name
            || !api.interfaces.contains(type->name)) {
            resolved.known = false;
            resolved.member = nullptr;
            return resolved;
        }
        const std::string& said = d.say.optional(member->label);
        const std::string& label = said.empty() ? member->name : said;
        resolved.owner = resolved.self && i == from ? label
                       : resolved.owner.empty()      ? label
                                                     : join(resolved.owner, kDot, label);
        resolved.self = false;
        resolved.object.append(".").append(path.names[i]);
        resolved.shape = type->name;
    }
    resolved.callee = resolved.object;
    return resolved;
}

const Resolved& Builder::resolve(const Node* node) const
{
    const Node* target = unparen(node);
    const Node* root = target;
    while (root != nullptr && root->kind == Kind::Member) {
        root = unparen(root->target);
    }
    if (root != nullptr && root->kind == Kind::Identifier) {
        const Binding* binding = m_source.scopes->lookup(root->text, node->start);
        if (binding == nullptr || binding->kind == BindingKind::Import) {
            const std::string_view key = std::string_view(m_source.javascript).substr(target->start, target->end - target->start);
            if (const auto found = m_resolved.find(key); found != m_resolved.end()) {
                return found->second;
            }
            return m_resolved.emplace(std::string(key), compute(node)).first->second;
        }
    }
    return m_computed.emplace_back(compute(node));
}

Resolved Builder::compute(const Node* node) const
{
    Resolved resolved;
    const Path path = pathOf(node);
    if (!path.ok) {
        return resolved;
    }
    if (path.root == Kind::Super) {
        if (path.names.size() == 1 && m_class != nullptr) {
            resolved.known = true;
            resolved.superCall = true;
            resolved.callee = join("super.", path.names[0]);
            const std::string_view base = api.interfaces.contains(m_class->base) ? std::string_view(m_class->base)
                                        : m_class->task                         ? std::string_view("Task")
                                        : m_class->component                    ? std::string_view("Component")
                                                                                : std::string_view();
            if (!base.empty()) {
                resolved.member = api.member(base, path.names[0]);
            }
        }
        return resolved;
    }
    if (path.root == Kind::This) {
        if (m_class == nullptr || path.names.empty()) {
            return resolved;
        }
        resolved.object = "this";
        const std::string_view first = path.names[0];
        if (first == "props" && path.names.size() >= 2) {
            const std::string_view prop = path.names[1];
            resolved.known = true;
            resolved.prop = prop;
            if (const auto kind = m_class->props.find(prop); kind != m_class->props.end()) {
                resolved.propKind = kind->second;
            }
            const auto label = m_class->propLabels.find(prop);
            resolved.owner = label != m_class->propLabels.end() && !label->second.empty() ? label->second
                                                                                          : std::string(prop);
            resolved.object = join("this.props.", prop);
            if (path.names.size() == 2) {
                resolved.callee = resolved.object;
                return resolved;
            }
            resolved.shape = resolved.propKind == "key" ? "Key" : resolved.propKind == "component" ? "Component" : "";
            if (resolved.shape.empty()) {
                resolved.known = false;
                return resolved;
            }
            resolved.prop.clear();
            return walkShape(std::move(resolved), path, 2);
        }
        if (path.names.size() == 1 && std::ranges::find(m_class->methods, first) != m_class->methods.end()) {
            resolved.known = true;
            resolved.custom = first;
            resolved.page = join("fn:", m_class->name, ".", first);
            resolved.callee = join("this.", first);
            return resolved;
        }
        if (std::ranges::find(m_class->events, first) != m_class->events.end()) {
            resolved.known = true;
            resolved.field = first;
            resolved.owner = first;
            resolved.object = join("this.", first);
            resolved.shape = "ComponentEvent";
            if (path.names.size() == 1) {
                resolved.callee = resolved.object;
                return resolved;
            }
            resolved.field.clear();
            return walkShape(std::move(resolved), path, 1);
        }
        if (std::ranges::find(m_class->fields, first) != m_class->fields.end()) {
            if (path.names.size() == 1) {
                resolved.known = true;
                resolved.field = first;
                resolved.owner = first;
                resolved.callee = join("this.", first);
            }
            return resolved;
        }
        resolved.known = true;
        resolved.self = true;
        resolved.shape = api.interfaces.contains(m_class->base) ? m_class->base : m_class->task ? "Task" : "Component";
        resolved.owner = say("node:self");
        return walkShape(std::move(resolved), path, 0);
    }
    const std::string_view root = path.rootName;
    const Binding* binding = m_source.scopes->lookup(root, node->start);
    const Binding* imported = binding;
    if (imported == nullptr && &m_source != &d.file() && d.file().scopes != nullptr && d.file().tree.program != nullptr) {
        imported = d.file().scopes->lookup(root, d.file().tree.program->end);
    }
    if (imported != nullptr && imported->kind == BindingKind::Import && path.names.empty()) {
        if (const LibraryFunction* function = importedFromLibrary(d.environment, *imported, root)) {
            resolved.known = true;
            resolved.custom = root;
            resolved.page = "file:" + std::string(kLibraryFile);
            resolved.callee = root;
            resolved.library = function;
            return resolved;
        }
    }
    if (binding != nullptr && binding->kind != BindingKind::Import) {
        if (path.names.empty() && binding->kind == BindingKind::Function && d.functions.contains(root)) {
            resolved.known = true;
            resolved.custom = root;
            resolved.page = join("fn:", root);
            resolved.callee = root;
        }
        return resolved;
    }
    if (path.names.empty()) {
        if (const ApiMember* global = api.global(root)) {
            resolved.known = true;
            resolved.member = global;
            resolved.callee = root;
        } else if (d.functions.contains(root)) {
            resolved.known = true;
            resolved.custom = root;
            resolved.page = join("fn:", root);
            resolved.callee = root;
        }
        return resolved;
    }
    if (root == "ext" && path.names.size() >= 2) {
        std::string space(root);
        for (std::size_t i = 0; i + 1 < path.names.size(); ++i) {
            const std::string_view part = path.names[i];
            space += identifierText(part) ? join(".", part) : join("[\"", part, "\"]");
        }
        if (const ApiMember* member = api.function(space, path.names.back())) {
            resolved.known = true;
            resolved.member = member;
            resolved.callee = join(space, ".", path.names.back());
        }
        return resolved;
    }
    const bool known = root == "keys" || root == "controls" || root == "vars";
    if (!known && api.space(root) != nullptr) {
        if (path.names.size() == 1) {
            if (const ApiMember* member = api.function(root, path.names[0])) {
                resolved.known = true;
                resolved.member = member;
                resolved.callee = join(root, ".", path.names[0]);
            }
        }
        return resolved;
    }
    if (root == "keys") {
        resolved.known = true;
        resolved.keyName = path.names[0];
        resolved.shape = "Key";
        resolved.object = join("keys.", path.names[0]);
        auto title = m_keyTitles.find(path.names[0]);
        if (title == m_keyTitles.end()) {
            title = m_keyTitles.emplace(std::string(path.names[0]), d.say.format("node:key", d.say.key(path.names[0]))).first;
        }
        resolved.owner = title->second;
        if (path.names.size() == 1) {
            resolved.callee = resolved.object;
            return resolved;
        }
        return walkShape(std::move(resolved), path, 1);
    }
    if (root == "controls") {
        const ControlEntry* entry = d.control(path.names[0]);
        resolved.known = true;
        resolved.control = path.names[0];
        resolved.shape = entry != nullptr && api.interfaces.contains(entry->type) ? entry->type : "Component";
        resolved.owner = entry != nullptr && !entry->name.empty() ? entry->name : std::string(path.names[0]);
        resolved.object = join("controls.", path.names[0]);
        if (path.names.size() == 1) {
            resolved.callee = resolved.object;
            return resolved;
        }
        return walkShape(std::move(resolved), path, 1);
    }
    if (root == "vars") {
        const VariableEntry* entry = d.variable(path.names[0]);
        resolved.known = true;
        resolved.variable = path.names[0];
        resolved.shape = "Variable";
        resolved.owner = entry != nullptr && !entry->name.empty() ? entry->name : std::string(path.names[0]);
        resolved.object = join("vars.", path.names[0]);
        if (path.names.size() == 1) {
            resolved.callee = resolved.object;
            return resolved;
        }
        return walkShape(std::move(resolved), path, 1);
    }
    if (root == "globals") {
        resolved.known = true;
        resolved.shape = "Globals";
        resolved.owner = say("node:globals");
        resolved.object = "globals";
        return walkShape(std::move(resolved), path, 0);
    }
    return resolved;
}

bool Builder::settable(const Node* target) const
{
    target = unparen(target);
    if (target == nullptr) {
        return false;
    }
    if (target->kind == Kind::Identifier) {
        return true;
    }
    if (target->kind != Kind::Member) {
        return false;
    }
    const Path path = pathOf(target);
    if (!path.ok) {
        return false;
    }
    if (path.root == Kind::Identifier && path.rootName == "vars" && path.names.size() == 2 && path.names[1] == "value") {
        return true;
    }
    if (path.root == Kind::This && path.names.size() == 1 && m_class != nullptr) {
        return true;
    }
    const Resolved& resolved = resolve(target);
    return resolved.member != nullptr && resolved.member->property && !resolved.member->readonly;
}

bool Builder::registration(const Node* statement) const
{
    if (statement->kind != Kind::ExpressionStatement) {
        return false;
    }
    const Node* expression = unparen(statement->target);
    if (expression == nullptr || expression->kind != Kind::Call) {
        return false;
    }
    const Resolved& resolved = resolve(expression->target);
    return resolved.member != nullptr && isEventMember(api, *resolved.member);
}

bool Builder::codeStatement(const Chain& target, std::size_t at) const
{
    const Node* statement = target.statements[at];
    if (target.shape == Shape::Expression) {
        const Node* expression = unparen(statement);
        if (expression->kind == Kind::Await) {
            expression = unparen(expression->target);
        }
        return !(expression->kind == Kind::Call
                 || (expression->kind == Kind::Assign && expression->text == "=" && settable(expression->target)));
    }
    if (statement->kind == Kind::Export && statement->target != nullptr) {
        statement = statement->target;
    }
    switch (statement->kind) {
    case Kind::ExpressionStatement: {
        const Node* expression = unparen(statement->target);
        if (expression->kind == Kind::Await) {
            expression = unparen(expression->target);
        }
        if (expression->kind == Kind::Call) {
            return false;
        }
        return !(expression->kind == Kind::Assign && expression->text == "=" && settable(expression->target));
    }
    case Kind::VarDecl:
        return !(statement->items.size() == 1 && statement->items[0]->target->kind == Kind::Identifier);
    case Kind::If:
    case Kind::While:
    case Kind::Return:
        return false;
    case Kind::For:
        return !repeatShape(statement) && !rangeShape(statement);
    case Kind::ForOf:
        return !forOfShape(statement) && !entriesShape(statement);
    case Kind::Switch:
        return false;
    case Kind::Break:
    case Kind::Continue:
        return statement->target != nullptr;
    default:
        return true;
    }
}

PinType Builder::typeOf(const TypeRef& type, std::string_view name, std::vector<Choice>* choices) const
{
    TypeRef current = withoutEmpty(type);
    for (int depth = 0; depth < 8 && current != nullptr; ++depth) {
        switch (current->kind) {
        case TypeNode::Kind::Name: {
            const std::string& typeName = current->name;
            if (typeName == "number") {
                return durationName(name) ? PinType::Duration : PinType::Number;
            }
            if (typeName == "boolean") {
                return PinType::Bool;
            }
            if (typeName == "string") {
                return PinType::Text;
            }
            if (typeName == "InputName") {
                return PinType::Key;
            }
            if (typeName == "PicturePoint" || typeName == "Found") {
                return PinType::Point;
            }
            if (typeName == "Region" || typeName == "RegionName") {
                return PinType::Region;
            }
            if (typeName == "TemplateName" || typeName == "ImagePath") {
                return PinType::Image;
            }
            if (typeName == "ColorName" || typeName == "HexColor") {
                return PinType::Color;
            }
            if (typeName == "unknown" || typeName == "any") {
                return PinType::Any;
            }
            if (typeName == "Array" || typeName == "ReadonlyArray") {
                return PinType::List;
            }
            if (api.interfaces.contains(typeName) && api.derives(typeName, "Component")) {
                return PinType::Component;
            }
            if (structNamed(d.types, typeName) != nullptr) {
                return PinType::Struct;
            }
            if (enumNamed(d.types, typeName) != nullptr) {
                return PinType::Enum;
            }
            if (std::ranges::find(d.classes, typeName, &ClassInfo::name) != d.classes.end()) {
                return PinType::Object;
            }
            const TypeRef resolved = api.resolve(current);
            if (resolved == current) {
                return PinType::Any;
            }
            current = withoutEmpty(resolved);
            continue;
        }
        case TypeNode::Kind::Union: {
            bool literals = true;
            for (const TypeRef& item : current->items) {
                literals = literals && item != nullptr && item->kind == TypeNode::Kind::Literal && !item->name.empty()
                        && (item->name.front() == '"' || item->name.front() == '\'');
            }
            if (literals) {
                if (choices != nullptr) {
                    for (const TypeRef& item : current->items) {
                        const std::string value = unquote(item->name);
                        const std::string shown = d.say.optional("choice:" + value);
                        choices->push_back({value, shown.empty() ? value : shown});
                    }
                }
                return PinType::Text;
            }
            PinType common = PinType::Any;
            bool first = true;
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
                const PinType one = typeOf(item, name);
                if (first) {
                    common = one;
                    first = false;
                } else if (common != one) {
                    common = PinType::Any;
                }
            }
            return common;
        }
        case TypeNode::Kind::Array:
        case TypeNode::Kind::Tuple:
            return PinType::List;
        case TypeNode::Kind::Literal:
            return current->name.empty() || current->name.front() == '"' || current->name.front() == '\''
                     ? PinType::Text
                     : PinType::Number;
        case TypeNode::Kind::Object: {
            const auto has = [&current](std::string_view field) {
                return std::ranges::any_of(current->props, [field](const TypeProp& prop) { return prop.name == field; });
            };
            if (has("x") && has("y")) {
                return has("width") ? PinType::Region : PinType::Point;
            }
            return PinType::Any;
        }
        case TypeNode::Kind::Intersection:
        case TypeNode::Kind::Template:
        case TypeNode::Kind::Keyof:
        case TypeNode::Kind::Function:
        case TypeNode::Kind::Unknown:
            return PinType::Any;
        }
    }
    return PinType::Any;
}

void Builder::express(Pin& pin, PinType type) const
{
    switch (type) {
    case PinType::Number:
        pin.editor = "number";
        break;
    case PinType::Duration:
        pin.editor = "duration";
        pin.unit = say("unit:s");
        pin.min = 0;
        break;
    case PinType::Text:
    case PinType::Color:
        pin.editor = pin.choices.empty() ? "text" : "choice";
        break;
    case PinType::Bool:
        pin.editor = "toggle";
        break;
    case PinType::Point:
        pin.editor = "point";
        if (pin.choices.empty()) {
            pin.choices = d.shared(PinType::Point);
        }
        break;
    case PinType::Region:
        pin.editor = "region";
        if (pin.choices.empty()) {
            pin.choices = d.shared(PinType::Region);
        }
        break;
    case PinType::Image:
        pin.editor = "image";
        if (pin.choices.empty()) {
            pin.choices = d.shared(PinType::Image);
        }
        break;
    case PinType::Component:
        pin.editor = "component";
        if (pin.choices.empty()) {
            pin.choices = d.shared(PinType::Component);
        }
        break;
    case PinType::Key:
        pin.editor = "key";
        break;
    case PinType::Exec:
    case PinType::List:
    case PinType::Any:
    case PinType::Transition:
    case PinType::Struct:
    case PinType::Enum:
    case PinType::Object:
        pin.editor.clear();
        break;
    }
}

Pin& Builder::addInput(Item& item, std::string name, std::string label, PinType type, bool optional) const
{
    Pin& pin = item.inputs.emplace_back();
    pin.name = std::move(name);
    pin.label = std::move(label);
    pin.type = type;
    pin.optional = optional;
    express(pin, type);
    return pin;
}

Pin& Builder::execIn(Item& item)
{
    Pin& pin = item.inputs.emplace_back();
    pin.name = "exec";
    pin.type = PinType::Exec;
    return pin;
}

Pin& Builder::execOut(Item& item, std::string name, std::string label, bool optional)
{
    Pin& pin = item.outputs.emplace_back();
    pin.name = std::move(name);
    pin.label = std::move(label);
    pin.type = PinType::Exec;
    pin.optional = optional;
    return pin;
}

void Builder::code(Pin& pin, const Node* expression) const
{
    pin.editor = "code";
    pin.value = d.text(m_source, expression);
    pin.raw = pin.value;
}

void Builder::pointValue(Pin& pin, double x, double y)
{
    pin.raw = {{"x", x}, {"y", y}};
    pin.value = percentText(x) + ", " + percentText(y);
}

void Builder::literal(Pin& pin, const Node* expression) const
{
    const Node* node = unparen(expression);
    if (const std::optional<double> number = numberValue(node)) {
        pin.raw = *number;
        pin.value = pin.type == PinType::Duration ? d.say.seconds(*number) : numberText(*number);
        if (pin.type == PinType::Any) {
            pin.editor = "number";
        }
        return;
    }
    if (node->kind == Kind::True || node->kind == Kind::False) {
        pin.raw = node->kind == Kind::True;
        pin.value = say(node->kind == Kind::True ? "value:true" : "value:false");
        if (pin.type == PinType::Any) {
            pin.editor = "toggle";
        }
        return;
    }
    if (const std::optional<std::string> textValue = stringValue(node)) {
        pin.raw = *textValue;
        pin.value = *textValue;
        if (pin.type == PinType::Key) {
            pin.value = d.say.key(*textValue);
        } else if (pin.type == PinType::Image) {
            std::string name = *textValue;
            if (name.starts_with("res/")) {
                name = name.substr(4);
            }
            pin.value = name;
            for (const ImageEntry& image : d.environment.images) {
                if (image.name == name || image.name == *textValue) {
                    pin.preview = image.preview;
                }
            }
        } else if (!pin.choices.empty()) {
            for (const Choice& choice : pin.choices) {
                if (choice.value == *textValue) {
                    pin.value = choice.text;
                }
            }
        } else if (pin.type == PinType::Any) {
            pin.editor = "text";
        }
        return;
    }
    code(pin, expression);
}

Item* Builder::pure(std::string key, std::string page, Item* consumer, Pin* pin, const Node* expression,
                    const Chain* context)
{
    const Node* node = unparen(expression);
    if (node == nullptr) {
        return nullptr;
    }
    const auto created = [&](std::string title, Literal icon, PinType type, std::string apiName) -> Item& {
        Item& item = make(std::move(key), Role::Pure, "pure", std::move(page));
        item.title = std::move(title);
        item.icon = icon;
        item.tone = "pure";
        item.expression = node;
        item.api = std::move(apiName);
        item.token = "pure";
        item.sigStart = node->start;
        item.sigEnd = node->end;
        item.chain = consumer != nullptr ? consumer->chain : nullptr;
        item.async = consumer != nullptr && consumer->async;
        Pin& out = item.outputs.emplace_back();
        out.name = "value";
        out.type = type;
        if (consumer != nullptr && pin != nullptr) {
            item.consumer = consumer;
            item.consumerPin = pin->name;
            pin->connected = true;
            link(&item, "value", consumer, pin->name, type == PinType::Component);
        }
        return item;
    };
    const auto operand = [&](Item& item, std::string name, std::string label, PinType type, const Node* value,
                             bool condition) {
        Pin& input = addInput(item, std::move(name), std::move(label), type, false);
        input.slot = {.kind = SlotKind::Expression, .node = value, .owner = node, .condition = condition};
        valueOf(item, input, value, context);
    };
    if (node->kind == Kind::Binary || node->kind == Kind::Logical
        || (node->kind == Kind::Unary && (node->text == "!" || node->text == "-")) || node->kind == Kind::Conditional) {
        std::string op(node->text);
        PinType out = PinType::Any;
        PinType operandType = PinType::Any;
        if (node->kind == Kind::Conditional) {
            op = "?:";
        } else if (node->kind == Kind::Unary) {
            op = node->text == "!" ? "!" : "neg";
            out = node->text == "!" ? PinType::Bool : PinType::Number;
            operandType = node->text == "!" ? PinType::Any : PinType::Number;
        } else if (op == "-" || op == "*" || op == "/" || op == "%" || op == "**") {
            out = PinType::Number;
            operandType = PinType::Number;
        } else if (op == "<" || op == ">" || op == "<=" || op == ">=") {
            out = PinType::Bool;
            operandType = PinType::Number;
        } else if (op == "==" || op == "!=" || op == "===" || op == "!==" || op == "&&" || op == "||") {
            out = PinType::Bool;
        } else if (op == "+") {
            out = numeric(node->target) || numeric(node->value) ? PinType::Number : PinType::Any;
        }
        Item& item = created(say("op:" + op), "", out, "op:" + op);
        item.inputs.reserve(3);
        if (node->kind == Kind::Conditional) {
            operand(item, "condition", say("pin:condition"), PinType::Bool, node->test, true);
            operand(item, "a", say("pin:true"), PinType::Any, node->body, false);
            operand(item, "b", say("pin:false"), PinType::Any, node->other, false);
        } else if (node->kind == Kind::Unary) {
            operand(item, "a", "", operandType, node->target, false);
        } else {
            operand(item, "a", "", operandType, node->target, false);
            operand(item, "b", "", operandType, node->value, false);
        }
        return &item;
    }
    if (node->kind == Kind::Object) {
        const ObjectShape shape = structOfObject(d, node);
        if (shape.info != nullptr) {
            Item& item = created(d.say.format("node:make", shape.info->name), "square", PinType::Struct,
                                 "make:" + shape.info->name);
            item.token = "make:" + shape.info->name;
            item.typeName = shape.info->name;
            item.callee = "make:" + shape.info->name;
            for (const StructFieldInfo& field : shape.info->fields) {
                const Node* given = propertyValue(node, field.name);
                Pin& input = addInput(item, field.name, field.name, typeOfName(d, field.type),
                                      given == nullptr || field.optional);
                if (given == nullptr) {
                    continue;
                }
                input.slot = {.kind = SlotKind::Expression, .node = given, .owner = node};
                valueOf(item, input, given, context);
            }
            return &item;
        }
    }
    if (node->kind == Kind::Identifier) {
        const Binding* binding = m_source.scopes->lookup(node->text, node->start);
        if (binding == nullptr || binding->kind == BindingKind::Import || binding->kind == BindingKind::Function
            || binding->kind == BindingKind::Class) {
            return nullptr;
        }
        Item& item = created(d.say.format("node:get", node->text), "", PinType::Any, "get");
        item.local = node->text;
        return &item;
    }
    if (node->kind == Kind::Member) {
        const Path path = pathOf(node);
        if (!path.ok) {
            return nullptr;
        }
        if (path.root == Kind::Identifier && path.rootName == "controls" && path.names.size() == 1) {
            const ControlEntry* entry = d.control(path.names[0]);
            Item& item = created(entry != nullptr && !entry->name.empty() ? entry->name : std::string(path.names[0]),
                                 controlIcon(entry != nullptr ? entry->type : ""), PinType::Component, "controls");
            item.callee = join("controls.", path.names[0]);
            return &item;
        }
        if (path.root == Kind::Identifier && path.rootName == "vars" && path.names.size() == 2
            && path.names[1] == "value") {
            const VariableEntry* entry = d.variable(path.names[0]);
            Item& item = created(d.say.format("node:varGet", entry != nullptr && !entry->name.empty()
                                                                 ? std::string_view(entry->name)
                                                                 : path.names[0]),
                                 "sliders-horizontal", PinType::Any, "Variable.value");
            item.callee = join("vars.", path.names[0]);
            return &item;
        }
        if (path.root == Kind::Identifier && path.names.size() == 1) {
            const EnumInfo* picked = enumNamed(d.types, path.rootName);
            if (picked != nullptr && std::ranges::find(picked->options, path.names[0]) != picked->options.end()) {
                Item& item = created(picked->name + std::string(kDot) + std::string(path.names[0]), "layers",
                                     PinType::Enum, "enum:" + picked->name);
                item.token = "enum:" + picked->name;
                item.typeName = picked->name;
                item.callee = join(path.rootName, ".", path.names[0]);
                return &item;
            }
            const Binding* binding = m_source.scopes->lookup(path.rootName, node->start);
            const auto found = binding != nullptr ? d.producers.find(binding) : d.producers.end();
            if (binding != nullptr && found != d.producers.end()) {
                const Item* producer = found->second.first;
                if (const StructInfo* shape = structNamed(d.types, producer->typeName); shape != nullptr) {
                    if (const StructFieldInfo* field = fieldNamed(*shape, path.names[0]); field != nullptr) {
                        Item& item = created(d.say.format("node:breakStruct", shape->name) + std::string(kDot)
                                                 + std::string(field->name),
                                             "square", typeOfName(d, field->type), "break:" + shape->name);
                        item.token = "break:" + shape->name;
                        item.typeName = shape->name;
                        item.callee = join(path.rootName, ".", path.names[0]);
                        Pin& input = addInput(item, "value", shape->name, PinType::Struct, false);
                        input.slot = {.kind = SlotKind::Expression, .node = node->target, .owner = node};
                        valueOf(item, input, node->target, context);
                        return &item;
                    }
                } else if (const ClassInfo* type = classOfObjectItem(d, producer); type != nullptr) {
                    if (std::ranges::find(type->fields, path.names[0]) != type->fields.end()) {
                        Item& item = created(type->name + std::string(kDot) + std::string(path.names[0]),
                                             "sliders-horizontal", PinType::Any, "member:" + type->name);
                        item.token = "member:" + type->name;
                        item.typeName = type->name;
                        item.callee = join(path.rootName, ".", path.names[0]);
                        Pin& input = addInput(item, "object", d.say("pin:object"), PinType::Object, false);
                        input.slot = {.kind = SlotKind::Expression, .node = node->target, .owner = node};
                        valueOf(item, input, node->target, context);
                        return &item;
                    }
                }
            }
        }
        const Resolved& resolved = resolve(node);
        if (!resolved.known) {
            return nullptr;
        }
        if (resolved.member != nullptr && resolved.member->property) {
            std::string label = d.say.optional(resolved.member->label);
            if (label.empty()) {
                label = resolved.member->name;
            }
            Item& item = created(resolved.owner.empty() ? label : resolved.owner + std::string(kDot) + label,
                                 controlIcon(resolved.shape), typeOf(resolved.member->type, resolved.member->name),
                                 resolved.member->label);
            item.callee = resolved.callee;
            return &item;
        }
        if (!resolved.prop.empty() || !resolved.field.empty()) {
            const PinType type = resolved.propKind == "position"    ? PinType::Point
                               : resolved.propKind == "number"      ? PinType::Number
                               : resolved.propKind == "text"        ? PinType::Text
                               : resolved.propKind == "toggle"      ? PinType::Bool
                               : resolved.propKind == "key"         ? PinType::Key
                               : resolved.propKind == "component"   ? PinType::Component
                                                                    : PinType::Any;
            Item& item = created(resolved.owner, "pin", type, resolved.prop.empty() ? "this.field" : "this.props");
            item.callee = resolved.callee;
            return &item;
        }
        return nullptr;
    }
    if (node->kind == Kind::Call) {
        const Resolved& resolved = resolve(node->target);
        if (resolved.member != nullptr && !resolved.member->property && !awaitable(resolved.member->returns)
            && !isName(resolved.member->returns, "void") && !isEventMember(api, *resolved.member)) {
            std::string label = d.say.optional(resolved.member->label);
            if (label.empty()) {
                label = resolved.owner.empty() ? resolved.member->label : resolved.member->name;
            }
            Item& item = created(resolved.owner.empty() ? label : resolved.owner + std::string(kDot) + label,
                                 groupIcon(resolved.member->owner), typeOf(resolved.member->returns, ""),
                                 resolved.member->label);
            item.callee = resolved.callee;
            item.apiMember = resolved.member;
            item.inputs.reserve(resolved.member->params.size() + 1);
            apiPins(item, node, *resolved.member, context, {});
            return &item;
        }
        if (!resolved.custom.empty()) {
            Item& item = created(resolved.custom, "puzzle", PinType::Any, "custom");
            item.target = resolved.page;
            item.callee = resolved.callee;
            if (resolved.library != nullptr) {
                item.badge = say("badge:library");
                argumentPins(item, node, resolved.library->params, context);
            } else {
                const auto declaration = d.functions.find(resolved.custom.view());
                customPins(item, node, declaration != d.functions.end() ? declaration->second : nullptr, context);
            }
            return &item;
        }
    }
    return nullptr;
}

void Builder::valueOf(Item& item, Pin& pin, const Node* expression, const Chain* context)
{
    const Node* node = unparen(expression);
    if (node == nullptr) {
        return;
    }
    if (node->kind == Kind::Identifier && node->text == "undefined") {
        return;
    }
    if (node->kind == Kind::Null) {
        pin.raw = nullptr;
        return;
    }
    if (numberValue(node) || stringValue(node) || node->kind == Kind::True || node->kind == Kind::False) {
        literal(pin, node);
        return;
    }
    if (pin.type == PinType::Point || pin.type == PinType::Any) {
        if (node->kind == Kind::Object && node->items.size() == 2) {
            const std::optional<double> x = numberValue(propertyValue(node, "x"));
            const std::optional<double> y = numberValue(propertyValue(node, "y"));
            if (x && y) {
                pointValue(pin, *x, *y);
                if (pin.type == PinType::Any) {
                    pin.editor = "point";
                }
                return;
            }
        }
        const Node* callee = node->kind == Kind::Call ? unparen(node->target) : nullptr;
        if (callee != nullptr && node->items.size() == 2 && callee->kind == Kind::Identifier && callee->text == "point") {
            const std::optional<double> x = numberValue(node->items[0]);
            const std::optional<double> y = numberValue(node->items[1]);
            if (x && y) {
                pointValue(pin, *x, *y);
                return;
            }
        }
        if (callee != nullptr && node->items.size() == 1 && callee->kind == Kind::Member) {
            const Path path = pathOf(callee);
            const std::optional<std::string> name = stringValue(unparen(node->items[0]));
            if (name && path.ok && path.root == Kind::Identifier && path.rootName == "res" && path.names.size() == 1
                && path.names[0] == "point") {
                pin.raw = *name;
                pin.value = *name;
                if (pin.type == PinType::Any) {
                    pin.editor = "point";
                }
                return;
            }
        }
        if (node->kind == Kind::Member && !d.environment.ownControl.empty()) {
            const Path path = pathOf(node);
            if (path.ok && path.root == Kind::Identifier && path.rootName == "controls" && path.names.size() == 2
                && path.names[0] == d.environment.ownControl && path.names[1] == "position") {
                pin.value = say("pin:ownPosition");
                pin.raw = pin.value;
                return;
            }
        }
    }
    if (pin.type == PinType::Region && node->kind == Kind::Object) {
        const std::optional<double> x = numberValue(propertyValue(node, "x"));
        const std::optional<double> y = numberValue(propertyValue(node, "y"));
        const std::optional<double> w = numberValue(propertyValue(node, "width"));
        const std::optional<double> h = numberValue(propertyValue(node, "height"));
        if (x && y && w && h) {
            pin.raw = {{"x", *x}, {"y", *y}, {"w", *w}, {"h", *h}};
            pin.value = percentText(*x) + ", " + percentText(*y) + std::string(kDot) + percentText(*w) + " \xC3\x97 "
                      + percentText(*h);
            return;
        }
    }
    if (node->kind == Kind::Member) {
        const Path path = pathOf(node);
        if (path.ok && path.root == Kind::Identifier && path.rootName == "res" && path.names.size() == 1
            && (pin.type == PinType::Image || pin.type == PinType::Any)) {
            pin.raw = path.names[0];
            pin.value = path.names[0];
            for (const ImageEntry& image : d.environment.images) {
                if (image.name == path.names[0]) {
                    pin.preview = image.preview;
                }
            }
            if (pin.type == PinType::Any) {
                pin.editor = "image";
            }
            return;
        }
        if (path.ok && path.root == Kind::Identifier && path.rootName == "controls" && path.names.size() == 1
            && pin.type == PinType::Component) {
            const ControlEntry* entry = d.control(path.names[0]);
            pin.raw = path.names[0];
            pin.value = entry != nullptr && !entry->name.empty() ? entry->name : path.names[0];
            return;
        }
    }
    if (node->kind == Kind::Identifier) {
        if (const Binding* binding = m_source.scopes->lookup(node->text, node->start)) {
            if (const auto found = d.producers.find(binding);
                found != d.producers.end() && found->second.first->page == item.page) {
                pin.connected = true;
                link(found->second.first, found->second.second, &item, pin.name);
                return;
            }
        }
    }
    if (pure(join(item.key, ".", pin.name), item.page, &item, &pin, node, context) != nullptr) {
        return;
    }
    code(pin, node);
}

void Builder::apiPins(Item& item, const Node* call, const ApiMember& member, const Chain* context,
                      std::span<const std::size_t> skipped)
{
    const std::vector<TypeParam>& params = member.params;
    const auto& args = call->items;
    const auto argument = [&args](std::size_t index) -> const Node* {
        return index < args.size() ? args[index] : nullptr;
    };
    const auto labelOf = [&](std::string_view name) -> std::string {
        m_scratch.assign("param:").append(member.label).append(".").append(name);
        if (const std::string& specific = d.say.optional(m_scratch); !specific.empty()) {
            return specific;
        }
        m_scratch.assign("param:").append(name);
        const std::string& general = d.say.optional(m_scratch);
        return general.empty() ? std::string(name) : general;
    };
    const auto pairCode = [&](Pin& pin, std::size_t index) {
        pin.editor = "code";
        const Node* second = argument(index + 1) != nullptr ? argument(index + 1) : argument(index);
        pin.value = d.text(m_source, argument(index)->start, second->end);
        pin.raw = pin.value;
    };
    const auto unfilled = [&item, this](const Pin& pin, const Node* given) {
        const Node* bare = unparen(given);
        if (!pin.optional && item.problem.empty() && bare != nullptr && bare->kind == Kind::Identifier
            && bare->text == "undefined") {
            item.problem = d.say.format("reason:noValue", pin.label);
        }
    };
    std::size_t arg = 0;
    for (std::size_t p = 0; p < params.size(); ++p) {
        const TypeParam& param = params[p];
        if (std::ranges::find(skipped, p) != skipped.end()) {
            ++arg;
            continue;
        }
        if (param.rest) {
            std::size_t count = 0;
            for (; arg < args.size(); ++arg, ++count) {
                const std::string suffix = count > 0 ? std::to_string(count + 1) : std::string();
                Pin& pin = addInput(item, param.name + suffix, labelOf(param.name) + (suffix.empty() ? "" : " " + suffix),
                                    PinType::Any, count > 0);
                pin.slot = {.kind = SlotKind::Rest, .call = call, .index = arg, .node = args[arg]};
                valueOf(item, pin, args[arg], context);
            }
            const std::string suffix = std::to_string(count + 1);
            Pin& extra = addInput(item, param.name + suffix, labelOf(param.name) + " " + suffix, PinType::Any, true);
            extra.slot = {.kind = SlotKind::Rest, .call = call, .index = arg};
            return;
        }
        if (pointParam(param.type) && p + 3 < params.size() && pointParam(params[p + 1].type)
            && numberType(params[p + 2]) && numberType(params[p + 3])) {
            const Node* first = argument(arg);
            const Node* second = argument(arg + 1);
            const Node* third = argument(arg + 2);
            const bool fromPoint = first == nullptr || (!numeric(first) && !numeric(second));
            const bool paired = fromPoint && !(undefinedNode(second) && third != nullptr);
            const bool toPoint = paired || pointExpression(third);
            const std::size_t toIndex = paired ? arg + 1 : arg + 2;
            const std::size_t durationIndex = paired ? arg + 2 : arg + 4;
            const std::size_t span = paired ? 2 : 4;
            for (std::size_t half = 0; half < 2; ++half) {
                const std::string name = half == 0 ? "from" : "to";
                const bool point = half == 0 ? fromPoint : toPoint;
                const std::size_t index = half == 0 ? arg : toIndex;
                Pin& pin = addInput(item, name, labelOf(name), PinType::Point, false);
                pin.slot = {.kind = SlotKind::Argument,
                            .call = call,
                            .index = index,
                            .width = span,
                            .node = argument(index),
                            .pair = !point,
                            .group = arg};
                if (!point && argument(index) != nullptr) {
                    const std::optional<double> x = numberValue(argument(index));
                    const std::optional<double> y = numberValue(argument(index + 1));
                    if (x && y) {
                        pointValue(pin, *x, *y);
                    } else {
                        pairCode(pin, index);
                    }
                } else if (point) {
                    valueOf(item, pin, argument(index), context);
                    unfilled(pin, argument(index));
                }
            }
            const auto used = [&](std::size_t index) {
                if (index == arg || index == durationIndex) {
                    return true;
                }
                if (index == toIndex) {
                    return true;
                }
                return (!fromPoint && index == arg + 1) || (!toPoint && index == toIndex + 1);
            };
            for (std::size_t index = arg; index < args.size(); ++index) {
                if (!used(index) && !undefinedNode(args[index])) {
                    if (item.problem.empty()) {
                        item.problem = d.say("reason:ignoredValue");
                    }
                    break;
                }
            }
            arg = durationIndex;
            p += 3;
            continue;
        }
        if (pointParam(param.type) && p + 1 < params.size() && numberType(params[p + 1])) {
            const Node* first = argument(arg);
            const Node* second = argument(arg + 1);
            const bool points = first == nullptr || !numeric(first);
            const bool pair = !points;
            const bool placeholder = second == nullptr || undefinedNode(second);
            const std::size_t span = !points || placeholder ? 2 : 1;
            const std::size_t durationIndex = !points || placeholder ? arg + 2 : arg + 1;
            Pin& pin = addInput(item, param.name, labelOf(param.name), PinType::Point, param.optional);
            pin.slot = {.kind = SlotKind::Argument,
                        .call = call,
                        .index = arg,
                        .width = span,
                        .node = first,
                        .pair = !points,
                        .group = arg};
            if (pair && first != nullptr) {
                const std::optional<double> x = numberValue(first);
                const std::optional<double> y = numberValue(argument(arg + 1));
                if (x && y) {
                    pointValue(pin, *x, *y);
                } else {
                    pairCode(pin, arg);
                }
            } else if (points) {
                valueOf(item, pin, first, context);
                unfilled(pin, first);
            }
            const auto used = [&](std::size_t index) {
                return (pair && index <= arg + 1) || index == arg || index == durationIndex;
            };
            for (std::size_t index = arg; index < args.size(); ++index) {
                if (!used(index) && !undefinedNode(args[index])) {
                    if (item.problem.empty()) {
                        item.problem = d.say("reason:ignoredValue");
                    }
                    break;
                }
            }
            arg = durationIndex;
            ++p;
            continue;
        }
        const bool function = isFunctionType(api, param.type);
        const TypeRef plain = withoutEmpty(param.type);
        std::optional<std::vector<TypeProp>> props;
        if (param.name == "options" || (plain != nullptr && plain->name.ends_with("Options"))
            || (item.role == Role::Job && p == 0)) {
            props = propsOf(api, param.type);
        }
        const bool options = props && !props->empty() && !function;
        if (options) {
            const Node* object = unparen(argument(arg));
            if (object != nullptr && object->kind != Kind::Object) {
                Pin& pin = addInput(item, param.name, labelOf(param.name), PinType::Any, true);
                pin.slot = {.kind = SlotKind::Argument, .call = call, .index = arg, .node = object};
                code(pin, object);
                ++arg;
                continue;
            }
            for (const TypeProp& prop : *props) {
                if (prop.name == "again") {
                    continue;
                }
                m_choices.clear();
                const PinType type = typeOf(prop.type, prop.name, &m_choices);
                Pin& pin = item.inputs.emplace_back();
                pin.name = join(param.name, ".", prop.name);
                pin.label = labelOf(prop.name);
                pin.type = type;
                pin.optional = item.role != Role::Job || prop.optional;
                pin.choices = d.keep(m_choices);
                express(pin, type);
                pin.fallback = declaredDefault(plain != nullptr ? std::string_view(plain->name) : std::string_view(),
                                               prop.name);
                const Node* entry = property(object, prop.name);
                pin.slot = {.kind = SlotKind::Option,
                            .call = call,
                            .index = arg,
                            .option = std::string_view(prop.name),
                            .node = entry != nullptr ? entry->value : nullptr,
                            .owner = object};
                if (entry != nullptr) {
                    valueOf(item, pin, entry->value, context);
                }
            }
            ++arg;
            continue;
        }
        m_choices.clear();
        const PinType type = function ? PinType::Any : typeOf(param.type, param.name, &m_choices);
        Pin& pin = item.inputs.emplace_back();
        pin.name = param.name;
        pin.label = labelOf(param.name);
        pin.type = type;
        pin.optional = param.optional;
        pin.choices = d.keep(m_choices);
        express(pin, type);
        if (function) {
            pin.editor = "code";
        }
        pin.slot = {.kind = SlotKind::Argument, .call = call, .index = arg, .node = argument(arg)};
        if (argument(arg) != nullptr) {
            if (function) {
                code(pin, argument(arg));
            } else {
                valueOf(item, pin, argument(arg), context);
                unfilled(pin, argument(arg));
            }
        }
        ++arg;
    }
    for (; arg < args.size(); ++arg) {
        Pin& pin = addInput(item, std::to_string(arg), "", PinType::Any, false);
        pin.slot = {.kind = SlotKind::Argument, .call = call, .index = arg, .node = args[arg]};
        code(pin, args[arg]);
    }
}

void Builder::customPins(Item& item, const Node* call, const Node* declaration, const Chain* context)
{
    std::vector<LibraryParam> params;
    if (declaration != nullptr) {
        params.reserve(declaration->items.size());
        for (const Node* param : declaration->items) {
            std::vector<const Node*> names;
            syntax::collectNames(param, names);
            params.push_back({names.empty() ? std::to_string(params.size()) : std::string(names.front()->text),
                              param->kind == Kind::AssignPattern});
        }
    }
    argumentPins(item, call, params, context);
}

void Builder::argumentPins(Item& item, const Node* call, std::span<const LibraryParam> params, const Chain* context)
{
    const std::size_t count = std::max(params.size(), call->items.size());
    for (std::size_t i = 0; i < count; ++i) {
        const std::string name = i < params.size() ? params[i].name : std::to_string(i);
        Pin& pin = addInput(item, name, name, PinType::Any, i < params.size() && params[i].optional);
        pin.slot = {.kind = SlotKind::Argument,
                    .call = call,
                    .index = i,
                    .node = i < call->items.size() ? call->items[i] : nullptr};
        if (i < call->items.size()) {
            valueOf(item, pin, call->items[i], context);
        }
    }
}

void Builder::genericPins(Item& item, const Node* call) const
{
    for (std::size_t i = 0; i < call->items.size(); ++i) {
        Pin& pin = addInput(item, std::to_string(i), "", PinType::Any, false);
        pin.slot = {.kind = SlotKind::Argument, .call = call, .index = i, .node = call->items[i]};
        code(pin, call->items[i]);
    }
}

void Builder::produce(Item& item, const Node* id, std::string label, PinType type)
{
    if (id == nullptr || id->kind != Kind::Identifier) {
        return;
    }
    std::string name(id->text);
    while (std::ranges::any_of(item.outputs, [&name](const Pin& pin) { return pin.name == name; })) {
        name += "'";
    }
    Pin& out = item.outputs.emplace_back();
    out.name = name;
    out.label = label.empty() ? std::string(id->text) : std::move(label);
    out.type = type;
    out.local = std::string(id->text);
    out.binding = m_source.scopes->declared(id);
    if (out.binding != nullptr) {
        d.producers[out.binding] = {&item, name};
    }
}

void Builder::callOutput(Item& item, const ApiMember* member, bool waited, const Node* declarator)
{
    TypeRef returns = member != nullptr ? member->returns : nullptr;
    if (waited) {
        returns = awaited(returns);
    }
    const PinType type = returns != nullptr ? typeOf(returns, "") : PinType::Any;
    if (declarator != nullptr) {
        item.local = std::string(declarator->target->text);
        produce(item, declarator->target, {}, type);
        return;
    }
    if (member == nullptr || returns == nullptr || isName(withoutEmpty(returns), "void")
        || isName(returns, "Subscription")) {
        return;
    }
    Pin& out = item.outputs.emplace_back();
    out.name = "result";
    const std::string label = d.say.optional("param:" + member->label + ".return");
    out.label = label.empty() ? say("pin:result") : label;
    out.type = type;
    out.optional = true;
}

void Builder::fill(Chain& target)
{
    Ordinals ordinals;
    std::size_t index = 0;
    while (index < target.statements.size()) {
        if (Item* item = statementItem(target, index, ordinals)) {
            target.items.push_back(item);
        }
    }
    Item* previous = nullptr;
    for (std::size_t i = 0; i < target.items.size(); ++i) {
        Item* item = target.items[i];
        if (i == 0) {
            if (target.head != nullptr) {
                link(target.head, target.pin, item, "exec");
            }
        } else if (previous != nullptr && !terminal(previous->role)) {
            link(previous, structure(previous->role) ? "then" : "exec", item, "exec");
        }
        previous = item;
    }
    if (const auto fan = m_fans.find(&target); fan != m_fans.end()) {
        const std::size_t at = fan->second;
        m_fans.erase(fan);
        fanOut(target, at);
    }
}

void Builder::fanOut(Chain& target, std::size_t at)
{
    const Node* call = parallelFan(target, at);
    if (call == nullptr) {
        return;
    }
    Item* head = target.items.empty() ? target.head : target.items.back();
    if (head == nullptr || (!target.items.empty() && terminal(head->role))) {
        return;
    }
    const std::string pin = target.items.empty() ? target.pin
                            : structure(head->role) ? std::string("then")
                                                    : std::string("exec");
    if (pin.empty()) {
        return;
    }
    for (std::size_t i = 0; i < call->items.size(); ++i) {
        const Node* arrow = syntax::unparen(call->items[i]);
        std::vector<const Node*> statements(arrow->body->items.begin(), arrow->body->items.end());
        Chain& branch = chain(Shape::Braced, arrow->body, std::move(statements), head, pin, arrow,
                              arrow->has(flag::kAsync), false, head->key + "/" + pin + "#" + std::to_string(i),
                              target.page);
        branch.parent = arrow;
        branch.state = target.state;
        fill(branch);
    }
}

Item& Builder::statementBase(Chain& target, std::size_t index, Role role, std::string token, Literal kind,
                             Ordinals& ordinals)
{
    const int ordinal = ordinals[token]++;
    std::array<char, 16> digits{};
    const auto written = std::to_chars(digits.data(), digits.data() + digits.size(), ordinal);
    Item& item = make(join(target.prefix, "/", token, "#", std::string_view(digits.data(), written.ptr - digits.data())),
                      role, kind, target.page);
    item.token = std::move(token);
    item.chain = &target;
    item.index = index;
    item.count = 1;
    item.async = target.async;
    return item;
}

Item& Builder::codeItem(Chain& target, std::size_t first, std::size_t last, Ordinals& ordinals)
{
    Item& item = statementBase(target, first, Role::Code, "code", "code", ordinals);
    item.count = last - first + 1;
    item.tone = "code";
    item.icon = "code-xml";
    item.title = say("node:code");
    const Node* start = target.statements[first];
    const Node* end = target.statements[last];
    std::size_t lineStart = start->start;
    while (lineStart > 0 && m_source.text[lineStart - 1] != '\n' && m_source.text[lineStart - 1] != '\r') {
        --lineStart;
    }
    const std::string_view prefix(m_source.text.data() + lineStart, start->start - lineStart);
    const bool clean = prefix.find_first_not_of(" \t") == std::string_view::npos;
    item.code = dedent(d.text(m_source, clean ? static_cast<std::uint32_t>(lineStart) : start->start, end->end));
    execIn(item);
    execOut(item);
    finish(item, start, end);
    return item;
}

Item* Builder::callItem(Chain& target, std::size_t index, const Node* statement, const Node* call, bool waited,
                        const Node* declarator, Ordinals& ordinals)
{
    const Resolved& resolved = resolve(call->target);
    if (resolved.superCall) {
        Item& item = statementBase(target, index, Role::Super, "super:" + resolved.callee.substr(6), "flow", ordinals);
        item.tone = "flow";
        item.icon = "chevron-right";
        std::string label = resolved.member != nullptr ? d.say.optional(resolved.member->label) : std::string();
        if (label.empty()) {
            label = resolved.callee.substr(6);
        }
        item.title = d.say.format("node:super", label);
        item.expression = call;
        item.callee = resolved.callee;
        execIn(item);
        execOut(item);
        genericPins(item, call);
        finish(item, statement, statement);
        return &item;
    }
    if (resolved.member != nullptr && !resolved.member->property) {
        const ApiMember& member = *resolved.member;
        const bool wait = member.label == "sleep";
        Item& item = statementBase(target, index, wait ? Role::Wait : Role::Call, "c:" + resolved.callee, "action",
                                   ordinals);
        item.api = member.label;
        item.apiMember = &member;
        item.callee = resolved.callee;
        item.expression = call;
        item.tone = resolved.control.empty() && resolved.keyName.empty() ? toneOf(member.label) : "action";
        item.icon = !resolved.shape.empty() ? controlIcon(resolved.shape)
                  : member.owner.empty()    ? (wait ? "timer" : "zap")
                                            : groupIcon(member.owner);
        std::string label = d.say.optional(member.label);
        if (label.empty()) {
            label = resolved.owner.empty() && !extensionSpace(member.owner) ? member.label : member.name;
        }
        item.title = wait ? say("node:wait") : resolved.owner.empty() ? label : resolved.owner + std::string(kDot) + label;
        execIn(item);
        apiPins(item, call, member, &target, {});
        execOut(item);
        callOutput(item, &member, waited, declarator);
        if (member.owner == "Globals" && !call->items.empty()) {
            if (const std::optional<std::string> name = stringValue(call->items[0])) {
                if (std::ranges::find(d.globalNames, *name) == d.globalNames.end()) {
                    d.globalNames.push_back(*name);
                }
            }
        }
        finish(item, statement, statement);
        return &item;
    }
    if (!resolved.custom.empty()) {
        Item& item = statementBase(target, index, Role::Custom, "c:" + resolved.callee, "action", ordinals);
        item.tone = "custom";
        item.icon = "puzzle";
        item.title = resolved.custom;
        item.target = resolved.page;
        item.callee = resolved.callee;
        item.expression = call;
        execIn(item);
        if (resolved.library != nullptr) {
            item.badge = say("badge:library");
            argumentPins(item, call, resolved.library->params, &target);
        } else {
            const auto declaration = d.functions.find(resolved.custom.view());
            const Node* function = declaration != d.functions.end() ? declaration->second : nullptr;
            if (function == nullptr && m_class != nullptr) {
                for (const Node* member : m_class->node->items) {
                    if (member->kind == Kind::ClassMember && member->target != nullptr
                        && member->target->text == resolved.custom && member->value != nullptr) {
                        function = member->value;
                    }
                }
            }
            customPins(item, call, function, &target);
        }
        execOut(item);
        if (declarator != nullptr) {
            item.local = std::string(declarator->target->text);
            produce(item, declarator->target, {}, PinType::Any);
        }
        finish(item, statement, statement);
        return &item;
    }
    const std::string callee = compact(text(call->target));
    Item& item = statementBase(target, index, Role::Call, "c:" + callee, "action", ordinals);
    item.tone = "action";
    item.icon = "code-xml";
    item.title = callee;
    item.callee = callee;
    item.expression = call;
    execIn(item);
    genericPins(item, call);
    execOut(item);
    if (declarator != nullptr) {
        item.local = std::string(declarator->target->text);
        produce(item, declarator->target, {}, PinType::Any);
    }
    finish(item, statement, statement);
    return &item;
}

Item* Builder::setItem(Chain& target, std::size_t index, const Node* statement, const Node* assigned, const Node* value,
                       const Node* declarator, const Node* owner, Ordinals& ordinals)
{
    const Node* place = unparen(assigned);
    const ObjectShape shape = structOfObject(d, value);
    std::string token;
    std::string title;
    std::string label = say("pin:value");
    PinType type = PinType::Any;
    Literal icon = "pencil";
    if (place->kind == Kind::Identifier) {
        token = join("s:", place->text);
        title = d.say.format("node:set", place->text);
        if (shape.info != nullptr) {
            type = PinType::Struct;
        }
    } else {
        const Path path = pathOf(place);
        if (path.root == Kind::Identifier && path.rootName == "vars" && path.names.size() == 2) {
            const VariableEntry* entry = d.variable(path.names[0]);
            token = join("s:vars.", path.names[0]);
            title = d.say.format("node:setVar", entry != nullptr && !entry->name.empty() ? std::string_view(entry->name)
                                                                                         : path.names[0]);
            icon = "sliders-horizontal";
        } else if (path.root == Kind::This && path.names.size() == 1) {
            token = join("s:this.", path.names[0]);
            title = d.say.format("node:set", path.names[0]);
        } else {
            const Resolved& resolved = resolve(place);
            if (resolved.member == nullptr) {
                return nullptr;
            }
            std::string memberLabel = d.say.optional(resolved.member->label);
            if (memberLabel.empty()) {
                memberLabel = resolved.member->name;
            }
            token = "s:" + resolved.callee;
            title = d.say.format("node:set", resolved.owner.empty() ? memberLabel : join(resolved.owner, kDot, memberLabel));
            label = memberLabel;
            type = typeOf(resolved.member->type, resolved.member->name);
            icon = controlIcon(resolved.shape);
        }
    }
    Item& item = statementBase(target, index, Role::Set, token, "action", ordinals);
    item.tone = "action";
    item.icon = icon;
    item.title = title;
    item.expression = owner;
    item.callee = item.token.substr(2);
    if (shape.info != nullptr) {
        item.typeName = shape.info->name;
    }
    execIn(item);
    Pin& pin = addInput(item, "value", label, type, false);
    if (declarator != nullptr) {
        item.local = std::string(declarator->target->text);
        pin.slot = {.kind = SlotKind::Declaration, .node = value, .owner = declarator};
    } else {
        pin.slot = {.kind = SlotKind::Expression, .node = value, .owner = owner};
    }
    valueOf(item, pin, value, &target);
    execOut(item);
    if (declarator != nullptr && shape.info != nullptr) {
        produce(item, declarator->target, {}, PinType::Struct);
    }
    finish(item, statement, statement);
    return &item;
}

Item* Builder::fusedItem(Chain& target, std::size_t index, const Node* statement, const Node* call,
                         const Node* declarator, const Fusion& fusion, const ApiMember& member, const std::string& callee,
                         Ordinals& ordinals)
{
    const Node* branch = target.statements[index + 1];
    Item& item = statementBase(target, index, Role::Fused, "find:" + callee, "flow", ordinals);
    item.count = 2;
    item.tone = "vision";
    item.icon = "search";
    item.title = say(fusion.title);
    item.api = member.label;
    item.apiMember = &member;
    item.callee = callee;
    item.expression = call;
    item.local = std::string(declarator->target->text);
    execIn(item);
    apiPins(item, call, member, &target, {});
    execOut(item, "found", say(fusion.found));
    execOut(item, "missing", say(fusion.missing));
    execOut(item, "then", say("pin:then"), true);
    produce(item, declarator->target, {}, PinType::Point);
    body(item, "found", nullptr, branch->body, false, &target, branch);
    body(item, "missing", nullptr, branch->other, false, &target, branch);
    finish(item, statement, branch);
    return &item;
}

Item* Builder::newItem(Chain& target, std::size_t index, const Node* statement, const Node* call, const ClassInfo& info,
                       const Node* declarator, Ordinals& ordinals)
{
    Item& item = statementBase(target, index, Role::Call, "new:" + info.name, "action", ordinals);
    item.tone = "action";
    item.icon = "square";
    item.title = d.say.format("node:new", info.name);
    item.typeName = info.name;
    item.expression = call;
    item.callee = "new " + info.name;
    execIn(item);
    if (info.constructor != nullptr) {
        const auto& params = info.constructor->items;
        const auto& args = call->items;
        for (std::size_t p = 0; p < params.size(); ++p) {
            const Node* param = params[p];
            const Node* id = param->kind == Kind::AssignPattern ? param->target : param;
            if (id == nullptr || id->kind != Kind::Identifier) {
                continue;
            }
            const std::string label(id->text);
            Pin& pin = addInput(item, label, label, PinType::Any, p >= args.size());
            if (p < args.size()) {
                pin.slot = {.kind = SlotKind::Argument, .call = call, .index = p, .node = args[p]};
                valueOf(item, pin, args[p], &target);
            }
        }
    }
    execOut(item);
    if (declarator != nullptr && declarator->target != nullptr) {
        item.local = std::string(declarator->target->text);
        produce(item, declarator->target, {}, PinType::Object);
    }
    finish(item, statement, statement);
    return &item;
}

Item* Builder::structureItem(Chain& target, std::size_t index, const Node* statement, Ordinals& ordinals)
{
    switch (statement->kind) {
    case Kind::If: {
        if (const ClassInfo* castTo = classOfCast(d, statement->test)) {
            Item& item = statementBase(target, index, Role::Branch, "cast:" + castTo->name, "flow", ordinals);
            item.tone = "flow";
            item.icon = "square";
            item.title = d.say.format("node:cast", castTo->name);
            item.expression = statement;
            execIn(item);
            const Node* tested = unparen(unparen(statement->test)->target);
            Pin& object = addInput(item, "object", d.say("pin:object"), PinType::Object, false);
            object.slot = {.kind = SlotKind::Expression, .node = tested, .owner = statement->test};
            valueOf(item, object, tested, &target);
            execOut(item, "is", d.say("pin:is"));
            execOut(item, "isNot", d.say("pin:isNot"));
            execOut(item, "then", say("pin:then"), true);
            Pin& value = item.outputs.emplace_back();
            value.name = "value";
            value.label = d.say("pin:object");
            value.type = PinType::Object;
            body(item, "is", nullptr, statement->body, false, &target, statement);
            body(item, "isNot", nullptr, statement->other, false, &target, statement);
            finish(item, statement, statement);
            return &item;
        }
        Item& item = statementBase(target, index, Role::Branch, "if", "flow", ordinals);
        item.tone = "flow";
        item.icon = "workflow";
        item.title = say("node:if");
        execIn(item);
        Pin& condition = addInput(item, "condition", say("pin:condition"), PinType::Bool, false);
        condition.slot = {.kind = SlotKind::Expression, .node = statement->test, .owner = statement, .condition = true};
        valueOf(item, condition, statement->test, &target);
        execOut(item, "true", say("pin:true"));
        execOut(item, "false", say("pin:false"));
        execOut(item, "then", say("pin:then"), true);
        body(item, "true", nullptr, statement->body, false, &target, statement);
        body(item, "false", nullptr, statement->other, false, &target, statement);
        finish(item, statement, statement);
        return &item;
    }
    case Kind::For: {
        const Node* declarator = statement->target->items[0];
        const Node* test = unparen(statement->test);
        if (!repeatShape(statement)) {
            Item& item = statementBase(target, index, Role::ForRange, "forRange", "flow", ordinals);
            item.tone = "flow";
            item.icon = "refresh-cw";
            item.title = say("node:forRange");
            item.badge = test->text == "<" ? "<" : "<=";
            execIn(item);
            Pin& from = addInput(item, "from", say("pin:from"), PinType::Number, false);
            from.slot = {.kind = SlotKind::Expression, .node = declarator->value, .owner = statement->target};
            valueOf(item, from, declarator->value, &target);
            Pin& to = addInput(item, "to", say("pin:to"), PinType::Number, false);
            to.slot = {.kind = SlotKind::Expression, .node = test->value, .owner = test};
            valueOf(item, to, test->value, &target);
            execOut(item, "each", say("pin:everyTime"));
            execOut(item, "then", say("pin:then"), true);
            produce(item, declarator->target, say("pin:index"), PinType::Number);
            body(item, "each", nullptr, statement->body, true, &target, statement);
            finish(item, statement, statement);
            return &item;
        }
        Item& item = statementBase(target, index, Role::Repeat, "repeat", "flow", ordinals);
        item.tone = "flow";
        item.icon = "refresh-cw";
        item.title = say("node:repeat");
        execIn(item);
        Pin& times = addInput(item, "times", say("pin:times"), PinType::Number, false);
        times.slot = {.kind = SlotKind::Expression, .node = test->value, .owner = test};
        valueOf(item, times, test->value, &target);
        execOut(item, "each", say("pin:everyTime"));
        execOut(item, "then", say("pin:then"), true);
        produce(item, declarator->target, say("pin:index"), PinType::Number);
        body(item, "each", nullptr, statement->body, true, &target, statement);
        finish(item, statement, statement);
        return &item;
    }
    case Kind::ForOf: {
        const Node* declarator = statement->target->items[0];
        if (!forOfShape(statement)) {
            const Node* pattern = declarator->target;
            const Node* entries = unparen(statement->value);
            const Node* holder = unparen(entries->target);
            const Node* items = unparen(holder->target);
            Item& item = statementBase(target, index, Role::ForEach, "forEachIndex", "flow", ordinals);
            item.tone = "flow";
            item.icon = "refresh-cw";
            item.title = say("node:forEachIndex");
            execIn(item);
            Pin& list = addInput(item, "list", say("pin:list"), PinType::List, false);
            list.slot = {.kind = SlotKind::Expression, .node = items, .owner = statement};
            valueOf(item, list, items, &target);
            execOut(item, "each", say("pin:each"));
            execOut(item, "then", say("pin:then"), true);
            produce(item, pattern->items[0], say("pin:index"), PinType::Number);
            produce(item, pattern->items[1], say("pin:current"), PinType::Any);
            body(item, "each", nullptr, statement->body, true, &target, statement);
            finish(item, statement, statement);
            return &item;
        }
        Item& item = statementBase(target, index, Role::ForEach, "forOf", "flow", ordinals);
        item.tone = "flow";
        item.icon = "refresh-cw";
        item.title = say("node:forOf");
        execIn(item);
        Pin& list = addInput(item, "list", say("pin:list"), PinType::List, false);
        list.slot = {.kind = SlotKind::Expression, .node = statement->value, .owner = statement};
        valueOf(item, list, statement->value, &target);
        execOut(item, "each", say("pin:each"));
        execOut(item, "then", say("pin:then"), true);
        produce(item, declarator->target, say("pin:current"), PinType::Any);
        body(item, "each", nullptr, statement->body, true, &target, statement);
        finish(item, statement, statement);
        return &item;
    }
    case Kind::While: {
        Item& item = statementBase(target, index, Role::While, "while", "flow", ordinals);
        item.tone = "flow";
        item.icon = "rotate-cw";
        item.title = say("node:while");
        execIn(item);
        Pin& condition = addInput(item, "condition", say("pin:condition"), PinType::Bool, false);
        condition.slot = {.kind = SlotKind::Expression, .node = statement->test, .owner = statement, .condition = true};
        valueOf(item, condition, statement->test, &target);
        execOut(item, "each", say("pin:everyTime"));
        execOut(item, "then", say("pin:then"), true);
        body(item, "each", nullptr, statement->body, true, &target, statement);
        finish(item, statement, statement);
        return &item;
    }
    case Kind::Switch: {
        const EnumInfo* info = enumOfSwitch(d, statement);
        if (info == nullptr) {
            return switchItem(target, index, statement, ordinals);
        }
        Item& item = statementBase(target, index, Role::Fused, "switch:" + info->name, "flow", ordinals);
        item.tone = "flow";
        item.icon = "workflow";
        item.title = d.say.format("node:switchOn", info->name);
        item.typeName = info->name;
        item.expression = statement;
        execIn(item);
        Pin& value = addInput(item, "value", d.say("pin:value"), PinType::Enum, false);
        value.slot = {.kind = SlotKind::Expression, .node = statement->test, .owner = statement};
        valueOf(item, value, statement->test, &target);
        if (const Node* given = unparen(statement->test); given != nullptr && given->kind == Kind::Identifier
            && given->text == "undefined") {
            item.problem = d.say.format("reason:noValue", d.say("pin:value"));
        }
        for (const std::string& option : info->options) {
            execOut(item, "case:" + option, option, true);
        }
        execOut(item, "default", d.say("pin:other"), true);
        for (const Node* caseNode : statement->items) {
            if (caseNode->kind != Kind::Case || caseNode->items.empty()) {
                continue;
            }
            std::vector<const Node*> statements(caseNode->items.begin(), caseNode->items.end());
            if (statements.back()->kind == Kind::Break) {
                statements.pop_back();
            }
            const std::string pin = caseNode->test == nullptr ? "default" : "case:" + optionOfCase(caseNode);
            Chain& made = chain(Shape::Braced, caseNode, std::move(statements), &item, pin, target.function,
                                target.async, target.loop, join(item.key, "/", pin), item.page);
            setBranch(item, pin, &made);
            fill(made);
        }
        finish(item, statement, statement);
        return &item;
    }
    case Kind::Break:
    case Kind::Continue: {
        const bool stop = statement->kind == Kind::Break;
        Item& item = statementBase(target, index, stop ? Role::Break : Role::Continue, stop ? "break" : "continue",
                                   "return", ordinals);
        item.tone = "flow";
        item.icon = stop ? "x" : "chevron-right";
        item.title = say(stop ? "node:break" : "node:continue");
        execIn(item);
        finish(item, statement, statement);
        return &item;
    }
    case Kind::Return: {
        if (!target.state.empty()) {
            if (Item* made = transitionItem(target, index, statement, ordinals)) {
                return made;
            }
        }
        Item& item = statementBase(target, index, Role::Return, "return", "return", ordinals);
        item.tone = "flow";
        item.icon = "arrow-left";
        item.title = say("node:return");
        execIn(item);
        if (statement->value != nullptr) {
            Pin& value = addInput(item, "value", say("pin:value"), PinType::Any, false);
            value.slot = {.kind = SlotKind::Expression, .node = statement->value, .owner = statement};
            valueOf(item, value, statement->value, &target);
        }
        finish(item, statement, statement);
        return &item;
    }
    default:
        return nullptr;
    }
}

Item* Builder::switchItem(Chain& target, std::size_t index, const Node* statement, Ordinals& ordinals)
{
    Item& item = statementBase(target, index, Role::Switch, "switch", "flow", ordinals);
    item.tone = "flow";
    item.icon = "workflow";
    item.title = say("node:switch");
    execIn(item);
    Pin& value = addInput(item, "value", say("pin:value"), PinType::Any, false);
    value.slot = {.kind = SlotKind::Expression, .node = statement->test, .owner = statement};
    valueOf(item, value, statement->test, &target);
    int ordinal = 0;
    for (const Node* each : statement->items) {
        if (each->kind != Kind::Case) {
            continue;
        }
        if (each->test == nullptr) {
            execOut(item, "default", say("pin:default"));
            caseBody(item, "default", each, target);
            continue;
        }
        const std::string pin = join("case:", std::to_string(ordinal++));
        execOut(item, pin, compact(d.text(m_source, each->test)));
        caseBody(item, pin, each, target);
    }
    execOut(item, "then", say("pin:then"), true);
    finish(item, statement, statement);
    return &item;
}

Chain* Builder::caseBody(Item& head, const std::string& pin, const Node* each, const Chain& outer)
{
    Node* block = nullptr;
    if (each->items.size() == 1 && each->items[0]->kind == Kind::Block) {
        block = each->items[0];
    } else {
        std::vector<const Node*> statements;
        for (const Node* statement : each->items) {
            if (statement->kind == Kind::Break && statement->target == nullptr && statement == each->items.back()) {
                continue;
            }
            statements.push_back(statement);
        }
        const std::uint32_t start = statements.empty() ? each->end : statements.front()->start;
        block = m_source.tree.make(Kind::Block, start);
        for (const Node* statement : statements) {
            block->items.push_back(const_cast<Node*>(statement));
        }
        block->end = statements.empty() ? each->end : statements.back()->end;
    }
    return body(head, pin, nullptr, block, false, &outer, each);
}

bool Builder::workCall(const Node* call, std::string_view name) const
{
    if (call == nullptr || call->kind != Kind::Call) {
        return false;
    }
    const Node* callee = unparen(call->target);
    if (callee == nullptr || callee->kind != Kind::Identifier || callee->text != name) {
        return false;
    }
    if (m_source.scopes != nullptr) {
        const Binding* binding = m_source.scopes->lookup(callee->text, callee->start);
        if (binding != nullptr && binding->kind != BindingKind::Import) {
            return false;
        }
    }
    const std::size_t first = name == "multiGate" ? 4 : 0;
    if (call->items.size() <= first) {
        return false;
    }
    for (std::size_t i = first; i < call->items.size(); ++i) {
        if (!syntax::isFunction(unparen(call->items[i]))) {
            return false;
        }
    }
    return true;
}

Item* Builder::workItem(Chain& target, std::size_t index, const Node* statement, const Node* call, Role role,
                        Ordinals& ordinals)
{
    const bool gate = role == Role::MultiGate;
    Item& item = statementBase(target, index, role, gate ? "multiGate" : "sequence", "flow", ordinals);
    item.tone = "flow";
    item.icon = gate ? "dial-cells" : "layers";
    item.title = say(gate ? "node:multiGate" : "node:sequence");
    item.expression = call;
    execIn(item);
    std::size_t first = 0;
    if (gate) {
        static constexpr std::string_view kNames[] = {"name", "times", "loop", "random"};
        static constexpr std::string_view kLabels[] = {"pin:name", "pin:times", "pin:loop", "pin:random"};
        static constexpr PinType kTypes[] = {PinType::Text, PinType::Number, PinType::Bool, PinType::Bool};
        for (std::size_t p = 0; p < std::size(kNames); ++p) {
            const Node* argument = p < call->items.size() ? call->items[p] : nullptr;
            Pin& pin = addInput(item, std::string(kNames[p]), say(kLabels[p]), kTypes[p], false);
            pin.slot = {.kind = SlotKind::Argument, .call = call, .index = p, .node = argument};
            if (argument != nullptr) {
                valueOf(item, pin, argument, &target);
            }
        }
        first = std::size(kNames);
    }
    int ordinal = 0;
    for (std::size_t a = first; a < call->items.size(); ++a) {
        const Node* arrow = unparen(call->items[a]);
        const std::string pin = std::to_string(++ordinal);
        execOut(item, pin, gate && a + 1 == call->items.size() ? say("pin:done") : pin);
        body(item, pin, arrow, arrow->body, false, &target, call);
    }
    execOut(item, "then", say("pin:then"), true);
    finish(item, statement, statement);
    return &item;
}

const Node* Builder::jobOf(const Node* call) const
{
    if (call == nullptr || call->kind != Kind::Call || call->items.size() > 3) {
        return nullptr;
    }
    const Node* callee = unparen(call->target);
    if (callee == nullptr || callee->kind != Kind::Member || callee->has(flag::kComputed) || callee->value == nullptr
        || callee->value->text != "follow") {
        return nullptr;
    }
    const Node* job = unparen(callee->target);
    if (job == nullptr || job->kind != Kind::Call) {
        return nullptr;
    }
    const Resolved& resolved = resolve(job->target);
    if (resolved.member == nullptr || resolved.member->property || !isName(resolved.member->returns, "ExtensionJob")) {
        return nullptr;
    }
    for (const Node* argument : call->items) {
        const Node* bare = unparen(argument);
        if (!syntax::isFunction(bare) && !undefinedNode(bare)) {
            return nullptr;
        }
    }
    return job;
}

Item* Builder::jobItem(Chain& target, std::size_t index, const Node* statement, const Node* call, const Node* job,
                       Ordinals& ordinals)
{
    const Resolved& resolved = resolve(job->target);
    const ApiMember& member = *resolved.member;
    Item& item = statementBase(target, index, Role::Job, "j:" + resolved.callee, "action", ordinals);
    item.tone = "action";
    item.icon = "loader-circle";
    item.title = member.name;
    item.badge = say("badge:job");
    item.api = member.label;
    item.apiMember = &member;
    item.callee = resolved.callee;
    item.expression = call;
    execIn(item);
    apiPins(item, job, member, &target, {});
    const TypeRef returned = member.returns->items.empty() ? nullptr : member.returns->items.front();
    struct Exit {
        std::string_view pin;
        std::array<std::string_view, 2> labels;
        std::array<PinType, 2> types;
    };
    const Exit exits[] = {
        {"progress", {"pin:fraction", "pin:status"}, {PinType::Number, PinType::Text}},
        {"done", {"pin:result", {}}, {returned != nullptr ? typeOf(returned, "") : PinType::Any, PinType::Any}},
        {"failed", {"pin:error", {}}, {PinType::Any, PinType::Any}},
    };
    for (std::size_t a = 0; a < std::size(exits); ++a) {
        const Exit& exit = exits[a];
        const std::string pin(exit.pin);
        const Node* arrow = a < call->items.size() ? unparen(call->items[a]) : nullptr;
        if (arrow == nullptr || !syntax::isFunction(arrow)) {
            continue;
        }
        execOut(item, pin, say(join("pin:", exit.pin)), true);
        for (std::size_t p = 0; p < arrow->items.size() && p < exit.labels.size() && !exit.labels[p].empty(); ++p) {
            const Node* param = arrow->items[p];
            if (param->kind == Kind::AssignPattern) {
                param = param->target;
            }
            produce(item, param, say(exit.labels[p]), exit.types[p]);
        }
        body(item, pin, arrow, arrow->body, false, &target, call);
    }
    execOut(item, "then", say("pin:then"), true);
    finish(item, statement, statement);
    return &item;
}

void collectReturns(const Node* node, std::vector<const Node*>& found)
{
    if (node == nullptr || syntax::isFunction(node) || node->kind == Kind::Class) {
        return;
    }
    if (node->kind == Kind::Return) {
        found.push_back(node);
        return;
    }
    for (const Node* child : {node->target, node->value, node->test, node->body, node->other}) {
        collectReturns(child, found);
    }
    for (const Node* child : node->items) {
        collectReturns(child, found);
    }
}

std::optional<std::string> transitionName(const Node* value, std::string_view param)
{
    value = unparen(value);
    if (param.empty() || value == nullptr || value->kind != Kind::Call) {
        return std::nullopt;
    }
    const Node* callee = unparen(value->target);
    if (callee == nullptr || callee->kind != Kind::Member) {
        return std::nullopt;
    }
    const Node* object = unparen(callee->target);
    if (object == nullptr || object->kind != Kind::Identifier || object->text != param) {
        return std::nullopt;
    }
    if (callee->has(flag::kComputed)) {
        return stringValue(callee->value);
    }
    return std::string(callee->value->text);
}

std::string_view firstParam(const Node* function)
{
    if (function == nullptr || function->items.empty()) {
        return {};
    }
    const Node* param = function->items[0];
    return param->kind == Kind::Identifier ? param->text : std::string_view();
}

bool Builder::machineCall(const Node* call) const
{
    if (call == nullptr || call->kind != Kind::Call || call->items.empty()) {
        return false;
    }
    const Node* callee = unparen(call->target);
    if (callee == nullptr || callee->kind != Kind::Identifier || callee->text != "machine") {
        return false;
    }
    const Node* object = unparen(call->items[0]);
    if (object == nullptr || object->kind != Kind::Object) {
        return false;
    }
    const Binding* binding = m_source.scopes->lookup(callee->text, callee->start);
    return binding == nullptr || binding->kind == BindingKind::Import;
}

Item* Builder::machineItem(Chain& target, std::size_t index, const Node* statement, const Node* call,
                           const Node* declarator, Ordinals& ordinals)
{
    Item& item = statementBase(target, index, Role::Machine, "machine", "machine", ordinals);
    item.tone = "flow";
    item.icon = "workflow";
    const Node* options = call->items.size() > 1 ? unparen(call->items[1]) : nullptr;
    std::string name;
    if (const std::optional<std::string> given = stringValue(propertyValue(options, "name"))) {
        name = *given;
    }
    item.stateName = name;
    item.title = name.empty() ? say("node:machine") : join(say("node:machine"), kDot, name);
    item.expression = call;
    execIn(item);
    execOut(item);
    if (declarator != nullptr && declarator->target->kind == Kind::Identifier) {
        item.local = declarator->target->text;
        produce(item, declarator->target, {}, PinType::Any);
    }
    finish(item, statement, statement);
    if (m_source.id.empty()) {
        machinePage(item, call);
    }
    return &item;
}

void Builder::machinePage(Item& machineNode, const Node* call)
{
    const std::string position = m_source.lines->position(call->start);
    const Node* object = unparen(call->items[0]);
    const Node* options = call->items.size() > 1 ? unparen(call->items[1]) : nullptr;
    Page& page = d.pages.emplace_back();
    page.key = "machine:" + position;
    page.kind = "machine";
    page.title = machineNode.title;
    page.icon = "workflow";
    page.anchor = "m:" + machineNode.key;
    page.parent = machineNode.page;
    page.readOnly = d.environment.readOnly;
    page.machine = &machineNode;
    machineNode.target = page.key;
    machineNode.machine = position;
    struct State {
        std::string name;
        const Node* member = nullptr;
        const Node* function = nullptr;
        Item* node = nullptr;
    };
    std::vector<State> states;
    bool foreign = false;
    for (const Node* member : object->items) {
        if (member->kind != Kind::Property || member->has(flag::kSpread) || member->has(flag::kComputed)
            || member->target == nullptr) {
            foreign = true;
            continue;
        }
        const Node* value = unparen(member->value);
        if (value == nullptr || !syntax::isFunction(value)) {
            foreign = true;
            continue;
        }
        std::string name = member->target->kind == Kind::String ? unquote(member->target->text)
                                                                : std::string(member->target->text);
        states.push_back({std::move(name), member, value, nullptr});
    }
    if (foreign) {
        machineNode.problem = say("reason:onlyStates");
        page.readOnly = true;
    }
    std::string start;
    if (const std::optional<std::string> given = stringValue(propertyValue(options, "start"))) {
        start = *given;
    }
    if (std::ranges::none_of(states, [&start](const State& state) { return state.name == start; })) {
        start = states.empty() ? std::string() : states.front().name;
    }
    const auto statePage = [&position](std::string_view name) { return join("state:", position, ":", name); };
    for (State& state : states) {
        Item& node = make(join(machineNode.key, "/state:", state.name), Role::State, "state", page.key);
        node.tone = "flow";
        node.icon = "square";
        node.title = state.name;
        node.token = "state";
        node.stateName = state.name;
        node.machine = position;
        node.target = statePage(state.name);
        node.start = state.name == start;
        node.function = state.function;
        node.async = state.function->has(flag::kAsync);
        Pin& in = node.inputs.emplace_back();
        in.name = "in";
        in.type = PinType::Transition;
        Pin& out = node.outputs.emplace_back();
        out.name = "out";
        out.type = PinType::Transition;
        finish(node, state.member, state.member);
        node.subtitle = node.note.str();
        node.note.clear();
        state.node = &node;
    }
    for (State& state : states) {
        const std::string_view param = firstParam(state.function);
        std::vector<const Node*> returns;
        collectReturns(state.function->body, returns);
        int ordinal = 0;
        for (const Node* returned : returns) {
            const std::optional<std::string> targetName = transitionName(returned->value, param);
            if (!targetName) {
                continue;
            }
            const auto found = std::ranges::find_if(states, [&](const State& other) { return other.name == *targetName; });
            if (found == states.end()) {
                state.node->problem = d.say.format("reason:unknownTarget", *targetName);
                continue;
            }
            const Node* transition = unparen(returned->value);
            Wire& wire = page.wires.emplace_back();
            wire.from = state.node->key;
            wire.fromPin = state.node->outputs.front().name;
            wire.to = found->node->key;
            wire.toPin = found->node->inputs.front().name;
            wire.type = "transition";
            if (!transition->items.empty()) {
                const std::optional<std::string> literal = stringValue(transition->items[0]);
                wire.label = literal ? *literal : compact(text(transition->items[0]));
            }
            wire.fromItem = state.node;
            wire.toItem = found->node;
            wire.call = transition;
            wire.key = join(wire.from, ">", wire.to, "#", std::to_string(ordinal++));
            state.node->outputs.front().connected = true;
            found->node->inputs.front().connected = true;
        }
    }
    for (State& state : states) {
        Page& inside = d.pages.emplace_back();
        inside.key = statePage(state.name);
        inside.kind = "state";
        inside.title = state.name;
        inside.icon = "square";
        inside.anchor = "st:" + state.node->key;
        inside.parent = page.key;
        inside.readOnly = page.readOnly;
        inside.function = state.function;
        Item& entry = make(join(state.node->key, "/entry"), Role::Entry, "entry", inside.key);
        entry.tone = "flow";
        entry.icon = "play";
        entry.title = d.say.format("node:enterState", state.name);
        entry.token = "entry";
        entry.function = state.function;
        entry.async = state.function->has(flag::kAsync);
        entry.machine = position;
        entry.stateName = state.name;
        inside.entry = &entry;
        execOut(entry);
        const std::string savedState = std::exchange(m_state, position);
        const std::string savedParam = std::exchange(m_stateParam, std::string(firstParam(state.function)));
        body(entry, "exec", state.function, state.function->body, false, nullptr, nullptr);
        m_state = savedState;
        m_stateParam = savedParam;
        finish(entry, state.member, state.member);
        entry.note.clear();
    }
}

Item* Builder::transitionItem(Chain& target, std::size_t index, const Node* statement, Ordinals& ordinals)
{
    if (statement->value == nullptr) {
        Item& item = statementBase(target, index, Role::Finish, "finish", "return", ordinals);
        item.tone = "flow";
        item.icon = "square";
        item.title = say("node:finish");
        execIn(item);
        finish(item, statement, statement);
        return &item;
    }
    const std::optional<std::string> name = transitionName(statement->value, m_stateParam);
    if (!name) {
        return nullptr;
    }
    const Node* call = unparen(statement->value);
    Item& item = statementBase(target, index, Role::Goto, "goto:" + *name, "return", ordinals);
    item.tone = "flow";
    item.icon = "chevron-right";
    item.title = d.say.format("node:goto", *name);
    if (!call->items.empty()) {
        const std::optional<std::string> literal = stringValue(call->items[0]);
        item.badge = literal ? *literal : compact(text(call->items[0]));
    }
    item.stateName = *name;
    item.expression = call;
    execIn(item);
    finish(item, statement, statement);
    return &item;
}

Item* Builder::statementItem(Chain& target, std::size_t& index, Ordinals& ordinals)
{
    const std::size_t at = index;
    if (codeStatement(target, at)) {
        std::size_t last = at;
        while (last + 1 < target.statements.size() && codeStatement(target, last + 1)) {
            ++last;
        }
        index = last + 1;
        return &codeItem(target, at, last, ordinals);
    }
    index = at + 1;
    const Node* statement = target.statements[at];
    if (target.shape == Shape::Expression) {
        const Node* expression = unparen(statement);
        bool waited = false;
        if (expression->kind == Kind::Await) {
            waited = true;
            expression = unparen(expression->target);
        }
        if (expression->kind == Kind::Call) {
            return callItem(target, at, statement, expression, waited, nullptr, ordinals);
        }
        return setItem(target, at, statement, expression->target, expression->value, nullptr, expression, ordinals);
    }
    const Node* inner = statement->kind == Kind::Export && statement->target != nullptr ? statement->target : statement;
    if (inner->kind == Kind::ExpressionStatement) {
        const Node* expression = unparen(inner->target);
        bool waited = false;
        if (expression->kind == Kind::Await) {
            waited = true;
            expression = unparen(expression->target);
        }
        const Item* previous = target.items.empty() ? nullptr : target.items.back();
        if (const Node* fan = parallelFan(target, at);
            fan != nullptr && (previous != nullptr || target.head != nullptr)
            && (previous == nullptr || !terminal(previous->role))
            && std::ranges::any_of(fan->items, [](const Node* argument) {
                const Node* arrow = unparen(argument);
                return !arrow->body->items.empty();
            })) {
            m_fans[&target] = at;
            return nullptr;
        }
        if (machineCall(expression)) {
            return machineItem(target, at, statement, expression, nullptr, ordinals);
        }
        if (workCall(expression, "sequence")) {
            return workItem(target, at, statement, expression, Role::Sequence, ordinals);
        }
        if (workCall(expression, "multiGate")) {
            return workItem(target, at, statement, expression, Role::MultiGate, ordinals);
        }
        if (const Node* job = jobOf(expression)) {
            return jobItem(target, at, statement, expression, job, ordinals);
        }
        if (expression->kind == Kind::Call) {
            return callItem(target, at, statement, expression, waited, nullptr, ordinals);
        }
        return setItem(target, at, statement, expression->target, expression->value, nullptr, expression, ordinals);
    }
    if (inner->kind == Kind::VarDecl) {
        const Node* declarator = inner->items[0];
        const Node* value = unparen(declarator->value);
        bool waited = false;
        if (value != nullptr && value->kind == Kind::Await) {
            waited = true;
            value = unparen(value->target);
        }
        if (machineCall(value)) {
            return machineItem(target, at, statement, value, declarator, ordinals);
        }
        if (value != nullptr && value->kind == Kind::New) {
            if (const ClassInfo* info = classOfNew(d, value)) {
                return newItem(target, at, statement, value, *info, declarator, ordinals);
            }
        }
        if (value != nullptr && value->kind == Kind::Call) {
            const Resolved& resolved = resolve(value->target);
            if (resolved.member != nullptr && at + 1 < target.statements.size()) {
                const Fusion* fusion = fusionOf(resolved.callee);
                const Node* next = target.statements[at + 1];
                const Node* test = next->kind == Kind::If ? unparen(next->test) : nullptr;
                if (fusion != nullptr && test != nullptr && test->kind == Kind::Identifier
                    && test->text == declarator->target->text) {
                    index = at + 2;
                    return fusedItem(target, at, statement, value, declarator, *fusion, *resolved.member, resolved.callee,
                                     ordinals);
                }
            }
            return callItem(target, at, statement, value, waited, declarator, ordinals);
        }
        return setItem(target, at, statement, declarator->target, declarator->value, declarator, inner, ordinals);
    }
    return structureItem(target, at, inner, ordinals);
}

void Builder::handlerOutputs(Item& item, const Node* handler, const TypeNode* type, bool several)
{
    if (handler->items.empty()) {
        return;
    }
    const Node* param = handler->items[0];
    if (param->kind == Kind::AssignPattern) {
        param = param->target;
    }
    std::vector<TypeProp> props;
    if (type != nullptr && !type->params.empty()) {
        props = propsOf(api, type->params[0].type);
    }
    const auto typeFor = [&](std::string_view name) {
        for (const TypeProp& prop : props) {
            if (prop.name == name) {
                return typeOf(prop.type, prop.name);
            }
        }
        return PinType::Any;
    };
    static_cast<void>(several);
    if (param->kind == Kind::Identifier) {
        produce(item, param, {}, PinType::Any);
        return;
    }
    if (param->kind != Kind::ObjectPattern) {
        return;
    }
    for (const Node* entry : param->items) {
        if (entry->kind != Kind::Property) {
            continue;
        }
        const Node* value = entry->value;
        if (value != nullptr && value->kind == Kind::AssignPattern) {
            value = value->target;
        }
        if (value == nullptr || value->kind != Kind::Identifier) {
            continue;
        }
        const std::string key = entry->target->kind == Kind::String ? unquote(entry->target->text)
                                                                     : std::string(entry->target->text);
        produce(item, value, {}, typeFor(key));
    }
}

bool Builder::event(const Node* statement, const std::string& prefix)
{
    if (statement->kind != Kind::ExpressionStatement) {
        return false;
    }
    const Node* call = unparen(statement->target);
    if (call == nullptr || call->kind != Kind::Call) {
        return false;
    }
    const Resolved& resolved = resolve(call->target);
    if (resolved.member == nullptr || !isEventMember(api, *resolved.member)) {
        return false;
    }
    const ApiMember& member = *resolved.member;
    const std::string token = "e:" + resolved.callee;
    const int ordinal = m_events[prefix + token]++;
    Item& item = make(prefix + token + "#" + std::to_string(ordinal), Role::Event, "event", std::string());
    item.tone = "event";
    item.token = token;
    item.api = member.label;
    item.apiMember = &member;
    item.callee = resolved.callee;
    item.expression = call;
    item.member = member.name;
    item.keyName = resolved.keyName;
    std::string label = d.say.optional(member.label);
    if (label.empty()) {
        label = resolved.owner.empty() ? member.label : member.name;
    }
    const bool key = resolved.shape == "Key";
    if (key) {
        item.title = resolved.owner;
        item.icon = resolved.keyName.starts_with("Mouse") || resolved.keyName.starts_with("Wheel") ? "mouse" : "keyboard";
    } else if (!resolved.owner.empty()) {
        item.title = resolved.owner + std::string(kDot) + label;
        item.icon = controlIcon(resolved.shape);
    } else {
        item.title = label;
        item.icon = member.owner.empty() ? "timer" : groupIcon(member.owner);
    }
    std::vector<std::size_t> handlers;
    std::vector<std::size_t> skipped;
    for (std::size_t p = 0; p < member.params.size(); ++p) {
        const TypeParam& param = member.params[p];
        if (isFunctionType(api, param.type)) {
            handlers.push_back(p);
            skipped.push_back(p);
        } else if (item.badgeArgument == kNone && durationName(param.name) && !param.optional) {
            item.badgeArgument = p;
            skipped.push_back(p);
        } else if (param.name == "options") {
            item.optionsArgument = p;
            skipped.push_back(p);
        }
    }
    const auto& args = call->items;
    const Node* duration = item.badgeArgument < args.size() ? args[item.badgeArgument] : nullptr;
    if (key) {
        if (duration != nullptr && numberValue(duration)) {
            item.badge = d.say.format("badge:" + member.label, numberText(*numberValue(duration) / 1000.0));
        } else {
            item.badge = say("badge:" + member.label);
        }
    } else if (duration != nullptr) {
        item.badge = numberValue(duration) ? d.say.seconds(*numberValue(duration)) : compact(text(duration));
    } else if (takesKey(d, item)) {
        item.badge = say(consumesKey(item) ? "badge:logicOnly" : "badge:alsoPresses");
    }
    apiPins(item, call, member, nullptr, skipped);
    item.handler = handlers.empty() ? kNone : handlers.front();
    const bool several = handlers.size() > 1;
    const bool alsoPresses = !several && takesKey(d, item) && !consumesKey(item);
    for (const std::size_t h : handlers) {
        const TypeParam& param = member.params[h];
        const std::string pin = several ? param.name : "exec";
        std::string pinLabel;
        if (several) {
            pinLabel = d.say.optional("param:" + member.label + "." + param.name);
            if (pinLabel.empty()) {
                pinLabel = param.name;
            }
        }
        execOut(item, pin, pinLabel, several && param.optional);
        const Node* handler = h < args.size() ? unparen(args[h]) : nullptr;
        if (handler != nullptr && syntax::isFunction(handler)) {
            if (item.function == nullptr) {
                item.function = handler;
            }
            handlerOutputs(item, handler, functionType(api, param.type), several);
            Chain* made = body(item, pin, handler, handler->body, false, nullptr, nullptr);
            if (alsoPresses && made != nullptr) {
                Item& press = make(item.key + "/default", Role::Default, "action", item.page);
                press.tone = "action";
                press.icon = "pointer";
                press.title = say("node:defaultAction");
                press.consumer = &item;
                execIn(press);
                execOut(press);
                setBranch(press, "exec", made);
                const std::size_t from = pinIndex(item.outputs, pin);
                const std::size_t to = pinIndex(press.outputs, "exec");
                for (Link& each : m_links) {
                    if (each.from == &item && each.fromPin == from) {
                        each.from = &press;
                        each.fromPin = to;
                    }
                }
                link(&item, pin, &press, "exec");
            }
        } else if (handler != nullptr && handler->kind == Kind::Identifier && h == item.handler) {
            item.badge = "\xE2\x86\x92 " + std::string(handler->text);
            if (d.functions.contains(handler->text)) {
                item.target = "fn:" + std::string(handler->text);
            }
        }
    }
    finish(item, statement, statement);
    return true;
}

Item& Builder::synthetic(Role role, const std::string& key, std::string title, Literal icon, std::string badge,
                         const Node* function, const Node* owner)
{
    Item& item = make(key, role, "event", "");
    item.tone = "event";
    item.token = key;
    item.title = std::move(title);
    item.icon = icon;
    item.badge = std::move(badge);
    item.function = function;
    execOut(item);
    if (owner != nullptr) {
        finish(item, owner, owner);
    }
    return item;
}

void Builder::registrationScope(const std::vector<const Node*>& statements, const std::string& prefix,
                                std::vector<const Node*>& rest, bool topLevel, std::vector<const Node*>& events)
{
    for (const Node* statement : statements) {
        const Node* inner = statement;
        if (topLevel && statement->kind == Kind::Export) {
            if (statement->target == nullptr) {
                continue;
            }
            inner = statement->target;
        }
        if (topLevel && statement->kind == Kind::ExportDefault) {
            inner = statement->target;
            if (inner->kind == Kind::Function || inner->kind == Kind::Class) {
                continue;
            }
            rest.push_back(statement);
            continue;
        }
        if (topLevel
            && (inner->kind == Kind::Import || inner->kind == Kind::Function || inner->kind == Kind::Class
                || inner->kind == Kind::Empty || declaresEnum(d, inner))) {
            continue;
        }
        if (registration(inner)) {
            if (inner == statement) {
                events.push_back(statement);
            }
            continue;
        }
        rest.push_back(statement);
    }
    static_cast<void>(prefix);
}

void Builder::functionPage(const Node* function, const std::string& name, const std::string& title, const Node* owner)
{
    Page& page = d.pages.emplace_back();
    page.key = "fn:" + name;
    page.kind = "function";
    page.title = title;
    page.icon = "puzzle";
    page.anchor = "f:" + name;
    page.listed = true;
    page.readOnly = d.environment.readOnly;
    page.function = function;
    Item& entry = make("f:" + name, Role::Entry, "entry", page.key);
    entry.tone = "custom";
    entry.icon = "play";
    entry.title = title;
    entry.token = "entry";
    entry.function = function;
    entry.async = function->has(flag::kAsync);
    page.entry = &entry;
    execOut(entry);
    for (const Node* param : function->items) {
        std::vector<const Node*> names;
        syntax::collectNames(param, names);
        for (const Node* id : names) {
            produce(entry, id, {}, PinType::Any);
        }
    }
    body(entry, "exec", function, function->body, false, nullptr, nullptr);
    if (function->body != nullptr && function->body->kind == Kind::Block) {
        for (const Node* statement : function->body->items) {
            if (statement->kind != Kind::VarDecl) {
                continue;
            }
            for (const Node* declarator : statement->items) {
                VariableInfo info = describeVariable(m_source, api, statement, declarator, page.key, false,
                                                     d.style.quote);
                if (!info.name.empty()) {
                    info.local = true;
                    d.variables.push_back(std::move(info));
                }
            }
        }
    }
    finish(entry, owner, owner);
}

void Builder::classPages(const Node* classNode)
{
    const ClassInfo* info = classNode->target != nullptr ? d.classNamed(classNode->target->text) : d.defaultClass();
    if (info == nullptr) {
        return;
    }
    const ClassInfo* saved = std::exchange(m_class, info);
    const std::string base = api.interfaces.contains(info->base) ? info->base : info->task ? "Task" : "Component";
    for (const Node* member : classNode->items) {
        if (member->kind != Kind::ClassMember || !member->has(flag::kMethod) || member->has(flag::kGetter)
            || member->has(flag::kSetter) || member->has(flag::kComputed) || member->value == nullptr) {
            continue;
        }
        const std::string name = member->target->kind == Kind::String ? unquote(member->target->text)
                                                                       : std::string(member->target->text);
        const Node* function = member->value;
        if (member->has(flag::kConstructor)) {
            functionPage(function, info->name + ".constructor", say("node:constructor"), member);
            continue;
        }
        if (info->component && name == "setup" && !member->has(flag::kStatic)) {
            std::vector<const Node*> statements(function->body->items.begin(), function->body->items.end());
            std::vector<const Node*> rest;
            std::vector<const Node*> events;
            registrationScope(statements, "setup/", rest, false, events);
            Item& item = synthetic(Role::Ready, "e:setup", say("node:setup"), "play", {}, function, member);
            Chain& made = chain(Shape::Braced, function->body, rest, &item, "exec", function,
                                function->has(flag::kAsync), false, item.key + "/exec", "");
            made.registrations = true;
            setBranch(item, "exec", &made);
            fill(made);
            for (const Node* statement : events) {
                event(statement, "setup/");
            }
            continue;
        }
        if (info->task && name == "run" && !member->has(flag::kStatic)) {
            Item& item = synthetic(Role::Run, "e:run", say("node:run"), "play", {}, function, member);
            body(item, "exec", function, function->body, false, nullptr, nullptr);
            continue;
        }
        const ApiMember* overridden = api.member(base, name);
        if ((info->component || info->task) && overridden != nullptr && isEventMember(api, *overridden)
            && !member->has(flag::kStatic)) {
            std::string label = d.say.optional(overridden->label);
            if (label.empty()) {
                label = name;
            }
            Item& item = synthetic(Role::Override, "e:over:" + name, label, "pointer", say("node:override"), function,
                                   member);
            item.api = overridden->label;
            item.member = name;
            body(item, "exec", function, function->body, false, nullptr, nullptr);
            continue;
        }
        functionPage(function, info->name + "." + name, name, member);
    }
    m_class = saved;
}

ClassInfo describeClass(const Node* classNode, bool exported, std::string_view text)
{
    ClassInfo info;
    info.node = classNode;
    info.exported = exported;
    info.name = classNode->target != nullptr ? std::string(classNode->target->text) : std::string("default");
    if (const Node* base = unparen(classNode->other)) {
        info.base = base->kind == Kind::Identifier ? std::string(base->text) : std::string();
        info.task = info.base == "Task";
        info.component = !info.task;
        info.plain = false;
    }
    const auto isPrivate = [text](const Node* member) {
        if (member->target == nullptr || member->target->start > text.size()) {
            return false;
        }
        std::size_t from = member->target->start;
        while (from > 0 && text[from - 1] != '\n' && text[from - 1] != ';' && text[from - 1] != '{'
               && text[from - 1] != '}') {
            --from;
        }
        return text.substr(from, member->target->start - from).find("private") != std::string_view::npos;
    };
    for (const Node* member : classNode->items) {
        if (member->kind != Kind::ClassMember || member->has(flag::kComputed) || member->target == nullptr) {
            continue;
        }
        const std::string name = member->target->kind == Kind::String ? unquote(member->target->text)
                                                                       : std::string(member->target->text);
        const bool priv = isPrivate(member);
        if (member->has(flag::kConstructor)) {
            info.hasConstructor = true;
            info.constructor = member->value;
            continue;
        }
        if (member->has(flag::kMethod)) {
            if (!member->has(flag::kGetter) && !member->has(flag::kSetter)) {
                info.methods.push_back(name);
                if (priv) {
                    info.privates.push_back(name);
                }
            }
            continue;
        }
        if (member->has(flag::kStatic)) {
            if (name == "props" || name == "params") {
                const Node* object = unparen(member->value);
                if (object == nullptr || object->kind != Kind::Object) {
                    continue;
                }
                for (const Node* entry : object->items) {
                    if (entry->kind != Kind::Property || entry->target == nullptr || entry->has(flag::kSpread)) {
                        continue;
                    }
                    const std::string prop = entry->target->kind == Kind::String ? unquote(entry->target->text)
                                                                                  : std::string(entry->target->text);
                    const Node* call = unparen(entry->value);
                    std::string kind;
                    std::string label;
                    if (call != nullptr && call->kind == Kind::Call && unparen(call->target)->kind == Kind::Member) {
                        kind = std::string(unparen(call->target)->value->text);
                        for (const Node* argument : call->items) {
                            if (const std::optional<std::string> given = stringValue(propertyValue(argument, "label"))) {
                                label = *given;
                            }
                        }
                    }
                    info.props[prop] = kind;
                    info.propLabels[prop] = label;
                }
            }
            continue;
        }
        const Node* value = unparen(member->value);
        const Node* callee = value != nullptr && value->kind == Kind::Call ? unparen(value->target) : nullptr;
        if (callee != nullptr && callee->kind == Kind::Member && unparen(callee->target)->kind == Kind::This
            && callee->value->text == "event") {
            info.events.push_back(name);
            if (priv) {
                info.privates.push_back(name);
            }
        } else {
            info.fields.push_back(name);
            if (priv) {
                info.privates.push_back(name);
            }
        }
    }
    return info;
}

void resolveClasses(std::vector<ClassInfo>& classes, const Api& api)
{
    for (ClassInfo& info : classes) {
        info.parent = nullptr;
        info.component = false;
        info.task = false;
        info.plain = true;
    }
    for (int pass = 0; pass < 8; ++pass) {
        bool changed = false;
        for (ClassInfo& info : classes) {
            const auto found = info.base.empty() ? classes.end() : std::ranges::find(classes, info.base, &ClassInfo::name);
            const ClassInfo* parent = found == classes.end() ? nullptr : &*found;
            const bool interfaceBase = !info.base.empty() && api.interfaces.contains(info.base);
            const bool task = info.base == "Task" || (parent != nullptr && parent->task);
            const bool component = (interfaceBase && api.derives(info.base, "Component")) || (parent != nullptr && parent->component);
            const bool plain = !task && !component;
            if (info.parent != parent || info.component != component || info.task != task || info.plain != plain) {
                info.parent = parent;
                info.component = component;
                info.task = task;
                info.plain = plain;
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
}

void Builder::file()
{
    d.types = scanTypes(m_source.text);
    const Node* program = m_source.tree.program;
    std::vector<const Node*> statements(program->items.begin(), program->items.end());
    std::vector<std::pair<const Node*, bool>> classNodes;
    std::vector<std::pair<const Node*, const Node*>> functionNodes;
    for (const Node* statement : statements) {
        const Node* inner = statement;
        bool exported = false;
        if ((statement->kind == Kind::Export || statement->kind == Kind::ExportDefault) && statement->target != nullptr) {
            inner = statement->target;
            exported = statement->kind == Kind::ExportDefault;
        }
        if (inner->kind == Kind::Function && inner->has(flag::kDeclaration)) {
            const std::string name = inner->target != nullptr ? std::string(inner->target->text) : std::string("default");
            d.functions[name] = inner;
            functionNodes.emplace_back(inner, statement);
        } else if (inner->kind == Kind::Class && inner->has(flag::kDeclaration)) {
            classNodes.emplace_back(inner, exported);
            d.classes.push_back(describeClass(inner, exported, m_source.text));
        }
    }
    resolveClasses(d.classes, api);
    std::vector<const Node*> rest;
    std::vector<const Node*> events;
    registrationScope(statements, "", rest, true, events);
    if (!rest.empty()) {
        Item& load = synthetic(Role::Load, "e:load", say("node:load"), "play", {}, nullptr, nullptr);
        Chain& made = chain(Shape::Program, program, std::move(rest), &load, "exec", nullptr, false, false,
                            load.key + "/exec", "");
        made.registrations = true;
        setBranch(load, "exec", &made);
        fill(made);
    }
    for (const Node* statement : events) {
        event(statement, "");
    }
    for (const Node* statement : statements) {
        const Node* inner = statement->kind == Kind::Export && statement->target != nullptr ? statement->target : statement;
        if (inner->kind != Kind::VarDecl || declaresEnum(d, inner)) {
            continue;
        }
        for (const Node* declarator : inner->items) {
            VariableInfo info = describeVariable(m_source, api, inner, declarator, "", false, d.style.quote);
            if (!info.name.empty()) {
                d.variables.push_back(std::move(info));
            }
        }
    }
    for (const ClassInfo& info : d.classes) {
        if (!info.exported || info.node == nullptr) {
            continue;
        }
        for (const Node* member : info.node->items) {
            if (member->kind != Kind::ClassMember || member->has(flag::kMethod) || member->has(flag::kStatic)
                || member->has(flag::kComputed) || member->target == nullptr) {
                continue;
            }
            const Node* value = unparen(member->value);
            const Node* callee = value != nullptr && value->kind == Kind::Call ? unparen(value->target) : nullptr;
            if (callee != nullptr && callee->kind == Kind::Member && unparen(callee->target)->kind == Kind::This
                && callee->value->text == "event") {
                continue;
            }
            VariableInfo made = describeVariable(m_source, api, member, member, "", true, d.style.quote);
            if (!made.name.empty()) {
                d.variables.push_back(std::move(made));
            }
        }
    }
    for (const auto& [classNode, exported] : classNodes) {
        static_cast<void>(exported);
        classPages(classNode);
    }
    for (const auto& [function, owner] : functionNodes) {
        const std::string name = function->target != nullptr ? std::string(function->target->text) : std::string("default");
        functionPage(function, name, name, owner);
    }
}

void Builder::floating(const Page& page, const FloatingCode& code)
{
    const Node* program = m_source.tree.program;
    std::vector<const Node*> statements(program->items.begin(), program->items.end());
    if (statements.size() == 1 && statements[0]->kind == Kind::ExpressionStatement) {
        const Node* expression = unparen(statements[0]->target);
        const bool pureShape = expression->kind == Kind::Binary || expression->kind == Kind::Logical
                            || expression->kind == Kind::Member || expression->kind == Kind::Conditional
                            || (expression->kind == Kind::Unary && (expression->text == "!" || expression->text == "-"));
        bool pureCall = false;
        if (expression->kind == Kind::Call) {
            const Resolved& resolved = resolve(expression->target);
            pureCall = resolved.member != nullptr && !resolved.member->property && !awaitable(resolved.member->returns)
                    && !isName(resolved.member->returns, "void") && !isEventMember(api, *resolved.member);
        }
        if (pureShape || pureCall) {
            if (Item* item = pure("~" + code.key, page.key, nullptr, nullptr, expression, nullptr)) {
                m_source.pure = true;
                item->statement = statements[0];
                item->last = statements[0];
                return;
            }
        }
    }
    if (statements.size() == 1 && registration(statements[0])) {
        return;
    }
    Chain& made = chain(Shape::Floating, program, std::move(statements), nullptr, {}, nullptr, true, false,
                        "~" + code.key, page.key);
    fill(made);
}

void Builder::broken(const Page& page, const FloatingCode& code)
{
    Item& item = make("~" + code.key, Role::Code, "code", page.key);
    item.tone = "code";
    item.icon = "code-xml";
    item.title = say("node:code");
    item.code = dedent(code.code);
    item.problem = d.say.format("problem:snippet", m_source.failure);
    execIn(item);
    execOut(item);
}

std::string problemMessage(std::string message)
{
    constexpr std::string_view kPrefix = "syntax error: ";
    if (message.starts_with(kPrefix)) {
        message.erase(0, kPrefix.size());
    }
    return message;
}

}

bool parseSource(Source& source)
{
    tsstrip::Result stripped = tsstrip::strip(source.text, tsstrip::Offsets::Bytes);
    source.javascript = std::move(stripped.javascript);
    source.lines = std::make_unique<Lines>(source.text);
    for (const tsstrip::Problem& problem : stripped.problems) {
        if (problem.code == tsstrip::ProblemCode::Syntax || problem.code == tsstrip::ProblemCode::Jsx) {
            source.ok = false;
            source.failure = problemMessage(problem.message);
            source.failureLine = problem.line;
            return false;
        }
    }
    source.tree = syntax::parse(source.javascript);
    if (!source.tree.failures.empty()) {
        source.ok = false;
        source.failure = problemMessage(source.tree.failures.front().message);
        source.failureLine = source.lines->lineOf(source.tree.failures.front().offset);
        return false;
    }
    source.scopes = std::make_unique<syntax::Scopes>(source.tree.program);
    return true;
}

std::vector<LibraryFunction> libraryFunctions(std::string_view typescript)
{
    std::vector<LibraryFunction> found;
    const tsstrip::Result stripped = tsstrip::strip(typescript, tsstrip::Offsets::Bytes);
    if (std::ranges::any_of(stripped.problems, [](const tsstrip::Problem& problem) {
            return problem.code == tsstrip::ProblemCode::Syntax || problem.code == tsstrip::ProblemCode::Jsx;
        })) {
        return found;
    }
    const syntax::Tree tree = syntax::parse(stripped.javascript);
    if (!tree.failures.empty() || tree.program == nullptr) {
        return found;
    }
    for (const Node* statement : tree.program->items) {
        const Node* inner = statement->kind == Kind::Export ? statement->target : nullptr;
        if (inner == nullptr || inner->kind != Kind::Function || !inner->has(flag::kDeclaration)
            || inner->target == nullptr) {
            continue;
        }
        LibraryFunction& function = found.emplace_back();
        function.name = std::string(inner->target->text);
        function.async = inner->has(flag::kAsync);
        bool optional = false;
        for (const Node* param : inner->items) {
            std::vector<const Node*> names;
            syntax::collectNames(param, names);
            optional = optional || param->kind == Kind::AssignPattern || param->kind == Kind::Rest;
            function.params.push_back(
                {names.empty() ? std::to_string(function.params.size()) : std::string(names.front()->text), optional});
        }
    }
    return found;
}

std::string libraryImportFor(std::string_view file)
{
    if (file.empty() || file == kLibraryFile) {
        return {};
    }
    const auto depth = static_cast<std::size_t>(std::ranges::count(file, '/'));
    std::string out = depth == 0 ? "./" : "";
    for (std::size_t i = 0; i < depth; ++i) {
        out += "../";
    }
    return out + std::string(kLibraryFile);
}

namespace {

void linkAll(Document::Impl& d, const std::vector<Link>& links)
{
    std::map<Page*, std::size_t> counts;
    for (const Link& link : links) {
        if (link.from != nullptr && link.to != nullptr && link.from->page == link.to->page) {
            if (Page* page = d.page(link.to->page)) {
                ++counts[page];
            }
        }
    }
    for (const auto& [page, count] : counts) {
        page->wires.reserve(page->wires.size() + count);
    }
    for (const Link& link : links) {
        if (link.from == nullptr || link.to == nullptr || link.from->page != link.to->page
            || link.fromPin >= link.from->outputs.size() || link.toPin >= link.to->inputs.size()) {
            continue;
        }
        Page* page = d.page(link.to->page);
        if (page == nullptr) {
            continue;
        }
        Pin* out = &link.from->outputs[link.fromPin];
        Pin* in = &link.to->inputs[link.toPin];
        out->connected = true;
        in->connected = true;
        Wire& wire = page->wires.emplace_back();
        wire.from = link.from->key;
        wire.fromPin = out->name;
        wire.to = link.to->key;
        wire.toPin = in->name;
        wire.type = Literal(pinTypeName(in->type == PinType::Any ? out->type : in->type).data());
        wire.dashed = link.dashed;
        wire.fromItem = link.from;
        wire.toItem = link.to;
        wire.key = join(wire.from, ":", wire.fromPin, ">", wire.to, ":", wire.toPin);
    }
}

void breakpoints(Document::Impl& d)
{
    for (const Breakpoint& breakpoint : d.layout.breakpoints) {
        Item* item = d.item(breakpoint.anchor);
        if (item == nullptr) {
            continue;
        }
        if (item->chain == nullptr || item->floating || item->role == Role::Pure) {
            continue;
        }
        if (!item->async) {
            item->breakpoint = "invalid";
            item->breakpointReason = d.say("reason:async");
            continue;
        }
        item->breakpoint = breakpoint.enabled ? "on" : "off";
    }
}

void positions(Document::Impl& d)
{
    for (Page& page : d.pages) {
        const auto found = d.layout.pages.find(page.anchor);
        if (found == d.layout.pages.end()) {
            continue;
        }
        for (Item* item : page.items) {
            if (const auto at = found->second.nodes.find(item->key); at != found->second.nodes.end()) {
                item->x = at->second[0];
                item->y = at->second[1];
                item->placed = true;
            }
        }
        for (const FloatingCode& code : found->second.floating) {
            if (!std::isfinite(code.x) || !std::isfinite(code.y)) {
                continue;
            }
            for (Item* item : page.items) {
                if (!item->placed && item->source != nullptr && item->source->id == code.key && floatingHead(*item)) {
                    item->x = code.x;
                    item->y = code.y;
                    item->placed = true;
                }
            }
        }
    }
}

void adopt(Document::Impl& d, const Document* previous)
{
    const Document::Impl* before = previous != nullptr ? &previous->internals() : nullptr;
    for (Page& page : d.pages) {
        std::vector<Item*> open;
        for (Item* item : page.items) {
            if (!item->placed) {
                open.push_back(item);
            }
        }
        if (open.empty()) {
            continue;
        }
        PageLayout& layout = d.layout.pages[page.anchor];
        const auto moveTo = [&](Item* item, const std::string& anchor) {
            const auto at = layout.nodes.find(anchor);
            if (at == layout.nodes.end()) {
                return false;
            }
            item->x = at->second[0];
            item->y = at->second[1];
            item->placed = true;
            layout.nodes[item->key] = at->second;
            layout.nodes.erase(anchor);
            for (Breakpoint& breakpoint : d.layout.breakpoints) {
                if (breakpoint.anchor == anchor) {
                    breakpoint.anchor = item->key;
                }
            }
            return true;
        };
        if (before != nullptr && before->ok) {
            std::vector<std::pair<std::string, const Item*>> orphans;
            for (const auto& [anchor, at] : layout.nodes) {
                static_cast<void>(at);
                if (d.byKey.contains(anchor)) {
                    continue;
                }
                if (const Item* old = before->item(anchor)) {
                    orphans.emplace_back(anchor, old);
                }
            }
            for (const bool exact : {true, false}) {
                for (Item* item : open) {
                    if (item->placed) {
                        continue;
                    }
                    const auto match = std::ranges::find_if(orphans, [&](const auto& orphan) {
                        return exact ? signatureOf(*orphan.second) == signatureOf(*item)
                                     : orphan.second->token == item->token && orphan.second->page == item->page
                                           && orphan.second->kind == item->kind;
                    });
                    if (match != orphans.end() && moveTo(item, match->first)) {
                        orphans.erase(match);
                    }
                }
            }
        }
        for (Item* item : open) {
            if (item->placed) {
                continue;
            }
            const auto pending = std::ranges::find_if(d.layout.pending, [&](const Pending& entry) {
                return entry.page == page.key && entry.signature == signatureOf(*item);
            });
            if (pending != d.layout.pending.end()) {
                item->x = pending->x;
                item->y = pending->y;
                item->placed = true;
                layout.nodes[item->key] = {item->x, item->y};
                d.layout.pending.erase(pending);
            }
        }
    }
}

}

void missingControls(Document::Impl& d)
{
    for (Item& item : d.items) {
        if (!item.callee.starts_with("controls.")) {
            continue;
        }
        const std::string_view rest(item.callee.data() + 9, item.callee.size() - 9);
        const std::string id(rest.substr(0, rest.find('.')));
        if (id.empty() || d.control(id) != nullptr) {
            continue;
        }
        std::string shown = id;
        const auto gone = std::ranges::find(d.environment.gone, id, &ControlEntry::id);
        if (gone != d.environment.gone.end() && !gone->name.empty()) {
            shown = gone->name;
        }
        item.fix = "missing";
        item.fixControl = id;
        item.problem = d.say.format("reason:noControl", shown);
    }
}

void build(Document::Impl& d, const Document* previous)
{
    Source& file = d.file();
    const ControlEntry* own = d.environment.ownControl.empty() ? nullptr : d.control(d.environment.ownControl);
    d.mainTitle = own != nullptr ? controlPageTitle(*own) : mainTitle(d.say, d.environment.fileName);
    Page& main = d.pages.emplace_back();
    main.key = "";
    main.kind = "events";
    main.title = d.mainTitle;
    main.icon = own != nullptr                             ? controlIcon(own->type).str()
              : d.environment.fileName == kLibraryFile ? std::string("layers")
                                                         : std::string("workflow");
    main.anchor = "";
    main.listed = true;
    if (!parseSource(file)) {
        d.ok = false;
        d.problemLine = file.failureLine;
        d.problem = d.say.format("problem:syntax", std::to_string(file.failureLine), file.failure);
        main.readOnly = true;
        return;
    }
    main.readOnly = d.environment.readOnly;
    detectStyle(d);
    std::vector<Link> links;
    links.reserve(256);
    Builder builder(d, file, links);
    builder.file();
    std::vector<std::pair<std::string, FloatingCode>> floating;
    for (const Page& page : d.pages) {
        if (const auto found = d.layout.pages.find(page.anchor); found != d.layout.pages.end()) {
            for (const FloatingCode& code : found->second.floating) {
                floating.emplace_back(page.key, code);
            }
        }
    }
    for (const auto& [pageKey, code] : floating) {
        Source& source = d.sources.emplace_back();
        source.id = code.key;
        source.page = pageKey;
        source.text = code.code;
        source.x = code.x;
        source.y = code.y;
        if (!parseSource(source)) {
            if (const Page* page = d.page(pageKey)) {
                Builder snippet(d, source, links);
                snippet.m_class = d.defaultClass();
                snippet.broken(*page, code);
            }
            continue;
        }
        Builder snippet(d, source, links);
        snippet.m_class = d.defaultClass();
        if (const Page* page = d.page(pageKey)) {
            snippet.floating(*page, code);
        }
    }
    linkAll(d, links);
    breakpoints(d);
    positions(d);
    missingControls(d);
    adopt(d, previous);
    place(d);
}

void place(Document::Impl& d)
{
    std::vector<std::size_t> localOf(d.items.size(), kNone);
    for (Page& page : d.pages) {
        const std::size_t count = page.items.size();
        for (std::size_t i = 0; i < count; ++i) {
            if (page.items[i]->serial < localOf.size()) {
                localOf[page.items[i]->serial] = i;
            }
        }
        const auto indexOf = [&](const Item* item) {
            return item != nullptr && item->serial < localOf.size() && localOf[item->serial] < count
                        && page.items[localOf[item->serial]] == item
                     ? localOf[item->serial]
                     : kNone;
        };
        std::vector<std::size_t> outStart(count + 1, 0);
        std::vector<char> entered(count, 0);
        std::vector<std::pair<std::size_t, std::size_t>> edges;
        edges.reserve(page.wires.size());
        for (const Wire& wire : page.wires) {
            if (wire.type != "exec" && wire.type != "transition") {
                continue;
            }
            const std::size_t from = indexOf(wire.fromItem);
            const std::size_t to = indexOf(wire.toItem);
            if (from == kNone || to == kNone) {
                continue;
            }
            edges.emplace_back(from, to);
            ++outStart[from + 1];
            entered[to] = 1;
        }
        for (std::size_t i = 0; i < count; ++i) {
            outStart[i + 1] += outStart[i];
        }
        std::vector<std::size_t> outs(edges.size());
        {
            std::vector<std::size_t> fill(outStart.begin(), outStart.end() - 1);
            for (const auto& [from, to] : edges) {
                outs[fill[from]++] = to;
            }
        }
        std::vector<std::size_t> pureStart(count + 1, 0);
        for (const Item* item : page.items) {
            if (item->role == Role::Pure) {
                if (const std::size_t owner = indexOf(item->consumer); owner != kNone) {
                    ++pureStart[owner + 1];
                }
            }
        }
        for (std::size_t i = 0; i < count; ++i) {
            pureStart[i + 1] += pureStart[i];
        }
        std::vector<std::size_t> pures(pureStart[count]);
        {
            std::vector<std::size_t> fill(pureStart.begin(), pureStart.end() - 1);
            for (std::size_t i = 0; i < count; ++i) {
                const Item* item = page.items[i];
                if (item->role == Role::Pure) {
                    if (const std::size_t owner = indexOf(item->consumer); owner != kNone) {
                        pures[fill[owner]++] = i;
                    }
                }
            }
        }
        double bottom = 0;
        bool any = false;
        for (const Item* item : page.items) {
            if (item->placed) {
                bottom = std::max(bottom, item->y + d.sizeOf(*item).height);
                any = true;
            }
        }
        double next = any ? bottom + 40 : 40;
        std::vector<char> seen(count, 0);
        std::function<double(std::size_t)> lay = [&](std::size_t first) -> double {
            const double top = page.items[first]->y;
            double height = 0;
            std::size_t at = first;
            while (at != kNone && seen[at] == 0) {
                seen[at] = 1;
                Item* item = page.items[at];
                const Size size = d.sizeOf(*item);
                double used = size.height;
                double pureY = item->y + size.height + 16;
                for (std::size_t p = pureStart[at]; p < pureStart[at + 1]; ++p) {
                    Item* child = page.items[pures[p]];
                    if (!child->placed) {
                        child->x = item->x - d.sizeOf(*child).width - 30;
                        child->y = pureY;
                        child->placed = true;
                    }
                    const double below = lay(pures[p]);
                    pureY = child->y + below + 12;
                    used = std::max(used, pureY - 12 - item->y);
                }
                std::size_t following = kNone;
                const std::size_t targets = outStart[at + 1] - outStart[at];
                if (targets == 1 && !structure(item->role)) {
                    const std::size_t to = outs[outStart[at]];
                    if (seen[to] == 0) {
                        Item* child = page.items[to];
                        if (!child->placed) {
                            child->x = item->x + size.width + 60;
                            child->y = item->y;
                            child->placed = true;
                        }
                        following = to;
                    }
                } else {
                    double cursor = item->y;
                    for (std::size_t e = outStart[at]; e < outStart[at + 1]; ++e) {
                        const std::size_t to = outs[e];
                        if (seen[to] != 0) {
                            continue;
                        }
                        Item* child = page.items[to];
                        if (!child->placed) {
                            child->x = item->x + size.width + 60;
                            child->y = cursor;
                            child->placed = true;
                        }
                        const double below = lay(to);
                        cursor = child->y + below + 40;
                        used = std::max(used, cursor - 40 - item->y);
                    }
                }
                height = std::max(height, item->y + used - top);
                at = following;
            }
            return height;
        };
        for (std::size_t i = 0; i < count; ++i) {
            Item* item = page.items[i];
            if (entered[i] != 0 || item->role == Role::Pure || seen[i] != 0) {
                continue;
            }
            const bool fresh = !item->placed;
            if (fresh) {
                item->x = 40;
                item->y = next;
                item->placed = true;
            }
            const double used = lay(i);
            if (fresh) {
                next = item->y + used + 40;
            }
        }
        for (Item* item : page.items) {
            if (!item->placed) {
                item->x = 40;
                item->y = next;
                item->placed = true;
                next += d.sizeOf(*item).height + 40;
            }
        }
        PageLayout& layout = d.layout.pages[page.anchor];
        for (const Item* item : page.items) {
            layout.nodes[item->key] = {item->x, item->y};
        }
    }
}

}
