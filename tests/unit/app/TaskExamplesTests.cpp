#include "McpTestSupport.h"
#include "TaskLibrary.h"

#include <gs/base/Clock.h>
#include <gs/base/Log.h>
#include <gs/protocol/Codec.h>
#include <gs/protocol/Constants.h>
#include <gs/record/MediaFile.h>
#include <gs/record/RecordedVision.h>
#include <gs/script/ApiFiles.h>
#include <gs/script/Replay.h>
#include <gs/script/Report.h>
#include <gs/script/ScriptHost.h>
#include <gs/script/Services.h>
#include <gs/script/Session.h>

#include <doctest/doctest.h>

#include <QByteArray>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using gs::Microseconds;
using gs::app::TaskEntry;
using gs::app::TaskExample;
using gs::app::TaskLibrary;

namespace {

namespace fs = std::filesystem;
namespace script = gs::script;

constexpr double kTolerance = 0.01;

struct Tap {
    double x = 0.0;
    double y = 0.0;
    Microseconds at{0};
};

struct Logged {
    Microseconds at{0};
    gs::log::Level level = gs::log::Level::Info;
    std::string message;
};

struct Replayed {
    bool loaded = false;
    bool reachedLimit = false;
    std::vector<script::ReplayLine> outputs;
    std::vector<Logged> logs;
    std::vector<std::string> failures;
};

fs::path pathOf(const QString& text)
{
    return {text.toStdWString()};
}

fs::path pathOf(const QTemporaryDir& dir)
{
    return pathOf(dir.path());
}

fs::path exampleTasks()
{
    return pathOf(QString::fromUtf8(GS_EXAMPLE_TASKS_DIR));
}

fs::path exampleComponents()
{
    return pathOf(QString::fromUtf8(GS_EXAMPLE_COMPONENTS_DIR));
}

fs::path demoRecording()
{
    return pathOf(QString::fromUtf8(GS_VIDEO_TEST_DATA_DIR)) / "emulator_h265.media";
}

std::string readBytes(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string readLines(const fs::path& file)
{
    std::string lines = readBytes(file);
    std::erase(lines, '\r');
    return lines;
}

std::optional<std::int64_t> firstFramePts(const fs::path& recording)
{
    gs::record::MediaReader reader(recording, gs::protocol::MediaChannel::Video);
    for (std::optional<gs::protocol::MediaHeader> header = reader.peek(); header; header = reader.peek()) {
        if (header->kind == gs::protocol::MediaKind::Frame) {
            return header->ptsUs;
        }
        reader.skip();
    }
    return std::nullopt;
}

bool failing(script::ReportKind kind)
{
    return kind == script::ReportKind::ScriptFailed || kind == script::ReportKind::TaskFailed ||
           kind == script::ReportKind::TaskInterrupted || kind == script::ReportKind::UnhandledRejection ||
           kind == script::ReportKind::MemoryLimit;
}

Replayed replayed(const fs::path& folder, const std::string& entry, std::vector<script::SessionEvent> events,
                  Microseconds until)
{
    const std::optional<std::int64_t> start = firstFramePts(demoRecording());
    REQUIRE(start.has_value());
    gs::ManualClock clock;
    script::RecordingServices services(clock, script::DeviceInfo{"Emulator", 15, 288, 640, 0}, {}, {});
    gs::record::RecordedVision vision(folder, demoRecording(), *start);
    REQUIRE(vision.opened());
    services.setVision(&vision);
    Replayed result;
    const script::ScriptHost::Sink sink = [&result, &clock](const script::Report& report) {
        if (report.kind == script::ReportKind::Log) {
            result.logs.push_back({.at = clock.now(), .level = report.level, .message = report.message});
        } else if (failing(report.kind)) {
            result.failures.push_back(script::describe(report));
        }
    };
    script::ReplayOptions options;
    options.source = script::ScriptSource{.folder = folder, .entry = entry};
    options.events = std::move(events);
    options.until = until;
    const script::ReplayResult ran = script::replay(options, services, clock, sink);
    result.loaded = ran.loaded;
    result.reachedLimit = ran.reachedLimit;
    result.outputs = services.outputs();
    return result;
}

Replayed ranTask(const fs::path& folder, Microseconds until)
{
    return replayed(folder, "task.ts",
                    {script::SessionEvent{Microseconds{0}, script::SessionEventKind::Run, "default", false}}, until);
}

std::vector<Tap> tapsIn(const std::vector<script::ReplayLine>& lines)
{
    std::vector<Tap> taps;
    for (const script::ReplayLine& line : lines) {
        if (!line.text.starts_with("tap ")) {
            continue;
        }
        std::istringstream fields(line.text.substr(4));
        Tap tap{.at = line.at};
        fields >> tap.x >> tap.y;
        if (fields) {
            taps.push_back(tap);
        }
    }
    return taps;
}

std::size_t looksFor(const std::vector<script::ReplayLine>& lines, const std::string& picture)
{
    const std::string wanted = "vision find " + picture;
    return static_cast<std::size_t>(
        std::ranges::count_if(lines, [&wanted](const script::ReplayLine& line) { return line.text == wanted; }));
}

const Logged* logged(const Replayed& run, const std::string& message)
{
    const auto found = std::ranges::find_if(run.logs, [&message](const Logged& line) { return line.message == message; });
    return found != run.logs.end() ? &*found : nullptr;
}

std::string transcriptOf(const Replayed& run)
{
    std::string text;
    for (const script::ReplayLine& line : run.outputs) {
        text += script::formatLine(line) + '\n';
    }
    for (const Logged& line : run.logs) {
        text += "log " + line.message + '\n';
    }
    for (const std::string& failure : run.failures) {
        text += "failed " + failure + '\n';
    }
    return text;
}

bool within(double actual, double expected)
{
    return std::abs(actual - expected) < kTolerance;
}

}

TEST_CASE("ADR-0105: the examples shelf lists the four example tasks with their titles, descriptions and settings, "
          "apart from the user's tasks")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    TaskLibrary library(pathOf(root));
    CHECK(library.examples().isEmpty());
    library.setExamplesFolder(exampleTasks());
    CHECK(library.tasks().isEmpty());
    CHECK(library.entries().empty());
    const QVariantList examples = library.examples();
    REQUIRE(examples.size() == 4);
    QStringList ids;
    for (const QVariant& each : examples) {
        const QVariantMap example = each.toMap();
        const std::string id = example.value(QStringLiteral("id")).toString().toStdString();
        CAPTURE(id);
        ids.append(example.value(QStringLiteral("id")).toString());
        CHECK_FALSE(example.value(QStringLiteral("name")).toString().isEmpty());
        CHECK_FALSE(example.value(QStringLiteral("description")).toString().isEmpty());
        CHECK_FALSE(example.value(QStringLiteral("icon")).toString().isEmpty());
        CHECK_FALSE(example.value(QStringLiteral("params")).toList().isEmpty());
    }
    CHECK(ids == QStringList{QStringLiteral("examples/find-and-tap"), QStringLiteral("examples/wait-then-tap"),
                             QStringLiteral("examples/tap-in-order"), QStringLiteral("examples/daily-check-in")});

    const TaskExample* find = library.findExample(QStringLiteral("examples/find-and-tap"));
    REQUIRE(find != nullptr);
    CHECK(find->name == QStringLiteral("Find a picture and tap it"));
    CHECK(find->icon == QStringLiteral("search"));
    CHECK(find->schedule == QStringLiteral("manual"));
    REQUIRE(find->params.size() == 2);
    CHECK(find->params[0].name == QStringLiteral("picture"));
    CHECK(find->params[0].kind == QStringLiteral("text"));
    CHECK(find->params[0].value.toString() == QStringLiteral("res/notifications.png"));
    CHECK(find->params[0].label == QStringLiteral("Picture"));
    CHECK(find->params[1].name == QStringLiteral("searchMs"));
    CHECK(find->params[1].kind == QStringLiteral("number"));
    CHECK(find->params[1].value.toDouble() == 5000.0);
    REQUIRE(find->params[1].min.has_value());
    CHECK(*find->params[1].min == 100.0);

    const TaskExample* wait = library.findExample(QStringLiteral("examples/wait-then-tap"));
    REQUIRE(wait != nullptr);
    CHECK(wait->name == QStringLiteral("Wait for a picture, then tap"));
    REQUIRE(wait->params.size() == 5);
    CHECK(wait->params[0].value.toString() == QStringLiteral("res/network.png"));
    CHECK(wait->params[1].value.toDouble() == 10000.0);
    CHECK(wait->params[2].kind == QStringLiteral("toggle"));
    CHECK(wait->params[2].value == QVariant(true));
    CHECK(wait->params[3].name == QStringLiteral("atX"));
    CHECK(wait->params[4].name == QStringLiteral("atY"));
    REQUIRE(wait->params[4].max.has_value());
    CHECK(*wait->params[4].max == 100.0);

    const TaskExample* order = library.findExample(QStringLiteral("examples/tap-in-order"));
    REQUIRE(order != nullptr);
    CHECK(order->name == QStringLiteral("Tap places in order"));
    REQUIRE(order->params.size() == 3);
    CHECK(order->params[0].value.toString() == QStringLiteral("35,32; 40,61; 24,70"));
    CHECK(order->params[1].value.toDouble() == 1000.0);
    CHECK(order->params[2].value.toDouble() == 1.0);

    const TaskExample* checkIn = library.findExample(QStringLiteral("examples/daily-check-in"));
    REQUIRE(checkIn != nullptr);
    CHECK(checkIn->name == QStringLiteral("Daily check-in template"));
    CHECK(checkIn->schedule == QStringLiteral("daily"));
    CHECK(checkIn->time == QStringLiteral("09:00"));
    REQUIRE(checkIn->params.size() == 4);
    CHECK(checkIn->params[0].value.toString() == QStringLiteral("com.android.settings"));
    CHECK(checkIn->params[1].value.toString() == QStringLiteral("res/check-in.png"));
    CHECK(checkIn->params[2].value.toString() == QStringLiteral("res/done.png"));
    CHECK(checkIn->params[3].value.toDouble() == 30000.0);

    for (const TaskExample& example : library.exampleTasks()) {
        const std::string id = example.id.toStdString();
        CAPTURE(id);
        CHECK(readLines(pathOf(example.folder) / "tsconfig.json") == gs::script::profileTsconfig());
        for (const gs::app::TaskParamSpec& spec : example.params) {
            const QString value = spec.value.toString();
            if (spec.kind == QStringLiteral("text") && value.endsWith(QStringLiteral(".png"))) {
                CHECK(fs::is_regular_file(pathOf(example.folder) / pathOf(value)));
            }
        }
    }
    CHECK(library.findExample(QStringLiteral("find-and-tap")) == nullptr);
    CHECK(library.find(QStringLiteral("examples/find-and-tap")) == nullptr);

    TaskLibrary bare(pathOf(root));
    bare.setExamplesFolder(pathOf(root) / "missing");
    CHECK(bare.examples().isEmpty());
    CHECK(bare.exampleTasks().empty());
}

TEST_CASE("ADR-0105: choosing an example copies it to the user's tasks as a normal task with the schedule off and "
          "its pictures")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    QString id;
    {
        TaskLibrary library(pathOf(root));
        library.setExamplesFolder(exampleTasks());
        id = library.copyExample(QStringLiteral("examples/daily-check-in"));
        CHECK(id == QStringLiteral("daily-check-in"));
        const TaskEntry* entry = library.find(id);
        REQUIRE(entry != nullptr);
        CHECK_FALSE(entry->readOnly);
        CHECK(entry->name == QStringLiteral("Daily check-in template"));
        CHECK(entry->schedule == QStringLiteral("manual"));
        CHECK(entry->time == QStringLiteral("09:00"));
        CHECK(entry->example == QStringLiteral("examples/daily-check-in"));
        CHECK(entry->params.size() == 4);
        const fs::path copied = pathOf(entry->folder);
        const fs::path example = exampleTasks() / "daily-check-in";
        for (const char* file : {"task.ts", "tsconfig.json", "res/check-in.png", "res/done.png"}) {
            CAPTURE(file);
            const std::string bytes = readBytes(copied / file);
            CHECK_FALSE(bytes.empty());
            CHECK(bytes == readBytes(example / file));
        }
        REQUIRE(library.tasks().size() == 1);
        const QVariantMap listed = library.tasks().front().toMap();
        CHECK(listed.value(QStringLiteral("id")).toString() == id);
        CHECK(listed.value(QStringLiteral("schedule")).toString() == QStringLiteral("manual"));
        CHECK_FALSE(listed.value(QStringLiteral("readOnly")).toBool());
        REQUIRE(library.findExample(QStringLiteral("examples/daily-check-in")) != nullptr);
        CHECK(listed.value(QStringLiteral("exampleNote")).toString() ==
              library.findExample(QStringLiteral("examples/daily-check-in"))->description);
        const QVariantList params = listed.value(QStringLiteral("params")).toList();
        REQUIRE(params.size() == 4);
        CHECK(params.front().toMap().value(QStringLiteral("label")).toString() ==
              QStringLiteral("App to open (package name)"));

        const QString second = library.copyExample(QStringLiteral("examples/daily-check-in"));
        CHECK(second == QStringLiteral("daily-check-in-2"));
        REQUIRE(library.find(second) != nullptr);
        CHECK(library.find(second)->name == QStringLiteral("Daily check-in template 2"));
        CHECK(library.tasks().size() == 2);

        CHECK(library.setValue(id, QStringLiteral("waitMs"), 5000));
        CHECK(library.paramsJson(id).contains(QStringLiteral("\"waitMs\":5000")));
        CHECK(library.setSchedule(id, QStringLiteral("daily"), QStringLiteral("09:00")));
        CHECK(library.rename(id, QStringLiteral("Morning check-in")));
        CHECK(library.copyExample(QStringLiteral("examples/missing")).isEmpty());
        CHECK(library.copyExample(id).isEmpty());
        CHECK(library.tasks().size() == 2);
    }
    TaskLibrary reread(pathOf(root));
    reread.setExamplesFolder(exampleTasks());
    const TaskEntry* again = reread.find(id);
    REQUIRE(again != nullptr);
    CHECK(again->name == QStringLiteral("Morning check-in"));
    CHECK(again->schedule == QStringLiteral("daily"));
    CHECK(again->time == QStringLiteral("09:00"));
    CHECK(again->example == QStringLiteral("examples/daily-check-in"));
    CHECK(reread.paramsJson(id).contains(QStringLiteral("\"waitMs\":5000")));
    for (const QVariant& task : reread.tasks()) {
        CHECK_FALSE(task.toMap().value(QStringLiteral("exampleNote")).toString().isEmpty());
    }

    const TaskLibrary bare(pathOf(root));
    REQUIRE(bare.find(id) != nullptr);
    CHECK(bare.find(id)->example == QStringLiteral("examples/daily-check-in"));
    for (const QVariant& task : bare.tasks()) {
        CHECK(task.toMap().value(QStringLiteral("exampleNote")).toString().isEmpty());
    }
}

TEST_CASE("ADR-0105: an example task itself cannot be renamed, set, scheduled, removed, copied as a task or exported")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    TaskLibrary library(pathOf(root));
    library.setExamplesFolder(exampleTasks());
    const QString id = QStringLiteral("examples/find-and-tap");
    const fs::path folder = exampleTasks() / "find-and-tap";
    const std::string json = readBytes(folder / "task.json");
    const std::string code = readBytes(folder / "task.ts");
    CHECK(library.find(id) == nullptr);
    CHECK_FALSE(library.rename(id, QStringLiteral("Mine")));
    CHECK_FALSE(library.setValue(id, QStringLiteral("searchMs"), 100));
    CHECK_FALSE(library.setSchedule(id, QStringLiteral("daily"), QStringLiteral("07:00")));
    CHECK_FALSE(library.setDays(id, {1}));
    CHECK_FALSE(library.setPhones(id, {QStringLiteral("phone")}));
    CHECK_FALSE(library.setOption(id, QStringLiteral("wake"), true));
    CHECK_FALSE(library.markScheduled(id, QStringLiteral("2026-11-06")));
    CHECK_FALSE(library.remove(id));
    CHECK(library.copyToMine(id).isEmpty());
    CHECK_FALSE(library.exportPackage(id, root.filePath(QStringLiteral("example.gsext"))));
    CHECK(library.paramsJson(id) == QStringLiteral("{}"));
    CHECK(library.tasks().isEmpty());
    CHECK(readBytes(folder / "task.json") == json);
    CHECK(readBytes(folder / "task.ts") == code);
    CHECK(fs::is_regular_file(folder / "res" / "notifications.png"));
}

TEST_CASE("ADR-0105: on the demo recording, Find a picture and tap it taps the Notifications row at once")
{
    const Replayed run = ranTask(exampleTasks() / "find-and-tap", Microseconds{10'000'000});
    const std::string transcript = transcriptOf(run);
    CAPTURE(transcript);
    CHECK(run.loaded);
    CHECK(run.failures.empty());
    CHECK_FALSE(run.reachedLimit);
    CHECK(looksFor(run.outputs, "res/notifications.png") == 1);
    const std::vector<Tap> taps = tapsIn(run.outputs);
    REQUIRE(taps.size() == 1);
    CHECK(within(taps[0].x, 116.0 / 288.0));
    CHECK(within(taps[0].y, 390.0 / 640.0));
    CHECK(taps[0].at < Microseconds{100'000});
    const Logged* tapped = logged(run, "Tapped res/notifications.png at 40%, 61%");
    REQUIRE(tapped != nullptr);
    CHECK(tapped->level == gs::log::Level::Info);
}

TEST_CASE("ADR-0105: on the demo recording, Wait for a picture, then tap taps the Network row once it is there")
{
    const Replayed run = ranTask(exampleTasks() / "wait-then-tap", Microseconds{20'000'000});
    const std::string transcript = transcriptOf(run);
    CAPTURE(transcript);
    CHECK(run.loaded);
    CHECK(run.failures.empty());
    CHECK_FALSE(run.reachedLimit);
    CHECK(looksFor(run.outputs, "res/network.png") >= 1);
    const std::vector<Tap> taps = tapsIn(run.outputs);
    REQUIRE(taps.size() == 1);
    CHECK(within(taps[0].x, 100.0 / 288.0));
    CHECK(within(taps[0].y, 206.0 / 640.0));
    CHECK(logged(run, "Waiting up to 10 s for res/network.png") != nullptr);
    CHECK(logged(run, "res/network.png appeared; tapped at 35%, 32%") != nullptr);
}

TEST_CASE("ADR-0105: Wait for a picture, then tap gives up cleanly with a log line when its picture never appears")
{
    const gs::test::McpApplication application;
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const fs::path folder = pathOf(root) / "wait-then-tap";
    std::error_code error;
    fs::copy(exampleTasks() / "wait-then-tap", folder, fs::copy_options::recursive, error);
    REQUIRE_FALSE(error);
    QImage noise(168, 40, QImage::Format_Grayscale8);
    std::uint32_t state = 12345;
    for (int row = 0; row < noise.height(); ++row) {
        uchar* line = noise.scanLine(row);
        for (int column = 0; column < noise.width(); ++column) {
            state = state * 1103515245U + 12345U;
            line[column] = static_cast<uchar>(state >> 24);
        }
    }
    REQUIRE(noise.save(QString::fromStdWString((folder / "res" / "network.png").wstring()), "PNG"));

    const Replayed run = ranTask(folder, Microseconds{20'000'000});
    const std::string transcript = transcriptOf(run);
    CAPTURE(transcript);
    CHECK(run.loaded);
    CHECK(run.failures.empty());
    CHECK_FALSE(run.reachedLimit);
    CHECK(tapsIn(run.outputs).empty());
    CHECK(looksFor(run.outputs, "res/network.png") >= 30);
    const Logged* gaveUp = logged(run, "Gave up: res/network.png did not appear within 10 s");
    REQUIRE(gaveUp != nullptr);
    CHECK(gaveUp->level == gs::log::Level::Warning);
    CHECK(gaveUp->at >= Microseconds{9'900'000});
    CHECK(gaveUp->at <= Microseconds{10'500'000});
}

TEST_CASE("ADR-0105: on the demo recording, Tap places in order taps its three places one after another")
{
    const Replayed run = ranTask(exampleTasks() / "tap-in-order", Microseconds{10'000'000});
    const std::string transcript = transcriptOf(run);
    CAPTURE(transcript);
    CHECK(run.loaded);
    CHECK(run.failures.empty());
    CHECK_FALSE(run.reachedLimit);
    const std::vector<Tap> taps = tapsIn(run.outputs);
    REQUIRE(taps.size() == 3);
    CHECK(within(taps[0].x, 0.35));
    CHECK(within(taps[0].y, 0.32));
    CHECK(within(taps[1].x, 0.40));
    CHECK(within(taps[1].y, 0.61));
    CHECK(within(taps[2].x, 0.24));
    CHECK(within(taps[2].y, 0.70));
    CHECK(taps[1].at - taps[0].at >= Microseconds{1'000'000});
    CHECK(taps[2].at - taps[1].at >= Microseconds{1'000'000});
    CHECK(logged(run, "Round 1: tapped place 1 at 35%, 32%") != nullptr);
    CHECK(logged(run, "Round 1: tapped place 2 at 40%, 61%") != nullptr);
    CHECK(logged(run, "Round 1: tapped place 3 at 24%, 70%") != nullptr);
}

TEST_CASE("ADR-0105: on the demo recording, the daily check-in template opens its app, taps the button and sees it "
          "done")
{
    const Replayed run = ranTask(exampleTasks() / "daily-check-in", Microseconds{70'000'000});
    const std::string transcript = transcriptOf(run);
    CAPTURE(transcript);
    CHECK(run.loaded);
    CHECK(run.failures.empty());
    CHECK_FALSE(run.reachedLimit);
    CHECK(looksFor(run.outputs, "res/check-in.png") >= 1);
    CHECK(looksFor(run.outputs, "res/done.png") >= 1);
    const std::vector<Tap> taps = tapsIn(run.outputs);
    REQUIRE(taps.size() == 1);
    CHECK(within(taps[0].x, 70.0 / 288.0));
    CHECK(within(taps[0].y, 451.0 / 640.0));
    CHECK(logged(run, "Opening com.android.settings") != nullptr);
    CHECK(logged(run, "Tapped the check-in button") != nullptr);
    CHECK(logged(run, "Checked in") != nullptr);
}

TEST_CASE("SCR-008: every example component and example task loads and passes gs check")
{
    const gs::test::McpApplication application;
    std::vector<fs::path> folders;
    for (const fs::path& shelf : {exampleComponents(), exampleTasks()}) {
        std::error_code error;
        for (fs::directory_iterator it(shelf, error), end; !error && it != end; it.increment(error)) {
            if (it->is_directory(error)) {
                folders.push_back(it->path());
            }
        }
    }
    std::ranges::sort(folders);
    CHECK(folders.size() == 9);
    for (const fs::path& folder : folders) {
        const std::string name = folder.filename().string();
        CAPTURE(name);
        const std::string entry = fs::is_regular_file(folder / "task.ts") ? "task.ts" : "component.ts";
        REQUIRE(fs::is_regular_file(folder / entry));
        CHECK(readLines(folder / "tsconfig.json") == gs::script::profileTsconfig());

        const Replayed loaded = replayed(folder, entry, {}, Microseconds{1'000'000});
        const std::string transcript = transcriptOf(loaded);
        CAPTURE(transcript);
        CHECK(loaded.loaded);
        CHECK(loaded.failures.empty());

        QProcess check;
        check.setProgram(QString::fromUtf8(GS_CLI_PROGRAM));
        check.setArguments({QStringLiteral("check"), QString::fromStdWString(folder.wstring()), QStringLiteral("--json")});
        check.start();
        REQUIRE(check.waitForFinished(180000));
        const QByteArray output = check.readAllStandardOutput();
        const std::string shown = output.toStdString();
        CAPTURE(shown);
        CHECK(check.exitStatus() == QProcess::NormalExit);
        CHECK(check.exitCode() == 0);
        const QJsonObject result = QJsonDocument::fromJson(output).object();
        CHECK(result.value(QStringLiteral("ok")).toBool());
        CHECK(result.value(QStringLiteral("problems")).toArray().isEmpty());
    }
}
