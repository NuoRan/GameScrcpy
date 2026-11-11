#include "TestFiles.h"

#include "gs/ext/Archive.h"
#include "gs/ext/Minisign.h"
#include "gs/ext/NativeCheck.h"
#include "gs/ext/Package.h"
#include "gs/ext/Signing.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace gs::ext;
using namespace gs::ext::test;
namespace fs = std::filesystem;

namespace {

constexpr std::string_view kId = "alice.signed";

MinisignSecretKey keyFrom(std::uint8_t number)
{
    Ed25519Seed seed{};
    seed.fill(number);
    MinisignKeyId id{};
    id.fill(number);
    return makeMinisignKey(seed, id);
}

std::string manifestFor(std::string_view version, const MinisignSecretKey* key)
{
    std::string text = R"({"schemaVersion": 1, "id": "alice.signed", "version": ")" + std::string(version) +
                       R"(", "name": "Signed", "license": "MIT", "requires": {"theme": "^1.0"},
                           "contributes": {"themes": [{"name": "Ember", "dark": {"accent": "#FF6B3D"}}]})";
    if (key != nullptr) {
        text += R"(, "authorKey": ")" + minisignKeyText(minisignPublicKey(*key)) + "\"";
    }
    return text + "}";
}

fs::path folderFor(const Scratch& scratch, std::string_view name, std::string_view version,
                   const MinisignSecretKey* key)
{
    const fs::path folder = scratch / ("folder-" + std::string(name));
    writeFile(folder / "manifest.json", manifestFor(version, key));
    writeFile(folder / "notes/readme.txt", "theme notes\n");
    return folder;
}

fs::path packageFor(const Scratch& scratch, std::string_view name, std::string_view version,
                    const MinisignSecretKey* key)
{
    const fs::path folder = folderFor(scratch, name, version, key);
    const fs::path archive = scratch / (std::string(name) + ".gsext");
    const std::vector<Diagnostic> problems =
        key != nullptr ? packSigned(folder, archive, *key, "test") : packDirectory(folder, archive);
    INFO(listed(problems));
    REQUIRE(problems.empty());
    return archive;
}

}

TEST_CASE("ADR-0112: a signed package lists every file in SHA512SUMS and its signature checks out with the key")
{
    Scratch scratch;
    const MinisignSecretKey key = keyFrom(1);
    const fs::path folder = folderFor(scratch, "first", "1.0.0", &key);
    writeFile(folder / "SHA512SUMS", "stale\n");
    writeFile(folder / "SHA512SUMS.minisig", "stale\n");
    const fs::path archive = scratch / "first.gsext";
    const std::vector<Diagnostic> packed = packSigned(folder, archive, key, signingComment(*checkDirectory(folder).manifest));
    INFO(listed(packed));
    REQUIRE(packed.empty());

    const PackageContents contents = archiveContents(archive);
    const std::optional<std::string> sums = contents.read(std::string(kChecksumsFile));
    REQUIRE(sums.has_value());
    CHECK(sums->find("  manifest.json\n") != std::string::npos);
    CHECK(sums->find("  notes/readme.txt\n") != std::string::npos);
    CHECK(sums->find("SHA512SUMS") == std::string::npos);
    CHECK(sums->find("stale") == std::string::npos);
    const std::optional<std::string> signature = contents.read(std::string(kChecksumsSignatureFile));
    REQUIRE(signature.has_value());
    CHECK(signature->find("trusted comment: file:SHA512SUMS\thashed\textension:alice.signed 1.0.0\n") !=
          std::string::npos);

    const SignatureCheck check = checkSignature(contents, minisignKeyText(minisignPublicKey(key)));
    INFO(check.problem);
    CHECK(check.state == SignatureState::Valid);
    REQUIRE(check.key.has_value());
    CHECK(*check.key == minisignPublicKey(key));
    CHECK(checkSignature(contents, minisignKeyText(minisignPublicKey(keyFrom(2)))).state == SignatureState::Invalid);
    CHECK(checkPackage(archive).ok());

    std::vector<Diagnostic> problems;
    CHECK(checksumsOf(directoryContents(folder), problems) == *sums);
    CHECK(problems.empty());
}

TEST_CASE("ADR-0112: the remembered publisher key is kept per extension, read back and forgotten")
{
    Scratch scratch;
    const fs::path root = scratch / "extensions";
    const MinisignKey key = minisignPublicKey(keyFrom(3));
    CHECK_FALSE(rememberedPublisher(root, kId).has_value());
    REQUIRE_FALSE(rememberPublisher(root, kId, key));
    CHECK(fs::exists(root / ".publishers/alice.signed.pub"));
    CHECK(readFile(root / ".publishers/alice.signed.pub") == minisignPublicKeyFile(key));
    CHECK(rememberedPublisher(root, kId) == key);
    CHECK(publisherOf(root, kId) == key);
    CHECK_FALSE(rememberedPublisher(root, "alice.other").has_value());
    CHECK(rememberPublisher(root, "../escape", key));
    CHECK_FALSE(rememberedPublisher(root, "../escape").has_value());
    CHECK_FALSE(forgetPublisher(root, kId));
    CHECK_FALSE(rememberedPublisher(root, kId).has_value());
    CHECK(scanInstalled(root).empty());
}

TEST_CASE("ADR-0112: against the remembered key, a new key, a lost signature and a broken one are refused")
{
    const MinisignKey first = minisignPublicKey(keyFrom(4));
    const MinisignKey second = minisignPublicKey(keyFrom(5));
    const SignatureCheck unsignedOne{};
    const SignatureCheck byFirst{.state = SignatureState::Valid, .key = first, .problem = {}};
    const SignatureCheck bySecond{.state = SignatureState::Valid, .key = second, .problem = {}};
    const SignatureCheck broken{.state = SignatureState::Invalid, .key = first, .problem = "manifest.json was changed"};

    CHECK_FALSE(publisherProblem(std::nullopt, unsignedOne).has_value());
    CHECK_FALSE(publisherProblem(std::nullopt, byFirst).has_value());
    CHECK_FALSE(publisherProblem(first, byFirst).has_value());
    const std::optional<Diagnostic> changed = publisherProblem(first, bySecond);
    REQUIRE(changed.has_value());
    CHECK(changed->code == Code::PublisherChanged);
    CHECK(changed->severity == Severity::Error);
    CHECK(changed->detail.find(minisignKeyId(second)) != std::string::npos);
    CHECK(changed->detail.find(minisignKeyId(first)) != std::string::npos);
    const std::optional<Diagnostic> dropped = publisherProblem(first, unsignedOne);
    REQUIRE(dropped.has_value());
    CHECK(dropped->code == Code::SignatureMissing);
    const std::optional<Diagnostic> invalid = publisherProblem(std::nullopt, broken);
    REQUIRE(invalid.has_value());
    CHECK(invalid->code == Code::SignatureInvalid);
    CHECK(invalid->detail == "manifest.json was changed");
}

TEST_CASE("ADR-0112: installing remembers the first key, takes updates signed with it, and refuses another key or "
          "no signature")
{
    Scratch scratch;
    const fs::path root = scratch / "extensions";
    const MinisignSecretKey key = keyFrom(6);
    const MinisignSecretKey other = keyFrom(7);

    const InstalledExtension first = installPackage(packageFor(scratch, "v1", "1.0.0", &key), root);
    INFO(listed(first.diagnostics));
    REQUIRE(first.ok());
    CHECK(rememberedPublisher(root, kId) == minisignPublicKey(key));

    const InstalledExtension update = installPackage(packageFor(scratch, "v2", "2.0.0", &key), root);
    INFO(listed(update.diagnostics));
    CHECK(update.ok());

    const InstalledExtension rekeyed = installPackage(packageFor(scratch, "v3", "3.0.0", &other), root);
    CHECK_FALSE(rekeyed.ok());
    CHECK(reports(rekeyed.diagnostics, Code::PublisherChanged, "$.authorKey"));
    CHECK_FALSE(fs::exists(root / "alice.signed/3.0.0"));

    const InstalledExtension stripped = installPackage(packageFor(scratch, "v4", "4.0.0", nullptr), root);
    CHECK_FALSE(stripped.ok());
    CHECK(reports(stripped.diagnostics, Code::SignatureMissing));
    CHECK_FALSE(fs::exists(root / "alice.signed/4.0.0"));
    CHECK(rememberedPublisher(root, kId) == minisignPublicKey(key));

    REQUIRE_FALSE(forgetPublisher(root, kId));
    CHECK(publisherOf(root, kId) == minisignPublicKey(key));
    CHECK_FALSE(installPackage(packageFor(scratch, "v5", "5.0.0", &other), root).ok());

    CHECK(removeInstalled(root, kId, Version{1, 0, 0}));
    CHECK(removeInstalled(root, kId, Version{2, 0, 0}));
    CHECK_FALSE(publisherOf(root, kId).has_value());
    const InstalledExtension accepted = installPackage(packageFor(scratch, "v6", "6.0.0", &other), root);
    INFO(listed(accepted.diagnostics));
    CHECK(accepted.ok());
    CHECK(rememberedPublisher(root, kId) == minisignPublicKey(other));
}

TEST_CASE("ADR-0112: an unsigned extension may start signing, and a package whose signature does not match is not "
          "installed")
{
    Scratch scratch;
    const fs::path root = scratch / "extensions";
    const MinisignSecretKey key = keyFrom(8);
    REQUIRE(installPackage(packageFor(scratch, "plain", "1.0.0", nullptr), root).ok());
    CHECK_FALSE(rememberedPublisher(root, kId).has_value());
    REQUIRE(installPackage(packageFor(scratch, "signed", "1.1.0", &key), root).ok());
    CHECK(rememberedPublisher(root, kId) == minisignPublicKey(key));

    const fs::path folder = folderFor(scratch, "forged", "1.2.0", &key);
    const fs::path signedOne = scratch / "forged-signed.gsext";
    REQUIRE(packSigned(folder, signedOne, key, "test").empty());
    const fs::path unpacked = scratch / "forged-files";
    REQUIRE(extractArchive(signedOne, unpacked).empty());
    writeFile(unpacked / "notes/readme.txt", "changed after signing\n");
    const fs::path forged = scratch / "forged.gsext";
    REQUIRE(packDirectory(unpacked, forged).empty());
    const InstalledExtension refused = installPackage(forged, root);
    CHECK_FALSE(refused.ok());
    CHECK(reports(refused.diagnostics, Code::SignatureInvalid));
    CHECK_FALSE(fs::exists(root / "alice.signed/1.2.0"));
}
