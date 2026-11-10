#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace gs::platform {

enum class WhenBusy : std::uint8_t { Wait, GiveUp };

[[nodiscard]] std::error_code writeFileAtomically(const std::filesystem::path& target,
                                                  std::string_view contents) noexcept;
[[nodiscard]] std::error_code readFile(const std::filesystem::path& file, std::size_t mostBytes, std::string& contents,
                                       WhenBusy busy = WhenBusy::Wait) noexcept;
[[nodiscard]] bool fileBusy(const std::error_code& error) noexcept;
[[nodiscard]] bool fileMissing(const std::error_code& error) noexcept;
[[nodiscard]] std::error_code flushFile(const std::filesystem::path& file) noexcept;
[[nodiscard]] std::error_code flushTree(const std::filesystem::path& directory) noexcept;
[[nodiscard]] std::error_code replaceFile(const std::filesystem::path& from,
                                          const std::filesystem::path& to) noexcept;
[[nodiscard]] std::error_code removeFile(const std::filesystem::path& file) noexcept;

}
