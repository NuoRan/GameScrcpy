#pragma once

#include "gs/ext/Diagnostics.h"
#include "gs/ext/Version.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gs::ext {

inline constexpr std::int64_t kSchemaVersion = 1;
inline constexpr std::size_t kMaxManifestBytes = 256 * 1024;
inline constexpr std::size_t kMaxIdLength = 64;
inline constexpr std::size_t kMaxReasonLength = 120;
inline constexpr std::string_view kReservedIdPrefix = "gamescrcpy.";

enum class ExtensionPoint : std::uint8_t {
    Profile, Theme, Filter, Script, Command, Preset, Native, Component, Task, Panel, Help, Tour
};
inline constexpr std::array<std::string_view, 12> kExtensionPointNames{"profile", "theme",     "filter",
                                                                       "script",  "command",   "preset",
                                                                       "native",  "component", "task",
                                                                       "panel",   "help",      "tour"};

inline constexpr std::array<std::string_view, 8> kNativeTypeNames{"boolean", "number", "string",   "point",
                                                                  "region",  "color",  "duration", "json"};

inline constexpr std::array<std::string_view, 15> kPermissionNames{
    "device.input", "device.screen", "device.clipboard", "device.apps", "devices.other", "mapping",
    "notify",       "network",       "files.pick",       "files.paths", "input.read",    "hardware",
    "ai",           "devices.provide", "network.listen"};
inline constexpr std::size_t kMaxComponentNameLength = 48;
inline constexpr std::string_view kNeverGrantedPermission = "adb.shell";

enum class TextFormat : std::uint8_t {
    Plain,
    ExtensionId,
    SettingKey,
    PackagePath,
    HttpsUrl,
    TokenName,
    Color,
    PermissionName,
    ProfileName,
    ProfileTarget,
    NativeName,
    NativeType,
    SigningKey,
    ScriptName,
    PanelName
};

struct TextRule {
    bool required = false;
    std::size_t maxLength = 256;
    TextFormat format = TextFormat::Plain;
    Code missing = Code::MissingField;
};

struct ListRule {
    std::size_t maxItems = 64;
    std::size_t maxLength = 256;
    TextFormat format = TextFormat::Plain;
};

struct MapRule {
    std::size_t maxEntries = 256;
    TextFormat keyFormat = TextFormat::Plain;
    TextFormat valueFormat = TextFormat::Plain;
    std::size_t maxLength = 256;
};

using TextMap = std::map<std::string, std::string, std::less<>>;
using L10n = std::map<std::string, TextMap, std::less<>>;

struct Dependency {
    VersionRange version;
    bool optional = false;

    friend bool operator==(const Dependency&, const Dependency&) = default;
};

struct FilterContribution {
    std::string name;
    std::string file;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64});
        v.text("file", m.file, {.required = true, .maxLength = 260, .format = TextFormat::PackagePath});
    }

    friend bool operator==(const FilterContribution&, const FilterContribution&) = default;
};

struct ThemeContribution {
    std::string name;
    TextMap dark;
    TextMap light;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        constexpr MapRule colors{.keyFormat = TextFormat::TokenName, .valueFormat = TextFormat::Color, .maxLength = 64};
        v.text("name", m.name, {.required = true, .maxLength = 64});
        v.textMap("dark", m.dark, colors);
        v.textMap("light", m.light, colors);
    }

    friend bool operator==(const ThemeContribution&, const ThemeContribution&) = default;
};

struct ProfileContribution {
    std::string name;
    std::string file;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::ProfileName});
        v.text("file", m.file, {.required = true, .maxLength = 260, .format = TextFormat::PackagePath});
    }

    friend bool operator==(const ProfileContribution&, const ProfileContribution&) = default;
};

struct ProfilePatchContribution {
    std::string target;
    std::string file;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("target", m.target,
               {.required = true, .maxLength = kMaxIdLength + 65, .format = TextFormat::ProfileTarget});
        v.text("file", m.file, {.required = true, .maxLength = 260, .format = TextFormat::PackagePath});
    }

    friend bool operator==(const ProfilePatchContribution&, const ProfilePatchContribution&) = default;
};

struct NativeTouchMethod {
    std::string name;
    std::string title;
    std::string description;
    bool wantsDevice = false;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.text("title", m.title, {.required = true, .maxLength = 40});
        v.text("description", m.description, {.maxLength = 500});
        v.boolean("wantsDevice", m.wantsDevice);
    }

    friend bool operator==(const NativeTouchMethod&, const NativeTouchMethod&) = default;
};

struct NativeParam {
    std::string name;
    std::string type;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.text("type", m.type, {.required = true, .maxLength = 16, .format = TextFormat::NativeType});
    }

    friend bool operator==(const NativeParam&, const NativeParam&) = default;
};

struct NativeNode {
    std::string name;
    std::vector<NativeParam> params;
    std::string returns;
    std::string description;
    std::vector<std::string> permissions;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.objectList("params", m.params, 16);
        v.text("returns", m.returns, {.maxLength = 16, .format = TextFormat::NativeType});
        v.text("description", m.description, {.maxLength = 500});
        v.textList("permissions", m.permissions, {.maxItems = 10, .maxLength = 64, .format = TextFormat::PermissionName});
    }

    friend bool operator==(const NativeNode&, const NativeNode&) = default;
};

enum class DeclaredParamType : std::uint8_t { Number, Integer, Toggle, Choice, Key, Point, Region, Curve, Color };
inline constexpr std::array<std::string_view, 9> kParamTypeNames{"number", "integer", "toggle", "choice", "key",
                                                                 "point",  "region",  "curve",  "color"};
enum class RegionShape : std::uint8_t { Circle, Rect };
inline constexpr std::array<std::string_view, 2> kRegionShapeNames{"circle", "rect"};
enum class NativeSlotKind : std::uint8_t { Key, Axis2, Axis1 };
inline constexpr std::array<std::string_view, 3> kNativeSlotKindNames{"key", "axis2", "axis1"};
inline constexpr std::size_t kMostNativeAttachments = 32;
inline constexpr std::size_t kMostNativeBehaviors = 32;
inline constexpr std::size_t kMostDeclaredParams = 24;
inline constexpr std::size_t kMostDeclaredSlots = 8;
inline constexpr std::size_t kMostCurvePoints = 16;

struct DeclaredParam {
    std::string key;
    DeclaredParamType type = DeclaredParamType::Number;
    std::string title;
    std::string description;
    std::string defaultValue;
    std::optional<double> min;
    std::optional<double> max;
    std::optional<double> step;
    std::string unit;
    bool log = false;
    std::vector<std::string> options;
    RegionShape shape = RegionShape::Circle;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("key", m.key, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.choice("type", m.type, kParamTypeNames, true);
        v.text("title", m.title, {.required = true, .maxLength = 60});
        v.text("description", m.description, {.maxLength = 500});
        v.anyJson("default", m.defaultValue);
        v.number("min", m.min);
        v.number("max", m.max);
        v.number("step", m.step);
        v.text("unit", m.unit, {.maxLength = 16});
        v.boolean("log", m.log);
        v.textList("options", m.options, {.maxItems = 32, .maxLength = 64, .format = TextFormat::NativeName});
        v.choice("shape", m.shape, kRegionShapeNames, false);
    }

    friend bool operator==(const DeclaredParam&, const DeclaredParam&) = default;
};

struct DeclaredSlot {
    std::string name;
    NativeSlotKind kind = NativeSlotKind::Key;
    std::string title;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.choice("kind", m.kind, kNativeSlotKindNames, true);
        v.text("title", m.title, {.maxLength = 40});
    }

    friend bool operator==(const DeclaredSlot&, const DeclaredSlot&) = default;
};

struct NativeAttachment {
    std::string name;
    std::string title;
    std::string description;
    std::vector<std::string> appliesTo;
    std::vector<DeclaredParam> params;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.text("title", m.title, {.required = true, .maxLength = 40});
        v.text("description", m.description, {.maxLength = 500});
        v.textList("appliesTo", m.appliesTo, {.maxItems = 32, .maxLength = 32});
        v.objectList("params", m.params, kMostDeclaredParams);
    }

    friend bool operator==(const NativeAttachment&, const NativeAttachment&) = default;
};

struct NativeBehavior {
    std::string name;
    std::string title;
    std::string description;
    std::vector<DeclaredSlot> slotList;
    std::optional<std::int64_t> fingers;
    bool frames = false;
    std::vector<DeclaredParam> params;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.text("title", m.title, {.required = true, .maxLength = 40});
        v.text("description", m.description, {.maxLength = 500});
        v.objectList("slots", m.slotList, kMostDeclaredSlots);
        v.integer("fingers", m.fingers, 1, 10);
        v.boolean("frames", m.frames);
        v.objectList("params", m.params, kMostDeclaredParams);
    }

    friend bool operator==(const NativeBehavior&, const NativeBehavior&) = default;
};

[[nodiscard]] std::string declaredDefault(const DeclaredParam& param);
[[nodiscard]] std::string declaredProblem(const DeclaredParam& param, std::string_view json);
[[nodiscard]] bool knownControlType(std::string_view type) noexcept;

inline constexpr std::size_t kMostNativeJobs = 32;
inline constexpr std::int64_t kMostRunningNativeJobs = 4;

struct NativeJob {
    std::string name;
    std::string title;
    std::vector<NativeParam> params;
    std::string returns;
    std::string description;
    std::vector<std::string> permissions;
    std::optional<std::int64_t> most;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.text("title", m.title, {.required = true, .maxLength = 40});
        v.objectList("params", m.params, 16);
        v.text("returns", m.returns, {.maxLength = 16, .format = TextFormat::NativeType});
        v.text("description", m.description, {.maxLength = 500});
        v.textList("permissions", m.permissions, {.maxItems = 10, .maxLength = 64, .format = TextFormat::PermissionName});
        v.integer("most", m.most, 1, kMostRunningNativeJobs);
    }

    friend bool operator==(const NativeJob&, const NativeJob&) = default;
};

inline constexpr std::size_t kMostDeviceProviders = 8;
inline constexpr std::array<std::string_view, 1> kDeviceKindNames{"touchBox"};
inline constexpr std::string_view kDevicesPermission = "devices.provide";

struct NativeDeviceProvider {
    std::string name;
    std::string title;
    std::vector<std::string> kinds;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.text("title", m.title, {.required = true, .maxLength = 40});
        v.textList("kinds", m.kinds, {.maxItems = 4, .maxLength = 32});
    }

    friend bool operator==(const NativeDeviceProvider&, const NativeDeviceProvider&) = default;
};

struct NativeContribution {
    std::string dll;
    std::vector<NativeTouchMethod> touchMethods;
    std::vector<NativeNode> nodes;
    std::vector<NativeAttachment> attachments;
    std::vector<NativeBehavior> behaviors;
    std::vector<NativeJob> jobs;
    std::vector<NativeDeviceProvider> deviceProviders;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("dll", m.dll, {.required = true, .maxLength = 260, .format = TextFormat::PackagePath});
        v.objectList("touchMethods", m.touchMethods, 8);
        v.objectList("nodes", m.nodes, 64);
        v.objectList("attachments", m.attachments, kMostNativeAttachments);
        v.objectList("behaviors", m.behaviors, kMostNativeBehaviors);
        v.objectList("jobs", m.jobs, kMostNativeJobs);
        v.objectList("deviceProviders", m.deviceProviders, kMostDeviceProviders);
    }

    friend bool operator==(const NativeContribution&, const NativeContribution&) = default;
};

struct ComponentContribution {
    std::string name;
    std::string folder;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name,
               {.required = true, .maxLength = kMaxComponentNameLength, .format = TextFormat::ProfileName});
        v.text("folder", m.folder, {.required = true, .maxLength = 260, .format = TextFormat::PackagePath});
    }

    friend bool operator==(const ComponentContribution&, const ComponentContribution&) = default;
};

struct TaskContribution {
    std::string name;
    std::string folder;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::ProfileName});
        v.text("folder", m.folder, {.required = true, .maxLength = 260, .format = TextFormat::PackagePath});
    }

    friend bool operator==(const TaskContribution&, const TaskContribution&) = default;
};

enum class ScriptRun : std::uint8_t { Session, App };
inline constexpr std::array<std::string_view, 2> kScriptRunNames{"session", "app"};
inline constexpr std::size_t kMaxScripts = 8;
inline constexpr std::size_t kMaxScriptNameLength = 32;

struct ScriptContribution {
    std::string name;
    std::string entry;
    ScriptRun run = ScriptRun::Session;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name,
               {.required = true, .maxLength = kMaxScriptNameLength, .format = TextFormat::ScriptName});
        v.text("entry", m.entry, {.required = true, .maxLength = 260, .format = TextFormat::PackagePath});
        v.choice("run", m.run, kScriptRunNames, true);
    }

    friend bool operator==(const ScriptContribution&, const ScriptContribution&) = default;
};

enum class PanelPlace : std::uint8_t { Mirror, Menu, Status, Page, Device, Settings, Extension, Toolbar, ContextMenu, Tray };
inline constexpr std::array<std::string_view, 10> kPanelPlaceNames{
    "mirror", "menu", "status", "page", "device", "settings", "extension", "toolbar", "contextMenu", "tray"};
inline constexpr std::array<std::string_view, 2> kPanelTargetNames{"device", "profile"};
inline constexpr std::size_t kMaxPanels = 8;
inline constexpr std::size_t kMaxPanelNameLength = 32;
inline constexpr std::string_view kMenuPanelName = "menu";

[[nodiscard]] constexpr bool panelForApp(PanelPlace where) noexcept
{
    return where == PanelPlace::Page || where == PanelPlace::Device || where == PanelPlace::Settings ||
           where == PanelPlace::Extension || where == PanelPlace::ContextMenu || where == PanelPlace::Tray;
}

struct PanelContribution {
    std::string name;
    std::string title;
    std::string icon;
    PanelPlace where = PanelPlace::Mirror;
    std::string script;
    std::vector<std::string> on;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = kMaxPanelNameLength, .format = TextFormat::PanelName});
        v.text("title", m.title, {.required = true, .maxLength = 40});
        v.text("icon", m.icon, {.maxLength = 64});
        v.choice("where", m.where, kPanelPlaceNames, true);
        v.text("script", m.script, {.required = true, .maxLength = 64});
        v.textList("on", m.on, {.maxItems = 2, .maxLength = 16});
    }

    friend bool operator==(const PanelContribution&, const PanelContribution&) = default;
};

inline constexpr std::size_t kMaxHelpTopics = 16;

struct HelpContribution {
    std::string id;
    std::string title;
    std::string file;
    TextMap locales;
    std::string icon;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("id", m.id, {.required = true, .maxLength = kMaxPanelNameLength, .format = TextFormat::PanelName});
        v.text("title", m.title, {.required = true, .maxLength = 60});
        v.text("file", m.file, {.required = true, .maxLength = 260, .format = TextFormat::PackagePath});
        v.textMap("locales", m.locales, {.maxEntries = 8, .valueFormat = TextFormat::PackagePath, .maxLength = 260});
        v.text("icon", m.icon, {.maxLength = 64});
    }

    friend bool operator==(const HelpContribution&, const HelpContribution&) = default;
};

enum class TourStart : std::uint8_t { Enabled, Manual };
inline constexpr std::array<std::string_view, 2> kTourStartNames{"enabled", "manual"};
inline constexpr std::size_t kMaxTours = 4;
inline constexpr std::size_t kMinTourSteps = 2;
inline constexpr std::size_t kMaxTourSteps = 12;

struct TourStep {
    std::string target;
    std::string title;
    std::string text;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("target", m.target, {.maxLength = 64});
        v.text("title", m.title, {.required = true, .maxLength = 60});
        v.text("text", m.text, {.required = true, .maxLength = 300});
    }

    friend bool operator==(const TourStep&, const TourStep&) = default;
};

struct TourContribution {
    std::string id;
    std::string title;
    TourStart start = TourStart::Enabled;
    std::vector<TourStep> steps;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("id", m.id, {.required = true, .maxLength = kMaxPanelNameLength, .format = TextFormat::PanelName});
        v.text("title", m.title, {.required = true, .maxLength = 60});
        v.choice("start", m.start, kTourStartNames, false);
        v.objectList("steps", m.steps, kMaxTourSteps);
    }

    friend bool operator==(const TourContribution&, const TourContribution&) = default;
};

struct Contributions {
    std::vector<FilterContribution> filters;
    std::vector<ThemeContribution> themes;
    std::vector<ProfileContribution> profiles;
    std::vector<ProfilePatchContribution> profilePatches;
    std::vector<ScriptContribution> scripts;
    std::string commands;
    std::string presets;
    std::optional<NativeContribution> native;
    std::vector<ComponentContribution> components;
    std::vector<TaskContribution> tasks;
    std::vector<PanelContribution> panels;
    std::vector<HelpContribution> help;
    std::vector<TourContribution> tours;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.objectList("filters", m.filters, 32);
        v.objectList("themes", m.themes, 32);
        v.objectList("profiles", m.profiles, 256);
        v.objectList("profilePatches", m.profilePatches, 256);
        v.objectList("scripts", m.scripts, kMaxScripts);
        v.rawJson("commands", m.commands);
        v.rawJson("presets", m.presets);
        v.optionalObject("native", m.native);
        v.objectList("components", m.components, 256);
        v.objectList("tasks", m.tasks, 256);
        v.objectList("panels", m.panels, kMaxPanels);
        v.objectList("help", m.help, kMaxHelpTopics);
        v.objectList("tours", m.tours, kMaxTours);
    }

    friend bool operator==(const Contributions&, const Contributions&) = default;
};

enum class SettingType : std::uint8_t { Choice, Integer, Number, Boolean, Text };
inline constexpr std::array<std::string_view, 5> kSettingTypeNames{"choice", "integer", "number", "boolean", "text"};

enum class SettingScope : std::uint8_t { Global, Device };
inline constexpr std::array<std::string_view, 2> kSettingScopeNames{"global", "device"};

using SettingValue = std::variant<std::monostate, bool, std::int64_t, double, std::string>;

struct SettingDeclaration {
    std::string key;
    SettingType type = SettingType::Boolean;
    std::string title;
    std::string description;
    std::string keywords;
    std::vector<std::string> choices;
    std::optional<double> min;
    std::optional<double> max;
    std::optional<double> step;
    std::optional<std::int64_t> maxLength;
    SettingValue defaultValue;
    SettingScope scope = SettingScope::Global;
    bool live = false;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("key", m.key, {.required = true, .maxLength = 64, .format = TextFormat::SettingKey});
        v.choice("type", m.type, kSettingTypeNames, true);
        v.text("title", m.title, {.required = true, .maxLength = 100});
        v.text("description", m.description, {.maxLength = 1000});
        v.text("keywords", m.keywords, {.maxLength = 500});
        v.textList("choices", m.choices, {.maxItems = 64, .maxLength = 64});
        v.number("min", m.min);
        v.number("max", m.max);
        v.number("step", m.step);
        v.integer("maxLength", m.maxLength, 1, 4096);
        v.settingValue("default", m.defaultValue, true);
        v.choice("scope", m.scope, kSettingScopeNames, false);
        v.boolean("live", m.live);
    }

    friend bool operator==(const SettingDeclaration&, const SettingDeclaration&) = default;
};

struct Permission {
    std::string name;
    std::string reason;
    std::vector<std::string> hosts;
    std::vector<std::string> paths;
    std::optional<std::int64_t> maxDevices;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::PermissionName});
        v.text("reason", m.reason,
               {.required = true, .maxLength = kMaxReasonLength, .missing = Code::PermissionReasonMissing});
        v.textList("hosts", m.hosts, {.maxItems = 64, .maxLength = 260});
        v.textList("paths", m.paths, {.maxItems = 64, .maxLength = 260});
        v.integer("maxDevices", m.maxDevices, 1, 64);
    }

    friend bool operator==(const Permission&, const Permission&) = default;
};

struct Manifest {
    std::int64_t schemaVersion = kSchemaVersion;
    std::string id;
    Version version;
    std::string name;
    std::string description;
    std::vector<std::string> authors;
    std::string license;
    std::string repository;
    std::string authorKey;
    bool aiAssisted = false;
    std::map<ExtensionPoint, VersionRange> requirements;
    std::map<std::string, Dependency, std::less<>> dependencies;
    Contributions contributes;
    std::vector<SettingDeclaration> settings;
    std::vector<Permission> permissions;
    L10n l10n;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.schemaVersion("schemaVersion", m.schemaVersion);
        v.text("id", m.id, {.required = true, .maxLength = kMaxIdLength, .format = TextFormat::ExtensionId});
        v.version("version", m.version);
        v.text("name", m.name, {.required = true, .maxLength = 100});
        v.text("description", m.description, {.maxLength = 1000});
        v.textList("authors", m.authors, {.maxItems = 32, .maxLength = 100});
        v.text("license", m.license, {.required = true, .maxLength = 100});
        v.text("repository", m.repository, {.maxLength = 512, .format = TextFormat::HttpsUrl});
        v.text("authorKey", m.authorKey, {.maxLength = 64, .format = TextFormat::SigningKey});
        v.boolean("aiAssisted", m.aiAssisted);
        v.requirements("requires", m.requirements);
        v.dependencies("dependencies", m.dependencies);
        v.object("contributes", m.contributes);
        v.objectList("settings", m.settings, 128);
        v.objectList("permissions", m.permissions, 32);
        v.l10n("l10n", m.l10n);
    }

    [[nodiscard]] bool declares(ExtensionPoint point) const { return requirements.contains(point); }

    friend bool operator==(const Manifest&, const Manifest&) = default;
};

struct LoadOptions {
    bool official = false;
};

struct LoadedManifest {
    bool usable = false;
    Manifest manifest;
    std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] std::optional<ExtensionPoint> extensionPointNamed(std::string_view name) noexcept;
[[nodiscard]] std::string_view nameOf(ExtensionPoint point) noexcept;
[[nodiscard]] bool validExtensionId(std::string_view id);
[[nodiscard]] bool settingAccepts(const SettingDeclaration& setting, const SettingValue& value);
[[nodiscard]] LoadedManifest loadManifest(std::string_view json, const LoadOptions& options = {});
[[nodiscard]] std::string saveManifest(const Manifest& manifest);
[[nodiscard]] std::string manifestJsonSchema();

}
