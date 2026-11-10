#include "gs/ext/Diagnostics.h"

#include <algorithm>

namespace gs::ext {

std::string_view codeName(Code code) noexcept
{
    switch (code) {
    case Code::TooLarge:
        return "too-large";
    case Code::MalformedJson:
        return "malformed-json";
    case Code::NotAnObject:
        return "not-an-object";
    case Code::MissingField:
        return "missing-field";
    case Code::WrongType:
        return "wrong-type";
    case Code::UnknownField:
        return "unknown-field";
    case Code::TooLong:
        return "too-long";
    case Code::TooMany:
        return "too-many";
    case Code::OutOfRange:
        return "out-of-range";
    case Code::Duplicate:
        return "duplicate";
    case Code::SchemaVersion:
        return "schema-version";
    case Code::IdInvalid:
        return "id-invalid";
    case Code::IdReserved:
        return "id-reserved";
    case Code::VersionInvalid:
        return "version-invalid";
    case Code::RangeInvalid:
        return "range-invalid";
    case Code::UrlInvalid:
        return "url-invalid";
    case Code::PathInvalid:
        return "path-invalid";
    case Code::ExtensionPointUnknown:
        return "extension-point-unknown";
    case Code::ContributionUndeclared:
        return "contribution-undeclared";
    case Code::FilterFileInvalid:
        return "filter-file-invalid";
    case Code::FilterDirectiveInvalid:
        return "filter-directive-invalid";
    case Code::FilterPassMissing:
        return "filter-pass-missing";
    case Code::FilterNameReserved:
        return "filter-name-reserved";
    case Code::ThemeTokenInvalid:
        return "theme-token-invalid";
    case Code::ThemeColorInvalid:
        return "theme-color-invalid";
    case Code::SettingKeyInvalid:
        return "setting-key-invalid";
    case Code::SettingChoicesMissing:
        return "setting-choices-missing";
    case Code::SettingRangeInvalid:
        return "setting-range-invalid";
    case Code::SettingDefaultInvalid:
        return "setting-default-invalid";
    case Code::SettingFieldUnused:
        return "setting-field-unused";
    case Code::PermissionUnknown:
        return "permission-unknown";
    case Code::PermissionNeverGranted:
        return "permission-never-granted";
    case Code::PermissionReasonMissing:
        return "permission-reason-missing";
    case Code::PermissionNotAllowed:
        return "permission-not-allowed";
    case Code::PermissionHostsMissing:
        return "permission-hosts-missing";
    case Code::PermissionHostInvalid:
        return "permission-host-invalid";
    case Code::PermissionAnyHost:
        return "permission-any-host";
    case Code::PermissionPathsMissing:
        return "permission-paths-missing";
    case Code::PermissionFieldUnused:
        return "permission-field-unused";
    case Code::DependencyMissing:
        return "dependency-missing";
    case Code::DependencyVersion:
        return "dependency-version";
    case Code::DependencyConflict:
        return "dependency-conflict";
    case Code::DependencyCycle:
        return "dependency-cycle";
    case Code::DependencyOptionalMissing:
        return "dependency-optional-missing";
    case Code::HostApiMissing:
        return "host-api-missing";
    case Code::HostApiVersion:
        return "host-api-version";
    case Code::ArchiveUnreadable:
        return "archive-unreadable";
    case Code::ArchiveTooManyEntries:
        return "archive-too-many-entries";
    case Code::ArchiveEntryTooLarge:
        return "archive-entry-too-large";
    case Code::ArchiveTooLarge:
        return "archive-too-large";
    case Code::ArchiveUnsafePath:
        return "archive-unsafe-path";
    case Code::ArchiveLink:
        return "archive-link";
    case Code::ArchiveEncrypted:
        return "archive-encrypted";
    case Code::ArchiveNameClash:
        return "archive-name-clash";
    case Code::ArchiveCorrupt:
        return "archive-corrupt";
    case Code::ArchiveWriteFailed:
        return "archive-write-failed";
    case Code::ManifestMissing:
        return "manifest-missing";
    case Code::FileMissing:
        return "file-missing";
    case Code::FilterSettingMismatch:
        return "filter-setting-mismatch";
    case Code::InstallMismatch:
        return "install-mismatch";
    case Code::TextureUnreadable:
        return "texture-unreadable";
    case Code::FilterCompileFailed:
        return "filter-compile-failed";
    case Code::ConsentRequired:
        return "consent-required";
    case Code::BundleIndexMissing:
        return "bundle-index-missing";
    case Code::BundlePackageMissing:
        return "bundle-package-missing";
    case Code::BundlePackageMismatch:
        return "bundle-package-mismatch";
    case Code::BundleBuiltinMissing:
        return "bundle-builtin-missing";
    case Code::ProfileNameInvalid:
        return "profile-name-invalid";
    case Code::ProfileTargetInvalid:
        return "profile-target-invalid";
    case Code::ProfileInvalid:
        return "profile-invalid";
    case Code::PatchInvalid:
        return "patch-invalid";
    case Code::PatchTargetMissing:
        return "patch-target-missing";
    case Code::NativeDllInvalid:
        return "native-dll-invalid";
    case Code::NativeNameInvalid:
        return "native-name-invalid";
    case Code::NativeTypeInvalid:
        return "native-type-invalid";
    case Code::NativePermissionUndeclared:
        return "native-permission-undeclared";
    case Code::NativeLoadFailed:
        return "native-load-failed";
    case Code::NativeMismatch:
        return "native-mismatch";
    case Code::NativeCrashed:
        return "native-crashed";
    case Code::AuthorKeyInvalid:
        return "author-key-invalid";
    case Code::NativeRefused:
        return "native-refused";
    case Code::ScriptNameInvalid:
        return "script-name-invalid";
    case Code::ScriptEntryInvalid:
        return "script-entry-invalid";
    case Code::PanelNameInvalid:
        return "panel-name-invalid";
    case Code::NativeParamInvalid:
        return "native-param-invalid";
    case Code::NativeParamUnused:
        return "native-param-unused";
    case Code::NativeTargetUnknown:
        return "native-target-unknown";
    case Code::NativeModuleFailed:
        return "native-module-failed";
    }
    return "unknown";
}

std::string_view fixHint(Code code) noexcept
{
    switch (code) {
    case Code::TooLarge:
        return "Keep manifest.json under 256 KiB and nest objects at most 32 levels deep.";
    case Code::MalformedJson:
        return "Make manifest.json valid JSON (UTF-8, double-quoted keys, no trailing commas or comments).";
    case Code::NotAnObject:
        return "Make the top level of manifest.json, and every place the schema expects one, a JSON object.";
    case Code::MissingField:
        return "Add the required field named in the path.";
    case Code::WrongType:
        return "Give the field the JSON type the schema expects.";
    case Code::UnknownField:
        return "Remove the field or fix its spelling; it is ignored.";
    case Code::TooLong:
        return "Shorten the text to the maximum length the schema gives.";
    case Code::TooMany:
        return "Keep the list within the maximum number of items the schema gives.";
    case Code::OutOfRange:
        return "Use a value within the range the schema gives.";
    case Code::Duplicate:
        return "Give each entry a name or key used only once.";
    case Code::SchemaVersion:
        return "Set \"schemaVersion\" to 1; a newer schema needs a newer GameScrcpy.";
    case Code::IdInvalid:
        return "Write the id as \"author.name\": lowercase letters, digits and hyphens in at least two dot-separated "
               "parts, 64 characters at most, such as \"alice.crt-filter\".";
    case Code::IdReserved:
        return "Ids starting with \"gamescrcpy.\" are reserved for official extensions; use your own author name.";
    case Code::VersionInvalid:
        return "Write a semantic version MAJOR.MINOR.PATCH, optionally with a pre-release tag, such as \"1.2.0\" or "
               "\"2.0.0-beta.1\".";
    case Code::RangeInvalid:
        return "Write a version range such as \"^1.2\", \"~1.2.3\", \"1.2.3\", \">=1.0 <3\" or \"*\".";
    case Code::UrlInvalid:
        return "Use an https:// address.";
    case Code::PathInvalid:
        return "Use a relative path inside the package with forward slashes, without \"..\" or a drive letter.";
    case Code::ExtensionPointUnknown:
        return "Use one of the extension points: profile, theme, filter, script, command, preset, native, component, "
               "task.";
    case Code::ContributionUndeclared:
        return "Declare the extension point of this contribution in \"requires\" with the interface version you build "
               "against.";
    case Code::FilterFileInvalid:
        return "Point \"file\" at an .hlsl shader inside the package.";
    case Code::FilterDirectiveInvalid:
        return "Write directives as \"//! PARAM <name> <float|int|bool> <default> [<min> <max> [<step>]]\", "
               "\"//! PASS <name> [scale=<ratio>] [format=rgba8|rgba16f]\" or \"//! TEXTURE <name> <file.png>\".";
    case Code::FilterPassMissing:
        return "Declare each pass with \"//! PASS <name>\" and define float4 <name>(float2 uv) for it.";
    case Code::FilterNameReserved:
        return "Choose another name: Original, Source, Linear, Point, SourceSize, OriginalSize, OutputSize, Frame "
               "and names starting with \"Gs\" belong to the host.";
    case Code::ThemeTokenInvalid:
        return "Use one of GameScrcpy's color token names, written in lowerCamelCase, such as \"bgBase\" or "
               "\"textPrimary\"; other names are ignored.";
    case Code::ThemeColorInvalid:
        return "Write colors as #RRGGBB or #AARRGGBB.";
    case Code::SettingKeyInvalid:
        return "Start the key with a letter and use letters and digits only, 64 characters at most.";
    case Code::SettingChoicesMissing:
        return "List the choices of a \"choice\" setting in \"choices\".";
    case Code::SettingRangeInvalid:
        return "Keep \"min\" at or below \"max\" and make \"step\" positive.";
    case Code::SettingDefaultInvalid:
        return "Give \"default\" a value of the setting's type, within its range, choices and maximum length.";
    case Code::SettingFieldUnused:
        return "Remove the field: it does not apply to this setting type.";
    case Code::PermissionUnknown:
        return "Use a permission from the list: device.input, device.screen, device.clipboard, device.apps, "
               "devices.other, mapping, notify, network, files.pick, files.paths, input.read, hardware, ai.";
    case Code::PermissionNeverGranted:
        return "adb.shell is never granted to extensions; use the device.* permissions for what you need.";
    case Code::PermissionReasonMissing:
        return "Say in \"reason\", in one sentence of at most 120 characters, why the extension needs the permission.";
    case Code::PermissionNotAllowed:
        return "Only code can ask for permissions: declare \"script\", \"component\", \"task\" or \"native\" in "
               "\"requires\", or remove the permissions.";
    case Code::PermissionHostsMissing:
        return "List the hosts the extension talks to in \"hosts\", such as \"api.example.com\" or \"*.example.com\".";
    case Code::PermissionHostInvalid:
        return "Write each host as a domain name, optionally starting with \"*.\" and ending with \":port\".";
    case Code::PermissionAnyHost:
        return "\"*\" lets the extension talk to any host; list the hosts it needs instead if you can.";
    case Code::PermissionPathsMissing:
        return "List the folders the extension reads or writes in \"paths\".";
    case Code::PermissionFieldUnused:
        return "Remove the field: it does not apply to this permission.";
    case Code::DependencyMissing:
        return "Install the extension this one depends on, or remove the dependency.";
    case Code::DependencyVersion:
        return "Install a version of the dependency that the version range accepts, or widen the range.";
    case Code::DependencyConflict:
        return "Extensions need versions of the same dependency that no single installed version satisfies; update "
               "one of them.";
    case Code::DependencyCycle:
        return "Break the dependency cycle: extensions cannot depend on each other in a loop.";
    case Code::DependencyOptionalMissing:
        return "The optional dependency is not available; the extension works without it.";
    case Code::HostApiMissing:
        return "This GameScrcpy does not offer the extension point; update GameScrcpy.";
    case Code::HostApiVersion:
        return "This GameScrcpy offers other interface versions of the extension point; update GameScrcpy or build "
               "against a version it offers.";
    case Code::ArchiveUnreadable:
        return "The file is not a readable zip archive; pack the extension again with \"gs pack\".";
    case Code::ArchiveTooManyEntries:
        return "Keep the package under 4096 files.";
    case Code::ArchiveEntryTooLarge:
        return "Keep every file in the package under 64 MiB.";
    case Code::ArchiveTooLarge:
        return "Keep the unpacked package under 256 MiB and its files no more than 100 times smaller packed than "
               "unpacked.";
    case Code::ArchiveUnsafePath:
        return "Use relative paths with forward slashes, without \"..\", drive letters, characters Windows forbids "
               "or reserved names such as CON or NUL, at most 260 bytes long with names of at most 255 bytes.";
    case Code::ArchiveLink:
        return "Put the files themselves into the package instead of links to them.";
    case Code::ArchiveEncrypted:
        return "Pack the extension without a password.";
    case Code::ArchiveNameClash:
        return "Give files and folders names that differ by more than letter case; Windows treats them as the same.";
    case Code::ArchiveCorrupt:
        return "The package is damaged or uses an unsupported compression; pack the extension again with \"gs pack\".";
    case Code::ArchiveWriteFailed:
        return "Check that the destination folder is writable and has free space.";
    case Code::ManifestMissing:
        return "Put manifest.json in the top folder of the extension.";
    case Code::FileMissing:
        return "Add the file to the extension or fix the path; paths start at the top folder and letter case must "
               "match.";
    case Code::FilterSettingMismatch:
        return "Give the setting the type of the filter parameter it controls: number or integer for float, integer "
               "for int, boolean for bool.";
    case Code::InstallMismatch:
        return "Install the extension from its package instead of copying folders; the folders are named after the "
               "id and version in its manifest.";
    case Code::TextureUnreadable:
        return "Save the texture as a PNG of at most 8192 by 8192 pixels.";
    case Code::FilterCompileFailed:
        return "Fix the HLSL error the compiler reports; its line numbers refer to your file.";
    case Code::ConsentRequired:
        return "This version asks for permissions or dependencies you have not allowed yet. Turn the extension on "
               "to allow them.";
    case Code::BundleIndexMissing:
        return "Put bundle.json at the root of the bundle.";
    case Code::BundlePackageMissing:
        return "Add the package at the path shown, or remove the extension from bundle.json.";
    case Code::BundlePackageMismatch:
        return "Pack the package whose id and version bundle.json names.";
    case Code::BundleBuiltinMissing:
        return "This bundle needs a built-in extension that this GameScrcpy does not have; update GameScrcpy or take "
               "the extension out of the bundle.";
    case Code::ProfileNameInvalid:
        return "Name the profile with letters, digits, hyphens and underscores, 64 characters at most; others refer "
               "to it as \"<extension id>/<name>\".";
    case Code::ProfileTargetInvalid:
        return "Write the target as \"<extension id>/<profile name>\", such as \"alice.shooter-pack/standard\".";
    case Code::ProfileInvalid:
        return "Make the file a valid gamescrcpy.profile document; the detail names the first problem.";
    case Code::PatchInvalid:
        return "Make the file a valid gamescrcpy.profile-patch document whose result is still a valid profile; the "
               "detail names the first problem.";
    case Code::PatchTargetMissing:
        return "The profile, control, command or state the patch changes is not there; check the target and the "
               "ids, or turn on the extension that provides the profile.";
    case Code::NativeDllInvalid:
        return "Point \"dll\" at a .dll file inside the package.";
    case Code::NativeNameInvalid:
        return "Start the name with a lowercase letter and use letters and digits only, 64 characters at most, such "
               "as \"now\" or \"readSensor\"; scripts call a node as ext.<extension>.<name>(...).";
    case Code::NativeTypeInvalid:
        return "Use one of the types boolean, number, string, point, region, color, duration and json.";
    case Code::NativePermissionUndeclared:
        return "List the permission the node needs in \"permissions\" with a reason, so the user can allow it.";
    case Code::NativeLoadFailed:
        return "The DLL could not be loaded or did not start as gamescrcpy_ext.h describes; build it as a 64-bit DLL "
               "against the same ABI version and put the DLLs it needs next to it.";
    case Code::NativeMismatch:
        return "Make gs_ext_entry offer the touch methods, nodes, attachments and behaviors the manifest lists, with "
               "the same names, types, slots, fingers and frames.";
    case Code::NativeCrashed:
        return "The DLL stopped working three times within a minute, so the extension was turned off; turn it on "
               "again on the Extensions page to try again, or update it.";
    case Code::AuthorKeyInvalid:
        return "Put the second line of your minisign public key file (56 characters starting with \"RW\") in "
               "\"authorKey\".";
    case Code::NativeRefused:
        return "The package is not installed: its native code reaches into other programs, a virus scan found a "
               "threat, it cannot be read, or its signature does not match its files.";
    case Code::ScriptNameInvalid:
        return "Name the script with lowercase letters, digits and hyphens, 32 characters at most, such as "
               "\"helper\" or \"auto-pick\"; panels refer to it by this name.";
    case Code::ScriptEntryInvalid:
        return "Point \"entry\" at a .ts or .js file inside the package, such as \"scripts/helper.ts\".";
    case Code::PanelNameInvalid:
        return "Name the panel with lowercase letters, digits and hyphens, 32 characters at most, such as \"stats\"; "
               "logic draws it with ui.panel(\"<name>\"). Only a panel shown in the right-click menu may be named "
               "\"menu\".";
    case Code::NativeParamInvalid:
        return "Give the parameter a default of its type within min and max (a number, a whole number, true or "
               "false, one of its options, a key name, [x, y], [x, y, r] or [x, y, w, h], a list of 2 to 16 [x, y] "
               "points with rising x, or #RRGGBB), keep min at or below max, step positive, and min above 0 for a "
               "log scale.";
    case Code::NativeParamUnused:
        return "Remove the field: it does not apply to this parameter type (min, max, step, unit and log are for "
               "numbers, options for choices, shape for regions).";
    case Code::NativeTargetUnknown:
        return "Use \"*\" or the types of the profile format, such as \"tap\", \"hold\", \"wasd\", \"camera\" or "
               "\"behavior\"; other names never match a component.";
    case Code::NativeModuleFailed:
        return "An attachment or behavior of the DLL failed three times within 10 seconds, so the extension was turned "
               "off; turn it on again on the Extensions page to try again, or update it.";
    }
    return "";
}

std::string_view severityName(Severity severity) noexcept
{
    switch (severity) {
    case Severity::Error:
        return "error";
    case Severity::Warning:
        return "warning";
    case Severity::Notice:
        return "notice";
    }
    return "error";
}

bool hasErrors(const std::vector<Diagnostic>& diagnostics) noexcept
{
    return std::ranges::any_of(diagnostics,
                               [](const Diagnostic& diagnostic) { return diagnostic.severity == Severity::Error; });
}

}
