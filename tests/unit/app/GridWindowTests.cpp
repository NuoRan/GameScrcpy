#include "GridWindow.h"

#include <gs/render/FrameSlot.h>
#include <gs/render/GraphicsDevice.h>
#include <gs/render/VideoItem.h>

#include <doctest/doctest.h>

#include <QObject>
#include <QQuickItem>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

using gs::app::GridWindow;
using gs::render::FrameSlot;
using gs::render::GraphicsDevice;
using gs::render::VideoItem;
using namespace Qt::StringLiterals;

namespace {

std::vector<VideoItem*> videosIn(const GridWindow& grid)
{
    std::vector<VideoItem*> videos;
    for (QQuickItem* child : grid.contentItem()->childItems()) {
        if (auto* video = qobject_cast<VideoItem*>(child); video != nullptr) {
            videos.push_back(video);
        }
    }
    return videos;
}

int croppedTo(const std::vector<VideoItem*>& videos, const QRectF& crop)
{
    int count = 0;
    for (const VideoItem* video : videos) {
        if (video->crop() == crop) {
            ++count;
        }
    }
    return count;
}

}

TEST_CASE("a capture card's crop reaches the video of its grid cell, also after the picture comes back to the grid "
          "(ADR-0101, ADR-0096)")
{
    const auto graphics = GraphicsDevice::create();
    REQUIRE(graphics);
    GridWindow grid(*graphics);
    const QRectF whole(0.0, 0.0, 1.0, 1.0);
    const QRectF crop(0.125, 0.0, 0.75, 1.0);
    REQUIRE(grid.addCell(u"capture-demo"_s, u"Capture card"_s, 0.5));
    grid.setCrop(u"capture-demo"_s, crop);
    grid.setFrames(u"capture-demo"_s, std::make_shared<FrameSlot>());
    std::vector<VideoItem*> videos = videosIn(grid);
    REQUIRE(videos.size() == 1);
    CHECK(videos.front()->crop() == crop);

    const QRectF moved(0.25, 0.125, 0.5, 0.75);
    grid.setCrop(u"capture-demo"_s, moved);
    CHECK(videos.front()->crop() == moved);

    grid.setFrames(u"capture-demo"_s, nullptr);
    CHECK(videosIn(grid).empty());
    grid.setFrames(u"capture-demo"_s, std::make_shared<FrameSlot>());
    videos = videosIn(grid);
    REQUIRE(videos.size() == 1);
    CHECK(videos.front()->crop() == moved);

    REQUIRE(grid.addCell(u"phone"_s, u"Phone"_s, 0.45));
    grid.setFrames(u"phone"_s, std::make_shared<FrameSlot>());
    videos = videosIn(grid);
    REQUIRE(videos.size() == 2);
    CHECK(croppedTo(videos, moved) == 1);
    CHECK(croppedTo(videos, whole) == 1);

    grid.setCrop(u"missing"_s, crop);
    CHECK(croppedTo(videosIn(grid), crop) == 0);
    CHECK(grid.removeCell(u"capture-demo"_s));
    videos = videosIn(grid);
    REQUIRE(videos.size() == 1);
    CHECK(videos.front()->crop() == whole);
}

TEST_CASE("ADR-0107: the x on the grid toolbar lets go of the keys and mouse and tells the devices once")
{
    const auto graphics = GraphicsDevice::create();
    REQUIRE(graphics);
    GridWindow grid(*graphics);
    REQUIRE(grid.addCell(u"phone"_s, u"Phone"_s, 0.45));
    grid.setFrames(u"phone"_s, std::make_shared<FrameSlot>());
    QStringList chosen;
    QObject::connect(&grid, &GridWindow::targetChosen, [&chosen](const QString& serial) { chosen.append(serial); });

    grid.setTarget(u"phone"_s);
    CHECK(grid.target() == u"phone"_s);
    CHECK(chosen.isEmpty());

    grid.releaseTarget();
    CHECK(grid.target().isEmpty());
    REQUIRE(chosen.size() == 1);
    CHECK(chosen.front().isEmpty());

    grid.releaseTarget();
    CHECK(chosen.size() == 1);
    grid.setTarget(u"phone"_s);
    CHECK(grid.target() == u"phone"_s);
}
