#include <gs/platform/Crypto.h>
#include <gs/platform/Usb.h>

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::string textOf(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string recordedHash(const std::string& buildInfo, const std::string& file)
{
    const std::string row = "| " + file + " | ";
    const std::size_t at = buildInfo.find(row);
    if (at == std::string::npos) {
        return {};
    }
    const std::size_t open = buildInfo.find('`', at);
    const std::size_t close = open == std::string::npos ? open : buildInfo.find('`', open + 1);
    return close == std::string::npos ? std::string() : buildInfo.substr(open + 1, close - open - 1);
}

}

TEST_CASE("the shipped libusb is the build BUILDINFO.md records, from the pinned and checked release (ADR-0009)")
{
    const std::filesystem::path root = std::filesystem::path(GS_THIRD_PARTY_DIR) / "libusb";
    const std::string buildInfo = textOf(root / "BUILDINFO.md");
    REQUIRE_FALSE(buildInfo.empty());
    CHECK(buildInfo.find("fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf") != std::string::npos);
    for (const char* file : {"bin/libusb-1.0.dll", "lib/libusb-1.0.lib"}) {
        CAPTURE(file);
        const std::string bytes = textOf(root / file);
        REQUIRE_FALSE(bytes.empty());
        const std::vector<std::uint8_t> data(bytes.begin(), bytes.end());
        CHECK(gs::platform::toHex(gs::platform::sha256(data)) == recordedHash(buildInfo, file));
    }
    const std::string version = gs::platform::usbLibraryVersion();
    CHECK(version == "1.0.30");
    CHECK(buildInfo.find("libusb-" + version) != std::string::npos);
    CHECK(std::filesystem::is_regular_file(root / "licenses" / "libusb-COPYING"));
    CHECK(textOf(root / "licenses" / "libusb-COPYING").find("GNU LESSER GENERAL PUBLIC LICENSE") != std::string::npos);
}
