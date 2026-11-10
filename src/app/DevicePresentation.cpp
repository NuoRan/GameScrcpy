#include "DevicePresentation.h"

namespace gs::app {

namespace {

using session::SessionPhase;

int stageOf(SessionPhase phase)
{
    switch (phase) {
    case SessionPhase::Deploying:
        return 0;
    case SessionPhase::Launching:
        return 1;
    case SessionPhase::Connecting:
        return 2;
    case SessionPhase::Handshaking:
        return 3;
    default:
        return -1;
    }
}

}

DevicePresentation DevicePresentation::of(SessionPhase phase, const std::optional<session::SessionFailure>& failure,
                                          bool listed, adb::DeviceState state)
{
    switch (phase) {
    case SessionPhase::Streaming:
        return {tr("Mirroring"), Tone::Live, -1, false, 0};
    case SessionPhase::Reconnecting:
        return {tr("Reconnecting"), Tone::Warn, -1, false, 0};
    case SessionPhase::Deploying:
    case SessionPhase::Launching:
    case SessionPhase::Connecting:
    case SessionPhase::Handshaking:
        return {tr("Connecting"), Tone::Warn, stageOf(phase), false, 1};
    case SessionPhase::Stopping:
        return {tr("Disconnecting"), Tone::Idle, -1, false, 1};
    case SessionPhase::Failed:
        return {tr("Could not connect"), Tone::Bad, failure ? stageOf(failure->phase) : -1, true, 3};
    case SessionPhase::Idle:
    case SessionPhase::Stopped:
        break;
    }
    if (!listed) {
        return {tr("Not detected"), Tone::Idle, -1, false, 4};
    }
    switch (state) {
    case adb::DeviceState::Device:
        return {tr("Online"), Tone::Ok, -1, false, 2};
    case adb::DeviceState::Unauthorized:
        return {tr("Waiting for authorization"), Tone::Warn, -1, false, 3};
    case adb::DeviceState::Offline:
        return {tr("Offline"), Tone::Bad, -1, false, 4};
    default:
        return {tr("Unavailable"), Tone::Idle, -1, false, 4};
    }
}

QString DevicePresentation::toneName(Tone tone)
{
    switch (tone) {
    case Tone::Live:
        return QStringLiteral("live");
    case Tone::Ok:
        return QStringLiteral("ok");
    case Tone::Warn:
        return QStringLiteral("warn");
    case Tone::Bad:
        return QStringLiteral("bad");
    case Tone::Idle:
        break;
    }
    return QStringLiteral("idle");
}

DevicePresentation DevicePresentation::ofAoa(SessionPhase phase, const std::optional<session::SessionFailure>& failure,
                                             bool listed, bool driverReady)
{
    if (phase != SessionPhase::Idle && phase != SessionPhase::Stopped) {
        return of(phase, failure, listed, adb::DeviceState::Device);
    }
    if (!listed) {
        return of(phase, failure, false, adb::DeviceState::Unknown);
    }
    if (!driverReady) {
        return {tr("Needs a driver"), Tone::Warn, -1, false, 3};
    }
    return of(phase, failure, true, adb::DeviceState::Device);
}

QString DevicePresentation::nameOf(const QString& marketName, const QString& manufacturer, const QString& model)
{
    if (!marketName.isEmpty()) {
        return marketName;
    }
    if (manufacturer.isEmpty() || model.isEmpty() || model.startsWith(manufacturer, Qt::CaseInsensitive)) {
        return model;
    }
    return manufacturer + u' ' + model;
}

QString DevicePresentation::serviceLabel(const QString& serviceName)
{
    const qsizetype last = serviceName.lastIndexOf(u'-');
    if (!serviceName.startsWith(u"adb-") || last <= 4 || last == serviceName.size() - 1) {
        return serviceName;
    }
    return serviceName.mid(4, last - 4);
}

}
