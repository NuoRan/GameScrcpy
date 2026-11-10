#include <gs/platform/CrashHandler.h>

#include <windows.h>

#include <filesystem>

namespace {

LPTOP_LEVEL_EXCEPTION_FILTER g_report = nullptr;

LONG WINAPI reportThenEnd(EXCEPTION_POINTERS* exception)
{
    if (g_report != nullptr) {
        g_report(exception);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) {
        return 2;
    }
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!gs::platform::installCrashHandler(std::filesystem::path(argv[1]), std::filesystem::path(argv[2]))) {
        return 3;
    }
    g_report = ::SetUnhandledExceptionFilter(&reportThenEnd);
    if (g_report == nullptr) {
        return 4;
    }
    gs::platform::crashOnPurpose();
}
