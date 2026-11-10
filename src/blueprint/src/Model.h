#pragma once

#include "Api.h"
#include "Scopes.h"
#include "Syntax.h"
#include "Variables.h"
#include "Types.h"

#include "gs/blueprint/Document.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gs::blueprint {

inline constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();

class Literal
{
public:
    constexpr Literal() noexcept = default;
    constexpr Literal(const char* text) noexcept : m_text(text) {}

    [[nodiscard]] constexpr std::string_view view() const noexcept { return m_text; }
    constexpr operator std::string_view() const noexcept { return m_text; }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_text.empty(); }
    [[nodiscard]] std::string str() const { return std::string(m_text); }
    constexpr void clear() noexcept { m_text = {}; }

    friend constexpr bool operator==(Literal left, std::string_view right) noexcept { return left.m_text == right; }
    friend constexpr bool operator==(Literal left, Literal right) noexcept { return left.m_text == right.m_text; }
    friend constexpr bool operator==(Literal left, const char* right) noexcept
    {
        return left.m_text == std::string_view(right);
    }

private:
    std::string_view m_text;
};

class Lazy
{
public:
    Lazy() noexcept = default;
    Lazy(std::string_view text) : m_text(text.empty() ? nullptr : std::make_unique<std::string>(text)) {}
    Lazy(const char* text) : Lazy(std::string_view(text)) {}
    Lazy(const Lazy& other) : m_text(other.m_text != nullptr ? std::make_unique<std::string>(*other.m_text) : nullptr) {}
    Lazy(Lazy&&) noexcept = default;
    ~Lazy() = default;

    Lazy& operator=(const Lazy& other)
    {
        if (this != &other) {
            assign(other.view());
        }
        return *this;
    }
    Lazy& operator=(Lazy&&) noexcept = default;
    Lazy& operator=(std::string text)
    {
        if (text.empty()) {
            m_text.reset();
        } else if (m_text != nullptr) {
            *m_text = std::move(text);
        } else {
            m_text = std::make_unique<std::string>(std::move(text));
        }
        return *this;
    }
    Lazy& operator=(std::string_view text)
    {
        assign(text);
        return *this;
    }
    Lazy& operator=(const char* text)
    {
        assign(std::string_view(text));
        return *this;
    }
    Lazy& operator+=(std::string_view text)
    {
        if (!text.empty()) {
            if (m_text == nullptr) {
                m_text = std::make_unique<std::string>(text);
            } else {
                m_text->append(text);
            }
        }
        return *this;
    }

    [[nodiscard]] const std::string& str() const noexcept { return m_text != nullptr ? *m_text : none(); }
    [[nodiscard]] std::string_view view() const noexcept { return str(); }
    operator const std::string&() const noexcept { return str(); }
    operator std::string_view() const noexcept { return str(); }
    [[nodiscard]] bool empty() const noexcept { return m_text == nullptr || m_text->empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_text != nullptr ? m_text->size() : 0; }
    void clear() noexcept { m_text.reset(); }
    [[nodiscard]] bool starts_with(std::string_view text) const noexcept { return view().starts_with(text); }
    [[nodiscard]] bool ends_with(std::string_view text) const noexcept { return view().ends_with(text); }
    [[nodiscard]] std::string substr(std::size_t from, std::size_t count = std::string::npos) const
    {
        return str().substr(from, count);
    }

    friend bool operator==(const Lazy& left, std::string_view right) noexcept { return left.view() == right; }
    friend bool operator==(const Lazy& left, const Lazy& right) noexcept { return left.view() == right.view(); }
    friend bool operator==(const Lazy& left, const char* right) noexcept
    {
        return left.view() == std::string_view(right);
    }

private:
    void assign(std::string_view text)
    {
        if (text.empty()) {
            m_text.reset();
        } else if (m_text != nullptr) {
            m_text->assign(text);
        } else {
            m_text = std::make_unique<std::string>(text);
        }
    }
    static const std::string& none() noexcept
    {
        static const std::string text;
        return text;
    }

    std::unique_ptr<std::string> m_text;
};

template <class... Parts>
[[nodiscard]] std::string join(const Parts&... parts)
{
    std::string text;
    text.reserve((std::string_view(parts).size() + ... + 0));
    (text.append(std::string_view(parts)), ...);
    return text;
}

enum class PinType : std::uint8_t {
    Exec,
    Bool,
    Number,
    Text,
    Point,
    Region,
    Image,
    Color,
    Component,
    Key,
    Duration,
    List,
    Any,
    Transition,
    Struct,
    Enum,
    Object,
};

[[nodiscard]] std::string_view pinTypeName(PinType type) noexcept;
[[nodiscard]] PinType typeOfName(const Document::Impl& d, std::string_view type);
[[nodiscard]] std::string defaultValueOfType(std::string_view type);

using Choice = GraphChoice;

enum class SlotKind : std::uint8_t { None, Argument, Option, Expression, Declaration, Rest };

struct Chain;

struct Slot {
    SlotKind kind = SlotKind::None;
    const syntax::Node* call = nullptr;
    std::size_t index = 0;
    std::size_t width = 1;
    Lazy option;
    std::size_t optionIndex = kNone;
    const syntax::Node* node = nullptr;
    const syntax::Node* owner = nullptr;
    bool pair = false;
    bool condition = false;
    std::size_t group = kNone;
};

struct Pin {
    std::string name;
    std::string label;
    PinType type = PinType::Any;
    bool connected = false;
    bool optional = false;
    std::string value;
    Literal editor;
    nlohmann::json raw;
    std::span<const Choice> choices;
    Lazy preview;
    std::optional<double> min;
    std::optional<double> max;
    std::optional<double> step;
    Lazy unit;
    Slot slot;
    const syntax::Binding* binding = nullptr;
    Lazy local;
    Chain* branch = nullptr;
    nlohmann::json fallback;
};

enum class Role : std::uint8_t {
    Event,
    Load,
    Ready,
    Run,
    Override,
    Call,
    Custom,
    Set,
    Wait,
    Branch,
    Fused,
    ForEach,
    Repeat,
    While,
    Break,
    Continue,
    Return,
    Code,
    Super,
    Machine,
    State,
    Entry,
    Goto,
    Finish,
    Pure,
    Reroute,
    Switch,
    Sequence,
    MultiGate,
    ForRange,
    Default,
};

enum class Shape : std::uint8_t { Program, Braced, Expression, Single, Missing, Floating };

struct Source;
struct Chain;
struct Item;

struct Source {
    std::string id;
    std::string page;
    std::string text;
    std::string javascript;
    syntax::Tree tree;
    std::unique_ptr<syntax::Scopes> scopes;
    std::unique_ptr<Lines> lines;
    double x = 0;
    double y = 0;
    bool ok = true;
    std::string failure;
    std::uint32_t failureLine = 0;
    bool pure = false;
};

struct Chain {
    Source* source = nullptr;
    const syntax::Node* owner = nullptr;
    const syntax::Node* parent = nullptr;
    Shape shape = Shape::Braced;
    std::vector<const syntax::Node*> statements;
    std::vector<Item*> items;
    const syntax::Node* function = nullptr;
    bool async = false;
    bool loop = false;
    bool registrations = false;
    Item* head = nullptr;
    std::string pin;
    std::string prefix;
    std::string page;
    std::string state;
};

struct Item {
    std::size_t serial = 0;
    std::string key;
    Literal kind;
    Literal tone;
    Literal icon;
    std::string title;
    Lazy badge;
    Lazy subtitle;
    Lazy note;
    double x = 0;
    double y = 0;
    bool placed = false;
    std::vector<Pin> inputs;
    std::vector<Pin> outputs;
    Lazy code;
    bool floating = false;
    Literal breakpoint;
    Lazy breakpointReason;
    Lazy problem;
    Lazy fix;
    Lazy fixControl;
    bool start = false;

    Role role = Role::Call;
    Source* source = nullptr;
    Chain* chain = nullptr;
    std::size_t index = 0;
    std::size_t count = 1;
    const syntax::Node* statement = nullptr;
    const syntax::Node* last = nullptr;
    const syntax::Node* expression = nullptr;
    const syntax::Node* function = nullptr;
    std::string api;
    std::string token;
    std::string typeName;
    mutable Lazy signature;
    std::uint32_t sigStart = 0;
    std::uint32_t sigEnd = 0;
    std::string page;
    bool async = false;
    Item* consumer = nullptr;
    Lazy consumerPin;
    Lazy target;
    Lazy keyName;
    Lazy member;
    std::string callee;
    Lazy local;
    std::size_t handler = kNone;
    std::size_t badgeArgument = kNone;
    std::size_t optionsArgument = kNone;
    const ApiMember* apiMember = nullptr;
    Lazy machine;
    Lazy stateName;
};

struct Wire {
    std::string key;
    std::string_view from;
    std::string_view fromPin;
    std::string_view to;
    std::string_view toPin;
    Literal type;
    Lazy label;
    bool active = false;
    bool dashed = false;
    Item* fromItem = nullptr;
    Item* toItem = nullptr;
    const syntax::Node* call = nullptr;
};

struct Page {
    std::string key;
    std::string kind;
    std::string title;
    std::string icon;
    std::string anchor;
    std::string parent;
    bool readOnly = false;
    bool listed = false;
    std::vector<Item*> items;
    std::vector<Wire> wires;
    const syntax::Node* function = nullptr;
    Item* entry = nullptr;
    Item* machine = nullptr;
};

struct Style {
    std::string indent = "  ";
    std::string lineBreak = "\n";
    bool semicolons = true;
    char quote = '"';
};

struct ClassInfo {
    std::string name;
    std::string base;
    bool exported = false;
    bool task = false;
    bool component = false;
    bool plain = true;
    bool hasConstructor = false;
    const syntax::Node* node = nullptr;
    const syntax::Node* constructor = nullptr;
    const ClassInfo* parent = nullptr;
    std::vector<std::string> fields;
    std::vector<std::string> privates;
    std::vector<std::string> events;
    std::vector<std::string> methods;
    std::map<std::string, std::string, std::less<>> props;
    std::map<std::string, std::string, std::less<>> propLabels;
};

class Say
{
public:
    explicit Say(const Labels* labels) noexcept : m_labels(labels) {}

    [[nodiscard]] const std::string& operator()(std::string_view key) const;
    [[nodiscard]] std::string english(std::string_view key) const;
    [[nodiscard]] const std::string& optional(std::string_view key) const;
    [[nodiscard]] std::string format(std::string_view key, std::string_view first, std::string_view second = {}) const;
    [[nodiscard]] std::string key(std::string_view name) const;
    [[nodiscard]] std::string seconds(double milliseconds) const;

private:
    const Labels* m_labels = nullptr;
    mutable std::map<std::string, std::string, std::less<>> m_optional;
    mutable std::map<std::string, std::string, std::less<>> m_shown;
    mutable std::map<std::string, std::string, std::less<>> m_keys;
};

[[nodiscard]] std::string numberText(double value);
[[nodiscard]] std::string fill(std::string text, std::string_view first, std::string_view second = {});

struct Document::Impl {
    Environment environment;
    Say say{nullptr};
    Layout layout;
    Sizes sizes;
    std::deque<Source> sources;
    std::deque<Chain> chains;
    std::deque<Item> items;
    std::deque<Page> pages;
    std::map<std::string_view, Item*, std::less<>> byKey;
    std::map<const syntax::Binding*, std::pair<Item*, std::string>> producers;
    std::map<std::string, const syntax::Node*, std::less<>> functions;
    std::vector<ClassInfo> classes;
    UserTypes types;
    std::vector<std::string> globalNames;
    std::vector<VariableInfo> variables;
    Style style;
    bool ok = true;
    std::string problem;
    std::uint32_t problemLine = 0;
    std::string mainTitle;
    std::deque<std::vector<Choice>> choiceLists;
    std::span<const Choice> imageChoices;
    std::span<const Choice> controlChoices;
    std::span<const Choice> regionChoices;
    std::span<const Choice> pointChoices;
    bool choicesReady = false;

    [[nodiscard]] const Api& api() const { return *environment.api; }
    [[nodiscard]] Source& file() { return sources.front(); }
    [[nodiscard]] const Source& file() const { return sources.front(); }
    [[nodiscard]] Page* page(std::string_view key);
    [[nodiscard]] const Page* page(std::string_view key) const;
    [[nodiscard]] Item* item(std::string_view key) const;
    [[nodiscard]] PageLayout* pageLayout(const Page& page);
    [[nodiscard]] std::string text(const Source& source, const syntax::Node* node) const;
    [[nodiscard]] std::string text(const Source& source, std::uint32_t start, std::uint32_t end) const;
    [[nodiscard]] Size sizeOf(const Item& item) const;
    [[nodiscard]] const ControlEntry* control(std::string_view id) const;
    [[nodiscard]] const VariableEntry* variable(std::string_view id) const;
    [[nodiscard]] const VariableInfo* declaredVariable(std::string_view page, std::string_view name) const;
    [[nodiscard]] const ClassInfo* classNamed(std::string_view name) const;
    [[nodiscard]] const ClassInfo* defaultClass() const;
    [[nodiscard]] std::span<const Choice> keep(std::span<const Choice> list);
    [[nodiscard]] std::span<const Choice> shared(PinType type);
};

void build(Document::Impl& impl, const Document* previous);
void place(Document::Impl& impl);
bool parseSource(Source& source);
void detectStyle(Document::Impl& impl);
void visit(const syntax::Node* node, const std::function<void(const syntax::Node*)>& visitor);
[[nodiscard]] std::string mainTitle(const Say& say, std::string_view fileName);
[[nodiscard]] std::string controlPageTitle(const ControlEntry& control);
[[nodiscard]] std::string percentText(double ratio);
[[nodiscard]] std::optional<double> numberValue(const syntax::Node* node);
[[nodiscard]] std::optional<std::string> stringValue(const syntax::Node* node);

[[nodiscard]] bool parallelArrow(const syntax::Node* node);
[[nodiscard]] const syntax::Node* parallelFan(const Chain& chain, std::size_t at);
[[nodiscard]] std::string compact(std::string_view text);
[[nodiscard]] const std::string& signatureOf(const Item& item);
[[nodiscard]] std::string positionOf(const Item& item);
[[nodiscard]] bool settingMember(std::string_view name) noexcept;
[[nodiscard]] bool hiddenBlock(std::string_view shape, std::string_view label);
[[nodiscard]] bool floatingHead(const Item& item) noexcept;
[[nodiscard]] bool takesKey(const Document::Impl& d, const Item& item);
[[nodiscard]] bool consumesKey(const Item& item);
[[nodiscard]] std::string dedent(std::string_view text);
[[nodiscard]] std::string noteText(std::string_view comment);
[[nodiscard]] std::string unquote(std::string_view literal);
[[nodiscard]] bool identifierText(std::string_view text);
[[nodiscard]] nlohmann::json declaredDefault(std::string_view type, std::string_view name);

}
