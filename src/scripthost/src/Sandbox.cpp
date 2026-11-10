#include "Sandbox.h"

#include "Pipe.h"

#include <gs/base/Log.h>
#include <gs/ext/NativeCheck.h>

#include <aclapi.h>
#include <userenv.h>

#include <array>
#include <cstddef>
#include <format>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <mutex>
#include <set>
#include <system_error>
#include <vector>

namespace gs::scripthost::detail {

namespace {

constexpr wchar_t kProfileName[] = L"GameScrcpy.ScriptHost";
constexpr wchar_t kProfileTitle[] = L"GameScrcpy script host";
constexpr wchar_t kProfileDescription[] =
    L"Runs the logic of GameScrcpy extensions without the user's files, the network or other programs";
constexpr DWORD kPipeBytes = DWORD{1} << 20;
constexpr std::size_t kMostProgramFiles = 64;
constexpr std::uintmax_t kMostImageBytes = std::uintmax_t{64} << 20;
constexpr DWORD kReadExecute = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
constexpr UINT kAbandoned = 9;

std::string failure(std::string_view what, DWORD error)
{
    return std::format("{} (error {})", what, error);
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

class ContainerSid
{
public:
    ContainerSid()
    {
        PSID made = nullptr;
        HRESULT result = CreateAppContainerProfile(kProfileName, kProfileTitle, kProfileDescription, nullptr, 0, &made);
        if (result == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
            result = DeriveAppContainerSidFromAppContainerName(kProfileName, &made);
        }
        if (SUCCEEDED(result) && made != nullptr) {
            m_sid = made;
        } else {
            m_problem = std::format("the AppContainer profile could not be made (error 0x{:08X})",
                                    static_cast<std::uint32_t>(result));
        }
    }

    ~ContainerSid()
    {
        if (m_sid != nullptr) {
            FreeSid(m_sid);
        }
    }

    ContainerSid(const ContainerSid&) = delete;
    ContainerSid& operator=(const ContainerSid&) = delete;

    [[nodiscard]] PSID sid() const noexcept { return m_sid; }
    [[nodiscard]] const std::string& problem() const noexcept { return m_problem; }

private:
    PSID m_sid = nullptr;
    std::string m_problem;
};

const ContainerSid& containerSid()
{
    static const ContainerSid sid;
    return sid;
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

bool allowsPackages(PACL dacl, PSID packages)
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
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE || (header->AceFlags & INHERIT_ONLY_ACE) != 0) {
            continue;
        }
        auto* allowed = static_cast<ACCESS_ALLOWED_ACE*>(entry);
        const ACCESS_MASK mask = allowed->Mask;
        const bool enough = (mask & kReadExecute) == kReadExecute || (mask & GENERIC_ALL) != 0 ||
                            (mask & (GENERIC_READ | GENERIC_EXECUTE)) == (GENERIC_READ | GENERIC_EXECUTE);
        if (enough && EqualSid(&allowed->SidStart, packages)) {
            return true;
        }
    }
    return false;
}

bool allowPackages(const std::filesystem::path& file, PSID packages)
{
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetNamedSecurityInfoW(file.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl,
                              nullptr, &descriptor) != ERROR_SUCCESS) {
        return false;
    }
    bool allowed = allowsPackages(dacl, packages);
    if (!allowed) {
        EXPLICIT_ACCESS_W access{};
        access.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
        access.grfAccessMode = GRANT_ACCESS;
        access.grfInheritance = NO_INHERITANCE;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        access.Trustee.ptstrName = static_cast<LPWSTR>(packages);
        PACL updated = nullptr;
        if (SetEntriesInAclW(1, &access, dacl, &updated) == ERROR_SUCCESS) {
            std::wstring name = file.wstring();
            allowed = SetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                            updated, nullptr) == ERROR_SUCCESS;
            LocalFree(updated);
        }
    }
    LocalFree(descriptor);
    return allowed;
}

bool allowContainers(const std::filesystem::path& program)
{
    static std::once_flag once;
    static bool allowed = false;
    std::call_once(once, [&program] {
        Sid packages;
        if (!wellKnown(WinBuiltinAnyPackageSid, packages)) {
            return;
        }
        const std::vector<std::filesystem::path> files = programFiles(program);
        std::size_t refused = 0;
        for (const std::filesystem::path& file : files) {
            if (!allowPackages(file, packages.get())) {
                ++refused;
            }
        }
        allowed = refused == 0;
        if (allowed) {
            log::info("logic", "AppContainers may read and run {} program file(s) of the logic sandbox", files.size());
        } else {
            log::warning("logic", "{} of {} program file(s) of the logic sandbox cannot be opened to AppContainers",
                         refused, files.size());
        }
    });
    return allowed;
}

bool allowInDefaultDacl(HANDLE token, PSID sid)
{
    DWORD size = 0;
    GetTokenInformation(token, TokenDefaultDacl, nullptr, 0, &size);
    if (size == 0) {
        return false;
    }
    std::vector<std::byte> buffer(size);
    if (!GetTokenInformation(token, TokenDefaultDacl, buffer.data(), size, &size)) {
        return false;
    }
    const auto* current = reinterpret_cast<const TOKEN_DEFAULT_DACL*>(buffer.data());
    EXPLICIT_ACCESS_W access{};
    access.grfAccessPermissions = GENERIC_ALL;
    access.grfAccessMode = GRANT_ACCESS;
    access.grfInheritance = NO_INHERITANCE;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    access.Trustee.ptstrName = static_cast<LPWSTR>(sid);
    PACL updated = nullptr;
    if (SetEntriesInAclW(1, &access, current->DefaultDacl, &updated) != ERROR_SUCCESS) {
        return false;
    }
    TOKEN_DEFAULT_DACL next{};
    next.DefaultDacl = updated;
    const bool set = SetTokenInformation(token, TokenDefaultDacl, &next, sizeof(next)) != FALSE;
    LocalFree(updated);
    return set;
}

bool userSid(HANDLE token, Sid& sid)
{
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (size == 0) {
        return false;
    }
    std::vector<std::byte> buffer(size);
    if (!GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        return false;
    }
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    return CopySid(static_cast<DWORD>(sid.bytes.size()), sid.get(), user->User.Sid) != FALSE;
}

HANDLE restrictedToken(std::string& problem)
{
    HANDLE own = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_DEFAULT, &own)) {
        problem = failure("the program's token could not be opened", GetLastError());
        return nullptr;
    }
    constexpr std::array<WELL_KNOWN_SID_TYPE, 4> kRestricting{WinWorldSid, WinBuiltinUsersSid, WinAuthenticatedUserSid,
                                                              WinRestrictedCodeSid};
    std::array<Sid, 4> sids{};
    std::array<SID_AND_ATTRIBUTES, 4> restricting{};
    Sid administrators;
    Sid low;
    bool known = wellKnown(WinBuiltinAdministratorsSid, administrators) && wellKnown(WinLowLabelSid, low);
    for (std::size_t index = 0; index < kRestricting.size() && known; ++index) {
        known = wellKnown(kRestricting[index], sids[index]);
        restricting[index].Sid = sids[index].get();
    }
    SID_AND_ATTRIBUTES disabled{};
    disabled.Sid = administrators.get();
    HANDLE restricted = nullptr;
    const bool made = known && CreateRestrictedToken(own, DISABLE_MAX_PRIVILEGE, 1, &disabled, 0, nullptr,
                                                     static_cast<DWORD>(restricting.size()), restricting.data(),
                                                     &restricted) != FALSE;
    const DWORD error = made ? ERROR_SUCCESS : GetLastError();
    CloseHandle(own);
    if (!made) {
        problem = failure("a restricted token could not be made", error);
        return nullptr;
    }
    TOKEN_MANDATORY_LABEL label{};
    label.Label.Attributes = SE_GROUP_INTEGRITY;
    label.Label.Sid = low.get();
    Sid user;
    if (!SetTokenInformation(restricted, TokenIntegrityLevel, &label,
                             static_cast<DWORD>(sizeof(label) + GetLengthSid(low.get()))) ||
        !userSid(restricted, user) || !allowInDefaultDacl(restricted, sids[3].get()) ||
        !allowInDefaultDacl(restricted, user.get())) {
        problem = failure("the restricted token could not be lowered", GetLastError());
        CloseHandle(restricted);
        return nullptr;
    }
    return restricted;
}

HANDLE sandboxJob(bool restrictUi)
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
    limits.JobMemoryLimit = kJobMemoryBytes;
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

void closeAll(std::initializer_list<HANDLE> handles) noexcept
{
    for (const HANDLE handle : handles) {
        if (handle != nullptr) {
            CloseHandle(handle);
        }
    }
}

}

std::optional<Launched> launch(const std::filesystem::path& program, SandboxMode mode, const std::wstring& extra,
                               std::string& problem)
{
    PSID container = nullptr;
    HANDLE token = nullptr;
    if (mode == SandboxMode::AppContainer) {
        const ContainerSid& sid = containerSid();
        if (sid.sid() == nullptr) {
            problem = sid.problem();
            return std::nullopt;
        }
        if (!allowContainers(program)) {
            problem = "the program files cannot be opened to AppContainers";
            return std::nullopt;
        }
        container = sid.sid();
    } else if (mode == SandboxMode::Restricted) {
        token = restrictedToken(problem);
        if (token == nullptr) {
            return std::nullopt;
        }
    }
    HANDLE childIn = nullptr;
    HANDLE toChild = nullptr;
    HANDLE fromChild = nullptr;
    HANDLE childOut = nullptr;
    if (!CreatePipe(&childIn, &toChild, nullptr, kPipeBytes) || !CreatePipe(&fromChild, &childOut, nullptr, kPipeBytes) ||
        !SetHandleInformation(childIn, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT) ||
        !SetHandleInformation(childOut, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
        problem = failure("the pipes to gs-scripthost.exe could not be made", GetLastError());
        closeAll({childIn, toChild, fromChild, childOut, token});
        return std::nullopt;
    }
    std::array<HANDLE, 2> inherited{childIn, childOut};
    std::wstring command = L"\"" + program.wstring() + L"\" --channel " + handleText(childIn, childOut);
    if (!extra.empty()) {
        command += L" " + extra;
    }
    const DWORD attributeCount = container != nullptr ? 2 : 1;
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, attributeCount, 0, &size);
    std::vector<std::byte> storage(size);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    SECURITY_CAPABILITIES capabilities{};
    capabilities.AppContainerSid = container;
    PROCESS_INFORMATION started{};
    DWORD error = ERROR_SUCCESS;
    bool made = InitializeProcThreadAttributeList(attributes, attributeCount, 0, &size) != FALSE;
    if (made) {
        made = UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited.data(),
                                         sizeof(HANDLE) * inherited.size(), nullptr, nullptr) != FALSE;
        if (made && container != nullptr) {
            made = UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &capabilities,
                                             sizeof(capabilities), nullptr, nullptr) != FALSE;
        }
        if (made) {
            STARTUPINFOEXW startup{};
            startup.StartupInfo.cb = sizeof(startup);
            startup.lpAttributeList = attributes;
            const std::wstring folder = systemFolder();
            const DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW;
            const wchar_t* directory = folder.empty() ? nullptr : folder.c_str();
            made = token != nullptr ? CreateProcessAsUserW(token, program.c_str(), command.data(), nullptr, nullptr, TRUE,
                                                           flags, nullptr, directory, &startup.StartupInfo,
                                                           &started) != FALSE
                                    : CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, TRUE, flags,
                                                     nullptr, directory, &startup.StartupInfo, &started) != FALSE;
        }
        error = made ? ERROR_SUCCESS : GetLastError();
        DeleteProcThreadAttributeList(attributes);
    } else {
        error = GetLastError();
    }
    closeAll({childIn, childOut, token});
    if (!made) {
        problem = failure("gs-scripthost.exe could not be started", error);
        closeAll({toChild, fromChild});
        return std::nullopt;
    }
    HANDLE job = sandboxJob(true);
    bool placed = job != nullptr && AssignProcessToJobObject(job, started.hProcess) != FALSE;
    DWORD placing = placed ? ERROR_SUCCESS : GetLastError();
    if (!placed && placing == ERROR_INVALID_PARAMETER) {
        if (job != nullptr) {
            CloseHandle(job);
        }
        job = sandboxJob(false);
        placed = job != nullptr && AssignProcessToJobObject(job, started.hProcess) != FALSE;
        placing = placed ? ERROR_SUCCESS : GetLastError();
    }
    if (!placed) {
        problem = failure("gs-scripthost.exe could not be put in its job", placing);
        TerminateProcess(started.hProcess, kAbandoned);
        closeAll({started.hThread, started.hProcess, job, toChild, fromChild});
        return std::nullopt;
    }
    ResumeThread(started.hThread);
    CloseHandle(started.hThread);
    return Launched{.process = started.hProcess,
                    .job = job,
                    .toChild = toChild,
                    .fromChild = fromChild,
                    .id = started.dwProcessId,
                    .mode = mode};
}

void closeLaunched(Launched& launched) noexcept
{
    closeAll({launched.process, launched.job, launched.toChild, launched.fromChild});
    launched = Launched{};
}

}
