#pragma once

#include <QString>

#include <string_view>

namespace gs::app {

[[nodiscard]] inline QString toQString(std::string_view utf8)
{
    return QString::fromUtf8(utf8.data(), static_cast<qsizetype>(utf8.size()));
}

}
