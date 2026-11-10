#include "stability/ChildProcess.h"
#include "stability/PersistedFiles.h"

#include <gs/input/ProfilePatch.h>

#include <doctest/doctest.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
namespace fs = std::filesystem;
using gs::stability::FileKind;
using gs::stability::kFileKinds;
using gs::stability::WriterProgress;
using gs::stability::WriteSlot;

namespace {

constexpr int kRounds = 100;
constexpr std::uint32_t kSeed = 20261030;
constexpr int kLongestRunMs = 150;
constexpr auto kStartLimit = 60s;
constexpr auto kFirstWriteLimit = 5s;
constexpr auto kTemporaryLimit = 10s;
constexpr std::size_t kMostAddedProfiles = 2;

class TemporaryDirectory
{
public:
    TemporaryDirectory()
        : m_path(fs::temp_directory_path() /
                 ("gs-persist-" + std::to_string(::GetCurrentProcessId()) + "-" + std::to_string(s_next++)))
    {
        fs::create_directories(m_path);
    }
    ~TemporaryDirectory()
    {
        std::error_code ignored;
        fs::remove_all(m_path, ignored);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const fs::path& path() const { return m_path; }

private:
    static inline std::atomic<int> s_next = 0;
    fs::path m_path;
};

class SharedProgress
{
public:
    SharedProgress()
        : m_name(std::format("Local\\gs-persist-{}-{}", ::GetCurrentProcessId(), s_next++))
        , m_mapping(::CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(WriterProgress),
                                         m_name.c_str()))
        , m_view(m_mapping != nullptr ? ::MapViewOfFile(m_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(WriterProgress))
                                      : nullptr)
    {
    }

    ~SharedProgress()
    {
        if (m_view != nullptr) {
            ::UnmapViewOfFile(m_view);
        }
        if (m_mapping != nullptr) {
            ::CloseHandle(m_mapping);
        }
    }

    SharedProgress(const SharedProgress&) = delete;
    SharedProgress& operator=(const SharedProgress&) = delete;

    [[nodiscard]] bool valid() const noexcept { return m_view != nullptr; }
    [[nodiscard]] std::wstring name() const { return {m_name.begin(), m_name.end()}; }

    WriterProgress& reset() { return *std::construct_at(static_cast<WriterProgress*>(m_view)); }

private:
    static inline std::atomic<int> s_next = 0;
    std::string m_name;
    HANDLE m_mapping;
    void* m_view;
};

class ScopedVariable
{
public:
    ScopedVariable(const wchar_t* name, const wchar_t* value)
        : m_name(name)
    {
        std::array<wchar_t, 32768> buffer{};
        const DWORD length = ::GetEnvironmentVariableW(name, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length > 0 && length < buffer.size()) {
            m_previous = std::wstring(buffer.data(), length);
        }
        ::SetEnvironmentVariableW(name, value);
    }

    ~ScopedVariable() { ::SetEnvironmentVariableW(m_name, m_previous ? m_previous->c_str() : nullptr); }

    ScopedVariable(const ScopedVariable&) = delete;
    ScopedVariable& operator=(const ScopedVariable&) = delete;

private:
    const wchar_t* m_name;
    std::optional<std::wstring> m_previous;
};

std::string generic(const fs::path& path)
{
    const std::u8string text = path.generic_u8string();
    return {text.begin(), text.end()};
}

fs::path temporaryOf(const fs::path& data, FileKind kind, std::int64_t version)
{
    fs::path target;
    switch (kind) {
    case FileKind::Settings:
        target = gs::stability::settingsPath(data);
        break;
    case FileKind::Devices:
        target = gs::stability::devicesPath(data);
        break;
    case FileKind::Profile:
        target = gs::stability::savedProfilePath(data);
        break;
    case FileKind::AddedProfile:
        target = gs::stability::addedProfilePath(data, version);
        break;
    case FileKind::Patch:
        target = gs::stability::patchPath(data);
        break;
    }
    target += gs::stability::kTemporarySuffix;
    return target;
}

bool reachTemporary(const fs::path& data, const WriterProgress& progress, const std::vector<std::string>& leftover,
                    const gs::test::ChildProcess& writer)
{
    const auto limit = std::chrono::steady_clock::now() + kTemporaryLimit;
    while (writer.running() && std::chrono::steady_clock::now() < limit) {
        if (const std::int32_t saving = progress.writing.load(); saving >= 0) {
            const auto kind = static_cast<std::size_t>(saving);
            const fs::path temporary =
                temporaryOf(data, static_cast<FileKind>(kind), progress.files[kind].started.load());
            std::error_code ignored;
            if (std::ranges::find(leftover, generic(temporary.lexically_relative(data))) == leftover.end() &&
                fs::exists(temporary, ignored)) {
                return true;
            }
        }
        std::this_thread::yield();
    }
    return false;
}

struct Target {
    FileKind kind = FileKind::Settings;
    std::string relative;
};

class Inspection
{
public:
    Inspection(const fs::path& data, const WriterProgress& progress, const gs::input::Profile& base)
        : m_data(data)
        , m_progress(progress)
        , m_base(base)
        , m_extensionJson(gs::stability::extensionProfileJson(base))
        , m_targets{{{FileKind::Settings, relative(gs::stability::settingsPath(data))},
                     {FileKind::Devices, relative(gs::stability::devicesPath(data))},
                     {FileKind::Profile, relative(gs::stability::savedProfilePath(data))},
                     {FileKind::Patch, relative(gs::stability::patchPath(data))}}}
    {
        walk();
        for (const Target& target : m_targets) {
            const WriteSlot& slot = slotOf(target.kind);
            if (slot.done.load() >= 0 && !m_present[gs::stability::indexOf(target.kind)]) {
                problem(std::format("{} is missing although version {} was saved", target.relative, slot.done.load()));
            }
        }
        const WriteSlot& added = slotOf(FileKind::AddedProfile);
        if (added.done.load() >= 0 && m_added == 0) {
            problem(std::format("no added profile is left although version {} was added", added.done.load()));
        }
        if (m_added > kMostAddedProfiles) {
            problem(std::format("{} added profiles pile up", m_added));
        }
        if (m_temporaries.size() > kFileKinds) {
            problem(std::format("{} temporary files pile up", m_temporaries.size()));
        }
    }

    [[nodiscard]] const std::vector<std::string>& problems() const noexcept { return m_problems; }
    [[nodiscard]] const std::vector<std::string>& temporaries() const noexcept { return m_temporaries; }

private:
    [[nodiscard]] std::string relative(const fs::path& path) const { return generic(path.lexically_relative(m_data)); }

    [[nodiscard]] const WriteSlot& slotOf(FileKind kind) const
    {
        return m_progress.files[gs::stability::indexOf(kind)];
    }

    void problem(std::string text) { m_problems.push_back(std::move(text)); }

    void walk()
    {
        const std::array<std::string, 3> folders{relative(gs::stability::profilesPath(m_data)),
                                                 relative(gs::stability::patchPath(m_data).parent_path().parent_path()),
                                                 relative(gs::stability::patchPath(m_data).parent_path())};
        std::error_code error;
        for (fs::recursive_directory_iterator it(m_data, error), end; !error && it != end; it.increment(error)) {
            const std::string name = relative(it->path());
            std::error_code ignored;
            if (it->is_directory(ignored)) {
                if (std::ranges::find(folders, name) == folders.end()) {
                    problem("unexpected folder " + name);
                }
                continue;
            }
            if (name.ends_with(gs::stability::kTemporarySuffix)) {
                const std::string target = name.substr(0, name.size() - gs::stability::kTemporarySuffix.size());
                if (!known(target)) {
                    problem("unexpected temporary file " + name);
                }
                m_temporaries.push_back(name);
                continue;
            }
            if (const Target* target = targetOf(name)) {
                m_present[gs::stability::indexOf(target->kind)] = true;
                check(target->kind, it->path(), name);
            } else if (const std::optional<std::int64_t> version = addedVersion(name)) {
                ++m_added;
                const std::optional<std::int64_t> inside = check(FileKind::AddedProfile, it->path(), name);
                if (inside && *inside != *version) {
                    problem(std::format("{} holds version {}", name, *inside));
                }
            } else {
                problem("unexpected file " + name);
            }
        }
        if (error) {
            problem("the data folder cannot be listed: " + error.message());
        }
    }

    [[nodiscard]] const Target* targetOf(std::string_view name) const
    {
        const auto found = std::ranges::find(m_targets, name, &Target::relative);
        return found != m_targets.end() ? &*found : nullptr;
    }

    [[nodiscard]] std::optional<std::int64_t> addedVersion(std::string_view name) const
    {
        const std::string folder = relative(gs::stability::profilesPath(m_data)) + "/";
        if (!name.starts_with(folder) || !name.ends_with(".json")) {
            return std::nullopt;
        }
        const std::string_view stem = name.substr(folder.size(), name.size() - folder.size() - 5);
        return gs::stability::addedVersionOf(stem);
    }

    [[nodiscard]] bool known(std::string_view name) const
    {
        return targetOf(name) != nullptr || addedVersion(name).has_value();
    }

    std::optional<std::int64_t> check(FileKind kind, const fs::path& path, const std::string& name)
    {
        const std::optional<std::string> text = gs::stability::readText(path);
        if (!text) {
            problem(name + " cannot be read");
            return std::nullopt;
        }
        if (text->empty()) {
            problem(name + " is empty");
            return std::nullopt;
        }
        std::string why;
        const std::optional<std::int64_t> version = versionIn(kind, *text, why);
        if (!version) {
            problem(std::format("{} {} ({} bytes)", name, why, text->size()));
            return std::nullopt;
        }
        if (*text != gs::stability::expectedContent(kind, m_base, *version)) {
            problem(std::format("{} is not exactly version {} ({} bytes)", name, *version, text->size()));
            return std::nullopt;
        }
        const WriteSlot& slot = slotOf(kind);
        if (*version < slot.done.load() || *version > slot.started.load()) {
            problem(std::format("{} is version {}, but the last saved version is {} and the one being written {}", name,
                                *version, slot.done.load(), slot.started.load()));
        }
        return version;
    }

    [[nodiscard]] std::optional<std::int64_t> versionIn(FileKind kind, const std::string& text, std::string& why) const
    {
        std::int64_t version = -1;
        switch (kind) {
        case FileKind::Settings: {
            const auto loaded = gs::app::settings::load<gs::app::settings::Settings>(text);
            if (!loaded.usable || !loaded.diagnostics.empty()) {
                why = "does not load as settings";
                return std::nullopt;
            }
            version = gs::stability::versionOf(loaded.document);
            break;
        }
        case FileKind::Devices: {
            const auto loaded = gs::app::settings::load<gs::app::settings::Devices>(text);
            if (!loaded.usable || !loaded.diagnostics.empty()) {
                why = "does not load as the device store";
                return std::nullopt;
            }
            version = gs::stability::versionOf(loaded.document);
            break;
        }
        case FileKind::Profile:
        case FileKind::AddedProfile: {
            const gs::input::LoadResult loaded = gs::input::loadProfile(text);
            if (!loaded.ok()) {
                why = "does not load as a profile";
                return std::nullopt;
            }
            version = gs::stability::versionOf(loaded.profile);
            break;
        }
        case FileKind::Patch: {
            const gs::input::PatchOutcome patched = gs::input::applyPatch(m_extensionJson, text);
            const gs::input::LoadResult loaded = gs::input::loadProfile(patched.profile);
            if (!patched.applied || !loaded.ok()) {
                why = "does not apply as a patch";
                return std::nullopt;
            }
            version = gs::stability::versionOf(loaded.profile);
            break;
        }
        }
        if (version < 0) {
            why = "carries no version";
            return std::nullopt;
        }
        return version;
    }

    const fs::path& m_data;
    const WriterProgress& m_progress;
    const gs::input::Profile& m_base;
    const std::string m_extensionJson;
    const std::array<Target, 4> m_targets;
    std::array<bool, kFileKinds> m_present{};
    std::size_t m_added = 0;
    std::vector<std::string> m_temporaries;
    std::vector<std::string> m_problems;
};

}

TEST_CASE("the writer of the kill test refuses to run without GS_DATA_DIR")
{
    const ScopedVariable unset(L"GS_DATA_DIR", nullptr);
    SharedProgress shared;
    REQUIRE(shared.valid());
    shared.reset();
    gs::test::ChildProcess writer(GS_PERSIST_WRITER, {shared.name(), L"0"});
    REQUIRE(writer.started());
    REQUIRE(writer.waitFor(30s));
    CHECK(writer.exitCode() == 2u);
}

TEST_CASE("settings, devices, profiles and patches stay whole when the app is killed while saving, 100 times (REL-005)")
{
    const std::optional<gs::input::Profile> base = gs::stability::loadTemplate();
    REQUIRE(base);
    const TemporaryDirectory directory;
    const ScopedVariable dataDirectory(L"GS_DATA_DIR", directory.path().c_str());
    SharedProgress shared;
    REQUIRE(shared.valid());

    std::mt19937 random(kSeed);
    std::uniform_int_distribution<int> runFor(0, kLongestRunMs);
    std::array<std::uint64_t, kFileKinds> writes{};
    std::array<std::uint64_t, kFileKinds> failures{};
    std::array<int, kFileKinds> killedWhileSaving{};
    int interrupted = 0;
    int aimedInterrupted = 0;
    int sawTemporary = 0;
    std::vector<std::string> leftover;
    std::size_t mostTemporaries = 0;
    int checked = 0;
    for (int round = 0; round < kRounds; ++round) {
        CAPTURE(round);
        WriterProgress& progress = shared.reset();
        gs::test::ChildProcess writer(GS_PERSIST_WRITER,
                                      {shared.name(), std::to_wstring(static_cast<std::size_t>(round) % kFileKinds)});
        REQUIRE(writer.started());
        const auto deadline = std::chrono::steady_clock::now() + kStartLimit;
        while (progress.ready.load() == 0u && writer.running() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(1ms);
        }
        REQUIRE_MESSAGE(progress.ready.load() == 1u,
                        "the writer did not start; exit code " << writer.exitCode().value_or(0));
        const std::size_t firstKind = static_cast<std::size_t>(round) % kFileKinds;
        const auto firstWrite = std::chrono::steady_clock::now() + kFirstWriteLimit;
        while (progress.files[firstKind].writes.load() == 0u && writer.running() &&
               std::chrono::steady_clock::now() < firstWrite) {
            std::this_thread::sleep_for(1ms);
        }
        const bool aimed = round % 2 == 1;
        if (aimed) {
            sawTemporary += reachTemporary(directory.path(), progress, leftover, writer) ? 1 : 0;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(runFor(random)));
        }
        CHECK_MESSAGE(writer.running(), "the writer stopped by itself; exit code " << writer.exitCode().value_or(0));
        writer.kill();

        for (std::size_t kind = 0; kind < kFileKinds; ++kind) {
            writes[kind] += progress.files[kind].writes.load();
            failures[kind] += progress.files[kind].failures.load();
        }
        if (const std::int32_t saving = progress.writing.load(); saving >= 0) {
            ++killedWhileSaving[static_cast<std::size_t>(saving)];
        }
        const Inspection inspection(directory.path(), progress, *base);
        const bool leftNew = std::ranges::any_of(inspection.temporaries(), [&leftover](const std::string& name) {
            return std::ranges::find(leftover, name) == leftover.end();
        });
        interrupted += leftNew ? 1 : 0;
        aimedInterrupted += aimed && leftNew ? 1 : 0;
        mostTemporaries = std::max(mostTemporaries, inspection.temporaries().size());
        leftover = inspection.temporaries();
        for (const std::string& problem : inspection.problems()) {
            FAIL_CHECK(problem);
        }
        if (!inspection.problems().empty()) {
            break;
        }
        ++checked;
    }

    CHECK(checked == kRounds);
    int killedInside = 0;
    for (std::size_t kind = 0; kind < kFileKinds; ++kind) {
        CAPTURE(gs::stability::kFileKindNames[kind]);
        CHECK(writes[kind] > 0);
        killedInside += killedWhileSaving[kind];
        MESSAGE(std::format("{}: {} saves, {} failed, killed while saving it {} times",
                            gs::stability::kFileKindNames[kind], writes[kind], failures[kind], killedWhileSaving[kind]));
    }
    CHECK(killedInside >= kRounds / 2);
    CHECK(aimedInterrupted >= kRounds / 4);
    MESSAGE(std::format("{} of {} kills aimed at a save's temporary file left it behind ({} saw it before the kill)",
                        aimedInterrupted, kRounds / 2, sawTemporary));
    MESSAGE(std::format("{} of {} kills left a temporary file behind (killed between creating and renaming it); at most "
                        "{} at once",
                        interrupted, checked, mostTemporaries));
}
