#pragma once

#include <windows.h>

#include <mfobjects.h>
#include <objbase.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace gs::capture {

class ComScope
{
public:
    ComScope();
    ~ComScope();
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

    [[nodiscard]] bool ready() const noexcept { return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE; }

private:
    HRESULT m_result;
};

class MediaFoundation
{
public:
    MediaFoundation();
    ~MediaFoundation();
    MediaFoundation(const MediaFoundation&) = delete;
    MediaFoundation& operator=(const MediaFoundation&) = delete;

    [[nodiscard]] bool ready() const noexcept { return SUCCEEDED(m_result); }

private:
    HRESULT m_result;
};

class Event
{
public:
    Event();
    ~Event();
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return m_handle; }

private:
    HANDLE m_handle;
};

struct LockedPlanes {
    const std::uint8_t* data = nullptr;
    std::ptrdiff_t pitch = 0;
    std::ptrdiff_t chromaOffset = 0;
    std::size_t length = 0;
};

enum class Layout : std::uint8_t { Bytes, Nv12, Yuy2 };

class BufferLock
{
public:
    BufferLock(IMFMediaBuffer* buffer, Layout layout, int width, int height);
    ~BufferLock();
    BufferLock(const BufferLock&) = delete;
    BufferLock& operator=(const BufferLock&) = delete;

    [[nodiscard]] bool locked() const noexcept { return m_locked; }
    [[nodiscard]] const LockedPlanes& planes() const noexcept { return m_planes; }

private:
    Microsoft::WRL::ComPtr<IMFMediaBuffer> m_buffer;
    Microsoft::WRL::ComPtr<IMF2DBuffer> m_twoD;
    LockedPlanes m_planes;
    bool m_locked = false;
};

[[nodiscard]] std::string utf8Of(std::wstring_view text);
[[nodiscard]] std::string failureText(const char* what, HRESULT result);
[[nodiscard]] std::int64_t steadyMicros() noexcept;

}
