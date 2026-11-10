#include <gs/esp32/Contacts.h>

#include <doctest/doctest.h>

#include <vector>

using gs::esp32::Contact;
using gs::esp32::ContactAction;
using gs::esp32::ContactSlots;
using gs::esp32::NaturalPoint;
using gs::esp32::naturalPoint;

namespace {

struct Recorder {
    std::vector<gs_esp32_touch> reports;

    [[nodiscard]] ContactSlots::Sink sink()
    {
        return [this](const gs_esp32_touch& touch) {
            reports.push_back(touch);
            return true;
        };
    }
};

Contact down(std::uint32_t pointer, std::uint16_t x, std::uint16_t y)
{
    return {.pointerId = pointer, .action = ContactAction::Down, .x = x, .y = y};
}

Contact move(std::uint32_t pointer, std::uint16_t x, std::uint16_t y)
{
    return {.pointerId = pointer, .action = ContactAction::Move, .x = x, .y = y};
}

Contact up(std::uint32_t pointer, std::uint16_t x, std::uint16_t y)
{
    return {.pointerId = pointer, .action = ContactAction::Up, .x = x, .y = y};
}

Contact cancel(std::uint32_t pointer, std::uint16_t x, std::uint16_t y)
{
    return {.pointerId = pointer, .action = ContactAction::Cancel, .x = x, .y = y};
}

const gs_esp32_contact* contactOf(const gs_esp32_touch& touch, std::uint8_t id)
{
    for (std::uint8_t i = 0; i < touch.count; ++i) {
        if (touch.contacts[i].id == id) {
            return &touch.contacts[i];
        }
    }
    return nullptr;
}

}

TEST_CASE("picture positions become 0 to 32767 in the phone's natural orientation for each rotation")
{
    CHECK(naturalPoint(0, 0, 0) == NaturalPoint{0, 0});
    CHECK(naturalPoint(0xFFFF, 0xFFFF, 0) == NaturalPoint{32767, 32767});
    CHECK(naturalPoint(0x8000, 0x8000, 0) == NaturalPoint{16384, 16384});
    CHECK(naturalPoint(0, 0, 1) == NaturalPoint{32767, 0});
    CHECK(naturalPoint(0, 0, 2) == NaturalPoint{32767, 32767});
    CHECK(naturalPoint(0, 0, 3) == NaturalPoint{0, 32767});
    CHECK(naturalPoint(0xFFFF, 0, 1) == NaturalPoint{32767, 32767});
    CHECK(naturalPoint(0xFFFF, 0, 3) == NaturalPoint{0, 0});
    CHECK(naturalPoint(100, 200, 4) == naturalPoint(100, 200, 0));
}

TEST_CASE("a tap is one report with the finger down and one with it lifted")
{
    ContactSlots tracker;
    Recorder recorder;
    const std::vector<Contact> press{down(7, 0x8000, 0x4000)};
    REQUIRE(tracker.frame(press, 0, recorder.sink()));
    const std::vector<Contact> lift{up(7, 0x8000, 0x4000)};
    REQUIRE(tracker.frame(lift, 0, recorder.sink()));
    REQUIRE(recorder.reports.size() == 2);
    CHECK(recorder.reports[0].count == 1);
    CHECK(recorder.reports[0].contacts[0].id == 0);
    CHECK(recorder.reports[0].contacts[0].tip == 1);
    CHECK(recorder.reports[0].contacts[0].x == 16384);
    CHECK(recorder.reports[0].contacts[0].y == 8192);
    CHECK(recorder.reports[1].count == 1);
    CHECK(recorder.reports[1].contacts[0].tip == 0);
    CHECK(recorder.reports[1].contacts[0].x == 16384);
    CHECK_FALSE(tracker.holding());
}

TEST_CASE("a finger lifted somewhere else reports that last position before it lifts")
{
    ContactSlots tracker;
    Recorder recorder;
    const std::vector<Contact> press{down(1, 0, 0)};
    REQUIRE(tracker.frame(press, 0, recorder.sink()));
    recorder.reports.clear();
    const std::vector<Contact> lift{up(1, 0xFFFF, 0xFFFF)};
    REQUIRE(tracker.frame(lift, 0, recorder.sink()));
    REQUIRE(recorder.reports.size() == 2);
    CHECK(recorder.reports[0].contacts[0].tip == 1);
    CHECK(recorder.reports[0].contacts[0].x == 32767);
    CHECK(recorder.reports[0].contacts[0].y == 32767);
    CHECK(recorder.reports[1].contacts[0].tip == 0);
    CHECK(recorder.reports[1].contacts[0].x == 32767);
    CHECK(recorder.reports[1].contacts[0].y == 32767);
}

TEST_CASE("a cancelled finger lifts where it was")
{
    ContactSlots tracker;
    Recorder recorder;
    const std::vector<Contact> press{down(1, 0x2000, 0x2000)};
    REQUIRE(tracker.frame(press, 0, recorder.sink()));
    recorder.reports.clear();
    const std::vector<Contact> lift{cancel(1, 0xF000, 0xF000)};
    REQUIRE(tracker.frame(lift, 0, recorder.sink()));
    REQUIRE(recorder.reports.size() == 1);
    CHECK(recorder.reports[0].contacts[0].tip == 0);
    CHECK(recorder.reports[0].contacts[0].x == naturalPoint(0x2000, 0x2000, 0).x);
}

TEST_CASE("a lift and a press in one frame take different slots while there is room")
{
    ContactSlots tracker;
    Recorder recorder;
    const std::vector<Contact> first{down(1, 0x1000, 0x1000)};
    REQUIRE(tracker.frame(first, 0, recorder.sink()));
    recorder.reports.clear();
    const std::vector<Contact> swap{up(1, 0x1000, 0x1000), down(2, 0x3000, 0x3000)};
    REQUIRE(tracker.frame(swap, 0, recorder.sink()));
    REQUIRE(recorder.reports.size() == 1);
    const gs_esp32_touch& report = recorder.reports[0];
    CHECK(report.count == 2);
    REQUIRE(contactOf(report, 0) != nullptr);
    CHECK(contactOf(report, 0)->tip == 0);
    REQUIRE(contactOf(report, 1) != nullptr);
    CHECK(contactOf(report, 1)->tip == 1);
}

TEST_CASE("with all ten slots held, a lift is reported before its slot is used again in the same frame")
{
    ContactSlots tracker;
    Recorder recorder;
    std::vector<Contact> ten;
    for (std::uint32_t pointer = 0; pointer < 10; ++pointer) {
        ten.push_back(down(pointer + 100, static_cast<std::uint16_t>(pointer * 0x1000), 0x1000));
    }
    REQUIRE(tracker.frame(ten, 0, recorder.sink()));
    REQUIRE(recorder.reports.size() == 1);
    CHECK(recorder.reports[0].count == 10);
    recorder.reports.clear();

    const std::vector<Contact> swap{up(103, 0x3000, 0x1000), down(42, 0xE000, 0xE000)};
    REQUIRE(tracker.frame(swap, 0, recorder.sink()));
    REQUIRE(recorder.reports.size() == 2);
    const gs_esp32_touch& lifted = recorder.reports[0];
    CHECK(lifted.count == 10);
    REQUIRE(contactOf(lifted, 3) != nullptr);
    CHECK(contactOf(lifted, 3)->tip == 0);
    CHECK(contactOf(lifted, 3)->x == naturalPoint(0x3000, 0x1000, 0).x);
    const gs_esp32_touch& pressed = recorder.reports[1];
    CHECK(pressed.count == 10);
    REQUIRE(contactOf(pressed, 3) != nullptr);
    CHECK(contactOf(pressed, 3)->tip == 1);
    CHECK(contactOf(pressed, 3)->x == naturalPoint(0xE000, 0xE000, 0).x);
    for (std::uint8_t id = 0; id < 10; ++id) {
        const gs_esp32_contact* contact = contactOf(pressed, id);
        REQUIRE(contact != nullptr);
        CHECK(contact->tip == 1);
    }
}

TEST_CASE("an eleventh finger is dropped and counted; its lift does nothing")
{
    ContactSlots tracker;
    Recorder recorder;
    std::vector<Contact> eleven;
    for (std::uint32_t pointer = 0; pointer < 11; ++pointer) {
        eleven.push_back(down(pointer, 0x1000, static_cast<std::uint16_t>(pointer * 0x1000)));
    }
    REQUIRE(tracker.frame(eleven, 0, recorder.sink()));
    REQUIRE(recorder.reports.size() == 1);
    CHECK(recorder.reports[0].count == 10);
    CHECK(tracker.ignored() == 1);
    recorder.reports.clear();
    const std::vector<Contact> lift{up(10, 0x1000, 0xA000)};
    REQUIRE(tracker.frame(lift, 0, recorder.sink()));
    CHECK(recorder.reports.empty());
}

TEST_CASE("moves only report when a finger actually moved")
{
    ContactSlots tracker;
    Recorder recorder;
    const std::vector<Contact> press{down(1, 0x4000, 0x4000)};
    REQUIRE(tracker.frame(press, 0, recorder.sink()));
    recorder.reports.clear();
    const std::vector<Contact> still{move(1, 0x4000, 0x4000)};
    REQUIRE(tracker.frame(still, 0, recorder.sink()));
    CHECK(recorder.reports.empty());
    const std::vector<Contact> moved{move(1, 0x5000, 0x4000)};
    REQUIRE(tracker.frame(moved, 0, recorder.sink()));
    REQUIRE(recorder.reports.size() == 1);
    CHECK(recorder.reports[0].contacts[0].tip == 1);
    CHECK(recorder.reports[0].contacts[0].x == naturalPoint(0x5000, 0x4000, 0).x);
}

TEST_CASE("releasing lifts every held finger in one report")
{
    ContactSlots tracker;
    Recorder recorder;
    const std::vector<Contact> press{down(1, 0x1000, 0x1000), down(2, 0x2000, 0x2000)};
    REQUIRE(tracker.frame(press, 1, recorder.sink()));
    recorder.reports.clear();
    REQUIRE(tracker.release(recorder.sink()));
    REQUIRE(recorder.reports.size() == 1);
    CHECK(recorder.reports[0].count == 2);
    CHECK(recorder.reports[0].contacts[0].tip == 0);
    CHECK(recorder.reports[0].contacts[1].tip == 0);
    CHECK_FALSE(tracker.holding());
    recorder.reports.clear();
    REQUIRE(tracker.release(recorder.sink()));
    CHECK(recorder.reports.empty());
}
