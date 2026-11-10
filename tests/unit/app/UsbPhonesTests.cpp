#include "HidTouchscreen.h"
#include "UsbPhones.h"

#include <gs/platform/Usb.h>

#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

using namespace gs;
using namespace gs::app;

namespace {

platform::UsbPhone phone(const char* serial, platform::UsbDriver driver = platform::UsbDriver::Ready)
{
    return {.serial = serial, .name = "Phone", .vendorId = 0x18D1, .productId = 0x4EE7, .driver = driver};
}

}

TEST_CASE("a phone on USB and the same phone in adb are one card, matched by the USB serial (HID-003)")
{
    const std::vector<platform::UsbPhone> phones{phone("R58N123ABC"), phone("3b7c2a4f"), phone("ZY22BXK"),
                                                 phone("8ABX0K4FA")};
    const std::vector<AdbPhone> adb{
        {.serial = QStringLiteral("R58N123ABC")},
        {.serial = QStringLiteral("192.168.1.20:5555"), .deviceId = QStringLiteral("3B7C2A4F")},
        {.serial = QStringLiteral("adb-ZY22BXK-Ab12Cd._adb-tls-connect._tcp")},
        {.serial = QStringLiteral("emulator-5554"), .deviceId = QStringLiteral("emulator-5554")},
    };
    const UsbPlan plan = planUsbPhones(phones, adb);
    const std::vector<std::pair<std::size_t, std::size_t>> attached{{0, 0}, {1, 1}, {2, 2}};
    CHECK(plan.attached == attached);
    CHECK(plan.alone == std::vector<std::size_t>{3});

    CHECK(planUsbPhones({phone("R58N123ABC")}, {{.serial = QStringLiteral("R58N123ABC")},
                                                {.serial = QStringLiteral("10.0.0.5:5555"),
                                                 .deviceId = QStringLiteral("R58N123ABC")}})
              .attached.size() == 2);
    CHECK(planUsbPhones({phone("XYZ")}, {}).alone == std::vector<std::size_t>{0});
    CHECK(planUsbPhones({phone("XYZ")}, {{.serial = QStringLiteral("10.0.0.5:5555")}}).alone ==
          std::vector<std::size_t>{0});
}

TEST_CASE("the serial inside a wireless debugging name, adb's USB serials and the AOA cards' own serials")
{
    CHECK(serialInMdnsName(QStringLiteral("adb-ZY22BXK-Ab12Cd._adb-tls-connect._tcp")) == QStringLiteral("ZY22BXK"));
    CHECK(serialInMdnsName(QStringLiteral("adb-R5-8N-x1._adb-tls-connect._tcp")) == QStringLiteral("R5-8N"));
    CHECK(serialInMdnsName(QStringLiteral("192.168.1.20:5555")).isEmpty());
    CHECK(serialInMdnsName(QStringLiteral("adb-._adb-tls-connect._tcp")).isEmpty());
    CHECK(isUsbAdbSerial(QStringLiteral("R58N123ABC")));
    CHECK_FALSE(isUsbAdbSerial(QStringLiteral("192.168.1.20:5555")));
    CHECK_FALSE(isUsbAdbSerial(QStringLiteral("emulator-5554")));
    CHECK_FALSE(isUsbAdbSerial(QStringLiteral("adb-ZY22BXK-Ab12Cd._adb-tls-connect._tcp")));
    CHECK(aoaSerialOf(QStringLiteral("R58N123ABC")) == QStringLiteral("aoa-R58N123ABC"));
    CHECK(isAoaSerial(aoaSerialOf(QStringLiteral("R58N123ABC"))));
    CHECK_FALSE(isUsbAdbSerial(aoaSerialOf(QStringLiteral("R58N123ABC"))));
    CHECK(sameSerial(u"r58n123abc", u"R58N123ABC"));
    CHECK_FALSE(sameSerial(u"", u""));
}

TEST_CASE("the control window takes the phone's screen in the picked orientation, or 9:20 when it was never seen "
          "(HID-006)")
{
    CHECK(rotationOf(ControlTurn::Portrait) == 0);
    CHECK(rotationOf(ControlTurn::LandscapeLeft) == 1);
    CHECK(rotationOf(ControlTurn::LandscapeRight) == 3);
    CHECK(controlCanvas(QSize(1080, 2340), ControlTurn::Portrait) == QSize(1080, 2340));
    CHECK(controlCanvas(QSize(1080, 2340), ControlTurn::LandscapeLeft) == QSize(2340, 1080));
    CHECK(controlCanvas(QSize(1080, 2340), ControlTurn::LandscapeRight) == QSize(2340, 1080));
    CHECK(controlCanvas(QSize(), ControlTurn::Portrait) == QSize(1080, 2400));
    CHECK(controlCanvas(QSize(0, 2400), ControlTurn::LandscapeLeft) == QSize(2400, 1080));
    CHECK(naturalScreenOf(QSize(2400, 1080), 1) == QSize(1080, 2400));
    CHECK(naturalScreenOf(QSize(2400, 1080), 3) == QSize(1080, 2400));
    CHECK(naturalScreenOf(QSize(1080, 2400), 0) == QSize(1080, 2400));
    CHECK(naturalScreenOf(QSize(1080, 2400), 2) == QSize(1080, 2400));
}

TEST_CASE("turned left puts the picture's left edge on the phone's top, turned right its right edge (HID-006)")
{
    const auto top = [](const UhidPosition& at) { return at.y == 0; };
    CHECK(top(naturalPosition(0, 32768, rotationOf(ControlTurn::LandscapeLeft))));
    CHECK(top(naturalPosition(65535, 32768, rotationOf(ControlTurn::LandscapeRight))));
    CHECK(top(naturalPosition(32768, 0, rotationOf(ControlTurn::Portrait))));
    CHECK_FALSE(top(naturalPosition(65535, 32768, rotationOf(ControlTurn::LandscapeLeft))));
}

TEST_CASE("the features a missing picture takes away are listed for the user to confirm (HID-006)")
{
    const QStringList features = featuresWithoutPicture();
    CHECK(features.size() >= 6);
    const QString all = features.join(u'\n');
    for (const char* needed : {"vision", "Recording", "Clipboard", "apps", "latency"}) {
        CAPTURE(needed);
        CHECK(all.contains(QString::fromLatin1(needed), Qt::CaseInsensitive));
    }
}

TEST_CASE("ADR-0108: a phone without a picture but with USB debugging lacks only vision, recording and the latency test")
{
    const QStringList full = featuresWithoutPicture();
    const QStringList adb = featuresWithoutPicture(true);
    CHECK(featuresWithoutPicture(false) == full);
    CHECK(full.size() == 8);
    REQUIRE(adb.size() == 4);
    CHECK(adb[0] == full[0]);
    CHECK(adb[1] == full[1]);
    CHECK(adb[2] == full[2]);
    CHECK(adb[3] == full[5]);
}

TEST_CASE("the USB watcher lists phones off the UI thread and tells only about changes (HID-003)")
{
    int argc = 1;
    char name[] = "test";
    char* argv[] = {name, nullptr};
    std::unique_ptr<QCoreApplication> app;
    if (QCoreApplication::instance() == nullptr) {
        app = std::make_unique<QCoreApplication>(argc, argv);
    }
    std::mutex mutex;
    std::vector<platform::UsbPhone> answer{phone("R58N123ABC", platform::UsbDriver::Missing)};
    std::atomic<int> listed{0};
    UsbPhoneWatcher watcher([&] {
        const std::scoped_lock lock(mutex);
        ++listed;
        return answer;
    });
    int changes = 0;
    QObject::connect(&watcher, &UsbPhoneWatcher::phonesChanged, [&changes] { ++changes; });
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
    REQUIRE(watcher.phones().size() == 1);
    CHECK(watcher.phones()[0].driver == platform::UsbDriver::Missing);

    const int before = listed.load();
    watcher.refresh();
    settle(1, before + 1);
    CHECK(changes == 1);

    {
        const std::scoped_lock lock(mutex);
        answer[0].driver = platform::UsbDriver::Ready;
    }
    watcher.refresh();
    settle(2, before + 2);
    CHECK(changes == 2);
    CHECK(watcher.phones()[0].driver == platform::UsbDriver::Ready);
}
