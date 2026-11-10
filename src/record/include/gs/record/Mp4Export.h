#pragma once

#include <gs/base/Expected.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gs::record {

struct ExportedVideo {
    std::filesystem::path file;
    std::int64_t durationUs = 0;
    int width = 0;
    int height = 0;
    bool audio = false;
};

[[nodiscard]] Expected<std::vector<ExportedVideo>, std::string> exportMp4(const std::filesystem::path& recording);

}
