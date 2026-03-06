#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>

#include "adbprocess.h"

/**
 * @brief ADB 进程实现 / ADB Process Implementation
 *
 * 使用 Win32 CreateProcessW 替代 QProcess，提供 adb 命令执行能力。
 * Uses Win32 CreateProcessW instead of QProcess for adb command execution.
 */
class AdbProcessImpl
{
public:
    using ResultCallback = std::function<void(qsc::AdbProcess::ADB_EXEC_RESULT)>;

    AdbProcessImpl();
    ~AdbProcessImpl();

    /// 设置结果回调（替代 Qt 信号）/ Set result callback (replaces Qt signal)
    void setResultCallback(ResultCallback cb);

    void execute(const std::string &serial, const std::vector<std::string> &args);
    void forward(const std::string &serial, uint16_t localPort, const std::string &deviceSocketName);
    void forwardRemove(const std::string &serial, uint16_t localPort);
    void reverse(const std::string &serial, const std::string &deviceSocketName, uint16_t localPort);
    void reverseRemove(const std::string &serial, const std::string &deviceSocketName);
    void push(const std::string &serial, const std::string &local, const std::string &remote);
    void install(const std::string &serial, const std::string &local);
    void removePath(const std::string &serial, const std::string &path);
    bool isRuning() const;
    void setShowTouchesEnabled(const std::string &serial, bool enabled);
    void kill();
    std::vector<std::string> arguments() const;
    std::vector<std::string> getDevicesSerialFromStdOut();
    std::string getDeviceIPFromStdOut();
    std::string getDeviceIPByIpFromStdOut();
    std::string getStdOut();
    std::string getErrorOut();

    static const std::string &getAdbPath();

private:
    void startProcess(const std::string &program, const std::vector<std::string> &args);
    void monitorProc(uint64_t generation);
    static size_t readPipeAvailable(HANDLE pipe, std::string &buf);
    void cleanupHandles();
    void emitResult(qsc::AdbProcess::ADB_EXEC_RESULT result);
    static std::wstring buildCommandLine(const std::string &program, const std::vector<std::string> &args);

    // Callback
    ResultCallback m_resultCallback;
    mutable std::mutex m_callbackMutex;

    // Process state
    HANDLE m_processHandle = nullptr;
    HANDLE m_stdOutRead = nullptr;
    HANDLE m_stdErrRead = nullptr;
    std::thread m_monitorThread;
    std::atomic<bool> m_running{false};
    std::atomic<uint64_t> m_generation{0};  // 防止旧进程回调干扰新进程

    // Output
    std::string m_standardOutput;
    std::string m_errorOutput;
    std::vector<std::string> m_arguments;

    static std::string s_adbPath;
};
