#pragma once

#include "gs/script/Replay.h"
#include "gs/script/Services.h"

#include <gs/base/Expected.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::script {

struct ReplayProfile {
    std::vector<ControlInfo> controls;
    std::vector<StateInfo> states;
};

struct ReplayKit {
    std::function<Expected<ReplayProfile, std::string>(const std::filesystem::path& file)> profile;
    std::function<std::unique_ptr<ReplayVision>(const std::filesystem::path& folder, const std::filesystem::path& video,
                                                std::int64_t videoStartPtsUs)>
        vision;
};

struct ReplayRequest {
    std::filesystem::path folder;
    std::string entry;
    std::filesystem::path session;
    std::optional<double> untilMs;
    std::uint64_t seed = 1;
};

struct ReplayRun {
    bool ok = false;
    std::string problem;
    std::vector<ReplayLine> lines;
    std::vector<ReplayMachineState> machines;
    ReplayResult result;
};

enum class TestVerdict : std::uint8_t { Pass, Fail, Error, Updated };

struct TestOutcome {
    std::string name;
    std::filesystem::path file;
    TestVerdict verdict = TestVerdict::Pass;
    std::string detail;
    bool missingRecording = false;
    bool logicFailed = false;
    std::size_t line = 0;
    std::optional<std::string> expected;
    std::optional<std::string> actual;
    std::vector<ReplayLine> lines;
};

inline constexpr std::string_view kTestsFolder = "tests";
inline constexpr std::string_view kTestSuffix = ".test.json";
inline constexpr double kDefaultReplayMs = 60'000.0;
inline constexpr double kLongestReplayMs = 24.0 * 3600.0 * 1000.0;

[[nodiscard]] std::filesystem::path sessionFileOf(const std::filesystem::path& sessionOrRecording);
[[nodiscard]] ReplayRun replaySession(const ReplayRequest& request, const ReplayKit& kit);
[[nodiscard]] std::vector<std::string> outputLines(const std::vector<ReplayLine>& lines);
[[nodiscard]] std::vector<std::filesystem::path> testFiles(const std::filesystem::path& folder);
[[nodiscard]] TestOutcome runTest(const std::filesystem::path& file, const std::filesystem::path& folder,
                                  std::string_view entry, bool update, const ReplayKit& kit);
[[nodiscard]] std::string testText(std::string_view session, const std::vector<std::string>& expect);
[[nodiscard]] std::string_view verdictName(TestVerdict verdict) noexcept;

}
