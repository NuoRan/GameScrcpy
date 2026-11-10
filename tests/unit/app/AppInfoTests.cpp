#include "AppInfo.h"

#include <doctest/doctest.h>

#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <filesystem>
#include <set>
#include <string>

using gs::app::AppInfo;

namespace {

QVariantMap licenseNamed(const QVariantList& licenses, const QString& name)
{
    for (const QVariant& entry : licenses) {
        if (entry.toMap().value(QStringLiteral("name")).toString() == name) {
            return entry.toMap();
        }
    }
    return {};
}

}

TEST_CASE("the license list names every directory under third_party, each with a version, a license and its text "
          "(REL-002)")
{
    const QVariantList licenses = AppInfo().licenses();
    REQUIRE_FALSE(licenses.isEmpty());
    std::set<std::string> directories;
    for (const QVariant& entry : licenses) {
        const QVariantMap license = entry.toMap();
        CAPTURE(license.value(QStringLiteral("name")).toString().toStdString());
        CHECK_FALSE(license.value(QStringLiteral("name")).toString().isEmpty());
        CHECK_FALSE(license.value(QStringLiteral("version")).toString().isEmpty());
        CHECK_FALSE(license.value(QStringLiteral("license")).toString().isEmpty());
        CHECK(license.value(QStringLiteral("text")).toString().size() > 200);
        const QString directory = license.value(QStringLiteral("directory")).toString();
        if (!directory.isEmpty()) {
            directories.insert(directory.section(QLatin1Char('/'), 0, 0).toStdString());
        }
    }
    int listed = 0;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(GS_THIRD_PARTY_DIR)) {
        if (entry.is_directory()) {
            const std::string name = entry.path().filename().string();
            CAPTURE(name);
            CHECK(directories.contains(name));
            ++listed;
        }
    }
    CHECK(listed > 5);

    const QVariantMap ffmpeg = licenseNamed(licenses, QStringLiteral("FFmpeg"));
    CHECK(ffmpeg.value(QStringLiteral("license")).toString() == QStringLiteral("LGPL-2.1-or-later"));
    CHECK(ffmpeg.value(QStringLiteral("text")).toString().contains(QStringLiteral("GNU LESSER GENERAL PUBLIC LICENSE")));
    CHECK(ffmpeg.value(QStringLiteral("text")).toString().contains(QStringLiteral("Version 2.1")));
    CHECK_FALSE(licenseNamed(licenses, QStringLiteral("dav1d")).isEmpty());
    const QVariantMap libusb = licenseNamed(licenses, QStringLiteral("libusb"));
    CHECK(libusb.value(QStringLiteral("license")).toString() == QStringLiteral("LGPL-2.1-or-later"));
    CHECK(libusb.value(QStringLiteral("version")).toString() == QStringLiteral("1.0.30"));
    CHECK(libusb.value(QStringLiteral("text")).toString().contains(QStringLiteral("GNU LESSER GENERAL PUBLIC LICENSE")));
    CHECK(licenseNamed(licenses, QStringLiteral("Qt")).value(QStringLiteral("license")).toString() ==
          QStringLiteral("LGPL-3.0-only"));
    CHECK_FALSE(licenseNamed(licenses, QStringLiteral("Microsoft Visual C++ runtime")).isEmpty());
    const QVariantMap adb = licenseNamed(licenses, QStringLiteral("Android SDK Platform-Tools (adb)"));
    CHECK(adb.value(QStringLiteral("license")).toString().startsWith(QStringLiteral("Apache-2.0")));
    CHECK(adb.value(QStringLiteral("text")).toString().contains(QStringLiteral("/bin/adb")));
}

TEST_CASE("the licenses folder next to the programs holds every license text and an index (REL-002)")
{
    const std::filesystem::path folder = GS_LICENSES_FOLDER;
    CHECK(std::filesystem::is_regular_file(folder / "README.txt"));
    for (const QVariant& entry : AppInfo().licenses()) {
        const std::filesystem::path file =
            folder / entry.toMap().value(QStringLiteral("file")).toString().toStdWString();
        CAPTURE(file.string());
        CHECK(std::filesystem::is_regular_file(file));
        CHECK(std::filesystem::file_size(file) > 200);
    }
}

TEST_CASE("the build names its configuration, the compiler and the Qt it runs on")
{
    const QString build = AppInfo().build();
    CHECK(build.startsWith(QStringLiteral(GS_BUILD_CONFIG)));
    CHECK(build.contains(QStringLiteral("MSVC ")));
    CHECK(build.contains(QString::fromLatin1(qVersion())));
}
