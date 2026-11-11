#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gs::ext {

enum class Severity : std::uint8_t { Error, Warning, Notice };

enum class Code : std::uint8_t {
    TooLarge,
    MalformedJson,
    NotAnObject,
    MissingField,
    WrongType,
    UnknownField,
    TooLong,
    TooMany,
    OutOfRange,
    Duplicate,
    SchemaVersion,
    IdInvalid,
    IdReserved,
    VersionInvalid,
    RangeInvalid,
    UrlInvalid,
    PathInvalid,
    ExtensionPointUnknown,
    ContributionUndeclared,
    FilterFileInvalid,
    FilterDirectiveInvalid,
    FilterPassMissing,
    FilterNameReserved,
    ThemeTokenInvalid,
    ThemeColorInvalid,
    SettingKeyInvalid,
    SettingChoicesMissing,
    SettingRangeInvalid,
    SettingDefaultInvalid,
    SettingFieldUnused,
    PermissionUnknown,
    PermissionNeverGranted,
    PermissionReasonMissing,
    PermissionNotAllowed,
    PermissionHostsMissing,
    PermissionHostInvalid,
    PermissionAnyHost,
    PermissionPathsMissing,
    PermissionFieldUnused,
    DependencyMissing,
    DependencyVersion,
    DependencyConflict,
    DependencyCycle,
    DependencyOptionalMissing,
    HostApiMissing,
    HostApiVersion,
    ArchiveUnreadable,
    ArchiveTooManyEntries,
    ArchiveEntryTooLarge,
    ArchiveTooLarge,
    ArchiveUnsafePath,
    ArchiveLink,
    ArchiveEncrypted,
    ArchiveNameClash,
    ArchiveCorrupt,
    ArchiveWriteFailed,
    ManifestMissing,
    FileMissing,
    FilterSettingMismatch,
    InstallMismatch,
    TextureUnreadable,
    FilterCompileFailed,
    ConsentRequired,
    BundleIndexMissing,
    BundlePackageMissing,
    BundlePackageMismatch,
    BundleBuiltinMissing,
    ProfileNameInvalid,
    ProfileTargetInvalid,
    ProfileInvalid,
    PatchInvalid,
    PatchTargetMissing,
    NativeDllInvalid,
    NativeNameInvalid,
    NativeTypeInvalid,
    NativePermissionUndeclared,
    NativeLoadFailed,
    NativeMismatch,
    NativeCrashed,
    AuthorKeyInvalid,
    NativeRefused,
    ScriptNameInvalid,
    ScriptEntryInvalid,
    PanelNameInvalid,
    NativeParamInvalid,
    NativeParamUnused,
    NativeTargetUnknown,
    NativeModuleFailed,
    TourTargetInvalid,
    SpeechEngineInvalid,
    InputNameInvalid,
    ServiceSchemaInvalid,
    ServiceTargetInvalid,
    NativeArchitectureUnknown,
    NativeArchitectureMissing,
    NativeMachineMismatch,
    NativeEntryMissing,
    SignatureInvalid,
    SignatureMissing,
    PublisherChanged,
    SigningKeyMismatch,
    CliCommandInvalid,
    ConverterInvalid,
    AiProviderInvalid,
};

struct Diagnostic {
    Severity severity = Severity::Error;
    Code code = Code::WrongType;
    std::string path;
    std::string detail;

    friend bool operator==(const Diagnostic&, const Diagnostic&) = default;
};

[[nodiscard]] std::string_view codeName(Code code) noexcept;
[[nodiscard]] std::string_view fixHint(Code code) noexcept;
[[nodiscard]] std::string_view severityName(Severity severity) noexcept;
[[nodiscard]] bool hasErrors(const std::vector<Diagnostic>& diagnostics) noexcept;

}
