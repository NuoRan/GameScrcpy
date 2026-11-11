#include "gs/ext/Manifest.h"

#include "gs/ext/Archive.h"
#include "gs/ext/Minisign.h"
#include "gs/ext/Schema.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <set>
#include <type_traits>
#include <utility>

namespace gs::ext {

namespace {

using Json = nlohmann::ordered_json;

constexpr int kMaxNesting = 32;
constexpr std::string_view kVersionPattern =
    R"(^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$)";

bool nestingWithinLimit(std::string_view text) noexcept
{
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (const char c : text) {
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
        } else if (c == '"') {
            inString = true;
        } else if (c == '{' || c == '[') {
            if (++depth > kMaxNesting) {
                return false;
            }
        } else if (c == '}' || c == ']') {
            --depth;
        }
    }
    return true;
}

std::size_t characters(std::string_view text) noexcept
{
    return static_cast<std::size_t>(
        std::ranges::count_if(text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

bool lowerOrDigit(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

bool letter(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool digit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

bool hexDigit(char c) noexcept
{
    return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

std::vector<std::string_view> split(std::string_view text, char separator)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = text.find(separator, start);
        if (end == std::string_view::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

bool hyphenatedWord(std::string_view part) noexcept
{
    return !part.empty() && part.front() != '-' && part.back() != '-' && part.find("--") == std::string_view::npos &&
           std::ranges::all_of(part, [](char c) { return lowerOrDigit(c) || c == '-'; });
}

bool validId(std::string_view text)
{
    if (text.size() > kMaxIdLength) {
        return false;
    }
    const std::vector<std::string_view> parts = split(text, '.');
    return parts.size() >= 2 && std::ranges::all_of(parts, hyphenatedWord) && safeArchivePath(text);
}

bool validSettingKey(std::string_view text) noexcept
{
    return !text.empty() && text.size() <= 64 && letter(text.front()) &&
           std::ranges::all_of(text, [](char c) { return letter(c) || digit(c); });
}

bool validPackagePath(std::string_view text)
{
    return safeArchivePath(text) && !text.ends_with('/');
}

bool validProfileName(std::string_view text) noexcept
{
    return !text.empty() && text.size() <= 64 &&
           std::ranges::all_of(text, [](char c) { return letter(c) || digit(c) || c == '_' || c == '-'; });
}

bool validProfileTarget(std::string_view text)
{
    const std::size_t slash = text.rfind('/');
    return slash != std::string_view::npos && validId(text.substr(0, slash)) &&
           validProfileName(text.substr(slash + 1));
}

bool validHttpsUrl(std::string_view text) noexcept
{
    return text.size() > 8 && text.starts_with("https://") && text.find_first_of(" \t\r\n") == std::string_view::npos;
}

bool validTokenName(std::string_view text) noexcept
{
    return !text.empty() && text.front() >= 'a' && text.front() <= 'z' &&
           std::ranges::all_of(text, [](char c) { return letter(c) || digit(c); });
}

bool validColor(std::string_view text) noexcept
{
    return (text.size() == 7 || text.size() == 9) && text.front() == '#' &&
           std::ranges::all_of(text.substr(1), hexDigit);
}

bool validNativeName(std::string_view text) noexcept
{
    return !text.empty() && text.size() <= 64 && text.front() >= 'a' && text.front() <= 'z' &&
           std::ranges::all_of(text, [](char c) { return letter(c) || digit(c); });
}

bool validNativeType(std::string_view text) noexcept
{
    return std::ranges::find(kNativeTypeNames, text) != kNativeTypeNames.end();
}

bool validScriptName(std::string_view text) noexcept
{
    return !text.empty() && text.size() <= kMaxScriptNameLength &&
           std::ranges::all_of(text, [](char c) { return lowerOrDigit(c) || c == '-'; });
}

bool validPanelName(std::string_view text) noexcept
{
    return !text.empty() && text.size() <= kMaxPanelNameLength &&
           std::ranges::all_of(text, [](char c) { return lowerOrDigit(c) || c == '-'; });
}

bool validInputSourceName(std::string_view text) noexcept
{
    return !text.empty() && text.size() <= kMaxInputNameLength && text.front() >= 'a' && text.front() <= 'z' &&
           std::ranges::all_of(text, lowerOrDigit);
}

bool validInputSlotName(std::string_view text) noexcept
{
    return !text.empty() && text.size() <= kMaxInputNameLength &&
           std::ranges::all_of(text, [](char c) { return letter(c) || digit(c) || c == '_' || c == '-'; });
}

bool validHost(std::string_view text)
{
    std::string_view host = text;
    if (const std::size_t colon = host.rfind(':'); colon != std::string_view::npos) {
        const std::string_view port = host.substr(colon + 1);
        if (port.empty() || port.size() > 5 || !std::ranges::all_of(port, digit) || std::stoi(std::string(port)) < 1 ||
            std::stoi(std::string(port)) > 65535) {
            return false;
        }
        host = host.substr(0, colon);
    }
    if (host.starts_with("*.")) {
        host.remove_prefix(2);
    }
    if (host.empty() || host.size() > 253) {
        return false;
    }
    return std::ranges::all_of(split(host, '.'), [](std::string_view label) {
        return !label.empty() && label.size() <= 63 && label.front() != '-' && label.back() != '-' &&
               std::ranges::all_of(label, [](char c) { return letter(c) || digit(c) || c == '-'; });
    });
}

bool matches(TextFormat format, std::string_view text)
{
    switch (format) {
    case TextFormat::Plain:
    case TextFormat::PermissionName:
        return true;
    case TextFormat::ExtensionId:
        return validId(text);
    case TextFormat::SettingKey:
        return validSettingKey(text);
    case TextFormat::PackagePath:
        return validPackagePath(text);
    case TextFormat::HttpsUrl:
        return validHttpsUrl(text);
    case TextFormat::TokenName:
        return validTokenName(text);
    case TextFormat::Color:
        return validColor(text);
    case TextFormat::ProfileName:
        return validProfileName(text);
    case TextFormat::ProfileTarget:
        return validProfileTarget(text);
    case TextFormat::NativeName:
        return validNativeName(text);
    case TextFormat::NativeType:
        return validNativeType(text);
    case TextFormat::SigningKey:
        return parseMinisignKey(text).has_value();
    case TextFormat::ScriptName:
        return validScriptName(text);
    case TextFormat::PanelName:
        return validPanelName(text);
    case TextFormat::InputSourceName:
        return validInputSourceName(text);
    case TextFormat::InputSlotName:
        return validInputSlotName(text);
    }
    return false;
}

Code invalidCode(TextFormat format) noexcept
{
    switch (format) {
    case TextFormat::ExtensionId:
        return Code::IdInvalid;
    case TextFormat::SettingKey:
        return Code::SettingKeyInvalid;
    case TextFormat::PackagePath:
        return Code::PathInvalid;
    case TextFormat::HttpsUrl:
        return Code::UrlInvalid;
    case TextFormat::TokenName:
        return Code::ThemeTokenInvalid;
    case TextFormat::Color:
        return Code::ThemeColorInvalid;
    case TextFormat::ProfileName:
        return Code::ProfileNameInvalid;
    case TextFormat::ProfileTarget:
        return Code::ProfileTargetInvalid;
    case TextFormat::NativeName:
        return Code::NativeNameInvalid;
    case TextFormat::NativeType:
        return Code::NativeTypeInvalid;
    case TextFormat::SigningKey:
        return Code::AuthorKeyInvalid;
    case TextFormat::ScriptName:
        return Code::ScriptNameInvalid;
    case TextFormat::PanelName:
        return Code::PanelNameInvalid;
    case TextFormat::InputSourceName:
    case TextFormat::InputSlotName:
        return Code::InputNameInvalid;
    case TextFormat::Plain:
    case TextFormat::PermissionName:
        break;
    }
    return Code::WrongType;
}

std::optional<std::int64_t> integerOf(const Json& node)
{
    if (!node.is_number_integer()) {
        return std::nullopt;
    }
    if (node.is_number_unsigned() &&
        node.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return node.get<std::int64_t>();
}

constexpr std::size_t kMostReportsPerCode = 50;
constexpr std::size_t kMostKeyBytes = 64;

std::string shortened(const std::string& key)
{
    if (key.size() <= kMostKeyBytes) {
        return key;
    }
    std::size_t end = kMostKeyBytes;
    while (end > 0 && (static_cast<unsigned char>(key[end]) & 0xC0) == 0x80) {
        --end;
    }
    return key.substr(0, end) + "\u2026";
}

std::string keyed(const std::string& path, const std::string& key)
{
    return path + "[" + Json(shortened(key)).dump(-1, ' ', false, Json::error_handler_t::replace) + "]";
}

std::string member(const std::string& path, const std::string& key)
{
    const bool plain = !key.empty() && key.size() <= kMostKeyBytes && !digit(key.front()) &&
                       std::ranges::all_of(key, [](char c) { return letter(c) || digit(c) || c == '_'; });
    return plain ? path + "." + key : keyed(path, key);
}

std::string indexed(const std::string& path, std::size_t index)
{
    return path + "[" + std::to_string(index) + "]";
}

struct Context {
    std::vector<Diagnostic>& diagnostics;
    std::map<Code, std::size_t> reported{};
    std::map<Code, std::pair<Severity, std::size_t>> dropped{};

    void report(Severity severity, Code code, std::string path, std::string detail = {})
    {
        if (++reported[code] <= kMostReportsPerCode) {
            diagnostics.push_back({severity, code, std::move(path), std::move(detail)});
            return;
        }
        const auto entry = dropped.try_emplace(code, severity, 0).first;
        entry->second.first = std::min(entry->second.first, severity);
        ++entry->second.second;
    }

    void finish()
    {
        for (const auto& [code, drop] : dropped) {
            diagnostics.push_back({drop.first, code, "$", std::to_string(drop.second) + " more"});
        }
        dropped.clear();
    }
};

std::pair<std::string_view, std::string_view> identity(const FilterContribution& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const ThemeContribution& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const ProfileContribution& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const ProfilePatchContribution& entry)
{
    return {"file", entry.file};
}

std::pair<std::string_view, std::string_view> identity(const NativeTouchMethod& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const NativeParam& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const NativeNode& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const NativeJob& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const NativeDeviceProvider& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const NativeSpeechEngine& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const NativeInputSlot& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const NativeInputSource& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const NativeTransport& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const NativeVideoSource& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const NativeAudioSource& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const NativeMediaSink& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const NativeRecordingExporter& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const NativeFrameTap& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const DeclaredParam& entry)
{
    return {"key", entry.key};
}

std::pair<std::string_view, std::string_view> identity(const DeclaredSlot& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const NativeAttachment& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const NativeBehavior& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const ComponentContribution& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const TaskContribution& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const ScriptContribution& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const PanelContribution& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const HelpContribution& entry)
{
    return {"id", entry.id};
}

std::pair<std::string_view, std::string_view> identity(const TourStep&)
{
    return {"target", {}};
}

std::pair<std::string_view, std::string_view> identity(const TourContribution& entry)
{
    return {"id", entry.id};
}

std::pair<std::string_view, std::string_view> identity(const ServiceContribution& entry)
{
    return {"name", entry.name};
}

std::pair<std::string_view, std::string_view> identity(const SettingDeclaration& entry)
{
    return {"key", entry.key};
}

std::pair<std::string_view, std::string_view> identity(const Permission& entry)
{
    return {"name", entry.name};
}

void checkEntry(const FilterContribution& filter, const std::string& path, Context& context)
{
    std::string lower = filter.file;
    std::ranges::transform(lower, lower.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; });
    if (!filter.file.empty() && !lower.ends_with(".hlsl")) {
        context.report(Severity::Error, Code::FilterFileInvalid, path + ".file", filter.file);
    }
}

void checkEntry(const ThemeContribution&, const std::string&, Context&) {}

void checkEntry(const ProfileContribution&, const std::string&, Context&) {}

void checkEntry(const ProfilePatchContribution&, const std::string&, Context&) {}

void checkEntry(const NativeTouchMethod&, const std::string&, Context&) {}

void checkEntry(const NativeParam&, const std::string&, Context&) {}

void checkEntry(const NativeNode&, const std::string&, Context&) {}

void checkEntry(const NativeJob&, const std::string&, Context&) {}

void checkEntry(const NativeDeviceProvider& provider, const std::string& path, Context& context)
{
    if (provider.kinds.empty()) {
        context.report(Severity::Error, Code::MissingField, path + ".kinds");
    }
    for (std::size_t index = 0; index < provider.kinds.size(); ++index) {
        if (std::ranges::find(kDeviceKindNames, provider.kinds[index]) == kDeviceKindNames.end()) {
            context.report(Severity::Error, Code::OutOfRange, indexed(path + ".kinds", index), provider.kinds[index]);
        }
    }
}

void checkEntry(const NativeSpeechEngine& engine, const std::string& path, Context& context)
{
    if (engine.say.empty() && engine.listen.empty()) {
        context.report(Severity::Error, Code::SpeechEngineInvalid, path, engine.name);
    }
    for (std::size_t index = 0; index < engine.languages.size(); ++index) {
        const std::string& language = engine.languages[index];
        const bool written = language.size() >= 2 && std::ranges::all_of(language, [](char c) {
                                 return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                                        c == '-';
                             });
        if (!written) {
            context.report(Severity::Error, Code::OutOfRange, indexed(path + ".languages", index), language);
        }
    }
}

void checkEntry(const NativeInputSlot&, const std::string&, Context&) {}

void checkEntry(const NativeInputSource& source, const std::string& path, Context& context)
{
    if (source.slotList.empty()) {
        context.report(Severity::Error, Code::MissingField, path + ".slots");
    }
}

void checkEntry(const NativeTransport&, const std::string&, Context&) {}

void checkEntry(const NativeVideoSource& source, const std::string& path, Context& context)
{
    for (std::size_t index = 0; index < source.kinds.size(); ++index) {
        const std::string& kind = source.kinds[index];
        if (std::ranges::find(kVideoSourceKindNames, kind) == kVideoSourceKindNames.end()) {
            context.report(Severity::Error, Code::OutOfRange, indexed(path + ".kinds", index), kind);
        } else if (std::ranges::find(source.kinds.begin(), source.kinds.begin() + static_cast<std::ptrdiff_t>(index),
                                     kind) != source.kinds.begin() + static_cast<std::ptrdiff_t>(index)) {
            context.report(Severity::Error, Code::Duplicate, indexed(path + ".kinds", index), kind);
        }
    }
}

void checkEntry(const NativeAudioSource&, const std::string&, Context&) {}

void checkEntry(const SinkVideo& video, const std::string& path, Context& context)
{
    const std::pair<const char*, bool> missing[] = {
        {"fps", !video.fps}, {"width", !video.width}, {"height", !video.height}};
    for (const auto& [name, absent] : missing) {
        if (absent) {
            context.report(Severity::Error, Code::MissingField, path + "." + name);
        }
    }
    if (video.width && video.height && *video.width * *video.height > kMostSinkPixels) {
        context.report(Severity::Error, Code::OutOfRange, path + ".width", "more than 3840 × 2160 pixels");
    }
}

void checkEntry(const NativeMediaSink& sink, const std::string& path, Context& context)
{
    if (!sink.video && !sink.audio) {
        context.report(Severity::Error, Code::MissingField, path + ".video",
                       "a sink takes the picture, the sound or both");
    }
}

void checkEntry(const NativeRecordingExporter& exporter, const std::string& path, Context& context)
{
    const bool plain = std::ranges::all_of(exporter.extension, lowerOrDigit);
    if (!exporter.extension.empty() && (!plain || exporter.extension == "gsrec")) {
        context.report(Severity::Error, Code::OutOfRange, path + ".extension",
                       "lowercase letters and digits without the dot, not gsrec");
    }
}

void checkEntry(const NativeFrameTap&, const std::string&, Context&) {}

void checkEntry(const NativeContribution& native, const std::string& path, Context& context)
{
    for (const auto& [architecture, file] : nativeDlls(native)) {
        const std::string at = architecture.empty() ? path + ".dll" : keyed(path + ".dll", architecture);
        std::string lower = file;
        std::ranges::transform(lower, lower.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; });
        if (!file.empty() && !lower.ends_with(".dll")) {
            context.report(Severity::Error, Code::NativeDllInvalid, at, file);
        }
        if (!architecture.empty() &&
            std::ranges::find(kNativeArchitectureNames, architecture) == kNativeArchitectureNames.end()) {
            context.report(Severity::Warning, Code::NativeArchitectureUnknown, at, architecture);
        }
    }
}

void checkEntry(const DeclaredParam& param, const std::string& path, Context& context)
{
    const bool numeric = param.type == DeclaredParamType::Number || param.type == DeclaredParamType::Integer;
    const std::pair<const char*, bool> unused[] = {
        {"min", !numeric && param.min.has_value()},
        {"max", !numeric && param.max.has_value()},
        {"step", !numeric && param.step.has_value()},
        {"unit", !numeric && !param.unit.empty()},
        {"log", !numeric && param.log},
        {"options", param.type != DeclaredParamType::Choice && !param.options.empty()},
        {"shape", param.type != DeclaredParamType::Region && param.shape != RegionShape::Circle},
    };
    for (const auto& [name, flagged] : unused) {
        if (flagged) {
            context.report(Severity::Warning, Code::NativeParamUnused, path + "." + name);
        }
    }
    if (numeric && param.min && param.max && *param.min > *param.max) {
        context.report(Severity::Error, Code::NativeParamInvalid, path + ".min", "above max");
    }
    if (numeric && param.step && !(*param.step > 0.0)) {
        context.report(Severity::Error, Code::NativeParamInvalid, path + ".step", "not positive");
    }
    if (param.type == DeclaredParamType::Integer) {
        const std::pair<const char*, const std::optional<double>*> bounds[] = {
            {"min", &param.min}, {"max", &param.max}, {"step", &param.step}};
        for (const auto& [name, bound] : bounds) {
            if (bound->has_value() && std::floor(**bound) != **bound) {
                context.report(Severity::Error, Code::NativeParamInvalid, path + "." + name, "not a whole number");
            }
        }
    }
    if (numeric && param.log && (!param.min || !(*param.min > 0.0))) {
        context.report(Severity::Error, Code::NativeParamInvalid, path + ".log", "a log scale needs min above 0");
    }
    if (param.type == DeclaredParamType::Choice && param.options.empty()) {
        context.report(Severity::Error, Code::NativeParamInvalid, path + ".options", "no options");
    }
    if (!param.defaultValue.empty()) {
        if (const std::string problem = declaredProblem(param, param.defaultValue); !problem.empty()) {
            context.report(Severity::Error, Code::NativeParamInvalid, path + ".default", problem);
        }
    }
}

void checkEntry(const DeclaredSlot&, const std::string&, Context&) {}

void checkEntry(const NativeAttachment& attachment, const std::string& path, Context& context)
{
    for (std::size_t index = 0; index < attachment.appliesTo.size(); ++index) {
        const std::string& type = attachment.appliesTo[index];
        if (type != "*" && !knownControlType(type)) {
            context.report(Severity::Warning, Code::NativeTargetUnknown, indexed(path + ".appliesTo", index), type);
        }
    }
}

void checkEntry(const NativeBehavior&, const std::string&, Context&) {}

bool finiteNumber(const Json& node)
{
    return node.is_number() && std::isfinite(node.get<double>());
}

std::string pairProblem(const Json& value)
{
    if (!value.is_array() || value.size() != 2 || !finiteNumber(value[0]) || !finiteNumber(value[1])) {
        return "not [x, y]";
    }
    if (std::abs(value[0].get<double>()) > 2.0 || std::abs(value[1].get<double>()) > 2.0) {
        return "outside -2 to 2";
    }
    return {};
}

std::string regionProblem(const Json& value, RegionShape shape)
{
    const std::size_t count = shape == RegionShape::Circle ? 3 : 4;
    const char* written = shape == RegionShape::Circle ? "not [x, y, r]" : "not [x, y, w, h]";
    if (!value.is_array() || value.size() != count) {
        return written;
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (!finiteNumber(value[index])) {
            return written;
        }
    }
    if (std::abs(value[0].get<double>()) > 2.0 || std::abs(value[1].get<double>()) > 2.0) {
        return "outside -2 to 2";
    }
    for (std::size_t index = 2; index < count; ++index) {
        if (value[index].get<double>() < 0.0 || value[index].get<double>() > 4.0) {
            return "a size below 0 or above 4";
        }
    }
    return {};
}

std::string curveProblem(const Json& value)
{
    if (!value.is_array() || value.size() < 2 || value.size() > kMostCurvePoints) {
        return "not 2 to 16 points";
    }
    double last = -1.0;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const Json& point = value[index];
        if (!point.is_array() || point.size() != 2 || !finiteNumber(point[0]) || !finiteNumber(point[1])) {
            return "a point is not [x, y]";
        }
        const double x = point[0].get<double>();
        const double y = point[1].get<double>();
        if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0) {
            return "a point outside 0 to 1";
        }
        if (!(x > last)) {
            return "x does not rise";
        }
        last = x;
    }
    return {};
}

bool rgb(std::string_view text) noexcept
{
    return text.size() == 7 && text.front() == '#' && std::ranges::all_of(text.substr(1), hexDigit);
}

void checkEntry(const ComponentContribution&, const std::string&, Context&) {}

void checkEntry(const TaskContribution&, const std::string&, Context&) {}

void checkEntry(const ScriptContribution& script, const std::string& path, Context& context)
{
    std::string lower = script.entry;
    std::ranges::transform(lower, lower.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; });
    const bool code = (lower.ends_with(".ts") && !lower.ends_with(".d.ts")) || lower.ends_with(".js");
    if (!script.entry.empty() && !code) {
        context.report(Severity::Error, Code::ScriptEntryInvalid, path + ".entry", script.entry);
    }
}

void checkEntry(const PanelContribution& panel, const std::string& path, Context& context)
{
    if (panel.name == kMenuPanelName && panel.where != PanelPlace::Menu) {
        context.report(Severity::Error, Code::PanelNameInvalid, path + ".name",
                       "\"menu\" names only the panel shown in the right-click menu");
    }
    if (panel.where != PanelPlace::ContextMenu) {
        if (!panel.on.empty()) {
            context.report(Severity::Error, Code::OutOfRange, path + ".on",
                           "only a panel with where \"contextMenu\" says which cards it shows on");
        }
        return;
    }
    if (panel.on.empty()) {
        context.report(Severity::Error, Code::MissingField, path + ".on");
    }
    for (std::size_t index = 0; index < panel.on.size(); ++index) {
        if (std::ranges::find(kPanelTargetNames, panel.on[index]) == kPanelTargetNames.end()) {
            context.report(Severity::Error, Code::OutOfRange, indexed(path + ".on", index), panel.on[index]);
        } else if (std::ranges::find(panel.on.begin(), panel.on.begin() + static_cast<std::ptrdiff_t>(index),
                                     panel.on[index]) != panel.on.begin() + static_cast<std::ptrdiff_t>(index)) {
            context.report(Severity::Error, Code::Duplicate, indexed(path + ".on", index), panel.on[index]);
        }
    }
}

bool markdownFile(std::string_view file)
{
    if (file.size() < 4) {
        return false;
    }
    std::string ending(file.substr(file.size() - 3));
    std::ranges::transform(ending, ending.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; });
    return ending == ".md";
}

bool languageCode(std::string_view code)
{
    const auto lower = [](char c) { return c >= 'a' && c <= 'z'; };
    const auto upper = [](char c) { return c >= 'A' && c <= 'Z'; };
    const std::size_t split = code.find('_');
    const std::string_view language = code.substr(0, split);
    if (language.size() < 2 || language.size() > 3 || !std::ranges::all_of(language, lower)) {
        return false;
    }
    if (split == std::string_view::npos) {
        return true;
    }
    const std::string_view region = code.substr(split + 1);
    return region.size() == 2 && std::ranges::all_of(region, upper);
}

void checkEntry(const HelpContribution& help, const std::string& path, Context& context)
{
    if (!help.file.empty() && !markdownFile(help.file)) {
        context.report(Severity::Error, Code::PathInvalid, path + ".file", help.file);
    }
    for (const auto& [language, file] : help.locales) {
        const std::string at = keyed(path + ".locales", language);
        if (!languageCode(language)) {
            context.report(Severity::Error, Code::OutOfRange, at, language);
        } else if (!markdownFile(file)) {
            context.report(Severity::Error, Code::PathInvalid, at, file);
        }
    }
}

void checkEntry(const TourStep& step, const std::string& path, Context& context)
{
    const std::string_view target = step.target;
    if (target.empty() || target == "card" || target == "device") {
        return;
    }
    for (const std::string_view kind : {std::string_view("page:"), std::string_view("settings:")}) {
        if (target.starts_with(kind) && validPanelName(target.substr(kind.size()))) {
            return;
        }
    }
    context.report(Severity::Error, Code::TourTargetInvalid, path + ".target", step.target);
}

void checkEntry(const TourContribution& tour, const std::string& path, Context& context)
{
    if (tour.steps.size() < kMinTourSteps) {
        context.report(Severity::Error, Code::OutOfRange, path + ".steps", "a tour has 2 to 12 steps");
    }
}

void checkEntry(const ServiceContribution& service, const std::string& path, Context& context)
{
    if (service.script.empty() == service.node.empty()) {
        context.report(Severity::Error, Code::ServiceTargetInvalid, path + ".script",
                       service.script.empty() ? "give script or node" : "give script or node, not both");
    }
    const std::pair<const char*, const std::string*> schemas[] = {{"input", &service.input},
                                                                  {"output", &service.output}};
    for (const auto& [name, schema] : schemas) {
        if (schema->empty()) {
            continue;
        }
        if (const std::string problem = schemaProblem(*schema); !problem.empty()) {
            context.report(Severity::Error, Code::ServiceSchemaInvalid, path + "." + name, problem);
        }
    }
}

bool within(const SettingDeclaration& setting, double value)
{
    return (!setting.min || value >= *setting.min) && (!setting.max || value <= *setting.max);
}

}

bool validExtensionId(std::string_view id)
{
    return validId(id);
}

bool settingAccepts(const SettingDeclaration& setting, const SettingValue& value)
{
    switch (setting.type) {
    case SettingType::Boolean:
        return std::holds_alternative<bool>(value);
    case SettingType::Integer: {
        const auto* number = std::get_if<std::int64_t>(&value);
        return number != nullptr && within(setting, static_cast<double>(*number));
    }
    case SettingType::Number: {
        if (const auto* whole = std::get_if<std::int64_t>(&value)) {
            return within(setting, static_cast<double>(*whole));
        }
        const auto* number = std::get_if<double>(&value);
        return number != nullptr && std::isfinite(*number) && within(setting, *number);
    }
    case SettingType::Text: {
        const auto* text = std::get_if<std::string>(&value);
        return text != nullptr && (!setting.maxLength || characters(*text) <= static_cast<std::size_t>(*setting.maxLength));
    }
    case SettingType::Choice: {
        const auto* text = std::get_if<std::string>(&value);
        return text != nullptr && std::ranges::find(setting.choices, *text) != setting.choices.end();
    }
    }
    return false;
}

bool knownControlType(std::string_view type) noexcept
{
    static constexpr std::array<std::string_view, 19> kTypes{
        "tap",       "hold",      "longPress", "turbo",     "swipe", "pinch",      "wasd",
        "rightWalk", "padStick",  "camera",    "padCamera", "freeLook", "skill",   "component",
        "dial",      "multiPress", "tempCursor", "actionWheel", "behavior"};
    return std::ranges::find(kTypes, type) != kTypes.end();
}

std::string declaredProblem(const DeclaredParam& param, std::string_view json)
{
    const Json value = Json::parse(json.begin(), json.end(), nullptr, false);
    if (value.is_discarded()) {
        return "not JSON";
    }
    const auto inRange = [&param](double number) {
        return (!param.min || number >= *param.min) && (!param.max || number <= *param.max);
    };
    switch (param.type) {
    case DeclaredParamType::Number:
        if (!finiteNumber(value)) {
            return "not a number";
        }
        return inRange(value.get<double>()) ? std::string() : std::string("outside min and max");
    case DeclaredParamType::Integer: {
        const std::optional<std::int64_t> whole = integerOf(value);
        if (!whole) {
            return "not a whole number";
        }
        return inRange(static_cast<double>(*whole)) ? std::string() : std::string("outside min and max");
    }
    case DeclaredParamType::Toggle:
        return value.is_boolean() ? std::string() : std::string("not true or false");
    case DeclaredParamType::Choice:
        return value.is_string() &&
                       std::ranges::find(param.options, value.get_ref<const std::string&>()) != param.options.end()
                   ? std::string()
                   : std::string("not one of the options");
    case DeclaredParamType::Key:
        return value.is_string() && characters(value.get_ref<const std::string&>()) <= 64 ? std::string()
                                                                                          : std::string("not a key name");
    case DeclaredParamType::Point:
        return pairProblem(value);
    case DeclaredParamType::Region:
        return regionProblem(value, param.shape);
    case DeclaredParamType::Curve:
        return curveProblem(value);
    case DeclaredParamType::Color:
        return value.is_string() && rgb(value.get_ref<const std::string&>()) ? std::string()
                                                                             : std::string("not #RRGGBB");
    }
    return "of no known type";
}

std::string declaredDefault(const DeclaredParam& param)
{
    if (!param.defaultValue.empty() && declaredProblem(param, param.defaultValue).empty()) {
        return param.defaultValue;
    }
    switch (param.type) {
    case DeclaredParamType::Number: {
        double value = param.min.value_or(0.0);
        if (param.max && value > *param.max) {
            value = *param.max;
        }
        return Json(value).dump();
    }
    case DeclaredParamType::Integer: {
        double value = std::ceil(param.min.value_or(0.0));
        if (param.max && value > *param.max) {
            value = std::floor(*param.max);
        }
        return Json(static_cast<std::int64_t>(value)).dump();
    }
    case DeclaredParamType::Toggle:
        return "false";
    case DeclaredParamType::Choice:
        return Json(param.options.empty() ? std::string() : param.options.front()).dump();
    case DeclaredParamType::Key:
        return "\"\"";
    case DeclaredParamType::Point:
        return "[0,0]";
    case DeclaredParamType::Region:
        return param.shape == RegionShape::Circle ? "[0,0,0.1]" : "[0,0,0.2,0.2]";
    case DeclaredParamType::Curve:
        return "[[0,0],[1,1]]";
    case DeclaredParamType::Color:
        return "\"#FFFFFF\"";
    }
    return "null";
}

namespace {

void checkEntry(const SettingDeclaration& setting, const std::string& path, Context& context)
{
    const bool numeric = setting.type == SettingType::Integer || setting.type == SettingType::Number;
    if (setting.type == SettingType::Choice && setting.choices.empty()) {
        context.report(Severity::Error, Code::SettingChoicesMissing, path + ".choices");
    }
    if (setting.type != SettingType::Choice && !setting.choices.empty()) {
        context.report(Severity::Warning, Code::SettingFieldUnused, path + ".choices");
    }
    const std::pair<const char*, const std::optional<double>*> bounds[] = {
        {"min", &setting.min}, {"max", &setting.max}, {"step", &setting.step}};
    for (const auto& [name, bound] : bounds) {
        if (!bound->has_value()) {
            continue;
        }
        if (!numeric) {
            context.report(Severity::Warning, Code::SettingFieldUnused, path + "." + name);
        } else if (setting.type == SettingType::Integer && std::floor(**bound) != **bound) {
            context.report(Severity::Error, Code::SettingRangeInvalid, path + "." + name, "not a whole number");
        }
    }
    if (numeric && setting.min && setting.max && *setting.min > *setting.max) {
        context.report(Severity::Error, Code::SettingRangeInvalid, path + ".min", "above max");
    }
    if (numeric && setting.step && *setting.step <= 0) {
        context.report(Severity::Error, Code::SettingRangeInvalid, path + ".step", "not positive");
    }
    if (setting.type != SettingType::Text && setting.maxLength) {
        context.report(Severity::Warning, Code::SettingFieldUnused, path + ".maxLength");
    }
    if (!std::holds_alternative<std::monostate>(setting.defaultValue) && !settingAccepts(setting, setting.defaultValue)) {
        context.report(Severity::Error, Code::SettingDefaultInvalid, path + ".default");
    }
}

void checkEntry(const Permission& permission, const std::string& path, Context& context)
{
    if (permission.name.empty()) {
        return;
    }
    if (permission.name == kNeverGrantedPermission) {
        context.report(Severity::Error, Code::PermissionNeverGranted, path + ".name",
                       "adb.shell is never granted to extensions");
        return;
    }
    if (std::ranges::find(kPermissionNames, permission.name) == kPermissionNames.end()) {
        context.report(Severity::Error, Code::PermissionUnknown, path + ".name", permission.name);
        return;
    }
    if (permission.name == "network") {
        if (permission.hosts.empty()) {
            context.report(Severity::Error, Code::PermissionHostsMissing, path + ".hosts");
        }
        for (std::size_t i = 0; i < permission.hosts.size(); ++i) {
            const std::string& host = permission.hosts[i];
            if (host == "*") {
                context.report(Severity::Warning, Code::PermissionAnyHost, indexed(path + ".hosts", i));
            } else if (!validHost(host)) {
                context.report(Severity::Error, Code::PermissionHostInvalid, indexed(path + ".hosts", i), host);
            }
        }
    } else if (!permission.hosts.empty()) {
        context.report(Severity::Warning, Code::PermissionFieldUnused, path + ".hosts");
    }
    if (permission.name == "files.paths") {
        if (permission.paths.empty()) {
            context.report(Severity::Error, Code::PermissionPathsMissing, path + ".paths");
        }
    } else if (!permission.paths.empty()) {
        context.report(Severity::Warning, Code::PermissionFieldUnused, path + ".paths");
    }
    if (permission.name != "devices.other" && permission.maxDevices) {
        context.report(Severity::Warning, Code::PermissionFieldUnused, path + ".maxDevices");
    }
}

class Reader
{
public:
    Reader(const Json& node, std::string path, Context& context)
        : m_node(node), m_path(std::move(path)), m_context(context)
    {
    }

    void finish()
    {
        for (auto it = m_node.begin(); it != m_node.end(); ++it) {
            if (std::ranges::find(m_visited, it.key()) == m_visited.end()) {
                m_context.report(Severity::Warning, Code::UnknownField, member(m_path, it.key()));
            }
        }
    }

    void schemaVersion(std::string_view name, std::int64_t& field)
    {
        const Json* node = find(name);
        if (node == nullptr) {
            report(Severity::Error, Code::MissingField, name);
            return;
        }
        const std::optional<std::int64_t> parsed = integerOf(*node);
        if (!parsed) {
            wrongType(name, *node);
            return;
        }
        field = *parsed;
        if (*parsed != kSchemaVersion) {
            report(Severity::Error, Code::SchemaVersion, name, node->dump());
        }
    }

    void text(std::string_view name, std::string& field, const TextRule& rule)
    {
        const Json* node = find(name);
        if (node == nullptr) {
            if (rule.required) {
                report(Severity::Error, rule.missing, name);
            }
            return;
        }
        if (!node->is_string()) {
            wrongType(name, *node);
            return;
        }
        const std::string& written = node->get_ref<const std::string&>();
        if (rule.required && written.empty()) {
            report(Severity::Error, rule.missing, name);
            return;
        }
        if (characters(written) > rule.maxLength) {
            report(Severity::Error, Code::TooLong, name, std::to_string(rule.maxLength));
            return;
        }
        if (!matches(rule.format, written)) {
            report(Severity::Error, invalidCode(rule.format), name, written);
        }
        field = written;
    }

    void boolean(std::string_view name, bool& field)
    {
        const Json* node = find(name);
        if (node == nullptr) {
            return;
        }
        if (!node->is_boolean()) {
            wrongType(name, *node);
            return;
        }
        field = node->get<bool>();
    }

    void version(std::string_view name, Version& field)
    {
        const Json* node = find(name);
        if (node == nullptr) {
            report(Severity::Error, Code::MissingField, name);
            return;
        }
        if (!node->is_string()) {
            wrongType(name, *node);
            return;
        }
        const std::optional<Version> parsed = Version::parse(node->get_ref<const std::string&>());
        if (!parsed) {
            report(Severity::Error, Code::VersionInvalid, name, node->dump());
            return;
        }
        field = *parsed;
    }

    void textList(std::string_view name, std::vector<std::string>& field, const ListRule& rule)
    {
        const Json* node = find(name);
        if (node == nullptr) {
            return;
        }
        if (!node->is_array()) {
            wrongType(name, *node);
            return;
        }
        field.clear();
        for (std::size_t i = 0; i < node->size(); ++i) {
            if (i == rule.maxItems) {
                report(Severity::Error, Code::TooMany, name, std::to_string(rule.maxItems));
                break;
            }
            const Json& item = (*node)[i];
            const std::string path = indexed(pathOf(name), i);
            if (!item.is_string()) {
                m_context.report(Severity::Error, Code::WrongType, path, item.dump());
                continue;
            }
            const std::string& written = item.get_ref<const std::string&>();
            if (written.empty() || characters(written) > rule.maxLength) {
                m_context.report(Severity::Error, written.empty() ? Code::OutOfRange : Code::TooLong, path);
                continue;
            }
            if (!matches(rule.format, written)) {
                m_context.report(Severity::Error, invalidCode(rule.format), path, written);
                continue;
            }
            field.push_back(written);
        }
    }

    void requirements(std::string_view name, std::map<ExtensionPoint, VersionRange>& field)
    {
        const Json* node = find(name);
        if (node == nullptr) {
            report(Severity::Error, Code::MissingField, name);
            return;
        }
        if (!node->is_object()) {
            wrongType(name, *node);
            return;
        }
        if (node->empty()) {
            report(Severity::Error, Code::MissingField, name, "no extension point");
            return;
        }
        field.clear();
        for (auto it = node->begin(); it != node->end(); ++it) {
            const std::string path = keyed(pathOf(name), it.key());
            const std::optional<ExtensionPoint> point = extensionPointNamed(it.key());
            if (!point) {
                m_context.report(Severity::Error, Code::ExtensionPointUnknown, path, it.key());
                continue;
            }
            if (!it.value().is_string()) {
                m_context.report(Severity::Error, Code::WrongType, path, it.value().dump());
                continue;
            }
            const std::optional<VersionRange> range = VersionRange::parse(it.value().get_ref<const std::string&>());
            if (!range) {
                m_context.report(Severity::Error, Code::RangeInvalid, path, it.value().dump());
                continue;
            }
            field.emplace(*point, *range);
        }
    }

    void dependencies(std::string_view name, std::map<std::string, Dependency, std::less<>>& field)
    {
        const Json* node = find(name);
        if (node == nullptr) {
            return;
        }
        if (!node->is_object()) {
            wrongType(name, *node);
            return;
        }
        field.clear();
        for (auto it = node->begin(); it != node->end(); ++it) {
            const std::string path = keyed(pathOf(name), it.key());
            if (!validId(it.key())) {
                m_context.report(Severity::Error, Code::IdInvalid, path, it.key());
                continue;
            }
            Dependency dependency;
            std::string range;
            std::string rangePath = path;
            if (it.value().is_string()) {
                range = it.value().get<std::string>();
            } else if (it.value().is_object()) {
                Reader nested(it.value(), path, m_context);
                nested.text("version", range, {.required = true, .maxLength = 128});
                nested.boolean("optional", dependency.optional);
                nested.finish();
                rangePath = path + ".version";
                if (range.empty()) {
                    continue;
                }
            } else {
                m_context.report(Severity::Error, Code::WrongType, path, it.value().dump());
                continue;
            }
            const std::optional<VersionRange> parsed = VersionRange::parse(range);
            if (!parsed) {
                m_context.report(Severity::Error, Code::RangeInvalid, rangePath, range);
                continue;
            }
            dependency.version = *parsed;
            field.emplace(it.key(), std::move(dependency));
        }
    }

    template <typename T>
    void object(std::string_view name, T& field)
    {
        const Json* node = find(name);
        if (node == nullptr) {
            return;
        }
        if (!node->is_object()) {
            report(Severity::Error, Code::NotAnObject, name, node->dump());
            return;
        }
        Reader nested(*node, pathOf(name), m_context);
        T::visit(nested, field);
        nested.finish();
    }

    template <typename T>
    void optionalObject(std::string_view name, std::optional<T>& field)
    {
        const Json* node = find(name);
        if (node == nullptr) {
            return;
        }
        if (!node->is_object()) {
            report(Severity::Error, Code::NotAnObject, name, node->dump());
            return;
        }
        T entry{};
        Reader nested(*node, pathOf(name), m_context);
        T::visit(nested, entry);
        nested.finish();
        checkEntry(entry, pathOf(name), m_context);
        field = std::move(entry);
    }

    template <typename T>
    void objectList(std::string_view name, std::vector<T>& field, std::size_t maxItems)
    {
        const Json* node = find(name);
        if (node == nullptr) {
            return;
        }
        if (!node->is_array()) {
            wrongType(name, *node);
            return;
        }
        field.clear();
        std::set<std::string, std::less<>> seen;
        for (std::size_t i = 0; i < node->size(); ++i) {
            if (i == maxItems) {
                report(Severity::Error, Code::TooMany, name, std::to_string(maxItems));
                break;
            }
            const Json& item = (*node)[i];
            const std::string path = indexed(pathOf(name), i);
            if (!item.is_object()) {
                m_context.report(Severity::Error, Code::NotAnObject, path, item.dump());
                continue;
            }
            T entry{};
            Reader nested(item, path, m_context);
            T::visit(nested, entry);
            nested.finish();
            checkEntry(entry, path, m_context);
            const auto [key, value] = identity(entry);
            if (!value.empty() && !seen.emplace(value).second) {
                m_context.report(Severity::Error, Code::Duplicate, path + "." + std::string(key), std::string(value));
            }
            field.push_back(std::move(entry));
        }
    }

    void rawJson(std::string_view name, std::string& field)
    {
        const Json* node = find(name);
        if (node == nullptr) {
            return;
        }
        if (!node->is_object() && !node->is_array()) {
            wrongType(name, *node);
            return;
        }
        field = node->dump();
    }

    void anyJson(std::string_view name, std::string& field)
    {
        const Json* node = find(name);
        if (node == nullptr) {
            return;
        }
        if (node->is_null()) {
            wrongType(name, *node);
            return;
        }
        field = node->dump(-1, ' ', false, Json::error_handler_t::replace);
    }

    void textMap(std::string_view name, TextMap& field, const MapRule& rule)
    {
        const Json* node = find(name);
        if (node == nullptr) {
            return;
        }
        if (!node->is_object()) {
            wrongType(name, *node);
            return;
        }
        field.clear();
        std::size_t entries = 0;
        for (auto it = node->begin(); it != node->end(); ++it) {
            if (entries++ == rule.maxEntries) {
                report(Severity::Error, Code::TooMany, name, std::to_string(rule.maxEntries));
                break;
            }
            const std::string path = keyed(pathOf(name), it.key());
            if (!matches(rule.keyFormat, it.key())) {
                m_context.report(Severity::Error, invalidCode(rule.keyFormat), path, it.key());
                continue;
            }
            if (!it.value().is_string()) {
                m_context.report(Severity::Error, Code::WrongType, path, it.value().dump());
                continue;
            }
            const std::string& written = it.value().get_ref<const std::string&>();
            if (characters(written) > rule.maxLength) {
                m_context.report(Severity::Error, Code::TooLong, path);
                continue;
            }
            if (!matches(rule.valueFormat, written)) {
                m_context.report(Severity::Error, invalidCode(rule.valueFormat), path, written);
                continue;
            }
            field.emplace(it.key(), written);
        }
    }

    void textOrMap(std::string_view name, std::string& field, TextMap& fields, const TextRule& rule,
                   const MapRule& mapRule)
    {
        const Json* node = find(name);
        if (node == nullptr || !node->is_object()) {
            text(name, field, rule);
            return;
        }
        if (node->empty()) {
            if (rule.required) {
                report(Severity::Error, rule.missing, name);
            }
            return;
        }
        textMap(name, fields, mapRule);
    }

    template <typename E, std::size_t N>
    void choice(std::string_view name, E& field, const std::array<std::string_view, N>& names, bool required)
    {
        const Json* node = find(name);
        if (node == nullptr) {
            if (required) {
                report(Severity::Error, Code::MissingField, name);
            }
            return;
        }
        if (!node->is_string()) {
            wrongType(name, *node);
            return;
        }
        const auto found = std::ranges::find(names, node->get_ref<const std::string&>());
        if (found == names.end()) {
            report(Severity::Error, Code::OutOfRange, name, node->dump());
            return;
        }
        field = static_cast<E>(found - names.begin());
    }

    void number(std::string_view name, std::optional<double>& field)
    {
        const Json* node = find(name);
        if (node == nullptr) {
            return;
        }
        if (!node->is_number()) {
            wrongType(name, *node);
            return;
        }
        field = node->get<double>();
    }

    void integer(std::string_view name, std::optional<std::int64_t>& field, std::int64_t min, std::int64_t max)
    {
        const Json* node = find(name);
        if (node == nullptr) {
            return;
        }
        if (!node->is_number_integer()) {
            wrongType(name, *node);
            return;
        }
        const std::optional<std::int64_t> parsed = integerOf(*node);
        if (!parsed || *parsed < min || *parsed > max) {
            report(Severity::Error, Code::OutOfRange, name, node->dump());
            return;
        }
        field = *parsed;
    }

    void settingValue(std::string_view name, SettingValue& field, bool required)
    {
        const Json* node = find(name);
        if (node == nullptr) {
            if (required) {
                report(Severity::Error, Code::MissingField, name);
            }
            return;
        }
        if (node->is_boolean()) {
            field = node->get<bool>();
        } else if (node->is_number_integer()) {
            const std::optional<std::int64_t> parsed = integerOf(*node);
            if (!parsed) {
                report(Severity::Error, Code::OutOfRange, name, node->dump());
                return;
            }
            field = *parsed;
        } else if (node->is_number()) {
            field = node->get<double>();
        } else if (node->is_string()) {
            field = node->get<std::string>();
        } else {
            wrongType(name, *node);
        }
    }

    void l10n(std::string_view name, L10n& field)
    {
        const Json* node = find(name);
        if (node == nullptr) {
            return;
        }
        if (!node->is_object()) {
            wrongType(name, *node);
            return;
        }
        field.clear();
        for (auto language = node->begin(); language != node->end(); ++language) {
            const std::string path = keyed(pathOf(name), language.key());
            if (!language.value().is_object()) {
                m_context.report(Severity::Error, Code::NotAnObject, path, language.value().dump());
                continue;
            }
            TextMap texts;
            for (auto entry = language.value().begin(); entry != language.value().end(); ++entry) {
                if (!entry.value().is_string()) {
                    m_context.report(Severity::Error, Code::WrongType, keyed(path, entry.key()), entry.value().dump());
                    continue;
                }
                texts.emplace(entry.key(), entry.value().get<std::string>());
            }
            field.emplace(language.key(), std::move(texts));
        }
    }

private:
    const Json* find(std::string_view name)
    {
        std::string key(name);
        const auto it = m_node.find(key);
        m_visited.push_back(std::move(key));
        return it == m_node.end() ? nullptr : &*it;
    }

    void report(Severity severity, Code code, std::string_view name, std::string detail = {})
    {
        m_context.report(severity, code, pathOf(name), std::move(detail));
    }

    void wrongType(std::string_view name, const Json& node)
    {
        report(Severity::Error, Code::WrongType, name, node.dump());
    }

    [[nodiscard]] std::string pathOf(std::string_view name) const { return m_path + "." + std::string(name); }

    const Json& m_node;
    std::string m_path;
    Context& m_context;
    std::vector<std::string> m_visited;
};

class Writer
{
public:
    explicit Writer(Json& out) : m_out(out) {}

    void schemaVersion(std::string_view name, const std::int64_t& field) { set(name, field); }

    void text(std::string_view name, const std::string& field, const TextRule&)
    {
        if (!field.empty()) {
            set(name, field);
        }
    }

    void boolean(std::string_view name, const bool& field)
    {
        if (field) {
            set(name, true);
        }
    }

    void version(std::string_view name, const Version& field) { set(name, field.text()); }

    void textList(std::string_view name, const std::vector<std::string>& field, const ListRule&)
    {
        if (!field.empty()) {
            set(name, Json(field));
        }
    }

    void requirements(std::string_view name, const std::map<ExtensionPoint, VersionRange>& field)
    {
        Json object = Json::object();
        for (const auto& [point, range] : field) {
            object[std::string(nameOf(point))] = range.text();
        }
        set(name, std::move(object));
    }

    void dependencies(std::string_view name, const std::map<std::string, Dependency, std::less<>>& field)
    {
        if (field.empty()) {
            return;
        }
        Json object = Json::object();
        for (const auto& [id, dependency] : field) {
            object[id] = dependency.optional ? Json{{"version", dependency.version.text()}, {"optional", true}}
                                             : Json(dependency.version.text());
        }
        set(name, std::move(object));
    }

    template <typename T>
    void object(std::string_view name, const T& field)
    {
        Json nested = Json::object();
        Writer writer(nested);
        T::visit(writer, field);
        if (!nested.empty()) {
            set(name, std::move(nested));
        }
    }

    template <typename T>
    void optionalObject(std::string_view name, const std::optional<T>& field)
    {
        if (!field) {
            return;
        }
        Json nested = Json::object();
        Writer writer(nested);
        T::visit(writer, *field);
        set(name, std::move(nested));
    }

    template <typename T>
    void objectList(std::string_view name, const std::vector<T>& field, std::size_t)
    {
        if (field.empty()) {
            return;
        }
        Json array = Json::array();
        for (const T& entry : field) {
            Json item = Json::object();
            Writer writer(item);
            T::visit(writer, entry);
            array.push_back(std::move(item));
        }
        set(name, std::move(array));
    }

    void rawJson(std::string_view name, const std::string& field)
    {
        if (!field.empty()) {
            set(name, Json::parse(field, nullptr, false));
        }
    }

    void anyJson(std::string_view name, const std::string& field)
    {
        if (!field.empty()) {
            set(name, Json::parse(field, nullptr, false));
        }
    }

    void textMap(std::string_view name, const TextMap& field, const MapRule&)
    {
        if (field.empty()) {
            return;
        }
        Json object = Json::object();
        for (const auto& [key, text] : field) {
            object[key] = text;
        }
        set(name, std::move(object));
    }

    void textOrMap(std::string_view name, const std::string& field, const TextMap& fields, const TextRule& rule,
                   const MapRule& mapRule)
    {
        if (!fields.empty()) {
            textMap(name, fields, mapRule);
            return;
        }
        text(name, field, rule);
    }

    template <typename E, std::size_t N>
    void choice(std::string_view name, const E& field, const std::array<std::string_view, N>& names, bool required)
    {
        if (required || field != E{}) {
            set(name, std::string(names[static_cast<std::size_t>(field)]));
        }
    }

    void number(std::string_view name, const std::optional<double>& field)
    {
        if (field) {
            set(name, *field);
        }
    }

    void integer(std::string_view name, const std::optional<std::int64_t>& field, std::int64_t, std::int64_t)
    {
        if (field) {
            set(name, *field);
        }
    }

    void settingValue(std::string_view name, const SettingValue& field, bool)
    {
        std::visit(
            [this, name](const auto& value) {
                if constexpr (!std::is_same_v<std::decay_t<decltype(value)>, std::monostate>) {
                    set(name, value);
                }
            },
            field);
    }

    void l10n(std::string_view name, const L10n& field)
    {
        if (field.empty()) {
            return;
        }
        Json object = Json::object();
        for (const auto& [language, texts] : field) {
            Json entries = Json::object();
            for (const auto& [key, text] : texts) {
                entries[key] = text;
            }
            object[language] = std::move(entries);
        }
        set(name, std::move(object));
    }

private:
    template <typename Value>
    void set(std::string_view name, Value&& value)
    {
        m_out[std::string(name)] = std::forward<Value>(value);
    }

    Json& m_out;
};

Json stringSchema(std::size_t maxLength, TextFormat format, bool required = false)
{
    Json schema = {{"type", "string"}, {"maxLength", maxLength}};
    if (required) {
        schema["minLength"] = 1;
    }
    switch (format) {
    case TextFormat::ExtensionId:
        schema["pattern"] = R"(^[a-z0-9]+(-[a-z0-9]+)*(\.[a-z0-9]+(-[a-z0-9]+)*)+$)";
        break;
    case TextFormat::SettingKey:
        schema["pattern"] = "^[A-Za-z][A-Za-z0-9]*$";
        break;
    case TextFormat::PackagePath:
        schema["pattern"] = R"(^[^/\\:*?"<>|][^\\:*?"<>|]*$)";
        break;
    case TextFormat::HttpsUrl:
        schema["pattern"] = R"(^https://\S+$)";
        break;
    case TextFormat::TokenName:
        schema["pattern"] = "^[a-z][A-Za-z0-9]*$";
        break;
    case TextFormat::Color:
        schema["pattern"] = "^#([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$";
        break;
    case TextFormat::ProfileName:
        schema["pattern"] = "^[A-Za-z0-9_-]+$";
        break;
    case TextFormat::ProfileTarget:
        schema["pattern"] = R"(^[a-z0-9]+(-[a-z0-9]+)*(\.[a-z0-9]+(-[a-z0-9]+)*)+/[A-Za-z0-9_-]{1,64}$)";
        break;
    case TextFormat::NativeName:
        schema["pattern"] = "^[a-z][A-Za-z0-9]*$";
        break;
    case TextFormat::NativeType: {
        Json names = Json::array();
        for (const std::string_view type : kNativeTypeNames) {
            names.push_back(std::string(type));
        }
        schema["enum"] = std::move(names);
        break;
    }
    case TextFormat::SigningKey:
        schema["pattern"] = "^RW[A-Za-z0-9+/]{54}$";
        break;
    case TextFormat::ScriptName:
    case TextFormat::PanelName:
        schema["pattern"] = "^[a-z0-9-]{1,32}$";
        break;
    case TextFormat::InputSourceName:
        schema["pattern"] = "^[a-z][a-z0-9]{0,31}$";
        break;
    case TextFormat::InputSlotName:
        schema["pattern"] = "^[A-Za-z0-9_-]{1,32}$";
        break;
    case TextFormat::PermissionName: {
        Json names = Json::array();
        for (const std::string_view permission : kPermissionNames) {
            names.push_back(std::string(permission));
        }
        schema["enum"] = std::move(names);
        break;
    }
    case TextFormat::Plain:
        break;
    }
    return schema;
}

class SchemaWriter
{
public:
    explicit SchemaWriter(Json& schema) : m_schema(schema)
    {
        m_schema["type"] = "object";
        m_schema["properties"] = Json::object();
        m_schema["additionalProperties"] = false;
    }

    void finish()
    {
        if (!m_required.empty()) {
            m_schema["required"] = m_required;
        }
    }

    void schemaVersion(std::string_view name, const std::int64_t&)
    {
        property(name, {{"const", kSchemaVersion}}, true);
    }

    void text(std::string_view name, const std::string&, const TextRule& rule)
    {
        property(name, stringSchema(rule.maxLength, rule.format, rule.required), rule.required);
    }

    void boolean(std::string_view name, const bool&) { property(name, {{"type", "boolean"}}); }

    void version(std::string_view name, const Version&)
    {
        property(name, {{"type", "string"}, {"pattern", kVersionPattern}}, true);
    }

    void textList(std::string_view name, const std::vector<std::string>&, const ListRule& rule)
    {
        property(name, {{"type", "array"}, {"maxItems", rule.maxItems}, {"items", stringSchema(rule.maxLength, rule.format, true)}});
    }

    void requirements(std::string_view name, const std::map<ExtensionPoint, VersionRange>&)
    {
        Json points = Json::array();
        for (const std::string_view point : kExtensionPointNames) {
            points.push_back(std::string(point));
        }
        property(name,
                 {{"type", "object"},
                  {"minProperties", 1},
                  {"propertyNames", {{"enum", std::move(points)}}},
                  {"additionalProperties", {{"type", "string"}, {"maxLength", 128}}}},
                 true);
    }

    void dependencies(std::string_view name, const std::map<std::string, Dependency, std::less<>>&)
    {
        const Json detailed = {{"type", "object"},
                               {"properties", {{"version", {{"type", "string"}, {"maxLength", 128}}}, {"optional", {{"type", "boolean"}}}}},
                               {"required", Json::array({"version"})},
                               {"additionalProperties", false}};
        property(name, {{"type", "object"},
                        {"propertyNames", stringSchema(kMaxIdLength, TextFormat::ExtensionId)},
                        {"additionalProperties", {{"oneOf", Json::array({{{"type", "string"}, {"maxLength", 128}}, detailed})}}}});
    }

    template <typename T>
    void object(std::string_view name, const T& field)
    {
        Json nested = Json::object();
        SchemaWriter writer(nested);
        T::visit(writer, field);
        writer.finish();
        property(name, std::move(nested));
    }

    template <typename T>
    void optionalObject(std::string_view name, const std::optional<T>&)
    {
        Json nested = Json::object();
        SchemaWriter writer(nested);
        const T fresh{};
        T::visit(writer, fresh);
        writer.finish();
        property(name, std::move(nested));
    }

    template <typename T>
    void objectList(std::string_view name, const std::vector<T>&, std::size_t maxItems)
    {
        Json item = Json::object();
        SchemaWriter writer(item);
        const T fresh{};
        T::visit(writer, fresh);
        writer.finish();
        property(name, {{"type", "array"}, {"maxItems", maxItems}, {"items", std::move(item)}});
    }

    void rawJson(std::string_view name, const std::string&)
    {
        property(name, {{"type", Json::array({"object", "array"})}});
    }

    void anyJson(std::string_view name, const std::string&)
    {
        property(name, {{"type", Json::array({"boolean", "number", "string", "array", "object"})}});
    }

    void textMap(std::string_view name, const TextMap&, const MapRule& rule)
    {
        property(name, {{"type", "object"},
                        {"maxProperties", rule.maxEntries},
                        {"propertyNames", stringSchema(64, rule.keyFormat)},
                        {"additionalProperties", stringSchema(rule.maxLength, rule.valueFormat)}});
    }

    void textOrMap(std::string_view name, const std::string&, const TextMap&, const TextRule& rule,
                   const MapRule& mapRule)
    {
        Json map = {{"type", "object"},
                    {"minProperties", 1},
                    {"maxProperties", mapRule.maxEntries},
                    {"propertyNames", stringSchema(64, mapRule.keyFormat)},
                    {"additionalProperties", stringSchema(mapRule.maxLength, mapRule.valueFormat)}};
        Json forms = Json::array();
        forms.push_back(stringSchema(rule.maxLength, rule.format, rule.required));
        forms.push_back(std::move(map));
        property(name, {{"oneOf", std::move(forms)}}, rule.required);
    }

    template <typename E, std::size_t N>
    void choice(std::string_view name, const E&, const std::array<std::string_view, N>& names, bool required)
    {
        Json values = Json::array();
        for (const std::string_view value : names) {
            values.push_back(std::string(value));
        }
        property(name, {{"enum", std::move(values)}}, required);
    }

    void number(std::string_view name, const std::optional<double>&) { property(name, {{"type", "number"}}); }

    void integer(std::string_view name, const std::optional<std::int64_t>&, std::int64_t min, std::int64_t max)
    {
        property(name, {{"type", "integer"}, {"minimum", min}, {"maximum", max}});
    }

    void settingValue(std::string_view name, const SettingValue&, bool required)
    {
        property(name, {{"type", Json::array({"boolean", "integer", "number", "string"})}}, required);
    }

    void l10n(std::string_view name, const L10n&)
    {
        property(name, {{"type", "object"},
                        {"additionalProperties", {{"type", "object"}, {"additionalProperties", {{"type", "string"}}}}}});
    }

private:
    void property(std::string_view name, Json schema, bool required = false)
    {
        m_schema["properties"][std::string(name)] = std::move(schema);
        if (required) {
            m_required.push_back(std::string(name));
        }
    }

    Json& m_schema;
    Json m_required = Json::array();
};

void checkManifest(const Manifest& manifest, const LoadOptions& options, Context& context)
{
    if (!options.official && manifest.id.starts_with(kReservedIdPrefix)) {
        context.report(Severity::Error, Code::IdReserved, "$.id", manifest.id);
    }
    struct Contributed {
        std::string_view key;
        ExtensionPoint point;
        bool present;
    };
    const Contributions& contributes = manifest.contributes;
    const Contributed contributed[] = {
        {"filters", ExtensionPoint::Filter, !contributes.filters.empty()},
        {"themes", ExtensionPoint::Theme, !contributes.themes.empty()},
        {"profiles", ExtensionPoint::Profile, !contributes.profiles.empty()},
        {"profilePatches", ExtensionPoint::Profile, !contributes.profilePatches.empty()},
        {"scripts", ExtensionPoint::Script, !contributes.scripts.empty()},
        {"commands", ExtensionPoint::Command, !contributes.commands.empty()},
        {"presets", ExtensionPoint::Preset, !contributes.presets.empty()},
        {"native", ExtensionPoint::Native, contributes.native.has_value()},
        {"components", ExtensionPoint::Component, !contributes.components.empty()},
        {"tasks", ExtensionPoint::Task, !contributes.tasks.empty()},
        {"panels", ExtensionPoint::Panel, !contributes.panels.empty()},
        {"help", ExtensionPoint::Help, !contributes.help.empty()},
        {"tours", ExtensionPoint::Tour, !contributes.tours.empty()},
    };
    for (const Contributed& entry : contributed) {
        if (entry.present && !manifest.declares(entry.point)) {
            context.report(Severity::Error, Code::ContributionUndeclared, "$.contributes." + std::string(entry.key),
                           std::string(nameOf(entry.point)));
        }
    }
    const auto hasPanel = [&contributes](std::string_view name, PanelPlace where) {
        return std::ranges::any_of(contributes.panels, [name, where](const PanelContribution& panel) {
            return panel.name == name && panel.where == where;
        });
    };
    const bool provides = contributes.native && !contributes.native->deviceProviders.empty();
    for (std::size_t tour = 0; tour < contributes.tours.size(); ++tour) {
        const std::vector<TourStep>& steps = contributes.tours[tour].steps;
        for (std::size_t step = 0; step < steps.size(); ++step) {
            const std::string_view target = steps[step].target;
            const std::string_view page = target.starts_with("page:") ? target.substr(5) : std::string_view();
            const std::string_view place = target.starts_with("settings:") ? target.substr(9) : std::string_view();
            const bool missing = (target == "device" && !provides) ||
                                 (validPanelName(page) && !hasPanel(page, PanelPlace::Page)) ||
                                 (validPanelName(place) && !hasPanel(place, PanelPlace::Settings));
            if (missing) {
                context.report(Severity::Error, Code::TourTargetInvalid,
                               indexed(indexed("$.contributes.tours", tour) + ".steps", step) + ".target",
                               steps[step].target);
            }
        }
    }
    if (!contributes.panels.empty() && !manifest.declares(ExtensionPoint::Script)) {
        context.report(Severity::Error, Code::ContributionUndeclared, "$.contributes.panels",
                       std::string(nameOf(ExtensionPoint::Script)));
    }
    std::size_t menus = 0;
    for (std::size_t index = 0; index < contributes.panels.size(); ++index) {
        if (contributes.panels[index].where == PanelPlace::Menu && ++menus > 1) {
            context.report(Severity::Error, Code::Duplicate, indexed("$.contributes.panels", index) + ".where",
                           std::string(kPanelPlaceNames[static_cast<std::size_t>(PanelPlace::Menu)]));
        }
    }
    const bool mayAsk = manifest.declares(ExtensionPoint::Script) || manifest.declares(ExtensionPoint::Component) ||
                        manifest.declares(ExtensionPoint::Task) || manifest.declares(ExtensionPoint::Native);
    if (!manifest.permissions.empty() && !mayAsk) {
        context.report(Severity::Error, Code::PermissionNotAllowed, "$.permissions");
    }
    if (contributes.native) {
        const auto checkNeeded = [&](const std::vector<std::string>& needed, const std::string& at) {
            for (std::size_t index = 0; index < needed.size(); ++index) {
                const std::string path = indexed(at, index);
                if (std::ranges::find(kPermissionNames, needed[index]) == kPermissionNames.end()) {
                    context.report(Severity::Error,
                                   needed[index] == kNeverGrantedPermission ? Code::PermissionNeverGranted
                                                                            : Code::PermissionUnknown,
                                   path, needed[index]);
                } else if (std::ranges::find(manifest.permissions, needed[index], &Permission::name) ==
                           manifest.permissions.end()) {
                    context.report(Severity::Error, Code::NativePermissionUndeclared, path, needed[index]);
                }
            }
        };
        for (std::size_t node = 0; node < contributes.native->nodes.size(); ++node) {
            checkNeeded(contributes.native->nodes[node].permissions,
                        "$.contributes.native.nodes[" + std::to_string(node) + "].permissions");
        }
        for (std::size_t job = 0; job < contributes.native->jobs.size(); ++job) {
            const std::string at = "$.contributes.native.jobs[" + std::to_string(job) + "]";
            checkNeeded(contributes.native->jobs[job].permissions, at + ".permissions");
            const std::string& name = contributes.native->jobs[job].name;
            if (std::ranges::find(contributes.native->nodes, name, &NativeNode::name) != contributes.native->nodes.end()) {
                context.report(Severity::Error, Code::Duplicate, at + ".name", name);
            }
        }
        if (!contributes.native->deviceProviders.empty() &&
            std::ranges::find(manifest.permissions, kDevicesPermission, &Permission::name) == manifest.permissions.end()) {
            context.report(Severity::Error, Code::NativePermissionUndeclared, "$.contributes.native.deviceProviders",
                           std::string(kDevicesPermission));
        }
        const NativeContribution& native = *contributes.native;
        const auto jobTakes = [&native](std::string_view name, std::initializer_list<NativeParam> wanted) {
            const auto job = std::ranges::find(native.jobs, name, &NativeJob::name);
            return job != native.jobs.end() && job->params.size() == wanted.size() &&
                   std::ranges::all_of(wanted, [&job](const NativeParam& param) {
                       return std::ranges::find(job->params, param) != job->params.end();
                   });
        };
        for (std::size_t engine = 0; engine < native.speechEngines.size(); ++engine) {
            const NativeSpeechEngine& declared = native.speechEngines[engine];
            const std::string at = indexed("$.contributes.native.speechEngines", engine);
            if (!declared.say.empty() && !jobTakes(declared.say, {{"text", "string"},
                                                                  {"voice", "string"},
                                                                  {"rate", "number"},
                                                                  {"volume", "number"}})) {
                context.report(Severity::Error, Code::SpeechEngineInvalid, at + ".say", declared.say);
            }
            if (!declared.listen.empty() &&
                !jobTakes(declared.listen, {{"phrases", "json"}, {"language", "string"}, {"confidence", "number"}})) {
                context.report(Severity::Error, Code::SpeechEngineInvalid, at + ".listen", declared.listen);
            }
            const auto voices = std::ranges::find(native.nodes, declared.voices, &NativeNode::name);
            if (!declared.voices.empty() &&
                (voices == native.nodes.end() || !voices->params.empty() || voices->returns != "json")) {
                context.report(Severity::Error, Code::SpeechEngineInvalid, at + ".voices", declared.voices);
            }
        }
        if (!contributes.native->inputSources.empty() &&
            std::ranges::find(manifest.permissions, kInputPermission, &Permission::name) == manifest.permissions.end()) {
            context.report(Severity::Error, Code::NativePermissionUndeclared, "$.contributes.native.inputSources",
                           std::string(kInputPermission));
        }
        if (!contributes.native->transports.empty() &&
            std::ranges::find(manifest.permissions, kTransportPermission, &Permission::name) ==
                manifest.permissions.end()) {
            context.report(Severity::Error, Code::NativePermissionUndeclared, "$.contributes.native.transports",
                           std::string(kTransportPermission));
        }
        const NativeContribution& media = *contributes.native;
        const std::pair<std::string_view, bool> mediaNeeds[] = {
            {"videoSources", !media.videoSources.empty()},
            {"audioSources", !media.audioSources.empty()},
            {"mediaSinks", !media.mediaSinks.empty()},
            {"recordingExporters", !media.recordingExporters.empty()},
        };
        for (const auto& [key, present] : mediaNeeds) {
            const std::string_view needed =
                key == "videoSources" || key == "audioSources" ? kMediaCapturePermission : kMediaOutputPermission;
            if (present &&
                std::ranges::find(manifest.permissions, needed, &Permission::name) == manifest.permissions.end()) {
                context.report(Severity::Error, Code::NativePermissionUndeclared,
                               "$.contributes.native." + std::string(key), std::string(needed));
            }
        }
        if (!contributes.native->frameTaps.empty() &&
            std::ranges::find(manifest.permissions, kMediaReadPermission, &Permission::name) ==
                manifest.permissions.end()) {
            context.report(Severity::Error, Code::NativePermissionUndeclared, "$.contributes.native.frameTaps",
                           std::string(kMediaReadPermission));
        }
    }
    for (std::size_t index = 0; index < contributes.services.size(); ++index) {
        const ServiceContribution& service = contributes.services[index];
        const std::string at = indexed("$.contributes.services", index);
        if (!service.script.empty()) {
            const auto script = std::ranges::find(contributes.scripts, service.script, &ScriptContribution::name);
            if (!manifest.declares(ExtensionPoint::Script)) {
                context.report(Severity::Error, Code::ContributionUndeclared, at + ".script",
                               std::string(nameOf(ExtensionPoint::Script)));
            } else if (script == contributes.scripts.end()) {
                context.report(Severity::Error, Code::ServiceTargetInvalid, at + ".script",
                               "no script is named " + service.script);
            } else if (script->run != ScriptRun::App) {
                context.report(Severity::Error, Code::ServiceTargetInvalid, at + ".script",
                               "the script " + service.script + " runs for each phone, not once for the app");
            }
        }
        if (!service.node.empty()) {
            const std::vector<NativeNode> none;
            const std::vector<NativeNode>& nodes = contributes.native ? contributes.native->nodes : none;
            const auto node = std::ranges::find(nodes, service.node, &NativeNode::name);
            if (!manifest.declares(ExtensionPoint::Native)) {
                context.report(Severity::Error, Code::ContributionUndeclared, at + ".node",
                               std::string(nameOf(ExtensionPoint::Native)));
            } else if (node == nodes.end()) {
                context.report(Severity::Error, Code::ServiceTargetInvalid, at + ".node",
                               "no native node is named " + service.node);
            } else if (node->params.size() != 1 || node->params[0].type != "json" || node->returns != "json") {
                context.report(Severity::Error, Code::ServiceTargetInvalid, at + ".node",
                               "the node " + service.node + " does not take one json value and return json");
            }
        }
    }
    if (!manifest.id.empty() && manifest.dependencies.contains(manifest.id)) {
        context.report(Severity::Error, Code::DependencyCycle, keyed("$.dependencies", manifest.id), manifest.id);
    }
}

}

std::optional<ExtensionPoint> extensionPointNamed(std::string_view name) noexcept
{
    const auto found = std::ranges::find(kExtensionPointNames, name);
    if (found == kExtensionPointNames.end()) {
        return std::nullopt;
    }
    return static_cast<ExtensionPoint>(found - kExtensionPointNames.begin());
}

std::string_view nameOf(ExtensionPoint point) noexcept
{
    return kExtensionPointNames[static_cast<std::size_t>(point)];
}

std::string_view hostArchitecture() noexcept
{
#if defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#elif defined(_M_X64) || defined(_M_AMD64) || defined(__x86_64__)
    return "x64";
#else
    return {};
#endif
}

std::vector<std::pair<std::string, std::string>> nativeDlls(const NativeContribution& native)
{
    std::vector<std::pair<std::string, std::string>> found;
    if (!native.dlls.empty()) {
        for (const auto& [architecture, file] : native.dlls) {
            found.emplace_back(architecture, file);
        }
    } else if (!native.dll.empty()) {
        found.emplace_back(std::string(), native.dll);
    }
    return found;
}

std::string nativeDllFor(const NativeContribution& native, std::string_view architecture)
{
    if (native.dlls.empty()) {
        return native.dll;
    }
    const auto found = native.dlls.find(architecture);
    return found != native.dlls.end() ? found->second : std::string();
}

std::string nativeDllField(std::string_view architecture)
{
    const std::string base = "$.contributes.native.dll";
    return architecture.empty() ? base : base + "[\"" + std::string(architecture) + "\"]";
}

LoadedManifest loadManifest(std::string_view json, const LoadOptions& options)
{
    LoadedManifest result;
    Context context{result.diagnostics};
    if (json.size() > kMaxManifestBytes || !nestingWithinLimit(json)) {
        context.report(Severity::Error, Code::TooLarge, "$");
        return result;
    }
    const Json root = Json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded()) {
        context.report(Severity::Error, Code::MalformedJson, "$");
        return result;
    }
    if (!root.is_object()) {
        context.report(Severity::Error, Code::NotAnObject, "$", root.type_name());
        return result;
    }
    Reader reader(root, "$", context);
    Manifest::visit(reader, result.manifest);
    reader.finish();
    checkManifest(result.manifest, options, context);
    context.finish();
    result.usable = !hasErrors(result.diagnostics);
    return result;
}

std::string saveManifest(const Manifest& manifest)
{
    Json out = Json::object();
    Writer writer(out);
    Manifest::visit(writer, manifest);
    return out.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

std::string manifestJsonSchema()
{
    Json schema = {{"$schema", "https://json-schema.org/draft/2020-12/schema"},
                   {"$id", "urn:gamescrcpy:extension-manifest:1"},
                   {"title", "GameScrcpy extension manifest"}};
    SchemaWriter writer(schema);
    const Manifest fresh{};
    Manifest::visit(writer, fresh);
    writer.finish();
    return schema.dump(2) + "\n";
}

}
