#include "VisionSearch.h"
#include "VisionWorkbench.h"

#include <gs/script/Resources.h>

#include <doctest/doctest.h>

extern "C" {
#include <libavutil/frame.h>
}

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace gs;
using namespace gs::app;
using namespace std::chrono_literals;

namespace {

class Application
{
public:
    Application()
    {
        if (QCoreApplication::instance() == nullptr) {
            m_app = std::make_unique<QCoreApplication>(m_argc, m_argv.data());
        }
    }

private:
    int m_argc = 1;
    char m_name[5] = "test";
    std::array<char*, 2> m_argv{m_name, nullptr};
    std::unique_ptr<QCoreApplication> m_app;
};

bool waitFor(const std::function<bool()>& condition)
{
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

struct FrameDeleter {
    void operator()(AVFrame* frame) const noexcept { av_frame_free(&frame); }
};
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

FramePtr buttons()
{
    FramePtr frame(av_frame_alloc());
    REQUIRE(frame != nullptr);
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = 240;
    frame->height = 160;
    frame->colorspace = AVCOL_SPC_BT709;
    frame->color_range = AVCOL_RANGE_MPEG;
    REQUIRE(av_frame_get_buffer(frame.get(), 0) >= 0);
    std::uint32_t seed = 7;
    for (int y = 0; y < 160; ++y) {
        for (int x = 0; x < 240; ++x) {
            seed = seed * 1664525u + 1013904223u;
            frame->data[0][y * frame->linesize[0] + x] = static_cast<std::uint8_t>(20 + (seed >> 26));
        }
    }
    for (int y = 0; y < 80; ++y) {
        std::fill_n(frame->data[1] + y * frame->linesize[1], 120, std::uint8_t{128});
        std::fill_n(frame->data[2] + y * frame->linesize[2], 120, std::uint8_t{128});
    }
    const auto button = [&frame](int left, int top, std::uint8_t cb, std::uint8_t cr) {
        for (int y = 0; y < 24; ++y) {
            for (int x = 0; x < 48; ++x) {
                const bool mark = x >= 12 && x < 36 && y >= 6 && y < 18 && (x + y) % 5 != 0;
                frame->data[0][(top + y) * frame->linesize[0] + left + x] = mark ? 60 : 150;
            }
        }
        for (int y = top / 2; y < (top + 24) / 2; ++y) {
            for (int x = left / 2; x < (left + 48) / 2; ++x) {
                frame->data[1][y * frame->linesize[1] + x] = cb;
                frame->data[2][y * frame->linesize[2] + x] = cr;
            }
        }
    };
    button(48, 40, 90, 200);
    button(144, 96, 200, 90);
    for (int y = 140; y < 156; ++y) {
        std::fill_n(frame->data[0] + y * frame->linesize[0] + 8, 32, std::uint8_t{100});
    }
    return frame;
}

class Bench
{
public:
    explicit Bench(std::shared_ptr<media::FrameSnapshots> snapshots = nullptr) : m_snapshots(std::move(snapshots))
    {
        REQUIRE(m_dir.isValid());
        reopen();
    }

    ~Bench() { bench.reset(); }

    Bench(const Bench&) = delete;
    Bench& operator=(const Bench&) = delete;

    void reopen()
    {
        bench.reset();
        VisionSetup setup{.profileId = QStringLiteral("daily"),
                          .profileName = QStringLiteral("Daily"),
                          .folder = folder(),
                          .phone = QStringLiteral("demo")};
        setup.phones = [this] {
            return m_snapshots != nullptr ? std::vector<VisionPhone>{{QStringLiteral("demo"), QStringLiteral("Demo")}}
                                          : std::vector<VisionPhone>{};
        };
        setup.feedOf = [this](const QString&) {
            return VisionFeed{.frames = m_frames, .snapshots = m_snapshots, .feed = [this](bool on) { fed.push_back(on); }};
        };
        setup.unsaved = [this] { return unsaved; };
        bench = std::make_unique<VisionWorkbench>(std::move(setup));
        QObject::connect(bench.get(), &VisionWorkbench::notice,
                         [this](const QString& text, const QString&) { notices.append(text); });
    }

    [[nodiscard]] std::filesystem::path folder() const { return m_dir.path().toStdWString(); }
    [[nodiscard]] std::filesystem::path res() const { return folder() / "res"; }

    [[nodiscard]] script::Resources saved() const
    {
        const auto loaded = script::loadResources(res() / "resources.json");
        REQUIRE(loaded);
        return *loaded;
    }

    [[nodiscard]] bool has(const QString& file) const { return QFileInfo::exists(m_dir.filePath(QStringLiteral("res/") + file)); }

    void write(const QString& name, const QByteArray& contents) const
    {
        REQUIRE(QDir(m_dir.path()).mkpath(QFileInfo(m_dir.filePath(name)).path()));
        QFile file(m_dir.filePath(name));
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(file.write(contents) == contents.size());
    }

    [[nodiscard]] QByteArray read(const QString& name) const
    {
        QFile file(m_dir.filePath(name));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    std::unique_ptr<VisionWorkbench> bench;
    std::vector<bool> fed;
    QStringList notices;
    QStringList unsaved;

private:
    QTemporaryDir m_dir;
    std::shared_ptr<media::FrameSnapshots> m_snapshots;
    std::shared_ptr<render::FrameSlot> m_frames = std::make_shared<render::FrameSlot>();
};

}

TEST_CASE("vision workbench: new items get default names, and renaming, moving, deleting and undoing are saved (VIS-004)")
{
    const Application app;
    Bench rig;
    VisionWorkbench& bench = *rig.bench;
    CHECK(bench.saved());
    CHECK(bench.phones().isEmpty());
    CHECK(bench.phone().isEmpty());
    CHECK_FALSE(bench.canUndo());

    CHECK(bench.addRegion(0.5, 0.6, 0.5, 0.4) == QStringLiteral("Region 1"));
    CHECK(bench.selectedKind() == QStringLiteral("region"));
    CHECK(bench.selectedName() == QStringLiteral("Region 1"));
    CHECK(bench.addPoint(0.123456, 0.08) == QStringLiteral("Point 1"));
    CHECK(bench.addPoint(0.5, 0.5) == QStringLiteral("Point 2"));
    CHECK(bench.addPath({QVariantMap{{QStringLiteral("x"), 0.1}, {QStringLiteral("y"), 0.8}},
                         QVariantMap{{QStringLiteral("x"), 0.3}, {QStringLiteral("y"), 0.75}}})
          == QStringLiteral("Path 1"));
    CHECK(bench.addPath({QVariantMap{{QStringLiteral("x"), 0.1}, {QStringLiteral("y"), 0.8}}}).isEmpty());
    CHECK(bench.addRegion(0.2, 0.2, 0.0001, 0.3).isEmpty());
    CHECK(rig.notices.size() == 2);
    CHECK(bench.canUndo());
    CHECK(bench.saved());

    const script::Resources first = rig.saved();
    CHECK(first == bench.resources());
    REQUIRE(first.findPoint("Point 1") != nullptr);
    CHECK(first.findPoint("Point 1")->at == script::Point{0.1235, 0.08});
    REQUIRE(first.findPath("Path 1") != nullptr);
    CHECK(first.findPath("Path 1")->durationMs == script::kDefaultPathMs);

    bench.select(QStringLiteral("region"), QStringLiteral("Region 1"));
    CHECK(bench.rename(QStringLiteral("region"), QStringLiteral("Region 1"), QStringLiteral(" 右下角 ")).isEmpty());
    CHECK(bench.selectedName() == QStringLiteral("右下角"));
    CHECK_FALSE(bench.rename(QStringLiteral("point"), QStringLiteral("Point 1"), QStringLiteral("a/b")).isEmpty());
    CHECK_FALSE(bench.rename(QStringLiteral("point"), QStringLiteral("Point 1"), QStringLiteral("Point 2")).isEmpty());
    CHECK_FALSE(bench.rename(QStringLiteral("point"), QStringLiteral("Point 1"), QStringLiteral("x.png")).isEmpty());
    CHECK(bench.rename(QStringLiteral("point"), QStringLiteral("Point 1"), QStringLiteral("活动入口")).isEmpty());
    CHECK(bench.setArea(QStringLiteral("右下角"), 0.4, 0.5, 0.5, 0.4));
    CHECK(bench.movePoint(QStringLiteral("活动入口"), 0.2, 0.1));
    CHECK(bench.movePath(QStringLiteral("Path 1"), 0.8, -0.1));
    CHECK(bench.setValue(QStringLiteral("path"), QStringLiteral("Path 1"), QStringLiteral("durationMs"), 400));
    CHECK_FALSE(bench.setValue(QStringLiteral("path"), QStringLiteral("Path 1"), QStringLiteral("durationMs"), 0));
    CHECK_FALSE(bench.setValue(QStringLiteral("region"), QStringLiteral("右下角"), QStringLiteral("width"), 0.9));

    const script::Resources moved = rig.saved();
    CHECK(moved == bench.resources());
    REQUIRE(moved.findRegion("右下角") != nullptr);
    CHECK(moved.findRegion("右下角")->area == script::Region{0.4, 0.5, 0.5, 0.4});
    CHECK(moved.findPoint("活动入口")->at == script::Point{0.2, 0.1});
    REQUIRE(moved.findPath("Path 1") != nullptr);
    CHECK(moved.findPath("Path 1")->points == std::vector<script::Point>{{0.8, 0.7}, {1.0, 0.65}});
    CHECK(moved.findPath("Path 1")->durationMs == 400.0);
    CHECK(bench.regions().size() == 1);
    CHECK(bench.points().size() == 2);

    CHECK(bench.remove(QStringLiteral("point"), QStringLiteral("活动入口")));
    CHECK(rig.saved().findPoint("活动入口") == nullptr);
    bench.undo();
    CHECK(rig.saved().findPoint("活动入口") != nullptr);
    CHECK(bench.canRedo());
    bench.redo();
    CHECK(rig.saved().findPoint("活动入口") == nullptr);
    for (int i = 0; i < 7; ++i) {
        bench.undo();
    }
    CHECK(rig.saved().findPoint("Point 1") != nullptr);
    REQUIRE(rig.saved().findRegion("Region 1") != nullptr);
    CHECK(rig.saved().findRegion("Region 1")->area == script::Region{0.5, 0.6, 0.5, 0.4});
    CHECK(rig.saved().findRegion("右下角") == nullptr);

    bench.select(QStringLiteral("point"), QStringLiteral("Point 2"));
    CHECK(bench.selected().value(QStringLiteral("x")).toDouble() == 0.5);
    bench.select(QStringLiteral("point"), QStringLiteral("nothing"));
    CHECK(bench.selectedKind().isEmpty());
    CHECK(bench.saved());
    CHECK(bench.problem().isEmpty());
}

TEST_CASE("vision workbench: a frozen color picture gives templates at video size and picked colors, kept as files (VIS-004)")
{
    const Application app;
    const FramePtr picture = buttons();
    const auto snapshots = std::make_shared<media::FrameSnapshots>();
    snapshots->offer(*picture, 1000, 1);
    Bench rig(snapshots);
    VisionWorkbench& bench = *rig.bench;
    CHECK(bench.phone() == QStringLiteral("demo"));
    REQUIRE(bench.phones().size() == 1);

    CHECK(bench.addTemplate(0.2, 0.25, 0.2, 0.15).isEmpty());
    bench.setFrozen(true);
    REQUIRE(waitFor([&] { return bench.frozen(); }));
    CHECK(bench.pictureSize() == QSize(240, 160));
    CHECK_FALSE(bench.frozenKey().isEmpty());

    CHECK(bench.addTemplate(0.2, 0.25, 0.2, 0.15) == QStringLiteral("Template 1"));
    CHECK(rig.has(QStringLiteral("Template 1.png")));
    const QImage cut = QImage::fromData(rig.read(QStringLiteral("res/Template 1.png")));
    CHECK(cut.size() == QSize(48, 24));
    const script::Resources made = rig.saved();
    REQUIRE(made.templates.size() == 1);
    CHECK(made.templates.front().file == "Template 1.png");
    CHECK(made.templates.front().source == script::PictureSize{240, 160});
    CHECK(made.templates.front().match == script::MatchMode::Color);
    CHECK(made.templates.front().threshold == VisionWorkbench::kNewTemplateThreshold);
    REQUIRE(bench.templates().size() == 1);
    const QVariantMap listed = bench.templates().front().toMap();
    CHECK(listed.value(QStringLiteral("width")).toInt() == 48);
    CHECK(listed.value(QStringLiteral("cut")).toMap().value(QStringLiteral("x")).toDouble() == doctest::Approx(0.2));
    CHECK_FALSE(listed.value(QStringLiteral("picture")).toString().isEmpty());

    CHECK(bench.addTemplate(10.0 / 240, 142.0 / 160, 20.0 / 240, 10.0 / 160).isEmpty());
    CHECK(bench.templates().size() == 1);

    const std::shared_ptr<const media::LumaSnapshot> shot = snapshots->capture(true);
    REQUIRE(shot != nullptr);
    const std::vector<std::uint8_t> face = media::rgbOf(*shot, 150, 98, 1, 1);
    REQUIRE(face.size() == 3);
    CHECK(bench.pickColor(150.5 / 240, 98.5 / 160) == QStringLiteral("Color 1"));
    REQUIRE(rig.saved().findColor("Color 1") != nullptr);
    CHECK(rig.saved().findColor("Color 1")->color == script::Rgb{face[0], face[1], face[2]});

    bench.select(QStringLiteral("template"), QStringLiteral("Template 1"));
    bench.testFind();
    REQUIRE(waitFor([&] { return bench.result().contains(QStringLiteral("count")); }));
    CHECK(bench.result().value(QStringLiteral("count")).toInt() == 1);
    REQUIRE(bench.results().size() == 1);
    const QVariantMap hit = bench.results().front().toMap();
    CHECK(hit.value(QStringLiteral("x")).toDouble() == doctest::Approx(0.2));
    CHECK(hit.value(QStringLiteral("y")).toDouble() == doctest::Approx(0.25));
    CHECK(hit.value(QStringLiteral("centerX")).toInt() == 72);
    CHECK(hit.value(QStringLiteral("centerY")).toInt() == 52);
    CHECK(hit.value(QStringLiteral("score")).toDouble() > 0.99);

    bench.select(QStringLiteral("color"), QStringLiteral("Color 1"));
    CHECK(bench.results().isEmpty());
    CHECK(bench.setValue(QStringLiteral("color"), QStringLiteral("Color 1"), QStringLiteral("tolerance"), 0.05));
    bench.testFind();
    REQUIRE(waitFor([&] { return bench.result().contains(QStringLiteral("count")); }));
    REQUIRE(bench.results().size() == 1);
    const QVariantMap patch = bench.results().front().toMap();
    CHECK(patch.value(QStringLiteral("x")).toDouble() == doctest::Approx(0.6));
    CHECK(patch.value(QStringLiteral("y")).toDouble() == doctest::Approx(0.6));
    CHECK(patch.value(QStringLiteral("centerX")).toInt() == 168);

    CHECK(bench.rename(QStringLiteral("template"), QStringLiteral("Template 1"), QStringLiteral("开始按钮")).isEmpty());
    CHECK(rig.has(QStringLiteral("开始按钮.png")));
    CHECK_FALSE(rig.has(QStringLiteral("Template 1.png")));
    CHECK(rig.saved().findTemplate("开始按钮")->file == "开始按钮.png");
    CHECK(bench.remove(QStringLiteral("template"), QStringLiteral("开始按钮")));
    CHECK_FALSE(rig.has(QStringLiteral("开始按钮.png")));
    bench.undo();
    CHECK(rig.has(QStringLiteral("开始按钮.png")));
    CHECK(QImage::fromData(rig.read(QStringLiteral("res/开始按钮.png"))).size() == QSize(48, 24));

    bench.setFrozen(false);
    CHECK_FALSE(bench.frozen());
    CHECK(bench.pickColor(0.5, 0.5).isEmpty());

    rig.reopen();
    CHECK(rig.bench->templates().size() == 1);
    CHECK(rig.bench->imageOf(QStringLiteral("开始按钮")).size() > 0);
    CHECK(rig.bench->colors().size() == 1);
}

TEST_CASE("vision workbench: a broken res/resources.json is reported and never written over (VIS-004)")
{
    const Application app;
    Bench rig;
    rig.write(QStringLiteral("res/resources.json"), QByteArrayLiteral("{\"format\": \"gamescrcpy.resources\""));
    rig.reopen();
    VisionWorkbench& bench = *rig.bench;
    CHECK_FALSE(bench.saved());
    CHECK(bench.problem().contains(QStringLiteral("res/resources.json")));
    CHECK(bench.addPoint(0.5, 0.5) == QStringLiteral("Point 1"));
    CHECK_FALSE(bench.saved());
    CHECK(rig.read(QStringLiteral("res/resources.json")) == QByteArrayLiteral("{\"format\": \"gamescrcpy.resources\""));
}

TEST_CASE("vision workbench: what others write to res/resources.json while it is open is kept, shown and undone around (VIS-004)")
{
    const Application app;
    Bench rig;
    VisionWorkbench& bench = *rig.bench;
    CHECK(bench.addPoint(0.5, 0.5) == QStringLiteral("Point 1"));

    QImage shot(16, 12, QImage::Format_RGB32);
    shot.fill(Qt::darkCyan);
    for (int x = 0; x < 16; ++x) {
        shot.setPixelColor(x, x % 12, Qt::yellow);
    }
    QByteArray png;
    QBuffer buffer(&png);
    REQUIRE(buffer.open(QIODevice::WriteOnly));
    REQUIRE(shot.save(&buffer, "PNG"));
    rig.write(QStringLiteral("res/截图1.png"), png);
    script::Resources foreign = rig.saved();
    foreign.regions.push_back({"截图区", {0.1, 0.1, 0.2, 0.2}});
    foreign.templates.push_back({.name = "截图1",
                                 .file = "截图1.png",
                                 .threshold = 0.9,
                                 .match = script::MatchMode::Color,
                                 .region = "截图区",
                                 .source = {240, 160}});
    REQUIRE(!script::saveResources(rig.res() / "resources.json", foreign));

    CHECK(bench.addPoint(0.25, 0.25) == QStringLiteral("Point 2"));
    const script::Resources merged = rig.saved();
    CHECK(merged.findPoint("Point 1") != nullptr);
    CHECK(merged.findPoint("Point 2") != nullptr);
    REQUIRE(merged.findRegion("截图区") != nullptr);
    REQUIRE(merged.findTemplate("截图1") != nullptr);
    CHECK(merged.findTemplate("截图1")->region == "截图区");
    CHECK(rig.has(QStringLiteral("截图1.png")));
    CHECK(rig.read(QStringLiteral("res/截图1.png")) == png);
    CHECK(merged == bench.resources());
    CHECK(bench.regions().size() == 1);
    CHECK(bench.templates().size() == 1);
    CHECK(bench.imageOf(QStringLiteral("截图1")) == png);

    bench.undo();
    CHECK(rig.saved().findPoint("Point 2") == nullptr);
    CHECK(rig.saved().findRegion("截图区") != nullptr);
    CHECK(rig.saved().findTemplate("截图1") != nullptr);
    CHECK(rig.has(QStringLiteral("截图1.png")));
    bench.redo();
    CHECK(rig.saved().findPoint("Point 2") != nullptr);

    script::Resources later = rig.saved();
    later.points.push_back({"外来点", {0.9, 0.9}});
    later.regions.clear();
    later.templates.front().region.clear();
    REQUIRE(!script::saveResources(rig.res() / "resources.json", later));
    REQUIRE(waitFor([&] { return bench.points().size() == 3; }));
    CHECK(bench.regions().isEmpty());
    CHECK(bench.saved());
    CHECK(bench.resources().findPoint("外来点") != nullptr);
    CHECK(bench.remove(QStringLiteral("point"), QStringLiteral("Point 1")));
    const script::Resources after = rig.saved();
    CHECK(after.findPoint("Point 1") == nullptr);
    CHECK(after.findPoint("外来点") != nullptr);
    CHECK(after.findRegion("截图区") == nullptr);
}

TEST_CASE("vision workbench: merging keeps each side's changes per entry and drops region names that are gone (VIS-004)")
{
    script::Resources base;
    base.points = {{"a", {0.1, 0.1}}, {"b", {0.2, 0.2}}, {"c", {0.3, 0.3}}};
    base.regions = {{"zone", {0.0, 0.0, 0.5, 0.5}}};
    base.colors = {{"red", {255, 0, 0}, 0.1, "zone"}};
    script::Resources ours = base;
    ours.points[0].at = {0.15, 0.15};
    ours.points.erase(ours.points.begin() + 2);
    ours.points.push_back({"mine", {0.4, 0.4}});
    script::Resources theirs = base;
    theirs.points[1].at = {0.25, 0.25};
    theirs.points.push_back({"theirs", {0.6, 0.6}});
    theirs.regions.clear();

    const script::Resources merged = gs::app::mergeResources(base, ours, theirs);
    REQUIRE(merged.points.size() == 4);
    CHECK(merged.points[0].name == "a");
    CHECK(merged.points[0].at == script::Point{0.15, 0.15});
    CHECK(merged.points[1].name == "b");
    CHECK(merged.points[1].at == script::Point{0.25, 0.25});
    CHECK(merged.points[2].name == "mine");
    CHECK(merged.points[3].name == "theirs");
    CHECK(merged.findPoint("c") == nullptr);
    CHECK(merged.regions.empty());
    REQUIRE(merged.colors.size() == 1);
    CHECK(merged.colors.front().region.empty());
    CHECK(gs::app::mergeResources(base, ours, base) == ours);
}

TEST_CASE("vision search: a template cut at another video size is scaled by the long sides before matching (VIS-004)")
{
    QImage picture(100, 50, QImage::Format_RGB32);
    picture.fill(Qt::white);
    CHECK(scaledPicture(picture, {1080, 2400}, 540, 1200).size() == QSize(50, 25));
    CHECK(scaledPicture(picture, {1080, 2400}, 1200, 540).size() == QSize(50, 25));
    CHECK(scaledPicture(picture, {1080, 2400}, 1080, 2400).size() == QSize(100, 50));
    CHECK(scaledPicture(picture, {1080, 2400}, 1081, 2401).size() == QSize(100, 50));
    CHECK(scaledPicture(picture, {}, 540, 1200).size() == QSize(100, 50));
    CHECK(pixelArea({0.25, 0.5, 0.5, 0.5}, 240, 160) == vision::Rect{60, 80, 120, 80});
    CHECK(foundJson({60, 80, 120, 80}, 1.5, 240, 160) == R"({"x":0.5,"y":0.75,"width":0.5,"height":0.5,"score":1})");
}

TEST_CASE("vision workbench: picking for a blueprint pin hands back the new resource's name once (SCR-014)")
{
    const Application app;
    Bench rig;
    VisionWorkbench& bench = *rig.bench;
    QStringList names;
    int changes = 0;
    QObject::connect(&bench, &VisionWorkbench::pickChanged, [&changes] { ++changes; });
    const auto keep = [&names](const QString& name) { names.append(name); };
    CHECK(bench.pick().isEmpty());

    bench.startPick(QStringLiteral("point"), QStringLiteral("Click the picture"), keep);
    CHECK(bench.pick().value(QStringLiteral("kind")).toString() == QStringLiteral("point"));
    CHECK(bench.pick().value(QStringLiteral("prompt")).toString() == QStringLiteral("Click the picture"));
    CHECK(bench.addRegion(0.1, 0.1, 0.3, 0.3) == QStringLiteral("Region 1"));
    CHECK(names.isEmpty());
    CHECK(bench.addPoint(0.25, 0.5) == QStringLiteral("Point 1"));
    CHECK(names == QStringList{QStringLiteral("Point 1")});
    CHECK(bench.pick().isEmpty());
    CHECK(bench.addPoint(0.5, 0.5) == QStringLiteral("Point 2"));
    CHECK(names.size() == 1);
    CHECK(rig.saved().findPoint("Point 1") != nullptr);

    bench.startPick(QStringLiteral("region"), QStringLiteral("Drag a box"), keep);
    bench.cancelPick();
    CHECK(bench.pick().isEmpty());
    CHECK(bench.addRegion(0.5, 0.5, 0.2, 0.2) == QStringLiteral("Region 2"));
    CHECK(names.size() == 1);
    bench.cancelPick();
    CHECK(changes == 4);
}

TEST_CASE("vision workbench: asking for materials walks the AI's list, names what it gets and answers with every item "
          "(33b)")
{
    const Application app;
    const FramePtr picture = buttons();
    const auto snapshots = std::make_shared<media::FrameSnapshots>();
    snapshots->offer(*picture, 1000, 1);
    Bench rig(snapshots);
    VisionWorkbench& bench = *rig.bench;

    CHECK_FALSE(bench.requesting());
    CHECK(bench.requestCurrent() == -1);
    CHECK_FALSE(bench.requestAnswered());
    CHECK(bench.requestItems().isEmpty());

    std::vector<VisionRequestResult> answered;
    int moved = 0;
    QObject::connect(&bench, &VisionWorkbench::requestCurrentChanged, [&moved] { ++moved; });
    bench.beginRequest({{.name = QStringLiteral("start button"), .kind = QStringLiteral("template"),
                         .note = QStringLiteral("On the title screen")},
                        {.name = QStringLiteral("hp red"), .kind = QStringLiteral("color")},
                        {.name = QStringLiteral("minimap"), .kind = QStringLiteral("region")},
                        {.name = QStringLiteral("skill 1"), .kind = QStringLiteral("point")}},
                       [&answered](const std::vector<VisionRequestResult>& results) { answered = results; });
    CHECK(bench.requesting());
    CHECK(bench.requestCurrent() == 0);
    CHECK(moved == 1);
    CHECK_FALSE(bench.requestAnswered());
    CHECK(bench.requestDoneCount() == 0);
    REQUIRE(bench.requestItems().size() == 4);
    const QVariantMap first = bench.requestItems().front().toMap();
    CHECK(first.value(QStringLiteral("name")).toString() == QStringLiteral("start button"));
    CHECK(first.value(QStringLiteral("kind")).toString() == QStringLiteral("template"));
    CHECK(first.value(QStringLiteral("note")).toString() == QStringLiteral("On the title screen"));
    CHECK(first.value(QStringLiteral("state")).toString() == QStringLiteral("pending"));
    CHECK(first.value(QStringLiteral("current")).toBool());
    CHECK(first.value(QStringLiteral("existing")).toStringList().isEmpty());

    bench.setFrozen(true);
    REQUIRE(waitFor([&] { return bench.frozen(); }));

    CHECK(bench.addTemplate(0.2, 0.25, 0.2, 0.15) == QStringLiteral("start button"));
    CHECK(rig.has(QStringLiteral("start button.png")));
    CHECK(rig.saved().findTemplate("start button") != nullptr);
    CHECK(bench.requestCurrent() == 1);
    CHECK(bench.requestDoneCount() == 1);

    CHECK(bench.pickColor(0.6, 0.6) == QStringLiteral("hp red"));
    CHECK(rig.saved().findColor("hp red") != nullptr);
    CHECK(bench.requestCurrent() == 2);

    CHECK(bench.addRegion(0.02, 0.02, 0.2, 0.2) == QStringLiteral("minimap"));
    CHECK(bench.requestCurrent() == 3);
    CHECK(bench.existingOf(QStringLiteral("region")) == QStringList{QStringLiteral("minimap")});

    CHECK(bench.addPoint(0.5, 0.5) == QStringLiteral("skill 1"));
    CHECK(bench.requestCurrent() == -1);
    CHECK(bench.requestAnswered());
    CHECK(bench.requestDoneCount() == 4);
    CHECK(answered.empty());

    bench.giveRequest();
    CHECK_FALSE(bench.requesting());
    CHECK(bench.requestItems().isEmpty());
    REQUIRE(answered.size() == 4);
    CHECK(answered.at(0).name == QStringLiteral("start button"));
    CHECK(answered.at(0).state == QStringLiteral("done"));
    CHECK(answered.at(0).resource == QStringLiteral("start button"));
    CHECK(answered.at(0).value.value(QStringLiteral("file")).toString() == QStringLiteral("res/start button.png"));
    CHECK(answered.at(0).value.value(QStringLiteral("width")).toInt() == 48);
    CHECK(answered.at(1).value.value(QStringLiteral("color")).toString().startsWith(QStringLiteral("#")));
    CHECK(answered.at(2).value.value(QStringLiteral("width")).toDouble() == doctest::Approx(0.2));
    CHECK(answered.at(3).value.value(QStringLiteral("x")).toDouble() == doctest::Approx(0.5));
    CHECK(bench.templates().size() == 1);
}

TEST_CASE("vision workbench: a material request can be skipped, answered with an existing resource, or given up "
          "when the window closes (33b)")
{
    const Application app;
    Bench rig;
    VisionWorkbench& bench = *rig.bench;
    CHECK(bench.addRegion(0.1, 0.1, 0.2, 0.2) == QStringLiteral("Region 1"));
    CHECK(bench.addPoint(0.3, 0.3) == QStringLiteral("Point 1"));

    std::vector<VisionRequestResult> answered;
    const auto keep = [&answered](const std::vector<VisionRequestResult>& results) { answered = results; };
    bench.beginRequest({{.name = QStringLiteral("Button"), .kind = QStringLiteral("region")},
                        {.name = QStringLiteral("Here"), .kind = QStringLiteral("point")},
                        {.name = QStringLiteral("Bar"), .kind = QStringLiteral("region")}},
                       keep);
    CHECK(bench.requestCurrent() == 0);
    CHECK(bench.addRegion(0.5, 0.5, 0.1, 0.1) == QStringLiteral("Button"));
    CHECK(bench.requestCurrent() == 1);

    bench.useExistingRequest(1, QStringLiteral("Point 1"));
    CHECK(bench.requestCurrent() == 2);
    CHECK(bench.requestItems().at(1).toMap().value(QStringLiteral("state")).toString() == QStringLiteral("existing"));
    bench.useExistingRequest(2, QStringLiteral("Nobody"));
    CHECK(bench.requestItems().at(2).toMap().value(QStringLiteral("state")).toString() == QStringLiteral("pending"));

    bench.skipRequest(2);
    CHECK(bench.requestCurrent() == -1);
    CHECK(bench.requestAnswered());
    bench.giveRequest();
    REQUIRE(answered.size() == 3);
    CHECK(answered.at(0).resource == QStringLiteral("Button"));
    CHECK(answered.at(1).state == QStringLiteral("existing"));
    CHECK(answered.at(1).resource == QStringLiteral("Point 1"));
    CHECK(answered.at(1).value.value(QStringLiteral("x")).toDouble() == doctest::Approx(0.3));
    CHECK(answered.at(2).state == QStringLiteral("skipped"));
    CHECK(answered.at(2).resource.isEmpty());
    CHECK(answered.at(2).value.isEmpty());

    answered.clear();
    bench.beginRequest({{.name = QStringLiteral("Aim"), .kind = QStringLiteral("point")},
                        {.name = QStringLiteral("Fire"), .kind = QStringLiteral("point")}},
                       keep);
    CHECK(bench.addPoint(0.8, 0.8) == QStringLiteral("Aim"));
    CHECK(bench.requestCurrent() == 1);
    bench.abandonRequest();
    CHECK_FALSE(bench.requesting());
    REQUIRE(answered.size() == 2);
    CHECK(answered.at(0).state == QStringLiteral("done"));
    CHECK(answered.at(1).state == QStringLiteral("skipped"));

    answered.clear();
    bench.beginRequest({{.name = QStringLiteral("Map"), .kind = QStringLiteral("region")}}, keep);
    CHECK_FALSE(bench.requestAnswered());
    bench.giveRequest();
    CHECK(bench.requesting());
    CHECK(answered.empty());
    bench.abandonRequest();
}

TEST_CASE("SCR-011: renaming a resource lists the logic's literals; Change them too edits only them, and undo and redo "
          "bring back the name and the files together")
{
    const Application app;
    Bench rig;
    VisionWorkbench& bench = *rig.bench;
    int asks = 0;
    QObject::connect(&bench, &VisionWorkbench::renamingChanged, [&asks] { ++asks; });
    CHECK(bench.addPoint(0.5, 0.5) == QStringLiteral("Point 1"));
    const QByteArray logic = QByteArrayLiteral("// res.point(\"Point 1\") in a comment\r\n"
                                               "const door = res.point(\"Point 1\");\r\n"
                                               "const near = res.point('Point 1'), far = res.point(\"Point 10\");\r\n"
                                               "log.info(`Point 1`, \"Point 1 is the door\");\r\n");
    const QByteArray more = QByteArrayLiteral("export const spot = \"Point 1\";\n");
    rig.write(QStringLiteral("logic.ts"), logic);
    rig.write(QStringLiteral("lib/more.ts"), more);

    CHECK(bench.rename(QStringLiteral("point"), QStringLiteral("Point 1"), QStringLiteral(" Door ")).isEmpty());
    CHECK(asks == 1);
    CHECK(rig.saved().findPoint("Point 1") != nullptr);
    CHECK(rig.read(QStringLiteral("logic.ts")) == logic);
    const QVariantMap asked = bench.renaming();
    CHECK(asked.value(QStringLiteral("kind")).toString() == QStringLiteral("point"));
    CHECK(asked.value(QStringLiteral("name")).toString() == QStringLiteral("Point 1"));
    CHECK(asked.value(QStringLiteral("wanted")).toString() == QStringLiteral("Door"));
    CHECK(asked.value(QStringLiteral("count")).toInt() == 4);
    CHECK(asked.value(QStringLiteral("more")).toInt() == 0);
    CHECK(asked.value(QStringLiteral("unsaved")).toStringList().isEmpty());
    CHECK_FALSE(asked.value(QStringLiteral("shared")).toBool());
    const QVariantList places = asked.value(QStringLiteral("places")).toList();
    REQUIRE(places.size() == 4);
    CHECK(places[0].toMap().value(QStringLiteral("place")).toString() == QStringLiteral("logic.ts:2"));
    CHECK(places[0].toMap().value(QStringLiteral("text")).toString()
          == QStringLiteral("const door = res.point(\"Point 1\");"));
    CHECK(places[1].toMap().value(QStringLiteral("place")).toString() == QStringLiteral("logic.ts:3"));
    CHECK(places[1].toMap().value(QStringLiteral("column")).toInt() == 24);
    CHECK(places[2].toMap().value(QStringLiteral("place")).toString() == QStringLiteral("logic.ts:4"));
    CHECK(places[3].toMap().value(QStringLiteral("place")).toString() == QStringLiteral("lib/more.ts:1"));
    CHECK_FALSE(places[3].toMap().value(QStringLiteral("unsaved")).toBool());

    CHECK(bench.finishRename(true).isEmpty());
    CHECK(bench.renaming().isEmpty());
    CHECK(asks == 2);
    CHECK(rig.saved().findPoint("Door") != nullptr);
    CHECK(rig.saved().findPoint("Point 1") == nullptr);
    CHECK(bench.selectedName() == QStringLiteral("Door"));
    const QByteArray changed = QByteArrayLiteral("// res.point(\"Point 1\") in a comment\r\n"
                                                 "const door = res.point(\"Door\");\r\n"
                                                 "const near = res.point('Door'), far = res.point(\"Point 10\");\r\n"
                                                 "log.info(`Door`, \"Point 1 is the door\");\r\n");
    CHECK(rig.read(QStringLiteral("logic.ts")) == changed);
    CHECK(rig.read(QStringLiteral("lib/more.ts")) == QByteArrayLiteral("export const spot = \"Door\";\n"));

    bench.undo();
    CHECK(rig.saved().findPoint("Point 1") != nullptr);
    CHECK(rig.saved().findPoint("Door") == nullptr);
    CHECK(rig.read(QStringLiteral("logic.ts")) == logic);
    CHECK(rig.read(QStringLiteral("lib/more.ts")) == more);
    bench.redo();
    CHECK(rig.saved().findPoint("Door") != nullptr);
    CHECK(rig.read(QStringLiteral("logic.ts")) == changed);
    CHECK(rig.read(QStringLiteral("lib/more.ts")) == QByteArrayLiteral("export const spot = \"Door\";\n"));
    CHECK(rig.notices.isEmpty());

    CHECK(bench.rename(QStringLiteral("point"), QStringLiteral("Door"), QStringLiteral("Gate")).isEmpty());
    CHECK_FALSE(bench.renaming().isEmpty());
    bench.cancelRename();
    CHECK(bench.renaming().isEmpty());
    CHECK(bench.finishRename(true).isEmpty());
    CHECK(rig.saved().findPoint("Door") != nullptr);
    CHECK(rig.read(QStringLiteral("logic.ts")) == changed);

    CHECK(bench.rename(QStringLiteral("point"), QStringLiteral("Door"), QStringLiteral("Gate")).isEmpty());
    CHECK(bench.finishRename(false).isEmpty());
    CHECK(rig.saved().findPoint("Gate") != nullptr);
    CHECK(rig.read(QStringLiteral("logic.ts")) == changed);
    bench.undo();
    CHECK(rig.saved().findPoint("Door") != nullptr);
    CHECK(rig.read(QStringLiteral("logic.ts")) == changed);
    bench.undo();
    CHECK(rig.saved().findPoint("Point 1") != nullptr);
    CHECK(rig.read(QStringLiteral("logic.ts")) == logic);
}

TEST_CASE("SCR-011: a file with unsaved edits is left out of Change them too, and undo and redo leave alone a file "
          "that changed since")
{
    const Application app;
    Bench rig;
    VisionWorkbench& bench = *rig.bench;
    rig.unsaved = {QStringLiteral("lib/open.ts")};
    CHECK(bench.addRegion(0.1, 0.1, 0.3, 0.3) == QStringLiteral("Region 1"));
    const QByteArray logic = QByteArrayLiteral("const found = await vision.find(\"button\", { region: \"Region 1\" });\n");
    const QByteArray open = QByteArrayLiteral("export const zone = res.region(\"Region 1\");\n");
    rig.write(QStringLiteral("logic.ts"), logic);
    rig.write(QStringLiteral("lib/open.ts"), open);

    CHECK(bench.rename(QStringLiteral("region"), QStringLiteral("Region 1"), QStringLiteral("Top bar")).isEmpty());
    const QVariantMap asked = bench.renaming();
    CHECK(asked.value(QStringLiteral("count")).toInt() == 2);
    CHECK(asked.value(QStringLiteral("unsaved")).toStringList() == QStringList{QStringLiteral("lib/open.ts")});
    const QVariantList places = asked.value(QStringLiteral("places")).toList();
    REQUIRE(places.size() == 2);
    CHECK_FALSE(places[0].toMap().value(QStringLiteral("unsaved")).toBool());
    CHECK(places[1].toMap().value(QStringLiteral("unsaved")).toBool());

    CHECK(bench.finishRename(true).isEmpty());
    CHECK(rig.saved().findRegion("Top bar") != nullptr);
    const QByteArray changed = QByteArrayLiteral("const found = await vision.find(\"button\", { region: \"Top bar\" });\n");
    CHECK(rig.read(QStringLiteral("logic.ts")) == changed);
    CHECK(rig.read(QStringLiteral("lib/open.ts")) == open);

    const QByteArray edited = changed + QByteArrayLiteral("log.info(\"edited\");\n");
    rig.write(QStringLiteral("logic.ts"), edited);
    bench.undo();
    CHECK(rig.saved().findRegion("Region 1") != nullptr);
    CHECK(rig.saved().findRegion("Top bar") == nullptr);
    CHECK(rig.read(QStringLiteral("logic.ts")) == edited);
    REQUIRE(rig.notices.size() == 1);
    CHECK(rig.notices.back() == QStringLiteral("logic.ts changed since; its text was not changed back"));

    bench.redo();
    CHECK(rig.saved().findRegion("Top bar") != nullptr);
    CHECK(rig.read(QStringLiteral("logic.ts")) == edited);
    REQUIRE(rig.notices.size() == 2);
    CHECK(rig.notices.back() == QStringLiteral("logic.ts changed since; its text was not changed"));
}

TEST_CASE("SCR-011: a name used only in comments and other strings renames at once, and a long list shows the first "
          "200 places and counts the rest")
{
    const Application app;
    Bench rig;
    VisionWorkbench& bench = *rig.bench;
    int asks = 0;
    QObject::connect(&bench, &VisionWorkbench::renamingChanged, [&asks] { ++asks; });
    CHECK(bench.addPoint(0.5, 0.5) == QStringLiteral("Point 1"));
    rig.write(QStringLiteral("logic.ts"), QByteArrayLiteral("// tap \"Point 1\" later\nconst label = \"Point 1 \";\n"));
    CHECK(bench.rename(QStringLiteral("point"), QStringLiteral("Point 1"), QStringLiteral("Spot")).isEmpty());
    CHECK(asks == 0);
    CHECK(bench.renaming().isEmpty());
    CHECK(rig.saved().findPoint("Spot") != nullptr);

    QByteArray many;
    for (int i = 0; i < 205; ++i) {
        many += QByteArrayLiteral("touch.tap(res.point(\"Spot\"));\n");
    }
    rig.write(QStringLiteral("many.ts"), many);
    CHECK(bench.addRegion(0.1, 0.1, 0.3, 0.3) == QStringLiteral("Region 1"));
    CHECK(bench.rename(QStringLiteral("region"), QStringLiteral("Region 1"), QStringLiteral("Spot")).isEmpty());
    CHECK(asks == 0);
    CHECK(rig.saved().findRegion("Spot") != nullptr);

    CHECK(bench.rename(QStringLiteral("point"), QStringLiteral("Spot"), QStringLiteral("Spot 2")).isEmpty());
    const QVariantMap asked = bench.renaming();
    CHECK(asked.value(QStringLiteral("count")).toInt() == 205);
    CHECK(asked.value(QStringLiteral("places")).toList().size() == VisionWorkbench::kMostRenamePlaces);
    CHECK(asked.value(QStringLiteral("more")).toInt() == 5);
    CHECK(asked.value(QStringLiteral("shared")).toBool());
    CHECK(bench.finishRename(true).isEmpty());
    CHECK(rig.saved().findPoint("Spot 2") != nullptr);
    CHECK(rig.saved().findRegion("Spot") != nullptr);
    CHECK(rig.read(QStringLiteral("many.ts")).count(QByteArrayView("\"Spot 2\"")) == 205);
    CHECK(rig.read(QStringLiteral("logic.ts"))
          == QByteArrayLiteral("// tap \"Point 1\" later\nconst label = \"Point 1 \";\n"));
}
