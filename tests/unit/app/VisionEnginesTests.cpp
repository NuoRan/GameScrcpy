#include "NativeExtensions.h"
#include "VisionEngines.h"

#include "unit/ext/TestFiles.h"

#include <gs/media/FrameSnapshot.h>
#include <gs/platform/TextReader.h>

#include <windows.h>

#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using gs::app::LogicCaller;
using gs::app::LogicServices;
using gs::app::VisionAsk;
using gs::app::VisionEngine;
using gs::app::VisionEngineInfo;
using gs::app::VisionEngines;
using gs::app::VisionFrames;
using gs::app::VisionPicture;
using gs::app::VisionReplies;
using gs::app::VisionReply;
namespace fs = std::filesystem;

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

bool pumpUntil(const std::function<bool()>& done)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

std::shared_ptr<gs::media::LumaSnapshot> blankShot(int width, int height)
{
    auto shot = std::make_shared<gs::media::LumaSnapshot>();
    shot->frame = 1;
    shot->width = width;
    shot->height = height;
    shot->luma.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), std::uint8_t{0});
    shot->chroma.assign(
        std::size_t{2} * static_cast<std::size_t>(shot->chromaWidth()) * static_cast<std::size_t>(shot->chromaHeight()),
        std::uint8_t{128});
    return shot;
}

std::shared_ptr<const gs::media::LumaSnapshot> solidShot(int width, int height, std::uint8_t luma, std::uint8_t blue,
                                                         std::uint8_t red)
{
    std::shared_ptr<gs::media::LumaSnapshot> shot = blankShot(width, height);
    std::ranges::fill(shot->luma, luma);
    for (std::size_t at = 0; at + 1 < shot->chroma.size(); at += 2) {
        shot->chroma[at] = blue;
        shot->chroma[at + 1] = red;
    }
    shot->matrix.rows = {{{0.0F, 0.0F, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}}};
    return shot;
}

std::shared_ptr<const gs::media::LumaSnapshot> textShot(int width, int height, std::wstring_view text)
{
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
    RECT whole{0, 0, width, height};
    FillRect(dc, &whole, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    HFONT font = CreateFontW(-28, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ previousFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    RECT place{40, 40, width, height};
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &place, DT_LEFT | DT_TOP | DT_NOPREFIX);
    GdiFlush();
    std::shared_ptr<gs::media::LumaSnapshot> shot = blankShot(width, height);
    const auto* pixels = static_cast<const std::uint8_t*>(bits);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* pixel = pixels + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                                  static_cast<std::size_t>(x)) *
                                                     4;
            shot->luma[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                static_cast<std::uint8_t>((pixel[0] + pixel[1] + pixel[2]) / 3);
        }
    }
    SelectObject(dc, previousFont);
    DeleteObject(font);
    SelectObject(dc, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return shot;
}

struct FakeState {
    std::mutex mutex;
    std::vector<std::string> params;
    std::vector<gs::exthost::VisionKind> kinds;
    std::string reply = R"([{"label":"boss","x":0.5,"y":0.5,"width":0.5,"height":0.5,"confidence":0.9}])";
    QString problem;
};

class FakeEngine final : public VisionEngine
{
public:
    FakeEngine(std::shared_ptr<FakeState> state, VisionEngineInfo info)
        : m_state(std::move(state))
        , m_info(std::move(info))
    {
    }

    VisionEngineInfo info() const override { return m_info; }

    VisionReply recognize(const VisionPicture& picture, const VisionAsk& ask) override
    {
        const std::scoped_lock lock(m_state->mutex);
        m_state->params.push_back(ask.params);
        m_state->kinds.push_back(ask.kind);
        return {.json = m_state->problem.isEmpty() ? m_state->reply : std::string(),
                .area = picture.region,
                .problem = m_state->problem};
    }

private:
    std::shared_ptr<FakeState> m_state;
    VisionEngineInfo m_info;
};

const LogicCaller kPhone{.logic = 1, .extension = {}, .script = {}, .device = QStringLiteral("phone-1"), .task = false};
const LogicCaller kOther{.logic = 2, .extension = {}, .script = {}, .device = QStringLiteral("phone-2"), .task = false};
const LogicCaller kHelper{
    .logic = 3, .extension = QStringLiteral("alice.helper"), .script = {}, .device = QStringLiteral("phone-1"), .task = false};
const LogicCaller kNoPhone{.logic = 4, .extension = {}, .script = {}, .device = {}, .task = false};

VisionEngineInfo bossInfo()
{
    return {.id = QStringLiteral("alice.finder/boss"),
            .title = QStringLiteral("Boss finder"),
            .kinds = {QStringLiteral("detect"), QStringLiteral("classify")},
            .languages = {}};
}

struct Fixture {
    Application app;
    VisionEngines engines{nullptr};
    std::shared_ptr<FakeState> fake = std::make_shared<FakeState>();
    std::vector<std::pair<quint64, QString>> answers;
    std::vector<std::pair<quint64, QString>> refusals;
    std::vector<std::pair<quint64, bool>> grabs;
    std::vector<quint64> ungrabs;
    std::shared_ptr<const gs::media::LumaSnapshot> shot = solidShot(200, 100, 20, 10, 30);
    bool holding = false;
    bool grabbing = true;
    std::vector<LogicServices::FrameTaken> held;

    Fixture()
    {
        engines.setReplies(VisionReplies{
            .answer = [this](std::uint64_t, quint64 request, const QString& json) { answers.emplace_back(request, json); },
            .refuse = [this](std::uint64_t, quint64 request,
                             const QString& message) { refusals.emplace_back(request, message); },
        });
        engines.setFrames(VisionFrames{
            .grab =
                [this](const LogicCaller&, quint64 request, bool color, LogicServices::FrameTaken taken) {
                    grabs.emplace_back(request, color);
                    if (!grabbing) {
                        return false;
                    }
                    if (holding) {
                        held.push_back(std::move(taken));
                    } else {
                        taken(shot);
                    }
                    return true;
                },
            .ungrab = [this](const LogicCaller&, quint64 request) { ungrabs.push_back(request); },
        });
        engines.addEngine(std::make_shared<FakeEngine>(fake, bossInfo()));
    }

    void ask(const LogicCaller& caller, quint64 id, const QString& json)
    {
        engines.request(caller, "vision.recognize", id, json);
    }

    QString answerTo(quint64 request) const
    {
        const auto found = std::ranges::find(answers, request, &std::pair<quint64, QString>::first);
        return found == answers.end() ? QString() : found->second;
    }

    QString refusalOf(quint64 request) const
    {
        const auto found = std::ranges::find(refusals, request, &std::pair<quint64, QString>::first);
        return found == refusals.end() ? QString() : found->second;
    }

    bool settled(quint64 request) const { return !answerTo(request).isEmpty() || !refusalOf(request).isEmpty(); }

    QJsonArray resultOf(quint64 request)
    {
        REQUIRE(pumpUntil([this, request] { return settled(request); }));
        INFO(refusalOf(request).toStdString());
        REQUIRE(!answerTo(request).isEmpty());
        return QJsonDocument::fromJson(answerTo(request).toUtf8()).array();
    }

    QString problemOf(quint64 request)
    {
        REQUIRE(pumpUntil([this, request] { return settled(request); }));
        return refusalOf(request);
    }
};

}

TEST_CASE("ADR-0112: the registry lists Windows first and the engines of extensions, with their kinds and languages")
{
    Fixture fixture;
    fixture.engines.addEngine(std::make_shared<gs::app::SystemVisionEngine>());
    const std::vector<VisionEngineInfo> listed = fixture.engines.engines();
    REQUIRE(listed.size() == 2);
    CHECK(listed[0].id == u"system");
    CHECK(listed[0].title == u"Windows");
    CHECK(listed[0].kinds == QStringList{QStringLiteral("ocr")});
    CHECK(listed[1] == bossInfo());
    const gs::script::ServiceAnswer answer = fixture.engines.call(kPhone, "vision.engines", "{}");
    REQUIRE(answer.code.empty());
    const QJsonArray json = QJsonDocument::fromJson(QByteArray::fromStdString(answer.json)).array();
    REQUIRE(json.size() == 2);
    CHECK(json[0].toObject().value(u"id").toString() == u"system");
    CHECK(json[0].toObject().value(u"kinds").toArray() == QJsonArray{QStringLiteral("ocr")});
    CHECK(json[1].toObject().value(u"title").toString() == u"Boss finder");
    CHECK(json[1].toObject().value(u"kinds").toArray() == QJsonArray{QStringLiteral("detect"), QStringLiteral("classify")});
    CHECK(json[1].toObject().value(u"languages").toArray().isEmpty());
    CHECK(fixture.engines.call(kPhone, "vision.nothing", "{}").code == "GS-API-070");

    fixture.engines.removeEngine(QStringLiteral("alice.finder/boss"));
    REQUIRE(fixture.engines.engines().size() == 1);
    CHECK(fixture.engines.engines()[0].id == u"system");
}

TEST_CASE("ADR-0112: vision.recognize takes a frame, runs the engine with the params and maps what it found from the "
          "region to the whole picture")
{
    Fixture fixture;
    fixture.ask(kPhone, 10,
                QStringLiteral(R"({"engine":"alice.finder/boss","kind":"detect",)"
                               R"("region":{"x":0.5,"y":0,"width":0.5,"height":1},"params":{"threshold":0.5}})"));
    const QJsonArray found = fixture.resultOf(10);
    REQUIRE(found.size() == 1);
    const QJsonObject boss = found[0].toObject();
    CHECK(boss.value(u"label").toString() == u"boss");
    CHECK(boss.value(u"x").toDouble() == doctest::Approx(0.75));
    CHECK(boss.value(u"y").toDouble() == doctest::Approx(0.5));
    CHECK(boss.value(u"width").toDouble() == doctest::Approx(0.25));
    CHECK(boss.value(u"height").toDouble() == doctest::Approx(0.5));
    CHECK(boss.value(u"confidence").toDouble() == doctest::Approx(0.9));
    CHECK(fixture.grabs == std::vector<std::pair<quint64, bool>>{{10, true}});
    {
        const std::scoped_lock lock(fixture.fake->mutex);
        CHECK(fixture.fake->params == std::vector<std::string>{R"({"threshold":0.5})"});
        CHECK(fixture.fake->kinds == std::vector<gs::exthost::VisionKind>{gs::exthost::VisionKind::Detect});
        fixture.fake->reply = R"([{"label":"night","confidence":2,"x":"ignored"},{"label":"day","confidence":-1}])";
    }
    fixture.ask(kOther, 11, QStringLiteral(R"({"engine":"alice.finder/boss","kind":"classify"})"));
    const QJsonArray classes = fixture.resultOf(11);
    REQUIRE(classes.size() == 2);
    CHECK(classes[0].toObject() == QJsonObject{{QStringLiteral("label"), QStringLiteral("night")},
                                              {QStringLiteral("confidence"), 1.0}});
    CHECK(classes[1].toObject().value(u"confidence").toDouble() == doctest::Approx(0.0));
    {
        const std::scoped_lock lock(fixture.fake->mutex);
        CHECK(fixture.fake->params.back() == "{}");
    }

    const std::vector<std::pair<std::string, QString>> broken{
        {R"({"label":"boss"})", QStringLiteral("GS-API-173 the engine's result is not valid: it is not a list")},
        {R"([{"label":"boss","x":0.5,"y":0.5,"width":0.5,"height":0.5}])",
         QStringLiteral("GS-API-173 the engine's result is not valid: entry 1 has no number confidence")},
        {R"([{"label":"boss","x":0.5,"width":0.5,"height":0.5,"confidence":1}])",
         QStringLiteral("GS-API-173 the engine's result is not valid: entry 1 has no number y")},
        {R"([{"x":0.5,"y":0.5,"width":0.5,"height":0.5,"confidence":1}])",
         QStringLiteral("GS-API-173 the engine's result is not valid: entry 1 has no label of at most 256 bytes")},
        {R"([1])", QStringLiteral("GS-API-173 the engine's result is not valid: entry 1 is not an object")},
    };
    quint64 id = 20;
    for (const auto& [reply, problem] : broken) {
        {
            const std::scoped_lock lock(fixture.fake->mutex);
            fixture.fake->reply = reply;
        }
        fixture.ask(kPhone, ++id, QStringLiteral(R"({"engine":"alice.finder/boss","kind":"detect"})"));
        CHECK(fixture.problemOf(id) == problem);
    }
    {
        const std::scoped_lock lock(fixture.fake->mutex);
        fixture.fake->problem = QStringLiteral("GS-API-174 the engine took longer than 5000 ms");
    }
    fixture.ask(kPhone, 30, QStringLiteral(R"({"engine":"alice.finder/boss","kind":"detect"})"));
    CHECK(fixture.problemOf(30) == u"GS-API-174 the engine took longer than 5000 ms");
    CHECK(fixture.engines.pending() == 0);
}

TEST_CASE("ADR-0112: vision.recognize refuses an unknown engine, a kind it does not offer, bad arguments, logic "
          "without a phone and an extension without device.screen, and gives null without a picture")
{
    Fixture fixture;
    const auto refused = [&fixture](const LogicCaller& caller, quint64 id, const QString& json) {
        fixture.ask(caller, id, json);
        return fixture.refusalOf(id);
    };
    CHECK(refused(kPhone, 1, QStringLiteral(R"({"engine":"nobody/none","kind":"ocr"})")).startsWith(u"GS-API-170 "));
    CHECK(refused(kPhone, 2, QStringLiteral(R"({"engine":"alice.finder/boss","kind":"ocr"})"))
              .startsWith(u"GS-API-171 "));
    CHECK(refused(kPhone, 3, QStringLiteral(R"({"engine":"alice.finder/boss","kind":"faces"})"))
              .startsWith(u"GS-API-172 "));
    CHECK(refused(kPhone, 4, QStringLiteral(R"({"engine":"","kind":"detect"})")).startsWith(u"GS-API-172 "));
    CHECK(refused(kPhone, 5, QStringLiteral(R"({"engine":"alice.finder/boss","kind":"detect","params":[1]})"))
              .startsWith(u"GS-API-172 "));
    CHECK(refused(kPhone, 6,
                  QStringLiteral(R"({"engine":"alice.finder/boss","kind":"detect",)"
                                 R"("region":{"x":0.75,"y":0,"width":0.5,"height":1}})"))
              .startsWith(u"GS-API-002 "));
    CHECK(refused(kNoPhone, 7, QStringLiteral(R"({"engine":"alice.finder/boss","kind":"detect"})"))
              .startsWith(u"GS-API-073 "));
    CHECK(refused(kHelper, 8, QStringLiteral(R"({"engine":"alice.finder/boss","kind":"detect"})"))
              .startsWith(u"GS-API-072 "));
    CHECK(fixture.grabs.empty());
    CHECK(fixture.engines.pending() == 0);

    fixture.engines.setPermitted({QStringLiteral("alice.helper")});
    fixture.ask(kHelper, 9, QStringLiteral(R"({"engine":"alice.finder/boss","kind":"detect"})"));
    CHECK(fixture.resultOf(9).size() == 1);

    fixture.shot = nullptr;
    fixture.ask(kPhone, 10, QStringLiteral(R"({"engine":"alice.finder/boss","kind":"detect"})"));
    REQUIRE(pumpUntil([&fixture] { return fixture.settled(10); }));
    CHECK(fixture.answerTo(10) == u"null");
    fixture.grabbing = false;
    fixture.ask(kPhone, 11, QStringLiteral(R"({"engine":"alice.finder/boss","kind":"detect"})"));
    CHECK(fixture.answerTo(11) == u"null");
    CHECK(fixture.engines.pending() == 0);
}

TEST_CASE("ADR-0112: an engine takes at most 16 pictures waiting, a dropped request lets go of its frame, and the end "
          "of a logic and an engine that goes away settle what waits")
{
    Fixture fixture;
    fixture.holding = true;
    for (quint64 id = 1; id <= VisionEngines::kMostWaiting; ++id) {
        fixture.ask(kPhone, id, QStringLiteral(R"({"engine":"alice.finder/boss","kind":"detect"})"));
    }
    CHECK(fixture.engines.pending() == VisionEngines::kMostWaiting);
    fixture.ask(kPhone, 100, QStringLiteral(R"({"engine":"alice.finder/boss","kind":"detect"})"));
    CHECK(fixture.refusalOf(100).startsWith(u"GS-API-174 "));

    fixture.engines.dropRequest(kPhone, 1);
    CHECK(fixture.ungrabs == std::vector<quint64>{1});
    CHECK(fixture.engines.pending() == VisionEngines::kMostWaiting - 1);
    fixture.held[0](fixture.shot);
    for (int round = 0; round < 5; ++round) {
        QCoreApplication::processEvents();
    }
    CHECK_FALSE(fixture.settled(1));

    fixture.held[1](fixture.shot);
    CHECK(fixture.resultOf(2).size() == 1);

    fixture.engines.endLogic(kPhone.logic);
    CHECK(fixture.engines.pending() == 0);
    fixture.ask(kOther, 200, QStringLiteral(R"({"engine":"alice.finder/boss","kind":"detect"})"));
    CHECK(fixture.engines.pending() == 1);
    fixture.engines.removeEngine(QStringLiteral("alice.finder/boss"));
    CHECK(fixture.refusalOf(200).startsWith(u"GS-API-170 "));
    CHECK(fixture.engines.pending() == 0);
    fixture.held.back()(fixture.shot);
    for (int round = 0; round < 5; ++round) {
        QCoreApplication::processEvents();
    }
    CHECK(fixture.answerTo(200).isEmpty());
}

TEST_CASE("ADR-0112: an extension's engine reads the part of the frame it is given through its DLL, as BGRA with its "
          "size and stride, and its boxes are mapped to the whole picture")
{
    Application app;
    gs::ext::test::Scratch scratch;
    const fs::path folder = scratch / "gstest.native/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(fs::path(GS_TEST_NATIVE_DLL), folder / "native.dll");
    gs::app::NativeExtensions registry(
        {.program = fs::path(GS_EXTHOST_PROGRAM), .storeRoot = scratch / ".store", .clock = nullptr});
    gs::app::NativeWanted wanted;
    wanted.id = "gstest.native";
    wanted.version = "1.0.0";
    wanted.name = QStringLiteral("Test native");
    wanted.directory = folder;
    wanted.official = true;
    wanted.native.dll = "native.dll";
    wanted.native.visionEngines = {{.name = "probe", .title = "Probe", .kinds = {"ocr", "classify"}, .languages = {}}};
    registry.apply({wanted});

    Fixture fixture;
    fixture.shot = solidShot(64, 32, 20, 10, 30);
    fixture.engines.addEngine(std::make_shared<gs::app::ExtensionVisionEngine>(
        gs::app::ExtensionVisionEngineInfo{.extension = QStringLiteral("gstest.native"),
                                           .name = QStringLiteral("probe"),
                                           .title = QStringLiteral("Probe"),
                                           .kinds = {QStringLiteral("ocr"), QStringLiteral("classify")},
                                           .languages = {}},
        registry));
    REQUIRE(fixture.engines.engines().size() == 2);
    CHECK(fixture.engines.engines()[0].id == u"alice.finder/boss");
    CHECK(fixture.engines.engines()[1].id == u"gstest.native/probe");

    fixture.ask(kPhone, 1,
                QStringLiteral(R"({"engine":"gstest.native/probe","kind":"classify",)"
                               R"("region":{"x":0.5,"y":0,"width":0.5,"height":0.5},"params":{"mode":"mean"}})"));
    const QJsonArray classes = fixture.resultOf(1);
    REQUIRE(classes.size() == 3);
    CHECK(classes[0].toObject().value(u"label").toString() == u"#1E140A");
    CHECK(classes[1].toObject().value(u"label").toString() == u"32x16/128");
    CHECK(classes[2].toObject().value(u"label").toString() == u"{\"mode\":\"mean\"}");
    CHECK(fixture.grabs.back() == std::pair<quint64, bool>{1, true});

    fixture.ask(kPhone, 2,
                QStringLiteral(R"({"engine":"gstest.native/probe","kind":"ocr",)"
                               R"("region":{"x":0.5,"y":0,"width":0.5,"height":0.5}})"));
    const QJsonArray read = fixture.resultOf(2);
    REQUIRE(read.size() == 1);
    const QJsonObject line = read[0].toObject();
    CHECK(line.value(u"text").toString() == u"OK");
    CHECK(line.value(u"x").toDouble() == doctest::Approx(0.75));
    CHECK(line.value(u"y").toDouble() == doctest::Approx(0.125));
    CHECK(line.value(u"width").toDouble() == doctest::Approx(0.25));
    CHECK(line.value(u"height").toDouble() == doctest::Approx(0.125));
    CHECK(line.value(u"confidence").toDouble() == doctest::Approx(0.75));

    fixture.ask(kPhone, 3, QStringLiteral(R"({"engine":"gstest.native/probe","kind":"classify","params":{"fail":1}})"));
    CHECK(fixture.problemOf(3) == u"GS-API-173 the test engine fails on purpose");
    fixture.ask(kPhone, 4, QStringLiteral(R"({"engine":"gstest.native/probe","kind":"classify","params":{"refuse":1}})"));
    CHECK(fixture.problemOf(4) == u"GS-API-172 the test engine does not take refuse");

    registry.apply({});
    fixture.ask(kPhone, 5, QStringLiteral(R"({"engine":"gstest.native/probe","kind":"classify"})"));
    CHECK(fixture.problemOf(5).startsWith(u"GS-API-170 "));
}

TEST_CASE("ADR-0112: the Windows engine reads lines or words of the frame and refuses a language it does not know")
{
    const std::vector<std::string> languages = gs::platform::textLanguages();
    if (std::ranges::none_of(languages, [](const std::string& tag) { return tag.starts_with("en"); })) {
        MESSAGE("no English text recognition on this computer");
        return;
    }
    Fixture fixture;
    fixture.engines.addEngine(std::make_shared<gs::app::SystemVisionEngine>());
    fixture.shot = textShot(480, 240, L"Battery\nSearch settings");
    const auto has = [](const QJsonArray& found, const QString& wanted) {
        return std::any_of(found.begin(), found.end(), [&wanted](const QJsonValue& item) {
            const QJsonObject entry = item.toObject();
            const double x = entry.value(u"x").toDouble(-1.0);
            const double y = entry.value(u"y").toDouble(-1.0);
            return entry.value(u"text").toString().contains(wanted, Qt::CaseInsensitive) && x >= 0.0 && x <= 1.0 &&
                   y >= 0.0 && y <= 1.0 && entry.value(u"confidence").toDouble() == 1.0;
        });
    };
    fixture.ask(kPhone, 1, QStringLiteral(R"({"engine":"system","kind":"ocr","params":{"language":"en"}})"));
    const QJsonArray lines = fixture.resultOf(1);
    CHECK(has(lines, QStringLiteral("battery")));
    CHECK(has(lines, QStringLiteral("search settings")));
    CHECK(fixture.grabs.back() == std::pair<quint64, bool>{1, false});

    fixture.ask(kPhone, 2,
                QStringLiteral(R"({"engine":"system","kind":"ocr","params":{"language":"en","words":true}})"));
    const QJsonArray words = fixture.resultOf(2);
    CHECK(words.size() > lines.size());
    CHECK(has(words, QStringLiteral("settings")));

    fixture.ask(kPhone, 3,
                QStringLiteral(R"({"engine":"system","kind":"ocr","region":{"x":0,"y":0,"width":0.5,"height":0.5},)"
                               R"("params":{"language":"en"}})"));
    for (const QJsonValue& item : fixture.resultOf(3)) {
        CHECK(item.toObject().value(u"x").toDouble() <= 0.5);
        CHECK(item.toObject().value(u"y").toDouble() <= 0.5);
    }

    fixture.ask(kPhone, 4, QStringLiteral(R"({"engine":"system","kind":"ocr","params":{"language":"xx"}})"));
    CHECK(fixture.problemOf(4).startsWith(u"GS-API-171 "));
    fixture.ask(kPhone, 5, QStringLiteral(R"({"engine":"system","kind":"ocr","params":{"language":"en us"}})"));
    CHECK(fixture.problemOf(5).startsWith(u"GS-API-172 "));
    fixture.ask(kPhone, 6, QStringLiteral(R"({"engine":"system","kind":"detect"})"));
    CHECK(fixture.problemOf(6).startsWith(u"GS-API-171 "));
}

TEST_CASE("ADR-0112: vision.recognize refuses what is asked nested more than 64 levels deep before it reads it, so "
          "that logic cannot overflow the stack of GameScrcpy")
{
    Fixture fixture;
    const auto asked = [](std::size_t levels) {
        return QString::fromStdString(R"({"engine":"alice.finder/boss","kind":"detect","params":{"deep":)" +
                                      std::string(levels, '[') + std::string(levels, ']') + "}}");
    };
    fixture.ask(kPhone, 1, asked(62));
    CHECK(fixture.resultOf(1).size() == 1);
    for (const std::size_t levels : {std::size_t{63}, std::size_t{30000}}) {
        fixture.ask(kPhone, 2, asked(levels));
        CHECK(fixture.problemOf(2) == u"GS-API-172 what is asked is nested more than 64 levels deep");
        fixture.refusals.clear();
    }
    CHECK(fixture.grabs.size() == 1);
    CHECK(fixture.engines.pending() == 0);
}
