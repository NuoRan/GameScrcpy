#define LOG_TAG "AdbProcessImpl"
#include "Logger.h"
#include <filesystem>
#include <regex>
#include <sstream>
#include <algorithm>

#include "adbprocessimpl.h"
#include "StringUtils.h"

std::string AdbProcessImpl::s_adbPath;
extern std::string g_adbPath;

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

AdbProcessImpl::AdbProcessImpl()
{
}

AdbProcessImpl::~AdbProcessImpl()
{
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_resultCallback = nullptr;
    }
    if (m_running) {
        kill();
    }
    if (m_monitorThread.joinable()) {
        m_monitorThread.join();
    }
    cleanupHandles();
}

void AdbProcessImpl::setResultCallback(ResultCallback cb)
{
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_resultCallback = std::move(cb);
}

// ---------------------------------------------------------------------------
// Win32 Command-line builder
// ---------------------------------------------------------------------------

std::wstring AdbProcessImpl::buildCommandLine(const std::string &program, const std::vector<std::string> &args)
{
    // Always quote the program path
    std::wstring cmd = L"\"" + strutil::toWide(program) + L"\"";
    for (const std::string &arg : args) {
        cmd += L' ';
        std::wstring warg = strutil::toWide(arg);
        bool needsQuote = warg.find_first_of(L" \t\"") != std::wstring::npos;
        if (needsQuote) {
            cmd += L'"';
            for (wchar_t ch : warg) {
                if (ch == L'"') cmd += L"\\\"";
                else cmd += ch;
            }
            cmd += L'"';
        } else {
            cmd += warg;
        }
    }
    return cmd;
}

// ---------------------------------------------------------------------------
// Process execution (async via Win32 CreateProcessW)
// ---------------------------------------------------------------------------

void AdbProcessImpl::startProcess(const std::string &program, const std::vector<std::string> &args)
{
    // Join any previous monitor thread
    if (m_monitorThread.joinable()) {
        m_monitorThread.join();
    }
    cleanupHandles();

    m_standardOutput.clear();
    m_errorOutput.clear();
    m_arguments = args;

    // Increment generation to invalidate any stale callbacks
    uint64_t gen = ++m_generation;

    // Create pipes for stdout / stderr
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE stdOutWrite = nullptr;
    HANDLE stdErrWrite = nullptr;

    if (!CreatePipe(&m_stdOutRead, &stdOutWrite, &sa, 0)) {
        emitResult(qsc::AdbProcess::AER_ERROR_START);
        return;
    }
    SetHandleInformation(m_stdOutRead, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&m_stdErrRead, &stdErrWrite, &sa, 0)) {
        CloseHandle(m_stdOutRead);
        m_stdOutRead = nullptr;
        CloseHandle(stdOutWrite);
        emitResult(qsc::AdbProcess::AER_ERROR_START);
        return;
    }
    SetHandleInformation(m_stdErrRead, HANDLE_FLAG_INHERIT, 0);

    // Build command line
    std::wstring cmdLine = buildCommandLine(program, args);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = stdOutWrite;
    si.hStdError  = stdErrWrite;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};

    BOOL created = CreateProcessW(
        nullptr,
        cmdLine.data(),   // mutable buffer required by CreateProcessW
        nullptr, nullptr,
        TRUE,             // inherit handles (for pipes)
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    // Parent no longer needs write ends
    CloseHandle(stdOutWrite);
    CloseHandle(stdErrWrite);

    if (!created) {
        DWORD err = GetLastError();
        CloseHandle(m_stdOutRead);
        m_stdOutRead = nullptr;
        CloseHandle(m_stdErrRead);
        m_stdErrRead = nullptr;

        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            emitResult(qsc::AdbProcess::AER_ERROR_MISSING_BINARY);
        } else {
            LOGE() << "CreateProcessW failed, error=" << err;
            emitResult(qsc::AdbProcess::AER_ERROR_START);
        }
        return;
    }

    m_processHandle = pi.hProcess;
    CloseHandle(pi.hThread); // Thread handle not needed
    m_running = true;

    // Notify: process has started
    emitResult(qsc::AdbProcess::AER_SUCCESS_START);

    // Launch background monitor
    m_monitorThread = std::thread(&AdbProcessImpl::monitorProc, this, gen);
}

// ---------------------------------------------------------------------------
// Monitor thread — polls pipes + waits for process exit
// ---------------------------------------------------------------------------

void AdbProcessImpl::monitorProc(uint64_t generation)
{
    std::string rawStdOut;
    std::string rawStdErr;

    while (m_running && m_generation.load() == generation) {
        readPipeAvailable(m_stdOutRead, rawStdOut);

        // Read stderr and log incrementally
        size_t prevSize = rawStdErr.size();
        readPipeAvailable(m_stdErrRead, rawStdErr);
        if (rawStdErr.size() > prevSize) {
            std::string newChunk(rawStdErr.begin() + prevSize, rawStdErr.end());
            std::string errStr = strutil::trim(newChunk);
            if (!errStr.empty()) {
                LOGW() << "AdbProcessImpl::error:" << errStr.data();
            }
        }

        DWORD waitResult = WaitForSingleObject(m_processHandle, 50);
        if (waitResult == WAIT_OBJECT_0) {
            // Process finished — drain remaining pipe data
            readPipeAvailable(m_stdOutRead, rawStdOut);
            prevSize = rawStdErr.size();
            readPipeAvailable(m_stdErrRead, rawStdErr);
            if (rawStdErr.size() > prevSize) {
                std::string tail(rawStdErr.begin() + prevSize, rawStdErr.end());
                std::string errStr = strutil::trim(tail);
                if (!errStr.empty()) {
                    LOGW() << "AdbProcessImpl::error:" << errStr.data();
                }
            }
            break;
        }
    }

    // Store output as std::string (trimmed)
    m_standardOutput = strutil::trim(rawStdOut);
    m_errorOutput = strutil::trim(rawStdErr);

    // Get exit code
    DWORD dwExitCode = 0;
    int exitCode = -1;
    if (m_processHandle && GetExitCodeProcess(m_processHandle, &dwExitCode)) {
        exitCode = static_cast<int>(dwExitCode);
    }

    m_running = false;

    // Only emit if this generation is still current (prevents stale callbacks after kill+reuse)
    if (m_generation.load() == generation) {
        if (exitCode == 0) {
            emitResult(qsc::AdbProcess::AER_SUCCESS_EXEC);
        } else {
            emitResult(qsc::AdbProcess::AER_ERROR_EXEC);
        }
    }
}

// ---------------------------------------------------------------------------
// Pipe reading helper (non-blocking via PeekNamedPipe)
// ---------------------------------------------------------------------------

size_t AdbProcessImpl::readPipeAvailable(HANDLE pipe, std::string &buf)
{
    if (!pipe) return 0;

    size_t totalRead = 0;
    DWORD available = 0;
    while (PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
        char chunk[4096];
        DWORD toRead = (available < sizeof(chunk)) ? available : static_cast<DWORD>(sizeof(chunk));
        DWORD bytesRead = 0;
        if (ReadFile(pipe, chunk, toRead, &bytesRead, nullptr) && bytesRead > 0) {
            buf.append(chunk, bytesRead);
            totalRead += bytesRead;
        } else {
            break;
        }
    }
    return totalRead;
}

// ---------------------------------------------------------------------------
// Handle cleanup
// ---------------------------------------------------------------------------

void AdbProcessImpl::cleanupHandles()
{
    if (m_stdOutRead) { CloseHandle(m_stdOutRead); m_stdOutRead = nullptr; }
    if (m_stdErrRead) { CloseHandle(m_stdErrRead); m_stdErrRead = nullptr; }
    if (m_processHandle) { CloseHandle(m_processHandle); m_processHandle = nullptr; }
}

// ---------------------------------------------------------------------------
// Emit callback (thread-safe)
// ---------------------------------------------------------------------------

void AdbProcessImpl::emitResult(qsc::AdbProcess::ADB_EXEC_RESULT result)
{
    ResultCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        cb = m_resultCallback;
    }
    if (cb) {
        cb(result);
    }
}

// ---------------------------------------------------------------------------
// ADB path resolution (preserved from original)
// ---------------------------------------------------------------------------

const std::string &AdbProcessImpl::getAdbPath()
{
    if (s_adbPath.empty()) {
        // Get application directory via Win32
        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::string appDir = std::filesystem::path(exePath).parent_path().string();

        // Potential ADB paths
        std::vector<std::string> potentialPaths;
        char *envPath = nullptr;
        size_t envLen = 0;
        if (_dupenv_s(&envPath, &envLen, "KZSCRCPY_ADB_PATH") == 0 && envPath) {
            potentialPaths.push_back(envPath);
            free(envPath);
        }
        if (!g_adbPath.empty()) potentialPaths.push_back(g_adbPath);
        potentialPaths.push_back(appDir + "/adb.exe");

        namespace fs = std::filesystem;
        for (const std::string &path : potentialPaths) {
            if (!path.empty() && fs::is_regular_file(fs::path(strutil::toWide(path)))) {
                s_adbPath = path;
                break;
            }
        }

        if (s_adbPath.empty()) {
            LOGW() << "ADB路径未找到";
        } else {
            auto absPath = fs::absolute(fs::path(strutil::toWide(s_adbPath)));
            LOG_I("adb path: %s", absPath.string().c_str());
        }
    }
    return s_adbPath;
}

// ---------------------------------------------------------------------------
// Public API — ADB operations
// ---------------------------------------------------------------------------

void AdbProcessImpl::execute(const std::string &serial, const std::vector<std::string> &args)
{
    std::vector<std::string> adbArgs;
    if (!serial.empty()) {
        adbArgs.push_back("-s");
        adbArgs.push_back(serial);
    }
    adbArgs.insert(adbArgs.end(), args.begin(), args.end());
    startProcess(getAdbPath(), adbArgs);
}

bool AdbProcessImpl::isRuning() const
{
    return m_running.load();
}

void AdbProcessImpl::setShowTouchesEnabled(const std::string &serial, bool enabled)
{
    std::vector<std::string> adbArgs = {
        "shell", "settings", "put", "system", "show_touches",
        enabled ? "1" : "0"
    };
    execute(serial, adbArgs);
}

void AdbProcessImpl::kill()
{
    if (m_processHandle && m_running) {
        TerminateProcess(m_processHandle, 1);
        // Monitor thread will detect exit and clean up
    }
}

std::vector<std::string> AdbProcessImpl::arguments() const
{
    return m_arguments;
}

std::vector<std::string> AdbProcessImpl::getDevicesSerialFromStdOut()
{
    std::vector<std::string> serials;
    std::istringstream stream(m_standardOutput);
    std::string line;
    while (std::getline(stream, line)) {
        // Remove trailing \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        // Split by tab
        auto tabPos = line.find('\t');
        if (tabPos != std::string::npos) {
            std::string serial = line.substr(0, tabPos);
            std::string status = line.substr(tabPos + 1);
            if (status == "device") {
                serials.push_back(serial);
            }
        }
    }
    return serials;
}

std::string AdbProcessImpl::getDeviceIPFromStdOut()
{
    std::string ip;
    std::regex ipRegExp("inet addr:([\\d.]+)", std::regex::icase);
    std::smatch match;
    if (std::regex_search(m_standardOutput, match, ipRegExp) && match.size() > 1) {
        ip = match[1].str();
    }
    return ip;
}

std::string AdbProcessImpl::getDeviceIPByIpFromStdOut()
{
    std::string ip;
    std::regex ipRegExp("wlan0\\s+inet\\s+([\\d.]+)", std::regex::icase);
    std::smatch match;
    if (std::regex_search(m_standardOutput, match, ipRegExp) && match.size() > 1) {
        ip = match[1].str();
    }
    return ip;
}

std::string AdbProcessImpl::getStdOut()
{
    return m_standardOutput;
}

std::string AdbProcessImpl::getErrorOut()
{
    return m_errorOutput;
}

void AdbProcessImpl::forward(const std::string &serial, uint16_t localPort, const std::string &deviceSocketName)
{
    std::vector<std::string> adbArgs = {
        "forward",
        "tcp:" + std::to_string(localPort),
        "localabstract:" + deviceSocketName
    };
    execute(serial, adbArgs);
}

void AdbProcessImpl::forwardRemove(const std::string &serial, uint16_t localPort)
{
    std::vector<std::string> adbArgs = {
        "forward", "--remove",
        "tcp:" + std::to_string(localPort)
    };
    execute(serial, adbArgs);
}

void AdbProcessImpl::reverse(const std::string &serial, const std::string &deviceSocketName, uint16_t localPort)
{
    std::vector<std::string> adbArgs = {
        "reverse",
        "localabstract:" + deviceSocketName,
        "tcp:" + std::to_string(localPort)
    };
    execute(serial, adbArgs);
}

void AdbProcessImpl::reverseRemove(const std::string &serial, const std::string &deviceSocketName)
{
    std::vector<std::string> adbArgs = {
        "reverse", "--remove",
        "localabstract:" + deviceSocketName
    };
    execute(serial, adbArgs);
}

void AdbProcessImpl::push(const std::string &serial, const std::string &local, const std::string &remote)
{
    std::vector<std::string> adbArgs = {
        "push", local, remote
    };
    execute(serial, adbArgs);
}

void AdbProcessImpl::install(const std::string &serial, const std::string &local)
{
    std::vector<std::string> adbArgs = {
        "install", "-r", local
    };
    execute(serial, adbArgs);
}

void AdbProcessImpl::removePath(const std::string &serial, const std::string &path)
{
    std::vector<std::string> adbArgs = {
        "shell", "rm", path
    };
    execute(serial, adbArgs);
}
