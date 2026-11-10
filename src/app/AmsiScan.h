#pragma once

#include <gs/ext/NativeCheck.h>

#include <string_view>

namespace gs::app {

[[nodiscard]] ext::ScanVerdict scanWithAmsi(std::string_view bytes, std::string_view name);

}
