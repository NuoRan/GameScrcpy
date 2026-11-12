#include "CompanionSync.h"

#include <gs/platform/Crypto.h>

#include <algorithm>
#include <vector>

namespace gs::app {

namespace {

constexpr std::string_view kComputerContext = "GameScrcpy companion computer";

}

CompanionComputer companionComputer(std::string_view machineId, std::string_view hostName)
{
    const std::string_view source = machineId.empty() ? hostName : machineId;
    const auto* context = reinterpret_cast<const std::uint8_t*>(kComputerContext.data());
    std::vector<std::uint8_t> input(context, context + kComputerContext.size());
    input.push_back(0);
    const auto* machine = reinterpret_cast<const std::uint8_t*>(source.data());
    input.insert(input.end(), machine, machine + source.size());
    const platform::Sha256Digest digest = platform::sha256(input);
    CompanionComputer computer;
    std::copy_n(digest.begin(), computer.id.size(), computer.id.begin());
    computer.name = clippedUtf8(hostName, protocol::kMaxComputerName);
    return computer;
}

std::string clippedUtf8(std::string_view text, std::size_t most)
{
    if (text.size() <= most) {
        return std::string(text);
    }
    std::size_t size = most;
    while (size > 0 && (static_cast<unsigned char>(text[size]) & 0xC0U) == 0x80U) {
        --size;
    }
    return std::string(text.substr(0, size));
}

std::uint32_t accentColor(settings::Accent accent) noexcept
{
    switch (accent) {
    case settings::Accent::Ember:
        return 0xFF6B3DU;
    case settings::Accent::Teal:
        return 0x22D3C5U;
    case settings::Accent::Azure:
        return 0x4C8DFFU;
    case settings::Accent::Lime:
        return 0xB8F34AU;
    case settings::Accent::Rose:
        return 0xFF4D8DU;
    case settings::Accent::Amber:
        return 0xFFB020U;
    }
    return 0xFF6B3DU;
}

protocol::CompanionSettings companionSnapshot(const settings::Companion& companion, const CompanionComputer& computer,
                                              protocol::ComputerLink link, std::uint32_t accent,
                                              std::uint32_t applied)
{
    protocol::CompanionSettings snapshot;
    snapshot.computer = computer.id;
    snapshot.name = clippedUtf8(computer.name, protocol::kMaxComputerName);
    snapshot.link = link;
    snapshot.accent = accent & protocol::kAccentMask;
    snapshot.applied = applied;
    snapshot.cursor = companion.cursor;
    snapshot.hints = companion.hints;
    snapshot.hintOpacity = static_cast<std::uint8_t>(std::clamp<std::int32_t>(
        companion.hintOpacity, protocol::kMinPhoneHintOpacity, protocol::kMaxPhoneHintOpacity));
    snapshot.ime = companion.ime;
    snapshot.autoConnect = companion.autoConnect;
    return snapshot;
}

std::string_view companionSettingPath(protocol::CompanionItem item) noexcept
{
    switch (item) {
    case protocol::CompanionItem::Cursor:
        return "companion.cursor";
    case protocol::CompanionItem::Hints:
        return "companion.hints";
    case protocol::CompanionItem::HintOpacity:
        return "companion.hintOpacity";
    case protocol::CompanionItem::Ime:
        return "companion.ime";
    case protocol::CompanionItem::AutoConnect:
        return "companion.autoConnect";
    }
    return {};
}

std::optional<settings::Value> companionSettingValue(const protocol::CompanionSettingChange& change)
{
    if (!protocol::CompanionSettingChange::isValid(change)) {
        return std::nullopt;
    }
    if (change.item == protocol::CompanionItem::HintOpacity) {
        return settings::Value(static_cast<std::int64_t>(change.value));
    }
    return settings::Value(change.value != 0);
}

}
