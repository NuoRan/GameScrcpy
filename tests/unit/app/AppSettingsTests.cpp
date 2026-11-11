#include "AppSettings.h"
#include "SettingsCatalog.h"
#include "SponsorPrompt.h"

#include <doctest/doctest.h>

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using gs::app::AppSettings;
using gs::app::SettingsCatalog;
using namespace gs::app::settings;

namespace {

class TemporaryDirectory
{
public:
    TemporaryDirectory()
        : m_path(std::filesystem::temp_directory_path() /
                 ("gs-app-settings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
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

Devices savedDevices(const TemporaryDirectory& directory)
{
    const auto path = directory.path() / "devices.json";
    if (!std::filesystem::exists(path)) {
        return {};
    }
    std::ifstream in(path, std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return load<Devices>(contents).document;
}

void count(AppSettings& settings, void (AppSettings::*signal)(), int& counter)
{
    QObject::connect(&settings, signal, &settings, [&counter] { ++counter; });
}

const QString kDevice = QStringLiteral("aid:ab3f31256ae3d44c");
const QString kMaxFps = QStringLiteral("video.maxFps");
const QString kBitrate = QStringLiteral("video.bitrateKbps");
const QString kTheme = QStringLiteral("appearance.theme");
const QString kStatusCapsule = QStringLiteral("display.statusCapsule");

}

TEST_CASE("a device override is written, reported once, and seen in the effective settings")
{
    const TemporaryDirectory directory;
    {
        int changed = 0;
        AppSettings settings(directory.path());
        count(settings, &AppSettings::devicesChanged, changed);

        CHECK(settings.setDeviceValue(kDevice, kMaxFps, 30));
        CHECK(changed == 1);
        CHECK(settings.overrides().value(kDevice).toMap().value(kMaxFps).toLongLong() == 30);
        CHECK(settings.effective(kDevice.toStdString()).video.maxFps == 30);
        CHECK(settings.effective({}).video.maxFps == Video{}.maxFps);

        CHECK(settings.setDeviceValue(kDevice, kMaxFps, 30));
        CHECK(changed == 1);

        CHECK(settings.setDeviceValue(kDevice, kBitrate, 4000));
        CHECK(changed == 2);
    }
    const Devices saved = savedDevices(directory);
    REQUIRE(saved.byId.contains(kDevice.toStdString()));
    CHECK(saved.byId.at(kDevice.toStdString()).overrides.size() == 2);
}

TEST_CASE("resetting the last override of a device with default settings removes its entry")
{
    const TemporaryDirectory directory;
    {
        AppSettings settings(directory.path());
        CHECK(settings.setDeviceValue(kDevice, kMaxFps, 30));
        CHECK(settings.setDeviceValue(kDevice, kBitrate, 4000));
    }
    REQUIRE(savedDevices(directory).byId.contains(kDevice.toStdString()));
    {
        int changed = 0;
        AppSettings settings(directory.path());
        count(settings, &AppSettings::devicesChanged, changed);

        settings.resetDeviceValue(kDevice, kMaxFps);
        CHECK(changed == 1);
        CHECK_FALSE(settings.overrides().value(kDevice).toMap().contains(kMaxFps));
        CHECK(settings.effective(kDevice.toStdString()).video.maxFps == Video{}.maxFps);

        settings.resetDeviceValue(kDevice, kMaxFps);
        CHECK(changed == 1);

        settings.resetDeviceValue(kDevice, kBitrate);
        CHECK(changed == 2);
        CHECK_FALSE(settings.overrides().contains(kDevice));
        CHECK(settings.device(kDevice.toStdString()) == DeviceSettings{});
    }
    CHECK_FALSE(savedDevices(directory).byId.contains(kDevice.toStdString()));
}

TEST_CASE("a device with its own connection keeps its entry without overrides")
{
    const TemporaryDirectory directory;
    {
        AppSettings settings(directory.path());
        DeviceSettings device;
        device.transport = Transport::WifiDirect;
        CHECK(settings.setDevice(kDevice.toStdString(), device));
        CHECK(settings.setDeviceValue(kDevice, kMaxFps, 30));
        settings.resetDeviceValue(kDevice, kMaxFps);
        CHECK_FALSE(settings.overrides().contains(kDevice));
    }
    const Devices saved = savedDevices(directory);
    REQUIRE(saved.byId.contains(kDevice.toStdString()));
    CHECK(saved.byId.at(kDevice.toStdString()).transport == Transport::WifiDirect);
    CHECK(saved.byId.at(kDevice.toStdString()).overrides.empty());
}

TEST_CASE("a global setting, a value out of range, or one of the wrong type is not a device override")
{
    const TemporaryDirectory directory;
    {
        int changed = 0;
        AppSettings settings(directory.path());
        count(settings, &AppSettings::devicesChanged, changed);

        CHECK_FALSE(settings.setDeviceValue(kDevice, kTheme, QStringLiteral("light")));
        CHECK_FALSE(settings.setDeviceValue(kDevice, kMaxFps, -1));
        CHECK_FALSE(settings.setDeviceValue(kDevice, kMaxFps, 241));
        CHECK_FALSE(settings.setDeviceValue(kDevice, kMaxFps, QStringLiteral("30")));
        CHECK_FALSE(settings.setDeviceValue(kDevice, kMaxFps, 30.5));
        CHECK_FALSE(settings.setDeviceValue(kDevice, QStringLiteral("video.unknown"), 30));
        CHECK(changed == 0);
        CHECK(settings.overrides().isEmpty());
    }
    CHECK_FALSE(savedDevices(directory).byId.contains(kDevice.toStdString()));
}

TEST_CASE("an empty or overlong device identifier stores nothing and reports it")
{
    const TemporaryDirectory directory;
    const std::string overlong(Devices::kMaxIdLength + 1, 'a');
    const std::string longest(Devices::kMaxIdLength, 'b');
    {
        int changed = 0;
        AppSettings settings(directory.path());
        count(settings, &AppSettings::devicesChanged, changed);

        CHECK_FALSE(settings.setDeviceValue(QString(), kMaxFps, 30));
        CHECK_FALSE(settings.setDeviceValue(QString::fromStdString(overlong), kMaxFps, 30));
        DeviceSettings device;
        device.transport = Transport::WifiDirect;
        CHECK_FALSE(settings.setDevice({}, device));
        CHECK_FALSE(settings.setDevice(overlong, device));
        CHECK(changed == 0);
        CHECK(settings.overrides().isEmpty());

        CHECK(settings.setDeviceValue(QString::fromStdString(longest), kMaxFps, 30));
        CHECK(changed == 1);
    }
    const Devices saved = savedDevices(directory);
    CHECK(saved.byId.size() == 1);
    CHECK(saved.byId.contains(longest));
}

TEST_CASE("a language change is reported once and only for the language (UI-012)")
{
    const TemporaryDirectory directory;
    int language = 0;
    AppSettings settings(directory.path());
    count(settings, &AppSettings::languageChanged, language);
    CHECK(settings.language() == QStringLiteral("system"));

    CHECK(settings.setValue(QStringLiteral("general.language"), QStringLiteral("en")));
    CHECK(language == 1);
    CHECK(settings.language() == QStringLiteral("en"));
    CHECK(settings.setValue(QStringLiteral("general.language"), QStringLiteral("en")));
    CHECK(settings.setValue(kTheme, QStringLiteral("light")));
    CHECK(language == 1);
    CHECK_FALSE(settings.setValue(QStringLiteral("general.language"), QStringLiteral("fr")));
    CHECK(settings.setValue(QStringLiteral("general.language"), QStringLiteral("zh_CN")));
    CHECK(language == 2);
}

TEST_CASE("a global value is reported once, and appearance changes are reported as such")
{
    const TemporaryDirectory directory;
    int values = 0;
    int appearance = 0;
    AppSettings settings(directory.path());
    count(settings, &AppSettings::valuesChanged, values);
    count(settings, &AppSettings::appearanceChanged, appearance);

    CHECK(settings.setValue(kMaxFps, 30));
    CHECK(values == 1);
    CHECK(appearance == 0);
    CHECK(settings.values().value(kMaxFps).toLongLong() == 30);

    CHECK(settings.setValue(kMaxFps, 30));
    CHECK(values == 1);

    CHECK(settings.setValue(kTheme, QStringLiteral("light")));
    CHECK(values == 2);
    CHECK(appearance == 1);
    CHECK(settings.theme() == QStringLiteral("light"));

    CHECK(settings.setValue(kTheme, QStringLiteral("light")));
    CHECK(values == 2);
    CHECK(appearance == 1);

    CHECK_FALSE(settings.setValue(kTheme, QStringLiteral("sepia")));
    CHECK_FALSE(settings.setValue(kMaxFps, -1));
    CHECK(values == 2);
    CHECK(appearance == 1);
}

TEST_CASE("the status capsule is automatic by default, changed for every device at once, and stays changed (UI-007)")
{
    const TemporaryDirectory directory;
    {
        int values = 0;
        AppSettings settings(directory.path());
        count(settings, &AppSettings::valuesChanged, values);
        CHECK(settings.values().value(kStatusCapsule).toString() == QStringLiteral("auto"));
        CHECK(settings.effective({}).display.statusCapsule == StatusCapsule::Auto);

        CHECK_FALSE(settings.setDeviceValue(kDevice, kStatusCapsule, QStringLiteral("hidden")));
        CHECK(settings.overrides().isEmpty());

        CHECK(settings.setValue(kStatusCapsule, QStringLiteral("hidden")));
        CHECK(values == 1);
        CHECK(settings.values().value(kStatusCapsule).toString() == QStringLiteral("hidden"));
        CHECK(settings.effective(kDevice.toStdString()).display.statusCapsule == StatusCapsule::Hidden);
    }
    const AppSettings reloaded(directory.path());
    CHECK(reloaded.values().value(kStatusCapsule).toString() == QStringLiteral("hidden"));
}

TEST_CASE("the mirror toolbar hint is due until marked shown, and stays shown after a restart (UI-007)")
{
    const TemporaryDirectory directory;
    {
        int values = 0;
        int hints = 0;
        AppSettings settings(directory.path());
        count(settings, &AppSettings::valuesChanged, values);
        count(settings, &AppSettings::hintsChanged, hints);
        CHECK_FALSE(settings.mirrorToolbarHintShown());

        settings.markMirrorToolbarHintShown();
        CHECK(settings.mirrorToolbarHintShown());
        CHECK(hints == 1);
        settings.markMirrorToolbarHintShown();
        CHECK(settings.mirrorToolbarHintShown());
        CHECK(hints == 1);
        CHECK(values == 0);
        for (const QString& path : settings.values().keys()) {
            CHECK_FALSE(path.startsWith(QStringLiteral("hints")));
        }
    }
    const AppSettings reloaded(directory.path());
    CHECK(reloaded.mirrorToolbarHintShown());
}

TEST_CASE("ADR-0109: the sponsor question is due at the 10th, 50th, 100th and 200th launch, each once")
{
    using gs::app::sponsorMilestoneDue;
    CHECK(sponsorMilestoneDue(0, 0) == 0);
    CHECK(sponsorMilestoneDue(9, 0) == 0);
    CHECK(sponsorMilestoneDue(10, 0) == 10);
    CHECK(sponsorMilestoneDue(10, 10) == 0);
    CHECK(sponsorMilestoneDue(49, 10) == 0);
    CHECK(sponsorMilestoneDue(50, 10) == 50);
    CHECK(sponsorMilestoneDue(57, 10) == 50);
    CHECK(sponsorMilestoneDue(120, 0) == 100);
    CHECK(sponsorMilestoneDue(200, 100) == 200);
    CHECK(sponsorMilestoneDue(5000, 200) == 0);
}

TEST_CASE("ADR-0109: launches are counted in settings.json, and the sponsor question comes once at each of its launches")
{
    const TemporaryDirectory directory;
    {
        int hints = 0;
        int values = 0;
        AppSettings settings(directory.path());
        count(settings, &AppSettings::hintsChanged, hints);
        count(settings, &AppSettings::valuesChanged, values);
        for (int launch = 1; launch <= 9; ++launch) {
            settings.countLaunch();
        }
        CHECK(settings.launches() == 9);
        CHECK(settings.sponsorMilestone() == 0);
        CHECK(hints == 9);
        CHECK(values == 0);
        for (const QString& path : settings.values().keys()) {
            CHECK_FALSE(path.startsWith(QStringLiteral("hints")));
        }
    }
    {
        AppSettings settings(directory.path());
        CHECK(settings.launches() == 9);
        settings.countLaunch();
        CHECK(settings.sponsorMilestone() == 10);
        settings.markSponsorAsked(10);
        CHECK(settings.sponsorMilestone() == 0);
    }
    AppSettings settings(directory.path());
    CHECK(settings.launches() == 10);
    CHECK(settings.sponsorMilestone() == 0);
    std::vector<int> asked{10};
    for (int launch = 11; launch <= 260; ++launch) {
        settings.countLaunch();
        if (const int milestone = settings.sponsorMilestone(); milestone != 0) {
            CHECK(milestone == launch);
            asked.push_back(milestone);
            settings.markSponsorAsked(milestone);
        }
    }
    CHECK(asked == std::vector<int>{10, 50, 100, 200});
    settings.markSponsorAsked(50);
    CHECK(settings.sponsorMilestone() == 0);
    CHECK(settings.launches() == 260);
}

TEST_CASE("ADR-0107: the other video devices row starts folded, and its folding is remembered and is no setting")
{
    const TemporaryDirectory directory;
    {
        int changed = 0;
        int values = 0;
        AppSettings settings(directory.path());
        count(settings, &AppSettings::otherVideoChanged, changed);
        count(settings, &AppSettings::valuesChanged, values);
        CHECK_FALSE(settings.otherVideoExpanded());

        settings.setOtherVideoExpanded(true);
        CHECK(settings.otherVideoExpanded());
        CHECK(changed == 1);
        settings.setOtherVideoExpanded(true);
        CHECK(changed == 1);
        CHECK(values == 0);
        for (const QString& path : settings.values().keys()) {
            CHECK_FALSE(path.startsWith(QStringLiteral("otherVideo")));
        }
    }
    {
        AppSettings reloaded(directory.path());
        CHECK(reloaded.otherVideoExpanded());
        reloaded.setOtherVideoExpanded(false);
    }
    const AppSettings folded(directory.path());
    CHECK_FALSE(folded.otherVideoExpanded());
}

TEST_CASE("reduce motion and reduce transparency are on, off, or as Windows has them")
{
    const TemporaryDirectory directory;
    AppSettings settings(directory.path());
    CHECK(settings.reducedMotion() == settings.systemReducedMotion());
    CHECK(settings.reducedTransparency() == settings.systemReducedTransparency());
    CHECK(settings.setValue(QStringLiteral("appearance.reducedMotion"), QStringLiteral("on")));
    CHECK(settings.reducedMotion());
    CHECK(settings.setValue(QStringLiteral("appearance.reducedMotion"), QStringLiteral("off")));
    CHECK_FALSE(settings.reducedMotion());
    CHECK(settings.setValue(QStringLiteral("appearance.reducedTransparency"), QStringLiteral("on")));
    CHECK(settings.reducedTransparency());
    CHECK_FALSE(settings.setValue(QStringLiteral("appearance.reducedMotion"), QStringLiteral("sometimes")));
}

TEST_CASE("the settings catalog lists a device's own settings by page and follows their changes")
{
    const TemporaryDirectory directory;
    AppSettings settings(directory.path());
    SettingsCatalog catalog;
    int changes = 0;
    QObject::connect(&catalog, &SettingsCatalog::overridesChanged, &catalog, [&changes] { ++changes; });
    const QString sharpness = QStringLiteral("display.sharpness");
    REQUIRE(settings.setDeviceValue(kDevice, kMaxFps, 90));
    CHECK(catalog.pageOverrides().isEmpty());

    catalog.openForDevice(kDevice, QStringLiteral("Pixel 8"));
    CHECK(catalog.pageOverrides().value(QStringLiteral("video")).toStringList() == QStringList{kMaxFps});
    CHECK(changes == 1);

    REQUIRE(settings.setDeviceValue(kDevice, sharpness, 20));
    CHECK(catalog.pageOverrides().value(QStringLiteral("video")).toStringList() == QStringList{sharpness, kMaxFps});
    CHECK(changes == 2);

    catalog.setScoped(false);
    CHECK(catalog.pageOverrides().isEmpty());
    catalog.setScoped(true);
    settings.resetDeviceValue(kDevice, kMaxFps);
    settings.resetDeviceValue(kDevice, sharpness);
    CHECK(catalog.pageOverrides().isEmpty());
}

TEST_CASE("a search finds the status capsule by its words, and not while scoped to a device (UI-007)")
{
    SettingsCatalog catalog;
    catalog.setQuery(QStringLiteral("statistics"));
    CHECK(catalog.hits().contains(kStatusCapsule));

    catalog.openForDevice(kDevice, QStringLiteral("Pixel 8"));
    CHECK_FALSE(catalog.hits().contains(kStatusCapsule));
}

TEST_CASE("the settings catalog narrows a search to per-device settings while scoped to a device")
{
    SettingsCatalog catalog;
    int opened = 0;
    QObject::connect(&catalog, &SettingsCatalog::openRequested, &catalog, [&opened] { ++opened; });

    catalog.setQuery(QStringLiteral("e"));
    const QVariantMap everything = catalog.hits();
    REQUIRE(everything.contains(kTheme));
    REQUIRE(everything.contains(kMaxFps));

    catalog.openForDevice(kDevice, QStringLiteral("Pixel 8"));
    CHECK(opened == 1);
    CHECK(catalog.scoped());
    CHECK(catalog.device() == kDevice);
    CHECK(catalog.scopeDevice() == kDevice);
    CHECK(catalog.scopeDeviceName() == QStringLiteral("Pixel 8"));

    const QVariantMap perDevice = catalog.hits();
    CHECK(perDevice.contains(kMaxFps));
    CHECK_FALSE(perDevice.contains(kTheme));
    CHECK(perDevice.size() < everything.size());
    for (const QString& path : perDevice.keys()) {
        const Entry* entry = findSetting(path.toStdString());
        REQUIRE(entry != nullptr);
        CHECK(entry->facets.scope == Scope::Device);
    }

    catalog.setScoped(false);
    CHECK_FALSE(catalog.scoped());
    CHECK(catalog.device().isEmpty());
    CHECK(catalog.scopeDevice() == kDevice);
    CHECK(catalog.hits() == everything);

    catalog.setScoped(true);
    CHECK(catalog.device() == kDevice);
    CHECK(catalog.hits() == perDevice);
    CHECK(opened == 1);
}

TEST_CASE("the settings catalog stays global without a device to scope to")
{
    SettingsCatalog catalog;
    catalog.setScoped(true);
    CHECK_FALSE(catalog.scoped());
    CHECK(catalog.device().isEmpty());
}

TEST_CASE("ADR-0112: the tour of an extension is remembered under its long key, next to the tours of GameScrcpy")
{
    const TemporaryDirectory directory;
    const QString key = QStringLiteral("ext/%1/intro").arg(QString(64, QLatin1Char('a')));
    {
        AppSettings settings(directory.path());
        for (int index = 0; index < 20; ++index) {
            settings.markTourSeen(QStringLiteral("tour-%1").arg(index));
        }
        settings.markTourSeen(key);
        CHECK(settings.tourSeen(key));
    }
    AppSettings settings(directory.path());
    CHECK(settings.tourSeen(key));
    CHECK(settings.toursSeen().size() == 21);
}

TEST_CASE("ADR-0110: each guided tour is remembered once it is finished or skipped, and the help can reset one")
{
    const TemporaryDirectory directory;
    {
        int tours = 0;
        int hints = 0;
        AppSettings settings(directory.path());
        count(settings, &AppSettings::toursChanged, tours);
        count(settings, &AppSettings::hintsChanged, hints);
        CHECK(settings.toursSeen().isEmpty());
        CHECK_FALSE(settings.tourSeen(QStringLiteral("devices")));

        settings.markTourSeen(QStringLiteral("devices"));
        CHECK(settings.tourSeen(QStringLiteral("devices")));
        CHECK_FALSE(settings.tourSeen(QStringLiteral("keymap")));
        CHECK(settings.toursSeen() == QStringList{QStringLiteral("devices")});
        CHECK(tours == 1);
        settings.markTourSeen(QStringLiteral("devices"));
        CHECK(tours == 1);

        settings.markTourSeen(QStringLiteral("mirror"));
        CHECK(settings.toursSeen() == QStringList{QStringLiteral("devices"), QStringLiteral("mirror")});
        CHECK(tours == 2);
        CHECK(hints == 0);

        settings.resetTour(QStringLiteral("devices"));
        CHECK_FALSE(settings.tourSeen(QStringLiteral("devices")));
        CHECK(settings.tourSeen(QStringLiteral("mirror")));
        CHECK(tours == 3);
        settings.resetTour(QStringLiteral("devices"));
        CHECK(tours == 3);

        CHECK_FALSE(settings.tourSeen(QString()));
        CHECK_FALSE(settings.tourSeen(QStringLiteral(" devices")));
        CHECK_FALSE(settings.tourSeen(QStringLiteral("devices ")));
        settings.markTourSeen(QString());
        settings.markTourSeen(QString(static_cast<int>(Hints::kMaxTourNameLength) + 1, QLatin1Char('x')));
        CHECK(settings.toursSeen() == QStringList{QStringLiteral("mirror")});
        CHECK(tours == 3);
        CHECK(hints == 0);
    }
    {
        AppSettings settings(directory.path());
        CHECK(settings.toursSeen() == QStringList{QStringLiteral("mirror")});
        CHECK(settings.tourSeen(QStringLiteral("mirror")));
        settings.resetTours();
        CHECK(settings.toursSeen().isEmpty());
        settings.resetTours();
        CHECK(settings.toursSeen().isEmpty());
    }
    {
        AppSettings settings(directory.path());
        CHECK(settings.toursSeen().isEmpty());
    }
}

TEST_CASE("ADR-0110: tours keep quiet while the setup guide or a dialog is open, and that is not saved")
{
    const TemporaryDirectory directory;
    {
        int quietChanges = 0;
        AppSettings settings(directory.path());
        QObject::connect(&settings, &AppSettings::toursQuietChanged, &settings, [&quietChanges] { ++quietChanges; });
        CHECK_FALSE(settings.toursQuiet());
        settings.setToursQuiet(true);
        CHECK(settings.toursQuiet());
        CHECK(quietChanges == 1);
        settings.setToursQuiet(true);
        CHECK(quietChanges == 1);
    }
    {
        AppSettings settings(directory.path());
        CHECK_FALSE(settings.toursQuiet());
    }
}
