#include "stability/ChildProcess.h"

#include <gs/platform/AtomicFile.h>

#include <doctest/doctest.h>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace {

class TemporaryDirectory
{
public:
    TemporaryDirectory()
        : m_path(std::filesystem::temp_directory_path() /
                 ("gs-atomic-" + std::to_string(::GetCurrentProcessId()) + "-" + std::to_string(s_next++)))
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

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

class HeldFile
{
public:
    HeldFile(const std::filesystem::path& path, DWORD share)
        : m_handle(::CreateFileW(path.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                 nullptr))
    {
    }
    ~HeldFile()
    {
        if (valid()) {
            ::CloseHandle(m_handle);
        }
    }
    HeldFile(const HeldFile&) = delete;
    HeldFile& operator=(const HeldFile&) = delete;

    [[nodiscard]] bool valid() const { return m_handle != INVALID_HANDLE_VALUE; }

    [[nodiscard]] std::string read() const
    {
        std::string text(64, '\0');
        DWORD got = 0;
        if (::ReadFile(m_handle, text.data(), static_cast<DWORD>(text.size()), &got, nullptr) == FALSE) {
            return {};
        }
        text.resize(got);
        return text;
    }

private:
    HANDLE m_handle;
};

constexpr DWORD kShareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

}

TEST_CASE("an atomic write creates or replaces the file and leaves no temporary")
{
    const TemporaryDirectory directory;
    const auto file = directory.path() / "settings.json";
    CHECK_FALSE(gs::platform::writeFileAtomically(file, "first"));
    CHECK(readFile(file) == "first");
    CHECK_FALSE(gs::platform::writeFileAtomically(file, "second, and longer"));
    CHECK(readFile(file) == "second, and longer");
    CHECK_FALSE(gs::platform::writeFileAtomically(file, ""));
    CHECK(readFile(file).empty());
    CHECK_FALSE(std::filesystem::exists(directory.path() / "settings.json.tmp"));
}

TEST_CASE("a temporary left by an interrupted write is overwritten")
{
    const TemporaryDirectory directory;
    const auto file = directory.path() / "settings.json";
    REQUIRE_FALSE(gs::platform::writeFileAtomically(file, "kept"));
    std::ofstream(directory.path() / "settings.json.tmp", std::ios::binary) << "half a fi";
    CHECK(readFile(file) == "kept");
    CHECK_FALSE(gs::platform::writeFileAtomically(file, "new"));
    CHECK(readFile(file) == "new");
    CHECK_FALSE(std::filesystem::exists(directory.path() / "settings.json.tmp"));
}

TEST_CASE("replacing moves a finished temporary over the target, and flushing works on files and folders")
{
    const TemporaryDirectory directory;
    const auto target = directory.path() / "package.gsext";
    const auto temporary = directory.path() / "package.gsext.tmp";
    std::ofstream(target, std::ios::binary) << "old";
    std::ofstream(temporary, std::ios::binary) << "new";
    {
        const HeldFile reader(target, kShareAll);
        REQUIRE(reader.valid());
        CHECK_FALSE(gs::platform::replaceFile(temporary, target));
        CHECK(reader.read() == "old");
    }
    CHECK(readFile(target) == "new");
    CHECK_FALSE(std::filesystem::exists(temporary));
    CHECK(gs::platform::replaceFile(temporary, target));
    CHECK(readFile(target) == "new");

    std::filesystem::create_directories(directory.path() / "tree" / "inner");
    std::ofstream(directory.path() / "tree" / "a.txt", std::ios::binary) << "a";
    std::ofstream(directory.path() / "tree" / "inner" / "b.txt", std::ios::binary) << "b";
    CHECK_FALSE(gs::platform::flushTree(directory.path() / "tree"));
    CHECK_FALSE(gs::platform::flushFile(directory.path() / "tree" / "a.txt"));
    CHECK(gs::platform::flushFile(directory.path() / "missing.txt"));
    CHECK(gs::platform::flushTree(directory.path() / "missing"));
}

TEST_CASE("an atomic write into a missing directory fails and creates nothing")
{
    const TemporaryDirectory directory;
    const auto file = directory.path() / "missing" / "settings.json";
    CHECK(gs::platform::writeFileAtomically(file, "data"));
    CHECK_FALSE(std::filesystem::exists(directory.path() / "missing"));
}

TEST_CASE("an atomic write replaces a file that another thread keeps looking at or reads with delete sharing")
{
    const TemporaryDirectory directory;
    const auto file = directory.path() / "settings.json";
    REQUIRE_FALSE(gs::platform::writeFileAtomically(file, "first"));

    {
        const HeldFile reader(file, kShareAll);
        REQUIRE(reader.valid());
        CHECK_FALSE(gs::platform::writeFileAtomically(file, "second"));
        CHECK(reader.read() == "first");
    }
    CHECK(readFile(file) == "second");

    std::atomic<bool> stop = false;
    std::thread looker([&] {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        while (!stop.load()) {
            static_cast<void>(::GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &data));
        }
    });
    int failed = 0;
    for (int i = 0; i < 200; ++i) {
        if (gs::platform::writeFileAtomically(file, std::to_string(i))) {
            ++failed;
        }
    }
    stop = true;
    looker.join();
    CHECK(failed == 0);
    CHECK(readFile(file) == "199");
    CHECK_FALSE(std::filesystem::exists(directory.path() / "settings.json.tmp"));
}

TEST_CASE("a reader that does not share deletion makes the write fail after a second, as busy, and keeps the old contents")
{
    const TemporaryDirectory directory;
    const auto file = directory.path() / "settings.json";
    REQUIRE_FALSE(gs::platform::writeFileAtomically(file, "kept"));
    {
        const HeldFile reader(file, FILE_SHARE_READ | FILE_SHARE_WRITE);
        REQUIRE(reader.valid());
        const auto started = std::chrono::steady_clock::now();
        const std::error_code error = gs::platform::writeFileAtomically(file, "lost");
        const auto waited = std::chrono::steady_clock::now() - started;
        CHECK(error);
        CHECK(gs::platform::fileBusy(error));
        CHECK(waited >= 900ms);
        CHECK(waited < 5s);
        CHECK(reader.read() == "kept");
    }
    CHECK(readFile(file) == "kept");
    CHECK_FALSE(std::filesystem::exists(directory.path() / "settings.json.tmp"));
    CHECK_FALSE(gs::platform::writeFileAtomically(file, "next"));
    CHECK(readFile(file) == "next");
}

TEST_CASE("a write waits for a reader that lets go within the second")
{
    const TemporaryDirectory directory;
    const auto file = directory.path() / "settings.json";
    REQUIRE_FALSE(gs::platform::writeFileAtomically(file, "old"));
    std::optional<HeldFile> reader;
    reader.emplace(file, FILE_SHARE_READ | FILE_SHARE_WRITE);
    REQUIRE(reader->valid());
    std::thread letGo([&] {
        std::this_thread::sleep_for(200ms);
        reader.reset();
    });
    const std::error_code error = gs::platform::writeFileAtomically(file, "new");
    letGo.join();
    CHECK_FALSE(error);
    CHECK(readFile(file) == "new");
}

TEST_CASE("reading tells a missing, an oversized and a busy file apart, and waits for a busy one only when asked")
{
    const TemporaryDirectory directory;
    const auto file = directory.path() / "store.json";
    std::string contents = "left over";
    CHECK(gs::platform::fileMissing(gs::platform::readFile(file, 64, contents)));
    CHECK(contents.empty());
    CHECK(gs::platform::fileMissing(gs::platform::readFile(directory.path() / "missing" / "store.json", 64, contents)));

    REQUIRE_FALSE(gs::platform::writeFileAtomically(file, R"({"presses":4})"));
    CHECK_FALSE(gs::platform::readFile(file, 64, contents));
    CHECK(contents == R"({"presses":4})");
    CHECK(gs::platform::readFile(file, 8, contents) == std::errc::file_too_large);
    CHECK_FALSE(gs::platform::fileMissing(gs::platform::readFile(file, 8, contents)));
    {
        const HeldFile reader(file, FILE_SHARE_READ | FILE_SHARE_WRITE);
        REQUIRE(reader.valid());
        CHECK_FALSE(gs::platform::readFile(file, 64, contents, gs::platform::WhenBusy::GiveUp));
        CHECK(contents == R"({"presses":4})");
    }
    {
        const HeldFile owner(file, 0);
        REQUIRE(owner.valid());
        auto started = std::chrono::steady_clock::now();
        std::error_code error = gs::platform::readFile(file, 64, contents, gs::platform::WhenBusy::GiveUp);
        CHECK(gs::platform::fileBusy(error));
        CHECK(std::chrono::steady_clock::now() - started < 500ms);
        started = std::chrono::steady_clock::now();
        error = gs::platform::readFile(file, 64, contents);
        const auto waited = std::chrono::steady_clock::now() - started;
        CHECK(gs::platform::fileBusy(error));
        CHECK(waited >= 900ms);
        CHECK(waited < 5s);
    }
    CHECK_FALSE(gs::platform::readFile(file, 64, contents));
    CHECK(contents == R"({"presses":4})");
}

TEST_CASE("removing a file waits for a reader that does not share deletion, and a missing file counts as removed")
{
    const TemporaryDirectory directory;
    const auto file = directory.path() / "consent.json";
    CHECK_FALSE(gs::platform::removeFile(file));
    CHECK_FALSE(gs::platform::removeFile(directory.path() / "missing" / "consent.json"));

    REQUIRE_FALSE(gs::platform::writeFileAtomically(file, "kept"));
    {
        const HeldFile reader(file, FILE_SHARE_READ | FILE_SHARE_WRITE);
        REQUIRE(reader.valid());
        const auto started = std::chrono::steady_clock::now();
        const std::error_code error = gs::platform::removeFile(file);
        const auto waited = std::chrono::steady_clock::now() - started;
        CHECK(gs::platform::fileBusy(error));
        CHECK(waited >= 900ms);
        CHECK(waited < 5s);
    }
    CHECK(readFile(file) == "kept");

    std::optional<HeldFile> reader;
    reader.emplace(file, FILE_SHARE_READ | FILE_SHARE_WRITE);
    REQUIRE(reader->valid());
    std::thread letGo([&] {
        std::this_thread::sleep_for(200ms);
        reader.reset();
    });
    const std::error_code error = gs::platform::removeFile(file);
    letGo.join();
    CHECK_FALSE(error);
    CHECK_FALSE(std::filesystem::exists(file));
}

TEST_CASE("a refusal that is not about another program is reported at once")
{
    const TemporaryDirectory directory;
    const auto file = directory.path() / "settings.json";
    REQUIRE_FALSE(gs::platform::writeFileAtomically(file, "kept"));
    REQUIRE(::SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE);
    const auto started = std::chrono::steady_clock::now();
    const std::error_code error = gs::platform::writeFileAtomically(file, "refused");
    const auto waited = std::chrono::steady_clock::now() - started;
    CHECK(::SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL) != FALSE);
    CHECK(error);
    CHECK_FALSE(gs::platform::fileBusy(error));
    CHECK(waited < 500ms);
    CHECK(readFile(file) == "kept");
    CHECK_FALSE(std::filesystem::exists(directory.path() / "settings.json.tmp"));
}

TEST_CASE("a writer killed at any moment leaves the file whole")
{
    const TemporaryDirectory directory;
    const auto file = directory.path() / "settings.json";
    const std::string first(256 * 1024, 'a');
    const std::string second(192 * 1024, 'b');
    std::mt19937 random(20261010);
    std::uniform_int_distribution<int> runFor(1, 80);
    int checked = 0;
    for (int round = 0; round < 25; ++round) {
        gs::test::ChildProcess child(GS_ATOMIC_WRITE_CHILD, {file.wstring()});
        REQUIRE(child.started());
        const auto firstWriteDeadline = std::chrono::steady_clock::now() + 10s;
        while (round == 0 && !std::filesystem::exists(file) && std::chrono::steady_clock::now() < firstWriteDeadline) {
            std::this_thread::sleep_for(1ms);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(runFor(random)));
        child.kill();
        if (std::filesystem::exists(file)) {
            const std::string contents = readFile(file);
            CHECK((contents == first || contents == second));
            ++checked;
        }
    }
    CHECK(checked > 0);
}
