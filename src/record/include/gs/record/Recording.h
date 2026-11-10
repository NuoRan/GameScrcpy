#pragma once

#include <gs/base/Expected.h>
#include <gs/script/Session.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace gs::record {

inline constexpr std::string_view kSessionFile = "session.json";
inline constexpr std::string_view kVideoFile = "video.media";
inline constexpr std::string_view kAudioFile = "audio.media";
inline constexpr std::string_view kRecordingExtension = ".gsrec";

struct Recording {
    std::filesystem::path folder;
    script::Session session;
    std::filesystem::path video;
    std::filesystem::path audio;
};

[[nodiscard]] bool isRecordingFolder(const std::filesystem::path& path);
[[nodiscard]] Expected<Recording, std::string> openRecording(const std::filesystem::path& folder);
[[nodiscard]] std::string safeFileName(std::string_view name);
[[nodiscard]] std::filesystem::path exportPath(const std::filesystem::path& recording, std::size_t index);

}
