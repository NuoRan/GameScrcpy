#include "AoaTouchSink.h"
#include "UhidMouse.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <span>
#include <vector>

TEST_CASE("ADR-0108: the virtual mouse of a phone without a picture is a UHID copy of the AOA mouse")
{
    const gs::protocol::UhidCreate create = gs::app::uhidMouseCreate();
    CHECK(create.id == gs::app::kUhidMouseId);
    CHECK(create.vendorId == 0x1209);
    CHECK(create.productId == 0x0002);
    CHECK(create.name == "GameScrcpy Mouse");
    CHECK(create.bus == gs::protocol::UhidCreate::kBusUsb);

    const std::span<const std::uint8_t> aoa = gs::app::aoaMouseDescriptor();
    CHECK(create.reportDescriptor == std::vector<std::uint8_t>(aoa.begin(), aoa.end()));

    CHECK(gs::app::kUhidMouseId != 1);
}

TEST_CASE("ADR-0108: virtual mouse reports carry the buttons, a clamped 16-bit move and an 8-bit wheel")
{
    const auto plain = gs::app::uhidMouseReport(0x01, 5, -3, 0, 0);
    CHECK(plain.id == gs::app::kUhidMouseId);
    CHECK(plain.report == std::vector<std::uint8_t>{0x01, 0x05, 0x00, 0xFD, 0xFF, 0x00, 0x00});

    const auto masked = gs::app::uhidMouseReport(0xFF, 0, 0, 0, 0);
    CHECK(masked.report[0] == 0x1F);

    const auto big = gs::app::uhidMouseReport(0, 100000, -100000, 300, -300);
    CHECK(big.report == std::vector<std::uint8_t>{0x00, 0xFF, 0x7F, 0x01, 0x80, 0x7F, 0x81});
}
