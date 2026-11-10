#include "Links.h"
#include "Worker.h"

#include <windows.h>

#include <hidsdi.h>
#include <setupapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <thread>
#include <utility>

namespace gs::hardware::detail {

namespace {

constexpr GUID kHidInterface{0x4D1E55B2, 0xF16F, 0x11CF, {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
constexpr USHORT kGenericDesktop = 0x01;
constexpr USHORT kMouse = 0x02;
constexpr USHORT kKeyboard = 0x06;
constexpr USHORT kKeypad = 0x07;
constexpr std::string_view kInterfacePrefix = "\\\\?\\";

struct Caps {
    USHORT usagePage = 0;
    USHORT usage = 0;
    USHORT input = 0;
    USHORT output = 0;
    USHORT feature = 0;
};

Caps capsOf(HANDLE handle)
{
    Caps caps;
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (!HidD_GetPreparsedData(handle, &preparsed)) {
        return caps;
    }
    HIDP_CAPS found{};
    if (HidP_GetCaps(preparsed, &found) == HIDP_STATUS_SUCCESS) {
        caps.usagePage = found.UsagePage;
        caps.usage = found.Usage;
        caps.input = found.InputReportByteLength;
        caps.output = found.OutputReportByteLength;
        caps.feature = found.FeatureReportByteLength;
    }
    HidD_FreePreparsedData(preparsed);
    return caps;
}

bool privateInput(const Caps& caps) noexcept
{
    return caps.usagePage == kGenericDesktop &&
           (caps.usage == kMouse || caps.usage == kKeyboard || caps.usage == kKeypad);
}

std::string interfaceId(std::string_view path)
{
    std::string id(path.starts_with(kInterfacePrefix) ? path.substr(kInterfacePrefix.size()) : path);
    std::ranges::transform(id, id.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return "hid:" + id;
}

std::string productName(HANDLE handle)
{
    std::array<wchar_t, 128> buffer{};
    if (!HidD_GetProductString(handle, buffer.data(), static_cast<ULONG>((buffer.size() - 1) * sizeof(wchar_t)))) {
        return {};
    }
    return narrow(buffer.data());
}

class HidLink final : public Device
{
public:
    HidLink(HANDLE handle, const Caps& caps, std::shared_ptr<Events> events)
        : m_handle(handle)
        , m_caps(caps)
        , m_events(std::move(events))
        , m_stop(CreateEventW(nullptr, TRUE, FALSE, nullptr))
        , m_readEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr))
        , m_writeEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr))
    {
        if (ready() && m_caps.input > 0) {
            m_reader = std::thread([this] { readLoop(); });
        }
    }

    ~HidLink() override
    {
        m_stopping.store(true, std::memory_order_release);
        if (m_stop != nullptr) {
            SetEvent(m_stop);
        }
        CancelIoEx(m_handle, nullptr);
        if (m_reader.joinable()) {
            m_reader.join();
        }
        m_worker.stop();
        CloseHandle(m_handle);
        for (const HANDLE event : {m_stop, m_readEvent, m_writeEvent}) {
            if (event != nullptr) {
                CloseHandle(event);
            }
        }
    }

    HidLink(const HidLink&) = delete;
    HidLink& operator=(const HidLink&) = delete;

    [[nodiscard]] bool ready() const noexcept
    {
        return m_stop != nullptr && m_readEvent != nullptr && m_writeEvent != nullptr;
    }

    void sendReport(std::uint8_t id, Bytes bytes, Completion done) override
    {
        m_worker.post([this, id, bytes = std::move(bytes), done = std::move(done)] { done(sendNow(id, bytes)); });
    }

    void sendFeature(std::uint8_t id, Bytes bytes, Completion done) override
    {
        m_worker.post([this, id, bytes = std::move(bytes), done = std::move(done)] {
            if (m_caps.feature == 0) {
                done("the device has no feature reports");
                return;
            }
            if (bytes.size() + 1 > static_cast<std::size_t>(m_caps.feature)) {
                done("the feature report is longer than the " + std::to_string(m_caps.feature - 1) +
                     " bytes the device takes");
                return;
            }
            std::vector<std::uint8_t> buffer(m_caps.feature, 0);
            buffer[0] = id;
            std::ranges::copy(bytes, buffer.begin() + 1);
            if (!HidD_SetFeature(m_handle, buffer.data(), static_cast<ULONG>(buffer.size()))) {
                done(systemMessage(GetLastError()));
                return;
            }
            done({});
        });
    }

    void getFeature(std::uint8_t id, BytesCompletion done) override
    {
        m_worker.post([this, id, done = std::move(done)] {
            if (m_caps.feature == 0) {
                done("the device has no feature reports", {});
                return;
            }
            std::vector<std::uint8_t> buffer(m_caps.feature, 0);
            buffer[0] = id;
            if (!HidD_GetFeature(m_handle, buffer.data(), static_cast<ULONG>(buffer.size()))) {
                done(systemMessage(GetLastError()), {});
                return;
            }
            done({}, Bytes(buffer.begin() + 1, buffer.end()));
        });
    }

private:
    std::string sendNow(std::uint8_t id, const Bytes& bytes)
    {
        if (m_caps.output == 0) {
            return "the device takes no output reports";
        }
        if (bytes.size() + 1 > static_cast<std::size_t>(m_caps.output)) {
            return "the report is longer than the " + std::to_string(m_caps.output - 1) + " bytes the device takes";
        }
        std::vector<std::uint8_t> buffer(m_caps.output, 0);
        buffer[0] = id;
        std::ranges::copy(bytes, buffer.begin() + 1);
        OVERLAPPED overlapped{};
        overlapped.hEvent = m_writeEvent;
        ResetEvent(m_writeEvent);
        DWORD written = 0;
        if (!WriteFile(m_handle, buffer.data(), static_cast<DWORD>(buffer.size()), &written, &overlapped)) {
            const DWORD error = GetLastError();
            if (error != ERROR_IO_PENDING) {
                return systemMessage(error);
            }
            const std::array<HANDLE, 2> waits{m_writeEvent, m_stop};
            const DWORD which =
                WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, INFINITE);
            if (which != WAIT_OBJECT_0) {
                CancelIoEx(m_handle, &overlapped);
                GetOverlappedResult(m_handle, &overlapped, &written, TRUE);
                return "the device was closed";
            }
            if (!GetOverlappedResult(m_handle, &overlapped, &written, FALSE)) {
                return systemMessage(GetLastError());
            }
        }
        return {};
    }

    void readLoop()
    {
        std::vector<std::uint8_t> buffer(m_caps.input, 0);
        for (;;) {
            OVERLAPPED overlapped{};
            overlapped.hEvent = m_readEvent;
            ResetEvent(m_readEvent);
            DWORD received = 0;
            if (!ReadFile(m_handle, buffer.data(), static_cast<DWORD>(buffer.size()), &received, &overlapped)) {
                const DWORD error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    lost(error);
                    return;
                }
                const std::array<HANDLE, 2> waits{m_readEvent, m_stop};
                const DWORD which =
                    WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, INFINITE);
                if (which != WAIT_OBJECT_0) {
                    CancelIoEx(m_handle, &overlapped);
                    GetOverlappedResult(m_handle, &overlapped, &received, TRUE);
                    return;
                }
                if (!GetOverlappedResult(m_handle, &overlapped, &received, FALSE)) {
                    lost(GetLastError());
                    return;
                }
            }
            if (m_stopping.load(std::memory_order_acquire)) {
                return;
            }
            if (received > 0) {
                const auto end = buffer.begin() + static_cast<std::ptrdiff_t>(received);
                m_events->report(buffer[0], Bytes(buffer.begin() + 1, end));
            }
        }
    }

    void lost(DWORD error)
    {
        if (m_stopping.load(std::memory_order_acquire)) {
            return;
        }
        m_events->closed(removalError(error) ? Closed::Removed : Closed::Failed, systemMessage(error));
    }

    HANDLE m_handle;
    Caps m_caps;
    std::shared_ptr<Events> m_events;
    HANDLE m_stop;
    HANDLE m_readEvent;
    HANDLE m_writeEvent;
    std::atomic<bool> m_stopping{false};
    Worker m_worker;
    std::thread m_reader;
};

}

std::vector<DeviceInfo> listHidDevices()
{
    std::vector<DeviceInfo> devices;
    const HDEVINFO set = SetupDiGetClassDevsW(&kHidInterface, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        return devices;
    }
    SP_DEVICE_INTERFACE_DATA entry{};
    entry.cbSize = sizeof(entry);
    for (DWORD at = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &kHidInterface, at, &entry); ++at) {
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &entry, nullptr, 0, &required, nullptr);
        if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            continue;
        }
        std::vector<DWORD> storage(required / sizeof(DWORD) + 2, 0);
        auto* described = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        described->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &entry, described, required, nullptr, nullptr)) {
            continue;
        }
        const std::wstring path = described->DevicePath;
        const HANDLE handle =
            CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            continue;
        }
        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        const bool known = HidD_GetAttributes(handle, &attributes) != FALSE;
        const Caps caps = capsOf(handle);
        std::string name = productName(handle);
        CloseHandle(handle);
        if (!known || privateInput(caps)) {
            continue;
        }
        DeviceInfo device;
        device.kind = Kind::Hid;
        device.path = narrow(path);
        device.id = interfaceId(device.path);
        device.vendorId = attributes.VendorID;
        device.productId = attributes.ProductID;
        device.usagePage = caps.usagePage;
        device.usage = caps.usage;
        device.name = name.empty() ? "HID " + hex4(device.vendorId) + ":" + hex4(device.productId) : std::move(name);
        devices.push_back(std::move(device));
    }
    SetupDiDestroyDeviceInfoList(set);
    std::ranges::sort(devices, {}, &DeviceInfo::id);
    return devices;
}

OpenResult openHid(const DeviceInfo& device, std::shared_ptr<Events> events)
{
    OpenResult result;
    const std::wstring path = widen(device.path);
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED) {
        handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                             FILE_FLAG_OVERLAPPED, nullptr);
    }
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        result.missing = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
        result.problem = systemMessage(error);
        return result;
    }
    const Caps caps = capsOf(handle);
    if (privateInput(caps)) {
        CloseHandle(handle);
        result.problem = "keyboards and mice are not offered";
        return result;
    }
    auto link = std::make_unique<HidLink>(handle, caps, std::move(events));
    if (!link->ready()) {
        result.problem = "the device could not be set up";
        return result;
    }
    result.device = std::move(link);
    return result;
}

}
