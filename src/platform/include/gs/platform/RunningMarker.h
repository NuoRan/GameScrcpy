#pragma once

#include <string_view>

namespace gs::platform {

class RunningMarker
{
public:
    explicit RunningMarker(std::wstring_view name);
    ~RunningMarker();
    RunningMarker(const RunningMarker&) = delete;
    RunningMarker& operator=(const RunningMarker&) = delete;

    [[nodiscard]] bool held() const noexcept { return m_local != nullptr; }

private:
    void* m_local = nullptr;
    void* m_global = nullptr;
};

[[nodiscard]] bool runningMarkerPresent(std::wstring_view name) noexcept;

}
