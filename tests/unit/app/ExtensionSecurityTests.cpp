#include "AmsiScan.h"
#include "AppSettings.h"
#include "ExtensionHost.h"

#include "unit/ext/PeImage.h"
#include "unit/ext/TestFiles.h"

#include <gs/ext/Archive.h>
#include <gs/ext/Bundle.h>
#include <gs/ext/Minisign.h>
#include <gs/ext/NativeCheck.h>
#include <gs/ext/Signing.h>

#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

using gs::app::AppSettings;
using gs::app::ExtensionHost;
using gs::ext::ScanVerdict;
using namespace gs::ext::test;
namespace fs = std::filesystem;

namespace {

constexpr auto kInstallTimeout = std::chrono::seconds(10);

constexpr std::string_view kShader = "//! PARAM strength float 0.5 0 1 0.05\n//! PASS glow\nfloat4 glow(float2 uv)\n{\n"
                                     "    return Source.Sample(Linear, uv) * strength;\n}\n";
constexpr std::string_view kFirstManifest =
    "{\"schemaVersion\": 1, \"id\": \"alice.signed\", \"version\": \"1.0.0\", \"name\": \"Signed\", \"license\": "
    "\"MIT\", \"authorKey\": \"RWQYB7al9OPSwU7aritYZ3nmUlPQk5fh7N4Izs3H4UUtma8A/RO4rFBZ\", \"requires\": {\"filter\": "
    "\"^1.0\"}, \"contributes\": {\"filters\": [{\"name\": \"Glow\", \"file\": \"shaders/glow.hlsl\"}]}}\n";
constexpr std::string_view kFirstSums =
    "a862fa25cfde982f84b7e0fa720f31391ebfc292eaa2929d90f98e7a4e835c166f6aaea179bdb63171ea3eb878675396d5b73893e070a8e7"
    "395e955551fa15d9  bin/helper.dll\n"
    "400ab0369117acaa05ceb47589b555f814de3356a4cdf212fbdd68f792aae464495090da81843f576cda9420137b99f19544830c04ac7fab"
    "72d6948eff22c683  manifest.json\n"
    "ec1121f24c14c91f59f4640d1cac22e16f192d7aabed66065fed48eeb14fb415483ba8816706c1b55c6ef85ea7673194384d53919e91852"
    "2f4a3f3c27b1b1d1c  shaders/glow.hlsl\n";
constexpr std::string_view kFirstSignature =
    "untrusted comment: signature from minisign secret key\n"
    "RUQYB7al9OPSwZpelaWkc03cFh/vmiuN5Rw4cWRIyA/knI9bTysfaTzR8os25hZtWyWaJEI/PPFmw1a9R/Mfvex9r3j04OVYMQM=\n"
    "trusted comment: timestamp:1762430400\tfile:SHA512SUMS\thashed\n"
    "379p5Xjpr6pT+q/oUbgX85O/XoLTbnBfT8Ano24sDS04Ug4AFJyLwYQGHive9jCgZ6I2BSV2pIWYnit0+GBzAQ==\n";
constexpr std::string_view kSecondManifest =
    "{\"schemaVersion\": 1, \"id\": \"alice.signed\", \"version\": \"2.0.0\", \"name\": \"Signed\", \"license\": "
    "\"MIT\", \"authorKey\": \"RWSId2ZVRDMiEa7DCLu1nxR+gMiGsuYNvpyJdwItkg7PqsjnQoi009w+\", \"requires\": {\"filter\": "
    "\"^1.0\"}, \"contributes\": {\"filters\": [{\"name\": \"Glow\", \"file\": \"shaders/glow.hlsl\"}]}}\n";
constexpr std::string_view kSecondSums =
    "a862fa25cfde982f84b7e0fa720f31391ebfc292eaa2929d90f98e7a4e835c166f6aaea179bdb63171ea3eb878675396d5b73893e070a8e7"
    "395e955551fa15d9  bin/helper.dll\n"
    "5f12161efe2ef61ffaeae9e3d05ef43cd3be5d840546d86d2fce645f09e3edd37d01a9646562b7987b3a6b3c36f87459480e5f949c17514"
    "0e72487a3923d30f1  manifest.json\n"
    "ec1121f24c14c91f59f4640d1cac22e16f192d7aabed66065fed48eeb14fb415483ba8816706c1b55c6ef85ea7673194384d53919e91852"
    "2f4a3f3c27b1b1d1c  shaders/glow.hlsl\n";
constexpr std::string_view kSecondSignature =
    "untrusted comment: signature from minisign secret key\n"
    "RUSId2ZVRDMiEV5ONUIhkCQvR8WN9CBYMfUiIgZqO31SDHcnViMQmjTilNUZ8OcRsEb4CesS1l2JKsfFu7pScQ1LsUX6bKPtLw8=\n"
    "trusted comment: timestamp:1762430400\tfile:SHA512SUMS\thashed\n"
    "DUlgB4ACJlb5A5HRnCCw1mowk87YrSmIFYb0gde+tMnt3w3IN/zq1Y83ZrNGb24LsPMiPwyxczZe8zJxemwUCQ==\n";

std::string nativeManifest(std::string_view id, std::string_view more = {})
{
    return R"({"schemaVersion": 1, "id": ")" + std::string(id) + R"(", "version": "1.0.0", "name": "Helper",
        "license": "MIT", "requires": {"native": "^1.0"}, "contributes": {"native": {"dll": "bin/helper.dll"}})" +
           std::string(more) + "}";
}

gs::ext::NativeScanner verdict(ScanVerdict result)
{
    return [result](std::string_view, std::string_view) { return result; };
}

QString textOf(const fs::path& path)
{
    return QString::fromStdWString(path.wstring());
}

std::string codesOf(const QVariantMap& result)
{
    std::string codes;
    const QVariantList problems = result.value(QStringLiteral("problems")).toList();
    for (const QVariant& problem : problems) {
        codes += problem.toMap().value(QStringLiteral("code")).toString().toStdString() + " ";
    }
    return codes;
}

QStringList namesOf(const QVariantList& capabilities)
{
    QStringList names;
    for (const QVariant& capability : capabilities) {
        names.append(capability.toMap().value(QStringLiteral("name")).toString());
    }
    return names;
}

class ApplicationScope
{
public:
    ApplicationScope()
    {
        if (QCoreApplication::instance() == nullptr) {
            m_application = std::make_unique<QCoreApplication>(m_argc, m_argv.data());
        }
    }

    ApplicationScope(const ApplicationScope&) = delete;
    ApplicationScope& operator=(const ApplicationScope&) = delete;

private:
    int m_argc = 1;
    std::string m_name = "gs_app_tests";
    std::array<char*, 2> m_argv{m_name.data(), nullptr};
    std::unique_ptr<QCoreApplication> m_application;
};

class InstallWatch
{
public:
    explicit InstallWatch(ExtensionHost& host)
    {
        QObject::connect(&host, &ExtensionHost::installFinished, &m_context,
                         [this](const QVariantMap& result) { m_results.append(result); });
        QObject::connect(&host, &ExtensionHost::bundleFinished, &m_context,
                         [this](const QVariantMap& result) { m_results.append(result); });
    }

    InstallWatch(const InstallWatch&) = delete;
    InstallWatch& operator=(const InstallWatch&) = delete;

    bool waitFor(qsizetype count)
    {
        const auto deadline = std::chrono::steady_clock::now() + kInstallTimeout;
        while (m_results.size() < count && std::chrono::steady_clock::now() < deadline) {
            QCoreApplication::processEvents();
            std::this_thread::yield();
        }
        return m_results.size() >= count;
    }

    [[nodiscard]] const QList<QVariantMap>& results() const noexcept { return m_results; }

private:
    QList<QVariantMap> m_results;
    QObject m_context;
};

struct Fixture {
    Scratch scratch;
    fs::path data = scratch / "data";
    fs::path user = scratch / "user";

    [[nodiscard]] fs::path folder(std::string_view stem, const std::string& manifestText, const PeLayout& layout) const
    {
        const fs::path where = scratch / ("package-" + std::string(stem));
        writeFile(where / "manifest.json", manifestText);
        writeFile(where / "bin/helper.dll", buildPe(layout));
        return where;
    }

    [[nodiscard]] fs::path pack(std::string_view stem, const std::string& manifestText, const PeLayout& layout) const
    {
        const fs::path archive = scratch / (std::string(stem) + ".gsext");
        REQUIRE(gs::ext::packDirectory(folder(stem, manifestText, layout), archive).empty());
        return archive;
    }

    [[nodiscard]] fs::path signedPackage(std::string_view stem, std::string_view manifestText, std::string_view sums,
                                         std::string_view signature) const
    {
        const fs::path where = scratch / ("signed-" + std::string(stem));
        writeFile(where / "manifest.json", manifestText);
        writeFile(where / "shaders/glow.hlsl", kShader);
        writeFile(where / "bin/helper.dll", buildPe({}));
        if (!sums.empty()) {
            writeFile(where / "SHA512SUMS", sums);
            writeFile(where / "SHA512SUMS.minisig", signature);
        }
        const fs::path archive = scratch / (std::string(stem) + ".gsext");
        REQUIRE(gs::ext::packDirectory(where, archive).empty());
        return archive;
    }
};

}

TEST_CASE("a package with native code shows that its author is not verified, what its code does and what it does not ask for")
{
    const Fixture fixture;
    const fs::path archive =
        fixture.pack("helper", nativeManifest("alice.helper"),
                     {.imports = {{"ws2_32.dll", {"connect"}}, {"kernel32.dll", {"CreateFileW"}},
                                  {"shell32.dll", {"ShellExecuteW"}}}});
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.setScanner(verdict(ScanVerdict::Clean));
    host.start(false);

    const QVariantMap info = host.inspect(textOf(archive));
    INFO(codesOf(info));
    CHECK(info.value(QStringLiteral("ok")).toBool());
    REQUIRE(info.contains(QStringLiteral("security")));
    const QVariantMap security = info.value(QStringLiteral("security")).toMap();
    CHECK(security.value(QStringLiteral("native")).toBool());
    CHECK(security.value(QStringLiteral("author")).toString() == QStringLiteral("unsigned"));
    CHECK(security.value(QStringLiteral("keyId")).toString().isEmpty());
    CHECK_FALSE(security.value(QStringLiteral("keyChanged")).toBool());
    CHECK(security.value(QStringLiteral("scan")).toString() == QStringLiteral("clean"));
    CHECK_FALSE(security.value(QStringLiteral("refused")).toBool());
    CHECK(security.value(QStringLiteral("files")).toStringList() == QStringList{QStringLiteral("bin/helper.dll")});
    const QVariantList capabilities = security.value(QStringLiteral("capabilities")).toList();
    CHECK(namesOf(capabilities) ==
          QStringList{QStringLiteral("network"), QStringLiteral("files"), QStringLiteral("processes")});
    for (const QVariant& capability : capabilities) {
        CHECK_FALSE(capability.toMap().value(QStringLiteral("declared")).toBool());
        CHECK_FALSE(capability.toMap().value(QStringLiteral("title")).toString().isEmpty());
    }
    REQUIRE_FALSE(capabilities.isEmpty());
    CHECK(capabilities.first().toMap().value(QStringLiteral("functions")).toStringList() ==
          QStringList{QStringLiteral("ws2_32.dll!connect")});
    CHECK(security.value(QStringLiteral("warnings")).toStringList().size() == 3);

    const fs::path asking = fixture.pack(
        "asking",
        nativeManifest("alice.asking",
                       R"(, "permissions": [{"name": "network", "reason": "Talks to its server.", "hosts": ["api.example.com"]}])"),
        {.imports = {{"ws2_32.dll", {"connect"}}, {"kernel32.dll", {"CreateFileW"}}}});
    const QVariantMap asked = host.inspect(textOf(asking));
    INFO(codesOf(asked));
    CHECK(asked.value(QStringLiteral("ok")).toBool());
    const QVariantList declared = asked.value(QStringLiteral("security")).toMap().value(QStringLiteral("capabilities")).toList();
    REQUIRE(declared.size() == 2);
    CHECK(declared[0].toMap().value(QStringLiteral("declared")).toBool());
    CHECK_FALSE(declared[1].toMap().value(QStringLiteral("declared")).toBool());
    CHECK(asked.value(QStringLiteral("security")).toMap().value(QStringLiteral("warnings")).toStringList().size() == 1);

    const fs::path plain = fixture.scratch / "plain";
    writeFile(plain / "manifest.json",
              R"({"schemaVersion": 1, "id": "alice.glow", "version": "1.0.0", "name": "Glow", "license": "MIT",
                  "requires": {"filter": "^1.0"}, "contributes": {"filters": [{"name": "Glow", "file": "shaders/glow.hlsl"}]}})");
    writeFile(plain / "shaders/glow.hlsl", kShader);
    const fs::path filter = fixture.scratch / "plain.gsext";
    REQUIRE(gs::ext::packDirectory(plain, filter).empty());
    const QVariantMap ordinary = host.inspect(textOf(filter));
    CHECK(ordinary.value(QStringLiteral("ok")).toBool());
    CHECK_FALSE(ordinary.contains(QStringLiteral("security")));
}

TEST_CASE("ADR-0112: a restricted DLL shows as restricted in the install dialog: what it imports is blocked, not a "
          "missing permission, and an unsigned one is still unsigned")
{
    const Fixture fixture;
    const PeLayout layout{.imports = {{"ws2_32.dll", {"connect"}},
                                      {"kernel32.dll", {"CreateFileW"}},
                                      {"shell32.dll", {"ShellExecuteW"}}}};
    const std::string restricted =
        R"({"schemaVersion": 1, "id": "alice.restricted", "version": "1.0.0", "name": "Helper", "license": "MIT",
            "requires": {"native": "^1.0"},
            "contributes": {"native": {"dll": "bin/helper.dll", "sandbox": "restricted"}},
            "permissions": [{"name": "hardware", "reason": "Talks to its board."}]})";
    const fs::path archive = fixture.pack("restricted", restricted, layout);
    const fs::path full = fixture.pack("full", nativeManifest("alice.full"), layout);
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.setScanner(verdict(ScanVerdict::Clean));
    host.start(false);

    const QVariantMap info = host.inspect(textOf(archive));
    INFO(codesOf(info));
    CHECK(info.value(QStringLiteral("ok")).toBool());
    REQUIRE(info.contains(QStringLiteral("security")));
    const QVariantMap security = info.value(QStringLiteral("security")).toMap();
    CHECK(security.value(QStringLiteral("native")).toBool());
    CHECK(security.value(QStringLiteral("restricted")).toBool());
    CHECK(security.value(QStringLiteral("author")).toString() == QStringLiteral("unsigned"));
    CHECK_FALSE(security.value(QStringLiteral("refused")).toBool());
    const QVariantList capabilities = security.value(QStringLiteral("capabilities")).toList();
    CHECK(namesOf(capabilities) ==
          QStringList{QStringLiteral("network"), QStringLiteral("files"), QStringLiteral("processes")});
    for (const QVariant& capability : capabilities) {
        CHECK(capability.toMap().value(QStringLiteral("blocked")).toBool());
    }
    CHECK(security.value(QStringLiteral("warnings")).toStringList().isEmpty());

    const QVariantMap trusted = host.inspect(textOf(full)).value(QStringLiteral("security")).toMap();
    CHECK_FALSE(trusted.value(QStringLiteral("restricted")).toBool());
    CHECK(trusted.value(QStringLiteral("warnings")).toStringList().size() == 3);
    for (const QVariant& capability : trusted.value(QStringLiteral("capabilities")).toList()) {
        CHECK_FALSE(capability.toMap().value(QStringLiteral("blocked")).toBool());
    }
}

TEST_CASE("native code that reaches into other programs is refused when inspected and when installed")
{
    ApplicationScope application;
    const Fixture fixture;
    const fs::path archive = fixture.pack("inject", nativeManifest("alice.inject"),
                                          {.imports = {{"kernel32.dll", {"OpenProcess", "WriteProcessMemory"}}}});
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.setScanner(verdict(ScanVerdict::Clean));
    host.start(false);

    const QVariantMap info = host.inspect(textOf(archive));
    CHECK_FALSE(info.value(QStringLiteral("ok")).toBool());
    CHECK(codesOf(info).find("native-refused") != std::string::npos);
    const QVariantMap security = info.value(QStringLiteral("security")).toMap();
    CHECK(security.value(QStringLiteral("refused")).toBool());
    CHECK_FALSE(security.value(QStringLiteral("reason")).toString().isEmpty());
    CHECK(security.value(QStringLiteral("detail")).toString() ==
          QStringLiteral("bin/helper.dll: kernel32.dll!WriteProcessMemory"));
    CHECK(namesOf(security.value(QStringLiteral("capabilities")).toList()).contains(QStringLiteral("injection")));

    InstallWatch watch(host);
    host.installPackage(textOf(archive));
    REQUIRE(watch.waitFor(1));
    const QVariantMap refused = watch.results().first();
    CHECK_FALSE(refused.value(QStringLiteral("ok")).toBool());
    CHECK(refused.value(QStringLiteral("id")).toString() == QStringLiteral("alice.inject"));
    CHECK(codesOf(refused) == "native-refused ");
    CHECK_FALSE(refused.value(QStringLiteral("problems")).toList().first().toMap().value(QStringLiteral("hint")).toString().isEmpty());
    CHECK_FALSE(host.installing());
    CHECK_FALSE(fs::exists(fixture.user / "alice.inject"));
    CHECK(host.entries().isEmpty());

    const gs::ext::InstalledExtension direct = host.install(archive);
    CHECK_FALSE(direct.ok());
    CHECK(reports(direct.diagnostics, gs::ext::Code::NativeRefused));
    CHECK_FALSE(fs::exists(fixture.user / "alice.inject"));
}

TEST_CASE("a threat found by the scan refuses native code, and a scan that cannot run only warns")
{
    const Fixture fixture;
    const fs::path archive =
        fixture.pack("quiet", nativeManifest("alice.quiet"), {.imports = {{"kernel32.dll", {"GetTickCount"}}}});
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);

    host.setScanner(verdict(ScanVerdict::Threat));
    const QVariantMap threat = host.inspect(textOf(archive));
    CHECK_FALSE(threat.value(QStringLiteral("ok")).toBool());
    const QVariantMap found = threat.value(QStringLiteral("security")).toMap();
    CHECK(found.value(QStringLiteral("refused")).toBool());
    CHECK(found.value(QStringLiteral("scan")).toString() == QStringLiteral("threat"));
    CHECK(found.value(QStringLiteral("detail")).toString() == QStringLiteral("bin/helper.dll"));
    CHECK_FALSE(host.install(archive).ok());
    CHECK_FALSE(fs::exists(fixture.user / "alice.quiet"));

    host.setScanner(verdict(ScanVerdict::Unavailable));
    const QVariantMap unscanned = host.inspect(textOf(archive));
    CHECK(unscanned.value(QStringLiteral("ok")).toBool());
    const QVariantMap warned = unscanned.value(QStringLiteral("security")).toMap();
    CHECK_FALSE(warned.value(QStringLiteral("refused")).toBool());
    CHECK(warned.value(QStringLiteral("scan")).toString() == QStringLiteral("unavailable"));
    CHECK(warned.value(QStringLiteral("warnings")).toStringList().size() == 1);
    CHECK(warned.value(QStringLiteral("capabilities")).toList().isEmpty());

    host.setScanner(verdict(ScanVerdict::Clean));
    const QVariantMap clean = host.inspect(textOf(archive));
    CHECK(clean.value(QStringLiteral("ok")).toBool());
    CHECK(clean.value(QStringLiteral("security")).toMap().value(QStringLiteral("warnings")).toStringList().isEmpty());
    const gs::ext::InstalledExtension installed = host.install(archive);
    INFO(listed(installed.diagnostics));
    CHECK(installed.ok());
    CHECK(fs::exists(fixture.user / "alice.quiet/1.0.0/bin/helper.dll"));
    CHECK_FALSE(settings.extensions().contains("alice.quiet"));
}

TEST_CASE("a signed update shows its author key, and a new key or a lost signature is called out against the installed one")
{
    const Fixture fixture;
    const fs::path first = fixture.signedPackage("first", kFirstManifest, kFirstSums, kFirstSignature);
    const fs::path second = fixture.signedPackage("second", kSecondManifest, kSecondSums, kSecondSignature);
    std::string bare(kSecondManifest);
    bare.replace(bare.find("\"authorKey\""), std::string_view("\"authorKey\": \"RWSId2ZVRDMiEa7DCLu1nxR+gMiGsuYNvpyJdwItkg7PqsjnQoi009w+\", ").size(), "");
    const fs::path unsignedUpdate = fixture.signedPackage("bare", bare, {}, {});
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.setScanner(verdict(ScanVerdict::Clean));
    host.start(false);

    const QVariantMap fresh = host.inspect(textOf(first));
    INFO(codesOf(fresh));
    CHECK(fresh.value(QStringLiteral("ok")).toBool());
    const QVariantMap freshSecurity = fresh.value(QStringLiteral("security")).toMap();
    CHECK(freshSecurity.value(QStringLiteral("author")).toString() == QStringLiteral("valid"));
    CHECK(freshSecurity.value(QStringLiteral("keyId")).toString() == QStringLiteral("C1D2E3F4A5B60718"));
    CHECK_FALSE(freshSecurity.value(QStringLiteral("keyChanged")).toBool());
    const gs::ext::InstalledExtension installed = host.install(first);
    INFO(listed(installed.diagnostics));
    REQUIRE(installed.ok());

    const QVariantMap again = host.inspect(textOf(first));
    CHECK_FALSE(again.value(QStringLiteral("security")).toMap().value(QStringLiteral("keyChanged")).toBool());

    const QVariantMap rekeyed = host.inspect(textOf(second));
    INFO(codesOf(rekeyed));
    CHECK_FALSE(rekeyed.value(QStringLiteral("ok")).toBool());
    CHECK(codesOf(rekeyed).find("publisher-changed") != std::string::npos);
    const QVariantMap rekeyedSecurity = rekeyed.value(QStringLiteral("security")).toMap();
    CHECK(rekeyedSecurity.value(QStringLiteral("author")).toString() == QStringLiteral("valid"));
    CHECK(rekeyedSecurity.value(QStringLiteral("keyId")).toString() == QStringLiteral("1122334455667788"));
    CHECK(rekeyedSecurity.value(QStringLiteral("keyChanged")).toBool());
    CHECK(rekeyedSecurity.value(QStringLiteral("previousKeyId")).toString() == QStringLiteral("C1D2E3F4A5B60718"));
    CHECK(rekeyedSecurity.value(QStringLiteral("warnings")).toStringList().isEmpty());
    const QVariantMap rekeyedPublisher = rekeyed.value(QStringLiteral("publisher")).toMap();
    CHECK(rekeyedPublisher.value(QStringLiteral("state")).toString() == QStringLiteral("changed"));
    CHECK(rekeyedPublisher.value(QStringLiteral("previousKeyId")).toString() == QStringLiteral("C1D2E3F4A5B60718"));
    const gs::ext::InstalledExtension refusedUpdate = host.install(second);
    CHECK_FALSE(refusedUpdate.ok());
    CHECK(reports(refusedUpdate.diagnostics, gs::ext::Code::PublisherChanged));
    CHECK_FALSE(fs::exists(fixture.user / "alice.signed/2.0.0"));

    const QVariantMap stripped = host.inspect(textOf(unsignedUpdate));
    INFO(codesOf(stripped));
    CHECK_FALSE(stripped.value(QStringLiteral("ok")).toBool());
    CHECK(codesOf(stripped).find("signature-missing") != std::string::npos);
    const QVariantMap strippedSecurity = stripped.value(QStringLiteral("security")).toMap();
    CHECK(strippedSecurity.value(QStringLiteral("author")).toString() == QStringLiteral("unsigned"));
    CHECK(strippedSecurity.value(QStringLiteral("keyChanged")).toBool());
    CHECK(stripped.value(QStringLiteral("publisher")).toMap().value(QStringLiteral("state")).toString() ==
          QStringLiteral("unsigned"));
    CHECK_FALSE(host.install(unsignedUpdate).ok());

    const fs::path tampered = fixture.scratch / "signed-tampered";
    writeFile(tampered / "manifest.json", kSecondManifest);
    writeFile(tampered / "shaders/glow.hlsl", kShader);
    writeFile(tampered / "bin/helper.dll", buildPe({.imports = {{"kernel32.dll", {"GetTickCount"}}}}));
    writeFile(tampered / "SHA512SUMS", kSecondSums);
    writeFile(tampered / "SHA512SUMS.minisig", kSecondSignature);
    const fs::path forged = fixture.scratch / "tampered.gsext";
    REQUIRE(gs::ext::packDirectory(tampered, forged).empty());
    const QVariantMap broken = host.inspect(textOf(forged));
    CHECK_FALSE(broken.value(QStringLiteral("ok")).toBool());
    CHECK(codesOf(broken).find("signature-invalid") == std::string::npos);
    const QVariantMap brokenSecurity = broken.value(QStringLiteral("security")).toMap();
    CHECK(brokenSecurity.value(QStringLiteral("author")).toString() == QStringLiteral("invalid"));
    CHECK(brokenSecurity.value(QStringLiteral("refused")).toBool());
    CHECK(brokenSecurity.value(QStringLiteral("detail")).toString() ==
          QStringLiteral("bin/helper.dll was changed after signing"));
}

TEST_CASE("a bundle with refused native code cannot be installed and names the package")
{
    ApplicationScope application;
    const Fixture fixture;
    const fs::path folder = fixture.folder("inject", nativeManifest("alice.inject"),
                                           {.imports = {{"kernel32.dll", {"CreateRemoteThread"}}}});
    gs::ext::Bundle bundle;
    bundle.name = "Risky";
    gs::ext::BundleEntry entry;
    entry.id = "alice.inject";
    entry.version = gs::ext::Version{1, 0, 0};
    bundle.extensions.push_back(entry);
    const fs::path staging = fixture.scratch / "bundle";
    const fs::path inner = staging / pathOf(gs::ext::bundlePackagePath(entry));
    fs::create_directories(inner.parent_path());
    REQUIRE(gs::ext::packDirectory(folder, inner).empty());
    const fs::path archive = fixture.scratch / "risky.gsbundle";
    REQUIRE(gs::ext::packBundle(bundle, staging, archive).empty());
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.setScanner(verdict(ScanVerdict::Clean));
    host.start(false);

    const QVariantMap info = host.inspectBundle(textOf(archive));
    INFO(codesOf(info));
    CHECK_FALSE(info.value(QStringLiteral("ok")).toBool());
    CHECK(codesOf(info).find("native-refused") != std::string::npos);
    const QVariantList rows = info.value(QStringLiteral("extensions")).toList();
    REQUIRE(rows.size() == 1);
    const QVariantMap security = rows.first().toMap().value(QStringLiteral("security")).toMap();
    CHECK(security.value(QStringLiteral("native")).toBool());
    CHECK(security.value(QStringLiteral("refused")).toBool());

    InstallWatch watch(host);
    host.installBundle(textOf(archive));
    REQUIRE(watch.waitFor(1));
    const QVariantMap done = watch.results().first();
    INFO(codesOf(done));
    CHECK_FALSE(done.value(QStringLiteral("ok")).toBool());
    CHECK(codesOf(done).find("native-refused") != std::string::npos);
    CHECK_FALSE(fs::exists(fixture.user / "alice.inject"));
}

TEST_CASE("the Windows scan of a harmless buffer finds nothing or is not available")
{
    const ScanVerdict result = gs::app::scanWithAmsi("GameScrcpy test text", "note.txt");
    CHECK((result == ScanVerdict::Clean || result == ScanVerdict::Unavailable));
}

namespace {

gs::ext::MinisignSecretKey testKey(std::uint8_t number)
{
    gs::ext::Ed25519Seed seed{};
    seed.fill(number);
    gs::ext::MinisignKeyId id{};
    id.fill(number);
    return gs::ext::makeMinisignKey(seed, id);
}

std::string themeManifest(std::string_view id, std::string_view version, std::string_view more)
{
    return R"({"schemaVersion": 1, "id": ")" + std::string(id) + R"(", "version": ")" + std::string(version) +
           R"(", "name": "Theme", "license": "MIT", "requires": {"theme": "^1.0"},
               "contributes": {"themes": [{"name": "Ember", "dark": {"accent": "#FF6B3D"}}]})" +
           std::string(more) + "}";
}

fs::path themePackage(const Fixture& fixture, std::string_view stem, const std::string& manifestText,
                      const gs::ext::MinisignSecretKey* key)
{
    const fs::path folder = fixture.scratch / ("theme-" + std::string(stem));
    writeFile(folder / "manifest.json", manifestText);
    const fs::path archive = fixture.scratch / (std::string(stem) + ".gsext");
    const std::vector<gs::ext::Diagnostic> problems =
        key != nullptr ? gs::ext::packSigned(folder, archive, *key, "test") : gs::ext::packDirectory(folder, archive);
    INFO(listed(problems));
    REQUIRE(problems.empty());
    return archive;
}

QVariantMap entryOf(const ExtensionHost& host, const QString& id)
{
    const QVariantList entries = host.entries();
    for (const QVariant& entry : entries) {
        if (entry.toMap().value(QStringLiteral("id")).toString() == id) {
            return entry.toMap();
        }
    }
    return {};
}

}

TEST_CASE("ADR-0112: the Extensions page names the verified publisher with its key, or says an extension is "
          "unsigned, and removing it forgets the key")
{
    const Fixture fixture;
    const gs::ext::MinisignSecretKey key = testKey(0x21);
    const gs::ext::MinisignKey publicKey = gs::ext::minisignPublicKey(key);
    const QString keyId = QString::fromStdString(gs::ext::minisignKeyId(publicKey));
    const std::string authorKey = R"(, "authors": ["Alice"], "authorKey": ")" + gs::ext::minisignKeyText(publicKey) + "\"";
    const fs::path signedTheme =
        themePackage(fixture, "signed", themeManifest("alice.ember", "1.0.0", authorKey), &key);
    const fs::path plainTheme = themePackage(fixture, "plain", themeManifest("bob.plain", "1.0.0", {}), nullptr);
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);

    const QVariantMap offered = host.inspect(textOf(signedTheme));
    INFO(codesOf(offered));
    CHECK(offered.value(QStringLiteral("ok")).toBool());
    CHECK_FALSE(offered.contains(QStringLiteral("security")));
    const QVariantMap offeredPublisher = offered.value(QStringLiteral("publisher")).toMap();
    CHECK(offeredPublisher.value(QStringLiteral("state")).toString() == QStringLiteral("verified"));
    CHECK(offeredPublisher.value(QStringLiteral("keyId")).toString() == keyId);
    CHECK(offeredPublisher.value(QStringLiteral("text")).toString() ==
          QStringLiteral("Verified publisher: Alice \u00B7 ") + keyId);

    REQUIRE(host.install(signedTheme).ok());
    REQUIRE(host.install(plainTheme).ok());
    CHECK(fs::exists(fixture.user / ".publishers/alice.ember.pub"));
    const QVariantMap signedEntry = entryOf(host, QStringLiteral("alice.ember"));
    REQUIRE_FALSE(signedEntry.isEmpty());
    const QVariantMap publisher = signedEntry.value(QStringLiteral("publisher")).toMap();
    CHECK(publisher.value(QStringLiteral("state")).toString() == QStringLiteral("verified"));
    CHECK(publisher.value(QStringLiteral("keyId")).toString() == keyId);
    CHECK(publisher.value(QStringLiteral("text")).toString() ==
          QStringLiteral("Verified publisher: Alice \u00B7 ") + keyId);
    const QVariantMap plainEntry = entryOf(host, QStringLiteral("bob.plain"));
    REQUIRE_FALSE(plainEntry.isEmpty());
    CHECK(plainEntry.value(QStringLiteral("publisher")).toMap().value(QStringLiteral("state")).toString() ==
          QStringLiteral("unsigned"));
    CHECK(plainEntry.value(QStringLiteral("publisher")).toMap().value(QStringLiteral("text")).toString() ==
          QStringLiteral("Unsigned"));

    const gs::ext::MinisignSecretKey other = testKey(0x22);
    const std::string otherKey =
        R"(, "authors": ["Alice"], "authorKey": ")" + gs::ext::minisignKeyText(gs::ext::minisignPublicKey(other)) + "\"";
    const fs::path rekeyed = themePackage(fixture, "rekeyed", themeManifest("alice.ember", "1.1.0", otherKey), &other);
    const QVariantMap refused = host.inspect(textOf(rekeyed));
    CHECK_FALSE(refused.value(QStringLiteral("ok")).toBool());
    CHECK(codesOf(refused).find("publisher-changed") != std::string::npos);
    CHECK_FALSE(host.install(rekeyed).ok());

    REQUIRE(host.remove(QStringLiteral("alice.ember"), QStringLiteral("1.0.0")));
    CHECK_FALSE(fs::exists(fixture.user / ".publishers/alice.ember.pub"));
    CHECK(host.inspect(textOf(rekeyed)).value(QStringLiteral("ok")).toBool());
    REQUIRE(host.install(rekeyed).ok());
    CHECK(entryOf(host, QStringLiteral("alice.ember"))
              .value(QStringLiteral("publisher"))
              .toMap()
              .value(QStringLiteral("keyId"))
              .toString() == QString::fromStdString(gs::ext::minisignKeyId(gs::ext::minisignPublicKey(other))));
}

TEST_CASE("ADR-0112: a native extension with no DLL for this processor is refused when installed and held back when "
          "found installed")
{
    const Fixture fixture;
    const bool armHost = gs::ext::hostArchitecture() == "arm64";
    const std::string elsewhere = armHost ? "x64" : "arm64";
    const std::uint16_t machine = armHost ? gs::ext::kMachineX64 : gs::ext::kMachineArm64;
    std::string manifestText = nativeManifest("alice.elsewhere");
    const std::string_view plainDll = "\"bin/helper.dll\"";
    manifestText.replace(manifestText.find(plainDll), plainDll.size(), "{\"" + elsewhere + "\": \"bin/helper.dll\"}");
    const fs::path archive =
        fixture.pack("elsewhere", manifestText, {.imports = {{"kernel32.dll", {"GetTickCount"}}}, .machine = machine});
    {
        AppSettings settings(fixture.data);
        ExtensionHost host(settings, {fixture.user, {}});
        host.setScanner(verdict(ScanVerdict::Clean));
        host.start(false);

        const QVariantMap info = host.inspect(textOf(archive));
        INFO(codesOf(info));
        CHECK_FALSE(info.value(QStringLiteral("ok")).toBool());
        CHECK(codesOf(info).find("native-refused") != std::string::npos);
        const QVariantMap security = info.value(QStringLiteral("security")).toMap();
        CHECK(security.value(QStringLiteral("refused")).toBool());
        const std::string_view here = gs::ext::hostArchitecture();
        CHECK(security.value(QStringLiteral("detail")).toString() ==
              QStringLiteral("no DLL for ") + QString::fromUtf8(here.data(), static_cast<qsizetype>(here.size())));
        CHECK(security.value(QStringLiteral("reason")).toString() ==
              QStringLiteral("This extension has no version for this computer."));
        CHECK_FALSE(host.install(archive).ok());
        CHECK_FALSE(fs::exists(fixture.user / "alice.elsewhere"));
    }

    const fs::path installed = fixture.user / "alice.elsewhere/1.0.0";
    writeFile(installed / "manifest.json", manifestText);
    writeFile(installed / "bin/helper.dll", buildPe({.machine = machine}));
    AppSettings settings(fixture.data);
    ExtensionHost found(settings, {fixture.user, {}});
    found.start(false);
    const QVariantMap entry = entryOf(found, QStringLiteral("alice.elsewhere"));
    REQUIRE_FALSE(entry.isEmpty());
    bool missing = false;
    for (const QVariant& problem : entry.value(QStringLiteral("problems")).toList()) {
        const QVariantMap map = problem.toMap();
        if (map.value(QStringLiteral("code")).toString() == QStringLiteral("native-architecture-missing")) {
            missing = true;
            CHECK(map.value(QStringLiteral("severity")).toString() == QStringLiteral("error"));
            CHECK(map.value(QStringLiteral("hint")).toString() ==
                  QStringLiteral("This extension has no version for this computer."));
        }
    }
    CHECK(missing);
    CHECK(entry.value(QStringLiteral("native")).toMap().value(QStringLiteral("architectures")).toStringList() ==
          QStringList{QString::fromStdString(elsewhere)});
    CHECK_FALSE(entry.value(QStringLiteral("active")).toBool());
}
