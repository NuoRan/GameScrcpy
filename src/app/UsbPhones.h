#pragma once

#include <gs/platform/Usb.h>

#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace gs::app {

struct AdbPhone {
    QString serial;
    QString deviceId;
};

struct UsbPlan {
    std::vector<std::pair<std::size_t, std::size_t>> attached;
    std::vector<std::size_t> alone;

    friend bool operator==(const UsbPlan&, const UsbPlan&) = default;
};

[[nodiscard]] bool sameSerial(QStringView a, QStringView b) noexcept;
[[nodiscard]] QString serialInMdnsName(const QString& adbSerial);
[[nodiscard]] bool isUsbAdbSerial(const QString& adbSerial);
[[nodiscard]] bool matchesUsbPhone(const platform::UsbPhone& phone, const AdbPhone& adb);
[[nodiscard]] UsbPlan planUsbPhones(const std::vector<platform::UsbPhone>& phones, const std::vector<AdbPhone>& adb);
[[nodiscard]] QString aoaSerialOf(const QString& usbSerial);
[[nodiscard]] bool isAoaSerial(const QString& serial);

enum class ControlTurn : std::uint8_t { Portrait, LandscapeLeft, LandscapeRight };

[[nodiscard]] int rotationOf(ControlTurn turn) noexcept;
[[nodiscard]] QSize controlCanvas(QSize naturalScreen, ControlTurn turn);
[[nodiscard]] QSize naturalScreenOf(QSize shown, int rotation);
[[nodiscard]] QStringList featuresWithoutPicture(bool adb = false);

class UsbPhoneWatcher final : public QObject
{
    Q_OBJECT

public:
    using Lister = std::function<std::vector<platform::UsbPhone>()>;

    explicit UsbPhoneWatcher(Lister lister = platform::listUsbPhones, QObject* parent = nullptr);
    ~UsbPhoneWatcher() override;

    void start();
    void refresh();
    [[nodiscard]] const std::vector<platform::UsbPhone>& phones() const noexcept { return m_phones; }

signals:
    void phonesChanged();

private:
    void run(const std::stop_token& stop);
    void arrived(std::vector<platform::UsbPhone> phones);

    Lister m_lister;
    std::vector<platform::UsbPhone> m_phones;
    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    bool m_dirty = false;
    std::unique_ptr<platform::UsbWatcher> m_watcher;
    std::jthread m_thread;
};

}
