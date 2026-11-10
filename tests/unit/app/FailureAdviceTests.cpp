#include "FailureAdvice.h"
#include "PhoneVendors.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <utility>

using namespace gs;
using gs::app::FailureAdvice;

TEST_CASE("every failure says what happened and what to do (SES-001)")
{
    for (auto kind = static_cast<std::uint8_t>(session::FailureKind::AdbUnavailable);
         kind <= static_cast<std::uint8_t>(session::FailureKind::DirectUnreachable); ++kind) {
        session::SessionFailure failure;
        failure.kind = static_cast<session::FailureKind>(kind);
        const auto advice = FailureAdvice::forFailure(failure);
        CAPTURE(kind);
        CHECK_FALSE(advice.problem.isEmpty());
        CHECK_FALSE(advice.action.isEmpty());
        const auto overWifi = FailureAdvice::forFailure(failure, true);
        CHECK_FALSE(overWifi.action.isEmpty());
    }
}

TEST_CASE("a phone over wireless adb is never asked to replug a cable (RC1 D-014)")
{
    for (const session::FailureKind kind : {session::FailureKind::DeviceOffline, session::FailureKind::DeviceNotFound}) {
        session::SessionFailure failure;
        failure.kind = kind;
        CAPTURE(static_cast<int>(kind));
        const auto wired = FailureAdvice::forFailure(failure);
        const auto wireless = FailureAdvice::forFailure(failure, true);
        CHECK(wired.action.contains(QStringLiteral("cable")));
        CHECK_FALSE(wireless.action.contains(QStringLiteral("cable")));
        CHECK(wireless.action.contains(QStringLiteral("same network")));
        CHECK(wireless.problem == wired.problem);
    }
}

TEST_CASE("while resuming, the advice keeps the problem but does not ask to connect again")
{
    session::SessionFailure failure;
    failure.kind = session::FailureKind::ConnectionLost;
    const auto resuming = FailureAdvice::forReconnecting(failure);
    CHECK(resuming.problem == FailureAdvice::forFailure(failure).problem);
    CHECK(resuming.action.contains(QStringLiteral("by itself")));
    CHECK_FALSE(resuming.action.contains(QStringLiteral("connect again")));
}

TEST_CASE("video without a GPU says why and what it costs (R12)")
{
    const auto advice = FailureAdvice::forSoftwareRendering(QStringLiteral("no hardware Direct3D 11 device"));
    CHECK(advice.problem.contains(QStringLiteral("graphics card")));
    CHECK(advice.action.contains(QStringLiteral("no hardware Direct3D 11 device")));
    CHECK(advice.action.contains(QStringLiteral("stutter")));
}

TEST_CASE("a missing server package asks for a reinstall, not for anything on the phone")
{
    const auto advice = FailureAdvice::forMissingPackage();
    CHECK(advice.problem.contains(QStringLiteral("installed")));
    CHECK(advice.action.contains(QStringLiteral("gs-server.apk")));
    CHECK(advice.action.contains(QStringLiteral("Reinstall")));
}

TEST_CASE("a failed wireless connection is told by the system's error code in adb's answer (UI-005)")
{
    const auto rejected = [](std::string message) {
        return FailureAdvice::forWirelessConnect(adb::AdbError{adb::AdbErrorKind::Rejected, std::move(message)});
    };
    CHECK(rejected("cannot connect to 192.168.31.68:5555: \xe7\x94\xb1\xe4\xba\x8e (10061)")
              .problem.contains(QStringLiteral("refused")));
    CHECK(rejected("cannot connect to 192.0.2.1:5555: \xe7\x94\xb1\xe4\xba\x8e (10060)")
              .problem.contains(QStringLiteral("No answer")));
    CHECK(rejected("cannot resolve host 'abc' and port 5555:  (11001)").problem.contains(QStringLiteral("found")));
    CHECK(rejected("empty address").problem.contains(QStringLiteral("Enter")));
    CHECK(rejected("failed to authenticate to 192.168.1.5:41234").action.contains(QStringLiteral("Pair")));
    const auto other = rejected("something unforeseen (abc)");
    CHECK(other.problem.contains(QStringLiteral("Could not connect")));
    CHECK_FALSE(other.action.isEmpty());
    CHECK(FailureAdvice::forWirelessConnect(adb::AdbError{adb::AdbErrorKind::ServerUnavailable, "no adb server"})
              .problem.contains(QStringLiteral("adb")));
}

TEST_CASE("a failed pairing says whether the dialog, the code or the address is to blame (UI-005)")
{
    const auto rejected = [](std::string message) {
        return FailureAdvice::forPairing(adb::AdbError{adb::AdbErrorKind::Rejected, std::move(message)});
    };
    CHECK(rejected("Failed: Unable to start pairing client.").problem.contains(QStringLiteral("pairing dialog")));
    CHECK(rejected("Failed: Wrong password or connection was dropped.").problem.contains(QStringLiteral("code")));
    CHECK(rejected("Invalid port while parsing address [notanaddress]").problem.contains(QStringLiteral("address")));
    const auto other = rejected("something unforeseen");
    CHECK(other.problem.contains(QStringLiteral("Could not pair")));
    CHECK_FALSE(other.action.isEmpty());
    CHECK(FailureAdvice::forPairing(adb::AdbError{adb::AdbErrorKind::Io, "connection closed"})
              .problem.contains(QStringLiteral("adb")));

    const auto byQr = FailureAdvice::forPairing(
        adb::AdbError{adb::AdbErrorKind::Rejected, "Failed: Wrong password or connection was dropped."}, true);
    CHECK(byQr.problem.contains(QStringLiteral("QR code")));
    CHECK(byQr.action.contains(QStringLiteral("Scan")));
    CHECK_FALSE(byQr.action.contains(QStringLiteral("digits")));
}

TEST_CASE("the server's error code refines the advice")
{
    session::SessionFailure failure;
    failure.kind = session::FailureKind::HandshakeRejected;
    failure.code = protocol::ErrorCode::VersionMismatch;
    CHECK(FailureAdvice::forFailure(failure).problem.contains(QStringLiteral("version")));
    failure.kind = session::FailureKind::ServerEnded;
    failure.code = protocol::ErrorCode::EncoderFailure;
    CHECK(FailureAdvice::forFailure(failure).problem.contains(QStringLiteral("encoder")));
}

TEST_CASE("WiFi direct failures say what to check and offer the adb tunnel instead (ADR-0055)")
{
    session::SessionFailure failure;
    failure.kind = session::FailureKind::DirectNoAddress;
    const auto noAddress = FailureAdvice::forFailure(failure);
    CHECK(noAddress.problem.contains(QStringLiteral("address")));
    CHECK(noAddress.action.contains(QStringLiteral("wireless debugging")));
    CHECK(noAddress.action.contains(QStringLiteral("adb tunnel")));
    failure.kind = session::FailureKind::DirectUnreachable;
    const auto unreachable = FailureAdvice::forFailure(failure);
    CHECK(unreachable.problem.contains(QStringLiteral("WiFi direct")));
    CHECK(unreachable.action.contains(QStringLiteral("AP isolation")));
    CHECK(unreachable.action.contains(QStringLiteral("security software")));
    CHECK(unreachable.action.contains(QStringLiteral("adb tunnel")));
}

TEST_CASE("every phase has a text")
{
    for (auto phase = static_cast<std::uint8_t>(session::SessionPhase::Idle);
         phase <= static_cast<std::uint8_t>(session::SessionPhase::Failed); ++phase) {
        CAPTURE(phase);
        CHECK_FALSE(FailureAdvice::phase(static_cast<session::SessionPhase>(phase)).isEmpty());
    }
}

TEST_CASE("refused injection gets the manufacturer's developer option (R6, SRV-012)")
{
    const auto action = [](std::string_view manufacturer) {
        const auto advice = FailureAdvice::forRefusal(protocol::ErrorCode::InjectionDenied, manufacturer);
        REQUIRE(advice);
        CHECK_FALSE(advice->problem.isEmpty());
        return advice->action;
    };
    CHECK(action("Xiaomi").contains(QStringLiteral("USB debugging (Security settings)")));
    CHECK(action("XIAOMI").contains(QStringLiteral("Mi account")));
    CHECK(action("Redmi").contains(QStringLiteral("USB debugging (Security settings)")));
    CHECK(action("OnePlus").contains(QStringLiteral("Disable permission monitoring")));
    CHECK(action("realme").contains(QStringLiteral("Disable system optimization")));
    CHECK(action("vivo").contains(QStringLiteral("USB simulate click")));
    CHECK(action("HONOR").contains(QStringLiteral("extra developer option")));
    CHECK(action("HUAWEI").contains(QStringLiteral("extra developer option")));
    CHECK(action("Google").contains(QStringLiteral("extra developer option")));
    CHECK(action("").contains(QStringLiteral("extra developer option")));
}

TEST_CASE("the setup guide and the advice read one vendor table, matched by the phone's manufacturer")
{
    using gs::app::PhoneVendors;
    CHECK(PhoneVendors::forManufacturer("Xiaomi").id == "xiaomi");
    CHECK(PhoneVendors::forManufacturer("POCO").id == "xiaomi");
    CHECK(PhoneVendors::forManufacturer("OnePlus").id == "oppo");
    CHECK(PhoneVendors::forManufacturer("HONOR").id == "honor");
    CHECK(PhoneVendors::forManufacturer("samsung").id == "samsung");
    CHECK(PhoneVendors::forManufacturer("Google").id == "other");
    CHECK(PhoneVendors::forManufacturer("").id == "other");
    CHECK(PhoneVendors::all().back().id == "other");
    for (const gs::app::PhoneVendor& vendor : PhoneVendors::all()) {
        const std::string id(vendor.id);
        CAPTURE(id);
        CHECK_FALSE(PhoneVendors::text(vendor.name).isEmpty());
        CHECK(PhoneVendors::text(vendor.aboutPath).startsWith(QStringLiteral("Settings > ")));
        CHECK_FALSE(PhoneVendors::text(vendor.versionLabel).isEmpty());
        CHECK(PhoneVendors::text(vendor.developerPath).endsWith(QStringLiteral("Developer options")));
        CHECK((vendor.inputNote.source == nullptr || vendor.inputSwitch.source != nullptr));
    }
}

TEST_CASE("refused clipboard access and failed audio capture are explained, other codes are not refusals")
{
    const auto clipboard = FailureAdvice::forRefusal(protocol::ErrorCode::ClipboardDenied, "Xiaomi");
    REQUIRE(clipboard);
    CHECK(clipboard->problem.contains(QStringLiteral("clipboard")));
    const auto audio = FailureAdvice::forRefusal(protocol::ErrorCode::AudioCaptureFailed, "Google");
    REQUIRE(audio);
    CHECK(audio->problem.contains(QStringLiteral("sound")));
    CHECK(audio->action.contains(QStringLiteral("Unlock")));
    CHECK_FALSE(FailureAdvice::forAudioStatus(protocol::AudioStatus::Available));
    const auto old = FailureAdvice::forAudioStatus(protocol::AudioStatus::UnsupportedAndroid);
    REQUIRE(old);
    CHECK(old->action.contains(QStringLiteral("Android 11")));
    const auto failed = FailureAdvice::forAudioStatus(protocol::AudioStatus::CaptureFailed);
    REQUIRE(failed);
    CHECK(failed->problem == audio->problem);
    CHECK_FALSE(FailureAdvice::forRefusal(protocol::ErrorCode::PermissionDenied, "Xiaomi"));
    CHECK_FALSE(FailureAdvice::forRefusal(protocol::ErrorCode::EncoderFailure, "Xiaomi"));
}

TEST_CASE("quality given up for a slow network is explained, and its return needs no words (ADR-0054)")
{
    const auto reduced = FailureAdvice::forRefusal(protocol::ErrorCode::VideoReduced, "Google");
    REQUIRE(reduced);
    CHECK(reduced->problem.contains(QStringLiteral("network")));
    CHECK(reduced->action.contains(QStringLiteral("comes back by itself")));
    CHECK_FALSE(FailureAdvice::forRefusal(protocol::ErrorCode::VideoRestored, "Google"));
}
