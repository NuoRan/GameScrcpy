#include "AppSettings.h"
#include "Recordings.h"

#include <gs/script/Session.h>

#include <doctest/doctest.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>

#include <cstdint>
#include <filesystem>

using gs::app::AppSettings;
using gs::app::Recordings;
using gs::app::recordingEntries;

namespace {

namespace script = gs::script;

void writeFile(const QString& path, const QByteArray& bytes)
{
    REQUIRE(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QString makeRecording(const QString& location, const QString& name, std::int64_t startedAtMs, int operations)
{
    const QString folder = QDir(location).filePath(name + QStringLiteral(".gsrec"));
    script::Session session;
    session.device = script::DeviceInfo{.model = "Test phone", .android = 15, .width = 1080, .height = 2400};
    session.startedAtMs = startedAtMs;
    session.durationMs = 65'000;
    for (int index = 0; index < operations; ++index) {
        session.operations.push_back(
            script::SessionOperation{gs::Microseconds{1000 * (index + 1)}, script::SessionText{3}});
    }
    writeFile(QDir(folder).filePath(QStringLiteral("session.json")),
              QByteArray::fromStdString(script::writeSession(session)));
    return folder;
}

void writeInfo(const QString& folder, const QString& serial)
{
    const QJsonObject info{{QStringLiteral("serial"), serial},
                           {QStringLiteral("phone"), QStringLiteral("Pixel 8")},
                           {QStringLiteral("profileId"), QStringLiteral("shooter")},
                           {QStringLiteral("profileName"), QStringLiteral("Shooter")}};
    writeFile(QDir(folder).filePath(QStringLiteral("recording.json")), QJsonDocument(info).toJson());
}

class DataDirectory
{
public:
    explicit DataDirectory(const QString& path)
        : m_before(qgetenv("GS_DATA_DIR"))
        , m_had(qEnvironmentVariableIsSet("GS_DATA_DIR"))
    {
        qputenv("GS_DATA_DIR", path.toUtf8());
    }
    ~DataDirectory()
    {
        if (m_had) {
            qputenv("GS_DATA_DIR", m_before);
        } else {
            qunsetenv("GS_DATA_DIR");
        }
    }
    DataDirectory(const DataDirectory&) = delete;
    DataDirectory& operator=(const DataDirectory&) = delete;

private:
    QByteArray m_before;
    bool m_had = false;
};

}

TEST_CASE("ADR-0106: the recordings list shows this phone's recordings newest first, by the serial written when "
          "recording or, for older recordings, by the phone's name")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString location = root.path();
    const QString newest = makeRecording(location, QStringLiteral("Pixel 8 2026-11-07 10.00.00"), 1'762'509'600'000, 3);
    writeInfo(newest, QStringLiteral("S1"));
    QImage frame(40, 90, QImage::Format_RGB32);
    frame.fill(Qt::darkGray);
    REQUIRE(QDir(newest).mkpath(QStringLiteral("thumbnails")));
    REQUIRE(frame.save(QDir(newest).filePath(QStringLiteral("thumbnails/0.png"))));
    const QString older = makeRecording(location, QStringLiteral("Pixel 8 2026-11-06 09.00.00"), 1'762'419'600'000, 1);
    writeInfo(makeRecording(location, QStringLiteral("Pixel 8 2026-11-05 08.00.00"), 1'762'329'600'000, 1),
              QStringLiteral("S2"));
    makeRecording(location, QStringLiteral("Other phone 2026-11-07 11.00.00"), 1'762'513'200'000, 1);
    REQUIRE(QDir(location).mkpath(QStringLiteral("Pixel 8 notes")));

    const QVariantList list = recordingEntries(location, QStringLiteral("S1"), QStringLiteral("Pixel 8"), 100);
    REQUIRE(list.size() == 2);
    const QVariantMap first = list[0].toMap();
    CHECK(first.value(QStringLiteral("folder")).toString() == QDir::toNativeSeparators(newest));
    CHECK(first.value(QStringLiteral("name")).toString() == QStringLiteral("Pixel 8 2026-11-07 10.00.00"));
    CHECK(first.value(QStringLiteral("serial")).toString() == QStringLiteral("S1"));
    CHECK(first.value(QStringLiteral("profileId")).toString() == QStringLiteral("shooter"));
    CHECK(first.value(QStringLiteral("profileName")).toString() == QStringLiteral("Shooter"));
    CHECK(first.value(QStringLiteral("duration")).toString() == QStringLiteral("01:05"));
    CHECK(first.value(QStringLiteral("operations")).toInt() == 3);
    CHECK(first.value(QStringLiteral("width")).toInt() == 1080);
    CHECK_FALSE(first.value(QStringLiteral("broken")).toBool());
    CHECK(QUrl(first.value(QStringLiteral("thumbnail")).toString()).toLocalFile() ==
          QDir(newest).filePath(QStringLiteral("thumbnails/0.png")));
    CHECK(first.value(QStringLiteral("thumbnailAspect")).toDouble() == doctest::Approx(90.0 / 40.0));
    CHECK_FALSE(first.value(QStringLiteral("when")).toString().isEmpty());
    const QVariantMap second = list[1].toMap();
    CHECK(second.value(QStringLiteral("folder")).toString() == QDir::toNativeSeparators(older));
    CHECK(second.value(QStringLiteral("serial")).toString() == QStringLiteral("S1"));
    CHECK(second.value(QStringLiteral("phone")).toString() == QStringLiteral("Pixel 8"));
    CHECK(second.value(QStringLiteral("profileId")).toString().isEmpty());
    CHECK(second.value(QStringLiteral("operations")).toInt() == 1);
    CHECK(second.value(QStringLiteral("thumbnail")).toString().isEmpty());

    CHECK(recordingEntries(location, QStringLiteral("S1"), QStringLiteral("Pixel 8"), 1).size() == 1);
    CHECK(recordingEntries(location, QStringLiteral("S2"), QStringLiteral("Pixel 8"), 100).size() == 2);
    CHECK(recordingEntries(location, QStringLiteral("S3"), QString(), 100).isEmpty());
}

TEST_CASE("ADR-0106: a recording whose session cannot be read is listed as broken, with the reason")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString folder = QDir(root.path()).filePath(QStringLiteral("Pixel 8 2026-11-07 10.00.00.gsrec"));
    writeFile(QDir(folder).filePath(QStringLiteral("session.json")), QByteArrayLiteral("{ not json"));
    writeInfo(folder, QStringLiteral("S1"));

    const QVariantList list = recordingEntries(root.path(), QStringLiteral("S1"), QStringLiteral("Pixel 8"), 100);
    REQUIRE(list.size() == 1);
    const QVariantMap entry = list[0].toMap();
    CHECK(entry.value(QStringLiteral("broken")).toBool());
    CHECK(entry.value(QStringLiteral("reason")).toString().startsWith(QStringLiteral("session.json")));
    CHECK(entry.value(QStringLiteral("profileName")).toString() == QStringLiteral("Shooter"));
    CHECK(entry.value(QStringLiteral("when")).toString() == QStringLiteral("2026-11-07 10:00"));
}

TEST_CASE("ADR-0106: only a recording folder inside the recordings folder can be moved to the Recycle Bin")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const DataDirectory data(root.path());
    AppSettings settings(std::filesystem::path(root.path().toStdWString()) / "settings");
    Recordings recordings(settings);
    const QString location = QDir(root.path()).filePath(QStringLiteral("recordings"));
    REQUIRE(QDir::cleanPath(recordings.location()) == QDir::cleanPath(QDir::toNativeSeparators(location)));

    const QString elsewhere = makeRecording(QDir(root.path()).filePath(QStringLiteral("elsewhere")),
                                            QStringLiteral("Pixel 8 2026-11-07 10.00.00"), 1'762'509'600'000, 1);
    CHECK_FALSE(recordings.moveToTrash(elsewhere).isEmpty());
    CHECK(QFileInfo::exists(elsewhere));

    const QString notes = QDir(location).filePath(QStringLiteral("Pixel 8 notes"));
    REQUIRE(QDir().mkpath(notes));
    CHECK_FALSE(recordings.moveToTrash(notes).isEmpty());
    CHECK(QFileInfo::exists(notes));

    CHECK_FALSE(recordings.moveToTrash(QString()).isEmpty());
}

TEST_CASE("ADR-0106: opening a recording from the list reads it again, with every thumbnail saved for it")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const DataDirectory data(root.path());
    AppSettings settings(std::filesystem::path(root.path().toStdWString()) / "settings");
    const Recordings recordings(settings);
    const QString folder = makeRecording(QDir(root.path()).filePath(QStringLiteral("recordings")),
                                         QStringLiteral("Pixel 8 2026-11-07 10.00.00"), 1'762'509'600'000, 2);
    REQUIRE(QDir(folder).mkpath(QStringLiteral("thumbnails")));
    QImage frame(40, 90, QImage::Format_RGB32);
    frame.fill(Qt::darkGray);
    REQUIRE(frame.save(QDir(folder).filePath(QStringLiteral("thumbnails/0.png"))));
    REQUIRE(frame.save(QDir(folder).filePath(QStringLiteral("thumbnails/1.png"))));

    const QVariantMap info = recordings.describe(QDir::toNativeSeparators(folder), QStringLiteral("S1"));
    CHECK(info.value(QStringLiteral("folder")).toString() == QDir::toNativeSeparators(folder));
    CHECK(info.value(QStringLiteral("serial")).toString() == QStringLiteral("S1"));
    CHECK(info.value(QStringLiteral("phone")).toString() == QStringLiteral("Pixel 8"));
    CHECK(info.value(QStringLiteral("duration")).toString() == QStringLiteral("01:05"));
    CHECK(info.value(QStringLiteral("operations")).toInt() == 2);
    const QVariantList thumbnails = info.value(QStringLiteral("thumbnails")).toList();
    REQUIRE(thumbnails.size() == 2);
    CHECK(thumbnails[1].value<QImage>().size() == QSize(40, 90));

    CHECK(recordings.describe(QDir(root.path()).filePath(QStringLiteral("missing.gsrec")), QStringLiteral("S1"))
              .isEmpty());
    CHECK(recordings.describe(QString(), QStringLiteral("S1")).isEmpty());
}
