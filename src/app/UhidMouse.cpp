#include "UhidMouse.h"

#include "AoaTouchSink.h"

#include <algorithm>
#include <limits>
#include <span>
#include <string>

namespace gs::app {

namespace {

void putSigned16(protocol::Bytes& report, std::size_t at, int value)
{
    constexpr int kMost = std::numeric_limits<std::int16_t>::max();
    const auto bits = static_cast<std::uint16_t>(static_cast<std::int16_t>(std::clamp(value, -kMost, kMost)));
    report[at] = static_cast<std::uint8_t>(bits & 0xFFU);
    report[at + 1] = static_cast<std::uint8_t>(bits >> 8U);
}

std::uint8_t signed8(int value)
{
    return static_cast<std::uint8_t>(static_cast<std::int8_t>(std::clamp(value, -127, 127)));
}

}

protocol::UhidCreate uhidMouseCreate()
{
    protocol::UhidCreate create;
    create.id = kUhidMouseId;
    create.vendorId = kUhidMouseVendor;
    create.productId = kUhidMouseProduct;
    create.name = std::string(kUhidMouseName);
    const std::span<const std::uint8_t> descriptor = aoaMouseDescriptor();
    create.reportDescriptor.assign(descriptor.begin(), descriptor.end());
    create.bus = protocol::UhidCreate::kBusUsb;
    return create;
}

protocol::UhidInput uhidMouseReport(std::uint8_t buttons, int dx, int dy, int vertical, int horizontal)
{
    protocol::UhidInput input;
    input.id = kUhidMouseId;
    input.report.assign(kUhidMouseReportSize, 0);
    input.report[0] = static_cast<std::uint8_t>(buttons & 0x1FU);
    putSigned16(input.report, 1, dx);
    putSigned16(input.report, 3, dy);
    input.report[5] = signed8(vertical);
    input.report[6] = signed8(horizontal);
    return input;
}

}
