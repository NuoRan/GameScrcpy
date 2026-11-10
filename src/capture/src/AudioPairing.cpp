#include "gs/capture/AudioPairing.h"

#include "MediaFoundation.h"

#include <windows.h>

#include <objbase.h>
#include <mmdeviceapi.h>
#include <propidl.h>
#include <propsys.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cwctype>

namespace gs::capture {

namespace {

using Microsoft::WRL::ComPtr;

constexpr PROPERTYKEY kContainerId{{0x8c7ed206, 0x3f8a, 0x4827, {0xb3, 0xab, 0xae, 0x9e, 0x1f, 0xae, 0xfc, 0x6c}}, 2};
constexpr PROPERTYKEY kFriendlyName{{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

std::wstring_view bare(std::wstring_view text) noexcept
{
    if (text.size() >= 2 && text.front() == L'{' && text.back() == L'}') {
        text.remove_prefix(1);
        text.remove_suffix(1);
    }
    return text;
}

bool shared(std::wstring_view container) noexcept
{
    constexpr std::array<std::wstring_view, 2> kShared{L"00000000-0000-0000-0000-000000000000",
                                                       L"00000000-0000-0000-ffff-ffffffffffff"};
    return std::ranges::any_of(kShared, [container](std::wstring_view known) { return sameContainer(known, container); });
}

}

bool sameContainer(std::wstring_view a, std::wstring_view b) noexcept
{
    const std::wstring_view left = bare(a);
    const std::wstring_view right = bare(b);
    return !left.empty() && left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [](wchar_t x, wchar_t y) {
               return std::towlower(static_cast<wint_t>(x)) == std::towlower(static_cast<wint_t>(y));
           });
}

std::optional<std::size_t> pairedEndpoint(std::wstring_view videoContainer, std::span<const AudioEndpoint> endpoints) noexcept
{
    if (bare(videoContainer).empty() || shared(videoContainer)) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < endpoints.size(); ++index) {
        if (sameContainer(endpoints[index].container, videoContainer)) {
            return index;
        }
    }
    return std::nullopt;
}

std::vector<AudioEndpoint> listCaptureEndpoints()
{
    std::vector<AudioEndpoint> endpoints;
    const ComScope com;
    ComPtr<IMMDeviceEnumerator> devices;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices)))) {
        return endpoints;
    }
    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(devices->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection))) {
        return endpoints;
    }
    UINT count = 0;
    if (FAILED(collection->GetCount(&count))) {
        return endpoints;
    }
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> device;
        LPWSTR id = nullptr;
        if (FAILED(collection->Item(index, &device)) || FAILED(device->GetId(&id))) {
            continue;
        }
        AudioEndpoint endpoint;
        endpoint.id = id;
        CoTaskMemFree(id);
        ComPtr<IPropertyStore> properties;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(properties->GetValue(kContainerId, &value)) && value.vt == VT_CLSID && value.puuid != nullptr) {
                std::array<wchar_t, 64> text{};
                if (StringFromGUID2(*value.puuid, text.data(), static_cast<int>(text.size())) > 0) {
                    endpoint.container = text.data();
                }
            }
            PropVariantClear(&value);
            PropVariantInit(&value);
            if (SUCCEEDED(properties->GetValue(kFriendlyName, &value)) && value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
                endpoint.name = utf8Of(value.pwszVal);
            }
            PropVariantClear(&value);
        }
        endpoints.push_back(std::move(endpoint));
    }
    return endpoints;
}

}
