#pragma once

#include <gs/adb/AdbClient.h>
#include <gs/protocol/Enums.h>
#include <gs/session/SessionState.h>

#include <QCoreApplication>
#include <QString>

#include <optional>
#include <string_view>

namespace gs::app {

struct Advice {
    QString problem;
    QString action;
};

class FailureAdvice
{
    Q_DECLARE_TR_FUNCTIONS(FailureAdvice)

public:
    [[nodiscard]] static QString phase(session::SessionPhase phase);
    [[nodiscard]] static Advice forFailure(const session::SessionFailure& failure, bool wireless = false);
    [[nodiscard]] static Advice forReconnecting(const session::SessionFailure& failure);
    [[nodiscard]] static std::optional<Advice> forRefusal(protocol::ErrorCode code, std::string_view manufacturer);
    [[nodiscard]] static std::optional<Advice> forAudioStatus(protocol::AudioStatus status);
    [[nodiscard]] static Advice forMissingPackage();
    [[nodiscard]] static Advice forSoftwareRendering(const QString& reason);
    [[nodiscard]] static Advice forWirelessConnect(const adb::AdbError& error);
    [[nodiscard]] static Advice forPairing(const adb::AdbError& error, bool byQrCode = false);
};

}
