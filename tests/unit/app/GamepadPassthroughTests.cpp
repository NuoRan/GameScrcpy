#include "AoaTouchSink.h"
#include "ControlChannel.h"
#include "GamepadPassthrough.h"
#include "InputHost.h"
#include "TouchSinkFakes.h"
#include "UhidTouchSink.h"

#include <gs/input/DeviceEvents.h>
#include <gs/input/PadReports.h>
#include <gs/input/Profile.h>
#include <gs/protocol/Messages.h>

#include <doctest/doctest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace gs;
using namespace gs::app;
using namespace std::chrono_literals;

namespace {

using PadMessage =
    std::variant<protocol::UhidFeature, protocol::UhidCreate, protocol::UhidInput, protocol::UhidDestroy>;

class PadSink final : public ControlSink
{
public:
    bool send(const protocol::TouchFrame& frame) override
    {
        frames.push_back(frame);
        return true;
    }

    bool send(const protocol::Key&) override { return true; }
    bool send(const protocol::Scroll&) override { return true; }
    bool send(const protocol::Text&) override { return true; }
    bool padFeature(const protocol::UhidFeature& feature) override { return keep(feature); }
    bool padCreate(const protocol::UhidCreate& create) override { return keep(create); }
    bool padInput(const protocol::UhidInput& input) override { return keep(input); }
    bool padDestroy(const protocol::UhidDestroy& destroy) override { return keep(destroy); }

    template <typename M>
    [[nodiscard]] std::vector<M> sent() const
    {
        std::vector<M> found;
        for (const PadMessage& message : messages) {
            if (const auto* typed = std::get_if<M>(&message)) {
                found.push_back(*typed);
            }
        }
        return found;
    }

    std::vector<protocol::TouchFrame> frames;
    std::vector<PadMessage> messages;

private:
    bool keep(PadMessage message)
    {
        messages.push_back(std::move(message));
        return true;
    }
};

struct Notice {
    const ControlChannel* channel = nullptr;
    PadNotice notice = PadNotice::Handed;
    input::PadFamily family = input::PadFamily::Generic;
};

struct Rig {
    PadSink sink;
    std::shared_ptr<ControlChannel> channel = std::make_shared<ControlChannel>();
    std::vector<input::PadRumble> rumbles;
    std::vector<Notice> notices;
    GamepadPassthrough pad{{.rumble = [this](const input::PadRumble& rumble) { rumbles.push_back(rumble); },
                            .notice =
                                [this](const ControlChannel* on, PadNotice notice, input::PadFamily family) {
                                    notices.push_back({.channel = on, .notice = notice, .family = family});
                                }}};

    explicit Rig(PadRoute route = PadRoute::Uhid, bool reports = true)
    {
        channel->attach(sink);
        static_cast<void>(channel->setPadLink({.route = route, .epoch = 1, .reports = reports}));
    }

    ~Rig() { channel->detach(); }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
};

platform::PadState padOf(platform::PadKind kind, std::uint16_t buttons = 0)
{
    return {.connected = true, .buttons = buttons, .kind = kind};
}

std::vector<std::uint8_t> bytesOf(std::span<const std::uint8_t> data)
{
    return {data.begin(), data.end()};
}

std::shared_ptr<const input::Profile> profileOf(std::string_view json)
{
    const input::LoadResult loaded = input::loadProfile(json);
    REQUIRE(loaded.ok());
    return std::make_shared<const input::Profile>(loaded.profile);
}

constexpr std::string_view kMappedPad = R"({"format":"gamescrcpy.profile","version":3,
    "controls":[{"id":"fire","type":"hold","anchor":"right","offset":[-0.3,0],"inputs":{"press":"Gamepad.A"}}]})";

constexpr std::string_view kHandedPad = R"({"format":"gamescrcpy.profile","version":3,"gamepad":"passthrough",
    "controls":[{"id":"fire","type":"hold","anchor":"right","offset":[-0.3,0],"inputs":{"press":"Gamepad.A"}}]})";

}

TEST_CASE("a DualShock 4 on the computer becomes a DualShock 4 on the phone, feature reports first (ADR-0103)")
{
    Rig rig;
    rig.pad.setTarget(rig.channel, true, 0us);
    rig.pad.setActive(true, 0us);
    CHECK(rig.pad.takesPad());
    CHECK(rig.sink.messages.empty());

    rig.pad.gamepad(padOf(platform::PadKind::PlayStation4), 1000us);
    const input::PadModel& model = input::padModel(input::PadFamily::DualShock4);
    REQUIRE(rig.sink.messages.size() == model.features.size() + 2);
    for (std::size_t i = 0; i < model.features.size(); ++i) {
        const auto* feature = std::get_if<protocol::UhidFeature>(&rig.sink.messages[i]);
        REQUIRE(feature != nullptr);
        CHECK(feature->id == kPadIdFirst);
        CHECK(feature->reportNumber == model.features[i].reportNumber);
        CHECK(feature->data == bytesOf(model.features[i].data));
    }
    const auto* create = std::get_if<protocol::UhidCreate>(&rig.sink.messages[model.features.size()]);
    REQUIRE(create != nullptr);
    CHECK(create->id == kPadIdFirst);
    CHECK(create->vendorId == 0x054C);
    CHECK(create->productId == 0x09CC);
    CHECK(create->bus == protocol::UhidCreate::kBusUsb);
    CHECK(create->name == std::string(model.name));
    CHECK(create->reportDescriptor == bytesOf(model.descriptor));
    const auto* first = std::get_if<protocol::UhidInput>(&rig.sink.messages.back());
    REQUIRE(first != nullptr);
    CHECK(first->id == kPadIdFirst);
    CHECK(first->report == input::padInputReport(input::PadFamily::DualShock4, {}));
    CHECK(rig.pad.family() == input::PadFamily::DualShock4);
    CHECK(rig.notices.empty());

    rig.pad.status(rig.channel.get(), kPadIdFirst, true, true, 2000us);
    REQUIRE(rig.notices.size() == 1);
    CHECK(rig.notices[0].notice == PadNotice::Handed);
    CHECK(rig.notices[0].family == input::PadFamily::DualShock4);
    CHECK(rig.notices[0].channel == rig.channel.get());
    rig.pad.status(rig.channel.get(), kPadIdFirst, true, true, 3000us);
    CHECK(rig.notices.size() == 1);
}

TEST_CASE("an Xbox pad becomes an Xbox Wireless Controller on Bluetooth, other pads a generic one (ADR-0103)")
{
    Rig rig;
    rig.pad.setTarget(rig.channel, true, 0us);
    rig.pad.gamepad(padOf(platform::PadKind::Xbox), 0us);
    auto creates = rig.sink.sent<protocol::UhidCreate>();
    REQUIRE(creates.size() == 1);
    CHECK(creates[0].vendorId == 0x045E);
    CHECK(creates[0].productId == 0x0B13);
    CHECK(creates[0].bus == protocol::UhidCreate::kBusBluetooth);
    CHECK(rig.sink.sent<protocol::UhidFeature>().empty());

    rig.pad.gamepad({}, 1000us);
    rig.pad.gamepad(padOf(platform::PadKind::Other), 2000us);
    creates = rig.sink.sent<protocol::UhidCreate>();
    REQUIRE(creates.size() == 2);
    CHECK(creates[1].id == kPadIdFirst + 1);
    CHECK(creates[1].vendorId == input::padModel(input::PadFamily::Generic).vendorId);
    CHECK(creates[1].productId == input::padModel(input::PadFamily::Generic).productId);
    CHECK(rig.pad.family() == input::PadFamily::Generic);

    CHECK(padFamilyOf(platform::PadKind::Xbox) == input::PadFamily::Xbox);
    CHECK(padFamilyOf(platform::PadKind::PlayStation4) == input::PadFamily::DualShock4);
    CHECK(padFamilyOf(platform::PadKind::PlayStation5) == input::PadFamily::DualSense);
    CHECK(padFamilyOf(platform::PadKind::Other) == input::PadFamily::Generic);
}

TEST_CASE("input reports go only when the pad changes, at most one every 4 ms (ADR-0103)")
{
    Rig rig;
    rig.pad.setTarget(rig.channel, true, 0us);
    rig.pad.setActive(true, 0us);
    rig.pad.gamepad(padOf(platform::PadKind::Other), 10'000us);
    CHECK(rig.sink.sent<protocol::UhidInput>().size() == 1);

    rig.pad.gamepad(padOf(platform::PadKind::Other), 20'000us);
    CHECK(rig.sink.sent<protocol::UhidInput>().size() == 1);

    rig.pad.gamepad(padOf(platform::PadKind::Other, input::xinput::kA), 30'000us);
    CHECK(rig.sink.sent<protocol::UhidInput>().size() == 2);

    rig.pad.gamepad(padOf(platform::PadKind::Other, input::xinput::kB), 31'000us);
    CHECK(rig.sink.sent<protocol::UhidInput>().size() == 2);
    CHECK(rig.pad.nextWakeup() == std::optional<Microseconds>(34'000us));
    rig.pad.gamepad(padOf(platform::PadKind::Other, input::xinput::kX), 32'000us);
    rig.pad.wake(33'000us);
    CHECK(rig.sink.sent<protocol::UhidInput>().size() == 2);

    rig.pad.wake(34'000us);
    const auto inputs = rig.sink.sent<protocol::UhidInput>();
    REQUIRE(inputs.size() == 3);
    CHECK(inputs.back().report ==
          input::padInputReport(input::PadFamily::Generic, {.buttons = input::pad_button::kWest}));
    CHECK_FALSE(rig.pad.nextWakeup().has_value());
}

TEST_CASE("a window that is not in front sends the released pad, the held state comes back with it (ADR-0103)")
{
    Rig rig;
    rig.pad.setTarget(rig.channel, true, 0us);
    rig.pad.setActive(true, 0us);
    rig.pad.gamepad(padOf(platform::PadKind::Other, input::xinput::kA), 0us);
    rig.pad.setActive(false, 10'000us);
    auto inputs = rig.sink.sent<protocol::UhidInput>();
    REQUIRE(inputs.size() == 2);
    CHECK(inputs.back().report == input::padInputReport(input::PadFamily::Generic, {}));

    rig.pad.gamepad(padOf(platform::PadKind::Other, input::xinput::kB), 20'000us);
    CHECK(rig.sink.sent<protocol::UhidInput>().size() == 2);
    rig.pad.setActive(true, 30'000us);
    inputs = rig.sink.sent<protocol::UhidInput>();
    REQUIRE(inputs.size() == 3);
    CHECK(inputs.back().report ==
          input::padInputReport(input::PadFamily::Generic, {.buttons = input::pad_button::kEast}));
    CHECK(rig.sink.sent<protocol::UhidDestroy>().empty());
}

TEST_CASE("a phone that does not recognise the DualSense gets a generic gamepad once, without vibration (ADR-0103)")
{
    Rig rig;
    rig.pad.setTarget(rig.channel, true, 0us);
    rig.pad.setActive(true, 0us);
    rig.pad.gamepad(padOf(platform::PadKind::PlayStation5), 0us);
    REQUIRE(rig.pad.deviceId() == std::optional<std::uint16_t>(kPadIdFirst));
    rig.pad.status(rig.channel.get(), static_cast<std::uint16_t>(kPadIdFirst + 7), false, false, 1000us);
    rig.pad.status(nullptr, kPadIdFirst, false, false, 1000us);
    CHECK(rig.pad.family() == input::PadFamily::DualSense);
    rig.sink.messages.clear();

    rig.pad.status(rig.channel.get(), kPadIdFirst, false, false, 1'500'000us);
    REQUIRE_FALSE(rig.sink.messages.empty());
    const auto* destroy = std::get_if<protocol::UhidDestroy>(&rig.sink.messages.front());
    REQUIRE(destroy != nullptr);
    CHECK(destroy->id == kPadIdFirst);
    const auto creates = rig.sink.sent<protocol::UhidCreate>();
    REQUIRE(creates.size() == 1);
    CHECK(creates[0].id == kPadIdFirst + 1);
    CHECK(creates[0].vendorId == input::padModel(input::PadFamily::Generic).vendorId);
    CHECK(rig.sink.sent<protocol::UhidFeature>().empty());
    CHECK(rig.pad.family() == input::PadFamily::Generic);
    REQUIRE(rig.notices.size() == 1);
    CHECK(rig.notices[0].notice == PadNotice::FellBack);
    CHECK(rig.notices[0].family == input::PadFamily::DualSense);

    rig.pad.status(rig.channel.get(), static_cast<std::uint16_t>(kPadIdFirst + 1), true, false, 1'600'000us);
    REQUIRE(rig.notices.size() == 2);
    CHECK(rig.notices[1].notice == PadNotice::Handed);
    CHECK(rig.notices[1].family == input::PadFamily::Generic);

    rig.pad.gamepad({}, 2'000'000us);
    rig.pad.gamepad(padOf(platform::PadKind::PlayStation5), 3'000'000us);
    CHECK(rig.pad.family() == input::PadFamily::Generic);

    rig.sink.messages.clear();
    rig.pad.status(rig.channel.get(), *rig.pad.deviceId(), false, false, 4'000'000us);
    CHECK(rig.sink.sent<protocol::UhidDestroy>().empty());
    CHECK(rig.sink.sent<protocol::UhidCreate>().empty());
    CHECK(rig.pad.family() == input::PadFamily::Generic);
}

TEST_CASE("the game's rumble reaches the computer's gamepad and stops when the pad leaves (ADR-0103)")
{
    Rig rig;
    rig.pad.setTarget(rig.channel, true, 0us);
    rig.pad.gamepad(padOf(platform::PadKind::PlayStation4), 0us);
    REQUIRE(rig.pad.deviceId().has_value());
    const std::uint16_t id = *rig.pad.deviceId();
    std::vector<std::uint8_t> output(32, 0x00);
    output[0] = 0x05;
    output[1] = 0x01;
    output[4] = 0x40;
    output[5] = 0xC0;
    rig.pad.output(rig.channel.get(), id, output);
    REQUIRE(rig.rumbles.size() == 1);
    CHECK(rig.rumbles[0] == input::PadRumble{.strong = 0xC0 * 257, .weak = 0x40 * 257});

    rig.pad.output(rig.channel.get(), static_cast<std::uint16_t>(id + 1), output);
    rig.pad.output(nullptr, id, output);
    output[1] = 0x02;
    rig.pad.output(rig.channel.get(), id, output);
    CHECK(rig.rumbles.size() == 1);

    rig.pad.gamepad({}, 1000us);
    REQUIRE(rig.rumbles.size() == 2);
    CHECK(rig.rumbles[1] == input::PadRumble{});
    CHECK(rig.sink.sent<protocol::UhidDestroy>().size() == 1);
    CHECK_FALSE(rig.pad.deviceId().has_value());
}

TEST_CASE("the pad leaves the phone when the profile maps it again, another window takes over or the phone goes (ADR-0103)")
{
    Rig rig;
    rig.pad.setTarget(rig.channel, true, 0us);
    rig.pad.setActive(true, 0us);
    rig.pad.gamepad(padOf(platform::PadKind::Other, input::xinput::kA), 0us);
    rig.pad.setTarget(rig.channel, false, 1000us);
    REQUIRE(rig.sink.messages.size() >= 2);
    const auto* released = std::get_if<protocol::UhidInput>(&rig.sink.messages[rig.sink.messages.size() - 2]);
    REQUIRE(released != nullptr);
    CHECK(released->report == input::padInputReport(input::PadFamily::Generic, {}));
    CHECK(std::holds_alternative<protocol::UhidDestroy>(rig.sink.messages.back()));
    CHECK_FALSE(rig.pad.takesPad());

    rig.pad.setTarget(rig.channel, true, 2000us);
    CHECK(rig.sink.sent<protocol::UhidCreate>().size() == 2);
    PadSink elsewhere;
    auto other = std::make_shared<ControlChannel>();
    other->attach(elsewhere);
    static_cast<void>(other->setPadLink({.route = PadRoute::Uhid, .epoch = 1, .reports = true}));
    rig.pad.setTarget(other, false, 3000us);
    CHECK(rig.sink.sent<protocol::UhidDestroy>().size() == 2);
    CHECK(elsewhere.messages.empty());

    rig.pad.setTarget(rig.channel, true, 4000us);
    CHECK(rig.sink.sent<protocol::UhidCreate>().size() == 3);
    rig.pad.forget(rig.channel.get());
    CHECK(rig.sink.sent<protocol::UhidDestroy>().size() == 3);
    CHECK_FALSE(rig.pad.takesPad());
    CHECK_FALSE(rig.pad.deviceId().has_value());
    other->detach();
}

TEST_CASE("a new session makes the pad again without destroying the one that went with the old session (ADR-0103)")
{
    Rig rig;
    rig.pad.setTarget(rig.channel, true, 0us);
    rig.pad.gamepad(padOf(platform::PadKind::Xbox), 0us);
    REQUIRE(rig.sink.sent<protocol::UhidCreate>().size() == 1);

    static_cast<void>(rig.channel->setPadLink({.route = PadRoute::Uhid, .epoch = 2, .reports = true}));
    rig.pad.linkChanged(nullptr, 15'000us);
    CHECK(rig.sink.sent<protocol::UhidCreate>().size() == 1);
    rig.pad.linkChanged(rig.channel.get(), 20'000us);
    const auto creates = rig.sink.sent<protocol::UhidCreate>();
    REQUIRE(creates.size() == 2);
    CHECK(creates[1].id != creates[0].id);
    CHECK(creates[1].bus == protocol::UhidCreate::kBusBluetooth);
    CHECK(rig.sink.sent<protocol::UhidDestroy>().empty());
}

TEST_CASE("when the touches move to AOA in the same session the UHID pad leaves and a generic one comes (ADR-0103)")
{
    Rig rig;
    rig.pad.setTarget(rig.channel, true, 0us);
    rig.pad.setActive(true, 0us);
    rig.pad.gamepad(padOf(platform::PadKind::PlayStation4, input::xinput::kA), 0us);
    REQUIRE(rig.pad.deviceId().has_value());
    const std::uint16_t first = *rig.pad.deviceId();

    static_cast<void>(rig.channel->setPadLink({.route = PadRoute::Aoa, .epoch = 1, .reports = true}));
    rig.pad.linkChanged(rig.channel.get(), 10'000us);
    const auto destroys = rig.sink.sent<protocol::UhidDestroy>();
    REQUIRE(destroys.size() == 1);
    CHECK(destroys[0].id == first);
    const auto creates = rig.sink.sent<protocol::UhidCreate>();
    REQUIRE(creates.size() == 2);
    CHECK(creates[1].vendorId == input::padModel(input::PadFamily::Generic).vendorId);
    CHECK(rig.pad.family() == input::PadFamily::Generic);

    static_cast<void>(rig.channel->setPadLink({.route = PadRoute::None, .epoch = 1, .reports = true}));
    rig.pad.linkChanged(rig.channel.get(), 20'000us);
    CHECK_FALSE(rig.pad.deviceId().has_value());
    CHECK(rig.sink.sent<protocol::UhidDestroy>().size() == 2);
}

TEST_CASE("a phone without UHID gets no pad and one notice about it (ADR-0103)")
{
    Rig rig(PadRoute::Missing);
    rig.pad.setTarget(rig.channel, true, 0us);
    rig.pad.gamepad(padOf(platform::PadKind::PlayStation4), 0us);
    rig.pad.gamepad(padOf(platform::PadKind::PlayStation4, input::xinput::kA), 1000us);
    CHECK(rig.sink.messages.empty());
    REQUIRE(rig.notices.size() == 1);
    CHECK(rig.notices[0].notice == PadNotice::NeedsUhid);
    CHECK(rig.pad.takesPad());
}

TEST_CASE("over AOA the pad is a generic gamepad registered as HID device 4, without vibration (ADR-0103)")
{
    auto bench = std::make_shared<test::AoaBench>();
    AoaPort port(std::make_unique<test::FakeAoaLink>(bench));
    test::SinkLog injection;
    AoaTouchSink sink(&injection, port, AoaDevices{});
    auto channel = std::make_shared<ControlChannel>();
    channel->attach(sink);
    static_cast<void>(channel->setPadLink({.route = PadRoute::Aoa, .epoch = 1}));
    std::vector<Notice> notices;
    GamepadPassthrough pad({.notice = [&notices](const ControlChannel* on, PadNotice notice, input::PadFamily family) {
        notices.push_back({.channel = on, .notice = notice, .family = family});
    }});
    pad.setTarget(channel, true, 0us);
    pad.setActive(true, 0us);
    pad.gamepad(padOf(platform::PadKind::PlayStation5, input::xinput::kA), 0us);
    port.flush();

    const auto registered = bench->of(aoa::kRegisterHid, aoa::kGamepad);
    REQUIRE(registered.size() == 1);
    CHECK(registered[0].index == input::padModel(input::PadFamily::Generic).descriptor.size());
    const auto events = bench->of(aoa::kSendHidEvent, aoa::kGamepad);
    REQUIRE(events.size() == 1);
    CHECK(events[0].data == input::padInputReport(input::PadFamily::Generic, {.buttons = input::pad_button::kSouth}));
    CHECK(pad.family() == input::PadFamily::Generic);
    REQUIRE(notices.size() == 1);
    CHECK(notices[0].notice == PadNotice::Handed);
    CHECK(notices[0].family == input::PadFamily::Generic);

    pad.gamepad({}, 10'000us);
    port.flush();
    CHECK(bench->of(aoa::kUnregisterHid, aoa::kGamepad).size() == 1);
    const auto after = bench->of(aoa::kSendHidEvent, aoa::kGamepad);
    REQUIRE(after.size() == 2);
    CHECK(after.back().data == input::padInputReport(input::PadFamily::Generic, {}));
    channel->detach();
}

TEST_CASE("an AOA phone lets go of a held pad when it detaches, and takes device 4 off with the rest (ADR-0103)")
{
    auto bench = std::make_shared<test::AoaBench>();
    AoaPort port(std::make_unique<test::FakeAoaLink>(bench));
    AoaTouchSink sink(nullptr, port, AoaDevices{});
    protocol::UhidCreate create;
    create.reportDescriptor = bytesOf(input::padModel(input::PadFamily::Generic).descriptor);
    REQUIRE(sink.padCreate(create));
    protocol::UhidInput held;
    held.report = input::padInputReport(input::PadFamily::Generic, {.buttons = input::pad_button::kSouth, .hat = 0});
    REQUIRE(sink.padInput(held));
    sink.detached();
    port.flush();

    const auto events = bench->of(aoa::kSendHidEvent, aoa::kGamepad);
    REQUIRE(events.size() == 2);
    CHECK(events.back().data == input::padInputReport(input::PadFamily::Generic, {}));
    CHECK(bench->of(aoa::kUnregisterHid, aoa::kGamepad).size() == 1);
    CHECK_FALSE(sink.padInput(held));
}

TEST_CASE("the AOA touch screen passes a pad that is not its device 4 on to the session (ADR-0103)")
{
    auto bench = std::make_shared<test::AoaBench>();
    AoaPort port(std::make_unique<test::FakeAoaLink>(bench));
    PadSink session;
    AoaTouchSink sink(&session, port, AoaDevices{});
    protocol::UhidInput stale;
    stale.id = kPadIdFirst;
    stale.report = input::padInputReport(input::PadFamily::DualShock4, {});
    protocol::UhidDestroy gone;
    gone.id = kPadIdFirst;
    CHECK(sink.padInput(stale));
    CHECK(sink.padDestroy(gone));

    protocol::UhidCreate create;
    create.id = static_cast<std::uint16_t>(kPadIdFirst + 1);
    create.reportDescriptor = bytesOf(input::padModel(input::PadFamily::Generic).descriptor);
    REQUIRE(sink.padCreate(create));
    protocol::UhidInput own;
    own.id = create.id;
    own.report = input::padInputReport(input::PadFamily::Generic, {.buttons = input::pad_button::kNorth});
    CHECK(sink.padInput(own));
    CHECK(sink.padInput(stale));
    protocol::UhidDestroy mine;
    mine.id = create.id;
    CHECK(sink.padDestroy(mine));
    port.flush();

    CHECK(session.sent<protocol::UhidInput>().size() == 2);
    CHECK(session.sent<protocol::UhidDestroy>().size() == 1);
    CHECK(session.sent<protocol::UhidCreate>().empty());
    CHECK(bench->of(aoa::kRegisterHid, aoa::kGamepad).size() == 1);
    CHECK(bench->of(aoa::kSendHidEvent, aoa::kGamepad).size() == 1);
    CHECK(bench->of(aoa::kUnregisterHid, aoa::kGamepad).size() == 1);
}

TEST_CASE("an external gamepad gets the pad's state, and without one that route reports it cannot (ADR-0103)")
{
    Rig rig(PadRoute::External);
    rig.pad.setTarget(rig.channel, true, 0us);
    rig.pad.setActive(true, 0us);
    rig.pad.gamepad(padOf(platform::PadKind::Xbox, input::xinput::kA), 0us);
    REQUIRE(rig.notices.size() == 1);
    CHECK(rig.notices[0].notice == PadNotice::Unavailable);

    std::vector<input::PadPassState> states;
    rig.pad.setExternalSink([&states](const input::PadPassState& state) { states.push_back(state); });
    rig.pad.linkChanged(rig.channel.get(), 10'000us);
    REQUIRE(states.size() == 1);
    CHECK(states[0] == input::PadPassState{.buttons = input::pad_button::kSouth});
    CHECK(rig.sink.messages.empty());
    REQUIRE(rig.notices.size() == 2);
    CHECK(rig.notices[1].notice == PadNotice::Handed);

    rig.pad.gamepad({}, 20'000us);
    REQUIRE(states.size() == 2);
    CHECK(states[1] == input::PadPassState{});
}

TEST_CASE("the computer's pad turns into the HID pad state: hat from the d-pad, Y downward, extra buttons (ADR-0103)")
{
    const platform::PadState state{
        .connected = true,
        .buttons = static_cast<std::uint16_t>(input::xinput::kDpadUp | input::xinput::kDpadRight | input::xinput::kStart),
        .leftTrigger = 200,
        .rightTrigger = 10,
        .leftX = -32768,
        .leftY = 32767,
        .rightX = 100,
        .rightY = -32768,
        .extra = static_cast<std::uint8_t>(platform::pad_extra::kGuide | platform::pad_extra::kTouchpad),
        .kind = platform::PadKind::PlayStation5};
    const input::PadPassState pass = padPassStateOf(state);
    CHECK(pass.hat == 1);
    CHECK(pass.buttons == (input::pad_button::kStart | input::pad_button::kGuide | input::pad_button::kTouchpad |
                           input::pad_button::kLeftTrigger));
    CHECK(pass.leftX == -32768);
    CHECK(pass.leftY == -32767);
    CHECK(pass.rightX == 100);
    CHECK(pass.rightY == 32767);
    CHECK(pass.leftTrigger == 200);
    CHECK(pass.rightTrigger == 10);

    const auto hat = [](std::uint16_t dpad) { return padPassStateOf({.connected = true, .buttons = dpad}).hat; };
    CHECK(hat(0) == input::kPadHatCentred);
    CHECK(hat(input::xinput::kDpadUp) == 0);
    CHECK(hat(input::xinput::kDpadRight) == 2);
    CHECK(hat(static_cast<std::uint16_t>(input::xinput::kDpadDown | input::xinput::kDpadRight)) == 3);
    CHECK(hat(input::xinput::kDpadDown) == 4);
    CHECK(hat(static_cast<std::uint16_t>(input::xinput::kDpadDown | input::xinput::kDpadLeft)) == 5);
    CHECK(hat(input::xinput::kDpadLeft) == 6);
    CHECK(hat(static_cast<std::uint16_t>(input::xinput::kDpadUp | input::xinput::kDpadLeft)) == 7);
    CHECK(hat(static_cast<std::uint16_t>(input::xinput::kDpadUp | input::xinput::kDpadDown)) == input::kPadHatCentred);
    CHECK(padPassStateOf({.connected = false, .buttons = input::xinput::kA}) == input::PadPassState{});
}

TEST_CASE("a UHID touch screen passes the pad's messages on to the session (ADR-0103)")
{
    PadSink session;
    test::WireLog wire;
    UhidTouchSink touch(session, wire);
    protocol::UhidFeature feature;
    feature.id = kPadIdFirst;
    protocol::UhidCreate create;
    create.id = kPadIdFirst;
    protocol::UhidInput input;
    input.id = kPadIdFirst;
    protocol::UhidDestroy destroy;
    destroy.id = kPadIdFirst;
    CHECK(touch.padFeature(feature));
    CHECK(touch.padCreate(create));
    CHECK(touch.padInput(input));
    CHECK(touch.padDestroy(destroy));
    CHECK(session.messages.size() == 4);
    CHECK(wire.messages.empty());
    test::SinkLog plain;
    CHECK_FALSE(plain.padCreate(create));
}

TEST_CASE("the input host hands the pad to the passthrough instead of the key mapping when the profile says so (ADR-0103)")
{
    PadSink sink;
    auto channel = std::make_shared<ControlChannel>();
    channel->attach(sink);
    channel->setVideo(7, 2400, 1080);
    static_cast<void>(channel->setPadLink({.route = PadRoute::Uhid, .epoch = 1}));
    std::vector<Notice> notices;
    InputHost host({.pressedMouse = [] { return std::uint8_t{0}; },
                    .padNotice = [&notices](const ControlChannel* on, PadNotice notice, input::PadFamily family) {
        notices.push_back({.channel = on, .notice = notice, .family = family});
    }});
    host.setChannel(channel, 0us);
    host.setProfile(profileOf(kMappedPad), 0us);
    host.setActive(true, 0us);
    host.gamepad({.connected = true, .buttons = input::xinput::kA}, 1000us);
    CHECK(host.activeTouches() == 1);
    CHECK(sink.messages.empty());

    host.setProfile(profileOf(kHandedPad), 2000us);
    CHECK(host.activeTouches() == 0);
    REQUIRE(sink.sent<protocol::UhidCreate>().size() == 1);
    REQUIRE(notices.size() == 1);
    CHECK(notices[0].notice == PadNotice::Handed);
    const std::size_t frames = sink.frames.size();
    host.gamepad({.connected = true, .buttons = input::xinput::kB}, 10'000us);
    CHECK(sink.frames.size() == frames);
    CHECK(host.activeTouches() == 0);
    CHECK(sink.sent<protocol::UhidInput>().back().report ==
          input::padInputReport(input::PadFamily::Generic, {.buttons = input::pad_button::kEast}));

    host.setProfile(profileOf(kMappedPad), 20'000us);
    CHECK(sink.sent<protocol::UhidDestroy>().size() == 1);
    host.gamepad({.connected = true, .buttons = input::xinput::kA}, 30'000us);
    CHECK(host.activeTouches() == 1);
    host.forget(channel.get(), 40'000us);
    channel->detach();
}

TEST_CASE("the gamepad notices read as the toasts on the mirror window (ADR-0103)")
{
    CHECK(padNoticeText(PadNotice::Handed, input::PadFamily::DualShock4) ==
          QStringLiteral("The gamepad goes to the game (as DualShock 4)"));
    CHECK(padNoticeText(PadNotice::Handed, input::PadFamily::Xbox) ==
          QStringLiteral("The gamepad goes to the game (as Xbox Wireless Controller)"));
    CHECK(padNoticeText(PadNotice::Handed, input::PadFamily::Generic) ==
          QStringLiteral("The gamepad goes to the game as a generic gamepad"));
    CHECK(padNoticeText(PadNotice::FellBack, input::PadFamily::DualSense) ==
          QStringLiteral("This phone does not recognise DualSense; it now gets a generic gamepad without vibration"));
    CHECK(padNoticeText(PadNotice::NeedsUhid, input::PadFamily::DualSense) ==
          QStringLiteral("This phone cannot take the gamepad directly (it needs UHID)"));
    CHECK(padNoticeText(PadNotice::Unavailable, input::PadFamily::Generic) ==
          QStringLiteral("The gamepad cannot go to the game over this connection yet"));
}
