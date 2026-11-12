#include "CompanionSync.h"
#include "Settings.h"

#include <gs/protocol/Codec.h>

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

using gs::app::accentColor;
using gs::app::clippedUtf8;
using gs::app::companionComputer;
using gs::app::CompanionComputer;
using gs::app::companionSettingPath;
using gs::app::companionSettingValue;
using gs::app::companionSnapshot;
namespace protocol = gs::protocol;
namespace settings = gs::app::settings;

TEST_CASE("ADR-0112: the computer's id stays the same for one machine, differs between machines and hides the "
          "machine id")
{
    const CompanionComputer first = companionComputer("0f3c1a2b-machine-guid", "DESKTOP-7Q2K1LM");
    CHECK(first == companionComputer("0f3c1a2b-machine-guid", "DESKTOP-7Q2K1LM"));
    CHECK(first.id == companionComputer("0f3c1a2b-machine-guid", "RENAMED").id);
    CHECK(first.name == "DESKTOP-7Q2K1LM");
    CHECK(first.id != companionComputer("another-machine-guid", "DESKTOP-7Q2K1LM").id);
    CHECK(first.id != CompanionComputer{}.id);

    const CompanionComputer nameOnly = companionComputer("", "GAMING-PC");
    CHECK(nameOnly.id == companionComputer("", "GAMING-PC").id);
    CHECK(nameOnly.id != companionComputer("", "OFFICE-PC").id);
    CHECK(nameOnly.id == companionComputer("GAMING-PC", "other").id);

    const std::string longName(80, 'n');
    CHECK(companionComputer("m", longName).name == std::string(protocol::kMaxComputerName, 'n'));
}

TEST_CASE("ADR-0112: a computer name longer than the protocol allows is cut at a character boundary")
{
    CHECK(clippedUtf8("short", 64) == "short");
    CHECK(clippedUtf8("", 64).empty());
    CHECK(clippedUtf8("abcdef", 3) == "abc");
    const std::string chinese = "\xE6\xB8\xB8\xE6\x88\x8F\xE7\x94\xB5\xE8\x84\x91";
    CHECK(clippedUtf8(chinese, 12) == chinese);
    CHECK(clippedUtf8(chinese, 11) == chinese.substr(0, 9));
    CHECK(clippedUtf8(chinese, 7) == chinese.substr(0, 6));
    CHECK(clippedUtf8(chinese, 2).empty());
}

TEST_CASE("ADR-0112: the accent colors match the design system")
{
    CHECK(accentColor(settings::Accent::Ember) == 0xFF6B3DU);
    CHECK(accentColor(settings::Accent::Teal) == 0x22D3C5U);
    CHECK(accentColor(settings::Accent::Azure) == 0x4C8DFFU);
    CHECK(accentColor(settings::Accent::Lime) == 0xB8F34AU);
    CHECK(accentColor(settings::Accent::Rose) == 0xFF4D8DU);
    CHECK(accentColor(settings::Accent::Amber) == 0xFFB020U);
}

TEST_CASE("ADR-0112: the snapshot carries this phone's companion settings, the computer, the link and the receipt")
{
    settings::Companion companion;
    companion.cursor = true;
    companion.hints = true;
    companion.hintOpacity = 35;
    companion.ime = false;
    companion.autoConnect = false;
    const CompanionComputer computer = companionComputer("guid", "DESKTOP-1");
    const protocol::CompanionSettings snapshot =
        companionSnapshot(companion, computer, protocol::ComputerLink::Wireless, 0x22D3C5U, 7);
    CHECK(snapshot.computer == computer.id);
    CHECK(snapshot.name == "DESKTOP-1");
    CHECK(snapshot.link == protocol::ComputerLink::Wireless);
    CHECK(snapshot.accent == 0x22D3C5U);
    CHECK(snapshot.applied == 7);
    CHECK(snapshot.cursor);
    CHECK(snapshot.hints);
    CHECK(snapshot.hintOpacity == 35);
    CHECK_FALSE(snapshot.ime);
    CHECK_FALSE(snapshot.autoConnect);
    CHECK(gs::protocol::encodePayload(snapshot).has_value());

    companion.hintOpacity = 4;
    CHECK(companionSnapshot(companion, computer, protocol::ComputerLink::Usb, 0xFF000000U | 0xFF6B3DU, 0)
              .hintOpacity == protocol::kMinPhoneHintOpacity);
    CHECK(companionSnapshot(companion, computer, protocol::ComputerLink::Usb, 0xFF000000U | 0xFF6B3DU, 0).accent ==
          0xFF6B3DU);
    companion.hintOpacity = 250;
    CHECK(companionSnapshot(companion, computer, protocol::ComputerLink::Lan, 0, 0).hintOpacity ==
          protocol::kMaxPhoneHintOpacity);

    const protocol::CompanionSettings defaults =
        companionSnapshot(settings::Companion{}, computer, protocol::ComputerLink::Usb, 0xFF6B3DU, 0);
    CHECK_FALSE(defaults.cursor);
    CHECK_FALSE(defaults.hints);
    CHECK(defaults.hintOpacity == 60);
    CHECK(defaults.ime);
    CHECK(defaults.autoConnect);
}

TEST_CASE("ADR-0112: each item from the phone maps to this phone's setting and a value of the right kind")
{
    CHECK(companionSettingPath(protocol::CompanionItem::Cursor) == "companion.cursor");
    CHECK(companionSettingPath(protocol::CompanionItem::Hints) == "companion.hints");
    CHECK(companionSettingPath(protocol::CompanionItem::HintOpacity) == "companion.hintOpacity");
    CHECK(companionSettingPath(protocol::CompanionItem::Ime) == "companion.ime");
    CHECK(companionSettingPath(protocol::CompanionItem::AutoConnect) == "companion.autoConnect");
    for (const protocol::CompanionItem item :
         {protocol::CompanionItem::Cursor, protocol::CompanionItem::Hints, protocol::CompanionItem::HintOpacity,
          protocol::CompanionItem::Ime, protocol::CompanionItem::AutoConnect}) {
        const settings::Entry* entry = settings::findSetting(companionSettingPath(item));
        REQUIRE(entry != nullptr);
        CHECK(entry->facets.scope == settings::Scope::Device);
        CHECK(entry->facets.live);
        CHECK(entry->page == settings::Page::Input);
    }

    const auto on = companionSettingValue({.change = 1, .item = protocol::CompanionItem::Cursor, .value = 1});
    REQUIRE(on.has_value());
    CHECK(*on == settings::Value(true));
    const auto off = companionSettingValue({.change = 2, .item = protocol::CompanionItem::AutoConnect, .value = 0});
    REQUIRE(off.has_value());
    CHECK(*off == settings::Value(false));
    const auto opacity = companionSettingValue({.change = 3, .item = protocol::CompanionItem::HintOpacity, .value = 35});
    REQUIRE(opacity.has_value());
    CHECK(*opacity == settings::Value(std::int64_t{35}));
    CHECK_FALSE(companionSettingValue({.change = 0, .item = protocol::CompanionItem::Cursor, .value = 1}).has_value());
    CHECK_FALSE(companionSettingValue({.change = 4, .item = protocol::CompanionItem::Ime, .value = 2}).has_value());
    CHECK_FALSE(
        companionSettingValue({.change = 5, .item = protocol::CompanionItem::HintOpacity, .value = 9}).has_value());
}

TEST_CASE("ADR-0112: the key hint opacity on the phone and connecting again over Wi-Fi are this phone's settings")
{
    using namespace settings;
    const Entry* opacity = findSetting("companion.hintOpacity");
    REQUIRE(opacity != nullptr);
    CHECK(opacity->kind == Kind::Integer);
    CHECK(opacity->min == 10);
    CHECK(opacity->max == 100);
    CHECK(opacity->defaultValue == Value(std::int64_t{60}));
    const Entry* again = findSetting("companion.autoConnect");
    REQUIRE(again != nullptr);
    CHECK(again->kind == Kind::Boolean);
    CHECK(again->defaultValue == Value(true));
    CHECK(save(Settings{}).find("\"companion\"") == std::string::npos);

    DeviceSettings device;
    CHECK(setOverride(device, "companion.hintOpacity", Value(std::int64_t{35})));
    CHECK(setOverride(device, "companion.autoConnect", Value(false)));
    CHECK_FALSE(setOverride(device, "companion.hintOpacity", Value(std::int64_t{5})));
    const Settings mine = effectiveSettings(Settings{}, device);
    CHECK(mine.companion.hintOpacity == 35);
    CHECK_FALSE(mine.companion.autoConnect);
    CHECK(effectiveSettings(Settings{}, DeviceSettings{}).companion.autoConnect);
    CHECK(effectiveSettings(Settings{}, DeviceSettings{}).companion.hintOpacity == 60);
}
