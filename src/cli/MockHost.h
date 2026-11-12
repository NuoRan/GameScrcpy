#pragma once

#include <gs/ext/Diagnostics.h>
#include <gs/ext/Manifest.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::cli {

using MockJson = nlohmann::ordered_json;

inline constexpr std::string_view kMockTestFormat = "gamescrcpy.ext-test";
inline constexpr std::int64_t kMockTestVersion = 1;
inline constexpr std::string_view kMockTestsFolder = "tests";
inline constexpr std::string_view kMockPhone = "mock-phone";
inline constexpr std::chrono::milliseconds kMockStepWait{10'000};
inline constexpr std::chrono::milliseconds kMockLongestWait{600'000};
inline constexpr std::int64_t kMockLongestJump = 86'400'000;
inline constexpr std::size_t kMockMostSteps = 1000;
inline constexpr std::size_t kMockMostTestBytes = std::size_t{1} << 20;
inline constexpr std::size_t kMockMostDepth = 64;

class TempFolder
{
public:
    explicit TempFolder(std::string_view prefix);
    ~TempFolder();
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

struct MockOptions {
    bool hosted = false;
    bool official = false;
    std::filesystem::path exthost;
};

enum class MockScriptState : std::uint8_t { Loaded, Limited, Failed };

struct MockScript {
    std::string name;
    std::string run;
    MockScriptState state = MockScriptState::Loaded;
    std::string problem;
    std::vector<std::string> logs;
    std::vector<std::string> calls;
};

struct MockExtension {
    std::filesystem::path input;
    std::filesystem::path folder;
    std::shared_ptr<TempFolder> scratch;
    std::optional<ext::Manifest> manifest;
    std::vector<ext::Diagnostic> diagnostics;
    std::vector<MockScript> scripts;
    std::string problem;

    [[nodiscard]] bool usable() const noexcept { return manifest.has_value() && problem.empty(); }
};

struct MockSpec {
    std::string name;
    std::string file;
    MockJson spec;
    std::string problem;
};

struct MockStep {
    std::size_t index = 0;
    std::string action;
    std::string label;
    std::int64_t at = 0;
    MockJson input = MockJson::object();
    MockJson expect = MockJson::object();
    MockJson output = MockJson::object();
    std::vector<std::string> problems;

    [[nodiscard]] bool passed() const noexcept { return problems.empty(); }
};

struct MockTest {
    std::string name;
    std::string file;
    std::string problem;
    std::vector<MockStep> steps;

    [[nodiscard]] bool passed() const noexcept;
};

struct MockRun {
    std::string where;
    std::vector<ext::Diagnostic> warnings;
    std::string problem;
    std::vector<MockTest> tests;

    [[nodiscard]] bool passed() const noexcept;
};

[[nodiscard]] MockExtension openMockExtension(const std::filesystem::path& input, const MockOptions& options);
[[nodiscard]] std::vector<std::filesystem::path> mockTestFiles(const std::filesystem::path& folder);
[[nodiscard]] MockSpec readMockSpec(const std::filesystem::path& file, const std::filesystem::path& base);
[[nodiscard]] MockRun runMockTests(const MockExtension& extension, const std::vector<MockSpec>& specs,
                                   const MockOptions& options);
[[nodiscard]] int mockExitCode(const MockExtension& extension, const MockRun& run) noexcept;
[[nodiscard]] std::string mockReportText(const MockExtension& extension, const MockRun& run);
[[nodiscard]] MockJson mockReportJson(const MockExtension& extension, const MockRun& run);

class MockInterrupt
{
public:
    void raise();
    void attach(std::function<void()> wake);
    [[nodiscard]] bool raised() const noexcept { return m_raised.load(std::memory_order_acquire); }

private:
    std::mutex m_mutex;
    std::atomic<bool> m_raised{false};
    std::function<void()> m_wake;
};

struct MockCliOption {
    std::string name;
    std::string type;
    std::string description;
    std::optional<MockJson> fallback;
};

struct MockCommandHooks {
    std::function<void(double fraction, const std::string& text)> progress;
    std::function<void(int level, const std::string& text)> log;
    MockInterrupt* interrupt = nullptr;
};

struct MockCommandResult {
    std::string where;
    std::string problem;
    bool worked = false;
    bool interrupted = false;
    MockJson result;
    std::string error;
    std::vector<std::pair<double, std::string>> progress;
    std::vector<std::pair<int, std::string>> logs;
    std::vector<MockJson> outputs;

    [[nodiscard]] int exitCode() const noexcept;
};

[[nodiscard]] const ext::CliContribution* findCliCommand(const ext::Manifest& manifest, std::string_view name);
[[nodiscard]] std::vector<MockCliOption> cliOptions(const ext::Manifest& manifest, const ext::CliContribution& command);
[[nodiscard]] std::optional<MockJson> cliArguments(const ext::Manifest& manifest, const ext::CliContribution& command,
                                                   const std::vector<std::pair<std::string, std::string>>& given,
                                                   std::string& problem);
[[nodiscard]] MockCommandResult runMockCommand(const MockExtension& extension, const ext::CliContribution& command,
                                               const MockJson& args, const MockOptions& options,
                                               const MockCommandHooks& hooks);

}
