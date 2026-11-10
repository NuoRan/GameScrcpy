#include "AppSettings.h"
#include "FailureAdvice.h"
#include "Settings.h"

#include <doctest/doctest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

using gs::app::AppSettings;
using namespace gs::app::settings;

namespace {

class TemporaryDirectory
{
public:
    TemporaryDirectory()
        : m_path(std::filesystem::temp_directory_path() /
                 ("gs-acceptance-settings-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                  std::to_string(s_next++)))
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

class CoreApplication
{
public:
    CoreApplication()
    {
        if (QCoreApplication::instance() == nullptr) {
            m_application = std::make_unique<QCoreApplication>(m_argc, m_argv.data());
        }
    }

private:
    int m_argc = 1;
    std::string m_name = "gs_app_tests";
    std::array<char*, 2> m_argv{m_name.data(), nullptr};
    std::unique_ptr<QCoreApplication> m_application;
};

class SavedVariable
{
public:
    explicit SavedVariable(const char* name)
        : m_name(name), m_had(qEnvironmentVariableIsSet(name)), m_value(qgetenv(name))
    {
    }
    ~SavedVariable()
    {
        if (m_had) {
            qputenv(m_name, m_value);
        } else {
            qunsetenv(m_name);
        }
    }
    SavedVariable(const SavedVariable&) = delete;
    SavedVariable& operator=(const SavedVariable&) = delete;

private:
    const char* m_name;
    bool m_had;
    QByteArray m_value;
};

class FlagFile
{
public:
    explicit FlagFile(std::filesystem::path path) : m_path(std::move(path))
    {
        std::ofstream out(m_path, std::ios::binary | std::ios::trunc);
        out << "portable\n";
    }
    ~FlagFile()
    {
        std::error_code ignored;
        std::filesystem::remove(m_path, ignored);
    }
    FlagFile(const FlagFile&) = delete;
    FlagFile& operator=(const FlagFile&) = delete;

private:
    std::filesystem::path m_path;
};

const QString kPhone = QStringLiteral("R58M123ABC");
const QString kOtherPhone = QStringLiteral("aid:ab3f31256ae3d44c");

}

TEST_CASE("MED-004: low-latency mode is off by default, turned on for one phone and kept after a restart")
{
    const TemporaryDirectory directory;
    {
        AppSettings settings(directory.path());
        CHECK_FALSE(settings.effective(kPhone.toStdString()).display.lowLatency);
        CHECK(settings.setDeviceValue(kPhone, QStringLiteral("display.lowLatency"), true));
        CHECK(settings.effective(kPhone.toStdString()).display.lowLatency);
        CHECK_FALSE(settings.effective(kOtherPhone.toStdString()).display.lowLatency);
        CHECK_FALSE(settings.setDeviceValue(kPhone, QStringLiteral("display.lowLatency"), QStringLiteral("fast")));
    }
    AppSettings reopened(directory.path());
    CHECK(reopened.effective(kPhone.toStdString()).display.lowLatency);
    CHECK_FALSE(reopened.effective(kOtherPhone.toStdString()).display.lowLatency);
    CHECK_FALSE(reopened.effective({}).display.lowLatency);
}

TEST_CASE("MED-005: sharpening runs from 0 to 100, is set for every phone or for one, and is kept after a restart")
{
    const TemporaryDirectory directory;
    {
        AppSettings settings(directory.path());
        CHECK(settings.effective({}).display.sharpness == 0);
        CHECK(settings.setValue(QStringLiteral("display.sharpness"), 40));
        CHECK(settings.setDeviceValue(kPhone, QStringLiteral("display.sharpness"), 75));
        CHECK_FALSE(settings.setValue(QStringLiteral("display.sharpness"), 101));
        CHECK_FALSE(settings.setDeviceValue(kPhone, QStringLiteral("display.sharpness"), -1));
        CHECK(settings.effective({}).display.sharpness == 40);
        CHECK(settings.effective(kPhone.toStdString()).display.sharpness == 75);
        CHECK(settings.effective(kOtherPhone.toStdString()).display.sharpness == 40);
    }
    AppSettings reopened(directory.path());
    CHECK(reopened.values().value(QStringLiteral("display.sharpness")).toLongLong() == 40);
    CHECK(reopened.effective(kPhone.toStdString()).display.sharpness == 75);
    CHECK(reopened.effective(kOtherPhone.toStdString()).display.sharpness == 40);
}

TEST_CASE("INP-006: key hints can be turned off and their resting opacity set from 10 to 100 percent, kept after a restart")
{
    const TemporaryDirectory directory;
    {
        AppSettings settings(directory.path());
        CHECK(settings.effective({}).keymap.hints);
        CHECK(settings.effective({}).keymap.hintOpacity == 60);
        CHECK(settings.setValue(QStringLiteral("keymap.hintOpacity"), 35));
        CHECK_FALSE(settings.setValue(QStringLiteral("keymap.hintOpacity"), 5));
        CHECK_FALSE(settings.setValue(QStringLiteral("keymap.hintOpacity"), 101));
        CHECK(settings.setValue(QStringLiteral("keymap.hints"), false));
        CHECK_FALSE(settings.setDeviceValue(kPhone, QStringLiteral("keymap.hintOpacity"), 50));
        CHECK(settings.effective({}).keymap.hintOpacity == 35);
    }
    AppSettings reopened(directory.path());
    CHECK(reopened.values().value(QStringLiteral("keymap.hintOpacity")).toLongLong() == 35);
    CHECK_FALSE(reopened.values().value(QStringLiteral("keymap.hints")).toBool());
    CHECK(reopened.effective(kPhone.toStdString()).keymap.hintOpacity == 35);
}

TEST_CASE("INP-021: each phone keeps the profile it last used and its own choice per game, apart from the others")
{
    constexpr const char* kGame = "com.tencent.tmgp.pubgmhd";
    const TemporaryDirectory directory;
    {
        AppSettings settings(directory.path());
        DeviceSettings first = settings.device(kPhone.toStdString());
        first.profile = "shooter";
        first.apps[kGame] = "pubg-phone";
        CHECK(settings.setDevice(kPhone.toStdString(), first));
        DeviceSettings second = settings.device(kOtherPhone.toStdString());
        second.profile = "racer";
        second.apps[kGame] = "pubg-tablet";
        CHECK(settings.setDevice(kOtherPhone.toStdString(), second));
    }
    AppSettings reopened(directory.path());
    const DeviceSettings first = reopened.device(kPhone.toStdString());
    const DeviceSettings second = reopened.device(kOtherPhone.toStdString());
    CHECK(first.profile == "shooter");
    CHECK(second.profile == "racer");
    REQUIRE(first.apps.contains(kGame));
    REQUIRE(second.apps.contains(kGame));
    CHECK(first.apps.at(kGame) == "pubg-phone");
    CHECK(second.apps.at(kGame) == "pubg-tablet");
    CHECK(reopened.device("unknown").profile.empty());
}

TEST_CASE("REL-001: data goes to GS_DATA_DIR when set, else beside the program when portable.flag is there, else to "
          "%APPDATA%")
{
    const CoreApplication application;
    const QString programDirectory = QCoreApplication::applicationDirPath();
    REQUIRE_FALSE(programDirectory.isEmpty());
    const std::filesystem::path flag = std::filesystem::path(programDirectory.toStdWString()) / "portable.flag";
    REQUIRE_FALSE(std::filesystem::exists(flag));
    const SavedVariable dataFolder("GS_DATA_DIR");
    qunsetenv("GS_DATA_DIR");
    const std::filesystem::path roaming((qEnvironmentVariable("APPDATA") + QStringLiteral("/GameScrcpy")).toStdWString());
    const std::filesystem::path portable((programDirectory + QStringLiteral("/data")).toStdWString());

    CHECK(gs::app::dataDirectory() == roaming);
    {
        const FlagFile marker(flag);
        REQUIRE(std::filesystem::exists(flag));
        CHECK(gs::app::dataDirectory() == portable);
        qputenv("GS_DATA_DIR", QByteArray("C:/gs-portable-test-data"));
        CHECK(gs::app::dataDirectory() == std::filesystem::path(L"C:/gs-portable-test-data"));
        qunsetenv("GS_DATA_DIR");
    }
    CHECK_FALSE(std::filesystem::exists(flag));
    CHECK(gs::app::dataDirectory() == roaming);
}

TEST_CASE("SRV-010: a phone older than Android 9 is told plainly that GameScrcpy needs Android 9 or newer")
{
    gs::session::SessionFailure failure;
    failure.kind = gs::session::FailureKind::UnsupportedAndroid;
    failure.phase = gs::session::SessionPhase::Deploying;
    failure.detail = "API 26";
    const gs::app::Advice wired = gs::app::FailureAdvice::forFailure(failure);
    CHECK(wired.problem.contains(QStringLiteral("too old")));
    CHECK(wired.action.contains(QStringLiteral("Android 9")));
    const gs::app::Advice wireless = gs::app::FailureAdvice::forFailure(failure, true);
    CHECK(wireless.problem == wired.problem);
    CHECK(wireless.action.contains(QStringLiteral("Android 9")));
}
