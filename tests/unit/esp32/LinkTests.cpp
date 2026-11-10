#include "FakeBoard.h"
#include "TestSupport.h"

#include <gs/esp32/Link.h>
#include <gs/esp32/TouchMethod.h>

#include <doctest/doctest.h>

#include <atomic>
#include <memory>
#include <vector>

using esp32test::Bytes;
using esp32test::eventually;
using esp32test::FakeBoard;
using esp32test::fromHex;
using gs::esp32::BoardInfo;
using gs::esp32::BoardStatus;
using gs::esp32::Contact;
using gs::esp32::ContactAction;
using gs::esp32::Esp32Capabilities;
using gs::esp32::Esp32Gamepad;
using gs::esp32::Esp32Link;
using gs::esp32::Esp32TouchMethod;
using gs::esp32::LinkEvents;
using gs::esp32::LinkOptions;
using gs::esp32::LinkState;

namespace {

using namespace std::chrono_literals;

struct Fixture {
    explicit Fixture(FakeBoard::Options board = {}, LinkOptions options = {}, LinkEvents events = {})
    {
        auto port = std::make_unique<FakeBoard>(board);
        fake = port.get();
        link = std::make_unique<Esp32Link>(std::move(port), options, std::move(events));
    }

    FakeBoard* fake = nullptr;
    std::unique_ptr<Esp32Link> link;
};

gs_esp32_touch decodeTouch(const esp32test::ReceivedFrame& frame)
{
    gs_esp32_touch touch{};
    REQUIRE(gs_esp32_decode_touch(frame.payload.data(), frame.payload.size(), &touch));
    return touch;
}

}

TEST_CASE("the link asks for HELLO and becomes ready with the board's firmware, chip and features (HID-004)")
{
    std::atomic<int> hellos{0};
    LinkEvents events;
    events.hello = [&hellos](const BoardInfo&, const BoardStatus&) { ++hellos; };
    Fixture fixture({}, {}, events);
    REQUIRE(fixture.link->waitSettled(2s) == LinkState::Ready);
    const std::optional<BoardInfo> board = fixture.link->board();
    REQUIRE(board.has_value());
    CHECK(board->protocol == 2);
    CHECK(board->firmware == "1.0.0");
    CHECK(board->chipId == GS_ESP32_CHIP_ESP32S3);
    CHECK(gs::esp32::chipName(board->chipId) == "ESP32-S3");
    CHECK(gs::esp32::chipTarget(board->chipId) == "esp32s3");
    CHECK(board->name == "Fake S3");
    CHECK(board->mac == std::array<std::uint8_t, 6>{0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6});
    CHECK((board->features & GS_ESP32_FEATURE_USB_HID) != 0);
    CHECK(fixture.link->status().wifi == GS_ESP32_WIFI_OFF);
    CHECK(hellos.load() == 1);
    CHECK(fixture.fake->count(GS_ESP32_MSG_HELLO_REQ) == 1);
    CHECK(fixture.link->baud() == GS_ESP32_HANDSHAKE_BAUD);
}

TEST_CASE("the link pings while idle so the board's two-second safety never fires, and measures the round trip")
{
    LinkOptions options;
    options.keepAlive = 50ms;
    std::atomic<int> trips{0};
    LinkEvents events;
    events.roundTrip = [&trips](std::chrono::microseconds) { ++trips; };
    Fixture fixture({}, options, events);
    REQUIRE(fixture.link->waitSettled(2s) == LinkState::Ready);
    CHECK(eventually([&fixture] { return fixture.fake->count(GS_ESP32_MSG_PING) >= 4; }));
    CHECK(eventually([&trips] { return trips.load() >= 2; }));
    CHECK(fixture.link->roundTrip().has_value());
    CHECK(fixture.link->state() == LinkState::Ready);
}

TEST_CASE("the default keep-alive is well inside the board's two seconds")
{
    const LinkOptions options;
    CHECK(options.keepAlive.count() * 3 < GS_ESP32_SAFETY_MS);
    CHECK(options.helloWait.count() == GS_ESP32_HELLO_WAIT_MS);
}

TEST_CASE("a STATUS pushed by the board reaches the listener")
{
    std::atomic<int> ready{0};
    LinkEvents events;
    events.status = [&ready](const BoardStatus& status) {
        if (status.usb == GS_ESP32_USB_READY && status.peer == "Pixel 8") {
            ++ready;
        }
    };
    Fixture fixture({}, {}, events);
    REQUIRE(fixture.link->waitSettled(2s) == LinkState::Ready);
    fixture.fake->pushStatus(GS_ESP32_USB_READY, GS_ESP32_BLE_NONE, "Pixel 8");
    CHECK(eventually([&ready] { return ready.load() == 1; }));
    CHECK(fixture.link->status().usb == GS_ESP32_USB_READY);
    CHECK(fixture.link->status().peer == "Pixel 8");
}

TEST_CASE("through a USB-to-serial chip the link moves to 2 000 000 baud when both sides can")
{
    LinkOptions options;
    options.fastBaud = true;
    Fixture fixture({}, options);
    REQUIRE(fixture.link->waitSettled(3s) == LinkState::Ready);
    CHECK(fixture.link->baud() == GS_ESP32_FAST_BAUD);
    CHECK(fixture.fake->hostBaud() == GS_ESP32_FAST_BAUD);
    CHECK(fixture.fake->boardBaud() == GS_ESP32_FAST_BAUD);
    CHECK(fixture.fake->count(GS_ESP32_MSG_SET_BAUD) == 1);
}

TEST_CASE("when the computer's serial chip cannot do 2 000 000 baud both sides fall back to 921 600")
{
    FakeBoard::Options board;
    board.hostSwitches = false;
    LinkOptions options;
    options.fastBaud = true;
    Fixture fixture(board, options);
    REQUIRE(fixture.link->waitSettled(5s) == LinkState::Ready);
    CHECK(fixture.link->baud() == GS_ESP32_HANDSHAKE_BAUD);
    CHECK(fixture.fake->boardBaud() == GS_ESP32_HANDSHAKE_BAUD);
}

TEST_CASE("a board that refuses the faster baud stays at 921 600")
{
    FakeBoard::Options board;
    board.acceptFastBaud = false;
    LinkOptions options;
    options.fastBaud = true;
    Fixture fixture(board, options);
    REQUIRE(fixture.link->waitSettled(3s) == LinkState::Ready);
    CHECK(fixture.link->baud() == GS_ESP32_HANDSHAKE_BAUD);
}

TEST_CASE("a board still at 2 000 000 baud from an earlier session answers when asked again at that speed")
{
    FakeBoard::Options board;
    board.startBaud = GS_ESP32_FAST_BAUD;
    board.revertsOnGarbage = false;
    LinkOptions options;
    options.fastBaud = true;
    Fixture fixture(board, options);
    REQUIRE(fixture.link->waitSettled(5s) == LinkState::Ready);
    CHECK(fixture.link->baud() == GS_ESP32_FAST_BAUD);
    CHECK(fixture.fake->hostBaud() == GS_ESP32_FAST_BAUD);
    CHECK(fixture.fake->count(GS_ESP32_MSG_HELLO_REQ) == 1);
    CHECK(fixture.fake->count(GS_ESP32_MSG_SET_BAUD) == 0);
}

TEST_CASE("over the chip's own USB serial port the baud is never changed")
{
    FakeBoard::Options board;
    board.features |= GS_ESP32_FEATURE_USB_SERIAL_LINK;
    LinkOptions options;
    options.fastBaud = true;
    Fixture fixture(board, options);
    REQUIRE(fixture.link->waitSettled(2s) == LinkState::Ready);
    CHECK(fixture.fake->count(GS_ESP32_MSG_SET_BAUD) == 0);
}

TEST_CASE("no HELLO but the old QineTouch greeting: touches go out as 12-byte old packets only (HID-004)")
{
    FakeBoard::Options board;
    board.answers = false;
    board.says = esp32test::textBytes("QineTouch v3.2 ready\r\n");
    LinkOptions options;
    options.legacyRetries = 0;
    const auto start = std::chrono::steady_clock::now();
    Fixture fixture(board, options);
    REQUIRE(fixture.link->waitSettled(2s) == LinkState::Legacy);
    CHECK(std::chrono::steady_clock::now() - start >= 290ms);

    const Bytes hello = fixture.fake->written();
    REQUIRE_FALSE(hello.empty());
    CHECK(std::ranges::find(hello, std::uint8_t{GS_ESP32_LEGACY_MAGIC}) == hello.end());

    gs_esp32_touch touch{};
    touch.count = 2;
    touch.contacts[0] = {.id = 0, .tip = 1, .x = 100, .y = 200};
    touch.contacts[1] = {.id = 1, .tip = 0, .x = 32767, .y = 0};
    REQUIRE(fixture.link->sendTouch(touch));
    const Bytes all = fixture.fake->written();
    const Bytes packets(all.begin() + static_cast<std::ptrdiff_t>(hello.size()), all.end());
    CHECK(packets == fromHex("f4010064000000c800000002"
                             "f40001ff7f00000000000002"));

    gs_esp32_keyboard keyboard{};
    CHECK_FALSE(fixture.link->sendKeyboard(keyboard));
    CHECK_FALSE(fixture.link->sendConsumer(0x0223));
    CHECK_FALSE(fixture.link->ping());
    CHECK_FALSE(fixture.link->releaseAll());
    std::this_thread::sleep_for(150ms);
    CHECK(fixture.fake->written().size() == all.size());
}

TEST_CASE("a board that boots late is found by the HELLO retries after the legacy decision")
{
    FakeBoard::Options board;
    board.answers = false;
    board.says = esp32test::textBytes("QineTouch\r\n");
    LinkOptions options;
    options.legacyRetry = 50ms;
    options.legacyRetries = 5;
    options.keepAlive = 10s;
    Fixture fixture(board, options);
    REQUIRE(fixture.link->waitSettled(2s) == LinkState::Legacy);
    fixture.fake->setAnswering(true);
    CHECK(eventually([&fixture] { return fixture.link->state() == LinkState::Ready; }));
    CHECK(fixture.link->board().has_value());
    const Bytes written = fixture.fake->written();
    CHECK(std::ranges::find(written, std::uint8_t{GS_ESP32_LEGACY_MAGIC}) == written.end());
}

TEST_CASE("a silent port still falls back to the old protocol, and the link remembers that nothing answered")
{
    FakeBoard::Options board;
    board.answers = false;
    LinkOptions options;
    options.legacyRetries = 0;
    Fixture fixture(board, options);
    REQUIRE(fixture.link->waitSettled(2s) == LinkState::Legacy);
    const gs::esp32::BoardTrouble trouble = fixture.link->trouble();
    CHECK(trouble.problem == gs::esp32::BoardProblem::Silent);
    CHECK(trouble.bytes == 0U);
    CHECK_FALSE(fixture.link->board().has_value());
}

TEST_CASE("boot ROM text without HELLO means the board keeps restarting, with its reset reason")
{
    FakeBoard::Options board;
    board.answers = false;
    board.says = esp32test::textBytes("ESP-ROM:esp32s3-20210327\r\n"
                                      "rst:0x3 (RTC_SW_SYS_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n");
    LinkOptions options;
    options.legacyRetries = 0;
    Fixture fixture(board, options);
    REQUIRE(fixture.link->waitSettled(2s) == LinkState::Lost);
    const gs::esp32::BoardTrouble trouble = fixture.link->trouble();
    CHECK(trouble.problem == gs::esp32::BoardProblem::Restarting);
    CHECK(trouble.resetReason == "0x3 (RTC_SW_SYS_RST)");
    CHECK(trouble.bytes >= 60U);
}

TEST_CASE("a board left in download mode is named as such instead of old firmware")
{
    FakeBoard::Options board;
    board.answers = false;
    board.says = esp32test::textBytes("ESP-ROM:esp32s3-20210327\r\n"
                                      "rst:0x1 (POWERON),boot:0x0 (DOWNLOAD(USB/UART0))\r\n"
                                      "waiting for download\r\n");
    LinkOptions options;
    options.legacyRetries = 0;
    Fixture fixture(board, options);
    REQUIRE(fixture.link->waitSettled(2s) == LinkState::Lost);
    const gs::esp32::BoardTrouble trouble = fixture.link->trouble();
    CHECK(trouble.problem == gs::esp32::BoardProblem::DownloadMode);
}

TEST_CASE("bytes that are not ESP32 talk are a wrong port or speed")
{
    FakeBoard::Options board;
    board.answers = false;
    board.says = fromHex("0080ff0013fe7f01a2c4");
    LinkOptions options;
    options.legacyRetries = 0;
    Fixture fixture(board, options);
    REQUIRE(fixture.link->waitSettled(2s) == LinkState::Lost);
    const gs::esp32::BoardTrouble trouble = fixture.link->trouble();
    CHECK(trouble.problem == gs::esp32::BoardProblem::WrongPort);
    CHECK(trouble.bytes == 10U);
}

TEST_CASE("the new HELLO reports each part's state and an old HELLO simply has none")
{
    FakeBoard::Options full;
    full.reportParts = true;
    full.parts = gs_esp32_parts{.usb = GS_ESP32_USB_READY,
                                .usb_problem = GS_ESP32_PART_OK,
                                .ble = GS_ESP32_BLE_NONE,
                                .ble_problem = GS_ESP32_PART_UNSUPPORTED,
                                .wifi_problem = GS_ESP32_PART_FAILED};
    {
        Fixture fixture(full);
        REQUIRE(fixture.link->waitSettled(2s) == LinkState::Ready);
        const std::optional<BoardInfo> board = fixture.link->board();
        REQUIRE(board.has_value());
        CHECK(board->parts.known);
        CHECK(board->parts.usb == GS_ESP32_USB_READY);
        CHECK(board->parts.usbProblem == GS_ESP32_PART_OK);
        CHECK(board->parts.ble == GS_ESP32_BLE_NONE);
        CHECK(board->parts.bleProblem == GS_ESP32_PART_UNSUPPORTED);
        CHECK(board->parts.wifiProblem == GS_ESP32_PART_FAILED);
        CHECK(board->wifi == GS_ESP32_WIFI_OFF);
        CHECK(fixture.link->trouble().problem == gs::esp32::BoardProblem::None);
    }
    {
        Fixture old({}, {});
        REQUIRE(old.link->waitSettled(2s) == LinkState::Ready);
        REQUIRE(old.link->board().has_value());
        CHECK_FALSE(old.link->board()->parts.known);
    }
}

TEST_CASE("the phone states in a HELLO become the live status, so the panel is right before the first STATUS")
{
    FakeBoard::Options board;
    board.reportParts = true;
    board.parts = gs_esp32_parts{.usb = GS_ESP32_USB_DETACHED,
                                 .usb_problem = GS_ESP32_PART_OK,
                                 .ble = GS_ESP32_BLE_NONE,
                                 .ble_problem = GS_ESP32_PART_UNSUPPORTED,
                                 .wifi_problem = GS_ESP32_PART_OK};
    Fixture fixture(board);
    REQUIRE(fixture.link->waitSettled(2s) == LinkState::Ready);
    CHECK(fixture.link->status().usb == GS_ESP32_USB_DETACHED);
    CHECK(fixture.link->status().ble == GS_ESP32_BLE_NONE);
    CHECK(fixture.link->status().wifi == GS_ESP32_WIFI_OFF);
}

TEST_CASE("with the old protocol not allowed the old greeting is still reported as old firmware")
{
    FakeBoard::Options board;
    board.answers = false;
    board.says = esp32test::textBytes("QineTouch\r\n");
    LinkOptions options;
    options.allowLegacy = false;
    Fixture fixture(board, options);
    REQUIRE(fixture.link->waitSettled(2s) == LinkState::Lost);
    CHECK(fixture.link->trouble().problem == gs::esp32::BoardProblem::OldFirmware);
}

TEST_CASE("over Wi-Fi every frame is one datagram; a silent address gives up instead of falling back")
{
    FakeBoard::Options board;
    board.datagrams = true;
    LinkOptions options;
    options.allowLegacy = false;
    {
        Fixture fixture(board, options);
        REQUIRE(fixture.link->waitSettled(2s) == LinkState::Ready);
        CHECK(fixture.link->datagrams());
    }
    board.answers = false;
    options.udpGiveUp = 300ms;
    options.udpRetry = 100ms;
    Fixture silent(board, options);
    CHECK(silent.link->waitSettled(2s) == LinkState::Lost);
    CHECK(silent.fake->count(GS_ESP32_MSG_HELLO_REQ) == 0);
    CHECK(silent.fake->written().size() >= 2 * (GS_ESP32_FRAME_OVERHEAD + 1));
}

TEST_CASE("a board that stops answering is reported lost")
{
    LinkOptions options;
    options.keepAlive = 50ms;
    options.silence = 300ms;
    Fixture fixture({}, options);
    REQUIRE(fixture.link->waitSettled(2s) == LinkState::Ready);
    fixture.fake->setAnswering(false);
    CHECK(eventually([&fixture] { return fixture.link->state() == LinkState::Lost; }));
}

TEST_CASE("a board that restarts mid-session is asked again at 921 600 and moved back to 2 000 000 baud")
{
    FakeBoard::Options board;
    board.revertsOnGarbage = false;
    LinkOptions options;
    options.fastBaud = true;
    options.keepAlive = 50ms;
    options.silence = 300ms;
    Fixture fixture(board, options);
    REQUIRE(fixture.link->waitSettled(3s) == LinkState::Ready);
    REQUIRE(fixture.link->baud() == GS_ESP32_FAST_BAUD);
    fixture.fake->restart();
    CHECK(eventually([&fixture] { return fixture.fake->count(GS_ESP32_MSG_SET_BAUD) == 2; }));
    CHECK(eventually([&fixture] {
        return fixture.fake->boardBaud() == GS_ESP32_FAST_BAUD && fixture.fake->hostBaud() == GS_ESP32_FAST_BAUD;
    }));
    CHECK(fixture.link->state() == LinkState::Ready);
    CHECK(fixture.link->baud() == GS_ESP32_FAST_BAUD);
    gs_esp32_touch touch{};
    touch.count = 1;
    touch.contacts[0].tip = 1;
    touch.contacts[0].x = 100;
    touch.contacts[0].y = 200;
    CHECK(fixture.link->sendTouch(touch));
    CHECK(eventually([&fixture] { return fixture.fake->count(GS_ESP32_MSG_TOUCH) == 1; }));
}

TEST_CASE("v2 messages reach the board with their fields")
{
    Fixture fixture;
    REQUIRE(fixture.link->waitSettled(2s) == LinkState::Ready);
    const gs_esp32_gamepad gamepad{.buttons = 0x81, .hat = 2, .lx = -100, .ly = 100, .rx = 7, .ry = -7, .lt = 9, .rt = 10};
    REQUIRE(fixture.link->sendGamepad(gamepad));
    REQUIRE(fixture.link->sendMouse({.buttons = 1, .dx = -3, .dy = 4, .wheel = -1}));
    REQUIRE(fixture.link->setName("Desk box"));
    const std::array<std::uint8_t, GS_ESP32_KEY_SIZE> key{1, 2, 3};
    REQUIRE(fixture.link->setWifi("HomeNet", "secret", key));
    CHECK_FALSE(fixture.link->setWifi("", "secret", key));
    CHECK_FALSE(fixture.link->setName(std::string(GS_ESP32_MAX_NAME + 1, 'x')));

    const auto pads = fixture.fake->framesOf(GS_ESP32_MSG_GAMEPAD);
    REQUIRE(pads.size() == 1);
    gs_esp32_gamepad decoded{};
    REQUIRE(gs_esp32_decode_gamepad(pads[0].payload.data(), pads[0].payload.size(), &decoded));
    CHECK(decoded.buttons == 0x81);
    CHECK(decoded.lx == -100);
    CHECK(decoded.rt == 10);
    const auto wifi = fixture.fake->framesOf(GS_ESP32_MSG_WIFI_SET);
    REQUIRE(wifi.size() == 1);
    gs_esp32_wifi_set settings{};
    REQUIRE(gs_esp32_decode_wifi_set(wifi[0].payload.data(), wifi[0].payload.size(), &settings));
    CHECK(std::string(settings.ssid) == "HomeNet");
    CHECK(settings.key[2] == 3);
    CHECK(fixture.fake->count(GS_ESP32_MSG_SET_NAME) == 1);
    CHECK(fixture.fake->count(GS_ESP32_MSG_MOUSE) == 1);
}

TEST_CASE("the ESP32 touch method turns contacts, keys, mouse and gamepad into board messages and releases on close")
{
    Fixture fixture;
    REQUIRE(fixture.link->waitSettled(2s) == LinkState::Ready);
    const auto journal = fixture.fake->journal();
    const esp32test::Journal& fake = *journal;
    Esp32TouchMethod method;
    method.attach(std::move(fixture.link));
    CHECK(method.opened());
    CHECK(method.capabilities() == Esp32Capabilities{});

    const std::vector<Contact> press{{.pointerId = 4, .action = ContactAction::Down, .x = 0x8000, .y = 0x8000}};
    REQUIRE(method.frame(press, 0));
    auto touches = fake.framesOf(GS_ESP32_MSG_TOUCH);
    REQUIRE(touches.size() == 1);
    const gs_esp32_touch pressed = decodeTouch(touches[0]);
    CHECK(pressed.count == 1);
    CHECK(pressed.contacts[0].tip == 1);
    CHECK(pressed.contacts[0].x == 16384);

    REQUIRE(method.key(3, true));
    REQUIRE(method.key(3, false));
    const auto consumers = fake.framesOf(GS_ESP32_MSG_CONSUMER);
    REQUIRE(consumers.size() == 2);
    CHECK(consumers[0].payload == Bytes{0x23, 0x02});
    CHECK(consumers[1].payload == Bytes{0x00, 0x00});

    REQUIRE(method.key(29, true));
    REQUIRE(method.key(59, true));
    const auto keyboards = fake.framesOf(GS_ESP32_MSG_KEYBOARD);
    REQUIRE(keyboards.size() == 2);
    CHECK(keyboards[1].payload == Bytes{0x02, 0x04, 0, 0, 0, 0, 0});
    CHECK_FALSE(method.key(63, true));

    REQUIRE(method.mouse(1, 40000, -5, 0));
    const auto mice = fake.framesOf(GS_ESP32_MSG_MOUSE);
    REQUIRE(mice.size() == 2);
    gs_esp32_mouse first{};
    gs_esp32_mouse second{};
    REQUIRE(gs_esp32_decode_mouse(mice[0].payload.data(), mice[0].payload.size(), &first));
    REQUIRE(gs_esp32_decode_mouse(mice[1].payload.data(), mice[1].payload.size(), &second));
    CHECK(first.dx == 32767);
    CHECK(second.dx == 40000 - 32767);
    CHECK(first.dy == -5);
    CHECK(second.dy == 0);

    REQUIRE(method.gamepad(Esp32Gamepad{.buttons = 3, .hat = 2, .lx = -32768, .ly = 5, .rx = 0, .ry = 0, .lt = 1, .rt = 2}));
    const auto pads = fake.framesOf(GS_ESP32_MSG_GAMEPAD);
    REQUIRE(pads.size() == 1);
    gs_esp32_gamepad pad{};
    REQUIRE(gs_esp32_decode_gamepad(pads[0].payload.data(), pads[0].payload.size(), &pad));
    CHECK(pad.lx == -32767);
    CHECK(pad.hat == 2);

    method.close();
    CHECK_FALSE(method.opened());
    touches = fake.framesOf(GS_ESP32_MSG_TOUCH);
    REQUIRE(touches.size() == 2);
    const gs_esp32_touch lifted = decodeTouch(touches[1]);
    CHECK(lifted.count == 1);
    CHECK(lifted.contacts[0].tip == 0);
    CHECK(fake.count(GS_ESP32_MSG_RELEASE_ALL) == 1);
}

TEST_CASE("with the old firmware the touch method only touches")
{
    FakeBoard::Options board;
    board.answers = false;
    board.says = esp32test::textBytes("QineTouch v3.2 ready\r\n");
    LinkOptions options;
    options.legacyRetries = 0;
    Fixture fixture(board, options);
    REQUIRE(fixture.link->waitSettled(2s) == LinkState::Legacy);
    const auto journal = fixture.fake->journal();
    const esp32test::Journal& fake = *journal;
    Esp32TouchMethod method;
    method.attach(std::move(fixture.link));
    const Esp32Capabilities capabilities = method.capabilities();
    CHECK(capabilities.maxContacts == 10);
    CHECK_FALSE(capabilities.keys);
    CHECK_FALSE(capabilities.mouse);
    CHECK_FALSE(capabilities.gamepad);
    CHECK_FALSE(capabilities.cancel);
    const std::size_t before = fake.written().size();
    const std::vector<Contact> press{{.pointerId = 1, .action = ContactAction::Down, .x = 0, .y = 0}};
    REQUIRE(method.frame(press, 0));
    CHECK(fake.written().size() == before + GS_ESP32_LEGACY_PACKET);
    CHECK_FALSE(method.key(3, true));
    CHECK_FALSE(method.mouse(0, 1, 1, 0));
    method.close();
    CHECK(fake.written().size() == before + 2 * GS_ESP32_LEGACY_PACKET);
}
