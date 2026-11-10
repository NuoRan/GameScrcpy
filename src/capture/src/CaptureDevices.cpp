#include "gs/capture/CaptureDevices.h"

#include "MediaFoundation.h"

#include <windows.h>

#include <cfgmgr32.h>
#include <mfapi.h>
#include <mfidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwctype>
#include <format>

namespace gs::capture {

namespace {

using Microsoft::WRL::ComPtr;

constexpr DEVPROPKEY kInstanceId{{0x78c34fc8, 0x104a, 0x4aca, {0x9e, 0xa4, 0x52, 0x4d, 0x52, 0x99, 0x6e, 0x57}}, 256};
constexpr DEVPROPKEY kContainerId{{0x8c7ed206, 0x3f8a, 0x4827, {0xb3, 0xab, 0xae, 0x9e, 0x1f, 0xae, 0xfc, 0x6c}}, 2};
constexpr DEVPROPKEY kPhysicalLocation{{0x540b947e, 0x8b40, 0x45bc, {0xa8, 0xa2, 0x6a, 0x0b, 0x89, 0x4c, 0xbd, 0xa2}},
                                       9};
constexpr GUID kVideoCameraClass{0xe5323777, 0xf976, 0x4f5b, {0x9b, 0x55, 0xb9, 0x46, 0x99, 0xc4, 0x6e, 0x44}};
constexpr GUID kCaptureClass{0x65e8773d, 0x8f56, 0x11d0, {0xa3, 0xb9, 0x00, 0xa0, 0xc9, 0x22, 0x31, 0x96}};
constexpr int kMostAncestors = 4;
constexpr std::uint32_t kFnvBasis = 2166136261U;
constexpr std::uint32_t kFnvPrime = 16777619U;

std::wstring allocatedString(IMFActivate& activate, REFGUID key)
{
    LPWSTR text = nullptr;
    UINT32 length = 0;
    if (FAILED(activate.GetAllocatedString(key, &text, &length)) || text == nullptr) {
        return {};
    }
    std::wstring copy(text, length);
    CoTaskMemFree(text);
    return copy;
}

bool removableNode(DEVINST node)
{
    DEVINST current = node;
    for (int depth = 0; depth < kMostAncestors; ++depth) {
        ULONG capabilities = 0;
        ULONG size = sizeof(capabilities);
        if (CM_Get_DevNode_Registry_PropertyW(current, CM_DRP_CAPABILITIES, nullptr, &capabilities, &size, 0) ==
                CR_SUCCESS &&
            (capabilities & CM_DEVCAP_REMOVABLE) != 0) {
            return true;
        }
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, current, 0) != CR_SUCCESS) {
            break;
        }
        current = parent;
    }
    return false;
}

void describe(CaptureDeviceInfo& device)
{
    std::array<wchar_t, MAX_DEVICE_ID_LEN + 1> instance{};
    ULONG size = static_cast<ULONG>(instance.size() * sizeof(wchar_t));
    DEVPROPTYPE type = 0;
    if (CM_Get_Device_Interface_PropertyW(device.link.c_str(), &kInstanceId, &type,
                                          reinterpret_cast<PBYTE>(instance.data()), &size, 0) != CR_SUCCESS ||
        type != DEVPROP_TYPE_STRING) {
        return;
    }
    DEVINST node = 0;
    if (CM_Locate_DevNodeW(&node, instance.data(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) {
        return;
    }
    GUID container{};
    size = sizeof(container);
    if (CM_Get_DevNode_PropertyW(node, &kContainerId, &type, reinterpret_cast<PBYTE>(&container), &size, 0) ==
            CR_SUCCESS &&
        type == DEVPROP_TYPE_GUID) {
        std::array<wchar_t, 64> text{};
        if (StringFromGUID2(container, text.data(), static_cast<int>(text.size())) > 0) {
            device.container = text.data();
        }
    }
    ULONG location = 0;
    device.panelLocated =
        CM_Get_DevNode_PropertyW(node, &kPhysicalLocation, &type, nullptr, &location, 0) == CR_BUFFER_SMALL &&
        location > 0;
    device.removable = removableNode(node);
}

}

bool builtInCamera(const CaptureDeviceInfo& device) noexcept
{
    return device.panelLocated && !device.removable;
}

std::string shortHash(std::wstring_view text)
{
    std::uint32_t hash = kFnvBasis;
    for (const wchar_t character : text) {
        const auto lower = static_cast<std::uint32_t>(std::towlower(static_cast<wint_t>(character)));
        hash ^= lower & 0xFFU;
        hash *= kFnvPrime;
        hash ^= (lower >> 8U) & 0xFFU;
        hash *= kFnvPrime;
    }
    return std::format("{:08x}", hash);
}

std::vector<CaptureDeviceInfo> listCaptureDevices()
{
    std::vector<CaptureDeviceInfo> devices;
    const ComScope com;
    const MediaFoundation foundation;
    if (!foundation.ready()) {
        return devices;
    }
    ComPtr<IMFAttributes> wanted;
    if (FAILED(MFCreateAttributes(&wanted, 1)) ||
        FAILED(wanted->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))) {
        return devices;
    }
    IMFActivate** found = nullptr;
    UINT32 count = 0;
    if (FAILED(MFEnumDeviceSources(wanted.Get(), &found, &count)) || found == nullptr) {
        return devices;
    }
    for (UINT32 index = 0; index < count; ++index) {
        CaptureDeviceInfo device;
        if (found[index] != nullptr) {
            device.link = allocatedString(*found[index], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
            device.name = utf8Of(allocatedString(*found[index], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME));
            found[index]->Release();
        }
        if (!device.link.empty()) {
            describe(device);
            devices.push_back(std::move(device));
        }
    }
    CoTaskMemFree(found);
    return devices;
}

struct DeviceWatcher::Impl {
    Changed changed;
    std::array<HCMNOTIFICATION, 2> handles{};

    static DWORD CALLBACK onEvent(HCMNOTIFICATION, PVOID context, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA,
                                  DWORD)
    {
        if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL || action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL) {
            const auto* impl = static_cast<const Impl*>(context);
            if (impl->changed) {
                impl->changed();
            }
        }
        return ERROR_SUCCESS;
    }
};

DeviceWatcher::DeviceWatcher(Changed changed) : m_impl(std::make_unique<Impl>())
{
    m_impl->changed = std::move(changed);
    const std::array<GUID, 2> classes{kVideoCameraClass, kCaptureClass};
    for (std::size_t index = 0; index < classes.size(); ++index) {
        CM_NOTIFY_FILTER filter{};
        filter.cbSize = sizeof(filter);
        filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
        filter.u.DeviceInterface.ClassGuid = classes[index];
        if (CM_Register_Notification(&filter, m_impl.get(), &Impl::onEvent, &m_impl->handles[index]) != CR_SUCCESS) {
            m_impl->handles[index] = nullptr;
        }
    }
}

DeviceWatcher::~DeviceWatcher()
{
    for (HCMNOTIFICATION handle : m_impl->handles) {
        if (handle != nullptr) {
            CM_Unregister_Notification(handle);
        }
    }
}

bool DeviceWatcher::started() const noexcept
{
    return std::ranges::any_of(m_impl->handles, [](HCMNOTIFICATION handle) { return handle != nullptr; });
}

}
