#include "CaptureCards.h"
#include "Settings.h"

#include <gs/capture/CaptureDevices.h>
#include <gs/capture/CropDetector.h>
#include <gs/platform/Usb.h>

#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

using namespace gs;
using namespace gs::app;

namespace {

const capture::CaptureDeviceInfo kCard{.link = L"\\\\?\\usb#vid_534d&pid_2109&mi_00#7&2b8e4f1&0&0000#{e5323777-f976-4f5b-9b55-b94699c46e44}\\global",
                                       .name = "USB3 Video",
                                       .container = L"{A1B2C3D4-E5F6-4711-8899-AABBCCDDEEFF}"};
const capture::CaptureDeviceInfo kSecond{.link = L"\\\\?\\usb#vid_0fd9&pid_0066#5&1d0a7c3&0&4#{e5323777-f976-4f5b-9b55-b94699c46e44}\\global",
                                         .name = "Game Capture HD60 S+",
                                         .container = L"{0F0F0F0F-0000-1111-2222-333344445555}"};
const capture::CaptureDeviceInfo kBezel{.link = L"\\\\?\\usb#vid_0bda&pid_5634#6&3a1b2c&0&5#{e5323777-f976-4f5b-9b55-b94699c46e44}\\global",
                                        .name = "Integrated Camera",
                                        .container = L"{00000000-0000-0000-FFFF-FFFFFFFFFFFF}",
                                        .panelLocated = true,
                                        .removable = false};
const capture::CaptureDeviceInfo kObs{.link = L"\\\\?\\root#image#0000#{e5323777-f976-4f5b-9b55-b94699c46e44}\\global",
                                      .name = "OBS Virtual Camera"};
const capture::CaptureDeviceInfo kStreaming{.link = L"\\\\?\\root#image#0001#{e5323777-f976-4f5b-9b55-b94699c46e44}\\global",
                                            .name = "Streaming Center Virtual Camera"};
const capture::CaptureDeviceInfo kWebcam{.link = L"\\\\?\\usb#vid_046d&pid_085e#8&1f2e3d&0&3#{e5323777-f976-4f5b-9b55-b94699c46e44}\\global",
                                         .name = "Logitech BRIO Webcam"};
const capture::CaptureDeviceInfo kNameless{.link = L"\\\\?\\usb#vid_1b3f&pid_2002#9&2c3d4e&0&2#{e5323777-f976-4f5b-9b55-b94699c46e44}\\global",
                                           .name = ""};

platform::UsbPhone usbPhone(const char* serial, platform::UsbDriver driver)
{
    return {.serial = serial, .name = "Phone", .vendorId = 0x18D1, .productId = 0x4EE7, .driver = driver};
}

bool reports(const settings::Loaded<settings::Devices>& result, settings::Problem problem, const std::string& path)
{
    return std::ranges::any_of(result.diagnostics,
                               [&](const settings::Diagnostic& d) { return d.problem == problem && d.path == path; });
}

}

TEST_CASE("capture cards get a serial from their device link and their own name; built-in cameras are left out (MED-014)")
{
    capture::CaptureDeviceInfo again = kCard;
    std::ranges::transform(again.link, again.link.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
    const std::vector<CaptureCard> cards = captureCardsOf({kCard, kBezel, again, {.link = L"", .name = "No link"}, kSecond});
    REQUIRE(cards.size() == 2);
    CHECK(cards[0].serial == captureSerialOf(kCard.link));
    CHECK(cards[0].serial.size() == 16);
    CHECK(cards[0].serial.startsWith(QStringLiteral("capture-")));
    CHECK(cards[0].name == QStringLiteral("USB3 Video"));
    CHECK(cards[0].link == kCard.link);
    CHECK(cards[0].container == kCard.container);
    CHECK(cards[1].name == QStringLiteral("Game Capture HD60 S+"));
    CHECK(cards[0].serial != cards[1].serial);

    CHECK(isCaptureSerial(cards[0].serial));
    CHECK(isCaptureSerial(demoCaptureSerial()));
    CHECK(demoCaptureSerial() == QStringLiteral("capture-demo"));
    CHECK_FALSE(isCaptureSerial(u"emulator-5554"));
    CHECK_FALSE(isCaptureSerial(u"aoa-R58N123ABC"));
}

TEST_CASE("ADR-0107: a capture card is called by its own name, with no category in front")
{
    CHECK(captureCardName(QStringLiteral("USB3 Video")) == QStringLiteral("USB3 Video"));
    CHECK(captureCardName(QStringLiteral("  Cam Link 4K ")) == QStringLiteral("Cam Link 4K"));
    CHECK(captureCardName({}) == QStringLiteral("Unnamed capture device"));
    CHECK(captureCardName(QStringLiteral("   ")) == QStringLiteral("Unnamed capture device"));
}

TEST_CASE("ADR-0107: names of capture cards are told from virtual cameras and webcams, and the capture words win")
{
    for (const char* other : {"Streaming Center Virtual Camera", "WebcastMate VirtualCamera", "Integrated Camera",
                              "OBS Virtual Camera", "Logitech BRIO Webcam", "OBS-Camera", "e2eSoft VCam"}) {
        CAPTURE(other);
        CHECK_FALSE(looksLikeCaptureCard(QString::fromUtf8(other)));
    }
    for (const char* card : {"USB3 Video", "Cam Link 4K", "UGREEN HDMI Capture", "Elgato HD60 X",
                             "AVerMedia Live Gamer", "USB Video", "Magewell USB Capture HDMI", "ACASIS 4K",
                             "UVC Camera", "HDMI Video Grabber", "", "AV TO USB2.0"}) {
        CAPTURE(card);
        CHECK(looksLikeCaptureCard(QString::fromUtf8(card)));
    }
    CHECK(looksLikeCaptureCard(u"usb3.0 hdmi 采集卡"));
    CHECK_FALSE(looksLikeCaptureCard(u"虚拟摄像头"));
    CHECK_FALSE(looksLikeCaptureCard(u"USB2.0 摄像头"));

    CHECK(listedAsCaptureCard(settings::CaptureKind::Automatic, u"Cam Link 4K"));
    CHECK_FALSE(listedAsCaptureCard(settings::CaptureKind::Automatic, u"OBS Virtual Camera"));
    CHECK(listedAsCaptureCard(settings::CaptureKind::CaptureCard, u"OBS Virtual Camera"));
    CHECK_FALSE(listedAsCaptureCard(settings::CaptureKind::NotCaptureCard, u"Cam Link 4K"));
}

TEST_CASE("capture rows appear and disappear with the devices, and the hidden ones stay out (MED-014)")
{
    const QString first = captureSerialOf(kCard.link);
    const QString second = captureSerialOf(kSecond.link);

    const CapturePlan fresh = planCaptureCards({kCard, kSecond, kBezel}, {}, {});
    REQUIRE(fresh.added.size() == 2);
    CHECK(fresh.added[0].serial == first);
    CHECK(fresh.added[1].serial == second);
    CHECK(fresh.removed.empty());
    CHECK(fresh.others.empty());

    const CapturePlan same = planCaptureCards({kCard, kSecond}, {first, second}, {});
    CHECK(same.added.empty());
    CHECK(same.removed.empty());

    const CapturePlan unplugged = planCaptureCards({kCard}, {first, second}, {});
    CHECK(unplugged.added.empty());
    CHECK(unplugged.removed == std::vector<QString>{second});

    const auto hideSecond = [&second](const QString& serial) {
        return serial == second ? settings::CaptureKind::NotCaptureCard : settings::CaptureKind::Automatic;
    };
    const CapturePlan hidden = planCaptureCards({kCard, kSecond}, {first}, hideSecond);
    CHECK(hidden.added.empty());
    CHECK(hidden.removed.empty());
    REQUIRE(hidden.others.size() == 1);
    CHECK(hidden.others[0].name == QStringLiteral("Game Capture HD60 S+"));
    CHECK(hidden.others[0].hidden);

    const CapturePlan hiddenWhileShown = planCaptureCards({kCard, kSecond}, {first, second}, hideSecond);
    CHECK(hiddenWhileShown.removed == std::vector<QString>{second});
    CHECK(hiddenWhileShown.added.empty());

    CHECK(planCaptureCards({}, {first}, {}).removed == std::vector<QString>{first});
}

TEST_CASE("ADR-0107: capture cards without a capture-like name go to other video devices, with the ones hidden by hand")
{
    const QString card = captureSerialOf(kCard.link);
    const QString obs = captureSerialOf(kObs.link);
    const QString streaming = captureSerialOf(kStreaming.link);
    const QString second = captureSerialOf(kSecond.link);
    const QString webcam = captureSerialOf(kWebcam.link);
    const QString nameless = captureSerialOf(kNameless.link);
    const auto chosen = [&](const QString& serial) {
        if (serial == second || serial == nameless) {
            return settings::CaptureKind::NotCaptureCard;
        }
        return serial == webcam ? settings::CaptureKind::CaptureCard : settings::CaptureKind::Automatic;
    };

    const CapturePlan plan =
        planCaptureCards({kCard, kObs, kStreaming, kSecond, kWebcam, kNameless, kBezel}, {}, chosen);
    REQUIRE(plan.added.size() == 2);
    CHECK(plan.added[0].serial == card);
    CHECK(plan.added[1].serial == webcam);
    CHECK(plan.added[1].name == QStringLiteral("Logitech BRIO Webcam"));
    CHECK(plan.removed.empty());
    CHECK(plan.others ==
          std::vector<OtherVideoDevice>{
              {.serial = obs, .name = QStringLiteral("OBS Virtual Camera"), .hidden = false},
              {.serial = streaming, .name = QStringLiteral("Streaming Center Virtual Camera"), .hidden = false},
              {.serial = second, .name = QStringLiteral("Game Capture HD60 S+"), .hidden = true},
              {.serial = nameless, .name = QStringLiteral("Unnamed capture device"), .hidden = true}});

    const CapturePlan automatic = planCaptureCards({kNameless, kObs}, {}, {});
    REQUIRE(automatic.added.size() == 1);
    CHECK(automatic.added[0].serial == nameless);
    REQUIRE(automatic.others.size() == 1);
    CHECK(automatic.others[0].serial == obs);
    CHECK_FALSE(automatic.others[0].hidden);

    const CapturePlan backToAutomatic = planCaptureCards({kCard, kObs}, {card, obs}, {});
    CHECK(backToAutomatic.added.empty());
    CHECK(backToAutomatic.removed == std::vector<QString>{obs});
    REQUIRE(backToAutomatic.others.size() == 1);
    CHECK(backToAutomatic.others[0].serial == obs);

    const CapturePlan treated = planCaptureCards({kObs, kStreaming}, {}, [&obs](const QString& serial) {
        return serial == obs ? settings::CaptureKind::CaptureCard : settings::CaptureKind::Automatic;
    });
    REQUIRE(treated.added.size() == 1);
    CHECK(treated.added[0].serial == obs);
    REQUIRE(treated.others.size() == 1);
    CHECK(treated.others[0].serial == streaming);
}

TEST_CASE("the capture card watcher lists devices off the UI thread and tells only about changes (MED-014)")
{
    int argc = 1;
    char name[] = "test";
    char* argv[] = {name, nullptr};
    std::unique_ptr<QCoreApplication> app;
    if (QCoreApplication::instance() == nullptr) {
        app = std::make_unique<QCoreApplication>(argc, argv);
    }
    std::mutex mutex;
    std::vector<capture::CaptureDeviceInfo> answer;
    std::atomic<int> listed{0};
    CaptureCardWatcher watcher([&] {
        const std::scoped_lock lock(mutex);
        ++listed;
        return answer;
    });
    int changes = 0;
    QObject::connect(&watcher, &CaptureCardWatcher::devicesChanged, [&changes] { ++changes; });
    const auto settle = [&](int wanted, int lists) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while ((changes < wanted || listed.load() < lists) && std::chrono::steady_clock::now() < deadline) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        QCoreApplication::processEvents();
    };
    watcher.start();
    settle(1, 1);
    REQUIRE(changes == 1);
    CHECK(watcher.devices().empty());

    const int before = listed.load();
    watcher.refresh();
    settle(1, before + 1);
    CHECK(changes == 1);

    {
        const std::scoped_lock lock(mutex);
        answer = {kCard, kBezel};
    }
    watcher.refresh();
    settle(2, before + 2);
    CHECK(changes == 2);
    REQUIRE(watcher.devices().size() == 2);
    CHECK(captureCardsOf(watcher.devices()).size() == 1);

    {
        const std::scoped_lock lock(mutex);
        answer = {kBezel};
    }
    watcher.refresh();
    settle(3, before + 3);
    CHECK(changes == 3);
    CHECK(captureCardsOf(watcher.devices()).empty());
}

TEST_CASE("a capture card's settings survive the devices file, defaults are left out and wrong values are named (MED-014)")
{
    const settings::CaptureSettings defaults;
    CHECK(defaults.touch == "none");
    CHECK(defaults.aoaPhone.empty());
    CHECK(defaults.format.empty());
    CHECK(defaults.sound);
    CHECK(defaults.landscape == settings::CaptureLandscape::Left);
    CHECK_FALSE(manualCropOf(defaults));
    CHECK(defaults.kind == settings::CaptureKind::Automatic);

    settings::DeviceSettings device;
    device.capture.touch = "aoa";
    device.capture.aoaPhone = "R58N123ABC";
    device.capture.format = "1920x1080@60/NV12";
    device.capture.sound = false;
    device.capture.landscape = settings::CaptureLandscape::Right;
    storeManualCrop(device.capture, capture::CropBox{.x = 0.1, .y = 0.2, .width = 0.5, .height = 0.6});
    device.capture.kind = settings::CaptureKind::NotCaptureCard;
    device.profile = "shooter-2";
    settings::Devices devices;
    devices.byId["capture-1a2b3c4d"] = device;
    devices.byId["capture-00000000"] = settings::DeviceSettings{};
    const std::string json = settings::save(devices);
    CHECK(json.find("\"landscape\": \"right\"") != std::string::npos);
    CHECK(json.find("\"aoaPhone\": \"R58N123ABC\"") != std::string::npos);
    CHECK(json.find("\"kind\": \"notCaptureCard\"") != std::string::npos);
    CHECK(json.find("capture-00000000") == std::string::npos);
    const settings::Loaded<settings::Devices> loaded = settings::load<settings::Devices>(json);
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    REQUIRE(loaded.document.byId.contains("capture-1a2b3c4d"));
    CHECK(loaded.document.byId.at("capture-1a2b3c4d") == device);
    CHECK(settings::save(settings::Devices{}).find("capture") == std::string::npos);

    const settings::Loaded<settings::Devices> wrong = settings::load<settings::Devices>(
        R"({"format": "gamescrcpy.devices", "version": 1, "devices": {"capture-x": {"capture":
            {"landscape": "upsideDown", "cropX": 20000, "sound": "yes", "touch": "aoa"}}}})");
    REQUIRE(wrong.document.byId.contains("capture-x"));
    const settings::CaptureSettings& read = wrong.document.byId.at("capture-x").capture;
    CHECK(read.touch == "aoa");
    CHECK(read.landscape == settings::CaptureLandscape::Left);
    CHECK(read.cropX == 0);
    CHECK(read.sound);
    CHECK(reports(wrong, settings::Problem::UnknownName, R"($.devices["capture-x"].capture.landscape)"));
    CHECK(reports(wrong, settings::Problem::OutOfRange, R"($.devices["capture-x"].capture.cropX)"));
    CHECK(reports(wrong, settings::Problem::WrongType, R"($.devices["capture-x"].capture.sound)"));
}

TEST_CASE("ADR-0107: an old hidden capture device reads as not a capture card, and the three-way choice survives the "
          "devices file")
{
    const settings::Loaded<settings::Devices> old = settings::load<settings::Devices>(
        R"({"format": "gamescrcpy.devices", "version": 1, "devices": {)"
        R"("capture-hidden": {"capture": {"hidden": true, "touch": "aoa"}},)"
        R"("capture-shown": {"capture": {"hidden": false}},)"
        R"("capture-both": {"capture": {"hidden": true, "kind": "captureCard"}}}})");
    CHECK(old.usable);
    CHECK(old.diagnostics.empty());
    REQUIRE(old.document.byId.contains("capture-hidden"));
    CHECK(old.document.byId.at("capture-hidden").capture.kind == settings::CaptureKind::NotCaptureCard);
    CHECK(old.document.byId.at("capture-hidden").capture.touch == "aoa");
    REQUIRE(old.document.byId.contains("capture-shown"));
    CHECK(old.document.byId.at("capture-shown").capture.kind == settings::CaptureKind::Automatic);
    REQUIRE(old.document.byId.contains("capture-both"));
    CHECK(old.document.byId.at("capture-both").capture.kind == settings::CaptureKind::CaptureCard);
    const std::string saved = settings::save(old.document);
    CHECK(saved.find("\"hidden\"") == std::string::npos);
    CHECK(saved.find("\"kind\": \"notCaptureCard\"") != std::string::npos);
    CHECK(saved.find("\"kind\": \"captureCard\"") != std::string::npos);
    CHECK(saved.find("capture-shown") == std::string::npos);

    for (const settings::CaptureKind kind : {settings::CaptureKind::Automatic, settings::CaptureKind::CaptureCard,
                                             settings::CaptureKind::NotCaptureCard}) {
        CAPTURE(static_cast<int>(kind));
        settings::Devices devices;
        devices.byId["capture-1a2b3c4d"].capture.kind = kind;
        devices.byId["capture-1a2b3c4d"].profile = "shooter-2";
        const std::string json = settings::save(devices);
        CHECK((json.find("\"kind\"") == std::string::npos) == (kind == settings::CaptureKind::Automatic));
        const settings::Loaded<settings::Devices> loaded = settings::load<settings::Devices>(json);
        CHECK(loaded.diagnostics.empty());
        CHECK(loaded.document == devices);
    }

    const settings::Loaded<settings::Devices> wrong = settings::load<settings::Devices>(
        R"({"format": "gamescrcpy.devices", "version": 1, "devices": {"capture-x": {"capture":
            {"kind": "maybe", "hidden": "yes"}}}})");
    REQUIRE(wrong.document.byId.contains("capture-x"));
    CHECK(wrong.document.byId.at("capture-x").capture.kind == settings::CaptureKind::Automatic);
    CHECK(reports(wrong, settings::Problem::UnknownName, R"($.devices["capture-x"].capture.kind)"));
    CHECK_FALSE(reports(wrong, settings::Problem::UnknownField, R"($.devices["capture-x"].capture.hidden)"));
}

TEST_CASE("a picture area picked by hand is kept in ten-thousandths, inside the picture, and none means automatic")
{
    settings::CaptureSettings stored;
    storeManualCrop(stored, capture::CropBox{.x = 0.1, .y = 0.2, .width = 0.5, .height = 0.6});
    CHECK(stored.cropX == 1000);
    CHECK(stored.cropY == 2000);
    CHECK(stored.cropWidth == 5000);
    CHECK(stored.cropHeight == 6000);
    const std::optional<capture::CropBox> read = manualCropOf(stored);
    REQUIRE(read);
    CHECK(read->closeTo(capture::CropBox{.x = 0.1, .y = 0.2, .width = 0.5, .height = 0.6}, 1e-9));

    storeManualCrop(stored, capture::CropBox{.x = 0.8, .y = -0.5, .width = 0.5, .height = 2.0});
    CHECK(stored.cropX == 8000);
    CHECK(stored.cropY == 0);
    CHECK(stored.cropWidth == 2000);
    CHECK(stored.cropHeight == 10000);

    stored.cropWidth = 50;
    CHECK_FALSE(manualCropOf(stored));

    storeManualCrop(stored, std::nullopt);
    CHECK(stored.cropX == 0);
    CHECK(stored.cropWidth == 0);
    CHECK_FALSE(manualCropOf(stored));
}

TEST_CASE("touch on a capture card falls back to none when its way is not there, and names what is missing (MED-014)")
{
    const std::vector<platform::UsbPhone> phones{usbPhone("R58N123ABC", platform::UsbDriver::Ready),
                                                 usbPhone("ZY22BXK", platform::UsbDriver::Missing)};

    const TouchChoice none = chooseCaptureTouch(QStringLiteral("none"), {}, phones, true);
    CHECK(none.method == QStringLiteral("none"));
    CHECK(none.unavailable.isEmpty());
    CHECK(chooseCaptureTouch({}, {}, phones, true).method == QStringLiteral("none"));

    const TouchChoice aoa = chooseCaptureTouch(QStringLiteral("aoa"), "R58N123ABC", phones, false);
    CHECK(aoa.method == QStringLiteral("aoa"));
    CHECK(aoa.unavailable.isEmpty());
    REQUIRE(aoa.phone);
    CHECK(aoa.phone->serial == "R58N123ABC");

    for (const std::string& phone : {std::string("ZY22BXK"), std::string("8ABX0K4FA"), std::string()}) {
        CAPTURE(phone);
        const TouchChoice missing = chooseCaptureTouch(QStringLiteral("aoa"), phone, phones, true);
        CHECK(missing.method == QStringLiteral("none"));
        CHECK(missing.unavailable == QStringLiteral("AOA"));
        CHECK_FALSE(missing.phone);
    }

    const TouchChoice off = chooseCaptureTouch(QStringLiteral("esp32"), {}, phones, false);
    CHECK(off.method == QStringLiteral("none"));
    CHECK(off.unavailable == QStringLiteral("ESP32"));
    CHECK(chooseCaptureTouch(QStringLiteral("esp32"), {}, phones, true).method == QStringLiteral("esp32"));
    CHECK(chooseCaptureTouch(QStringLiteral("hid-bridge"), {}, {}, false).unavailable == QStringLiteral("hid-bridge"));
    CHECK(touchMethodName(QStringLiteral("none")) == QStringLiteral("No touch"));

    DroppingSink dropping;
    CHECK(dropping.send(protocol::TouchFrame{}));
    CHECK(dropping.send(protocol::Key{}));
}

TEST_CASE("ADR-0107: a capture card's summary line names the picture size it would use, its touch and its profile")
{
    const QStringList formats{QStringLiteral("3840x2160@30/MJPG"), QStringLiteral("1280x720@60/NV12"),
                              QStringLiteral("1920x1080@60/YUY2"), QStringLiteral("not a format")};
    CHECK(captureResolution(formats, {}) == QSize(1920, 1080));
    CHECK(captureResolution(formats, "1280x720@60/NV12") == QSize(1280, 720));
    CHECK(captureResolution(formats, "2560x1440@60/NV12") == QSize(1920, 1080));
    CHECK(captureResolution({}, "2560x1440@59.94/MJPG") == QSize(2560, 1440));
    CHECK_FALSE(captureResolution({}, {}).isValid());
    CHECK_FALSE(captureResolution({QStringLiteral("not a format")}, "garbage").isValid());

    CHECK(captureSummary(QSize(1920, 1080), QStringLiteral("ESP32 touch box"), QStringLiteral("Genshin · Default")) ==
          QStringLiteral("Capture card · 1920 × 1080 · Touch: ESP32 touch box · Profile: Genshin · Default"));
    CHECK(captureSummary({}, QStringLiteral("No touch"), {}) == QStringLiteral("Capture card · Touch: No touch"));
    CHECK(captureSummary(QSize(1280, 720), {}, QStringLiteral("Shooter")) ==
          QStringLiteral("Capture card · 1280 × 720 · Profile: Shooter"));
    CHECK(captureSummary(QSize(0, 1080), {}, {}) == QStringLiteral("Capture card"));
}
