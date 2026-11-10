#pragma once

#include <gs/base/Expected.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gs::record {

struct RecordedSound {
    int sampleRate = 0;
    int channels = 0;
    bool aac = false;
    std::size_t packets = 0;
    std::int64_t firstPtsUs = 0;
    std::vector<float> samples;
};

[[nodiscard]] Expected<RecordedSound, std::string> decodeRecordedSound(const std::filesystem::path& file,
                                                                       std::size_t mostFrames = 48'000 * 60);

}
