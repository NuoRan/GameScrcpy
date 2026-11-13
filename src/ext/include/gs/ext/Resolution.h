#pragma once

#include "gs/ext/Diagnostics.h"
#include "gs/ext/Manifest.h"
#include "gs/ext/Version.h"

#include <map>
#include <string>
#include <vector>

namespace gs::ext {

struct HostApi {
    std::map<ExtensionPoint, std::vector<Version>> versions;
};

[[nodiscard]] const HostApi& currentHostApi();
[[nodiscard]] std::optional<VersionRange> requirementFor(ExtensionPoint point);
[[nodiscard]] std::optional<Version> writtenFor(const Manifest& manifest);
[[nodiscard]] std::vector<Diagnostic> checkCompatibility(const Manifest& manifest, const HostApi& host);

struct Selected {
    std::string id;
    Version version;

    friend bool operator==(const Selected&, const Selected&) = default;
};

struct Resolution {
    std::vector<Selected> order;
    std::vector<Diagnostic> problems;

    [[nodiscard]] bool ok() const noexcept { return !hasErrors(problems); }
};

[[nodiscard]] Resolution resolveDependencies(const std::vector<Manifest>& installed,
                                             const std::vector<std::string>& enabled);

struct UpdateChanges {
    std::vector<Permission> addedPermissions;
    std::vector<Permission> removedPermissions;
    std::vector<std::string> addedDependencies;
    std::vector<std::string> removedDependencies;
    std::vector<ExtensionPoint> changedRequirements;

    [[nodiscard]] bool needsConsent() const noexcept { return !addedPermissions.empty() || !addedDependencies.empty(); }
};

[[nodiscard]] UpdateChanges compareForUpdate(const Manifest& before, const Manifest& after);

}
