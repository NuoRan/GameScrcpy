#include "DeviceBatch.h"

#include <doctest/doctest.h>

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <utility>
#include <vector>

using gs::app::BatchPhone;
using gs::app::DeviceBatch;

namespace {

const QString kA = QStringLiteral("A");
const QString kB = QStringLiteral("B");
const QString kC = QStringLiteral("C");
const QString kD = QStringLiteral("D");

struct Recorder {
    std::vector<std::pair<QString, QString>> tasks;
    std::vector<QStringList> launches;
    std::vector<std::pair<QStringList, QString>> installs;
    std::vector<std::pair<QStringList, QStringList>> pushes;
    QStringList refusing;

    DeviceBatch::Actions actions()
    {
        return {.startTask =
                    [this](const QString& task, const QString& serial) {
                        tasks.emplace_back(task, serial);
                        return refusing.contains(serial) ? QStringLiteral("%1 is not mirroring").arg(serial)
                                                         : QString();
                    },
                .launchApp =
                    [this](const QString& serial, const QString& package, const QString& label) {
                        launches.push_back({serial, package, label});
                    },
                .installApk =
                    [this](const QStringList& serials, const QString& path) { installs.emplace_back(serials, path); },
                .pushFiles =
                    [this](const QStringList& serials, const QStringList& paths) {
                        pushes.emplace_back(serials, paths);
                    }};
    }
};

std::vector<BatchPhone> phones()
{
    return {{.serial = kA, .name = QStringLiteral("Pixel"), .online = true, .mirroring = true, .apps = true},
            {.serial = kB, .name = QStringLiteral("Mate"), .online = true},
            {.serial = kC, .name = QStringLiteral("Demo device"), .mirroring = true},
            {.serial = kD, .name = QStringLiteral("Old phone")}};
}

QVariant field(const DeviceBatch& batch, qsizetype row, const char* key)
{
    return batch.jobs().at(row).toMap().value(QString::fromLatin1(key));
}

QString text(const DeviceBatch& batch, qsizetype row, const char* key)
{
    return field(batch, row, key).toString();
}

}

TEST_CASE("ticking phones enters multi-select and enables only the batch actions the ticked phones can do (ADR-0096)")
{
    Recorder recorder;
    DeviceBatch batch(recorder.actions());
    int selections = 0;
    int availabilities = 0;
    QObject::connect(&batch, &DeviceBatch::selectionChanged, [&selections] { ++selections; });
    QObject::connect(&batch, &DeviceBatch::availabilityChanged, [&availabilities] { ++availabilities; });
    batch.setPhones(phones());
    CHECK_FALSE(batch.selecting());
    CHECK_FALSE(batch.canStartTask());
    CHECK_FALSE(batch.canLaunchApp());
    CHECK_FALSE(batch.canInstall());
    CHECK_FALSE(batch.canPushFiles());

    batch.setSelected(QStringLiteral("Z"), true);
    CHECK_FALSE(batch.selecting());
    CHECK(selections == 0);

    batch.setSelected(kD, true);
    CHECK(batch.selecting());
    CHECK(batch.count() == 1);
    CHECK(selections == 1);
    CHECK_FALSE(batch.canStartTask());
    CHECK_FALSE(batch.canInstall());
    CHECK(availabilities == 0);

    batch.toggle(kB);
    CHECK(batch.selected() == QStringList{kD, kB});
    CHECK(batch.canInstall());
    CHECK(batch.canPushFiles());
    CHECK_FALSE(batch.canStartTask());
    CHECK_FALSE(batch.canLaunchApp());
    CHECK(batch.appSource().isEmpty());
    CHECK(availabilities == 1);

    batch.setSelected(kC, true);
    CHECK(batch.canStartTask());
    CHECK_FALSE(batch.canLaunchApp());
    CHECK(batch.appSource().isEmpty());

    batch.setSelected(kA, true);
    CHECK(batch.canLaunchApp());
    CHECK(batch.appSource() == kA);
    const int before = selections;
    batch.setSelected(kA, true);
    CHECK(selections == before);
    CHECK(batch.count() == 4);

    std::vector<BatchPhone> fewer = phones();
    fewer.erase(fewer.begin());
    batch.setPhones(fewer);
    CHECK(batch.selected() == QStringList{kD, kB, kC});
    CHECK(selections == before + 1);
    CHECK_FALSE(batch.canLaunchApp());
    CHECK(batch.appSource().isEmpty());

    batch.toggle(kD);
    CHECK(batch.count() == 2);
    batch.clear();
    CHECK_FALSE(batch.selecting());
    CHECK_FALSE(batch.canInstall());
    const int cleared = selections;
    batch.clear();
    CHECK(selections == cleared);
}

TEST_CASE("a batch start runs the task once on each ticked phone and lists each phone's result (ADR-0096)")
{
    Recorder recorder;
    recorder.refusing = {kB};
    DeviceBatch batch(recorder.actions());
    batch.setPhones(phones());
    int changes = 0;
    QObject::connect(&batch, &DeviceBatch::jobsChanged, [&changes] { ++changes; });
    batch.startTask(QStringLiteral("daily"), QStringLiteral("Daily check-in"));
    CHECK(recorder.tasks.empty());
    CHECK(changes == 0);

    batch.setSelected(kA, true);
    batch.setSelected(kB, true);
    batch.setSelected(kC, true);
    batch.startTask(QStringLiteral("daily"), QStringLiteral("Daily check-in"));
    REQUIRE(recorder.tasks.size() == 3);
    CHECK(recorder.tasks[0].first == QStringLiteral("daily"));
    CHECK(recorder.tasks[0].second == kA);
    CHECK(recorder.tasks[1].second == kB);
    CHECK(recorder.tasks[2].second == kC);
    CHECK(changes == 1);

    REQUIRE(batch.jobs().size() == 3);
    CHECK(text(batch, 0, "kind") == QStringLiteral("task"));
    CHECK(text(batch, 0, "phone") == QStringLiteral("Pixel"));
    CHECK(text(batch, 0, "name") == QStringLiteral("Daily check-in"));
    CHECK(text(batch, 0, "state") == QStringLiteral("done"));
    CHECK(text(batch, 1, "state") == QStringLiteral("failed"));
    CHECK(text(batch, 1, "status") == QStringLiteral("B is not mirroring"));
    CHECK(field(batch, 1, "failed").toBool());
    CHECK(text(batch, 2, "state") == QStringLiteral("done"));
    CHECK(batch.running() == 0);
    CHECK(text(batch, 0, "key") != text(batch, 1, "key"));

    batch.clearFinished();
    CHECK(batch.jobs().isEmpty());
    CHECK(changes == 2);
}

TEST_CASE("a batch launch starts the app where it can, and a phone without the app says so (ADR-0096)")
{
    Recorder recorder;
    DeviceBatch batch(recorder.actions());
    batch.setPhones(phones());
    for (const QString& serial : {kA, kB, kC}) {
        batch.setSelected(serial, true);
    }
    batch.launchApp(QStringLiteral("  "), {});
    batch.launchApp(QStringLiteral("com.example game"), {});
    CHECK(recorder.launches.empty());
    CHECK(batch.jobs().isEmpty());

    batch.launchApp(QStringLiteral(" com.example.game "), QStringLiteral("Game"));
    REQUIRE(recorder.launches.size() == 1);
    CHECK(recorder.launches[0] == QStringList{kA, QStringLiteral("com.example.game"), QStringLiteral("Game")});
    REQUIRE(batch.jobs().size() == 3);
    CHECK(text(batch, 0, "kind") == QStringLiteral("app"));
    CHECK(text(batch, 0, "state") == QStringLiteral("done"));
    CHECK(text(batch, 0, "status") == QStringLiteral("Started"));
    CHECK(text(batch, 1, "status") == QStringLiteral("Not mirroring"));
    CHECK(text(batch, 2, "status") == QStringLiteral("This phone cannot start apps"));

    batch.appShown(kA, QStringLiteral("com.other"));
    batch.appRefused(kB);
    CHECK(text(batch, 1, "status") == QStringLiteral("Not mirroring"));
    batch.appRefused(kA);
    CHECK(text(batch, 0, "state") == QStringLiteral("failed"));
    CHECK(text(batch, 0, "status") == QStringLiteral("This app is not on the phone"));

    batch.launchApp(QStringLiteral("com.example.game"), {});
    REQUIRE(recorder.launches.size() == 2);
    CHECK(recorder.launches[1][2] == QStringLiteral("com.example.game"));
    REQUIRE(batch.jobs().size() == 6);
    batch.appShown(kA, QStringLiteral("com.example.game"));
    batch.appRefused(kA);
    CHECK(text(batch, 3, "state") == QStringLiteral("done"));
    CHECK(text(batch, 0, "state") == QStringLiteral("failed"));
}

TEST_CASE("installs and sends go to the phones on adb, and each row follows the job reported for it (ADR-0096)")
{
    Recorder recorder;
    DeviceBatch batch(recorder.actions());
    batch.setPhones(phones());
    for (const QString& serial : {kA, kB, kC}) {
        batch.setSelected(serial, true);
    }
    batch.installApk(QUrl(QStringLiteral("https://example.com/game.apk")));
    CHECK(recorder.installs.empty());

    const QString apk = QStringLiteral("C:/Downloads/game.apk");
    batch.installApk(QUrl::fromLocalFile(apk));
    REQUIRE(recorder.installs.size() == 1);
    CHECK(recorder.installs[0].first == QStringList{kA, kB});
    CHECK(recorder.installs[0].second == apk);
    REQUIRE(batch.jobs().size() == 3);
    CHECK(text(batch, 0, "kind") == QStringLiteral("apk"));
    CHECK(text(batch, 0, "name") == QStringLiteral("game.apk"));
    CHECK(text(batch, 0, "status") == QStringLiteral("Waiting"));
    CHECK(text(batch, 2, "state") == QStringLiteral("failed"));
    CHECK(text(batch, 2, "status") == QStringLiteral("Not connected over adb"));
    CHECK(batch.running() == 2);

    batch.installUpdated({{QStringLiteral("id"), 41},
                          {QStringLiteral("serial"), kB},
                          {QStringLiteral("name"), QStringLiteral("game.apk")},
                          {QStringLiteral("state"), QStringLiteral("copying")},
                          {QStringLiteral("done"), 25.0},
                          {QStringLiteral("total"), 100.0}});
    CHECK(text(batch, 1, "status") == QStringLiteral("Copying 25%"));
    CHECK(field(batch, 1, "progress").toDouble() == doctest::Approx(0.25));
    CHECK(field(batch, 0, "progress").toDouble() < 0.0);
    batch.installUpdated({{QStringLiteral("id"), 42},
                          {QStringLiteral("serial"), QStringLiteral("Z")},
                          {QStringLiteral("name"), QStringLiteral("game.apk")},
                          {QStringLiteral("state"), QStringLiteral("copying")}});
    CHECK(batch.jobs().size() == 3);
    batch.installUpdated({{QStringLiteral("id"), 41}, {QStringLiteral("state"), QStringLiteral("installing")}});
    CHECK(text(batch, 1, "status") == QStringLiteral("Installing…"));
    batch.installUpdated({{QStringLiteral("id"), 41}, {QStringLiteral("state"), QStringLiteral("done")}});
    CHECK(text(batch, 1, "status") == QStringLiteral("Installed"));
    batch.installUpdated({{QStringLiteral("id"), 40},
                          {QStringLiteral("serial"), kA},
                          {QStringLiteral("name"), QStringLiteral("game.apk")},
                          {QStringLiteral("state"), QStringLiteral("failed")},
                          {QStringLiteral("message"), QStringLiteral("Not enough space")}});
    CHECK(text(batch, 0, "status") == QStringLiteral("Not enough space"));
    CHECK(batch.running() == 0);

    batch.clearFinished();
    CHECK(batch.jobs().isEmpty());
    batch.pushFiles({QUrl::fromLocalFile(QStringLiteral("C:/a.png")), QUrl::fromLocalFile(QStringLiteral("C:/b.txt")),
                     QUrl()});
    REQUIRE(recorder.pushes.size() == 1);
    CHECK(recorder.pushes[0].first == QStringList{kA, kB});
    CHECK(recorder.pushes[0].second == QStringList{QStringLiteral("C:/a.png"), QStringLiteral("C:/b.txt")});
    REQUIRE(batch.jobs().size() == 5);
    CHECK(text(batch, 0, "kind") == QStringLiteral("file"));
    CHECK(text(batch, 3, "phone") == QStringLiteral("Mate"));
    CHECK(text(batch, 3, "name") == QStringLiteral("b.txt"));
    CHECK(text(batch, 4, "name") == QStringLiteral("2 file(s)"));
    CHECK(text(batch, 4, "status") == QStringLiteral("Not connected over adb"));

    batch.transferUpdated({{QStringLiteral("id"), 7},
                           {QStringLiteral("serial"), kB},
                           {QStringLiteral("direction"), QStringLiteral("pull")},
                           {QStringLiteral("name"), QStringLiteral("b.txt")},
                           {QStringLiteral("state"), QStringLiteral("running")}});
    CHECK(text(batch, 3, "state") == QStringLiteral("queued"));
    batch.transferUpdated({{QStringLiteral("id"), 7},
                           {QStringLiteral("serial"), kB},
                           {QStringLiteral("direction"), QStringLiteral("push")},
                           {QStringLiteral("name"), QStringLiteral("b.txt")},
                           {QStringLiteral("state"), QStringLiteral("running")},
                           {QStringLiteral("done"), 1.0},
                           {QStringLiteral("total"), 4.0}});
    CHECK(text(batch, 3, "status") == QStringLiteral("Sending 25%"));
    batch.transferUpdated({{QStringLiteral("id"), 7},
                           {QStringLiteral("direction"), QStringLiteral("push")},
                           {QStringLiteral("state"), QStringLiteral("done")}});
    CHECK(text(batch, 3, "status") == QStringLiteral("In Download"));
    batch.transferUpdated({{QStringLiteral("id"), 8},
                           {QStringLiteral("serial"), kA},
                           {QStringLiteral("direction"), QStringLiteral("push")},
                           {QStringLiteral("name"), QStringLiteral("a.png")},
                           {QStringLiteral("state"), QStringLiteral("cancelled")}});
    CHECK(text(batch, 0, "status") == QStringLiteral("Cancelled"));
    CHECK(batch.running() == 2);
}
