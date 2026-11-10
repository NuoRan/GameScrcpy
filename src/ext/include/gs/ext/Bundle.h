#pragma once

#include "gs/ext/Diagnostics.h"
#include "gs/ext/Manifest.h"
#include "gs/ext/Version.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::ext {

inline constexpr std::int64_t kBundleSchemaVersion = 1;
inline constexpr std::size_t kMaxBundleIndexBytes = 256 * 1024;
inline constexpr std::size_t kMaxBundleExtensions = 64;
inline constexpr std::string_view kBundleIndexName = "bundle.json";

struct BundleEntry {
    std::string id;
    Version version;
    bool builtin = false;
    bool enabled = true;
    std::map<std::string, SettingValue, std::less<>> values;

    friend bool operator==(const BundleEntry&, const BundleEntry&) = default;
};

struct Bundle {
    std::int64_t schemaVersion = kBundleSchemaVersion;
    std::string name;
    std::string description;
    std::vector<BundleEntry> extensions;

    friend bool operator==(const Bundle&, const Bundle&) = default;
};

struct LoadedBundle {
    bool usable = false;
    Bundle bundle;
    std::vector<Diagnostic> diagnostics;
};

struct BundleCheck {
    std::optional<Bundle> bundle;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept { return bundle.has_value() && !hasErrors(diagnostics); }
};

[[nodiscard]] LoadedBundle loadBundle(std::string_view json);
[[nodiscard]] std::string saveBundle(const Bundle& bundle);
[[nodiscard]] std::string bundlePackagePath(const BundleEntry& entry);
[[nodiscard]] BundleCheck checkBundle(const std::filesystem::path& archive);
[[nodiscard]] std::vector<Diagnostic> extractBundlePackage(const std::filesystem::path& archive, const BundleEntry& entry,
                                                           const std::filesystem::path& file);
[[nodiscard]] std::vector<Diagnostic> packBundle(const Bundle& bundle, const std::filesystem::path& staging,
                                                 const std::filesystem::path& archive);

}
