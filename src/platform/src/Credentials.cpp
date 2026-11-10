#include "gs/platform/Credentials.h"

#include <windows.h>
#include <wincred.h>

#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <vector>

namespace gs::platform {

namespace {

static_assert(kMostSecretBytes == static_cast<std::size_t>(CRED_MAX_CREDENTIAL_BLOB_SIZE));

constexpr wchar_t kUserName[] = L"GameScrcpy";

struct CredentialFree {
    void operator()(CREDENTIALW* credential) const noexcept { ::CredFree(credential); }
};

std::error_code lastError() noexcept
{
    return {static_cast<int>(::GetLastError()), std::system_category()};
}

std::error_code invalid() noexcept
{
    return std::make_error_code(std::errc::invalid_argument);
}

std::wstring wide(std::string_view utf8)
{
    if (utf8.empty() || utf8.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {};
    }
    const int size = static_cast<int>(utf8.size());
    const int length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), size, nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring text(static_cast<std::size_t>(length), L'\0');
    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), size, text.data(), length) != length) {
        return {};
    }
    return text;
}

bool validTarget(const std::wstring& name) noexcept
{
    return !name.empty() && name.size() < static_cast<std::size_t>(CRED_MAX_GENERIC_TARGET_NAME_LENGTH) &&
           name.find(L'\0') == std::wstring::npos;
}

}

std::error_code writeCredential(std::string_view target, std::string_view secret) noexcept
{
    try {
        std::wstring name = wide(target);
        if (!validTarget(name)) {
            return invalid();
        }
        if (secret.size() > kMostSecretBytes) {
            return std::make_error_code(std::errc::value_too_large);
        }
        std::vector<BYTE> blob(secret.size());
        if (!secret.empty()) {
            std::memcpy(blob.data(), secret.data(), secret.size());
        }
        std::wstring user(kUserName);
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = name.data();
        credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
        credential.CredentialBlob = blob.empty() ? nullptr : blob.data();
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        credential.UserName = user.data();
        const bool written = ::CredWriteW(&credential, 0) != FALSE;
        const std::error_code error = written ? std::error_code() : lastError();
        if (!blob.empty()) {
            ::SecureZeroMemory(blob.data(), blob.size());
        }
        return error;
    } catch (const std::bad_alloc&) {
        return std::make_error_code(std::errc::not_enough_memory);
    }
}

Expected<std::string, std::error_code> readCredential(std::string_view target) noexcept
{
    try {
        const std::wstring name = wide(target);
        if (!validTarget(name)) {
            return makeUnexpected(invalid());
        }
        PCREDENTIALW found = nullptr;
        if (::CredReadW(name.c_str(), CRED_TYPE_GENERIC, 0, &found) == FALSE) {
            return makeUnexpected(lastError());
        }
        const std::unique_ptr<CREDENTIALW, CredentialFree> owned(found);
        std::string secret;
        if (owned->CredentialBlob != nullptr && owned->CredentialBlobSize > 0) {
            secret.assign(reinterpret_cast<const char*>(owned->CredentialBlob), owned->CredentialBlobSize);
        }
        return secret;
    } catch (const std::bad_alloc&) {
        return makeUnexpected(std::make_error_code(std::errc::not_enough_memory));
    }
}

std::error_code deleteCredential(std::string_view target) noexcept
{
    try {
        const std::wstring name = wide(target);
        if (!validTarget(name)) {
            return invalid();
        }
        if (::CredDeleteW(name.c_str(), CRED_TYPE_GENERIC, 0) != FALSE) {
            return {};
        }
        const std::error_code error = lastError();
        return credentialMissing(error) ? std::error_code() : error;
    } catch (const std::bad_alloc&) {
        return std::make_error_code(std::errc::not_enough_memory);
    }
}

bool credentialMissing(const std::error_code& error) noexcept
{
    return error.category() == std::system_category() && error.value() == static_cast<int>(ERROR_NOT_FOUND);
}

}
