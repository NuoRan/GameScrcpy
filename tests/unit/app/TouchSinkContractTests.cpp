#include "AoaTouchSink.h"
#include "ControlChannel.h"
#include "ExtensionTouchSink.h"
#include "HidReports.h"
#include "TouchSinkFakes.h"
#include "UhidTouchSink.h"

#include <gs/exthost/Module.h>

#include <gs/input/Geometry.h>
#include <gs/protocol/Messages.h>
#include <gs/session/SessionState.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace gs;
using namespace gs::app;
using namespace gs::test;

namespace {

constexpr int kTolerance = 3;
constexpr std::uint16_t kWidth = 1080;
constexpr std::uint16_t kHeight = 2400;

struct Spot {
    std::uint16_t x = 0;
    std::uint16_t y = 0;

    friend auto operator<=>(const Spot&, const Spot&) = default;
};

struct Lift {
    Spot at;
    bool canceled = false;
};

struct Seen {
    std::vector<Spot> held;
    std::vector<Lift> lifted;
};

Spot at(double x, double y)
{
    return {.x = input::fixedCoordinate(x), .y = input::fixedCoordinate(y)};
}

bool matches(Spot a, Spot b)
{
    return std::abs(int{a.x} - int{b.x}) <= kTolerance && std::abs(int{a.y} - int{b.y}) <= kTolerance;
}

std::string describe(const Seen& seen)
{
    std::string text = "held:";
    for (const Spot& spot : seen.held) {
        text += " (" + std::to_string(spot.x) + ", " + std::to_string(spot.y) + ")";
    }
    text += "; lifted:";
    for (const Lift& lift : seen.lifted) {
        text += " (" + std::to_string(lift.at.x) + ", " + std::to_string(lift.at.y) +
                (lift.canceled ? ", canceled)" : ")");
    }
    return text;
}

bool sameSpots(std::vector<Spot> actual, std::vector<Spot> expected)
{
    if (actual.size() != expected.size()) {
        return false;
    }
    std::ranges::sort(actual);
    std::ranges::sort(expected);
    return std::ranges::equal(actual, expected, matches);
}

bool holds(const Seen& seen, std::vector<Spot> expected)
{
    return sameSpots(seen.held, std::move(expected));
}

bool lifts(const Seen& seen, std::vector<Spot> expected, std::optional<bool> canceled = std::nullopt)
{
    std::vector<Spot> lifted;
    for (const Lift& lift : seen.lifted) {
        if (canceled && lift.canceled != *canceled) {
            return false;
        }
        lifted.push_back(lift.at);
    }
    return sameSpots(std::move(lifted), std::move(expected));
}

class InjectionPhone
{
public:
    void video(std::uint16_t session) { m_session = session; }

    void apply(const protocol::TouchFrame& frame, Seen& seen)
    {
        const bool stale = frame.videoSession != m_session;
        std::vector<std::pair<std::uint8_t, bool>> lifting;
        std::vector<std::pair<std::uint8_t, Spot>> pressing;
        for (const protocol::TouchPoint& point : frame.points) {
            const auto held = m_held.find(point.pointerId);
            if (point.action == protocol::TouchAction::Up || point.action == protocol::TouchAction::Cancel) {
                if (held == m_held.end()) {
                    continue;
                }
                if (!stale && point.action == protocol::TouchAction::Up) {
                    held->second = {.x = point.x, .y = point.y};
                }
                lifting.emplace_back(point.pointerId, point.action == protocol::TouchAction::Cancel);
            } else if (!stale) {
                if (held != m_held.end()) {
                    held->second = {.x = point.x, .y = point.y};
                } else if (point.action == protocol::TouchAction::Down) {
                    pressing.emplace_back(point.pointerId, Spot{.x = point.x, .y = point.y});
                }
            }
        }
        for (const auto& lift : lifting) {
            seen.lifted.push_back({.at = m_held.at(lift.first), .canceled = lift.second});
            m_held.erase(lift.first);
        }
        for (const auto& press : pressing) {
            m_held[press.first] = press.second;
        }
    }

    void disconnect(Seen& seen)
    {
        for (const auto& entry : m_held) {
            seen.lifted.push_back({.at = entry.second, .canceled = true});
        }
        m_held.clear();
    }

    void report(Seen& seen) const
    {
        for (const auto& entry : m_held) {
            seen.held.push_back(entry.second);
        }
    }

private:
    std::uint16_t m_session = 0;
    std::map<std::uint8_t, Spot> m_held;
};

class UhidPhone
{
public:
    void rotate(int rotation) { m_rotation = rotation & 3; }

    void apply(const protocol::UhidCreate& create, Seen& seen)
    {
        if (m_layout) {
            cancelAll(seen);
        }
        m_layout = parseHid(create.reportDescriptor);
        m_id = create.id;
        m_expected = 0;
        m_last.clear();
        CHECK(m_layout->complete);
        for (const HidField& field : m_layout->fields) {
            if (field.kind == HidKind::Input && field.finger == 0 && field.usage == kHidX) {
                m_maximumX = static_cast<double>(field.logicalMaximum);
            } else if (field.kind == HidKind::Input && field.finger == 0 && field.usage == kHidY) {
                m_maximumY = static_cast<double>(field.logicalMaximum);
            }
        }
        CHECK(m_maximumX > 1.0);
        CHECK(m_maximumY > 1.0);
    }

    void apply(const protocol::UhidInput& input, Seen& seen)
    {
        if (!m_layout || input.id != m_id || input.report.empty()) {
            return;
        }
        const std::uint8_t reportId = m_layout->numbered ? input.report.front() : 0;
        CHECK(input.report.size() == m_layout->bytes(HidKind::Input, reportId));
        m_last = input.report;
        const std::uint32_t contacts = value(input, reportId, kHidContactCount, -1);
        if (contacts > 0) {
            m_expected = contacts;
        }
        for (int finger = 0; finger < m_layout->fingers && static_cast<std::uint32_t>(finger) < m_expected; ++finger) {
            const std::uint32_t id = value(input, reportId, kHidContactIdentifier, finger);
            if (value(input, reportId, kHidTipSwitch, finger) != 0) {
                m_held[id] = {.x = value(input, reportId, kHidX, finger), .y = value(input, reportId, kHidY, finger)};
            } else if (const auto held = m_held.find(id); held != m_held.end()) {
                seen.lifted.push_back({.at = picture(held->second), .canceled = false});
                m_held.erase(held);
            }
        }
    }

    void apply(const protocol::UhidDestroy& destroy, Seen& seen)
    {
        if (m_layout && destroy.id == m_id) {
            cancelAll(seen);
            m_layout.reset();
        }
    }

    void disconnect(Seen& seen)
    {
        if (!m_layout) {
            return;
        }
        if (!m_last.empty()) {
            protocol::UhidInput released;
            released.id = m_id;
            released.report.assign(m_last.size(), 0);
            if (m_layout->numbered) {
                released.report.front() = m_last.front();
            }
            apply(released, seen);
        }
        protocol::UhidDestroy destroy;
        destroy.id = m_id;
        apply(destroy, seen);
    }

    void report(Seen& seen) const
    {
        for (const auto& entry : m_held) {
            seen.held.push_back(picture(entry.second));
        }
    }

private:
    struct Natural {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
    };

    std::uint32_t value(const protocol::UhidInput& input, std::uint8_t reportId, std::uint32_t usage,
                        int finger) const
    {
        const HidField* field = m_layout->field(HidKind::Input, reportId, usage, finger);
        REQUIRE(field != nullptr);
        return hidValue(input.report, *field, m_layout->numbered);
    }

    Spot picture(Natural natural) const
    {
        const double u = natural.x / m_maximumX;
        const double v = natural.y / m_maximumY;
        switch (m_rotation) {
        case 1:
            return at(v, 1.0 - u);
        case 2:
            return at(1.0 - u, 1.0 - v);
        case 3:
            return at(1.0 - v, u);
        default:
            return at(u, v);
        }
    }

    void cancelAll(Seen& seen)
    {
        for (const auto& entry : m_held) {
            seen.lifted.push_back({.at = picture(entry.second), .canceled = true});
        }
        m_held.clear();
        m_expected = 0;
    }

    std::optional<HidLayout> m_layout;
    std::uint16_t m_id = 0;
    std::uint32_t m_expected = 0;
    std::vector<std::uint8_t> m_last;
    std::map<std::uint32_t, Natural> m_held;
    double m_maximumX = 1.0;
    double m_maximumY = 1.0;
    int m_rotation = 0;
};

class Backend
{
public:
    virtual ~Backend() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual ControlSink& sink() = 0;
    virtual void video(std::uint16_t session, int rotation) = 0;
    virtual void disconnect() = 0;
    [[nodiscard]] virtual Seen seen() = 0;
};

class InjectionBackend final : public Backend
{
public:
    [[nodiscard]] std::string name() const override { return "injection"; }
    [[nodiscard]] ControlSink& sink() override { return m_sink; }
    void video(std::uint16_t session, int) override { m_phone.video(session); }

    void disconnect() override
    {
        drain();
        m_phone.disconnect(m_pending);
    }

    [[nodiscard]] Seen seen() override
    {
        drain();
        Seen result = std::exchange(m_pending, {});
        m_phone.report(result);
        return result;
    }

private:
    void drain()
    {
        for (; m_read < m_sink.frames.size(); ++m_read) {
            m_phone.apply(m_sink.frames[m_read], m_pending);
        }
    }

    SinkLog m_sink;
    InjectionPhone m_phone;
    Seen m_pending;
    std::size_t m_read = 0;
};

class UhidBackend final : public Backend
{
public:
    UhidBackend()
    {
        m_sink.welcomed(protocol::Welcome::kUhid);
        m_sink.phaseChanged(session::SessionPhase::Streaming);
    }

    [[nodiscard]] std::string name() const override { return "uhid"; }
    [[nodiscard]] ControlSink& sink() override { return m_sink; }

    void video(std::uint16_t session, int rotation) override
    {
        m_sink.setVideo(session, rotation);
        m_phone.rotate(rotation);
    }

    void disconnect() override
    {
        drain();
        m_phone.disconnect(m_pending);
    }

    [[nodiscard]] Seen seen() override
    {
        drain();
        CHECK(m_injection.frames.empty());
        Seen result = std::exchange(m_pending, {});
        m_phone.report(result);
        return result;
    }

private:
    void drain()
    {
        for (; m_read < m_wire.messages.size(); ++m_read) {
            std::visit([this](const auto& message) { m_phone.apply(message, m_pending); }, m_wire.messages[m_read]);
        }
    }

    SinkLog m_injection;
    WireLog m_wire;
    UhidTouchSink m_sink{m_injection, m_wire};
    UhidPhone m_phone;
    Seen m_pending;
    std::size_t m_read = 0;
};

class AoaBackend final : public Backend
{
public:
    [[nodiscard]] std::string name() const override { return "aoa"; }
    [[nodiscard]] ControlSink& sink() override { return m_sink; }

    void video(std::uint16_t session, int rotation) override
    {
        m_sink.setVideo(session, rotation);
        m_phone.rotate(rotation);
    }

    void disconnect() override { drain(); }

    [[nodiscard]] Seen seen() override
    {
        drain();
        CHECK(m_injection.frames.empty());
        Seen result = std::exchange(m_pending, {});
        m_phone.report(result);
        return result;
    }

private:
    void drain()
    {
        m_port.flush();
        const std::vector<AoaTransfer> transfers = m_bench->taken();
        for (; m_read < transfers.size(); ++m_read) {
            const AoaTransfer& transfer = transfers[m_read];
            switch (transfer.request) {
            case aoa::kRegisterHid:
                m_descriptor.assign(transfer.index, 0);
                m_filled = 0;
                break;
            case aoa::kSetHidReportDescriptor:
                std::ranges::copy(transfer.data, m_descriptor.begin() + transfer.index);
                m_filled += transfer.data.size();
                if (m_filled == m_descriptor.size()) {
                    protocol::UhidCreate create;
                    create.id = transfer.value;
                    create.reportDescriptor = m_descriptor;
                    m_phone.apply(create, m_pending);
                }
                break;
            case aoa::kSendHidEvent: {
                protocol::UhidInput input;
                input.id = transfer.value;
                input.report = transfer.data;
                m_phone.apply(input, m_pending);
                break;
            }
            case aoa::kUnregisterHid: {
                protocol::UhidDestroy destroy;
                destroy.id = transfer.value;
                m_phone.apply(destroy, m_pending);
                break;
            }
            default:
                break;
            }
        }
    }

    std::shared_ptr<AoaBench> m_bench = std::make_shared<AoaBench>();
    AoaPort m_port{std::make_unique<FakeAoaLink>(m_bench)};
    SinkLog m_injection;
    AoaTouchSink m_sink{&m_injection, m_port, AoaDevices{}};
    UhidPhone m_phone;
    Seen m_pending;
    std::vector<std::uint8_t> m_descriptor;
    std::size_t m_filled = 0;
    std::size_t m_read = 0;
};

class RecordingModule final : public exthost::Module
{
public:
    RecordingModule()
    {
        m_info.touchMethods.push_back(
            {.name = "board", .maxContacts = 4, .canCancel = true, .needsSession = true, .keys = false});
    }

    [[nodiscard]] const exthost::ModuleInfo& info() const noexcept override { return m_info; }
    [[nodiscard]] exthost::CallResult call(std::size_t, std::span<const exthost::Value>) override { return {}; }
    [[nodiscard]] std::uint64_t openTouch(std::size_t method, std::string_view, std::string_view) override
    {
        return method == 0 ? 1 : 0;
    }

    [[nodiscard]] bool touch(std::uint64_t session, const exthost::TouchFrame& frame) override
    {
        if (session != 1) {
            return false;
        }
        frames.push_back(frame);
        return true;
    }

    [[nodiscard]] bool key(std::uint64_t, const exthost::KeyEvent&) override { return false; }
    void closeTouch(std::uint64_t) override { ++closes; }

    std::vector<exthost::TouchFrame> frames;
    int closes = 0;

private:
    exthost::ModuleInfo m_info;
};

class ExtensionPhone
{
public:
    void apply(const exthost::TouchFrame& frame, Seen& seen)
    {
        for (const exthost::Contact& contact : frame.contacts) {
            CHECK(contact.id < 4);
            const auto held = m_held.find(contact.id);
            switch (contact.action) {
            case exthost::TouchAction::Down:
                CHECK(held == m_held.end());
                m_held[contact.id] = picture(contact, frame.rotation);
                break;
            case exthost::TouchAction::Move:
                CHECK(held != m_held.end());
                m_held[contact.id] = picture(contact, frame.rotation);
                break;
            case exthost::TouchAction::Up:
                REQUIRE(held != m_held.end());
                seen.lifted.push_back({.at = picture(contact, frame.rotation), .canceled = false});
                m_held.erase(held);
                break;
            case exthost::TouchAction::Cancel:
                REQUIRE(held != m_held.end());
                seen.lifted.push_back({.at = held->second, .canceled = true});
                m_held.erase(held);
                break;
            }
        }
    }

    void report(Seen& seen) const
    {
        for (const auto& entry : m_held) {
            seen.held.push_back(entry.second);
        }
    }

private:
    static Spot picture(const exthost::Contact& contact, std::uint32_t rotation)
    {
        const double u = contact.naturalX;
        const double v = contact.naturalY;
        switch (rotation & 3U) {
        case 1:
            return at(v, 1.0 - u);
        case 2:
            return at(1.0 - u, 1.0 - v);
        case 3:
            return at(1.0 - v, u);
        default:
            return at(u, v);
        }
    }

    std::map<std::uint32_t, Spot> m_held;
};

class ExtensionBackend final : public Backend
{
public:
    [[nodiscard]] std::string name() const override { return "extension"; }
    [[nodiscard]] ControlSink& sink() override { return m_sink; }
    void video(std::uint16_t session, int rotation) override { m_sink.setVideo(session, rotation); }
    void disconnect() override { drain(); }

    [[nodiscard]] Seen seen() override
    {
        drain();
        CHECK(m_injection.frames.empty());
        CHECK_FALSE(m_sink.fellBack());
        Seen result = std::exchange(m_pending, {});
        m_phone.report(result);
        return result;
    }

private:
    void drain()
    {
        for (; m_read < m_module->frames.size(); ++m_read) {
            m_phone.apply(m_module->frames[m_read], m_pending);
        }
    }

    std::shared_ptr<RecordingModule> m_module = std::make_shared<RecordingModule>();
    SinkLog m_injection;
    ExtensionTouchSink m_sink{m_injection, std::make_shared<ModuleTouchPort>(m_module, 0, "phone"), {}};
    ExtensionPhone m_phone;
    Seen m_pending;
    std::size_t m_read = 0;
};

std::vector<std::unique_ptr<Backend>> backends()
{
    std::vector<std::unique_ptr<Backend>> all;
    all.push_back(std::make_unique<InjectionBackend>());
    all.push_back(std::make_unique<UhidBackend>());
    all.push_back(std::make_unique<AoaBackend>());
    all.push_back(std::make_unique<ExtensionBackend>());
    return all;
}

class Rig
{
public:
    explicit Rig(Backend& backend)
        : m_backend(backend)
    {
        m_channel.attach(backend.sink());
        video(7, 0);
        static_cast<void>(m_backend.seen());
    }

    void video(std::uint16_t session, int rotation)
    {
        m_backend.video(session, rotation);
        m_channel.setVideo(session, kWidth, kHeight);
        m_session = session;
    }

    Seen frame(std::initializer_list<protocol::TouchPoint> points) { return frame(m_session, points); }

    Seen frame(std::uint16_t session, std::initializer_list<protocol::TouchPoint> points)
    {
        protocol::TouchFrame touch;
        touch.videoSession = session;
        for (const protocol::TouchPoint& point : points) {
            touch.points.push_back(point);
        }
        CHECK(m_channel.send(touch));
        return m_backend.seen();
    }

    Seen detach()
    {
        m_channel.detach();
        m_backend.disconnect();
        return m_backend.seen();
    }

    [[nodiscard]] SinkCapabilities capabilities() const { return m_channel.capabilities(); }

private:
    Backend& m_backend;
    ControlChannel m_channel;
    std::uint16_t m_session = 0;
};

}

TEST_CASE("touch backend contract: a tap holds one finger where it lands and lifts it there (ADR-0009, HID-001)")
{
    for (const auto& backend : backends()) {
        INFO("backend ", backend->name());
        Rig rig(*backend);
        Seen seen = rig.frame({pressAt(0, 0.25, 0.75)});
        INFO(describe(seen));
        CHECK(holds(seen, {at(0.25, 0.75)}));
        CHECK(seen.lifted.empty());
        seen = rig.frame({liftAt(0, 0.25, 0.75)});
        CHECK(seen.held.empty());
        CHECK(lifts(seen, {at(0.25, 0.75)}, false));
    }
}

TEST_CASE("touch backend contract: two fingers press, move and lift in either order or together (ADR-0009, HID-001)")
{
    struct Finger {
        std::uint8_t id = 0;
        double x = 0.0;
        double y = 0.0;
    };
    const Finger a{.id = 0, .x = 0.3, .y = 0.4};
    const Finger b{.id = 1, .x = 0.6, .y = 0.5};
    for (const int order : {0, 1, 2}) {
        for (const auto& backend : backends()) {
            INFO("backend ", backend->name(), ", order ", order);
            Rig rig(*backend);
            Seen seen = rig.frame({pressAt(a.id, 0.2, 0.3)});
            INFO(describe(seen));
            CHECK(holds(seen, {at(0.2, 0.3)}));
            seen = rig.frame({pressAt(b.id, 0.7, 0.6)});
            CHECK(holds(seen, {at(0.2, 0.3), at(0.7, 0.6)}));
            seen = rig.frame({moveTo(a.id, a.x, a.y), moveTo(b.id, b.x, b.y)});
            CHECK(holds(seen, {at(a.x, a.y), at(b.x, b.y)}));
            CHECK(seen.lifted.empty());
            if (order == 2) {
                seen = rig.frame({liftAt(b.id, b.x, b.y), liftAt(a.id, a.x, a.y)});
                CHECK(seen.held.empty());
                CHECK(lifts(seen, {at(a.x, a.y), at(b.x, b.y)}, false));
                continue;
            }
            const Finger& first = order == 0 ? a : b;
            const Finger& second = order == 0 ? b : a;
            seen = rig.frame({liftAt(first.id, first.x, first.y)});
            CHECK(holds(seen, {at(second.x, second.y)}));
            CHECK(lifts(seen, {at(first.x, first.y)}, false));
            seen = rig.frame({moveTo(second.id, 0.5, 0.5)});
            CHECK(holds(seen, {at(0.5, 0.5)}));
            seen = rig.frame({liftAt(second.id, 0.5, 0.5)});
            CHECK(seen.held.empty());
            CHECK(lifts(seen, {at(0.5, 0.5)}, false));
        }
    }
}

TEST_CASE("touch backend contract: a pointer id used again after its release is a new finger (ADR-0009, HID-001)")
{
    for (const auto& backend : backends()) {
        INFO("backend ", backend->name());
        Rig rig(*backend);
        Seen seen = rig.frame({pressAt(4, 0.1, 0.2)});
        INFO(describe(seen));
        seen = rig.frame({liftAt(4, 0.1, 0.2)});
        CHECK(seen.held.empty());
        CHECK(lifts(seen, {at(0.1, 0.2)}, false));
        seen = rig.frame({pressAt(4, 0.9, 0.8)});
        CHECK(holds(seen, {at(0.9, 0.8)}));
        CHECK(seen.lifted.empty());
        seen = rig.frame({moveTo(4, 0.8, 0.7)});
        CHECK(holds(seen, {at(0.8, 0.7)}));
        seen = rig.frame({liftAt(4, 0.8, 0.7)});
        CHECK(seen.held.empty());
        CHECK(lifts(seen, {at(0.8, 0.7)}, false));
    }
}

TEST_CASE("touch backend contract: fingers beyond the backend's maximum are ignored and the rest keep working "
          "(ADR-0009, HID-001)")
{
    for (const auto& backend : backends()) {
        INFO("backend ", backend->name());
        Rig rig(*backend);
        const SinkCapabilities capabilities = rig.capabilities();
        CHECK(capabilities.maxContacts >= 1);
        CHECK(capabilities.maxContacts <= static_cast<int>(protocol::TouchFrame::kMaxPoints));
        const int pressed = std::min(capabilities.maxContacts + 1, static_cast<int>(protocol::TouchFrame::kMaxPoints));
        const int kept = std::min(pressed, capabilities.maxContacts);
        Seen seen;
        INFO(describe(seen));
        for (int finger = 0; finger < pressed; ++finger) {
            seen = rig.frame({pressAt(static_cast<std::uint8_t>(finger), 0.02 + 0.03 * finger, 0.5)});
        }
        CHECK(seen.held.size() == static_cast<std::size_t>(kept));
        std::size_t lifted = 0;
        for (int finger = 0; finger < pressed; ++finger) {
            seen = rig.frame({liftAt(static_cast<std::uint8_t>(finger), 0.02 + 0.03 * finger, 0.5)});
            lifted += seen.lifted.size();
        }
        CHECK(lifted == static_cast<std::size_t>(kept));
        CHECK(seen.held.empty());
        seen = rig.frame({pressAt(0, 0.5, 0.25)});
        CHECK(holds(seen, {at(0.5, 0.25)}));
    }
}

TEST_CASE("touch backend contract: a frame for an old video session neither presses nor moves, but still lifts "
          "(ADR-0009, HID-001)")
{
    for (const auto& backend : backends()) {
        INFO("backend ", backend->name());
        Rig rig(*backend);
        Seen seen = rig.frame(6, {pressAt(0, 0.3, 0.3)});
        INFO(describe(seen));
        CHECK(seen.held.empty());
        CHECK(seen.lifted.empty());
        seen = rig.frame({pressAt(1, 0.4, 0.4)});
        CHECK(holds(seen, {at(0.4, 0.4)}));
        rig.video(8, 0);
        seen = rig.frame(7, {moveTo(1, 0.6, 0.6)});
        CHECK(holds(seen, {at(0.4, 0.4)}));
        seen = rig.frame(7, {pressAt(2, 0.5, 0.5)});
        CHECK(holds(seen, {at(0.4, 0.4)}));
        seen = rig.frame(7, {liftAt(1, 0.6, 0.6)});
        CHECK(seen.held.empty());
        CHECK(lifts(seen, {at(0.4, 0.4)}, false));
    }
}

TEST_CASE("touch backend contract: a cancel lifts the finger where it last was, as a cancel where the backend has "
          "one (ADR-0009, HID-001)")
{
    for (const auto& backend : backends()) {
        INFO("backend ", backend->name());
        Rig rig(*backend);
        const bool cancels = rig.capabilities().cancel;
        Seen seen = rig.frame({pressAt(0, 0.3, 0.6)});
        INFO(describe(seen));
        seen = rig.frame({pressAt(1, 0.8, 0.2)});
        seen = rig.frame({moveTo(0, 0.35, 0.65)});
        seen = rig.frame({cancelAt(0, 0.9, 0.1)});
        CHECK(holds(seen, {at(0.8, 0.2)}));
        CHECK(lifts(seen, {at(0.35, 0.65)}, cancels));
        seen = rig.frame({cancelAt(1, 0.1, 0.9)});
        CHECK(seen.held.empty());
        CHECK(lifts(seen, {at(0.8, 0.2)}, cancels));
    }
}

TEST_CASE("touch backend contract: detaching with fingers down lifts every one of them (ADR-0009, HID-001)")
{
    for (const auto& backend : backends()) {
        INFO("backend ", backend->name());
        Rig rig(*backend);
        Seen seen = rig.frame({pressAt(0, 0.2, 0.2)});
        INFO(describe(seen));
        seen = rig.frame({pressAt(1, 0.8, 0.8)});
        CHECK(holds(seen, {at(0.2, 0.2), at(0.8, 0.8)}));
        seen = rig.detach();
        CHECK(seen.held.empty());
        CHECK(lifts(seen, {at(0.2, 0.2), at(0.8, 0.8)}));
    }
}

TEST_CASE("touch backend contract: every rotation of the phone puts fingers on the same picture positions "
          "(ADR-0009, HID-001)")
{
    for (int rotation = 0; rotation < 4; ++rotation) {
        for (const auto& backend : backends()) {
            INFO("backend ", backend->name(), ", rotation ", rotation);
            Rig rig(*backend);
            rig.video(9, rotation);
            Seen seen = rig.frame({pressAt(0, 0.25, 0.8), pressAt(1, 1.0, 0.0)});
            INFO(describe(seen));
            CHECK(holds(seen, {at(0.25, 0.8), at(1.0, 0.0)}));
            seen = rig.frame({moveTo(0, 0.7, 0.1)});
            CHECK(holds(seen, {at(0.7, 0.1), at(1.0, 0.0)}));
            seen = rig.frame({liftAt(0, 0.6, 0.2), liftAt(1, 1.0, 0.0)});
            CHECK(seen.held.empty());
            CHECK(lifts(seen, {at(0.6, 0.2), at(1.0, 0.0)}, false));
        }
    }
}
