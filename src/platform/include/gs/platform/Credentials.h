#pragma once

#include <gs/base/Expected.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>

namespace gs::platform {

inline constexpr std::size_t kMostSecretBytes = 2560;

[[nodiscard]] std::error_code writeCredential(std::string_view target, std::string_view secret) noexcept;
[[nodiscard]] Expected<std::string, std::error_code> readCredential(std::string_view target) noexcept;
[[nodiscard]] std::error_code deleteCredential(std::string_view target) noexcept;
[[nodiscard]] bool credentialMissing(const std::error_code& error) noexcept;

}
