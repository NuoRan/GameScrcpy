#include "gs/input/PadReports.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace gs::input;

namespace {

enum class Kind : std::uint8_t { Input, Output, Feature };

struct Field {
    Kind kind = Kind::Input;
    std::uint8_t reportId = 0;
    std::uint32_t usage = 0;
    std::size_t bit = 0;
    std::size_t size = 0;
    bool constant = false;
};

struct Layout {
    bool numbered = false;
    bool balanced = false;
    std::uint32_t application = 0;
    std::vector<Field> fields;
    std::map<std::pair<Kind, std::uint8_t>, std::size_t> bits;

    [[nodiscard]] std::optional<std::size_t> bytes(Kind kind, std::uint8_t reportId) const
    {
        const auto found = bits.find({kind, reportId});
        if (found == bits.end()) {
            return std::nullopt;
        }
        return (found->second + 7) / 8 + (numbered ? 1 : 0);
    }

    [[nodiscard]] bool has(Kind kind) const
    {
        return std::ranges::any_of(bits, [kind](const auto& entry) { return entry.first.first == kind; });
    }

    [[nodiscard]] const Field* find(std::uint8_t reportId, std::uint32_t usage) const
    {
        for (const Field& field : fields) {
            if (field.kind == Kind::Input && field.reportId == reportId && field.usage == usage && !field.constant) {
                return &field;
            }
        }
        return nullptr;
    }
};

Layout walk(std::span<const std::uint8_t> descriptor)
{
    Layout layout;
    std::uint32_t page = 0;
    std::size_t size = 0;
    std::size_t count = 0;
    std::uint8_t reportId = 0;
    std::vector<std::uint32_t> usages;
    std::optional<std::uint32_t> usageMinimum;
    std::optional<std::uint32_t> usageMaximum;
    int depth = 0;
    bool balanced = true;
    std::size_t at = 0;
    while (at < descriptor.size()) {
        const std::uint8_t prefix = descriptor[at];
        const std::size_t length = (prefix & 3U) == 3U ? 4 : (prefix & 3U);
        REQUIRE(prefix != 0xFE);
        REQUIRE(at + 1 + length <= descriptor.size());
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < length; ++i) {
            value |= static_cast<std::uint32_t>(descriptor[at + 1 + i]) << (8 * i);
        }
        const std::uint32_t full = length == 4 ? value : (page << 16U) | value;
        switch (prefix & 0xFCU) {
        case 0x04:
            page = value;
            break;
        case 0x08:
            usages.push_back(full);
            break;
        case 0x18:
            usageMinimum = full;
            break;
        case 0x28:
            usageMaximum = full;
            break;
        case 0x74:
            size = value;
            break;
        case 0x84:
            reportId = static_cast<std::uint8_t>(value);
            layout.numbered = true;
            break;
        case 0x94:
            count = value;
            break;
        case 0xA0:
            if (depth == 0 && value == 1 && !usages.empty()) {
                layout.application = usages.back();
            }
            ++depth;
            usages.clear();
            usageMinimum.reset();
            usageMaximum.reset();
            break;
        case 0xC0:
            --depth;
            balanced = balanced && depth >= 0;
            usages.clear();
            usageMinimum.reset();
            usageMaximum.reset();
            break;
        case 0x80:
        case 0x90:
        case 0xB0: {
            const Kind kind = (prefix & 0xFCU) == 0x80 ? Kind::Input : (prefix & 0xFCU) == 0x90 ? Kind::Output : Kind::Feature;
            std::size_t& bits = layout.bits[{kind, reportId}];
            for (std::size_t i = 0; i < count; ++i) {
                std::uint32_t usage = 0;
                if (usageMinimum && usageMaximum) {
                    usage = std::min(*usageMinimum + static_cast<std::uint32_t>(i), *usageMaximum);
                } else if (!usages.empty()) {
                    usage = usages[std::min(i, usages.size() - 1)];
                }
                layout.fields.push_back({.kind = kind,
                                         .reportId = reportId,
                                         .usage = usage,
                                         .bit = bits + i * size,
                                         .size = size,
                                         .constant = (value & 1U) != 0});
            }
            bits += size * count;
            usages.clear();
            usageMinimum.reset();
            usageMaximum.reset();
            break;
        }
        default:
            break;
        }
        at += 1 + length;
    }
    layout.balanced = balanced && depth == 0;
    return layout;
}

std::uint32_t read(const std::vector<std::uint8_t>& report, const Field& field, bool numbered)
{
    const std::size_t base = numbered ? 8 : 0;
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < field.size && i < 32; ++i) {
        const std::size_t bit = base + field.bit + i;
        REQUIRE(bit / 8 < report.size());
        if (((report[bit / 8] >> (bit % 8)) & 1U) != 0) {
            value |= 1U << i;
        }
    }
    return value;
}

std::vector<std::uint8_t> fromHex(std::string_view text)
{
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
        bytes.push_back(static_cast<std::uint8_t>(std::stoi(std::string(text.substr(i, 2)), nullptr, 16)));
    }
    return bytes;
}

struct Byte {
    std::size_t at = 0;
    std::uint8_t value = 0;
};

std::vector<std::uint8_t> patched(std::vector<std::uint8_t> bytes, std::initializer_list<Byte> changes)
{
    for (const Byte& change : changes) {
        bytes.at(change.at) = change.value;
    }
    return bytes;
}

constexpr std::array<PadFamily, 4> kFamilies{PadFamily::Generic, PadFamily::Xbox, PadFamily::DualShock4,
                                             PadFamily::DualSense};

constexpr std::uint32_t kGamePad = 0x00010005;
constexpr std::uint32_t kX = 0x00010030;
constexpr std::uint32_t kY = 0x00010031;
constexpr std::uint32_t kZ = 0x00010032;
constexpr std::uint32_t kRx = 0x00010033;
constexpr std::uint32_t kRy = 0x00010034;
constexpr std::uint32_t kRz = 0x00010035;
constexpr std::uint32_t kHat = 0x00010039;
constexpr std::uint32_t kBrake = 0x000200C5;
constexpr std::uint32_t kAccelerator = 0x000200C4;
constexpr std::uint32_t kRecord = 0x000C00B2;

constexpr std::uint32_t button(std::uint32_t number) noexcept
{
    return 0x00090000U | number;
}

constexpr std::uint32_t kEveryButton =
    pad_button::kSouth | pad_button::kEast | pad_button::kWest | pad_button::kNorth | pad_button::kLeftShoulder |
    pad_button::kRightShoulder | pad_button::kLeftTrigger | pad_button::kRightTrigger | pad_button::kBack |
    pad_button::kStart | pad_button::kGuide | pad_button::kLeftStick | pad_button::kRightStick | pad_button::kMisc |
    pad_button::kTouchpad;

const PadPassState kEveryButtonState{.buttons = kEveryButton};
const PadPassState kExtremes{.hat = 2,
                             .leftX = -32768,
                             .leftY = 32767,
                             .rightX = 32767,
                             .rightY = -32768,
                             .leftTrigger = 255,
                             .rightTrigger = 128};

struct Probe {
    std::uint32_t usage = 0;
    std::uint32_t expected = 0;
};

void probe(PadFamily family, const PadPassState& state, std::initializer_list<Probe> probes)
{
    const PadModel& model = padModel(family);
    const Layout layout = walk(model.descriptor);
    const std::vector<std::uint8_t> report = padInputReport(family, state);
    INFO(std::string(model.name));
    for (const Probe& each : probes) {
        INFO("usage " << each.usage);
        const Field* field = layout.find(model.inputReport, each.usage);
        REQUIRE(field != nullptr);
        CHECK(read(report, *field, layout.numbered) == each.expected);
    }
}

std::uint8_t hatByte(PadFamily family, std::uint8_t hat)
{
    const std::vector<std::uint8_t> report = padInputReport(family, {.hat = hat});
    switch (family) {
    case PadFamily::Xbox:
        return report.at(13);
    case PadFamily::DualShock4:
        return static_cast<std::uint8_t>(report.at(5) & 0x0FU);
    case PadFamily::DualSense:
        return static_cast<std::uint8_t>(report.at(8) & 0x0FU);
    case PadFamily::Generic:
        break;
    }
    return report.at(3);
}

std::vector<std::uint8_t> genericNeutral()
{
    return patched(std::vector<std::uint8_t>(14, 0x00), {{0, 0x01}});
}

std::vector<std::uint8_t> xboxNeutral()
{
    return patched(std::vector<std::uint8_t>(17, 0x00), {{0, 0x01}, {2, 0x80}, {4, 0x80}, {6, 0x80}, {8, 0x80}});
}

std::vector<std::uint8_t> dualShock4Neutral()
{
    return patched(std::vector<std::uint8_t>(64, 0x00),
                   {{0, 0x01}, {1, 0x80}, {2, 0x80}, {3, 0x80}, {4, 0x80}, {5, 0x08}, {30, 0x1B}, {33, 0x01},
                    {35, 0x80}, {39, 0x80}, {44, 0x80}, {48, 0x80}, {53, 0x80}, {57, 0x80}});
}

std::vector<std::uint8_t> dualSenseNeutral()
{
    return patched(std::vector<std::uint8_t>(64, 0x00), {{0, 0x01}, {1, 0x80}, {2, 0x80}, {3, 0x80}, {4, 0x80},
                                                         {8, 0x08}, {33, 0x80}, {37, 0x80}, {53, 0x2A}});
}

}

TEST_CASE("each emulated gamepad has the ids, bus, name and report sizes of the real one (ADR-0103)")
{
    const PadModel& xbox = padModel(PadFamily::Xbox);
    CHECK(xbox.family == PadFamily::Xbox);
    CHECK(xbox.vendorId == 0x045E);
    CHECK(xbox.productId == 0x0B13);
    CHECK(xbox.bus == kPadBusBluetooth);
    CHECK(xbox.name == "Xbox Wireless Controller");
    CHECK(xbox.features.empty());
    CHECK(xbox.rumble);

    const PadModel& dualShock4 = padModel(PadFamily::DualShock4);
    CHECK(dualShock4.family == PadFamily::DualShock4);
    CHECK(dualShock4.vendorId == 0x054C);
    CHECK(dualShock4.productId == 0x09CC);
    CHECK(dualShock4.bus == kPadBusUsb);
    CHECK(dualShock4.rumble);

    const PadModel& dualSense = padModel(PadFamily::DualSense);
    CHECK(dualSense.family == PadFamily::DualSense);
    CHECK(dualSense.vendorId == 0x054C);
    CHECK(dualSense.productId == 0x0CE6);
    CHECK(dualSense.bus == kPadBusUsb);
    CHECK(dualSense.rumble);

    const PadModel& generic = padModel(PadFamily::Generic);
    CHECK(generic.family == PadFamily::Generic);
    CHECK(generic.bus == kPadBusUsb);
    CHECK(generic.features.empty());
    CHECK_FALSE(generic.rumble);

    CHECK(xbox.inputSize == 17);
    CHECK(dualShock4.inputSize == 64);
    CHECK(dualSense.inputSize == 64);
    CHECK(generic.inputSize == 14);
}

TEST_CASE("every gamepad descriptor parses and agrees with its input, output and feature report sizes (ADR-0103)")
{
    for (const PadFamily family : kFamilies) {
        const PadModel& model = padModel(family);
        INFO(std::string(model.name));
        const Layout layout = walk(model.descriptor);
        CHECK(layout.balanced);
        CHECK(layout.numbered);
        CHECK(layout.application == kGamePad);
        CHECK(model.descriptor.size() <= 4096);
        CHECK(model.name.size() <= 127);
        CHECK(layout.bytes(Kind::Input, model.inputReport) == model.inputSize);
        const std::vector<std::uint8_t> neutral = padInputReport(family, {});
        CHECK(neutral.size() == model.inputSize);
        CHECK(neutral.at(0) == model.inputReport);
        for (const PadFeature& feature : model.features) {
            INFO("feature report " << static_cast<int>(feature.reportNumber));
            REQUIRE_FALSE(feature.data.empty());
            CHECK(feature.data[0] == feature.reportNumber);
            CHECK(layout.bytes(Kind::Feature, feature.reportNumber) == feature.data.size());
            CHECK(feature.data.size() <= 512);
        }
    }
    CHECK(walk(padModel(PadFamily::Xbox).descriptor).bytes(Kind::Output, 0x03) == std::size_t{9});
    CHECK(walk(padModel(PadFamily::DualShock4).descriptor).bytes(Kind::Output, 0x05) == std::size_t{32});
    CHECK(walk(padModel(PadFamily::DualSense).descriptor).bytes(Kind::Output, 0x02) == std::size_t{48});
    CHECK_FALSE(walk(padModel(PadFamily::Generic).descriptor).has(Kind::Output));
}

TEST_CASE("the PlayStation pads carry the feature reports their kernel drivers ask for while probing (ADR-0103)")
{
    const auto sizes = [](PadFamily family) {
        std::map<int, std::size_t> found;
        for (const PadFeature& feature : padModel(family).features) {
            found[feature.reportNumber] = feature.data.size();
        }
        return found;
    };
    CHECK(sizes(PadFamily::DualShock4) == std::map<int, std::size_t>{{0x02, 37}, {0x12, 16}, {0x81, 7}, {0xA3, 49}});
    CHECK(sizes(PadFamily::DualSense) == std::map<int, std::size_t>{{0x05, 41}, {0x09, 20}, {0x20, 64}});

    const auto word = [](std::span<const std::uint8_t> data, std::size_t at) {
        return static_cast<std::int16_t>(static_cast<std::uint16_t>(data[at] | (data[at + 1] << 8U)));
    };
    for (const PadFamily family : {PadFamily::DualShock4, PadFamily::DualSense}) {
        const PadModel& model = padModel(family);
        const auto calibration = std::ranges::find(model.features, family == PadFamily::DualShock4 ? 0x02 : 0x05,
                                                   &PadFeature::reportNumber);
        REQUIRE(calibration != model.features.end());
        INFO(std::string(model.name));
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const int bias = word(calibration->data, 1 + 2 * axis);
            const int plus = word(calibration->data, 7 + 4 * axis);
            const int minus = word(calibration->data, 9 + 4 * axis);
            CHECK(std::abs(plus - bias) + std::abs(minus - bias) > 0);
        }
        CHECK(word(calibration->data, 19) + word(calibration->data, 21) > 0);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            CHECK(word(calibration->data, 23 + 4 * axis) - word(calibration->data, 25 + 4 * axis) > 0);
        }
    }
}

TEST_CASE("input reports have fixed bytes when released, with every button and at the extremes (ADR-0103)")
{
    CHECK(padInputReport(PadFamily::Generic, {}) == genericNeutral());
    CHECK(padInputReport(PadFamily::Generic, kEveryButtonState) == patched(genericNeutral(), {{1, 0xDB}, {2, 0xFF}}));
    CHECK(padInputReport(PadFamily::Generic, kExtremes) ==
          patched(genericNeutral(), {{3, 0x03}, {4, 0x00}, {5, 0x80}, {6, 0xFF}, {7, 0x7F}, {8, 0xFF}, {9, 0x7F},
                                     {10, 0x00}, {11, 0x80}, {12, 0xFF}, {13, 0x80}}));

    CHECK(padInputReport(PadFamily::Xbox, {}) == xboxNeutral());
    CHECK(padInputReport(PadFamily::Xbox, kEveryButtonState) ==
          patched(xboxNeutral(), {{14, 0xDB}, {15, 0x7C}, {16, 0x01}}));
    CHECK(padInputReport(PadFamily::Xbox, kExtremes) ==
          patched(xboxNeutral(), {{1, 0x00}, {2, 0x00}, {3, 0xFF}, {4, 0xFF}, {5, 0xFF}, {6, 0xFF}, {7, 0x00},
                                  {8, 0x00}, {9, 0xFF}, {10, 0x03}, {11, 0x01}, {12, 0x02}, {13, 0x03}}));

    CHECK(padInputReport(PadFamily::DualShock4, {}) == dualShock4Neutral());
    CHECK(padInputReport(PadFamily::DualShock4, kEveryButtonState) ==
          patched(dualShock4Neutral(), {{5, 0xF8}, {6, 0xFF}, {7, 0x03}}));
    CHECK(padInputReport(PadFamily::DualShock4, kExtremes) ==
          patched(dualShock4Neutral(), {{1, 0x00}, {2, 0xFF}, {3, 0xFF}, {4, 0x00}, {5, 0x02}, {8, 0xFF}, {9, 0x80}}));

    CHECK(padInputReport(PadFamily::DualSense, {}) == dualSenseNeutral());
    CHECK(padInputReport(PadFamily::DualSense, kEveryButtonState) ==
          patched(dualSenseNeutral(), {{8, 0xF8}, {9, 0xFF}, {10, 0x07}}));
    CHECK(padInputReport(PadFamily::DualSense, kExtremes) ==
          patched(dualSenseNeutral(), {{1, 0x00}, {2, 0xFF}, {3, 0xFF}, {4, 0x00}, {5, 0xFF}, {6, 0x80}, {8, 0x02}}));
}

TEST_CASE("the hat goes up clockwise to up-left, and centred is the null value of each pad (ADR-0103)")
{
    for (std::uint8_t hat = 0; hat < 8; ++hat) {
        INFO("hat " << static_cast<int>(hat));
        CHECK(hatByte(PadFamily::Generic, hat) == hat + 1);
        CHECK(hatByte(PadFamily::Xbox, hat) == hat + 1);
        CHECK(hatByte(PadFamily::DualShock4, hat) == hat);
        CHECK(hatByte(PadFamily::DualSense, hat) == hat);
    }
    for (const std::uint8_t centred : {std::uint8_t{8}, std::uint8_t{9}, std::uint8_t{255}}) {
        CHECK(hatByte(PadFamily::Generic, centred) == 0);
        CHECK(hatByte(PadFamily::Xbox, centred) == 0);
        CHECK(hatByte(PadFamily::DualShock4, centred) == 8);
        CHECK(hatByte(PadFamily::DualSense, centred) == 8);
    }
}

TEST_CASE("each pad's descriptor reads its own reports back: sticks, triggers, hat and buttons (ADR-0103)")
{
    probe(PadFamily::Generic, kExtremes,
          {{kX, 0x8000}, {kY, 0x7FFF}, {kZ, 0x7FFF}, {kRz, 0x8000}, {kBrake, 255}, {kAccelerator, 128}, {kHat, 3}});
    probe(PadFamily::Xbox, kExtremes,
          {{kX, 0}, {kY, 0xFFFF}, {kZ, 0xFFFF}, {kRz, 0}, {kBrake, 1023}, {kAccelerator, 513}, {kHat, 3}});
    probe(PadFamily::DualShock4, kExtremes,
          {{kX, 0}, {kY, 255}, {kZ, 255}, {kRz, 0}, {kRx, 255}, {kRy, 128}, {kHat, 2}});
    probe(PadFamily::DualSense, kExtremes,
          {{kX, 0}, {kY, 255}, {kZ, 255}, {kRz, 0}, {kRx, 255}, {kRy, 128}, {kHat, 2}});

    probe(PadFamily::Generic, kEveryButtonState,
          {{button(1), 1}, {button(2), 1}, {button(3), 0}, {button(4), 1}, {button(5), 1}, {button(6), 0},
           {button(7), 1}, {button(8), 1}, {button(9), 1}, {button(10), 1}, {button(11), 1}, {button(12), 1},
           {button(13), 1}, {button(14), 1}, {button(15), 1}, {button(16), 1}});
    probe(PadFamily::Xbox, kEveryButtonState,
          {{button(1), 1}, {button(2), 1}, {button(3), 0}, {button(4), 1}, {button(5), 1}, {button(6), 0},
           {button(7), 1}, {button(8), 1}, {button(9), 0}, {button(10), 0}, {button(11), 1}, {button(12), 1},
           {button(13), 1}, {button(14), 1}, {button(15), 1}, {kRecord, 1}});
    probe(PadFamily::DualShock4, kEveryButtonState,
          {{button(1), 1}, {button(2), 1}, {button(3), 1}, {button(4), 1}, {button(5), 1}, {button(6), 1},
           {button(7), 1}, {button(8), 1}, {button(9), 1}, {button(10), 1}, {button(11), 1}, {button(12), 1},
           {button(13), 1}, {button(14), 1}});
    probe(PadFamily::DualSense, kEveryButtonState,
          {{button(1), 1}, {button(2), 1}, {button(3), 1}, {button(4), 1}, {button(5), 1}, {button(6), 1},
           {button(7), 1}, {button(8), 1}, {button(9), 1}, {button(10), 1}, {button(11), 1}, {button(12), 1},
           {button(13), 1}, {button(14), 1}, {button(15), 1}});

    probe(PadFamily::Xbox, {.buttons = pad_button::kSouth}, {{button(1), 1}, {button(2), 0}, {button(4), 0}});
    probe(PadFamily::DualShock4, {.buttons = pad_button::kSouth}, {{button(1), 0}, {button(2), 1}, {button(3), 0}});
    probe(PadFamily::DualSense, {.buttons = pad_button::kWest}, {{button(1), 1}, {button(2), 0}});
    probe(PadFamily::Generic, {.buttons = pad_button::kGuide}, {{button(13), 1}, {button(11), 0}});
}

TEST_CASE("rumble comes out of each pad's output report, other output reports leave it alone (ADR-0103)")
{
    const auto rumble = [](PadFamily family, std::string_view hex) {
        const std::vector<std::uint8_t> bytes = fromHex(hex);
        return padRumble(family, bytes);
    };

    CHECK(rumble(PadFamily::Xbox, "030300006432ff00ff") == PadRumble{.strong = 65535, .weak = 32767});
    CHECK(rumble(PadFamily::Xbox, "030f0a14c8000000eb") ==
          PadRumble{.strong = 65535, .weak = 0, .leftTrigger = 6553, .rightTrigger = 13107});
    CHECK(rumble(PadFamily::Xbox, "030064646464ff00ff") == PadRumble{});
    CHECK(rumble(PadFamily::Xbox, "0303000064") == std::nullopt);
    CHECK(rumble(PadFamily::Xbox, "040300006432ff00ff") == std::nullopt);

    std::vector<std::uint8_t> dualShock4(32, 0x00);
    dualShock4[0] = 0x05;
    dualShock4[1] = 0x01;
    dualShock4[4] = 0x80;
    dualShock4[5] = 0xFF;
    CHECK(padRumble(PadFamily::DualShock4, dualShock4) == PadRumble{.strong = 65535, .weak = 32896});
    dualShock4[1] = 0x07;
    dualShock4[4] = 0x00;
    dualShock4[5] = 0x00;
    CHECK(padRumble(PadFamily::DualShock4, dualShock4) == PadRumble{});
    dualShock4[1] = 0x02;
    dualShock4[5] = 0xFF;
    CHECK(padRumble(PadFamily::DualShock4, dualShock4) == std::nullopt);
    dualShock4[0] = 0x11;
    dualShock4[1] = 0x01;
    CHECK(padRumble(PadFamily::DualShock4, dualShock4) == std::nullopt);

    std::vector<std::uint8_t> dualSense(63, 0x00);
    dualSense[0] = 0x02;
    dualSense[1] = 0x03;
    dualSense[3] = 0x40;
    dualSense[4] = 0xC0;
    CHECK(padRumble(PadFamily::DualSense, dualSense) == PadRumble{.strong = 49344, .weak = 16448});
    dualSense[1] = 0x02;
    dualSense[39] = 0x04;
    dualSense[3] = 0x10;
    dualSense[4] = 0x20;
    CHECK(padRumble(PadFamily::DualSense, dualSense) == PadRumble{.strong = 8224, .weak = 4112});
    dualSense[39] = 0x02;
    CHECK(padRumble(PadFamily::DualSense, dualSense) == std::nullopt);
    const std::vector<std::uint8_t> sdlEffects = fromHex("020300ff7f");
    CHECK(padRumble(PadFamily::DualSense, sdlEffects) == PadRumble{.strong = 32639, .weak = 65535});

    CHECK(padRumble(PadFamily::Generic, dualSense) == std::nullopt);
    CHECK(padRumble(PadFamily::DualShock4, std::span<const std::uint8_t>{}) == std::nullopt);
}
