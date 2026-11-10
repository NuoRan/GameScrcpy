#include "gs/record/Recording.h"

#include <algorithm>
#include <format>
#include <system_error>
#include <utility>

namespace gs::record {

namespace {

std::filesystem::path pathOf(std::string_view utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string utf8Of(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::filesystem::path folderOf(const std::filesystem::path& path)
{
    std::filesystem::path folder = path.lexically_normal();
    if (!folder.has_filename()) {
        folder = folder.parent_path();
    }
    return folder;
}

std::string lowered(std::string text)
{
    std::ranges::transform(text, text.begin(),
                           [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    return text;
}

}

bool isRecordingFolder(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::is_directory(path, error)
           && std::filesystem::is_regular_file(path / pathOf(kSessionFile), error);
}

Expected<Recording, std::string> openRecording(const std::filesystem::path& folder)
{
    Recording recording;
    recording.folder = folderOf(folder);
    auto session = script::loadSession(recording.folder / pathOf(kSessionFile));
    if (!session) {
        return makeUnexpected(std::string(kSessionFile) + ": " + session.error());
    }
    recording.session = std::move(*session);
    if (!recording.session.video.empty()) {
        recording.video = recording.folder / pathOf(recording.session.video);
    }
    if (!recording.session.audio.empty()) {
        recording.audio = recording.folder / pathOf(recording.session.audio);
    }
    return recording;
}

std::string safeFileName(std::string_view name)
{
    constexpr std::string_view kRefused = "<>:\"/\\|?*";
    std::string safe;
    safe.reserve(name.size());
    for (const char c : name) {
        const bool control = static_cast<unsigned char>(c) < 0x20U;
        safe.push_back(control || kRefused.find(c) != std::string_view::npos ? '_' : c);
    }
    while (!safe.empty() && (safe.back() == '.' || safe.back() == ' ')) {
        safe.pop_back();
    }
    const std::size_t first = safe.find_first_not_of(' ');
    safe.erase(0, first == std::string::npos ? safe.size() : first);
    return safe.empty() ? std::string("Phone") : safe;
}

std::filesystem::path exportPath(const std::filesystem::path& recording, std::size_t index)
{
    const std::filesystem::path folder = folderOf(recording);
    const bool named = lowered(utf8Of(folder.extension())) == kRecordingExtension;
    std::string name = utf8Of(named ? folder.stem() : folder.filename());
    if (index > 0) {
        name += std::format(" ({})", index + 1);
    }
    return folder.parent_path() / pathOf(name + ".mp4");
}

}
