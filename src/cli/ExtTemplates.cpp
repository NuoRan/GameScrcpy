#include "ExtTemplates.h"

#include "ScriptProject.h"

#include <gs/ext/Manifest.h>
#include <gs/script/ApiFiles.h>

#include <nlohmann/json.hpp>

#include <iostream>
#include <system_error>
#include <utility>
#include <vector>

namespace gs::cli {

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

constexpr std::string_view kHeaderPath = "include/gamescrcpy_ext.h";
constexpr std::size_t kMostSlugBytes = 40;
constexpr std::size_t kMostTitleBytes = 40;

constexpr std::string_view kNativeCMake = R"cmake(cmake_minimum_required(VERSION 3.21)
project(@PROJECT@ LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(native SHARED src/native.cpp)
target_include_directories(native PRIVATE include)
set_target_properties(native PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}$<0:>"
    PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols$<0:>"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
if(MSVC)
    target_compile_options(native PRIVATE /W4 /permissive- /utf-8)
endif()
)cmake";

constexpr std::string_view kNativeSource = R"cpp(#include <gamescrcpy_ext.h>

#include <atomic>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

const gs_ext_host* g_host = nullptr;

struct Jobs {
    std::mutex mutex;
    std::map<uint64_t, std::shared_ptr<std::atomic<bool>>> stops;
    std::vector<std::thread> threads;
};

Jobs& jobs()
{
    static Jobs all;
    return all;
}

double numberIn(const std::string& json, const std::string& key, double fallback)
{
    const size_t at = json.find("\"" + key + "\"");
    const size_t colon = at == std::string::npos ? at : json.find(':', at + key.size() + 2);
    if (colon == std::string::npos) {
        return fallback;
    }
    char* end = nullptr;
    const double value = std::strtod(json.c_str() + colon + 1, &end);
    return end == json.c_str() + colon + 1 ? fallback : value;
}

int32_t greet(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    const std::string text = "Hello, " + std::string(args[0].text, args[0].size) + "!";
    return g_host->set_text(result, GS_EXT_TYPE_STRING, text.data(), text.size());
}

void finish(uint64_t job, bool worked, const std::string& text)
{
    if (worked) {
        g_host->job_finish(g_host->context, job, text.data(), text.size());
    } else {
        g_host->job_fail(g_host->context, job, text.data(), text.size());
    }
}

int32_t countdownStart(void*, uint64_t job, const char* args, size_t size)
{
    const double from = numberIn(std::string(args, size), "from", 3.0);
    if (from < 0.0 || from > 1000000.0) {
        return GS_EXT_BAD_ARGUMENT;
    }
    const int steps = static_cast<int>(from);
    auto stop = std::make_shared<std::atomic<bool>>(false);
    Jobs& all = jobs();
    const std::scoped_lock lock(all.mutex);
    all.stops[job] = stop;
    all.threads.emplace_back([job, steps, stop] {
        for (int left = steps; left > 0; --left) {
            if (stop->load()) {
                finish(job, false, "cancelled");
                return;
            }
            const std::string line = std::to_string(left) + " to go";
            g_host->job_progress(g_host->context, job, static_cast<double>(steps - left) / steps, line.data(),
                                 line.size());
        }
        finish(job, true, "{\"counted\":" + std::to_string(steps) + "}");
    });
    return GS_EXT_OK;
}

void countdownCancel(void*, uint64_t job)
{
    Jobs& all = jobs();
    const std::scoped_lock lock(all.mutex);
    if (const auto found = all.stops.find(job); found != all.stops.end()) {
        found->second->store(true);
    }
}

void unload(void*)
{
    std::vector<std::thread> threads;
    {
        Jobs& all = jobs();
        const std::scoped_lock lock(all.mutex);
        for (auto& entry : all.stops) {
            entry.second->store(true);
        }
        threads.swap(all.threads);
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
}

const uint32_t kGreetParams[] = {GS_EXT_TYPE_STRING};

const gs_ext_node kGreet = {sizeof(gs_ext_node), "greet", kGreetParams, 1, GS_EXT_TYPE_STRING, nullptr, &greet};

const gs_ext_job kCountdown = {sizeof(gs_ext_job), "countdown", nullptr, &countdownStart, &countdownCancel};

const gs_ext_contribution kContributions[] = {
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "greet", &kGreet},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_JOB, "countdown", &kCountdown},
};

const gs_ext_plugin kPlugin = {
    sizeof(gs_ext_plugin), GS_EXT_ABI_VERSION, nullptr, 2, kContributions, &unload, nullptr, nullptr,
};

}

GS_EXT_EXPORT int32_t gs_ext_main(const gs_ext_host* host, const gs_ext_plugin** plugin)
{
    if (host == nullptr || plugin == nullptr || host->abi_version != GS_EXT_ABI_VERSION) {
        return GS_EXT_UNSUPPORTED;
    }
    g_host = host;
    *plugin = &kPlugin;
    return GS_EXT_OK;
}
)cpp";

constexpr std::string_view kNativeAgents = R"md(# Native extension

A GameScrcpy extension with a DLL written in C++ against `include/gamescrcpy_ext.h`, GameScrcpy's native interface. This folder is the extension: `manifest.json` describes it and the build puts `native.dll` next to it.

- Build with Visual Studio 2022 for x64: `cmake -S . -B .build` and `cmake --build .build --config Release`. Folders whose names start with a dot stay out of checks and packages, so `.build` never ships.
- Check: `gs validate .` checks the manifest and its files.
- Test: `gs ext test .` loads the manifest, the DLL and the scripts without GameScrcpy and without a phone, and runs `tests/*.json`. `--hosted` runs the DLL in gs-exthost.exe, as GameScrcpy runs extensions from others; `--json` writes the report for tools. The exit code is 0 when every step passes, 1 when a step fails and 2 when the extension cannot be loaded.
- Run a job from the command line: `gs ext . countdown --from 5` runs the command that `contributes.cli` declares.
- Pack: `gs pack .` writes the .gsext package next to this folder; it carries the sources too, so move them out first if you do not want to ship them.
- `src/native.cpp` offers the node `greet`, which scripts call as `ext.<extension id>.greet("world")` and which returns within 100 ms, and the job `countdown`, which runs on a thread of its own, tells how far it got and ends with exactly one `job_finish` or `job_fail`. Every struct starts with its size, and GameScrcpy only adds fields at their end.
- A test is a JSON file with a list of steps, such as `{ "steps": [ { "do": "call", "node": "greet", "args": ["world"], "expect": { "value": "Hello, world!" } } ] }`. The steps are call, job, job.cancel, job.wait, touch.open, touch.frame, touch.key, touch.text, touch.mouse, touch.pad, touch.close, devices, device.act, events, settings, wait, holder, holder.release, claim.want, claims, scripts and manifest.
- Set `license` in `manifest.json` to the license you publish under.
)md";

constexpr std::string_view kScriptMain = R"ts(const page = ui.panel("main");
let clicks = 0;

function draw(): void {
    page.render({
        type: "column",
        children: [
            { type: "text", text: `Clicked ${clicks} times` },
            { type: "button", id: "count", title: "Count", style: "primary" },
        ],
    });
}

page.on("click", ({ id }) => {
    if (id === "count") {
        clicks += 1;
        draw();
    }
});

draw();
log.info("ready");
)ts";

constexpr std::string_view kScriptAgents = R"md(# Script extension

A GameScrcpy extension whose logic is TypeScript: `src/main.ts` is an app script that starts with GameScrcpy and draws the page `main` of the main window with GameScrcpy's own controls.

- Check: `gs check .` type-checks the script and `gs validate .` checks the manifest and its files.
- Test: `gs ext test .` loads the manifest and the scripts without GameScrcpy and without a phone and runs `tests/*.json`; the step `scripts` tells whether each script loaded and what it logged. `--json` writes the report for tools.
- Pack: `gs pack .` writes the .gsext package next to this folder.
- GameScrcpy erases types instead of compiling them, so `enum`, namespaces with code, decorators and `import x = ...` are refused.
- `types/` is written by `gs types .`; run it again after GameScrcpy is updated.
- Set `license` in `manifest.json` to the license you publish under.
)md";

constexpr std::string_view kThemeAgents = R"md(# Theme extension

A GameScrcpy color theme: `manifest.json` gives colors for the dark and the light look by design token, and tokens it leaves out keep GameScrcpy's own colors.

- Check: `gs validate .`; test: `gs ext test .` runs `tests/*.json`; pack: `gs pack .`.
- Set `license` in `manifest.json` to the license you publish under.
)md";

std::string slugOf(std::string_view name)
{
    std::string slug;
    for (const char c : name) {
        const char lower = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        if ((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9')) {
            slug += lower;
        } else if (!slug.empty() && slug.back() != '-') {
            slug += '-';
        }
    }
    if (slug.size() > kMostSlugBytes) {
        slug.resize(kMostSlugBytes);
    }
    while (!slug.empty() && slug.back() == '-') {
        slug.pop_back();
    }
    return slug.empty() ? std::string("extension") : slug;
}

std::string titleOf(std::string text)
{
    if (text.size() > kMostTitleBytes) {
        text.resize(kMostTitleBytes);
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0U) == 0x80U) {
            text.pop_back();
        }
        if (!text.empty() && static_cast<unsigned char>(text.back()) >= 0xC0U) {
            text.pop_back();
        }
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '.')) {
        text.pop_back();
    }
    return text;
}

std::string projectOf(std::string_view slug)
{
    std::string name(slug);
    for (char& c : name) {
        if (c == '-') {
            c = '_';
        }
    }
    return name;
}

std::string replaced(std::string_view text, std::string_view marker, std::string_view value)
{
    std::string made(text);
    for (std::size_t at = made.find(marker); at != std::string::npos; at = made.find(marker, at + value.size())) {
        made.replace(at, marker.size(), value);
    }
    return made;
}

std::string apiFile(std::string_view path)
{
    for (const script::ApiFile& file : script::apiFiles()) {
        if (file.path == path) {
            return file.contents;
        }
    }
    return {};
}

std::string jsonText(const Json& value)
{
    return value.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

Json baseManifest(const std::string& id, const std::string& name, std::string_view description)
{
    Json manifest = Json::object();
    manifest["schemaVersion"] = 1;
    manifest["id"] = id;
    manifest["version"] = "0.1.0";
    manifest["name"] = name;
    manifest["description"] = std::string(description);
    manifest["license"] = "MIT";
    return manifest;
}

Json testFile(Json steps)
{
    Json test = Json::object();
    test["format"] = "gamescrcpy.ext-test";
    test["version"] = 1;
    test["steps"] = std::move(steps);
    return test;
}

std::vector<std::pair<std::string, std::string>> nativeFiles(const std::string& id, const std::string& name,
                                                             const std::string& slug)
{
    Json manifest = baseManifest(id, name, "A native extension with a node and a job.");
    manifest["requires"]["native"] = "^1.0";
    Json greet = Json::object();
    greet["name"] = "greet";
    greet["params"] = Json::array({Json{{"name", "name"}, {"type", "string"}}});
    greet["returns"] = "string";
    greet["description"] = "Says hello to a name.";
    Json countdown = Json::object();
    countdown["name"] = "countdown";
    countdown["title"] = "Count down";
    countdown["params"] = Json::array({Json{{"name", "from"}, {"type", "number"}}});
    countdown["returns"] = "json";
    countdown["description"] = "Counts down from a number to zero and tells how far it got.";
    countdown["most"] = 1;
    manifest["contributes"]["native"]["dll"] = "native.dll";
    manifest["contributes"]["native"]["nodes"] = Json::array({greet});
    manifest["contributes"]["native"]["jobs"] = Json::array({countdown});
    Json command = Json::object();
    command["name"] = "countdown";
    command["description"] = "Counts down to zero.";
    command["job"] = "countdown";
    command["params"] = Json::array({Json{{"name", "from"}, {"description", "Where to start"}, {"default", 3}}});
    manifest["contributes"]["cli"] = Json::array({command});
    Json call = Json::object();
    call["do"] = "call";
    call["node"] = "greet";
    call["args"] = Json::array({"world"});
    call["expect"]["value"] = "Hello, world!";
    Json job = Json::object();
    job["do"] = "job";
    job["job"] = "countdown";
    job["args"]["from"] = 3;
    job["expect"]["result"]["counted"] = 3;
    job["expect"]["progress"] = Json::array({"3 to go"});
    return {{"manifest.json", jsonText(manifest)},
            {"CMakeLists.txt", replaced(kNativeCMake, "@PROJECT@", projectOf(slug))},
            {"src/native.cpp", std::string(kNativeSource)},
            {std::string(kHeaderPath), apiFile(kHeaderPath)},
            {"tests/basic.json", jsonText(testFile(Json::array({call, job})))},
            {"AGENTS.md", std::string(kNativeAgents)},
            {".gitignore", ".build/\n"}};
}

std::vector<std::pair<std::string, std::string>> scriptFiles(const std::string& id, const std::string& name)
{
    Json manifest = baseManifest(id, name, "A script extension with a page in the main window.");
    manifest["requires"]["script"] = "^1.0";
    manifest["requires"]["panel"] = "^1.0";
    manifest["contributes"]["scripts"] =
        Json::array({Json{{"name", "main"}, {"entry", "src/main.ts"}, {"run", "app"}}});
    manifest["contributes"]["panels"] =
        Json::array({Json{{"name", "main"}, {"title", titleOf(name)}, {"where", "page"}, {"script", "main"}}});
    Json scripts = Json::object();
    scripts["do"] = "scripts";
    scripts["expect"]["loaded"] = Json::array({"main"});
    scripts["expect"]["logs"] = Json::array({"ready"});
    return {{"manifest.json", jsonText(manifest)},
            {"src/main.ts", std::string(kScriptMain)},
            {"tsconfig.json", script::scriptTsconfig()},
            {std::string(kApiTypesFile), apiDeclarations()},
            {"tests/basic.json", jsonText(testFile(Json::array({scripts})))},
            {"AGENTS.md", std::string(kScriptAgents)}};
}

std::vector<std::pair<std::string, std::string>> themeFiles(const std::string& id, const std::string& name)
{
    Json manifest = baseManifest(id, name, "A color theme for GameScrcpy.");
    manifest["requires"]["theme"] = "^1.0";
    Json theme = Json::object();
    theme["name"] = titleOf(name);
    theme["dark"] = Json{{"bgBase", "#10161B"},     {"bgRaised", "#161E25"},      {"bgOverlay", "#1C262F"},
                         {"bgElevated", "#232E38"}, {"bgSunken", "#0B1014"},      {"textPrimary", "#E3EBF1"},
                         {"textSecondary", "#A3B2BF"}, {"textTertiary", "#7C8B98"}};
    theme["light"] = Json{{"bgBase", "#E7ECF0"},     {"bgRaised", "#F3F6F8"},      {"bgOverlay", "#FFFFFF"},
                          {"bgElevated", "#FFFFFF"}, {"bgSunken", "#E1E7EC"},      {"textPrimary", "#101820"},
                          {"textSecondary", "#43525F"}, {"textTertiary", "#586774"}};
    manifest["contributes"]["themes"] = Json::array({theme});
    Json check = Json::object();
    check["do"] = "manifest";
    check["expect"]["value"]["contributes"]["themes"] = Json::array({Json{{"name", titleOf(name)}}});
    return {{"manifest.json", jsonText(manifest)},
            {"tests/basic.json", jsonText(testFile(Json::array({check})))},
            {"AGENTS.md", std::string(kThemeAgents)}};
}

}

int initExtension(std::string_view kind, const std::filesystem::path& folder, const std::optional<std::string>& id)
{
    if (kind != "native" && kind != "script" && kind != "theme") {
        std::cerr << "gs ext init: the kinds are native, script and theme\n";
        return kUsage;
    }
    std::error_code error;
    if (fs::exists(folder, error) && !fs::is_empty(folder, error)) {
        std::cerr << "gs ext init: " << utf8(folder) << " is not empty; choose a new folder\n";
        return kUsage;
    }
    fs::path normal = folder.lexically_normal();
    if (!normal.has_filename()) {
        normal = normal.parent_path();
    }
    std::string name = titleOf(utf8(normal.filename()));
    const std::string slug = slugOf(name);
    if (name.empty()) {
        name = slug;
    }
    const std::string chosen = id.value_or("my." + slug);
    if (!ext::validExtensionId(chosen)) {
        std::cerr << "gs ext init: " << chosen
                  << " is not an extension id; use two or more parts of lowercase letters, digits and dashes joined "
                     "by dots, such as alice.board-tools\n";
        return kUsage;
    }
    if (chosen.starts_with(ext::kReservedIdPrefix)) {
        std::cerr << "gs ext init: ids that start with " << ext::kReservedIdPrefix
                  << " are kept for GameScrcpy's own extensions\n";
        return kUsage;
    }
    const std::vector<std::pair<std::string, std::string>> files = kind == "native"   ? nativeFiles(chosen, name, slug)
                                                                   : kind == "script" ? scriptFiles(chosen, name)
                                                                                      : themeFiles(chosen, name);
    std::string written;
    for (const auto& [path, contents] : files) {
        if (!writeText(folder / pathOf(path), contents)) {
            std::cerr << "gs ext init: cannot write " << path << " in " << utf8(folder) << '\n';
            return kProblems;
        }
        written += (written.empty() ? "" : ", ") + path;
    }
    std::cout << "created " << utf8(folder) << " (" << chosen << "): " << written << '\n';
    if (kind == "native") {
        std::cout << "next: cmake -S \"" << utf8(folder) << "\" -B \"" << utf8(folder / ".build")
                  << "\", cmake --build \"" << utf8(folder / ".build") << "\" --config Release, then gs ext test \""
                  << utf8(folder) << "\"\n";
    } else {
        std::cout << "next: gs ext test \"" << utf8(folder) << "\"\n";
    }
    return kSuccess;
}

}
