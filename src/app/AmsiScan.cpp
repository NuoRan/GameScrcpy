#include "AmsiScan.h"

#include <windows.h>
#include <objbase.h>

#include <limits>
#include <string>

namespace gs::app {

namespace {

constexpr int kDetected = 32768;
constexpr int kBlockedFirst = 0x4000;
constexpr int kBlockedLast = 0x4fff;

using Initialize = HRESULT(WINAPI*)(LPCWSTR, void**);
using Uninitialize = void(WINAPI*)(void*);
using OpenSession = HRESULT(WINAPI*)(void*, void**);
using CloseSession = void(WINAPI*)(void*, void*);
using ScanBuffer = HRESULT(WINAPI*)(void*, void*, ULONG, LPCWSTR, void*, int*);

class Library
{
public:
    Library() : m_module(LoadLibraryExW(L"amsi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {}

    ~Library()
    {
        if (m_module != nullptr) {
            FreeLibrary(m_module);
        }
    }

    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    template <typename Function>
    [[nodiscard]] Function entry(const char* name) const
    {
        return m_module != nullptr ? reinterpret_cast<Function>(reinterpret_cast<void*>(GetProcAddress(m_module, name)))
                                   : nullptr;
    }

private:
    HMODULE m_module;
};

class ComScope
{
public:
    ComScope() : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}

    ~ComScope()
    {
        if (SUCCEEDED(m_result)) {
            CoUninitialize();
        }
    }

    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    HRESULT m_result;
};

std::wstring wide(std::string_view text)
{
    if (text.empty() || text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {};
    }
    const int size = static_cast<int>(text.size());
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), size, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(length > 0 ? length : 0), L'\0');
    if (length > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.data(), size, out.data(), length);
    }
    return out;
}

ext::ScanVerdict verdictOf(int result)
{
    if (result >= kDetected) {
        return ext::ScanVerdict::Threat;
    }
    if (result >= kBlockedFirst && result <= kBlockedLast) {
        return ext::ScanVerdict::Blocked;
    }
    return ext::ScanVerdict::Clean;
}

}

ext::ScanVerdict scanWithAmsi(std::string_view bytes, std::string_view name)
{
    if (bytes.size() > std::numeric_limits<ULONG>::max()) {
        return ext::ScanVerdict::Unavailable;
    }
    const ComScope com;
    const Library library;
    const auto initialize = library.entry<Initialize>("AmsiInitialize");
    const auto uninitialize = library.entry<Uninitialize>("AmsiUninitialize");
    const auto openSession = library.entry<OpenSession>("AmsiOpenSession");
    const auto closeSession = library.entry<CloseSession>("AmsiCloseSession");
    const auto scan = library.entry<ScanBuffer>("AmsiScanBuffer");
    if (initialize == nullptr || uninitialize == nullptr || openSession == nullptr || closeSession == nullptr ||
        scan == nullptr) {
        return ext::ScanVerdict::Unavailable;
    }
    void* context = nullptr;
    if (FAILED(initialize(L"GameScrcpy", &context)) || context == nullptr) {
        return ext::ScanVerdict::Unavailable;
    }
    void* session = nullptr;
    const bool opened = SUCCEEDED(openSession(context, &session));
    const std::wstring content = wide(name);
    int result = 0;
    const HRESULT scanned = scan(context, const_cast<char*>(bytes.data()), static_cast<ULONG>(bytes.size()),
                                 content.empty() ? nullptr : content.c_str(), opened ? session : nullptr, &result);
    if (opened) {
        closeSession(context, session);
    }
    uninitialize(context);
    return SUCCEEDED(scanned) ? verdictOf(result) : ext::ScanVerdict::Unavailable;
}

}
