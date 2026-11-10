#pragma once

#include "gs/ext/Diagnostics.h"
#include "gs/ext/Manifest.h"
#include "gs/ext/Version.h"

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace gs::ext {

inline constexpr std::string_view kManifestFileName = "manifest.json";
inline constexpr std::string_view kStagingFolder = ".staging";

struct PackageCheck {
    std::optional<Manifest> manifest;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept { return manifest.has_value() && !hasErrors(diagnostics); }
};

struct InstalledExtension {
    std::filesystem::path directory;
    std::optional<Manifest> manifest;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept { return manifest.has_value() && !hasErrors(diagnostics); }
};

[[nodiscard]] PackageCheck checkPackage(const std::filesystem::path& archive, const LoadOptions& options = {});
[[nodiscard]] PackageCheck checkDirectory(const std::filesystem::path& directory, const LoadOptions& options = {});
[[nodiscard]] std::filesystem::path installedDirectory(const std::filesystem::path& root, std::string_view id,
                                                       const Version& version);
[[nodiscard]] InstalledExtension installPackage(const std::filesystem::path& archive, const std::filesystem::path& root,
                                                const LoadOptions& options = {});
[[nodiscard]] std::vector<InstalledExtension> scanInstalled(const std::filesystem::path& root,
                                                            const LoadOptions& options = {});
[[nodiscard]] bool removeInstalled(const std::filesystem::path& root, std::string_view id, const Version& version);
void clearStaging(const std::filesystem::path& root);

}
