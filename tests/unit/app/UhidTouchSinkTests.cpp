#include "ControlChannel.h"
#include "HidReports.h"
#include "TouchSinkFakes.h"
#include "UhidTouchSink.h"

#include <gs/input/Geometry.h>
#include <gs/protocol/Messages.h>
#include <gs/session/SessionState.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
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

std::vector<Contact> contactsOf(const protocol::UhidInput& input)
{
    const HidLayout layout = parseHid(uhidTouchDescriptor());
    const HidField* count = layout.field(HidKind::Input, 1, kHidContactCount);
    REQUIRE(count != nullptr);
    REQUIRE(input.report.size() == layout.bytes(HidKind::Input, 1));
    const std::uint32_t reported = hidValue(input.report, *count, layout.numbered);
    const auto read = [&](std::uint32_t usage, int finger) {
        const HidField* field = layout.field(HidKind::Input, 1, usage, finger);
        REQUIRE(field != nullptr);
        return hidValue(input.report, *field, layout.numbered);
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

std::size_t tipsOf(const std::vector<Contact>& contacts)
{
    return static_cast<std::size_t>(std::ranges::count_if(contacts, [](const Contact& contact) { return contact.tip; }));
}

double spread(int finger)
{
    return 0.05 + 0.05 * finger;
}

struct Rig {
    SinkLog injection;
    WireLog wire;
    UhidTouchSink sink{injection, wire};
    ControlChannel channel;
    std::uint16_t current = 0;

    explicit Rig(bool offered = true, bool streaming = true)
    {
        sink.welcomed(offered ? protocol::Welcome::kUhid : 0U);
        if (streaming) {
            sink.phaseChanged(session::SessionPhase::Streaming);
        }
        channel.attach(sink);
        video(5, 0);
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

    [[nodiscard]] std::vector<protocol::UhidInput> reports() const { return wire.sent<protocol::UhidInput>(); }
};

}

TEST_CASE("the UHID touch screen descriptor declares the 16-finger report the sink writes (HID-002)")
{
    const std::span<const std::uint8_t> descriptor = uhidTouchDescriptor();
    CHECK(descriptor.size() <= 4096);
    const HidLayout layout = parseHid(descriptor);
    CHECK(layout.complete);
    CHECK(layout.numbered);
    CHECK(layout.fingers == kUhidMaxContacts);
    CHECK(layout.bytes(HidKind::Input, 1) == kUhidTouchReportSize);
    const HidField* count = layout.field(HidKind::Input, 1, kHidContactCount);
    REQUIRE(count != nullptr);
    CHECK(count->bit == 0);
    CHECK(count->size == 8);
    CHECK(count->logicalMaximum == kUhidMaxContacts);
    for (int finger = 0; finger < kUhidMaxContacts; ++finger) {
        INFO("finger ", finger);
        const HidField* tip = layout.field(HidKind::Input, 1, kHidTipSwitch, finger);
        const HidField* id = layout.field(HidKind::Input, 1, kHidContactIdentifier, finger);
        const HidField* x = layout.field(HidKind::Input, 1, kHidX, finger);
        const HidField* y = layout.field(HidKind::Input, 1, kHidY, finger);
        REQUIRE(tip != nullptr);
        REQUIRE(id != nullptr);
        REQUIRE(x != nullptr);
        REQUIRE(y != nullptr);
        CHECK(tip->bit == static_cast<std::size_t>(8 + 48 * finger));
        CHECK(tip->size == 1);
        CHECK(id->bit == tip->bit + 8);
        CHECK(id->size == 8);
        CHECK(x->bit == tip->bit + 16);
        CHECK(x->size == 16);
        CHECK(y->bit == tip->bit + 32);
        CHECK(y->size == 16);
        CHECK(x->logicalMaximum == kUhidLogicalMaximum);
        CHECK(y->logicalMaximum == kUhidLogicalMaximum);
    }
    const HidField* maximum = layout.field(HidKind::Feature, 2, kHidContactCountMaximum);
    REQUIRE(maximum != nullptr);
    CHECK(maximum->logicalMaximum == kUhidMaxContacts);
    CHECK(layout.bytes(HidKind::Feature, 2) == 2);

    Rig rig;
    REQUIRE(rig.frame({pressAt(0, 0.5, 0.5)}));
    const std::vector<protocol::UhidInput> reports = rig.reports();
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].id == kUhidTouchId);
    CHECK(reports[0].report.size() == layout.bytes(HidKind::Input, 1));
    CHECK(reports[0].report[0] == 1);
}

TEST_CASE("picture positions turn into touch screen positions in the display's natural orientation in one function "
          "(ADR-0009, HID-002)")
{
    CHECK(naturalPosition(0, 0, 0) == UhidPosition{.x = 0, .y = 0});
    CHECK(naturalPosition(65535, 0, 0) == UhidPosition{.x = 32767, .y = 0});
    CHECK(naturalPosition(65535, 65535, 0) == UhidPosition{.x = 32767, .y = 32767});

    CHECK(naturalPosition(0, 0, 1) == UhidPosition{.x = 32767, .y = 0});
    CHECK(naturalPosition(65535, 0, 1) == UhidPosition{.x = 32767, .y = 32767});
    CHECK(naturalPosition(0, 65535, 1) == UhidPosition{.x = 0, .y = 0});

    CHECK(naturalPosition(0, 0, 2) == UhidPosition{.x = 32767, .y = 32767});
    CHECK(naturalPosition(65535, 0, 2) == UhidPosition{.x = 0, .y = 32767});
    CHECK(naturalPosition(0, 65535, 2) == UhidPosition{.x = 32767, .y = 0});

    CHECK(naturalPosition(0, 0, 3) == UhidPosition{.x = 0, .y = 32767});
    CHECK(naturalPosition(65535, 0, 3) == UhidPosition{.x = 0, .y = 0});
    CHECK(naturalPosition(0, 65535, 3) == UhidPosition{.x = 32767, .y = 32767});

    for (int rotation = 0; rotation < 4; ++rotation) {
        INFO("rotation ", rotation);
        CHECK(naturalPosition(1000, 50000, rotation + 4) == naturalPosition(1000, 50000, rotation));
        const UhidPosition middle = naturalPosition(32768, 32768, rotation);
        CHECK(std::abs(middle.x - 16384) <= 1);
        CHECK(std::abs(middle.y - 16384) <= 1);
    }
}

TEST_CASE("a phone without UHID keeps every touch on injection (HID-002)")
{
    Rig rig(false);
    CHECK_FALSE(rig.sink.usesUhid());
    CHECK(rig.wire.messages.empty());
    CHECK(rig.channel.capabilities() == SinkCapabilities{});
    REQUIRE(rig.frame({pressAt(0, 0.5, 0.5)}));
    REQUIRE(rig.frame({liftAt(0, 0.5, 0.5)}));
    CHECK(rig.injection.frames.size() == 2);
    CHECK(rig.wire.messages.empty());
}

TEST_CASE("the UHID touch screen is made once the session streams; keys, scrolling and text stay on injection "
          "(HID-002)")
{
    Rig rig(true, false);
    CHECK(rig.wire.messages.empty());
    CHECK_FALSE(rig.sink.usesUhid());
    rig.sink.phaseChanged(session::SessionPhase::Streaming);
    const std::vector<protocol::UhidCreate> made = rig.wire.sent<protocol::UhidCreate>();
    REQUIRE(made.size() == 1);
    CHECK(made[0].id == kUhidTouchId);
    CHECK(made[0].vendorId == kUhidTouchVendor);
    CHECK(made[0].productId == kUhidTouchProduct);
    CHECK(made[0].name == "GameScrcpy Touch");
    const std::span<const std::uint8_t> descriptor = uhidTouchDescriptor();
    CHECK(std::ranges::equal(made[0].reportDescriptor, descriptor));
    CHECK(rig.sink.usesUhid());
    CHECK(rig.channel.capabilities() ==
          SinkCapabilities{.maxContacts = kUhidMaxContacts, .keys = true, .cancel = false});

    protocol::Key key;
    key.keycode = 4;
    CHECK(rig.channel.send(key));
    CHECK(rig.channel.scroll({.x = 0.5, .y = 0.5}, 0, 256));
    CHECK(rig.channel.type("hello"));
    CHECK(rig.injection.keys.size() == 1);
    CHECK(rig.injection.scrolls.size() == 1);
    CHECK(rig.injection.texts.size() == 1);

    REQUIRE(rig.frame({pressAt(0, 0.5, 0.5)}));
    CHECK(rig.injection.frames.empty());
    CHECK(rig.reports().size() == 1);
}

TEST_CASE("the UHID touch screen is made again after a reconnection and destroyed when the session stops (HID-002)")
{
    Rig rig;
    REQUIRE(rig.wire.sent<protocol::UhidCreate>().size() == 1);
    REQUIRE(rig.frame({pressAt(0, 0.4, 0.4)}));
    rig.sink.phaseChanged(session::SessionPhase::Reconnecting);
    CHECK_FALSE(rig.sink.usesUhid());
    CHECK(rig.wire.sent<protocol::UhidDestroy>().empty());

    rig.sink.welcomed(protocol::Welcome::kUhid);
    rig.sink.phaseChanged(session::SessionPhase::Streaming);
    CHECK(rig.wire.sent<protocol::UhidCreate>().size() == 2);
    CHECK(rig.sink.usesUhid());
    REQUIRE(rig.frame({moveTo(0, 0.5, 0.5)}));
    CHECK(rig.reports().size() == 1);
    REQUIRE(rig.frame({pressAt(1, 0.6, 0.6)}));
    const std::vector<protocol::UhidInput> reports = rig.reports();
    REQUIRE(reports.size() == 2);
    CHECK(contactsOf(reports[1]).size() == 1);

    rig.sink.phaseChanged(session::SessionPhase::Stopping);
    const std::vector<protocol::UhidDestroy> destroyed = rig.wire.sent<protocol::UhidDestroy>();
    REQUIRE(destroyed.size() == 1);
    CHECK(destroyed[0].id == kUhidTouchId);
    CHECK_FALSE(rig.sink.usesUhid());
    rig.channel.detach();
    CHECK(rig.wire.sent<protocol::UhidDestroy>().size() == 1);
    CHECK(rig.injection.detaches == 1);
}

TEST_CASE("detaching destroys the UHID touch screen, and a later stream does not make it again (HID-002)")
{
    Rig rig;
    REQUIRE(rig.frame({pressAt(0, 0.4, 0.4)}));
    rig.channel.detach();
    CHECK(rig.wire.sent<protocol::UhidDestroy>().size() == 1);
    CHECK(rig.injection.detaches == 1);
    rig.sink.phaseChanged(session::SessionPhase::Streaming);
    CHECK(rig.wire.sent<protocol::UhidCreate>().size() == 1);
}

TEST_CASE("a seventeenth finger is ignored and counted, and its later moves and lift too (HID-002)")
{
    Rig rig;
    for (int finger = 0; finger <= kUhidMaxContacts; ++finger) {
        REQUIRE(rig.frame({pressAt(static_cast<std::uint8_t>(finger), spread(finger), 0.5)}));
    }
    std::vector<protocol::UhidInput> reports = rig.reports();
    REQUIRE(reports.size() == static_cast<std::size_t>(kUhidMaxContacts));
    CHECK(contactsOf(reports.back()).size() == static_cast<std::size_t>(kUhidMaxContacts));
    CHECK(rig.sink.ignoredContacts() == 1);

    REQUIRE(rig.frame({moveTo(16, 0.9, 0.9)}));
    REQUIRE(rig.frame({liftAt(16, 0.9, 0.9)}));
    CHECK(rig.reports().size() == static_cast<std::size_t>(kUhidMaxContacts));

    REQUIRE(rig.frame({liftAt(0, spread(0), 0.5)}));
    reports = rig.reports();
    REQUIRE(reports.size() == static_cast<std::size_t>(kUhidMaxContacts) + 1);
    const std::vector<Contact> last = contactsOf(reports.back());
    CHECK(last.size() == static_cast<std::size_t>(kUhidMaxContacts));
    CHECK(tipsOf(last) == static_cast<std::size_t>(kUhidMaxContacts) - 1);
    CHECK(rig.sink.ignoredContacts() == 1);
}

TEST_CASE("a finger lifted somewhere else first moves there, then lifts (HID-002)")
{
    Rig rig;
    REQUIRE(rig.frame({pressAt(0, 0.2, 0.2)}));
    REQUIRE(rig.frame({liftAt(0, 0.4, 0.6)}));
    std::vector<protocol::UhidInput> reports = rig.reports();
    REQUIRE(reports.size() == 3);
    const std::vector<Contact> moved = contactsOf(reports[1]);
    REQUIRE(moved.size() == 1);
    CHECK(moved[0].tip);
    const UhidPosition target = naturalPosition(input::fixedCoordinate(0.4), input::fixedCoordinate(0.6), 0);
    CHECK(moved[0].x == target.x);
    CHECK(moved[0].y == target.y);
    const std::vector<Contact> lifted = contactsOf(reports[2]);
    REQUIRE(lifted.size() == 1);
    CHECK_FALSE(lifted[0].tip);
    CHECK(lifted[0].id == moved[0].id);

    REQUIRE(rig.frame({pressAt(1, 0.5, 0.5)}));
    REQUIRE(rig.frame({liftAt(1, 0.5, 0.5)}));
    CHECK(rig.reports().size() == 5);
}

TEST_CASE("contact identifiers start at 1, so the server's zeroed release report touches no finger (HID-002)")
{
    Rig rig;
    REQUIRE(rig.frame({pressAt(0, 0.2, 0.2), pressAt(1, 0.8, 0.8)}));
    const std::vector<protocol::UhidInput> reports = rig.reports();
    REQUIRE(reports.size() == 1);
    const std::vector<Contact> contacts = contactsOf(reports[0]);
    REQUIRE(contacts.size() == 2);
    CHECK(contacts[0].id != 0);
    CHECK(contacts[1].id != 0);
    CHECK(contacts[0].id != contacts[1].id);
}

TEST_CASE("a finger pressed while another lifts shares one report, or follows it when every slot is taken (HID-002)")
{
    Rig rig;
    REQUIRE(rig.frame({pressAt(0, 0.2, 0.2), pressAt(1, 0.4, 0.4)}));
    REQUIRE(rig.frame({liftAt(0, 0.2, 0.2), pressAt(2, 0.6, 0.6)}));
    std::vector<protocol::UhidInput> reports = rig.reports();
    REQUIRE(reports.size() == 2);
    std::vector<Contact> contacts = contactsOf(reports[1]);
    CHECK(contacts.size() == 3);
    CHECK(tipsOf(contacts) == 2);
    REQUIRE(rig.frame({liftAt(1, 0.4, 0.4), liftAt(2, 0.6, 0.6)}));

    for (int finger = 0; finger < kUhidMaxContacts; ++finger) {
        REQUIRE(rig.frame({pressAt(static_cast<std::uint8_t>(finger), spread(finger), 0.3)}));
    }
    const std::size_t before = rig.reports().size();
    REQUIRE(rig.frame({liftAt(3, spread(3), 0.3), pressAt(20, 0.5, 0.9)}));
    reports = rig.reports();
    REQUIRE(reports.size() == before + 2);
    contacts = contactsOf(reports[before]);
    CHECK(contacts.size() == static_cast<std::size_t>(kUhidMaxContacts));
    CHECK(tipsOf(contacts) == static_cast<std::size_t>(kUhidMaxContacts) - 1);
    contacts = contactsOf(reports[before + 1]);
    CHECK(contacts.size() == static_cast<std::size_t>(kUhidMaxContacts));
    CHECK(tipsOf(contacts) == static_cast<std::size_t>(kUhidMaxContacts));
    CHECK(rig.sink.ignoredContacts() == 0);
}

TEST_CASE("a touch that began on injection before the touch screen existed ends on injection (HID-002)")
{
    Rig rig(true, false);
    REQUIRE(rig.frame({pressAt(0, 0.3, 0.3)}));
    CHECK(rig.injection.frames.size() == 1);
    rig.sink.phaseChanged(session::SessionPhase::Streaming);
    CHECK(rig.wire.sent<protocol::UhidCreate>().size() == 1);
    CHECK_FALSE(rig.sink.usesUhid());
    REQUIRE(rig.frame({moveTo(0, 0.4, 0.4)}));
    REQUIRE(rig.frame({liftAt(0, 0.4, 0.4)}));
    CHECK(rig.injection.frames.size() == 3);
    CHECK(rig.sink.usesUhid());
    REQUIRE(rig.frame({pressAt(1, 0.6, 0.6)}));
    CHECK(rig.injection.frames.size() == 3);
    CHECK(rig.reports().size() == 1);
}

TEST_CASE("a frame the session cannot take is refused whole and goes out when it is sent again (HID-002)")
{
    Rig rig;
    rig.wire.refusals = 1;
    CHECK_FALSE(rig.frame({pressAt(0, 0.3, 0.3)}));
    CHECK(rig.reports().empty());
    REQUIRE(rig.frame({pressAt(0, 0.3, 0.3)}));
    REQUIRE(rig.reports().size() == 1);
    CHECK(contactsOf(rig.reports()[0]).size() == 1);

    rig.wire.refusals = 1;
    CHECK_FALSE(rig.frame({liftAt(0, 0.3, 0.3)}));
    REQUIRE(rig.frame({liftAt(0, 0.3, 0.3)}));
    const std::vector<protocol::UhidInput> reports = rig.reports();
    REQUIRE(reports.size() == 2);
    const std::vector<Contact> contacts = contactsOf(reports[1]);
    REQUIRE(contacts.size() == 1);
    CHECK_FALSE(contacts[0].tip);
}

TEST_CASE("touches use the rotation of the video session they are aimed at (HID-002)")
{
    Rig rig;
    rig.video(6, 1);
    REQUIRE(rig.frame({pressAt(0, 0.25, 0.75)}));
    const std::vector<Contact> contacts = contactsOf(rig.reports().back());
    REQUIRE(contacts.size() == 1);
    const UhidPosition expected = naturalPosition(input::fixedCoordinate(0.25), input::fixedCoordinate(0.75), 1);
    CHECK(contacts[0].x == expected.x);
    CHECK(contacts[0].y == expected.y);
    rig.video(7, 3);
    REQUIRE(rig.frame({moveTo(0, 0.25, 0.75)}));
    const std::vector<Contact> turned = contactsOf(rig.reports().back());
    REQUIRE(turned.size() == 1);
    const UhidPosition again = naturalPosition(input::fixedCoordinate(0.25), input::fixedCoordinate(0.75), 3);
    CHECK(turned[0].x == again.x);
    CHECK(turned[0].y == again.y);
}
