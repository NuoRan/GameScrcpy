#include "ControlChannel.h"
#include "InputHost.h"
#include "PictureZoom.h"
#include "ProfileEditor.h"

#include <doctest/doctest.h>

#include <QObject>
#include <QPointer>
#include <QSizeF>
#include <QVariantList>
#include <QVariantMap>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

using namespace gs;
using namespace gs::app;
using namespace std::chrono_literals;

namespace {

const input::ScreenGeometry kScreen{.width = 2400, .height = 1080};

constexpr std::string_view kProfile = R"({"format":"gamescrcpy.profile","version":3,
    "controls":[
        {"id":"jump","type":"hold","anchor":"bottomRight","offset":[-0.2,-0.2],"inputs":{"press":"Space"}}]})";

constexpr platform::RawKey kSpace{.makeCode = 0x39};
constexpr platform::RawKey kSpaceUp{.makeCode = 0x39, .released = true};

std::shared_ptr<const input::Profile> profileOf(std::string_view json)
{
    const input::LoadResult loaded = input::loadProfile(json);
    REQUIRE(loaded.ok());
    return std::make_shared<const input::Profile>(loaded.profile);
}

input::Vec2 centerOf(const input::Profile& profile, std::size_t index)
{
    const input::Control& control = profile.controls.at(index);
    return input::CoordinateMapper(kScreen, profile.screen.scaleMode, profile.screen.safeArea)
        .position(control.anchor, control.offset);
}

bool near(const std::optional<input::Vec2>& touched, input::Vec2 expected)
{
    return touched && std::abs(touched->x - expected.x) < 1e-3 && std::abs(touched->y - expected.y) < 1e-3;
}

class TouchLog final : public ControlSink
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

    std::vector<protocol::TouchFrame> frames;
};

class Engine
{
public:
    Engine()
    {
        m_channel->attach(m_log);
        m_channel->setVideo(7, 2400, 1080);
        m_host.setChannel(m_channel, tick());
        m_host.setActive(true, tick());
    }

    std::optional<input::Vec2> tap(std::shared_ptr<const input::Profile> profile)
    {
        m_host.setProfile(std::move(profile), tick());
        const std::size_t before = m_log.frames.size();
        m_host.key(kSpace, tick());
        m_host.key(kSpaceUp, tick());
        if (m_log.frames.size() <= before || m_log.frames[before].points.empty()) {
            return std::nullopt;
        }
        const protocol::TouchPoint& down = m_log.frames[before].points[0];
        return input::Vec2{down.x / 65535.0, down.y / 65535.0};
    }

private:
    std::chrono::microseconds tick() { return m_clock += 1000us; }

    TouchLog m_log;
    std::shared_ptr<ControlChannel> m_channel = std::make_shared<ControlChannel>();
    InputHost m_host{{.pressedMouse = [] { return std::uint8_t{0}; }}};
    std::chrono::microseconds m_clock{0};
};

}

TEST_CASE("trying hands the working copy to the engine, unsaved changes included, and the saved profile comes back "
          "(EDT-008)")
{
    const std::shared_ptr<const input::Profile> saved = profileOf(kProfile);
    auto editor = std::make_unique<ProfileEditor>(*saved, kScreen);
    int switches = 0;
    QObject::connect(editor.get(), &ProfileEditor::tryingChanged, [&switches] { ++switches; });
    Engine engine;

    CHECK_FALSE(editor->trying());
    CHECK(editor->trial() == nullptr);
    CHECK(profileInUse(editor.get(), saved) == saved);

    editor->select({QStringLiteral("jump")});
    editor->moveSelection(-0.3, -0.2);
    REQUIRE(editor->dirty());
    CHECK(profileInUse(editor.get(), saved) == saved);

    editor->setTrying(true);
    editor->setTrying(true);
    CHECK(switches == 1);
    CHECK(editor->trying());
    const std::shared_ptr<const input::Profile> trial = profileInUse(editor.get(), saved);
    REQUIRE(trial != nullptr);
    CHECK(trial != saved);
    CHECK(*trial == editor->profile());
    CHECK(near(engine.tap(trial), centerOf(editor->profile(), 0)));
    CHECK_FALSE(near(engine.tap(trial), centerOf(*saved, 0)));

    editor->moveSelection(0.1, 0.0);
    const std::shared_ptr<const input::Profile> nudged = profileInUse(editor.get(), saved);
    CHECK(nudged != trial);
    CHECK(near(engine.tap(nudged), centerOf(editor->profile(), 0)));

    editor->setTrying(false);
    CHECK(switches == 2);
    CHECK(editor->trial() == nullptr);
    CHECK(profileInUse(editor.get(), saved) == saved);
    CHECK(near(engine.tap(profileInUse(editor.get(), saved)), centerOf(*saved, 0)));
    CHECK(editor->dirty());

    editor->setTrying(true);
    const QPointer<ProfileEditor> closing(editor.get());
    editor.reset();
    CHECK(closing == nullptr);
    CHECK(profileInUse(closing, saved) == saved);
}

TEST_CASE("a trial keeps its snapshot through selecting and saving, and takes a new one after each edit")
{
    ProfileEditor editor(*profileOf(kProfile), kScreen);
    editor.setTrying(true);
    const std::shared_ptr<const input::Profile> first = editor.trial();
    REQUIRE(first != nullptr);
    editor.select({QStringLiteral("jump")});
    CHECK(editor.trial() == first);
    editor.markSaved();
    CHECK(editor.trial() == first);

    editor.moveSelection(0.05, 0.0);
    const std::shared_ptr<const input::Profile> moved = editor.trial();
    CHECK(moved != first);
    CHECK(*moved == editor.profile());
    editor.undo();
    const std::shared_ptr<const input::Profile> undone = editor.trial();
    CHECK(undone != moved);
    CHECK(*undone == *first);
}

TEST_CASE("the aspect preview places every key where the mapper puts it on a screen of that shape (EDT-012)")
{
    ProfileEditor editor(input::Profile{}, kScreen);
    editor.addControl(QStringLiteral("hold"), 0.9, 0.85);
    editor.addControl(QStringLiteral("wasd"), 0.2, 0.7);
    editor.addControl(QStringLiteral("swipe"), 0.6, 0.4);
    const input::Profile& profile = editor.profile();
    std::vector<double> buttonShares;
    for (const QSizeF size : {QSizeF(1600, 900), QSizeF(2100, 900), QSizeF(900, 1950)}) {
        CAPTURE(size.width());
        const input::CoordinateMapper mapper({.width = size.width(), .height = size.height()},
                                             profile.screen.scaleMode, profile.screen.safeArea);
        const QVariantList markers = editor.previewMarkers(size.width(), size.height());
        REQUIRE(markers.size() == 3);
        for (const QVariant& item : markers) {
            const QVariantMap marker = item.toMap();
            const auto index = static_cast<std::size_t>(marker.value(QStringLiteral("control")).toInt());
            const input::Control& control = profile.controls.at(index);
            const input::Vec2 center = mapper.position(control.anchor, control.offset);
            CHECK(marker.value(QStringLiteral("x")).toDouble() == doctest::Approx(center.x * size.width()));
            CHECK(marker.value(QStringLiteral("y")).toDouble() == doctest::Approx(center.y * size.height()));
            if (const auto* stick = std::get_if<input::WasdControl>(&control.kind)) {
                CHECK(marker.value(QStringLiteral("rx")).toDouble() ==
                      doctest::Approx(mapper.radius(stick->radius).x * size.width()));
                CHECK(marker.value(QStringLiteral("ry")).toDouble() ==
                      doctest::Approx(mapper.radius(stick->radius).y * size.height()));
            }
            if (const auto* swipe = std::get_if<input::SwipeControl>(&control.kind)) {
                const QVariantList path = marker.value(QStringLiteral("path")).toList();
                REQUIRE(path.size() == static_cast<qsizetype>(swipe->path.size()));
                for (std::size_t i = 0; i < swipe->path.size(); ++i) {
                    const input::Vec2 extent = mapper.extent(swipe->path[i]);
                    const QVariantMap point = path.at(static_cast<qsizetype>(i)).toMap();
                    CHECK(point.value(QStringLiteral("x")).toDouble() ==
                          doctest::Approx((center.x + extent.x) * size.width()));
                    CHECK(point.value(QStringLiteral("y")).toDouble() ==
                          doctest::Approx((center.y + extent.y) * size.height()));
                }
            }
            if (index == 0) {
                buttonShares.push_back(marker.value(QStringLiteral("x")).toDouble() / size.width());
            }
        }
    }
    REQUIRE(buttonShares.size() == 3);
    CHECK(buttonShares[0] != doctest::Approx(buttonShares[1]));
    CHECK(editor.markers(2400, 1080) == editor.previewMarkers(2400, 1080));
}

TEST_CASE("zooming keeps the picture point under the pointer, centres a small picture and stops at the edges")
{
    const QRectF view(0, 88, 1000, 500);
    const QSizeF frame(2400, 1080);

    const ZoomedPicture fit = zoomedPicture(view, frame, 1.0, {300, 200});
    CHECK(fit.picture.width() == doctest::Approx(1000));
    CHECK(fit.picture.height() == doctest::Approx(450));
    CHECK(fit.picture.y() == doctest::Approx(113));
    CHECK(fit.scroll == QPointF());

    const ZoomedPicture half = zoomedPicture(view, frame, 0.5, {300, 300});
    CHECK(half.picture.width() == doctest::Approx(500));
    CHECK(half.picture.center().x() == doctest::Approx(view.center().x()));
    CHECK(half.picture.center().y() == doctest::Approx(view.center().y()));
    CHECK(half.scroll == QPointF());

    const ZoomedPicture far = zoomedPicture(view, frame, 2.0, {5000, -40});
    CHECK(far.picture.size() == QSizeF(2000, 900));
    CHECK(far.scroll.x() == doctest::Approx(1000));
    CHECK(far.scroll.y() == doctest::Approx(0));
    CHECK(far.picture.right() == doctest::Approx(view.right()));
    CHECK(far.picture.top() == doctest::Approx(view.top()));

    const QPointF pointer = view.center();
    const QPointF share((pointer.x() - fit.picture.x()) / fit.picture.width(),
                        (pointer.y() - fit.picture.y()) / fit.picture.height());
    const QPointF scroll = scrollKeeping(view, frame, 1.0, 2.0, {}, pointer);
    const ZoomedPicture zoomed = zoomedPicture(view, frame, 2.0, scroll);
    CHECK(zoomed.scroll.x() == doctest::Approx(scroll.x()));
    CHECK(zoomed.scroll.y() == doctest::Approx(scroll.y()));
    CHECK(zoomed.picture.x() + share.x() * zoomed.picture.width() == doctest::Approx(pointer.x()));
    CHECK(zoomed.picture.y() + share.y() * zoomed.picture.height() == doctest::Approx(pointer.y()));

    const QPointF corner(view.right() - 1, view.bottom() - 1);
    const ZoomedPicture edge = zoomedPicture(view, frame, 1.5, scrollKeeping(view, frame, 1.0, 1.5, {}, corner));
    CHECK(edge.picture.left() <= view.left());
    CHECK(edge.picture.right() >= view.right() - 1e-6);
    CHECK(edge.picture.top() <= view.top());
    CHECK(edge.picture.bottom() >= view.bottom() - 1e-6);

    const ZoomedPicture back = zoomedPicture(view, frame, 1.0, scrollKeeping(view, frame, 2.0, 1.0, scroll, pointer));
    CHECK(back.picture == fit.picture);
}

TEST_CASE("keys on a zoomed picture land where the pointer finds them, at every zoom step and scroll")
{
    ProfileEditor editor(input::Profile{}, kScreen);
    editor.addControl(QStringLiteral("hold"), 0.9, 0.85);
    editor.addControl(QStringLiteral("wasd"), 0.2, 0.7);
    const QRectF view(0, 88, 1000, 500);
    const QSizeF frame(2400, 1080);
    for (const double zoom : {0.5, 0.75, 1.0, 1.25, 1.5, 2.0}) {
        for (const QPointF scroll : {QPointF(0, 0), QPointF(180, 40), QPointF(5000, 5000)}) {
            CAPTURE(zoom);
            const QRectF picture = zoomedPicture(view, frame, zoom, scroll).picture;
            const QVariantList markers = editor.markers(picture.width(), picture.height());
            REQUIRE(markers.size() == 2);
            for (std::size_t i = 0; i < 2; ++i) {
                const QVariantMap marker = markers.at(static_cast<qsizetype>(i)).toMap();
                const QPointF spot(picture.x() + marker.value(QStringLiteral("x")).toDouble(),
                                   picture.y() + marker.value(QStringLiteral("y")).toDouble());
                const input::Vec2 place = centerOf(editor.profile(), i);
                CHECK((spot.x() - picture.x()) / picture.width() == doctest::Approx(place.x));
                CHECK((spot.y() - picture.y()) / picture.height() == doctest::Approx(place.y));
            }
        }
    }
}
