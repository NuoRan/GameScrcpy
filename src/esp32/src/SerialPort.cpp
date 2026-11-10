#include "gs/esp32/Port.h"

#include <gs/base/Log.h>

#include <windows.h>

#include <initguid.h>

#include <cfgmgr32.h>
#include <devguid.h>
#include <setupapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <limits>
#include <mutex>

namespace gs::esp32 {

namespace {

constexpr std::string_view kCategory = "esp32";
constexpr DWORD kQueueSize = 16 * 1024;
constexpr DWORD kWriteTimeoutMs = 2000;
constexpr DWORD kDriverReadTimeoutMs = 1000;
constexpr std::size_t kMostTransfer = 64 * 1024;

std::string narrow(std::wstring_view text)
{
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr,
                                         nullptr);
    std::string out(static_cast<std::size_t>(std::max(size, 0)), '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    }
    return out;
}

char upper(char c) noexcept
{
    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

int hexDigit(char c) noexcept
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    const char lowered = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lowered >= 'a' && lowered <= 'f') {
        return lowered - 'a' + 10;
    }
    return -1;
}

std::optional<std::uint16_t> hexAfter(std::string_view text, std::string_view key) noexcept
{
    for (std::size_t at = 0; at + key.size() + 4 <= text.size(); ++at) {
        bool same = true;
        for (std::size_t i = 0; i < key.size(); ++i) {
            if (upper(text[at + i]) != key[i]) {
                same = false;
                break;
            }
        }
        if (!same) {
            continue;
        }
        int value = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            const int digit = hexDigit(text[at + key.size() + i]);
            if (digit < 0) {
                return std::nullopt;
            }
            value = value * 16 + digit;
        }
        return static_cast<std::uint16_t>(value);
    }
    return std::nullopt;
}

DWORD clampedMilliseconds(std::chrono::milliseconds timeout) noexcept
{
    const auto count = std::clamp<std::chrono::milliseconds::rep>(timeout.count(), 0, MAXDWORD - 1);
    return static_cast<DWORD>(count);
}

std::wstring registryText(HDEVINFO set, SP_DEVINFO_DATA& info, const wchar_t* name)
{
    const HKEY key = SetupDiOpenDevRegKey(set, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
    if (key == nullptr || key == INVALID_HANDLE_VALUE) {
        return {};
    }
    std::array<wchar_t, 256> buffer{};
    DWORD type = 0;
    DWORD bytes = static_cast<DWORD>((buffer.size() - 1) * sizeof(wchar_t));
    const LSTATUS status = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) {
        return {};
    }
    return buffer.data();
}

std::wstring propertyText(HDEVINFO set, SP_DEVINFO_DATA& info, DWORD property)
{
    DWORD type = 0;
    DWORD required = 0;
    SetupDiGetDeviceRegistryPropertyW(set, &info, property, &type, nullptr, 0, &required);
    if (required == 0) {
        return {};
    }
    std::vector<wchar_t> buffer(required / sizeof(wchar_t) + 2, L'\0');
    if (!SetupDiGetDeviceRegistryPropertyW(set, &info, property, &type, reinterpret_cast<PBYTE>(buffer.data()),
                                           static_cast<DWORD>((buffer.size() - 1) * sizeof(wchar_t)), nullptr)) {
        return {};
    }
    return buffer.data();
}

class SerialPort final : public Port
{
public:
    explicit SerialPort(HANDLE handle) noexcept
        : m_handle(handle)
        , m_readEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr))
        , m_writeEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr))
        , m_cancelEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr))
    {
    }

    ~SerialPort() override
    {
        CancelIoEx(m_handle, nullptr);
        CloseHandle(m_handle);
        for (const HANDLE event : {m_readEvent, m_writeEvent, m_cancelEvent}) {
            if (event != nullptr) {
                CloseHandle(event);
            }
        }
    }

    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    [[nodiscard]] bool ready() const noexcept
    {
        return m_readEvent != nullptr && m_writeEvent != nullptr && m_cancelEvent != nullptr;
    }

    [[nodiscard]] bool write(std::span<const std::uint8_t> bytes) override
    {
        const std::lock_guard lock(m_writeMutex);
        std::size_t done = 0;
        while (done < bytes.size()) {
            if (m_gone.load(std::memory_order_acquire)) {
                return false;
            }
            OVERLAPPED overlapped{};
            overlapped.hEvent = m_writeEvent;
            ResetEvent(m_writeEvent);
            const auto chunk = static_cast<DWORD>(std::min(bytes.size() - done, kMostTransfer));
            DWORD written = 0;
            if (!WriteFile(m_handle, bytes.data() + done, chunk, &written, &overlapped)) {
                const DWORD error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    lose(error);
                    return false;
                }
                if (WaitForSingleObject(m_writeEvent, kWriteTimeoutMs) != WAIT_OBJECT_0) {
                    CancelIoEx(m_handle, &overlapped);
                    GetOverlappedResult(m_handle, &overlapped, &written, TRUE);
                    log::warning(kCategory, "writing to the serial port took longer than {} ms", kWriteTimeoutMs);
                    return false;
                }
                if (!GetOverlappedResult(m_handle, &overlapped, &written, FALSE)) {
                    lose(GetLastError());
                    return false;
                }
            }
            if (written == 0) {
                return false;
            }
            done += written;
        }
        return true;
    }

    [[nodiscard]] std::size_t read(std::span<std::uint8_t> buffer, std::chrono::milliseconds timeout) override
    {
        if (buffer.empty() || m_gone.load(std::memory_order_acquire) ||
            m_interrupted.load(std::memory_order_acquire)) {
            return 0;
        }
        OVERLAPPED overlapped{};
        overlapped.hEvent = m_readEvent;
        ResetEvent(m_readEvent);
        const auto size = static_cast<DWORD>(std::min(buffer.size(), kMostTransfer));
        DWORD received = 0;
        if (ReadFile(m_handle, buffer.data(), size, &received, &overlapped)) {
            return received;
        }
        DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING) {
            lose(error);
            return 0;
        }
        const std::array<HANDLE, 2> handles{m_readEvent, m_cancelEvent};
        const DWORD waited =
            WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, clampedMilliseconds(timeout));
        if (waited != WAIT_OBJECT_0) {
            CancelIoEx(m_handle, &overlapped);
        }
        if (!GetOverlappedResult(m_handle, &overlapped, &received, TRUE)) {
            error = GetLastError();
            if (error != ERROR_OPERATION_ABORTED) {
                lose(error);
            }
            return 0;
        }
        return received;
    }

    [[nodiscard]] bool setBaud(std::uint32_t baud) override
    {
        DCB dcb{};
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(m_handle, &dcb)) {
            return false;
        }
        dcb.BaudRate = baud;
        return SetCommState(m_handle, &dcb) != 0;
    }

    [[nodiscard]] bool setDtr(bool on) override { return EscapeCommFunction(m_handle, on ? SETDTR : CLRDTR) != 0; }

    [[nodiscard]] bool setRts(bool on) override { return EscapeCommFunction(m_handle, on ? SETRTS : CLRRTS) != 0; }

    void discardInput() override { PurgeComm(m_handle, PURGE_RXCLEAR); }

    void interrupt() override
    {
        m_interrupted.store(true, std::memory_order_release);
        SetEvent(m_cancelEvent);
        CancelIoEx(m_handle, nullptr);
    }

    [[nodiscard]] bool datagrams() const override { return false; }

    [[nodiscard]] bool gone() const override { return m_gone.load(std::memory_order_acquire); }

private:
    void lose(DWORD error) noexcept
    {
        if ((error != ERROR_OPERATION_ABORTED || !m_interrupted.load(std::memory_order_acquire))
            && !m_gone.exchange(true, std::memory_order_acq_rel)) {
            log::warning(kCategory, "the serial port stopped working (Windows error {})", error);
        }
    }

    HANDLE m_handle;
    HANDLE m_readEvent;
    HANDLE m_writeEvent;
    HANDLE m_cancelEvent;
    std::mutex m_writeMutex;
    std::atomic<bool> m_gone{false};
    std::atomic<bool> m_interrupted{false};
};

}

PortKind classifyPort(std::uint16_t vendorId, std::uint16_t) noexcept
{
    switch (vendorId) {
    case kEspressifVendor:
        return PortKind::EspressifUsb;
    case kWchVendor:
        return PortKind::Ch34x;
    case kSiliconLabsVendor:
        return PortKind::Cp210x;
    default:
        return PortKind::Other;
    }
}

bool usbSerialJtag(std::uint16_t vendorId, std::uint16_t productId) noexcept
{
    return vendorId == kEspressifVendor && productId == kUsbSerialJtagProduct;
}

bool probedAutomatically(PortKind kind) noexcept
{
    return kind != PortKind::Other;
}

bool bridge(PortKind kind) noexcept
{
    return kind == PortKind::Ch34x || kind == PortKind::Cp210x || kind == PortKind::Other;
}

std::optional<std::pair<std::uint16_t, std::uint16_t>> usbIdsOf(std::string_view instanceId) noexcept
{
    const std::optional<std::uint16_t> vendor = hexAfter(instanceId, "VID_");
    const std::optional<std::uint16_t> product = hexAfter(instanceId, "PID_");
    if (!vendor || !product) {
        return std::nullopt;
    }
    return std::pair{*vendor, *product};
}

int portNumber(std::string_view name) noexcept
{
    if (name.size() < 4 || name.size() > 6 || upper(name[0]) != 'C' || upper(name[1]) != 'O' || upper(name[2]) != 'M') {
        return 0;
    }
    int number = 0;
    for (const char c : name.substr(3)) {
        if (c < '0' || c > '9') {
            return 0;
        }
        number = number * 10 + (c - '0');
    }
    if (name[3] == '0' || number < 1 || number > 256) {
        return 0;
    }
    return number;
}

bool validPortName(std::string_view name) noexcept
{
    return portNumber(name) > 0;
}

std::vector<SerialPortInfo> listSerialPorts()
{
    std::vector<SerialPortInfo> ports;
    const HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        return ports;
    }
    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    for (DWORD at = 0; SetupDiEnumDeviceInfo(set, at, &info); ++at) {
        const std::string name = narrow(registryText(set, info, L"PortName"));
        const int number = portNumber(name);
        if (number == 0) {
            continue;
        }
        SerialPortInfo port;
        port.name = "COM" + std::to_string(number);
        port.description = narrow(propertyText(set, info, SPDRP_FRIENDLYNAME));
        std::array<wchar_t, MAX_DEVICE_ID_LEN + 1> id{};
        if (SetupDiGetDeviceInstanceIdW(set, &info, id.data(), static_cast<DWORD>(id.size()), nullptr)) {
            if (const auto ids = usbIdsOf(narrow(id.data()))) {
                port.vendorId = ids->first;
                port.productId = ids->second;
            }
        }
        port.kind = classifyPort(port.vendorId, port.productId);
        ports.push_back(std::move(port));
    }
    SetupDiDestroyDeviceInfoList(set);
    std::ranges::sort(ports, {}, [](const SerialPortInfo& port) { return portNumber(port.name); });
    return ports;
}

std::unique_ptr<Port> openSerialPort(std::string_view name, std::uint32_t baud, SerialFailure& failure)
{
    failure = SerialFailure::NotFound;
    const int number = portNumber(name);
    if (number == 0) {
        return nullptr;
    }
    const std::wstring path = L"\\\\.\\COM" + std::to_wstring(number);
    const HANDLE handle =
        CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION) {
            failure = SerialFailure::Busy;
        } else if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
            failure = SerialFailure::Refused;
        }
        return nullptr;
    }
    SetupComm(handle, kQueueSize, kQueueSize);
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    GetCommState(handle, &dcb);
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fTXContinueOnXoff = TRUE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;
    if (!SetCommState(handle, &dcb)) {
        CloseHandle(handle);
        failure = SerialFailure::Refused;
        return nullptr;
    }
    EscapeCommFunction(handle, CLRDTR);
    EscapeCommFunction(handle, CLRRTS);
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = kDriverReadTimeoutMs;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = kWriteTimeoutMs;
    SetCommTimeouts(handle, &timeouts);
    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    auto port = std::make_unique<SerialPort>(handle);
    if (!port->ready()) {
        failure = SerialFailure::Refused;
        return nullptr;
    }
    failure = SerialFailure::None;
    return port;
}

}
