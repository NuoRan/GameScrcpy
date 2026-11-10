#include "FailureAdvice.h"

#include "PhoneVendors.h"

#include <charconv>
#include <optional>

namespace gs::app {

namespace {

std::optional<int> socketErrorOf(std::string_view message)
{
    const std::size_t open = message.rfind('(');
    if (open == std::string_view::npos || !message.ends_with(')')) {
        return std::nullopt;
    }
    const std::string_view digits = message.substr(open + 1, message.size() - open - 2);
    int code = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), code);
    if (error != std::errc{} || end != digits.data() + digits.size()) {
        return std::nullopt;
    }
    return code;
}

}

QString FailureAdvice::phase(session::SessionPhase phase)
{
    switch (phase) {
    case session::SessionPhase::Idle:
        return tr("Not connected");
    case session::SessionPhase::Deploying:
        return tr("Preparing the device");
    case session::SessionPhase::Launching:
        return tr("Starting the server");
    case session::SessionPhase::Connecting:
        return tr("Connecting");
    case session::SessionPhase::Handshaking:
        return tr("Verifying the connection");
    case session::SessionPhase::Streaming:
        return tr("Connected");
    case session::SessionPhase::Reconnecting:
        return tr("Connection lost, reconnecting");
    case session::SessionPhase::Stopping:
        return tr("Disconnecting");
    case session::SessionPhase::Stopped:
        return tr("Disconnected");
    case session::SessionPhase::Failed:
        return tr("Could not connect");
    }
    return {};
}

Advice FailureAdvice::forFailure(const session::SessionFailure& failure, bool wireless)
{
    using session::FailureKind;
    switch (failure.kind) {
    case FailureKind::AdbUnavailable:
        return {tr("adb is not available"),
                tr("Install Android SDK Platform-Tools, or make sure no other program blocks adb, then try again.")};
    case FailureKind::DeviceNotFound:
        if (wireless) {
            return {tr("The device is not connected"),
                    tr("Make sure the phone is on the same network as this computer and wireless debugging is still "
                       "on, then connect it again under Wireless debugging.")};
        }
        return {tr("The device is not connected"),
                tr("Reconnect the USB cable and make sure USB debugging is on.")};
    case FailureKind::DeviceUnauthorized:
        return {tr("USB debugging is not allowed yet"),
                tr("Unlock the phone and tap Allow in the USB debugging prompt; tick “Always allow from this "
                   "computer”.")};
    case FailureKind::DeviceOffline:
        if (wireless) {
            return {tr("The device is offline"),
                    tr("Make sure the phone is on the same network as this computer and its screen is on, then try "
                       "again. If it stays offline, turn wireless debugging off and on again on the phone.")};
        }
        return {tr("The device is offline"),
                tr("Unplug and replug the cable. If it stays offline, turn USB debugging off and on again.")};
    case FailureKind::UnsupportedAndroid:
        return {tr("This Android version is too old"), tr("GameScrcpy needs Android 9 or newer.")};
    case FailureKind::DeployFailed:
        return {tr("Could not install the server component"),
                tr("Make sure the phone has free storage. If the problem remains, reinstall GameScrcpy.")};
    case FailureKind::LaunchFailed:
        return {tr("The server on the phone did not start"), tr("Restart the phone and try again.")};
    case FailureKind::TunnelFailed:
        return {tr("Could not open a connection to the device"),
                tr("Reconnect the device. If that does not help, restart adb.")};
    case FailureKind::ConnectFailed:
        return {tr("Could not reach the server on the phone"),
                tr("Try again. If it keeps failing, restart the phone.")};
    case FailureKind::HandshakeRejected:
        if (failure.code == protocol::ErrorCode::VersionMismatch) {
            return {tr("The server on the phone is a different version"),
                    tr("Update GameScrcpy; the matching server is installed automatically.")};
        }
        return {tr("The phone refused the connection"), tr("Try again.")};
    case FailureKind::ProtocolError:
        return {tr("Communication with the phone failed"), tr("Update GameScrcpy and try again.")};
    case FailureKind::Timeout:
        return {tr("The phone did not respond in time"), tr("Unlock the phone, keep the screen on and try again.")};
    case FailureKind::ConnectionLost:
        return {tr("The connection was lost"), tr("Check the cable or the network, then connect again.")};
    case FailureKind::ServerEnded:
        if (failure.code == protocol::ErrorCode::EncoderFailure) {
            return {tr("The phone's video encoder failed"), tr("Try a lower resolution or another video format.")};
        }
        return {tr("The phone ended the session"), tr("Connect again.")};
    case FailureKind::DirectNoAddress:
        return {tr("WiFi direct needs the phone's network address"),
                tr("Connect the phone over wireless debugging (its IP address and port), or switch the connection "
                   "to adb tunnel.")};
    case FailureKind::DirectUnreachable:
        return {tr("The phone did not answer over WiFi direct"),
                tr("Make sure the computer and the phone are on the same network, that the router does not isolate "
                   "its devices (AP isolation), and that no firewall or security software (such as Huorong or 360) "
                   "blocks GameScrcpy's UDP traffic; allow GameScrcpy there if it asks. Or switch the connection to "
                   "adb tunnel.")};
    }
    return {};
}

Advice FailureAdvice::forReconnecting(const session::SessionFailure& failure)
{
    return {forFailure(failure).problem,
            tr("Check the cable or the network. GameScrcpy reconnects by itself once the device is back.")};
}

std::optional<Advice> FailureAdvice::forRefusal(protocol::ErrorCode code, std::string_view manufacturer)
{
    switch (code) {
    case protocol::ErrorCode::InjectionDenied: {
        const QString problem = tr("The phone does not accept simulated input");
        const PhoneVendor& vendor = PhoneVendors::forManufacturer(manufacturer);
        if (vendor.inputSwitch.source != nullptr) {
            QString action = tr("In Developer options, turn on “%1”, then connect again.")
                                 .arg(PhoneVendors::text(vendor.inputSwitch));
            if (vendor.inputNote.source != nullptr) {
                action += QLatin1Char(' ') + PhoneVendors::text(vendor.inputNote);
            }
            return Advice{problem, action};
        }
        return Advice{problem, tr("Some phones only accept simulated input after an extra developer option is "
                                  "turned on, such as “USB debugging (Security settings)”. Turn it on and connect "
                                  "again.")};
    }
    case protocol::ErrorCode::ClipboardDenied:
        return Advice{tr("The phone does not allow clipboard access"),
                      tr("Characters beyond the keyboard, such as Chinese, cannot be typed. Check whether the "
                         "phone restricts clipboard access.")};
    case protocol::ErrorCode::AudioCaptureFailed:
        return Advice{tr("The phone's sound cannot be captured"),
                      tr("The picture works without sound. Unlock the phone and connect again; Android 11 only "
                         "lets the sound be captured while the phone is unlocked.")};
    case protocol::ErrorCode::VideoReduced:
        return Advice{tr("The network is too slow for the full picture"),
                      tr("To keep the delay low, the resolution or the frame rate was lowered. It comes back by "
                         "itself once the network recovers.")};
    default:
        return std::nullopt;
    }
}

std::optional<Advice> FailureAdvice::forAudioStatus(protocol::AudioStatus status)
{
    switch (status) {
    case protocol::AudioStatus::Available:
        return std::nullopt;
    case protocol::AudioStatus::UnsupportedAndroid:
        return Advice{tr("No sound from this phone"),
                      tr("Sharing the phone's sound needs Android 11 or later. The picture and the controls work "
                         "as usual.")};
    case protocol::AudioStatus::CaptureFailed:
        return forRefusal(protocol::ErrorCode::AudioCaptureFailed, {});
    }
    return std::nullopt;
}

Advice FailureAdvice::forSoftwareRendering(const QString& reason)
{
    return {tr("Video runs without a graphics card"),
            tr("No usable graphics card was found (%1), as happens in virtual machines and remote desktops. "
               "The processor draws and decodes the picture, which may stutter; an up-to-date graphics driver "
               "helps.")
                .arg(reason)};
}

Advice FailureAdvice::forMissingPackage()
{
    return {tr("GameScrcpy is not installed completely"),
            tr("The server component (gs-server.apk) is missing. Reinstall GameScrcpy.")};
}

Advice FailureAdvice::forWirelessConnect(const adb::AdbError& error)
{
    if (error.kind != adb::AdbErrorKind::Rejected) {
        return {tr("adb did not answer"), tr("Try again once adb is running.")};
    }
    const std::string_view message = error.message;
    if (message == "empty address") {
        return {tr("Enter an address"), tr("Such as 192.168.1.5:5555.")};
    }
    if (message.find("authenticate") != std::string_view::npos) {
        return {tr("The phone did not accept this computer"),
                tr("Pair it first: on the phone, Wireless debugging > Pair device with pairing code.")};
    }
    switch (socketErrorOf(message).value_or(0)) {
    case 11001:
        return {tr("No phone can be found at that address"),
                tr("Enter the IP address and port that the phone's Wireless debugging screen shows, such as "
                   "192.168.1.5:41234.")};
    case 10061:
        return {tr("The phone refused the connection"),
                tr("Make sure Wireless debugging is on, and use the IP address and port on its main screen, not "
                   "those for pairing. Phones before Android 11 need “adb tcpip 5555” over USB first.")};
    case 10060:
    case 10065:
    case 10051:
        return {tr("No answer from that address"),
                tr("Check that the phone and this computer are on the same network and that the IP address is "
                   "current. Some routers keep their devices apart.")};
    default:
        return {tr("Could not connect"), tr("Check the address and that Wireless debugging is on, then try again.")};
    }
}

Advice FailureAdvice::forPairing(const adb::AdbError& error, bool byQrCode)
{
    if (error.kind != adb::AdbErrorKind::Rejected) {
        return {tr("adb did not answer"), tr("Try again once adb is running.")};
    }
    const std::string_view message = error.message;
    if (message.find("Unable to start pairing client") != std::string_view::npos) {
        return {tr("Could not reach the phone's pairing dialog"),
                tr("Keep the pairing dialog open on the phone, and make sure the phone and this computer are on "
                   "the same network.")};
    }
    if (message.find("Wrong password") != std::string_view::npos && byQrCode) {
        return {tr("The phone did not accept the QR code"), tr("Scan the code shown here again with the phone.")};
    }
    if (message.find("Wrong password") != std::string_view::npos) {
        return {tr("The pairing code was not accepted"),
                tr("Type the six digits the phone shows now; it shows a new code each time the pairing dialog "
                   "opens.")};
    }
    if (message.find("Invalid port") != std::string_view::npos || message.find("parse") != std::string_view::npos) {
        return {tr("That address is not valid"),
                tr("Enter the IP address and port that the pairing dialog shows, such as 192.168.1.5:37123.")};
    }
    return {tr("Could not pair"), tr("Open the pairing dialog on the phone again, then retry.")};
}

}
