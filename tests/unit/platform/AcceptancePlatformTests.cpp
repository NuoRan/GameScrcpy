#include "stability/ChildProcess.h"

#include <gs/platform/CrashHandler.h>

#include <doctest/doctest.h>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

using namespace std::chrono_literals;

namespace {

#if defined(__SANITIZE_ADDRESS__)
constexpr bool kAddressSanitizer = true;
#else
constexpr bool kAddressSanitizer = false;
#endif

class TemporaryDirectory
{
public:
    TemporaryDirectory()
        : m_path(std::filesystem::temp_directory_path() /
                 ("gs-acceptance-crash-" + std::to_string(::GetCurrentProcessId()) + "-" + std::to_string(s_next++)))
    {
        std::filesystem::create_directories(m_path);
    }
    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return m_path; }

private:
    static inline std::atomic<int> s_next = 0;
    std::filesystem::path m_path;
};

std::string contentsOf(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}

TEST_CASE("REL-004: a crash writes a minidump, a summary and the log into a crash folder of its own" *
          doctest::skip(kAddressSanitizer))
{
    const TemporaryDirectory directory;
    const std::filesystem::path crashes = directory.path() / "crashes";
    const std::filesystem::path logFile = directory.path() / "gamescrcpy.log";
    const std::string lastWords = "the last line before the crash\n";
    {
        std::ofstream out(logFile, std::ios::binary | std::ios::trunc);
        out << lastWords;
    }

    gs::test::ChildProcess child(GS_CRASH_CHILD, {crashes.wstring(), logFile.wstring()});
    REQUIRE(child.started());
    REQUIRE(child.waitFor(60s));
    const auto code = child.exitCode();
    REQUIRE(code.has_value());
    MESSAGE("the crashing program ended with " << std::format("0x{:08X}", *code));
    CHECK(*code == static_cast<DWORD>(EXCEPTION_ACCESS_VIOLATION));
    REQUIRE(std::filesystem::is_directory(crashes));

    std::vector<std::filesystem::path> folders;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(crashes)) {
        if (entry.is_directory()) {
            folders.push_back(entry.path());
        }
    }
    REQUIRE(folders.size() == 1);
    const std::filesystem::path folder = folders.front();
    const std::string name = folder.filename().string();
    CHECK(name.starts_with(gs::platform::kCrashFolderPrefix));
    CHECK(name.size() == gs::platform::kCrashFolderLength);

    const std::filesystem::path dump = folder / (name + ".dmp");
    REQUIRE(std::filesystem::exists(dump));
    const std::string dumped = contentsOf(dump);
    REQUIRE(dumped.size() > 4);
    CHECK(dumped.substr(0, 4) == "MDMP");

    const std::string summary = contentsOf(folder / "crash.txt");
    MESSAGE(summary);
    CHECK(summary.starts_with("version "));
    CHECK(summary.find("exception 0xC0000005 access violation writing 0x") != std::string::npos);
    CHECK(summary.find("thread ") != std::string::npos);

    CHECK(contentsOf(folder / "gamescrcpy.log") == lastWords);
}
