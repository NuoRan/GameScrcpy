#include "PersistedFiles.h"

#include <gs/input/ProfileDiff.h>

#include <charconv>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>
#include <variant>

namespace gs::stability {

namespace {

constexpr std::size_t kDevices = 8;
constexpr std::size_t kNoteLength = 4096;
constexpr std::string_view kVersionKey = "version";
constexpr std::string_view kNoteKey = "note";
constexpr char kVersionMark = 'v';

std::optional<std::int64_t> parseVersion(std::string_view text)
{
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size() || value < 0) {
        return std::nullopt;
    }
    return value;
}

app::settings::Scalars valuesAt(std::int64_t version)
{
    const char letter = static_cast<char>('a' + version % 26);
    app::settings::Scalars values;
    values.emplace(std::string(kVersionKey), version);
    values.emplace(std::string(kNoteKey), std::string(kNoteLength, letter));
    return values;
}

std::int64_t versionIn(const app::settings::Scalars& values)
{
    const auto found = values.find(kVersionKey);
    if (found == values.end()) {
        return -1;
    }
    const auto* version = std::get_if<std::int64_t>(&found->second);
    return version != nullptr && *version >= 0 ? *version : -1;
}

std::string jsonName(std::string_view stem)
{
    return std::string(stem) + ".json";
}

}

std::filesystem::path settingsPath(const std::filesystem::path& data)
{
    return data / "settings.json";
}

std::filesystem::path devicesPath(const std::filesystem::path& data)
{
    return data / "devices.json";
}

std::filesystem::path profilesPath(const std::filesystem::path& data)
{
    return data / "profiles";
}

std::filesystem::path savedProfilePath(const std::filesystem::path& data)
{
    return profilesPath(data) / jsonName(kSavedProfileName);
}

std::filesystem::path addedProfilePath(const std::filesystem::path& data, std::int64_t version)
{
    return profilesPath(data) / jsonName(addedProfileName(version));
}

std::filesystem::path patchPath(const std::filesystem::path& data)
{
    return profilesPath(data) / "patches" / std::string(kExtensionId) / jsonName(kExtensionProfileName);
}

std::string extensionProfileId()
{
    return std::format("{}/{}", kExtensionId, kExtensionProfileName);
}

std::string addedProfileName(std::int64_t version)
{
    return std::format("{}{}", kAddedProfilePrefix, version);
}

std::optional<std::string> readText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (in.bad()) {
        return std::nullopt;
    }
    return text;
}

std::optional<input::Profile> loadTemplate()
{
    const std::optional<std::string> text = readText(std::filesystem::path(GS_PROFILE_TEMPLATE));
    if (!text) {
        return std::nullopt;
    }
    input::LoadResult loaded = input::loadProfile(*text);
    if (!loaded.ok() || loaded.profile.controls.empty()) {
        return std::nullopt;
    }
    return std::move(loaded.profile);
}

app::settings::Settings settingsAt(std::int64_t version)
{
    app::settings::Settings settings;
    settings.hubWindow = {.x = static_cast<std::int32_t>(version % 1000), .y = 40, .width = 1280, .height = 800,
                          .maximized = false};
    settings.extensions.emplace(std::string(kExtensionId), app::settings::ExtensionState{.enabled = true,
                                                                                           .version = "1.0.0",
                                                                                           .granted = {},
                                                                                           .values = valuesAt(version)});
    return settings;
}

app::settings::Devices devicesAt(std::int64_t version)
{
    app::settings::Devices devices;
    for (std::size_t i = 0; i < kDevices; ++i) {
        app::settings::DeviceSettings device;
        device.profile = std::string(kSavedProfileName);
        device.apps.emplace(std::format("com.example.game{}", i), std::string(kSavedProfileName));
        device.extensions.emplace(std::string(kExtensionId), app::settings::DeviceExtension{.values = valuesAt(version)});
        devices.byId.emplace(std::format("kill-{}", i), std::move(device));
    }
    return devices;
}

input::Profile profileAt(const input::Profile& base, std::string name, std::int64_t version)
{
    input::Profile profile = base;
    profile.meta.name = std::move(name);
    profile.controls.front().name = std::format("{}{}", kVersionMark, version);
    return profile;
}

std::string extensionProfileJson(const input::Profile& base)
{
    input::Profile profile = base;
    profile.meta.name = std::string(kExtensionProfileName);
    return input::saveProfile(profile);
}

input::Profile extensionBase(const input::Profile& base)
{
    return input::loadProfile(extensionProfileJson(base)).profile;
}

std::string expectedContent(FileKind kind, const input::Profile& base, std::int64_t version)
{
    switch (kind) {
    case FileKind::Settings:
        return app::settings::save(settingsAt(version));
    case FileKind::Devices:
        return app::settings::save(devicesAt(version));
    case FileKind::Profile:
        return input::saveProfile(profileAt(base, std::string(kSavedProfileName), version));
    case FileKind::AddedProfile:
        return input::saveProfile(profileAt(base, addedProfileName(version), version));
    case FileKind::Patch: {
        const input::Profile original = extensionBase(base);
        return input::diffProfiles(original, profileAt(original, std::string(kExtensionProfileName), version)).patch;
    }
    }
    return {};
}

std::int64_t versionOf(const app::settings::Settings& settings)
{
    const auto found = settings.extensions.find(kExtensionId);
    return found != settings.extensions.end() ? versionIn(found->second.values) : -1;
}

std::int64_t versionOf(const app::settings::Devices& devices)
{
    const auto device = devices.byId.find("kill-0");
    if (device == devices.byId.end()) {
        return -1;
    }
    const auto extension = device->second.extensions.find(kExtensionId);
    return extension != device->second.extensions.end() ? versionIn(extension->second.values) : -1;
}

std::int64_t versionOf(const input::Profile& profile)
{
    if (profile.controls.empty()) {
        return -1;
    }
    const std::string_view name = profile.controls.front().name;
    if (!name.starts_with(kVersionMark)) {
        return -1;
    }
    return parseVersion(name.substr(1)).value_or(-1);
}

std::optional<std::int64_t> addedVersionOf(std::string_view stem)
{
    if (!stem.starts_with(kAddedProfilePrefix)) {
        return std::nullopt;
    }
    return parseVersion(stem.substr(kAddedProfilePrefix.size()));
}

}
