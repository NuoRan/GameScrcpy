#pragma once

#include <gs/input/Profile.h>

#include <string>

namespace gs::input {

struct ProfileDiff {
    bool empty = true;
    bool exact = true;
    std::string patch;
};

[[nodiscard]] ProfileDiff diffProfiles(const Profile& original, const Profile& changed);

}
