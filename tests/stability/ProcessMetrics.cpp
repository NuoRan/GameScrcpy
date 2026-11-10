#include "ProcessMetrics.h"

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

namespace gs::stability {

namespace {

std::uint64_t threadCount()
{
    const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }
    const DWORD self = ::GetCurrentProcessId();
    THREADENTRY32 entry{};
    entry.dwSize = sizeof entry;
    std::uint64_t count = 0;
    for (BOOL more = ::Thread32First(snapshot, &entry); more != FALSE; more = ::Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID == self) {
            ++count;
        }
    }
    ::CloseHandle(snapshot);
    return count;
}

}

ProcessSample sampleProcess()
{
    ProcessSample sample;
    DWORD handles = 0;
    if (::GetProcessHandleCount(::GetCurrentProcess(), &handles) != FALSE) {
        sample.handles = handles;
    }
    sample.threads = threadCount();
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof memory;
    if (::GetProcessMemoryInfo(::GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                               sizeof memory) != FALSE) {
        sample.privateBytes = memory.PrivateUsage;
        sample.workingSet = memory.WorkingSetSize;
    }
    return sample;
}

}
