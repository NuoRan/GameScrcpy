#include "gs/espflash/Firmware.h"

#include <gs/platform/AtomicFile.h>
#include <gs/platform/Crypto.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>

namespace gs::espflash {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMostManifest = 64 * 1024;
constexpr std::size_t kMostImage = 32 * 1024 * 1024;
constexpr std::size_t kMostFileName = 128;
constexpr std::uint32_t kMostChipId = 0xFFFF;

int hexValue(char c) noexcept
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::optional<std::array<std::uint8_t, 32>> digestFromHex(std::string_view text) noexcept
{
    std::array<std::uint8_t, 32> digest{};
    if (text.size() != digest.size() * 2) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < digest.size(); ++i) {
        const int high = hexValue(text[i * 2]);
        const int low = hexValue(text[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        digest[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return digest;
}

bool safeFileName(std::string_view name) noexcept
{
    if (name.empty() || name.size() > kMostFileName || name == "." || name == "..") {
        return false;
    }
    return std::ranges::none_of(name, [](char c) {
        return c == '/' || c == '\\' || c == ':' || static_cast<unsigned char>(c) < 0x20;
    });
}

std::optional<std::string> textOf(const Json& entry, const char* key)
{
    const auto found = entry.find(key);
    if (found == entry.end() || !found->is_string()) {
        return std::nullopt;
    }
    return found->get<std::string>();
}

std::optional<std::uint32_t> numberOf(const Json& entry, const char* key, std::uint32_t most)
{
    const auto found = entry.find(key);
    if (found == entry.end() || !found->is_number_unsigned()) {
        return std::nullopt;
    }
    const auto value = found->get<std::uint64_t>();
    if (value > most) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

struct VersionParts {
    std::array<unsigned, 3> numbers{};
    bool prerelease = false;
    std::string_view suffix;
};

VersionParts versionParts(std::string_view text) noexcept
{
    VersionParts parts;
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) {
        text.remove_prefix(1);
    }
    std::size_t at = 0;
    for (std::size_t index = 0; index < parts.numbers.size() && at < text.size(); ++index) {
        unsigned value = 0;
        const auto [end, error] = std::from_chars(text.data() + at, text.data() + text.size(), value);
        if (error != std::errc{}) {
            break;
        }
        parts.numbers[index] = value;
        at = static_cast<std::size_t>(end - text.data());
        if (at < text.size() && text[at] == '.') {
            ++at;
        } else {
            break;
        }
    }
    const std::size_t dash = text.find('-');
    if (dash != std::string_view::npos) {
        parts.prerelease = true;
        parts.suffix = text.substr(dash + 1);
    }
    return parts;
}

}

Expected<FirmwareBundle, FirmwareProblem> parseFirmwareBundle(std::string_view json, std::filesystem::path folder)
{
    const Json root = Json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return makeUnexpected(FirmwareProblem::Malformed);
    }
    const std::optional<std::string> format = textOf(root, "format");
    const std::optional<std::string> version = textOf(root, "version");
    const auto targets = root.find("targets");
    if (!format || *format != kFirmwareFormat || !version || targets == root.end() || !targets->is_object()) {
        return makeUnexpected(FirmwareProblem::Malformed);
    }
    FirmwareBundle bundle;
    bundle.version = *version;
    bundle.folder = std::move(folder);
    for (auto it = targets->begin(); it != targets->end(); ++it) {
        const Json& entry = it.value();
        if (!entry.is_object()) {
            return makeUnexpected(FirmwareProblem::Malformed);
        }
        const std::optional<std::string> file = textOf(entry, "file");
        const std::optional<std::string> sha256 = textOf(entry, "sha256");
        const std::optional<std::string> flashSize = textOf(entry, "flashSize");
        const std::optional<std::uint32_t> chipId = numberOf(entry, "chipId", kMostChipId);
        std::optional<std::uint32_t> offset = 0U;
        if (entry.contains("offset")) {
            offset = numberOf(entry, "offset", 0x00FFFFFF);
        }
        const std::optional<std::array<std::uint8_t, 32>> digest =
            sha256 ? digestFromHex(*sha256) : std::optional<std::array<std::uint8_t, 32>>{};
        if (!file || !safeFileName(*file) || !digest || !flashSize || !flashSizeBytes(*flashSize) || !chipId ||
            !offset) {
            return makeUnexpected(FirmwareProblem::Malformed);
        }
        bundle.images.push_back(FirmwareImage{.target = it.key(),
                                              .file = *file,
                                              .sha256 = *digest,
                                              .flashSize = *flashSize,
                                              .chipId = *chipId,
                                              .offset = *offset});
    }
    return bundle;
}

Expected<FirmwareBundle, FirmwareProblem> loadFirmwareBundle(const std::filesystem::path& folder)
{
    std::string text;
    const std::error_code error = platform::readFile(folder / kFirmwareManifest, kMostManifest, text);
    if (error) {
        return makeUnexpected(platform::fileMissing(error) ? FirmwareProblem::Missing : FirmwareProblem::Malformed);
    }
    return parseFirmwareBundle(text, folder);
}

const FirmwareImage* imageForChip(const FirmwareBundle& bundle, std::uint32_t chipId) noexcept
{
    const auto found = std::ranges::find(bundle.images, chipId, &FirmwareImage::chipId);
    return found == bundle.images.end() ? nullptr : &*found;
}

Expected<std::vector<std::uint8_t>, FirmwareProblem> readImage(const FirmwareBundle& bundle, const FirmwareImage& image)
{
    if (!safeFileName(image.file)) {
        return makeUnexpected(FirmwareProblem::ImageMissing);
    }
    std::string text;
    if (platform::readFile(bundle.folder / image.file, kMostImage, text)) {
        return makeUnexpected(FirmwareProblem::ImageMissing);
    }
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    if (bytes.empty() || platform::sha256(bytes) != image.sha256) {
        return makeUnexpected(FirmwareProblem::ImageCorrupt);
    }
    return bytes;
}

std::optional<std::uint32_t> flashSizeBytes(std::string_view text) noexcept
{
    static constexpr std::array<std::pair<std::string_view, std::uint32_t>, 7> kSizes{{
        {"2MB", 2U << 20},
        {"4MB", 4U << 20},
        {"8MB", 8U << 20},
        {"16MB", 16U << 20},
        {"32MB", 32U << 20},
        {"64MB", 64U << 20},
        {"128MB", 128U << 20},
    }};
    for (const auto& [name, bytes] : kSizes) {
        if (name == text) {
            return bytes;
        }
    }
    return std::nullopt;
}

int compareVersions(std::string_view left, std::string_view right) noexcept
{
    const VersionParts a = versionParts(left);
    const VersionParts b = versionParts(right);
    if (a.numbers != b.numbers) {
        return a.numbers < b.numbers ? -1 : 1;
    }
    if (a.prerelease != b.prerelease) {
        return a.prerelease ? -1 : 1;
    }
    if (a.suffix != b.suffix) {
        return a.suffix < b.suffix ? -1 : 1;
    }
    return 0;
}

}
