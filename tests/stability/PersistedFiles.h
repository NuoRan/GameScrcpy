#pragma once

#include "Settings.h"

#include <gs/input/Profile.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace gs::stability {

enum class FileKind : std::uint8_t { Settings, Devices, Profile, AddedProfile, Patch };

inline constexpr std::size_t kFileKinds = 5;
inline constexpr std::array<std::string_view, kFileKinds> kFileKindNames{"settings", "devices", "profile",
                                                                         "added profile", "profile patch"};
inline constexpr std::string_view kSavedProfileName = "Saved";
inline constexpr std::string_view kAddedProfilePrefix = "Added ";
inline constexpr std::string_view kExtensionId = "kill.test";
inline constexpr std::string_view kExtensionProfileName = "Base";
inline constexpr std::string_view kTemporarySuffix = ".tmp";

struct WriteSlot {
    std::atomic<std::int64_t> started{-1};
    std::atomic<std::int64_t> done{-1};
    std::atomic<std::uint64_t> writes{0};
    std::atomic<std::uint64_t> failures{0};
};

struct WriterProgress {
    std::atomic<std::uint32_t> ready{0};
    std::atomic<std::int32_t> writing{-1};
    std::array<WriteSlot, kFileKinds> files;
};

static_assert(std::atomic<std::int64_t>::is_always_lock_free && std::atomic<std::uint64_t>::is_always_lock_free &&
              std::atomic<std::uint32_t>::is_always_lock_free && std::atomic<std::int32_t>::is_always_lock_free);

[[nodiscard]] constexpr std::size_t indexOf(FileKind kind) noexcept
{
    return static_cast<std::size_t>(kind);
}

[[nodiscard]] std::filesystem::path settingsPath(const std::filesystem::path& data);
[[nodiscard]] std::filesystem::path devicesPath(const std::filesystem::path& data);
[[nodiscard]] std::filesystem::path profilesPath(const std::filesystem::path& data);
[[nodiscard]] std::filesystem::path savedProfilePath(const std::filesystem::path& data);
[[nodiscard]] std::filesystem::path addedProfilePath(const std::filesystem::path& data, std::int64_t version);
[[nodiscard]] std::filesystem::path patchPath(const std::filesystem::path& data);
[[nodiscard]] std::string extensionProfileId();
[[nodiscard]] std::string addedProfileName(std::int64_t version);

[[nodiscard]] std::optional<std::string> readText(const std::filesystem::path& path);
[[nodiscard]] std::optional<input::Profile> loadTemplate();

[[nodiscard]] app::settings::Settings settingsAt(std::int64_t version);
[[nodiscard]] app::settings::Devices devicesAt(std::int64_t version);
[[nodiscard]] input::Profile profileAt(const input::Profile& base, std::string name, std::int64_t version);
[[nodiscard]] std::string extensionProfileJson(const input::Profile& base);
[[nodiscard]] input::Profile extensionBase(const input::Profile& base);
[[nodiscard]] std::string expectedContent(FileKind kind, const input::Profile& base, std::int64_t version);

[[nodiscard]] std::int64_t versionOf(const app::settings::Settings& settings);
[[nodiscard]] std::int64_t versionOf(const app::settings::Devices& devices);
[[nodiscard]] std::int64_t versionOf(const input::Profile& profile);
[[nodiscard]] std::optional<std::int64_t> addedVersionOf(std::string_view stem);

}
