#pragma once

#include "gs/ext/Manifest.h"
#include "gs/ext/Minisign.h"
#include "gs/ext/Resolution.h"
#include "gs/ext/Version.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::ext {

inline constexpr std::string_view kUpdateIndexFormat = "gamescrcpy.extension-index";
inline constexpr std::int64_t kUpdateIndexVersion = 1;
inline constexpr std::size_t kMostUpdateIndexBytes = std::size_t{1} << 20;
inline constexpr std::size_t kMostIndexedExtensions = 256;
inline constexpr std::size_t kMostIndexedReleases = 128;
inline constexpr std::uint64_t kMostUpdatePackageBytes = std::uint64_t{200} << 20;
inline constexpr std::size_t kMostReleaseNotesBytes = 4096;

struct IndexedRelease {
    Version version;
    std::string url;
    std::string sha256;
    std::uint64_t size = 0;
    std::optional<MinisignKey> publisher;
    std::map<ExtensionPoint, VersionRange> requirements;
    bool needsUnknown = false;
    std::string released;
    std::string notes;

    friend bool operator==(const IndexedRelease&, const IndexedRelease&) = default;
};

struct IndexedExtension {
    std::string id;
    std::vector<IndexedRelease> releases;

    friend bool operator==(const IndexedExtension&, const IndexedExtension&) = default;
};

struct UpdateIndex {
    std::vector<IndexedExtension> extensions;

    [[nodiscard]] const IndexedExtension* find(std::string_view id) const;
};

struct ReadIndex {
    std::optional<UpdateIndex> index;
    std::string problem;
};

[[nodiscard]] bool validUpdateUrl(std::string_view url);
[[nodiscard]] ReadIndex readUpdateIndex(std::string_view json);
[[nodiscard]] bool runsOn(const IndexedRelease& release, const HostApi& host);
[[nodiscard]] const IndexedRelease* offeredRelease(const IndexedExtension& extension, const Version& installed,
                                                   const HostApi& host,
                                                   const std::optional<Version>& skipped = std::nullopt);

}
