#include "ExtCommands.h"
#include "KeyTools.h"
#include "McpRelay.h"
#include "ScriptProject.h"
#include "ScriptTools.h"

#include <gs/base/BuildInfo.h>
#include <gs/ext/Archive.h>
#include <gs/ext/Bundle.h>
#include <gs/ext/Diagnostics.h>
#include <gs/ext/FilterSource.h>
#include <gs/ext/Manifest.h>
#include <gs/ext/NativeCheck.h>
#include <gs/ext/Package.h>
#include <gs/ext/Resolution.h>
#include <gs/ext/Signing.h>
#include <gs/input/Profile.h>
#include <gs/input/ProfilePatch.h>
#include <gs/platform/AtomicFile.h>
#include <gs/script/ApiFiles.h>

#include <nlohmann/json.hpp>

#include <windows.h>

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace ext = gs::ext;
using Json = nlohmann::ordered_json;
using Microsoft::WRL::ComPtr;
using Reader = std::function<std::optional<std::string>(std::string_view path, std::uint64_t maxBytes)>;

constexpr int kSuccess = 0;
constexpr int kProblems = 1;
constexpr int kUsage = 2;
constexpr int kDefaultCompileSeconds = 20;
constexpr int kMostCompileSeconds = 3600;

constexpr std::string_view kUsageText =
    "usage: gs validate <folder | package.gsext | bundle.gsbundle> [--official] [--json]\n"
    "                   [--compile-timeout <seconds>]\n"
    "       gs pack <folder> [-o <package.gsext>] [--sign <secret key file>] [--official] [--json]\n"
    "               [--compile-timeout <seconds>]\n"
    "       gs ext keygen <secret key file> [--force] [--json]\n"
    "       gs new <script folder> [--profile <profile.json>]\n"
    "       gs check <script folder> [--json]\n"
    "       gs types <script folder>\n"
    "       gs run <script folder> --replay <session.json | recording.gsrec> [--until <ms>] [--seed <n>] [--json]\n"
    "       gs test <script folder> [--update] [--json]\n"
    "       gs api write|check <sdk folder>\n"
    "       gs ext test <folder | package.gsext> [--test <file>]... [--json] [--hosted] [--official]\n"
    "       gs ext init <native | script | theme> <folder> [--id <id>]\n"
    "       gs ext <id | folder | package.gsext> <command> [--<param> <value>]... [--json] [--hosted]\n"
    "       gs ext <id | folder | package.gsext> --help [--json]\n"
    "       gs mcp\n"
    "       gs --version\n"
    "--compile-timeout: seconds to wait for each shader to compile (1 to 3600, default 20)\n";

struct Options {
    std::wstring command;
    std::vector<fs::path> inputs;
    std::optional<fs::path> output;
    bool official = false;
    bool json = false;
    int compileSeconds = kDefaultCompileSeconds;
    std::optional<fs::path> sign;
};

struct Extras {
    std::optional<ext::SignatureCheck> signature;
    std::vector<ext::NativeLibrary> libraries;
};

struct Compiler {
    std::chrono::seconds limit{kDefaultCompileSeconds};
    bool stalled = false;
};

enum class Outcome : std::uint8_t { Compiled, Failed, TimedOut };

struct Compilation {
    Outcome outcome = Outcome::Failed;
    std::string errors;
};

struct BundleItem {
    ext::BundleEntry entry;
    std::vector<ext::Diagnostic> diagnostics;
};

std::string utf8(const fs::path& path)
{
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

fs::path pathOf(std::string_view text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

class Scratch
{
public:
    Scratch()
    {
        std::error_code error;
        const fs::path base = fs::temp_directory_path(error);
        std::random_device device;
        const fs::path folder = base / std::format("gs-validate-{:08x}{:08x}", device(), device());
        if (error || !fs::create_directory(folder, error)) {
            throw std::runtime_error("cannot create a temporary folder in " + utf8(base));
        }
        m_path = folder;
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    ~Scratch()
    {
        std::error_code error;
        fs::remove_all(m_path, error);
    }

    [[nodiscard]] const fs::path& path() const noexcept { return m_path; }

private:
    fs::path m_path;
};

std::optional<int> secondsOf(std::wstring_view text)
{
    if (text.empty() || text.size() > 4) {
        return std::nullopt;
    }
    int seconds = 0;
    for (const wchar_t digit : text) {
        if (digit < L'0' || digit > L'9') {
            return std::nullopt;
        }
        seconds = seconds * 10 + (digit - L'0');
    }
    if (seconds < 1 || seconds > kMostCompileSeconds) {
        return std::nullopt;
    }
    return seconds;
}

std::optional<Options> parse(int argc, wchar_t** argv)
{
    if (argc < 2) {
        return std::nullopt;
    }
    Options options;
    options.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::wstring_view argument = argv[i];
        if (argument == L"--json") {
            options.json = true;
        } else if (argument == L"--official") {
            options.official = true;
        } else if (argument == L"-o" && i + 1 < argc) {
            options.output = fs::path(argv[++i]);
        } else if (argument == L"--sign" && i + 1 < argc && !options.sign) {
            options.sign = fs::path(argv[++i]);
        } else if (argument == L"--compile-timeout" && i + 1 < argc) {
            const std::optional<int> seconds = secondsOf(argv[++i]);
            if (!seconds) {
                return std::nullopt;
            }
            options.compileSeconds = *seconds;
        } else if (argument.starts_with(L'-')) {
            return std::nullopt;
        } else {
            options.inputs.emplace_back(argument);
        }
    }
    const bool known = options.command == L"validate" || options.command == L"pack";
    if (!known || options.inputs.size() != 1 || (options.output && options.command != L"pack") ||
        (options.sign && options.command != L"pack")) {
        return std::nullopt;
    }
    return options;
}

bool isBundle(const fs::path& input)
{
    std::string suffix = utf8(input.extension());
    std::ranges::transform(suffix, suffix.begin(),
                           [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    return suffix == ".gsbundle";
}

Reader readerFor(const fs::path& input, bool folder)
{
    if (!folder) {
        return [input](std::string_view path, std::uint64_t maxBytes) {
            return ext::readArchiveFile(input, path, maxBytes);
        };
    }
    return [input](std::string_view path, std::uint64_t maxBytes) -> std::optional<std::string> {
        std::ifstream in(input / pathOf(path), std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        std::string text(static_cast<std::size_t>(maxBytes) + 1, '\0');
        in.read(text.data(), static_cast<std::streamsize>(text.size()));
        text.resize(static_cast<std::size_t>(in.gcount()));
        if (in.bad() || text.size() > maxBytes) {
            return std::nullopt;
        }
        return text;
    };
}

std::string printable(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto byte = static_cast<unsigned char>(text[index]);
        const auto next = index + 1 < text.size() ? static_cast<unsigned char>(text[index + 1]) : 0;
        const auto third = index + 2 < text.size() ? static_cast<unsigned char>(text[index + 2]) : 0;
        if (byte < 0x20 || byte == 0x7F) {
            out += std::format("\\x{:02X}", byte);
        } else if (byte == 0xC2 && next >= 0x80 && next <= 0x9F) {
            out += std::format("\\u{:04X}", next);
            index += 1;
        } else if (byte == 0xE2 && ((next == 0x80 && third >= 0xAA && third <= 0xAE) ||
                                    (next == 0x81 && third >= 0xA6 && third <= 0xA9))) {
            out += std::format("\\u{:04X}", 0x2000 + ((next & 0x3F) << 6) + (third & 0x3F));
            index += 2;
        } else {
            out += text[index];
        }
    }
    return out;
}

std::string trimmed(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == '\0')) {
        text.pop_back();
    }
    return text;
}

Compilation compileOnce(const std::string& shader, const std::string& file, const std::string& entry)
{
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    if (SUCCEEDED(D3DCompile(shader.data(), shader.size(), file.c_str(), nullptr, nullptr, entry.c_str(), "ps_5_0",
                             D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(), errors.GetAddressOf()))) {
        return {Outcome::Compiled, {}};
    }
    return {Outcome::Failed,
            errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize())
                   : std::string("the shader did not compile")};
}

Compilation compile(Compiler& compiler, std::string shader, std::string file, std::string entry)
{
    auto task = std::make_shared<std::packaged_task<Compilation()>>(
        [source = std::move(shader), name = std::move(file), function = std::move(entry)] {
            return compileOnce(source, name, function);
        });
    std::future<Compilation> done = task->get_future();
    std::thread([task] { (*task)(); }).detach();
    if (done.wait_for(compiler.limit) == std::future_status::timeout) {
        compiler.stalled = true;
        return {Outcome::TimedOut, {}};
    }
    return done.get();
}

void compileFilters(const ext::Manifest& manifest, const Reader& reader, Compiler& compiler,
                    std::vector<ext::Diagnostic>& diagnostics)
{
    for (std::size_t index = 0; index < manifest.contributes.filters.size() && !compiler.stalled; ++index) {
        const ext::FilterContribution& filter = manifest.contributes.filters[index];
        const std::string field = "$.contributes.filters[" + std::to_string(index) + "].file";
        const std::optional<std::string> text = reader(filter.file, ext::kMaxFilterSourceBytes);
        if (!text) {
            diagnostics.push_back({ext::Severity::Error, ext::Code::FileMissing, field, filter.file});
            continue;
        }
        const ext::FilterSource source = ext::parseFilterSource(*text);
        if (!source.usable()) {
            continue;
        }
        const std::string shader = ext::filterShaderText(source, filter.file);
        for (const ext::FilterPass& pass : source.passes) {
            const Compilation compiled = compile(compiler, shader, filter.file, ext::passEntryPoint(pass));
            if (compiled.outcome == Outcome::Compiled) {
                continue;
            }
            const std::string detail =
                compiled.outcome == Outcome::TimedOut
                    ? std::format("compilation timed out after {} s; the remaining shaders were not compiled",
                                  compiler.limit.count())
                    : trimmed(compiled.errors);
            diagnostics.push_back(
                {ext::Severity::Error, ext::Code::FilterCompileFailed, field, pass.name + ": " + detail});
            break;
        }
    }
}

void checkProfiles(const ext::Manifest& manifest, const Reader& reader, std::vector<ext::Diagnostic>& diagnostics)
{
    constexpr std::uint64_t kMaxProfileBytes = 1024 * 1024;
    constexpr std::string_view kEmptyProfile = R"({"format": "gamescrcpy.profile", "version": 3})";
    const auto described = [](const gs::input::Diagnostic& diagnostic) {
        return diagnostic.detail.empty() ? diagnostic.path : diagnostic.path + ": " + diagnostic.detail;
    };
    const auto severity = [](gs::input::Severity level) {
        return level == gs::input::Severity::Error ? ext::Severity::Error : ext::Severity::Warning;
    };
    std::map<std::string, std::string, std::less<>> own;
    for (std::size_t index = 0; index < manifest.contributes.profiles.size(); ++index) {
        const ext::ProfileContribution& profile = manifest.contributes.profiles[index];
        const std::string field = "$.contributes.profiles[" + std::to_string(index) + "].file";
        std::optional<std::string> text = reader(profile.file, kMaxProfileBytes);
        if (!text) {
            diagnostics.push_back({ext::Severity::Error, ext::Code::FileMissing, field, profile.file});
            continue;
        }
        const gs::input::LoadResult loaded = gs::input::loadProfile(*text);
        for (const gs::input::Diagnostic& diagnostic : loaded.diagnostics) {
            if (diagnostic.severity != gs::input::Severity::Info) {
                diagnostics.push_back({severity(diagnostic.severity), ext::Code::ProfileInvalid, field, described(diagnostic)});
            }
        }
        if (loaded.ok()) {
            own.emplace(manifest.id + "/" + profile.name, std::move(*text));
        }
    }
    for (std::size_t index = 0; index < manifest.contributes.profilePatches.size(); ++index) {
        const ext::ProfilePatchContribution& patch = manifest.contributes.profilePatches[index];
        const std::string field = "$.contributes.profilePatches[" + std::to_string(index) + "].file";
        const std::optional<std::string> text = reader(patch.file, kMaxProfileBytes);
        if (!text) {
            diagnostics.push_back({ext::Severity::Error, ext::Code::FileMissing, field, patch.file});
            continue;
        }
        const auto target = own.find(patch.target);
        const bool known = target != own.end();
        const gs::input::PatchOutcome outcome =
            gs::input::applyPatch(known ? std::string_view(target->second) : kEmptyProfile, *text);
        for (const gs::input::Diagnostic& diagnostic : outcome.diagnostics) {
            const bool missing = diagnostic.code == gs::input::DiagnosticCode::UnknownReference;
            if (!known && (missing || diagnostic.code == gs::input::DiagnosticCode::PatchBreaksProfile)) {
                continue;
            }
            diagnostics.push_back({missing ? ext::Severity::Warning : severity(diagnostic.severity),
                                   missing ? ext::Code::PatchTargetMissing : ext::Code::PatchInvalid, field,
                                   described(diagnostic)});
        }
    }
}

ext::PackageCheck inspect(const fs::path& input, bool folder, const ext::LoadOptions& load, Compiler& compiler)
{
    ext::PackageCheck check = folder ? ext::checkDirectory(input, load) : ext::checkPackage(input, load);
    if (check.manifest && !ext::hasErrors(check.diagnostics)) {
        const Reader reader = readerFor(input, folder);
        compileFilters(*check.manifest, reader, compiler, check.diagnostics);
        checkProfiles(*check.manifest, reader, check.diagnostics);
    }
    if (check.manifest) {
        if (!check.manifest->gamescrcpy) {
            check.diagnostics.push_back(
                {ext::Severity::Warning, ext::Code::GameScrcpyVersionMissing, "$.gamescrcpy"});
        }
        std::vector<ext::Diagnostic> compatibility =
            ext::checkCompatibility(*check.manifest, ext::currentHostApi());
        check.diagnostics.insert(check.diagnostics.end(), compatibility.begin(), compatibility.end());
    }
    return check;
}

Extras inspectExtras(const fs::path& input, bool folder, const ext::Manifest& manifest, bool resigning,
                     std::vector<ext::Diagnostic>& diagnostics)
{
    Extras extras;
    const ext::PackageContents contents = folder ? ext::directoryContents(input) : ext::archiveContents(input);
    ext::LibraryCheck libraries = ext::checkNativeLibraries(contents, manifest);
    std::ranges::move(libraries.diagnostics, std::back_inserter(diagnostics));
    extras.libraries = std::move(libraries.libraries);
    if (!resigning) {
        ext::SignatureCheck signature = ext::checkSignature(contents, manifest.authorKey);
        if (signature.state == ext::SignatureState::Invalid) {
            diagnostics.push_back({ext::Severity::Error, ext::Code::SignatureInvalid,
                                   std::string(ext::kChecksumsSignatureFile), signature.problem});
        }
        extras.signature = std::move(signature);
    }
    return extras;
}

std::string_view signatureName(ext::SignatureState state) noexcept
{
    switch (state) {
    case ext::SignatureState::Unsigned:
        return "unsigned";
    case ext::SignatureState::Valid:
        return "valid";
    case ext::SignatureState::Invalid:
        return "invalid";
    }
    return "unsigned";
}

std::optional<ext::MinisignSecretKey> loadSecretKey(const fs::path& file)
{
    constexpr std::size_t kMostKeyBytes = 4096;
    std::string text;
    if (gs::platform::readFile(file, kMostKeyBytes, text, gs::platform::WhenBusy::GiveUp)) {
        std::cerr << "gs pack: cannot read the secret key " << utf8(file) << '\n';
        return std::nullopt;
    }
    const ext::LoadedSecretKey loaded = ext::parseMinisignSecretKey(text);
    switch (loaded.state) {
    case ext::SecretKeyState::Valid:
        return loaded.key;
    case ext::SecretKeyState::Encrypted:
        std::cerr << "gs pack: " << utf8(file)
                  << " is protected by a password, and gs signs only with keys without one; make a key with "
                     "gs ext keygen, or take the password off a copy with minisign -C -W\n";
        return std::nullopt;
    case ext::SecretKeyState::Damaged:
        std::cerr << "gs pack: " << utf8(file) << " is damaged: its checksum does not match the key\n";
        return std::nullopt;
    case ext::SecretKeyState::Malformed:
        break;
    }
    std::cerr << "gs pack: " << utf8(file) << " is not a minisign secret key\n";
    return std::nullopt;
}

Json listed(const std::vector<ext::Diagnostic>& diagnostics)
{
    Json rows = Json::array();
    for (const ext::Diagnostic& diagnostic : diagnostics) {
        rows.push_back({{"severity", std::string(ext::severityName(diagnostic.severity))},
                        {"code", std::string(ext::codeName(diagnostic.code))},
                        {"path", diagnostic.path},
                        {"detail", diagnostic.detail},
                        {"hint", std::string(ext::fixHint(diagnostic.code))}});
    }
    return rows;
}

void printJson(const Json& report)
{
    std::cout << report.dump(2, ' ', false, Json::error_handler_t::replace) << '\n';
}

void printDiagnostics(const std::vector<ext::Diagnostic>& diagnostics)
{
    for (const ext::Diagnostic& diagnostic : diagnostics) {
        std::cout << ext::severityName(diagnostic.severity) << ' ' << ext::codeName(diagnostic.code) << " at "
                  << printable(diagnostic.path);
        if (!diagnostic.detail.empty()) {
            std::cout << ": " << printable(diagnostic.detail);
        }
        std::cout << '\n';
        if (diagnostic.severity != ext::Severity::Notice) {
            std::cout << "  " << ext::fixHint(diagnostic.code) << '\n';
        }
    }
}

std::string tally(const std::vector<ext::Diagnostic>& diagnostics)
{
    const auto errors = std::ranges::count(diagnostics, ext::Severity::Error, &ext::Diagnostic::severity);
    const auto warnings = std::ranges::count(diagnostics, ext::Severity::Warning, &ext::Diagnostic::severity);
    if (!ext::hasErrors(diagnostics) && warnings == 0) {
        return "ok";
    }
    return std::format("{} error(s), {} warning(s)", errors, warnings);
}

void print(const Options& options, const std::optional<ext::Manifest>& manifest,
           const std::vector<ext::Diagnostic>& diagnostics, const fs::path& input, const std::optional<fs::path>& output,
           const Extras& extras)
{
    if (options.json) {
        Json report = {{"ok", !ext::hasErrors(diagnostics)}};
        if (manifest) {
            report["id"] = manifest->id;
            report["version"] = manifest->version.text();
        }
        if (output) {
            report["output"] = utf8(*output);
        }
        if (extras.signature) {
            Json signature = {{"state", std::string(signatureName(extras.signature->state))}};
            if (extras.signature->state == ext::SignatureState::Valid && extras.signature->key) {
                signature["keyId"] = ext::minisignKeyId(*extras.signature->key);
            }
            report["signature"] = std::move(signature);
        }
        if (!extras.libraries.empty()) {
            Json libraries = Json::array();
            for (const ext::NativeLibrary& library : extras.libraries) {
                libraries.push_back({{"architecture", library.architecture},
                                     {"path", library.path},
                                     {"machine", ext::machineName(library.machine)},
                                     {"entry", library.entry}});
            }
            report["libraries"] = std::move(libraries);
        }
        report["diagnostics"] = listed(diagnostics);
        printJson(report);
        return;
    }
    printDiagnostics(diagnostics);
    const std::string subject = manifest ? manifest->id + " " + manifest->version.text() : utf8(input);
    std::cout << subject << ": " << tally(diagnostics) << '\n';
    for (const ext::NativeLibrary& library : extras.libraries) {
        std::cout << printable(library.path) << ": " << ext::machineName(library.machine);
        if (!library.architecture.empty()) {
            std::cout << " (listed for " << printable(library.architecture) << ")";
        }
        std::cout << (library.entry ? ", exports gs_ext_main\n" : ", does not export gs_ext_main\n");
    }
    if (extras.signature && extras.signature->state == ext::SignatureState::Valid && extras.signature->key) {
        std::cout << "signed with key " << ext::minisignKeyId(*extras.signature->key) << '\n';
    } else if (extras.signature && extras.signature->state == ext::SignatureState::Unsigned) {
        std::cout << "not signed\n";
    }
    if (output) {
        std::cout << "packed " << utf8(*output) << '\n';
    }
}

void printBundle(const Options& options, const fs::path& input, const ext::BundleCheck& check,
                 const std::vector<BundleItem>& items, const std::vector<ext::Diagnostic>& all)
{
    if (options.json) {
        Json rows = Json::array();
        for (const BundleItem& item : items) {
            Json row = {{"id", item.entry.id},
                        {"version", item.entry.version.text()},
                        {"builtin", item.entry.builtin},
                        {"checked", !item.entry.builtin}};
            if (!item.entry.builtin) {
                row["package"] = ext::bundlePackagePath(item.entry);
                row["ok"] = !ext::hasErrors(item.diagnostics);
            }
            rows.push_back(std::move(row));
        }
        Json report = {{"ok", !ext::hasErrors(all)}};
        if (check.bundle) {
            report["name"] = check.bundle->name;
        }
        report["extensions"] = std::move(rows);
        report["diagnostics"] = listed(all);
        printJson(report);
        return;
    }
    printDiagnostics(check.diagnostics);
    for (const BundleItem& item : items) {
        printDiagnostics(item.diagnostics);
        std::cout << item.entry.id << ' ' << item.entry.version.text() << ": "
                  << (item.entry.builtin ? std::string("builtin, not checked") : tally(item.diagnostics)) << '\n';
    }
    std::cout << utf8(input) << ": " << tally(all) << '\n';
}

std::vector<ext::Diagnostic> checkMember(const fs::path& bundle, const ext::BundleEntry& entry, const fs::path& file,
                                         const ext::LoadOptions& load, Compiler& compiler)
{
    std::vector<ext::Diagnostic> diagnostics = ext::extractBundlePackage(bundle, entry, file);
    if (ext::hasErrors(diagnostics)) {
        return diagnostics;
    }
    const std::string inner = ext::bundlePackagePath(entry);
    ext::PackageCheck check = inspect(file, false, load, compiler);
    if (check.manifest && !ext::hasErrors(check.diagnostics)) {
        inspectExtras(file, false, *check.manifest, false, check.diagnostics);
    }
    for (ext::Diagnostic& diagnostic : check.diagnostics) {
        diagnostic.path = inner + ":" + diagnostic.path;
        diagnostics.push_back(std::move(diagnostic));
    }
    if (check.manifest && (check.manifest->id != entry.id || check.manifest->version != entry.version)) {
        diagnostics.push_back({ext::Severity::Error, ext::Code::BundlePackageMismatch, inner,
                               check.manifest->id + " " + check.manifest->version.text()});
    }
    return diagnostics;
}

int validateBundle(const Options& options, const fs::path& input, const ext::LoadOptions& load, Compiler& compiler)
{
    ext::BundleCheck check = ext::checkBundle(input);
    std::vector<BundleItem> items;
    std::optional<Scratch> scratch;
    if (check.bundle) {
        for (const ext::BundleEntry& entry : check.bundle->extensions) {
            BundleItem& item = items.emplace_back(BundleItem{entry, {}});
            if (entry.builtin) {
                continue;
            }
            const std::string inner = ext::bundlePackagePath(entry);
            const auto missing = std::ranges::find_if(check.diagnostics, [&inner](const ext::Diagnostic& diagnostic) {
                return diagnostic.code == ext::Code::BundlePackageMissing && diagnostic.path == inner;
            });
            if (missing != check.diagnostics.end()) {
                item.diagnostics.push_back(std::move(*missing));
                check.diagnostics.erase(missing);
                continue;
            }
            if (!scratch) {
                scratch.emplace();
            }
            item.diagnostics =
                checkMember(input, entry, scratch->path() / std::format("{}.gsext", items.size()), load, compiler);
        }
    }
    std::vector<ext::Diagnostic> all = check.diagnostics;
    for (const BundleItem& item : items) {
        all.insert(all.end(), item.diagnostics.begin(), item.diagnostics.end());
    }
    printBundle(options, input, check, items, all);
    return ext::hasErrors(all) ? kProblems : kSuccess;
}

int validate(const Options& options, Compiler& compiler)
{
    const fs::path& input = options.inputs.front();
    std::error_code error;
    if (!fs::exists(input, error)) {
        std::cerr << "gs validate: " << utf8(input) << " does not exist\n";
        return kUsage;
    }
    const bool folder = fs::is_directory(input, error);
    const ext::LoadOptions load{.official = options.official};
    if (!folder && isBundle(input)) {
        return validateBundle(options, input, load, compiler);
    }
    ext::PackageCheck check = inspect(input, folder, load, compiler);
    Extras extras;
    if (check.manifest && !ext::hasErrors(check.diagnostics)) {
        extras = inspectExtras(input, folder, *check.manifest, false, check.diagnostics);
    }
    print(options, check.manifest, check.diagnostics, input, std::nullopt, extras);
    return ext::hasErrors(check.diagnostics) ? kProblems : kSuccess;
}

int pack(const Options& options, Compiler& compiler)
{
    const fs::path& folder = options.inputs.front();
    std::error_code error;
    if (!fs::is_directory(folder, error)) {
        std::cerr << "gs pack: " << utf8(folder) << " is not a folder\n";
        return kUsage;
    }
    std::optional<ext::MinisignSecretKey> key;
    if (options.sign) {
        key = loadSecretKey(*options.sign);
        if (!key) {
            return kUsage;
        }
    }
    const ext::LoadOptions load{.official = options.official};
    ext::PackageCheck check = inspect(folder, true, load, compiler);
    std::vector<ext::Diagnostic> diagnostics = std::move(check.diagnostics);
    Extras extras;
    if (check.manifest && !ext::hasErrors(diagnostics)) {
        extras = inspectExtras(folder, true, *check.manifest, key.has_value(), diagnostics);
    }
    if (key && check.manifest) {
        const ext::MinisignKey own = ext::minisignPublicKey(*key);
        const std::optional<ext::MinisignKey> named = ext::parseMinisignKey(check.manifest->authorKey);
        if (!named || *named != own) {
            diagnostics.push_back({ext::Severity::Error, ext::Code::SigningKeyMismatch, "$.authorKey",
                                   "\"authorKey\": \"" + ext::minisignKeyText(own) + "\""});
        }
    }
    std::optional<fs::path> output;
    if (check.manifest && !ext::hasErrors(diagnostics)) {
        fs::path parent = fs::absolute(folder).lexically_normal();
        if (!parent.has_filename()) {
            parent = parent.parent_path();
        }
        output = options.output.value_or(parent.parent_path() /
                                         pathOf(check.manifest->id + "-" + check.manifest->version.text() + ".gsext"));
        std::error_code outputError;
        std::error_code folderError;
        const fs::path target = fs::weakly_canonical(fs::absolute(*output, outputError), outputError);
        const fs::path root = fs::weakly_canonical(fs::absolute(folder, folderError), folderError);
        if (outputError || folderError) {
            std::cerr << "gs pack: cannot resolve " << utf8(*output) << '\n';
            return kUsage;
        }
        const fs::path inside = target.lexically_relative(root);
        if (!inside.empty() && *inside.begin() != "..") {
            std::cerr << "gs pack: write the package outside the folder being packed\n";
            return kUsage;
        }
        std::vector<ext::Diagnostic> packed =
            key ? ext::packSigned(folder, *output, *key, ext::signingComment(*check.manifest))
                : ext::packDirectory(folder, *output);
        if (ext::hasErrors(packed)) {
            output.reset();
        }
        diagnostics.insert(diagnostics.end(), packed.begin(), packed.end());
        if (output && key) {
            ext::SignatureCheck made = ext::checkSignature(ext::archiveContents(*output), check.manifest->authorKey);
            if (made.state != ext::SignatureState::Valid) {
                diagnostics.push_back({ext::Severity::Error, ext::Code::SignatureInvalid,
                                       std::string(ext::kChecksumsSignatureFile), made.problem});
            }
            extras.signature = std::move(made);
        }
    }
    print(options, check.manifest, diagnostics, folder, output, extras);
    return ext::hasErrors(diagnostics) ? kProblems : kSuccess;
}

int api(int argc, wchar_t** argv)
{
    const std::wstring_view verb = argc == 4 ? std::wstring_view(argv[2]) : std::wstring_view();
    if (verb != L"write" && verb != L"check") {
        std::cerr << kUsageText;
        return kUsage;
    }
    const fs::path root(argv[3]);
    const std::vector<gs::script::ApiFile> files = gs::script::apiFiles();
    std::size_t stale = 0;
    for (const gs::script::ApiFile& file : files) {
        const fs::path target = root / pathOf(file.path);
        if (verb == L"write") {
            std::error_code error;
            fs::create_directories(target.parent_path(), error);
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            out.write(file.contents.data(), static_cast<std::streamsize>(file.contents.size()));
            out.close();
            if (error || !out) {
                std::cerr << "gs api: cannot write " << utf8(target) << '\n';
                return kProblems;
            }
            continue;
        }
        std::ifstream in(target, std::ios::binary);
        std::string current{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        std::erase(current, '\r');
        if (!in.is_open() || current != file.contents) {
            std::cout << "out of date: " << file.path << '\n';
            ++stale;
        }
    }
    if (stale > 0) {
        std::cout << stale << " of " << files.size() << " generated API files differ; run gs api write "
                  << utf8(root) << '\n';
        return kProblems;
    }
    std::cout << (verb == L"write" ? "wrote " : "up to date: ") << files.size() << " generated API files in "
              << utf8(root) << '\n';
    return kSuccess;
}

int run(int argc, wchar_t** argv, Compiler& compiler)
{
    if (argc == 2 && std::wstring_view(argv[1]) == L"--version") {
        std::cout << "gs " << gs::buildInfo().version << '\n';
        return kSuccess;
    }
    if (argc >= 2 && std::wstring_view(argv[1]) == L"api") {
        return api(argc, argv);
    }
    if (argc >= 2 && std::wstring_view(argv[1]) == L"mcp") {
        return gs::cli::runMcpRelay(std::vector<std::wstring_view>(argv + 2, argv + argc));
    }
    if (const std::optional<int> handled = gs::cli::runKeyCommand(argc, argv)) {
        return *handled;
    }
    if (argc >= 2 && std::wstring_view(argv[1]) == L"ext") {
        return gs::cli::runExtCommand(std::vector<std::wstring_view>(argv + 2, argv + argc));
    }
    if (const std::optional<int> handled = gs::cli::runScriptCommand(argc, argv)) {
        return *handled;
    }
    const std::optional<Options> options = parse(argc, argv);
    if (!options) {
        std::cerr << kUsageText;
        return kUsage;
    }
    compiler.limit = std::chrono::seconds(options->compileSeconds);
    return options->command == L"validate" ? validate(*options, compiler) : pack(*options, compiler);
}

bool wantsJson(int argc, wchar_t** argv)
{
    return argc > 1 && std::any_of(argv + 1, argv + argc,
                                   [](const wchar_t* argument) { return std::wstring_view(argument) == L"--json"; });
}

}

int wmain(int argc, wchar_t** argv)
{
    std::ostringstream out;
    std::ostringstream err;
    std::streambuf* const outBuffer = std::cout.rdbuf(out.rdbuf());
    std::streambuf* const errBuffer = std::cerr.rdbuf(err.rdbuf());
    Compiler compiler;
    int result = kProblems;
    try {
        result = run(argc, argv, compiler);
    } catch (const std::exception& failure) {
        if (wantsJson(argc, argv)) {
            const Json report = {{"ok", false}, {"error", failure.what()}, {"diagnostics", Json::array()}};
            out.str({});
            out << report.dump(2, ' ', false, Json::error_handler_t::replace) << '\n';
        }
        err << "gs: " << failure.what() << '\n';
    }
    std::cout.rdbuf(outBuffer);
    std::cerr.rdbuf(errBuffer);
    gs::cli::writeTo(gs::cli::Stream::Out, out.str());
    gs::cli::writeTo(gs::cli::Stream::Error, err.str());
    if (compiler.stalled) {
        TerminateProcess(GetCurrentProcess(), static_cast<UINT>(result));
    }
    return result;
}
