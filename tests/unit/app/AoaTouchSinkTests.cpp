#include "AoaTouchSink.h"
#include "ControlChannel.h"
#include "HidReports.h"
#include "HidTouchscreen.h"
#include "TouchSinkFakes.h"

#include <gs/input/Geometry.h>
#include <gs/protocol/Messages.h>
#include <gs/session/SessionState.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <vector>

using namespace gs;
using namespace gs::app;
using namespace gs::test;

namespace {

struct Contact {
    bool tip = false;
    std::uint32_t id = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};

std::vector<Contact> contactsOf(std::span<const std::uint8_t> report)
{
    const HidLayout layout = parseHid(uhidTouchDescriptor());
    const HidField* count = layout.field(HidKind::Input, 1, kHidContactCount);
    REQUIRE(count != nullptr);
    REQUIRE(report.size() == layout.bytes(HidKind::Input, 1));
    const std::uint32_t reported = hidValue(report, *count, layout.numbered);
    const auto read = [&](std::uint32_t usage, int finger) {
        const HidField* field = layout.field(HidKind::Input, 1, usage, finger);
        REQUIRE(field != nullptr);
        return hidValue(report, *field, layout.numbered);
    };
    std::vector<Contact> contacts;
    for (int finger = 0; finger < layout.fingers && static_cast<std::uint32_t>(finger) < reported; ++finger) {
        contacts.push_back({.tip = read(kHidTipSwitch, finger) != 0,
                            .id = read(kHidContactIdentifier, finger),
                            .x = read(kHidX, finger),
                            .y = read(kHidY, finger)});
    }
    return contacts;
}

std::int16_t signedAt(const std::vector<std::uint8_t>& bytes, std::size_t at)
{
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8U)));
}

struct Rig {
    std::shared_ptr<AoaBench> bench = std::make_shared<AoaBench>();
    AoaPort port;
    SinkLog injection;
    AoaTouchSink sink;
    ControlChannel channel;

    explicit Rig(bool adb = true, std::uint16_t version = 2, std::size_t packetSize = 64)
        : port(makeLink(bench, version, packetSize))
        , sink(adb ? &injection : nullptr, port, AoaDevices{.mouse = !adb, .keyboard = !adb})
    {
        channel.attach(sink);
        video(3, 0);
        port.flush();
    }

    static std::unique_ptr<AoaLink> makeLink(const std::shared_ptr<AoaBench>& bench, std::uint16_t version,
                                             std::size_t packetSize)
    {
        bench->version = version;
        bench->packetSize = packetSize;
        return std::make_unique<FakeAoaLink>(bench);
    }

    void video(std::uint16_t session, int rotation)
    {
        sink.setVideo(session, rotation);
        channel.setVideo(session, 1080, 2400);
        current = session;
    }

    bool frame(std::initializer_list<protocol::TouchPoint> points)
    {
        protocol::TouchFrame touch;
        touch.videoSession = current;
        for (const protocol::TouchPoint& point : points) {
            touch.points.push_back(point);
        }
        return channel.send(touch);
    }

    std::vector<AoaTransfer> events(std::uint16_t id)
    {
        port.flush();
        return bench->of(aoa::kSendHidEvent, id);
    }

    std::uint16_t current = 0;
};

}

TEST_CASE("the AOA touch screen asks the protocol first, then registers its descriptor in chunks of endpoint zero's "
          "packet size (ADR-0009, HID-003)")
{
    for (const std::size_t packet : {std::size_t{8}, std::size_t{64}}) {
        INFO("packet size ", packet);
        Rig rig(true, 2, packet);
        const std::vector<AoaTransfer> transfers = rig.bench->taken();
        REQUIRE(transfers.size() >= 2);
        CHECK(transfers[0].request == aoa::kGetProtocol);
        CHECK(transfers[1].request == aoa::kRegisterHid);
        CHECK(transfers[1].value == aoa::kTouchscreen);
        const std::span<const std::uint8_t> descriptor = uhidTouchDescriptor();
        CHECK(transfers[1].index == descriptor.size());
        CHECK(transfers[1].data.empty());
        std::vector<std::uint8_t> sent;
        std::size_t chunks = 0;
        for (const AoaTransfer& transfer : transfers) {
            if (transfer.request != aoa::kSetHidReportDescriptor) {
                continue;
            }
            CHECK(transfer.value == aoa::kTouchscreen);
            CHECK(transfer.index == sent.size());
            CHECK(transfer.data.size() <= packet);
            CHECK_FALSE(transfer.data.empty());
            sent.insert(sent.end(), transfer.data.begin(), transfer.data.end());
            ++chunks;
        }
        CHECK(std::ranges::equal(sent, descriptor));
        CHECK(chunks == (descriptor.size() + packet - 1) / packet);
        CHECK(rig.bench->of(aoa::kRegisterHid, aoa::kMouse).empty());
        CHECK(rig.bench->of(aoa::kRegisterHid, aoa::kKeyboard).empty());
        CHECK(rig.port.state() == AoaState::Ready);
        CHECK(rig.channel.capabilities() == SinkCapabilities{.maxContacts = kUhidMaxContacts, .keys = true, .cancel = false});
    }
}

TEST_CASE("touches become the UHID touch screen's reports through ACCESSORY_SEND_HID_EVENT, and detaching lifts every "
          "finger before unregistering (HID-003)")
{
    Rig rig;
    REQUIRE(rig.frame({pressAt(0, 0.25, 0.5), pressAt(1, 0.75, 0.5)}));
    std::vector<AoaTransfer> events = rig.events(aoa::kTouchscreen);
    REQUIRE(events.size() == 1);
    CHECK(events[0].index == 0);
    std::vector<Contact> contacts = contactsOf(events[0].data);
    REQUIRE(contacts.size() == 2);
    CHECK(contacts[0].tip);
    CHECK(contacts[1].tip);
    const UhidPosition first = naturalPosition(input::fixedCoordinate(0.25), input::fixedCoordinate(0.5), 0);
    CHECK(contacts[0].x == first.x);
    CHECK(contacts[0].y == first.y);
    CHECK(rig.injection.frames.empty());

    rig.channel.detach();
    rig.port.flush();
    const std::vector<AoaTransfer> transfers = rig.bench->taken();
    const auto unregister = std::ranges::find_if(transfers, [](const AoaTransfer& transfer) {
        return transfer.request == aoa::kUnregisterHid && transfer.value == aoa::kTouchscreen;
    });
    REQUIRE(unregister != transfers.end());
    CHECK(unregister + 1 == transfers.end());
    const AoaTransfer& release = *(unregister - 1);
    REQUIRE(release.request == aoa::kSendHidEvent);
    contacts = contactsOf(release.data);
    REQUIRE(contacts.size() == 2);
    CHECK_FALSE(contacts[0].tip);
    CHECK_FALSE(contacts[1].tip);
    CHECK(rig.injection.detaches == 1);
    CHECK_FALSE(rig.frame({pressAt(2, 0.5, 0.5)}));
}

TEST_CASE("stopping the session lifts held fingers and unregisters once, a reconnection only lifts them (HID-003)")
{
    Rig rig;
    REQUIRE(rig.frame({pressAt(0, 0.4, 0.4)}));
    rig.sink.phaseChanged(session::SessionPhase::Reconnecting);
    rig.port.flush();
    std::vector<AoaTransfer> events = rig.bench->of(aoa::kSendHidEvent, aoa::kTouchscreen);
    REQUIRE(events.size() == 2);
    CHECK_FALSE(contactsOf(events[1].data).front().tip);
    CHECK(rig.bench->of(aoa::kUnregisterHid, aoa::kTouchscreen).empty());

    REQUIRE(rig.frame({pressAt(0, 0.6, 0.6)}));
    rig.sink.phaseChanged(session::SessionPhase::Stopping);
    rig.sink.phaseChanged(session::SessionPhase::Stopped);
    rig.channel.detach();
    rig.port.flush();
    events = rig.bench->of(aoa::kSendHidEvent, aoa::kTouchscreen);
    REQUIRE(events.size() == 4);
    CHECK_FALSE(contactsOf(events[3].data).front().tip);
    CHECK(rig.bench->of(aoa::kUnregisterHid, aoa::kTouchscreen).size() == 1);
}

TEST_CASE("moves queued while the cable is busy merge into the latest position; presses and lifts never merge "
          "(HID-003)")
{
    Rig rig;
    rig.bench->pause(true);
    REQUIRE(rig.frame({pressAt(0, 0.1, 0.1)}));
    for (int step = 1; step <= 5; ++step) {
        REQUIRE(rig.frame({moveTo(0, 0.1 + 0.1 * step, 0.1)}));
    }
    REQUIRE(rig.frame({liftAt(0, 0.6, 0.1)}));
    REQUIRE(rig.frame({pressAt(1, 0.9, 0.9)}));
    rig.bench->pause(false);
    const std::vector<AoaTransfer> events = rig.events(aoa::kTouchscreen);
    REQUIRE(events.size() == 4);
    const std::vector<Contact> pressed = contactsOf(events[0].data);
    const std::vector<Contact> moved = contactsOf(events[1].data);
    const std::vector<Contact> lifted = contactsOf(events[2].data);
    REQUIRE(pressed.size() == 1);
    REQUIRE(moved.size() == 1);
    REQUIRE(lifted.size() == 1);
    CHECK(pressed[0].x == naturalPosition(input::fixedCoordinate(0.1), input::fixedCoordinate(0.1), 0).x);
    CHECK(moved[0].tip);
    CHECK(moved[0].x == naturalPosition(input::fixedCoordinate(0.6), input::fixedCoordinate(0.1), 0).x);
    CHECK_FALSE(lifted[0].tip);
    CHECK(contactsOf(events[3].data).front().tip);
}

TEST_CASE("every picked way of holding the phone puts touches where the phone's own screen has them (HID-003, "
          "HID-006)")
{
    for (const int rotation : {0, 1, 3}) {
        INFO("rotation ", rotation);
        Rig rig;
        rig.video(9, rotation);
        REQUIRE(rig.frame({pressAt(0, 0.0, 0.5)}));
        const std::vector<AoaTransfer> events = rig.events(aoa::kTouchscreen);
        REQUIRE(events.size() == 1);
        const std::vector<Contact> contacts = contactsOf(events[0].data);
        REQUIRE(contacts.size() == 1);
        const UhidPosition expected = naturalPosition(0, input::fixedCoordinate(0.5), rotation);
        CHECK(contacts[0].x == expected.x);
        CHECK(contacts[0].y == expected.y);
        if (rotation == 1) {
            CHECK(contacts[0].y == 0);
        } else if (rotation == 3) {
            CHECK(contacts[0].y == kUhidLogicalMaximum);
        } else {
            CHECK(contacts[0].x == 0);
        }
    }
}

TEST_CASE("with adb the keys, scrolling and text keep going through injection and no keyboard or mouse is made "
          "(HID-003)")
{
    Rig rig;
    protocol::Key key;
    key.keycode = 29;
    CHECK(rig.channel.send(key));
    CHECK(rig.channel.scroll({.x = 0.5, .y = 0.5}, 0, 256));
    CHECK(rig.channel.type("hi"));
    CHECK(rig.injection.keys.size() == 1);
    CHECK(rig.injection.scrolls.size() == 1);
    CHECK(rig.injection.texts.size() == 1);
    CHECK_FALSE(rig.sink.moveMouse(5, 5));
    CHECK_FALSE(rig.sink.pressKey(4));
    rig.port.flush();
    CHECK(rig.bench->of(aoa::kSendHidEvent, aoa::kKeyboard).empty());
    CHECK(rig.bench->of(aoa::kSendHidEvent, aoa::kMouse).empty());
}

TEST_CASE("without adb the mapping's keys go as keyboard reports, and back, home and recent apps as consumer keys "
          "(HID-003)")
{
    Rig rig(false);
    CHECK(rig.bench->of(aoa::kRegisterHid, aoa::kMouse).size() == 1);
    CHECK(rig.bench->of(aoa::kRegisterHid, aoa::kKeyboard).size() == 1);
    const HidLayout keyboard = parseHid(aoaKeyboardDescriptor());
    CHECK(keyboard.complete);
    CHECK(keyboard.bytes(HidKind::Input, aoa::kKeyboardReport) == aoa::kKeyboardReportSize);
    CHECK(keyboard.bytes(HidKind::Input, aoa::kConsumerReport) == aoa::kConsumerReportSize);

    protocol::Key key;
    key.keycode = 113;
    key.action = protocol::KeyAction::Down;
    REQUIRE(rig.channel.send(key));
    key.keycode = 29;
    REQUIRE(rig.channel.send(key));
    key.action = protocol::KeyAction::Up;
    REQUIRE(rig.channel.send(key));
    key.keycode = 113;
    REQUIRE(rig.channel.send(key));
    REQUIRE(rig.sink.pressKey(4));
    REQUIRE(rig.sink.pressKey(187));
    const std::vector<AoaTransfer> events = rig.events(aoa::kKeyboard);
    REQUIRE(events.size() == 8);
    CHECK(events[0].data == std::vector<std::uint8_t>{1, 0x01, 0, 0, 0, 0, 0, 0, 0});
    CHECK(events[1].data == std::vector<std::uint8_t>{1, 0x01, 0, 0x04, 0, 0, 0, 0, 0});
    CHECK(events[2].data == std::vector<std::uint8_t>{1, 0x01, 0, 0, 0, 0, 0, 0, 0});
    CHECK(events[3].data == std::vector<std::uint8_t>{1, 0, 0, 0, 0, 0, 0, 0, 0});
    CHECK(events[4].data == std::vector<std::uint8_t>{2, 0x24, 0x02});
    CHECK(events[5].data == std::vector<std::uint8_t>{2, 0, 0});
    CHECK(events[6].data == std::vector<std::uint8_t>{2, 0xA2, 0x01});
    CHECK(events[7].data == std::vector<std::uint8_t>{2, 0, 0});
    CHECK(rig.injection.keys.empty());

    CHECK(hidKeyOf(3) == HidKey{.report = aoa::kConsumerReport, .usage = 0x0223});
    CHECK(hidKeyOf(7) == HidKey{.report = aoa::kKeyboardReport, .usage = 0x27});
    CHECK(hidKeyOf(8) == HidKey{.report = aoa::kKeyboardReport, .usage = 0x1E});
    CHECK(hidKeyOf(62) == HidKey{.report = aoa::kKeyboardReport, .usage = 0x2C});
    CHECK(hidKeyOf(142) == HidKey{.report = aoa::kKeyboardReport, .usage = 0x45});
    CHECK_FALSE(hidKeyOf(1000).has_value());
}

TEST_CASE("without adb typed text goes as key presses with Shift where the US layout needs it (HID-003)")
{
    Rig rig(false);
    CHECK(rig.channel.type("aB!"));
    CHECK_FALSE(rig.channel.type("\xE4\xB8\xAD"));
    const std::vector<AoaTransfer> events = rig.events(aoa::kKeyboard);
    REQUIRE(events.size() == 6);
    CHECK(events[0].data == std::vector<std::uint8_t>{1, 0, 0, 0x04, 0, 0, 0, 0, 0});
    CHECK(events[1].data == std::vector<std::uint8_t>{1, 0, 0, 0, 0, 0, 0, 0, 0});
    CHECK(events[2].data == std::vector<std::uint8_t>{1, 0x02, 0, 0x05, 0, 0, 0, 0, 0});
    CHECK(events[4].data == std::vector<std::uint8_t>{1, 0x02, 0, 0x1E, 0, 0, 0, 0, 0});
}

TEST_CASE("the phone's pointer moves by relative reports that merge while the cable is busy; buttons and the wheel "
          "never merge (HID-003, HID-006)")
{
    Rig rig(false);
    const HidLayout mouse = parseHid(aoaMouseDescriptor());
    CHECK(mouse.complete);
    CHECK_FALSE(mouse.numbered);
    CHECK(mouse.bytes(HidKind::Input, 0) == aoa::kMouseReportSize);

    rig.bench->pause(true);
    REQUIRE(rig.sink.setMouseButtons(0x01));
    REQUIRE(rig.sink.moveMouse(3, 4));
    REQUIRE(rig.sink.moveMouse(5, -1));
    REQUIRE(rig.sink.moveMouse(-2, 0));
    REQUIRE(rig.sink.setMouseButtons(0));
    REQUIRE(rig.sink.scrollMouse(1, 0));
    REQUIRE(rig.sink.scrollMouse(1, 0));
    rig.bench->pause(false);
    const std::vector<AoaTransfer> events = rig.events(aoa::kMouse);
    REQUIRE(events.size() == 5);
    CHECK(events[0].data[0] == 0x01);
    CHECK(events[1].data[0] == 0x01);
    CHECK(signedAt(events[1].data, 1) == 6);
    CHECK(signedAt(events[1].data, 3) == 3);
    CHECK(events[2].data[0] == 0);
    CHECK(events[3].data[5] == 1);
    CHECK(events[4].data[5] == 1);
}

TEST_CASE("a phone without HID over AOA takes nothing, and an unplugged cable ends the port (HID-003)")
{
    {
        Rig rig(true, 1);
        CHECK(rig.port.state() == AoaState::Unsupported);
        CHECK_FALSE(rig.frame({pressAt(0, 0.5, 0.5)}));
        const std::vector<AoaTransfer> transfers = rig.bench->taken();
        REQUIRE(transfers.size() == 1);
        CHECK(transfers[0].request == aoa::kGetProtocol);
    }
    {
        Rig rig;
        REQUIRE(rig.frame({pressAt(0, 0.5, 0.5)}));
        rig.port.flush();
        rig.bench->unplug();
        static_cast<void>(rig.frame({moveTo(0, 0.6, 0.6)}));
        rig.port.flush();
        CHECK(rig.port.state() == AoaState::Gone);
        CHECK_FALSE(rig.frame({moveTo(0, 0.7, 0.7)}));
    }
}
