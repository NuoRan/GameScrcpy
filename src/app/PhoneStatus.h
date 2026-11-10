#pragma once

#include <gs/protocol/Messages.h>

#include <QCoreApplication>
#include <QString>

#include <optional>

namespace gs::app {

struct PhoneStatus {
    std::optional<int> batteryPercent;
    bool charging = false;
    std::optional<int> temperatureDeciC;
    std::optional<int> cpuPermille;

    [[nodiscard]] static PhoneStatus of(const protocol::DeviceStatus& message);
    [[nodiscard]] QString text() const;

    friend bool operator==(const PhoneStatus&, const PhoneStatus&) = default;

    Q_DECLARE_TR_FUNCTIONS(PhoneStatus)
};

}
