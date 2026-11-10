#pragma once

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::blueprint {

struct ViewState {
    double x = 0;
    double y = 0;
    double zoom = 1;

    friend bool operator==(const ViewState&, const ViewState&) = default;
};

struct CommentBox {
    std::string key;
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
    std::string title;
    std::string color = "neutral";

    friend bool operator==(const CommentBox&, const CommentBox&) = default;
};

struct Reroute {
    std::string key;
    double x = 0;
    double y = 0;
    std::string from;
    std::string fromPin;
    std::string to;
    std::string toPin;

    friend bool operator==(const Reroute&, const Reroute&) = default;
};

struct FloatingCode {
    std::string key;
    std::string code;
    double x = 0;
    double y = 0;

    friend bool operator==(const FloatingCode&, const FloatingCode&) = default;
};

struct PageLayout {
    std::optional<ViewState> view;
    std::map<std::string, std::array<double, 2>, std::less<>> nodes;
    std::vector<CommentBox> comments;
    std::vector<Reroute> reroutes;
    std::vector<FloatingCode> floating;

    friend bool operator==(const PageLayout&, const PageLayout&) = default;
};

struct Breakpoint {
    std::string anchor;
    bool enabled = true;

    friend bool operator==(const Breakpoint&, const Breakpoint&) = default;
};

struct Pending {
    std::string page;
    std::string signature;
    double x = 0;
    double y = 0;

    friend bool operator==(const Pending&, const Pending&) = default;
};

struct Layout {
    std::map<std::string, PageLayout, std::less<>> pages;
    std::vector<Breakpoint> breakpoints;
    std::vector<Pending> pending;
    std::string open;

    friend bool operator==(const Layout&, const Layout&) = default;
};

inline constexpr std::string_view kLayoutFormat = "gamescrcpy.blueprint";
inline constexpr int kLayoutVersion = 1;

[[nodiscard]] Layout parseLayout(std::string_view json);
[[nodiscard]] std::string writeLayout(const Layout& layout);
[[nodiscard]] std::string layoutFileName(std::string_view codeFile);
[[nodiscard]] bool sameLayout(const Layout& first, const Layout& second);

}
