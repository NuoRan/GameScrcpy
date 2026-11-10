#include "TaskLibrary.h"

#include <doctest/doctest.h>

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTime>
#include <QTimeZone>
#include <QVariantList>
#include <QVariantMap>

#include <filesystem>
#include <utility>
#include <vector>

using gs::app::checkSchedule;
using gs::app::closesToTray;
using gs::app::lastDueTime;
using gs::app::ScheduleCheck;
using gs::app::ScheduleVerdict;
using gs::app::ScreenPlan;
using gs::app::screenPlanFor;
using gs::app::sleepsWhenDone;
using gs::app::TaskEntry;
using gs::app::TaskLibrary;
using gs::app::TaskParamSpec;
using gs::app::taskParamsIn;

namespace {

std::filesystem::path pathOf(const QTemporaryDir& dir)
{
    return {dir.path().toStdWString()};
}

QDateTime utc(int year, int month, int day, int hour, int minute, int second = 0)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute, second), QTimeZone(QTimeZone::UTC));
}

TaskEntry timedTask(const QString& mode, const QString& time, QList<int> days = {})
{
    TaskEntry entry;
    entry.id = QStringLiteral("farm");
    entry.name = QStringLiteral("Farm");
    entry.schedule = mode;
    entry.time = time;
    entry.days = std::move(days);
    return entry;
}

}

TEST_CASE("ADR-0091: a task's settings are read from static params without running the code")
{
    const std::vector<TaskParamSpec> params = taskParamsIn(QStringLiteral(R"(
export default class Farm extends Task {
    static params = {
        rounds: param.number(10, { label: "Rounds", min: 1, max: 99 }),
        "map name": param.text('Erangel, the big one'),
        claim: param.toggle(false),
        broken: param.number(later),
    };

    async run() {
        const other = { x: param.number(3) };
    }
}
)"));
    REQUIRE(params.size() == 3);
    CHECK(params[0].name == QStringLiteral("rounds"));
    CHECK(params[0].kind == QStringLiteral("number"));
    CHECK(params[0].value.toDouble() == 10.0);
    CHECK(params[0].label == QStringLiteral("Rounds"));
    REQUIRE(params[0].min.has_value());
    CHECK(*params[0].min == 1.0);
    REQUIRE(params[0].max.has_value());
    CHECK(*params[0].max == 99.0);
    CHECK(params[1].name == QStringLiteral("map name"));
    CHECK(params[1].kind == QStringLiteral("text"));
    CHECK(params[1].value.toString() == QStringLiteral("Erangel, the big one"));
    CHECK(params[2].name == QStringLiteral("claim"));
    CHECK(params[2].kind == QStringLiteral("toggle"));
    CHECK(params[2].value == QVariant(false));
    CHECK(taskParamsIn(QStringLiteral("export default class A extends Task { async run() {} }")).empty());
}

TEST_CASE("ADR-0091: a new task can run at once, and its settings are kept in task.json")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    TaskLibrary library(pathOf(root));
    CHECK(library.tasks().isEmpty());
    const QString id = library.add(QStringLiteral("Daily check-in!"));
    CHECK(id == QStringLiteral("daily-check-in"));
    CHECK(library.add(QStringLiteral("Daily check-in!")) == QStringLiteral("daily-check-in-2"));
    CHECK(library.tasks().size() == 2);

    const TaskEntry* entry = library.find(id);
    REQUIRE(entry != nullptr);
    CHECK(entry->name == QStringLiteral("Daily check-in!"));
    CHECK(entry->schedule == QStringLiteral("manual"));
    REQUIRE(entry->params.size() == 1);
    CHECK(entry->params[0].name == QStringLiteral("rounds"));
    CHECK(QFile::exists(QDir(entry->folder).filePath(QStringLiteral("task.ts"))));
    CHECK(QFile::exists(QDir(entry->folder).filePath(QStringLiteral("tsconfig.json"))));
    CHECK(library.paramsJson(id) == QStringLiteral(R"({"rounds":3})"));

    CHECK(library.setValue(id, QStringLiteral("rounds"), 0));
    CHECK(library.paramsJson(id) == QStringLiteral(R"({"rounds":1})"));
    CHECK_FALSE(library.setValue(id, QStringLiteral("missing"), 1));
    CHECK(library.setPhones(id, {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("a")}));
    CHECK(library.setSchedule(id, QStringLiteral("daily"), QStringLiteral("22:00")));
    CHECK_FALSE(library.setSchedule(id, QStringLiteral("hourly"), QStringLiteral("22:00")));
    CHECK_FALSE(library.setSchedule(id, QStringLiteral("daily"), QStringLiteral("25:00")));
    CHECK(library.setOption(id, QStringLiteral("onError"), QStringLiteral("retry")));
    CHECK_FALSE(library.setOption(id, QStringLiteral("onError"), QStringLiteral("explode")));
    CHECK(library.setOption(id, QStringLiteral("sleepAfter"), true));
    CHECK(library.rename(id, QStringLiteral("Check-in")));
    CHECK(library.markScheduled(id, QStringLiteral("2026-11-04")));

    TaskLibrary reread(pathOf(root));
    const TaskEntry* again = reread.find(id);
    REQUIRE(again != nullptr);
    CHECK(again->name == QStringLiteral("Check-in"));
    CHECK(again->phones == QStringList{QStringLiteral("a"), QStringLiteral("b")});
    CHECK(again->schedule == QStringLiteral("daily"));
    CHECK(again->time == QStringLiteral("22:00"));
    CHECK(again->onError == QStringLiteral("retry"));
    CHECK(again->sleepAfter);
    CHECK_FALSE(again->wake);
    CHECK(again->lastScheduled == QStringLiteral("2026-11-04"));
    CHECK(reread.paramsJson(id) == QStringLiteral(R"({"rounds":1})"));
    CHECK(reread.tasks().size() == 2);

    CHECK(reread.setSchedule(id, QStringLiteral("once"), QStringLiteral("07:30")));
    CHECK(reread.find(id)->lastScheduled.isEmpty());
}

TEST_CASE("ADR-0091: the recent task runs are kept beside the tasks folder for the next start")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const std::filesystem::path tasks = pathOf(root) / "tasks";
    {
        TaskLibrary library(tasks);
        CHECK(library.history().isEmpty());
        library.saveHistory({QVariantMap{{QStringLiteral("task"), QStringLiteral("daily")},
                                         {QStringLiteral("result"), QStringLiteral("done")},
                                         {QStringLiteral("durationMs"), 4200}}});
    }
    CHECK(QFile::exists(QDir(root.path()).filePath(QStringLiteral("task-history.json"))));
    const TaskLibrary again(tasks);
    const QVariantList history = again.history();
    REQUIRE(history.size() == 1);
    CHECK(history[0].toMap().value(QStringLiteral("task")).toString() == QStringLiteral("daily"));
    CHECK(history[0].toMap().value(QStringLiteral("result")).toString() == QStringLiteral("done"));
    CHECK(history[0].toMap().value(QStringLiteral("durationMs")).toInt() == 4200);
    CHECK(again.tasks().isEmpty());
}

TEST_CASE("ADR-0091: a weekly task keeps its days in task.json, and a task set to run once goes back to by hand after it ran")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    TaskLibrary library(pathOf(root));
    const QString id = library.add(QStringLiteral("Weekly farm"));
    REQUIRE_FALSE(id.isEmpty());

    REQUIRE(library.setSchedule(id, QStringLiteral("weekly"), QStringLiteral("21:30")));
    REQUIRE(library.find(id)->days.size() == 1);
    CHECK(library.find(id)->days.front() >= 1);
    CHECK(library.find(id)->days.front() <= 7);
    CHECK(library.setDays(id, {5, 1, 3, 3}));
    CHECK(library.find(id)->days == QList<int>{1, 3, 5});
    CHECK_FALSE(library.setDays(id, {}));
    CHECK_FALSE(library.setDays(id, {0, 2}));
    CHECK_FALSE(library.setDays(id, {8}));
    CHECK_FALSE(library.setDays(QStringLiteral("missing"), {1}));
    CHECK(library.find(id)->days == QList<int>{1, 3, 5});
    REQUIRE(library.tasks().size() == 1);
    const QVariantMap listed = library.tasks().front().toMap();
    CHECK(listed.value(QStringLiteral("schedule")).toString() == QStringLiteral("weekly"));
    CHECK(listed.value(QStringLiteral("days")).toList() == QVariantList{1, 3, 5});

    const TaskLibrary reread(pathOf(root));
    const TaskEntry* again = reread.find(id);
    REQUIRE(again != nullptr);
    CHECK(again->schedule == QStringLiteral("weekly"));
    CHECK(again->time == QStringLiteral("21:30"));
    CHECK(again->days == QList<int>{1, 3, 5});

    CHECK(library.setSchedule(id, QStringLiteral("once"), QStringLiteral("07:30")));
    CHECK(library.find(id)->schedule == QStringLiteral("once"));
    CHECK(library.markScheduled(id, QStringLiteral("2026-11-06")));
    CHECK(library.find(id)->schedule == QStringLiteral("manual"));
    const TaskLibrary afterOnce(pathOf(root));
    REQUIRE(afterOnce.find(id) != nullptr);
    CHECK(afterOnce.find(id)->schedule == QStringLiteral("manual"));
    CHECK(afterOnce.find(id)->days == QList<int>{1, 3, 5});

    CHECK(library.setSchedule(id, QStringLiteral("daily"), QStringLiteral("06:00")));
    CHECK(library.markScheduled(id, QStringLiteral("2026-11-07")));
    CHECK(library.find(id)->schedule == QStringLiteral("daily"));
}

TEST_CASE("ADR-0091: a scheduled task is due for five minutes after its time, and a time the computer slept through is missed")
{
    const TaskEntry daily = timedTask(QStringLiteral("daily"), QStringLiteral("08:00"));
    CHECK(lastDueTime(daily, utc(2026, 11, 6, 8, 0, 10)) == utc(2026, 11, 6, 8, 0));
    CHECK(lastDueTime(daily, utc(2026, 11, 6, 7, 59)) == utc(2026, 11, 5, 8, 0));
    CHECK_FALSE(lastDueTime(timedTask(QStringLiteral("manual"), QStringLiteral("08:00")), utc(2026, 11, 6, 9, 0)).isValid());
    CHECK_FALSE(lastDueTime(timedTask(QStringLiteral("daily"), QStringLiteral("25:00")), utc(2026, 11, 6, 9, 0)).isValid());

    CHECK(checkSchedule(daily, utc(2026, 11, 6, 7, 59, 30), utc(2026, 11, 6, 7, 59, 50)).verdict == ScheduleVerdict::Idle);
    const ScheduleCheck due = checkSchedule(daily, utc(2026, 11, 6, 7, 59, 50), utc(2026, 11, 6, 8, 0, 10));
    CHECK(due.verdict == ScheduleVerdict::Due);
    CHECK(due.at == utc(2026, 11, 6, 8, 0));
    CHECK(checkSchedule(daily, utc(2026, 11, 6, 7, 30), utc(2026, 11, 6, 8, 3)).verdict == ScheduleVerdict::Due);
    CHECK(checkSchedule(daily, utc(2026, 11, 6, 8, 4), utc(2026, 11, 6, 8, 4, 30)).verdict == ScheduleVerdict::Due);
    const ScheduleCheck missed = checkSchedule(daily, utc(2026, 11, 6, 7, 0), utc(2026, 11, 6, 9, 30));
    CHECK(missed.verdict == ScheduleVerdict::Missed);
    CHECK(missed.at == utc(2026, 11, 6, 8, 0));
    CHECK(checkSchedule(daily, utc(2026, 11, 6, 9, 29, 40), utc(2026, 11, 6, 9, 30)).verdict == ScheduleVerdict::Idle);
    CHECK(checkSchedule(daily, utc(2026, 11, 6, 9, 30), utc(2026, 11, 6, 9, 30)).verdict == ScheduleVerdict::Idle);
    CHECK(checkSchedule(daily, QDateTime(), utc(2026, 11, 6, 9, 30)).verdict == ScheduleVerdict::Idle);

    TaskEntry ran = daily;
    ran.lastScheduled = QStringLiteral("2026-11-06");
    CHECK(checkSchedule(ran, utc(2026, 11, 6, 7, 59, 50), utc(2026, 11, 6, 8, 0, 10)).verdict == ScheduleVerdict::Idle);
    CHECK(checkSchedule(ran, utc(2026, 11, 6, 7, 0), utc(2026, 11, 6, 9, 30)).verdict == ScheduleVerdict::Idle);
    CHECK(checkSchedule(ran, utc(2026, 11, 7, 7, 59, 50), utc(2026, 11, 7, 8, 0, 10)).verdict == ScheduleVerdict::Due);

    const TaskEntry weekly = timedTask(QStringLiteral("weekly"), QStringLiteral("08:00"), {1, 3});
    CHECK(checkSchedule(weekly, utc(2026, 11, 6, 7, 59, 50), utc(2026, 11, 6, 8, 0, 10)).verdict == ScheduleVerdict::Idle);
    CHECK(checkSchedule(weekly, utc(2026, 11, 4, 7, 59, 50), utc(2026, 11, 4, 8, 0, 10)).verdict == ScheduleVerdict::Due);
    const ScheduleCheck slept = checkSchedule(weekly, utc(2026, 11, 3, 22, 0), utc(2026, 11, 5, 10, 0));
    CHECK(slept.verdict == ScheduleVerdict::Missed);
    CHECK(slept.at == utc(2026, 11, 4, 8, 0));
    CHECK(checkSchedule(timedTask(QStringLiteral("weekly"), QStringLiteral("08:00")), utc(2026, 11, 4, 7, 59, 50),
                        utc(2026, 11, 4, 8, 0, 10))
              .verdict == ScheduleVerdict::Idle);

    TaskEntry once = timedTask(QStringLiteral("once"), QStringLiteral("08:00"));
    CHECK(checkSchedule(once, utc(2026, 11, 6, 7, 59, 50), utc(2026, 11, 6, 8, 0, 10)).verdict == ScheduleVerdict::Due);
    once.lastScheduled = QStringLiteral("2026-11-05");
    CHECK(checkSchedule(once, utc(2026, 11, 6, 7, 59, 50), utc(2026, 11, 6, 8, 0, 10)).verdict == ScheduleVerdict::Idle);
    CHECK(checkSchedule(timedTask(QStringLiteral("manual"), QStringLiteral("08:00")), utc(2026, 11, 6, 7, 59, 50),
                        utc(2026, 11, 6, 8, 0, 10))
              .verdict == ScheduleVerdict::Idle);
}

TEST_CASE("ADR-0091: closing the main window keeps GameScrcpy in the tray only while a task has a schedule and the setting is on")
{
    const std::vector<TaskEntry> byHand{timedTask(QStringLiteral("manual"), QStringLiteral("08:00"))};
    std::vector<TaskEntry> timed = byHand;
    timed.push_back(timedTask(QStringLiteral("weekly"), QStringLiteral("08:00"), {2}));
    CHECK_FALSE(closesToTray(true, {}));
    CHECK_FALSE(closesToTray(true, byHand));
    CHECK(closesToTray(true, timed));
    CHECK_FALSE(closesToTray(false, timed));
    CHECK(closesToTray(true, {timedTask(QStringLiteral("once"), QStringLiteral("23:59"))}));
}

TEST_CASE("ADR-0091: a scheduled start wakes the phone, and puts back to sleep only a phone it connected once nothing else runs on it")
{
    const ScreenPlan both{.wake = true, .sleepAfter = true};
    const ScreenPlan wakeOnly{.wake = true, .sleepAfter = false};
    const ScreenPlan none{};
    CHECK(screenPlanFor(true, true) == both);
    CHECK(screenPlanFor(true, false) == wakeOnly);
    CHECK(screenPlanFor(false, true) == none);
    CHECK(screenPlanFor(false, false) == none);
    CHECK(sleepsWhenDone(true, 0));
    CHECK_FALSE(sleepsWhenDone(true, 1));
    CHECK_FALSE(sleepsWhenDone(false, 0));
}
