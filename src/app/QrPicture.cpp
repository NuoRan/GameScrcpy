#include "QrPicture.h"

#include <qrcodegen.hpp>

#include <format>

namespace gs::app {

QrPicture qrPicture(std::string_view text)
{
    const std::string terminated(text);
    try {
        const qrcodegen::QrCode code = qrcodegen::QrCode::encodeText(terminated.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        const int size = code.getSize();
        return {size, darkModulesPath(size, [&code](int x, int y) { return code.getModule(x, y); })};
    } catch (const qrcodegen::data_too_long&) {
        return {};
    }
}

std::string darkModulesPath(int size, const std::function<bool(int x, int y)>& dark)
{
    std::string path;
    for (int y = 0; y < size; ++y) {
        int x = 0;
        while (x < size) {
            if (!dark(x, y)) {
                ++x;
                continue;
            }
            int end = x + 1;
            while (end < size && dark(end, y)) {
                ++end;
            }
            path += std::format("M{} {}h{}v1h-{}z", x, y, end - x, end - x);
            x = end;
        }
    }
    return path;
}

}
