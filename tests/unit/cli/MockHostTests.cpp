#include "MockHost.h"

#include <gs/ext/Archive.h>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace gs::cli;
namespace fs = std::filesystem;
using Json = MockJson;

namespace {

const fs::path kData(GS_EXT_TEST_DATA_DIR);
const fs::path kTestDll(GS_TEST_NATIVE_DLL);
const fs::path kOldDll(GS_TEST_NATIVE_OLD_DLL);
const fs::path kProgram(GS_EXTHOST_PROGRAM);

void writeFile(const fs::path& file, std::string_view text)
{
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

fs::path nativeFolder(const TempFolder& scratch, const fs::path& dll)
{
    const fs::path folder = scratch.path() / "native";
    fs::create_directories(folder / "tests");
    fs::copy_file(kData / "native" / "manifest.json", folder / "manifest.json");
    for (const fs::directory_entry& entry : fs::directory_iterator(kData / "native" / "tests")) {
        fs::copy_file(entry.path(), folder / "tests" / entry.path().filename());
    }
    fs::copy_file(dll, folder / "native.dll");
    return folder;
}

MockRun runOne(const MockExtension& extension, std::string_view text, const MockOptions& options = {})
{
    return runMockTests(extension, {MockSpec{.name = "inline", .file = "inline", .spec = Json::parse(text), .problem = {}}},
                        options);
}

std::string problemsOf(const MockRun& run)
{
    std::string text = run.problem;
    for (const MockTest& test : run.tests) {
        text += test.name + ": " + test.problem + " ";
        for (const MockStep& step : test.steps) {
            for (const std::string& problem : step.problems) {
                text += "#" + std::to_string(step.index) + " " + problem + "; ";
            }
        }
    }
    return text;
}

std::vector<MockSpec> fixtureSpecs(const fs::path& folder)
{
    std::vector<MockSpec> specs;
    for (const fs::path& file : mockTestFiles(folder)) {
        specs.push_back(readMockSpec(file, folder));
    }
    return specs;
}

}

TEST_CASE("ADR-0112: gs ext test runs the test files of an extension against its DLL in this process and reports "
          "every step with its time, input, output and verdict")
{
    TempFolder scratch("gs-mock-tests");
    const fs::path folder = nativeFolder(scratch, kTestDll);
    const MockOptions options;
    const MockExtension extension = openMockExtension(folder, options);
    REQUIRE_MESSAGE(extension.usable(), extension.problem);
    const std::vector<MockSpec> specs = fixtureSpecs(folder);
    REQUIRE(specs.size() == 6);
    CHECK(specs[0].name == "basic");
    CHECK(specs[0].file == "tests/basic.json");
    CHECK(specs[0].problem.empty());

    const MockRun run = runMockTests(extension, specs, options);
    INFO(problemsOf(run));
    CHECK(run.passed());
    CHECK(mockExitCode(extension, run) == 0);
    CHECK(run.where == "native.dll in this process");
    CHECK(run.warnings.empty());
    REQUIRE(run.tests.size() == 6);
    const MockTest& basic = run.tests[0];
    REQUIRE(basic.steps.size() == 10);
    CHECK(basic.steps[0].index == 1);
    CHECK(basic.steps[0].action == "call");
    CHECK(basic.steps[0].input == Json{{"node", "add"}, {"args", Json::array({1, 2})}});
    CHECK(basic.steps[0].output.at("value") == 3);
    CHECK(basic.steps[2].output.at("error") == "GS-API-062 the test node fails on purpose");
    CHECK(basic.steps[5].label == "count to three");
    CHECK(basic.steps[5].output.at("result") == Json{{"counted", 3}});
    CHECK(basic.steps[5].output.at("logs")[0].at("text") == "counting");
    CHECK(basic.steps[6].output.at("happened")[0].at("kind") == "event");
    CHECK(basic.steps[7].output.at("now").get<std::int64_t>() >= 5000);
    CHECK(basic.steps[8].at >= 5000);

    const std::string report = mockReportText(extension, run);
    CHECK(report.find("gstest.mock 1.0.0: native.dll in this process\n") != std::string::npos);
    CHECK(report.find("PASS basic (tests/basic.json)\n") != std::string::npos);
    CHECK(report.find("  pass  #1 ") != std::string::npos);
    CHECK(report.find("\n6 passed, 0 failed\n") != std::string::npos);
    const Json json = mockReportJson(extension, run);
    CHECK(json.at("ok") == true);
    CHECK(json.at("loaded") == true);
    CHECK(json.at("id") == "gstest.mock");
    CHECK(json.at("passed") == 6);
    CHECK(json.at("tests")[0].at("steps")[0].at("do") == "call");
    CHECK(json.at("tests")[0].at("steps")[0].at("ok") == true);
}

TEST_CASE("ADR-0112: the same test files pass with the DLL in gs-exthost.exe")
{
    TempFolder scratch("gs-mock-tests");
    const fs::path folder = nativeFolder(scratch, kTestDll);
    MockOptions options;
    options.hosted = true;
    options.exthost = kProgram;
    const MockExtension extension = openMockExtension(folder, options);
    REQUIRE_MESSAGE(extension.usable(), extension.problem);
    const MockRun run = runMockTests(extension, fixtureSpecs(folder), options);
    INFO(problemsOf(run));
    CHECK(run.passed());
    CHECK(run.tests.size() == 6);
    CHECK(run.where == "native.dll in gs-exthost.exe");
    CHECK(mockExitCode(extension, run) == 0);
}

TEST_CASE("ADR-0112: a job step lists the outputs the job gave on its way and checks them in order")
{
    TempFolder scratch("gs-mock-tests");
    const MockExtension extension = openMockExtension(nativeFolder(scratch, kTestDll), {});
    REQUIRE_MESSAGE(extension.usable(), extension.problem);
    const MockRun run = runOne(extension, R"x({"name": "outputs", "steps": [
        {"do": "job", "id": "listen", "job": "hear", "wait": false,
         "args": {"phrases": ["jump", "duck"], "language": "en", "confidence": 0.5}},
        {"do": "job.cancel", "id": "listen"},
        {"do": "job.wait", "id": "listen", "expect": {"error": "GS-API-066", "outputs": [{"phrase": "duck"}, {"phrase": "jump"}]}}
    ]})x");
    REQUIRE(run.tests.size() == 1);
    REQUIRE(run.tests[0].steps.size() == 3);
    const MockStep& waited = run.tests[0].steps[2];
    const Json& outputs = waited.output.at("outputs");
    REQUIRE(outputs.size() == 2);
    CHECK(outputs[0].at("phrase") == "jump");
    CHECK(outputs[1].at("phrase") == "duck");
    REQUIRE(waited.problems.size() == 1);
    CHECK(waited.problems[0].starts_with(R"x(outputs: {"phrase":"jump"} did not come in order)x"));
}

TEST_CASE("ADR-0112: a step that does not get what it expects fails its test and says why, and gs ext test ends with 1")
{
    TempFolder scratch("gs-mock-tests");
    const MockExtension extension = openMockExtension(nativeFolder(scratch, kTestDll), {});
    REQUIRE_MESSAGE(extension.usable(), extension.problem);
    const MockSpec failing = readMockSpec(kData / "failing.json", kData);
    CHECK(failing.name == "failing");
    CHECK(failing.file == "failing.json");
    const MockRun run = runMockTests(extension, {failing}, {});
    REQUIRE(run.tests.size() == 1);
    CHECK_FALSE(run.tests[0].passed());
    REQUIRE(run.tests[0].steps.size() == 2);
    CHECK(run.tests[0].steps[0].passed());
    CHECK(run.tests[0].steps[1].problems == std::vector<std::string>{"value: expected 4, got 3"});
    CHECK(mockExitCode(extension, run) == 1);
    const std::string report = mockReportText(extension, run);
    CHECK(report.find("FAIL failing (failing.json)\n") != std::string::npos);
    CHECK(report.find("  FAIL  #2 ") != std::string::npos);
    CHECK(report.find("        value: expected 4, got 3\n") != std::string::npos);
    CHECK(report.find("\n0 passed, 1 failed\n") != std::string::npos);
    CHECK(mockReportJson(extension, run).at("ok") == false);

    const MockSpec missing = readMockSpec(kData / "nowhere.json", kData);
    CHECK(missing.problem.find("cannot be read") != std::string::npos);
    const MockRun broken = runMockTests(extension, {missing}, {});
    CHECK_FALSE(broken.passed());
    CHECK(mockExitCode(extension, broken) == 1);
    CHECK(mockExitCode(extension, runMockTests(extension, {}, {})) == 1);
}

TEST_CASE("ADR-0112: gs ext test ends with 2 when the DLL, the manifest or the extension cannot be loaded")
{
    TempFolder scratch("gs-mock-tests");
    const MockExtension old = openMockExtension(nativeFolder(scratch, kOldDll), {});
    REQUIRE_MESSAGE(old.usable(), old.problem);
    const MockRun run = runOne(old, R"({"steps": [{"do": "call", "node": "add", "args": [1, 2]}]})");
    CHECK(run.problem.find("native.dll cannot be loaded: the DLL was made for an older GameScrcpy") !=
          std::string::npos);
    CHECK(mockExitCode(old, run) == 2);
    CHECK(mockReportText(old, run).find("cannot be loaded: native.dll cannot be loaded") != std::string::npos);
    CHECK(mockReportJson(old, run).at("loaded") == false);

    writeFile(scratch.path() / "broken" / "manifest.json", R"({"schemaVersion": 1, "id": "gstest.broken"})");
    const MockExtension broken = openMockExtension(scratch.path() / "broken", {});
    CHECK_FALSE(broken.usable());
    CHECK_FALSE(broken.diagnostics.empty());
    CHECK(mockExitCode(broken, MockRun{}) == 2);
    CHECK(mockReportText(broken, MockRun{}).find("cannot be loaded: the extension has the errors above") !=
          std::string::npos);

    const MockExtension missing = openMockExtension(scratch.path() / "nowhere", {});
    CHECK(missing.problem.find("does not exist") != std::string::npos);
    CHECK(mockExitCode(missing, MockRun{}) == 2);
}

TEST_CASE("ADR-0112: an unknown step, input or check fails its step, so that a typo cannot pass")
{
    TempFolder scratch("gs-mock-tests");
    const MockExtension extension = openMockExtension(nativeFolder(scratch, kTestDll), {});
    REQUIRE_MESSAGE(extension.usable(), extension.problem);
    const MockRun run = runOne(extension, R"({"steps": [
        {"do": "cal", "node": "add"},
        {"do": "call", "node": "add", "arg": [1, 2]},
        {"do": "call", "node": "add", "args": [1, 2], "expect": {"vaule": 3}},
        {"node": "add"},
        {"do": "call", "node": "add", "args": [1, 2], "timeout": 0},
        {"do": "call", "node": "add", "args": ["one", 2]},
        {"do": "call", "node": "nothing"}
    ]})");
    REQUIRE(run.tests.size() == 1);
    const std::vector<MockStep>& steps = run.tests[0].steps;
    REQUIRE(steps.size() == 7);
    CHECK(steps[0].problems.at(0).find("there is no step \"cal\"") == 0);
    CHECK(steps[1].problems.at(0) == "call takes no \"arg\"");
    CHECK(steps[2].problems.at(0) == "call cannot check \"vaule\"");
    CHECK(steps[3].problems.at(0).find("a step says what it does in \"do\"") == 0);
    CHECK(steps[4].problems.at(0).find("timeout is a whole number") == 0);
    CHECK(steps[5].problems.at(0) == "expected no error, got GS-API-061 value 1: a number belongs here");
    CHECK(steps[6].problems.at(0) == "expected no error, got GS-API-060 Mock host test does not offer this node");
    CHECK(mockExitCode(extension, run) == 1);
}

TEST_CASE("ADR-0112: a test grants fewer permissions, and nodes that need the others are refused as in GameScrcpy")
{
    TempFolder scratch("gs-mock-tests");
    const MockExtension extension = openMockExtension(nativeFolder(scratch, kTestDll), {});
    REQUIRE_MESSAGE(extension.usable(), extension.problem);
    const MockRun granted = runOne(extension, R"({"granted": ["notify"], "steps": [
        {"do": "call", "node": "claim", "args": ["serial:COM3", false],
         "expect": {"error": "GS-API-063 it needs the permission \"hardware\""}},
        {"do": "call", "node": "notify", "args": ["Title", "Text"], "expect": {"value": 0}}
    ]})");
    INFO(problemsOf(granted));
    CHECK(granted.passed());

    const MockRun unknown = runOne(extension, R"({"granted": ["network"], "steps": [{"do": "wait", "ms": 1}]})");
    REQUIRE(unknown.tests.size() == 1);
    CHECK(unknown.tests[0].problem == "granted lists \"network\", which the manifest does not ask for");
    CHECK(unknown.tests[0].steps.empty());
}

TEST_CASE("ADR-0112: the test clock starts at 0 and wait steps move it on, and an expected event that does not come "
          "fails its step")
{
    TempFolder scratch("gs-mock-tests");
    const MockExtension extension = openMockExtension(nativeFolder(scratch, kTestDll), {});
    REQUIRE_MESSAGE(extension.usable(), extension.problem);
    const MockRun run = runOne(extension, R"({"steps": [
        {"do": "wait", "ms": 1500},
        {"do": "events", "timeout": 50, "expect": {"events": ["never.comes"]}},
        {"do": "call", "node": "say", "args": ["a.b", "2"], "expect": {"value": 0, "events": [{"topic": "a.b", "value": 2}]}},
        {"do": "call", "node": "say", "args": ["c.d", "[1]"]},
        {"do": "events", "expect": {"events": ["c.d"]}}
    ]})");
    REQUIRE(run.tests.size() == 1);
    const std::vector<MockStep>& steps = run.tests[0].steps;
    REQUIRE(steps.size() == 5);
    CHECK(steps[0].at < 1500);
    CHECK(steps[0].output.at("now").get<std::int64_t>() >= 1500);
    CHECK(steps[1].at >= 1500);
    CHECK(steps[1].problems == std::vector<std::string>{"events: \"never.comes\" did not come within 50 ms"});
    CHECK(steps[2].passed());
    CHECK(steps[4].passed());
    CHECK(steps[4].output.at("events") == Json::array({Json{{"topic", "c.d"}, {"value", Json::array({1})}}}));
}

TEST_CASE("ADR-0112: a command of contributes.cli turns its options into the job's arguments and runs the job to its "
          "result, and an interrupt cancels it")
{
    TempFolder scratch("gs-mock-tests");
    const MockExtension extension = openMockExtension(nativeFolder(scratch, kTestDll), {});
    REQUIRE_MESSAGE(extension.usable(), extension.problem);
    const gs::ext::Manifest& manifest = *extension.manifest;
    const gs::ext::CliContribution* count = findCliCommand(manifest, "count");
    REQUIRE(count != nullptr);
    CHECK(findCliCommand(manifest, "nothing") == nullptr);
    const std::vector<MockCliOption> options = cliOptions(manifest, *count);
    REQUIRE(options.size() == 3);
    CHECK(options[0].name == "steps");
    CHECK(options[0].type == "number");
    CHECK(options[0].description == "How far to count");
    CHECK_FALSE(options[0].fallback.has_value());
    CHECK(options[1].fallback == Json(0));

    std::string problem;
    const std::optional<Json> args = cliArguments(manifest, *count, {{"steps", "3"}}, problem);
    REQUIRE_MESSAGE(args.has_value(), problem);
    CHECK(*args == Json{{"steps", 3}, {"pause", 0}, {"fail", 0}});
    CHECK_FALSE(cliArguments(manifest, *count, {}, problem).has_value());
    CHECK(problem == "count needs --steps");
    CHECK_FALSE(cliArguments(manifest, *count, {{"steps", "three"}}, problem).has_value());
    CHECK(problem == "--steps is a number");
    CHECK_FALSE(cliArguments(manifest, *count, {{"speed", "1"}}, problem).has_value());
    CHECK(problem == "count takes no --speed");
    CHECK_FALSE(cliArguments(manifest, *count, {{"steps", "1"}, {"steps", "2"}}, problem).has_value());
    CHECK(problem == "--steps is given twice");

    std::vector<std::string> heard;
    MockCommandHooks hooks;
    hooks.progress = [&heard](double, const std::string& text) { heard.push_back(text); };
    const MockCommandResult result = runMockCommand(extension, *count, *args, {}, hooks);
    CHECK_MESSAGE(result.worked, result.error);
    CHECK(result.exitCode() == 0);
    CHECK(result.where == "native.dll in this process");
    CHECK(result.result == Json{{"counted", 3}});
    REQUIRE_FALSE(heard.empty());
    CHECK(heard.front() == "step 1");
    REQUIRE_FALSE(result.logs.empty());
    CHECK(result.logs.front().second == "counting");

    const std::optional<Json> failing = cliArguments(manifest, *count, {{"steps", "1"}, {"fail", "1"}}, problem);
    REQUIRE(failing.has_value());
    const MockCommandResult failed = runMockCommand(extension, *count, *failing, {}, {});
    CHECK_FALSE(failed.worked);
    CHECK(failed.error == "GS-API-065 asked to fail");
    CHECK(failed.exitCode() == 1);

    MockInterrupt interrupt;
    interrupt.raise();
    MockCommandHooks stopping;
    stopping.interrupt = &interrupt;
    const std::optional<Json> lengthy = cliArguments(manifest, *count, {{"steps", "1000"}, {"pause", "20"}}, problem);
    REQUIRE(lengthy.has_value());
    const MockCommandResult stopped = runMockCommand(extension, *count, *lengthy, {}, stopping);
    CHECK(stopped.interrupted);
    CHECK(stopped.error == "GS-API-066 cancelled");
    CHECK(stopped.exitCode() == 1);
}

TEST_CASE("ADR-0112: the scripts of an extension load without GameScrcpy, what they log and draw shows in the step "
          "scripts, and one that fails to load stops gs ext test with 2")
{
    TempFolder scratch("gs-mock-tests");
    const fs::path folder = scratch.path() / "script";
    writeFile(folder / "manifest.json", R"({
        "schemaVersion": 1, "id": "gstest.mock-script", "version": "1.0.0", "name": "Script", "license": "MIT",
        "requires": {"script": "^1.0", "panel": "^1.0"},
        "contributes": {"scripts": [{"name": "main", "entry": "src/main.ts", "run": "app"}],
                        "panels": [{"name": "main", "title": "Main", "where": "page", "script": "main"}]}})");
    writeFile(folder / "src" / "main.ts",
              "const page = ui.panel(\"main\");\nconst count: number = 2;\npage.render({ type: \"text\", text: "
              "`count ${count}` });\nlog.info(\"ready\");\n");
    const MockExtension extension = openMockExtension(folder, {});
    REQUIRE_MESSAGE(extension.usable(), extension.problem);
    REQUIRE(extension.scripts.size() == 1);
    CHECK(extension.scripts[0].name == "main");
    CHECK(extension.scripts[0].run == "app");
    CHECK(extension.scripts[0].state == MockScriptState::Loaded);
    CHECK(extension.scripts[0].logs == std::vector<std::string>{"ready"});
    REQUIRE(extension.scripts[0].calls.size() == 1);
    CHECK(extension.scripts[0].calls[0].find("ui.render ") == 0);
    const MockRun run = runOne(extension, R"({"steps": [
        {"do": "scripts", "expect": {"loaded": ["main"], "logs": ["ready"]}},
        {"do": "call", "node": "add", "args": [1, 2], "expect": {"error": "GS-API-060"}}
    ]})");
    INFO(problemsOf(run));
    CHECK(run.passed());
    CHECK(run.where == "no native DLL");

    writeFile(folder / "src" / "main.ts", "log.info(\"before\");\nthrow new Error(\"broken on purpose\");\n");
    const MockExtension broken = openMockExtension(folder, {});
    CHECK_FALSE(broken.usable());
    REQUIRE(broken.scripts.size() == 1);
    CHECK(broken.scripts[0].state == MockScriptState::Failed);
    CHECK(broken.problem.find("the script main cannot be loaded") == 0);
    CHECK(broken.problem.find("broken on purpose") != std::string::npos);
    CHECK(mockExitCode(broken, MockRun{}) == 2);
}

TEST_CASE("ADR-0112: a .gsext package is unpacked and tested like its folder")
{
    TempFolder scratch("gs-mock-tests");
    const fs::path folder = scratch.path() / "theme";
    writeFile(folder / "manifest.json", R"({
        "schemaVersion": 1, "id": "gstest.mock-theme", "version": "1.0.0", "name": "Theme", "license": "MIT",
        "requires": {"theme": "^1.0"},
        "contributes": {"themes": [{"name": "Night", "dark": {"bgBase": "#101010"}}]}})");
    writeFile(folder / "tests" / "basic.json",
              R"({"steps": [{"do": "manifest", "expect": {"value": {"contributes": {"themes": [{"name": "Night"}]}}}}]})");
    const fs::path package = scratch.path() / "theme.gsext";
    REQUIRE_FALSE(gs::ext::hasErrors(gs::ext::packDirectory(folder, package)));
    const MockExtension extension = openMockExtension(package, {});
    REQUIRE_MESSAGE(extension.usable(), extension.problem);
    CHECK(extension.folder != package);
    const std::vector<MockSpec> specs = fixtureSpecs(extension.folder);
    REQUIRE(specs.size() == 1);
    const MockRun run = runMockTests(extension, specs, {});
    INFO(problemsOf(run));
    CHECK(run.passed());
    CHECK(mockExitCode(extension, run) == 0);
}

TEST_CASE("ADR-0112: a test file or a json option nested more than 64 levels deep is refused before it is read, so "
          "that it cannot overflow the stack of gs")
{
    TempFolder scratch("gs-mock-tests");
    const auto nested = [](std::size_t levels) {
        return R"({"steps": [{"do": "wait", "ms": 1, "x": )" + std::string(levels, '[') + std::string(levels, ']') +
               "}]}";
    };
    writeFile(scratch.path() / "fits.json", nested(kMockMostDepth - 3));
    writeFile(scratch.path() / "deep.json", nested(kMockMostDepth - 2));
    writeFile(scratch.path() / "deeper.json", nested(200'000));
    const MockSpec fits = readMockSpec(scratch.path() / "fits.json", scratch.path());
    CHECK(fits.problem.empty());
    CHECK(fits.spec.is_object());
    for (const char* file : {"deep.json", "deeper.json"}) {
        const MockSpec refused = readMockSpec(scratch.path() / file, scratch.path());
        CHECK(refused.problem == "the test file is nested more than 64 levels deep");
        CHECK(refused.spec.is_null());
    }

    const fs::path theme = scratch.path() / "theme";
    writeFile(theme / "manifest.json", R"({
        "schemaVersion": 1, "id": "gstest.mock-deep", "version": "1.0.0", "name": "Theme", "license": "MIT",
        "requires": {"theme": "^1.0"},
        "contributes": {"themes": [{"name": "Night", "dark": {"bgBase": "#101010"}}]}})");
    const MockExtension themed = openMockExtension(theme, {});
    REQUIRE_MESSAGE(themed.usable(), themed.problem);
    const MockRun run = runMockTests(themed, {readMockSpec(scratch.path() / "deeper.json", scratch.path())}, {});
    REQUIRE(run.tests.size() == 1);
    CHECK(run.tests[0].problem == "the test file is nested more than 64 levels deep");
    CHECK(run.tests[0].steps.empty());
    CHECK(mockExitCode(themed, run) == 1);

    const MockExtension native = openMockExtension(nativeFolder(scratch, kTestDll), {});
    REQUIRE_MESSAGE(native.usable(), native.problem);
    gs::ext::Manifest manifest = *native.manifest;
    manifest.contributes.cli.push_back(
        gs::ext::CliContribution{.name = "hear", .description = {}, .job = "hear", .params = {}});
    const gs::ext::CliContribution& hear = manifest.contributes.cli.back();
    std::string problem;
    const std::optional<Json> heard =
        cliArguments(manifest, hear, {{"phrases", R"(["jump"])"}, {"language", "en"}, {"confidence", "0.5"}}, problem);
    REQUIRE_MESSAGE(heard.has_value(), problem);
    CHECK(heard->at("phrases") == Json::array({"jump"}));
    const std::string deepJson = std::string(20'000, '[') + std::string(20'000, ']');
    CHECK_FALSE(
        cliArguments(manifest, hear, {{"phrases", deepJson}, {"language", "en"}, {"confidence", "0.5"}}, problem)
            .has_value());
    CHECK(problem == "--phrases is nested more than 64 levels deep");
}

TEST_CASE("ADR-0112: a cli option whose default is not JSON leaves the option without a default, so that gs ext still "
          "prints the help of the command and says which option is missing")
{
    TempFolder scratch("gs-mock-tests");
    const MockExtension extension = openMockExtension(nativeFolder(scratch, kTestDll), {});
    REQUIRE_MESSAGE(extension.usable(), extension.problem);
    gs::ext::Manifest manifest = *extension.manifest;
    manifest.contributes.cli.push_back(gs::ext::CliContribution{
        .name = "hear",
        .description = {},
        .job = "hear",
        .params = {gs::ext::CliParam{.name = "phrases", .description = {}, .defaultValue = "1, 2"}}});
    const gs::ext::CliContribution& hear = manifest.contributes.cli.back();
    const std::vector<MockCliOption> options = cliOptions(manifest, hear);
    REQUIRE(options.size() == 3);
    CHECK(options[0].name == "phrases");
    CHECK(options[0].type == "json");
    CHECK_FALSE(options[0].fallback.has_value());

    std::string problem;
    CHECK_FALSE(cliArguments(manifest, hear, {{"language", "en"}, {"confidence", "0.5"}}, problem).has_value());
    CHECK(problem == "hear needs --phrases");

    manifest.contributes.cli.back().params[0].defaultValue = R"(["jump"])";
    const std::vector<MockCliOption> filled = cliOptions(manifest, hear);
    REQUIRE(filled.size() == 3);
    REQUIRE(filled[0].fallback.has_value());
    CHECK(*filled[0].fallback == Json::array({"jump"}));
    const std::optional<Json> args = cliArguments(manifest, hear, {{"language", "en"}, {"confidence", "0.5"}}, problem);
    REQUIRE_MESSAGE(args.has_value(), problem);
    CHECK(args->at("phrases") == Json::array({"jump"}));
}

TEST_CASE("ADR-0112: an interrupted gs ext test ends its child, forgets its container and removes its temporary folder")
{
    TempFolder scratch("gs-mock-tests");
    std::vector<std::string> order;
    MockTestCleanup cleanup([&order](std::string_view container) {
        order.push_back("forget " + std::string(container));
        return true;
    });
    const fs::path folder = scratch.path() / "left-behind";
    writeFile(folder / "tests" / "basic.json", "{}");
    cleanup.watchFolder(folder);
    cleanup.watchChild("gs-test:gstest.mock", [&order] { order.push_back("child"); });

    cleanup.run();

    CHECK((order == std::vector<std::string>{"child", "forget gs-test:gstest.mock"}));
    CHECK_FALSE(fs::exists(folder));
    CHECK(fs::exists(scratch.path()));

    order.clear();
    cleanup.run();
    CHECK(order.empty());
}

TEST_CASE("ADR-0112: an interrupted gs ext test without a running child ends nothing, forgets no container and still "
          "removes its temporary folder")
{
    TempFolder scratch("gs-mock-tests");
    std::vector<std::string> forgotten;
    MockTestCleanup cleanup([&forgotten](std::string_view container) {
        forgotten.emplace_back(container);
        return true;
    });
    int ended = 0;
    const fs::path folder = scratch.path() / "left-behind";
    fs::create_directories(folder);
    cleanup.watchFolder(folder);
    cleanup.watchChild("gs-test:gstest.mock", [&ended] { ++ended; });
    cleanup.forgetChild();

    cleanup.run();

    CHECK(ended == 0);
    CHECK(forgotten.empty());
    CHECK_FALSE(fs::exists(folder));
}
