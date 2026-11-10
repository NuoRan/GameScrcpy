#include "AppSettings.h"
#include "CrashReport.h"

#include <gs/platform/CrashHandler.h>

#include <doctest/doctest.h>

#include <QObject>
#include <QString>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using gs::app::AppSettings;
using gs::app::CrashReport;

namespace {

class TemporaryDirectory
{
public:
    TemporaryDirectory()
        : m_path(std::filesystem::temp_directory_path() /
                 ("gs-crash-report-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                  "-" + std::to_string(s_next++)))
    {
        std::filesystem::create_directories(m_path);
    }
    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return m_path; }

private:
    static inline std::atomic<int> s_next = 0;
    std::filesystem::path m_path;
};

std::string contents(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}

TEST_CASE("crash folders are told apart by their name (REL-004)")
{
    CHECK(gs::app::isCrashFolder("crash-20261030-105812-345"));
    CHECK(gs::app::isCrashFolder("crash-19991231-235959-999"));
    CHECK_FALSE(gs::app::isCrashFolder("crash-20261030-105812"));
    CHECK_FALSE(gs::app::isCrashFolder("crash-20261030-105812-3456"));
    CHECK_FALSE(gs::app::isCrashFolder("crash-2026103a-105812-345"));
    CHECK_FALSE(gs::app::isCrashFolder("crash-20261030_105812-345"));
    CHECK_FALSE(gs::app::isCrashFolder("Crash-20261030-105812-345"));
    CHECK_FALSE(gs::app::isCrashFolder(""));
    CHECK(gs::platform::kCrashFolderLength == std::string_view("crash-20261030-105812-345").size());
}

TEST_CASE("only the five newest crash folders are kept, and nothing else in the folder is touched (REL-004)")
{
    const TemporaryDirectory directory;
    const std::vector<std::string> names{
        "crash-20261024-100000-000", "crash-20261025-100000-000", "crash-20261026-100000-000",
        "crash-20261027-100000-000", "crash-20261028-100000-000", "crash-20261029-100000-000",
        "crash-20261030-100000-000"};
    for (auto it = names.rbegin(); it != names.rend(); ++it) {
        std::filesystem::create_directories(directory.path() / *it);
        std::ofstream(directory.path() / *it / "gamescrcpy.log") << "line\n";
    }
    std::filesystem::create_directories(directory.path() / "notes");
    std::ofstream(directory.path() / "crash-20261001-100000-000") << "a file, not a crash folder\n";

    gs::app::keepNewestCrashes(directory.path(), CrashReport::kKept);

    CHECK(gs::app::crashFolders(directory.path()) == std::vector<std::string>(names.begin() + 2, names.end()));
    CHECK(std::filesystem::is_directory(directory.path() / "notes"));
    CHECK(std::filesystem::is_regular_file(directory.path() / "crash-20261001-100000-000"));
    CHECK_FALSE(std::filesystem::exists(directory.path() / names[0]));
    CHECK_FALSE(std::filesystem::exists(directory.path() / names[1]));
}

TEST_CASE("a crash is due for the notice until it or a newer one is acknowledged (REL-004)")
{
    const std::vector<std::string> folders{"crash-20261029-080000-000", "crash-20261030-105812-345"};
    CHECK_FALSE(gs::app::unacknowledgedCrash({}, "").has_value());
    CHECK_FALSE(gs::app::unacknowledgedCrash({}, "crash-20261030-105812-345").has_value());
    CHECK(gs::app::unacknowledgedCrash(folders, "") == "crash-20261030-105812-345");
    CHECK(gs::app::unacknowledgedCrash(folders, "crash-20261029-080000-000") == "crash-20261030-105812-345");
    CHECK_FALSE(gs::app::unacknowledgedCrash(folders, "crash-20261030-105812-345").has_value());
    CHECK_FALSE(gs::app::unacknowledgedCrash(folders, "crash-20261101-000000-000").has_value());
    CHECK(gs::app::unacknowledgedCrash(folders, "not a crash") == "crash-20261030-105812-345");
}

TEST_CASE("the next start shows the notice once per crash, acknowledged in the settings file (REL-004)")
{
    const TemporaryDirectory directory;
    const std::filesystem::path crashes = directory.path() / "crashes";
    {
        AppSettings settings(directory.path());
        const CrashReport report(settings, crashes);
        CHECK_FALSE(report.pending());
        CHECK_FALSE(report.newestFolder().has_value());
    }

    std::filesystem::create_directories(crashes / "crash-20261029-080000-000");
    std::filesystem::create_directories(crashes / "crash-20261030-105812-345");
    {
        AppSettings settings(directory.path());
        CrashReport report(settings, crashes);
        int changes = 0;
        QObject::connect(&report, &CrashReport::pendingChanged, &report, [&changes] { ++changes; });
        CHECK(report.pending());
        CHECK(report.newestFolder() == crashes / "crash-20261030-105812-345");
        report.acknowledge();
        CHECK_FALSE(report.pending());
        CHECK(changes == 1);
        report.acknowledge();
        CHECK(changes == 1);
    }
    CHECK(contents(directory.path() / "settings.json").find("\"crashNotice\": \"crash-20261030-105812-345\"") !=
          std::string::npos);

    {
        AppSettings settings(directory.path());
        CHECK(settings.acknowledgedCrash() == QStringLiteral("crash-20261030-105812-345"));
        const CrashReport report(settings, crashes);
        CHECK_FALSE(report.pending());
        CHECK(report.newestFolder() == crashes / "crash-20261030-105812-345");
    }

    std::filesystem::create_directories(crashes / "crash-20261031-090000-000");
    {
        AppSettings settings(directory.path());
        const CrashReport report(settings, crashes);
        CHECK(report.pending());
        CHECK(report.newestFolder() == crashes / "crash-20261031-090000-000");
    }
}

TEST_CASE("an overlong acknowledgement is not stored")
{
    const TemporaryDirectory directory;
    AppSettings settings(directory.path());
    int hints = 0;
    QObject::connect(&settings, &AppSettings::hintsChanged, &settings, [&hints] { ++hints; });
    settings.acknowledgeCrash(QString(65, QLatin1Char('x')));
    CHECK(settings.acknowledgedCrash().isEmpty());
    settings.acknowledgeCrash(QStringLiteral("crash-20261030-105812-345"));
    settings.acknowledgeCrash(QStringLiteral("crash-20261030-105812-345"));
    CHECK(hints == 1);
}
