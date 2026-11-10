#include "PhoneStatus.h"

#include <QStringList>

#include <algorithm>

namespace gs::app {

PhoneStatus PhoneStatus::of(const protocol::DeviceStatus& message)
{
    using protocol::DeviceStatus;
    PhoneStatus status;
    if (message.batteryPercent != DeviceStatus::kUnknownBattery) {
        status.batteryPercent = std::min<int>(message.batteryPercent, DeviceStatus::kMaxBattery);
    }
    status.charging = message.charging;
    if (message.temperatureDeciC != DeviceStatus::kUnknownTemperature) {
        status.temperatureDeciC = message.temperatureDeciC;
    }
    if (message.cpuPermille != DeviceStatus::kUnknownCpu) {
        status.cpuPermille = std::min<int>(message.cpuPermille, DeviceStatus::kMaxCpu);
    }
    return status;
}

QString PhoneStatus::text() const
{
    QStringList parts;
    if (batteryPercent) {
        parts.append(tr("%1%").arg(*batteryPercent));
    }
    if (charging) {
        parts.append(tr("Charging"));
    }
    if (temperatureDeciC) {
        parts.append(tr("%1 °C").arg(*temperatureDeciC / 10.0, 0, 'f', 1));
    }
    if (cpuPermille) {
        parts.append(tr("CPU %1%").arg((*cpuPermille + 5) / 10));
    }
    return parts.join(QStringLiteral(" · "));
}

}
