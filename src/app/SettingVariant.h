#pragma once

#include "Settings.h"

#include <QVariant>

#include <optional>

namespace gs::app {

[[nodiscard]] QVariant toVariant(const settings::Value& value);
[[nodiscard]] std::optional<settings::Value> fromVariant(const settings::Entry& entry, const QVariant& value);

}
