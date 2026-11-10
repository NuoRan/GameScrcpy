#include "gs/ext/NativeCheck.h"

#include <gs/platform/AtomicFile.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::ext {

namespace {

constexpr std::uint16_t kPe32 = 0x10B;
constexpr std::uint16_t kPe32Plus = 0x20B;
constexpr std::uint64_t kMostSections = 96;
constexpr std::uint64_t kMostModules = 4096;
constexpr std::uint64_t kMostThunks = 65536;
constexpr std::size_t kMostFunctions = 200000;
constexpr std::size_t kMostNameBytes = 4096;
constexpr std::uint32_t kImportDirectory = 1;
constexpr std::uint32_t kDelayImportDirectory = 13;
constexpr std::uint64_t kMostDirectories = 16;
constexpr std::size_t kMostChecksumsBytes = std::size_t{4} << 20;

constexpr auto kNetworkModules =
    std::to_array<std::string_view>({"ws2_32.dll", "wsock32.dll", "mswsock.dll", "winhttp.dll", "wininet.dll",
                                     "urlmon.dll", "httpapi.dll", "websocket.dll", "dnsapi.dll"});
constexpr auto kByteOrderFunctions = std::to_array<std::string_view>({"htonl", "htons", "ntohl", "ntohs"});
constexpr auto kNetworkFunctions =
    std::to_array<std::string_view>({"IcmpSendEcho", "IcmpSendEcho2", "IcmpSendEcho2Ex", "Icmp6SendEcho2"});
constexpr auto kFileFunctions = std::to_array<std::string_view>(
    {"CreateFileA",         "CreateFileW",         "CreateFile2",           "CreateFileTransactedA",
     "CreateFileTransactedW", "OpenFile",          "_lopen",                "_lcreat",
     "DeleteFileA",         "DeleteFileW",         "DeleteFileTransactedA", "DeleteFileTransactedW",
     "MoveFileA",           "MoveFileW",           "MoveFileExA",           "MoveFileExW",
     "MoveFileWithProgressA", "MoveFileWithProgressW", "CopyFileA",         "CopyFileW",
     "CopyFileExA",         "CopyFileExW",         "CopyFile2",             "ReplaceFileA",
     "ReplaceFileW",        "CreateDirectoryA",    "CreateDirectoryW",      "CreateDirectoryExA",
     "CreateDirectoryExW",  "RemoveDirectoryA",    "RemoveDirectoryW",      "FindFirstFileA",
     "FindFirstFileW",      "FindFirstFileExA",    "FindFirstFileExW",      "SetFileAttributesA",
     "SetFileAttributesW",  "CreateHardLinkA",     "CreateHardLinkW",       "CreateSymbolicLinkA",
     "CreateSymbolicLinkW", "GetOpenFileNameA",    "GetOpenFileNameW",      "GetSaveFileNameA",
     "GetSaveFileNameW",    "SHFileOperationA",    "SHFileOperationW",      "NtCreateFile",
     "ZwCreateFile",        "NtOpenFile",          "ZwOpenFile",            "NtDeleteFile",
     "ZwDeleteFile",        "fopen",               "fopen_s",               "_wfopen",
     "_wfopen_s",           "_fsopen",             "_wfsopen",              "freopen",
     "freopen_s",           "_wfreopen",           "_wfreopen_s",           "_open",
     "_wopen",              "_sopen",              "_wsopen",               "_sopen_s",
     "_wsopen_s",           "_creat",              "_wcreat",               "remove",
     "_wremove",            "_unlink",             "_wunlink",              "rename",
     "_wrename",            "_mkdir",              "_wmkdir",               "_rmdir",
     "_wrmdir",             "_findfirst32",        "_findfirst64",          "_findfirst32i64",
     "_findfirst64i32",     "_wfindfirst32",       "_wfindfirst64",         "_wfindfirst32i64",
     "_wfindfirst64i32"});
constexpr auto kInputFunctions = std::to_array<std::string_view>(
    {"GetAsyncKeyState", "RegisterRawInputDevices", "GetRawInputData", "GetRawInputBuffer", "RegisterHotKey",
     "DirectInput8Create", "DirectInputCreateA", "DirectInputCreateW", "DirectInputCreateEx"});
constexpr auto kProcessFunctions = std::to_array<std::string_view>(
    {"CreateProcessA",         "CreateProcessW",         "CreateProcessAsUserA", "CreateProcessAsUserW",
     "CreateProcessWithLogonW", "CreateProcessWithTokenW", "CreateProcessInternalA", "CreateProcessInternalW",
     "ShellExecuteA",          "ShellExecuteW",          "ShellExecuteExA",      "ShellExecuteExW",
     "WinExec",                "NtCreateUserProcess",    "ZwCreateUserProcess",  "RtlCreateUserProcess",
     "NtCreateProcess",        "NtCreateProcessEx",      "ZwCreateProcess",      "ZwCreateProcessEx",
     "system",                 "_wsystem",               "_popen",               "_wpopen"});
constexpr auto kProcessPrefixes = std::to_array<std::string_view>({"_exec", "_wexec", "_spawn", "_wspawn"});
constexpr auto kInjectionFunctions = std::to_array<std::string_view>(
    {"WriteProcessMemory",   "VirtualAllocEx",        "VirtualAllocExNuma",   "VirtualProtectEx",
     "CreateRemoteThread",   "CreateRemoteThreadEx",  "NtCreateThreadEx",     "ZwCreateThreadEx",
     "NtCreateThread",       "ZwCreateThread",        "RtlCreateUserThread",  "NtWriteVirtualMemory",
     "ZwWriteVirtualMemory", "NtMapViewOfSection",    "ZwMapViewOfSection",   "NtUnmapViewOfSection",
     "ZwUnmapViewOfSection", "QueueUserAPC",          "QueueUserAPC2",        "NtQueueApcThread",
     "NtQueueApcThreadEx",   "ZwQueueApcThread",      "ZwQueueApcThreadEx",   "RtlQueueApcWow64Thread",
     "SetThreadContext",     "Wow64SetThreadContext", "NtSetContextThread",   "ZwSetContextThread",
     "SetWindowsHookExA",    "SetWindowsHookExW",     "SetWindowsHookA",      "SetWindowsHookW"});
constexpr auto kSystemModules =
    std::to_array<std::string_view>({"kernel32.dll", "kernelbase.dll", "ntdll.dll", "user32.dll"});
constexpr auto kNativeSuffixes = std::to_array<std::string_view>(
    {".dll", ".exe", ".sys", ".ocx", ".cpl", ".scr", ".drv", ".efi", ".node", ".pyd", ".ax"});

std::string lowered(std::string_view text)
{
    std::string out(text);
    std::ranges::transform(out, out.begin(),
                           [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    return out;
}

template <std::size_t N>
bool listed(const std::array<std::string_view, N>& names, std::string_view name)
{
    return std::ranges::find(names, name) != names.end();
}

bool systemModule(std::string_view module)
{
    return listed(kSystemModules, module) || module.starts_with("api-ms-win-") || module.starts_with("ext-ms-");
}

bool runtimeModule(std::string_view module)
{
    return module == "ucrtbase.dll" || module == "ucrtbased.dll" || module == "msvcrt.dll" ||
           module.starts_with("api-ms-win-crt-") || (module.starts_with("msvcr") && module.ends_with(".dll"));
}

bool hexDigit(char c) noexcept
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

std::string hexOf(const Digest512& digest)
{
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string text;
    text.reserve(2 * digest.size());
    for (const std::uint8_t byte : digest) {
        text.push_back(kDigits[byte >> 4]);
        text.push_back(kDigits[byte & 0x0f]);
    }
    return text;
}

std::filesystem::path pathOf(std::string_view text)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

int severity(NativeRefusal refusal) noexcept
{
    switch (refusal) {
    case NativeRefusal::None:
        return 0;
    case NativeRefusal::Unreadable:
        return 1;
    case NativeRefusal::BadSignature:
        return 2;
    case NativeRefusal::Injection:
        return 3;
    case NativeRefusal::Blocked:
        return 4;
    case NativeRefusal::Threat:
        return 5;
    }
    return 0;
}

void merge(std::vector<CapabilityUse>& into, CapabilityUse use)
{
    const auto found = std::ranges::find(into, use.capability, &CapabilityUse::capability);
    if (found == into.end()) {
        const auto at = std::ranges::upper_bound(into, use.capability, {}, &CapabilityUse::capability);
        into.insert(at, std::move(use));
        return;
    }
    for (std::string& function : use.functions) {
        if (std::ranges::find(found->functions, function) == found->functions.end()) {
            found->functions.push_back(std::move(function));
        }
    }
}

struct Section {
    std::uint64_t address = 0;
    std::uint64_t rawSize = 0;
    std::uint64_t rawOffset = 0;
};

class PeReader
{
public:
    explicit PeReader(std::string_view bytes) : m_bytes(bytes) {}

    std::string read(PeImports& out)
    {
        if (!looksLikePe(m_bytes)) {
            return "it is not a Windows program or library";
        }
        const std::uint64_t coff = *number(0x3C, 4) + 4;
        const std::optional<std::uint64_t> machine = number(coff, 2);
        const std::optional<std::uint64_t> sections = number(coff + 2, 2);
        const std::optional<std::uint64_t> optionalSize = number(coff + 16, 2);
        if (!machine || !sections || !optionalSize) {
            return "its file header is cut short";
        }
        const std::uint64_t optional = coff + 20;
        const std::optional<std::uint64_t> magic = number(optional, 2);
        if (!magic || (*magic != kPe32 && *magic != kPe32Plus)) {
            return "its optional header is neither PE32 nor PE32+";
        }
        m_wide = *magic == kPe32Plus;
        const std::optional<std::uint64_t> base = m_wide ? number(optional + 24, 8) : number(optional + 28, 4);
        const std::optional<std::uint64_t> headers = number(optional + 60, 4);
        const std::optional<std::uint64_t> count = number(optional + (m_wide ? 108 : 92), 4);
        if (!base || !headers || !count) {
            return "its optional header is cut short";
        }
        if (*sections > kMostSections) {
            return "it has too many sections";
        }
        m_base = *base;
        m_headers = std::min<std::uint64_t>(*headers, m_bytes.size());
        const std::uint64_t table = optional + *optionalSize;
        for (std::uint64_t index = 0; index < *sections; ++index) {
            const std::uint64_t at = table + 40 * index;
            const std::optional<std::uint64_t> address = number(at + 12, 4);
            const std::optional<std::uint64_t> rawSize = number(at + 16, 4);
            const std::optional<std::uint64_t> rawOffset = number(at + 20, 4);
            if (!address || !rawSize || !rawOffset) {
                return "a section header is cut short";
            }
            m_sections.push_back({.address = *address, .rawSize = *rawSize, .rawOffset = *rawOffset});
        }
        out.wide = m_wide;
        out.machine = static_cast<std::uint16_t>(*machine);
        const std::uint64_t directories = optional + (m_wide ? 112 : 96);
        const std::uint64_t present = std::min<std::uint64_t>(*count, kMostDirectories);
        const std::uint64_t end = optional + *optionalSize;
        const auto directory = [&](std::uint32_t index) -> std::optional<std::uint64_t> {
            if (index >= present || directories + 8 * (std::uint64_t{index} + 1) > end) {
                return std::uint64_t{0};
            }
            return number(directories + 8 * std::uint64_t{index}, 4);
        };
        const std::optional<std::uint64_t> imports = directory(kImportDirectory);
        const std::optional<std::uint64_t> delayed = directory(kDelayImportDirectory);
        if (!imports || !delayed) {
            return "its data directories are cut short";
        }
        if (*imports != 0) {
            if (std::string problem = readImports(*imports, out); !problem.empty()) {
                return problem;
            }
        }
        if (*delayed != 0) {
            if (std::string problem = readDelayed(*delayed, out); !problem.empty()) {
                return problem;
            }
        }
        return {};
    }

private:
    [[nodiscard]] std::optional<std::uint64_t> number(std::uint64_t offset, std::size_t width) const
    {
        if (offset > m_bytes.size() || m_bytes.size() - offset < width) {
            return std::nullopt;
        }
        std::uint64_t value = 0;
        for (std::size_t k = width; k-- > 0;) {
            value = (value << 8) | static_cast<std::uint8_t>(m_bytes[static_cast<std::size_t>(offset) + k]);
        }
        return value;
    }

    [[nodiscard]] std::optional<std::uint64_t> offsetOf(std::uint64_t rva) const
    {
        if (rva < m_headers) {
            return rva;
        }
        for (const Section& section : m_sections) {
            if (rva >= section.address && rva - section.address < section.rawSize) {
                return section.rawOffset + (rva - section.address);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint64_t> relative(std::uint64_t value, bool virtualAddresses) const
    {
        if (!virtualAddresses) {
            return value;
        }
        if (value < m_base) {
            return std::nullopt;
        }
        return value - m_base;
    }

    [[nodiscard]] std::optional<std::string> textAt(std::uint64_t rva) const
    {
        const std::optional<std::uint64_t> at = offsetOf(rva);
        if (!at || *at >= m_bytes.size()) {
            return std::nullopt;
        }
        const std::string_view rest = m_bytes.substr(static_cast<std::size_t>(*at));
        const std::size_t end = rest.find('\0');
        if (end == std::string_view::npos || end > kMostNameBytes) {
            return std::nullopt;
        }
        return std::string(rest.substr(0, end));
    }

    std::string readImports(std::uint64_t rva, PeImports& out) const
    {
        for (std::uint64_t index = 0;; ++index) {
            if (index == kMostModules) {
                return "it imports from too many libraries";
            }
            const std::optional<std::uint64_t> at = offsetOf(rva + 20 * index);
            if (!at) {
                return "an import descriptor lies outside the file";
            }
            const std::optional<std::uint64_t> lookup = number(*at, 4);
            const std::optional<std::uint64_t> name = number(*at + 12, 4);
            const std::optional<std::uint64_t> address = number(*at + 16, 4);
            if (!lookup || !name || !address) {
                return "an import descriptor is cut short";
            }
            if (*name == 0) {
                return {};
            }
            const std::optional<std::string> module = textAt(*name);
            if (!module) {
                return "the name of an imported library lies outside the file";
            }
            const std::uint64_t thunks = *lookup != 0 ? *lookup : *address;
            if (thunks == 0) {
                continue;
            }
            if (std::string problem = readThunks(thunks, lowered(*module), false, false, out); !problem.empty()) {
                return problem;
            }
        }
    }

    std::string readDelayed(std::uint64_t rva, PeImports& out) const
    {
        for (std::uint64_t index = 0;; ++index) {
            if (index == kMostModules) {
                return "it delay-loads too many libraries";
            }
            const std::optional<std::uint64_t> at = offsetOf(rva + 32 * index);
            if (!at) {
                return "a delay-load descriptor lies outside the file";
            }
            const std::optional<std::uint64_t> attributes = number(*at, 4);
            const std::optional<std::uint64_t> name = number(*at + 4, 4);
            const std::optional<std::uint64_t> address = number(*at + 12, 4);
            const std::optional<std::uint64_t> names = number(*at + 16, 4);
            if (!attributes || !name || !address || !names) {
                return "a delay-load descriptor is cut short";
            }
            if (*name == 0) {
                return {};
            }
            const bool virtualAddresses = (*attributes & 1) == 0;
            const std::uint64_t table = *names != 0 ? *names : *address;
            if (table == 0) {
                continue;
            }
            const std::optional<std::uint64_t> nameRva = relative(*name, virtualAddresses);
            const std::optional<std::uint64_t> tableRva = relative(table, virtualAddresses);
            if (!nameRva || !tableRva) {
                return "a delay-load descriptor points outside the image";
            }
            const std::optional<std::string> module = textAt(*nameRva);
            if (!module) {
                return "the name of a delay-loaded library lies outside the file";
            }
            if (std::string problem = readThunks(*tableRva, lowered(*module), true, virtualAddresses, out);
                !problem.empty()) {
                return problem;
            }
        }
    }

    std::string readThunks(std::uint64_t rva, const std::string& module, bool delayed, bool virtualAddresses,
                           PeImports& out) const
    {
        const std::size_t width = m_wide ? 8 : 4;
        const std::uint64_t ordinalFlag = std::uint64_t{1} << (m_wide ? 63 : 31);
        for (std::uint64_t index = 0;; ++index) {
            if (index == kMostThunks || out.functions.size() >= kMostFunctions) {
                return "it imports too many functions";
            }
            const std::optional<std::uint64_t> at = offsetOf(rva + width * index);
            if (!at) {
                return "an import table lies outside the file";
            }
            const std::optional<std::uint64_t> thunk = number(*at, width);
            if (!thunk) {
                return "an import table is cut short";
            }
            if (*thunk == 0) {
                return {};
            }
            ImportedFunction function{.module = module, .delayed = delayed};
            if ((*thunk & ordinalFlag) != 0) {
                function.byOrdinal = true;
                function.ordinal = static_cast<std::uint16_t>(*thunk & 0xffff);
            } else {
                const std::optional<std::uint64_t> entry =
                    relative(virtualAddresses ? *thunk : (*thunk & 0x7fffffff), virtualAddresses);
                const std::optional<std::string> name = entry ? textAt(*entry + 2) : std::nullopt;
                if (!name || name->empty()) {
                    return "the name of an imported function lies outside the file";
                }
                function.name = *name;
            }
            out.functions.push_back(std::move(function));
        }
    }

    std::string_view m_bytes;
    std::vector<Section> m_sections;
    std::uint64_t m_base = 0;
    std::uint64_t m_headers = 0;
    bool m_wide = false;
};

}

bool looksLikePe(std::string_view bytes) noexcept
{
    if (bytes.size() < 64 || bytes[0] != 'M' || bytes[1] != 'Z') {
        return false;
    }
    std::uint64_t offset = 0;
    for (std::size_t k = 4; k-- > 0;) {
        offset = (offset << 8) | static_cast<std::uint8_t>(bytes[0x3C + k]);
    }
    return offset <= bytes.size() - 24 && bytes.substr(static_cast<std::size_t>(offset), 4) == std::string_view("PE\0\0", 4);
}

PeImports readPeImports(std::string_view image)
{
    PeImports imports;
    PeReader reader(image);
    std::string problem = reader.read(imports);
    imports.valid = problem.empty();
    if (!imports.valid) {
        imports.functions.clear();
        imports.problem = std::move(problem);
    }
    return imports;
}

std::string_view capabilityName(Capability capability) noexcept
{
    return kCapabilityNames[static_cast<std::size_t>(capability)];
}

std::vector<CapabilityUse> classifyImports(const std::vector<ImportedFunction>& functions)
{
    std::vector<CapabilityUse> uses;
    const auto note = [&uses](Capability capability, const std::string& label) {
        merge(uses, CapabilityUse{.capability = capability, .functions = {label}});
    };
    for (const ImportedFunction& function : functions) {
        const std::string label =
            function.module + "!" + (function.byOrdinal ? "#" + std::to_string(function.ordinal) : function.name);
        if (function.byOrdinal) {
            if (systemModule(function.module)) {
                note(Capability::Injection, label);
            }
            if (listed(kNetworkModules, function.module)) {
                note(Capability::Network, label);
            }
            continue;
        }
        const std::string_view name = function.name;
        const bool runtimeProcess =
            runtimeModule(function.module) &&
            std::ranges::any_of(kProcessPrefixes, [name](std::string_view prefix) { return name.starts_with(prefix); });
        if (listed(kInjectionFunctions, name)) {
            note(Capability::Injection, label);
        }
        if (listed(kProcessFunctions, name) || runtimeProcess) {
            note(Capability::Processes, label);
        }
        if (listed(kInputFunctions, name)) {
            note(Capability::Input, label);
        }
        if (listed(kFileFunctions, name)) {
            note(Capability::Files, label);
        }
        if ((listed(kNetworkModules, function.module) && !listed(kByteOrderFunctions, name)) ||
            listed(kNetworkFunctions, name)) {
            note(Capability::Network, label);
        }
    }
    return uses;
}

std::vector<Capability> uncoveredCapabilities(const std::vector<CapabilityUse>& uses,
                                              const std::vector<Permission>& permissions)
{
    const auto asks = [&permissions](std::string_view name) {
        return std::ranges::any_of(permissions, [name](const Permission& permission) { return permission.name == name; });
    };
    std::vector<Capability> uncovered;
    for (const CapabilityUse& use : uses) {
        bool covered = false;
        switch (use.capability) {
        case Capability::Network:
            covered = asks("network");
            break;
        case Capability::Files:
            covered = asks("files.paths") || asks("files.pick");
            break;
        case Capability::Input:
            covered = asks("input.read");
            break;
        case Capability::Processes:
            covered = false;
            break;
        case Capability::Injection:
            covered = true;
            break;
        }
        if (!covered) {
            uncovered.push_back(use.capability);
        }
    }
    return uncovered;
}

PackageContents archiveContents(const std::filesystem::path& archive)
{
    ArchiveListing listing = inspectArchive(archive);
    PackageContents contents{.files = std::move(listing.files), .problems = std::move(listing.problems), .read = {}};
    contents.read = [archive](const std::string& path) {
        return readArchiveFile(archive, path, ArchiveLimits{}.maxFileBytes);
    };
    return contents;
}

PackageContents directoryContents(const std::filesystem::path& directory)
{
    ArchiveListing listing = inspectDirectory(directory);
    PackageContents contents{.files = std::move(listing.files), .problems = std::move(listing.problems), .read = {}};
    contents.read = [directory](const std::string& path) -> std::optional<std::string> {
        if (!safeArchivePath(path)) {
            return std::nullopt;
        }
        std::string data;
        if (platform::readFile(directory / pathOf(path), static_cast<std::size_t>(ArchiveLimits{}.maxFileBytes), data,
                               platform::WhenBusy::GiveUp)) {
            return std::nullopt;
        }
        return data;
    };
    return contents;
}

SignatureCheck checkSignature(const PackageContents& contents, std::string_view authorKey)
{
    SignatureCheck check;
    const auto contains = [&contents](std::string_view path) {
        return std::ranges::any_of(contents.files, [path](const ArchiveEntry& entry) { return entry.path == path; });
    };
    if (!contains(kChecksumsSignatureFile)) {
        return check;
    }
    check.state = SignatureState::Invalid;
    check.key = parseMinisignKey(authorKey);
    if (!check.key) {
        check.problem = "the manifest has no valid authorKey to check SHA512SUMS.minisig with";
        return check;
    }
    const std::optional<std::string> sums =
        contains(kChecksumsFile) && contents.read ? contents.read(std::string(kChecksumsFile)) : std::nullopt;
    const std::optional<std::string> signature =
        contents.read ? contents.read(std::string(kChecksumsSignatureFile)) : std::nullopt;
    if (!sums || !signature || sums->size() > kMostChecksumsBytes) {
        check.problem = "SHA512SUMS is missing or cannot be read";
        return check;
    }
    switch (verifyMinisign(*sums, *signature, *check.key)) {
    case MinisignResult::Valid:
        break;
    case MinisignResult::Malformed:
        check.problem = "SHA512SUMS.minisig is not a minisign signature";
        return check;
    case MinisignResult::OtherKey:
        check.problem = "SHA512SUMS.minisig was made with another key than authorKey";
        return check;
    case MinisignResult::BadSignature:
        check.problem = "the signature does not match SHA512SUMS";
        return check;
    case MinisignResult::BadComment:
        check.problem = "the trusted comment of SHA512SUMS.minisig was changed after signing";
        return check;
    }
    std::map<std::string, std::string, std::less<>> hashes;
    const std::string_view text = *sums;
    std::size_t start = 0;
    std::size_t number = 0;
    while (start < text.size()) {
        std::size_t stop = text.find('\n', start);
        if (stop == std::string_view::npos) {
            stop = text.size();
        }
        std::string_view line = text.substr(start, stop - start);
        start = stop + 1;
        ++number;
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        if (line.empty()) {
            continue;
        }
        const bool shaped = line.size() > 130 && std::ranges::all_of(line.substr(0, 128), hexDigit) &&
                            line[128] == ' ' && (line[129] == ' ' || line[129] == '*');
        if (!shaped) {
            check.problem = "line " + std::to_string(number) + " of SHA512SUMS is not \"<hash>  <path>\"";
            return check;
        }
        std::string_view path = line.substr(130);
        if (path.starts_with("./")) {
            path.remove_prefix(2);
        }
        if (!hashes.emplace(std::string(path), lowered(line.substr(0, 128))).second) {
            check.problem = std::string(path) + " is listed twice in SHA512SUMS";
            return check;
        }
    }
    for (const ArchiveEntry& file : contents.files) {
        if (file.path == kChecksumsFile || file.path == kChecksumsSignatureFile) {
            continue;
        }
        const auto found = hashes.find(file.path);
        if (found == hashes.end()) {
            check.problem = file.path + " is not listed in SHA512SUMS";
            return check;
        }
        const std::optional<std::string> data = contents.read(file.path);
        if (!data || hexOf(sha512(*data)) != found->second) {
            check.problem = file.path + " was changed after signing";
            return check;
        }
        hashes.erase(found);
    }
    if (!hashes.empty()) {
        check.problem = hashes.begin()->first + " is listed in SHA512SUMS but missing";
        return check;
    }
    check.state = SignatureState::Valid;
    return check;
}

bool authorChanged(const SignatureCheck& before, const SignatureCheck& after) noexcept
{
    return before.state == SignatureState::Valid && before.key.has_value() &&
           (after.state != SignatureState::Valid || !after.key.has_value() || *after.key != *before.key);
}

bool hasNativeCode(const PackageContents& contents, const Manifest& manifest)
{
    if (manifest.contributes.native.has_value()) {
        return true;
    }
    return std::ranges::any_of(contents.files, [](const ArchiveEntry& entry) {
        const std::string name = lowered(entry.path);
        return std::ranges::any_of(kNativeSuffixes, [&name](std::string_view suffix) { return name.ends_with(suffix); });
    });
}

NativeCheck checkNative(const PackageContents& contents, const Manifest& manifest, const NativeScanner& scanner)
{
    NativeCheck check;
    check.native = hasNativeCode(contents, manifest);
    if (!check.native) {
        return check;
    }
    const auto refuse = [&check](NativeRefusal refusal, std::string detail) {
        if (severity(refusal) > severity(check.refusal)) {
            check.refusal = refusal;
            check.detail = std::move(detail);
        }
    };
    if (const auto problem = std::ranges::find(contents.problems, Severity::Error, &Diagnostic::severity);
        problem != contents.problems.end()) {
        refuse(NativeRefusal::Unreadable, std::string(codeName(problem->code)) + " " + problem->path);
    }
    const std::string declared = manifest.contributes.native ? manifest.contributes.native->dll : std::string();
    if (!declared.empty() &&
        std::ranges::none_of(contents.files, [&declared](const ArchiveEntry& entry) { return entry.path == declared; })) {
        refuse(NativeRefusal::Unreadable, declared + " is missing");
    }
    for (const ArchiveEntry& entry : contents.files) {
        const std::optional<std::string> bytes = contents.read ? contents.read(entry.path) : std::nullopt;
        if (!bytes) {
            refuse(NativeRefusal::Unreadable, entry.path + " cannot be read");
            continue;
        }
        if (!looksLikePe(*bytes)) {
            if (entry.path == declared) {
                refuse(NativeRefusal::Unreadable, entry.path + " is not a Windows library");
            }
            continue;
        }
        NativeFile file{.path = entry.path,
                        .imports = readPeImports(*bytes),
                        .scan = scanner ? scanner(*bytes, entry.path) : ScanVerdict::Skipped};
        if (!file.imports.valid) {
            refuse(NativeRefusal::Unreadable, entry.path + ": " + file.imports.problem);
        }
        if (file.scan == ScanVerdict::Threat) {
            refuse(NativeRefusal::Threat, entry.path);
        } else if (file.scan == ScanVerdict::Blocked) {
            refuse(NativeRefusal::Blocked, entry.path);
        }
        check.scan = std::max(check.scan, file.scan);
        for (CapabilityUse& use : classifyImports(file.imports.functions)) {
            if (use.capability == Capability::Injection) {
                refuse(NativeRefusal::Injection, entry.path + ": " + use.functions.front());
            }
            merge(check.capabilities, std::move(use));
        }
        check.files.push_back(std::move(file));
    }
    check.uncovered = uncoveredCapabilities(check.capabilities, manifest.permissions);
    check.signature = checkSignature(contents, manifest.authorKey);
    if (check.signature.state == SignatureState::Invalid) {
        refuse(NativeRefusal::BadSignature, check.signature.problem);
    }
    return check;
}

}
