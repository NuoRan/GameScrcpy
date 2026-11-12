#include "PeImage.h"
#include "TestFiles.h"

#include "gs/ext/Archive.h"
#include "gs/ext/Manifest.h"
#include "gs/ext/Minisign.h"
#include "gs/ext/NativeCheck.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <windows.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace gs::ext;
using namespace gs::ext::test;
namespace fs = std::filesystem;

namespace {

constexpr std::string_view kAuthorKey = "RWQYB7al9OPSwU7aritYZ3nmUlPQk5fh7N4Izs3H4UUtma8A/RO4rFBZ";
constexpr std::string_view kOtherKey = "RWSId2ZVRDMiEa7DCLu1nxR+gMiGsuYNvpyJdwItkg7PqsjnQoi009w+";
constexpr std::string_view kSignedManifest =
    "{\"schemaVersion\": 1, \"id\": \"alice.signed\", \"version\": \"1.0.0\", \"name\": \"Signed\", \"license\": "
    "\"MIT\", \"requires\": {\"filter\": \"^1.0\"}, \"authorKey\": "
    "\"RWQYB7al9OPSwU7aritYZ3nmUlPQk5fh7N4Izs3H4UUtma8A/RO4rFBZ\"}\n";
constexpr std::string_view kNotes = "native code goes here\n";
constexpr std::string_view kSums =
    "8f0c07221899cf4b21fd8db290633f7ebbc679c5ea4d43dce3035ac08948b215bdc433d58766e8ba0afc47b539ca26d9e5a86232af9b468ae4"
    "2b58142c549622  bin/notes.txt\n"
    "7ff8902aa979b3ce4c4c4d2cef72164d98e76fabdc123bb33fc427b03d8ab0434d52e1cfaab5dbef804fb1fa4571b2ce2f073dec5678cf0ea"
    "cd7576f9e853503  manifest.json\n";
constexpr std::string_view kSumsSignature =
    "untrusted comment: signature from minisign secret key\n"
    "RUQYB7al9OPSwclsdUHnR82yKgl9sMEEog35i7nUcxkXSFasQWdcntEfLrQXbjs4WTKgCujPl8LPT6WFVqVwtyNwmSb6kiJCew8=\n"
    "trusted comment: timestamp:1762430400\tfile:SHA512SUMS\thashed\n"
    "7dW7uSQ9WQCkAIlOHYsX6eh14BlNBXzbaNf/AzgxyocrtvDN+S1L55byuVr3436Cl2Co4sjq+YN3woTR97AbBQ==\n";

PeLayout mixedLayout()
{
    return PeLayout{.wide = true,
                    .imports = {{"KERNEL32.dll", {"CreateFileW", "WriteProcessMemory"}},
                                {"WS2_32.dll", {"connect", "htons", "#23"}},
                                {"SHELL32.dll", {"ShellExecuteW"}}},
                    .delayed = {{"USER32.dll", {"GetAsyncKeyState"}}},
                    .virtualDelay = false};
}

fs::path systemLibrary(std::wstring_view name)
{
    std::array<wchar_t, MAX_PATH> buffer{};
    const UINT length = GetSystemDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    return fs::path(std::wstring(buffer.data(), length)) / name;
}

NativeScanner verdict(ScanVerdict result)
{
    return [result](std::string_view, std::string_view) { return result; };
}

bool has(const std::vector<CapabilityUse>& uses, Capability capability)
{
    return std::ranges::any_of(uses, [capability](const CapabilityUse& use) { return use.capability == capability; });
}

void writeSigned(const fs::path& folder)
{
    writeFile(folder / "manifest.json", kSignedManifest);
    writeFile(folder / "bin/notes.txt", kNotes);
    writeFile(folder / "SHA512SUMS", kSums);
    writeFile(folder / "SHA512SUMS.minisig", kSumsSignature);
}

}

TEST_CASE("the import table of a real system library is read, with the libraries it imports from")
{
    const std::string image = readFile(systemLibrary(L"kernel32.dll"));
    REQUIRE(looksLikePe(image));
    const PeImports imports = readPeImports(image);
    INFO(imports.problem);
    REQUIRE(imports.valid);
    CHECK(imports.wide == (sizeof(void*) == 8));
    CHECK(imports.functions.size() > 100);
    CHECK(std::ranges::all_of(imports.functions, [](const ImportedFunction& function) {
        return !function.module.empty() && (function.byOrdinal || !function.name.empty());
    }));
    CHECK(std::ranges::any_of(imports.functions, [](const ImportedFunction& function) {
        return function.module == "ntdll.dll" || function.module.starts_with("api-ms-win-");
    }));
    CHECK(std::ranges::none_of(imports.functions, [](const ImportedFunction& function) {
        return std::ranges::any_of(function.module, [](char c) { return c >= 'A' && c <= 'Z'; });
    }));
}

TEST_CASE("a hand-built library lists its imports with their libraries, ordinals and delay-loaded ones")
{
    const PeImports wide = readPeImports(buildPe(mixedLayout()));
    INFO(wide.problem);
    REQUIRE(wide.valid);
    CHECK(wide.wide);
    CHECK(wide.machine == 0x8664);
    const std::vector<ImportedFunction> expected{
        {.module = "kernel32.dll", .name = "CreateFileW"},
        {.module = "kernel32.dll", .name = "WriteProcessMemory"},
        {.module = "ws2_32.dll", .name = "connect"},
        {.module = "ws2_32.dll", .name = "htons"},
        {.module = "ws2_32.dll", .ordinal = 23, .byOrdinal = true},
        {.module = "shell32.dll", .name = "ShellExecuteW"},
        {.module = "user32.dll", .name = "GetAsyncKeyState", .delayed = true},
    };
    CHECK(wide.functions == expected);

    const PeImports plain = readPeImports(buildPe({.wide = false, .imports = {{"kernel32.dll", {"GetTickCount"}}}}));
    INFO(plain.problem);
    REQUIRE(plain.valid);
    CHECK_FALSE(plain.wide);
    CHECK(plain.machine == 0x014C);
    CHECK(plain.functions == std::vector<ImportedFunction>{{.module = "kernel32.dll", .name = "GetTickCount"}});

    const PeImports old = readPeImports(buildPe({.wide = false,
                                                 .imports = {{"kernel32.dll", {"GetTickCount"}}},
                                                 .delayed = {{"WSOCK32.dll", {"#4", "recv"}}},
                                                 .virtualDelay = true}));
    INFO(old.problem);
    REQUIRE(old.valid);
    CHECK(old.functions == std::vector<ImportedFunction>{
                               {.module = "kernel32.dll", .name = "GetTickCount"},
                               {.module = "wsock32.dll", .ordinal = 4, .byOrdinal = true, .delayed = true},
                               {.module = "wsock32.dll", .name = "recv", .delayed = true},
                           });

    const PeImports none = readPeImports(buildPe({}));
    CHECK(none.valid);
    CHECK(none.functions.empty());
}

TEST_CASE("a damaged library is not read and says why")
{
    const std::string image = buildPe(mixedLayout());
    const PeImports cut = readPeImports(image.substr(0, 0x260));
    CHECK_FALSE(cut.valid);
    CHECK(cut.functions.empty());
    CHECK_FALSE(cut.problem.empty());

    std::string moved = image;
    moved[0x3C] = '\xF0';
    moved[0x3D] = '\xFF';
    CHECK_FALSE(looksLikePe(moved));
    CHECK_FALSE(readPeImports(moved).valid);

    std::string dos(100, '\0');
    dos[0] = 'M';
    dos[1] = 'Z';
    CHECK_FALSE(looksLikePe(dos));
    CHECK_FALSE(looksLikePe("MZ"));

    std::string headless(0x58, '\0');
    headless[0] = 'M';
    headless[1] = 'Z';
    headless[0x3C] = '\x40';
    headless.replace(0x40, 4, std::string("PE\0\0", 4));
    CHECK(looksLikePe(headless));
    const PeImports stub = readPeImports(headless);
    CHECK_FALSE(stub.valid);
    CHECK_FALSE(stub.problem.empty());
}

TEST_CASE("imported functions are sorted into network, files, input, programs and injection")
{
    const std::vector<CapabilityUse> uses = classifyImports(readPeImports(buildPe(mixedLayout())).functions);
    REQUIRE(uses.size() == 5);
    CHECK(uses[0] == CapabilityUse{Capability::Network, {"ws2_32.dll!connect", "ws2_32.dll!#23"}});
    CHECK(uses[1] == CapabilityUse{Capability::Files, {"kernel32.dll!CreateFileW"}});
    CHECK(uses[2] == CapabilityUse{Capability::Input, {"user32.dll!GetAsyncKeyState"}});
    CHECK(uses[3] == CapabilityUse{Capability::Processes, {"shell32.dll!ShellExecuteW"}});
    CHECK(uses[4] == CapabilityUse{Capability::Injection, {"kernel32.dll!WriteProcessMemory"}});
    CHECK(capabilityName(Capability::Input) == "input");

    const std::vector<ImportedFunction> more{
        {.module = "kernel32.dll", .ordinal = 12, .byOrdinal = true},
        {.module = "api-ms-win-core-processthreads-l1-1-0.dll", .name = "CreateRemoteThreadEx"},
        {.module = "user32.dll", .name = "SetWindowsHookExW"},
        {.module = "ucrtbase.dll", .name = "_wspawnl"},
        {.module = "mylib.dll", .name = "_spawnHelper"},
        {.module = "iphlpapi.dll", .name = "IcmpSendEcho"},
        {.module = "api-ms-win-crt-stdio-l1-1-0.dll", .name = "fopen"},
        {.module = "kernel32.dll", .name = "GetTickCount"},
        {.module = "comctl32.dll", .ordinal = 17, .byOrdinal = true},
    };
    const std::vector<CapabilityUse> sorted = classifyImports(more);
    REQUIRE(sorted.size() == 4);
    CHECK(sorted[0] == CapabilityUse{Capability::Network, {"iphlpapi.dll!IcmpSendEcho"}});
    CHECK(sorted[1] == CapabilityUse{Capability::Files, {"api-ms-win-crt-stdio-l1-1-0.dll!fopen"}});
    CHECK(sorted[2] == CapabilityUse{Capability::Processes, {"ucrtbase.dll!_wspawnl"}});
    CHECK(sorted[3] == CapabilityUse{Capability::Injection,
                                     {"kernel32.dll!#12", "api-ms-win-core-processthreads-l1-1-0.dll!CreateRemoteThreadEx",
                                      "user32.dll!SetWindowsHookExW"}});
    CHECK(classifyImports({}).empty());
}

TEST_CASE("what the permissions do not cover is listed, and starting programs always is")
{
    const std::vector<CapabilityUse> uses = classifyImports(readPeImports(buildPe(mixedLayout())).functions);
    const Permission network{.name = "network", .reason = "Talks to its server.", .hosts = {"api.example.com"}};
    const Permission pick{.name = "files.pick", .reason = "Opens a file you choose."};
    const Permission paths{.name = "files.paths", .reason = "Keeps its presets.", .paths = {"%APPDATA%/Kit"}};
    const Permission input{.name = "input.read", .reason = "Reads the keys you press."};
    CHECK(uncoveredCapabilities(uses, {}) ==
          std::vector<Capability>{Capability::Network, Capability::Files, Capability::Input, Capability::Processes});
    CHECK(uncoveredCapabilities(uses, {network}) ==
          std::vector<Capability>{Capability::Files, Capability::Input, Capability::Processes});
    CHECK(uncoveredCapabilities(uses, {network, pick, input}) == std::vector<Capability>{Capability::Processes});
    CHECK(uncoveredCapabilities(uses, {network, paths, input}) == std::vector<Capability>{Capability::Processes});
    CHECK(uncoveredCapabilities({}, {}).empty());
}

TEST_CASE("a package with native code reports its libraries, the scan and what it does not ask for")
{
    Scratch scratch;
    const fs::path folder = scratch / "native";
    writeFile(folder / "manifest.json", "{}");
    writeFile(folder / "bin/helper.dll",
              buildPe({.imports = {{"ws2_32.dll", {"connect"}}, {"kernel32.dll", {"CreateFileW", "GetTickCount"}}}}));
    writeFile(folder / "bin/readme.txt", "MZ but not a program");
    Manifest manifest;

    const NativeCheck clean = checkNative(directoryContents(folder), manifest, verdict(ScanVerdict::Clean));
    CHECK(clean.native);
    CHECK_FALSE(clean.refused());
    REQUIRE(clean.files.size() == 1);
    CHECK(clean.files[0].path == "bin/helper.dll");
    CHECK(clean.files[0].scan == ScanVerdict::Clean);
    CHECK(clean.scan == ScanVerdict::Clean);
    CHECK(has(clean.capabilities, Capability::Network));
    CHECK(has(clean.capabilities, Capability::Files));
    CHECK(clean.uncovered == std::vector<Capability>{Capability::Network, Capability::Files});
    CHECK(clean.signature.state == SignatureState::Unsigned);

    manifest.permissions = {Permission{.name = "network", .reason = "Talks to its server.", .hosts = {"api.example.com"}}};
    CHECK(checkNative(directoryContents(folder), manifest).uncovered == std::vector<Capability>{Capability::Files});
    CHECK(checkNative(directoryContents(folder), manifest).scan == ScanVerdict::Skipped);

    const NativeCheck unavailable = checkNative(directoryContents(folder), manifest, verdict(ScanVerdict::Unavailable));
    CHECK_FALSE(unavailable.refused());
    CHECK(unavailable.scan == ScanVerdict::Unavailable);

    const NativeCheck threat = checkNative(directoryContents(folder), manifest, verdict(ScanVerdict::Threat));
    CHECK(threat.refusal == NativeRefusal::Threat);
    CHECK(threat.detail == "bin/helper.dll");
    CHECK(checkNative(directoryContents(folder), manifest, verdict(ScanVerdict::Blocked)).refusal ==
          NativeRefusal::Blocked);

    writeFile(folder / "bin/hook.dll", buildPe({.imports = {{"user32.dll", {"SetWindowsHookExW"}}}}));
    const NativeCheck hooked = checkNative(directoryContents(folder), manifest, verdict(ScanVerdict::Clean));
    CHECK(hooked.refusal == NativeRefusal::Injection);
    CHECK(hooked.detail == "bin/hook.dll: user32.dll!SetWindowsHookExW");
    CHECK(checkNative(directoryContents(folder), manifest, verdict(ScanVerdict::Threat)).refusal ==
          NativeRefusal::Threat);
    fs::remove(folder / "bin/hook.dll");

    std::string headless(0x58, '\0');
    headless[0] = 'M';
    headless[1] = 'Z';
    headless[0x3C] = '\x40';
    headless.replace(0x40, 4, std::string("PE\0\0", 4));
    writeFile(folder / "bin/broken.dll", headless);
    const NativeCheck broken = checkNative(directoryContents(folder), manifest, verdict(ScanVerdict::Clean));
    CHECK(broken.refusal == NativeRefusal::Unreadable);
    CHECK(broken.detail.starts_with("bin/broken.dll: "));
    fs::remove(folder / "bin/broken.dll");

    manifest.authorKey = std::string(kAuthorKey);
    writeFile(folder / "SHA512SUMS.minisig", "not a signature");
    const NativeCheck forged = checkNative(directoryContents(folder), manifest, verdict(ScanVerdict::Clean));
    CHECK(forged.signature.state == SignatureState::Invalid);
    CHECK(forged.refusal == NativeRefusal::BadSignature);

    const fs::path plain = scratch / "plain";
    writeFile(plain / "manifest.json", "{}");
    writeFile(plain / "shaders/glow.hlsl", "float4 glow(float2 uv) { return 0; }");
    const NativeCheck none = checkNative(directoryContents(plain), Manifest{}, verdict(ScanVerdict::Threat));
    CHECK_FALSE(none.native);
    CHECK_FALSE(none.refused());
    CHECK_FALSE(hasNativeCode(directoryContents(plain), Manifest{}));
}

TEST_CASE("ADR-0112: a restricted DLL still lists what it imports, asks for nothing it imports, and is refused for "
          "the same reasons")
{
    Scratch scratch;
    const fs::path folder = scratch / "restricted";
    writeFile(folder / "manifest.json", "{}");
    writeFile(folder / "bin/plugin.dll",
              buildPe({.imports = {{"ws2_32.dll", {"connect"}}, {"kernel32.dll", {"CreateFileW", "GetTickCount"}},
                                   {"shell32.dll", {"ShellExecuteW"}}}}));
    Manifest manifest;
    manifest.contributes.native = NativeContribution{.dll = "bin/plugin.dll"};
    const NativeCheck full = checkNative(directoryContents(folder), manifest, verdict(ScanVerdict::Clean));
    CHECK_FALSE(full.restricted);
    CHECK(full.uncovered == std::vector<Capability>{Capability::Network, Capability::Files, Capability::Processes});

    manifest.contributes.native->sandbox = NativeSandbox::Restricted;
    const NativeCheck restricted = checkNative(directoryContents(folder), manifest, verdict(ScanVerdict::Clean));
    CHECK(restricted.restricted);
    CHECK_FALSE(restricted.refused());
    CHECK(has(restricted.capabilities, Capability::Network));
    CHECK(has(restricted.capabilities, Capability::Files));
    CHECK(has(restricted.capabilities, Capability::Processes));
    CHECK(restricted.uncovered.empty());
    CHECK(restricted.signature.state == SignatureState::Unsigned);
    CHECK(checkNative(directoryContents(folder), manifest, verdict(ScanVerdict::Threat)).refusal ==
          NativeRefusal::Threat);

    writeFile(folder / "bin/hook.dll", buildPe({.imports = {{"user32.dll", {"SetWindowsHookExW"}}}}));
    const NativeCheck hooked = checkNative(directoryContents(folder), manifest, verdict(ScanVerdict::Clean));
    CHECK(hooked.restricted);
    CHECK(hooked.refusal == NativeRefusal::Injection);
}

TEST_CASE("a native package whose declared library is missing or not a library is refused")
{
    Scratch scratch;
    const fs::path folder = scratch / "native";
    writeFile(folder / "manifest.json", "{}");
    writeFile(folder / "bin/notes.txt", "hello");
    Manifest manifest;
    manifest.contributes.native = NativeContribution{.dll = "bin/missing.dll"};
    CHECK(hasNativeCode(directoryContents(folder), manifest));
    const NativeCheck missing = checkNative(directoryContents(folder), manifest);
    CHECK(missing.refusal == NativeRefusal::Unreadable);
    CHECK(missing.detail == "bin/missing.dll is missing");

    manifest.contributes.native = NativeContribution{.dll = "bin/notes.txt"};
    CHECK(checkNative(directoryContents(folder), manifest).refusal == NativeRefusal::Unreadable);

    writeFile(folder / "bin/plugin.dll", buildPe({.imports = {{"kernel32.dll", {"GetTickCount"}}}}));
    manifest.contributes.native = NativeContribution{.dll = "bin/plugin.dll"};
    const NativeCheck fine = checkNative(directoryContents(folder), manifest);
    CHECK_FALSE(fine.refused());
    CHECK(fine.capabilities.empty());
}

#ifdef GS_TEST_NATIVE_INJECT
TEST_CASE("the test library that writes into other programs is classified as injection and refused")
{
    const std::string image = readFile(fs::path(GS_TEST_NATIVE_INJECT));
    REQUIRE(looksLikePe(image));
    const PeImports imports = readPeImports(image);
    INFO(imports.problem);
    REQUIRE(imports.valid);
    CHECK(has(classifyImports(imports.functions), Capability::Injection));
    Scratch scratch;
    writeFile(scratch / "package/manifest.json", "{}");
    writeFile(scratch / "package/bin/inject.dll", image);
    const NativeCheck check = checkNative(directoryContents(scratch / "package"), Manifest{}, verdict(ScanVerdict::Clean));
    CHECK(check.native);
    CHECK(check.refusal == NativeRefusal::Injection);
    CHECK(check.detail.starts_with("bin/inject.dll: "));
}
#endif

TEST_CASE("a package signature over SHA512SUMS is unsigned, valid or invalid, in a folder and in a package")
{
    Scratch scratch;
    const fs::path folder = scratch / "signed";
    writeFile(folder / "manifest.json", kSignedManifest);
    writeFile(folder / "bin/notes.txt", kNotes);
    CHECK(checkSignature(directoryContents(folder), kAuthorKey).state == SignatureState::Unsigned);
    writeFile(folder / "SHA512SUMS", kSums);
    CHECK(checkSignature(directoryContents(folder), kAuthorKey).state == SignatureState::Unsigned);
    writeFile(folder / "SHA512SUMS.minisig", kSumsSignature);

    const SignatureCheck valid = checkSignature(directoryContents(folder), kAuthorKey);
    INFO(valid.problem);
    CHECK(valid.state == SignatureState::Valid);
    REQUIRE(valid.key.has_value());
    CHECK(minisignKeyId(*valid.key) == "C1D2E3F4A5B60718");
    const fs::path archive = scratch / "signed.gsext";
    REQUIRE(packDirectory(folder, archive).empty());
    CHECK(checkSignature(archiveContents(archive), kAuthorKey).state == SignatureState::Valid);

    const SignatureCheck other = checkSignature(directoryContents(folder), kOtherKey);
    CHECK(other.state == SignatureState::Invalid);
    CHECK(other.problem.find("another key") != std::string::npos);
    const SignatureCheck keyless = checkSignature(directoryContents(folder), "");
    CHECK(keyless.state == SignatureState::Invalid);
    CHECK_FALSE(keyless.key.has_value());

    writeFile(folder / "bin/notes.txt", "native code went here\n");
    const SignatureCheck changed = checkSignature(directoryContents(folder), kAuthorKey);
    CHECK(changed.state == SignatureState::Invalid);
    CHECK(changed.problem == "bin/notes.txt was changed after signing");
    writeFile(folder / "bin/notes.txt", kNotes);

    writeFile(folder / "bin/extra.txt", "slipped in");
    const SignatureCheck extra = checkSignature(directoryContents(folder), kAuthorKey);
    CHECK(extra.state == SignatureState::Invalid);
    CHECK(extra.problem == "bin/extra.txt is not listed in SHA512SUMS");
    fs::remove(folder / "bin/extra.txt");

    fs::remove(folder / "bin/notes.txt");
    const SignatureCheck lost = checkSignature(directoryContents(folder), kAuthorKey);
    CHECK(lost.state == SignatureState::Invalid);
    CHECK(lost.problem == "bin/notes.txt is listed in SHA512SUMS but missing");

    const fs::path edited = scratch / "edited";
    writeSigned(edited);
    writeFile(edited / "SHA512SUMS", std::string(kSums) + "\n");
    const SignatureCheck resummed = checkSignature(directoryContents(edited), kAuthorKey);
    CHECK(resummed.state == SignatureState::Invalid);
    CHECK(resummed.problem == "the signature does not match SHA512SUMS");
    fs::remove(edited / "SHA512SUMS");
    CHECK(checkSignature(directoryContents(edited), kAuthorKey).state == SignatureState::Invalid);
}

TEST_CASE("a new author key or a lost signature on an update counts as a key change")
{
    const SignatureCheck signedByA{.state = SignatureState::Valid, .key = parseMinisignKey(kAuthorKey)};
    const SignatureCheck signedByB{.state = SignatureState::Valid, .key = parseMinisignKey(kOtherKey)};
    const SignatureCheck invalidA{.state = SignatureState::Invalid, .key = parseMinisignKey(kAuthorKey)};
    const SignatureCheck unsignedOne{};
    REQUIRE(signedByA.key.has_value());
    REQUIRE(signedByB.key.has_value());
    CHECK_FALSE(authorChanged(signedByA, signedByA));
    CHECK(authorChanged(signedByA, signedByB));
    CHECK(authorChanged(signedByA, unsignedOne));
    CHECK(authorChanged(signedByA, invalidA));
    CHECK_FALSE(authorChanged(unsignedOne, signedByB));
    CHECK_FALSE(authorChanged(unsignedOne, unsignedOne));
    CHECK_FALSE(authorChanged(invalidA, signedByB));

    Scratch scratch;
    writeSigned(scratch / "first");
    const SignatureCheck first = checkSignature(directoryContents(scratch / "first"), kAuthorKey);
    REQUIRE(first.state == SignatureState::Valid);
    CHECK_FALSE(authorChanged(first, checkSignature(directoryContents(scratch / "first"), kAuthorKey)));
    CHECK(authorChanged(first, checkSignature(directoryContents(scratch / "first"), kOtherKey)));
}

TEST_CASE("ADR-0112: the export table of a library lists its functions, and its machine field names its processor")
{
    const PeExports none = readPeExports(buildPe({}));
    INFO(none.problem);
    CHECK(none.valid);
    CHECK(none.names.empty());
    CHECK(none.machine == kMachineX64);

    const PeExports plugin = readPeExports(buildPe({.exports = {"gs_ext_main", "helper"}}));
    INFO(plugin.problem);
    REQUIRE(plugin.valid);
    CHECK(plugin.names == std::vector<std::string>{"gs_ext_main", "helper"});

    const PeExports arm = readPeExports(buildPe({.machine = kMachineArm64, .exports = {"gs_ext_main"}}));
    INFO(arm.problem);
    REQUIRE(arm.valid);
    CHECK(arm.machine == kMachineArm64);
    CHECK(arm.names == std::vector<std::string>{"gs_ext_main"});
    CHECK(readPeImports(buildPe({.machine = kMachineArm64})).machine == kMachineArm64);
    CHECK(readPeExports(buildPe({.wide = false, .exports = {"gs_ext_main"}})).machine == 0x014C);

    CHECK(machineName(kMachineX64) == "x64");
    CHECK(machineName(kMachineArm64) == "arm64");
    CHECK(machineName(0x014C) == "x86");
    CHECK(machineName(0x1234) == "machine 0x1234");
    CHECK(architectureMachine("x64") == kMachineX64);
    CHECK(architectureMachine("arm64") == kMachineArm64);
    CHECK(architectureMachine("x86") == 0);
    CHECK(architectureMachine("") == 0);

    const PeExports system = readPeExports(readFile(systemLibrary(L"kernel32.dll")));
    INFO(system.problem);
    REQUIRE(system.valid);
    CHECK(system.names.size() > 100);
    CHECK(std::ranges::find(system.names, std::string("GetTickCount")) != system.names.end());

    std::string cut = buildPe({.exports = {"gs_ext_main"}});
    cut.resize(kPeSectionFile + 32);
    CHECK_FALSE(readPeExports(cut).valid);
    CHECK_FALSE(readPeExports("MZ").valid);
}

TEST_CASE("ADR-0112: each DLL a package lists is checked for its processor and for gs_ext_main")
{
    Scratch scratch;
    const fs::path folder = scratch / "dlls";
    writeFile(folder / "manifest.json", "{}");
    writeFile(folder / "bin/x64/plugin.dll", buildPe({.exports = {"gs_ext_main"}}));
    writeFile(folder / "bin/arm64/plugin.dll", buildPe({.machine = kMachineArm64, .exports = {"gs_ext_main"}}));
    writeFile(folder / "bin/old.dll", buildPe({.exports = {"gs_ext_entry"}}));
    writeFile(folder / "bin/x86.dll", buildPe({.wide = false, .exports = {"gs_ext_main"}}));
    writeFile(folder / "bin/notes.dll", "not a library, only text that happens to end in .dll and is long enough");
    Manifest manifest;
    manifest.contributes.native =
        NativeContribution{.dlls = {{"x64", "bin/x64/plugin.dll"}, {"arm64", "bin/arm64/plugin.dll"}}};

    const LibraryCheck both = checkNativeLibraries(directoryContents(folder), manifest);
    INFO(listed(both.diagnostics));
    CHECK(both.diagnostics.empty());
    REQUIRE(both.libraries.size() == 2);
    CHECK(both.libraries[0] == NativeLibrary{.architecture = "arm64",
                                             .path = "bin/arm64/plugin.dll",
                                             .machine = kMachineArm64,
                                             .entry = true});
    CHECK(both.libraries[1] ==
          NativeLibrary{.architecture = "x64", .path = "bin/x64/plugin.dll", .machine = kMachineX64, .entry = true});

    manifest.contributes.native->dlls = {{"x64", "bin/x64/plugin.dll"}, {"arm64", "bin/x64/plugin.dll"}};
    const LibraryCheck swapped = checkNativeLibraries(directoryContents(folder), manifest);
    CHECK(reports(swapped.diagnostics, Code::NativeMachineMismatch, R"($.contributes.native.dll["arm64"])"));
    CHECK_FALSE(reports(swapped.diagnostics, Code::NativeMachineMismatch, R"($.contributes.native.dll["x64"])"));

    manifest.contributes.native->dlls = {{"riscv64", "bin/x64/plugin.dll"}};
    CHECK(checkNativeLibraries(directoryContents(folder), manifest).diagnostics.empty());

    manifest.contributes.native->dlls.clear();
    manifest.contributes.native->dll = "bin/old.dll";
    const LibraryCheck old = checkNativeLibraries(directoryContents(folder), manifest);
    REQUIRE(old.diagnostics.size() == 1);
    CHECK(old.diagnostics[0].code == Code::NativeEntryMissing);
    CHECK(old.diagnostics[0].path == "$.contributes.native.dll");
    CHECK(old.diagnostics[0].detail.find("gs_ext_entry") != std::string::npos);
    REQUIRE(old.libraries.size() == 1);
    CHECK(old.libraries[0].oldEntry);
    CHECK_FALSE(old.libraries[0].entry);

    manifest.contributes.native->dll = "bin/x86.dll";
    CHECK(reports(checkNativeLibraries(directoryContents(folder), manifest).diagnostics, Code::NativeMachineMismatch,
                  "$.contributes.native.dll"));
    manifest.contributes.native->dll = "bin/arm64/plugin.dll";
    CHECK(checkNativeLibraries(directoryContents(folder), manifest).diagnostics.empty());
    manifest.contributes.native->dll = "bin/notes.dll";
    CHECK(reports(checkNativeLibraries(directoryContents(folder), manifest).diagnostics, Code::NativeDllInvalid,
                  "$.contributes.native.dll"));
    manifest.contributes.native->dll = "bin/missing.dll";
    CHECK(reports(checkNativeLibraries(directoryContents(folder), manifest).diagnostics, Code::FileMissing,
                  "$.contributes.native.dll"));
    CHECK(checkNativeLibraries(directoryContents(folder), Manifest{}).libraries.empty());
}

TEST_CASE("ADR-0112: installing takes the DLL for this processor and refuses a package that has none")
{
    Scratch scratch;
    const fs::path folder = scratch / "both";
    writeFile(folder / "manifest.json", "{}");
    writeFile(folder / "bin/x64/plugin.dll", buildPe({.exports = {"gs_ext_main"}}));
    writeFile(folder / "bin/arm64/plugin.dll", buildPe({.machine = kMachineArm64, .exports = {"gs_ext_main"}}));
    Manifest manifest;
    manifest.contributes.native =
        NativeContribution{.dlls = {{"x64", "bin/x64/plugin.dll"}, {"arm64", "bin/arm64/plugin.dll"}}};
    CHECK_FALSE(checkNative(directoryContents(folder), manifest, {}, "x64").refused());
    CHECK_FALSE(checkNative(directoryContents(folder), manifest, {}, "arm64").refused());
    CHECK(checkNative(directoryContents(folder), manifest).files.size() == 2);

    manifest.contributes.native->dlls = {{"arm64", "bin/arm64/plugin.dll"}};
    const NativeCheck none = checkNative(directoryContents(folder), manifest, {}, "x64");
    CHECK(none.refusal == NativeRefusal::Architecture);
    CHECK(none.detail == "no DLL for x64");
    CHECK_FALSE(checkNative(directoryContents(folder), manifest, {}, "arm64").refused());

    manifest.contributes.native->dlls = {{"x64", "bin/arm64/plugin.dll"}};
    const NativeCheck wrong = checkNative(directoryContents(folder), manifest, {}, "x64");
    CHECK(wrong.refusal == NativeRefusal::Architecture);
    CHECK(wrong.detail == "bin/arm64/plugin.dll is built for arm64, not x64");

    manifest.contributes.native->dlls.clear();
    manifest.contributes.native->dll = "bin/arm64/plugin.dll";
    CHECK(checkNative(directoryContents(folder), manifest, {}, "x64").refusal == NativeRefusal::Architecture);
    CHECK_FALSE(checkNative(directoryContents(folder), manifest, {}, "arm64").refused());

    writeFile(folder / "bin/hook.dll", buildPe({.imports = {{"user32.dll", {"SetWindowsHookExW"}}}}));
    CHECK(checkNative(directoryContents(folder), manifest, {}, "x64").refusal == NativeRefusal::Injection);
}

TEST_CASE("ADR-0112: the quick signature check names the signing key without hashing every file")
{
    Scratch scratch;
    writeSigned(scratch / "signed");
    CHECK(checkSignedSums(directoryContents(scratch / "signed"), kAuthorKey).state == SignatureState::Valid);
    writeFile(scratch / "signed/bin/notes.txt", "changed after signing\n");
    CHECK(checkSignature(directoryContents(scratch / "signed"), kAuthorKey).state == SignatureState::Invalid);
    const SignatureCheck quick = checkSignedSums(directoryContents(scratch / "signed"), kAuthorKey);
    CHECK(quick.state == SignatureState::Valid);
    REQUIRE(quick.key.has_value());
    CHECK(minisignKeyId(*quick.key) == "C1D2E3F4A5B60718");
    CHECK(checkSignedSums(directoryContents(scratch / "signed"), kOtherKey).state == SignatureState::Invalid);
    fs::remove(scratch / "signed/SHA512SUMS.minisig");
    CHECK(checkSignedSums(directoryContents(scratch / "signed"), kAuthorKey).state == SignatureState::Unsigned);
}

TEST_CASE("components and tasks are contributed by name and folder under their own extension points")
{
    const std::string json = R"({"schemaVersion": 1, "id": "alice.kit", "version": "1.0.0", "name": "Kit",
        "license": "MIT", "authorKey": "RWQYB7al9OPSwU7aritYZ3nmUlPQk5fh7N4Izs3H4UUtma8A/RO4rFBZ",
        "requires": {"component": "^1.0", "task": "^1.0"},
        "contributes": {"components": [{"name": "burst", "folder": "components/burst"}],
                        "tasks": [{"name": "daily", "folder": "tasks/daily"}]},
        "permissions": [{"name": "input.read", "reason": "Reads the keys you press."}]})";
    const LoadedManifest loaded = loadManifest(json);
    INFO(listed(loaded.diagnostics));
    REQUIRE(loaded.usable);
    CHECK(loaded.manifest.contributes.components ==
          std::vector<ComponentContribution>{{.name = "burst", .folder = "components/burst"}});
    CHECK(loaded.manifest.contributes.tasks == std::vector<TaskContribution>{{.name = "daily", .folder = "tasks/daily"}});
    CHECK(loaded.manifest.authorKey == kAuthorKey);
    CHECK(loaded.manifest.declares(ExtensionPoint::Component));
    CHECK(loaded.manifest.declares(ExtensionPoint::Task));
    CHECK(extensionPointNamed("component") == ExtensionPoint::Component);
    CHECK(extensionPointNamed("task") == ExtensionPoint::Task);
    const LoadedManifest again = loadManifest(saveManifest(loaded.manifest));
    REQUIRE(again.usable);
    CHECK(again.manifest == loaded.manifest);

    const LoadedManifest undeclared = loadManifest(R"({"schemaVersion": 1, "id": "alice.kit", "version": "1.0.0",
        "name": "Kit", "license": "MIT", "requires": {"filter": "^1.0"},
        "contributes": {"components": [{"name": "burst", "folder": "components/burst"}],
                        "tasks": [{"name": "daily", "folder": "tasks/daily"}]},
        "permissions": [{"name": "input.read", "reason": "Reads the keys you press."}]})");
    CHECK_FALSE(undeclared.usable);
    CHECK(reports(undeclared.diagnostics, Code::ContributionUndeclared, "$.contributes.components"));
    CHECK(reports(undeclared.diagnostics, Code::ContributionUndeclared, "$.contributes.tasks"));
    CHECK(reports(undeclared.diagnostics, Code::PermissionNotAllowed, "$.permissions"));

    const LoadedManifest wrong = loadManifest(R"({"schemaVersion": 1, "id": "alice.kit", "version": "1.0.0",
        "name": "Kit", "license": "MIT", "authorKey": "RWQ", "requires": {"component": "^1.0"},
        "contributes": {"components": [{"name": "my burst", "folder": "../burst"},
                                       {"name": "same", "folder": "a"}, {"name": "same", "folder": "b"}]}})");
    CHECK_FALSE(wrong.usable);
    CHECK(reports(wrong.diagnostics, Code::AuthorKeyInvalid, "$.authorKey"));
    CHECK(reports(wrong.diagnostics, Code::ProfileNameInvalid, "$.contributes.components[0].name"));
    CHECK(reports(wrong.diagnostics, Code::PathInvalid, "$.contributes.components[0].folder"));
    CHECK(reports(wrong.diagnostics, Code::Duplicate, "$.contributes.components[2].name"));
    CHECK(codeName(Code::AuthorKeyInvalid) == "author-key-invalid");
    CHECK(codeName(Code::NativeRefused) == "native-refused");
    CHECK_FALSE(fixHint(Code::NativeRefused).empty());

    const nlohmann::json schema = nlohmann::json::parse(manifestJsonSchema());
    CHECK(schema["properties"]["authorKey"]["pattern"] == "^RW[A-Za-z0-9+/]{54}$");
    CHECK(schema["properties"]["contributes"]["properties"].contains("components"));
    CHECK(schema["properties"]["contributes"]["properties"].contains("tasks"));
}
