#include <gs/esp32/Keys.h>

#include <doctest/doctest.h>

#include <vector>

using gs::esp32::HidUsage;
using gs::esp32::hidUsageOf;
using gs::esp32::KeyboardState;
using gs::esp32::TypedKey;
using gs::esp32::typedKeyOf;
using gs::esp32::UsageKind;

namespace {

HidUsage keyboard(std::uint16_t usage)
{
    return {.kind = UsageKind::Keyboard, .usage = usage};
}

HidUsage consumer(std::uint16_t usage)
{
    return {.kind = UsageKind::Consumer, .usage = usage};
}

HidUsage modifier(std::uint16_t bit)
{
    return {.kind = UsageKind::Modifier, .usage = bit};
}

std::vector<std::uint8_t> keysOf(const gs_esp32_keyboard& report)
{
    return {report.keys, report.keys + GS_ESP32_KEYS};
}

}

TEST_CASE("Android keys map to the phone-side HID usages: home, back, recents and volume are consumer keys")
{
    CHECK(hidUsageOf(3) == consumer(0x0223));
    CHECK(hidUsageOf(4) == consumer(0x0224));
    CHECK(hidUsageOf(187) == consumer(0x01A2));
    CHECK(hidUsageOf(24) == consumer(0x00E9));
    CHECK(hidUsageOf(25) == consumer(0x00EA));
    CHECK(hidUsageOf(164) == consumer(0x00E2));
    CHECK(hidUsageOf(26) == consumer(0x0030));
    CHECK(hidUsageOf(82) == consumer(0x0040));
}

TEST_CASE("letters, digits, function keys and the number pad are keyboard usages")
{
    CHECK(hidUsageOf(29) == keyboard(0x04));
    CHECK(hidUsageOf(54) == keyboard(0x1D));
    CHECK(hidUsageOf(7) == keyboard(0x27));
    CHECK(hidUsageOf(8) == keyboard(0x1E));
    CHECK(hidUsageOf(16) == keyboard(0x26));
    CHECK(hidUsageOf(66) == keyboard(0x28));
    CHECK(hidUsageOf(67) == keyboard(0x2A));
    CHECK(hidUsageOf(62) == keyboard(0x2C));
    CHECK(hidUsageOf(111) == keyboard(0x29));
    CHECK(hidUsageOf(19) == keyboard(0x52));
    CHECK(hidUsageOf(22) == keyboard(0x4F));
    CHECK(hidUsageOf(131) == keyboard(0x3A));
    CHECK(hidUsageOf(142) == keyboard(0x45));
    CHECK(hidUsageOf(144) == keyboard(0x62));
    CHECK(hidUsageOf(145) == keyboard(0x59));
    CHECK(hidUsageOf(153) == keyboard(0x61));
    CHECK(hidUsageOf(160) == keyboard(0x58));
}

TEST_CASE("shift, control, alt and meta are modifier bits; unknown keycodes map to nothing")
{
    CHECK(hidUsageOf(59) == modifier(0x02));
    CHECK(hidUsageOf(60) == modifier(0x20));
    CHECK(hidUsageOf(113) == modifier(0x01));
    CHECK(hidUsageOf(57) == modifier(0x04));
    CHECK(hidUsageOf(117) == modifier(0x08));
    CHECK_FALSE(hidUsageOf(0).has_value());
    CHECK_FALSE(hidUsageOf(1).has_value());
    CHECK_FALSE(hidUsageOf(63).has_value());
    CHECK_FALSE(hidUsageOf(100000).has_value());
}

TEST_CASE("typing ASCII uses the US layout with shift where needed")
{
    CHECK(typedKeyOf('a') == TypedKey{0x04, false});
    CHECK(typedKeyOf('Z') == TypedKey{0x1D, true});
    CHECK(typedKeyOf('0') == TypedKey{0x27, false});
    CHECK(typedKeyOf('5') == TypedKey{0x22, false});
    CHECK(typedKeyOf('!') == TypedKey{0x1E, true});
    CHECK(typedKeyOf(')') == TypedKey{0x27, true});
    CHECK(typedKeyOf('?') == TypedKey{0x38, true});
    CHECK(typedKeyOf('/') == TypedKey{0x38, false});
    CHECK(typedKeyOf('"') == TypedKey{0x34, true});
    CHECK(typedKeyOf(' ') == TypedKey{0x2C, false});
    CHECK(typedKeyOf('\n') == TypedKey{0x28, false});
    CHECK_FALSE(typedKeyOf('\x01').has_value());
    CHECK_FALSE(typedKeyOf('\xE9').has_value());
}

TEST_CASE("the keyboard report holds up to six keys and the modifier bits")
{
    KeyboardState state;
    CHECK(state.empty());
    CHECK(state.apply(keyboard(0x04), true));
    CHECK(state.apply(keyboard(0x05), true));
    CHECK_FALSE(state.apply(keyboard(0x05), true));
    CHECK(state.apply(modifier(0x02), true));
    gs_esp32_keyboard report = state.report();
    CHECK(report.modifiers == 0x02);
    CHECK(keysOf(report) == std::vector<std::uint8_t>{0x04, 0x05, 0, 0, 0, 0});

    CHECK(state.apply(keyboard(0x04), false));
    CHECK(keysOf(state.report()) == std::vector<std::uint8_t>{0x05, 0, 0, 0, 0, 0});
    CHECK_FALSE(state.apply(keyboard(0x04), false));

    for (std::uint16_t usage = 0x10; usage < 0x15; ++usage) {
        CHECK(state.apply(keyboard(usage), true));
    }
    CHECK_FALSE(state.apply(keyboard(0x20), true));
    CHECK(keysOf(state.report()) == std::vector<std::uint8_t>{0x05, 0x10, 0x11, 0x12, 0x13, 0x14});
    CHECK_FALSE(state.apply(consumer(0x0223), true));

    CHECK(state.apply(modifier(0x02), false));
    state.clear();
    CHECK(state.empty());
    CHECK(state.report().modifiers == 0);
}
