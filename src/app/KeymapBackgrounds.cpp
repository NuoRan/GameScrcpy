#include "KeymapBackgrounds.h"

#include <gs/platform/AtomicFile.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

constexpr std::array<std::uint8_t, 8> kPngSignature{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
constexpr std::size_t kPngHeader = 24;
constexpr std::uint32_t kMostSide = 32768;
constexpr qsizetype kMostStem = 96;

std::uint32_t bigEndian(std::span<const std::uint8_t> bytes) noexcept
{
    std::uint32_t value = 0;
    for (const std::uint8_t byte : bytes) {
        value = (value << 8U) | byte;
    }
    return value;
}

std::optional<PictureInfo> pngInfo(std::span<const std::uint8_t> bytes) noexcept
{
    if (bytes.size() < kPngHeader || !std::equal(kPngSignature.begin(), kPngSignature.end(), bytes.begin())) {
        return std::nullopt;
    }
    if (bytes[12] != 'I' || bytes[13] != 'H' || bytes[14] != 'D' || bytes[15] != 'R') {
        return std::nullopt;
    }
    const std::uint32_t width = bigEndian(bytes.subspan(16, 4));
    const std::uint32_t height = bigEndian(bytes.subspan(20, 4));
    if (width == 0 || height == 0 || width > kMostSide || height > kMostSide) {
        return std::nullopt;
    }
    return PictureInfo{.kind = PictureKind::Png, .width = static_cast<int>(width), .height = static_cast<int>(height)};
}

bool frameMarker(std::uint8_t marker) noexcept
{
    return marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
}

bool standaloneMarker(std::uint8_t marker) noexcept
{
    return marker == 0x01 || marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7);
}

std::optional<PictureInfo> jpegInfo(std::span<const std::uint8_t> bytes) noexcept
{
    if (bytes.size() < 4 || bytes[0] != 0xFF || bytes[1] != 0xD8) {
        return std::nullopt;
    }
    std::size_t at = 2;
    while (at + 4 <= bytes.size()) {
        if (bytes[at] != 0xFF) {
            return std::nullopt;
        }
        const std::uint8_t marker = bytes[at + 1];
        if (marker == 0xFF) {
            ++at;
            continue;
        }
        if (standaloneMarker(marker)) {
            at += 2;
            continue;
        }
        if (marker == 0xD9 || marker == 0xDA) {
            return std::nullopt;
        }
        const std::size_t length = (std::size_t{bytes[at + 2]} << 8U) | bytes[at + 3];
        if (length < 2 || at + 2 + length > bytes.size()) {
            return std::nullopt;
        }
        if (frameMarker(marker)) {
            if (length < 7) {
                return std::nullopt;
            }
            const std::uint32_t height = bigEndian(bytes.subspan(at + 5, 2));
            const std::uint32_t width = bigEndian(bytes.subspan(at + 7, 2));
            if (width == 0 || height == 0) {
                return std::nullopt;
            }
            return PictureInfo{.kind = PictureKind::Jpeg,
                               .width = static_cast<int>(width),
                               .height = static_cast<int>(height)};
        }
        at += 2 + length;
    }
    return std::nullopt;
}

bool keptInStem(char16_t c) noexcept
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'.' ||
           c == u'_' || c == u'-';
}

}

std::optional<PictureInfo> pictureInfoOf(std::span<const std::uint8_t> bytes) noexcept
{
    if (std::optional<PictureInfo> png = pngInfo(bytes)) {
        return png;
    }
    return jpegInfo(bytes);
}

QString backgroundStem(QStringView phone)
{
    QString stem;
    stem.reserve(std::min(phone.size(), kMostStem));
    for (const QChar c : phone.left(kMostStem)) {
        stem.append(keptInStem(c.unicode()) ? c : QChar(u'_'));
    }
    return stem.isEmpty() ? QStringLiteral("phone") : stem;
}

KeymapBackgrounds::KeymapBackgrounds(std::filesystem::path folder)
    : m_folder(std::move(folder))
{
}

std::filesystem::path KeymapBackgrounds::fileOf(QStringView phone, bool landscape, PictureKind kind) const
{
    const QString name = backgroundStem(phone) + (landscape ? QStringLiteral("-landscape") : QStringLiteral("-portrait")) +
                         (kind == PictureKind::Png ? QStringLiteral(".png") : QStringLiteral(".jpg"));
    return m_folder / name.toStdWString();
}

std::optional<std::filesystem::path> KeymapBackgrounds::find(QStringView phone, bool landscape) const
{
    for (const PictureKind kind : {PictureKind::Jpeg, PictureKind::Png}) {
        std::filesystem::path file = fileOf(phone, landscape, kind);
        std::error_code error;
        if (std::filesystem::is_regular_file(file, error)) {
            return file;
        }
    }
    return std::nullopt;
}

Expected<StoredBackground, BackgroundProblem> KeymapBackgrounds::store(QStringView phone,
                                                                       std::span<const std::uint8_t> bytes) const
{
    if (bytes.size() > kMostBackgroundBytes) {
        return makeUnexpected(BackgroundProblem::TooLarge);
    }
    const std::optional<PictureInfo> info = pictureInfoOf(bytes);
    if (!info) {
        return makeUnexpected(BackgroundProblem::NotPicture);
    }
    const bool landscape = info->width > info->height;
    const std::filesystem::path target = fileOf(phone, landscape, info->kind);
    std::error_code error;
    static_cast<void>(std::filesystem::create_directories(m_folder, error));
    const std::string_view contents(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (const std::error_code failed = platform::writeFileAtomically(target, contents); failed) {
        return makeUnexpected(BackgroundProblem::Unwritable);
    }
    const PictureKind other = info->kind == PictureKind::Png ? PictureKind::Jpeg : PictureKind::Png;
    static_cast<void>(std::filesystem::remove(fileOf(phone, landscape, other), error));
    return StoredBackground{.path = target, .landscape = landscape, .width = info->width, .height = info->height};
}

Expected<StoredBackground, BackgroundProblem> KeymapBackgrounds::importFile(QStringView phone,
                                                                            const std::filesystem::path& file) const
{
    std::string contents;
    if (const std::error_code failed = platform::readFile(file, kMostBackgroundBytes, contents); failed) {
        return makeUnexpected(failed == std::errc::file_too_large ? BackgroundProblem::TooLarge
                                                                  : BackgroundProblem::Missing);
    }
    return store(phone, std::span(reinterpret_cast<const std::uint8_t*>(contents.data()), contents.size()));
}

bool KeymapBackgrounds::clear(QStringView phone, bool landscape) const
{
    bool removed = false;
    for (const PictureKind kind : {PictureKind::Jpeg, PictureKind::Png}) {
        std::error_code error;
        removed = std::filesystem::remove(fileOf(phone, landscape, kind), error) || removed;
    }
    return removed;
}

}
