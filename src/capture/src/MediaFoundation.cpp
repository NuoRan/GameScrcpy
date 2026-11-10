#include "MediaFoundation.h"

#include <mfapi.h>

#include <algorithm>
#include <chrono>
#include <format>

namespace gs::capture {

ComScope::ComScope() : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}

ComScope::~ComScope()
{
    if (SUCCEEDED(m_result)) {
        CoUninitialize();
    }
}

MediaFoundation::MediaFoundation() : m_result(MFStartup(MF_VERSION, MFSTARTUP_LITE)) {}

MediaFoundation::~MediaFoundation()
{
    if (SUCCEEDED(m_result)) {
        MFShutdown();
    }
}

Event::Event() : m_handle(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}

Event::~Event()
{
    if (m_handle != nullptr) {
        CloseHandle(m_handle);
    }
}

namespace {

bool fits(const LockedPlanes& planes, Layout layout, int width, int height) noexcept
{
    const auto rows = static_cast<std::ptrdiff_t>(height);
    switch (layout) {
    case Layout::Bytes:
        return planes.length > 0;
    case Layout::Nv12:
        return planes.pitch >= width &&
               static_cast<std::size_t>(planes.chromaOffset + planes.pitch * ((rows + 1) / 2)) <= planes.length;
    case Layout::Yuy2:
        return planes.pitch >= 2 * static_cast<std::ptrdiff_t>(width) &&
               static_cast<std::size_t>(planes.pitch * rows) <= planes.length;
    }
    return false;
}

std::ptrdiff_t chromaOffsetOf(Layout layout, std::size_t length, std::ptrdiff_t pitch, int height) noexcept
{
    if (layout != Layout::Nv12 || pitch <= 0) {
        return 0;
    }
    const auto padded = static_cast<std::ptrdiff_t>(length * 2 / (3 * static_cast<std::size_t>(pitch)));
    return pitch * std::max(padded, static_cast<std::ptrdiff_t>(height));
}

}

BufferLock::BufferLock(IMFMediaBuffer* buffer, Layout layout, int width, int height)
{
    if (buffer == nullptr) {
        return;
    }
    if (layout != Layout::Bytes && SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&m_twoD)))) {
        BYTE* scanline = nullptr;
        LONG pitch = 0;
        DWORD length = 0;
        if (SUCCEEDED(m_twoD->Lock2D(&scanline, &pitch))) {
            if (pitch > 0 && SUCCEEDED(m_twoD->GetContiguousLength(&length))) {
                m_planes.data = scanline;
                m_planes.pitch = pitch;
                m_planes.length = length;
                m_planes.chromaOffset = chromaOffsetOf(layout, length, pitch, height);
                if (fits(m_planes, layout, width, height)) {
                    m_locked = true;
                    return;
                }
            }
            m_twoD->Unlock2D();
        }
        m_twoD.Reset();
    }
    BYTE* data = nullptr;
    DWORD most = 0;
    DWORD current = 0;
    if (FAILED(buffer->Lock(&data, &most, &current))) {
        return;
    }
    m_buffer = buffer;
    const std::ptrdiff_t pitch = layout == Layout::Yuy2 ? 2 * static_cast<std::ptrdiff_t>(width) : width;
    m_planes = {data, pitch, chromaOffsetOf(layout, current, pitch, height), current};
    m_locked = fits(m_planes, layout, width, height);
}

BufferLock::~BufferLock()
{
    if (m_twoD) {
        m_twoD->Unlock2D();
    } else if (m_buffer) {
        m_buffer->Unlock();
    }
}

std::string utf8Of(std::wstring_view text)
{
    if (text.empty()) {
        return {};
    }
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(size, 0)), '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr,
                            nullptr);
    }
    return out;
}

std::string failureText(const char* what, HRESULT result)
{
    return std::format("{} (0x{:08X})", what, static_cast<unsigned>(result));
}

std::int64_t steadyMicros() noexcept
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}
