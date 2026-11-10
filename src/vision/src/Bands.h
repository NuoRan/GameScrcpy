#pragma once

#include <functional>

namespace gs::vision::detail {

[[nodiscard]] int bandRows(int rows, int leastRows) noexcept;

void runBands(int bands, const std::function<void(int)>& job);

}
