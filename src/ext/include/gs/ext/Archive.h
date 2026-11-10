#pragma once

#include "gs/ext/Diagnostics.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::ext {

struct ArchiveLimits {
    std::size_t maxEntries = 4096;
    std::uint64_t maxFileBytes = std::uint64_t{64} << 20;
    std::uint64_t maxTotalBytes = std::uint64_t{256} << 20;
    std::uint64_t maxRatio = 100;
};

struct ArchiveEntry {
    std::string path;
    std::uint64_t size = 0;

    friend bool operator==(const ArchiveEntry&, const ArchiveEntry&) = default;
};

struct ArchiveListing {
    std::vector<ArchiveEntry> files;
    std::vector<Diagnostic> problems;

    [[nodiscard]] bool ok() const noexcept { return !hasErrors(problems); }
};

[[nodiscard]] bool safeArchivePath(std::string_view path);
[[nodiscard]] ArchiveListing inspectArchive(const std::filesystem::path& archive, const ArchiveLimits& limits = {});
[[nodiscard]] ArchiveListing inspectDirectory(const std::filesystem::path& directory, const ArchiveLimits& limits = {},
                                              const std::vector<std::string>& skipped = {});
[[nodiscard]] std::vector<Diagnostic> extractArchive(const std::filesystem::path& archive,
                                                     const std::filesystem::path& destination,
                                                     const ArchiveLimits& limits = {});
[[nodiscard]] std::optional<std::string> readArchiveFile(const std::filesystem::path& archive, std::string_view path,
                                                         std::uint64_t maxBytes);
[[nodiscard]] std::vector<Diagnostic> packDirectory(const std::filesystem::path& directory,
                                                    const std::filesystem::path& archive,
                                                    const ArchiveLimits& limits = {},
                                                    const std::vector<std::string>& skipped = {});

}
