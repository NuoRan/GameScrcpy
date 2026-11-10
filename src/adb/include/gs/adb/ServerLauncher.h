#pragma once

#include "gs/adb/AdbClient.h"

#include <chrono>
#include <filesystem>

namespace gs::adb {

[[nodiscard]] AdbResult<int> ensureServer(const AdbClient& client, const std::filesystem::path& adbExecutable,
                                          std::chrono::milliseconds timeout = std::chrono::seconds(15));

}
