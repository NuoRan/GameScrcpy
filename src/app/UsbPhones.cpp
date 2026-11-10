#include "UsbPhones.h"

#include <gs/platform/Thread.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QtLogging>

#include <utility>

namespace gs::app {

namespace {

constexpr QStringView kMdnsSuffix = u"._adb-tls-connect._tcp";
constexpr QStringView kMdnsPrefix = u"adb-";
constexpr QStringView kAoaPrefix = u"aoa-";
constexpr int kDefaultShortSide = 1080;
constexpr int kDefaultLongSide = 2400;

}

bool sameSerial(QStringView a, QStringView b) noexcept
{
    return !a.isEmpty() && a.compare(b, Qt::CaseInsensitive) == 0;
}

QString serialInMdnsName(const QString& adbSerial)
{
    const qsizetype end = adbSerial.indexOf(kMdnsSuffix);
    if (!adbSerial.startsWith(kMdnsPrefix) || end < 0) {
        return {};
    }
    const QString name = adbSerial.mid(kMdnsPrefix.size(), end - kMdnsPrefix.size());
    const qsizetype dash = name.lastIndexOf(u'-');
    return dash > 0 ? name.left(dash) : QString();
}

bool isAoaSerial(const QString& serial)
{
    return serial.startsWith(kAoaPrefix);
}

bool isUsbAdbSerial(const QString& adbSerial)
{
    return !adbSerial.isEmpty() && !adbSerial.startsWith(u"emulator-") && !adbSerial.contains(u':') &&
           !adbSerial.contains(kMdnsSuffix) && !isAoaSerial(adbSerial);
}

bool matchesUsbPhone(const platform::UsbPhone& phone, const AdbPhone& adb)
{
    const QString serial = QString::fromStdString(phone.serial);
    if (sameSerial(adb.deviceId, serial)) {
        return true;
    }
    if (isUsbAdbSerial(adb.serial) && sameSerial(adb.serial, serial)) {
        return true;
    }
    return sameSerial(serialInMdnsName(adb.serial), serial);
}

UsbPlan planUsbPhones(const std::vector<platform::UsbPhone>& phones, const std::vector<AdbPhone>& adb)
{
    UsbPlan plan;
    for (std::size_t phone = 0; phone < phones.size(); ++phone) {
        bool matched = false;
        for (std::size_t row = 0; row < adb.size(); ++row) {
            if (matchesUsbPhone(phones[phone], adb[row])) {
                plan.attached.emplace_back(phone, row);
                matched = true;
            }
        }
        if (!matched) {
            plan.alone.push_back(phone);
        }
    }
    return plan;
}

QString aoaSerialOf(const QString& usbSerial)
{
    return kAoaPrefix.toString() + usbSerial;
}

int rotationOf(ControlTurn turn) noexcept
{
    switch (turn) {
    case ControlTurn::LandscapeLeft:
        return 1;
    case ControlTurn::LandscapeRight:
        return 3;
    case ControlTurn::Portrait:
        break;
    }
    return 0;
}

QSize naturalScreenOf(QSize shown, int rotation)
{
    return (rotation & 1) != 0 ? shown.transposed() : shown;
}

QSize controlCanvas(QSize naturalScreen, ControlTurn turn)
{
    const QSize natural =
        naturalScreen.width() > 0 && naturalScreen.height() > 0 ? naturalScreen : QSize(kDefaultShortSide, kDefaultLongSide);
    return (rotationOf(turn) & 1) != 0 ? natural.transposed() : natural;
}

QStringList featuresWithoutPicture(bool adb)
{
    QStringList features{
        QCoreApplication::translate("UsbPhones", "Vision: finding pictures and colors, and the vision workbench"),
        QCoreApplication::translate("UsbPhones", "vision.* in logic and scripts, which answers null, [] or false"),
        QCoreApplication::translate("UsbPhones", "Recording"),
    };
    if (!adb) {
        features.append(QCoreApplication::translate("UsbPhones", "Clipboard sharing"));
        features.append(QCoreApplication::translate("UsbPhones", "Starting apps and the Apps page"));
    }
    features.append(QCoreApplication::translate("UsbPhones", "The latency test"));
    if (!adb) {
        features.append(QCoreApplication::translate("UsbPhones", "Sound, turning the screen off and rotation"));
        features.append(QCoreApplication::translate("UsbPhones", "Switching profiles with the game in front"));
    }
    return features;
}

UsbPhoneWatcher::UsbPhoneWatcher(Lister lister, QObject* parent)
    : QObject(parent)
    , m_lister(std::move(lister))
{
}

UsbPhoneWatcher::~UsbPhoneWatcher()
{
    m_watcher.reset();
    m_thread.request_stop();
    m_wake.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void UsbPhoneWatcher::start()
{
    if (m_thread.joinable()) {
        return;
    }
    m_thread = std::jthread([this](const std::stop_token& stop) { run(stop); });
    m_watcher = std::make_unique<platform::UsbWatcher>([this] { refresh(); });
    if (!m_watcher->started()) {
        qWarning("USB arrivals are not watched: phones plugged in later are seen when the app looks again");
    }
    refresh();
}

void UsbPhoneWatcher::refresh()
{
    {
        const std::scoped_lock lock(m_mutex);
        m_dirty = true;
    }
    m_wake.notify_all();
}

void UsbPhoneWatcher::run(const std::stop_token& stop)
{
    platform::setCurrentThreadName("usb phones");
    for (;;) {
        {
            std::unique_lock lock(m_mutex);
            if (!m_wake.wait(lock, stop, [this] { return m_dirty; })) {
                return;
            }
            m_dirty = false;
        }
        std::vector<platform::UsbPhone> phones = m_lister();
        QMetaObject::invokeMethod(
            this, [this, phones = std::move(phones)]() mutable { arrived(std::move(phones)); }, Qt::QueuedConnection);
    }
}

void UsbPhoneWatcher::arrived(std::vector<platform::UsbPhone> phones)
{
    if (phones == m_phones) {
        return;
    }
    m_phones = std::move(phones);
    emit phonesChanged();
}

}
