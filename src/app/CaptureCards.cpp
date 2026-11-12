#include "CaptureCards.h"

#include <gs/capture/CaptureFormat.h>
#include <gs/platform/Thread.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QtLogging>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace gs::app {

namespace {

constexpr QStringView kCapturePrefix = u"capture-";

constexpr std::array<QStringView, 12> kCaptureWords{
    u"capture", u"hdmi",   u"cam link",  u"elgato", u"avermedia",     u"magewell",
    u"ugreen",  u"acasis", u"usb video", u"uvc",    u"video grabber", u"采集"};

constexpr std::array<QStringView, 9> kOtherVideoWords{
    u"virtual", u"虚拟", u"obs", u"vcam", u"streaming center", u"webcastmate", u"camera", u"webcam", u"摄像"};

std::int32_t scaled(double fraction)
{
    return static_cast<std::int32_t>(
        std::lround(std::clamp(fraction, 0.0, 1.0) * static_cast<double>(settings::CaptureSettings::kCropScale)));
}

}

QString captureSerialOf(std::wstring_view link)
{
    return kCapturePrefix.toString() + QString::fromStdString(capture::shortHash(link));
}

bool isCaptureSerial(QStringView serial) noexcept
{
    return serial.startsWith(kCapturePrefix);
}

QString demoCaptureSerial()
{
    return kCapturePrefix.toString() + QStringLiteral("demo");
}

QString captureCardName(const QString& friendlyName)
{
    const QString name = friendlyName.trimmed();
    return name.isEmpty() ? QCoreApplication::translate("CaptureCards", "Unnamed capture device") : name;
}

bool looksLikeCaptureCard(QStringView name)
{
    const auto named = [name](QStringView word) { return name.contains(word, Qt::CaseInsensitive); };
    return std::ranges::any_of(kCaptureWords, named) || std::ranges::none_of(kOtherVideoWords, named);
}

bool listedAsCaptureCard(settings::CaptureKind kind, QStringView name)
{
    switch (kind) {
    case settings::CaptureKind::CaptureCard:
        return true;
    case settings::CaptureKind::NotCaptureCard:
        return false;
    case settings::CaptureKind::Automatic:
        break;
    }
    return looksLikeCaptureCard(name);
}

std::vector<CaptureCard> captureCardsOf(const std::vector<capture::CaptureDeviceInfo>& devices)
{
    std::vector<CaptureCard> cards;
    for (const capture::CaptureDeviceInfo& device : devices) {
        if (capture::builtInCamera(device) || device.link.empty()) {
            continue;
        }
        CaptureCard card{.serial = captureSerialOf(device.link),
                         .name = QString::fromStdString(device.name),
                         .link = device.link,
                         .container = device.container};
        if (std::ranges::none_of(cards, [&card](const CaptureCard& other) { return other.serial == card.serial; })) {
            cards.push_back(std::move(card));
        }
    }
    return cards;
}

CapturePlan planCaptureCards(const std::vector<capture::CaptureDeviceInfo>& devices, const std::vector<QString>& shown,
                             const CaptureKindOf& kindOf)
{
    CapturePlan plan;
    std::vector<QString> listed;
    for (const CaptureCard& card : captureCardsOf(devices)) {
        const settings::CaptureKind kind = kindOf ? kindOf(card.serial) : settings::CaptureKind::Automatic;
        if (!listedAsCaptureCard(kind, card.name)) {
            plan.others.push_back({.serial = card.serial,
                                   .name = captureCardName(card.name),
                                   .hidden = kind == settings::CaptureKind::NotCaptureCard});
            continue;
        }
        listed.push_back(card.serial);
        if (std::ranges::find(shown, card.serial) == shown.end()) {
            plan.added.push_back(card);
        }
    }
    for (const QString& serial : shown) {
        if (std::ranges::find(listed, serial) == listed.end()) {
            plan.removed.push_back(serial);
        }
    }
    return plan;
}

QSize captureResolution(const QStringList& formats, std::string_view chosen)
{
    std::vector<capture::CaptureFormat> known;
    for (const QString& name : formats) {
        if (const std::optional<capture::CaptureFormat> format = capture::parseFormatName(name.toStdString())) {
            known.push_back(*format);
        }
    }
    std::optional<capture::CaptureFormat> picked = capture::chooseFormat(known, chosen);
    if (!picked && known.empty()) {
        picked = capture::parseFormatName(chosen);
    }
    return picked ? QSize(picked->width, picked->height) : QSize();
}

QString captureSummary(QSize resolution, const QString& touch, const QString& profile)
{
    QStringList parts{QCoreApplication::translate("CaptureCards", "Capture card")};
    if (resolution.width() > 0 && resolution.height() > 0) {
        parts.append(QStringLiteral("%1 × %2").arg(resolution.width()).arg(resolution.height()));
    }
    if (!touch.isEmpty()) {
        parts.append(QCoreApplication::translate("CaptureCards", "Touch: %1").arg(touch));
    }
    if (!profile.isEmpty()) {
        parts.append(QCoreApplication::translate("CaptureCards", "Profile: %1").arg(profile));
    }
    return parts.join(QStringLiteral(" · "));
}

std::optional<capture::CropBox> manualCropOf(const settings::CaptureSettings& settings)
{
    if (settings.cropWidth <= 0 || settings.cropHeight <= 0) {
        return std::nullopt;
    }
    const auto scale = static_cast<double>(settings::CaptureSettings::kCropScale);
    capture::CropBox box{.x = settings.cropX / scale,
                         .y = settings.cropY / scale,
                         .width = settings.cropWidth / scale,
                         .height = settings.cropHeight / scale};
    box.width = std::min(box.width, 1.0 - box.x);
    box.height = std::min(box.height, 1.0 - box.y);
    if (box.width < 0.01 || box.height < 0.01) {
        return std::nullopt;
    }
    return box;
}

void storeManualCrop(settings::CaptureSettings& settings, const std::optional<capture::CropBox>& crop)
{
    if (!crop) {
        settings.cropX = 0;
        settings.cropY = 0;
        settings.cropWidth = 0;
        settings.cropHeight = 0;
        return;
    }
    settings.cropX = scaled(crop->x);
    settings.cropY = scaled(crop->y);
    settings.cropWidth = std::min(scaled(crop->width), static_cast<std::int32_t>(settings::CaptureSettings::kCropScale) -
                                                           settings.cropX);
    settings.cropHeight = std::min(scaled(crop->height),
                                   static_cast<std::int32_t>(settings::CaptureSettings::kCropScale) - settings.cropY);
}

QString touchMethodName(const QString& method)
{
    if (method == u"aoa") {
        return QStringLiteral("AOA");
    }
    if (method.isEmpty() || method == u"none") {
        return QCoreApplication::translate("CaptureCards", "No touch");
    }
    return method;
}

TouchChoice chooseCaptureTouch(const QString& method, const std::string& aoaPhone,
                               const std::vector<platform::UsbPhone>& phones, bool extensionReady)
{
    const QString none = QStringLiteral("none");
    if (method.isEmpty() || method == none) {
        return {.method = none};
    }
    if (method == u"aoa") {
        const auto found = std::ranges::find_if(phones, [&aoaPhone](const platform::UsbPhone& phone) {
            return !aoaPhone.empty() && phone.serial == aoaPhone;
        });
        if (found == phones.end() || found->driver != platform::UsbDriver::Ready) {
            return {.method = none, .unavailable = touchMethodName(method)};
        }
        return {.method = method, .phone = *found};
    }
    if (!extensionReady) {
        return {.method = none, .unavailable = touchMethodName(method)};
    }
    return {.method = method};
}

QString touchBoxSerial(const ProvidedDevice& device)
{
    return QStringLiteral("box:%1/%2/%3")
        .arg(QString::fromStdString(device.extension), QString::fromStdString(device.provider),
             QString::fromStdString(device.id));
}

bool isTouchBoxSerial(QStringView serial) noexcept
{
    return serial.startsWith(u"box:");
}

bool pairsWithCapture(const ProvidedDevice& device, bool listed) noexcept
{
    return listed && device.capture && !device.touchMethod.empty();
}

CaptureCardWatcher::CaptureCardWatcher(Lister lister, QObject* parent) : QObject(parent), m_lister(std::move(lister)) {}

CaptureCardWatcher::~CaptureCardWatcher()
{
    m_watcher.reset();
    m_thread.request_stop();
    m_wake.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void CaptureCardWatcher::start()
{
    if (m_thread.joinable()) {
        return;
    }
    m_thread = std::jthread([this](const std::stop_token& stop) { run(stop); });
    m_watcher = std::make_unique<capture::DeviceWatcher>([this] { refresh(); });
    if (!m_watcher->started()) {
        qWarning("capture devices are not watched: a capture card plugged in later shows up when the app looks again");
    }
    refresh();
}

void CaptureCardWatcher::refresh()
{
    {
        const std::scoped_lock lock(m_mutex);
        m_dirty = true;
    }
    m_wake.notify_all();
}

void CaptureCardWatcher::run(const std::stop_token& stop)
{
    platform::setCurrentThreadName("capture cards");
    for (;;) {
        {
            std::unique_lock lock(m_mutex);
            if (!m_wake.wait(lock, stop, [this] { return m_dirty; })) {
                return;
            }
            m_dirty = false;
        }
        std::vector<capture::CaptureDeviceInfo> devices = m_lister ? m_lister() : std::vector<capture::CaptureDeviceInfo>();
        QMetaObject::invokeMethod(
            this, [this, devices = std::move(devices)]() mutable { arrived(std::move(devices)); }, Qt::QueuedConnection);
    }
}

void CaptureCardWatcher::arrived(std::vector<capture::CaptureDeviceInfo> devices)
{
    if (m_listed && devices == m_devices) {
        return;
    }
    m_listed = true;
    m_devices = std::move(devices);
    emit devicesChanged();
}

}
