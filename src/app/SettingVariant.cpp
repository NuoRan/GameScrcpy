#include "SettingVariant.h"

#include <QMetaType>
#include <QString>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

namespace gs::app {

namespace {

std::optional<settings::Value> integerFrom(const QVariant& value)
{
    switch (value.typeId()) {
    case QMetaType::Int:
    case QMetaType::LongLong:
        return settings::Value{static_cast<std::int64_t>(value.toLongLong())};
    case QMetaType::UInt:
    case QMetaType::ULongLong: {
        const qulonglong number = value.toULongLong();
        if (number > static_cast<qulonglong>(std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
        return settings::Value{static_cast<std::int64_t>(number)};
    }
    case QMetaType::Double: {
        const double number = value.toDouble();
        constexpr auto lowest = static_cast<double>(std::numeric_limits<std::int64_t>::min());
        constexpr auto beyond = static_cast<double>(std::numeric_limits<std::int64_t>::max());
        if (!std::isfinite(number) || std::trunc(number) != number || number < lowest || number >= beyond) {
            return std::nullopt;
        }
        return settings::Value{static_cast<std::int64_t>(number)};
    }
    default:
        return std::nullopt;
    }
}

}

QVariant toVariant(const settings::Value& value)
{
    return std::visit(
        [](const auto& held) -> QVariant {
            using Held = std::decay_t<decltype(held)>;
            if constexpr (std::is_same_v<Held, std::string>) {
                return QString::fromStdString(held);
            } else if constexpr (std::is_same_v<Held, std::int64_t>) {
                return QVariant::fromValue(static_cast<qint64>(held));
            } else {
                return QVariant(held);
            }
        },
        value);
}

std::optional<settings::Value> fromVariant(const settings::Entry& entry, const QVariant& value)
{
    switch (entry.kind) {
    case settings::Kind::Boolean:
        if (value.typeId() == QMetaType::Bool) {
            return settings::Value{value.toBool()};
        }
        return std::nullopt;
    case settings::Kind::Integer:
        return integerFrom(value);
    case settings::Kind::Choice:
    case settings::Kind::Text:
        if (value.typeId() == QMetaType::QString) {
            return settings::Value{value.toString().toStdString()};
        }
        return std::nullopt;
    }
    return std::nullopt;
}

}
