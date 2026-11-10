#include "MirrorInputItem.h"

#include <gs/render/FrameSlot.h>
#include <gs/render/VideoItem.h>

#include <doctest/doctest.h>

#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>

#include <array>
#include <cmath>
#include <memory>
#include <optional>

using gs::app::MirrorInputItem;
using gs::input::Vec2;
using gs::render::FrameSlot;
using gs::render::VideoItem;

TEST_CASE("MED-003: at every window size the picture keeps its shape in the middle, and input maps through that rectangle")
{
    VideoItem video;
    video.setPlaceholderSize(QSize(1080, 2400));
    const std::array<QSizeF, 5> windows{QSizeF(400, 300), QSizeF(1280, 720), QSizeF(540, 1200), QSizeF(300, 1200),
                                        QSizeF(1920, 1080)};
    for (const QSizeF& window : windows) {
        CAPTURE(window.width());
        CAPTURE(window.height());
        video.setSize(window);
        const QRectF content = video.contentRect();
        REQUIRE_FALSE(content.isEmpty());
        CHECK(content.width() / content.height() == doctest::Approx(1080.0 / 2400.0));
        const bool fillsWidth = std::abs(content.width() - window.width()) < 0.5;
        const bool fillsHeight = std::abs(content.height() - window.height()) < 0.5;
        CHECK((fillsWidth || fillsHeight));
        CHECK(content.left() > -0.5);
        CHECK(content.top() > -0.5);
        CHECK(content.right() < window.width() + 0.5);
        CHECK(content.bottom() < window.height() + 0.5);
        CHECK(content.center().x() == doctest::Approx(window.width() / 2));
        CHECK(content.center().y() == doctest::Approx(window.height() / 2));

        const QPointF inside(content.left() + 0.25 * content.width(), content.top() + 0.75 * content.height());
        const std::optional<Vec2> mapped = MirrorInputItem::normalized(content, inside, false);
        REQUIRE(mapped.has_value());
        CHECK(mapped->x == doctest::Approx(0.25));
        CHECK(mapped->y == doctest::Approx(0.75));
        const std::optional<Vec2> middle = MirrorInputItem::normalized(content, content.center(), false);
        REQUIRE(middle.has_value());
        CHECK(middle->x == doctest::Approx(0.5));
        CHECK(middle->y == doctest::Approx(0.5));

        if (content.left() > 2.0) {
            const QPointF bar(content.left() / 2, content.center().y());
            CHECK_FALSE(MirrorInputItem::normalized(content, bar, false).has_value());
            const std::optional<Vec2> held = MirrorInputItem::normalized(content, bar, true);
            REQUIRE(held.has_value());
            CHECK(held->x == doctest::Approx(0.0));
        }
        if (content.top() > 2.0) {
            const QPointF bar(content.center().x(), content.top() / 2);
            CHECK_FALSE(MirrorInputItem::normalized(content, bar, false).has_value());
        }
    }
}

TEST_CASE("MED-004: low-latency mode hands every picture on as it arrives instead of waiting for the screen, and turns back")
{
    const auto slot = std::make_shared<FrameSlot>();
    VideoItem video;
    video.setSource(slot);
    CHECK_FALSE(slot->paced());

    video.setImmediate(true);
    CHECK(slot->paced());
    video.setImmediate(false);
    CHECK_FALSE(slot->paced());

    video.setImmediate(true);
    const auto next = std::make_shared<FrameSlot>();
    video.setSource(next);
    CHECK(next->paced());
}
