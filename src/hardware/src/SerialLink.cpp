#include "Links.h"
#include "Worker.h"

#include <windows.h>

#include <setupapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <thread>
#include <utility>

namespace gs::hardware::detail {

namespace {

constexpr GUID kPortsClass{0x4D36E978, 0xE325, 0x11CE, {0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18}};
constexpr DWORD kQueueSize = 16 * 1024;
constexpr DWORD kReadChunk = 4096;
constexpr DWORD kReadWaitMs = 1000;
constexpr DWORD kWriteTimeoutMs = 2000;
constexpr char kXon = 0x11;
constexpr char kXoff = 0x13;

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

bool comPort(std::wstring_view name) noexcept
{
    if (name.size() < 4 || name.size() > 6) {
        return false;
    }
    const auto upper = [](wchar_t c) { return c >= L'a' && c <= L'z' ? static_cast<wchar_t>(c - L'a' + L'A') : c; };
    if (upper(name[0]) != L'C' || upper(name[1]) != L'O' || upper(name[2]) != L'M' || name[3] == L'0') {
        return false;
    }
    return std::ranges::all_of(name.substr(3), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
}

BYTE parityOf(Parity parity) noexcept
{
    switch (parity) {
    case Parity::Even:
        return EVENPARITY;
    case Parity::Odd:
        return ODDPARITY;
    case Parity::Mark:
        return MARKPARITY;
    case Parity::Space:
        return SPACEPARITY;
    case Parity::None:
        break;
    }
    return NOPARITY;
}

BYTE stopBitsOf(StopBits bits) noexcept
{
    switch (bits) {
    case StopBits::OneAndHalf:
        return ONE5STOPBITS;
    case StopBits::Two:
        return TWOSTOPBITS;
    case StopBits::One:
        break;
    }
    return ONESTOPBIT;
}

bool configure(HANDLE handle, const SerialOptions& options)
{
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle, &dcb)) {
        return false;
    }
    dcb.BaudRate = options.baudRate;
    dcb.ByteSize = options.dataBits;
    dcb.Parity = parityOf(options.parity);
    dcb.StopBits = stopBitsOf(options.stopBits);
    dcb.fBinary = TRUE;
    dcb.fParity = options.parity == Parity::None ? FALSE : TRUE;
    dcb.fOutxCtsFlow = options.flowControl == FlowControl::Hardware ? TRUE : FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = options.dtr ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
    dcb.fRtsControl = options.flowControl == FlowControl::Hardware ? RTS_CONTROL_HANDSHAKE
                      : options.rts                                ? RTS_CONTROL_ENABLE
                                                                   : RTS_CONTROL_DISABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fTXContinueOnXoff = TRUE;
    dcb.fOutX = options.flowControl == FlowControl::Software ? TRUE : FALSE;
    dcb.fInX = options.flowControl == FlowControl::Software ? TRUE : FALSE;
    dcb.XonChar = kXon;
    dcb.XoffChar = kXoff;
    dcb.XonLim = static_cast<WORD>(kQueueSize / 4);
    dcb.XoffLim = static_cast<WORD>(kQueueSize / 4);
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;
    return SetCommState(handle, &dcb) != 0;
}

class SerialLink final : public Device
{
public:
    SerialLink(HANDLE handle, std::shared_ptr<Events> events)
        : m_handle(handle)
        , m_events(std::move(events))
        , m_stop(CreateEventW(nullptr, TRUE, FALSE, nullptr))
        , m_readEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr))
        , m_writeEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr))
    {
        if (ready()) {
            m_reader = std::thread([this] { readLoop(); });
        }
    }

    ~SerialLink() override
    {
        m_stopping.store(true, std::memory_order_release);
        if (m_stop != nullptr) {
            SetEvent(m_stop);
        }
        CancelIoEx(m_handle, nullptr);
        if (m_reader.joinable()) {
            m_reader.join();
        }
        m_writer.stop();
        CloseHandle(m_handle);
        for (const HANDLE event : {m_stop, m_readEvent, m_writeEvent}) {
            if (event != nullptr) {
                CloseHandle(event);
            }
        }
    }

    SerialLink(const SerialLink&) = delete;
    SerialLink& operator=(const SerialLink&) = delete;

    [[nodiscard]] bool ready() const noexcept
    {
        return m_stop != nullptr && m_readEvent != nullptr && m_writeEvent != nullptr;
    }

    void write(Bytes bytes, Completion done) override
    {
        m_writer.post([this, bytes = std::move(bytes), done = std::move(done)] { done(writeNow(bytes)); });
    }

    void setSignals(std::optional<bool> dtr, std::optional<bool> rts, Completion done) override
    {
        m_writer.post([this, dtr, rts, done = std::move(done)] {
            std::string problem;
            if (dtr && !EscapeCommFunction(m_handle, *dtr ? SETDTR : CLRDTR)) {
                problem = systemMessage(GetLastError());
            }
            if (problem.empty() && rts && !EscapeCommFunction(m_handle, *rts ? SETRTS : CLRRTS)) {
                problem = systemMessage(GetLastError());
            }
            done(std::move(problem));
        });
    }

private:
    std::string writeNow(const Bytes& bytes)
    {
        std::size_t done = 0;
        while (done < bytes.size()) {
            if (m_stopping.load(std::memory_order_acquire)) {
                return "the device was closed";
            }
            OVERLAPPED overlapped{};
            overlapped.hEvent = m_writeEvent;
            ResetEvent(m_writeEvent);
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - done, kMostWriteBytes));
            DWORD written = 0;
            if (!WriteFile(m_handle, bytes.data() + done, chunk, &written, &overlapped)) {
                const DWORD error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    return systemMessage(error);
                }
                const std::array<HANDLE, 2> waits{m_writeEvent, m_stop};
                const DWORD which = WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE,
                                                           INFINITE);
                if (which != WAIT_OBJECT_0) {
                    CancelIoEx(m_handle, &overlapped);
                    GetOverlappedResult(m_handle, &overlapped, &written, TRUE);
                    return "the device was closed";
                }
                if (!GetOverlappedResult(m_handle, &overlapped, &written, FALSE)) {
                    return systemMessage(GetLastError());
                }
            }
            if (written == 0) {
                return "the device did not take the data in time";
            }
            done += written;
        }
        return {};
    }

    void readLoop()
    {
        std::vector<std::uint8_t> buffer(kReadChunk);
        for (;;) {
            OVERLAPPED overlapped{};
            overlapped.hEvent = m_readEvent;
            ResetEvent(m_readEvent);
            DWORD received = 0;
            if (!ReadFile(m_handle, buffer.data(), kReadChunk, &received, &overlapped)) {
                const DWORD error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    lost(error);
                    return;
                }
                const std::array<HANDLE, 2> waits{m_readEvent, m_stop};
                const DWORD which = WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE,
                                                           INFINITE);
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
                m_events->data(Bytes(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(received)));
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
    std::shared_ptr<Events> m_events;
    HANDLE m_stop;
    HANDLE m_readEvent;
    HANDLE m_writeEvent;
    std::atomic<bool> m_stopping{false};
    Worker m_writer;
    std::thread m_reader;
};

}

std::vector<DeviceInfo> listSerialPorts()
{
    std::vector<DeviceInfo> ports;
    const HDEVINFO set = SetupDiGetClassDevsW(&kPortsClass, nullptr, nullptr, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        return ports;
    }
    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    for (DWORD at = 0; SetupDiEnumDeviceInfo(set, at, &info); ++at) {
        const std::wstring port = registryText(set, info, L"PortName");
        if (!comPort(port)) {
            continue;
        }
        std::array<wchar_t, 512> instance{};
        if (!SetupDiGetDeviceInstanceIdW(set, &info, instance.data(), static_cast<DWORD>(instance.size()), nullptr)) {
            continue;
        }
        DeviceInfo device;
        device.kind = Kind::Serial;
        const std::string instanceId = narrow(instance.data());
        device.id = "serial:" + instanceId;
        device.path = "\\\\.\\" + narrow(port);
        const std::wstring friendly = propertyText(set, info, SPDRP_FRIENDLYNAME);
        device.name = friendly.empty() ? narrow(port) : narrow(friendly);
        device.vendorId = hexAfter(instanceId, "VID_");
        device.productId = hexAfter(instanceId, "PID_");
        ports.push_back(std::move(device));
    }
    SetupDiDestroyDeviceInfoList(set);
    std::ranges::sort(ports, {}, &DeviceInfo::id);
    return ports;
}

OpenResult openSerial(const DeviceInfo& device, const SerialOptions& options, std::shared_ptr<Events> events)
{
    OpenResult result;
    if (!validSerialOptions(options)) {
        result.problem = "the serial settings are not possible";
        return result;
    }
    const std::wstring path = widen(device.path);
    const HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        result.missing = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
        result.problem = systemMessage(error);
        return result;
    }
    SetupComm(handle, kQueueSize, kQueueSize);
    if (!configure(handle, options)) {
        result.problem = systemMessage(GetLastError());
        CloseHandle(handle);
        return result;
    }
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = kReadWaitMs;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = kWriteTimeoutMs;
    SetCommTimeouts(handle, &timeouts);
    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    auto link = std::make_unique<SerialLink>(handle, std::move(events));
    if (!link->ready()) {
        result.problem = "the device could not be set up";
        return result;
    }
    result.device = std::move(link);
    return result;
}

}
