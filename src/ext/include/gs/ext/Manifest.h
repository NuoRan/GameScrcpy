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
#include <utility>
#include <variant>
#include <vector>

namespace gs::ext {

inline constexpr std::int64_t kSchemaVersion = 1;
inline constexpr std::size_t kMaxManifestBytes = 256 * 1024;
inline constexpr std::size_t kMaxIdLength = 64;
inline constexpr std::size_t kMaxReasonLength = 120;
inline constexpr std::string_view kReservedIdPrefix = "gamescrcpy.";

enum class ExtensionPoint : std::uint8_t {
    Profile, Theme, Filter, Script, Command, Preset, Native, Component, Task, Panel, Help, Tour, Locale
};
inline constexpr std::array<std::string_view, 13> kExtensionPointNames{"profile", "theme",     "filter",
                                                                       "script",  "command",   "preset",
                                                                       "native",  "component", "task",
                                                                       "panel",   "help",      "tour",
                                                                       "locale"};

inline constexpr std::array<std::string_view, 8> kNativeTypeNames{"boolean", "number", "string",   "point",
                                                                  "region",  "color",  "duration", "json"};

inline constexpr std::array<std::string_view, 23> kPermissionNames{
    "device.input", "device.screen", "device.clipboard", "device.apps", "devices.other", "mapping",
    "notify",       "network",       "files.pick",       "files.paths", "input.read",    "hardware",
    "ai",           "devices.provide", "network.listen", "speech.listen", "events.app", "input.provide",
    "transport",    "media.capture",   "media.output",   "media.read",   "ai.provide"};
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
    PanelName,
    InputSourceName,
    InputSlotName
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

inline constexpr std::size_t kMostSpeechEngines = 4;
inline constexpr std::string_view kSpeechListenPermission = "speech.listen";

struct NativeSpeechEngine {
    std::string name;
    std::string title;
    std::string say;
    std::string voices;
    std::string listen;
    bool dictation = false;
    std::vector<std::string> languages;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.text("title", m.title, {.required = true, .maxLength = 40});
        v.text("say", m.say, {.maxLength = 64, .format = TextFormat::NativeName});
        v.text("voices", m.voices, {.maxLength = 64, .format = TextFormat::NativeName});
        v.text("listen", m.listen, {.maxLength = 64, .format = TextFormat::NativeName});
        v.boolean("dictation", m.dictation);
        v.textList("languages", m.languages, {.maxItems = 32, .maxLength = 16});
    }

    friend bool operator==(const NativeSpeechEngine&, const NativeSpeechEngine&) = default;
};

inline constexpr std::size_t kMostInputSources = 8;
inline constexpr std::size_t kMostInputSlots = 64;
inline constexpr std::size_t kMaxInputNameLength = 32;
inline constexpr std::string_view kInputPermission = "input.provide";
enum class InputSlotKind : std::uint8_t { Button, Axis, Stick };
inline constexpr std::array<std::string_view, 3> kInputSlotKindNames{"button", "axis", "stick"};

struct NativeInputSlot {
    std::string name;
    InputSlotKind kind = InputSlotKind::Button;
    std::string title;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name,
               {.required = true, .maxLength = kMaxInputNameLength, .format = TextFormat::InputSlotName});
        v.choice("kind", m.kind, kInputSlotKindNames, true);
        v.text("title", m.title, {.maxLength = 40});
    }

    friend bool operator==(const NativeInputSlot&, const NativeInputSlot&) = default;
};

struct NativeInputSource {
    std::string name;
    std::string title;
    std::vector<NativeInputSlot> slotList;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name,
               {.required = true, .maxLength = kMaxInputNameLength, .format = TextFormat::InputSourceName});
        v.text("title", m.title, {.required = true, .maxLength = 40});
        v.objectList("slots", m.slotList, kMostInputSlots);
    }

    friend bool operator==(const NativeInputSource&, const NativeInputSource&) = default;
};

inline constexpr std::array<std::string_view, 2> kNativeArchitectureNames{"x64", "arm64"};
inline constexpr std::size_t kMostNativeDlls = 8;

inline constexpr std::size_t kMostNativeTransports = 8;
inline constexpr std::string_view kTransportPermission = "transport";

struct NativeTransport {
    std::string name;
    std::string title;
    std::string icon;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.text("title", m.title, {.required = true, .maxLength = 40});
        v.text("icon", m.icon, {.maxLength = 64});
    }

    friend bool operator==(const NativeTransport&, const NativeTransport&) = default;
};

inline constexpr std::size_t kMostMediaSources = 8;
inline constexpr std::size_t kMostMediaSinks = 8;
inline constexpr std::size_t kMostRecordingExporters = 8;
inline constexpr std::array<std::string_view, 2> kVideoSourceKindNames{"phone", "touchBox"};
inline constexpr std::string_view kMediaCapturePermission = "media.capture";
inline constexpr std::string_view kMediaOutputPermission = "media.output";
inline constexpr std::int64_t kMostSinkFps = 60;
inline constexpr std::int64_t kLeastSinkSide = 16;
inline constexpr std::int64_t kMostSinkSide = 4096;
inline constexpr std::int64_t kMostSinkPixels = 3840 * 2160;
inline constexpr std::size_t kMostExporterExtensionLength = 16;

struct NativeVideoSource {
    std::string name;
    std::string title;
    std::vector<std::string> kinds;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.text("title", m.title, {.required = true, .maxLength = 40});
        v.textList("kinds", m.kinds, {.maxItems = 2, .maxLength = 16});
    }

    friend bool operator==(const NativeVideoSource&, const NativeVideoSource&) = default;
};

struct NativeAudioSource {
    std::string name;
    std::string title;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.text("title", m.title, {.required = true, .maxLength = 40});
    }

    friend bool operator==(const NativeAudioSource&, const NativeAudioSource&) = default;
};

struct SinkVideo {
    std::optional<std::int64_t> fps;
    std::optional<std::int64_t> width;
    std::optional<std::int64_t> height;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.integer("fps", m.fps, 1, kMostSinkFps);
        v.integer("width", m.width, kLeastSinkSide, kMostSinkSide);
        v.integer("height", m.height, kLeastSinkSide, kMostSinkSide);
    }

    friend bool operator==(const SinkVideo&, const SinkVideo&) = default;
};

struct NativeMediaSink {
    std::string name;
    std::string title;
    std::optional<SinkVideo> video;
    bool audio = false;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.text("title", m.title, {.required = true, .maxLength = 40});
        v.optionalObject("video", m.video);
        v.boolean("audio", m.audio);
    }

    friend bool operator==(const NativeMediaSink&, const NativeMediaSink&) = default;
};

struct NativeRecordingExporter {
    std::string name;
    std::string title;
    std::string extension;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.text("title", m.title, {.required = true, .maxLength = 40});
        v.text("extension", m.extension, {.required = true, .maxLength = kMostExporterExtensionLength});
    }

    friend bool operator==(const NativeRecordingExporter&, const NativeRecordingExporter&) = default;
};

inline constexpr std::size_t kMostFrameTaps = 8;
inline constexpr std::int64_t kMostTapFps = 60;
inline constexpr std::int64_t kDefaultTapFps = 30;
inline constexpr std::int64_t kLeastTapSize = 64;
inline constexpr std::int64_t kMostTapSize = 3840;
inline constexpr std::int64_t kDefaultTapSize = 1280;
inline constexpr std::string_view kMediaReadPermission = "media.read";

struct NativeFrameTap {
    std::string name;
    std::string title;
    std::optional<std::int64_t> fps;
    std::optional<std::int64_t> size;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.text("title", m.title, {.required = true, .maxLength = 40});
        v.integer("fps", m.fps, 1, kMostTapFps);
        v.integer("size", m.size, kLeastTapSize, kMostTapSize);
    }

    friend bool operator==(const NativeFrameTap&, const NativeFrameTap&) = default;
};

inline constexpr std::size_t kMostAiProviders = 4;
inline constexpr std::string_view kAiProvidePermission = "ai.provide";

struct NativeAiProvider {
    std::string name;
    std::string title;
    std::string models;
    std::string chat;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.text("title", m.title, {.required = true, .maxLength = 40});
        v.text("models", m.models, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.text("chat", m.chat, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
    }

    friend bool operator==(const NativeAiProvider&, const NativeAiProvider&) = default;
};

inline constexpr std::size_t kMostVisionEngines = 4;
inline constexpr std::array<std::string_view, 3> kVisionKindNames{"ocr", "detect", "classify"};

struct NativeVisionEngine {
    std::string name;
    std::string title;
    std::vector<std::string> kinds;
    std::vector<std::string> languages;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.text("title", m.title, {.required = true, .maxLength = 40});
        v.textList("kinds", m.kinds, {.maxItems = 3, .maxLength = 16});
        v.textList("languages", m.languages, {.maxItems = 32, .maxLength = 16});
    }

    friend bool operator==(const NativeVisionEngine&, const NativeVisionEngine&) = default;
};

enum class NativeSandbox : std::uint8_t { Full, Restricted };
inline constexpr std::array<std::string_view, 2> kNativeSandboxNames{"full", "restricted"};

struct NativeContribution {
    std::string dll;
    TextMap dlls;
    std::vector<NativeTouchMethod> touchMethods;
    std::vector<NativeNode> nodes;
    std::vector<NativeAttachment> attachments;
    std::vector<NativeBehavior> behaviors;
    std::vector<NativeJob> jobs;
    std::vector<NativeDeviceProvider> deviceProviders;
    std::vector<NativeSpeechEngine> speechEngines;
    std::vector<NativeInputSource> inputSources;
    std::vector<NativeTransport> transports;
    std::vector<NativeVideoSource> videoSources;
    std::vector<NativeAudioSource> audioSources;
    std::vector<NativeMediaSink> mediaSinks;
    std::vector<NativeRecordingExporter> recordingExporters;
    std::vector<NativeFrameTap> frameTaps;
    std::vector<NativeAiProvider> aiProviders;
    std::vector<NativeVisionEngine> visionEngines;
    NativeSandbox sandbox = NativeSandbox::Full;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.textOrMap("dll", m.dll, m.dlls, {.required = true, .maxLength = 260, .format = TextFormat::PackagePath},
                    {.maxEntries = kMostNativeDlls,
                     .keyFormat = TextFormat::NativeName,
                     .valueFormat = TextFormat::PackagePath,
                     .maxLength = 260});
        v.objectList("touchMethods", m.touchMethods, 8);
        v.objectList("nodes", m.nodes, 64);
        v.objectList("attachments", m.attachments, kMostNativeAttachments);
        v.objectList("behaviors", m.behaviors, kMostNativeBehaviors);
        v.objectList("jobs", m.jobs, kMostNativeJobs);
        v.objectList("deviceProviders", m.deviceProviders, kMostDeviceProviders);
        v.objectList("speechEngines", m.speechEngines, kMostSpeechEngines);
        v.objectList("inputSources", m.inputSources, kMostInputSources);
        v.objectList("transports", m.transports, kMostNativeTransports);
        v.objectList("videoSources", m.videoSources, kMostMediaSources);
        v.objectList("audioSources", m.audioSources, kMostMediaSources);
        v.objectList("mediaSinks", m.mediaSinks, kMostMediaSinks);
        v.objectList("recordingExporters", m.recordingExporters, kMostRecordingExporters);
        v.objectList("frameTaps", m.frameTaps, kMostFrameTaps);
        v.objectList("aiProviders", m.aiProviders, kMostAiProviders);
        v.objectList("visionEngines", m.visionEngines, kMostVisionEngines);
        v.choice("sandbox", m.sandbox, kNativeSandboxNames, false);
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

inline constexpr std::size_t kMaxServices = 32;

struct ServiceContribution {
    std::string name;
    std::string title;
    std::string description;
    std::string input;
    std::string output;
    std::string script;
    std::string node;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.text("title", m.title, {.maxLength = 40});
        v.text("description", m.description, {.required = true, .maxLength = 500});
        v.rawJson("input", m.input);
        v.rawJson("output", m.output);
        v.text("script", m.script, {.maxLength = kMaxScriptNameLength, .format = TextFormat::ScriptName});
        v.text("node", m.node, {.maxLength = 64, .format = TextFormat::NativeName});
    }

    friend bool operator==(const ServiceContribution&, const ServiceContribution&) = default;
};

inline constexpr std::size_t kMaxCliCommands = 16;
inline constexpr std::size_t kMaxCliNameLength = 32;
inline constexpr std::array<std::string_view, 3> kCliReservedOptions{"json", "hosted", "help"};

struct CliParam {
    std::string name;
    std::string description;
    std::string defaultValue;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.text("description", m.description, {.maxLength = 200});
        v.anyJson("default", m.defaultValue);
    }

    friend bool operator==(const CliParam&, const CliParam&) = default;
};

struct CliContribution {
    std::string name;
    std::string description;
    std::string job;
    std::vector<CliParam> params;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = kMaxCliNameLength});
        v.text("description", m.description, {.maxLength = 200});
        v.text("job", m.job, {.required = true, .maxLength = 64, .format = TextFormat::NativeName});
        v.objectList("params", m.params, 16);
    }

    friend bool operator==(const CliContribution&, const CliContribution&) = default;
};

inline constexpr std::size_t kMaxConverters = 16;
inline constexpr std::size_t kMaxConvertFormatLength = 16;
inline constexpr std::array<std::string_view, 3> kConvertFormatNames{"profile", "task", "recording"};

struct ConverterContribution {
    std::string name;
    std::string title;
    std::string from;
    std::string to;
    std::string script;
    std::vector<std::string> extensions;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = kMaxPanelNameLength, .format = TextFormat::PanelName});
        v.text("title", m.title, {.required = true, .maxLength = 40});
        v.text("from", m.from, {.required = true, .maxLength = kMaxConvertFormatLength});
        v.text("to", m.to, {.required = true, .maxLength = kMaxConvertFormatLength});
        v.text("script", m.script, {.required = true, .maxLength = 260, .format = TextFormat::PackagePath});
        v.textList("extensions", m.extensions, {.maxItems = 8, .maxLength = kMaxConvertFormatLength});
    }

    friend bool operator==(const ConverterContribution&, const ConverterContribution&) = default;
};

[[nodiscard]] bool appFormat(std::string_view format) noexcept;
[[nodiscard]] bool fileFormat(std::string_view format) noexcept;

inline constexpr std::size_t kMaxLocales = 8;
inline constexpr std::size_t kMaxLocaleNameLength = 40;
inline constexpr std::array<std::string_view, 2> kBuiltinLocales{"en", "zh_CN"};

struct LocaleContribution {
    std::string locale;
    std::string name;
    std::string translations;
    std::string help;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("locale", m.locale, {.required = true, .maxLength = 16});
        v.text("name", m.name, {.required = true, .maxLength = kMaxLocaleNameLength});
        v.text("translations", m.translations, {.required = true, .maxLength = 260, .format = TextFormat::PackagePath});
        v.text("help", m.help, {.maxLength = 260, .format = TextFormat::PackagePath});
    }

    friend bool operator==(const LocaleContribution&, const LocaleContribution&) = default;
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
    std::vector<ServiceContribution> services;
    std::vector<CliContribution> cli;
    std::vector<ConverterContribution> converters;
    std::vector<LocaleContribution> locales;

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
        v.objectList("services", m.services, kMaxServices);
        v.objectList("cli", m.cli, kMaxCliCommands);
        v.objectList("converters", m.converters, kMaxConverters);
        v.objectList("locales", m.locales, kMaxLocales);
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

inline constexpr std::size_t kMaxPermissionFolders = 16;
inline constexpr std::string_view kNetworkPermission = "network";
inline constexpr std::string_view kFilesPickPermission = "files.pick";
inline constexpr std::string_view kFilesPathsPermission = "files.paths";

struct PermissionFolder {
    std::string name;
    std::string purpose;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = kMaxPanelNameLength, .format = TextFormat::PanelName});
        v.text("purpose", m.purpose, {.required = true, .maxLength = kMaxReasonLength});
    }

    friend bool operator==(const PermissionFolder&, const PermissionFolder&) = default;
};

struct Permission {
    std::string name;
    std::string reason;
    std::vector<std::string> hosts;
    std::vector<std::string> paths;
    std::optional<std::int64_t> maxDevices;
    std::vector<PermissionFolder> folders;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, {.required = true, .maxLength = 64, .format = TextFormat::PermissionName});
        v.text("reason", m.reason,
               {.required = true, .maxLength = kMaxReasonLength, .missing = Code::PermissionReasonMissing});
        v.textList("hosts", m.hosts, {.maxItems = 64, .maxLength = 260});
        v.textList("paths", m.paths, {.maxItems = 64, .maxLength = 260});
        v.integer("maxDevices", m.maxDevices, 1, 64);
        v.objectList("folders", m.folders, kMaxPermissionFolders);
    }

    friend bool operator==(const Permission&, const Permission&) = default;
};

inline constexpr std::size_t kMaxUpdateIndexLength = 2048;

struct UpdateSource {
    std::string index;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("index", m.index,
               {.required = true, .maxLength = kMaxUpdateIndexLength, .format = TextFormat::HttpsUrl});
    }

    friend bool operator==(const UpdateSource&, const UpdateSource&) = default;
};

struct Manifest {
    std::int64_t schemaVersion = kSchemaVersion;
    std::string id;
    Version version;
    std::optional<Version> gamescrcpy;
    std::string name;
    std::string description;
    std::vector<std::string> authors;
    std::string license;
    std::string repository;
    std::string authorKey;
    std::optional<UpdateSource> updates;
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
        v.gameVersion("gamescrcpy", m.gamescrcpy);
        v.text("name", m.name, {.required = true, .maxLength = 100});
        v.text("description", m.description, {.maxLength = 1000});
        v.textList("authors", m.authors, {.maxItems = 32, .maxLength = 100});
        v.text("license", m.license, {.required = true, .maxLength = 100});
        v.text("repository", m.repository, {.maxLength = 512, .format = TextFormat::HttpsUrl});
        v.text("authorKey", m.authorKey, {.maxLength = 64, .format = TextFormat::SigningKey});
        v.optionalObject("updates", m.updates);
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

[[nodiscard]] std::string_view hostArchitecture() noexcept;
[[nodiscard]] std::vector<std::pair<std::string, std::string>> nativeDlls(const NativeContribution& native);
[[nodiscard]] std::string nativeDllFor(const NativeContribution& native, std::string_view architecture);
[[nodiscard]] std::string nativeDllField(std::string_view architecture);

[[nodiscard]] std::optional<ExtensionPoint> extensionPointNamed(std::string_view name) noexcept;
[[nodiscard]] std::string_view nameOf(ExtensionPoint point) noexcept;
[[nodiscard]] bool validExtensionId(std::string_view id);
[[nodiscard]] bool hostListed(const std::vector<std::string>& hosts, std::string_view host, int port);
[[nodiscard]] bool settingAccepts(const SettingDeclaration& setting, const SettingValue& value);
[[nodiscard]] LoadedManifest loadManifest(std::string_view json, const LoadOptions& options = {});
[[nodiscard]] std::string saveManifest(const Manifest& manifest);
[[nodiscard]] std::string manifestJsonSchema();

}
