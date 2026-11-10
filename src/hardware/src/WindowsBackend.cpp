#include "Links.h"

#include <windows.h>

#include <cfgmgr32.h>
#include <objbase.h>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <map>
#include <mutex>
#include <utility>

namespace gs::hardware {

namespace {

constexpr GUID kHidInterface{0x4D1E55B2, 0xF16F, 0x11CF, {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};

CM_NOTIFY_FILTER interfaceFilter(const GUID& interfaceClass)
{
    CM_NOTIFY_FILTER filter{};
    filter.cbSize = sizeof(filter);
    filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    filter.u.DeviceInterface.ClassGuid = interfaceClass;
    return filter;
}

CM_NOTIFY_FILTER instanceFilter()
{
    CM_NOTIFY_FILTER filter{};
    filter.cbSize = sizeof(filter);
    filter.Flags = CM_NOTIFY_FILTER_FLAG_ALL_DEVICE_INSTANCES;
    filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE;
    return filter;
}

struct Shared {
    mutable std::mutex mutex;
    std::vector<DeviceInfo> wired;
    std::map<std::string, DeviceInfo, std::less<>> seen;
    std::function<void()> changed;

    void notify()
    {
        std::function<void()> callback;
        {
            const std::scoped_lock lock(mutex);
            callback = changed;
        }
        if (callback) {
            callback();
        }
    }

    void sawBle(const DeviceInfo& device)
    {
        bool fresh = false;
        {
            const std::scoped_lock lock(mutex);
            const auto found = seen.find(device.id);
            if (found == seen.end()) {
                seen.emplace(device.id, device);
                fresh = true;
            } else {
                DeviceInfo merged = found->second;
                if (!device.name.empty()) {
                    merged.name = device.name;
                }
                for (const std::string& service : device.services) {
                    if (std::ranges::find(merged.services, service) == merged.services.end()) {
                        merged.services.push_back(service);
                    }
                }
                if (merged != found->second) {
                    found->second = std::move(merged);
                    fresh = true;
                }
            }
        }
        if (fresh) {
            notify();
        }
    }
};

class WindowsBackend final : public Backend
{
public:
    WindowsBackend()
        : m_shared(std::make_shared<Shared>())
        , m_control(std::make_shared<detail::Worker>(
              [] { static_cast<void>(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); },
              [this] {
                  m_scanner.reset();
                  CoUninitialize();
              }))
    {
        m_instances = listen(instanceFilter());
        m_hid = listen(interfaceFilter(kHidInterface));
        requestRefresh();
    }

    ~WindowsBackend() override
    {
        for (const HCMNOTIFICATION notification : {m_instances, m_hid}) {
            if (notification != nullptr) {
                CM_Unregister_Notification(notification);
            }
        }
        m_control->stop();
    }

    WindowsBackend(const WindowsBackend&) = delete;
    WindowsBackend& operator=(const WindowsBackend&) = delete;

    [[nodiscard]] std::vector<DeviceInfo> devices() const override
    {
        const std::scoped_lock lock(m_shared->mutex);
        std::vector<DeviceInfo> all = m_shared->wired;
        for (const auto& [id, device] : m_shared->seen) {
            all.push_back(device);
        }
        return all;
    }

    void watch(std::function<void()> changed) override
    {
        {
            const std::scoped_lock lock(m_shared->mutex);
            m_shared->changed = std::move(changed);
        }
        requestRefresh();
    }

    void scan(bool on) override
    {
        m_control->post([this, on] {
            if (!m_scanner) {
                if (!on) {
                    return;
                }
                m_scanner = std::make_unique<detail::BleScanner>(
                    [shared = m_shared](const DeviceInfo& device) { shared->sawBle(device); });
            }
            if (on) {
                m_scanner->start();
            } else {
                m_scanner->stop();
            }
        });
    }

    void open(OpenRequest request, Opened done) override
    {
        m_control->post([control = m_control, request = std::move(request), done = std::move(done)] {
            switch (request.device.kind) {
            case Kind::Serial:
                done(detail::openSerial(request.device, request.serial, request.events));
                return;
            case Kind::Hid:
                done(detail::openHid(request.device, request.events));
                return;
            case Kind::Ble:
                done(detail::openBle(control, request.device, request.events));
                return;
            }
        });
    }

private:
    static DWORD CALLBACK noticed(HCMNOTIFICATION, PVOID context, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA,
                                 DWORD)
    {
        if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL || action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL ||
            action == CM_NOTIFY_ACTION_DEVICEINSTANCESTARTED || action == CM_NOTIFY_ACTION_DEVICEINSTANCEREMOVED) {
            static_cast<WindowsBackend*>(context)->requestRefresh();
        }
        return ERROR_SUCCESS;
    }

    HCMNOTIFICATION listen(CM_NOTIFY_FILTER filter)
    {
        HCMNOTIFICATION notification = nullptr;
        if (CM_Register_Notification(&filter, this, &WindowsBackend::noticed, &notification) != CR_SUCCESS) {
            return nullptr;
        }
        return notification;
    }

    void requestRefresh()
    {
        if (!m_refreshWanted.exchange(true, std::memory_order_acq_rel)) {
            m_control->post([this] { refresh(); });
        }
    }

    void refresh()
    {
        m_refreshWanted.store(false, std::memory_order_release);
        std::vector<DeviceInfo> wired = detail::listSerialPorts();
        std::vector<DeviceInfo> hid = detail::listHidDevices();
        wired.insert(wired.end(), std::make_move_iterator(hid.begin()), std::make_move_iterator(hid.end()));
        bool fresh = false;
        {
            const std::scoped_lock lock(m_shared->mutex);
            if (wired != m_shared->wired) {
                m_shared->wired = std::move(wired);
                fresh = true;
            }
        }
        if (fresh) {
            m_shared->notify();
        }
    }

    std::shared_ptr<Shared> m_shared;
    std::unique_ptr<detail::BleScanner> m_scanner;
    std::shared_ptr<detail::Worker> m_control;
    std::atomic<bool> m_refreshWanted{false};
    HCMNOTIFICATION m_instances = nullptr;
    HCMNOTIFICATION m_hid = nullptr;
};

}

std::unique_ptr<Backend> makeWindowsBackend()
{
    return std::make_unique<WindowsBackend>();
}

}
