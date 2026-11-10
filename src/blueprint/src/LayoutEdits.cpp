#include "Model.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gs::blueprint {

namespace {

std::string freshKey(const PageLayout& page, char prefix)
{
    for (int n = 1;; ++n) {
        const std::string key = std::string(1, prefix) + std::to_string(n);
        const bool taken = std::ranges::any_of(page.comments, [&key](const CommentBox& box) { return box.key == key; })
                        || std::ranges::any_of(page.reroutes, [&key](const Reroute& reroute) { return reroute.key == key; });
        if (!taken) {
            return key;
        }
    }
}

std::string anchorOf(const Document::Impl& d, std::string_view page)
{
    const Page* found = d.page(page);
    return found != nullptr ? found->anchor : std::string(page);
}

}

Outcome Document::move(std::string_view page, const std::vector<std::string>& keys, double dx, double dy) const
{
    const Impl& d = *m_impl;
    Outcome outcome;
    outcome.layout = d.layout;
    PageLayout& layout = outcome.layout.pages[anchorOf(d, page)];
    for (const std::string& key : keys) {
        if (const auto at = layout.nodes.find(key); at != layout.nodes.end()) {
            at->second[0] += dx;
            at->second[1] += dy;
            if (const Item* item = d.item(key); item != nullptr && floatingHead(*item)) {
                for (FloatingCode& code : layout.floating) {
                    if (code.key == item->source->id) {
                        code.x += dx;
                        code.y += dy;
                    }
                }
            }
            continue;
        }
        for (CommentBox& box : layout.comments) {
            if (box.key == key) {
                box.x += dx;
                box.y += dy;
            }
        }
        for (Reroute& reroute : layout.reroutes) {
            if (reroute.key == key) {
                reroute.x += dx;
                reroute.y += dy;
            }
        }
    }
    return outcome;
}

Outcome Document::addComment(std::string_view page, const std::vector<std::string>& keys) const
{
    const Impl& d = *m_impl;
    Outcome outcome;
    outcome.layout = d.layout;
    PageLayout& layout = outcome.layout.pages[anchorOf(d, page)];
    double left = std::numeric_limits<double>::max();
    double top = std::numeric_limits<double>::max();
    double right = std::numeric_limits<double>::lowest();
    double bottom = std::numeric_limits<double>::lowest();
    for (const std::string& key : keys) {
        const Item* item = d.item(key);
        if (item == nullptr) {
            continue;
        }
        const Size size = d.sizeOf(*item);
        left = std::min(left, item->x);
        top = std::min(top, item->y);
        right = std::max(right, item->x + size.width);
        bottom = std::max(bottom, item->y + size.height);
    }
    CommentBox box;
    box.key = freshKey(layout, 'c');
    if (left <= right) {
        box.x = left - 16;
        box.y = top - 40;
        box.width = right - left + 32;
        box.height = bottom - top + 56;
    } else {
        box.x = 40;
        box.y = 40;
        box.width = 320;
        box.height = 200;
    }
    outcome.key = box.key;
    layout.comments.push_back(std::move(box));
    return outcome;
}

Outcome Document::editComment(std::string_view page, std::string_view key, const nlohmann::json& changes) const
{
    const Impl& d = *m_impl;
    Outcome outcome;
    outcome.layout = d.layout;
    PageLayout& layout = outcome.layout.pages[anchorOf(d, page)];
    for (CommentBox& box : layout.comments) {
        if (box.key != key) {
            continue;
        }
        if (const auto title = changes.find("title"); title != changes.end() && title->is_string()) {
            box.title = title->get<std::string>();
        }
        if (const auto color = changes.find("color"); color != changes.end() && color->is_string()) {
            box.color = color->get<std::string>();
        }
        for (const auto& [name, target] : {std::pair{"width", &box.width}, std::pair{"height", &box.height},
                                           std::pair{"x", &box.x}, std::pair{"y", &box.y}}) {
            if (const auto value = changes.find(name); value != changes.end() && value->is_number()) {
                *target = value->get<double>();
            }
        }
        box.width = std::max(box.width, 80.0);
        box.height = std::max(box.height, 60.0);
        outcome.key = box.key;
    }
    return outcome;
}

Outcome Document::addReroute(std::string_view page, std::string_view wire, double x, double y) const
{
    const Impl& d = *m_impl;
    Outcome outcome;
    outcome.layout = d.layout;
    const Page* found = d.page(page);
    if (found == nullptr) {
        return outcome;
    }
    const auto match = std::ranges::find(found->wires, wire, &Wire::key);
    if (match == found->wires.end()) {
        return outcome;
    }
    PageLayout& layout = outcome.layout.pages[found->anchor];
    for (Reroute& reroute : layout.reroutes) {
        if (reroute.from == match->from && reroute.fromPin == match->fromPin && reroute.to == match->to
            && reroute.toPin == match->toPin) {
            reroute.x = x;
            reroute.y = y;
            outcome.key = reroute.key;
            return outcome;
        }
    }
    Reroute reroute{.key = freshKey(layout, 'r'),
                    .x = x,
                    .y = y,
                    .from = std::string(match->from),
                    .fromPin = std::string(match->fromPin),
                    .to = std::string(match->to),
                    .toPin = std::string(match->toPin)};
    outcome.key = reroute.key;
    layout.reroutes.push_back(std::move(reroute));
    return outcome;
}

Outcome Document::arrange(std::string_view page) const
{
    const Impl& d = *m_impl;
    Outcome outcome;
    const Page* found = d.page(page);
    if (found == nullptr || !d.ok) {
        outcome.layout = d.layout;
        return outcome;
    }
    Layout cleared = d.layout;
    PageLayout& layout = cleared.pages[found->anchor];
    layout.nodes.clear();
    for (FloatingCode& code : layout.floating) {
        code.x = std::numeric_limits<double>::quiet_NaN();
        code.y = std::numeric_limits<double>::quiet_NaN();
    }
    const Document fresh(d.file().text, d.environment, cleared, nullptr, d.sizes);
    outcome.layout = d.layout;
    PageLayout& target = outcome.layout.pages[found->anchor];
    const auto arranged = fresh.layout().pages.find(found->anchor);
    if (arranged == fresh.layout().pages.end()) {
        return outcome;
    }
    target.nodes = arranged->second.nodes;
    for (FloatingCode& code : target.floating) {
        for (const Item& item : fresh.internals().items) {
            if (item.source != nullptr && item.source->id == code.key && floatingHead(item)) {
                code.x = item.x;
                code.y = item.y;
            }
        }
    }
    return outcome;
}

Outcome Document::setView(std::string_view page, double x, double y, double zoom) const
{
    const Impl& d = *m_impl;
    Outcome outcome;
    outcome.layout = d.layout;
    outcome.layout.pages[anchorOf(d, page)].view = ViewState{x, y, zoom > 0 ? zoom : 1};
    return outcome;
}

Outcome Document::toggleBreakpoint(std::string_view node) const
{
    const Impl& d = *m_impl;
    Outcome outcome;
    outcome.layout = d.layout;
    const Item* item = d.item(node);
    if (item == nullptr || item->chain == nullptr || item->floating || item->role == Role::Pure) {
        return outcome;
    }
    std::vector<Breakpoint>& list = outcome.layout.breakpoints;
    const auto found = std::ranges::find(list, node, &Breakpoint::anchor);
    if (found != list.end()) {
        list.erase(found);
    } else {
        list.push_back({std::string(node), true});
    }
    return outcome;
}

Outcome Document::enableBreakpoint(std::string_view node, bool on) const
{
    const Impl& d = *m_impl;
    Outcome outcome;
    outcome.layout = d.layout;
    const Item* item = d.item(node);
    if (item == nullptr || item->chain == nullptr || item->floating || item->role == Role::Pure) {
        return outcome;
    }
    std::vector<Breakpoint>& list = outcome.layout.breakpoints;
    const auto found = std::ranges::find(list, node, &Breakpoint::anchor);
    if (found != list.end()) {
        found->enabled = on;
    } else {
        list.push_back({std::string(node), on});
    }
    return outcome;
}

}
