#include "gs/ext/Package.h"

#include "gs/ext/Archive.h"
#include "gs/ext/FilterSource.h"
#include "gs/ext/Signing.h"

#include <gs/platform/AtomicFile.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <random>
#include <string>
#include <system_error>
#include <utility>

namespace gs::ext {

namespace {

namespace fs = std::filesystem;

using Reader = std::function<std::optional<std::string>(std::string_view path, std::uint64_t maxBytes)>;

fs::path pathOf(std::string_view text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string textOf(const fs::path& path)
{
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::optional<std::string> readFrom(const fs::path& directory, std::string_view path, std::uint64_t maxBytes)
{
    std::ifstream in(directory / pathOf(path), std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string text(static_cast<std::size_t>(maxBytes) + 1, '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (in.bad()) {
        return std::nullopt;
    }
    text.resize(static_cast<std::size_t>(in.gcount()));
    if (text.size() > maxBytes) {
        return std::nullopt;
    }
    return text;
}

std::string randomName()
{
    constexpr std::string_view digits = "0123456789abcdef";
    std::random_device device;
    std::string name;
    for (int i = 0; i < 16; ++i) {
        name.push_back(digits[device() % digits.size()]);
    }
    return name;
}

void add(std::vector<Diagnostic>& diagnostics, Code code, std::string path, std::string detail = {})
{
    diagnostics.push_back({Severity::Error, code, std::move(path), std::move(detail)});
}

const ArchiveEntry* entryOf(const ArchiveListing& listing, std::string_view path)
{
    const auto at = std::ranges::lower_bound(listing.files, path, {}, &ArchiveEntry::path);
    return at != listing.files.end() && at->path == path ? &*at : nullptr;
}

bool fits(ParamType param, SettingType setting) noexcept
{
    switch (param) {
    case ParamType::Float:
        return setting == SettingType::Number || setting == SettingType::Integer;
    case ParamType::Int:
        return setting == SettingType::Integer;
    case ParamType::Bool:
        return setting == SettingType::Boolean;
    }
    return false;
}

std::string located(const std::string& file, std::string_view where)
{
    constexpr std::string_view line = "line ";
    return where.starts_with(line) ? file + ":" + std::string(where.substr(line.size())) : file;
}

void checkFilter(const Manifest& manifest, std::size_t index, const ArchiveListing& listing, const Reader& reader,
                 std::vector<Diagnostic>& diagnostics)
{
    const FilterContribution& filter = manifest.contributes.filters[index];
    const std::string field = "$.contributes.filters[" + std::to_string(index) + "].file";
    const ArchiveEntry* entry = entryOf(listing, filter.file);
    if (entry == nullptr) {
        add(diagnostics, Code::FileMissing, field, filter.file);
        return;
    }
    if (entry->size > kMaxFilterSourceBytes) {
        add(diagnostics, Code::TooLarge, field, std::to_string(entry->size));
        return;
    }
    const std::optional<std::string> text = reader(filter.file, kMaxFilterSourceBytes);
    if (!text) {
        add(diagnostics, Code::ArchiveCorrupt, field, filter.file);
        return;
    }
    FilterSource source = parseFilterSource(*text);
    for (Diagnostic& diagnostic : source.diagnostics) {
        diagnostic.path = located(filter.file, diagnostic.path);
        diagnostics.push_back(std::move(diagnostic));
    }
    for (const FilterTexture& texture : source.textures) {
        if (entryOf(listing, texture.file) == nullptr) {
            add(diagnostics, Code::FileMissing, filter.file + ":" + std::to_string(texture.line), texture.file);
        }
    }
    for (const FilterParam& param : source.params) {
        const auto setting = std::ranges::find(manifest.settings, param.name, &SettingDeclaration::key);
        if (setting != manifest.settings.end() && !fits(param.type, setting->type)) {
            add(diagnostics, Code::FilterSettingMismatch,
                "$.settings[" + std::to_string(setting - manifest.settings.begin()) + "].type", param.name);
        }
    }
}

PackageCheck checkFiles(const ArchiveListing& listing, const Reader& reader, const LoadOptions& options)
{
    PackageCheck check;
    check.diagnostics = listing.problems;
    if (hasErrors(check.diagnostics)) {
        return check;
    }
    const std::string manifestFile(kManifestFileName);
    const ArchiveEntry* entry = entryOf(listing, manifestFile);
    if (entry == nullptr) {
        add(check.diagnostics, Code::ManifestMissing, manifestFile);
        return check;
    }
    if (entry->size > kMaxManifestBytes) {
        add(check.diagnostics, Code::TooLarge, manifestFile, std::to_string(entry->size));
        return check;
    }
    const std::optional<std::string> text = reader(manifestFile, kMaxManifestBytes);
    if (!text) {
        add(check.diagnostics, Code::ArchiveCorrupt, manifestFile);
        return check;
    }
    LoadedManifest loaded = loadManifest(*text, options);
    std::ranges::move(loaded.diagnostics, std::back_inserter(check.diagnostics));
    if (!loaded.usable) {
        return check;
    }
    for (std::size_t index = 0; index < loaded.manifest.contributes.filters.size(); ++index) {
        checkFilter(loaded.manifest, index, listing, reader, check.diagnostics);
    }
    if (const std::optional<NativeContribution>& native = loaded.manifest.contributes.native) {
        for (const auto& [architecture, file] : nativeDlls(*native)) {
            if (entryOf(listing, file) == nullptr) {
                add(check.diagnostics, Code::FileMissing, nativeDllField(architecture), file);
            }
        }
    }
    for (std::size_t index = 0; index < loaded.manifest.contributes.scripts.size(); ++index) {
        const ScriptContribution& script = loaded.manifest.contributes.scripts[index];
        if (entryOf(listing, script.entry) == nullptr) {
            add(check.diagnostics, Code::FileMissing, "$.contributes.scripts[" + std::to_string(index) + "].entry",
                script.entry);
        }
    }
    check.manifest = std::move(loaded.manifest);
    return check;
}

bool safeFolderName(std::string_view name)
{
    return name.find('/') == std::string_view::npos && safeArchivePath(name);
}

constexpr std::string_view kRestoreSuffix = ".restore";

bool safeRestoreTarget(std::string_view relative)
{
    const std::size_t slash = relative.find('/');
    if (slash == std::string_view::npos || relative.find('/', slash + 1) != std::string_view::npos) {
        return false;
    }
    const std::string_view id = relative.substr(0, slash);
    return safeFolderName(id) && !id.starts_with('.') && safeFolderName(relative.substr(slash + 1));
}

void restoreInterrupted(const fs::path& root, const fs::path& staging, std::string_view journalName)
{
    std::ifstream in(staging / pathOf(journalName), std::ios::binary);
    const std::string relative{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    in.close();
    if (!safeRestoreTarget(relative)) {
        return;
    }
    const fs::path backup = staging / pathOf(journalName.substr(0, journalName.size() - kRestoreSuffix.size()));
    const fs::path target = root / pathOf(relative);
    std::error_code error;
    if (!fs::is_directory(backup, error) || fs::exists(target, error)) {
        return;
    }
    fs::create_directories(target.parent_path(), error);
    fs::rename(backup, target, error);
}

}

PackageCheck checkPackage(const std::filesystem::path& archive, const LoadOptions& options)
{
    return checkFiles(
        inspectArchive(archive),
        [&](std::string_view path, std::uint64_t maxBytes) { return readArchiveFile(archive, path, maxBytes); },
        options);
}

PackageCheck checkDirectory(const std::filesystem::path& directory, const LoadOptions& options)
{
    return checkFiles(
        inspectDirectory(directory),
        [&](std::string_view path, std::uint64_t maxBytes) { return readFrom(directory, path, maxBytes); },
        options);
}

std::filesystem::path installedDirectory(const std::filesystem::path& root, std::string_view id,
                                         const Version& version)
{
    const std::string versionText = version.text();
    if (!safeFolderName(id) || id.starts_with('.') || !safeFolderName(versionText)) {
        return {};
    }
    return root / pathOf(id) / pathOf(versionText);
}

InstalledExtension installPackage(const std::filesystem::path& archive, const std::filesystem::path& root,
                                  const LoadOptions& options)
{
    InstalledExtension result;
    const fs::path staging = root / pathOf(kStagingFolder) / randomName();
    result.diagnostics = extractArchive(archive, staging);
    if (hasErrors(result.diagnostics)) {
        return result;
    }
    PackageCheck check = checkDirectory(staging, options);
    result.diagnostics = std::move(check.diagnostics);
    result.manifest = std::move(check.manifest);
    std::error_code error;
    const fs::path target =
        result.manifest ? installedDirectory(root, result.manifest->id, result.manifest->version) : fs::path();
    if (result.manifest && target.empty()) {
        add(result.diagnostics, Code::IdInvalid, "$.id", result.manifest->id);
    }
    std::optional<MinisignKey> remembered;
    SignatureCheck incoming;
    if (result.ok()) {
        remembered = publisherOf(root, result.manifest->id);
        incoming = checkSignature(directoryContents(staging), result.manifest->authorKey);
        if (std::optional<Diagnostic> refused = publisherProblem(remembered, incoming)) {
            result.diagnostics.push_back(std::move(*refused));
        }
    }
    if (!result.ok()) {
        fs::remove_all(staging, error);
        return result;
    }
    if (const std::error_code flushed = platform::flushTree(staging)) {
        add(result.diagnostics, Code::ArchiveWriteFailed, textOf(staging), flushed.message());
        fs::remove_all(staging, error);
        return result;
    }
    const std::string backupName = randomName();
    const fs::path previous = root / pathOf(kStagingFolder) / pathOf(backupName);
    const fs::path journal = root / pathOf(kStagingFolder) / pathOf(backupName + std::string(kRestoreSuffix));
    fs::create_directories(target.parent_path(), error);
    const bool replacing = !error && fs::exists(target, error);
    if (replacing) {
        error = platform::writeFileAtomically(journal, textOf(target.lexically_relative(root)));
        if (!error) {
            fs::rename(target, previous, error);
        }
    }
    if (!error) {
        fs::rename(staging, target, error);
    }
    if (error) {
        add(result.diagnostics, Code::ArchiveWriteFailed, textOf(target), error.message());
        std::error_code ignored;
        if (replacing && !fs::exists(target, ignored)) {
            fs::rename(previous, target, ignored);
        }
        fs::remove(journal, ignored);
        fs::remove_all(staging, ignored);
        return result;
    }
    if (replacing) {
        fs::remove(journal, error);
        fs::remove_all(previous, error);
    }
    if (incoming.state == SignatureState::Valid && incoming.key) {
        static_cast<void>(rememberPublisher(root, result.manifest->id, *incoming.key));
    }
    result.directory = target;
    return result;
}

std::vector<InstalledExtension> scanInstalled(const std::filesystem::path& root, const LoadOptions& options)
{
    std::vector<InstalledExtension> found;
    std::error_code error;
    for (auto ids = fs::directory_iterator(root, error); !error && ids != fs::directory_iterator();
         ids.increment(error)) {
        std::error_code ignored;
        const std::string id = textOf(ids->path().filename());
        if (id.starts_with('.') || ids->is_symlink(ignored) || !ids->is_directory(ignored)) {
            continue;
        }
        std::error_code inner;
        for (auto versions = fs::directory_iterator(ids->path(), inner);
             !inner && versions != fs::directory_iterator(); versions.increment(inner)) {
            if (versions->is_symlink(ignored) || !versions->is_directory(ignored)) {
                continue;
            }
            const std::string version = textOf(versions->path().filename());
            PackageCheck check = checkDirectory(versions->path(), options);
            InstalledExtension extension{versions->path(), std::move(check.manifest), std::move(check.diagnostics)};
            if (extension.manifest &&
                (extension.manifest->id != id || extension.manifest->version.text() != version)) {
                add(extension.diagnostics, Code::InstallMismatch, id + "/" + version,
                    extension.manifest->id + "/" + extension.manifest->version.text());
            }
            found.push_back(std::move(extension));
        }
    }
    std::ranges::sort(found, {}, &InstalledExtension::directory);
    return found;
}

bool removeInstalled(const std::filesystem::path& root, std::string_view id, const Version& version)
{
    const fs::path target = installedDirectory(root, id, version);
    std::error_code error;
    if (target.empty() || !fs::exists(target, error)) {
        return false;
    }
    fs::remove_all(target, error);
    if (error) {
        return false;
    }
    std::error_code ignored;
    fs::remove(target.parent_path(), ignored);
    return true;
}

void clearStaging(const std::filesystem::path& root)
{
    const fs::path staging = root / pathOf(kStagingFolder);
    std::vector<std::string> journals;
    std::error_code error;
    for (auto at = fs::directory_iterator(staging, error); !error && at != fs::directory_iterator();
         at.increment(error)) {
        std::string name = textOf(at->path().filename());
        if (name.ends_with(kRestoreSuffix)) {
            journals.push_back(std::move(name));
        }
    }
    for (const std::string& name : journals) {
        restoreInterrupted(root, staging, name);
    }
    fs::remove_all(staging, error);
}

}
