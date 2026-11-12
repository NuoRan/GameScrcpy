#include "Container.h"

#include "gs/exthost/Hosted.h"

#include <gs/ext/NativeCheck.h>
#include <gs/platform/Crypto.h>

#include <aclapi.h>
#include <userenv.h>

#include <array>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <system_error>
#include <vector>

namespace gs::exthost {

namespace detail {

namespace {

constexpr wchar_t kProfileDescription[] =
    L"Runs the restricted DLL of a GameScrcpy extension without the user's files, the network or other programs";
constexpr DWORD kReadExecute = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
constexpr std::size_t kMostProgramFiles = 64;
constexpr std::uintmax_t kMostImageBytes = std::uintmax_t{64} << 20;
constexpr std::size_t kNameBytes = 16;

struct FreedSid {
    void operator()(void* sid) const noexcept { FreeSid(sid); }
};

struct Containers {
    std::mutex mutex;
    std::map<std::wstring, std::unique_ptr<void, FreedSid>> sids;
    std::set<std::wstring> openPrograms;
};

Containers& containers()
{
    static Containers made;
    return made;
}

struct Sid {
    std::array<BYTE, SECURITY_MAX_SID_SIZE> bytes{};

    [[nodiscard]] PSID get() noexcept { return bytes.data(); }
};

bool wellKnown(WELL_KNOWN_SID_TYPE type, Sid& sid)
{
    DWORD size = static_cast<DWORD>(sid.bytes.size());
    return CreateWellKnownSid(type, nullptr, sid.get(), &size) != FALSE;
}

std::wstring wide(std::string_view text)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size())).wstring();
}

std::string narrow(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::wstring lowered(std::wstring text)
{
    for (wchar_t& c : text) {
        if (c >= L'A' && c <= L'Z') {
            c = static_cast<wchar_t>(c - L'A' + L'a');
        }
    }
    return text;
}

std::string readImage(const std::filesystem::path& file)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(file, error);
    if (error || size > kMostImageBytes) {
        return {};
    }
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<std::filesystem::path> programFiles(const std::filesystem::path& program)
{
    std::vector<std::filesystem::path> files{program};
    std::set<std::wstring> seen{lowered(program.filename().wstring())};
    for (std::size_t index = 0; index < files.size() && files.size() < kMostProgramFiles; ++index) {
        const ext::PeImports imports = ext::readPeImports(readImage(files[index]));
        for (const ext::ImportedFunction& function : imports.functions) {
            const std::filesystem::path name(function.module);
            if (!seen.insert(lowered(name.wstring())).second) {
                continue;
            }
            const std::filesystem::path beside = program.parent_path() / name;
            std::error_code error;
            if (std::filesystem::is_regular_file(beside, error)) {
                files.push_back(beside);
            }
        }
    }
    return files;
}

bool grants(PACL dacl, PSID first, PSID second, bool inherited)
{
    if (dacl == nullptr) {
        return true;
    }
    ACL_SIZE_INFORMATION size{};
    if (!GetAclInformation(dacl, &size, sizeof(size), AclSizeInformation)) {
        return false;
    }
    for (DWORD index = 0; index < size.AceCount; ++index) {
        void* entry = nullptr;
        if (!GetAce(dacl, index, &entry)) {
            continue;
        }
        const auto* header = static_cast<const ACE_HEADER*>(entry);
        const bool applies = inherited ? (header->AceFlags & OBJECT_INHERIT_ACE) != 0
                                       : (header->AceFlags & INHERIT_ONLY_ACE) == 0;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE || !applies) {
            continue;
        }
        auto* allowed = static_cast<ACCESS_ALLOWED_ACE*>(entry);
        const ACCESS_MASK mask = allowed->Mask;
        const bool enough = (mask & kReadExecute) == kReadExecute || (mask & GENERIC_ALL) != 0 ||
                            (mask & (GENERIC_READ | GENERIC_EXECUTE)) == (GENERIC_READ | GENERIC_EXECUTE);
        const bool named = EqualSid(&allowed->SidStart, first) != FALSE ||
                           (second != nullptr && EqualSid(&allowed->SidStart, second) != FALSE);
        if (enough && named) {
            return true;
        }
    }
    return false;
}

bool grant(const std::filesystem::path& target, PSID sid, PSID also, bool inherited, std::string& problem)
{
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    DWORD error = GetNamedSecurityInfoW(target.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                        &dacl, nullptr, &descriptor);
    if (error != ERROR_SUCCESS) {
        problem = std::format("who may open {} cannot be read (error {})", narrow(target), error);
        return false;
    }
    if (!grants(dacl, sid, also, inherited)) {
        EXPLICIT_ACCESS_W access{};
        access.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
        access.grfAccessMode = GRANT_ACCESS;
        access.grfInheritance = inherited ? SUB_CONTAINERS_AND_OBJECTS_INHERIT : NO_INHERITANCE;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        access.Trustee.ptstrName = static_cast<LPWSTR>(sid);
        PACL updated = nullptr;
        error = SetEntriesInAclW(1, &access, dacl, &updated);
        if (error == ERROR_SUCCESS) {
            std::wstring name = target.wstring();
            error = SetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                          updated, nullptr);
            LocalFree(updated);
        }
        if (error != ERROR_SUCCESS) {
            problem = std::format("{} cannot be opened to the extension's AppContainer (error {})", narrow(target), error);
        }
    }
    LocalFree(descriptor);
    return error == ERROR_SUCCESS;
}

}

std::wstring containerName(std::string_view extension)
{
    const platform::Sha256Digest digest =
        platform::sha256(std::span(reinterpret_cast<const std::uint8_t*>(extension.data()), extension.size()));
    std::wstring name = L"GameScrcpy.Extension.";
    for (std::size_t index = 0; index < kNameBytes; ++index) {
        name += std::format(L"{:02X}", digest[index]);
    }
    return name;
}

PSID containerSid(std::string_view extension, std::string& problem)
{
    const std::wstring name = containerName(extension);
    Containers& all = containers();
    const std::scoped_lock lock(all.mutex);
    if (const auto found = all.sids.find(name); found != all.sids.end()) {
        return found->second.get();
    }
    const std::wstring title = L"GameScrcpy extension " + wide(extension);
    PSID made = nullptr;
    HRESULT result = CreateAppContainerProfile(name.c_str(), title.c_str(), kProfileDescription, nullptr, 0, &made);
    if (result == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
        result = DeriveAppContainerSidFromAppContainerName(name.c_str(), &made);
    }
    if (FAILED(result) || made == nullptr) {
        problem = std::format("the extension's AppContainer could not be made (error 0x{:08X})",
                              static_cast<std::uint32_t>(result));
        return nullptr;
    }
    return all.sids.emplace(name, std::unique_ptr<void, FreedSid>(made)).first->second.get();
}

bool openToContainers(const std::filesystem::path& program, std::string& problem)
{
    Containers& all = containers();
    const std::wstring key = lowered(program.wstring());
    {
        const std::scoped_lock lock(all.mutex);
        if (all.openPrograms.contains(key)) {
            return true;
        }
    }
    Sid packages;
    if (!wellKnown(WinBuiltinAnyPackageSid, packages)) {
        problem = std::format("the group of all AppContainers is not known (error {})", GetLastError());
        return false;
    }
    for (const std::filesystem::path& file : programFiles(program)) {
        if (!grant(file, packages.get(), nullptr, false, problem)) {
            return false;
        }
    }
    const std::scoped_lock lock(all.mutex);
    all.openPrograms.insert(key);
    return true;
}

bool openToContainer(const std::filesystem::path& folder, const std::filesystem::path& dll, PSID sid,
                     std::string& problem)
{
    Sid packages;
    if (!wellKnown(WinBuiltinAnyPackageSid, packages)) {
        problem = std::format("the group of all AppContainers is not known (error {})", GetLastError());
        return false;
    }
    return grant(folder, sid, packages.get(), true, problem) && grant(dll, sid, packages.get(), false, problem);
}

HANDLE containerJob(bool restrictUi)
{
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) {
        return nullptr;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
                                              JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION |
                                              JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_JOB_MEMORY;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    limits.JobMemoryLimit = kContainerMemoryBytes;
    JOBOBJECT_BASIC_UI_RESTRICTIONS restrictions{};
    restrictions.UIRestrictionsClass = JOB_OBJECT_UILIMIT_ALL;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
        (restrictUi &&
         !SetInformationJobObject(job, JobObjectBasicUIRestrictions, &restrictions, sizeof(restrictions)))) {
        CloseHandle(job);
        return nullptr;
    }
    return job;
}

std::wstring systemFolder()
{
    std::array<wchar_t, MAX_PATH> buffer{};
    const UINT length = GetSystemDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    return length > 0 && length < buffer.size() ? std::wstring(buffer.data(), length) : std::wstring();
}

}

bool forgetContainer(std::string_view extension)
{
    const std::wstring name = detail::containerName(extension);
    {
        detail::Containers& all = detail::containers();
        const std::scoped_lock lock(all.mutex);
        all.sids.erase(name);
    }
    return SUCCEEDED(DeleteAppContainerProfile(name.c_str()));
}

bool shareWithContainer(std::string_view extension, const std::filesystem::path& file, std::string& problem)
{
    const PSID sid = detail::containerSid(extension, problem);
    return sid != nullptr && detail::grant(file, sid, nullptr, false, problem);
}

}
