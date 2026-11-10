#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gs::capture {

struct CaptureDeviceInfo {
    std::wstring link;
    std::string name;
    std::wstring container;
    bool panelLocated = false;
    bool removable = true;

    friend bool operator==(const CaptureDeviceInfo&, const CaptureDeviceInfo&) = default;
};

[[nodiscard]] bool builtInCamera(const CaptureDeviceInfo& device) noexcept;
[[nodiscard]] std::string shortHash(std::wstring_view text);
[[nodiscard]] std::vector<CaptureDeviceInfo> listCaptureDevices();

class DeviceWatcher
{
public:
    using Changed = std::function<void()>;

    explicit DeviceWatcher(Changed changed);
    ~DeviceWatcher();
    DeviceWatcher(const DeviceWatcher&) = delete;
    DeviceWatcher& operator=(const DeviceWatcher&) = delete;

    [[nodiscard]] bool started() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
