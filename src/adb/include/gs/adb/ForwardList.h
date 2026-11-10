#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gs::adb {

struct ForwardEntry {
    std::string serial;
    std::string host;
    std::string device;

    friend bool operator==(const ForwardEntry&, const ForwardEntry&) = default;
};

[[nodiscard]] std::vector<ForwardEntry> parseForwardList(std::string_view text);
[[nodiscard]] std::vector<ForwardEntry> parseReverseList(std::string_view text);

}
