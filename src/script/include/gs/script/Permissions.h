#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gs::script {

inline constexpr std::string_view kNeverGranted = "never";

struct ServiceRule {
    std::string_view permission;
    bool phone = true;
};

[[nodiscard]] ServiceRule serviceRule(std::string_view method) noexcept;
[[nodiscard]] ServiceRule appRule(std::string_view request) noexcept;
[[nodiscard]] ServiceRule eventRule(std::string_view kind, std::string_view id) noexcept;
[[nodiscard]] ServiceRule channelRule(std::string_view service) noexcept;
[[nodiscard]] bool permits(const std::vector<std::string>& granted, std::string_view permission);
[[nodiscard]] std::string permissionProblem(std::string_view permission);
[[nodiscard]] std::string phoneProblem();

}
