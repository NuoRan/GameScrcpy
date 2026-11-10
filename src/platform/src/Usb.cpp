#include "gs/platform/Usb.h"

#include <windows.h>

#include <initguid.h>

#include <cfgmgr32.h>
#include <devpkey.h>
#include <setupapi.h>

#include <libusb.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwchar>
#include <cwctype>
#include <format>
#include <string_view>
#include <utility>

namespace gs::platform {

namespace {

constexpr unsigned kTransferTimeoutMs = 1000;
constexpr std::size_t kDefaultPacketSize = 64;
constexpr std::uint8_t kVendorToDevice = 0x40;
constexpr std::uint8_t kVendorFromDevice = 0xC0;
constexpr std::uint16_t kGoogle = 0x18D1;
constexpr std::uint16_t kFirstAccessory = 0x2D00;
constexpr std::uint16_t kLastAccessory = 0x2D05;

constexpr std::array<std::uint16_t, 30> kAndroidVendors{
    0x18D1, 0x04E8, 0x2717, 0x12D1, 0x339B, 0x22D9, 0x2A70, 0x2D95, 0x2A45, 0x19D2, 0x17EF, 0x22B8, 0x1004, 0x0FCE, 0x0BB4,
    0x0B05, 0x2E04, 0x1EBF, 0x0E8D, 0x05C6, 0x1BBB, 0x2B4C, 0x29A9, 0x109B, 0x1F3A, 0x2207, 0x1782, 0x0489, 0x04DD, 0x2AE5,
};

struct Instance {
    std::uint16_t vendorId = 0;
    std::uint16_t productId = 0;
    std::string serial;
    bool interfaceOf = false;
};

struct Functions {
    bool adb = false;
    bool mtp = false;
    bool winUsb = false;
};

bool equalsIgnoringCase(std::wstring_view a, std::wstring_view b) noexcept
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) {
               return std::towlower(static_cast<wint_t>(x)) == std::towlower(static_cast<wint_t>(y));
           });
}

bool startsWithIgnoringCase(std::wstring_view text, std::wstring_view prefix) noexcept
{
    return text.size() >= prefix.size() && equalsIgnoringCase(text.substr(0, prefix.size()), prefix);
}

bool sameSerial(std::string_view a, std::string_view b) noexcept
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

std::optional<std::uint16_t> hexAfter(std::wstring_view text, std::wstring_view key)
{
    for (std::size_t at = 0; at + key.size() + 4 <= text.size(); ++at) {
        if (!equalsIgnoringCase(text.substr(at, key.size()), key)) {
            continue;
        }
        std::uint16_t value = 0;
        for (const wchar_t digit : text.substr(at + key.size(), 4)) {
            if (!std::iswxdigit(static_cast<wint_t>(digit))) {
                return std::nullopt;
            }
            const auto lower = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(digit)));
            value = static_cast<std::uint16_t>(value * 16 + (lower <= L'9' ? lower - L'0' : lower - L'a' + 10));
        }
        return value;
    }
    return std::nullopt;
}

std::optional<Instance> parseInstance(std::wstring_view id)
{
    if (!startsWithIgnoringCase(id, L"USB\\")) {
        return std::nullopt;
    }
    const std::size_t slash = id.find(L'\\', 4);
    if (slash == std::wstring_view::npos) {
        return std::nullopt;
    }
    const std::wstring_view hardware = id.substr(4, slash - 4);
    const std::wstring_view rest = id.substr(slash + 1);
    const std::optional<std::uint16_t> vendor = hexAfter(hardware, L"VID_");
    const std::optional<std::uint16_t> product = hexAfter(hardware, L"PID_");
    if (!vendor || !product) {
        return std::nullopt;
    }
    Instance instance{.vendorId = *vendor, .productId = *product};
    for (std::size_t at = 0; at + 4 <= hardware.size(); ++at) {
        if (equalsIgnoringCase(hardware.substr(at, 4), L"&MI_")) {
            instance.interfaceOf = true;
        }
    }
    const bool generated = rest.find(L'&') != std::wstring_view::npos;
    if (!generated && !rest.empty() && std::ranges::all_of(rest, [](wchar_t c) { return c > 0x20 && c < 0x7F; })) {
        instance.serial.reserve(rest.size());
        for (const wchar_t c : rest) {
            instance.serial.push_back(static_cast<char>(c));
        }
    }
    return instance;
}

std::string narrow(std::wstring_view text)
{
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr,
                                         nullptr);
    std::string out(static_cast<std::size_t>(std::max(size, 0)), '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr,
                            nullptr);
    }
    return out;
}

std::vector<std::wstring> nodeTexts(DEVINST node, ULONG property)
{
    ULONG bytes = 0;
    ULONG type = 0;
    if (CM_Get_DevNode_Registry_PropertyW(node, property, &type, nullptr, &bytes, 0) != CR_BUFFER_SMALL || bytes == 0) {
        return {};
    }
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 2, L'\0');
    if (CM_Get_DevNode_Registry_PropertyW(node, property, &type, buffer.data(), &bytes, 0) != CR_SUCCESS) {
        return {};
    }
    std::vector<std::wstring> texts;
    for (const wchar_t* at = buffer.data(); *at != L'\0'; at += std::wcslen(at) + 1) {
        texts.emplace_back(at);
        if (type != REG_MULTI_SZ) {
            break;
        }
    }
    return texts;
}

void describe(DEVINST node, Functions& functions, bool child)
{
    bool adb = false;
    for (const std::wstring& compatible : nodeTexts(node, CM_DRP_COMPATIBLEIDS)) {
        if (startsWithIgnoringCase(compatible, L"USB\\Class_ff&SubClass_42&Prot_01")) {
            adb = true;
        } else if (startsWithIgnoringCase(compatible, L"USB\\Class_06&SubClass_01&Prot_01") ||
                   startsWithIgnoringCase(compatible, L"USB\\MS_COMP_MTP")) {
            functions.mtp = true;
        }
    }
    functions.adb = functions.adb || adb;
    const std::vector<std::wstring> service = nodeTexts(node, CM_DRP_SERVICE);
    if (!service.empty() && equalsIgnoringCase(service.front(), L"WinUSB") && !(child && adb)) {
        functions.winUsb = true;
    }
}

Functions functionsOf(DEVINST device)
{
    Functions functions;
    describe(device, functions, false);
    DEVINST child = 0;
    for (CONFIGRET found = CM_Get_Child(&child, device, 0); found == CR_SUCCESS;
         found = CM_Get_Sibling(&child, child, 0)) {
        describe(child, functions, true);
    }
    return functions;
}

std::wstring deviceText(HDEVINFO set, SP_DEVINFO_DATA& info, const DEVPROPKEY& key)
{
    DEVPROPTYPE type = 0;
    DWORD bytes = 0;
    SetupDiGetDevicePropertyW(set, &info, &key, &type, nullptr, 0, &bytes, 0);
    if (bytes == 0 || type != DEVPROP_TYPE_STRING) {
        return {};
    }
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    if (!SetupDiGetDevicePropertyW(set, &info, &key, &type, reinterpret_cast<PBYTE>(buffer.data()), bytes, nullptr,
                                   0)) {
        return {};
    }
    return buffer.data();
}

bool knownAndroidVendor(std::uint16_t vendor) noexcept
{
    return std::ranges::find(kAndroidVendors, vendor) != kAndroidVendors.end();
}

libusb_context* usbContext()
{
    static libusb_context* const context = [] {
        libusb_context* made = nullptr;
        return libusb_init_context(&made, nullptr, 0) == LIBUSB_SUCCESS ? made : nullptr;
    }();
    return context;
}

UsbFailure failureOf(int error) noexcept
{
    switch (error) {
    case LIBUSB_ERROR_NOT_SUPPORTED:
        return UsbFailure::NoDriver;
    case LIBUSB_ERROR_ACCESS:
    case LIBUSB_ERROR_BUSY:
        return UsbFailure::Busy;
    case LIBUSB_ERROR_NO_DEVICE:
    case LIBUSB_ERROR_NOT_FOUND:
        return UsbFailure::NotFound;
    default:
        return UsbFailure::Refused;
    }
}

}

std::vector<UsbPhone> listUsbPhones()
{
    std::vector<UsbPhone> phones;
    const HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE) {
        return phones;
    }
    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    for (DWORD at = 0; SetupDiEnumDeviceInfo(set, at, &info); ++at) {
        std::array<wchar_t, MAX_DEVICE_ID_LEN + 1> id{};
        if (!SetupDiGetDeviceInstanceIdW(set, &info, id.data(), static_cast<DWORD>(id.size()), nullptr)) {
            continue;
        }
        const std::optional<Instance> instance = parseInstance(id.data());
        if (!instance || instance->interfaceOf || instance->serial.empty()) {
            continue;
        }
        const Functions functions = functionsOf(info.DevInst);
        const bool accessory = instance->vendorId == kGoogle && instance->productId >= kFirstAccessory &&
                               instance->productId <= kLastAccessory;
        if (!functions.adb && !functions.mtp && !accessory &&
            !(functions.winUsb && knownAndroidVendor(instance->vendorId))) {
            continue;
        }
        std::wstring name = deviceText(set, info, DEVPKEY_Device_BusReportedDeviceDesc);
        if (name.empty()) {
            name = deviceText(set, info, DEVPKEY_Device_FriendlyName);
        }
        if (name.empty()) {
            name = deviceText(set, info, DEVPKEY_Device_DeviceDesc);
        }
        phones.push_back({.serial = instance->serial,
                          .name = narrow(name),
                          .vendorId = instance->vendorId,
                          .productId = instance->productId,
                          .driver = functions.winUsb ? UsbDriver::Ready : UsbDriver::Missing,
                          .debugging = functions.adb});
    }
    SetupDiDestroyDeviceInfoList(set);
    std::ranges::sort(phones, {}, &UsbPhone::serial);
    return phones;
}

std::string usbLibraryVersion()
{
    const libusb_version* version = libusb_get_version();
    return std::format("{}.{}.{}", version->major, version->minor, version->micro);
}

struct UsbDevice::Impl {
    libusb_device_handle* handle = nullptr;
    std::size_t packetSize = kDefaultPacketSize;
    UsbFailure failure = UsbFailure::None;

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    ~Impl()
    {
        if (handle != nullptr) {
            libusb_close(handle);
        }
    }
};

UsbDevice::UsbDevice(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

UsbDevice::~UsbDevice() = default;

std::unique_ptr<UsbDevice> UsbDevice::open(const UsbPhone& phone, UsbFailure& failure)
{
    failure = UsbFailure::NotFound;
    libusb_context* context = usbContext();
    if (context == nullptr) {
        failure = UsbFailure::Refused;
        return nullptr;
    }
    libusb_device** devices = nullptr;
    const ssize_t count = libusb_get_device_list(context, &devices);
    std::unique_ptr<UsbDevice> opened;
    for (ssize_t at = 0; at < count && opened == nullptr; ++at) {
        libusb_device_descriptor descriptor{};
        if (libusb_get_device_descriptor(devices[at], &descriptor) != LIBUSB_SUCCESS ||
            descriptor.idVendor != phone.vendorId || descriptor.idProduct != phone.productId ||
            descriptor.iSerialNumber == 0) {
            continue;
        }
        libusb_device_handle* handle = nullptr;
        if (const int result = libusb_open(devices[at], &handle); result != LIBUSB_SUCCESS) {
            const UsbFailure reason = failureOf(result);
            if (reason != UsbFailure::NotFound) {
                failure = reason;
            }
            continue;
        }
        std::array<unsigned char, 256> text{};
        const int length =
            libusb_get_string_descriptor_ascii(handle, descriptor.iSerialNumber, text.data(), static_cast<int>(text.size()));
        if (length <= 0 ||
            !sameSerial(std::string_view(reinterpret_cast<const char*>(text.data()), static_cast<std::size_t>(length)),
                        phone.serial)) {
            libusb_close(handle);
            continue;
        }
        auto impl = std::make_unique<Impl>();
        impl->handle = handle;
        impl->packetSize = descriptor.bMaxPacketSize0 > 0 ? descriptor.bMaxPacketSize0 : kDefaultPacketSize;
        opened.reset(new UsbDevice(std::move(impl)));
        failure = UsbFailure::None;
    }
    if (count >= 0) {
        libusb_free_device_list(devices, 1);
    }
    return opened;
}

bool UsbDevice::controlOut(std::uint8_t request, std::uint16_t value, std::uint16_t index,
                           std::span<const std::uint8_t> data)
{
    if (m_impl->failure == UsbFailure::Gone || data.size() > 0xFFFF) {
        return false;
    }
    const int sent = libusb_control_transfer(m_impl->handle, kVendorToDevice, request, value, index,
                                             const_cast<unsigned char*>(data.data()),
                                             static_cast<std::uint16_t>(data.size()), kTransferTimeoutMs);
    if (sent == LIBUSB_ERROR_NO_DEVICE) {
        m_impl->failure = UsbFailure::Gone;
        return false;
    }
    if (sent < 0 || static_cast<std::size_t>(sent) != data.size()) {
        m_impl->failure = UsbFailure::Refused;
        return false;
    }
    m_impl->failure = UsbFailure::None;
    return true;
}

std::optional<std::vector<std::uint8_t>> UsbDevice::controlIn(std::uint8_t request, std::uint16_t value,
                                                              std::uint16_t index, std::size_t length)
{
    if (m_impl->failure == UsbFailure::Gone || length > 0xFFFF) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> data(length, 0);
    const int received = libusb_control_transfer(m_impl->handle, kVendorFromDevice, request, value, index,
                                                 data.data(), static_cast<std::uint16_t>(data.size()),
                                                 kTransferTimeoutMs);
    if (received == LIBUSB_ERROR_NO_DEVICE) {
        m_impl->failure = UsbFailure::Gone;
        return std::nullopt;
    }
    if (received < 0) {
        m_impl->failure = UsbFailure::Refused;
        return std::nullopt;
    }
    m_impl->failure = UsbFailure::None;
    data.resize(static_cast<std::size_t>(received));
    return data;
}

std::size_t UsbDevice::maxPacketSize() const noexcept
{
    return m_impl->packetSize;
}

UsbFailure UsbDevice::failure() const noexcept
{
    return m_impl->failure;
}

struct UsbWatcher::Impl {
    Changed changed;
    HCMNOTIFICATION handle = nullptr;

    static DWORD CALLBACK onEvent(HCMNOTIFICATION, PVOID context, CM_NOTIFY_ACTION action,
                                  PCM_NOTIFY_EVENT_DATA data, DWORD)
    {
        if ((action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL || action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL) &&
            data != nullptr && data->FilterType == CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE &&
            startsWithIgnoringCase(data->u.DeviceInterface.SymbolicLink, L"\\\\?\\USB#")) {
            const auto* impl = static_cast<const Impl*>(context);
            if (impl->changed) {
                impl->changed();
            }
        }
        return ERROR_SUCCESS;
    }
};

UsbWatcher::UsbWatcher(Changed changed)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->changed = std::move(changed);
    CM_NOTIFY_FILTER filter{};
    filter.cbSize = sizeof(filter);
    filter.Flags = CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES;
    filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    if (CM_Register_Notification(&filter, m_impl.get(), &Impl::onEvent, &m_impl->handle) != CR_SUCCESS) {
        m_impl->handle = nullptr;
    }
}

UsbWatcher::~UsbWatcher()
{
    if (m_impl->handle != nullptr) {
        CM_Unregister_Notification(m_impl->handle);
    }
}

bool UsbWatcher::started() const noexcept
{
    return m_impl->handle != nullptr;
}

}
