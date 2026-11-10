#include "gs/hardware/Backend.h"
#include "gs/hardware/Devices.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <utility>

namespace gs::hardware {

namespace {

constexpr std::string_view kBleIdPrefix = "ble:";
constexpr std::string_view kBluetoothBase = "-0000-1000-8000-00805f9b34fb";
constexpr std::string_view kUnsupported = "this kind of device does not do that";
constexpr std::string_view kSerialPrefix = "\\\\.\\";

bool hexDigit(char c) noexcept
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

char lowered(char c) noexcept
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool allHex(std::string_view text) noexcept
{
    return !text.empty() && std::ranges::all_of(text, hexDigit);
}

std::string loweredText(std::string_view text)
{
    std::string out(text);
    std::ranges::transform(out, out.begin(), lowered);
    return out;
}

bool fullUuid(std::string_view text) noexcept
{
    if (text.size() != 36) {
        return false;
    }
    for (std::size_t at = 0; at < text.size(); ++at) {
        const bool dash = at == 8 || at == 13 || at == 18 || at == 23;
        if (dash ? text[at] != '-' : !hexDigit(text[at])) {
            return false;
        }
    }
    return true;
}

bool matchesFilter(const DeviceInfo& device, const Filter& filter)
{
    if (filter.vendorId && device.vendorId != *filter.vendorId) {
        return false;
    }
    if (filter.productId && device.productId != *filter.productId) {
        return false;
    }
    if (filter.usagePage && device.usagePage != *filter.usagePage) {
        return false;
    }
    if (filter.usage && device.usage != *filter.usage) {
        return false;
    }
    if (!filter.namePrefix.empty() && !device.name.starts_with(filter.namePrefix)) {
        return false;
    }
    if (!filter.services.empty()) {
        const bool offered = std::ranges::any_of(filter.services, [&device](const std::string& wanted) {
            const std::optional<std::string> canonical = canonicalUuid(wanted);
            return canonical && std::ranges::find(device.services, *canonical) != device.services.end();
        });
        if (!offered) {
            return false;
        }
    }
    return true;
}

}

std::string_view kindName(Kind kind) noexcept
{
    return kKindNames[static_cast<std::size_t>(kind)];
}

std::optional<Kind> kindNamed(std::string_view name) noexcept
{
    const auto found = std::ranges::find(kKindNames, name);
    if (found == kKindNames.end()) {
        return std::nullopt;
    }
    return static_cast<Kind>(found - kKindNames.begin());
}

std::optional<std::string> canonicalUuid(std::string_view text)
{
    if (text.size() >= 2 && text.front() == '{' && text.back() == '}') {
        text = text.substr(1, text.size() - 2);
    }
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2);
    }
    if (text.size() == 4 && allHex(text)) {
        return "0000" + loweredText(text) + std::string(kBluetoothBase);
    }
    if (text.size() == 8 && allHex(text)) {
        return loweredText(text) + std::string(kBluetoothBase);
    }
    if (fullUuid(text)) {
        return loweredText(text);
    }
    return std::nullopt;
}

bool matches(const DeviceInfo& device, Kind kind, const std::vector<Filter>& filters)
{
    if (device.kind != kind) {
        return false;
    }
    return filters.empty() ||
           std::ranges::any_of(filters, [&device](const Filter& filter) { return matchesFilter(device, filter); });
}

std::string hex4(std::uint16_t value)
{
    return std::format("{:04X}", value);
}

std::string addressText(std::uint64_t address)
{
    std::string text;
    for (int byte = 5; byte >= 0; --byte) {
        if (!text.empty()) {
            text += ':';
        }
        text += std::format("{:02X}", static_cast<unsigned>((address >> (8 * byte)) & 0xFFu));
    }
    return text;
}

std::string bleId(std::uint64_t address)
{
    return std::string(kBleIdPrefix) + std::format("{:012x}", address & 0xFFFF'FFFF'FFFFull);
}

std::optional<std::uint64_t> bleAddressOf(std::string_view id)
{
    if (!id.starts_with(kBleIdPrefix)) {
        return std::nullopt;
    }
    const std::string_view digits = id.substr(kBleIdPrefix.size());
    if (digits.size() != 12 || !allHex(digits)) {
        return std::nullopt;
    }
    std::uint64_t address = 0;
    for (const char c : digits) {
        const char low = lowered(c);
        address = (address << 4) | static_cast<std::uint64_t>(low <= '9' ? low - '0' : low - 'a' + 10);
    }
    return address;
}

std::string detailOf(const DeviceInfo& device)
{
    const bool ids = device.vendorId != 0 || device.productId != 0;
    const std::string usb = ids ? hex4(device.vendorId) + ":" + hex4(device.productId) : std::string();
    switch (device.kind) {
    case Kind::Serial: {
        const std::string_view path = device.path;
        const std::string port(path.starts_with(kSerialPrefix) ? path.substr(kSerialPrefix.size()) : path);
        if (usb.empty()) {
            return port;
        }
        return port.empty() ? usb : port + " \u00B7 " + usb;
    }
    case Kind::Hid:
        return usb;
    case Kind::Ble:
        if (const std::optional<std::uint64_t> address = bleAddressOf(device.id)) {
            return addressText(*address);
        }
        return device.path;
    }
    return {};
}

bool validSerialOptions(const SerialOptions& options) noexcept
{
    if (options.baudRate < kLeastBaudRate || options.baudRate > kMostBaudRate) {
        return false;
    }
    if (options.dataBits < 5 || options.dataBits > 8) {
        return false;
    }
    if (options.dataBits == 5 && options.stopBits == StopBits::Two) {
        return false;
    }
    return !(options.dataBits > 5 && options.stopBits == StopBits::OneAndHalf);
}

void Device::write(Bytes, Completion done)
{
    done(std::string(kUnsupported));
}

void Device::setSignals(std::optional<bool>, std::optional<bool>, Completion done)
{
    done(std::string(kUnsupported));
}

void Device::sendReport(std::uint8_t, Bytes, Completion done)
{
    done(std::string(kUnsupported));
}

void Device::sendFeature(std::uint8_t, Bytes, Completion done)
{
    done(std::string(kUnsupported));
}

void Device::getFeature(std::uint8_t, BytesCompletion done)
{
    done(std::string(kUnsupported), {});
}

void Device::services(ServicesCompletion done)
{
    done(std::string(kUnsupported), {});
}

void Device::read(const std::string&, const std::string&, BytesCompletion done)
{
    done(std::string(kUnsupported), {});
}

void Device::writeValue(const std::string&, const std::string&, Bytes, bool, Completion done)
{
    done(std::string(kUnsupported));
}

void Device::subscribe(const std::string&, const std::string&, bool, Completion done)
{
    done(std::string(kUnsupported));
}

}
