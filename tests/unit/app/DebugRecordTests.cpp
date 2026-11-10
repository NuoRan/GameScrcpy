#include "DebugRecord.h"

#include <doctest/doctest.h>

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <memory>
#include <optional>

using gs::app::DebugRecord;

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

QVariantMap stepHit(double at, const QString& position, const QVariantMap& values = {},
                    const QString& label = QStringLiteral("every(500)"))
{
    return {{QStringLiteral("task"), 3},
            {QStringLiteral("label"), label},
            {QStringLiteral("file"), QStringLiteral("main.ts")},
            {QStringLiteral("position"), position},
            {QStringLiteral("values"), values},
            {QStringLiteral("at"), at}};
}

QVariantMap tapHit(double at, double x, double y)
{
    return {{QStringLiteral("kind"), QStringLiteral("tap")},
            {QStringLiteral("x"), x},
            {QStringLiteral("y"), y},
            {QStringLiteral("at"), at}};
}

QVariantMap visionHit(double at, const QString& json)
{
    return {{QStringLiteral("kind"), QStringLiteral("vision")}, {QStringLiteral("json"), json}, {QStringLiteral("at"), at}};
}

QByteArray picture(const QColor& color)
{
    QImage image(48, 96, QImage::Format_RGB32);
    image.fill(color);
    return DebugRecord::encodeFrame(image);
}

QVariantList laneSteps(const QVariantMap& timeline, const QString& lane)
{
    QVariantList found;
    for (const QVariant& step : timeline.value(QStringLiteral("steps")).toList()) {
        if (step.toMap().value(QStringLiteral("lane")).toString() == lane) {
            found.push_back(step);
        }
    }
    return found;
}

QJsonObject savedRecord(const QString& folder)
{
    QFile file(QDir(folder).filePath(QStringLiteral("record.json")));
    REQUIRE(file.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(file.readAll()).object();
}

}

TEST_CASE("a debug record keeps the last five minutes and forgets them when debugging stops (SCR-016)")
{
    const Application app;
    QTemporaryDir root;
    REQUIRE(root.isValid());
    double clock = 10'000'000.0;
    DebugRecord record;
    record.setClock([&clock] { return clock; });
    const QString live = QDir(root.path()).filePath(QStringLiteral(".live-test"));

    CHECK_FALSE(record.active());
    record.start(QString());
    CHECK_FALSE(record.recording());

    record.start(live);
    CHECK(record.recording());
    CHECK(record.active());
    CHECK(record.live());
    CHECK(QFileInfo(live).isDir());

    record.addHits({stepHit(clock - 400'000.0, QStringLiteral("2:5")), stepHit(clock - 1000.0, QStringLiteral("3:5")),
                    visionHit(clock - 900.0, QStringLiteral(R"({"x":0.5,"y":0.5,"width":0.2,"height":0.1,"score":0.93})")),
                    tapHit(clock - 500.0, 0.5, 0.25)});
    record.addFrame(picture(Qt::red));
    REQUIRE(QDir(live).entryList(QDir::Files).size() == 1);

    QVariantMap timeline = record.timeline();
    const QVariantMap span = timeline.value(QStringLiteral("span")).toMap();
    CHECK(span.value(QStringLiteral("start")).toDouble() == doctest::Approx(clock - 1000.0));
    CHECK(span.value(QStringLiteral("end")).toDouble() == doctest::Approx(clock));
    CHECK(timeline.value(QStringLiteral("live")).toBool());
    CHECK(timeline.value(QStringLiteral("playhead")).toDouble() == doctest::Approx(clock));
    CHECK(timeline.value(QStringLiteral("frames")).toList().size() == 1);
    CHECK(timeline.value(QStringLiteral("lanes")).toList().size() == 2);
    const QVariantList code = laneSteps(timeline, QStringLiteral("code"));
    REQUIRE(code.size() == 1);
    CHECK(code[0].toMap().value(QStringLiteral("text")).toString() == QStringLiteral("every(500) · main.ts line 3"));
    const QVariantList actions = laneSteps(timeline, QStringLiteral("act"));
    REQUIRE(actions.size() == 2);
    CHECK(actions[0].toMap().value(QStringLiteral("text")).toString() == QStringLiteral("Found 0.93"));
    CHECK(actions[0].toMap().value(QStringLiteral("tone")).toString() == QStringLiteral("vision"));
    CHECK(actions[1].toMap().value(QStringLiteral("text")).toString() == QStringLiteral("Tap 0.50, 0.25"));

    clock += DebugRecord::kWindowMs + 1000.0;
    record.refresh();
    timeline = record.timeline();
    CHECK(timeline.value(QStringLiteral("frames")).toList().isEmpty());
    CHECK(timeline.value(QStringLiteral("steps")).toList().isEmpty());
    CHECK(QDir(live).entryList(QDir::Files).isEmpty());

    record.addFrame(picture(Qt::blue));
    record.stop();
    CHECK_FALSE(record.recording());
    CHECK_FALSE(record.active());
    CHECK_FALSE(QFileInfo::exists(live));
}

TEST_CASE("looking back shows that moment's picture, what was found and the values (SCR-016)")
{
    const Application app;
    QTemporaryDir root;
    REQUIRE(root.isValid());
    double clock = 5'000'000.0;
    DebugRecord record;
    record.setClock([&clock] { return clock; });
    record.start(QDir(root.path()).filePath(QStringLiteral(".live-test")));
    std::optional<QString> replayed;
    QVariantMap replayedValues;
    int wentLive = 0;
    QObject::connect(&record, &DebugRecord::replayed, [&](const QString& file, const QString& position, const QVariantMap& values) {
        replayed = file + u'#' + position;
        replayedValues = values;
    });
    QObject::connect(&record, &DebugRecord::wentLive, [&wentLive] { ++wentLive; });

    const double start = clock;
    record.addHits({stepHit(start, QStringLiteral("4:1"), {{QStringLiteral("n"), QStringLiteral("1")}})});
    record.addFrame(picture(Qt::red));
    clock += 2000.0;
    record.addHits({stepHit(clock, QStringLiteral("7:3"), {{QStringLiteral("n"), QStringLiteral("2")}}),
                    visionHit(clock, QStringLiteral(R"([{"x":0.5,"y":0.5,"width":0.2,"height":0.1,"score":0.9}])"))});
    record.addFrame(picture(Qt::green));
    clock += 2000.0;

    record.seek(start + 500.0);
    CHECK_FALSE(record.live());
    CHECK(replayed == QStringLiteral("main.ts#4:1"));
    CHECK(replayedValues.value(QStringLiteral("n")).toString() == QStringLiteral("1"));
    QVariantMap moment = record.moment();
    CHECK(moment.value(QStringLiteral("title")).toString() == QStringLiteral("Moment · 00:00.5"));
    CHECK(moment.value(QStringLiteral("subtitle")).toString() == QStringLiteral("every(500) · main.ts line 4"));
    CHECK(QUrl(moment.value(QStringLiteral("frame")).toString()).toLocalFile().endsWith(QStringLiteral("frame-1.jpg")));
    CHECK(moment.value(QStringLiteral("boxes")).toList().isEmpty());
    const QVariantList firstValues = moment.value(QStringLiteral("variables")).toList();
    REQUIRE(firstValues.size() == 1);
    CHECK(firstValues[0].toMap().value(QStringLiteral("value")).toString() == QStringLiteral("1"));

    record.seek(start + 2200.0);
    CHECK(replayed == QStringLiteral("main.ts#7:3"));
    moment = record.moment();
    CHECK(QUrl(moment.value(QStringLiteral("frame")).toString()).toLocalFile().endsWith(QStringLiteral("frame-2.jpg")));
    CHECK(moment.value(QStringLiteral("result")).toString() == QStringLiteral("Found 1 place(s)"));
    const QVariantList boxes = moment.value(QStringLiteral("boxes")).toList();
    REQUIRE(boxes.size() == 1);
    CHECK(boxes[0].toMap().value(QStringLiteral("x")).toDouble() == doctest::Approx(0.4));
    CHECK(boxes[0].toMap().value(QStringLiteral("y")).toDouble() == doctest::Approx(0.45));
    CHECK(boxes[0].toMap().value(QStringLiteral("w")).toDouble() == doctest::Approx(0.2));
    CHECK(boxes[0].toMap().value(QStringLiteral("h")).toDouble() == doctest::Approx(0.1));

    record.seek(start - 10'000.0);
    CHECK(record.timeline().value(QStringLiteral("playhead")).toDouble() == doctest::Approx(start));

    record.goLive();
    CHECK(record.live());
    CHECK(wentLive == 1);
    record.goLive();
    CHECK(wentLive == 1);
}

TEST_CASE("a pause keeps its values and call stack until it is resumed (SCR-016)")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    double clock = 7'000'000.0;
    DebugRecord record;
    record.setClock([&clock] { return clock; });
    record.start(QDir(root.path()).filePath(QStringLiteral(".live-test")));

    record.addHits({stepHit(clock, QStringLiteral("9:3"), {{QStringLiteral("hp"), QStringLiteral("41")}}, QStringLiteral("skill.onPress"))});
    record.addPause({{QStringLiteral("pause"), 7},
                     {QStringLiteral("file"), QStringLiteral("main.ts")},
                     {QStringLiteral("position"), QStringLiteral("9:3")},
                     {QStringLiteral("values"), QVariantMap{{QStringLiteral("hp"), QStringLiteral("42")}}},
                     {QStringLiteral("stack"), QVariantList{QStringLiteral("main.ts#2:1")}}});
    clock += 5000.0;

    QVariantMap moment = record.moment();
    CHECK(moment.value(QStringLiteral("subtitle")).toString() == QStringLiteral("skill.onPress · main.ts line 9"));
    QVariantList variables = moment.value(QStringLiteral("variables")).toList();
    REQUIRE(variables.size() == 1);
    CHECK(variables[0].toMap().value(QStringLiteral("value")).toString() == QStringLiteral("42"));
    const QVariantList stack = moment.value(QStringLiteral("stack")).toList();
    REQUIRE(stack.size() == 2);
    CHECK(stack[0].toMap().value(QStringLiteral("title")).toString() == QStringLiteral("skill.onPress"));
    CHECK(stack[0].toMap().value(QStringLiteral("place")).toString() == QStringLiteral("main.ts:2"));
    CHECK_FALSE(stack[0].toMap().value(QStringLiteral("current")).toBool());
    CHECK(stack[1].toMap().value(QStringLiteral("title")).toString() == QStringLiteral("Paused here"));
    CHECK(stack[1].toMap().value(QStringLiteral("place")).toString() == QStringLiteral("main.ts:9"));
    CHECK(stack[1].toMap().value(QStringLiteral("current")).toBool());

    record.addResume(8);
    CHECK(record.moment().value(QStringLiteral("stack")).toList().size() == 2);

    record.addResume(7);
    moment = record.moment();
    variables = moment.value(QStringLiteral("variables")).toList();
    REQUIRE(variables.size() == 1);
    CHECK(variables[0].toMap().value(QStringLiteral("value")).toString() == QStringLiteral("41"));
    CHECK(moment.value(QStringLiteral("stack")).toList().size() == 1);
    CHECK(laneSteps(record.timeline(), QStringLiteral("act")).isEmpty());
}

TEST_CASE("a kept part is saved with its code and pictures and opens again later (SCR-016)")
{
    const Application app;
    QTemporaryDir root;
    QTemporaryDir code;
    REQUIRE(root.isValid());
    REQUIRE(code.isValid());
    const auto write = [&code](const QString& relative, const QByteArray& bytes) {
        const QString path = QDir(code.path()).filePath(relative);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(bytes);
    };
    write(QStringLiteral("main.ts"), "on();\n");
    write(QStringLiteral("lib/util.ts"), "export {};\n");
    write(QStringLiteral("res/resources.json"), "{}");
    write(QStringLiteral("res/button.png"), "png");
    write(QStringLiteral("node_modules/x/index.js"), "x");
    write(QStringLiteral("types/gamescrcpy.d.ts"), "declare");

    double clock = 9'000'000.0;
    DebugRecord record;
    record.setClock([&clock] { return clock; });
    CHECK(record.save(root.path(), code.path(), QStringLiteral("idle")).isEmpty());
    record.start(QDir(root.path()).filePath(QStringLiteral("debug-records/.live-test")));
    const double start = clock;
    record.addHits({stepHit(start, QStringLiteral("1:1"), {{QStringLiteral("n"), QStringLiteral("1")}}), tapHit(start + 10.0, 0.1, 0.2)});
    record.addFrame(picture(Qt::red));
    clock += 1500.0;
    record.addHits({stepHit(clock, QStringLiteral("2:1"))});
    record.addFrame(picture(Qt::green));

    const QString folder = record.save(root.path(), code.path(), QStringLiteral("Farm: daily"));
    REQUIRE_FALSE(folder.isEmpty());
    CHECK(QFileInfo(folder).fileName().startsWith(QStringLiteral("Farm_ daily-")));
    CHECK(QFileInfo(folder).absolutePath() == QDir(root.path()).filePath(QStringLiteral("debug-records")));
    CHECK(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("code/main.ts"))));
    CHECK(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("code/lib/util.ts"))));
    CHECK(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("code/res/resources.json"))));
    CHECK_FALSE(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("code/res/button.png"))));
    CHECK_FALSE(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("code/node_modules"))));
    CHECK_FALSE(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("code/types"))));
    CHECK(QDir(QDir(folder).filePath(QStringLiteral("frames"))).entryList(QDir::Files).size() == 2);
    const QJsonObject saved = savedRecord(folder);
    CHECK(saved.value(QStringLiteral("format")).toString() == QStringLiteral("gamescrcpy.debug-record"));
    CHECK(saved.value(QStringLiteral("version")).toInt() == 1);
    CHECK(saved.value(QStringLiteral("steps")).toArray().size() == 2);
    CHECK(saved.value(QStringLiteral("events")).toArray().size() == 1);

    record.stop();
    CHECK(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("record.json"))));

    DebugRecord opened;
    std::optional<QString> replayed;
    QObject::connect(&opened, &DebugRecord::replayed,
                     [&replayed](const QString& file, const QString& position, const QVariantMap&) { replayed = file + u'#' + position; });
    CHECK_FALSE(opened.open(root.path()));
    REQUIRE(opened.open(folder));
    CHECK(opened.opened());
    CHECK(opened.active());
    CHECK_FALSE(opened.recording());
    CHECK_FALSE(opened.live());
    CHECK(opened.name() == QStringLiteral("Farm: daily"));
    CHECK(replayed == QStringLiteral("main.ts#1:1"));
    const QVariantMap timeline = opened.timeline();
    const QVariantMap span = timeline.value(QStringLiteral("span")).toMap();
    CHECK(span.value(QStringLiteral("start")).toDouble() == doctest::Approx(start));
    CHECK(span.value(QStringLiteral("end")).toDouble() == doctest::Approx(start + 1500.0));
    CHECK(timeline.value(QStringLiteral("frames")).toList().size() == 2);
    CHECK(laneSteps(timeline, QStringLiteral("code")).size() == 2);
    CHECK(laneSteps(timeline, QStringLiteral("act")).size() == 1);
    const QString frame = QUrl(opened.moment().value(QStringLiteral("frame")).toString()).toLocalFile();
    CHECK(frame.startsWith(QDir::fromNativeSeparators(folder)));
    CHECK(QImage(frame).size() == QSize(48, 96));
    opened.goLive();
    CHECK_FALSE(opened.live());
    CHECK(opened.save(root.path(), QString(), QStringLiteral("again")).size() > 0);

    opened.close();
    CHECK_FALSE(opened.active());
    CHECK(opened.moment().isEmpty());
    CHECK(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("record.json"))));
}

TEST_CASE("an opened debug record keeps the code it was made with, apart from the code as it is now (SCR-016)")
{
    const Application app;
    QTemporaryDir root;
    QTemporaryDir code;
    REQUIRE(root.isValid());
    REQUIRE(code.isValid());
    const auto write = [&code](const QString& relative, const QByteArray& bytes) {
        const QString path = QDir(code.path()).filePath(relative);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(bytes);
    };
    write(QStringLiteral("main.ts"), "tap();\n");
    write(QStringLiteral("main.blueprint.json"), "{}");

    double clock = 11'000'000.0;
    DebugRecord record;
    record.setClock([&clock] { return clock; });
    record.start(QDir(root.path()).filePath(QStringLiteral("debug-records/.live-code")));
    record.addHits({stepHit(clock, QStringLiteral("1:1"))});
    clock += 500.0;
    const QString folder = record.save(root.path(), code.path(), QStringLiteral("Code"));
    REQUIRE_FALSE(folder.isEmpty());
    CHECK(record.codeFolder().isEmpty());
    record.stop();
    write(QStringLiteral("main.ts"), "swipe();\nswipe();\n");

    DebugRecord opened;
    CHECK(opened.codeFolder().isEmpty());
    CHECK(opened.recordedFile(QStringLiteral("main.ts")).isEmpty());
    REQUIRE(opened.open(folder));
    CHECK(QDir::cleanPath(opened.codeFolder()) == QDir::cleanPath(QDir(folder).filePath(QStringLiteral("code"))));
    const QString copy = opened.recordedFile(QStringLiteral("main.ts"));
    REQUIRE_FALSE(copy.isEmpty());
    QFile kept(copy);
    REQUIRE(kept.open(QIODevice::ReadOnly));
    CHECK(kept.readAll() == QByteArray("tap();\n"));
    CHECK_FALSE(opened.recordedFile(QStringLiteral("main.blueprint.json")).isEmpty());
    CHECK(opened.recordedFile(QStringLiteral("./main.ts")) == copy);
    CHECK(opened.recordedFile(QStringLiteral("../record.json")).isEmpty());
    CHECK(opened.recordedFile(QStringLiteral("missing.ts")).isEmpty());
    CHECK(opened.recordedFile(QDir(code.path()).filePath(QStringLiteral("main.ts"))).isEmpty());

    opened.close();
    CHECK(opened.codeFolder().isEmpty());
    CHECK(opened.recordedFile(QStringLiteral("main.ts")).isEmpty());
}

TEST_CASE("actions read plainly: a color without its quotes, and actions close together merged as actions (SCR-016)")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    double clock = 30'000'000.0;
    DebugRecord record;
    record.setClock([&clock] { return clock; });
    record.start(QDir(root.path()).filePath(QStringLiteral(".live-test")));
    record.addHits({visionHit(clock - 5000.0, QStringLiteral(R"("#ECEEF2")")), tapHit(clock - 2000.0, 0.5, 0.5),
                    tapHit(clock - 1990.0, 0.5, 0.5), visionHit(clock - 1980.0, QStringLiteral("null"))});
    const QVariantList actions = laneSteps(record.timeline(), QStringLiteral("act"));
    REQUIRE(actions.size() == 2);
    CHECK(actions[0].toMap().value(QStringLiteral("text")).toString() == QStringLiteral("#ECEEF2"));
    CHECK(actions[1].toMap().value(QStringLiteral("text")).toString() == QStringLiteral("3 action(s)"));
}

TEST_CASE("a busy loop keeps at most 200 steps a second, always with the newest, and the timeline merges them (SCR-016)")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    double clock = 20'000'000.0;
    DebugRecord record;
    record.setClock([&clock] { return clock; });
    record.start(QDir(root.path()).filePath(QStringLiteral(".live-test")));

    QVariantList burst;
    for (int i = 0; i < 1000; ++i) {
        burst.push_back(stepHit(clock + i * 0.5, i == 999 ? QStringLiteral("99:1") : QStringLiteral("5:1")));
    }
    clock += 600.0;
    record.addHits(burst);
    const QJsonArray kept = savedRecord(record.save(root.path(), QString(), QStringLiteral("burst")))
                                .value(QStringLiteral("steps"))
                                .toArray();
    CHECK(kept.size() == DebugRecord::kStepsPerSecond);
    CHECK(kept.last().toObject().value(QStringLiteral("position")).toString() == QStringLiteral("99:1"));

    record.stop();
    record.start(QDir(root.path()).filePath(QStringLiteral(".live-test")));
    const double begin = clock;
    for (int second = 0; second < 290; ++second) {
        QVariantList hits;
        for (int i = 0; i < 10; ++i) {
            hits.push_back(stepHit(begin + second * 1000.0 + i * 100.0, i % 2 == 0 ? QStringLiteral("1:1") : QStringLiteral("2:1")));
        }
        record.addHits(hits);
    }
    clock = begin + 290'000.0;
    const QVariantList busy = laneSteps(record.timeline(), QStringLiteral("code"));
    CHECK(busy.size() <= 301);
    REQUIRE(busy.size() >= 200);
    CHECK(busy[0].toMap().value(QStringLiteral("text")).toString().contains(QStringLiteral("step")));

    record.stop();
    record.start(QDir(root.path()).filePath(QStringLiteral(".live-test")));
    QVariantList calm;
    for (int i = 0; i < 20; ++i) {
        calm.push_back(stepHit(clock + i * 500.0, QStringLiteral("3:1")));
    }
    clock += 10'000.0;
    record.addHits(calm);
    const QVariantList joined = laneSteps(record.timeline(), QStringLiteral("code"));
    REQUIRE(joined.size() == 1);
    CHECK(joined[0].toMap().value(QStringLiteral("text")).toString() == QStringLiteral("every(500) · main.ts line 3"));
    CHECK(record.timeline().value(QStringLiteral("lanes")).toList().size() == 1);
}
