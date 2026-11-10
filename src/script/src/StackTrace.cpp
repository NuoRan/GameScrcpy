#include "StackTrace.h"

#include <charconv>
#include <optional>
#include <string>

namespace gs::script::detail {

namespace {

std::optional<int> numberOf(std::string_view text)
{
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value <= 0) {
        return std::nullopt;
    }
    return value;
}

std::optional<SourceLocation> positionOf(std::string_view text)
{
    const std::size_t columnColon = text.rfind(':');
    if (columnColon == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view rest = text.substr(0, columnColon);
    const std::size_t lineColon = rest.rfind(':');
    if (lineColon == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view module = rest.substr(0, lineColon);
    const auto row = numberOf(rest.substr(lineColon + 1));
    const auto column = numberOf(text.substr(columnColon + 1));
    if (!row || !column || module.empty() || module.front() == '<') {
        return std::nullopt;
    }
    return SourceLocation{.module = std::string(module), .line = *row, .column = *column};
}

std::optional<SourceLocation> frameLocation(std::string_view line)
{
    const std::size_t start = line.find_first_not_of(' ');
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    line.remove_prefix(start);
    if (!line.starts_with("at ")) {
        return std::nullopt;
    }
    line.remove_prefix(3);
    if (!line.ends_with(')')) {
        return positionOf(line);
    }
    for (std::size_t open = line.find(" ("); open != std::string_view::npos;
         open = line.find(" (", open + 1)) {
        if (auto location = positionOf(line.substr(open + 2, line.size() - open - 3))) {
            location->function = std::string(line.substr(0, open));
            return location;
        }
    }
    return std::nullopt;
}

}

SourceLocation locationFromStack(std::string_view stack)
{
    while (!stack.empty()) {
        const std::size_t end = stack.find('\n');
        if (auto location = frameLocation(stack.substr(0, end))) {
            return *location;
        }
        if (end == std::string_view::npos) {
            break;
        }
        stack.remove_prefix(end + 1);
    }
    return {};
}

}
