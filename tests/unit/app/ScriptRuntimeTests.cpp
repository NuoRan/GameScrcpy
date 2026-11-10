#include "ControlChannel.h"
#include "InputHost.h"
#include "ScriptRuntime.h"
#include "VisionSearch.h"

#include <gs/platform/AtomicFile.h>
#include <gs/platform/TextReader.h>
#include <gs/script/Resources.h>

#include <doctest/doctest.h>

extern "C" {
#include <libavutil/frame.h>
}

#include <windows.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QtLogging>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
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

class LogicLines
{
public:
    LogicLines() { s_previous = qInstallMessageHandler(&LogicLines::capture); }

    ~LogicLines()
    {
        qInstallMessageHandler(s_previous);
        const std::scoped_lock lock(s_mutex);
        s_lines.clear();
    }

    LogicLines(const LogicLines&) = delete;
    LogicLines& operator=(const LogicLines&) = delete;

    [[nodiscard]] bool has(const QString& id, const QString& message) const
    {
        const QString line = QStringLiteral("logic %1 on test phone: %2").arg(id, message);
        const std::scoped_lock lock(s_mutex);
        return s_lines.contains(line);
    }

private:
    static void capture(QtMsgType type, const QMessageLogContext& context, const QString& message)
    {
        {
            const std::scoped_lock lock(s_mutex);
            s_lines.append(message);
        }
        if (s_previous != nullptr) {
            s_previous(type, context, message);
        }
    }

    static inline std::mutex s_mutex;
    static inline QStringList s_lines;
    static inline QtMessageHandler s_previous = nullptr;
};

class RecordingSink final : public ControlSink
{
public:
    bool send(const protocol::TouchFrame& frame) override
    {
        frames.push_back(frame);
        return true;
    }

    bool send(const protocol::Key& key) override
    {
        keys.push_back(key);
        return true;
    }

    bool send(const protocol::Scroll&) override { return true; }
    bool send(const protocol::Text&) override { return true; }

    std::vector<protocol::TouchFrame> frames;
    std::vector<protocol::Key> keys;
};

struct Phone {
    explicit Phone(std::uint16_t videoSession)
    {
        channel->attach(sink);
        channel->setVideo(videoSession, 2400, 1080);
    }

    ~Phone() { channel->detach(); }

    Phone(const Phone&) = delete;
    Phone& operator=(const Phone&) = delete;

    RecordingSink sink;
    std::shared_ptr<ControlChannel> channel = std::make_shared<ControlChannel>();
};

class Rig
{
public:
    using Task = std::function<void(InputHost&, Microseconds)>;

    [[nodiscard]] Microseconds now() const
    {
        return std::chrono::duration_cast<Microseconds>(std::chrono::steady_clock::now() - m_started);
    }

    void post(Task task)
    {
        const std::scoped_lock lock(m_mutex);
        m_tasks.push_back(std::move(task));
    }

    void pump()
    {
        std::deque<Task> tasks;
        {
            const std::scoped_lock lock(m_mutex);
            tasks.swap(m_tasks);
        }
        for (Task& task : tasks) {
            task(host, now());
        }
        if (const std::optional<Microseconds> due = host.nextWakeup(); due && *due <= now()) {
            host.wake(now());
        }
    }

    template <typename Condition>
    bool until(Condition condition, std::chrono::milliseconds timeout = 10s)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!condition()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            QCoreApplication::processEvents();
            pump();
            std::this_thread::sleep_for(1ms);
        }
        return true;
    }

    void settle(std::chrono::milliseconds time)
    {
        static_cast<void>(until([] { return false; }, time));
    }

    void press(std::uint16_t makeCode)
    {
        host.key({.makeCode = makeCode}, now());
        host.key({.makeCode = makeCode, .released = true}, now());
    }

    InputHost host{{.pressedMouse = [] { return std::uint8_t{0}; }}};

private:
    std::mutex m_mutex;
    std::deque<Task> m_tasks;
    const std::chrono::steady_clock::time_point m_started = std::chrono::steady_clock::now();
};

class FakeLane final : public LaneAccess
{
public:
    FakeLane(Rig& rig, std::shared_ptr<ControlChannel> phone)
        : m_rig(rig)
        , m_phone(std::move(phone))
    {
    }

    void attach(std::shared_ptr<const input::Profile> profile, std::shared_ptr<LaneObserver> observer) override
    {
        m_rig.post([target = m_phone, profile = std::move(profile), observer = std::move(observer)](
                       InputHost& host, Microseconds now) { host.attach(target, profile, observer, now); });
    }

    void detach(std::shared_ptr<LaneObserver> observer) override
    {
        m_rig.post([target = m_phone, observer = std::move(observer)](InputHost& host, Microseconds now) {
            host.detach(target.get(), observer.get(), now);
        });
    }

    void post(std::function<void(InputHost& host, Microseconds now)> task) override { m_rig.post(std::move(task)); }

    void watch(std::shared_ptr<const input::Profile> profile, std::shared_ptr<LaneObserver> observer) override
    {
        m_rig.post([target = m_phone, profile = std::move(profile), observer = std::move(observer)](
                       InputHost& host, Microseconds now) { host.watch(target, profile, observer, now); });
    }

    void unwatch(std::shared_ptr<LaneObserver> observer) override
    {
        m_rig.post([target = m_phone, observer = std::move(observer)](InputHost& host, Microseconds now) {
            host.unwatch(target.get(), observer.get(), now);
        });
    }

    void watchKeys(std::shared_ptr<LaneObserver> observer, std::vector<input::InputSource> keys) override
    {
        m_rig.post([target = m_phone, observer = std::move(observer), keys = std::move(keys)](
                       InputHost& host, Microseconds) mutable { host.watchKeys(target.get(), observer.get(), std::move(keys)); });
    }

    [[nodiscard]] const ControlChannel* channel() const noexcept override { return m_phone.get(); }

private:
    Rig& m_rig;
    std::shared_ptr<ControlChannel> m_phone;
};

class LogicFolder
{
public:
    explicit LogicFolder(std::string_view logic)
    {
        REQUIRE(m_dir.isValid());
        write(logic);
    }

    void write(std::string_view logic) const
    {
        QFile file(m_dir.filePath(QStringLiteral("logic.ts")));
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(file.write(logic.data(), static_cast<qint64>(logic.size())) == static_cast<qint64>(logic.size()));
    }

    [[nodiscard]] std::filesystem::path path() const { return m_dir.path().toStdWString(); }

    void put(const QString& name, const QImage& image) const
    {
        REQUIRE(QDir(m_dir.path()).mkpath(QFileInfo(m_dir.filePath(name)).path()));
        REQUIRE(image.save(m_dir.filePath(name), "PNG"));
    }

    void text(const QString& name, std::string_view contents) const
    {
        REQUIRE(QDir(m_dir.path()).mkpath(QFileInfo(m_dir.filePath(name)).path()));
        QFile file(m_dir.filePath(name));
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(file.write(contents.data(), static_cast<qint64>(contents.size())) == static_cast<qint64>(contents.size()));
    }

private:
    QTemporaryDir m_dir;
};

class Store
{
public:
    Store() { REQUIRE(m_dir.isValid()); }

    [[nodiscard]] std::filesystem::path folder() const { return m_dir.path().toStdWString(); }

    [[nodiscard]] QString path(const QString& id, const QString& phone = QStringLiteral("test-phone")) const
    {
        return m_dir.filePath(id + u'/' + phone + QStringLiteral(".json"));
    }

    [[nodiscard]] QJsonObject of(const QString& id, const QString& phone = QStringLiteral("test-phone")) const
    {
        std::string contents;
        if (platform::readFile(std::filesystem::path(path(id, phone).toStdWString()), kMostBytes, contents,
                               platform::WhenBusy::GiveUp)) {
            return {};
        }
        return QJsonDocument::fromJson(QByteArray::fromStdString(contents)).object();
    }

    [[nodiscard]] int number(const QString& id, const QString& key,
                             const QString& phone = QStringLiteral("test-phone")) const
    {
        return of(id, phone).value(key).toInt();
    }

private:
    static constexpr std::size_t kMostBytes = 1 << 20;

    QTemporaryDir m_dir;
};

struct Logic {
    Logic(Rig& rig, const Phone& phone, const LogicFolder& folder, std::shared_ptr<const input::Profile> profile,
          const QString& id, const Store& store, const QString& key = QStringLiteral("test-phone"),
          std::shared_ptr<media::FrameSnapshots> snapshots = nullptr)
        : runtime(LogicSource{.folder = folder.path(),
                              .entry = "logic.ts",
                              .profile = std::move(profile),
                              .id = id,
                              .name = id,
                              .device = QStringLiteral("test phone"),
                              .deviceKey = key,
                              .snapshots = std::move(snapshots)},
                  std::make_shared<FakeLane>(rig, phone.channel), store.folder())
    {
        QObject::connect(&runtime, &ScriptRuntime::loaded, [this] { ++loads; });
        QObject::connect(&runtime, &ScriptRuntime::failed, [this](const QString& text) { failures.append(text); });
        QObject::connect(&runtime, &ScriptRuntime::tooFast, [this] { ++fast; });
        QObject::connect(&runtime, &ScriptRuntime::storeBusy, [this] { ++busy; });
        runtime.setDevice({.model = "Pixel test", .android = 15, .width = 1080, .height = 2400});
    }

    int loads = 0;
    int fast = 0;
    int busy = 0;
    QStringList failures;
    ScriptRuntime runtime;
};

std::shared_ptr<const input::Profile> profileOf(std::string_view json)
{
    const input::LoadResult loaded = input::loadProfile(json);
    REQUIRE(loaded.ok());
    return std::make_shared<const input::Profile>(loaded.profile);
}

std::vector<protocol::TouchPoint> pointsOf(const RecordingSink& sink, bool script)
{
    std::vector<protocol::TouchPoint> points;
    for (const protocol::TouchFrame& frame : sink.frames) {
        for (const protocol::TouchPoint& point : frame.points) {
            if ((point.pointerId >= InputHost::kEnginePointers) == script) {
                points.push_back(point);
            }
        }
    }
    return points;
}

std::size_t downs(const RecordingSink& sink, bool script)
{
    const std::vector<protocol::TouchPoint> points = pointsOf(sink, script);
    return static_cast<std::size_t>(std::ranges::count(points, protocol::TouchAction::Down, &protocol::TouchPoint::action));
}

struct FrameDeleter {
    void operator()(AVFrame* frame) const noexcept { av_frame_free(&frame); }
};
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

FramePtr noisyPicture(int width, int height)
{
    FramePtr frame(av_frame_alloc());
    REQUIRE(frame != nullptr);
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = width;
    frame->height = height;
    REQUIRE(av_frame_get_buffer(frame.get(), 0) >= 0);
    std::uint32_t seed = 2026;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            seed = seed * 1664525u + 1013904223u;
            frame->data[0][y * frame->linesize[0] + x] = static_cast<std::uint8_t>(seed >> 24);
        }
    }
    return frame;
}

FramePtr buttonsPicture(int width, int height)
{
    FramePtr frame = noisyPicture(width, height);
    frame->colorspace = AVCOL_SPC_BT709;
    frame->color_range = AVCOL_RANGE_MPEG;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::uint8_t& light = frame->data[0][y * frame->linesize[0] + x];
            light = static_cast<std::uint8_t>(20 + light / 4);
        }
    }
    for (int y = 0; y < (height + 1) / 2; ++y) {
        std::fill_n(frame->data[1] + y * frame->linesize[1], (width + 1) / 2, std::uint8_t{128});
        std::fill_n(frame->data[2] + y * frame->linesize[2], (width + 1) / 2, std::uint8_t{128});
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
    return frame;
}

FramePtr textPicture(int width, int height, std::wstring_view text)
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
    FramePtr frame = noisyPicture(width, height);
    frame->colorspace = AVCOL_SPC_BT709;
    frame->color_range = AVCOL_RANGE_MPEG;
    const auto* pixels = static_cast<const std::uint8_t*>(bits);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* pixel = pixels + (static_cast<std::size_t>(y) * width + x) * 4;
            const int grey = (pixel[0] + pixel[1] + pixel[2]) / 3;
            frame->data[0][y * frame->linesize[0] + x] = static_cast<std::uint8_t>(16 + grey * 219 / 255);
        }
    }
    for (int y = 0; y < (height + 1) / 2; ++y) {
        std::fill_n(frame->data[1] + y * frame->linesize[1], (width + 1) / 2, std::uint8_t{128});
        std::fill_n(frame->data[2] + y * frame->linesize[2], (width + 1) / 2, std::uint8_t{128});
    }
    SelectObject(dc, previousFont);
    DeleteObject(font);
    SelectObject(dc, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return frame;
}

constexpr std::uint16_t kSpace = 0x39;
constexpr std::uint16_t kKeyF = 0x21;
constexpr std::uint16_t kKeyR = 0x13;

constexpr std::string_view kProfile = R"({"format":"gamescrcpy.profile","version":3,
    "meta":{"name":"Logic"},
    "controls":[
        {"id":"jump","type":"hold","anchor":"bottomRight","offset":[-0.2,-0.2],"inputs":{"press":"Space"}},
        {"id":"fire","type":"hold","anchor":"right","offset":[-0.3,0],"inputs":{"press":"KeyF"}},
        {"id":"probe","type":"tap","anchor":"left","offset":[0.2,0],"inputs":{"press":"KeyR"}}],
    "vars":[{"id":"aiming"}]})";

constexpr std::string_view kEchoLogic = R"(let presses = 0;
store.set("model", device.info().model);
controls.jump.onPress(async () => {
    presses += 1;
    store.set("presses", presses);
    await controls.jump.trigger();
    await controls.fire.trigger();
    store.set("done", 1);
    log.info("done");
});
controls.fire.onPress(() => {
    store.set("fire", 1);
    log.info("fire");
});
)";

constexpr std::string_view kAimLogic = R"(store.set("startAiming", vars.aiming.value ? 1 : 0);
vars.aiming.onChange(({ value }) => {
    controls.fire.enabled = !value;
    const changes = Number(store.get("changes") ?? 0) + 1;
    store.set("changes", changes);
    log.info("changes", changes);
});
)";

constexpr std::string_view kThrowingLogic = R"(controls.jump.onPress(() => {
    throw new Error("boom");
});
controls.fire.onPress(() => {
    store.set("fire", Number(store.get("fire") ?? 0) + 1);
    log.info("fire");
});
)";

constexpr std::string_view kSteadyLogic = R"(controls.jump.onPress(async () => {
    await touch.tap(0.5, 0.5, 20);
    store.set("tapped", Number(store.get("tapped") ?? 0) + 1);
    log.info("tapped");
});
)";

constexpr std::string_view kCountLogic = R"(controls.jump.onPress(() => {
    store.set("presses", Number(store.get("presses") ?? 0) + 1);
});
)";

constexpr std::string_view kAskLogic = R"(ui.toast("ready");
controls.jump.onPress(async () => {
    const sure = await ui.confirm("Sure?");
    log.info("sure", sure);
});
)";

constexpr std::string_view kDrawLogic = R"(ui.box(0.1, 0.1, 0.2, 0.2, { label: "top" });
controls.jump.onPress(() => {
    ui.text(0.5, 0.5, "pressed");
});
)";

constexpr std::string_view kVisionLogic = R"(const describe = (found) => found
    ? [found.x, found.y, found.width, found.height].map((v) => v.toFixed(4)).join(" ") + (found.score > 0.99 ? " sure" : " unsure")
    : "none";
const codeOf = async (image) => {
    try {
        await vision.find(image);
        return "none";
    } catch (error) {
        return error.message.split(" ")[0];
    }
};
controls.jump.onPress(async () => {
    log.info("still", describe(await vision.find("res/patch.png")));
});
controls.fire.onPress(async () => {
    const all = await vision.findAll("res/patch.png", { region: { x: 0.25, y: 0.25, width: 0.5, height: 0.5 } });
    log.info("fresh", all.length, describe(all[0]));
    const elsewhere = await vision.findAll("res/patch.png", { region: { x: 0, y: 0, width: 0.25, height: 0.25 } });
    log.info("elsewhere", elsewhere.length);
    log.info("bad", await codeOf("res/missing.png"), await codeOf("res/flat.png"), await codeOf("res/clear.png"));
});
controls.probe.onPress(async () => {
    const one = await vision.find("res/patch.png", { timeoutMs: 100 });
    const all = await vision.findAll("res/patch.png", { timeoutMs: 100 });
    log.info("blank", describe(one), all.length);
});
)";

constexpr std::string_view kNamedVisionLogic = R"(const describe = (found) => found
    ? [found.x, found.y, found.width, found.height].map((v) => v.toFixed(4)).join(" ") + (found.score > 0.99 ? " sure" : " unsure")
    : "none";
const box = (found) => found
    ? [found.x, found.y, found.width, found.height].map((v) => v.toFixed(4)).join(" ") + " " + found.score.toFixed(2)
    : "none";
controls.jump.onPress(async () => {
    const red = await vision.find("红按钮");
    const grey = await vision.findAll("红按钮", { match: "grey" });
    const blue = await vision.findColor("蓝色");
    const none = await vision.findColor("#00FF00", { tolerance: 0.05 });
    log.info("named", describe(red), grey.length, box(blue), none === null);
});
)";

constexpr std::string_view kTextLogic = R"(controls.jump.onPress(async () => {
    const read = await vision.readText({ language: "en" });
    log.info("read", read ? read.lines.map((line) => line.text.toLowerCase().replace(/ /g, "")).join("|") : "none");
    const found = await vision.findText("search settings", { language: "en" });
    log.info("found", found !== null && found.x > 0.1 && found.x < 0.5 && found.y > 0.3 && found.y < 0.5);
    const right = await vision.findText("battery", { region: { x: 0.5, y: 0, width: 0.5, height: 1 }, language: "en" });
    log.info("right", right === null);
    const missing = await vision.readText({ language: "xx" }).then(() => "none", (error) => error.message.split(" ")[0]);
    log.info("missing", missing);
});
)";

constexpr std::string_view kFloodLogic = R"(function flood(): void {
    for (let i = 0; i < 150; i += 1) {
        void touch.tap(0.5, 0.5, 1);
    }
}
flood();
store.set("loads", Number(store.get("loads") ?? 0) + 1);
controls.fire.onPress(() => {
    flood();
    store.set("combos", Number(store.get("combos") ?? 0) + 1);
});
)";

constexpr std::string_view kClipboardLogic = R"(controls.jump.onPress(async () => {
    const text = await clipboard.read();
    const sent = await clipboard.write("from logic");
    log.info("clipboard", text, sent);
});
)";

struct Setup {
    Setup() : profile(profileOf(kProfile))
    {
        rig.host.setChannel(phone.channel, rig.now());
        rig.host.setProfile(profile, rig.now());
        rig.host.setActive(true, rig.now());
    }

    Phone phone{7};
    Rig rig;
    std::shared_ptr<const input::Profile> profile;
    LogicLines lines;
};

}

TEST_CASE("logic starts once the phone's states arrive, and its own presses never reach its listeners (ADR-0081, SCR-010)")
{
    const Application app;
    const Store store;
    const LogicFolder folder(kEchoLogic);
    Setup setup;
    Logic logic(setup.rig, setup.phone, folder, setup.profile, QStringLiteral("echo"), store);
    const QString id = QStringLiteral("echo");

    for (int i = 0; i < 20; ++i) {
        QCoreApplication::processEvents();
    }
    CHECK_FALSE(logic.runtime.running());
    REQUIRE(setup.rig.until([&] { return logic.loads == 1; }));
    CHECK(logic.runtime.running());
    CHECK(setup.rig.until([&] {
        return store.of(id).value(QStringLiteral("model")).toString() == QStringLiteral("Pixel test");
    }));

    setup.rig.press(kSpace);
    REQUIRE(setup.rig.until([&] { return setup.lines.has(id, QStringLiteral("done")); }));
    setup.rig.settle(150ms);
    CHECK(setup.rig.until([&] { return store.number(id, QStringLiteral("done")) == 1; }));
    CHECK(store.number(id, QStringLiteral("presses")) == 1);
    CHECK_FALSE(setup.lines.has(id, QStringLiteral("fire")));
    CHECK_FALSE(store.of(id).contains(QStringLiteral("fire")));
    CHECK(downs(setup.phone.sink, false) == 3);
    CHECK(setup.phone.sink.frames.size() == 6);
    CHECK(pointsOf(setup.phone.sink, true).empty());

    setup.rig.press(kKeyF);
    REQUIRE(setup.rig.until([&] { return setup.lines.has(id, QStringLiteral("fire")); }));
    CHECK(setup.rig.until([&] { return store.number(id, QStringLiteral("fire")) == 1; }));
    CHECK(store.number(id, QStringLiteral("presses")) == 1);
    CHECK(logic.failures.isEmpty());
}

TEST_CASE("switches the logic turned off come back when it reloads or stops, and the states stay (ADR-0081, SCR-018)")
{
    const Application app;
    const Store store;
    const LogicFolder folder(kAimLogic);
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("aim");
    std::optional<Logic> logic;
    bool aiming = false;
    const auto toggleAiming = [&] {
        aiming = !aiming;
        rig.post([channel = setup.phone.channel.get(), value = aiming](InputHost& host, Microseconds now) {
            static_cast<void>(host.setState(channel, "aiming", value, now));
        });
    };
    logic.emplace(rig, setup.phone, folder, setup.profile, id, store);
    REQUIRE(rig.until([&] { return logic->loads == 1; }));
    CHECK(rig.until([&] { return store.of(id).contains(QStringLiteral("startAiming")); }));
    CHECK(store.number(id, QStringLiteral("startAiming")) == 0);

    toggleAiming();
    REQUIRE(rig.until([&] { return setup.lines.has(id, QStringLiteral("changes 1")); }));
    rig.settle(50ms);
    std::size_t before = downs(setup.phone.sink, false);
    rig.press(kKeyF);
    CHECK(downs(setup.phone.sink, false) == before);

    logic->runtime.reload();
    REQUIRE(rig.until([&] { return logic->loads == 2; }));
    rig.settle(50ms);
    CHECK(rig.until([&] { return store.number(id, QStringLiteral("startAiming")) == 1; }));
    rig.press(kKeyF);
    CHECK(downs(setup.phone.sink, false) == before + 1);

    toggleAiming();
    REQUIRE(rig.until([&] { return setup.lines.has(id, QStringLiteral("changes 2")); }));
    toggleAiming();
    REQUIRE(rig.until([&] { return setup.lines.has(id, QStringLiteral("changes 3")); }));
    rig.settle(50ms);
    before = downs(setup.phone.sink, false);
    rig.press(kKeyF);
    CHECK(downs(setup.phone.sink, false) == before);

    logic.reset();
    rig.settle(50ms);
    rig.press(kKeyF);
    CHECK(downs(setup.phone.sink, false) == before + 1);
    CHECK(store.number(id, QStringLiteral("changes")) == 3);
}

TEST_CASE("a handler that throws is reported while the phone, the logic's other handlers and another phone's logic keep working (SCR-009)")
{
    const Application app;
    const Store store;
    const LogicFolder throwing(kThrowingLogic);
    const LogicFolder steady(kSteadyLogic);
    Phone second(3);
    Setup setup;
    Rig& rig = setup.rig;
    Logic thrower(rig, setup.phone, throwing, setup.profile, QStringLiteral("thrower"), store);
    Logic other(rig, second, steady, setup.profile, QStringLiteral("steady"), store);
    REQUIRE(rig.until([&] { return thrower.loads == 1 && other.loads == 1; }));

    rig.press(kSpace);
    REQUIRE(rig.until([&] { return thrower.failures.size() == 1; }));
    CHECK(thrower.failures.front().startsWith(QStringLiteral("Logic error: logic.ts line 2: ")));
    CHECK(thrower.failures.front().contains(QStringLiteral("boom")));
    CHECK(downs(setup.phone.sink, false) == 1);

    rig.press(kKeyF);
    REQUIRE(rig.until([&] { return setup.lines.has(QStringLiteral("thrower"), QStringLiteral("fire")); }));
    CHECK(rig.until([&] { return store.number(QStringLiteral("thrower"), QStringLiteral("fire")) == 1; }));
    CHECK(downs(setup.phone.sink, false) == 2);
    rig.press(kSpace);
    REQUIRE(rig.until([&] { return thrower.failures.size() == 2; }));
    CHECK(thrower.runtime.running());

    rig.host.setChannel(second.channel, rig.now());
    rig.host.setProfile(setup.profile, rig.now());
    rig.press(kSpace);
    REQUIRE(rig.until([&] { return setup.lines.has(QStringLiteral("steady"), QStringLiteral("tapped")); }));
    CHECK(rig.until([&] { return store.number(QStringLiteral("steady"), QStringLiteral("tapped")) == 1; }));
    CHECK(downs(second.sink, false) == 1);
    CHECK(downs(second.sink, true) == 1);
    CHECK(other.failures.isEmpty());
    CHECK(thrower.failures.size() == 2);
    CHECK(pointsOf(setup.phone.sink, true).empty());
}

TEST_CASE("logic acting more than 100 times a second is noticed once per load and nothing is held back (ADR-0081)")
{
    const Application app;
    const Store store;
    const LogicFolder folder(kFloodLogic);
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("flood");
    Logic logic(rig, setup.phone, folder, setup.profile, id, store);
    REQUIRE(rig.until([&] { return logic.loads == 1 && logic.fast == 1; }));
    REQUIRE(rig.until([&] { return downs(setup.phone.sink, true) == 150; }));

    rig.press(kKeyF);
    REQUIRE(rig.until([&] { return downs(setup.phone.sink, true) == 300; }));
    rig.settle(100ms);
    CHECK(logic.fast == 1);
    CHECK(rig.until([&] { return store.number(id, QStringLiteral("combos")) == 1; }));

    logic.runtime.reload();
    REQUIRE(rig.until([&] { return logic.loads == 2 && logic.fast == 2; }));
    REQUIRE(rig.until([&] { return downs(setup.phone.sink, true) == 450; }));
    CHECK(rig.until([&] { return store.number(id, QStringLiteral("loads")) == 2; }));
    CHECK(logic.failures.isEmpty());
}

TEST_CASE("saving the logic file reloads it, and a file that fails to load is reported with its line (ADR-0081)")
{
    const Application app;
    const Store store;
    const LogicFolder folder("store.set(\"version\", 1);\n");
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("saved");
    Logic logic(rig, setup.phone, folder, setup.profile, id, store);
    REQUIRE(rig.until([&] { return logic.loads == 1; }));
    CHECK(rig.until([&] { return store.number(id, QStringLiteral("version")) == 1; }));

    folder.write("const answer = 42;\nthrow new Error(\"broken \" + answer);\n");
    REQUIRE(rig.until([&] { return !logic.failures.isEmpty(); }));
    CHECK(logic.failures.back().startsWith(QStringLiteral("Logic error: logic.ts line 2: ")));
    CHECK(logic.failures.back().contains(QStringLiteral("broken 42")));
    CHECK(logic.loads == 1);

    folder.write("store.set(\"version\", 3);\n");
    REQUIRE(rig.until([&] { return logic.loads >= 2; }));
    CHECK(rig.until([&] { return store.number(id, QStringLiteral("version")) == 3; }));
}

TEST_CASE("logic reports each log line and error with its level, text and place for the script console")
{
    const Application app;
    const Store store;
    const LogicFolder folder("log.info(\"ready\", 2);\nlog.warn(\"slow\");\ncontrols.jump.onPress(() => {\n"
                             "    throw new Error(\"boom\");\n});\n");
    Setup setup;
    Rig& rig = setup.rig;
    Logic logic(rig, setup.phone, folder, setup.profile, QStringLiteral("reporting"), store);
    std::vector<LogicReport> reports;
    QObject::connect(&logic.runtime, &ScriptRuntime::reported,
                     [&reports](const LogicReport& report) { reports.push_back(report); });
    const auto find = [&reports](script::ReportKind kind, const QString& text) -> const LogicReport* {
        const auto found = std::ranges::find_if(reports, [kind, &text](const LogicReport& report) {
            return report.kind == kind && report.text.contains(text);
        });
        return found == reports.end() ? nullptr : &*found;
    };
    REQUIRE(rig.until([&] {
        return logic.loads == 1 && find(script::ReportKind::ScriptLoaded, QStringLiteral("logic.ts")) != nullptr;
    }));
    const LogicReport* ready = find(script::ReportKind::Log, QStringLiteral("ready 2"));
    REQUIRE(ready != nullptr);
    CHECK(ready->level == log::Level::Info);
    CHECK(ready->text == QStringLiteral("ready 2"));
    CHECK(ready->file.isEmpty());
    CHECK(ready->time.isValid());
    CHECK(sameFolder(ready->folder, QString::fromStdWString(folder.path().wstring())));
    const LogicReport* slow = find(script::ReportKind::Log, QStringLiteral("slow"));
    REQUIRE(slow != nullptr);
    CHECK(slow->level == log::Level::Warning);
    CHECK_FALSE(logic.runtime.halted());
    CHECK(logic.runtime.lastError().isEmpty());

    rig.press(kSpace);
    REQUIRE(rig.until([&] { return find(script::ReportKind::TaskFailed, QStringLiteral("boom")) != nullptr; }));
    const LogicReport* boom = find(script::ReportKind::TaskFailed, QStringLiteral("boom"));
    CHECK(boom->level == log::Level::Error);
    CHECK(boom->file == QStringLiteral("logic.ts"));
    CHECK(boom->line == 4);
    CHECK_FALSE(boom->text.startsWith(QStringLiteral("logic.ts")));
    CHECK(logic.runtime.lastError().contains(QStringLiteral("boom")));
    CHECK_FALSE(logic.runtime.halted());
    CHECK(logic.runtime.running());

    folder.write("const answer = 42;\nthrow new Error(\"broken \" + answer);\n");
    REQUIRE(rig.until([&] { return logic.runtime.halted(); }));
    const LogicReport* broken = find(script::ReportKind::ScriptFailed, QStringLiteral("broken 42"));
    REQUIRE(broken != nullptr);
    CHECK(broken->level == log::Level::Error);
    CHECK(broken->file == QStringLiteral("logic.ts"));
    CHECK(broken->line == 2);
    CHECK(logic.runtime.lastError().startsWith(QStringLiteral("Logic error: logic.ts line 2: ")));

    folder.write("log.info(\"fixed\");\n");
    REQUIRE(rig.until([&] {
        return find(script::ReportKind::Log, QStringLiteral("fixed")) != nullptr && !logic.runtime.halted();
    }));
    CHECK(logic.runtime.lastError().isEmpty());
}

TEST_CASE("each phone keeps its own store for the same logic, and a phone identified later takes it along (ADR-0081)")
{
    const Application app;
    const Store store;
    const LogicFolder folder(kCountLogic);
    Phone second(3);
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("count");
    Logic first(rig, setup.phone, folder, setup.profile, id, store, QStringLiteral("sn:FIRST"));
    Logic other(rig, second, folder, setup.profile, id, store, QStringLiteral("sn:SECOND"));
    REQUIRE(rig.until([&] { return first.loads == 1 && other.loads == 1; }));
    CHECK(first.runtime.storePath() == (store.folder() / L"count" / L"sn%3AFIRST.json"));

    rig.press(kSpace);
    REQUIRE(rig.until([&] { return store.number(id, QStringLiteral("presses"), QStringLiteral("sn%3AFIRST")) == 1; }));

    rig.host.setChannel(second.channel, rig.now());
    rig.host.setProfile(setup.profile, rig.now());
    rig.press(kSpace);
    rig.press(kSpace);
    REQUIRE(rig.until([&] { return store.number(id, QStringLiteral("presses"), QStringLiteral("sn%3ASECOND")) == 2; }));
    CHECK(store.number(id, QStringLiteral("presses"), QStringLiteral("sn%3AFIRST")) == 1);

    other.runtime.setDeviceKey(QStringLiteral("aid:0123"));
    REQUIRE(rig.until([&] { return other.loads == 2; }));
    CHECK(rig.until([&] { return store.number(id, QStringLiteral("presses"), QStringLiteral("aid%3A0123")) == 2; }));
    rig.press(kSpace);
    REQUIRE(rig.until([&] { return store.number(id, QStringLiteral("presses"), QStringLiteral("aid%3A0123")) == 3; }));
    CHECK(store.number(id, QStringLiteral("presses"), QStringLiteral("sn%3ASECOND")) == 2);
    CHECK(store.number(id, QStringLiteral("presses"), QStringLiteral("sn%3AFIRST")) == 1);
    CHECK(first.failures.isEmpty());
    CHECK(other.failures.isEmpty());
}

TEST_CASE("a phone identified later goes back to its own store when it already has one (ADR-0081)")
{
    const Application app;
    const Store store;
    const LogicFolder folder(kCountLogic);
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("known");
    REQUIRE(QDir().mkpath(QFileInfo(store.path(id, QStringLiteral("aid%3Aknown"))).path()));
    {
        QFile own(store.path(id, QStringLiteral("aid%3Aknown")));
        REQUIRE(own.open(QIODevice::WriteOnly));
        own.write(R"({"presses":5})");
    }
    Logic logic(rig, setup.phone, folder, setup.profile, id, store, QStringLiteral("serial-1"));
    REQUIRE(rig.until([&] { return logic.loads == 1; }));
    rig.press(kSpace);
    REQUIRE(rig.until([&] { return store.number(id, QStringLiteral("presses"), QStringLiteral("serial-1")) == 1; }));

    logic.runtime.setDeviceKey(QStringLiteral("aid:known"));
    REQUIRE(rig.until([&] { return logic.loads == 2; }));
    rig.press(kSpace);
    REQUIRE(rig.until([&] { return store.number(id, QStringLiteral("presses"), QStringLiteral("aid%3Aknown")) == 6; }));
    CHECK(store.number(id, QStringLiteral("presses"), QStringLiteral("serial-1")) == 1);
    CHECK(logic.failures.isEmpty());
}

TEST_CASE("a store file that is not a JSON object is kept aside and the logic starts empty (ADR-0081)")
{
    const Application app;
    const Store store;
    const LogicFolder folder(kCountLogic);
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("broken");
    const QString path = store.path(id);
    REQUIRE(QDir().mkpath(QFileInfo(path).path()));
    const QByteArray broken = R"({"presses": 4,})";
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(broken);
    }
    Logic logic(rig, setup.phone, folder, setup.profile, id, store);
    REQUIRE(rig.until([&] { return logic.loads == 1; }));
    rig.press(kSpace);
    REQUIRE(rig.until([&] { return store.number(id, QStringLiteral("presses")) == 1; }));
    QFile aside(path + QStringLiteral(".bad"));
    REQUIRE(aside.open(QIODevice::ReadOnly));
    CHECK(aside.readAll() == broken);
    CHECK(logic.busy == 0);
    CHECK(logic.failures.isEmpty());
}

TEST_CASE("a broken store file another program holds for a moment is still kept aside once it lets go (ADR-0058, ADR-0081)")
{
    const Application app;
    const Store store;
    const LogicFolder folder(kCountLogic);
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("scanned");
    const QString path = store.path(id);
    REQUIRE(QDir().mkpath(QFileInfo(path).path()));
    const QByteArray broken = R"({"presses": 4,})";
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(broken);
    }
    const std::wstring native = path.toStdWString();
    HANDLE scanner = ::CreateFileW(native.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(scanner != INVALID_HANDLE_VALUE);
    Logic logic(rig, setup.phone, folder, setup.profile, id, store);
    REQUIRE(rig.until([&] { return logic.loads == 1; }));
    const std::jthread letGo([scanner] {
        std::this_thread::sleep_for(300ms);
        ::CloseHandle(scanner);
    });
    rig.press(kSpace);
    REQUIRE(rig.until([&] { return store.number(id, QStringLiteral("presses")) == 1; }));
    QFile aside(path + QStringLiteral(".bad"));
    REQUIRE(aside.open(QIODevice::ReadOnly));
    CHECK(aside.readAll() == broken);
    CHECK(logic.busy == 0);
    CHECK(logic.failures.isEmpty());
}

TEST_CASE("a broken store file another program keeps holding is not saved over, and is set aside on the next load (ADR-0058, ADR-0081)")
{
    const Application app;
    const Store store;
    const LogicFolder folder(kCountLogic);
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("held");
    const QString path = store.path(id);
    REQUIRE(QDir().mkpath(QFileInfo(path).path()));
    const QByteArray broken = R"({"presses": 4,})";
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(broken);
    }
    const std::wstring native = path.toStdWString();
    HANDLE scanner = ::CreateFileW(native.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(scanner != INVALID_HANDLE_VALUE);
    Logic logic(rig, setup.phone, folder, setup.profile, id, store);
    REQUIRE(rig.until([&] { return logic.loads == 1; }));
    rig.press(kSpace);
    REQUIRE(rig.until([&] { return logic.busy == 1; }));
    rig.press(kSpace);
    rig.settle(200ms);
    ::CloseHandle(scanner);
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::ReadOnly));
        CHECK(file.readAll() == broken);
    }
    CHECK_FALSE(QFile::exists(path + QStringLiteral(".bad")));

    logic.runtime.reload();
    REQUIRE(rig.until([&] { return logic.loads == 2; }));
    rig.press(kSpace);
    REQUIRE(rig.until([&] { return store.number(id, QStringLiteral("presses")) == 1; }));
    QFile aside(path + QStringLiteral(".bad"));
    REQUIRE(aside.open(QIODevice::ReadOnly));
    CHECK(aside.readAll() == broken);
    CHECK(logic.busy == 1);
    CHECK(logic.failures.isEmpty());
}

TEST_CASE("a store file another program keeps from being read is left alone for the run (ADR-0058, ADR-0081)")
{
    const Application app;
    const Store store;
    const LogicFolder folder(kCountLogic);
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("locked");
    const QString path = store.path(id);
    REQUIRE(QDir().mkpath(QFileInfo(path).path()));
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(R"({"presses":4})");
    }
    const std::wstring native = path.toStdWString();
    HANDLE owner = ::CreateFileW(native.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(owner != INVALID_HANDLE_VALUE);
    Logic logic(rig, setup.phone, folder, setup.profile, id, store);
    REQUIRE(rig.until([&] { return logic.loads == 1; }));
    rig.press(kSpace);
    REQUIRE(rig.until([&] { return logic.busy == 1; }));
    rig.press(kSpace);
    rig.settle(200ms);
    ::CloseHandle(owner);
    rig.settle(300ms);
    CHECK(store.number(id, QStringLiteral("presses")) == 4);

    logic.runtime.reload();
    REQUIRE(rig.until([&] { return logic.loads == 2; }));
    rig.press(kSpace);
    REQUIRE(rig.until([&] { return store.number(id, QStringLiteral("presses")) == 5; }));
    CHECK(logic.busy == 1);
    CHECK(logic.failures.isEmpty());
}

TEST_CASE("logic shows toasts and questions in the mirror window and gets the answers back (ADR-0082)")
{
    const Application app;
    const Store store;
    const LogicFolder folder(kAskLogic);
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("asking");
    Logic logic(rig, setup.phone, folder, setup.profile, id, store);
    QStringList toasts;
    std::vector<std::pair<quint64, QVariantMap>> prompts;
    QList<quint64> withdrawn;
    QObject::connect(&logic.runtime, &ScriptRuntime::toast, [&toasts](const QString& text) { toasts.append(text); });
    QObject::connect(&logic.runtime, &ScriptRuntime::prompt,
                     [&prompts](quint64 request, const QVariantMap& shown) { prompts.emplace_back(request, shown); });
    QObject::connect(&logic.runtime, &ScriptRuntime::promptWithdrawn,
                     [&withdrawn](quint64 request) { withdrawn.append(request); });
    REQUIRE(rig.until([&] { return logic.loads == 1 && toasts.size() == 1; }));
    CHECK(toasts[0] == QStringLiteral("ready"));

    rig.press(kSpace);
    REQUIRE(rig.until([&] { return prompts.size() == 1; }));
    CHECK(prompts[0].second.value(QStringLiteral("kind")).toString() == QStringLiteral("confirm"));
    CHECK(prompts[0].second.value(QStringLiteral("question")).toString() == QStringLiteral("Sure?"));
    logic.runtime.answer(prompts[0].first, QStringLiteral("true"));
    REQUIRE(rig.until([&] { return setup.lines.has(id, QStringLiteral("sure true")); }));

    rig.press(kSpace);
    REQUIRE(rig.until([&] { return prompts.size() == 2; }));
    logic.runtime.reload();
    REQUIRE(rig.until([&] { return withdrawn.size() == 1; }));
    CHECK(withdrawn[0] == prompts[1].first);
    CHECK(logic.failures.isEmpty());
}

TEST_CASE("logic asks for the phone's clipboard and gives it text through the runtime, and gets the answers back "
          "(ADR-0091)")
{
    const Application app;
    const Store store;
    const LogicFolder folder(kClipboardLogic);
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("clipping");
    Logic logic(rig, setup.phone, folder, setup.profile, id, store);
    QList<quint64> wanted;
    std::vector<std::pair<quint64, QString>> given;
    QObject::connect(&logic.runtime, &ScriptRuntime::clipboardWanted,
                     [&wanted](quint64 request) { wanted.append(request); });
    QObject::connect(&logic.runtime, &ScriptRuntime::clipboardGiven,
                     [&given](quint64 request, const QString& text) { given.emplace_back(request, text); });
    REQUIRE(rig.until([&] { return logic.loads == 1; }));

    rig.press(kSpace);
    REQUIRE(rig.until([&] { return wanted.size() == 1; }));
    CHECK(given.empty());
    logic.runtime.answer(wanted[0], QStringLiteral("\"on the phone\""));
    REQUIRE(rig.until([&] { return given.size() == 1; }));
    CHECK(given[0].second == QStringLiteral("from logic"));
    logic.runtime.answer(given[0].first, QStringLiteral("true"));
    REQUIRE(rig.until([&] { return setup.lines.has(id, QStringLiteral("clipboard on the phone true")); }));
    CHECK(wanted.size() == 1);
    CHECK(logic.failures.isEmpty());
}

TEST_CASE("logic drawings reach the mirror window, leave with their task and are cleared on reload (ADR-0082)")
{
    const Application app;
    const Store store;
    const LogicFolder folder(kDrawLogic);
    Setup setup;
    Rig& rig = setup.rig;
    Logic logic(rig, setup.phone, folder, setup.profile, QStringLiteral("drawing"), store);
    std::vector<std::pair<quint64, QVariantMap>> drawn;
    QList<quint64> erased;
    int cleared = 0;
    QObject::connect(&logic.runtime, &ScriptRuntime::drawn,
                     [&drawn](quint64 id, const QVariantMap& shown) { drawn.emplace_back(id, shown); });
    QObject::connect(&logic.runtime, &ScriptRuntime::erased, [&erased](quint64 id) { erased.append(id); });
    QObject::connect(&logic.runtime, &ScriptRuntime::drawingsCleared, [&cleared] { ++cleared; });
    REQUIRE(rig.until([&] { return logic.loads == 1 && drawn.size() == 1; }));
    CHECK(drawn[0].second.value(QStringLiteral("kind")).toString() == QStringLiteral("box"));
    CHECK(drawn[0].second.value(QStringLiteral("text")).toString() == QStringLiteral("top"));
    CHECK(drawn[0].second.value(QStringLiteral("color")).toString() == QStringLiteral("accent"));
    CHECK(drawn[0].second.value(QStringLiteral("width")).toDouble() == doctest::Approx(0.2));

    rig.press(kSpace);
    REQUIRE(rig.until([&] { return drawn.size() == 2 && erased.size() == 1; }));
    CHECK(drawn[1].second.value(QStringLiteral("kind")).toString() == QStringLiteral("text"));
    CHECK(erased[0] == drawn[1].first);

    logic.runtime.reload();
    CHECK(cleared == 1);
    REQUIRE(rig.until([&] { return logic.loads == 2 && drawn.size() == 3; }));
    CHECK(drawn[2].first != drawn[0].first);
    CHECK(logic.failures.isEmpty());
}

TEST_CASE("INP-012: the reveal of all key hints a logic or a task left on is turned off when it reloads or stops")
{
    const Application app;
    const Store store;
    const LogicFolder folder(R"(controls.jump.onPress(() => {
    hints.reveal(true);
});
controls.fire.onPress(() => {
    hints.reveal(false);
});
)");
    const LogicFolder task("");
    task.text(QStringLiteral("task.ts"), R"(export default class Peek extends Task {
    async run() {
        hints.reveal(true);
        log.info("peeking");
        await sleep(60000);
    }
}
)");
    Setup setup;
    Rig& rig = setup.rig;
    const QString on = QStringLiteral(R"({"reveal":true})");
    const QString off = QStringLiteral(R"({"reveal":false})");
    QStringList asked;
    std::optional<Logic> logic;
    logic.emplace(rig, setup.phone, folder, setup.profile, QStringLiteral("reveal"), store);
    QObject::connect(&logic->runtime, &ScriptRuntime::appRequested,
                     [&asked](const QString& request, const QString& json) {
                         if (request == u"hints") {
                             asked.append(json);
                         }
                     });
    REQUIRE(rig.until([&] { return logic->loads == 1; }));

    rig.press(kSpace);
    REQUIRE(rig.until([&] { return asked.size() == 1; }));
    rig.press(kKeyF);
    REQUIRE(rig.until([&] { return asked.size() == 2; }));
    logic->runtime.reload();
    CHECK(asked == QStringList{on, off});
    REQUIRE(rig.until([&] { return logic->loads == 2; }));

    rig.press(kSpace);
    REQUIRE(rig.until([&] { return asked.size() == 3; }));
    logic->runtime.reload();
    CHECK(asked == QStringList{on, off, on, off});
    REQUIRE(rig.until([&] { return logic->loads == 3; }));

    rig.press(kSpace);
    REQUIRE(rig.until([&] { return asked.size() == 5; }));
    logic.reset();
    CHECK(asked == QStringList{on, off, on, off, on, off});

    QStringList taskAsked;
    {
        ScriptRuntime running(LogicSource{.folder = task.path(),
                                          .entry = "task.ts",
                                          .profile = setup.profile,
                                          .id = QStringLiteral("peek"),
                                          .name = QStringLiteral("peek"),
                                          .device = QStringLiteral("test phone"),
                                          .deviceKey = QStringLiteral("test-phone"),
                                          .task = true,
                                          .gestureOffset = std::uint64_t{1} << 56},
                              std::make_shared<TaskLane>(std::make_shared<FakeLane>(rig, setup.phone.channel)),
                              store.folder());
        QObject::connect(&running, &ScriptRuntime::appRequested,
                         [&taskAsked](const QString& request, const QString& json) {
                             if (request == u"hints") {
                                 taskAsked.append(json);
                             }
                         });
        REQUIRE(rig.until([&] {
            return taskAsked.size() == 1 && setup.lines.has(QStringLiteral("peek"), QStringLiteral("peeking"));
        }));
        CHECK(taskAsked == QStringList{on});
    }
    CHECK(taskAsked == QStringList{on, off});
}

TEST_CASE("a store save that another program blocks is reported and the next save writes everything (ADR-0058)")
{
    const Application app;
    const Store store;
    const LogicFolder folder(kCountLogic);
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("blocked");
    Logic logic(rig, setup.phone, folder, setup.profile, id, store);
    REQUIRE(rig.until([&] { return logic.loads == 1; }));
    rig.press(kSpace);
    REQUIRE(rig.until([&] { return store.number(id, QStringLiteral("presses")) == 1; }));

    const std::wstring path = store.path(id).toStdWString();
    HANDLE reader = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(reader != INVALID_HANDLE_VALUE);
    rig.press(kSpace);
    REQUIRE(rig.until([&] { return logic.busy == 1; }));
    CHECK(store.number(id, QStringLiteral("presses")) == 1);
    ::CloseHandle(reader);

    rig.press(kSpace);
    REQUIRE(rig.until([&] { return store.number(id, QStringLiteral("presses")) == 3; }));
    CHECK(logic.busy == 1);
    CHECK(logic.runtime.running());
    CHECK(logic.failures.isEmpty());
}

TEST_CASE("logic hears the phone's screen turn, and facts that keep the rotation are not a turn (ADR-0091)")
{
    const Application app;
    const Store store;
    const LogicFolder folder("let turns = 0;\n"
                             "device.onRotate(({ rotation, width, height }) => {\n"
                             "    turns += 1;\n"
                             "    log.info(\"turned\", turns, rotation, width, height);\n"
                             "});\n"
                             "log.info(\"ready\");\n");
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("turning");
    Logic logic(rig, setup.phone, folder, setup.profile, id, store);
    REQUIRE(rig.until([&] { return setup.lines.has(id, QStringLiteral("ready")); }));
    logic.runtime.setDevice({.model = "Pixel test", .android = 15, .width = 1080, .height = 2400});
    logic.runtime.setDevice({.model = "Pixel test", .android = 15, .width = 2400, .height = 1080, .rotation = 1});
    REQUIRE(rig.until([&] { return setup.lines.has(id, QStringLiteral("turned 1 1 2400 1080")); }));
    logic.runtime.setDevice({.model = "Pixel test", .android = 15, .width = 2400, .height = 1080, .rotation = 1});
    logic.runtime.setDevice({.model = "Pixel test", .android = 15, .width = 1080, .height = 2400});
    REQUIRE(rig.until([&] { return setup.lines.has(id, QStringLiteral("turned 2 0 1080 2400")); }));
    CHECK(logic.failures.isEmpty());
}

TEST_CASE("logic finds a picture in the next frame, in the still picture when none comes, and nothing without one (ADR-0083)")
{
    const Application app;
    const Store store;
    const LogicFolder folder(kVisionLogic);
    const FramePtr picture = noisyPicture(240, 160);
    QImage patch(32, 24, QImage::Format_Grayscale8);
    for (int y = 0; y < patch.height(); ++y) {
        for (int x = 0; x < patch.width(); ++x) {
            patch.scanLine(y)[x] = picture->data[0][(60 + y) * picture->linesize[0] + 100 + x];
        }
    }
    folder.put(QStringLiteral("res/patch.png"), patch);
    QImage flat(16, 16, QImage::Format_RGB32);
    flat.fill(qRgb(90, 90, 90));
    folder.put(QStringLiteral("res/flat.png"), flat);
    QImage clear(16, 16, QImage::Format_ARGB32);
    clear.fill(qRgba(0, 0, 0, 0));
    folder.put(QStringLiteral("res/clear.png"), clear);

    const auto snapshots = std::make_shared<media::FrameSnapshots>();
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("seeing");
    Logic logic(rig, setup.phone, folder, setup.profile, id, store, QStringLiteral("test-phone"), snapshots);
    REQUIRE(rig.until([&] { return logic.loads == 1; }));

    snapshots->offer(*picture, 1000, 1);
    rig.press(kSpace);
    REQUIRE(rig.until([&] { return setup.lines.has(id, QStringLiteral("still 0.4833 0.4500 0.1333 0.1500 sure")); }));
    CHECK(snapshots->counters().captured == 1);

    std::int64_t pts = 1000;
    rig.press(kKeyF);
    REQUIRE(rig.until([&] {
        pts += 1000;
        snapshots->offer(*picture, pts, 1);
        return setup.lines.has(id, QStringLiteral("bad GS-API-016 GS-API-016 GS-API-016"));
    }));
    CHECK(setup.lines.has(id, QStringLiteral("fresh 1 0.4833 0.4500 0.1333 0.1500 sure")));
    CHECK(setup.lines.has(id, QStringLiteral("elsewhere 0")));
    CHECK(snapshots->counters().captured == 1);

    snapshots->forget();
    rig.press(kKeyR);
    REQUIRE(rig.until([&] { return setup.lines.has(id, QStringLiteral("blank none 0")); }));
    CHECK(snapshots->counters().captured == 1);
    CHECK(logic.failures.isEmpty());
}

TEST_CASE("logic finds a named template in color and a named color with findColor in a color frame (VIS-004)")
{
    const Application app;
    const Store store;
    const LogicFolder folder(kNamedVisionLogic);
    const FramePtr picture = buttonsPicture(240, 160);
    media::FrameSnapshots local;
    local.request(true);
    local.offer(*picture, 1, 1);
    const std::shared_ptr<const media::LumaSnapshot> shot = local.latest();
    REQUIRE(shot != nullptr);
    REQUIRE(shot->colored());
    const QImage seen = pictureOf(*shot);
    REQUIRE(seen.size() == QSize(240, 160));
    folder.put(QStringLiteral("res/红按钮.png"), seen.copy(48, 40, 48, 24));
    const std::vector<std::uint8_t> face = media::rgbOf(*shot, 144, 96, 1, 1);
    REQUIRE(face.size() == 3);
    const std::string blue = script::colorText({face[0], face[1], face[2]});
    folder.text(QStringLiteral("res/resources.json"),
                R"({"format": "gamescrcpy.resources", "version": 1,
                    "templates": [{"name": "红按钮", "file": "红按钮.png", "threshold": 0.9, "match": "color"}],
                    "colors": [{"name": "蓝色", "color": ")" + blue + R"(", "tolerance": 0.05}]})");

    const auto snapshots = std::make_shared<media::FrameSnapshots>();
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("colors");
    Logic logic(rig, setup.phone, folder, setup.profile, id, store, QStringLiteral("test-phone"), snapshots);
    REQUIRE(rig.until([&] { return logic.loads == 1; }));

    std::int64_t pts = 1000;
    rig.press(kSpace);
    REQUIRE(rig.until([&] {
        pts += 1000;
        snapshots->offer(*picture, pts, 1);
        return setup.lines.has(
            id, QStringLiteral("named 0.3000 0.3250 0.2000 0.1500 sure 2 0.7000 0.6750 0.2000 0.1500 0.80 true"));
    }));
    CHECK(logic.failures.isEmpty());
}

TEST_CASE("logic reads the text of a frame and finds where a line says something (ADR-0093)")
{
    const std::vector<std::string> languages = platform::textLanguages();
    if (std::ranges::none_of(languages, [](const std::string& tag) { return tag.starts_with("en"); })) {
        MESSAGE("no English text recognition on this computer");
        return;
    }
    const Application app;
    const Store store;
    const LogicFolder folder(kTextLogic);
    const FramePtr picture = textPicture(480, 240, L"Battery\nSearch settings");
    const auto snapshots = std::make_shared<media::FrameSnapshots>();
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("reading");
    Logic logic(rig, setup.phone, folder, setup.profile, id, store, QStringLiteral("test-phone"), snapshots);
    REQUIRE(rig.until([&] { return logic.loads == 1; }));

    std::int64_t pts = 1000;
    rig.press(kSpace);
    REQUIRE(rig.until([&] {
        pts += 1000;
        snapshots->offer(*picture, pts, 1);
        return setup.lines.has(id, QStringLiteral("missing GS-API-039"));
    }));
    CHECK(setup.lines.has(id, QStringLiteral("read battery|searchsettings")));
    CHECK(setup.lines.has(id, QStringLiteral("found true")));
    CHECK(setup.lines.has(id, QStringLiteral("right true")));
    CHECK(logic.failures.isEmpty());
}

TEST_CASE("a task's fingers and the profile logic's fingers on one phone stay apart, and a stopped task lifts its own (ADR-0091)")
{
    const Application app;
    const Store store;
    const LogicFolder logic(R"(touch.down(0.2, 0.2);
log.info("logic holds");
)");
    const LogicFolder task("");
    task.text(QStringLiteral("task.ts"), R"(export default class Hold extends Task {
    async run() {
        touch.down(0.8, 0.8);
        log.info("task holds");
        await sleep(60000);
    }
}
)");
    Setup setup;
    Rig& rig = setup.rig;
    Logic profileLogic(rig, setup.phone, logic, setup.profile, QStringLiteral("hold"), store);
    REQUIRE(rig.until([&] { return downs(setup.phone.sink, true) == 1; }));
    {
        ScriptRuntime running(LogicSource{.folder = task.path(),
                                          .entry = "task.ts",
                                          .profile = setup.profile,
                                          .id = QStringLiteral("task-hold"),
                                          .name = QStringLiteral("Hold"),
                                          .device = QStringLiteral("test phone"),
                                          .deviceKey = QStringLiteral("test-phone"),
                                          .task = true,
                                          .gestureOffset = std::uint64_t{1} << 56},
                              std::make_shared<TaskLane>(std::make_shared<FakeLane>(rig, setup.phone.channel)),
                              store.folder());
        REQUIRE(rig.until([&] { return downs(setup.phone.sink, true) == 2; }));
        std::vector<std::int32_t> pointers;
        for (const protocol::TouchPoint& point : pointsOf(setup.phone.sink, true)) {
            if (point.action == protocol::TouchAction::Down) {
                pointers.push_back(point.pointerId);
            }
        }
        REQUIRE(pointers.size() == 2);
        CHECK(pointers[0] != pointers[1]);
    }
    REQUIRE(rig.until([&] {
        const std::vector<protocol::TouchPoint> points = pointsOf(setup.phone.sink, true);
        return std::ranges::count(points, protocol::TouchAction::Up, &protocol::TouchPoint::action) == 1;
    }));
    rig.settle(50ms);
    const std::vector<protocol::TouchPoint> points = pointsOf(setup.phone.sink, true);
    CHECK(std::ranges::count(points, protocol::TouchAction::Up, &protocol::TouchPoint::action) == 1);
    CHECK(profileLogic.runtime.running());
}

TEST_CASE("a task hears the phone's components and its own keys without taking them, and its controls follow a new "
          "profile (ADR-0091)")
{
    const Application app;
    const Store store;
    const LogicFolder task("");
    task.text(QStringLiteral("task.ts"), R"(export default class Watch extends Task {
    async run() {
        keys.R.onPress(() => log.info("key R"));
        controls.jump.onPress(() => log.info("jump pressed"));
        log.info("ready", Object.keys(controls).join(","));
        while (controls.fire !== undefined) {
            await sleep(20);
        }
        log.info("switched", Object.keys(controls).join(","), controls.jump.type, Object.keys(vars).length);
        await sleep(60000);
    }
}
)");
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("watching");
    ScriptRuntime running(LogicSource{.folder = task.path(),
                                      .entry = "task.ts",
                                      .profile = setup.profile,
                                      .id = id,
                                      .name = id,
                                      .device = QStringLiteral("test phone"),
                                      .deviceKey = QStringLiteral("test-phone"),
                                      .task = true,
                                      .gestureOffset = std::uint64_t{1} << 56},
                          std::make_shared<TaskLane>(std::make_shared<FakeLane>(rig, setup.phone.channel)),
                          store.folder());
    REQUIRE(rig.until([&] { return setup.lines.has(id, QStringLiteral("ready jump,fire,probe")); }));
    rig.settle(50ms);
    rig.press(kKeyR);
    REQUIRE(rig.until([&] { return setup.lines.has(id, QStringLiteral("key R")); }));
    CHECK(downs(setup.phone.sink, false) == 1);
    rig.press(kSpace);
    REQUIRE(rig.until([&] { return setup.lines.has(id, QStringLiteral("jump pressed")); }));

    const auto other = profileOf(R"({"format":"gamescrcpy.profile","version":3,
    "controls":[{"id":"jump","type":"tap","inputs":{"press":"Space"}}]})");
    rig.host.setProfile(other, rig.now());
    REQUIRE(rig.until([&] { return setup.lines.has(id, QStringLiteral("switched jump tap 0")); }));
}

TEST_CASE("a task's profile.current() names the phone's profile and follows it to the next one (ADR-0091)")
{
    const Application app;
    const Store store;
    const LogicFolder task("");
    task.text(QStringLiteral("task.ts"), R"(export default class Follow extends Task {
    async run() {
        log.info("profile " + profile.current()?.name);
        while (controls.fire !== undefined) {
            await sleep(20);
        }
        log.info("now " + profile.current()?.id + " " + profile.current()?.name);
        await sleep(60000);
    }
}
)");
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("following");
    ScriptRuntime running(LogicSource{.folder = task.path(),
                                      .entry = "task.ts",
                                      .profile = setup.profile,
                                      .id = id,
                                      .name = id,
                                      .device = QStringLiteral("test phone"),
                                      .deviceKey = QStringLiteral("test-phone"),
                                      .task = true,
                                      .gestureOffset = std::uint64_t{1} << 56},
                          std::make_shared<TaskLane>(std::make_shared<FakeLane>(rig, setup.phone.channel)),
                          store.folder());
    running.setProfileFact(QStringLiteral("shooter"), QStringLiteral("Shooter"));
    REQUIRE(rig.until([&] { return setup.lines.has(id, QStringLiteral("profile Shooter")); }));
    CHECK_FALSE(setup.lines.has(id, QStringLiteral("profile following")));

    running.setProfileFact(QStringLiteral("racer"), QStringLiteral("Racer"));
    const auto other = profileOf(R"({"format":"gamescrcpy.profile","version":3,
    "controls":[{"id":"jump","type":"tap","inputs":{"press":"Space"}}]})");
    rig.host.setProfile(other, rig.now());
    REQUIRE(rig.until([&] { return setup.lines.has(id, QStringLiteral("now racer Racer")); }));
}

TEST_CASE("the logic's snapshot lists the components with their live state and the variables with their values (ADR-0091)")
{
    const Application app;
    const Store store;
    const LogicFolder folder(R"(vars.aiming.value = true;
log.info("set");
)");
    Setup setup;
    Rig& rig = setup.rig;
    Logic logic(rig, setup.phone, folder, setup.profile, QStringLiteral("snapshot"), store);
    REQUIRE(rig.until([&] { return setup.lines.has(QStringLiteral("snapshot"), QStringLiteral("set")); }));
    const QVariantMap snapshot = logic.runtime.snapshot();
    const QVariantList components = snapshot.value(QStringLiteral("components")).toList();
    REQUIRE(components.size() == 3);
    const QVariantMap jump = components[0].toMap();
    CHECK(jump.value(QStringLiteral("id")).toString() == QStringLiteral("jump"));
    CHECK(jump.value(QStringLiteral("type")).toString() == QStringLiteral("hold"));
    CHECK(jump.value(QStringLiteral("enabled")).toBool());
    CHECK_FALSE(jump.value(QStringLiteral("touching")).toBool());
    CHECK_FALSE(jump.value(QStringLiteral("moving")).toBool());
    const QVariantList vars = snapshot.value(QStringLiteral("vars")).toList();
    REQUIRE(vars.size() == 1);
    CHECK(vars[0].toMap().value(QStringLiteral("id")).toString() == QStringLiteral("aiming"));
    CHECK(vars[0].toMap().value(QStringLiteral("value")) == QVariant(true));
}

TEST_CASE("a pixel of a color frame reads as #RRGGBB and matches a color within the tolerance (ADR-0091)")
{
    const FramePtr picture = buttonsPicture(240, 160);
    media::FrameSnapshots local;
    local.request(true);
    local.offer(*picture, 1, 1);
    const std::shared_ptr<const media::LumaSnapshot> shot = local.latest();
    REQUIRE(shot != nullptr);
    REQUIRE(shot->colored());
    const std::vector<std::uint8_t> face = media::rgbOf(*shot, 147, 99, 1, 1);
    REQUIRE(face.size() == 3);
    const vision::Rgb seen{face[0], face[1], face[2]};
    const std::string hex = script::colorText({face[0], face[1], face[2]});
    const script::Point at{0.6125, 0.61875};
    CHECK(pixelAt(*shot, at) == seen);
    CHECK(pixelJson(*shot, {.at = at}) == "\"" + hex + "\"");
    script::PixelQuery match{
        .at = at, .color = hex, .rgb = {face[0], face[1], face[2]}, .tolerance = 0.0, .match = true};
    CHECK(pixelJson(*shot, match) == "true");
    match.rgb.red = static_cast<std::uint8_t>(face[0] ^ 0x80);
    CHECK(pixelJson(*shot, match) == "false");
    match.tolerance = 0.6;
    CHECK(pixelJson(*shot, match) == "true");
    CHECK(pixelAt(*shot, {1.0, 1.0}) == pixelAt(*shot, {239.5 / 240.0, 159.5 / 160.0}));

    const media::LumaSnapshot grey{.width = 4, .height = 4, .luma = std::vector<std::uint8_t>(16, 120)};
    CHECK_FALSE(pixelAt(grey, at).has_value());
    CHECK(pixelJson(grey, {.at = at}) == "null");
    CHECK(pixelJson(grey, match) == "false");
}

TEST_CASE("logic reads a pixel's color and matches it against colors in a color frame (ADR-0091)")
{
    const Application app;
    const Store store;
    const FramePtr picture = buttonsPicture(240, 160);
    media::FrameSnapshots local;
    local.request(true);
    local.offer(*picture, 1, 1);
    const std::shared_ptr<const media::LumaSnapshot> shot = local.latest();
    REQUIRE(shot != nullptr);
    const std::vector<std::uint8_t> face = media::rgbOf(*shot, 147, 99, 1, 1);
    REQUIRE(face.size() == 3);
    const std::string hex = script::colorText({face[0], face[1], face[2]});
    const LogicFolder folder("const at = point(0.6125, 0.61875);\n"
                             "controls.jump.onPress(async () => {\n"
                             "    const color = await vision.pickColor(at);\n"
                             "    const same = await vision.matchColor(at, \"" + hex + "\", { tolerance: 0 });\n"
                             "    const green = await vision.matchColor(at, \"#00FF00\", { tolerance: 0.05 });\n"
                             "    log.info(\"pixel\", color, same, green);\n"
                             "});\n");

    const auto snapshots = std::make_shared<media::FrameSnapshots>();
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("pixels");
    Logic logic(rig, setup.phone, folder, setup.profile, id, store, QStringLiteral("test-phone"), snapshots);
    REQUIRE(rig.until([&] { return logic.loads == 1; }));

    std::int64_t pts = 1000;
    rig.press(kSpace);
    REQUIRE(rig.until([&] {
        pts += 1000;
        snapshots->offer(*picture, pts, 1);
        return setup.lines.has(id, QStringLiteral("pixel %1 true false").arg(QString::fromStdString(hex)));
    }));
    CHECK(logic.failures.isEmpty());
}

TEST_CASE("logic hears the mirror window gain and lose the focus, and window.focused() says which (ADR-0091)")
{
    const Application app;
    const Store store;
    const LogicFolder folder("let focus = 0;\n"
                             "let blur = 0;\n"
                             "window.onFocus(() => {\n"
                             "    focus += 1;\n"
                             "    log.info(\"focused\", focus, window.focused());\n"
                             "});\n"
                             "window.onBlur(() => {\n"
                             "    blur += 1;\n"
                             "    log.info(\"blurred\", blur, window.focused());\n"
                             "});\n"
                             "log.info(\"ready\", window.focused());\n");
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("focusing");
    Logic logic(rig, setup.phone, folder, setup.profile, id, store);
    REQUIRE(rig.until([&] { return setup.lines.has(id, QStringLiteral("ready false")); }));
    logic.runtime.setFocused(true);
    logic.runtime.setFocused(true);
    REQUIRE(rig.until([&] { return setup.lines.has(id, QStringLiteral("focused 1 true")); }));
    logic.runtime.setFocused(false);
    REQUIRE(rig.until([&] { return setup.lines.has(id, QStringLiteral("blurred 1 false")); }));
    CHECK_FALSE(setup.lines.has(id, QStringLiteral("focused 2 true")));
    CHECK(logic.failures.isEmpty());
}

TEST_CASE("logic retunes a component on the phone's engine, offers the change to profile.save and puts it back when "
          "it stops (ADR-0091)")
{
    const Application app;
    const Store store;
    const LogicFolder folder("controls.jump.moveTo(0.5, 0.5);\n"
                             "controls.probe.setDuration(80);\n"
                             "log.info(\"tuned\", controls.jump.position.x.toFixed(2), "
                             "controls.jump.position.y.toFixed(2), controls.probe.tapMs);\n"
                             "profile.save().then((saved) => log.info(\"saved\", saved));\n");
    Setup setup;
    Rig& rig = setup.rig;
    const QString id = QStringLiteral("tuning");
    std::optional<Logic> logic;
    logic.emplace(rig, setup.phone, folder, setup.profile, id, store);
    logic->runtime.setDevice({.model = "Pixel test", .android = 15, .width = 2400, .height = 1080});
    std::optional<std::tuple<quint64, QVariantList, std::shared_ptr<const input::Profile>>> wanted;
    QObject::connect(&logic->runtime, &ScriptRuntime::saveWanted,
                     [&wanted](quint64 request, const QVariantList& changes,
                               std::shared_ptr<const input::Profile> profile) {
                         wanted.emplace(request, changes, std::move(profile));
                     });
    REQUIRE(rig.until([&] { return wanted.has_value(); }));
    CHECK(setup.lines.has(id, QStringLiteral("tuned 0.50 0.50 80")));
    const auto& [request, changes, saved] = *wanted;
    REQUIRE(changes.size() == 2);
    CHECK(changes[0].toMap() == QVariantMap{{QStringLiteral("name"), QStringLiteral("jump")},
                                            {QStringLiteral("prop"), QStringLiteral("position")},
                                            {QStringLiteral("before"), QStringLiteral("0.91, 0.80")},
                                            {QStringLiteral("after"), QStringLiteral("0.50, 0.50")}});
    CHECK(changes[1].toMap() == QVariantMap{{QStringLiteral("name"), QStringLiteral("probe")},
                                            {QStringLiteral("prop"), QStringLiteral("duration")},
                                            {QStringLiteral("before"), QStringLiteral("0")},
                                            {QStringLiteral("after"), QStringLiteral("80")}});
    REQUIRE(saved != nullptr);
    CHECK(std::get<input::TapControl>(saved->controls[2].kind).tapMs == 80);
    CHECK(saved->controls[1] == setup.profile->controls[1]);
    logic->runtime.answer(request, QStringLiteral("true"));
    REQUIRE(rig.until([&] { return setup.lines.has(id, QStringLiteral("saved true")); }));

    const auto lastDown = [&setup] {
        const std::vector<protocol::TouchPoint> points = pointsOf(setup.phone.sink, false);
        for (auto point = points.rbegin(); point != points.rend(); ++point) {
            if (point->action == protocol::TouchAction::Down) {
                return *point;
            }
        }
        return protocol::TouchPoint{};
    };
    const auto around = [](std::uint16_t value, double ratio) {
        return std::abs(static_cast<int>(value) - static_cast<int>(input::fixedCoordinate(ratio))) <= 8;
    };
    rig.press(kSpace);
    REQUIRE(rig.until([&] { return downs(setup.phone.sink, false) == 1; }));
    CHECK(around(lastDown().x, 0.5));
    CHECK(around(lastDown().y, 0.5));
    CHECK(logic->failures.isEmpty());

    logic.reset();
    rig.settle(100ms);
    rig.press(kSpace);
    REQUIRE(rig.until([&] { return downs(setup.phone.sink, false) == 2; }));
    CHECK(around(lastDown().x, 0.91));
    CHECK(around(lastDown().y, 0.8));
}
