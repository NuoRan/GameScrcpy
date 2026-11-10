#pragma once

#include <gs/input/Profile.h>

#include <string>
#include <string_view>
#include <vector>

namespace gs::input {

inline constexpr std::string_view kPatchFormatName = "gamescrcpy.profile-patch";
inline constexpr int kPatchFormatVersion = 1;

struct PatchOutcome {
    bool applied = false;
    std::string profile;
    std::vector<Diagnostic> diagnostics;
    std::vector<std::string> touched;
};

[[nodiscard]] PatchOutcome applyPatch(std::string_view profile, std::string_view patch);

}
