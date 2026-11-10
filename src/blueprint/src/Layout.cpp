#include "gs/blueprint/Layout.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <utility>

namespace gs::blueprint {

namespace {

using Json = nlohmann::json;
using Ordered = nlohmann::ordered_json;

double number(const Json& object, std::string_view key, double fallback = 0)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_number() ? found->get<double>() : fallback;
}

std::string text(const Json& object, std::string_view key)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
}

double tidy(double value)
{
    return std::isfinite(value) ? std::round(value * 10.0) / 10.0 : 0.0;
}

PageLayout readPage(const Json& page)
{
    PageLayout layout;
    if (const auto view = page.find("view"); view != page.end() && view->is_object()) {
        const double zoom = number(*view, "zoom", 1);
        layout.view = ViewState{number(*view, "x"), number(*view, "y"), zoom > 0 ? zoom : 1};
    }
    if (const auto nodes = page.find("nodes"); nodes != page.end() && nodes->is_object()) {
        for (const auto& [anchor, at] : nodes->items()) {
            if (at.is_array() && at.size() == 2 && at[0].is_number() && at[1].is_number()) {
                layout.nodes.emplace(anchor, std::array<double, 2>{at[0].get<double>(), at[1].get<double>()});
            }
        }
    }
    if (const auto comments = page.find("comments"); comments != page.end() && comments->is_array()) {
        for (const Json& item : *comments) {
            if (!item.is_object() || text(item, "key").empty()) {
                continue;
            }
            CommentBox box{.key = text(item, "key"),
                           .x = number(item, "x"),
                           .y = number(item, "y"),
                           .width = number(item, "width", 320),
                           .height = number(item, "height", 200),
                           .title = text(item, "title"),
                           .color = text(item, "color")};
            if (box.color.empty()) {
                box.color = "neutral";
            }
            layout.comments.push_back(std::move(box));
        }
    }
    if (const auto reroutes = page.find("reroutes"); reroutes != page.end() && reroutes->is_array()) {
        for (const Json& item : *reroutes) {
            if (!item.is_object() || text(item, "key").empty()) {
                continue;
            }
            layout.reroutes.push_back({.key = text(item, "key"),
                                       .x = number(item, "x"),
                                       .y = number(item, "y"),
                                       .from = text(item, "from"),
                                       .fromPin = text(item, "fromPin"),
                                       .to = text(item, "to"),
                                       .toPin = text(item, "toPin")});
        }
    }
    if (const auto floating = page.find("floating"); floating != page.end() && floating->is_array()) {
        for (const Json& item : *floating) {
            if (!item.is_object() || text(item, "key").empty()) {
                continue;
            }
            layout.floating.push_back(
                {.key = text(item, "key"), .code = text(item, "code"), .x = number(item, "x"), .y = number(item, "y")});
        }
    }
    return layout;
}

}

Layout parseLayout(std::string_view json)
{
    Layout layout;
    const Json root = Json::parse(json, nullptr, false);
    if (!root.is_object() || text(root, "format") != kLayoutFormat) {
        return layout;
    }
    const auto version = root.find("version");
    if (version == root.end() || !version->is_number_integer() || version->get<int>() != kLayoutVersion) {
        return layout;
    }
    layout.open = text(root, "open");
    if (const auto pages = root.find("pages"); pages != root.end() && pages->is_object()) {
        for (const auto& [anchor, page] : pages->items()) {
            if (page.is_object()) {
                layout.pages.emplace(anchor, readPage(page));
            }
        }
    }
    if (const auto breakpoints = root.find("breakpoints"); breakpoints != root.end() && breakpoints->is_array()) {
        for (const Json& item : *breakpoints) {
            if (!item.is_object() || text(item, "anchor").empty()) {
                continue;
            }
            const auto enabled = item.find("enabled");
            layout.breakpoints.push_back(
                {text(item, "anchor"), enabled == item.end() || !enabled->is_boolean() || enabled->get<bool>()});
        }
    }
    return layout;
}

std::string writeLayout(const Layout& layout)
{
    Ordered pages = Ordered::object();
    for (const auto& [anchor, page] : layout.pages) {
        Ordered entry = Ordered::object();
        if (page.view) {
            entry["view"] = {{"x", tidy(page.view->x)}, {"y", tidy(page.view->y)}, {"zoom", page.view->zoom}};
        }
        Ordered nodes = Ordered::object();
        for (const auto& [node, at] : page.nodes) {
            nodes[node] = Ordered::array({tidy(at[0]), tidy(at[1])});
        }
        entry["nodes"] = std::move(nodes);
        Ordered comments = Ordered::array();
        for (const CommentBox& box : page.comments) {
            comments.push_back({{"key", box.key},
                                {"x", tidy(box.x)},
                                {"y", tidy(box.y)},
                                {"width", tidy(box.width)},
                                {"height", tidy(box.height)},
                                {"title", box.title},
                                {"color", box.color}});
        }
        entry["comments"] = std::move(comments);
        Ordered reroutes = Ordered::array();
        for (const Reroute& reroute : page.reroutes) {
            reroutes.push_back({{"key", reroute.key},
                                {"x", tidy(reroute.x)},
                                {"y", tidy(reroute.y)},
                                {"from", reroute.from},
                                {"fromPin", reroute.fromPin},
                                {"to", reroute.to},
                                {"toPin", reroute.toPin}});
        }
        entry["reroutes"] = std::move(reroutes);
        Ordered floating = Ordered::array();
        for (const FloatingCode& code : page.floating) {
            floating.push_back({{"key", code.key}, {"code", code.code}, {"x", tidy(code.x)}, {"y", tidy(code.y)}});
        }
        entry["floating"] = std::move(floating);
        pages[anchor] = std::move(entry);
    }
    Ordered breakpoints = Ordered::array();
    for (const Breakpoint& breakpoint : layout.breakpoints) {
        breakpoints.push_back({{"anchor", breakpoint.anchor}, {"enabled", breakpoint.enabled}});
    }
    const Ordered root{{"format", kLayoutFormat},
                       {"version", kLayoutVersion},
                       {"open", layout.open},
                       {"pages", std::move(pages)},
                       {"breakpoints", std::move(breakpoints)}};
    return root.dump(2) + "\n";
}

std::string layoutFileName(std::string_view codeFile)
{
    std::string_view stem = codeFile;
    for (const std::string_view extension : {std::string_view(".ts"), std::string_view(".mts"), std::string_view(".js")}) {
        if (stem.size() > extension.size() && stem.ends_with(extension)) {
            stem.remove_suffix(extension.size());
            break;
        }
    }
    return std::string(stem) + ".blueprint.json";
}

bool sameLayout(const Layout& first, const Layout& second)
{
    return first.pages == second.pages && first.breakpoints == second.breakpoints;
}

}
