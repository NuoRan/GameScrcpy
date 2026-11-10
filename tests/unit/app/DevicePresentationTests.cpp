#include "DevicePresentation.h"

#include <doctest/doctest.h>

#include <optional>

using gs::adb::DeviceState;
using gs::app::DevicePresentation;
using gs::session::FailureKind;
using gs::session::SessionFailure;
using gs::session::SessionPhase;
using Tone = DevicePresentation::Tone;

TEST_CASE("a mirroring device is live, first, with nothing to step through")
{
    const auto shown = DevicePresentation::of(SessionPhase::Streaming, std::nullopt, true, DeviceState::Device);
    CHECK(shown.tone == Tone::Live);
    CHECK(shown.stage == -1);
    CHECK_FALSE(shown.failed);
    CHECK(shown.rank == 0);
    CHECK(DevicePresentation::toneName(shown.tone) == "live");
}

TEST_CASE("connecting goes through the stepper's stages")
{
    const std::pair<SessionPhase, int> stages[] = {
        {SessionPhase::Deploying, 0},
        {SessionPhase::Launching, 1},
        {SessionPhase::Connecting, 2},
        {SessionPhase::Handshaking, 3},
    };
    for (const auto& [phase, stage] : stages) {
        const auto shown = DevicePresentation::of(phase, std::nullopt, true, DeviceState::Device);
        CHECK(shown.stage == stage);
        CHECK(shown.tone == Tone::Warn);
        CHECK(shown.rank == 1);
        CHECK_FALSE(shown.failed);
    }
}

TEST_CASE("a failure points at the stage it stopped in")
{
    SessionFailure failure;
    failure.kind = FailureKind::TunnelFailed;
    failure.phase = SessionPhase::Connecting;
    const auto shown = DevicePresentation::of(SessionPhase::Failed, failure, true, DeviceState::Device);
    CHECK(shown.failed);
    CHECK(shown.stage == 2);
    CHECK(shown.tone == Tone::Bad);
    CHECK(shown.rank == 3);

    failure.kind = FailureKind::ConnectionLost;
    failure.phase = SessionPhase::Streaming;
    CHECK(DevicePresentation::of(SessionPhase::Failed, failure, true, DeviceState::Device).stage == -1);
}

TEST_CASE("without a session, adb's state decides")
{
    const auto online = DevicePresentation::of(SessionPhase::Idle, std::nullopt, true, DeviceState::Device);
    CHECK(online.tone == Tone::Ok);
    CHECK(online.rank == 2);
    CHECK(DevicePresentation::of(SessionPhase::Stopped, std::nullopt, true, DeviceState::Device).tone == Tone::Ok);

    const auto unauthorized = DevicePresentation::of(SessionPhase::Idle, std::nullopt, true, DeviceState::Unauthorized);
    CHECK(unauthorized.tone == Tone::Warn);
    CHECK(unauthorized.rank == 3);

    const auto offline = DevicePresentation::of(SessionPhase::Idle, std::nullopt, true, DeviceState::Offline);
    CHECK(offline.tone == Tone::Bad);
    CHECK(offline.rank == 4);

    const auto gone = DevicePresentation::of(SessionPhase::Idle, std::nullopt, false, DeviceState::Device);
    CHECK(gone.tone == Tone::Idle);
    CHECK(gone.rank == 4);
    CHECK(gone.stage == -1);
}

TEST_CASE("a phone seen only over AOA is ready with its driver, asks for it without one, and follows its session "
          "(HID-003)")
{
    const auto ready = DevicePresentation::ofAoa(SessionPhase::Idle, std::nullopt, true, true);
    CHECK(ready.tone == Tone::Ok);
    CHECK(ready.rank == 2);
    CHECK(ready.pill == DevicePresentation::of(SessionPhase::Idle, std::nullopt, true, DeviceState::Device).pill);
    CHECK(DevicePresentation::ofAoa(SessionPhase::Stopped, std::nullopt, true, true).tone == Tone::Ok);

    const auto driverless = DevicePresentation::ofAoa(SessionPhase::Idle, std::nullopt, true, false);
    CHECK(driverless.tone == Tone::Warn);
    CHECK(driverless.rank == 3);
    CHECK_FALSE(driverless.failed);
    CHECK(driverless.pill != ready.pill);

    CHECK(DevicePresentation::ofAoa(SessionPhase::Idle, std::nullopt, false, true).tone == Tone::Idle);
    CHECK(DevicePresentation::ofAoa(SessionPhase::Streaming, std::nullopt, true, true).tone == Tone::Live);
    CHECK(DevicePresentation::ofAoa(SessionPhase::Connecting, std::nullopt, true, true).stage == 2);
    SessionFailure failure;
    failure.kind = FailureKind::ConnectionLost;
    failure.phase = SessionPhase::Streaming;
    CHECK(DevicePresentation::ofAoa(SessionPhase::Failed, failure, true, true).failed);
}

TEST_CASE("a device goes by its market name, else by its manufacturer and model")
{
    CHECK(DevicePresentation::nameOf("Redmi K70", "Xiaomi", "23113RKC6C") == "Redmi K70");
    CHECK(DevicePresentation::nameOf("", "Google", "Pixel 7") == "Google Pixel 7");
    CHECK(DevicePresentation::nameOf("", "samsung", "SM-S921B") == "samsung SM-S921B");
    CHECK(DevicePresentation::nameOf("", "OnePlus", "ONEPLUS A6003") == "ONEPLUS A6003");
    CHECK(DevicePresentation::nameOf("", "", "Pixel 7") == "Pixel 7");
    CHECK(DevicePresentation::nameOf("", "Google", "").isEmpty());
}

TEST_CASE("a phone found by mDNS goes by the serial in its service name")
{
    CHECK(DevicePresentation::serviceLabel("adb-R58M123ABC-AbCdEf") == "R58M123ABC");
    CHECK(DevicePresentation::serviceLabel("adb-HW-77AB-x1Y2z3") == "HW-77AB");
    CHECK(DevicePresentation::serviceLabel("adb-FAKE0001-AbCdEf") == "FAKE0001");
    CHECK(DevicePresentation::serviceLabel("Pixel 7") == "Pixel 7");
    CHECK(DevicePresentation::serviceLabel("adb-NOSUFFIX") == "adb-NOSUFFIX");
    CHECK(DevicePresentation::serviceLabel("adb--x") == "adb--x");
    CHECK(DevicePresentation::serviceLabel("adb-TRAILING-") == "adb-TRAILING-");
}
