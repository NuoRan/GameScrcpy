#pragma once

#include "Settings.h"

#include <gs/protocol/Messages.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gs::app {

struct CompanionComputer {
    std::array<std::uint8_t, protocol::kCompanionComputerIdSize> id{};
    std::string name;

    friend bool operator==(const CompanionComputer&, const CompanionComputer&) = default;
};

[[nodiscard]] CompanionComputer companionComputer(std::string_view machineId, std::string_view hostName);
[[nodiscard]] std::string clippedUtf8(std::string_view text, std::size_t most);
[[nodiscard]] std::uint32_t accentColor(settings::Accent accent) noexcept;
[[nodiscard]] protocol::CompanionSettings companionSnapshot(const settings::Companion& companion,
                                                            const CompanionComputer& computer,
                                                            protocol::ComputerLink link, std::uint32_t accent,
                                                            std::uint32_t applied);
[[nodiscard]] std::string_view companionSettingPath(protocol::CompanionItem item) noexcept;
[[nodiscard]] std::optional<settings::Value> companionSettingValue(const protocol::CompanionSettingChange& change);

}
