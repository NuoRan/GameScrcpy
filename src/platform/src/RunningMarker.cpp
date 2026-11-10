#include "gs/platform/RunningMarker.h"

#include <windows.h>

#include <string>

namespace gs::platform {

RunningMarker::RunningMarker(std::wstring_view name)
{
    const std::wstring local(name);
    const std::wstring global = L"Global\\" + local;
    m_local = CreateMutexW(nullptr, FALSE, local.c_str());
    m_global = CreateMutexW(nullptr, FALSE, global.c_str());
}

RunningMarker::~RunningMarker()
{
    for (void* handle : {m_local, m_global}) {
        if (handle != nullptr) {
            CloseHandle(handle);
        }
    }
}

bool runningMarkerPresent(std::wstring_view name) noexcept
{
    try {
        const std::wstring wanted(name);
        HANDLE handle = OpenMutexW(SYNCHRONIZE, FALSE, wanted.c_str());
        if (handle == nullptr) {
            return false;
        }
        CloseHandle(handle);
        return true;
    } catch (...) {
        return false;
    }
}

}
