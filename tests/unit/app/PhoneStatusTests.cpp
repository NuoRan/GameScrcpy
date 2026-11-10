#include "AppSettings.h"
#include "PhoneStatus.h"
#include "Settings.h"

#include <doctest/doctest.h>

#include <QString>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

using gs::app::AppSettings;
using gs::app::PhoneStatus;
using gs::protocol::DeviceStatus;

namespace {

class TemporaryDirectory
{
public:
    TemporaryDirectory()
        : m_path(std::filesystem::temp_directory_path() /
                 ("gs-phone-status-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
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

DeviceStatus reported(std::uint8_t battery, bool charging, std::int16_t temperature, std::uint16_t cpu)
{
    DeviceStatus status;
    status.batteryPercent = battery;
    status.charging = charging;
    status.temperatureDeciC = temperature;
    status.cpuPermille = cpu;
    return status;
}

QString textOf(std::uint8_t battery, bool charging, std::int16_t temperature, std::uint16_t cpu)
{
    return PhoneStatus::of(reported(battery, charging, temperature, cpu)).text();
}

constexpr std::uint8_t kNoBattery = DeviceStatus::kUnknownBattery;
constexpr std::int16_t kNoTemperature = DeviceStatus::kUnknownTemperature;
constexpr std::uint16_t kNoCpu = DeviceStatus::kUnknownCpu;

}

TEST_CASE("phone status: every known item shows in one line, in a fixed order")
{
    const PhoneStatus status = PhoneStatus::of(reported(82, true, 365, 230));
    CHECK(status.batteryPercent == 82);
    CHECK(status.charging);
    CHECK(status.temperatureDeciC == 365);
    CHECK(status.cpuPermille == 230);
    CHECK(status.text() == QStringLiteral("82% · Charging · 36.5 °C · CPU 23%"));
    CHECK(textOf(100, false, 412, 1000) == QStringLiteral("100% · 41.2 °C · CPU 100%"));
}

TEST_CASE("phone status: items the phone does not know are left out")
{
    const PhoneStatus unknown = PhoneStatus::of(DeviceStatus{});
    CHECK_FALSE(unknown.batteryPercent.has_value());
    CHECK_FALSE(unknown.charging);
    CHECK_FALSE(unknown.temperatureDeciC.has_value());
    CHECK_FALSE(unknown.cpuPermille.has_value());
    CHECK(unknown.text().isEmpty());

    CHECK(textOf(64, false, kNoTemperature, 47) == QStringLiteral("64% · CPU 5%"));
    CHECK(textOf(kNoBattery, false, 298, kNoCpu) == QStringLiteral("29.8 °C"));
    CHECK(textOf(kNoBattery, false, kNoTemperature, 0) == QStringLiteral("CPU 0%"));
    CHECK(textOf(15, false, kNoTemperature, kNoCpu) == QStringLiteral("15%"));
}

TEST_CASE("phone status: charging is named only while the phone charges")
{
    CHECK(textOf(50, true, kNoTemperature, kNoCpu) == QStringLiteral("50% · Charging"));
    CHECK(textOf(50, false, kNoTemperature, kNoCpu) == QStringLiteral("50%"));
    CHECK(textOf(kNoBattery, true, kNoTemperature, kNoCpu) == QStringLiteral("Charging"));
}

TEST_CASE("phone status: the temperature keeps one decimal and the CPU use rounds to whole percent")
{
    CHECK(textOf(kNoBattery, false, -125, kNoCpu) == QStringLiteral("-12.5 °C"));
    CHECK(textOf(kNoBattery, false, 0, kNoCpu) == QStringLiteral("0.0 °C"));
    CHECK(textOf(kNoBattery, false, 370, kNoCpu) == QStringLiteral("37.0 °C"));
    CHECK(textOf(kNoBattery, false, kNoTemperature, 235) == QStringLiteral("CPU 24%"));
    CHECK(textOf(kNoBattery, false, kNoTemperature, 234) == QStringLiteral("CPU 23%"));
    CHECK(textOf(kNoBattery, false, kNoTemperature, 4) == QStringLiteral("CPU 0%"));
}

TEST_CASE("phone status: the setting is off by default, kept for each phone and applied at once")
{
    using namespace gs::app::settings;
    const Entry* entry = findSetting("display.phoneStatus");
    REQUIRE(entry != nullptr);
    CHECK(entry->kind == Kind::Boolean);
    CHECK(entry->page == Page::Video);
    CHECK(entry->facets.scope == Scope::Device);
    CHECK(entry->facets.live);
    CHECK(entry->defaultValue == Value(false));
    CHECK_FALSE(Settings{}.display.phoneStatus);
    CHECK(save(Settings{}).find("phoneStatus") == std::string::npos);

    DeviceSettings device;
    CHECK(setOverride(device, "display.phoneStatus", Value(true)));
    CHECK(effectiveSettings(Settings{}, device).display.phoneStatus);
    CHECK_FALSE(effectiveSettings(Settings{}, DeviceSettings{}).display.phoneStatus);
}

TEST_CASE("phone status: turning it on for one phone is remembered for that phone only")
{
    const TemporaryDirectory directory;
    const QString phone = QStringLiteral("aid:0123456789abcdef");
    const QString other = QStringLiteral("aid:fedcba9876543210");
    const QString path = QStringLiteral("display.phoneStatus");
    {
        AppSettings settings(directory.path());
        CHECK_FALSE(settings.effective(phone.toStdString()).display.phoneStatus);
        CHECK(settings.setDeviceValue(phone, path, true));
        CHECK(settings.effective(phone.toStdString()).display.phoneStatus);
        CHECK_FALSE(settings.effective(other.toStdString()).display.phoneStatus);
        CHECK_FALSE(settings.effective({}).display.phoneStatus);
    }
    {
        AppSettings settings(directory.path());
        CHECK(settings.effective(phone.toStdString()).display.phoneStatus);
        CHECK_FALSE(settings.effective(other.toStdString()).display.phoneStatus);
        CHECK(settings.setDeviceValue(phone, path, false));
        CHECK_FALSE(settings.effective(phone.toStdString()).display.phoneStatus);
    }
}
