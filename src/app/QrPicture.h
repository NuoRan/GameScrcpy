#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace gs::app {

struct QrPicture {
    int size = 0;
    std::string path;

    friend bool operator==(const QrPicture&, const QrPicture&) = default;
};

[[nodiscard]] QrPicture qrPicture(std::string_view text);

[[nodiscard]] std::string darkModulesPath(int size, const std::function<bool(int x, int y)>& dark);

}
