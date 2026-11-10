#pragma once

#include <gs/adb/DeviceList.h>
#include <gs/session/SessionState.h>

#include <QCoreApplication>
#include <QString>

#include <optional>

namespace gs::app {

struct DevicePresentation {
    enum class Tone { Live, Ok, Warn, Bad, Idle };

    QString pill;
    Tone tone = Tone::Idle;
    int stage = -1;
    bool failed = false;
    int rank = 0;

    [[nodiscard]] static DevicePresentation of(session::SessionPhase phase,
                                               const std::optional<session::SessionFailure>& failure, bool listed,
                                               adb::DeviceState state);
    [[nodiscard]] static DevicePresentation ofAoa(session::SessionPhase phase,
                                                  const std::optional<session::SessionFailure>& failure, bool listed,
                                                  bool driverReady);
    [[nodiscard]] static QString toneName(Tone tone);
    [[nodiscard]] static QString nameOf(const QString& marketName, const QString& manufacturer, const QString& model);
    [[nodiscard]] static QString serviceLabel(const QString& serviceName);

    Q_DECLARE_TR_FUNCTIONS(DevicePresentation)
};

}
