#pragma once

#include "ControlChannel.h"
#include "ProvidedDevice.h"
#include "Settings.h"

#include <gs/capture/CaptureDevices.h>
#include <gs/capture/CropDetector.h>
#include <gs/platform/Usb.h>

#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace gs::app {

struct CaptureCard {
    QString serial;
    QString name;
    std::wstring link;
    std::wstring container;

    friend bool operator==(const CaptureCard&, const CaptureCard&) = default;
};

struct OtherVideoDevice {
    QString serial;
    QString name;
    bool hidden = false;

    friend bool operator==(const OtherVideoDevice&, const OtherVideoDevice&) = default;
};

struct CapturePlan {
    std::vector<CaptureCard> added;
    std::vector<QString> removed;
    std::vector<OtherVideoDevice> others;
};

using CaptureKindOf = std::function<settings::CaptureKind(const QString& serial)>;

[[nodiscard]] QString captureSerialOf(std::wstring_view link);
[[nodiscard]] bool isCaptureSerial(QStringView serial) noexcept;
[[nodiscard]] QString demoCaptureSerial();
[[nodiscard]] QString captureCardName(const QString& friendlyName);
[[nodiscard]] bool looksLikeCaptureCard(QStringView name);
[[nodiscard]] bool listedAsCaptureCard(settings::CaptureKind kind, QStringView name);
[[nodiscard]] std::vector<CaptureCard> captureCardsOf(const std::vector<capture::CaptureDeviceInfo>& devices);
[[nodiscard]] CapturePlan planCaptureCards(const std::vector<capture::CaptureDeviceInfo>& devices,
                                           const std::vector<QString>& shown, const CaptureKindOf& kindOf);
[[nodiscard]] QSize captureResolution(const QStringList& formats, std::string_view chosen);
[[nodiscard]] QString captureSummary(QSize resolution, const QString& touch, const QString& profile);

[[nodiscard]] std::optional<capture::CropBox> manualCropOf(const settings::CaptureSettings& settings);
void storeManualCrop(settings::CaptureSettings& settings, const std::optional<capture::CropBox>& crop);
[[nodiscard]] QString touchMethodName(const QString& method);

struct TouchChoice {
    QString method;
    QString unavailable;
    std::optional<platform::UsbPhone> phone;
};

[[nodiscard]] TouchChoice chooseCaptureTouch(const QString& method, const std::string& aoaPhone,
                                             const std::vector<platform::UsbPhone>& phones, bool extensionReady);
[[nodiscard]] QString touchBoxSerial(const ProvidedDevice& device);
[[nodiscard]] bool isTouchBoxSerial(QStringView serial) noexcept;
[[nodiscard]] bool pairsWithCapture(const ProvidedDevice& device, bool listed) noexcept;

class DroppingSink final : public ControlSink
{
public:
    [[nodiscard]] bool send(const protocol::TouchFrame&) override { return true; }
    [[nodiscard]] bool send(const protocol::Key&) override { return true; }
    [[nodiscard]] bool send(const protocol::Scroll&) override { return true; }
    [[nodiscard]] bool send(const protocol::Text&) override { return true; }
};

class CaptureCardWatcher final : public QObject
{
    Q_OBJECT

public:
    using Lister = std::function<std::vector<capture::CaptureDeviceInfo>()>;

    explicit CaptureCardWatcher(Lister lister = capture::listCaptureDevices, QObject* parent = nullptr);
    ~CaptureCardWatcher() override;

    void start();
    void refresh();
    [[nodiscard]] const std::vector<capture::CaptureDeviceInfo>& devices() const noexcept { return m_devices; }

signals:
    void devicesChanged();

private:
    void run(const std::stop_token& stop);
    void arrived(std::vector<capture::CaptureDeviceInfo> devices);

    Lister m_lister;
    std::vector<capture::CaptureDeviceInfo> m_devices;
    bool m_listed = false;
    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    bool m_dirty = false;
    std::unique_ptr<capture::DeviceWatcher> m_watcher;
    std::jthread m_thread;
};

}
