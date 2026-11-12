#pragma once

#include "gs/ext/Archive.h"
#include "gs/ext/Diagnostics.h"
#include "gs/ext/Manifest.h"
#include "gs/ext/Minisign.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::ext {

inline constexpr std::string_view kChecksumsFile = "SHA512SUMS";
inline constexpr std::string_view kChecksumsSignatureFile = "SHA512SUMS.minisig";
inline constexpr std::string_view kNativeEntryName = "gs_ext_main";
inline constexpr std::string_view kOldNativeEntryName = "gs_ext_entry";
inline constexpr std::uint16_t kMachineX64 = 0x8664;
inline constexpr std::uint16_t kMachineArm64 = 0xAA64;

struct ImportedFunction {
    std::string module;
    std::string name;
    std::uint16_t ordinal = 0;
    bool byOrdinal = false;
    bool delayed = false;

    friend bool operator==(const ImportedFunction&, const ImportedFunction&) = default;
};

struct PeImports {
    bool valid = false;
    bool wide = false;
    std::uint16_t machine = 0;
    std::vector<ImportedFunction> functions;
    std::string problem;
};

enum class Capability : std::uint8_t { Network, Files, Input, Processes, Injection };
inline constexpr std::array<std::string_view, 5> kCapabilityNames{"network", "files", "input", "processes",
                                                                  "injection"};

struct CapabilityUse {
    Capability capability = Capability::Network;
    std::vector<std::string> functions;

    friend bool operator==(const CapabilityUse&, const CapabilityUse&) = default;
};

enum class SignatureState : std::uint8_t { Unsigned, Valid, Invalid };

struct SignatureCheck {
    SignatureState state = SignatureState::Unsigned;
    std::optional<MinisignKey> key;
    std::string problem;
};

struct PackageContents {
    std::vector<ArchiveEntry> files;
    std::vector<Diagnostic> problems;
    std::function<std::optional<std::string>(const std::string& path)> read;
};

enum class ScanVerdict : std::uint8_t { Skipped, Clean, Unavailable, Blocked, Threat };
using NativeScanner = std::function<ScanVerdict(std::string_view bytes, std::string_view path)>;

enum class NativeRefusal : std::uint8_t { None, Injection, Threat, Blocked, BadSignature, Unreadable, Architecture };

struct PeExports {
    bool valid = false;
    std::uint16_t machine = 0;
    std::vector<std::string> names;
    std::string problem;
};

struct NativeLibrary {
    std::string architecture;
    std::string path;
    std::uint16_t machine = 0;
    bool entry = false;
    bool oldEntry = false;

    friend bool operator==(const NativeLibrary&, const NativeLibrary&) = default;
};

struct LibraryCheck {
    std::vector<NativeLibrary> libraries;
    std::vector<Diagnostic> diagnostics;
};

struct NativeFile {
    std::string path;
    PeImports imports;
    ScanVerdict scan = ScanVerdict::Skipped;
};

struct NativeCheck {
    bool native = false;
    std::vector<NativeFile> files;
    std::vector<CapabilityUse> capabilities;
    std::vector<Capability> uncovered;
    SignatureCheck signature;
    ScanVerdict scan = ScanVerdict::Skipped;
    NativeRefusal refusal = NativeRefusal::None;
    std::string detail;
    bool restricted = false;

    [[nodiscard]] bool refused() const noexcept { return refusal != NativeRefusal::None; }
};

[[nodiscard]] bool looksLikePe(std::string_view bytes) noexcept;
[[nodiscard]] PeImports readPeImports(std::string_view image);
[[nodiscard]] PeExports readPeExports(std::string_view image);
[[nodiscard]] std::string machineName(std::uint16_t machine);
[[nodiscard]] std::uint16_t architectureMachine(std::string_view architecture) noexcept;
[[nodiscard]] std::string_view capabilityName(Capability capability) noexcept;
[[nodiscard]] std::vector<CapabilityUse> classifyImports(const std::vector<ImportedFunction>& functions);
[[nodiscard]] std::vector<Capability> uncoveredCapabilities(const std::vector<CapabilityUse>& uses,
                                                            const std::vector<Permission>& permissions);
[[nodiscard]] PackageContents archiveContents(const std::filesystem::path& archive);
[[nodiscard]] PackageContents directoryContents(const std::filesystem::path& directory);
[[nodiscard]] SignatureCheck checkSignature(const PackageContents& contents, std::string_view authorKey);
[[nodiscard]] SignatureCheck checkSignedSums(const PackageContents& contents, std::string_view authorKey);
[[nodiscard]] bool authorChanged(const SignatureCheck& before, const SignatureCheck& after) noexcept;
[[nodiscard]] bool hasNativeCode(const PackageContents& contents, const Manifest& manifest);
[[nodiscard]] NativeCheck checkNative(const PackageContents& contents, const Manifest& manifest,
                                      const NativeScanner& scanner = {},
                                      std::string_view architecture = hostArchitecture());
[[nodiscard]] LibraryCheck checkNativeLibraries(const PackageContents& contents, const Manifest& manifest);

}
