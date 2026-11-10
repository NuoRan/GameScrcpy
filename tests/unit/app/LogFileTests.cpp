#include "LogFile.h"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using gs::app::LogFile;

namespace {

std::vector<std::string> lines(const std::filesystem::path& file)
{
    std::vector<std::string> result;
    std::ifstream in(file);
    for (std::string line; std::getline(in, line);) {
        result.push_back(line);
    }
    return result;
}

std::filesystem::path freshDirectory()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto directory = std::filesystem::temp_directory_path() / ("gs_logfile_test_" + std::to_string(stamp));
    std::filesystem::remove_all(directory);
    return directory;
}

}

TEST_CASE("log lines reach the file in order, and earlier runs are appended to")
{
    const auto directory = freshDirectory();
    {
        LogFile log(directory, "app");
        log.append("first");
        log.append("second");
    }
    {
        LogFile log(directory, "app");
        log.append("third");
    }
    CHECK(lines(directory / "app.log") == std::vector<std::string>{"first", "second", "third"});
    std::filesystem::remove_all(directory);
}

TEST_CASE("the file rotates by size and keeps only the newest files")
{
    const auto directory = freshDirectory();
    {
        LogFile log(directory, "app", 30, 2);
        for (int i = 0; i < 10; ++i) {
            log.append("line " + std::to_string(i) + "...");
        }
    }
    CHECK(lines(directory / "app.log") == std::vector<std::string>{"line 9..."});
    CHECK(lines(directory / "app.1.log") == std::vector<std::string>{"line 6...", "line 7...", "line 8..."});
    CHECK(lines(directory / "app.2.log") == std::vector<std::string>{"line 3...", "line 4...", "line 5..."});
    CHECK_FALSE(std::filesystem::exists(directory / "app.3.log"));
    std::filesystem::remove_all(directory);
}
