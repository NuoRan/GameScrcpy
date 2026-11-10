#pragma once

#include "gs/scripthost/RemoteLogicHost.h"

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>

namespace gs::scripthost::detail {

inline constexpr std::size_t kJobMemoryBytes = std::size_t{256} << 20;

struct Launched {
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    HANDLE toChild = nullptr;
    HANDLE fromChild = nullptr;
    DWORD id = 0;
    SandboxMode mode = SandboxMode::None;
};

[[nodiscard]] std::optional<Launched> launch(const std::filesystem::path& program, SandboxMode mode,
                                             const std::wstring& extra, std::string& problem);
void closeLaunched(Launched& launched) noexcept;

}
