#include "ControlChannel.h"
#include "DemoFeed.h"
#include "GridCells.h"
#include "GridLayout.h"
#include "InputHost.h"
#include "TouchSinkFakes.h"

#include <doctest/doctest.h>

#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string_view>
#include <vector>

using gs::app::GridCells;
using gs::app::GridHints;
using gs::app::GridItem;
using gs::app::GridShape;
using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace {

constexpr double kPortrait = 0.45;
constexpr double kLandscape = 2.2;
constexpr double kGap = 8.0;

constexpr std::string_view kJump = R"({"format":"gamescrcpy.profile","version":3,
    "controls":[{"id":"jump","type":"hold","anchor":"bottomRight","offset":[-0.2,-0.2],"inputs":{"press":"Space"}}]})";

std::vector<GridItem> phones(int count, double aspect, QSizeF chrome = QSizeF(0.0, 0.0))
{
    return std::vector<GridItem>(static_cast<std::size_t>(count), GridItem{.aspect = aspect, .chrome = chrome});
}

std::vector<GridItem> mixed(int count)
{
    std::vector<GridItem> items;
    for (int i = 0; i < count; ++i) {
        items.push_back({.aspect = i % 2 == 0 ? kPortrait : kLandscape});
    }
    return items;
}

double pictureArea(const std::vector<QRectF>& cells, const std::vector<GridItem>& items)
{
    double total = 0.0;
    for (std::size_t i = 0; i < cells.size() && i < items.size(); ++i) {
        const QRectF bounds = cells[i].adjusted(0.0, items[i].chrome.height(), -items[i].chrome.width(), 0.0);
        const QRectF picture = gs::app::fittedPicture(bounds, items[i].aspect);
        total += picture.width() * picture.height();
    }
    return total;
}

void checkBest(QSizeF area, const std::vector<GridItem>& items)
{
    const int count = static_cast<int>(items.size());
    const GridShape best = gs::app::gridShape(area, items, kGap);
    REQUIRE(best.columns >= 1);
    CHECK(best.columns <= count);
    CHECK(best.rows == (count + best.columns - 1) / best.columns);
    const QRectF bounds(QPointF(0.0, 0.0), area);
    const std::vector<QRectF> cells = gs::app::gridCells(bounds, count, best, kGap);
    REQUIRE(cells.size() == items.size());
    for (std::size_t i = 0; i < cells.size(); ++i) {
        CHECK(cells[i].left() >= -1e-6);
        CHECK(cells[i].top() >= -1e-6);
        CHECK(cells[i].right() <= area.width() + 1e-6);
        CHECK(cells[i].bottom() <= area.height() + 1e-6);
        for (std::size_t j = i + 1; j < cells.size(); ++j) {
            CHECK_FALSE(cells[i].intersects(cells[j]));
        }
    }
    const double chosen = pictureArea(cells, items);
    for (int columns = 1; columns <= count; ++columns) {
        const GridShape other{.columns = columns, .rows = (count + columns - 1) / columns};
        CHECK(pictureArea(gs::app::gridCells(bounds, count, other, kGap), items) <= chosen * (1.0 + 1e-6) + 1e-6);
    }
}

void checkTiles(const QRect& area, const std::vector<GridItem>& windows)
{
    const std::vector<QRect> tiles = gs::app::tileWindows(area, windows, 8);
    REQUIRE(tiles.size() == windows.size());
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const QRect& tile = tiles[i];
        REQUIRE_FALSE(tile.isEmpty());
        CHECK(area.contains(tile));
        const double width = tile.width() - windows[i].chrome.width();
        const double height = tile.height() - windows[i].chrome.height();
        REQUIRE(height > 0.0);
        CHECK(std::abs(width / height - windows[i].aspect) <= 0.02 * windows[i].aspect);
        for (std::size_t j = i + 1; j < tiles.size(); ++j) {
            CHECK_FALSE(tile.intersects(tiles[j]));
        }
    }
}

GridCells laidOut(const QStringList& serials)
{
    GridCells cells;
    for (const QString& serial : serials) {
        REQUIRE(cells.add(serial, u"Phone "_s + serial, kPortrait, false));
    }
    cells.setDoubleClick(400, 5.0);
    cells.layout(QRectF(0.0, 40.0, 1600.0, 860.0), {});
    return cells;
}

QPointF middleOf(const GridCells& cells, const QString& serial)
{
    const GridCells::Cell* cell = cells.find(serial);
    REQUIRE(cell != nullptr);
    return cell->view.center();
}

QPointF headerOf(const GridCells& cells, const QString& serial)
{
    const GridCells::Cell* cell = cells.find(serial);
    REQUIRE(cell != nullptr);
    return cell->header.center();
}

constexpr double kTop = 84.0;
constexpr double kHeightShare = 0.8;
constexpr GridCells::Metrics kChrome{.header = 32.0, .gap = 8.0, .margin = 8.0, .inset = 2.0, .menu = 30.0};
constexpr QSize kScreen(1920, 1040);

GridCells phonesIn(const std::vector<double>& aspects)
{
    GridCells cells;
    for (std::size_t i = 0; i < aspects.size(); ++i) {
        REQUIRE(cells.add(QString::number(i), u"Phone"_s, aspects[i], false));
    }
    return cells;
}

void checkPicturesFit(GridCells& cells, QSize window, double aspect)
{
    cells.layout(QRectF(0.0, kTop, window.width(), window.height() - kTop), kChrome);
    REQUIRE_FALSE(cells.cells().empty());
    for (const GridCells::Cell& cell : cells.cells()) {
        REQUIRE(cell.view.height() > 0.0);
        CHECK(std::abs(cell.view.width() / cell.view.height() - aspect) <= 0.01 * aspect);
    }
}

}

TEST_CASE("the grid picks rows and columns that give 1 to 12 phones the most picture, whatever their shape (ADR-0096)")
{
    const std::vector<QSizeF> areas{QSizeF(1600.0, 900.0), QSizeF(900.0, 1600.0), QSizeF(1200.0, 1200.0)};
    for (const QSizeF& area : areas) {
        for (int count = 1; count <= 12; ++count) {
            CAPTURE(count);
            checkBest(area, phones(count, kPortrait));
            checkBest(area, phones(count, kLandscape));
            checkBest(area, mixed(count));
            checkBest(area, phones(count, kPortrait, QSizeF(4.0, 30.0)));
        }
    }

    const QSizeF wide(1600.0, 900.0);
    CHECK(gs::app::gridShape(wide, phones(1, kPortrait), kGap) == GridShape{.columns = 1, .rows = 1});
    CHECK(gs::app::gridShape(wide, phones(2, kPortrait), kGap) == GridShape{.columns = 2, .rows = 1});
    CHECK(gs::app::gridShape(wide, phones(2, kLandscape), kGap) == GridShape{.columns = 1, .rows = 2});
    CHECK(gs::app::gridShape(wide, phones(4, kPortrait), kGap) == GridShape{.columns = 4, .rows = 1});
    CHECK(gs::app::gridShape(wide, phones(4, kLandscape), kGap) == GridShape{.columns = 2, .rows = 2});
    CHECK(gs::app::gridShape(wide, phones(12, kPortrait), kGap) == GridShape{.columns = 6, .rows = 2});
    CHECK(gs::app::gridShape(wide, mixed(3), kGap) == GridShape{.columns = 3, .rows = 1});
    CHECK(gs::app::gridShape(QSizeF(900.0, 1600.0), phones(3, kPortrait), kGap) == GridShape{.columns = 2, .rows = 2});
    CHECK(gs::app::gridShape(wide, {}, kGap) == GridShape{});
}

TEST_CASE("grid cells are the same size, leave the gap between them and centre a short last row (ADR-0096)")
{
    const std::vector<QRectF> cells =
        gs::app::gridCells(QRectF(0.0, 0.0, 1000.0, 600.0), 5, GridShape{.columns = 3, .rows = 2}, 10.0);
    REQUIRE(cells.size() == 5);
    const double width = (1000.0 - 20.0) / 3.0;
    for (const QRectF& cell : cells) {
        CHECK(cell.width() == doctest::Approx(width));
        CHECK(cell.height() == doctest::Approx(295.0));
    }
    CHECK(cells[0].x() == doctest::Approx(0.0));
    CHECK(cells[1].x() == doctest::Approx(width + 10.0));
    CHECK(cells[2].x() == doctest::Approx(2.0 * (width + 10.0)));
    CHECK(cells[3].x() == doctest::Approx((width + 10.0) / 2.0));
    CHECK(cells[4].x() == doctest::Approx(1.5 * (width + 10.0)));
    CHECK(cells[3].y() == doctest::Approx(305.0));
    CHECK(gs::app::gridCells(QRectF(0.0, 0.0, 100.0, 100.0), 0, GridShape{.columns = 1, .rows = 1}, 10.0).empty());

    const QRectF tall = gs::app::fittedPicture(QRectF(0.0, 0.0, 400.0, 400.0), 0.5);
    CHECK(tall == QRectF(100.0, 0.0, 200.0, 400.0));
    const QRectF flat = gs::app::fittedPicture(QRectF(0.0, 0.0, 400.0, 400.0), 2.0);
    CHECK(flat == QRectF(0.0, 100.0, 400.0, 200.0));
    const QRectF unknown = gs::app::fittedPicture(QRectF(0.0, 0.0, 400.0, 400.0), 0.0);
    CHECK(unknown.width() == doctest::Approx(400.0 * gs::app::kFallbackAspect));
    CHECK(unknown.center() == QPointF(200.0, 200.0));
    CHECK(gs::app::fittedPicture(QRectF(0.0, 0.0, 0.0, 400.0), 0.5).isEmpty());
}

TEST_CASE("tiling puts every mirror window on the work area without overlap and keeps each picture's shape (ADR-0096)")
{
    const QRect screen(0, 0, 1920, 1040);
    const std::vector<QRect> one = gs::app::tileWindows(screen, phones(1, kPortrait, QSizeF(0.0, 40.0)), 8);
    REQUIRE(one.size() == 1);
    CHECK(one[0] == QRect(735, 0, 450, 1040));

    std::vector<GridItem> four = phones(3, kPortrait, QSizeF(0.0, 40.0));
    four.push_back({.aspect = kLandscape, .chrome = QSizeF(0.0, 40.0)});
    checkTiles(screen, four);
    checkTiles(QRect(-1920, 20, 1920, 1040), phones(5, kLandscape, QSizeF(16.0, 48.0)));
    for (int count = 1; count <= 12; ++count) {
        CAPTURE(count);
        checkTiles(screen, phones(count, kPortrait, QSizeF(0.0, 40.0)));
        checkTiles(QRect(0, 0, 1280, 1400), mixed(count));
    }
    CHECK(gs::app::tileWindows(screen, {}, 8).empty());
}

TEST_CASE("cells come and go with the phones, and the input target goes away with its cell (ADR-0096)")
{
    GridCells cells;
    CHECK(cells.add(u"A"_s, u"Phone A"_s, kPortrait, false));
    CHECK(cells.add(u"B"_s, u"Phone B"_s, kPortrait, false));
    CHECK(cells.add(u"C"_s, u"Phone C"_s, kLandscape, false));
    CHECK_FALSE(cells.add(u"B"_s, u"Phone B again"_s, kPortrait, false));
    CHECK_FALSE(cells.add(QString(), u"Nameless"_s, kPortrait, false));
    cells.layout(QRectF(0.0, 40.0, 1600.0, 860.0), {});
    CHECK(cells.serials() == QStringList{u"A"_s, u"B"_s, u"C"_s});
    CHECK(cells.shape() == GridShape{.columns = 3, .rows = 1});
    for (const GridCells::Cell& cell : cells.cells()) {
        CHECK(cell.area.contains(cell.header));
        CHECK(cell.area.contains(cell.view));
        CHECK(cell.view.top() >= cell.header.bottom());
        CHECK(cell.area.top() >= 40.0);
    }

    CHECK(cells.setTarget(u"B"_s));
    CHECK_FALSE(cells.setTarget(u"B"_s));
    CHECK_FALSE(cells.setTarget(u"missing"_s));
    CHECK(cells.target() == u"B"_s);
    CHECK(cells.remove(u"B"_s));
    CHECK(cells.target().isEmpty());
    CHECK_FALSE(cells.remove(u"B"_s));

    CHECK(cells.add(u"D"_s, u"Phone D"_s, 0.0, false));
    cells.layout(QRectF(0.0, 40.0, 1600.0, 860.0), {});
    CHECK(cells.serials() == QStringList{u"A"_s, u"C"_s, u"D"_s});
    REQUIRE(cells.find(u"D"_s) != nullptr);
    CHECK(cells.find(u"D"_s)->aspect == 0.0);
    CHECK(cells.setAspect(u"D"_s, kPortrait));
    CHECK_FALSE(cells.setAspect(u"D"_s, kPortrait * 1.005));
    CHECK(cells.setAspect(u"D"_s, kLandscape));
    CHECK_FALSE(cells.setAspect(u"D"_s, 0.0));

    CHECK(cells.setLabel(u"A"_s, u"Phone A"_s, u"warn"_s, u"Reconnecting"_s));
    CHECK_FALSE(cells.setLabel(u"A"_s, u"Phone A"_s, u"warn"_s, u"Reconnecting"_s));
    CHECK(cells.find(u"A"_s)->status == u"Reconnecting"_s);

    CHECK(cells.setTarget(u"A"_s));
    CHECK(cells.setSeparate(u"A"_s, true));
    CHECK(cells.target().isEmpty());
    CHECK_FALSE(cells.setTarget(u"A"_s));
    CHECK(cells.setSeparate(u"A"_s, false));
    CHECK(cells.setTarget(u"A"_s));
    CHECK(cells.setTarget(QString()));
    CHECK(cells.target().isEmpty());
}

TEST_CASE("a click on another cell makes that phone the input target and lets go of what the last one held (ADR-0096)")
{
    const gs::input::LoadResult loaded = gs::input::loadProfile(kJump);
    REQUIRE(loaded.ok());
    gs::test::SinkLog sinkA;
    gs::test::SinkLog sinkB;
    const auto channelA = std::make_shared<gs::app::ControlChannel>();
    const auto channelB = std::make_shared<gs::app::ControlChannel>();
    channelA->attach(sinkA);
    channelB->attach(sinkB);
    channelA->setVideo(7, 1080, 2400);
    channelB->setVideo(3, 1080, 2400);
    const std::map<QString, std::shared_ptr<gs::app::ControlChannel>> channels{{u"A"_s, channelA},
                                                                                 {u"B"_s, channelB}};
    GridCells cells = laidOut({u"A"_s, u"B"_s});
    gs::app::InputHost host{{.pressedMouse = [] { return std::uint8_t{0}; }}};
    host.setProfile(std::make_shared<const gs::input::Profile>(loaded.profile), 0us);
    host.setActive(true, 0us);
    const auto click = [&](const QString& serial, std::int64_t timeMs) {
        const GridCells::Press press = cells.press(middleOf(cells, serial), Qt::LeftButton, timeMs);
        if (press.action == GridCells::Action::Select) {
            host.setChannel(channels.at(press.serial), std::chrono::milliseconds(timeMs));
        }
        return press.action;
    };

    CHECK(click(u"A"_s, 1000) == GridCells::Action::Select);
    CHECK(cells.target() == u"A"_s);
    host.key({.makeCode = 0x39}, 1100ms);
    REQUIRE(sinkA.frames.size() == 1);
    CHECK(sinkA.frames.back().points[0].action == gs::protocol::TouchAction::Down);
    CHECK(host.activeTouches() == 1);

    CHECK(click(u"B"_s, 2000) == GridCells::Action::Select);
    CHECK(cells.target() == u"B"_s);
    REQUIRE(sinkA.frames.size() == 2);
    CHECK(sinkA.frames.back().points[0].action == gs::protocol::TouchAction::Cancel);
    CHECK(host.activeTouches() == 0);
    host.key({.makeCode = 0x39, .released = true}, 2100ms);
    host.key({.makeCode = 0x39}, 2200ms);
    REQUIRE(sinkB.frames.size() == 1);
    CHECK(sinkB.frames.back().points[0].action == gs::protocol::TouchAction::Down);
    CHECK(sinkA.frames.size() == 2);

    CHECK(click(u"B"_s, 5000) == GridCells::Action::Forward);
    CHECK(cells.target() == u"B"_s);
}

TEST_CASE("a double click opens the phone's own window and another brings it back, single clicks only choose (ADR-0096)")
{
    GridCells cells = laidOut({u"A"_s, u"B"_s});
    const QPointF a = middleOf(cells, u"A"_s);

    CHECK(cells.press(a, Qt::LeftButton, 1000).action == GridCells::Action::Select);
    const GridCells::Press open = cells.press(a, Qt::LeftButton, 1200);
    CHECK(open.action == GridCells::Action::OwnWindow);
    CHECK(open.serial == u"A"_s);
    CHECK(cells.setSeparate(u"A"_s, true));
    CHECK(cells.target().isEmpty());

    CHECK(cells.press(a, Qt::LeftButton, 2000).action == GridCells::Action::None);
    CHECK(cells.target().isEmpty());
    const GridCells::Press back = cells.press(a, Qt::LeftButton, 2200);
    CHECK(back.action == GridCells::Action::BackToGrid);
    CHECK(back.serial == u"A"_s);
    CHECK(cells.setSeparate(u"A"_s, false));

    CHECK(cells.press(a, Qt::LeftButton, 3000).action == GridCells::Action::Select);
    CHECK(cells.press(a, Qt::LeftButton, 5000).action == GridCells::Action::Forward);
    CHECK(cells.press(a, Qt::LeftButton, 5100).action == GridCells::Action::Forward);

    const QPointF title = headerOf(cells, u"A"_s);
    CHECK(cells.press(title, Qt::LeftButton, 7000).action == GridCells::Action::None);
    CHECK(cells.press(title, Qt::LeftButton, 7100).action == GridCells::Action::OwnWindow);

    const QPointF b = middleOf(cells, u"B"_s);
    CHECK(cells.press(b, Qt::RightButton, 8000).action == GridCells::Action::Select);
    CHECK(cells.press(b, Qt::LeftButton, 8100).action == GridCells::Action::Forward);

    CHECK(cells.press(a, Qt::LeftButton, 9000).action == GridCells::Action::Select);
    CHECK(cells.press(a + QPointF(20.0, 0.0), Qt::LeftButton, 9100).action == GridCells::Action::Forward);

    CHECK(cells.press(QPointF(1.0, 1.0), Qt::LeftButton, 9500).action == GridCells::Action::Outside);
}

TEST_CASE("only the cell that has the keyboard and mouse shows key hints and the action wheel (ADR-0096, ADR-0094)")
{
    GridCells cells = laidOut({u"A"_s, u"B"_s, u"C"_s});
    CHECK(gs::app::gridHints(cells, true, false, true, u"A"_s) == GridHints{});

    REQUIRE(cells.setTarget(u"A"_s));
    CHECK(gs::app::gridHints(cells, true, false, true, u"A"_s) ==
          GridHints{.serial = u"A"_s, .keyHints = true, .wheel = true});
    CHECK(gs::app::gridHints(cells, true, false, true, QString()) ==
          GridHints{.serial = u"A"_s, .keyHints = true, .wheel = false});
    CHECK(gs::app::gridHints(cells, true, false, true, u"B"_s) ==
          GridHints{.serial = u"A"_s, .keyHints = true, .wheel = false});
    CHECK(gs::app::gridHints(cells, false, false, true, u"A"_s) ==
          GridHints{.serial = u"A"_s, .keyHints = false, .wheel = true});
    CHECK(gs::app::gridHints(cells, true, true, true, u"A"_s) ==
          GridHints{.serial = u"A"_s, .keyHints = false, .wheel = true});
    CHECK(gs::app::gridHints(cells, true, false, false, u"A"_s) ==
          GridHints{.serial = u"A"_s, .keyHints = true, .wheel = false});

    CHECK(cells.press(middleOf(cells, u"B"_s), Qt::LeftButton, 1000).action == GridCells::Action::Select);
    CHECK(gs::app::gridHints(cells, true, false, true, u"A"_s) ==
          GridHints{.serial = u"B"_s, .keyHints = true, .wheel = false});
    CHECK(gs::app::gridHints(cells, true, false, true, u"B"_s).wheel);

    CHECK(cells.setSeparate(u"B"_s, true));
    CHECK(gs::app::gridHints(cells, true, false, true, u"B"_s) == GridHints{});
    CHECK_FALSE(cells.setTarget(u"B"_s));
    CHECK(cells.setSeparate(u"B"_s, false));
    REQUIRE(cells.setTarget(u"C"_s));
    CHECK(gs::app::gridHints(cells, true, false, true, u"C"_s).serial == u"C"_s);
    CHECK(cells.remove(u"C"_s));
    CHECK(gs::app::gridHints(cells, true, false, true, u"C"_s) == GridHints{});
}

TEST_CASE("each --demo-device adds a demo device of its own, paired with a --demo-game (ADR-0096)")
{
    const std::vector<gs::app::DemoDevice> shared =
        gs::app::demoDevicesOf({u"a.media"_s, u"b.media"_s, u"c.media"_s}, {u"com.game.one"_s});
    REQUIRE(shared.size() == 3);
    CHECK(shared[0] == gs::app::DemoDevice{.recording = u"a.media"_s, .game = u"com.game.one"_s});
    CHECK(shared[2] == gs::app::DemoDevice{.recording = u"c.media"_s, .game = u"com.game.one"_s});

    const std::vector<gs::app::DemoDevice> paired =
        gs::app::demoDevicesOf({u"a.media"_s, u"b.media"_s}, {u"one"_s, u"two"_s, u"three"_s});
    REQUIRE(paired.size() == 2);
    CHECK(paired[1].game == u"two"_s);

    const std::vector<gs::app::DemoDevice> plain = gs::app::demoDevicesOf({u"a.media"_s}, {});
    REQUIRE(plain.size() == 1);
    CHECK(plain[0].game.isEmpty());
    CHECK(gs::app::demoDevicesOf({}, {u"one"_s}).empty());

    CHECK(gs::app::demoSerialOf(1) == u"gs-demo"_s);
    CHECK(gs::app::demoSerialOf(2) == u"gs-demo-2"_s);
    CHECK(gs::app::demoSerialOf(12) == u"gs-demo-12"_s);
}

TEST_CASE("ADR-0107: three portrait phones give a grid window wider than tall whose cells fit the pictures")
{
    GridCells cells = phonesIn({kPortrait, kPortrait, kPortrait});
    const QSize window = cells.windowSize(kScreen, kTop, kChrome, kHeightShare);
    CHECK(window.height() == 832);
    CHECK(window.width() > window.height());
    CHECK(window.width() < kScreen.width());
    checkPicturesFit(cells, window, kPortrait);
    CHECK(cells.shape() == GridShape{.columns = 3, .rows = 1});
}

TEST_CASE("ADR-0107: four landscape phones give a 2 x 2 grid window, one phone a window of its own shape")
{
    GridCells landscape = phonesIn({kLandscape, kLandscape, kLandscape, kLandscape});
    const QSize four = landscape.windowSize(kScreen, kTop, kChrome, kHeightShare);
    CHECK(four.height() == 832);
    CHECK(four.width() <= kScreen.width());
    checkPicturesFit(landscape, four, kLandscape);
    CHECK(landscape.shape() == GridShape{.columns = 2, .rows = 2});

    GridCells portrait = phonesIn({kPortrait});
    const QSize one = portrait.windowSize(kScreen, kTop, kChrome, kHeightShare);
    CHECK(one.height() == 832);
    CHECK(one.width() < one.height());
    checkPicturesFit(portrait, one, kPortrait);

    GridCells flat = phonesIn({kLandscape});
    const QSize wide = flat.windowSize(kScreen, kTop, kChrome, kHeightShare);
    CHECK(wide.height() == 832);
    CHECK(wide.width() > wide.height());
    checkPicturesFit(flat, wide, kLandscape);

    CHECK(phonesIn({0.0}).windowSize(kScreen, kTop, kChrome, kHeightShare) ==
          phonesIn({gs::app::kFallbackAspect}).windowSize(kScreen, kTop, kChrome, kHeightShare));
}

TEST_CASE("ADR-0107: on a small work area the grid window stays inside it and keeps the pictures' shape")
{
    GridCells cells = phonesIn({kLandscape, kLandscape});
    constexpr QSize area(900, 500);
    const QSize window = cells.windowSize(area, kTop, kChrome, kHeightShare);
    CHECK(window.width() == area.width());
    CHECK(window.height() < area.height() * kHeightShare);
    checkPicturesFit(cells, window, kLandscape);
    CHECK(cells.shape() == GridShape{.columns = 2, .rows = 1});

    CHECK(cells.windowSize(QSize(300, 100), kTop, kChrome, kHeightShare) == QSize(300, 100));
    CHECK(GridCells().windowSize(area, kTop, kChrome, kHeightShare).isEmpty());
    CHECK(gs::app::gridWindowSize(QSize(), phones(2, kPortrait), {.top = kTop}, kHeightShare).isEmpty());
}

TEST_CASE("ADR-0107: a grid cell keeps whole frames per second, its sync role, and counts the pictures of known shape")
{
    GridCells cells;
    REQUIRE(cells.add(u"A"_s, u"Phone A"_s, kPortrait, false));
    REQUIRE(cells.add(u"B"_s, u"Phone B"_s, 0.0, false));
    CHECK(cells.find(u"A"_s)->rate == -1.0);
    CHECK(cells.setRate(u"A"_s, 59.6));
    CHECK(cells.find(u"A"_s)->rate == 60.0);
    CHECK_FALSE(cells.setRate(u"A"_s, 60.3));
    CHECK(cells.setRate(u"A"_s, 58.2));
    CHECK(cells.find(u"A"_s)->rate == 58.0);
    CHECK(cells.setRate(u"A"_s, -1.0));
    CHECK(cells.find(u"A"_s)->rate == -1.0);
    CHECK_FALSE(cells.setRate(u"A"_s, std::nan("")));
    CHECK_FALSE(cells.setRate(u"missing"_s, 30.0));

    const GridCells::Sync leads{.role = u"master"_s, .tone = 1, .followers = 2};
    CHECK(cells.setSync(u"A"_s, leads));
    CHECK_FALSE(cells.setSync(u"A"_s, leads));
    CHECK(cells.find(u"A"_s)->sync == leads);
    CHECK(cells.setSync(u"B"_s, {.role = u"follower"_s, .tone = 1, .leader = u"Phone A"_s}));
    CHECK(cells.find(u"B"_s)->sync.leader == u"Phone A"_s);
    CHECK(cells.setSync(u"A"_s, {}));
    CHECK(cells.find(u"A"_s)->sync.role.isEmpty());
    CHECK_FALSE(cells.setSync(u"missing"_s, leads));

    CHECK(cells.knownAspects() == 1);
    CHECK(cells.setAspect(u"B"_s, kLandscape));
    CHECK(cells.knownAspects() == 2);
    CHECK(cells.setAspect(u"B"_s, kPortrait));
    CHECK(cells.knownAspects() == 2);
    CHECK(cells.remove(u"A"_s));
    CHECK(cells.knownAspects() == 1);
}

TEST_CASE("ADR-0107: a press on a cell's menu button opens its menu without choosing the cell or making a double click")
{
    GridCells cells = laidOut({u"A"_s, u"B"_s});
    const GridCells::Cell* a = cells.find(u"A"_s);
    REQUIRE(a != nullptr);
    CHECK(a->menu.width() == doctest::Approx(30.0));
    CHECK(a->menu.right() == doctest::Approx(a->area.right()));
    CHECK(a->header.contains(a->menu));
    const QPointF menu = a->menu.center();
    const QPointF picture = middleOf(cells, u"A"_s);

    const GridCells::Press opened = cells.press(menu, Qt::LeftButton, 1000);
    CHECK(opened.action == GridCells::Action::Menu);
    CHECK(opened.serial == u"A"_s);
    CHECK(cells.target().isEmpty());
    CHECK(cells.press(menu, Qt::LeftButton, 1100).action == GridCells::Action::Menu);

    CHECK(cells.press(picture, Qt::LeftButton, 2000).action == GridCells::Action::Select);
    CHECK(cells.press(menu, Qt::LeftButton, 2100).action == GridCells::Action::Menu);
    CHECK(cells.press(picture, Qt::LeftButton, 2200).action == GridCells::Action::Forward);
    CHECK(cells.target() == u"A"_s);

    REQUIRE(cells.setSeparate(u"B"_s, true));
    const GridCells::Cell* b = cells.find(u"B"_s);
    REQUIRE(b != nullptr);
    CHECK(cells.press(b->menu.center(), Qt::LeftButton, 3000).action == GridCells::Action::Menu);
    CHECK(cells.press(headerOf(cells, u"B"_s), Qt::LeftButton, 3100).action == GridCells::Action::None);
    CHECK(cells.target() == u"A"_s);
}
