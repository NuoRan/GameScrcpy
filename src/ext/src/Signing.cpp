#include "gs/ext/Signing.h"

#include "gs/ext/Archive.h"
#include "gs/ext/Package.h"

#include <gs/platform/AtomicFile.h>

#include <cstddef>
#include <cstdint>
#include <system_error>
#include <utility>

namespace gs::ext {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kMostPublisherFileBytes = 4096;

fs::path pathOf(std::string_view text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
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

fs::path publisherFile(const fs::path& root, std::string_view id)
{
    if (root.empty() || !validExtensionId(id)) {
        return {};
    }
    return root / pathOf(kPublishersFolder) / pathOf(std::string(id) + ".pub");
}

}

std::string checksumsOf(const PackageContents& contents, std::vector<Diagnostic>& problems)
{
    std::string sums;
    for (const ArchiveEntry& file : contents.files) {
        if (file.path == kChecksumsFile || file.path == kChecksumsSignatureFile) {
            continue;
        }
        const std::optional<std::string> data = contents.read ? contents.read(file.path) : std::nullopt;
        if (!data) {
            problems.push_back({Severity::Error, Code::ArchiveUnreadable, file.path, {}});
            continue;
        }
        sums += hexOf(sha512(*data));
        sums += "  ";
        sums += file.path;
        sums += '\n';
    }
    return sums;
}

std::string signingComment(const Manifest& manifest)
{
    return "file:SHA512SUMS\thashed\textension:" + manifest.id + " " + manifest.version.text();
}

std::vector<Diagnostic> packSigned(const fs::path& directory, const fs::path& archive, const MinisignSecretKey& key,
                                   std::string_view trustedComment)
{
    const PackageContents contents = directoryContents(directory);
    if (hasErrors(contents.problems)) {
        return contents.problems;
    }
    std::vector<Diagnostic> problems;
    std::string sums = checksumsOf(contents, problems);
    if (hasErrors(problems)) {
        return problems;
    }
    std::string signature = signMinisign(sums, key, trustedComment);
    const std::vector<PackedFile> added{
        PackedFile{.path = std::string(kChecksumsFile), .data = std::move(sums)},
        PackedFile{.path = std::string(kChecksumsSignatureFile), .data = std::move(signature)}};
    return packDirectory(directory, archive, ArchiveLimits{}, std::vector<std::string>{}, added);
}

std::optional<MinisignKey> rememberedPublisher(const fs::path& root, std::string_view id)
{
    const fs::path file = publisherFile(root, id);
    std::string text;
    if (file.empty() || platform::readFile(file, kMostPublisherFileBytes, text, platform::WhenBusy::GiveUp)) {
        return std::nullopt;
    }
    const std::size_t first = text.find('\n');
    if (first == std::string::npos) {
        return std::nullopt;
    }
    std::string_view line = std::string_view(text).substr(first + 1);
    line = line.substr(0, line.find('\n'));
    if (line.ends_with('\r')) {
        line.remove_suffix(1);
    }
    return parseMinisignKey(line);
}

std::error_code rememberPublisher(const fs::path& root, std::string_view id, const MinisignKey& key)
{
    const fs::path file = publisherFile(root, id);
    if (file.empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    if (error) {
        return error;
    }
    return platform::writeFileAtomically(file, minisignPublicKeyFile(key));
}

std::error_code forgetPublisher(const fs::path& root, std::string_view id)
{
    const fs::path file = publisherFile(root, id);
    if (file.empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    return platform::removeFile(file);
}

std::optional<MinisignKey> publisherOf(const fs::path& root, std::string_view id)
{
    if (std::optional<MinisignKey> remembered = rememberedPublisher(root, id)) {
        return remembered;
    }
    if (root.empty() || !validExtensionId(id)) {
        return std::nullopt;
    }
    std::error_code error;
    for (fs::directory_iterator it(root / pathOf(id), error), end; !error && it != end; it.increment(error)) {
        std::error_code ignored;
        if (it->is_symlink(ignored) || !it->is_directory(ignored)) {
            continue;
        }
        const PackageCheck check = checkDirectory(it->path());
        if (!check.manifest || check.manifest->id != id) {
            continue;
        }
        const SignatureCheck signature = checkSignedSums(directoryContents(it->path()), check.manifest->authorKey);
        if (signature.state == SignatureState::Valid && signature.key) {
            return signature.key;
        }
    }
    return std::nullopt;
}

std::optional<Diagnostic> publisherProblem(const std::optional<MinisignKey>& remembered,
                                           const SignatureCheck& incoming)
{
    if (incoming.state == SignatureState::Invalid) {
        return Diagnostic{Severity::Error, Code::SignatureInvalid, std::string(kChecksumsSignatureFile),
                          incoming.problem};
    }
    if (!remembered) {
        return std::nullopt;
    }
    if (incoming.state != SignatureState::Valid || !incoming.key) {
        return Diagnostic{Severity::Error, Code::SignatureMissing, std::string(kChecksumsSignatureFile),
                          "the installed version is signed with key " + minisignKeyId(*remembered)};
    }
    if (*incoming.key != *remembered) {
        return Diagnostic{Severity::Error, Code::PublisherChanged, "$.authorKey",
                          "signed with key " + minisignKeyId(*incoming.key) + ", the installed version with key " +
                              minisignKeyId(*remembered)};
    }
    return std::nullopt;
}

}
