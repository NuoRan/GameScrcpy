#pragma once

#include "gs/blueprint/Environment.h"
#include "gs/blueprint/Layout.h"
#include "gs/blueprint/Text.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gs::blueprint {

struct Size {
    double width = 0;
    double height = 0;
};

using Sizes = std::map<std::string, Size, std::less<>>;

struct Outcome {
    std::string reason;
    std::vector<TextChange> changes;
    Layout layout;
    std::string key;
    std::string signature;
    std::string page;

    [[nodiscard]] bool ok() const noexcept { return reason.empty(); }
};

struct Target {
    std::uint32_t line = 0;
    std::uint32_t column = 0;
};

struct GraphChoice {
    std::string value;
    std::string text;
};

struct GraphPin {
    std::string_view name;
    std::string_view label;
    std::string_view type;
    bool connected = false;
    bool optional = false;
    std::string_view value;
    std::string_view editor;
    const nlohmann::json* raw = nullptr;
    std::span<const GraphChoice> choices;
    std::string_view preview;
    std::optional<double> min;
    std::optional<double> max;
    std::optional<double> step;
    std::string_view unit;
};

struct GraphNode {
    std::string_view key;
    std::string_view kind;
    std::string_view tone;
    std::string_view icon;
    std::string_view title;
    std::string_view badge;
    std::string_view subtitle;
    std::string_view note;
    double x = 0;
    double y = 0;
    std::span<const GraphPin> inputs;
    std::span<const GraphPin> outputs;
    std::string_view code;
    bool floating = false;
    std::string_view breakpoint;
    std::string_view breakpointReason;
    std::string_view problem;
    std::string_view fix;
    std::string_view fixControl;
    bool start = false;
    bool active = false;
    double activeSince = 0;
    bool visited = false;
};

struct GraphWire {
    std::string_view key;
    std::string_view from;
    std::string_view fromPin;
    std::string_view to;
    std::string_view toPin;
    std::string_view type;
    std::string_view label;
    bool active = false;
    bool dashed = false;
};

struct Graph {
    Graph() = default;
    Graph(Graph&&) noexcept = default;
    Graph& operator=(Graph&&) noexcept = default;
    Graph(const Graph&) = delete;
    Graph& operator=(const Graph&) = delete;
    ~Graph() = default;

    std::string key;
    std::string kind;
    std::string title;
    bool readOnly = false;
    std::vector<GraphNode> nodes;
    std::vector<GraphWire> wires;
    std::vector<CommentBox> comments;
    std::string problem;
    std::uint32_t problemLine = 0;
    std::optional<ViewState> view;
    std::vector<GraphPin> pins;
    std::deque<std::string> strings;
};

[[nodiscard]] nlohmann::ordered_json graphJson(const Graph& graph);

class Document
{
public:
    Document(std::string source, const Environment& environment, Layout layout, const Document* previous = nullptr,
             const Sizes& sizes = {});
    ~Document();
    Document(Document&&) noexcept;
    Document& operator=(Document&&) noexcept;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    [[nodiscard]] const std::string& source() const noexcept;
    [[nodiscard]] const Layout& layout() const noexcept;
    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] const std::string& problem() const noexcept;
    [[nodiscard]] std::uint32_t problemLine() const noexcept;

    [[nodiscard]] nlohmann::ordered_json pages() const;
    [[nodiscard]] Graph view(std::string_view page) const;
    [[nodiscard]] nlohmann::ordered_json graph(std::string_view page) const;
    [[nodiscard]] nlohmann::ordered_json crumbs(std::string_view page) const;
    [[nodiscard]] nlohmann::ordered_json sidebar(std::string_view page = {}) const;
    [[nodiscard]] nlohmann::ordered_json typeInfo(std::string_view name) const;
    [[nodiscard]] nlohmann::ordered_json details(std::string_view page, std::string_view key,
                                                 const nlohmann::ordered_json& log = {}) const;
    [[nodiscard]] nlohmann::ordered_json variable(std::string_view page, std::string_view key) const;
    [[nodiscard]] nlohmann::ordered_json newVariable(std::string_view page) const;
    [[nodiscard]] nlohmann::ordered_json catalog(std::string_view page, std::string_view node, std::string_view pin,
                                                 std::string_view query) const;
    [[nodiscard]] bool hasPage(std::string_view page) const;
    [[nodiscard]] std::string pageOf(std::string_view key) const;
    [[nodiscard]] std::string find(std::string_view signature) const;
    [[nodiscard]] std::vector<std::string> keys(std::string_view page) const;

    [[nodiscard]] std::string check(std::string_view from, std::string_view fromPin, std::string_view to,
                                    std::string_view toPin) const;
    [[nodiscard]] Outcome connect(std::string_view from, std::string_view fromPin, std::string_view to,
                                  std::string_view toPin) const;
    [[nodiscard]] Outcome rewire(std::string_view node, std::string_view pin, std::string_view toNode,
                                 std::string_view toPin) const;
    [[nodiscard]] Outcome disconnect(std::string_view node, std::string_view pin) const;
    [[nodiscard]] Outcome removeWire(std::string_view wire) const;
    [[nodiscard]] Outcome setValue(std::string_view node, std::string_view pin, const nlohmann::json& value) const;
    [[nodiscard]] Outcome add(std::string_view page, std::string_view id, double x, double y, std::string_view fromNode,
                              std::string_view fromPin) const;
    [[nodiscard]] Outcome drop(std::string_view page, const nlohmann::json& item, double x, double y,
                               std::string_view node, std::string_view pin) const;
    [[nodiscard]] Outcome insertOnWire(std::string_view wire, std::string_view node) const;
    [[nodiscard]] Outcome remove(std::string_view page, const std::vector<std::string>& keys) const;
    [[nodiscard]] std::string copy(const std::vector<std::string>& keys) const;
    [[nodiscard]] Outcome paste(std::string_view page, std::string_view text, double x, double y) const;
    [[nodiscard]] Outcome duplicate(std::string_view page, const std::vector<std::string>& keys) const;
    [[nodiscard]] Outcome setCode(std::string_view node, std::string_view text) const;
    [[nodiscard]] Outcome replaceControl(std::string_view node, std::string_view from, std::string_view to) const;
    [[nodiscard]] Outcome collapse(const std::vector<std::string>& keys) const;
    [[nodiscard]] Outcome editDetail(std::string_view page, std::string_view key, std::string_view name,
                                     const nlohmann::json& value) const;
    [[nodiscard]] Outcome createVariable(std::string_view page, const nlohmann::json& spec) const;
    [[nodiscard]] Outcome saveVariable(std::string_view page, std::string_view key,
                                       const nlohmann::json& spec) const;
    [[nodiscard]] Outcome deleteVariable(std::string_view page, std::string_view key) const;
    [[nodiscard]] Outcome promoteToVariable(std::string_view page, std::string_view node,
                                            std::string_view pin) const;
    [[nodiscard]] Outcome addType(std::string_view kind, std::string_view name) const;
    [[nodiscard]] Outcome editType(std::string_view name, std::string_view field,
                                   const nlohmann::json& value = {}) const;

    [[nodiscard]] Outcome move(std::string_view page, const std::vector<std::string>& keys, double dx, double dy) const;
    [[nodiscard]] Outcome addComment(std::string_view page, const std::vector<std::string>& keys) const;
    [[nodiscard]] Outcome editComment(std::string_view page, std::string_view key, const nlohmann::json& changes) const;
    [[nodiscard]] Outcome addReroute(std::string_view page, std::string_view wire, double x, double y) const;
    [[nodiscard]] Outcome arrange(std::string_view page) const;
    [[nodiscard]] Outcome setView(std::string_view page, double x, double y, double zoom) const;
    [[nodiscard]] Outcome toggleBreakpoint(std::string_view node) const;
    [[nodiscard]] Outcome enableBreakpoint(std::string_view node, bool on) const;

    [[nodiscard]] Outcome addState(std::string_view page, double x, double y) const;
    [[nodiscard]] Outcome connectStates(std::string_view from, std::string_view to) const;
    [[nodiscard]] Outcome setTransitionLabel(std::string_view wire, std::string_view text) const;
    [[nodiscard]] Outcome renameState(std::string_view node, std::string_view name) const;
    [[nodiscard]] Outcome setStart(std::string_view node) const;

    void setOpenPage(std::string_view page);

    [[nodiscard]] std::string open(std::string_view node) const;
    [[nodiscard]] Target jumpTarget(std::string_view node) const;
    [[nodiscard]] std::string nodeAt(std::string_view position) const;
    [[nodiscard]] std::string wireInto(std::string_view node, std::string_view from) const;
    [[nodiscard]] std::string stateNode(std::string_view machinePosition, std::string_view state) const;
    [[nodiscard]] std::string machineOnLine(int line) const;
    [[nodiscard]] std::string transitionWire(std::string_view machinePosition, std::string_view from,
                                             std::string_view to) const;
    [[nodiscard]] nlohmann::ordered_json breakpointPositions() const;
    [[nodiscard]] std::string localProducer(std::string_view position, std::string_view name) const;

    struct Impl;

    [[nodiscard]] const Impl& internals() const noexcept { return *m_impl; }

private:
    std::unique_ptr<Impl> m_impl;
};

}
