#include "ExtCommands.h"

#include "ExtTemplates.h"
#include "MockHost.h"
#include "ScriptProject.h"

#include <gs/ext/Manifest.h>
#include <gs/ext/Package.h>
#include <gs/ext/Version.h>

#include <windows.h>

#include <atomic>
#include <cmath>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace gs::cli {

namespace {

namespace fs = std::filesystem;
using Json = MockJson;

constexpr std::string_view kExtUsage =
    "usage: gs ext test <folder | package.gsext> [--test <file>]... [--json] [--hosted] [--official]\n"
    "       gs ext init <native | script | theme> <folder> [--id <id>]\n"
    "       gs ext <id | folder | package.gsext> <command> [--<param> <value>]... [--json] [--hosted]\n"
    "       gs ext <id | folder | package.gsext> --help [--json]\n"
    "       gs ext keygen <secret key file> [--force] [--json]\n";

std::atomic<MockInterrupt*> g_interrupt{nullptr};
std::atomic<MockTestCleanup*> g_testCleanup{nullptr};

struct Target {
    fs::path folder;
    bool official = false;
};

int usage()
{
    std::cerr << kExtUsage;
    return kUsage;
}

std::string narrow(std::wstring_view text)
{
    if (text.empty()) {
        return {};
    }
    const int size = static_cast<int>(text.size());
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), size, nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string made(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), size, made.data(), length, nullptr, nullptr);
    return made;
}

void printJson(const Json& value)
{
    std::cout << value.dump(2, ' ', false, Json::error_handler_t::replace) << '\n';
}

MockInterrupt& commandInterrupt()
{
    static MockInterrupt interrupt;
    return interrupt;
}

BOOL WINAPI onBreak(DWORD signal)
{
    if (signal != CTRL_C_EVENT && signal != CTRL_BREAK_EVENT) {
        return FALSE;
    }
    MockInterrupt* interrupt = g_interrupt.load();
    if (interrupt == nullptr) {
        return FALSE;
    }
    interrupt->raise();
    return TRUE;
}

BOOL WINAPI onTestBreak(DWORD signal)
{
    if (signal != CTRL_C_EVENT && signal != CTRL_BREAK_EVENT && signal != CTRL_CLOSE_EVENT &&
        signal != CTRL_LOGOFF_EVENT && signal != CTRL_SHUTDOWN_EVENT) {
        return FALSE;
    }
    if (MockTestCleanup* cleanup = g_testCleanup.load()) {
        cleanup->run();
    }
    return FALSE;
}

int testCommand(const std::vector<std::wstring_view>& args)
{
    std::optional<fs::path> input;
    std::vector<fs::path> tests;
    bool json = false;
    MockOptions options;
    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::wstring_view argument = args[index];
        if (argument == L"--json") {
            json = true;
        } else if (argument == L"--hosted") {
            options.hosted = true;
        } else if (argument == L"--official") {
            options.official = true;
        } else if (argument == L"--test" && index + 1 < args.size()) {
            tests.emplace_back(args[++index]);
        } else if (argument.starts_with(L'-') || input) {
            return usage();
        } else {
            input = fs::path(argument);
        }
    }
    if (!input) {
        return usage();
    }
    std::error_code error;
    if (!fs::exists(*input, error)) {
        std::cerr << "gs ext test: " << utf8(*input) << " does not exist\n";
        return kUsage;
    }
    options.exthost = programFolder() / "gs-exthost.exe";
    MockTestCleanup cleanup;
    options.cleanup = &cleanup;
    g_testCleanup.store(&cleanup);
    SetConsoleCtrlHandler(&onTestBreak, TRUE);
    const MockExtension extension = openMockExtension(*input, options);
    if (extension.scratch != nullptr) {
        cleanup.watchFolder(extension.scratch->path());
    }
    MockRun run;
    if (extension.usable()) {
        std::vector<MockSpec> specs;
        if (tests.empty()) {
            for (const fs::path& file : mockTestFiles(extension.folder)) {
                specs.push_back(readMockSpec(file, extension.folder));
            }
        } else {
            const fs::path here = fs::current_path(error);
            for (const fs::path& file : tests) {
                specs.push_back(readMockSpec(file, here));
            }
        }
        run = runMockTests(extension, specs, options);
    }
    if (json) {
        printJson(mockReportJson(extension, run));
    } else {
        std::cout << mockReportText(extension, run);
    }
    cleanup.run();
    SetConsoleCtrlHandler(&onTestBreak, FALSE);
    g_testCleanup.store(nullptr);
    return mockExitCode(extension, run);
}

int initCommand(const std::vector<std::wstring_view>& args)
{
    if (args.size() != 3 && !(args.size() == 5 && args[3] == L"--id")) {
        return usage();
    }
    const std::optional<std::string> id =
        args.size() == 5 ? std::optional<std::string>(narrow(args[4])) : std::nullopt;
    return initExtension(narrow(args[1]), fs::path(args[2]), id);
}

std::optional<Target> resolveTarget(std::wstring_view given, std::string& problem)
{
    const fs::path asked(given);
    std::error_code error;
    if (fs::exists(asked, error)) {
        return Target{.folder = asked, .official = false};
    }
    const std::string id = narrow(given);
    if (!ext::validExtensionId(id)) {
        problem = id + " is not a folder, a .gsext package or the id of an installed extension";
        return std::nullopt;
    }
    const fs::path data = dataFolder();
    const std::pair<fs::path, bool> roots[] = {{programFolder() / "extensions", true},
                                               {data.empty() ? fs::path() : data / "extensions", false}};
    std::optional<std::pair<ext::Version, Target>> best;
    for (const auto& [root, official] : roots) {
        if (root.empty()) {
            continue;
        }
        for (const ext::InstalledExtension& installed : ext::scanInstalled(root, {.official = official})) {
            if (!installed.manifest || installed.manifest->id != id) {
                continue;
            }
            if (!best || best->first < installed.manifest->version) {
                best.emplace(installed.manifest->version, Target{.folder = installed.directory, .official = official});
            }
        }
    }
    if (!best) {
        problem = "no extension " + id + " is installed in " + utf8(data / "extensions") +
                  "; give its folder or .gsext package instead";
        return std::nullopt;
    }
    return best->second;
}

void printCommands(const ext::Manifest& manifest, bool json, std::wstring_view target)
{
    if (json) {
        Json commands = Json::array();
        for (const ext::CliContribution& command : manifest.contributes.cli) {
            Json rows = Json::array();
            for (const MockCliOption& option : cliOptions(manifest, command)) {
                Json row{{"name", option.name}, {"type", option.type}, {"description", option.description}};
                if (option.fallback) {
                    row["default"] = *option.fallback;
                }
                rows.push_back(std::move(row));
            }
            commands.push_back(Json{{"name", command.name},
                                    {"description", command.description},
                                    {"job", command.job},
                                    {"options", std::move(rows)}});
        }
        printJson(Json{{"id", manifest.id},
                       {"version", manifest.version.text()},
                       {"name", manifest.name},
                       {"commands", std::move(commands)}});
        return;
    }
    std::cout << manifest.id << ' ' << manifest.version.text() << " (" << manifest.name << ")\n";
    if (manifest.contributes.cli.empty()) {
        std::cout << "it has no commands\n";
        return;
    }
    std::cout << "commands:\n";
    for (const ext::CliContribution& command : manifest.contributes.cli) {
        std::cout << std::format("  {:<16} {}\n", command.name, command.description);
        for (const MockCliOption& option : cliOptions(manifest, command)) {
            std::string line = std::format("      --{} <{}>", option.name, option.type);
            if (!option.description.empty()) {
                line += "  " + option.description;
            }
            if (option.fallback) {
                line += " (default " + option.fallback->dump() + ")";
            }
            std::cout << line << '\n';
        }
    }
    std::cout << "usage: gs ext " << narrow(target) << " <command> [--<param> <value>]... [--json] [--hosted]\n";
}

void printOutcome(const ext::Manifest& manifest, const ext::CliContribution& command, const Json& args,
                  const MockCommandResult& result, bool json)
{
    if (json) {
        Json report{{"ok", result.exitCode() == kSuccess},
                    {"id", manifest.id},
                    {"version", manifest.version.text()},
                    {"command", command.name},
                    {"job", command.job},
                    {"args", args},
                    {"where", result.where}};
        Json progress = Json::array();
        for (const auto& [fraction, text] : result.progress) {
            progress.push_back(Json{{"fraction", fraction}, {"text", text}});
        }
        Json logs = Json::array();
        for (const auto& [level, text] : result.logs) {
            logs.push_back(Json{{"level", level}, {"text", text}});
        }
        report["progress"] = std::move(progress);
        report["logs"] = std::move(logs);
        report["outputs"] = result.outputs;
        if (!result.problem.empty()) {
            report["problem"] = result.problem;
        } else if (result.worked) {
            report["result"] = result.result;
        } else {
            report["error"] = result.error;
        }
        if (result.interrupted) {
            report["interrupted"] = true;
        }
        printJson(report);
        return;
    }
    if (!result.problem.empty()) {
        std::cerr << "gs ext " << command.name << ": " << manifest.id << " cannot be loaded: " << result.problem
                  << '\n';
    } else if (result.worked) {
        std::cout << result.result.dump(2, ' ', false, Json::error_handler_t::replace) << '\n';
    } else {
        std::cerr << "gs ext " << command.name << ": " << result.error << '\n';
    }
}

int extensionCommand(const std::vector<std::wstring_view>& args)
{
    bool json = false;
    bool help = false;
    MockOptions options;
    std::optional<std::string> command;
    std::vector<std::pair<std::string, std::string>> given;
    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::wstring_view argument = args[index];
        if (argument == L"--json") {
            json = true;
        } else if (argument == L"--hosted") {
            options.hosted = true;
        } else if (argument == L"--help") {
            help = true;
        } else if (argument.starts_with(L"--") && argument.size() > 2) {
            std::wstring_view name = argument.substr(2);
            std::wstring_view value;
            if (const std::size_t equals = name.find(L'='); equals != std::wstring_view::npos) {
                value = name.substr(equals + 1);
                name = name.substr(0, equals);
            } else if (index + 1 < args.size()) {
                value = args[++index];
            } else {
                std::cerr << "gs ext: --" << narrow(name) << " needs a value\n";
                return kUsage;
            }
            given.emplace_back(narrow(name), narrow(value));
        } else if (!command && !argument.starts_with(L'-')) {
            command = narrow(argument);
        } else {
            return usage();
        }
    }
    std::string problem;
    const std::optional<Target> target = resolveTarget(args[0], problem);
    if (!target) {
        std::cerr << "gs ext: " << problem << '\n';
        return kUsage;
    }
    options.official = target->official;
    options.exthost = programFolder() / "gs-exthost.exe";
    const MockExtension extension = openMockExtension(target->folder, options);
    if (!extension.usable()) {
        if (json) {
            printJson(mockReportJson(extension, MockRun{}));
        } else {
            std::cerr << mockReportText(extension, MockRun{});
        }
        return kUsage;
    }
    const ext::Manifest& manifest = *extension.manifest;
    if (help || !command) {
        printCommands(manifest, json, args[0]);
        return help ? kSuccess : kUsage;
    }
    const ext::CliContribution* found = findCliCommand(manifest, *command);
    if (found == nullptr) {
        std::cerr << "gs ext: " << manifest.id << " has no command " << *command << "; gs ext " << narrow(args[0])
                  << " --help lists its commands\n";
        return kUsage;
    }
    const std::optional<Json> jobArgs = cliArguments(manifest, *found, given, problem);
    if (!jobArgs) {
        std::cerr << "gs ext " << found->name << ": " << problem << '\n';
        return kUsage;
    }
    MockCommandHooks hooks;
    if (!json) {
        hooks.progress = [](double fraction, const std::string& text) {
            const std::string share =
                fraction >= 0.0 ? std::format("[{:3}%] ", std::lround(fraction * 100.0)) : std::string("[    ] ");
            writeTo(Stream::Out, share + text + "\n");
        };
        hooks.log = [](int level, const std::string& text) {
            writeTo(Stream::Out, std::string(level >= 2 ? "  error: " : level == 1 ? "  warning: " : "  ") + text + "\n");
        };
    }
    MockInterrupt& interrupt = commandInterrupt();
    hooks.interrupt = &interrupt;
    g_interrupt.store(&interrupt);
    SetConsoleCtrlHandler(&onBreak, TRUE);
    const MockCommandResult result = runMockCommand(extension, *found, *jobArgs, options, hooks);
    SetConsoleCtrlHandler(&onBreak, FALSE);
    g_interrupt.store(nullptr);
    printOutcome(manifest, *found, *jobArgs, result, json);
    return result.exitCode();
}

}

int runExtCommand(const std::vector<std::wstring_view>& args)
{
    if (args.empty()) {
        return usage();
    }
    if (args[0] == L"test") {
        return testCommand(args);
    }
    if (args[0] == L"init") {
        return initCommand(args);
    }
    if (args[0].starts_with(L'-')) {
        return usage();
    }
    return extensionCommand(args);
}

}
