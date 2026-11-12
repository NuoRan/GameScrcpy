#include "TouchBoxSession.h"

#include "ExtensionTouchSink.h"

#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QObject>
#include <QSize>
#include <QString>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace exthost = gs::exthost;
namespace protocol = gs::protocol;
namespace session = gs::session;
using gs::app::ControlSink;
using gs::app::ExtensionTouch;
using gs::app::ExtensionTouchSink;
using gs::app::PadRoute;
using gs::app::SinkCapabilities;
using gs::app::TouchBoxSession;
using gs::app::TouchPort;
using namespace std::chrono_literals;

namespace {

constexpr std::string_view kBoxMethod = "example.box/box";

std::unique_ptr<QCoreApplication> ensureApp()
{
    static int argc = 1;
    static char name[] = "test";
    static char* argv[] = {name, nullptr};
    if (QCoreApplication::instance() != nullptr) {
        return nullptr;
    }
    return std::make_unique<QCoreApplication>(argc, argv);
}

bool settle(const std::function<bool()>& done, std::chrono::milliseconds timeout = 3000ms)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done() && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    QCoreApplication::processEvents();
    return done();
}

class BoxPort final : public TouchPort
{
public:
    [[nodiscard]] SinkCapabilities capabilities() const override
    {
        return SinkCapabilities{.maxContacts = 10, .keys = true, .cancel = false, .gamepad = true};
    }

    [[nodiscard]] bool frame(const exthost::TouchFrame& frame) override
    {
        frames.push_back(frame);
        return !broken;
    }

    [[nodiscard]] bool key(const protocol::Key& key) override
    {
        keys.push_back(key);
        return true;
    }

    [[nodiscard]] bool text(std::string_view utf8) override
    {
        texts.emplace_back(utf8);
        return true;
    }

    [[nodiscard]] bool mouse(const exthost::MouseEvent& mouse) override
    {
        mice.push_back(mouse);
        return true;
    }

    [[nodiscard]] bool gamepad(const exthost::PadEvent&) override { return true; }
    [[nodiscard]] bool working() const override { return !broken && !closed; }
    void close() override { closed = true; }

    std::vector<exthost::TouchFrame> frames;
    std::vector<protocol::Key> keys;
    std::vector<std::string> texts;
    std::vector<exthost::MouseEvent> mice;
    bool broken = false;
    bool closed = false;
};

class BoxSource final : public gs::app::TouchMethodSource
{
public:
    [[nodiscard]] std::unique_ptr<ExtensionTouch> openTouch(std::string_view key, std::string_view device,
                                                            ControlSink& injection,
                                                            std::function<void()> fellBack) override
    {
        keys.emplace_back(key);
        devices.emplace_back(device);
        if (refuse) {
            return nullptr;
        }
        return std::make_unique<ExtensionTouchSink>(injection, port, std::move(fellBack));
    }

    [[nodiscard]] QString touchTitle(std::string_view) const override { return QStringLiteral("Touch box"); }

    std::shared_ptr<BoxPort> port = std::make_shared<BoxPort>();
    std::vector<std::string> keys;
    std::vector<std::string> devices;
    bool refuse = false;
};

protocol::TouchFrame touchAt(std::uint16_t videoSession, protocol::TouchAction action, double x, double y)
{
    protocol::TouchFrame frame;
    frame.videoSession = videoSession;
    protocol::TouchPoint point;
    point.action = action;
    point.pointerId = 0;
    point.x = static_cast<std::uint16_t>(x * 65535.0);
    point.y = static_cast<std::uint16_t>(y * 65535.0);
    point.pressure = 65535;
    frame.points.push_back(point);
    return frame;
}

}

TEST_CASE("a touch box session opens the box through its extension's touch method, without a picture (HID-008)")
{
    const auto app = ensureApp();
    BoxSource source;
    TouchBoxSession control(QStringLiteral("Touch box"), std::string(kBoxMethod));
    std::vector<session::SessionPhase> phases;
    QObject::connect(&control, &TouchBoxSession::stateChanged, &control,
                     [&phases](const session::SessionState& state) { phases.push_back(state.phase); });

    control.start(&source, "box-1", 1, QSize(2400, 1080));
    CHECK(source.keys == std::vector<std::string>{std::string(kBoxMethod)});
    CHECK(source.devices == std::vector<std::string>{"box-1"});
    CHECK(control.phase() == session::SessionPhase::Streaming);
    CHECK(control.active());
    const std::vector<session::SessionPhase> started{session::SessionPhase::Connecting,
                                                     session::SessionPhase::Streaming};
    CHECK(phases == started);
    CHECK(control.canvas() == QSize(2400, 1080));
    CHECK(control.rotation() == 1);
    CHECK(control.frames() != nullptr);

    const std::optional<gs::app::InputScreen> screen = control.channel()->screen();
    REQUIRE(screen.has_value());
    CHECK(screen->geometry.width == 2400);
    CHECK(screen->geometry.height == 1080);
    CHECK(control.channel()->padLink().route == PadRoute::External);

    CHECK(control.channel()->send(touchAt(screen->videoSession, protocol::TouchAction::Down, 0.25, 0.5)));
    REQUIRE(source.port->frames.size() == 1);
    const exthost::TouchFrame& frame = source.port->frames.front();
    CHECK(frame.rotation == 1);
    REQUIRE(frame.contacts.size() == 1);
    CHECK(frame.contacts[0].action == exthost::TouchAction::Down);
    CHECK(frame.contacts[0].naturalX == doctest::Approx(0.5).epsilon(0.001));
    CHECK(frame.contacts[0].naturalY == doctest::Approx(0.25).epsilon(0.001));

    control.stop();
    CHECK(control.phase() == session::SessionPhase::Stopped);
    CHECK(source.port->closed);
    CHECK(control.channel()->padLink().route == PadRoute::None);
}

TEST_CASE("without a picture the mouse, its buttons and the wheel go to the box's mouse; keys and text to its keyboard "
          "(HID-008)")
{
    const auto app = ensureApp();
    BoxSource source;
    TouchBoxSession control(QStringLiteral("Touch box"), std::string(kBoxMethod));
    control.start(&source, "box-1", 0, QSize(1080, 2400));
    REQUIRE(control.phase() == session::SessionPhase::Streaming);
    const std::vector<exthost::MouseEvent>& mice = source.port->mice;

    control.moveMouse(5, -3);
    REQUIRE(mice.size() == 1);
    CHECK(mice.back() == exthost::MouseEvent{.buttons = 0, .dx = 5, .dy = -3, .wheel = 0});

    control.setMouseButtons(1);
    REQUIRE(mice.size() == 2);
    CHECK(mice.back() == exthost::MouseEvent{.buttons = 1, .dx = 0, .dy = 0, .wheel = 0});

    CHECK(control.channel()->scroll({0.5, 0.5}, 0, 256));
    REQUIRE(mice.size() == 3);
    CHECK(mice.back() == exthost::MouseEvent{.buttons = 1, .dx = 0, .dy = 0, .wheel = 1});
    CHECK(control.channel()->scroll({0.5, 0.5}, 0, -40));
    REQUIRE(mice.size() == 4);
    CHECK(mice.back().wheel == -1);

    control.setMouseButtons(0x18 | 0x02);
    REQUIRE(mice.size() == 5);
    CHECK(mice.back().buttons == 0x02U);

    control.pressKey(4);
    REQUIRE(source.port->keys.size() == 2);
    CHECK(source.port->keys[0].keycode == 4U);
    CHECK(source.port->keys[0].action == protocol::KeyAction::Down);
    CHECK(source.port->keys[1].action == protocol::KeyAction::Up);

    CHECK(control.channel()->type("ok"));
    CHECK(source.port->texts == std::vector<std::string>{"ok"});
}

TEST_CASE("how the phone is held decides the rotation the box's touches use (HID-008)")
{
    const auto app = ensureApp();
    BoxSource source;
    TouchBoxSession control(QStringLiteral("Touch box"), std::string(kBoxMethod));
    control.start(&source, "box-1", 1, QSize(2400, 1080));
    REQUIRE(control.phase() == session::SessionPhase::Streaming);
    const std::uint16_t before = control.channel()->screen()->videoSession;

    control.setOrientation(0, QSize(1080, 2400));
    const std::optional<gs::app::InputScreen> screen = control.channel()->screen();
    REQUIRE(screen.has_value());
    CHECK(screen->videoSession != before);
    CHECK(screen->geometry.width == 1080);
    CHECK(screen->geometry.height == 2400);
    CHECK(control.rotation() == 0);

    CHECK(control.channel()->send(touchAt(screen->videoSession, protocol::TouchAction::Down, 0.25, 0.5)));
    REQUIRE_FALSE(source.port->frames.empty());
    CHECK(source.port->frames.back().rotation == 0);
    CHECK(source.port->frames.back().contacts[0].naturalX == doctest::Approx(0.25).epsilon(0.001));
}

TEST_CASE("a touch method that does not open fails the session, and a box that stops answering fails it later "
          "(HID-008)")
{
    const auto app = ensureApp();
    BoxSource refusing;
    refusing.refuse = true;
    TouchBoxSession refused(QStringLiteral("Touch box"), std::string(kBoxMethod));
    std::optional<session::SessionState> last;
    QObject::connect(&refused, &TouchBoxSession::stateChanged, &refused,
                     [&last](const session::SessionState& state) { last = state; });
    refused.start(&refusing, "box-1", 0, QSize(1080, 2400));
    CHECK(refused.phase() == session::SessionPhase::Failed);
    REQUIRE(last.has_value());
    REQUIRE(last->failure.has_value());
    CHECK(last->failure->kind == session::FailureKind::ConnectFailed);
    TouchBoxSession nothing(QStringLiteral("Touch box"), std::string(kBoxMethod));
    nothing.start(nullptr, "box-1", 0, QSize(1080, 2400));
    CHECK(nothing.phase() == session::SessionPhase::Failed);

    BoxSource source;
    TouchBoxSession control(QStringLiteral("Touch box"), std::string(kBoxMethod));
    std::optional<session::SessionState> ended;
    QObject::connect(&control, &TouchBoxSession::stateChanged, &control,
                     [&ended](const session::SessionState& state) { ended = state; });
    control.start(&source, "box-1", 0, QSize(1080, 2400));
    REQUIRE(control.phase() == session::SessionPhase::Streaming);
    source.port->broken = true;
    static_cast<void>(control.channel()->send(
        touchAt(control.channel()->screen()->videoSession, protocol::TouchAction::Down, 0.5, 0.5)));
    REQUIRE(settle([&control] { return control.phase() == session::SessionPhase::Failed; }));
    REQUIRE(ended.has_value());
    REQUIRE(ended->failure.has_value());
    CHECK(ended->failure->kind == session::FailureKind::ConnectionLost);
    CHECK(source.port->closed);
    CHECK(control.channel()->padLink().route == PadRoute::None);
}
