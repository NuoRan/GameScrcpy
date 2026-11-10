#pragma once

#include <gs/base/Expected.h>

#include <QString>
#include <QStringView>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace gs::app {

inline constexpr std::size_t kMostBackgroundBytes = 8U * 1024U * 1024U;

enum class PictureKind : std::uint8_t { Png, Jpeg };

struct PictureInfo {
    PictureKind kind = PictureKind::Png;
    int width = 0;
    int height = 0;

    friend bool operator==(const PictureInfo&, const PictureInfo&) = default;
};

enum class BackgroundProblem : std::uint8_t { Missing, TooLarge, NotPicture, Unwritable };

struct StoredBackground {
    std::filesystem::path path;
    bool landscape = false;
    int width = 0;
    int height = 0;
};

[[nodiscard]] std::optional<PictureInfo> pictureInfoOf(std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] QString backgroundStem(QStringView phone);

class KeymapBackgrounds final
{
public:
    explicit KeymapBackgrounds(std::filesystem::path folder);

    [[nodiscard]] const std::filesystem::path& folder() const noexcept { return m_folder; }
    [[nodiscard]] std::optional<std::filesystem::path> find(QStringView phone, bool landscape) const;
    [[nodiscard]] Expected<StoredBackground, BackgroundProblem> store(QStringView phone,
                                                                      std::span<const std::uint8_t> bytes) const;
    [[nodiscard]] Expected<StoredBackground, BackgroundProblem> importFile(QStringView phone,
                                                                           const std::filesystem::path& file) const;
    bool clear(QStringView phone, bool landscape) const;

private:
    [[nodiscard]] std::filesystem::path fileOf(QStringView phone, bool landscape, PictureKind kind) const;

    std::filesystem::path m_folder;
};

}
