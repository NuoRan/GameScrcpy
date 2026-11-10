#include "Links.h"

#include <objbase.h>

#include <winrt/base.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>

#include <atomic>
#include <format>
#include <map>
#include <mutex>
#include <utility>

namespace gs::hardware::detail {

namespace {

namespace bluetooth = winrt::Windows::Devices::Bluetooth;
namespace advertising = winrt::Windows::Devices::Bluetooth::Advertisement;
namespace gatt = winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
using winrt::Windows::Security::Cryptography::CryptographicBuffer;
using winrt::Windows::Storage::Streams::IBuffer;

std::string uuidText(const winrt::guid& id)
{
    return std::format("{:08x}-{:04x}-{:04x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}", id.Data1, id.Data2,
                       id.Data3, static_cast<unsigned>(id.Data4[0]), static_cast<unsigned>(id.Data4[1]),
                       static_cast<unsigned>(id.Data4[2]), static_cast<unsigned>(id.Data4[3]),
                       static_cast<unsigned>(id.Data4[4]), static_cast<unsigned>(id.Data4[5]),
                       static_cast<unsigned>(id.Data4[6]), static_cast<unsigned>(id.Data4[7]));
}

Bytes bytesOf(const IBuffer& buffer)
{
    if (!buffer) {
        return {};
    }
    winrt::com_array<std::uint8_t> array;
    CryptographicBuffer::CopyToByteArray(buffer, array);
    return Bytes(array.begin(), array.end());
}

std::string failureOf(gatt::GattCommunicationStatus status)
{
    switch (status) {
    case gatt::GattCommunicationStatus::Success:
        return {};
    case gatt::GattCommunicationStatus::Unreachable:
        return "the Bluetooth device is not in reach";
    case gatt::GattCommunicationStatus::ProtocolError:
        return "the Bluetooth device refused it";
    case gatt::GattCommunicationStatus::AccessDenied:
        return "Windows did not allow it";
    }
    return "Bluetooth failed";
}

std::string failureOf(const winrt::hresult_error& error)
{
    const std::string message = winrt::to_string(error.message());
    return message.empty() ? std::string("Bluetooth failed") : message;
}

std::vector<std::string> propertiesOf(gatt::GattCharacteristicProperties properties)
{
    const auto bits = static_cast<std::uint32_t>(properties);
    const auto has = [bits](gatt::GattCharacteristicProperties flag) {
        return (bits & static_cast<std::uint32_t>(flag)) != 0;
    };
    std::vector<std::string> names;
    if (has(gatt::GattCharacteristicProperties::Read)) {
        names.emplace_back("read");
    }
    if (has(gatt::GattCharacteristicProperties::Write)) {
        names.emplace_back("write");
    }
    if (has(gatt::GattCharacteristicProperties::WriteWithoutResponse)) {
        names.emplace_back("writeWithoutResponse");
    }
    if (has(gatt::GattCharacteristicProperties::Notify)) {
        names.emplace_back("notify");
    }
    if (has(gatt::GattCharacteristicProperties::Indicate)) {
        names.emplace_back("indicate");
    }
    return names;
}

std::string keyOf(const std::string& service, const std::string& characteristic)
{
    return service + "/" + characteristic;
}

struct Session : std::enable_shared_from_this<Session> {
    explicit Session(std::shared_ptr<Events> sink)
        : events(std::move(sink))
    {
    }

    ~Session()
    {
        try {
            for (const auto& [key, token] : subscriptions) {
                if (const auto found = characteristics.find(key); found != characteristics.end()) {
                    found->second.ValueChanged(token);
                }
            }
            if (device) {
                device.ConnectionStatusChanged(statusToken);
            }
            if (gattSession) {
                gattSession.MaintainConnection(false);
                gattSession.Close();
            }
            if (device) {
                device.Close();
            }
        } catch (...) {
            ended.store(true, std::memory_order_release);
        }
    }

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void end(Closed reason, const std::string& problem)
    {
        if (!ended.exchange(true, std::memory_order_acq_rel)) {
            events->closed(reason, problem);
        }
    }

    [[nodiscard]] const gatt::GattCharacteristic* find(const std::string& service, const std::string& characteristic) const
    {
        const auto found = characteristics.find(keyOf(service, characteristic));
        return found == characteristics.end() ? nullptr : &found->second;
    }

    void connect(std::uint64_t address, OpenResult& result)
    {
        device = bluetooth::BluetoothLEDevice::FromBluetoothAddressAsync(address).get();
        if (!device) {
            result.missing = true;
            result.problem = "the Bluetooth device is not in reach";
            return;
        }
        gattSession = gatt::GattSession::FromDeviceIdAsync(device.BluetoothDeviceId()).get();
        if (gattSession) {
            gattSession.MaintainConnection(true);
        }
        const gatt::GattDeviceServicesResult listed =
            device.GetGattServicesAsync(bluetooth::BluetoothCacheMode::Uncached).get();
        if (listed.Status() != gatt::GattCommunicationStatus::Success) {
            result.missing = listed.Status() == gatt::GattCommunicationStatus::Unreachable;
            result.problem = failureOf(listed.Status());
            return;
        }
        for (const gatt::GattDeviceService& service : listed.Services()) {
            BleService entry;
            entry.uuid = uuidText(service.Uuid());
            const gatt::GattCharacteristicsResult found =
                service.GetCharacteristicsAsync(bluetooth::BluetoothCacheMode::Uncached).get();
            if (found.Status() == gatt::GattCommunicationStatus::Success) {
                for (const gatt::GattCharacteristic& characteristic : found.Characteristics()) {
                    BleCharacteristic described;
                    described.uuid = uuidText(characteristic.Uuid());
                    described.properties = propertiesOf(characteristic.CharacteristicProperties());
                    characteristics.insert_or_assign(keyOf(entry.uuid, described.uuid), characteristic);
                    entry.characteristics.push_back(std::move(described));
                }
            }
            services.push_back(std::move(entry));
        }
        statusToken = device.ConnectionStatusChanged(
            [weak = weak_from_this()](const bluetooth::BluetoothLEDevice& sender, const winrt::Windows::Foundation::IInspectable&) {
                try {
                    if (sender.ConnectionStatus() != bluetooth::BluetoothConnectionStatus::Disconnected) {
                        return;
                    }
                } catch (...) {
                    return;
                }
                if (const std::shared_ptr<Session> alive = weak.lock()) {
                    alive->end(Closed::Removed, "the Bluetooth device went away");
                }
            });
    }

    std::string subscribe(const std::string& service, const std::string& characteristic, bool on)
    {
        const gatt::GattCharacteristic* found = find(service, characteristic);
        if (found == nullptr) {
            return "the device has no characteristic " + characteristic + " in service " + service;
        }
        const std::string key = keyOf(service, characteristic);
        const auto bits = static_cast<std::uint32_t>(found->CharacteristicProperties());
        const bool notifies = (bits & static_cast<std::uint32_t>(gatt::GattCharacteristicProperties::Notify)) != 0;
        const bool indicates = (bits & static_cast<std::uint32_t>(gatt::GattCharacteristicProperties::Indicate)) != 0;
        if (on && !notifies && !indicates) {
            return "the characteristic does not notify";
        }
        const gatt::GattClientCharacteristicConfigurationDescriptorValue value =
            !on        ? gatt::GattClientCharacteristicConfigurationDescriptorValue::None
            : notifies ? gatt::GattClientCharacteristicConfigurationDescriptorValue::Notify
                       : gatt::GattClientCharacteristicConfigurationDescriptorValue::Indicate;
        const gatt::GattCommunicationStatus status = found->WriteClientCharacteristicConfigurationDescriptorAsync(value).get();
        if (status != gatt::GattCommunicationStatus::Success) {
            return failureOf(status);
        }
        const auto existing = subscriptions.find(key);
        if (on && existing == subscriptions.end()) {
            subscriptions.emplace(
                key, found->ValueChanged([weak = weak_from_this(), service, characteristic](
                                             const gatt::GattCharacteristic&, const gatt::GattValueChangedEventArgs& args) {
                    const std::shared_ptr<Session> alive = weak.lock();
                    if (alive == nullptr || alive->ended.load(std::memory_order_acquire)) {
                        return;
                    }
                    try {
                        alive->events->notified(service, characteristic, bytesOf(args.CharacteristicValue()));
                    } catch (...) {
                        return;
                    }
                }));
        } else if (!on && existing != subscriptions.end()) {
            found->ValueChanged(existing->second);
            subscriptions.erase(existing);
        }
        return {};
    }

    std::shared_ptr<Events> events;
    std::atomic<bool> ended{false};
    bluetooth::BluetoothLEDevice device{nullptr};
    gatt::GattSession gattSession{nullptr};
    winrt::event_token statusToken{};
    std::vector<BleService> services;
    std::map<std::string, gatt::GattCharacteristic, std::less<>> characteristics;
    std::map<std::string, winrt::event_token, std::less<>> subscriptions;
};

class BleLink final : public Device
{
public:
    BleLink(std::shared_ptr<Worker> control, std::shared_ptr<Session> session)
        : m_control(std::move(control))
        , m_session(std::move(session))
    {
    }

    ~BleLink() override
    {
        m_session->ended.store(true, std::memory_order_release);
        m_control->post([session = std::move(m_session)]() mutable { session.reset(); });
    }

    BleLink(const BleLink&) = delete;
    BleLink& operator=(const BleLink&) = delete;

    void services(ServicesCompletion done) override
    {
        m_control->post([session = m_session, done = std::move(done)] { done({}, session->services); });
    }

    void read(const std::string& service, const std::string& characteristic, BytesCompletion done) override
    {
        m_control->post([session = m_session, service, characteristic, done = std::move(done)] {
            const gatt::GattCharacteristic* found = session->find(service, characteristic);
            if (found == nullptr) {
                done("the device has no characteristic " + characteristic + " in service " + service, {});
                return;
            }
            try {
                const gatt::GattReadResult read = found->ReadValueAsync(bluetooth::BluetoothCacheMode::Uncached).get();
                if (read.Status() != gatt::GattCommunicationStatus::Success) {
                    done(failureOf(read.Status()), {});
                    return;
                }
                done({}, bytesOf(read.Value()));
            } catch (const winrt::hresult_error& error) {
                done(failureOf(error), {});
            }
        });
    }

    void writeValue(const std::string& service, const std::string& characteristic, Bytes bytes, bool response,
                    Completion done) override
    {
        m_control->post([session = m_session, service, characteristic, bytes = std::move(bytes), response,
                         done = std::move(done)] {
            const gatt::GattCharacteristic* found = session->find(service, characteristic);
            if (found == nullptr) {
                done("the device has no characteristic " + characteristic + " in service " + service);
                return;
            }
            try {
                const IBuffer buffer = CryptographicBuffer::CreateFromByteArray(bytes);
                const gatt::GattCommunicationStatus status =
                    found->WriteValueAsync(buffer, response ? gatt::GattWriteOption::WriteWithResponse
                                                            : gatt::GattWriteOption::WriteWithoutResponse)
                        .get();
                done(failureOf(status));
            } catch (const winrt::hresult_error& error) {
                done(failureOf(error));
            }
        });
    }

    void subscribe(const std::string& service, const std::string& characteristic, bool on, Completion done) override
    {
        m_control->post([session = m_session, service, characteristic, on, done = std::move(done)] {
            try {
                done(session->subscribe(service, characteristic, on));
            } catch (const winrt::hresult_error& error) {
                done(failureOf(error));
            }
        });
    }

private:
    std::shared_ptr<Worker> m_control;
    std::shared_ptr<Session> m_session;
};

}

struct BleScanner::State {
    Seen seen;
    advertising::BluetoothLEAdvertisementWatcher watcher{nullptr};
    winrt::event_token token{};
    bool running = false;
};

BleScanner::BleScanner(Seen seen)
    : m_state(std::make_shared<State>())
{
    m_state->seen = std::move(seen);
}

BleScanner::~BleScanner()
{
    stop();
    try {
        if (m_state->watcher) {
            m_state->watcher.Received(m_state->token);
        }
    } catch (...) {
        m_state->running = false;
    }
}

void BleScanner::start()
{
    if (m_state->running) {
        return;
    }
    try {
        if (!m_state->watcher) {
            m_state->watcher = advertising::BluetoothLEAdvertisementWatcher();
            m_state->watcher.ScanningMode(advertising::BluetoothLEScanningMode::Active);
            m_state->token = m_state->watcher.Received(
                [weak = std::weak_ptr<State>(m_state)](const advertising::BluetoothLEAdvertisementWatcher&,
                                                       const advertising::BluetoothLEAdvertisementReceivedEventArgs& args) {
                    const std::shared_ptr<State> alive = weak.lock();
                    if (alive == nullptr || !alive->seen) {
                        return;
                    }
                    try {
                        DeviceInfo device;
                        device.kind = Kind::Ble;
                        const std::uint64_t address = args.BluetoothAddress();
                        device.id = bleId(address);
                        device.path = addressText(address);
                        const advertising::BluetoothLEAdvertisement advertisement = args.Advertisement();
                        device.name = winrt::to_string(advertisement.LocalName());
                        for (const winrt::guid& uuid : advertisement.ServiceUuids()) {
                            device.services.push_back(uuidText(uuid));
                        }
                        alive->seen(device);
                    } catch (...) {
                        return;
                    }
                });
        }
        m_state->watcher.Start();
        m_state->running = true;
    } catch (...) {
        m_state->running = false;
    }
}

void BleScanner::stop()
{
    if (!m_state->running) {
        return;
    }
    m_state->running = false;
    try {
        m_state->watcher.Stop();
    } catch (...) {
        m_state->running = false;
    }
}

OpenResult openBle(const std::shared_ptr<Worker>& control, const DeviceInfo& device, std::shared_ptr<Events> events)
{
    OpenResult result;
    const std::optional<std::uint64_t> address = bleAddressOf(device.id);
    if (!address) {
        result.missing = true;
        result.problem = "not a Bluetooth device";
        return result;
    }
    auto session = std::make_shared<Session>(std::move(events));
    try {
        session->connect(*address, result);
    } catch (const winrt::hresult_error& error) {
        result.problem = failureOf(error);
    } catch (...) {
        result.problem = "Bluetooth failed";
    }
    if (result.problem.empty()) {
        result.device = std::make_unique<BleLink>(control, std::move(session));
    }
    return result;
}

}
