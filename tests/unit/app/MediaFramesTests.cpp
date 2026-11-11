#include "AppSettings.h"
#include "MediaFrames.h"

#include "unit/ext/TestFiles.h"

#include <gs/exthost/InProcess.h>
#include <gs/media/FrameSnapshot.h>

extern "C" {
#include <libavutil/frame.h>
}

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <QCoreApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSize>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

using gs::app::FramePhone;
using gs::app::LogicCaller;
using gs::app::MediaFrames;
using gs::app::MediaReplies;
using gs::app::MediaSources;
using gs::app::FrameTapSource;
using gs::ext::test::Scratch;
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

struct FrameDeleter {
    void operator()(AVFrame* frame) const noexcept { av_frame_free(&frame); }
};
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

FramePtr greyPicture(int width, int height, std::uint8_t light)
{
    FramePtr frame(av_frame_alloc());
    REQUIRE(frame != nullptr);
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = width;
    frame->height = height;
    frame->colorspace = AVCOL_SPC_BT709;
    frame->color_range = AVCOL_RANGE_JPEG;
    REQUIRE(av_frame_get_buffer(frame.get(), 0) >= 0);
    for (int y = 0; y < height; ++y) {
        std::fill_n(frame->data[0] + y * frame->linesize[0], width, light);
    }
    for (int y = 0; y < (height + 1) / 2; ++y) {
        std::fill_n(frame->data[1] + y * frame->linesize[1], (width + 1) / 2, std::uint8_t{128});
        std::fill_n(frame->data[2] + y * frame->linesize[2], (width + 1) / 2, std::uint8_t{128});
    }
    return frame;
}

bool pumpUntil(const std::function<bool()>& done)
{
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(2ms);
    }
    return true;
}

void pumpFor(std::chrono::milliseconds span)
{
    const auto until = std::chrono::steady_clock::now() + span;
    while (std::chrono::steady_clock::now() < until) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(2ms);
    }
}

struct Told {
    std::uint64_t logic = 0;
    QString kind;
    QString id;
    QJsonObject value;
};

struct Kept {
    std::uint64_t logic = 0;
    QSize size;
    gs::script::PictureSize source;
    quint64 frames = 0;
    QRgb first = 0;
};

using Overlay = std::tuple<QString, QString, QVariantList>;

const LogicCaller kOwn{.logic = 1, .device = QStringLiteral("phone-1")};
const LogicCaller kReader{.logic = 2, .extension = QStringLiteral("com.example.reader"), .device = QStringLiteral("phone-1")};
const LogicCaller kAppScript{.logic = 3, .extension = QStringLiteral("com.example.reader")};

struct Fixture {
    Application app;
    Scratch scratch;
    gs::app::AppSettings settings{scratch / "data"};
    MediaFrames frames{nullptr};
    std::shared_ptr<gs::media::FrameSnapshots> shots = std::make_shared<gs::media::FrameSnapshots>(nullptr);
    std::vector<FramePhone> phones;
    std::vector<FrameTapSource> taps;
    std::vector<std::pair<quint64, QJsonObject>> answers;
    std::vector<std::pair<quint64, QString>> refusals;
    std::vector<Told> told;
    std::vector<Kept> kept;
    std::vector<std::pair<std::uint64_t, quint64>> forgotten;
    std::vector<Overlay> overlays;
    std::int64_t pts = 0;

    Fixture()
    {
        frames.setSettings(&settings);
        frames.setReplies(MediaReplies{
            .answer = [this](std::uint64_t, quint64 request,
                             const QString& json) { answers.emplace_back(request, QJsonDocument::fromJson(json.toUtf8()).object()); },
            .refuse = [this](std::uint64_t, quint64 request,
                             const QString& message) { refusals.emplace_back(request, message); },
            .tell = [this](std::uint64_t logic, const QString& kind, const QString& id, const QString& json) {
                told.push_back({logic, kind, id, QJsonDocument::fromJson(json.toUtf8()).object()});
            },
            .keep = [this](std::uint64_t logic, const QImage& picture, gs::script::PictureSize source, quint64 group) {
                kept.push_back({logic, picture.size(), source, group, picture.isNull() ? QRgb{0} : picture.pixel(0, 0)});
                return static_cast<std::uint64_t>(kept.size());
            },
            .forget = [this](std::uint64_t logic, quint64 group) { forgotten.emplace_back(logic, group); },
        });
        phones.push_back({.serial = QStringLiteral("phone-1"), .deviceKey = QStringLiteral("device-1"), .snapshots = shots});
        frames.setSources(MediaSources{
            .phones = [this] { return phones; },
            .taps = [this] { return taps; },
            .overlay = [this](const QString& serial, const QString& group,
                              const QVariantList& shapes) { overlays.emplace_back(serial, group, shapes); },
        });
    }

    void show(const std::shared_ptr<gs::media::FrameSnapshots>& target, int width, int height, std::uint8_t light)
    {
        const FramePtr frame = greyPicture(width, height, light);
        pts += 40'000;
        target->offer(*frame, pts, 1);
    }

    void show(int width, int height, std::uint8_t light) { show(shots, width, height, light); }

    QString start(const LogicCaller& caller, quint64 id, const QString& json)
    {
        const std::size_t before = answers.size();
        frames.request(caller, "media.frames", id, json);
        INFO((refusals.empty() ? QString().toStdString() : refusals.back().second.toStdString()));
        REQUIRE(answers.size() == before + 1);
        REQUIRE(answers.back().first == id);
        return answers.back().second.value(QStringLiteral("handle")).toString();
    }

    QString refusal(const LogicCaller& caller, quint64 id, const QString& json)
    {
        const std::size_t before = refusals.size();
        frames.request(caller, "media.frames", id, json);
        REQUIRE(refusals.size() == before + 1);
        REQUIRE(refusals.back().first == id);
        return refusals.back().second;
    }

    [[nodiscard]] std::vector<QJsonObject> framesTo(const QString& handle) const
    {
        std::vector<QJsonObject> found;
        for (const Told& each : told) {
            if (each.id == handle && each.value.value(QStringLiteral("type")).toString() == u"frame") {
                found.push_back(each.value);
            }
        }
        return found;
    }

    void done(const LogicCaller& caller, const QString& handle)
    {
        const gs::script::ServiceAnswer said =
            frames.call(caller, "media.done", QStringLiteral(R"({"handle":"%1"})").arg(handle).toStdString());
        CHECK(said.code.empty());
    }
};

std::optional<std::size_t> nodeIndex(const gs::exthost::Module& module, std::string_view name)
{
    const std::vector<gs::exthost::NodeInfo>& nodes = module.info().nodes;
    const auto found = std::ranges::find(nodes, name, &gs::exthost::NodeInfo::name);
    return found == nodes.end() ? std::nullopt
                                : std::optional<std::size_t>(static_cast<std::size_t>(found - nodes.begin()));
}

nlohmann::json tapStats(gs::exthost::Module& module)
{
    const std::optional<std::size_t> index = nodeIndex(module, "tapStats");
    REQUIRE(index.has_value());
    const gs::exthost::CallResult stats = module.call(*index, {});
    REQUIRE_MESSAGE(stats.ok(), stats.problem);
    return nlohmann::json::parse(stats.value.text);
}

}

TEST_CASE("ADR-0112: frame pixels are the picture's colors in blue, green, red and 255, averaged when made smaller "
          "and cut to a part")
{
    gs::media::LumaSnapshot shot;
    shot.width = 4;
    shot.height = 2;
    shot.luma = {10, 20, 30, 40, 50, 60, 70, 80};
    shot.chroma = std::vector<std::uint8_t>(4, 128);
    shot.matrix.rows = {{{1.0F, 0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F, 0.0F}}};
    const std::vector<std::uint8_t> half = gs::app::framePixels(shot, 0, 0, 4, 2, 2, 1);
    REQUIRE(half.size() == 8);
    CHECK(half == std::vector<std::uint8_t>{35, 35, 35, 255, 55, 55, 55, 255});
    const std::vector<std::uint8_t> cut = gs::app::framePixels(shot, 1, 0, 2, 1, 2, 1);
    CHECK(cut == std::vector<std::uint8_t>{20, 20, 20, 255, 30, 30, 30, 255});
    CHECK(gs::app::framePixels(shot, 3, 0, 2, 1, 2, 1).empty());
    CHECK(gs::app::framePixels(shot, 0, 0, 4, 2, 8, 2).empty());

    shot.chroma = {10, 200, 10, 200};
    shot.matrix.rows = {{{0.0F, 0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}}};
    const std::vector<std::uint8_t> colored = gs::app::framePixels(shot, 0, 0, 4, 2, 4, 2);
    REQUIRE(colored.size() == 32);
    CHECK(std::vector<std::uint8_t>(colored.begin(), colored.begin() + 4) == std::vector<std::uint8_t>{10, 0, 200, 255});
}

TEST_CASE("ADR-0112: a tap's shapes become drawings of the mirror window: rectangles, lines, points and texts in "
          "picture ratios and the colors logic draws with")
{
    const QString name = QStringLiteral("Detector");
    const QVariantMap box = gs::app::overlayShape(
        {.kind = gs::exthost::ShapeKind::Rect, .color = 1, .id = 7, .x = 0.6, .y = 0.5, .x2 = 0.2, .y2 = 0.1, .text = "car"},
        name);
    CHECK(box.value(QStringLiteral("kind")) == QStringLiteral("box"));
    CHECK(box.value(QStringLiteral("id")) == QStringLiteral("7"));
    CHECK(box.value(QStringLiteral("color")) == QStringLiteral("good"));
    CHECK(box.value(QStringLiteral("x")).toDouble() == doctest::Approx(0.2));
    CHECK(box.value(QStringLiteral("y")).toDouble() == doctest::Approx(0.1));
    CHECK(box.value(QStringLiteral("width")).toDouble() == doctest::Approx(0.4));
    CHECK(box.value(QStringLiteral("height")).toDouble() == doctest::Approx(0.4));
    CHECK(box.value(QStringLiteral("text")) == QStringLiteral("car"));
    CHECK(box.value(QStringLiteral("extension")) == name);

    const QVariantMap line = gs::app::overlayShape(
        {.kind = gs::exthost::ShapeKind::Line, .color = 9, .id = 1, .x = 0.5, .y = 0.5, .x2 = 0.25, .y2 = 0.75, .text = "x"},
        name);
    CHECK(line.value(QStringLiteral("kind")) == QStringLiteral("line"));
    CHECK(line.value(QStringLiteral("color")) == QStringLiteral("accent"));
    CHECK(line.value(QStringLiteral("width")).toDouble() == doctest::Approx(-0.25));
    CHECK(line.value(QStringLiteral("height")).toDouble() == doctest::Approx(0.25));
    CHECK(line.value(QStringLiteral("text")).toString().isEmpty());

    const QVariantMap point =
        gs::app::overlayShape({.kind = gs::exthost::ShapeKind::Point, .color = 3, .id = 2, .x = 0.3, .y = 0.4}, name);
    CHECK(point.value(QStringLiteral("kind")) == QStringLiteral("mark"));
    CHECK(point.value(QStringLiteral("color")) == QStringLiteral("bad"));
    const QVariantMap text = gs::app::overlayShape(
        {.kind = gs::exthost::ShapeKind::Text, .color = 4, .id = 3, .x = 5.0, .y = 0.0, .text = "HP 40"}, name);
    CHECK(text.value(QStringLiteral("kind")) == QStringLiteral("text"));
    CHECK(text.value(QStringLiteral("color")) == QStringLiteral("white"));
    CHECK(text.value(QStringLiteral("x")).toDouble() == doctest::Approx(2.0));
    CHECK(text.value(QStringLiteral("text")) == QStringLiteral("HP 40"));
}

TEST_CASE("ADR-0112: media.frames gives the logic the current frame and then new ones, cut to its region and made "
          "smaller to its size, one at a time and no faster than its fps")
{
    Fixture fixture;
    fixture.show(1280, 720, 200);
    const QString handle =
        fixture.start(kOwn, 1, QStringLiteral(R"({"fps":10,"size":320,"region":{"x":0.5,"y":0,"width":0.5,"height":1}})"));
    CHECK(handle == QStringLiteral("f1"));
    REQUIRE(pumpUntil([&fixture, &handle] { return fixture.framesTo(handle).size() == 1; }));
    const QJsonObject first = fixture.framesTo(handle).front();
    CHECK(first.value(QStringLiteral("time")).toDouble() == doctest::Approx(40.0));
    const QJsonObject image = first.value(QStringLiteral("image")).toObject();
    CHECK(image.value(QStringLiteral("capture")).toInt() == 1);
    CHECK(image.value(QStringLiteral("x")).toDouble() == doctest::Approx(0.5));
    CHECK(image.value(QStringLiteral("y")).toDouble() == doctest::Approx(0.0));
    CHECK(image.value(QStringLiteral("width")).toDouble() == doctest::Approx(0.5));
    CHECK(image.value(QStringLiteral("height")).toDouble() == doctest::Approx(1.0));
    CHECK(image.value(QStringLiteral("pixelWidth")).toInt() == 284);
    CHECK(image.value(QStringLiteral("pixelHeight")).toInt() == 320);
    REQUIRE(fixture.kept.size() == 1);
    CHECK(fixture.kept[0].logic == 1);
    CHECK(fixture.kept[0].size == QSize(284, 320));
    CHECK(fixture.kept[0].source == gs::script::PictureSize{569, 320});
    CHECK(fixture.kept[0].frames == 1);
    CHECK(qRed(fixture.kept[0].first) >= 198);
    CHECK(qRed(fixture.kept[0].first) <= 202);
    CHECK(qBlue(fixture.kept[0].first) == qRed(fixture.kept[0].first));

    for (int round = 0; round < 5; ++round) {
        fixture.show(1280, 720, 100);
        pumpFor(40ms);
    }
    CHECK(fixture.framesTo(handle).size() == 1);
    fixture.done(kOwn, handle);
    REQUIRE(pumpUntil([&fixture, &handle] { return fixture.framesTo(handle).size() == 2; }));
    CHECK(fixture.framesTo(handle).back().value(QStringLiteral("time")).toDouble() == doctest::Approx(240.0));
    fixture.done(kOwn, handle);
    pumpFor(300ms);
    CHECK(fixture.framesTo(handle).size() == 2);

    const QString slow = fixture.start(kOwn, 2, QStringLiteral(R"({"fps":2})"));
    std::size_t answered = 0;
    const auto started = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - started < 1300ms) {
        fixture.show(64, 36, 100);
        pumpFor(20ms);
        for (const std::size_t got = fixture.framesTo(slow).size(); answered < got; ++answered) {
            fixture.done(kOwn, slow);
        }
    }
    CHECK(answered >= 2);
    CHECK(answered <= 4);
    CHECK(fixture.framesTo(slow).back().value(QStringLiteral("image")).toObject().value(QStringLiteral("pixelWidth")).toInt() ==
          64);

    CHECK(fixture.frames.call(kOwn, "media.close", QStringLiteral(R"({"handle":"%1"})").arg(handle).toStdString())
              .code.empty());
    REQUIRE(pumpUntil([&fixture] {
        return std::ranges::find(fixture.forgotten, std::pair<std::uint64_t, quint64>{1, 1}) != fixture.forgotten.end();
    }));
    fixture.frames.endLogic(1);
    pumpFor(100ms);
    const std::size_t before = fixture.framesTo(handle).size() + fixture.framesTo(slow).size();
    for (int round = 0; round < 5; ++round) {
        fixture.show(64, 36, 100);
        pumpFor(40ms);
    }
    CHECK(fixture.framesTo(handle).size() + fixture.framesTo(slow).size() == before);
    CHECK(std::ranges::none_of(fixture.told, [&handle](const Told& each) {
        return each.id == handle && each.value.value(QStringLiteral("type")).toString() == u"closed";
    }));
}

TEST_CASE("ADR-0112: media.frames refuses app scripts, extensions without media.read, wrong options, phones that show "
          "no picture and a fifth subscription, and ends when the phone goes or the permission does")
{
    Fixture fixture;
    fixture.show(64, 36, 50);
    CHECK(fixture.refusal(kAppScript, 10, QStringLiteral("{}")).startsWith(u"GS-API-073 "));
    CHECK(fixture.refusal(kReader, 11, QStringLiteral("{}")).startsWith(u"GS-API-072 "));
    CHECK(fixture.frames.call(kReader, "media.done", R"({"handle":"f1"})").code.empty());
    fixture.frames.setPermitted({QStringLiteral("com.example.reader")});
    const QString first = fixture.start(kReader, 12, QStringLiteral("{}"));
    for (const QString& wrong :
         {QStringLiteral(R"({"fps":0})"), QStringLiteral(R"({"fps":11})"), QStringLiteral(R"({"size":63})"),
          QStringLiteral(R"({"size":1921})"), QStringLiteral(R"({"size":100.5})"),
          QStringLiteral(R"({"region":{"x":0.8,"y":0,"width":0.5,"height":1}})"),
          QStringLiteral(R"({"region":{"x":0,"y":0,"width":0,"height":1}})"), QStringLiteral(R"({"region":5})")}) {
        INFO(wrong.toStdString());
        CHECK(fixture.refusal(kReader, 13, wrong).startsWith(u"GS-API-120 "));
    }
    CHECK(fixture.refusal({.logic = 2, .extension = QStringLiteral("com.example.reader"), .device = QStringLiteral("phone-2")},
                          14, QStringLiteral("{}"))
              .startsWith(u"GS-API-121 "));
    fixture.start(kReader, 15, QStringLiteral("{}"));
    fixture.start(kReader, 16, QStringLiteral("{}"));
    const QString fourth = fixture.start(kReader, 17, QStringLiteral("{}"));
    CHECK(fixture.refusal(kReader, 18, QStringLiteral("{}")).startsWith(u"GS-API-122 "));
    CHECK(fixture.frames.call(kReader, "media.done", R"({"handle":"x"})").code == "GS-API-120");

    fixture.frames.setPermitted({});
    const auto closed = [&fixture](const QString& handle, const QString& reason) {
        return std::ranges::any_of(fixture.told, [&handle, &reason](const Told& each) {
            return each.id == handle && each.value.value(QStringLiteral("type")).toString() == u"closed" &&
                   each.value.value(QStringLiteral("reason")).toString() == reason;
        });
    };
    CHECK(closed(first, QStringLiteral("forbidden")));
    CHECK(closed(fourth, QStringLiteral("forbidden")));

    const QString own = fixture.start(kOwn, 19, QStringLiteral("{}"));
    fixture.phones.clear();
    fixture.frames.sync();
    CHECK(closed(own, QStringLiteral("ended")));
    CHECK(fixture.refusal(kOwn, 20, QStringLiteral("{}")).startsWith(u"GS-API-121 "));
}

TEST_CASE("ADR-0112: a frame tap runs only on the phones where the user turned it on, gets frames at its size, and "
          "its overlay is drawn over that phone and taken away when it is turned off")
{
    Fixture fixture;
    std::string problem;
    std::unique_ptr<gs::exthost::InProcessModule> loaded = gs::exthost::InProcessModule::load(
        {.dll = std::filesystem::path(GS_TEST_NATIVE_DLL), .store = fixture.scratch / "store.json"}, problem);
    REQUIRE_MESSAGE(loaded != nullptr, problem);
    const std::shared_ptr<gs::exthost::Module> module(loaded.release());
    const std::optional<std::size_t> reset = nodeIndex(*module, "tapReset");
    REQUIRE(reset.has_value());
    REQUIRE(module->call(*reset, {}).ok());

    const auto other = std::make_shared<gs::media::FrameSnapshots>(nullptr);
    fixture.phones.push_back({.serial = QStringLiteral("phone-2"), .deviceKey = QStringLiteral("device-2"), .snapshots = other});
    fixture.taps.push_back({.extension = "gstest.native",
                            .extensionName = QStringLiteral("Test native"),
                            .name = "counter",
                            .title = QStringLiteral("Counter"),
                            .fps = 30,
                            .size = 64,
                            .module = module,
                            .identity = module.get(),
                            .index = 0});
    fixture.frames.sync();
    const QString key = QStringLiteral("gstest.native/counter");
    QVariantList listed = fixture.frames.taps();
    REQUIRE(listed.size() == 2);
    const QVariantMap entry = listed[0].toMap();
    CHECK(entry.value(QStringLiteral("device")) == QStringLiteral("phone-1"));
    CHECK(entry.value(QStringLiteral("key")) == key);
    CHECK(entry.value(QStringLiteral("title")) == QStringLiteral("Counter"));
    CHECK(entry.value(QStringLiteral("extensionName")) == QStringLiteral("Test native"));
    CHECK(entry.value(QStringLiteral("on")) == false);

    fixture.show(128, 72, 90);
    pumpFor(200ms);
    CHECK(tapStats(*module)["frames"] == 0);

    CHECK(fixture.frames.setTapOn(QStringLiteral("phone-1"), key, true));
    CHECK(fixture.settings.device("device-1").frameTaps == std::vector<std::string>{"gstest.native/counter"});
    CHECK(fixture.settings.device("device-2").frameTaps.empty());
    listed = fixture.frames.taps();
    CHECK(listed[0].toMap().value(QStringLiteral("on")) == true);
    CHECK(listed[1].toMap().value(QStringLiteral("on")) == false);
    fixture.show(128, 72, 90);
    fixture.show(other, 100, 100, 30);
    REQUIRE(pumpUntil([&module] { return tapStats(*module)["frames"].get<int>() >= 1; }));
    nlohmann::json stats = tapStats(*module);
    CHECK(stats["opened"] == 1);
    CHECK(stats["device"] == "phone-1");
    CHECK(stats["width"] == 64);
    CHECK(stats["height"] == 36);
    CHECK(stats["frameWidth"] == 64);
    CHECK(stats["frameHeight"] == 36);
    CHECK(stats["stride"] == 256);
    CHECK(stats["first"][0].get<int>() >= 88);
    CHECK(stats["first"][0].get<int>() <= 92);
    CHECK(stats["first"][3] == 255);

    REQUIRE(pumpUntil([&fixture] { return !fixture.overlays.empty(); }));
    {
        const auto& [serial, group, shapes] = fixture.overlays.back();
        CHECK(serial == QStringLiteral("phone-1"));
        CHECK(group == QStringLiteral("tap:gstest.native/counter"));
        REQUIRE(shapes.size() == 1);
        const QVariantMap box = shapes[0].toMap();
        CHECK(box.value(QStringLiteral("kind")) == QStringLiteral("box"));
        CHECK(box.value(QStringLiteral("color")) == QStringLiteral("good"));
        CHECK(box.value(QStringLiteral("id")) == QStringLiteral("1"));
        CHECK(box.value(QStringLiteral("x")).toDouble() == doctest::Approx(0.1));
        CHECK(box.value(QStringLiteral("width")).toDouble() == doctest::Approx(0.2));
        CHECK(box.value(QStringLiteral("text")) == QStringLiteral("box"));
        CHECK(box.value(QStringLiteral("extension")) == QStringLiteral("Test native"));
    }

    for (int round = 0; round < 3; ++round) {
        fixture.show(other, 100, 100, 30);
        pumpFor(40ms);
    }
    CHECK(tapStats(*module)["opened"] == 1);

    CHECK(fixture.frames.setTapOn(QStringLiteral("phone-1"), key, false));
    {
        const auto& [serial, group, shapes] = fixture.overlays.back();
        CHECK(serial == QStringLiteral("phone-1"));
        CHECK(group == QStringLiteral("tap:gstest.native/counter"));
        CHECK(shapes.isEmpty());
    }
    REQUIRE(pumpUntil([&module] { return tapStats(*module)["closed"].get<int>() == 1; }));
    CHECK(fixture.settings.device("device-1").frameTaps.empty());
    const int seen = tapStats(*module)["frames"].get<int>();
    fixture.show(128, 72, 90);
    pumpFor(200ms);
    CHECK(tapStats(*module)["frames"] == seen);

    CHECK_FALSE(fixture.frames.setTapOn(QStringLiteral("phone-9"), key, true));
    CHECK_FALSE(fixture.frames.setTapOn(QStringLiteral("phone-1"), QStringLiteral("gstest.native/nothing"), true));
    fixture.taps.clear();
    fixture.frames.sync();
    CHECK(fixture.frames.taps().isEmpty());
}
