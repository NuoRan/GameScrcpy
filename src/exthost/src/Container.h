#pragma once

#include <windows.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace gs::exthost::detail {

[[nodiscard]] std::wstring containerName(std::string_view extension);
[[nodiscard]] PSID containerSid(std::string_view extension, std::string& problem);
[[nodiscard]] bool openToContainers(const std::filesystem::path& program, std::string& problem);
[[nodiscard]] bool openToContainer(const std::filesystem::path& folder, const std::filesystem::path& dll, PSID sid,
                                   std::string& problem);
[[nodiscard]] HANDLE containerJob(bool restrictUi);
[[nodiscard]] std::wstring systemFolder();

}
