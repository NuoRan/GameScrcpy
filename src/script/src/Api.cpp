#include "Api.h"

#include "Engine.h"

#include "gs/script/Resources.h"
#include "gs/script/Services.h"
#include "gs/script/TextSearch.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace gs::script::detail::api {

namespace {

constexpr int kFlags = JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE;
constexpr double kTapMs = 60.0;
constexpr double kSwipeMs = 300.0;
constexpr double kLongestGestureMs = 60'000.0;
constexpr std::size_t kStoreQuota = std::size_t{1} << 20;
constexpr std::size_t kMostToastChars = 100;
constexpr std::size_t kMostQuestionChars = 200;
constexpr std::size_t kMostChoiceChars = 60;
constexpr std::size_t kMostAnswerChars = 200;
constexpr std::size_t kMostChoices = 8;
constexpr double kLongestDrawingMs = 600'000.0;
constexpr double kLongestFindMs = 10'000.0;
constexpr int kMostFound = 64;
constexpr std::size_t kMostTypedBytes = 65536;
constexpr std::size_t kMostClipboardBytes = 262'000;
constexpr std::size_t kMostGlobalBytes = std::size_t{64} << 10;
constexpr std::string_view kActionType = "Action<";

constexpr std::array<std::string_view, 24> kKeys{
    "back",      "home",       "appSwitch",  "menu",     "power",    "volumeUp",       "volumeDown", "volumeMute",
    "enter",     "delete",     "tab",        "space",    "escape",   "up",             "down",       "left",
    "right",     "pageUp",     "pageDown",   "search",   "camera",   "mediaPlayPause", "notifications",
    "settings",
};

constexpr std::array<std::uint32_t, kKeys.size()> kKeycodes{
    4,  3,  187, 82, 26, 24, 25, 164, 66, 67, 61, 62, 111, 19, 20, 21,
    22, 92, 93,  84, 27, 85, 83, 176,
};

JSValueConst argument(int argc, JSValueConst* argv, int index) noexcept
{
    return index < argc ? argv[index] : JS_UNDEFINED;
}

JSValue throwCoded(JSContext* ctx, std::string_view code, std::string_view text, bool range)
{
    const std::string message = std::string(code) + " " + std::string(text);
    return range ? JS_ThrowRangeError(ctx, "%s", message.c_str()) : JS_ThrowTypeError(ctx, "%s", message.c_str());
}

ScriptServices* servicesOf(JSContext* ctx)
{
    ScriptServices* services = Engine::of(ctx).services();
    if (services == nullptr) {
        JS_ThrowInternalError(ctx, "%s", "GS-API-001 no phone is connected to this script");
    }
    return services;
}

bool ratio(JSContext* ctx, JSValueConst value, std::string_view what, double& out)
{
    if (JS_ToFloat64(ctx, &out, value) < 0) {
        return false;
    }
    if (!(out >= 0.0 && out <= 1.0)) {
        throwCoded(ctx, "GS-API-002", std::string(what) + " must be a picture ratio from 0 to 1", true);
        return false;
    }
    return true;
}

bool duration(JSContext* ctx, JSValueConst value, double& out)
{
    if (JS_IsUndefined(value)) {
        return true;
    }
    if (JS_ToFloat64(ctx, &out, value) < 0) {
        return false;
    }
    if (!(out > 0.0 && out <= kLongestGestureMs)) {
        throwCoded(ctx, "GS-API-003", "the duration must be more than 0 and at most 60000 milliseconds", true);
        return false;
    }
    return true;
}

Microseconds millis(double ms) noexcept
{
    return Microseconds{static_cast<std::int64_t>(std::ceil(ms * 1000.0))};
}

bool text(JSContext* ctx, JSValueConst value, std::string& out)
{
    if (!JS_IsString(value)) {
        return false;
    }
    std::size_t length = 0;
    const char* chars = JS_ToCStringLen(ctx, &length, value);
    if (chars == nullptr) {
        return false;
    }
    out.assign(chars, length);
    JS_FreeCString(ctx, chars);
    return true;
}

bool define(JSContext* ctx, JSValueConst target, std::string_view name, JSValue value, int flags)
{
    if (JS_IsException(value)) {
        return false;
    }
    const std::string key(name);
    return JS_DefinePropertyValueStr(ctx, target, key.c_str(), value, flags) >= 0;
}

JSValue sleepNative(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv, int) noexcept
{
    return Engine::jsSleep(ctx, self, argc, argv);
}

JSValue nextFrameNative(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv, int) noexcept
{
    return Engine::jsNextFrame(ctx, self, argc, argv);
}

JSValue nowNative(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv, int) noexcept
{
    return Engine::jsNow(ctx, self, argc, argv);
}

JSValue tapNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        double x = 0.0;
        double y = 0.0;
        double ms = kTapMs;
        if (!ratio(ctx, argument(argc, argv, 0), "x", x) || !ratio(ctx, argument(argc, argv, 1), "y", y)
            || !duration(ctx, argument(argc, argv, 2), ms)) {
            return JS_EXCEPTION;
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        return Engine::of(ctx).gesture(
            ctx, millis(ms), [services, at = Point{x, y}, ms](std::uint64_t id) { services->tap(id, at, ms); },
            [services](std::uint64_t id) { services->lift(id); });
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue swipeNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        Point from;
        Point to;
        double ms = kSwipeMs;
        if (!ratio(ctx, argument(argc, argv, 0), "fromX", from.x) || !ratio(ctx, argument(argc, argv, 1), "fromY", from.y)
            || !ratio(ctx, argument(argc, argv, 2), "toX", to.x) || !ratio(ctx, argument(argc, argv, 3), "toY", to.y)
            || !duration(ctx, argument(argc, argv, 4), ms)) {
            return JS_EXCEPTION;
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        return Engine::of(ctx).gesture(
            ctx, millis(ms), [services, from, to, ms](std::uint64_t id) { services->swipe(id, from, to, ms); },
            [services](std::uint64_t id) { services->lift(id); });
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue pressKeyNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::string name;
        if (!text(ctx, argument(argc, argv, 0), name)
            || std::ranges::find(kKeys, std::string_view(name)) == kKeys.end()) {
            return throwCoded(ctx, "GS-API-004", "unknown Android key; use one of the names in AndroidKey", false);
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        services->key(name, true);
        return Engine::of(ctx).delay(ctx, millis(kTapMs), [services, name] { services->key(name, false); });
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue controlList(JSContext* ctx, const std::vector<ControlInfo>& controls)
{
    JSValue list = JS_NewArray(ctx);
    std::uint32_t index = 0;
    for (const ControlInfo& control : controls) {
        JSValue entry = JS_NewObject(ctx);
        const bool made =
            define(ctx, entry, "id", JS_NewStringLen(ctx, control.id.data(), control.id.size()), JS_PROP_ENUMERABLE)
            && define(ctx, entry, "name", JS_NewStringLen(ctx, control.name.data(), control.name.size()),
                      JS_PROP_ENUMERABLE)
            && define(ctx, entry, "type", JS_NewStringLen(ctx, control.type.data(), control.type.size()),
                      JS_PROP_ENUMERABLE)
            && define(ctx, entry, "enabled", JS_NewBool(ctx, control.enabled), JS_PROP_ENUMERABLE)
            && define(ctx, entry, "props", JS_NewStringLen(ctx, control.props.data(), control.props.size()), 0);
        if (!made || JS_SetPropertyUint32(ctx, list, index++, entry) < 0) {
            JS_FreeValue(ctx, list);
            return JS_EXCEPTION;
        }
    }
    return list;
}

JSValue varValue(JSContext* ctx, const VarValue& value)
{
    if (const bool* on = std::get_if<bool>(&value)) {
        return JS_NewBool(ctx, *on);
    }
    if (const double* number = std::get_if<double>(&value)) {
        return JS_NewFloat64(ctx, *number);
    }
    const std::string& words = std::get<std::string>(value);
    return JS_NewStringLen(ctx, words.data(), words.size());
}

bool varOf(JSContext* ctx, JSValueConst value, VarValue& out)
{
    if (JS_IsBool(value)) {
        out = JS_ToBool(ctx, value) != 0;
        return true;
    }
    if (JS_IsNumber(value)) {
        double number = 0.0;
        if (JS_ToFloat64(ctx, &number, value) < 0 || !std::isfinite(number)) {
            return false;
        }
        out = number;
        return true;
    }
    std::string words;
    if (text(ctx, value, words)) {
        out = std::move(words);
        return true;
    }
    return false;
}

JSValue parsedJson(JSContext* ctx, const std::string& json)
{
    JSValue value = JS_ParseJSON(ctx, json.c_str(), json.size(), "<host>");
    if (JS_IsException(value)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return JS_NULL;
    }
    return value;
}

JSValue deviceInfoNative(JSContext* ctx, JSValueConst, int, JSValueConst*, int) noexcept
{
    try {
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        const DeviceInfo info = services->device();
        JSValue result = JS_NewObject(ctx);
        const bool made = define(ctx, result, "model", JS_NewStringLen(ctx, info.model.data(), info.model.size()),
                                 JS_PROP_ENUMERABLE)
                       && define(ctx, result, "android", JS_NewInt32(ctx, info.android), JS_PROP_ENUMERABLE)
                       && define(ctx, result, "width", JS_NewInt32(ctx, info.width), JS_PROP_ENUMERABLE)
                       && define(ctx, result, "height", JS_NewInt32(ctx, info.height), JS_PROP_ENUMERABLE)
                       && define(ctx, result, "rotation", JS_NewInt32(ctx, info.rotation), JS_PROP_ENUMERABLE);
        if (!made) {
            JS_FreeValue(ctx, result);
            return JS_EXCEPTION;
        }
        return result;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue controlsNative(JSContext* ctx, JSValueConst, int, JSValueConst*, int) noexcept
{
    try {
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        return controlList(ctx, services->controls());
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue hostControlsNative(JSContext* ctx, JSValueConst, int, JSValueConst*, int) noexcept
{
    try {
        ScriptServices* services = Engine::of(ctx).services();
        return controlList(ctx, services != nullptr ? services->controls() : std::vector<ControlInfo>{});
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue hostStatesNative(JSContext* ctx, JSValueConst, int, JSValueConst*, int) noexcept
{
    try {
        ScriptServices* services = Engine::of(ctx).services();
        JSValue list = JS_NewArray(ctx);
        if (services == nullptr) {
            return list;
        }
        std::uint32_t index = 0;
        for (const StateInfo& state : services->states()) {
            JSValue entry = JS_NewObject(ctx);
            const bool made =
                define(ctx, entry, "id", JS_NewStringLen(ctx, state.id.data(), state.id.size()), JS_PROP_ENUMERABLE)
                && define(ctx, entry, "name", JS_NewStringLen(ctx, state.name.data(), state.name.size()),
                          JS_PROP_ENUMERABLE)
                && define(ctx, entry, "value", varValue(ctx, state.value), JS_PROP_ENUMERABLE);
            if (!made || JS_SetPropertyUint32(ctx, list, index++, entry) < 0) {
                JS_FreeValue(ctx, list);
                return JS_EXCEPTION;
            }
        }
        return list;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue stateValueNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::string id;
        if (!text(ctx, argument(argc, argv, 0), id)) {
            return throwCoded(ctx, "GS-API-010", "the state id must be a string", false);
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        const std::optional<VarValue> value = services->stateValue(id);
        if (!value) {
            return throwCoded(ctx, "GS-API-010", "no state with the id '" + id + "' in the profile in use", false);
        }
        return varValue(ctx, *value);
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue setStateNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::string id;
        if (!text(ctx, argument(argc, argv, 0), id)) {
            return throwCoded(ctx, "GS-API-010", "the state id must be a string", false);
        }
        VarValue value;
        if (!varOf(ctx, argument(argc, argv, 1), value)) {
            return throwCoded(ctx, "GS-API-010", "a variable holds true or false, a finite number or a string", false);
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        if (!services->setState(id, value)) {
            return throwCoded(ctx, "GS-API-010", "no state with the id '" + id + "' in the profile in use", false);
        }
        return JS_UNDEFINED;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue spawnNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        const JSValueConst function = argument(argc, argv, 0);
        std::string label;
        if (!JS_IsFunction(ctx, function) || !text(ctx, argument(argc, argv, 1), label)) {
            return JS_ThrowTypeError(ctx, "%s", "spawn(function, label, extra) needs a function and a label");
        }
        return Engine::of(ctx).spawn(ctx, function, label, argument(argc, argv, 2));
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue cancelNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::int64_t id = 0;
        if (JS_ToInt64(ctx, &id, argument(argc, argv, 0)) < 0) {
            return JS_EXCEPTION;
        }
        if (id > 0) {
            Engine::of(ctx).cancelFromScript(ctx, static_cast<TaskId>(id));
        }
        return JS_UNDEFINED;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue currentTaskNative(JSContext* ctx, JSValueConst, int, JSValueConst*, int) noexcept
{
    return JS_NewInt64(ctx, static_cast<std::int64_t>(Engine::of(ctx).currentTask()));
}

JSValue wallNowNative(JSContext* ctx, JSValueConst, int, JSValueConst*, int) noexcept
{
    try {
        return JS_NewFloat64(ctx, Engine::of(ctx).wallMilliseconds());
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue randomNative(JSContext* ctx, JSValueConst, int, JSValueConst*, int) noexcept
{
    return JS_NewFloat64(ctx, Engine::of(ctx).randomNumber());
}

JSValue holdControlNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int hold) noexcept
{
    try {
        std::string id;
        if (!text(ctx, argument(argc, argv, 0), id)) {
            return throwCoded(ctx, "GS-API-005", "the control id must be a string", false);
        }
        double ms = kTapMs;
        if (hold != 0) {
            ms = 0.0;
            if (JS_IsUndefined(argument(argc, argv, 1))) {
                return throwCoded(ctx, "GS-API-003", "hold needs a duration in milliseconds", true);
            }
            if (!duration(ctx, argument(argc, argv, 1), ms)) {
                return JS_EXCEPTION;
            }
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        if (!services->pressControl(id, true)) {
            return throwCoded(ctx, "GS-API-005", "no control with the id '" + id + "' in the profile in use", false);
        }
        return Engine::of(ctx).delay(ctx, millis(ms), [services, id] {
            static_cast<void>(services->pressControl(id, false));
        });
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue enableControlNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int fromScript) noexcept
{
    try {
        std::string id;
        if (!text(ctx, argument(argc, argv, 0), id)) {
            return throwCoded(ctx, "GS-API-005", "the control id must be a string", false);
        }
        const int on = JS_ToBool(ctx, argument(argc, argv, 1));
        if (on < 0) {
            return JS_EXCEPTION;
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        if (fromScript != 0 && !Engine::of(ctx).releaseBinding(ctx, id)) {
            return JS_EXCEPTION;
        }
        if (!services->enableControl(id, on != 0)) {
            return throwCoded(ctx, "GS-API-005", "no control with the id '" + id + "' in the profile in use", false);
        }
        return JS_UNDEFINED;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

bool saveStore(JSContext* ctx, JSValueConst store)
{
    JSValue json = JS_JSONStringify(ctx, store, JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(json)) {
        return false;
    }
    std::string contents;
    const bool read = text(ctx, json, contents);
    JS_FreeValue(ctx, json);
    if (!read) {
        return false;
    }
    if (contents.size() > kStoreQuota) {
        throwCoded(ctx, "GS-API-007", "the store is over its 1 MiB quota; remove keys you no longer need", true);
        return false;
    }
    ScriptServices* services = servicesOf(ctx);
    if (services == nullptr) {
        return false;
    }
    services->saveStore(Engine::of(ctx).ownerOf(ctx), contents);
    return true;
}

JSValue copyOf(JSContext* ctx, JSValueConst value, bool& valid)
{
    valid = true;
    JSValue json = JS_JSONStringify(ctx, value, JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(json)) {
        return JS_EXCEPTION;
    }
    if (!JS_IsString(json)) {
        JS_FreeValue(ctx, json);
        valid = false;
        return JS_UNDEFINED;
    }
    std::string contents;
    const bool read = text(ctx, json, contents);
    JS_FreeValue(ctx, json);
    if (!read) {
        return JS_EXCEPTION;
    }
    return JS_ParseJSON(ctx, contents.c_str(), contents.size(), "<store>");
}

JSValue storeNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int operation) noexcept
{
    try {
        JSValue store = Engine::of(ctx).stores().object(ctx);
        if (JS_IsException(store)) {
            return JS_EXCEPTION;
        }
        if (operation == 3) {
            JSPropertyEnum* names = nullptr;
            std::uint32_t count = 0;
            const int listed =
                JS_GetOwnPropertyNames(ctx, &names, &count, store, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY);
            JS_FreeValue(ctx, store);
            if (listed < 0) {
                return JS_EXCEPTION;
            }
            JSValue keys = JS_NewArray(ctx);
            for (std::uint32_t i = 0; i < count && !JS_IsException(keys); ++i) {
                if (JS_SetPropertyUint32(ctx, keys, i, JS_AtomToString(ctx, names[i].atom)) < 0) {
                    JS_FreeValue(ctx, keys);
                    keys = JS_EXCEPTION;
                }
            }
            JS_FreePropertyEnum(ctx, names, count);
            return keys;
        }
        std::string key;
        if (!text(ctx, argument(argc, argv, 0), key)) {
            JS_FreeValue(ctx, store);
            return throwCoded(ctx, "GS-API-006", "store keys must be strings", false);
        }
        JSValue result = JS_UNDEFINED;
        if (operation == 0) {
            JSValue value = JS_GetPropertyStr(ctx, store, key.c_str());
            bool valid = true;
            result = JS_IsUndefined(value) ? JS_UNDEFINED : copyOf(ctx, value, valid);
            JS_FreeValue(ctx, value);
        } else if (operation == 1) {
            bool valid = true;
            JSValue copy = copyOf(ctx, argument(argc, argv, 1), valid);
            if (!valid) {
                JS_FreeValue(ctx, store);
                return throwCoded(ctx, "GS-API-006", "store values must be JSON values (no functions or undefined)",
                                  false);
            }
            JSValue previous = JS_GetPropertyStr(ctx, store, key.c_str());
            if (JS_IsException(copy) || JS_IsException(previous)
                || JS_SetPropertyStr(ctx, store, key.c_str(), copy) < 0) {
                JS_FreeValue(ctx, previous);
                JS_FreeValue(ctx, store);
                return JS_EXCEPTION;
            }
            if (!saveStore(ctx, store)) {
                JSValue failure = JS_GetException(ctx);
                if (JS_IsUndefined(previous)) {
                    const JSAtom atom = JS_NewAtomLen(ctx, key.data(), key.size());
                    static_cast<void>(JS_DeleteProperty(ctx, store, atom, 0));
                    JS_FreeAtom(ctx, atom);
                } else {
                    static_cast<void>(JS_SetPropertyStr(ctx, store, key.c_str(), JS_DupValue(ctx, previous)));
                }
                JS_FreeValue(ctx, previous);
                JS_FreeValue(ctx, store);
                return JS_Throw(ctx, failure);
            }
            JS_FreeValue(ctx, previous);
        } else {
            const JSAtom atom = JS_NewAtomLen(ctx, key.data(), key.size());
            const int deleted = JS_DeleteProperty(ctx, store, atom, 0);
            JS_FreeAtom(ctx, atom);
            if (deleted < 0 || !saveStore(ctx, store)) {
                JS_FreeValue(ctx, store);
                return JS_EXCEPTION;
            }
        }
        JS_FreeValue(ctx, store);
        return result;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

std::string clipped(std::string value, std::size_t mostChars)
{
    std::size_t chars = 0;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if ((static_cast<unsigned char>(value[i]) & 0xC0U) == 0x80U) {
            continue;
        }
        if (chars == mostChars) {
            value.resize(i);
            value += "\xE2\x80\xA6";
            return value;
        }
        ++chars;
    }
    return value;
}

bool optionText(JSContext* ctx, JSValueConst options, const char* name, std::size_t mostChars, std::string& out)
{
    JSValue value = JS_GetPropertyStr(ctx, options, name);
    if (JS_IsException(value)) {
        return false;
    }
    bool ok = true;
    if (!JS_IsUndefined(value)) {
        ok = text(ctx, value, out);
        out = clipped(std::move(out), mostChars);
    }
    JS_FreeValue(ctx, value);
    return ok;
}

JSValue toastNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::string message;
        if (!text(ctx, argument(argc, argv, 0), message)) {
            return throwCoded(ctx, "GS-API-011", "the message must be a string", false);
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        services->toast(clipped(std::move(message), kMostToastChars));
        return JS_UNDEFINED;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue promptNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int kind) noexcept
{
    try {
        Prompt prompt;
        prompt.kind = static_cast<PromptKind>(kind);
        if (!text(ctx, argument(argc, argv, 0), prompt.question) || prompt.question.empty()) {
            return throwCoded(ctx, "GS-API-011", "the question must be a string that is not empty", false);
        }
        prompt.question = clipped(std::move(prompt.question), kMostQuestionChars);
        JSValueConst second = argument(argc, argv, 1);
        if (prompt.kind == PromptKind::Choose) {
            std::int64_t count = 0;
            JSValue length = JS_IsArray(second) ? JS_GetPropertyStr(ctx, second, "length") : JS_UNDEFINED;
            const bool counted = JS_IsNumber(length) && JS_ToInt64(ctx, &count, length) == 0;
            JS_FreeValue(ctx, length);
            if (!counted || count < 2 || count > static_cast<std::int64_t>(kMostChoices)) {
                return throwCoded(ctx, "GS-API-012", "the choices must be two to eight strings", false);
            }
            for (std::int64_t i = 0; i < count; ++i) {
                JSValue choice = JS_GetPropertyUint32(ctx, second, static_cast<std::uint32_t>(i));
                std::string label;
                const bool read = text(ctx, choice, label);
                JS_FreeValue(ctx, choice);
                if (!read || label.empty()) {
                    return throwCoded(ctx, "GS-API-012", "the choices must be two to eight strings", false);
                }
                prompt.choices.push_back(clipped(std::move(label), kMostChoiceChars));
            }
        } else if (!JS_IsUndefined(second)) {
            if (!JS_IsObject(second)
                || !optionText(ctx, second, "ok", kMostChoiceChars, prompt.ok)
                || !optionText(ctx, second, "cancel", kMostChoiceChars, prompt.cancel)
                || !optionText(ctx, second, "initial", kMostAnswerChars, prompt.initial)) {
                return throwCoded(ctx, "GS-API-011", "the options must be an object of strings", false);
            }
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        return Engine::of(ctx).request(
            ctx, [services, &prompt](std::uint64_t id) { services->ask(id, prompt); },
            [services](std::uint64_t id) { services->withdraw(id); });
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

constexpr std::string_view kReadRefused =
    "this logic may not read your keys and cursor yet; allow it in the mirror window";

JSValue cursorNative(JSContext* ctx, JSValueConst, int, JSValueConst*, int) noexcept
{
    try {
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        if (!services->readsInput()) {
            return throwCoded(ctx, "GS-API-098", kReadRefused, false);
        }
        const std::optional<Point> at = services->cursor();
        if (!at) {
            return JS_NULL;
        }
        JSValue point = JS_NewObject(ctx);
        if (!define(ctx, point, "x", JS_NewFloat64(ctx, at->x), JS_PROP_ENUMERABLE)
            || !define(ctx, point, "y", JS_NewFloat64(ctx, at->y), JS_PROP_ENUMERABLE)) {
            JS_FreeValue(ctx, point);
            return JS_EXCEPTION;
        }
        return point;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue keyDownNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::string name;
        if (!text(ctx, argument(argc, argv, 0), name)) {
            return throwCoded(ctx, "GS-API-013", "the key name must be a string, such as \"KeyW\"", false);
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        if (!services->readsInput()) {
            return throwCoded(ctx, "GS-API-098", kReadRefused, false);
        }
        return JS_NewBool(ctx, services->keyDown(name));
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue deviceKeyNative(JSContext* ctx, JSValueConst, int, JSValueConst*, int key) noexcept
{
    try {
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        const std::string name(kKeys[static_cast<std::size_t>(key)]);
        services->key(name, true);
        return Engine::of(ctx).delay(ctx, millis(kTapMs), [services, name] { services->key(name, false); });
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue typeNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::string words;
        if (!text(ctx, argument(argc, argv, 0), words)) {
            return throwCoded(ctx, "GS-API-011", "the text must be a string", false);
        }
        if (words.size() > kMostTypedBytes) {
            return throwCoded(ctx, "GS-API-011", "the text must be at most 65536 bytes", true);
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        services->type(words);
        return JS_UNDEFINED;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue clipboardReadNative(JSContext* ctx, JSValueConst, int, JSValueConst*, int) noexcept
{
    try {
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        return Engine::of(ctx).request(ctx, [services](std::uint64_t id) { services->readClipboard(id); }, {});
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue clipboardWriteNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::string words;
        if (!text(ctx, argument(argc, argv, 0), words)) {
            return throwCoded(ctx, "GS-API-011", "the text must be a string", false);
        }
        if (words.size() > kMostClipboardBytes) {
            return throwCoded(ctx, "GS-API-017", "the text must be at most 262000 bytes as UTF-8", true);
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        return Engine::of(ctx).request(
            ctx, [services, &words](std::uint64_t id) { services->writeClipboard(id, words); }, {});
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue pressNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::string id;
        if (!text(ctx, argument(argc, argv, 0), id)) {
            return throwCoded(ctx, "GS-API-005", "the control id must be a string", false);
        }
        const int down = JS_ToBool(ctx, argument(argc, argv, 1));
        if (down < 0) {
            return JS_EXCEPTION;
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        if (!services->pressControl(id, down != 0)) {
            return throwCoded(ctx, "GS-API-005", "no control with the id '" + id + "' in the profile in use", false);
        }
        return JS_UNDEFINED;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue actNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::string id;
        std::string action;
        Point at;
        if (!text(ctx, argument(argc, argv, 0), id) || !text(ctx, argument(argc, argv, 1), action) ||
            JS_ToFloat64(ctx, &at.x, argument(argc, argv, 2)) < 0 || JS_ToFloat64(ctx, &at.y, argument(argc, argv, 3)) < 0) {
            return throwCoded(ctx, "GS-API-005", "the control id and action must be strings", false);
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        if (!services->controlAction(id, action, at)) {
            return throwCoded(ctx, "GS-API-005", "no control with the id '" + id + "' in the profile in use", false);
        }
        return JS_UNDEFINED;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue tuneNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::string id;
        std::string change;
        if (!text(ctx, argument(argc, argv, 0), id) || !text(ctx, argument(argc, argv, 1), change)) {
            return throwCoded(ctx, "GS-API-005", "the control id and the change must be strings", false);
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        const std::optional<std::string> settings = services->tuneControl(id, change);
        if (!settings) {
            return throwCoded(ctx, "GS-API-005", "no control with the id '" + id + "' in the profile in use", false);
        }
        return JS_NewStringLen(ctx, settings->data(), settings->size());
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue saveProfileNative(JSContext* ctx, JSValueConst, int, JSValueConst*, int) noexcept
{
    try {
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        return Engine::of(ctx).request(
            ctx, [services](std::uint64_t id) { services->saveProfile(id); }, [](std::uint64_t) {});
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue statusNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::string id;
        ScriptServices* services = Engine::of(ctx).services();
        if (!text(ctx, argument(argc, argv, 0), id) || services == nullptr) {
            return JS_NULL;
        }
        const std::optional<ControlStatus> status = services->controlStatus(id);
        if (!status) {
            return JS_NULL;
        }
        JSValue stick = JS_NewObject(ctx);
        if (!define(ctx, stick, "x", JS_NewFloat64(ctx, status->stick.x), JS_PROP_ENUMERABLE) ||
            !define(ctx, stick, "y", JS_NewFloat64(ctx, status->stick.y), JS_PROP_ENUMERABLE)) {
            JS_FreeValue(ctx, stick);
            return JS_EXCEPTION;
        }
        JSValue result = JS_NewObject(ctx);
        const bool made = define(ctx, result, "touching", JS_NewBool(ctx, status->touching), JS_PROP_ENUMERABLE) &&
                          define(ctx, result, "stick", stick, JS_PROP_ENUMERABLE) &&
                          define(ctx, result, "aiming", JS_NewBool(ctx, status->aiming), JS_PROP_ENUMERABLE) &&
                          define(ctx, result, "walking", JS_NewBool(ctx, status->walking), JS_PROP_ENUMERABLE) &&
                          define(ctx, result, "cancelled", JS_NewBool(ctx, status->cancelled), JS_PROP_ENUMERABLE) &&
                          define(ctx, result, "cell", JS_NewInt32(ctx, status->cell), JS_PROP_ENUMERABLE);
        if (!made) {
            JS_FreeValue(ctx, result);
            return JS_EXCEPTION;
        }
        return result;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue lockedNative(JSContext* ctx, JSValueConst, int, JSValueConst*, int) noexcept
{
    ScriptServices* services = Engine::of(ctx).services();
    return JS_NewBool(ctx, services != nullptr && services->cameraLocked());
}

JSValue claimKeyNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::string name;
        if (!text(ctx, argument(argc, argv, 0), name)) {
            return JS_FALSE;
        }
        ScriptServices* services = Engine::of(ctx).services();
        return JS_NewBool(ctx, services == nullptr || services->claimKey(name));
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue handOverNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::string id;
        if (!text(ctx, argument(argc, argv, 0), id)) {
            return JS_UNDEFINED;
        }
        const bool on = JS_ToBool(ctx, argument(argc, argv, 1)) > 0;
        if (ScriptServices* services = Engine::of(ctx).services()) {
            services->handOver(id, on);
        }
        return JS_UNDEFINED;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue globalNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::string name;
        if (!text(ctx, argument(argc, argv, 0), name)) {
            return JS_UNDEFINED;
        }
        ScriptServices* services = Engine::of(ctx).services();
        const std::optional<std::string> json = services != nullptr ? services->globalValue(name) : std::nullopt;
        return json ? parsedJson(ctx, *json) : JS_UNDEFINED;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue setGlobalNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::string name;
        std::string json;
        if (!text(ctx, argument(argc, argv, 0), name) || !text(ctx, argument(argc, argv, 1), json)) {
            return throwCoded(ctx, "GS-API-006", "global names and values must be strings and JSON values", false);
        }
        if (json.size() > kMostGlobalBytes) {
            return throwCoded(ctx, "GS-API-007", "a global value must be at most 64 KiB of JSON", true);
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        services->setGlobal(name, json);
        return JS_UNDEFINED;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue appNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::string request;
        std::string json;
        if (!text(ctx, argument(argc, argv, 0), request) || !text(ctx, argument(argc, argv, 1), json)) {
            return JS_NULL;
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        return parsedJson(ctx, services->app(request, json));
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue connectedNative(JSContext* ctx, JSValueConst, int, JSValueConst*, int) noexcept
{
    try {
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        return JS_NewBool(ctx, services->connected());
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue awaitConnectionNative(JSContext* ctx, JSValueConst, int, JSValueConst*, int) noexcept
{
    try {
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        return Engine::of(ctx).request(ctx, [services](std::uint64_t id) { services->awaitConnection(id); }, {});
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue fingerDownNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        Point at;
        if (!ratio(ctx, argument(argc, argv, 0), "x", at.x) || !ratio(ctx, argument(argc, argv, 1), "y", at.y)) {
            return JS_EXCEPTION;
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        const std::uint64_t id = Engine::of(ctx).nextFinger();
        services->hold(id, at);
        return JS_NewFloat64(ctx, static_cast<double>(id));
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue fingerMoveNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        double id = 0.0;
        Point at;
        double ms = 0.0;
        if (JS_ToFloat64(ctx, &id, argument(argc, argv, 0)) < 0 || !ratio(ctx, argument(argc, argv, 1), "x", at.x) ||
            !ratio(ctx, argument(argc, argv, 2), "y", at.y) || JS_ToFloat64(ctx, &ms, argument(argc, argv, 3)) < 0) {
            return JS_EXCEPTION;
        }
        if (ScriptServices* services = Engine::of(ctx).services(); services != nullptr && id >= 1.0) {
            services->move(static_cast<std::uint64_t>(id), at, std::max(ms, 0.0));
        }
        return JS_UNDEFINED;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue fingerUpNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        double id = 0.0;
        if (JS_ToFloat64(ctx, &id, argument(argc, argv, 0)) < 0) {
            return JS_EXCEPTION;
        }
        if (ScriptServices* services = Engine::of(ctx).services(); services != nullptr && id >= 1.0) {
            services->lift(static_cast<std::uint64_t>(id));
        }
        return JS_UNDEFINED;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue liftAllNative(JSContext* ctx, JSValueConst, int, JSValueConst*, int) noexcept
{
    try {
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        services->liftAll();
        return JS_UNDEFINED;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

bool drawOptions(JSContext* ctx, JSValueConst options, Drawing& drawing, double& durationMs)
{
    drawing.color = "accent";
    if (JS_IsUndefined(options)) {
        return true;
    }
    if (!JS_IsObject(options)) {
        throwCoded(ctx, "GS-API-011", "the options must be an object", false);
        return false;
    }
    std::string color;
    if (!optionText(ctx, options, "color", kMostChoiceChars, color)) {
        throwCoded(ctx, "GS-API-011", "the color must be a string", false);
        return false;
    }
    if (!color.empty()) {
        constexpr std::string_view kColors[] = {"accent", "good", "warn", "bad", "white"};
        if (std::ranges::find(kColors, std::string_view(color)) == std::end(kColors)) {
            throwCoded(ctx, "GS-API-014", "the color must be accent, good, warn, bad or white", true);
            return false;
        }
        drawing.color = color;
    }
    if (drawing.kind != DrawingKind::Text && !optionText(ctx, options, "label", kMostChoiceChars, drawing.text)) {
        throwCoded(ctx, "GS-API-011", "the label must be a string", false);
        return false;
    }
    JSValue duration = JS_GetPropertyStr(ctx, options, "durationMs");
    if (JS_IsException(duration)) {
        return false;
    }
    bool ok = true;
    if (!JS_IsUndefined(duration)) {
        ok = JS_ToFloat64(ctx, &durationMs, duration) == 0;
        if (ok && !(durationMs > 0.0 && durationMs <= kLongestDrawingMs)) {
            throwCoded(ctx, "GS-API-003", "the duration must be more than 0 and at most 600000 milliseconds", true);
            ok = false;
        }
    }
    JS_FreeValue(ctx, duration);
    return ok;
}

JSValue shownDrawing(JSContext* ctx, const Drawing& drawing, double durationMs)
{
    ScriptServices* services = servicesOf(ctx);
    if (services == nullptr) {
        return JS_EXCEPTION;
    }
    Engine& engine = Engine::of(ctx);
    const std::uint64_t id = engine.nextDrawing();
    JSValue made = engine.drawn(ctx, id, durationMs);
    if (JS_IsException(made)) {
        return made;
    }
    services->draw(id, drawing);
    return made;
}

JSValue drawNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int kind) noexcept
{
    try {
        Drawing drawing;
        drawing.kind = static_cast<DrawingKind>(kind);
        if (!ratio(ctx, argument(argc, argv, 0), "x", drawing.at.x)
            || !ratio(ctx, argument(argc, argv, 1), "y", drawing.at.y)) {
            return JS_EXCEPTION;
        }
        int optionsAt = 3;
        if (drawing.kind == DrawingKind::Box) {
            if (!ratio(ctx, argument(argc, argv, 2), "width", drawing.width)
                || !ratio(ctx, argument(argc, argv, 3), "height", drawing.height)) {
                return JS_EXCEPTION;
            }
            if (drawing.at.x + drawing.width > 1.0 + 1e-9 || drawing.at.y + drawing.height > 1.0 + 1e-9) {
                return throwCoded(ctx, "GS-API-002", "the box must stay inside the picture", true);
            }
            optionsAt = 4;
        } else if (drawing.kind == DrawingKind::Text) {
            if (!text(ctx, argument(argc, argv, 2), drawing.text) || drawing.text.empty()) {
                return throwCoded(ctx, "GS-API-011", "the text must be a string that is not empty", false);
            }
            drawing.text = clipped(std::move(drawing.text), kMostToastChars);
        } else {
            optionsAt = 2;
        }
        double durationMs = 0.0;
        if (!drawOptions(ctx, argument(argc, argv, optionsAt), drawing, durationMs)) {
            return JS_EXCEPTION;
        }
        return shownDrawing(ctx, drawing, durationMs);
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue eraseNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        double id = 0.0;
        if (JS_ToFloat64(ctx, &id, argument(argc, argv, 0)) < 0) {
            return JS_EXCEPTION;
        }
        if (ScriptServices* services = Engine::of(ctx).services(); services != nullptr && id >= 1.0) {
            services->erase(static_cast<std::uint64_t>(id));
        }
        return JS_UNDEFINED;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

bool imagePath(std::string_view path)
{
    if (path.empty() || path.size() > 260 || path.front() == '/' || path.front() == '\\'
        || path.find(':') != std::string_view::npos) {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find_first_of("/\\", start);
        const std::string_view part = path.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    const std::size_t dot = path.rfind('.');
    if (dot == std::string_view::npos) {
        return false;
    }
    std::string extension(path.substr(dot + 1));
    std::ranges::transform(extension, extension.begin(),
                           [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; });
    return extension == "png" || extension == "jpg" || extension == "jpeg" || extension == "bmp";
}

bool numberOption(JSContext* ctx, JSValueConst options, const char* name, double& out, bool& present)
{
    JSValue value = JS_GetPropertyStr(ctx, options, name);
    if (JS_IsException(value)) {
        return false;
    }
    present = !JS_IsUndefined(value);
    const bool ok = !present || JS_ToFloat64(ctx, &out, value) == 0;
    JS_FreeValue(ctx, value);
    return ok;
}

const Resources* resourcesOf(JSContext* ctx)
{
    const LoadedResources& loaded = Engine::of(ctx).resources(ctx);
    if (!loaded) {
        const std::string first = loaded.error().empty() ? std::string("it is not valid") : loaded.error().front();
        throwCoded(ctx, "GS-API-020", std::format("{} cannot be used: {}", kResourcesFile, first), false);
        return nullptr;
    }
    return &*loaded;
}

void unknownName(JSContext* ctx, ResourceKind kind, std::string_view name)
{
    throwCoded(ctx, "GS-API-018", std::format("there is no {} named \"{}\"", kindName(kind), name), false);
}

bool regionNamed(JSContext* ctx, std::string_view name, Region& out)
{
    const Resources* resources = resourcesOf(ctx);
    if (resources == nullptr) {
        return false;
    }
    const RegionResource* found = resources->findRegion(name);
    if (found == nullptr) {
        unknownName(ctx, ResourceKind::Region, name);
        return false;
    }
    out = found->area;
    return true;
}

bool regionValue(JSContext* ctx, JSValueConst region, Region& out)
{
    if (JS_IsString(region)) {
        std::string name;
        return text(ctx, region, name) && regionNamed(ctx, name, out);
    }
    Region area;
    bool read = JS_IsObject(region);
    bool threw = false;
    const auto field = [&](const char* name, double& value) {
        if (!read) {
            return;
        }
        JSValue part = JS_GetPropertyStr(ctx, region, name);
        if (JS_IsException(part) || !ratio(ctx, part, name, value)) {
            read = false;
            threw = true;
        }
        JS_FreeValue(ctx, part);
    };
    field("x", area.x);
    field("y", area.y);
    field("width", area.width);
    field("height", area.height);
    if (threw) {
        return false;
    }
    if (!read || area.x + area.width > 1.0 + 1e-9 || area.y + area.height > 1.0 + 1e-9) {
        throwCoded(ctx, "GS-API-002", "the region must be an object inside the picture", true);
        return false;
    }
    out = area;
    return true;
}

bool regionOption(JSContext* ctx, JSValueConst options, Region& out)
{
    JSValue region = JS_GetPropertyStr(ctx, options, "region");
    if (JS_IsException(region)) {
        return false;
    }
    const bool ok = JS_IsUndefined(region) || regionValue(ctx, region, out);
    JS_FreeValue(ctx, region);
    return ok;
}

bool timeoutOption(JSContext* ctx, JSValueConst options, double& out)
{
    bool present = false;
    if (!numberOption(ctx, options, "timeoutMs", out, present)) {
        return false;
    }
    if (present && !(out > 0.0 && out <= kLongestFindMs)) {
        throwCoded(ctx, "GS-API-003", "the timeout must be more than 0 and at most 10000 milliseconds", true);
        return false;
    }
    return true;
}

bool matchOption(JSContext* ctx, JSValueConst options, MatchMode& out)
{
    JSValue value = JS_GetPropertyStr(ctx, options, "match");
    if (JS_IsException(value)) {
        return false;
    }
    if (JS_IsUndefined(value)) {
        return true;
    }
    std::string mode;
    const bool read = text(ctx, value, mode);
    JS_FreeValue(ctx, value);
    if (!read || (mode != "color" && mode != "grey")) {
        throwCoded(ctx, "GS-API-019", "match must be \"color\" or \"grey\"", false);
        return false;
    }
    out = mode == "color" ? MatchMode::Color : MatchMode::Grey;
    return true;
}

bool templateNamed(JSContext* ctx, FindQuery& query)
{
    const Resources* resources = resourcesOf(ctx);
    if (resources == nullptr) {
        return false;
    }
    const TemplateResource* found = resources->findTemplate(query.image);
    if (found == nullptr) {
        unknownName(ctx, ResourceKind::Template, query.image);
        return false;
    }
    query.file = "res/" + found->file;
    query.threshold = found->threshold;
    query.match = found->match;
    query.source = found->source;
    return found->region.empty() || regionNamed(ctx, found->region, query.region);
}

bool capturedImage(JSContext* ctx, JSValueConst value, std::uint64_t& out)
{
    if (!JS_IsObject(value)) {
        return false;
    }
    JSValue id = JS_GetPropertyStr(ctx, value, "capture");
    double number = 0.0;
    const bool read = JS_IsNumber(id) && JS_ToFloat64(ctx, &number, id) == 0 && number >= 1.0 &&
                      number <= 9007199254740991.0 && std::floor(number) == number;
    JS_FreeValue(ctx, id);
    if (read) {
        out = static_cast<std::uint64_t>(number);
    }
    return read;
}

bool imageArgument(JSContext* ctx, JSValueConst value, FindQuery& query)
{
    constexpr std::string_view kNotAnImage = "the image must be a template name, a picture inside the profile folder "
                                             "such as \"res/chest.png\", or an image from vision.capture()";
    if (capturedImage(ctx, value, query.capture)) {
        query.image = std::format("captured image {}", query.capture);
        query.threshold = kNewTemplateThreshold;
        query.match = MatchMode::Color;
        return true;
    }
    if (!text(ctx, value, query.image)) {
        throwCoded(ctx, "GS-API-015", kNotAnImage, false);
        return false;
    }
    if (resourceName(query.image)) {
        return templateNamed(ctx, query);
    }
    if (imagePath(query.image)) {
        query.file = query.image;
        return true;
    }
    throwCoded(ctx, "GS-API-015", kNotAnImage, false);
    return false;
}

JSValue findNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int all) noexcept
{
    try {
        FindQuery query;
        query.all = all != 0;
        if (!imageArgument(ctx, argument(argc, argv, 0), query)) {
            return JS_EXCEPTION;
        }
        JSValueConst options = argument(argc, argv, 1);
        if (!JS_IsUndefined(options)) {
            if (!JS_IsObject(options)) {
                return throwCoded(ctx, "GS-API-011", "the options must be an object", false);
            }
            bool present = false;
            if (!numberOption(ctx, options, "threshold", query.threshold, present)) {
                return JS_EXCEPTION;
            }
            if (present && !(query.threshold >= 0.0 && query.threshold <= 1.0)) {
                return throwCoded(ctx, "GS-API-017", "the threshold must be from 0 to 1", true);
            }
            double most = query.most;
            if (!numberOption(ctx, options, "max", most, present)) {
                return JS_EXCEPTION;
            }
            if (present && !(most >= 1.0 && most <= static_cast<double>(kMostFound) && std::floor(most) == most)) {
                return throwCoded(ctx, "GS-API-017", "max must be a whole number from 1 to 64", true);
            }
            query.most = static_cast<int>(most);
            if (!timeoutOption(ctx, options, query.timeoutMs) || !regionOption(ctx, options, query.region)
                || !matchOption(ctx, options, query.match)) {
                return JS_EXCEPTION;
            }
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        return Engine::of(ctx).request(
            ctx, [services, &query](std::uint64_t id) { services->find(id, query); },
            [services](std::uint64_t id) { services->cancelFind(id); });
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue imageNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        FindQuery source;
        JSValueConst image = argument(argc, argv, 0);
        if (!imageArgument(ctx, image, source)) {
            return JS_EXCEPTION;
        }
        Drawing drawing;
        drawing.kind = DrawingKind::Image;
        drawing.image = source.file;
        drawing.capture = source.capture;
        JSValueConst region = argument(argc, argv, 1);
        JSValue where = JS_IsUndefined(region) && source.capture != 0 ? JS_GetPropertyStr(ctx, image, "region")
                                                                      : JS_DupValue(ctx, region);
        if (JS_IsException(where)) {
            return where;
        }
        if (JS_IsUndefined(where)) {
            return throwCoded(ctx, "GS-API-002", "a template or a picture file needs a region to show in", true);
        }
        Region area;
        const bool placed = regionValue(ctx, where, area);
        JS_FreeValue(ctx, where);
        if (!placed) {
            return JS_EXCEPTION;
        }
        drawing.at = {area.x, area.y};
        drawing.width = area.width;
        drawing.height = area.height;
        double durationMs = 0.0;
        if (!drawOptions(ctx, argument(argc, argv, 2), drawing, durationMs)) {
            return JS_EXCEPTION;
        }
        return shownDrawing(ctx, drawing, durationMs);
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

bool lookOptions(JSContext* ctx, JSValueConst options, double* threshold, double& timeoutMs)
{
    if (JS_IsUndefined(options)) {
        return true;
    }
    if (!JS_IsObject(options)) {
        throwCoded(ctx, "GS-API-011", "the options must be an object", false);
        return false;
    }
    if (threshold != nullptr) {
        bool present = false;
        if (!numberOption(ctx, options, "threshold", *threshold, present)) {
            return false;
        }
        if (present && !(*threshold >= 0.0 && *threshold <= 1.0)) {
            throwCoded(ctx, "GS-API-017", "the threshold must be from 0 to 1", true);
            return false;
        }
    }
    return timeoutOption(ctx, options, timeoutMs);
}

JSValue captureNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        CaptureQuery query;
        JSValueConst region = argument(argc, argv, 0);
        if (!JS_IsUndefined(region) && !JS_IsNull(region) && !regionValue(ctx, region, query.region)) {
            return JS_EXCEPTION;
        }
        if (!lookOptions(ctx, argument(argc, argv, 1), nullptr, query.timeoutMs)) {
            return JS_EXCEPTION;
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        return Engine::of(ctx).request(
            ctx, [services, &query](std::uint64_t id) { services->capture(id, query); },
            [services](std::uint64_t id) { services->cancelFind(id); });
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue changedNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int keyed) noexcept
{
    try {
        ChangeQuery query;
        const int first = keyed != 0 ? 1 : 0;
        if (keyed != 0 && !text(ctx, argument(argc, argv, 0), query.key)) {
            return throwCoded(ctx, "GS-API-011", "the key must be a string", false);
        }
        JSValueConst region = argument(argc, argv, first);
        if (!JS_IsUndefined(region) && !JS_IsNull(region) && !regionValue(ctx, region, query.region)) {
            return JS_EXCEPTION;
        }
        if (!lookOptions(ctx, argument(argc, argv, first + 1), &query.threshold, query.timeoutMs)) {
            return JS_EXCEPTION;
        }
        if (keyed == 0) {
            query.key = std::format("changed {} {} {} {}", query.region.x, query.region.y, query.region.width,
                                    query.region.height);
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        return Engine::of(ctx).request(
            ctx, [services, &query](std::uint64_t id) { services->changed(id, query); },
            [services](std::uint64_t id) { services->cancelFind(id); });
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue saveCaptureNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::uint64_t capture = 0;
        if (!capturedImage(ctx, argument(argc, argv, 0), capture)) {
            return throwCoded(ctx, "GS-API-015", "only an image from vision.capture() can be saved", false);
        }
        std::string name;
        if (!text(ctx, argument(argc, argv, 1), name) || !resourceName(name)) {
            return throwCoded(ctx, "GS-API-038",
                              "the name must be 1 to 64 characters of plain text that does not end in .png, .jpg or "
                              ".bmp",
                              false);
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        return Engine::of(ctx).request(
            ctx, [services, capture, &name](std::uint64_t id) { services->saveCapture(id, capture, name); },
            [services](std::uint64_t id) { services->cancelFind(id); });
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

bool colorArgument(JSContext* ctx, JSValueConst value, std::string& color, Rgb& rgb, double& tolerance,
                   Region* region)
{
    if (!text(ctx, value, color) || color.empty()) {
        throwCoded(ctx, "GS-API-019", "the color must be written \"#RRGGBB\" or be a color name", false);
        return false;
    }
    if (color.front() == '#') {
        const std::optional<Rgb> parsed = parseColor(color);
        if (!parsed) {
            throwCoded(ctx, "GS-API-019", "the color must be written \"#RRGGBB\", such as \"#F5C542\"", false);
            return false;
        }
        rgb = *parsed;
        tolerance = kDefaultTolerance;
        return true;
    }
    const Resources* resources = resourcesOf(ctx);
    if (resources == nullptr) {
        return false;
    }
    const ColorResource* found = resources->findColor(color);
    if (found == nullptr) {
        unknownName(ctx, ResourceKind::Color, color);
        return false;
    }
    rgb = found->color;
    tolerance = found->tolerance;
    return region == nullptr || found->region.empty() || regionNamed(ctx, found->region, *region);
}

bool toleranceOption(JSContext* ctx, JSValueConst options, double& out)
{
    bool present = false;
    if (!numberOption(ctx, options, "tolerance", out, present)) {
        return false;
    }
    if (present && !(out >= 0.0 && out <= 1.0)) {
        throwCoded(ctx, "GS-API-017", "the tolerance must be from 0 to 1", true);
        return false;
    }
    return true;
}

bool pointArgument(JSContext* ctx, JSValueConst value, Point& out)
{
    if (!JS_IsObject(value)) {
        throwCoded(ctx, "GS-API-002", "the position must be a point with x and y from 0 to 1", true);
        return false;
    }
    const auto field = [ctx, value](const char* name, double& into) {
        JSValue part = JS_GetPropertyStr(ctx, value, name);
        const bool read = !JS_IsException(part) && ratio(ctx, part, name, into);
        JS_FreeValue(ctx, part);
        return read;
    };
    return field("x", out.x) && field("y", out.y);
}

JSValue findColorNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        ColorQuery query;
        if (!colorArgument(ctx, argument(argc, argv, 0), query.color, query.rgb, query.tolerance, &query.region)) {
            return JS_EXCEPTION;
        }
        JSValueConst options = argument(argc, argv, 1);
        if (!JS_IsUndefined(options)) {
            if (!JS_IsObject(options)) {
                return throwCoded(ctx, "GS-API-011", "the options must be an object", false);
            }
            if (!toleranceOption(ctx, options, query.tolerance) || !timeoutOption(ctx, options, query.timeoutMs)
                || !regionOption(ctx, options, query.region)) {
                return JS_EXCEPTION;
            }
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        return Engine::of(ctx).request(
            ctx, [services, &query](std::uint64_t id) { services->findColor(id, query); },
            [services](std::uint64_t id) { services->cancelFind(id); });
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

bool languageTag(std::string_view tag) noexcept
{
    return !tag.empty() && tag.size() <= kMostLanguageChars && std::ranges::all_of(tag, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
    });
}

bool languageOption(JSContext* ctx, JSValueConst options, std::string& out)
{
    JSValue value = JS_GetPropertyStr(ctx, options, "language");
    if (JS_IsException(value)) {
        return false;
    }
    const bool given = !JS_IsUndefined(value);
    const bool read = !given || text(ctx, value, out);
    JS_FreeValue(ctx, value);
    if (!read || (given && !languageTag(out))) {
        throwCoded(ctx, "GS-API-011", "the language must be a tag such as \"zh-Hans\" or \"en\"", false);
        return false;
    }
    return true;
}

JSValue readTextNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int find) noexcept
{
    try {
        TextQuery query;
        query.find = find != 0;
        if (query.find) {
            const bool read = text(ctx, argument(argc, argv, 0), query.text);
            const auto chars = std::ranges::count_if(
                query.text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U; });
            if (!read || foldedText(query.text).empty() || static_cast<std::size_t>(chars) > kMostFindTextChars) {
                return throwCoded(ctx, "GS-API-011",
                                  "the text to find must be a string of 1 to 200 characters that are not all spaces",
                                  false);
            }
        }
        JSValueConst options = argument(argc, argv, query.find ? 1 : 0);
        if (!JS_IsUndefined(options)) {
            if (!JS_IsObject(options)) {
                return throwCoded(ctx, "GS-API-011", "the options must be an object", false);
            }
            if (!timeoutOption(ctx, options, query.timeoutMs) || !regionOption(ctx, options, query.region)
                || !languageOption(ctx, options, query.language)) {
                return JS_EXCEPTION;
            }
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        return Engine::of(ctx).request(
            ctx, [services, &query](std::uint64_t id) { services->readText(id, query); },
            [services](std::uint64_t id) { services->cancelFind(id); });
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue pixelColorNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int match) noexcept
{
    try {
        PixelQuery query;
        query.match = match != 0;
        if (!pointArgument(ctx, argument(argc, argv, 0), query.at)) {
            return JS_EXCEPTION;
        }
        if (query.match
            && !colorArgument(ctx, argument(argc, argv, 1), query.color, query.rgb, query.tolerance, nullptr)) {
            return JS_EXCEPTION;
        }
        JSValueConst options = argument(argc, argv, query.match ? 2 : 1);
        if (!JS_IsUndefined(options)) {
            if (!JS_IsObject(options)) {
                return throwCoded(ctx, "GS-API-011", "the options must be an object", false);
            }
            if ((query.match && !toleranceOption(ctx, options, query.tolerance))
                || !timeoutOption(ctx, options, query.timeoutMs)) {
                return JS_EXCEPTION;
            }
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        return Engine::of(ctx).request(
            ctx, [services, &query](std::uint64_t id) { services->pickColor(id, query); },
            [services](std::uint64_t id) { services->cancelFind(id); });
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue pointValue(JSContext* ctx, const Point& point)
{
    JSValue object = JS_NewObject(ctx);
    if (JS_IsException(object)) {
        return object;
    }
    if (!define(ctx, object, "x", JS_NewFloat64(ctx, point.x), JS_PROP_ENUMERABLE)
        || !define(ctx, object, "y", JS_NewFloat64(ctx, point.y), JS_PROP_ENUMERABLE)) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    return object;
}

JSValue regionValue(JSContext* ctx, const Region& area)
{
    JSValue object = JS_NewObject(ctx);
    if (JS_IsException(object)) {
        return object;
    }
    if (!define(ctx, object, "x", JS_NewFloat64(ctx, area.x), JS_PROP_ENUMERABLE)
        || !define(ctx, object, "y", JS_NewFloat64(ctx, area.y), JS_PROP_ENUMERABLE)
        || !define(ctx, object, "width", JS_NewFloat64(ctx, area.width), JS_PROP_ENUMERABLE)
        || !define(ctx, object, "height", JS_NewFloat64(ctx, area.height), JS_PROP_ENUMERABLE)) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    return object;
}

JSValue pathValue(JSContext* ctx, const PathResource& path)
{
    JSValue points = JS_NewArray(ctx);
    if (JS_IsException(points)) {
        return points;
    }
    std::uint32_t index = 0;
    for (const Point& point : path.points) {
        JSValue entry = pointValue(ctx, point);
        if (JS_IsException(entry) || JS_SetPropertyUint32(ctx, points, index++, entry) < 0) {
            JS_FreeValue(ctx, points);
            return JS_EXCEPTION;
        }
    }
    JSValue object = JS_NewObject(ctx);
    if (JS_IsException(object)) {
        JS_FreeValue(ctx, points);
        return object;
    }
    if (!define(ctx, object, "points", points, JS_PROP_ENUMERABLE)
        || !define(ctx, object, "durationMs", JS_NewFloat64(ctx, path.durationMs), JS_PROP_ENUMERABLE)) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    return object;
}

JSValue resourceNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int kind) noexcept
{
    try {
        const auto wanted = static_cast<ResourceKind>(kind);
        std::string name;
        if (!text(ctx, argument(argc, argv, 0), name)) {
            return throwCoded(ctx, "GS-API-018", std::format("the {} name must be a string", kindName(wanted)), false);
        }
        const Resources* resources = resourcesOf(ctx);
        if (resources == nullptr) {
            return JS_EXCEPTION;
        }
        if (wanted == ResourceKind::Point) {
            if (const PointResource* found = resources->findPoint(name)) {
                return pointValue(ctx, found->at);
            }
        } else if (wanted == ResourceKind::Region) {
            if (const RegionResource* found = resources->findRegion(name)) {
                return regionValue(ctx, found->area);
            }
        } else if (wanted == ResourceKind::Path) {
            if (const PathResource* found = resources->findPath(name)) {
                return pathValue(ctx, *found);
            }
        }
        unknownName(ctx, wanted, name);
        return JS_EXCEPTION;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

constexpr std::size_t kMostNodeTextBytes = 256 * 1024 - 4096;
constexpr double kLongestNodeDurationMs = 2147483647.0;

std::string_view nodeTypeTitle(NodeType type) noexcept
{
    switch (type) {
    case NodeType::None:
        return "nothing";
    case NodeType::Boolean:
        return "true or false";
    case NodeType::Number:
        return "a finite number";
    case NodeType::String:
        return "a string";
    case NodeType::Point:
        return "a point with x and y from 0 to 1";
    case NodeType::Region:
        return "a region inside the picture or a region name";
    case NodeType::Color:
        return "a color written \"#RRGGBB\" or a color name";
    case NodeType::Duration:
        return "milliseconds from 0 to 2147483647";
    case NodeType::Json:
        return "a JSON value";
    }
    return "a value";
}

JSValue throwNodeError(JSContext* ctx, std::string_view code, const std::string& path, std::string_view problem)
{
    const std::string message = std::string(code) + " " + path + ": " + std::string(problem);
    if (code == "GS-API-061") {
        return JS_ThrowTypeError(ctx, "%s", message.c_str());
    }
    return JS_ThrowPlainError(ctx, "%s", message.c_str());
}

bool nodeValueOf(JSContext* ctx, JSValueConst value, NodeType type, NodeValue& out)
{
    out.type = type;
    switch (type) {
    case NodeType::None:
        return false;
    case NodeType::Boolean:
        if (!JS_IsBool(value)) {
            return false;
        }
        out.flag = JS_ToBool(ctx, value) != 0;
        return true;
    case NodeType::Number:
    case NodeType::Duration:
        if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &out.number, value) < 0 || !std::isfinite(out.number)) {
            return false;
        }
        return type == NodeType::Number || (out.number >= 0.0 && out.number <= kLongestNodeDurationMs);
    case NodeType::String:
        return text(ctx, value, out.text) && out.text.size() <= kMostNodeTextBytes;
    case NodeType::Point: {
        Point at;
        if (!pointArgument(ctx, value, at)) {
            return false;
        }
        out.area = Region{at.x, at.y, 0.0, 0.0};
        return true;
    }
    case NodeType::Region:
        return regionValue(ctx, value, out.area);
    case NodeType::Color: {
        std::string color;
        Rgb rgb;
        double tolerance = 0.0;
        if (!colorArgument(ctx, value, color, rgb, tolerance, nullptr)) {
            return false;
        }
        out.text = colorText(rgb);
        return true;
    }
    case NodeType::Json: {
        JSValue json = JS_JSONStringify(ctx, value, JS_UNDEFINED, JS_UNDEFINED);
        if (JS_IsException(json)) {
            JS_FreeValue(ctx, JS_GetException(ctx));
            return false;
        }
        const bool made = JS_IsString(json) && text(ctx, json, out.text) && out.text.size() <= kMostNodeTextBytes;
        JS_FreeValue(ctx, json);
        return made;
    }
    }
    return false;
}

JSValue nodeResult(JSContext* ctx, const NodeValue& value, NodeType declared, const std::string& path)
{
    if (value.type != declared) {
        return throwNodeError(ctx, "GS-API-062", path,
                              "it gave back " + std::string(nodeTypeTitle(value.type)) + " instead of " +
                                  std::string(nodeTypeTitle(declared)));
    }
    switch (declared) {
    case NodeType::None:
        return JS_UNDEFINED;
    case NodeType::Boolean:
        return JS_NewBool(ctx, value.flag);
    case NodeType::Number:
    case NodeType::Duration:
        return JS_NewFloat64(ctx, value.number);
    case NodeType::String:
    case NodeType::Color:
        return JS_NewStringLen(ctx, value.text.data(), value.text.size());
    case NodeType::Point:
        return pointValue(ctx, Point{value.area.x, value.area.y});
    case NodeType::Region:
        return regionValue(ctx, value.area);
    case NodeType::Json: {
        JSValue parsed = JS_ParseJSON(ctx, value.text.c_str(), value.text.size(), "<node>");
        if (JS_IsException(parsed)) {
            JS_FreeValue(ctx, JS_GetException(ctx));
            return throwNodeError(ctx, "GS-API-062", path, "it gave back JSON that cannot be read");
        }
        return parsed;
    }
    }
    return JS_UNDEFINED;
}

JSValue nodesNative(JSContext* ctx, JSValueConst, int, JSValueConst*, int) noexcept
{
    try {
        ScriptServices* services = Engine::of(ctx).services();
        JSValue list = JS_NewArray(ctx);
        if (JS_IsException(list) || services == nullptr) {
            return list;
        }
        std::uint32_t index = 0;
        for (const NodeInfo& node : services->nodes()) {
            JSValue owner = JS_NewArray(ctx);
            if (JS_IsException(owner)) {
                JS_FreeValue(ctx, list);
                return JS_EXCEPTION;
            }
            std::uint32_t at = 0;
            for (const std::string& segment : nodeOwner(node.extension)) {
                if (JS_SetPropertyUint32(ctx, owner, at++, JS_NewStringLen(ctx, segment.data(), segment.size())) < 0) {
                    JS_FreeValue(ctx, owner);
                    JS_FreeValue(ctx, list);
                    return JS_EXCEPTION;
                }
            }
            JSValue entry = JS_NewObject(ctx);
            const bool made =
                define(ctx, entry, "owner", owner, JS_PROP_ENUMERABLE)
                && define(ctx, entry, "extension", JS_NewStringLen(ctx, node.extension.data(), node.extension.size()),
                          JS_PROP_ENUMERABLE)
                && define(ctx, entry, "name", JS_NewStringLen(ctx, node.name.data(), node.name.size()),
                          JS_PROP_ENUMERABLE)
                && define(ctx, entry, "count", JS_NewInt32(ctx, static_cast<int>(node.params.size())),
                          JS_PROP_ENUMERABLE);
            if (!made || JS_SetPropertyUint32(ctx, list, index++, entry) < 0) {
                if (!made) {
                    JS_FreeValue(ctx, entry);
                }
                JS_FreeValue(ctx, list);
                return JS_EXCEPTION;
            }
        }
        return list;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue callNodeNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::string extension;
        std::string node;
        const JSValueConst values = argument(argc, argv, 2);
        std::int64_t length = 0;
        if (!text(ctx, argument(argc, argv, 0), extension) || !text(ctx, argument(argc, argv, 1), node)
            || !JS_IsArray(values) || JS_GetLength(ctx, values, &length) < 0) {
            return JS_ThrowTypeError(ctx, "%s", "callNode(extension, node, values) needs two names and a list");
        }
        ScriptServices* services = servicesOf(ctx);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        const std::string path = nodePath(extension, node);
        const std::vector<NodeInfo> nodes = services->nodes();
        const auto found = std::ranges::find_if(
            nodes, [&](const NodeInfo& info) { return info.extension == extension && info.name == node; });
        if (found == nodes.end()) {
            return throwNodeError(ctx, "GS-API-060", path,
                                  "no extension offers this node now; turn on the extension that adds it");
        }
        if (length != static_cast<std::int64_t>(found->params.size())) {
            return throwNodeError(ctx, "GS-API-061", path,
                                  std::format("it takes {} values, not {}", found->params.size(), length));
        }
        std::vector<NodeValue> arguments;
        arguments.reserve(found->params.size());
        for (std::uint32_t index = 0; index < found->params.size(); ++index) {
            const NodeParam& param = found->params[index];
            JSValue item = JS_GetPropertyUint32(ctx, values, index);
            if (JS_IsException(item)) {
                return JS_EXCEPTION;
            }
            NodeValue value;
            const bool converted = nodeValueOf(ctx, item, param.type, value);
            JS_FreeValue(ctx, item);
            if (!converted) {
                if (JS_HasException(ctx)) {
                    return JS_EXCEPTION;
                }
                return throwNodeError(ctx, "GS-API-061", path,
                                      param.name + " must be " + std::string(nodeTypeTitle(param.type)));
            }
            arguments.push_back(std::move(value));
        }
        const NodeAnswer answer = services->callNode(extension, node, arguments);
        if (!answer.code.empty()) {
            return throwNodeError(ctx, answer.code, path, answer.problem);
        }
        return nodeResult(ctx, answer.value, found->returns, path);
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

ScriptServices* serviceArguments(JSContext* ctx, int argc, JSValueConst* argv, std::string& service, std::string& json)
{
    if (!text(ctx, argument(argc, argv, 0), service) || !text(ctx, argument(argc, argv, 1), json)
        || service.empty() || service.size() > kMostServiceNameBytes) {
        JS_ThrowTypeError(ctx, "%s", "a service call needs a service name and JSON text");
        return nullptr;
    }
    if (json.size() > kMostServiceBytes) {
        throwCoded(ctx, "GS-API-071", "what is sent to a service must be at most 1 MiB as JSON", true);
        return nullptr;
    }
    if (const std::optional<std::string> refused = Engine::of(ctx).refusal(ctx, channelRule(service))) {
        JS_ThrowTypeError(ctx, "%s", refused->c_str());
        return nullptr;
    }
    ScriptServices* services = servicesOf(ctx);
    if (services != nullptr && !services->offersService(service)) {
        throwCoded(ctx, "GS-API-070", "no part of GameScrcpy offers the service " + service, false);
        return nullptr;
    }
    return services;
}

JSValue serviceCallNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::string service;
        std::string json;
        ScriptServices* services = serviceArguments(ctx, argc, argv, service, json);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        const ServiceAnswer answer = services->callService(service, json);
        if (!answer.code.empty()) {
            return throwCoded(ctx, answer.code, answer.problem, false);
        }
        JSValue value = JS_ParseJSON(ctx, answer.json.c_str(), answer.json.size(), "<service>");
        if (JS_IsException(value)) {
            JS_FreeValue(ctx, JS_GetException(ctx));
            return JS_NULL;
        }
        return value;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue serviceRequestNative(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int) noexcept
{
    try {
        std::string service;
        std::string json;
        ScriptServices* services = serviceArguments(ctx, argc, argv, service, json);
        if (services == nullptr) {
            return JS_EXCEPTION;
        }
        return Engine::of(ctx).request(
            ctx, [services, &service, &json](std::uint64_t id) { services->requestService(id, service, json); },
            [services](std::uint64_t id) { services->dropService(id); });
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

struct Gate {
    JSCFunctionMagic* native;
    std::string_view method;
};

constexpr Gate kGates[] = {
    {&tapNative, "tap"},
    {&swipeNative, "swipe"},
    {&pressKeyNative, "key"},
    {&deviceKeyNative, "key"},
    {&typeNative, "type"},
    {&fingerDownNative, "hold"},
    {&fingerMoveNative, "move"},
    {&deviceInfoNative, "device"},
    {&controlsNative, "controls"},
    {&stateValueNative, "stateValue"},
    {&setStateNative, "setState"},
    {&holdControlNative, "pressControl"},
    {&pressNative, "pressControl"},
    {&enableControlNative, "enableControl"},
    {&actNative, "controlAction"},
    {&tuneNative, "tuneControl"},
    {&saveProfileNative, "saveProfile"},
    {&statusNative, "controlStatus"},
    {&lockedNative, "cameraLocked"},
    {&globalNative, "globalValue"},
    {&setGlobalNative, "setGlobal"},
    {&claimKeyNative, "claimKey"},
    {&handOverNative, "handOver"},
    {&cursorNative, "cursor"},
    {&keyDownNative, "keyDown"},
    {&toastNative, "toast"},
    {&promptNative, "ask"},
    {&drawNative, "draw"},
    {&imageNative, "draw"},
    {&awaitConnectionNative, "awaitConnection"},
    {&clipboardReadNative, "readClipboard"},
    {&clipboardWriteNative, "writeClipboard"},
    {&findNative, "find"},
    {&findColorNative, "findColor"},
    {&pixelColorNative, "pickColor"},
    {&captureNative, "capture"},
    {&changedNative, "changed"},
    {&readTextNative, "readText"},
    {&saveCaptureNative, "saveCapture"},
    {&appNative, "app"},
};

JSValue gatedNative(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv, int magic,
                    JSValueConst* data) noexcept
{
    try {
        std::int32_t index = -1;
        if (JS_ToInt32(ctx, &index, data[0]) < 0 || index < 0 || static_cast<std::size_t>(index) >= std::size(kGates)) {
            return JS_ThrowInternalError(ctx, "%s", "the script library is damaged");
        }
        const Gate& gate = kGates[static_cast<std::size_t>(index)];
        ServiceRule rule = serviceRule(gate.method);
        std::string request;
        if (gate.native == &appNative && text(ctx, argument(argc, argv, 0), request)) {
            rule = appRule(request);
        }
        if (const std::optional<std::string> refused = Engine::of(ctx).refusal(ctx, rule)) {
            return JS_ThrowTypeError(ctx, "%s", refused->c_str());
        }
        return gate.native(ctx, self, argc, argv, magic);
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue nativeFunction(JSContext* ctx, JSCFunctionMagic* native, const std::string& name, int length, int magic)
{
    const auto gate = std::ranges::find(kGates, native, &Gate::native);
    if (gate == std::end(kGates) || !Engine::of(ctx).gated(ctx)) {
        return JS_NewCFunctionMagic(ctx, native, name.c_str(), length, JS_CFUNC_generic_magic, magic);
    }
    JSValue index = JS_NewInt32(ctx, static_cast<std::int32_t>(gate - std::begin(kGates)));
    JSValue made = JS_NewCFunctionData(ctx, &gatedNative, length, magic, 1, &index);
    if (JS_IsException(made)) {
        return made;
    }
    if (JS_DefinePropertyValueStr(ctx, made, "name", JS_NewStringLen(ctx, name.data(), name.size()),
                                  JS_PROP_CONFIGURABLE) < 0) {
        JS_FreeValue(ctx, made);
        return JS_EXCEPTION;
    }
    return made;
}

constexpr Failure kUntilFailure = {"GS-API-030",
                                   "until() was used and its condition did not come true in time on any try",
                                   "Raise timeoutMs or retry, or check what the condition waits for."};
constexpr Failure kDeviceFailures[] = {
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};
constexpr Failure kPointFailures[] = {
    {"GS-API-002", "a position is outside 0 to 1", "Positions are picture ratios; divide pixels by the picture size."},
};
constexpr Failure kListenFailures[] = {
    {"GS-API-008", "the handler is not a function", "Pass a function."},
    {"GS-API-009", "again is not one of ignore, restart, stop, queue or parallel", "Use one of the five rules."},
};
constexpr Param kSleepParams[] = {{"ms", "number", "How long to wait, in milliseconds (0 to 2147483647)."}};
constexpr Failure kSleepFailures[] = {
    {"RangeError", "ms is not a number from 0 to 2147483647", "Pass a whole number of milliseconds."},
};
constexpr Param kEveryParams[] = {
    {"intervalMs", "number", "Milliseconds between runs, from 1 to 86400000."},
    {"handler", "(event: TaskContext) => void | Promise<void>", "What to run; each run is its own task."},
    {"options", "ListenOptions", "What happens when the last run is still going.", true},
};
constexpr Param kAfterParams[] = {
    {"delayMs", "number", "Milliseconds to wait, from 1 to 86400000."},
    {"handler", "(event: TaskContext) => void | Promise<void>", "What to run, as its own task."},
    {"options", "ListenOptions", "What happens when it is still running from before.", true},
};
constexpr Param kWaitUntilParams[] = {
    {"condition", "() => unknown", "Checked once per frame until it gives a truthy value; may return a Promise."},
    {"options", "UntilOptions", "How long to wait, in milliseconds (5000 by default).", true},
};
constexpr Param kRetryParams[] = {
    {"times", "number", "How many more tries after the first, from 0 to 10."},
    {"action", "(attempt: number) => unknown", "What to try; it gets the number of the try, from 0."},
};
constexpr Param kWithTimeoutParams[] = {
    {"timeoutMs", "number", "The time limit in milliseconds, from 1 to 600000."},
    {"action", "(context: TaskContext) => unknown", "What to run; its signal aborts when the time is up."},
};
constexpr Param kWorkParams[] = {{"work", "...Work[]", "Promises, or functions to start at once."}};
constexpr Param kSequenceParams[] = {
    {"work", "...Work[]", "Functions to run one after another, waiting for each before the next."}};
constexpr Param kMultiGateParams[] = {
    {"name", "string", "The gate's name; the same name keeps the same count."},
    {"times", "number", "How many outputs it has, from 1 to 1000."},
    {"loop", "boolean", "Whether it starts over at the first output after the last one."},
    {"random", "boolean", "Whether the outputs come in a random order."},
    {"work", "...Work[]", "One function per output, then one more for after the last."},
};
constexpr Param kDoNParams[] = {
    {"name", "string", "Which count; the same name with the same limit keeps counting."},
    {"times", "number", "How many times it passes before it says no, from 1 to 1000000."},
};
constexpr Param kOnceParams[] = {
    {"name", "string", "Which one; each name runs once until resetOnce."},
    {"action", "() => unknown", "What to run the first time."},
};
constexpr Param kNameParams[] = {{"name", "string", "The name you gave it."}};
constexpr Param kGateParams[] = {
    {"name", "string", "The gate's name; the same name gives the same gate."},
    {"startClosed", "boolean", "Whether a new gate starts closed (open by default).", true},
};
constexpr Param kThrottleParams[] = {
    {"intervalMs", "number", "The shortest time between two runs, from 1 to 86400000 milliseconds."},
    {"action", "(...values: unknown[]) => unknown", "What to run."},
};
constexpr Param kDebounceParams[] = {
    {"waitMs", "number", "How long it must stay quiet before running, from 1 to 86400000 milliseconds."},
    {"action", "(...values: unknown[]) => unknown", "What to run, with the values of the last call."},
};
constexpr Param kPointParams2[] = {
    {"x", "number", "Horizontal position as a ratio of the picture width: 0 is the left edge, 1 the right edge."},
    {"y", "number", "Vertical position as a ratio of the picture height: 0 is the top edge, 1 the bottom edge."},
};
constexpr Failure kHandlerFailures[] = {
    {"GS-API-003", "the time is out of range", "Pass milliseconds in the range given."},
    {"GS-API-008", "the handler or action is not a function", "Pass a function."},
};
constexpr Failure kWaitFailures[] = {
    {"GS-API-008", "the condition is not a function", "Pass a function."},
    kUntilFailure,
};
constexpr Failure kNameFailures[] = {
    {"GS-API-011", "the name is not a string or is empty", "Pass a name."},
};
constexpr Failure kMultiGateFailures[] = {
    {"GS-API-011", "the name is not a string or is empty", "Pass a name."},
    {"GS-API-017", "times is not a whole number from 1 to 1000", "Pass a whole number of outputs."},
};
constexpr Failure kDoNFailures[] = {
    {"GS-API-011", "the name is not a string or is empty", "Pass a name."},
    {"GS-API-017", "times is not a whole number from 1 to 1000000", "Pass a whole number of times."},
};

constexpr Function kGlobals[] = {
    {"sleep", kSleepParams, "Promise<void>",
     "Waits without blocking other tasks. Rejects with an AbortError when the task is cancelled.",
     "await sleep(500);", &sleepNative, 0, kSleepFailures},
    {"nextFrame", {}, "Promise<void>", "Waits until the next picture frame is shown.", "await nextFrame();",
     &nextFrameNative},
    {"every", kEveryParams, "Subscription", "Runs a handler again and again, waiting intervalMs before each run.",
     "every(500, async () => { if (await vision.find(\"res/pick.png\")) await controls.pick.tap(); });", nullptr, 0,
     kHandlerFailures},
    {"after", kAfterParams, "Subscription", "Runs a handler once after a delay, unless it is disposed first.",
     "after(3000, () => ui.toast(\"Ready\"));", nullptr, 0, kHandlerFailures},
    {"waitUntil", kWaitUntilParams, "Promise<NonNullable<unknown>>",
     "Waits until a condition gives a truthy value and resolves with it; rejects with a TimeoutError (GS-API-030) "
     "when the time is up.",
     "await waitUntil(() => vars.ready.value, { timeoutMs: 3000 });", nullptr, 0, kWaitFailures},
    {"retry", kRetryParams, "Promise<unknown>",
     "Runs an action and tries again when it throws, up to times more; a cancelled task is not retried.",
     "await retry(2, () => touch.tap(0.5, 0.9).until(() => vision.find(\"res/ok.png\")));", nullptr, 0,
     kHandlerFailures},
    {"withTimeout", kWithTimeoutParams, "Promise<unknown>",
     "Runs an action with a time limit: when the time is up its signal aborts and the Promise rejects with a "
     "TimeoutError (GS-API-030).",
     "await withTimeout(5000, async ({ signal }) => { while (!signal.aborted) await controls.fire.trigger(); });",
     nullptr, 0, kHandlerFailures},
    {"parallel", kWorkParams, "Promise<unknown[]>", "Runs several things at the same time and waits for all of them.",
     "await parallel(controls.skill1.trigger(), controls.skill2.trigger());", nullptr},
    {"sequence", kSequenceParams, "Promise<unknown[]>",
     "Runs several things one after another, in order, waiting for each before it starts the next.",
     "await sequence(async () => { await ui.toast(\"one\"); }, async () => { await ui.toast(\"two\"); });", nullptr},
    {"multiGate", kMultiGateParams, "Promise<unknown>",
     "Runs the next of several functions each time it is called, in turn or in random order, and one more "
     "function after they have all run.",
     "await multiGate(\"door\", 2, false, false, async () => {}, async () => {}, async () => {});", nullptr, 0,
     kMultiGateFailures},
    {"race", kWorkParams, "Promise<unknown>", "Runs several things at the same time and waits for the first to finish.",
     "await race(sleep(2000), waitUntil(() => keys.Space.isDown));", nullptr},
    {"doOnce", kOnceParams, "unknown", "Runs an action only the first time it is called with this name.",
     "doOnce(\"greet\", () => ui.toast(\"Hello\"));", nullptr, 0, kNameFailures},
    {"resetOnce", kNameParams, "void", "Lets doOnce run the action with this name again.", "resetOnce(\"greet\");",
     nullptr, 0, kNameFailures},
    {"doN", kDoNParams, "boolean",
     "Says yes for the first times calls with this name, and no after that, until resetDoN with the same name.",
     "if (doN(\"dash\", 3)) await controls.dash.tap();", nullptr, 0, kDoNFailures},
    {"resetDoN", kNameParams, "void", "Lets doN with this name pass that many times again.", "resetDoN(\"dash\");",
     nullptr, 0, kNameFailures},
    {"gate", kGateParams, "Gate", "A named gate that lets an action through only while it is open.",
     "gate(\"loot\").close();", nullptr, 0, kNameFailures},
    {"flipFlop", kNameParams, "\"A\" | \"B\"", "Gives \"A\" and \"B\" in turn, starting with \"A\", for each name.",
     "if (flipFlop(\"light\") === \"A\") controls.light.press(); else controls.light.release();", nullptr, 0,
     kNameFailures},
    {"throttle", kThrottleParams, "(...values: unknown[]) => unknown",
     "Makes a function that runs the action at most once per interval and ignores calls in between.",
     "const shout = throttle(1000, () => ui.toast(\"Fire\"));", nullptr, 0, kHandlerFailures},
    {"debounce", kDebounceParams, "(...values: unknown[]) => void",
     "Makes a function that runs the action only after the calls have stopped for waitMs.",
     "const save = debounce(500, () => store.set(\"x\", 1));", nullptr, 0, kHandlerFailures},
    {"stopwatch", {}, "Stopwatch", "A stopwatch that starts now.", "const watch = stopwatch();", nullptr},
    {"point", kPointParams2, "PicturePoint", "A position on the picture, as ratios.", "await touch.tap(point(0.5, 0.9));",
     nullptr, 0, kPointFailures},
};

constexpr Param kLogParams[] = {{"values", "...unknown[]", "Values to write, joined with spaces."}};

constexpr Param kTimerParams[] = {{"name", "string", "The timer's name."}};

constexpr Function kLog[] = {
    {"info", kLogParams, "void", "Writes an information line to the script console.", "log.info(\"started\", 3);",
     &Engine::jsLog, static_cast<int>(log::Level::Info)},
    {"warn", kLogParams, "void", "Writes a warning line to the script console.", "log.warn(\"slow\");",
     &Engine::jsLog, static_cast<int>(log::Level::Warning)},
    {"error", kLogParams, "void", "Writes an error line to the script console.", "log.error(\"missing\");",
     &Engine::jsLog, static_cast<int>(log::Level::Error)},
    {"time", kTimerParams, "void",
     "Starts a timer with this name; log.timeEnd() with the same name writes how long it has been running.",
     "log.time(\"search\");", nullptr, 0, kNameFailures},
    {"timeEnd", kTimerParams, "number | null",
     "Writes an information line with the milliseconds since log.time() with this name and gives that number; with "
     "no such timer it writes a warning line and gives null.",
     "log.time(\"search\"); await vision.find(\"res/chest.png\"); log.timeEnd(\"search\");", nullptr, 0,
     kNameFailures},
};

constexpr Function kTime[] = {
    {"now", {}, "number", "Milliseconds since the scripts started; virtual time when replaying a recording.",
     "const started = time.now();", &nowNative},
};

constexpr Param kKeyParams[] = {{"key", "AndroidKey", "The Android key to press."}};
constexpr Failure kKeyFailures[] = {
    {"GS-API-004", "the key name is not one of AndroidKey", "Use a name from the AndroidKey type, such as \"back\"."},
    kUntilFailure,
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};
constexpr Failure kDeviceKeyFailures[] = {
    kUntilFailure,
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};

constexpr Param kTapParams[] = {
    {"x", "number | PicturePoint", "Where to tap: a ratio of the picture width, or a point."},
    {"y", "number", "A ratio of the picture height; with a point, the duration instead.", true},
    {"durationMs", "number", "How long the finger stays down, in milliseconds (default 60).", true},
};
constexpr Param kLongPressParams[] = {
    {"x", "number | PicturePoint", "Where to press: a ratio of the picture width, or a point."},
    {"y", "number", "A ratio of the picture height; with a point, the duration instead.", true},
    {"durationMs", "number", "How long the finger stays down, in milliseconds (default 800).", true},
};
constexpr Param kSwipeParams[] = {
    {"fromX", "number | PicturePoint", "Start: a ratio of the picture width, or a point."},
    {"fromY", "number | PicturePoint", "Start height as a ratio; with points, the end point."},
    {"toX", "number", "End, as a ratio of the picture width; with points, the duration.", true},
    {"toY", "number", "End, as a ratio of the picture height.", true},
    {"durationMs", "number", "How long the swipe takes, in milliseconds (default 300).", true},
};
constexpr Param kDownParams[] = {
    {"x", "number | PicturePoint", "Where to put the finger: a ratio of the picture width, or a point."},
    {"y", "number", "A ratio of the picture height; leave it out with a point.", true},
};
constexpr Failure kTouchFailures[] = {
    {"GS-API-002", "a position is outside 0 to 1", "Positions are picture ratios; divide pixels by the picture size."},
    {"GS-API-003", "the duration is not more than 0 and at most 60000", "Pass a duration in milliseconds."},
    kUntilFailure,
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};

constexpr Param kPinchParams[] = {
    {"center", "PicturePoint", "The position between the two fingers."},
    {"from", "number", "How far apart the fingers start, from 0 to 1."},
    {"to", "number", "How far apart they end, from 0 to 1."},
    {"durationMs", "number", "How long the fingers take to move; 300 when not given.", true},
};

constexpr Function kTouch[] = {
    {"tap", kTapParams, "Action<void>",
     "Taps the picture at a position; resolves when the finger is lifted, which is at once if the task is cancelled.",
     "await touch.tap(0.8, 0.9).until(() => vision.find(\"res/playing.png\"), { timeoutMs: 10000, retry: 1 });",
     nullptr, 0, kTouchFailures},
    {.name = "tap",
     .returns = "Action<void>",
     .overload = "tap(x: number, y: number, durationMs?: number): Action<void>"},
    {.name = "tap",
     .returns = "Action<void>",
     .overload = "tap(x: PicturePoint, durationMs?: number): Action<void>"},
    {.name = "tap",
     .returns = "Action<void>",
     .overload = "tap(x: PicturePoint, y?: undefined, durationMs?: number): Action<void>"},
    {"longPress", kLongPressParams, "Action<void>", "Presses and holds the picture at a position, then lifts the finger.",
     "await touch.longPress(point(0.5, 0.5), 1200);", nullptr, 0, kTouchFailures},
    {.name = "longPress",
     .returns = "Action<void>",
     .overload = "longPress(x: number, y: number, durationMs?: number): Action<void>"},
    {.name = "longPress",
     .returns = "Action<void>",
     .overload = "longPress(x: PicturePoint, durationMs?: number): Action<void>"},
    {.name = "longPress",
     .returns = "Action<void>",
     .overload = "longPress(x: PicturePoint, y?: undefined, durationMs?: number): Action<void>"},
    {"swipe", kSwipeParams, "Action<void>",
     "Swipes from one position to another; resolves when it ends. If the task is cancelled, the finger is lifted "
     "where it is.",
     "await touch.swipe(0.5, 0.8, 0.5, 0.2, 250);", nullptr, 0, kTouchFailures},
    {.name = "swipe",
     .returns = "Action<void>",
     .overload = "swipe(fromX: number, fromY: number, toX: number, toY: number, durationMs?: number): Action<void>"},
    {.name = "swipe",
     .returns = "Action<void>",
     .overload = "swipe(from: PicturePoint, to: PicturePoint, durationMs?: number): Action<void>"},
    {.name = "swipe",
     .returns = "Action<void>",
     .overload = "swipe(fromX: PicturePoint, fromY: undefined, toX: PicturePoint, toY?: undefined, "
                 "durationMs?: number): Action<void>"},
    {.name = "swipe",
     .returns = "Action<void>",
     .overload = "swipe(fromX: PicturePoint, fromY: undefined, toX: number, toY: number, durationMs?: number): "
                 "Action<void>"},
    {.name = "swipe",
     .returns = "Action<void>",
     .overload = "swipe(fromX: number, fromY: number, toX: PicturePoint, toY?: undefined, durationMs?: number): "
                 "Action<void>"},
    {"down", kDownParams, "Finger",
     "Puts a finger down and keeps it there until up() or the end of the task that put it down.",
     "const finger = touch.down(0.3, 0.7); await finger.move(0.6, 0.7, 300); finger.up();", nullptr, 0,
     kTouchFailures},
    {.name = "down", .returns = "Finger", .overload = "down(x: number, y: number): Finger"},
    {.name = "down", .returns = "Finger", .overload = "down(x: PicturePoint): Finger"},
    {"pinch", kPinchParams, "Action<void>",
     "Pinches with two fingers around a position: from is how far apart they start and to how far apart they end, as "
     "ratios of the picture width, so a growing distance zooms in. If the task is cancelled, both fingers are lifted.",
     "await touch.pinch(point(0.5, 0.5), 0.1, 0.4, 300);", nullptr, 0, kTouchFailures},
    {"releaseAll", {}, "void", "Lifts every finger the script has put down.", "touch.releaseAll();", nullptr, 0,
     kDeviceFailures},
};

constexpr Param kTypeParams[] = {{"text", "string", "The text to type into the focused box on the phone."}};
constexpr Param kLaunchParams[] = {
    {"packageName", "string", "The app's package name, such as \"com.tencent.tmgp.pubgmhd\"."},
    {"options", "LaunchOptions", "Whether to stop the app first.", true},
};
constexpr Param kForegroundParams[] = {
    {"handler", "(event: TaskContext & ForegroundApp) => void | Promise<void>", "What to run when another app comes "
                                                                               "to the front."},
    {"options", "ListenOptions", "What happens when it comes again while the last task is running.", true},
};
constexpr Param kRotateParams[] = {
    {"orientation", "\"auto\" | \"portrait\" | \"landscape\"",
     "Keep the screen upright or sideways, or let it follow the phone again."},
};
constexpr Param kTurnParams[] = {
    {"handler", "(event: TaskContext & DeviceInfo) => void | Promise<void>",
     "What to run when the screen turns; the event has the new size and rotation."},
    {"options", "ListenOptions", "What happens when it turns again while the last task is running.", true},
};
constexpr Param kDisconnectParams[] = {
    {"handler", "(event: TaskContext) => void | Promise<void>",
     "What to run when the connection to the phone drops and GameScrcpy starts reconnecting."},
    {"options", "ListenOptions", "What happens when it drops again while the last task is running.", true},
};
constexpr Param kReconnectParams[] = {
    {"handler", "(event: TaskContext) => void | Promise<void>",
     "What to run when the phone is connected again after it dropped."},
    {"options", "ListenOptions", "What happens when it comes back again while the last task is running.", true},
};
constexpr Param kInputBoxParams[] = {
    {"handler", "(event: TaskContext & InputBox) => void | Promise<void>",
     "What to run when an input box opens or closes on the phone; the event says which and what kind of box."},
    {"options", "ListenOptions", "What happens when it changes again while the last task is running.", true},
};
constexpr Failure kRotateFailures[] = {
    {"GS-API-017", "the orientation is not \"auto\", \"portrait\" or \"landscape\"", "Write one of the three."},
};
constexpr Failure kSystemPanelFailures[] = {
    {"GS-API-090", "the logic does not run on a phone", "Start the script on a phone that is mirroring."},
    {"GS-API-091",
     "the phone is connected without the GameScrcpy server, such as in a window without a picture or through a "
     "capture card",
     "Mirror the phone over USB or WiFi to reach its notification shade."},
};
constexpr Failure kTypeFailures[] = {
    {"GS-API-011", "the text is not a string or is longer than 65536 bytes", "Pass shorter text."},
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};

constexpr Function kDevice[] = {
    {"info", {}, "DeviceInfo", "The phone this script runs on: model, Android version, screen size and rotation.",
     "const { width, height } = device.info();", &deviceInfoNative, 0, kDeviceFailures},
    {"back", {}, "Action<void>", "Presses the phone's Back key.", "await device.back();", &deviceKeyNative, 0,
     kDeviceKeyFailures},
    {"home", {}, "Action<void>", "Presses the phone's Home key.", "await device.home();", &deviceKeyNative, 1,
     kDeviceKeyFailures},
    {"recents", {}, "Action<void>", "Opens the list of recent apps.", "await device.recents();", &deviceKeyNative, 2,
     kDeviceKeyFailures},
    {"menu", {}, "Action<void>", "Presses the phone's Menu key.", "await device.menu();", &deviceKeyNative, 3,
     kDeviceKeyFailures},
    {"power", {}, "Action<void>", "Presses the phone's Power key.", "await device.power();", &deviceKeyNative, 4,
     kDeviceKeyFailures},
    {"volumeUp", {}, "Action<void>", "Turns the phone's volume up one step.", "await device.volumeUp();",
     &deviceKeyNative, 5, kDeviceKeyFailures},
    {"volumeDown", {}, "Action<void>", "Turns the phone's volume down one step.", "await device.volumeDown();",
     &deviceKeyNative, 6, kDeviceKeyFailures},
    {"mute", {}, "Action<void>", "Mutes or unmutes the phone.", "await device.mute();", &deviceKeyNative, 7,
     kDeviceKeyFailures},
    {"notifications", {}, "boolean",
     "Pulls down the phone's notification shade, as the More menu of the mirror window's toolbar does; true once the "
     "phone is asked, false when it cannot be asked now, such as while GameScrcpy reconnects to it. When the phone "
     "does not allow it, its mirror window says so. The shade stays down when the logic stops.",
     "if (device.notifications()) await sleep(800);", nullptr, 0, kSystemPanelFailures},
    {"quickSettings", {}, "boolean",
     "Opens the phone's quick settings, as the More menu of the mirror window's toolbar does; true once the phone is "
     "asked, false when it cannot be asked now. They stay open when the logic stops.",
     "device.quickSettings();", nullptr, 0, kSystemPanelFailures},
    {"collapsePanels", {}, "boolean",
     "Closes the phone's notification shade and quick settings, as the More menu of the mirror window's toolbar "
     "does; true once the phone is asked, false when it cannot be asked now.",
     "device.collapsePanels();", nullptr, 0, kSystemPanelFailures},
    {"pressKey", kKeyParams, "Action<void>", "Presses and releases an Android key on the phone.",
     "await device.pressKey(\"back\");", &pressKeyNative, 0, kKeyFailures},
    {"type", kTypeParams, "void", "Types text into the box that has the focus on the phone.",
     "device.type(\"gg\");", &typeNative, 0, kTypeFailures},
    {"launch", kLaunchParams, "boolean", "Starts an app on the phone; false when the phone cannot be asked now.",
     "device.launch(\"com.android.settings\");", nullptr, 0, kNameFailures},
    {"screenOff", {}, "boolean", "Turns the phone's screen off while mirroring goes on.", "device.screenOff();",
     nullptr},
    {"screenOn", {}, "boolean", "Turns the phone's screen back on.", "device.screenOn();", nullptr},
    {"foregroundApp", {}, "ForegroundApp | null", "The app in front on the phone, when the phone has said which.",
     "const app = device.foregroundApp();", nullptr},
    {"onForegroundApp", kForegroundParams, "Subscription", "Starts a task each time another app comes to the front.",
     "device.onForegroundApp(({ game }) => { if (game) mapping.on(); });", nullptr, 0, kListenFailures},
    {"onRotate", kTurnParams, "Subscription", "Starts a task each time the phone's screen turns.",
     "device.onRotate(({ rotation }) => { if (rotation % 2 === 1) mapping.on(); });", nullptr, 0, kListenFailures},
    {"connected", {}, "boolean",
     "Whether the phone is connected now; false while GameScrcpy is reconnecting to it. Taps, swipes, key presses "
     "and other actions made while it is not connected wait and happen once it is back; the logic and tasks go on, and "
     "stop only when reconnecting is given up.",
     "if (!device.connected()) log.warn(\"waiting for the phone\");", nullptr},
    {"onDisconnect", kDisconnectParams, "Subscription",
     "Starts a task each time the connection to the phone drops and reconnecting begins.",
     "device.onDisconnect(() => log.warn(\"the phone dropped\"));", nullptr, 0, kListenFailures},
    {"onReconnect", kReconnectParams, "Subscription",
     "Starts a task each time the phone is connected again after it dropped.",
     "device.onReconnect(async () => { await device.home(); });", nullptr, 0, kListenFailures},
    {"onInputBox", kInputBoxParams, "Subscription",
     "Starts a task each time an input box opens, changes or closes on the phone. It needs the GameScrcpy Companion "
     "input method on the phone; without it, and in replays, it never fires.",
     "device.onInputBox(({ shown, action }) => { if (shown && action === \"send\") log.info(\"chat box\"); });",
     nullptr, 0, kListenFailures},
    {"rotate", kRotateParams, "boolean",
     "Keeps the phone's screen upright or sideways, or lets it follow the phone again, like the screen direction in "
     "the mirror toolbar; false when the phone cannot be asked now.",
     "device.rotate(\"landscape\");", nullptr, 0, kRotateFailures},
};

constexpr Param kClipboardWriteParams[] = {
    {"text", "string", "What to put on the phone's clipboard, up to 262000 bytes as UTF-8."},
};
constexpr Failure kClipboardWriteFailures[] = {
    {"GS-API-011", "the text is not a string", "Pass the text as a string."},
    {"GS-API-017", "the text is longer than 262000 bytes as UTF-8", "Pass shorter text."},
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};

constexpr Function kClipboard[] = {
    {"read", {}, "Promise<string | null>",
     "Reads the text on the phone's clipboard, an empty string when it holds no text; null when the phone cannot "
     "share its clipboard now, such as when it is not mirroring or mirrors an app on its own display, and in replays.",
     "const copied = await clipboard.read(); if (copied) log.info(\"copied\", copied);", &clipboardReadNative, 0,
     kDeviceFailures},
    {"write", kClipboardWriteParams, "Promise<boolean>",
     "Puts text on the phone's clipboard; the computer's clipboard stays as it is. Resolves with true when the text "
     "was sent to the phone, false when the phone cannot share its clipboard now and in replays.",
     "await clipboard.write(\"Room 4421\");", &clipboardWriteNative, 0, kClipboardWriteFailures},
};

constexpr Param kControlParams[] = {{"id", "string", "The control id from the profile in use."}};
constexpr Param kHoldParams[] = {
    {"id", "string", "The control id from the profile in use."},
    {"durationMs", "number", "How long to hold it down, in milliseconds."},
};
constexpr Param kEnableParams[] = {
    {"id", "string", "The control id from the profile in use."},
    {"enabled", "boolean", "Whether the control reacts to its keys."},
};
constexpr Param kTypeFilterParams[] = {
    {"type",
     "ComponentType | Interface<unknown> | { readonly prototype: Component } | { readonly component?: Component }",
     "Only components of this type, of this class or with this interface.", true}};
constexpr Failure kControlFailures[] = {
    {"GS-API-005", "there is no control with this id in the profile in use", "Use an id from mapping.controls()."},
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};
constexpr Failure kTriggerFailures[] = {
    {"GS-API-005", "there is no control with this id in the profile in use", "Use an id from mapping.controls()."},
    kUntilFailure,
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};
constexpr Failure kHoldFailures[] = {
    {"GS-API-005", "there is no control with this id in the profile in use", "Use an id from mapping.controls()."},
    {"GS-API-003", "the duration is missing, not more than 0 or more than 60000", "Pass a duration in milliseconds."},
    kUntilFailure,
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};

constexpr Function kMapping[] = {
    {"controls", {}, "ControlInfo[]", "The controls of the profile in use, with their ids and whether they are on.",
     "const ids = mapping.controls().map((control) => control.id);", &controlsNative, 0, kDeviceFailures},
    {"get", kControlParams, "Component | null", "The component with this id, or null.",
     "mapping.get(\"fire\")?.trigger();", nullptr},
    {"all", kTypeFilterParams, "Component[]",
     "All components of the profile, or those of one type, of one class or with one interface.",
     "for (const stick of mapping.all(\"wasd\")) stick.reset();", nullptr},
    {"trigger", kControlParams, "Action<void>",
     "Presses and releases a control as if its key were tapped; the script's own output never triggers scripts.",
     "await mapping.trigger(\"jump\");", &holdControlNative, 0, kTriggerFailures},
    {"hold", kHoldParams, "Action<void>",
     "Holds a control down for a while, then releases it; released early if the task is cancelled.",
     "await mapping.hold(\"sprint\", 1500);", &holdControlNative, 1, kHoldFailures},
    {"setEnabled", kEnableParams, "void",
     "Turns a control on or off until the script ends or the profile changes; this ends a binding of the control's "
     "enabled.",
     "mapping.setEnabled(\"fire\", false);", &enableControlNative, 1, kControlFailures},
    {"on", {}, "boolean", "Turns key mapping on for this phone.", "mapping.on();", nullptr},
    {"off", {}, "boolean", "Turns key mapping off for this phone, so keys go to the phone as they are.", "mapping.off();",
     nullptr},
    {"toggle", {}, "boolean", "Turns key mapping on or off for this phone.", "mapping.toggle();", nullptr},
    {"isOn", {}, "boolean", "Whether key mapping is on for this phone.", "if (!mapping.isOn()) mapping.on();", nullptr},
};

constexpr Param kOpacityParams[] = {{"opacity", "number", "From 0 (invisible) to 1 (solid)."}};
constexpr Param kRevealParams[] = {
    {"on", "boolean", "true to draw every key hint at full opacity, false to give them back their usual look."}};

constexpr Function kHints[] = {
    {"show", {}, "boolean", "Shows the key hints on the picture.", "hints.show();", nullptr},
    {"hide", {}, "boolean", "Hides the key hints on the picture.", "hints.hide();", nullptr},
    {"toggle", {}, "boolean", "Shows or hides the key hints.", "hints.toggle();", nullptr},
    {"setOpacity", kOpacityParams, "boolean", "Sets how solid the key hints are.", "hints.setOpacity(0.3);", nullptr},
    {"reveal", kRevealParams, "boolean",
     "While on, every key hint of this phone is drawn at full opacity, even when the hints are hidden or faded; "
     "typing on the phone still hides them. Each logic and each task has its own switch, which turns off when that "
     "logic or task stops.",
     "keys.Tab.onPress(() => { hints.reveal(true); }); keys.Tab.onRelease(() => { hints.reveal(false); });", nullptr},
};

constexpr Param kSwitchParams[] = {{"name", "string", "The id or the name of a profile in the library."}};

constexpr Function kProfile[] = {
    {"current", {}, "ProfileInfo | null", "The profile this script belongs to.", "log.info(profile.current()?.name);",
     nullptr},
    {"switchTo", kSwitchParams, "boolean",
     "Switches this phone to another profile by id or name; this script stops and that profile's logic starts.",
     "profile.switchTo(\"Driving\");", nullptr, 0, kNameFailures},
    {"next", {}, "boolean", "Switches this phone to the next profile in the library.", "profile.next();", nullptr},
    {"previous", {}, "boolean", "Switches this phone to the previous profile in the library.", "profile.previous();",
     nullptr},
    {"save", {}, "Promise<boolean>",
     "Asks in the phone's mirror window whether to keep the changes this logic made to its components (each shown as "
     "old and new value) in the profile, and resolves with whether they were saved. Saving reloads the profile and "
     "starts the logic again, as saving in the key editor does. Resolves with false at once when nothing was changed, "
     "while the key editor is open, for a profile that is not in the library, and in a task.",
     "controls.fire.setRate(12); await profile.save();", &saveProfileNative, 0, kDeviceFailures},
};

constexpr Param kClampParams[] = {
    {"value", "number", "The number."},
    {"low", "number", "The smallest it may be."},
    {"high", "number", "The largest it may be."},
};
constexpr Param kLerpParams[] = {
    {"from", "number", "Where it starts."},
    {"to", "number", "Where it ends."},
    {"t", "number", "How far along, 0 at from and 1 at to."},
};
constexpr Param kRandomParams[] = {
    {"low", "number", "The smallest it may be (0 by default).", true},
    {"high", "number", "The largest it may be (1 by default).", true},
};
constexpr Param kRandomIntParams[] = {
    {"low", "number", "The smallest whole number it may be."},
    {"high", "number", "The largest whole number it may be."},
};
constexpr Param kRegionParams1[] = {{"region", "Region", "Part of the picture, as ratios."}};
constexpr Param kTwoPointParams[] = {
    {"from", "PicturePoint", "The first position."},
    {"to", "PicturePoint", "The second position."},
};
constexpr Param kRoundParams[] = {{"value", "number", "The number."}};
constexpr Param kTwoNumberParams[] = {
    {"a", "number", "The first number."},
    {"b", "number", "The second number."},
};
constexpr Failure kMathFailures[] = {
    {"GS-API-017", "a value is not a finite number", "Pass numbers."},
};

constexpr Function kMath[] = {
    {"clamp", kClampParams, "number", "Keeps a number between low and high.", "math.clamp(1.4, 0, 1);", nullptr, 0,
     kMathFailures},
    {"lerp", kLerpParams, "number", "The number t of the way from from to to.", "math.lerp(0, 10, 0.25);", nullptr, 0,
     kMathFailures},
    {"random", kRandomParams, "number", "A random number from low up to high.", "math.random(0.4, 0.6);", nullptr, 0,
     kMathFailures},
    {"randomInt", kRandomIntParams, "number", "A random whole number from low to high, both included.",
     "math.randomInt(1, 6);", nullptr, 0, kMathFailures},
    {"randomIn", kRegionParams1, "PicturePoint", "A random position inside a region.",
     "await touch.tap(math.randomIn(res.region(\"Button\")));", nullptr, 0, kMathFailures},
    {"distance", kTwoPointParams, "number", "How far apart two positions are, as a ratio of the picture.",
     "math.distance(point(0, 0), point(0.3, 0.4));", nullptr, 0, kMathFailures},
    {"angle", kTwoPointParams, "number",
     "The direction from one position to another in degrees: 0 is up, 90 right, 180 down and 270 left.",
     "math.angle(point(0.5, 0.5), point(0.5, 0.2));", nullptr, 0, kMathFailures},
    {"round", kRoundParams, "number", "The nearest whole number; a half goes up.", "math.round(1.6);", nullptr, 0,
     kMathFailures},
    {"floor", kRoundParams, "number", "The largest whole number that is not more than the value.",
     "math.floor(1.6);", nullptr, 0, kMathFailures},
    {"ceil", kRoundParams, "number", "The smallest whole number that is not less than the value.", "math.ceil(1.2);",
     nullptr, 0, kMathFailures},
    {"abs", kRoundParams, "number", "The value without its sign.", "math.abs(-3);", nullptr, 0, kMathFailures},
    {"min", kTwoNumberParams, "number", "The smaller of two numbers.", "math.min(2, 5);", nullptr, 0, kMathFailures},
    {"max", kTwoNumberParams, "number", "The larger of two numbers.", "math.max(2, 5);", nullptr, 0, kMathFailures},
};

constexpr Param kListItems[] = {{"items", "unknown[]", "The list."}};
constexpr Param kListValue[] = {
    {"items", "unknown[]", "The list."},
    {"value", "unknown", "The value."},
};
constexpr Param kListIndex[] = {
    {"items", "unknown[]", "The list."},
    {"index", "number", "The position, counted from 0."},
};
constexpr Param kListIndexValue[] = {
    {"items", "unknown[]", "The list."},
    {"index", "number", "The position, counted from 0."},
    {"value", "unknown", "The value."},
};
constexpr Failure kListFailures[] = {
    {"GS-API-011", "the list is not a list", "Pass a list, such as a variable of the list kind."},
    {"GS-API-017", "the position is not a whole number in the list",
     "Pass a position from 0 up to the length of the list."},
};

constexpr Function kList[] = {
    {"add", kListValue, "void", "Adds a value to the end of the list.", "list.add(names, \"bo\");", nullptr, 0,
     kListFailures},
    {"insert", kListIndexValue, "void", "Puts a value into the list at a position; the rest move up.",
     "list.insert(names, 0, \"bo\");", nullptr, 0, kListFailures},
    {"removeAt", kListIndex, "unknown", "Takes the value at a position out of the list and gives it back.",
     "const first = list.removeAt(names, 0);", nullptr, 0, kListFailures},
    {"remove", kListValue, "boolean", "Takes the first value equal to it out of the list; says whether it found one.",
     "list.remove(names, \"bo\");", nullptr, 0, kListFailures},
    {"indexOf", kListValue, "number", "The position of the first value equal to it, or -1 when it is not there.",
     "if (list.indexOf(names, \"bo\") < 0) await ui.toast(\"missing\");", nullptr, 0, kListFailures},
    {"at", kListIndex, "unknown", "The value at a position, or undefined when the list is shorter.",
     "const first = list.at(names, 0);", nullptr, 0, kListFailures},
    {"setAt", kListIndexValue, "void", "Replaces the value at a position.",
     "list.setAt(names, 0, \"bo\");", nullptr, 0, kListFailures},
    {"size", kListItems, "number", "How many values the list has.", "if (list.size(names) === 0) await ui.toast(\"empty\");",
     nullptr, 0, kListFailures},
    {"has", kListValue, "boolean", "Whether the list has a value equal to it.", "if (list.has(names, \"bo\")) await sleep(10);",
     nullptr, 0, kListFailures},
    {"sort", kListItems, "void", "Sorts the list in place: numbers by size, other values as text.",
     "list.sort(names);", nullptr, 0, kListFailures},
    {"shuffle", kListItems, "void", "Mixes the list into a random order, in place.", "list.shuffle(names);", nullptr, 0,
     kListFailures},
    {"clear", kListItems, "void", "Takes every value out of the list.", "list.clear(names);", nullptr, 0, kListFailures},
};

constexpr Param kDictKey[] = {
    {"map", "Map<unknown, unknown>", "The dictionary."},
    {"key", "unknown", "The key."},
};
constexpr Param kDictSet[] = {
    {"map", "Map<unknown, unknown>", "The dictionary."},
    {"key", "unknown", "The key."},
    {"value", "unknown", "The value."},
};
constexpr Param kDictMap[] = {{"map", "Map<unknown, unknown>", "The dictionary."}};
constexpr Failure kDictFailures[] = {
    {"GS-API-011", "the dictionary is not a dictionary", "Pass a dictionary from dict.make or a variable of the dictionary kind."},
};

constexpr Function kDict[] = {
    {"make", {}, "Map<unknown, unknown>", "A new, empty dictionary.", "const scores = dict.make();", nullptr, 0,
     kDictFailures},
    {"set", kDictSet, "void", "Stores a value under a key, replacing what was there.", "dict.set(scores, \"bo\", 3);",
     nullptr, 0, kDictFailures},
    {"get", kDictKey, "unknown", "The value stored under a key, or undefined.",
     "await ui.toast(String(dict.get(scores, \"bo\")));", nullptr, 0, kDictFailures},
    {"has", kDictKey, "boolean", "Whether the dictionary has a value under that key.",
     "if (dict.has(scores, \"bo\")) await sleep(10);", nullptr, 0, kDictFailures},
    {"remove", kDictKey, "boolean", "Removes the value under a key; says whether there was one.",
     "dict.remove(scores, \"bo\");", nullptr, 0, kDictFailures},
    {"keys", kDictMap, "unknown[]", "All the keys of the dictionary, as a list.", "for (const key of dict.keys(scores)) log.info(key);",
     nullptr, 0, kDictFailures},
    {"values", kDictMap, "unknown[]", "All the values of the dictionary, as a list.",
     "for (const score of dict.values(scores)) log.info(score);", nullptr, 0, kDictFailures},
    {"size", kDictMap, "number", "How many keys the dictionary has.", "if (dict.size(scores) > 0) await sleep(10);",
     nullptr, 0, kDictFailures},
    {"clear", kDictMap, "void", "Takes every key out of the dictionary.", "dict.clear(scores);", nullptr, 0,
     kDictFailures},
};

constexpr Param kSetItems[] = {{"items", "Set<unknown>", "The set."}};
constexpr Param kSetValue[] = {
    {"items", "Set<unknown>", "The set."},
    {"value", "unknown", "The value."},
};
constexpr Failure kSetFailures[] = {
    {"GS-API-011", "the set is not a set", "Pass a set from set.make or a variable of the set kind."},
};

constexpr Function kSet[] = {
    {"make", {}, "Set<unknown>", "A new, empty set.", "const seen = set.make();", nullptr, 0, kSetFailures},
    {"add", kSetValue, "void", "Adds a value to the set; a value that is already there stays once.",
     "set.add(seen, \"bo\");", nullptr, 0, kSetFailures},
    {"has", kSetValue, "boolean", "Whether the set has that value.", "if (set.has(seen, \"bo\")) await sleep(10);", nullptr, 0,
     kSetFailures},
    {"remove", kSetValue, "boolean", "Takes a value out of the set; says whether it was there.",
     "set.remove(seen, \"bo\");", nullptr, 0, kSetFailures},
    {"values", kSetItems, "unknown[]", "All the values of the set, as a list.", "for (const name of set.values(seen)) log.info(name);",
     nullptr, 0, kSetFailures},
    {"size", kSetItems, "number", "How many values the set has.", "if (set.size(seen) > 0) await sleep(10);", nullptr, 0,
     kSetFailures},
    {"clear", kSetItems, "void", "Takes every value out of the set.", "set.clear(seen);", nullptr, 0, kSetFailures},
};

constexpr Param kTextJoin[] = {
    {"parts", "unknown[]", "The values to join, in order."},
    {"separator", "string", "What goes between them (nothing by default).", true},
};
constexpr Param kTextFormat[] = {
    {"template", "string", "The text, with {0}, {1} and so on where the values go."},
    {"values", "...unknown[]", "The values, in the order of the numbers."},
};
constexpr Param kTextValue[] = {{"text", "string", "The text."}};
constexpr Param kTextPart[] = {
    {"text", "string", "The text."},
    {"part", "string", "What to look for."},
};
constexpr Param kTextSplit[] = {
    {"text", "string", "The text to cut."},
    {"separator", "string", "Where to cut it."},
};
constexpr Param kTextReplace[] = {
    {"text", "string", "The text."},
    {"part", "string", "What to look for; only the first one is replaced."},
    {"instead", "string", "What to put there."},
};
constexpr Failure kTextFailures[] = {
    {"GS-API-011", "one of the arguments is not text", "Pass text."},
};

constexpr Function kText[] = {
    {"join", kTextJoin, "string", "Joins values into one text, with a separator between them.",
     "const line = text.join(names, \", \");", nullptr, 0, kTextFailures},
    {"format", kTextFormat, "string", "Fills {0}, {1} and so on in a template with the values.",
     "await ui.toast(text.format(\"Round {0} of {1}\", round, total));", nullptr, 0, kTextFailures},
    {"find", kTextPart, "number", "Where the part starts, or -1 when it is not there.",
     "if (text.find(line, \"Round\") === 0) await sleep(10);", nullptr, 0, kTextFailures},
    {"has", kTextPart, "boolean", "Whether the text contains the part.", "if (text.has(line, \"Round\")) await sleep(10);",
     nullptr, 0, kTextFailures},
    {"split", kTextSplit, "string[]", "Cuts the text wherever the separator is and gives the pieces as a list.",
     "const fields = text.split(line, \",\");", nullptr, 0, kTextFailures},
    {"replace", kTextReplace, "string", "Gives the text with the first part replaced by the other text.",
     "const fixed = text.replace(line, \"bo\", \"Bo\");", nullptr, 0, kTextFailures},
    {"upper", kTextValue, "string", "The same text in upper case.", "text.upper(name);", nullptr, 0, kTextFailures},
    {"lower", kTextValue, "string", "The same text in lower case.", "text.lower(name);", nullptr, 0, kTextFailures},
    {"trim", kTextValue, "string", "The same text without spaces at either end.", "text.trim(name);", nullptr, 0,
     kTextFailures},
    {"length", kTextValue, "number", "How many characters the text has.", "if (text.length(name) > 8) await sleep(10);",
     nullptr, 0, kTextFailures},
};

constexpr Param kStoreKey[] = {{"key", "string", "The name of the value."}};
constexpr Param kStoreSet[] = {
    {"key", "string", "The name of the value."},
    {"value", "StoreValue", "A JSON value: null, a boolean, number, string, array or plain object."},
};
constexpr Failure kStoreFailures[] = {
    {"GS-API-006", "the key is not a string or the value is not a JSON value", "Store plain data only."},
    {"GS-API-007", "the store would be larger than 1 MiB", "Remove keys you no longer need."},
};

constexpr Function kStore[] = {
    {"get", kStoreKey, "StoreValue | undefined", "Reads a value this script stored earlier, or undefined.",
     "const best = store.get(\"best\") ?? 0;", &storeNative, 0, kStoreFailures},
    {"set", kStoreSet, "void", "Stores a value that stays after the script and the program restart.",
     "store.set(\"best\", 42);", &storeNative, 1, kStoreFailures},
    {"remove", kStoreKey, "void", "Removes a stored value.", "store.remove(\"best\");", &storeNative, 2,
     kStoreFailures},
    {"keys", {}, "string[]", "The names of all stored values.", "for (const key of store.keys()) log.info(key);",
     &storeNative, 3},
};

constexpr Param kToastParams[] = {{"message", "string", "What to show; longer than 100 characters is cut short."}};
constexpr Param kConfirmParams[] = {
    {"question", "string", "The question, up to 200 characters."},
    {"options", "ConfirmOptions", "Other words for the two buttons.", true},
};
constexpr Param kChooseParams[] = {
    {"question", "string", "The question, up to 200 characters."},
    {"choices", "string[]", "Two to eight choices, each up to 60 characters."},
};
constexpr Param kAskParams[] = {
    {"question", "string", "The question, up to 200 characters."},
    {"options", "AskOptions", "The text already in the box.", true},
};
constexpr Failure kUiFailures[] = {
    {"GS-API-011", "the message or question is missing or not a string", "Pass the text as a string."},
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};
constexpr Failure kChooseFailures[] = {
    {"GS-API-011", "the question is missing or not a string", "Pass the question as a string."},
    {"GS-API-012", "the choices are not two to eight strings", "Pass between two and eight short strings."},
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};
constexpr Param kBoxParams[] = {
    {"x", "number", "Left edge, as a ratio of the picture width."},
    {"y", "number", "Top edge, as a ratio of the picture height."},
    {"width", "number", "Width as a ratio of the picture width; the box stays inside the picture."},
    {"height", "number", "Height as a ratio of the picture height."},
    {"options", "BoxOptions", "Color, label and how long it stays.", true},
};
constexpr Param kTextParams[] = {
    {"x", "number", "Left edge, as a ratio of the picture width."},
    {"y", "number", "Top edge, as a ratio of the picture height."},
    {"text", "string", "What to write; longer than 100 characters is cut short."},
    {"options", "TextOptions", "Color and how long it stays.", true},
};
constexpr Param kMarkParams[] = {
    {"x", "number", "Where the point is, as a ratio of the picture width."},
    {"y", "number", "Where the point is, as a ratio of the picture height."},
    {"options", "MarkOptions", "Color, label and how long it stays.", true},
};
constexpr Failure kDrawFailures[] = {
    {"GS-API-002", "a position or size is outside 0 to 1, or the box leaves the picture",
     "Positions and sizes are picture ratios."},
    {"GS-API-003", "the duration is not more than 0 and at most 600000", "Pass a duration in milliseconds."},
    {"GS-API-010", "64 drawings are already shown", "Dispose the drawings you no longer need."},
    {"GS-API-011", "the text, label or options are not strings or an object", "Pass strings and a plain object."},
    {"GS-API-014", "the color is not one of DrawColor", "Use accent, good, warn, bad or white."},
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};
constexpr Param kProgressParams[] = {
    {"title", "string | null", "What the card says, up to 80 characters; null takes the card away."},
    {"value", "number", "How far along, from 0 to 1; leave it out only with null.", true},
};
constexpr Failure kProgressFailures[] = {
    {"GS-API-017", "the progress is not a number from 0 to 1", "Pass a ratio such as done / total."},
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};
constexpr Param kNotifyParams[] = {
    {"title", "string", "The title, up to 64 characters."},
    {"text", "string", "The line under the title, up to 256 characters.", true},
};
constexpr Failure kNotifyFailures[] = {
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};
constexpr Param kPanelParams[] = {
    {"name", "string", "The name the extension's manifest gives the panel in contributes.panels."},
};
constexpr Failure kPanelFailures[] = {
    {"GS-API-011", "the name is not a string or is empty", "Pass the name from contributes.panels."},
};
constexpr Param kMenuParams[] = {
    {"items", "readonly UiMenuItem[]", "Up to 16 items; an empty list takes the group away."},
};
constexpr Failure kMenuFailures[] = {
    {"GS-API-075", "the items are not a list of up to 16 items with an id and a title each; the message names the "
                   "item that is wrong",
     "Fix the item the message names."},
    {"GS-API-076", "the extension declares no right-click menu for this logic, or the logic is not an extension's",
     "Declare a panel with where \"menu\" whose script is this one."},
};
constexpr Param kStatusParams[] = {
    {"name", "string", "The name the extension's manifest gives the widget in contributes.panels."},
};
constexpr Param kMenuOnParams[] = {
    {"type", "\"select\"", "The event: select, when an item of the group is picked."},
    {"handler", "(event: UiEvent) => void | Promise<void>",
     "What to run; event.id is the item's id, and event.value its new checked state or null."},
    {"options", "ListenOptions", "What happens when it comes again while the last task is running.", true},
};
constexpr Failure kMenuOnFailures[] = {
    {"GS-API-017", "the event is not select", "Listen to \"select\"."},
    {"GS-API-008", "the handler is not a function", "Pass a function."},
    {"GS-API-009", "again is not one of ignore, restart, stop, queue or parallel", "Use one of the five rules."},
};
constexpr Param kImageParams[] = {
    {"image", "TemplateName | ImagePath | CapturedImage",
     "A template name, a picture in the profile folder or an image from vision.capture()."},
    {"region", "Region | RegionName",
     "Where to show it, stretched to fit; an image from vision.capture() goes where it was taken by default.", true},
    {"options", "BoxOptions", "The frame's color, a label and how long it stays.", true},
};
constexpr Failure kImageFailures[] = {
    {"GS-API-015", "the image is not a template name, a png, jpg or bmp path inside the profile folder or an image "
                   "from vision.capture()",
     "Use a name made in the vision workbench, or a relative path such as \"res/chest.png\"."},
    {"GS-API-018", "there is no template or region with that name", "Use a name from res/resources.json."},
    {"GS-API-020", "res/resources.json cannot be read or is not valid", "Open the vision workbench, or fix the file."},
    {"GS-API-002", "the region is missing for a template or a picture file, or is outside the picture",
     "Regions are picture ratios."},
    {"GS-API-003", "the duration is not more than 0 and at most 600000", "Pass a duration in milliseconds."},
    {"GS-API-010", "64 drawings are already shown", "Dispose the drawings you no longer need."},
    {"GS-API-011", "the label or options are not a string or an object", "Pass strings and a plain object."},
    {"GS-API-014", "the color is not one of DrawColor", "Use accent, good, warn, bad or white."},
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};

constexpr Function kUi[] = {
    {"toast", kToastParams, "void", "Shows a short message in the phone's mirror window.", "ui.toast(\"Round 2\");",
     &toastNative, 0, kUiFailures},
    {"confirm", kConfirmParams, "Promise<boolean>",
     "Asks a yes or no question in the phone's mirror window and resolves with the answer; cancelling the task "
     "closes the question.",
     "if (await ui.confirm(\"Sell the old gear?\")) log.info(\"selling\");", &promptNative,
     static_cast<int>(PromptKind::Confirm), kUiFailures},
    {"choose", kChooseParams, "Promise<number | null>",
     "Offers choices in the phone's mirror window and resolves with the index chosen, or null when it is closed.",
     "const mode = await ui.choose(\"Mode\", [\"Easy\", \"Hard\"]);", &promptNative,
     static_cast<int>(PromptKind::Choose), kChooseFailures},
    {"ask", kAskParams, "Promise<string | null>",
     "Asks for a line of text in the phone's mirror window and resolves with it, or null when it is closed.",
     "const team = await ui.ask(\"Team name\");", &promptNative, static_cast<int>(PromptKind::Ask), kUiFailures},
    {"box", kBoxParams, "Drawing",
     "Draws a box on the phone picture until it is disposed, its duration ends or the task that drew it ends.",
     "const found = ui.box(0.4, 0.3, 0.2, 0.1, { color: \"good\", label: \"chest\" });", &drawNative,
     static_cast<int>(DrawingKind::Box), kDrawFailures},
    {"text", kTextParams, "Drawing",
     "Writes text on the phone picture until it is disposed, its duration ends or the task that drew it ends.",
     "ui.text(0.05, 0.05, \"Round 2\", { durationMs: 3000 });", &drawNative, static_cast<int>(DrawingKind::Text),
     kDrawFailures},
    {"mark", kMarkParams, "Drawing",
     "Marks a point on the phone picture with a ring and a dot until it is disposed, its duration ends or the task "
     "that drew it ends; the label shows to the right of the ring.",
     "const spot = ui.mark(0.5, 0.8, { color: \"warn\", label: \"tap here\", durationMs: 2000 });", &drawNative,
     static_cast<int>(DrawingKind::Mark), kDrawFailures},
    {"image", kImageParams, "Drawing",
     "Shows a picture on the phone picture in a thin frame until it is disposed, its duration ends or the task that "
     "drew it ends. An image from vision.capture() that is no longer kept shows nothing.",
     "const score = await vision.capture(res.region(\"Score\")); if (score) ui.image(score, { x: 0.7, y: 0.02, "
     "width: 0.28, height: 0.08 });",
     &imageNative, 0, kImageFailures},
    {"progress", kProgressParams, "void",
     "Shows a card at the top of the phone picture with the title, a bar and the percentage. Each logic and each "
     "task has one card: calling again updates it, and it goes away with ui.progress(null) or when the logic or task "
     "stops.",
     "ui.progress(\"Rounds\", done / total);", nullptr, 0, kProgressFailures},
    {"notify", kNotifyParams, "boolean",
     "Shows a Windows notification; clicking it opens the phone's mirror window. Each logic and each task shows at "
     "most one every 10 seconds; false means this one was not shown.",
     "ui.notify(\"Done\", \"All 30 rounds finished\");", nullptr, 0, kNotifyFailures},
    {"panel", kPanelParams, "UiPanel",
     "Gives a panel of this extension, declared in contributes.panels: a side panel of the mirror window or a page "
     "of the main window. GameScrcpy draws the tree given to render() with its own controls; no interface code of "
     "the extension runs.",
     "const stats = ui.panel(\"stats\"); stats.render({ type: \"text\", text: \"Rounds: 3\" });", nullptr, 0,
     kPanelFailures},
    {"menu", kMenuParams, "void",
     "Puts this extension's group of items into the right-click menu on the phone picture, in place of the last "
     "ones; ui.menu.on(\"select\", handler) hears the item picked.",
     "ui.menu([{ id: \"reset\", title: \"Reset the counter\" }]);", nullptr, 0, kMenuFailures},
    {"status", kStatusParams, "UiStatus",
     "Gives a status widget of this extension, declared in contributes.panels: a little text, icons and progress "
     "shown with the status of the mirror window.",
     "ui.status(\"fps\").render({ type: \"text\", text: \"60\" });", nullptr, 0, kPanelFailures},
};

constexpr Function kUiMenu[] = {
    {"on", kMenuOnParams, "Subscription",
     "Starts a task each time an item of this extension's group in the right-click menu is picked.",
     "ui.menu.on(\"select\", ({ id }) => log.info(\"picked\", id));", nullptr, 0, kMenuOnFailures},
};

constexpr Param kInputNameParams[] = {
    {"name", "InputName", "A key or mouse button, written as in profiles: \"KeyW\", \"Space\", \"Mouse.Left\"."},
};
constexpr Failure kReadFailure = {"GS-API-098", "this logic may not read your keys and cursor yet",
                                  "Allow it when the phone's mirror window asks; logic made in GameScrcpy and "
                                  "profiles imported with confirmation may read them from the start."};
constexpr Failure kCursorFailures[] = {
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
    kReadFailure,
};
constexpr Failure kInputFailures[] = {
    {"GS-API-013", "the name is not a string", "Write the key as in profiles, such as \"KeyW\"."},
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
    kReadFailure,
};

constexpr Failure kHardwarePermissionFailure = {
    "GS-API-080", "the logic belongs to an extension whose manifest does not ask for the permission hardware",
    "Add \"hardware\" to the permissions in the extension's manifest."};
constexpr Param kSourceParams[] = {
    {"name", "string", "The name of the input source: 1 to 32 lowercase letters, digits or -."},
};
constexpr Failure kSourceFailures[] = {
    {"GS-API-085", "the name is not 1 to 32 lowercase letters, digits or -",
     "Use a name such as \"deck\" or \"pedal-board\"."},
    kHardwarePermissionFailure,
};

constexpr Function kInput[] = {
    {"source", kSourceParams, "InputSource",
     "An input source of this logic, such as a device it reads: the keys it presses and the axes it moves reach the "
     "phone's key mapping as the input Ext.<extension id>.<name>.<key>, or Ext.mine.<name>.<key> for your own logic, "
     "which profiles bind like any key. While the key editor waits for a key, a press is taken as that key.",
     "const deck = input.source(\"deck\"); deck.press(\"k1\"); deck.release(\"k1\");", nullptr, 0, kSourceFailures},
    {"cursor", {}, "PicturePoint | null",
     "Where the pointer is over the phone picture, as ratios; null when the phone's mirror window is not in front "
     "or the pointer is outside the picture.",
     "const at = input.cursor();", &cursorNative, 0, kCursorFailures},
    {"isDown", kInputNameParams, "boolean",
     "Whether a key or mouse button is held; always false when the phone's mirror window is not in front.",
     "if (input.isDown(\"ShiftLeft\")) log.info(\"walking\");", &keyDownNative, 0, kInputFailures},
};

constexpr Param kFocusParams[] = {
    {"handler", "(event: TaskContext) => void | Promise<void>",
     "What to run when the phone's mirror window becomes the active window."},
    {"options", "ListenOptions", "What happens when it comes again while the last task is running.", true},
};
constexpr Param kBlurParams[] = {
    {"handler", "(event: TaskContext) => void | Promise<void>",
     "What to run when the phone's mirror window stops being the active window."},
    {"options", "ListenOptions", "What happens when it comes again while the last task is running.", true},
};

constexpr Function kWindow[] = {
    {"focused", {}, "boolean",
     "Whether the phone's mirror window is the active window, the one that gets the keyboard; false while another "
     "window is in front or the mirror window is closed.",
     "if (!window.focused()) log.info(\"the mirror window is in the background\");", nullptr},
    {"onFocus", kFocusParams, "Subscription",
     "Starts a task each time the phone's mirror window becomes the active window.",
     "window.onFocus(() => hints.show());", nullptr, 0, kListenFailures},
    {"onBlur", kBlurParams, "Subscription",
     "Starts a task each time the phone's mirror window stops being the active window, such as when another window "
     "comes to the front.",
     "window.onBlur(() => touch.releaseAll());", nullptr, 0, kListenFailures},
};

constexpr Param kFindParams[] = {
    {"image", "TemplateName | ImagePath | CapturedImage",
     "A template name from res/resources.json, whose threshold, match mode, region and picture size are the defaults, "
     "a picture in the profile folder such as \"res/chest.png\" (png, jpg or bmp) cut from the mirrored picture at the "
     "same size, or an image from vision.capture(), compared in color and 0.9 alike by default like a template cut in "
     "the vision workbench; transparent pixels are ignored."},
    {"options", "FindOptions", "Where to look, how alike it must be, how many and how long to wait for a picture.", true},
};
constexpr Failure kFindFailures[] = {
    {"GS-API-015", "the image is not a template name, a png, jpg or bmp path inside the profile folder or an image "
                   "from vision.capture()",
     "Use a name made in the vision workbench, or a relative path such as \"res/chest.png\"."},
    {"GS-API-018", "there is no template or region with that name", "Use a name from res/resources.json."},
    {"GS-API-020", "res/resources.json cannot be read or is not valid", "Open the vision workbench, or fix the file."},
    {"GS-API-016", "the picture cannot be read, is empty, fully transparent or a single colour",
     "Save it again as a png cut from the mirrored picture."},
    {"GS-API-037", "the image from vision.capture() is no longer kept: a script keeps its 8 latest",
     "Keep the ones you need with save(), or capture again."},
    {"GS-API-002", "the region is outside the picture", "Regions are picture ratios."},
    {"GS-API-003", "the timeout is not more than 0 and at most 10000", "Pass milliseconds."},
    {"GS-API-017", "the threshold is not from 0 to 1, or max is not from 1 to 64", "Use a ratio and a whole number."},
    {"GS-API-019", "match is not \"color\" or \"grey\"", "Write one of the two."},
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};

constexpr Param kFindColorParams[] = {
    {"color", "ColorName | HexColor", "A color written \"#RRGGBB\", or the name of a color in res/resources.json, "
                                      "whose tolerance and region are the defaults."},
    {"options", "ColorOptions", "Where to look, how far a pixel may be from the color and how long to wait for a picture.",
     true},
};
constexpr Failure kFindColorFailures[] = {
    {"GS-API-019", "the color is not a string written \"#RRGGBB\" or a name", "Write it as \"#F5C542\" or use a name."},
    {"GS-API-018", "there is no color or region with that name", "Use a name from res/resources.json."},
    {"GS-API-020", "res/resources.json cannot be read or is not valid", "Open the vision workbench, or fix the file."},
    {"GS-API-002", "the region is outside the picture", "Regions are picture ratios."},
    {"GS-API-003", "the timeout is not more than 0 and at most 10000", "Pass milliseconds."},
    {"GS-API-017", "the tolerance is not from 0 to 1", "Use a ratio."},
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};

constexpr Param kWaitFindParams[] = {
    {"image", "TemplateName | ImagePath | CapturedImage",
     "A template name, a picture in the profile folder or an image from vision.capture(), as for find()."},
    {"options", "WaitOptions", "Where to look and how alike it must be, as for find(), and how long and how often to look.",
     true},
};
constexpr Param kWatchParams[] = {
    {"image", "TemplateName | ImagePath | CapturedImage",
     "A template name, a picture in the profile folder or an image from vision.capture(), as for find()."},
    {"onAppear", "(event: TaskContext & Found) => void | Promise<void>",
     "What to run when the picture appears; the event says where it is."},
    {"onGone", "(event: TaskContext) => void | Promise<void>", "What to run when the picture goes away.", true},
    {"options", "WatchOptions", "Where to look and how alike it must be, as for find(), how often to look and what "
                                "happens when it comes again while the last task is still running.",
     true},
};
constexpr Failure kWaitFindFailures[] = {
    {"GS-API-015", "the image is not a template name, a png, jpg or bmp path inside the profile folder or an image "
                   "from vision.capture()",
     "Use a name made in the vision workbench, or a relative path such as \"res/chest.png\"."},
    {"GS-API-018", "there is no template or region with that name", "Use a name from res/resources.json."},
    {"GS-API-020", "res/resources.json cannot be read or is not valid", "Open the vision workbench, or fix the file."},
    {"GS-API-016", "the picture cannot be read, is empty, fully transparent or a single colour",
     "Save it again as a png cut from the mirrored picture."},
    {"GS-API-037", "the image from vision.capture() is no longer kept: a script keeps its 8 latest",
     "Keep the ones you need with save(), or capture again."},
    {"GS-API-002", "the region is outside the picture", "Regions are picture ratios."},
    {"GS-API-003", "the time is out of range", "Pass milliseconds in the range given."},
    {"GS-API-017", "the threshold is not from 0 to 1, or max is not from 1 to 64", "Use a ratio and a whole number."},
    {"GS-API-019", "match is not \"color\" or \"grey\"", "Write one of the two."},
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};
constexpr Failure kWatchFailures[] = {
    {"GS-API-008", "the handler is not a function", "Pass a function."},
    {"GS-API-009", "again is not one of ignore, restart, stop, queue or parallel", "Use one of the five rules."},
    {"GS-API-003", "the time is out of range", "Pass milliseconds in the range given."},
    {"GS-API-015", "the image is not a template name, a png, jpg or bmp path inside the profile folder or an image "
                   "from vision.capture()",
     "Use a name made in the vision workbench, or a relative path such as \"res/chest.png\"."},
    {"GS-API-018", "there is no template or region with that name", "Use a name from res/resources.json."},
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};
constexpr Param kPickColorParams[] = {
    {"at", "PicturePoint", "The pixel to read, as picture ratios."},
    {"options", "PixelOptions", "How long to wait for a picture.", true},
};
constexpr Param kMatchColorParams[] = {
    {"at", "PicturePoint", "The pixel to compare, as picture ratios."},
    {"color", "ColorName | HexColor",
     "A color written \"#RRGGBB\", or the name of a color in res/resources.json, whose tolerance is the default."},
    {"options", "PixelMatchOptions", "How far the pixel may be from the color and how long to wait for a picture.",
     true},
};
constexpr Failure kPickColorFailures[] = {
    {"GS-API-002", "the position is not a point inside the picture", "Pass a point such as point(0.5, 0.5)."},
    {"GS-API-011", "the options are not an object", "Pass a plain object."},
    {"GS-API-003", "the timeout is not more than 0 and at most 10000", "Pass milliseconds."},
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};
constexpr Failure kMatchColorFailures[] = {
    {"GS-API-002", "the position is not a point inside the picture", "Pass a point such as point(0.5, 0.5)."},
    {"GS-API-019", "the color is not a string written \"#RRGGBB\" or a name", "Write it as \"#F5C542\" or use a name."},
    {"GS-API-018", "there is no color with that name", "Use a name from res/resources.json."},
    {"GS-API-020", "res/resources.json cannot be read or is not valid", "Open the vision workbench, or fix the file."},
    {"GS-API-011", "the options are not an object", "Pass a plain object."},
    {"GS-API-003", "the timeout is not more than 0 and at most 10000", "Pass milliseconds."},
    {"GS-API-017", "the tolerance is not from 0 to 1", "Use a ratio."},
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};
constexpr Param kCaptureParams[] = {
    {"region", "Region | RegionName", "The part to take; the whole picture by default.", true},
    {"options", "PixelOptions", "How long to wait for a picture.", true},
};
constexpr Failure kCaptureFailures[] = {
    {"GS-API-002", "the region is outside the picture", "Regions are picture ratios."},
    {"GS-API-018", "there is no region with that name", "Use a name from res/resources.json."},
    {"GS-API-020", "res/resources.json cannot be read or is not valid", "Open the vision workbench, or fix the file."},
    {"GS-API-011", "the options are not an object", "Pass a plain object."},
    {"GS-API-003", "the timeout is not more than 0 and at most 10000", "Pass milliseconds."},
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};
constexpr Param kChangedParams[] = {
    {"region", "Region | RegionName", "The part to look at; the whole picture by default.", true},
    {"options", "ChangeOptions", "How much must change and how long to wait for a picture.", true},
};
constexpr Failure kChangedFailures[] = {
    {"GS-API-002", "the region is outside the picture", "Regions are picture ratios."},
    {"GS-API-018", "there is no region with that name", "Use a name from res/resources.json."},
    {"GS-API-020", "res/resources.json cannot be read or is not valid", "Open the vision workbench, or fix the file."},
    {"GS-API-011", "the options are not an object", "Pass a plain object."},
    {"GS-API-017", "the threshold is not from 0 to 1", "Use a ratio."},
    {"GS-API-003", "the timeout is not more than 0 and at most 10000", "Pass milliseconds."},
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};
constexpr Param kOnChangeParams[] = {
    {"region", "Region | RegionName", "The part to keep looking at."},
    {"handler", "(event: TaskContext) => void | Promise<void>", "What to run each time it changed."},
    {"options", "OnChangeOptions",
     "How much must change, how often to look and what happens when it changes again while the last task is running.",
     true},
};
constexpr Failure kOnChangeFailures[] = {
    {"GS-API-008", "the handler is not a function", "Pass a function."},
    {"GS-API-009", "again is not one of ignore, restart, stop, queue or parallel", "Use one of the five rules."},
    {"GS-API-003", "the time is out of range", "Pass milliseconds in the range given."},
    {"GS-API-002", "the region is outside the picture", "Regions are picture ratios."},
    {"GS-API-018", "there is no region with that name", "Use a name from res/resources.json."},
    {"GS-API-017", "the threshold is not from 0 to 1", "Use a ratio."},
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};
constexpr Param kReadTextParams[] = {
    {"options", "ReadTextOptions", "Where to read, in which language and how long to wait for a picture.", true},
};
constexpr Param kFindTextParams[] = {
    {"text", "string", "The text to look for, up to 200 characters; capitals and spaces do not matter."},
    {"options", "ReadTextOptions", "Where to look, in which language and how long to wait for a picture.", true},
};
constexpr Failure kReadTextFailures[] = {
    {"GS-API-011", "the options are not an object, or the language is not a tag such as \"zh-Hans\"",
     "Pass a plain object and a language tag."},
    {"GS-API-002", "the region is outside the picture", "Regions are picture ratios."},
    {"GS-API-018", "there is no region with that name", "Use a name from res/resources.json."},
    {"GS-API-020", "res/resources.json cannot be read or is not valid", "Open the vision workbench, or fix the file."},
    {"GS-API-003", "the timeout is not more than 0 and at most 10000", "Pass milliseconds."},
    {"GS-API-039", "this computer has no text recognition for the language, or Windows could not read the picture",
     "Add the language in Windows Settings, Time & language, Language & region, or name one the computer has."},
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};
constexpr Failure kFindTextFailures[] = {
    {"GS-API-011",
     "the text is not a string of 1 to 200 characters that are not all spaces, the options are not an object, or the "
     "language is not a tag such as \"zh-Hans\"",
     "Pass the words to find, a plain object and a language tag."},
    {"GS-API-002", "the region is outside the picture", "Regions are picture ratios."},
    {"GS-API-018", "there is no region with that name", "Use a name from res/resources.json."},
    {"GS-API-020", "res/resources.json cannot be read or is not valid", "Open the vision workbench, or fix the file."},
    {"GS-API-003", "the timeout is not more than 0 and at most 10000", "Pass milliseconds."},
    {"GS-API-039", "this computer has no text recognition for the language, or Windows could not read the picture",
     "Add the language in Windows Settings, Time & language, Language & region, or name one the computer has."},
    {"GS-API-001", "no phone is connected to this script", "Start the script on a phone that is mirroring."},
};

constexpr Function kVision[] = {
    {"find", kFindParams, "Promise<Found | null>",
     "Looks for a picture in the next frame, or in the picture already shown when no new frame comes within 0.25 s, "
     "and resolves with the best place; null when it is not there, or when there is no picture before the timeout.",
     "const chest = await vision.find(\"res/chest.png\"); if (chest) await touch.tap(chest.x, chest.y);",
     &findNative, 0, kFindFailures},
    {"findAll", kFindParams, "Promise<Found[]>",
     "Looks for every place a picture appears in the next frame (or the picture already shown), best first, without "
     "overlapping matches.",
     "const coins = await vision.findAll(\"res/coin.png\", { max: 8 });", &findNative, 1, kFindFailures},
    {"findColor", kFindColorParams, "Promise<Found | null>",
     "Looks for a color in the next frame (or the picture already shown) and resolves with its largest group of "
     "pixels: a pixel matches when its red, green and blue each differ from the color by at most tolerance times 255, "
     "and matching pixels that touch, diagonally too, form one group. x and y are the center of the group's bounding "
     "box, width and height its size, and score the share of matching pixels in that box; null when no pixel "
     "matches, or when there is no picture before the timeout.",
     "const banner = await vision.findColor(\"#F5C542\", { tolerance: 0.15 }); if (banner) await touch.tap(banner.x, "
     "banner.y);",
     &findColorNative, 0, kFindColorFailures},
    {"pickColor", kPickColorParams, "Promise<HexColor | null>",
     "Reads the color of one pixel in the next frame, or in the picture already shown when no new frame comes within "
     "0.25 s, and resolves with it written \"#RRGGBB\"; null when there is no picture before the timeout.",
     "const sky = await vision.pickColor(point(0.5, 0.05)); log.info(\"sky\", sky);", &pixelColorNative, 0,
     kPickColorFailures},
    {"matchColor", kMatchColorParams, "Promise<boolean>",
     "Checks one pixel in the next frame (or the picture already shown) against a color: true when its red, green "
     "and blue each differ from the color by at most tolerance times 255; false when they do not, or when there is "
     "no picture before the timeout.",
     "if (await vision.matchColor(point(0.9, 0.1), \"#E53935\")) await touch.tap(0.9, 0.1);", &pixelColorNative, 1,
     kMatchColorFailures},
    {"waitFor", kWaitFindParams, "Promise<Found | null>",
     "Looks for a picture again and again until it appears and resolves with where it is; null when it has not "
     "appeared within timeoutMs.",
     "const start = await vision.waitFor(\"res/start.png\", { timeoutMs: 30000 }); if (start) await touch.tap(start);",
     nullptr, 0, kWaitFindFailures},
    {"waitGone", kWaitFindParams, "Promise<boolean>",
     "Looks for a picture again and again until it is gone; true when it went away, false when it was still there "
     "after timeoutMs.",
     "await vision.waitGone(\"res/loading.png\", { timeoutMs: 60000 });", nullptr, 0, kWaitFindFailures},
    {"findAndTap", kFindParams, "Promise<Found | null>",
     "Looks for a picture once and taps the middle of the best place; resolves with where it was, or null without "
     "tapping when it is not there.",
     "await vision.findAndTap(\"res/close.png\");", nullptr, 0, kFindFailures},
    {"watch", kWatchParams, "Subscription",
     "Keeps looking for a picture, starting onAppear each time it appears and onGone each time it goes away, each run "
     "as its own task; dispose() stops looking.",
     "vision.watch(\"res/revive.png\", (found) => touch.tap(found), () => log.info(\"gone\"));", nullptr, 0,
     kWatchFailures},
    {"capture", kCaptureParams, "Promise<CapturedImage | null>",
     "Takes part of the next frame, or of the picture already shown when no new frame comes within 0.25 s, as an "
     "image: look for it later with find(), show it with ui.image() or keep it with save(). Resolves with null when "
     "there is no picture before the timeout. A script keeps its 8 latest images; older ones can no longer be used.",
     "const before = await vision.capture(res.region(\"Score\"));", nullptr, 0, kCaptureFailures},
    {"changed", kChangedParams, "Promise<boolean>",
     "Looks at part of the next frame (or the picture already shown) and compares it with how it looked the last "
     "time changed() looked at the same region: true when it changed. The first look only remembers it and resolves "
     "with false, as does a look without a picture before the timeout.",
     "if (await vision.changed(res.region(\"Score\"))) log.info(\"the score changed\");", &changedNative, 0,
     kChangedFailures},
    {"onChange", kOnChangeParams, "Subscription",
     "Keeps looking at part of the picture and starts a task each time it changed since the last look; the first "
     "look only remembers it. dispose() stops looking.",
     "vision.onChange(res.region(\"Chat\"), () => ui.notify(\"New message\"));", nullptr, 0, kOnChangeFailures},
    {"readText", kReadTextParams, "Promise<TextResult | null>",
     "Reads the text in the next frame, or in the picture already shown when no new frame comes within 0.25 s, with "
     "the text recognition that comes with Windows, and resolves with each line and where it is; null when there is "
     "no picture before the timeout. Chinese and Japanese characters are joined without spaces, other words with one. "
     "Name the language the game is written in: Chinese recognition reads English words poorly and English "
     "recognition cannot read Chinese. Reading a whole picture can take a few tenths of a second, so give a region "
     "when you can.",
     "const gold = await vision.readText({ region: res.region(\"Gold\") }); if (gold) log.info(gold.text);",
     &readTextNative, 0, kReadTextFailures},
    {"findText", kFindTextParams, "Promise<Found | null>",
     "Reads the text in the next frame (or the picture already shown) and resolves with where a line says it: a line "
     "that says only that comes first, then the first line from the top that contains it, ignoring capitals and "
     "spaces. The place covers the words that say it and score is always 1; null when no line says it, or when there "
     "is no picture before the timeout.",
     "const ok = await vision.findText(\"OK\"); if (ok) await touch.tap(ok);", &readTextNative, 1,
     kFindTextFailures},
};

constexpr Param kPointParams[] = {{"name", "PointName", "The name of a point in res/resources.json."}};
constexpr Param kRegionParams[] = {{"name", "RegionName", "The name of a region in res/resources.json."}};
constexpr Param kPathParams[] = {{"name", "PathName", "The name of a path in res/resources.json."}};
constexpr Failure kResourceFailures[] = {
    {"GS-API-018", "the name is not a string or there is no such resource", "Use a name from res/resources.json."},
    {"GS-API-020", "res/resources.json cannot be read or is not valid", "Open the vision workbench, or fix the file."},
};

constexpr Function kRes[] = {
    {"point", kPointParams, "PicturePoint", "A named point of the profile, as picture ratios.",
     "const entry = res.point(\"Event entry\"); await touch.tap(entry.x, entry.y);", &resourceNative,
     static_cast<int>(ResourceKind::Point), kResourceFailures},
    {"region", kRegionParams, "Region", "A named region of the profile, as picture ratios.",
     "const corner = res.region(\"Bottom right\"); const found = await vision.find(\"res/coin.png\", { region: corner "
     "});",
     &resourceNative, static_cast<int>(ResourceKind::Region), kResourceFailures},
    {"path", kPathParams, "PicturePath",
     "A named path of the profile: its points as picture ratios and how long a swipe along it takes.",
     "const drag = res.path(\"Drag map\"); const end = drag.points[drag.points.length - 1]; await "
     "touch.swipe(drag.points[0].x, drag.points[0].y, end.x, end.y, drag.durationMs);",
     &resourceNative, static_cast<int>(ResourceKind::Path), kResourceFailures},
};

constexpr Param kParamNumber[] = {
    {"value", "number", "The value it starts with."},
    {"options", "ParamOptions", "Its label, and the smallest and largest value.", true},
};
constexpr Param kParamText[] = {
    {"value", "string", "The text it starts with."},
    {"options", "ParamOptions", "Its label.", true},
};
constexpr Param kParamToggle[] = {
    {"value", "boolean", "Whether it starts on."},
    {"options", "ParamOptions", "Its label.", true},
};
constexpr Failure kParamFailures[] = {
    {"GS-API-034", "the value is not of the parameter's kind", "Pass a number, a string or true or false."},
};

constexpr Function kParam[] = {
    {"number", kParamNumber, "TaskParam", "A number setting of a task, shown as a number box on the Tasks page.",
     "static params = { rounds: param.number(10, { min: 1 }) };", nullptr, 0, kParamFailures},
    {"text", kParamText, "TaskParam", "A text setting of a task, shown as a text box on the Tasks page.",
     "static params = { map: param.text(\"Erangel\") };", nullptr, 0, kParamFailures},
    {"toggle", kParamToggle, "TaskParam", "An on or off setting of a task, shown as a switch on the Tasks page.",
     "static params = { claim: param.toggle(true) };", nullptr, 0, kParamFailures},
};

constexpr Param kPropKey[] = {
    {"key", "string", "The key it starts with, such as \"F\" or \"MouseLeft\".", true},
    {"options", "PropOptions", "Its label.", true},
};
constexpr Param kPropPosition[] = {
    {"at", "PicturePoint", "The position it starts with.", true},
    {"options", "PropOptions", "Its label.", true},
};
constexpr Param kPropNumber[] = {
    {"value", "number", "The value it starts with."},
    {"options", "NumberPropOptions", "Its label, and the smallest and largest value.", true},
};
constexpr Param kPropText[] = {
    {"value", "string", "The text it starts with."},
    {"options", "PropOptions", "Its label.", true},
};
constexpr Param kPropToggle[] = {
    {"value", "boolean", "Whether it starts on."},
    {"options", "PropOptions", "Its label.", true},
};
constexpr Param kPropComponent[] = {
    {"of", "string | Interface<unknown> | { readonly prototype: Component } | { readonly component?: Component }",
     "The id of the component it starts with; or a component class, whose type is then the only one the editor lists, "
     "or an interface.",
     true},
    {"options", "PropOptions", "Its label.", true},
};

constexpr Function kProp[] = {
    {"key", kPropKey, "ComponentProp", "A key property: the component gets it as a Key object in props.",
     "static props = { key: prop.key(\"F\") };", nullptr, 0, kParamFailures},
    {"position", kPropPosition, "ComponentProp", "A position property: the component gets it as a point in props.",
     "static props = { at: prop.position(point(0.5, 0.8)) };", nullptr, 0, kParamFailures},
    {"number", kPropNumber, "ComponentProp", "A number property.", "static props = { count: prop.number(3) };", nullptr,
     0, kParamFailures},
    {"text", kPropText, "ComponentProp", "A text property.", "static props = { label: prop.text(\"Go\") };", nullptr, 0,
     kParamFailures},
    {"toggle", kPropToggle, "ComponentProp", "An on or off property.", "static props = { loop: prop.toggle(false) };",
     nullptr, 0, kParamFailures},
    {"component", kPropComponent, "ComponentProp",
     "A property that names another component; the component gets that component object in props, or null when the "
     "named one is not of the class or interface given.",
     "static props = { target: prop.component(\"fire\"), scope: prop.component(Hold) };", nullptr, 0, kParamFailures},
};

constexpr Param kMcpToolParams[] = {
    {"name", "string",
     "The tool's name: a lowercase letter, then up to 31 lowercase letters, digits and underscores. AI clients see it "
     "as ext_<extension id>_<name>, with the dots and hyphens of the id written as underscores."},
    {"spec", "McpToolSpec", "What the tool does, for the AI, and the JSON Schema of its input."},
    {"handler", "(input: any, event: TaskContext) => unknown",
     "Runs as its own task for each call and gives back the tool's result as a JSON value; an error it throws is "
     "passed to the client as the tool's error. A call that is not answered within 60 seconds fails."},
};
constexpr Failure kMcpFailures[] = {
    {"GS-API-074",
     "the logic is not the app script of an extension, the name or the description is not valid, the input schema is "
     "not an object of type object or is larger than 16 KiB, or the extension already offers 32 tools",
     "Call mcp.tool in a script that manifest.json lists with \"run\": \"app\", with a valid name, a description and "
     "an input schema of type object."},
};
constexpr Function kMcp[] = {
    {"tool", kMcpToolParams, "Subscription",
     "Offers a function of this extension to AI clients as a tool of the local MCP connection. The user confirms each "
     "call, as for running a script on a phone. The tool is gone when the subscription is disposed or the logic stops.",
     "mcp.tool(\"greet\", { description: \"Says hello to someone\", input: { type: \"object\", properties: { name: { "
     "type: \"string\" } } } }, (input) => \"Hello \" + input.name);",
     nullptr, 0, kMcpFailures},
};

constexpr Failure kHardwareGrantFailure = {"GS-API-081", "the device has not been allowed",
                                           "Ask for it with hardware.request() first."};
constexpr Failure kHardwareMissingFailure = {"GS-API-082", "the device is not connected or went away",
                                             "Plug it in or turn it on, then open it again."};
constexpr Failure kHardwareFailedFailure = {"GS-API-083",
                                            "Windows could not open, read or write the device, or it is already open",
                                            "The message says why; close the program or logic that holds it."};
constexpr Param kHardwareRequestParams[] = {
    {"options", "HardwareRequest", "The kind of device and the filters that the listed devices must match."},
};
constexpr Failure kHardwareRequestFailures[] = {
    {"GS-API-085", "the kind is not serial, hid or ble, a filter is not valid, or 8 requests are already waiting",
     "Pass { kind } and filters as HardwareFilter describes."},
    kHardwarePermissionFailure,
};
constexpr Failure kHardwareListFailures[] = {kHardwarePermissionFailure};
constexpr Param kHardwareOpenParams[] = {
    {"id", "string", "The id of an allowed device, from hardware.request() or hardware.list()."},
    {"options", "SerialOptions",
     "For a serial port: speed, data bits, parity, stop bits, flow control and the DTR and RTS lines.", true},
};
constexpr Failure kHardwareOpenFailures[] = {
    kHardwareGrantFailure,
    kHardwareMissingFailure,
    kHardwareFailedFailure,
    {"GS-API-085", "the serial options are not possible, or this logic already has 16 devices open",
     "Use the values SerialOptions describes, and close the devices you no longer need."},
    kHardwarePermissionFailure,
};
constexpr Param kHardwareForgetParams[] = {{"id", "string", "The id of an allowed device."}};
constexpr Failure kHardwareForgetFailures[] = {
    {"GS-API-085", "the id is not a string that is not empty", "Pass the id of a device from hardware.list()."},
    kHardwarePermissionFailure,
};

constexpr Function kHardware[] = {
    {"request", kHardwareRequestParams, "Promise<HardwareInfo[]>",
     "Shows the user a list of the connected devices of that kind; the user ticks the ones this logic may use. "
     "Resolves with every ticked device, those allowed before included, or with an empty list when the user cancels. "
     "Bluetooth devices are looked for while the list is open.",
     "const [pad] = await hardware.request({ kind: \"hid\", filters: [{ vendorId: 0x0fd9 }] });", nullptr, 0,
     kHardwareRequestFailures},
    {"list", {}, "HardwareInfo[]",
     "The devices this logic may open, connected or not; present says whether each one is connected now.",
     "for (const device of hardware.list()) log.info(device.name, device.present);", nullptr, 0,
     kHardwareListFailures},
    {"open", kHardwareOpenParams, "Promise<SerialDevice | HidDevice | BleDevice>",
     "Opens an allowed device; a device can be open only once in GameScrcpy. A device opened in a task closes when "
     "the task ends, one opened at the top level when the logic stops.",
     "const port = await hardware.open(board.id, { baudRate: 9600 }); await port.write(\"ping\\n\");", nullptr, 0,
     kHardwareOpenFailures},
    {"forget", kHardwareForgetParams, "Promise<void>",
     "Gives up the permission to use a device, as its remove button on the Extensions page does; the device closes "
     "if it is open.",
     "await hardware.forget(device.id);", nullptr, 0, kHardwareForgetFailures},
};

constexpr Param kAiAskParams[] = {
    {"question", "string", "What to ask, up to 4000 characters."},
    {"options", "AiAskOptions", "A picture of the phone to send along, and how long to wait for the answer.", true},
};
constexpr Failure kAiAskFailures[] = {
    {"GS-API-011",
     "the question is not a string of 1 to 4000 characters that are not all spaces, the options are not an object, or "
     "picture is not true, false, a region or a region name",
     "Pass the question as text and the options as a plain object."},
    {"GS-API-003", "timeoutMs is not from 1000 to 120000", "Pass milliseconds in that range."},
    {"GS-API-002", "the region is outside the picture", "Regions are picture ratios."},
    {"GS-API-018", "there is no region with that name", "Use a name from res/resources.json."},
    {"GS-API-020", "res/resources.json cannot be read or is not valid", "Open the vision workbench, or fix the file."},
    {"GS-API-095", "Let logic call AI is off in Settings, AI, or no AI service is set up there",
     "Set up the service there, test the connection and turn the setting on."},
    {"GS-API-096", "this logic called AI 10 times in the last minute", "Ask less often, or wait a little."},
    {"GS-API-097", "the AI service failed or gave no answer within timeoutMs; the message says why",
     "Check the service in Settings, AI, or try again later."},
    {"GS-API-072", "the logic belongs to an extension whose manifest does not ask for the permission ai",
     "Add \"ai\" to the permissions in the extension's manifest, with a reason."},
    {"GS-API-073", "a picture is asked for by logic that has no phone, such as the app script of an extension",
     "Ask without a picture, or from a session script."},
};

constexpr Function kAi[] = {
    {"ask", kAiAskParams, "Promise<string | null>",
     "Asks the AI service set up in Settings, AI, the one the assistant in the editor uses, and resolves "
     "with the text of its answer; null when it answers without text, when a picture is asked for and the phone has "
     "shown none yet, and in replays, which ask nothing. With picture, the phone's picture or a part of it goes along, "
     "made smaller so its long side is at most 1024 pixels. It works only while Let logic call AI is on there: each "
     "question spends your quota, and each logic may ask at most 10 times a minute. Cancelling the task stops "
     "waiting.",
     "const answer = await ai.ask(\"Is a boss on the screen? Answer yes or no.\", { picture: true }); if "
     "(answer?.toLowerCase().startsWith(\"yes\")) ui.notify(\"A boss is here\");",
     nullptr, 0, kAiAskFailures},
};

constexpr Namespace kNamespaces[] = {
    {"log", "Lines in the script console, with the task that wrote them.", kLog},
    {"time", "The script clock.", kTime},
    {"touch", "Touches on the phone picture. Positions are ratios of the picture: (0, 0) top left, (1, 1) bottom right.",
     kTouch},
    {"device", "The phone this script runs on.", kDevice},
    {"clipboard", "The phone's clipboard, not the computer's.", kClipboard},
    {"mapping", "The controls of the profile in use, by id; the script's output never feeds back into its input.",
     kMapping},
    {"hints", "The key hints drawn on the phone picture.", kHints},
    {"profile", "The profile in use on this phone.", kProfile},
    {"store", "Values this script keeps between runs (up to 1 MiB of JSON).", kStore},
    {"ui", "Messages, questions and drawings in the phone's mirror window.", kUi},
    {"input", "What the user holds and points at, readable only while the phone's mirror window is in front.",
     kInput},
    {"hardware",
     "Serial ports, USB HID devices and Bluetooth LE devices the user allows. Every logic, your own too, can only "
     "open the devices the user ticked; the logic of an extension also needs the permission hardware.",
     kHardware},
    {"window", "The phone's mirror window.", kWindow},
    {"vision", "Finding pictures in the phone's picture: the next frame, or the picture already shown when none comes "
               "within 0.25 s.",
     kVision},
    {"res", "The named points, regions and paths of the profile, kept in res/resources.json by the vision workbench.",
     kRes},
    {"math", "Numbers and positions.", kMath},
    {"list", "Lists of values, changed in place.", kList},
    {"dict", "Dictionaries from keys to values, as JavaScript's Map.", kDict},
    {"set", "Sets of values that each appear once, as JavaScript's Set.", kSet},
    {"text", "Text: joining, filling in, finding, cutting and changing case.", kText},
    {"param", "Settings a task declares in static params; the Tasks page shows them and passes the values to the task.",
     kParam},
    {"prop", "Properties a component made in logic declares in static props.", kProp},
    {"mcp", "Tools that the app script of an extension offers to AI clients over the local MCP connection.", kMcp},
    {"ai",
     "Questions from logic to the AI service set up in Settings, AI; they work only while Let logic call AI "
     "is on there.",
     kAi},
};

constexpr Namespace kAttached[] = {
    {"ui.menu", "The events of the group of items that ui.menu() puts into the right-click menu.", kUiMenu},
};

constexpr Type kTypes[] = {
    {"AbortSignal",
     "{ readonly aborted: boolean; readonly reason: unknown; onabort: (() => void) | null; throwIfAborted(): void; "
     "addEventListener(type: \"abort\", listener: () => void): void; "
     "removeEventListener(type: \"abort\", listener: () => void): void }",
     "Tells a task that it was cancelled: when its key is released, the script stops or the profile changes."},
    {"TaskContext", "{ readonly signal: AbortSignal }",
     "What every exported handler receives when a key, a blueprint or the console starts it."},
    {"TaskParams", "{ readonly [name: string]: any }",
     "The settings of a running task, as chosen on the Tasks page; the task declares them in static params."},
    {"TaskParam",
     "{ readonly kind: \"number\" | \"text\" | \"toggle\"; readonly value: number | string | boolean; "
     "readonly label?: string; readonly min?: number; readonly max?: number }",
     "A task setting made by param.number, param.text or param.toggle."},
    {"ComponentOptions", "{ readonly id?: string; readonly enabled?: boolean; readonly [prop: string]: unknown }",
     "The values for a new component's static props, with an optional id and whether it starts on."},
    {"ComponentProp",
     "{ readonly kind: \"key\" | \"position\" | \"number\" | \"text\" | \"toggle\" | \"component\"; "
     "readonly value: unknown; readonly label?: string }",
     "A property declared in static props with prop.key, prop.position, prop.number, prop.text, prop.toggle or "
     "prop.component."},
    {"PropOptions", "{ label?: string }", "How a component property is labelled."},
    {"McpToolSpec", "{ readonly description: string; readonly input?: { readonly [key: string]: unknown } }",
     "What an MCP tool of an extension does, in a sentence or two for the AI, and the JSON Schema of its input: an "
     "object of type object of at most 16 KiB; without one the tool takes an empty object."},
    {"NumberPropOptions", "{ label?: string; min?: number; max?: number }",
     "How a number property is labelled, and the smallest and largest value its box in the property panel takes."},
    {"ParamOptions", "{ label?: string; min?: number; max?: number }",
     "How a task setting shows on the Tasks page: its label, and for numbers the smallest and largest value."},
    {"AndroidKey",
     "\"back\" | \"home\" | \"appSwitch\" | \"menu\" | \"power\" | \"volumeUp\" | \"volumeDown\" | \"volumeMute\" | "
     "\"enter\" | \"delete\" | \"tab\" | \"space\" | \"escape\" | \"up\" | \"down\" | \"left\" | \"right\" | "
     "\"pageUp\" | \"pageDown\" | \"search\" | \"camera\" | \"mediaPlayPause\" | \"notifications\" | \"settings\"",
     "The Android keys a script can press."},
    {"DeviceInfo",
     "{ readonly model: string; readonly android: number; readonly width: number; readonly height: number; "
     "readonly rotation: 0 | 1 | 2 | 3 }",
     "The phone: model name, Android version, screen size in pixels and rotation in quarter turns."},
    {"ComponentType",
     "\"tap\" | \"hold\" | \"longPress\" | \"turbo\" | \"swipe\" | \"pinch\" | \"wasd\" | \"rightWalk\" | "
     "\"padStick\" | \"camera\" | \"padCamera\" | \"freeLook\" | \"skill\"",
     "The 13 kinds of component."},
    {"MachineStep", "{ readonly state: string; readonly reason: string }",
     "A move to another state, made by to.<state>(reason) and returned from a state of machine()."},
    {"MachineTo<S>", "{ readonly [K in S & string]: (reason?: string) => MachineStep }",
     "What a state of machine() receives: one function per state that makes the move to it, with an optional reason "
     "the editor shows on the transition."},
    {"MachineOptions<S>", "{ name?: string; start?: NoInfer<S & string> }",
     "The name the editor shows for a state machine, and the state it starts in (the first one when left out)."},
    {"Interface<T>", "{ readonly name: string; all(): (Component & T)[] }",
     "An interface made with defineInterface(): its name, and all(), every component of the profile whose class lists it "
     "in static implements, so logic can tell each of them at once."},
    {"BuiltInClass<T, O>",
     "{ new (): Omit<T, keyof O> & O; readonly component?: T; readonly implements?: readonly Interface<unknown>[] }",
     "A built-in component class. A component of yours that extends it is placed as that built-in component and "
     "presses the screen just as it does; the event methods of O that the class writes run each time the event comes, "
     "as well. T is the type its components have in controls."},
    {"ControlInfo",
     "{ readonly id: string; readonly name: string; readonly type: ComponentType; readonly enabled: boolean }",
     "A control of the profile in use."},
    {"VarValue", "boolean | number | string", "What a variable holds."},
    {"StoreValue", "null | boolean | number | string | StoreValue[] | { [key: string]: StoreValue }",
     "A value the store can keep."},
    {"ListenOptions", "{ readonly again?: \"ignore\" | \"restart\" | \"stop\" | \"queue\" | \"parallel\" }",
     "What happens when the event comes again while the last task it started is still running: ignore it "
     "(default), restart (cancel the running task first), stop (only cancel the running task), queue (run after "
     "it) or parallel (run both)."},
    {"KeyListenOptions", "ListenOptions & { readonly consume?: boolean }",
     "The options of a control's key events: again as for other events, and consume. With consume true (the "
     "default), the profile's logic takes the key of a tap, hold, long press, turbo, swipe or multi-press control "
     "it listens to: pressing the key no longer touches the phone, and the control touches only when the logic calls "
     "it, such as with press() or tap(), a few milliseconds later than its own touch would. consume: false keeps the "
     "control's own touch, which comes before the handler runs. Only the profile's own logic takes keys: tasks, "
     "extension scripts and scripts an AI client runs only listen, the event methods a component class overrides "
     "keep the built-in touch, and other controls always keep working."},
    {"Subscription", "{ dispose(): void }",
     "A listener; dispose() stops it. Listeners made inside a task stop when that task ends, those made at module "
     "top level last as long as the script."},
    {"UntilOptions", "{ readonly timeoutMs?: number; readonly retry?: number }",
     "How long until() waits for its condition after each try of the action, in milliseconds (5000 by default, from "
     "1 to 600000), and how many more times it does the action when the condition does not come true in time (0 by "
     "default, at most 10)."},
    {"Action<T>",
     "Promise<T> & { until<R>(condition: () => R | Promise<R>, options?: UntilOptions): "
     "Promise<NonNullable<Awaited<R>>> }",
     "What an action returns: a Promise that resolves when the action is done, with until(), which then checks the "
     "condition once per frame and resolves with its first truthy value, doing the action again, as often as retry "
     "allows, when it does not come true in time. The condition may return a Promise, as vision.find() does. until() "
     "rejects with a TimeoutError (GS-API-030) when the tries run out and with an AbortError when the task is "
     "cancelled; it throws GS-API-008 for a condition that is not a function, GS-API-011 for options that are not an "
     "object, GS-API-003 for a timeoutMs out of range and GS-API-017 for a retry out of range."},
    {"Binding", "{ dispose(): void }",
     "A binding made with bind(); dispose() stops it and puts the property back to its value from before the "
     "binding. Bindings made inside a task stop when that task ends, those made at module top level last as long as "
     "the script."},
    {"ControlEvent", "TaskContext & { readonly control: string }",
     "What a control listener receives: the cancel signal and the id of the control."},
    {"DirectionEvent", "ControlEvent & { readonly direction: number }",
     "What onDirectionChange receives: the new direction in degrees."},
    {"CellEvent", "ControlEvent & { readonly cell: number }",
     "What a direction wheel's onChoose receives: the cell picked, counted from 0 at the first cell going clockwise."},
    {"WheelChoice", "ControlEvent & { readonly cell: number; readonly name: string }",
     "What an action wheel's onChoose receives: the cell run, counted from 0 at the top going clockwise, and its name."},
    {"VarChange", "TaskContext & { readonly id: string; readonly value: VarValue }",
     "What a variable listener receives: the cancel signal, the variable id and its new value."},
    {"KeyEvent", "TaskContext & { readonly key: InputName; readonly heldMs?: number }",
     "What a key listener receives: the key's name, and on release how long it was held."},
    {"Keys", "{ readonly [name: string]: Key }",
     "Keys by name, as in profiles (\"KeyF\", \"Space\", \"Mouse.Left\") or short (F, Esc, MouseLeft, WheelUp, "
     "GamepadA)."},
    {"GlobalChange", "TaskContext & { readonly name: string; readonly value: StoreValue }",
     "What a global listener receives: the name and the new value."},
    {"ForegroundApp", "{ readonly package: string; readonly game: boolean }",
     "The app in front on the phone: its package and whether Android calls it a game."},
    {"InputBox",
     "{ readonly shown: boolean; readonly inputType: number; readonly action: \"unspecified\" | \"none\" | \"go\" | "
     "\"search\" | \"send\" | \"next\" | \"done\" | \"previous\"; readonly multiLine: boolean }",
     "An input box on the phone: whether it is open, its kind as Android's InputType number (1 text, 2 number, 3 "
     "phone, 4 date and time, with the variation and flag bits added), what its Enter key does and whether it takes "
     "several lines. When it closes the other fields are 0, \"unspecified\" and false."},
    {"LaunchOptions", "{ readonly restart?: boolean }", "Whether to stop the app before starting it."},
    {"ProfileInfo", "{ readonly id: string; readonly name: string }", "A profile's id and name."},
    {"Work", "Promise<unknown> | (() => unknown)", "Something to run: a Promise, or a function to start."},
    {"ConfirmOptions", "{ readonly ok?: string; readonly cancel?: string }",
     "The words on the two buttons of a yes or no question (up to 60 characters each)."},
    {"AskOptions", "{ readonly initial?: string }", "The text already in the box when the question opens."},
    {"PicturePoint", "{ readonly x: number; readonly y: number }",
     "A position on the phone picture as ratios: (0, 0) top left, (1, 1) bottom right."},
    {"InputName", "string",
     "A key or mouse button, written as in profiles, such as \"KeyW\", \"ShiftLeft\" or \"Mouse.Right\"."},
    {"DrawColor", "\"accent\" | \"good\" | \"warn\" | \"bad\" | \"white\"",
     "The colors a drawing can use; they follow the program's theme."},
    {"BoxOptions", "{ readonly color?: DrawColor; readonly label?: string; readonly durationMs?: number }",
     "A box's color (accent by default), a label of up to 60 characters and how long it stays, in milliseconds."},
    {"TextOptions", "{ readonly color?: DrawColor; readonly durationMs?: number }",
     "The text's color (accent by default) and how long it stays, in milliseconds."},
    {"MarkOptions", "{ readonly color?: DrawColor; readonly label?: string; readonly durationMs?: number }",
     "A mark's color (accent by default), a label of up to 60 characters shown to the right of the ring and how long "
     "it stays, in milliseconds."},
    {"Drawing", "{ dispose(): void }",
     "Something drawn on the picture; dispose() removes it. At most 64 are shown at a time."},
    {"Region", "{ readonly x: number; readonly y: number; readonly width: number; readonly height: number }",
     "Part of the picture, as ratios: left, top, width and height."},
    {"FindOptions",
     "{ readonly region?: Region | RegionName; readonly threshold?: number; readonly match?: MatchMode; "
     "readonly max?: number; readonly timeoutMs?: number }",
     "Where to look (a region or a region name; the whole picture by default), how alike it must be from 0 to 1 (0.8 "
     "by default), how to compare (grey by default), how many to return for findAll (16 by default, at most 64) and "
     "how long to wait while there is no picture yet (2000 ms by default, at most 10000). A template name brings its "
     "own threshold, match mode and region; options given here win."},
    {"Found",
     "{ readonly x: number; readonly y: number; readonly width: number; readonly height: number; "
     "readonly score: number }",
     "Where a picture was found: x and y are its center and width and height its size, as ratios of the picture; "
     "score is how alike it is, from 0 to 1."},
    {"WaitOptions", "FindOptions & { readonly intervalMs?: number }",
     "Where to look and how alike it must be, as for find(); timeoutMs is how long to keep looking in all (10000 ms by "
     "default, at most 600000) and intervalMs how long to wait between looks (250 ms by default, from 50 to 60000)."},
    {"WatchOptions", "FindOptions & ListenOptions & { readonly intervalMs?: number }",
     "Where to look and how alike it must be, as for find(); intervalMs is how long to wait between looks (500 ms by "
     "default, from 50 to 60000), and again what happens when the picture comes or goes again while the last task it "
     "started is still running."},
    {"MatchMode", "\"color\" | \"grey\"",
     "How a template is compared. \"grey\" compares brightness only (zero-mean normalized cross-correlation). "
     "\"color\" also compares colors: the score is multiplied by 1 - d / 128 (at least 0), where d is the mean "
     "absolute difference of red, green and blue (0 to 255) over the template's opaque pixels, so the same shape in "
     "another color scores low."},
    {"ColorOptions",
     "{ readonly region?: Region | RegionName; readonly tolerance?: number; readonly timeoutMs?: number }",
     "Where to look (the whole picture by default), how far each of red, green and blue may be from the color as a "
     "ratio of 255 (0.1 by default) and how long to wait while there is no picture yet (2000 ms by default, at most "
     "10000). A color name brings its own tolerance and region; options given here win."},
    {"PixelOptions", "{ readonly timeoutMs?: number }",
     "How long to wait while there is no picture yet (2000 ms by default, at most 10000)."},
    {"ChangeOptions", "{ readonly threshold?: number; readonly timeoutMs?: number }",
     "How much must change to count, as the share of the region from 0 to 1 (0.05 by default; a spot counts when its "
     "brightness moves by about a tenth or more), and how long to wait while there is no picture yet (2000 ms by "
     "default, at most 10000)."},
    {"OnChangeOptions", "ListenOptions & { readonly threshold?: number; readonly intervalMs?: number }",
     "How much must change, as for changed(); intervalMs is how long to wait between looks (500 ms by default, from "
     "50 to 60000), and again what happens when it changes again while the last task it started is still running."},
    {"ReadTextOptions",
     "{ readonly region?: Region | RegionName; readonly language?: string; readonly timeoutMs?: number }",
     "Where to read (a region or a region name; the whole picture by default), the language as a tag such as "
     "\"zh-Hans\" or \"en\" (by default the first Windows display language that has text recognition, otherwise the "
     "first one this computer has; findText looks for text of English letters, digits and signs with English "
     "recognition when the computer has it) and how long to wait while there is no picture yet (2000 ms by default, "
     "at most 10000)."},
    {"TextResult", "{ readonly text: string; readonly lines: readonly TextLine[]; readonly language: string }",
     "Text read from the picture: text is every line joined with line breaks, lines each line with its place from "
     "top to bottom, and language the tag of the language it was read in, such as \"zh-Hans-CN\"."},
    {"TextLine",
     "{ readonly text: string; readonly x: number; readonly y: number; readonly width: number; readonly height: number "
     "}",
     "One line of text: x and y are its center and width and height its size, as ratios of the picture."},
    {"PixelMatchOptions", "{ readonly tolerance?: number; readonly timeoutMs?: number }",
     "How far each of red, green and blue may be from the color as a ratio of 255 (0.1 by default; a color name "
     "brings its own) and how long to wait while there is no picture yet (2000 ms by default, at most 10000)."},
    {"PicturePath", "{ readonly points: readonly PicturePoint[]; readonly durationMs: number }",
     "A path on the picture: its points as ratios and how long a swipe along it takes, in milliseconds."},
    {"ImagePath",
     "`${string}.png` | `${string}.jpg` | `${string}.jpeg` | `${string}.bmp` | `${string}.PNG` | `${string}.JPG` | "
     "`${string}.JPEG` | `${string}.BMP`",
     "A picture in the profile folder, written as a relative path such as \"res/chest.png\"."},
    {"HexColor", "`#${string}`", "A color written \"#RRGGBB\", such as \"#F5C542\"."},
    {"TemplateName", "keyof TemplateNames", "The name of a template in res/resources.json."},
    {"RegionName", "keyof RegionNames", "The name of a region in res/resources.json."},
    {"PointName", "keyof PointNames", "The name of a point in res/resources.json."},
    {"PathName", "keyof PathNames", "The name of a path in res/resources.json."},
    {"ColorName", "keyof ColorNames", "The name of a color in res/resources.json."},
    {"UiEventType", "\"click\" | \"change\" | \"submit\" | \"select\" | \"open\" | \"close\"",
     "What the user does in a panel."},
    {"UiEvent", "TaskContext & { readonly id: string; readonly value: unknown }",
     "What a panel or menu listener receives: the id of the node or item, and its new value or null."},
    {"UiTone", "\"normal\" | \"muted\" | \"accent\" | \"good\" | \"warn\" | \"bad\"",
     "The colors of text and icons in a panel; they follow the program's theme."},
    {"UiColor", "string",
     "A color in a chart or canvas: \"#RRGGBB\", or the name of a theme color such as \"accent\", \"textPrimary\" or "
     "\"success\"."},
    {"UiNode",
     "UiColumn | UiRow | UiGroup | UiText | UiButton | UiField | UiNumber | UiToggle | UiChoice | UiSlider | UiList | "
     "UiTable | UiChart | UiCanvas | UiImage | UiProgress | UiSeparator",
     "A node of a panel tree. A tree is at most 256 KiB as JSON, 2000 nodes and 16 levels deep."},
    {"UiColumn", "{ readonly type: \"column\"; readonly id?: string; readonly children?: readonly UiNode[] }",
     "Nodes one under another."},
    {"UiRow", "{ readonly type: \"row\"; readonly id?: string; readonly children?: readonly UiNode[] }",
     "Nodes side by side, going on in the next line when the panel is too narrow."},
    {"UiGroup",
     "{ readonly type: \"group\"; readonly id?: string; readonly title?: string; readonly children?: readonly "
     "UiNode[] }",
     "Nodes in a card with a title."},
    {"UiText",
     "{ readonly type: \"text\"; readonly id?: string; readonly text: string; readonly format?: \"plain\" | "
     "\"markdown\"; readonly style?: \"body\" | \"heading\" | \"caption\"; readonly tone?: UiTone }",
     "Text of up to 4000 characters. With markdown it shows **bold**, *italic*, `code` and lines that start with - or "
     "1.; it never opens links or shows pictures."},
    {"UiButton",
     "{ readonly type: \"button\"; readonly id: string; readonly title: string; readonly icon?: string; readonly "
     "style?: \"normal\" | \"primary\" | \"danger\"; readonly enabled?: boolean }",
     "A button; a click sends click. icon is the name of a GameScrcpy icon."},
    {"UiField",
     "{ readonly type: \"field\"; readonly id: string; readonly title?: string; readonly value?: string; readonly "
     "placeholder?: string; readonly enabled?: boolean }",
     "A text box; leaving it sends change with the text, and Enter sends submit as well. The phone's keys pause while "
     "it has the keyboard."},
    {"UiNumber",
     "{ readonly type: \"number\"; readonly id: string; readonly title?: string; readonly value?: number; readonly "
     "min?: number; readonly max?: number; readonly step?: number; readonly unit?: string; readonly enabled?: boolean }",
     "A number box; a new number sends change."},
    {"UiToggle",
     "{ readonly type: \"toggle\"; readonly id: string; readonly title: string; readonly value?: boolean; readonly "
     "enabled?: boolean }",
     "A switch; flipping it sends change with true or false."},
    {"UiChoice",
     "{ readonly type: \"choice\"; readonly id: string; readonly title?: string; readonly options: readonly UiOption[]; "
     "readonly value?: string; readonly enabled?: boolean }",
     "Up to 64 options to pick one from; picking sends change with the option's id."},
    {"UiOption", "string | { readonly id: string; readonly title: string }",
     "An option of a choice: text that is both its id and its title, or an id and a title."},
    {"UiSlider",
     "{ readonly type: \"slider\"; readonly id: string; readonly title?: string; readonly value?: number; readonly "
     "min?: number; readonly max?: number; readonly step?: number; readonly enabled?: boolean }",
     "A slider from min (0 if left out) to max (1 if left out); letting it go sends change with the value."},
    {"UiList",
     "{ readonly type: \"list\"; readonly id: string; readonly items: readonly UiListItem[]; readonly selected?: "
     "string; readonly selectable?: boolean }",
     "A list of up to 500 items; clicking one sends select with its id, unless selectable is false."},
    {"UiListItem", "{ readonly id: string; readonly title: string; readonly subtitle?: string; readonly icon?: string }",
     "An item of a list."},
    {"UiTable",
     "{ readonly type: \"table\"; readonly id?: string; readonly columns: readonly string[]; readonly rows?: readonly "
     "UiTableRow[] }",
     "A table of up to 16 columns and 200 rows."},
    {"UiTableRow", "readonly UiCell[]", "A row of a table, one cell for each column."},
    {"UiCell", "string | number | boolean | null", "A cell of a table."},
    {"UiChart",
     "{ readonly type: \"chart\"; readonly id?: string; readonly title?: string; readonly kind?: \"line\" | \"bar\"; "
     "readonly series: readonly UiSeries[]; readonly height?: number; readonly min?: number; readonly max?: number }",
     "A line or bar chart of up to 4 series; height is from 60 to 480, and min and max fix the scale."},
    {"UiSeries", "{ readonly title?: string; readonly color?: UiColor; readonly points: readonly number[] }",
     "A series of a chart: up to 500 values from left to right."},
    {"UiCanvas",
     "{ readonly type: \"canvas\"; readonly id?: string; readonly title?: string; readonly width: number; readonly "
     "height: number; readonly shapes?: readonly UiShape[] }",
     "A drawing of up to 500 shapes. width and height, from 1 to 2000, are in the panel's units; a wider drawing is "
     "made smaller to fit."},
    {"UiShape",
     "{ readonly kind: \"rect\" | \"circle\" | \"line\" | \"polyline\" | \"text\"; readonly x?: number; readonly y?: "
     "number; readonly width?: number; readonly height?: number; readonly radius?: number; readonly x1?: number; "
     "readonly y1?: number; readonly x2?: number; readonly y2?: number; readonly points?: readonly number[]; readonly "
     "closed?: boolean; readonly text?: string; readonly size?: \"body\" | \"caption\" | \"heading\"; readonly color?: "
     "UiColor; readonly fill?: UiColor; readonly stroke?: UiColor; readonly strokeWidth?: number }",
     "A shape of a canvas: rect (x, y, width, height, radius), circle (x, y, radius), line (x1, y1, x2, y2), polyline "
     "(points as x, y, x, y… and closed) or text (x, y, text, size, color); fill, stroke and strokeWidth paint it."},
    {"UiImage",
     "{ readonly type: \"image\"; readonly id?: string; readonly path: string; readonly title?: string; readonly "
     "width?: number; readonly height?: number }",
     "A PNG or SVG picture of up to 2 MiB in the extension's folder, written as a relative path; nothing outside the "
     "folder is shown."},
    {"UiProgress", "{ readonly type: \"progress\"; readonly id?: string; readonly title?: string; readonly value?: number "
                   "| null }",
     "A progress bar from 0 to 1; while value is null or left out it moves on its own."},
    {"UiSeparator", "{ readonly type: \"separator\"; readonly id?: string }", "A thin line between nodes."},
    {"UiStatusNode", "UiStatusRow | UiStatusPart", "A status widget: a row or a single part, at most 64 nodes."},
    {"UiStatusRow", "{ readonly type: \"row\"; readonly id?: string; readonly children?: readonly UiStatusPart[] }",
     "Parts of a status widget side by side."},
    {"UiStatusPart", "UiText | UiStatusIcon | UiProgress",
     "A part of a status widget; its text is at most 100 characters."},
    {"UiStatusIcon", "{ readonly type: \"icon\"; readonly id?: string; readonly name: string; readonly tone?: UiTone }",
     "A GameScrcpy icon by its name."},
    {"UiMenuItem",
     "{ readonly id: string; readonly title: string; readonly icon?: string; readonly checked?: boolean; readonly "
     "enabled?: boolean }",
     "An item of the right-click menu; with checked it shows a check mark, and picking it sends the opposite."},
    {"HardwareKind", "\"serial\" | \"hid\" | \"ble\"",
     "The kinds of device logic can use: serial ports, USB HID devices and Bluetooth LE devices."},
    {"HardwareFilter",
     "{ readonly vendorId?: number; readonly productId?: number; readonly usagePage?: number; readonly usage?: number; "
     "readonly services?: readonly string[]; readonly namePrefix?: string }",
     "What a listed device must have: USB vendor and product ids and HID usage page and usage (0 to 65535), one of "
     "the Bluetooth service UUIDs, and how its name starts (up to 64 characters). A device is listed when it matches "
     "any one filter."},
    {"HardwareRequest", "{ readonly kind: HardwareKind; readonly filters?: readonly HardwareFilter[] }",
     "Which devices hardware.request() lists: one kind and up to 16 filters."},
    {"HardwareInfo",
     "{ readonly id: string; readonly kind: HardwareKind; readonly name: string; readonly present?: boolean }",
     "An allowed device: the id to open it with, its kind and name and, in hardware.list(), whether it is connected "
     "now."},
    {"HardwareBytes", "Uint8Array | ArrayBuffer | ArrayBufferView | readonly number[]",
     "Bytes to send: a Uint8Array, another typed array or a list of numbers from 0 to 255."},
    {"SerialOptions",
     "{ readonly baudRate?: number; readonly dataBits?: 5 | 6 | 7 | 8; readonly parity?: \"none\" | \"even\" | "
     "\"odd\" | \"mark\" | \"space\"; readonly stopBits?: 1 | 1.5 | 2; readonly flowControl?: \"none\" | "
     "\"hardware\" | \"software\"; readonly dtr?: boolean; readonly rts?: boolean }",
     "How a serial port talks: 115200 baud (50 to 4000000), 8 data bits, no parity, 1 stop bit and no flow control "
     "unless given, with the DTR and RTS lines on."},
    {"SerialSignals", "{ readonly dtr?: boolean; readonly rts?: boolean }",
     "The DTR and RTS lines of a serial port to turn on or off; the lines left out stay as they are."},
    {"BleService", "{ readonly uuid: string; readonly characteristics: readonly BleCharacteristic[] }",
     "A service of a Bluetooth LE device and its characteristics."},
    {"BleCharacteristic", "{ readonly uuid: string; readonly properties: readonly string[] }",
     "A characteristic and what it allows: read, write, writeWithoutResponse, notify and indicate."},
    {"BleWriteOptions", "{ readonly response?: boolean }",
     "Whether a write waits for the device to confirm it (true unless given)."},
    {"DeviceDataEvent", "TaskContext & { readonly data: Uint8Array }",
     "What onData receives: the bytes that came in."},
    {"DeviceReportEvent", "TaskContext & { readonly report: number; readonly data: Uint8Array }",
     "What onReport receives: the report id and the bytes after it."},
    {"DeviceNotifyEvent",
     "TaskContext & { readonly service: string; readonly characteristic: string; readonly data: Uint8Array }",
     "What a subscription receives: the service and characteristic UUIDs, written in full, and the new value."},
    {"DeviceCloseEvent",
     "TaskContext & { readonly reason: \"removed\" | \"failed\" | \"forgotten\"; readonly message: string }",
     "Why a device closed by itself: it went away, Windows reported an error, or the permission to use it was taken "
     "back."},
    {"AiAskOptions", "{ readonly picture?: boolean | Region | RegionName; readonly timeoutMs?: number }",
     "What ai.ask() sends along and how long it waits: picture true for the whole phone picture, a region or the name "
     "of one for that part of it, false or nothing for no picture; timeoutMs from 1000 to 120000 milliseconds, 60000 by "
     "default."},
};

constexpr std::string_view kTouchStartDoc = "Starts a task each time it puts a finger on the screen.";
constexpr std::string_view kTouchEndDoc = "Starts a task each time it lifts its last finger.";

constexpr Member kComponentMembers[] = {
    {"id", "readonly id: string", "The control id from the profile; stays the same when the control is renamed.",
     "log.info(controls.jump.id);"},
    {"name", "readonly name: string", "The name shown in the editor.", "log.info(controls.jump.name);"},
    {"position", "readonly position: PicturePoint | null",
     "Where the component sits on the picture, as ratios, for the phone's picture when the logic started; it follows "
     "moveTo(). null for a component made in logic with new.",
     "await touch.tap(controls.jump.position);"},
    {"type", "readonly type: ComponentType", "The kind of component, such as \"tap\", \"wasd\" or \"camera\".",
     "if (controls.jump.type === \"tap\") log.info(\"tap\");"},
    {"props", "readonly props: Readonly<Record<string, any>>",
     "The component's settings: from profile.json, or for a component made in logic its static props with the values "
     "given to it (keys become Key objects, positions points and component ids components).",
     "log.info(controls.fire.props.rateHz);"},
    {"key", "readonly key: InputName | null", "Its main key, such as \"KeyF\" or \"Mouse.Right\"; null when it has none.",
     "log.info(controls.fire.key);"},
    {"inputs", "readonly inputs: Readonly<Record<string, InputName>>",
     "Every key of the component by slot, such as { up: \"KeyW\" }.", "log.info(controls.move.inputs.up);"},
    {"hint", "readonly hint: Hint", "Its key hint on the picture.", "controls.fire.hint.hide();"},
    {"enabled", "enabled: boolean",
     "Whether the control reacts to its keys. Setting it lasts until the script stops or the profile changes, and "
     "ends a binding made with bind().",
     "controls.fire.enabled = false;"},
    {"enable", "enable(): void", "Turns the component on (the same as enabled = true).", "controls.fire.enable();"},
    {"disable", "disable(): void", "Turns the component off.", "controls.fire.disable();"},
    {"toggle", "toggle(): void", "Turns the component on or off.", "controls.fire.toggle();"},
    {"active", "readonly active: boolean", "Whether its key is held down now.", "if (controls.aim.active) log.info(\"aiming\");"},
    {"pressedAt", "readonly pressedAt: number | null", "When its key went down (time.now()), or null when it is up.",
     "log.info(controls.aim.pressedAt);"},
    {"heldMs", "readonly heldMs: number", "How long its key has been held, in milliseconds; 0 when it is up.",
     "if (controls.aim.heldMs > 1000) log.info(\"long\");"},
    {"touching", "readonly touching: boolean", "Whether it has a finger on the phone's screen now.",
     "if (controls.fire.touching) log.info(\"firing\");"},
    {"trigger", "trigger(): Action<void>",
     "Presses and releases the control with its own behavior; the script's own output never triggers scripts.",
     "await controls.jump.trigger();"},
    {"hold", "hold(durationMs: number): Action<void>",
     "Holds the control down for a while, then releases it; released early if the task is cancelled. Throws "
     "GS-API-003 when the duration is not more than 0 and at most 60000.",
     "await controls.sprint.hold(1500);"},
    {"press", "press(): void", "Puts its finger down and keeps it there until release().", "controls.fire.press();"},
    {"release", "release(): void", "Lifts its finger.", "controls.fire.release();"},
    {"reset", "reset(): void",
     "Lifts all its fingers and puts it back at rest: a stick back to the middle, a view back to its anchor.",
     "controls.move.reset();"},
    {"moveTo", "moveTo(x: number | PicturePoint, y?: number): void",
     "Moves the component and its key hint to another place on the picture until the logic stops or the profile "
     "changes; a finger already down stays where it is, and profile.save() can keep the new place. Throws GS-API-002 "
     "for a place outside the picture and GS-API-036 for a component made in logic with new.",
     "controls.fire.moveTo(0.85, 0.7);"},
    {"moveBy", "moveBy(dx: number, dy: number): void", "Moves the component by picture ratios, as moveTo() does.",
     "controls.fire.moveBy(-0.02, 0);"},
    {"resetPosition", "resetPosition(): void", "Puts the component back where the profile has it.",
     "controls.fire.resetPosition();"},
    {"resetProps", "resetProps(): void",
     "Puts back everything the logic changed on this component: its place, rate, duration, radius and sensitivity.",
     "controls.fire.resetProps();"},
    {"bind", "bind(property: \"enabled\", compute: () => unknown): Binding",
     "Keeps a property computed from states: compute runs at once and again whenever a state it read last time "
     "changes, also when the script changes it. Only \"enabled\" can be bound. Disposing the binding, or the end of "
     "the task that made it, puts the property back to its value from before the binding; binding it again takes "
     "over without putting it back, and setting it directly, or with mapping.setEnabled, ends the binding. Throws "
     "GS-API-031 for another "
     "property, GS-API-008 for a compute that is not a function and GS-API-032 when compute returns a Promise. "
     "Later, a compute that throws is reported and keeps the last value, and one that returns a Promise is reported "
     "with GS-API-032 and ends the binding as dispose() does.",
     "controls.fire.bind(\"enabled\", () => !vars.aiming.value);"},
    {"onPress", "onPress(handler: (event: ControlEvent) => void | Promise<void>, options?: KeyListenOptions): Subscription",
     "Starts a task each time the control's key goes down. In the profile's logic it takes the key of a tap, hold, "
     "long press, turbo, swipe or multi-press control unless consume is false (KeyListenOptions). Throws GS-API-008 "
     "for a handler that is not a function, GS-API-009 for an unknown again rule and GS-API-011 for a consume that "
     "is not true or false.",
     "controls.jump.onPress(async () => { await controls.crouch.trigger(); });"},
    {"onRelease",
     "onRelease(handler: (event: ControlEvent) => void | Promise<void>, options?: KeyListenOptions): Subscription",
     "Starts a task each time the control's key comes up. It takes the key as onPress does.",
     "controls.aim.onRelease(() => log.info(\"aim off\"));"},
    {"whileHeld",
     "whileHeld(handler: (event: ControlEvent) => void | Promise<void>, options?: KeyListenOptions): Subscription",
     "Starts a task when the control's key goes down and cancels it when the key comes up. It takes the key as "
     "onPress does.",
     "controls.fire.whileHeld(async ({ signal }) => { while (!signal.aborted) await controls.shoot.trigger(); });"},
    {"onEnabled",
     "onEnabled(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time it is turned on.", "controls.fire.onEnabled(() => log.info(\"on\"));"},
    {"onDisabled",
     "onDisabled(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time it is turned off.", "controls.fire.onDisabled(() => log.info(\"off\"));"},
    {"onTouchStart",
     "onTouchStart(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     kTouchStartDoc, "controls.fire.onTouchStart(() => log.info(\"down\"));"},
    {"onTouchEnd",
     "onTouchEnd(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     kTouchEndDoc, "controls.fire.onTouchEnd(() => log.info(\"up\"));"},
    {"setup", "setup(): void",
     "For a component made in logic: runs once after it is created; set up its listeners here. Create components at the "
     "top level of the logic so these listeners last.",
     "setup() { this.props.key.onPress(() => this.finger.down(this.props.at)); }"},
    {"event", "event(): ComponentEvent",
     "Makes an event the component can emit and others can listen to, kept in a field: pressed = this.event().",
     "pressed = this.event();"},
    {"child", "child<T extends Component>(kind: { readonly component?: T }): T",
     "In a field of a component class: one of its child components, a built-in component the editor places with it and "
     "moves with it, such as cancel = this.child(Tap) or skills = [this.child(Skill), this.child(Skill)]. The calls "
     "take the children in the order the class declares them; null when the profile has no such child. Throws "
     "GS-API-008 when kind is not a class.",
     "cancel = this.child(Tap);"},
    {"finger", "readonly finger: ComponentFinger",
     "For a component made in logic: its own finger, which stays down until up() or until the logic stops.",
     "this.finger.down(this.props.at);"},
};

constexpr Member kHintMembers[] = {
    {"show", "show(): void", "Shows this hint again after hide().", "controls.fire.hint.show();"},
    {"hide", "hide(): void", "Hides this hint on the picture; the component still works.", "controls.fire.hint.hide();"},
    {"setText", "setText(text: string): void", "Writes other words in the hint, up to 16 characters.",
     "controls.fire.hint.setText(\"Auto\");"},
    {"moveTo", "moveTo(x: number | PicturePoint, y?: number): void",
     "Shows the hint at another place on the picture; the component itself still touches where the profile has it.",
     "controls.fire.hint.moveTo(0.5, 0.9);"},
    {"flash", "flash(): void", "Makes the hint flash twice to draw the eye to it; nothing else changes.",
     "controls.fire.hint.flash();"},
    {"reset", "reset(): void", "Puts the hint back as the profile has it.", "controls.fire.hint.reset();"},
};

constexpr Member kTapMembers[] = {
    {"tapMs", "readonly tapMs: number", "How long each tap holds the finger down, in milliseconds; 0 means two frames.",
     "log.info(controls.jump.tapMs);"},
    {"tap", "tap(): Action<void>", "Taps once, holding the finger down for tapMs (60 milliseconds when tapMs is 0).",
     "await controls.jump.tap();"},
    {"tapTimes", "tapTimes(count: number, gapMs?: number): Action<void>",
     "Taps count times (1 to 100), gapMs apart (100 by default), each as long as tap().",
     "await controls.jump.tapTimes(3, 80);"},
    {"setDuration", "setDuration(durationMs: number): void",
     "Changes how long each tap holds the finger down (0 to 1000 milliseconds; 0 means two frames) until the logic "
     "stops or the profile changes. Throws GS-API-017 outside that range.",
     "controls.jump.setDuration(60);"},
    {"resetDuration", "resetDuration(): void", "Puts back the tap length from the profile.",
     "controls.jump.resetDuration();"},
};

constexpr Member kHoldMembers[] = {
    {"holding", "readonly holding: boolean", "Whether it is pressing the screen now.",
     "if (controls.scope.holding) controls.fire.press();"},
    {"holdFor", "holdFor(durationMs: number): Action<void>", "Presses for a while, then lets go.",
     "await controls.scope.holdFor(800);"},
    {"toggleHold", "toggleHold(): void", "Presses if it is up, lets go if it is pressing.", "controls.scope.toggleHold();"},
    {"onPressed", "onPressed(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     kTouchStartDoc, "controls.scope.onPressed(() => log.info(\"scoped\"));"},
    {"onReleased",
     "onReleased(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     kTouchEndDoc, "controls.scope.onReleased(() => log.info(\"unscoped\"));"},
};

constexpr Member kLongPressMembers[] = {
    {"holdMs", "readonly holdMs: number", "How long a long press lasts, in milliseconds.",
     "log.info(controls.bag.holdMs);"},
    {"holding", "readonly holding: boolean", "Whether it is pressing the screen now.", "log.info(controls.bag.holding);"},
    {"longPress", "longPress(durationMs?: number): Action<void>", "Long-presses for holdMs, or for durationMs when given.",
     "await controls.bag.longPress();"},
    {"setDuration", "setDuration(durationMs: number): void",
     "Changes how long a long press lasts (100 to 10000 milliseconds) until the logic stops or the profile changes. "
     "Throws GS-API-017 outside that range.",
     "controls.bag.setDuration(1500);"},
    {"resetDuration", "resetDuration(): void", "Puts back the long press length from the profile.",
     "controls.bag.resetDuration();"},
    {"onStart", "onStart(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     kTouchStartDoc, "controls.bag.onStart(() => log.info(\"start\"));"},
    {"onEnd", "onEnd(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     kTouchEndDoc, "controls.bag.onEnd(() => log.info(\"end\"));"},
};

constexpr Member kTurboMembers[] = {
    {"rateHz", "readonly rateHz: number", "Presses a second: from the profile, or the rate setRate() gave.",
     "log.info(controls.burst.rateHz);"},
    {"tapMs", "readonly tapMs: number", "How long each tap holds the finger down, in milliseconds; 0 means two frames.",
     "log.info(controls.burst.tapMs);"},
    {"firing", "readonly firing: boolean", "Whether start() is pressing again and again now.",
     "if (!controls.burst.firing) controls.burst.start();"},
    {"start", "start(rateHz?: number): void",
     "Presses again and again until stop(), at rateHz or the given rate (1 to 30 a second).",
     "controls.burst.start(15);"},
    {"stop", "stop(): void", "Stops start().", "controls.burst.stop();"},
    {"burst", "burst(count: number): Action<void>", "Presses count times (1 to 100) at its rate.",
     "await controls.burst.burst(5);"},
    {"setRate", "setRate(rateHz: number): void",
     "Changes the presses a second (1 to 30) for its key, start() and burst(), until the logic stops or the profile "
     "changes. Throws GS-API-017 outside that range.",
     "controls.burst.setRate(12);"},
    {"resetRate", "resetRate(): void", "Puts back the rate from the profile.", "controls.burst.resetRate();"},
};

constexpr Member kSwipeMembers[] = {
    {"durationMs", "readonly durationMs: number", "How long the swipe takes, in milliseconds.",
     "log.info(controls.slide.durationMs);"},
    {"swiping", "readonly swiping: boolean", "Whether it is swiping now.", "log.info(controls.slide.swiping);"},
    {"swipe", "swipe(): Action<void>", "Swipes along its path once.", "await controls.slide.swipe();"},
    {"setDuration", "setDuration(durationMs: number): void",
     "Changes how long the swipe takes (10 to 10000 milliseconds) until the logic stops or the profile changes. "
     "Throws GS-API-017 outside that range.",
     "controls.slide.setDuration(350);"},
    {"resetDuration", "resetDuration(): void", "Puts back the swipe length from the profile.",
     "controls.slide.resetDuration();"},
    {"onStart", "onStart(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     kTouchStartDoc, "controls.slide.onStart(() => log.info(\"start\"));"},
    {"onEnd", "onEnd(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     kTouchEndDoc, "controls.slide.onEnd(() => log.info(\"end\"));"},
};

constexpr Member kPinchMembers[] = {
    {"pinching", "readonly pinching: boolean", "Whether it is pinching now.", "log.info(controls.zoom.pinching);"},
    {"zoomIn", "zoomIn(): void", "Spreads two fingers apart once.", "controls.zoom.zoomIn();"},
    {"zoomOut", "zoomOut(): void", "Pinches two fingers together once.", "controls.zoom.zoomOut();"},
};

constexpr Member kStickMembers[] = {
    {"radius", "readonly radius: number", "The stick's radius, as a ratio of the picture height.",
     "log.info(controls.move.radius);"},
    {"moving", "readonly moving: boolean", "Whether the stick is pushed now.", "if (controls.move.moving) log.info(\"moving\");"},
    {"direction", "readonly direction: number | null",
     "Where the stick points in degrees (0 up, 90 right, 180 down, 270 left); null when it is in the middle.",
     "log.info(controls.move.direction);"},
    {"amount", "readonly amount: number", "How far the stick is pushed, from 0 to 1.", "log.info(controls.move.amount);"},
    {"push", "push(direction: number, amount?: number): void",
     "Pushes the stick toward a direction in degrees (0 up, 90 right, 180 down, 270 left), amount from 0 to 1 (1 by "
     "default), until release().",
     "controls.move.push(90);"},
    {"release", "release(): void", "Lets the stick go back to the middle after push().", "controls.move.release();"},
    {"walkFor", "walkFor(direction: number, durationMs: number, amount?: number): Action<void>",
     "Pushes the stick toward a direction in degrees (0 up, 90 right, 180 down, 270 left) for a while, then lets it "
     "go.",
     "await controls.move.walkFor(0, 1500);"},
    {"setRadius", "setRadius(radius: number): void",
     "Changes the stick's radius (0.01 to 1, a ratio of the picture height) until the logic stops or the profile "
     "changes. Throws GS-API-017 outside that range.",
     "controls.move.setRadius(0.15);"},
    {"resetRadius", "resetRadius(): void", "Puts back the radius from the profile.", "controls.move.resetRadius();"},
    {"onMoveStart",
     "onMoveStart(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time the stick starts moving.", "controls.move.onMoveStart(() => log.info(\"go\"));"},
    {"onStop", "onStop(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time the stick comes back to the middle.", "controls.move.onStop(() => log.info(\"stop\"));"},
    {"onDirectionChange",
     "onDirectionChange(handler: (event: DirectionEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time the stick points another way.",
     "controls.move.onDirectionChange(({ direction }) => log.info(direction));"},
};

constexpr Member kWasdMembers[] = {
    {"walkScale", "readonly walkScale: number", "How far the walk key pushes the stick, from the profile.",
     "log.info(controls.move.walkScale);"},
    {"sprintScale", "readonly sprintScale: number", "How far the sprint key pushes the stick, from the profile.",
     "log.info(controls.move.sprintScale);"},
};

constexpr Member kRightWalkMembers[] = {
    {"walking", "readonly walking: boolean", "Whether it is walking now.", "log.info(controls.walk.walking);"},
    {"walkMsPerUnit", "readonly walkMsPerUnit: number",
     "How long it walks to cross the picture's height, in milliseconds.", "log.info(controls.walk.walkMsPerUnit);"},
    {"stopOnRelease", "readonly stopOnRelease: boolean", "Whether letting go of its key stops the walk.",
     "log.info(controls.walk.stopOnRelease);"},
    {"walkTo", "walkTo(x: number | PicturePoint, y?: number): void",
     "Walks toward a position on the picture, as a click there would.", "controls.walk.walkTo(0.7, 0.4);"},
    {"stop", "stop(): void", "Stops walking.", "controls.walk.stop();"},
    {"setRadius", "setRadius(radius: number): void",
     "Changes the radius of its stick (0.01 to 1, a ratio of the picture height) until the logic stops or the profile "
     "changes. Throws GS-API-017 outside that range.",
     "controls.walk.setRadius(0.15);"},
    {"resetRadius", "resetRadius(): void", "Puts back the radius from the profile.", "controls.walk.resetRadius();"},
    {"onWalkStart",
     "onWalkStart(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time it starts walking.", "controls.walk.onWalkStart(() => log.info(\"walk\"));"},
    {"onStop", "onStop(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time it stops walking.", "controls.walk.onStop(() => log.info(\"arrived\"));"},
};

constexpr Member kLookMembers[] = {
    {"turning", "readonly turning: boolean", "Whether its finger is turning the view now.",
     "log.info(controls.view.turning);"},
    {"recenter", "recenter(): void", "Lifts its finger so the next turn starts from its anchor again.",
     "controls.view.recenter();"},
    {"turn", "turn(dx: number, dy: number): void", "Turns the view as if the mouse moved dx and dy counts.",
     "controls.view.turn(120, 0);"},
    {"setSensitivity", "setSensitivity(x: number, y?: number): void",
     "Changes how far the view turns for each mouse or stick count (0.01 to 100; y is x when left out) until the "
     "logic stops or the profile changes. Throws GS-API-017 outside that range.",
     "controls.view.setSensitivity(1.5);"},
    {"resetSensitivity", "resetSensitivity(): void", "Puts back the sensitivity from the profile.",
     "controls.view.resetSensitivity();"},
    {"onTurnStart",
     "onTurnStart(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     kTouchStartDoc, "controls.view.onTurnStart(() => log.info(\"turn\"));"},
    {"onTurnEnd",
     "onTurnEnd(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     kTouchEndDoc, "controls.view.onTurnEnd(() => log.info(\"still\"));"},
};

constexpr Member kCameraMembers[] = {
    {"locked", "readonly locked: boolean", "Whether the view is locked to the mouse now.",
     "if (!controls.view.locked) controls.view.lock();"},
    {"lock", "lock(): void", "Locks the view to the mouse, as the lock key does.", "controls.view.lock();"},
    {"unlock", "unlock(): void", "Gives the mouse back.", "controls.view.unlock();"},
    {"toggleLock", "toggleLock(): void", "Locks or unlocks the view.", "controls.view.toggleLock();"},
    {"onLock", "onLock(handler: (event: TaskContext) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time the view is locked.", "controls.view.onLock(() => hints.hide());"},
    {"onUnlock",
     "onUnlock(handler: (event: TaskContext) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time the view is unlocked.", "controls.view.onUnlock(() => hints.show());"},
};

constexpr Member kFreeLookMembers[] = {
    {"looking", "readonly looking: boolean", "Whether it is looking around now.", "log.info(controls.peek.looking);"},
    {"begin", "begin(): void", "Starts looking around, as holding its key does.", "controls.peek.begin();"},
    {"end", "end(): void", "Stops looking around.", "controls.peek.end();"},
    {"turn", "turn(dx: number, dy: number): void",
     "Turns the view as if the mouse moved dx and dy counts; it only turns while it is looking around (begin() or its "
     "key).",
     "controls.peek.turn(-200, 0);"},
    {"setSensitivity", "setSensitivity(x: number, y?: number): void",
     "Changes how far looking around turns for each mouse count (0.01 to 100; y is x when left out) until the logic "
     "stops or the profile changes. Throws GS-API-017 outside that range.",
     "controls.peek.setSensitivity(0.8);"},
    {"resetSensitivity", "resetSensitivity(): void", "Puts back the sensitivity from the profile.",
     "controls.peek.resetSensitivity();"},
    {"onStart", "onStart(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     kTouchStartDoc, "controls.peek.onStart(() => log.info(\"peek\"));"},
    {"onEnd", "onEnd(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     kTouchEndDoc, "controls.peek.onEnd(() => log.info(\"back\"));"},
};

constexpr Member kSkillMembers[] = {
    {"aiming", "readonly aiming: boolean", "Whether it is aiming now.", "log.info(controls.skill1.aiming);"},
    {"aim", "aim(): void", "Starts aiming, as pressing its key does.", "controls.skill1.aim();"},
    {"cast", "cast(): void", "Casts the skill where it is aiming.", "controls.skill1.cast();"},
    {"cancel", "cancel(): void", "Cancels the skill.", "controls.skill1.cancel();"},
    {"turn", "turn(dx: number, dy: number): void", "Turns the view as if the mouse moved dx and dy counts.",
     "controls.skill1.turn(50, 0);"},
    {"setRadius", "setRadius(radius: number): void",
     "Changes how far the aiming finger can move from the skill's place (0.01 to 1, a ratio of the picture height) "
     "until the logic stops or the profile changes. Throws GS-API-017 outside that range.",
     "controls.skill1.setRadius(0.2);"},
    {"resetRadius", "resetRadius(): void", "Puts back the radius from the profile.", "controls.skill1.resetRadius();"},
    {"setSensitivity", "setSensitivity(x: number, y?: number): void",
     "Changes how far aiming moves for each mouse count (0.01 to 100; y is x when left out) until the logic stops or "
     "the profile changes. Throws GS-API-017 outside that range.",
     "controls.skill1.setSensitivity(1.2);"},
    {"resetSensitivity", "resetSensitivity(): void", "Puts back the sensitivity from the profile.",
     "controls.skill1.resetSensitivity();"},
    {"onAimStart",
     "onAimStart(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time it starts aiming.", "controls.skill1.onAimStart(() => { controls.move.enabled = false; });"},
    {"onCast", "onCast(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time the skill is cast.", "controls.skill1.onCast(() => { controls.move.enabled = true; });"},
    {"onCancel",
     "onCancel(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time the skill is cancelled.", "controls.skill1.onCancel(() => log.info(\"cancelled\"));"},
};

constexpr Member kDialMembers[] = {
    {"open", "readonly open: boolean", "Whether the finger is down on the wheel now.",
     "if (controls.wheel.open) log.info(controls.wheel.cell);"},
    {"cell", "readonly cell: number",
     "The cell the finger is on now, counted from 0 at the first cell going clockwise; -1 while it is in the middle "
     "or the key is up.",
     "log.info(controls.wheel.cell);"},
    {"cells", "readonly cells: number", "How many cells the wheel has, from the profile.",
     "log.info(controls.wheel.cells);"},
    {"choose", "choose(cell: number): Action<void>",
     "Presses the middle of the wheel, slides to the cell (0 is the first, going clockwise) and lets go, as if its key "
     "were pressed and the mouse moved that way. Throws GS-API-017 when the cell is not a whole number from 0 to cells "
     "- 1.",
     "await controls.wheel.choose(2);"},
    {"onOpen", "onOpen(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     kTouchStartDoc, "controls.wheel.onOpen(() => log.info(\"open\"));"},
    {"onChoose", "onChoose(handler: (event: CellEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time the key is let go with the finger on a cell; the event says which.",
     "controls.wheel.onChoose(({ cell }) => log.info(\"picked\", cell));"},
};

constexpr Member kMultiPressMembers[] = {
    {"places", "readonly places: number", "How many places it presses together, from 2 to 10.",
     "log.info(controls.peekFire.places);"},
    {"holding", "readonly holding: boolean", "Whether it is pressing the screen now.",
     "if (controls.peekFire.holding) log.info(\"peeking and firing\");"},
    {"holdFor", "holdFor(durationMs: number): Action<void>", "Presses every place for a while, then lets go.",
     "await controls.peekFire.holdFor(800);"},
    {"toggleHold", "toggleHold(): void", "Presses if it is up, lets go if it is pressing.",
     "controls.peekFire.toggleHold();"},
    {"onPressed", "onPressed(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     kTouchStartDoc, "controls.peekFire.onPressed(() => log.info(\"down\"));"},
    {"onReleased",
     "onReleased(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     kTouchEndDoc, "controls.peekFire.onReleased(() => log.info(\"up\"));"},
};

constexpr Member kTempCursorMembers[] = {
    {"showing", "readonly showing: boolean",
     "Whether it has the cursor out now: its key is held while the view is locked.",
     "if (controls.cursor.showing) log.info(\"cursor out\");"},
    {"onShow", "onShow(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time it brings the cursor out.", "controls.cursor.onShow(() => hints.hide());"},
    {"onHide", "onHide(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time the cursor goes back and the view locks again.",
     "controls.cursor.onHide(() => hints.show());"},
};

constexpr Member kActionWheelMembers[] = {
    {"open", "readonly open: boolean", "Whether the wheel is open in the middle of the picture now.",
     "if (controls.items.open) log.info(controls.items.cell);"},
    {"cell", "readonly cell: number",
     "The cell the mouse points at now, counted from 0 at the top going clockwise; -1 while it points at the middle or "
     "the wheel is closed.",
     "log.info(controls.items.cell);"},
    {"cells", "readonly cells: number", "How many cells the wheel has, from the profile.",
     "log.info(controls.items.cells);"},
    {"names", "readonly names: readonly string[]", "The names of the cells, from the profile; empty for a cell without a name.",
     "log.info(controls.items.names.join(\", \"));"},
    {"choose", "choose(cell: number): Action<void>",
     "Opens the wheel, points at the cell (0 is the top, going clockwise) and lets go, so the cell runs as if its key "
     "were used. Throws GS-API-017 when the cell is not a whole number from 0 to cells - 1.",
     "await controls.items.choose(1);"},
    {"onOpen", "onOpen(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time its key opens the wheel.", "controls.items.onOpen(() => log.info(\"open\"));"},
    {"onChoose", "onChoose(handler: (event: WheelChoice) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time the key is let go while pointing at a cell; the event says which cell and its name.",
     "controls.items.onChoose(({ cell, name }) => log.info(\"ran\", cell, name));"},
};

constexpr Member kVariableMembers[] = {
    {"id", "readonly id: string", "The variable id from the profile.", "log.info(vars.onBoat.id);"},
    {"name", "readonly name: string", "The name shown in the editor.", "log.info(vars.onBoat.name);"},
    {"value", "value: VarValue",
     "The current value: true or false, a number or a string. Setting it changes what controls and logic see; throws "
     "GS-API-010 for other values or when the profile no longer has the variable.",
     "vars.onBoat.value = !vars.onBoat.value;"},
    {"toggle", "toggle(): void", "Flips a true or false value.", "vars.onBoat.toggle();"},
    {"onChange", "onChange(handler: (event: VarChange) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time the value changes, whoever changed it.",
     "vars.onBoat.onChange(({ value }) => { controls.drive.enabled = Boolean(value); });"},
};

constexpr Member kKeyMembers[] = {
    {"name", "readonly name: InputName", "The key's name, such as \"KeyF\".", "log.info(keys.F.name);"},
    {"isDown", "readonly isDown: boolean", "Whether the key is held down now.", "if (keys.Shift.isDown) log.info(\"walk\");"},
    {"pressedAt", "readonly pressedAt: number | null", "When the key went down (time.now()), or null when it is up.",
     "log.info(keys.F.pressedAt);"},
    {"heldMs", "readonly heldMs: number", "How long the key has been held, in milliseconds; 0 when it is up.",
     "log.info(keys.F.heldMs);"},
    {"onPress", "onPress(handler: (event: KeyEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time the key goes down.", "keys.MouseLeft.onPress(() => controls.fire.press());"},
    {"onRelease", "onRelease(handler: (event: KeyEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time the key comes up.", "keys.MouseLeft.onRelease(() => controls.fire.release());"},
    {"whileHeld", "whileHeld(handler: (event: KeyEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task when the key goes down and cancels it when the key comes up.",
     "keys.E.whileHeld(async ({ signal }) => { while (!signal.aborted) await controls.loot.tap(); });"},
    {"onTap", "onTap(handler: (event: KeyEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task when the key is let go within 250 ms of pressing it.", "keys.F.onTap(() => controls.pick.tap());"},
    {"onDoubleTap",
     "onDoubleTap(handler: (event: KeyEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task when the key is pressed again within 300 ms of a tap.", "keys.Q.onDoubleTap(() => controls.roll.tap());"},
    {"onLongPress",
     "onLongPress(durationMs: number, handler: (event: KeyEvent) => void | Promise<void>, options?: ListenOptions): "
     "Subscription",
     "Starts a task when the key has been held for durationMs (500 by default).",
     "keys.F.onLongPress(500, () => controls.bag.tap());"},
};

constexpr Member kGlobalsMembers[] = {
    {"get", "get(name: string): StoreValue | undefined", "Reads a global value, shared by every profile and task on this phone.",
     "if (globals.get(\"afk\")) return;"},
    {"set", "set(name: string, value: StoreValue): void", "Sets a global value; it lasts until the program closes.",
     "globals.set(\"afk\", true);"},
    {"onChange",
     "onChange(name: string, handler: (event: GlobalChange) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time a global value changes.", "globals.onChange(\"afk\", ({ value }) => log.info(value));"},
};

constexpr Member kGateMembers[] = {
    {"isOpen", "readonly isOpen: boolean", "Whether the gate is open.", "log.info(gate(\"loot\").isOpen);"},
    {"open", "open(): void", "Opens the gate.", "gate(\"loot\").open();"},
    {"close", "close(): void", "Closes the gate.", "gate(\"loot\").close();"},
    {"toggle", "toggle(): void", "Opens or closes the gate.", "gate(\"loot\").toggle();"},
    {"run", "run<T>(action: () => T): T | undefined", "Runs the action when the gate is open.",
     "gate(\"loot\").run(() => controls.pick.tap());"},
};

constexpr Member kStopwatchMembers[] = {
    {"elapsedMs", "readonly elapsedMs: number", "Milliseconds since it started or was reset.", "log.info(watch.elapsedMs);"},
    {"reset", "reset(): void", "Starts again from 0.", "watch.reset();"},
    {"lap", "lap(): number", "Milliseconds since the last lap, or since it started.", "log.info(watch.lap());"},
};

constexpr Member kFingerMembers[] = {
    {"down", "readonly down: boolean", "Whether the finger is still down.", "log.info(finger.down);"},
    {"move", "move(x: number | PicturePoint, y?: number, durationMs?: number): Action<void>",
     "Moves the finger to a position, at once or over durationMs.", "await finger.move(0.6, 0.7, 300);"},
    {"up", "up(): void", "Lifts the finger.", "finger.up();"},
};

constexpr Member kCapturedImageMembers[] = {
    {"region", "readonly region: Region", "Where it was taken, as ratios of the picture.", "log.info(shot.region);"},
    {"width", "readonly width: number", "Its width in pixels.", "log.info(shot.width);"},
    {"height", "readonly height: number", "Its height in pixels.", "log.info(shot.height);"},
    {"save", "save(name: string): Promise<void>",
     "Keeps it as a template of the script's folder: res/<name>.png and an entry in res/resources.json, replacing the "
     "picture of a template with that name; resolves once both are written. Logic keeps running. Throws GS-API-038 "
     "when the name is not allowed; rejects with GS-API-038 when the files cannot be written and GS-API-037 when the "
     "image is no longer kept.",
     "await shot.save(\"Boss icon\");"},
};

constexpr Member kComponentEventMembers[] = {
    {"emit", "emit(value?: unknown): void", "Starts a task in each listener with { value }.", "this.pressed.emit(1);"},
    {"on",
     "on(handler: (event: { readonly value: unknown }) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time the event is emitted.", "toggle.pressed.on(({ value }) => log.info(value));"},
};

constexpr Member kComponentFingerMembers[] = {
    {"isDown", "readonly isDown: boolean", "Whether the finger is down.", "log.info(this.finger.isDown);"},
    {"down", "down(x: number | PicturePoint, y?: number): Finger",
     "Puts the finger down at a position, lifting it first when it is already down; it stays down after the task ends.",
     "this.finger.down(this.props.at);"},
    {"move", "move(x: number | PicturePoint, y?: number, durationMs?: number): Action<void>",
     "Moves the finger, at once or over durationMs.", "await this.finger.move(0.6, 0.7, 200);"},
    {"up", "up(): void", "Lifts the finger.", "this.finger.up();"},
};

constexpr Member kComponentClassMembers[] = {
    {"new", "new (options?: ComponentOptions): Component",
     "Makes a component of a class that extends Component, with values for its static props; then calls its setup()."},
    {"prototype", "readonly prototype: Component", "The members every component has."},
    {"implements", "readonly implements?: readonly Interface<unknown>[]",
     "The interfaces the components of a class implement, written static implements = [Fire]; the classes that extend "
     "it implement them too.",
     "static implements = [Fire];"},
};

constexpr std::string_view kOverrideDoc =
    "Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well "
    "as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.";

constexpr Member kComponentOverrideMembers[] = {
    {"setup", "setup(): void", "Runs once after the component is created; set up its listeners here.",
     "setup() { log.info(this.name); }"},
    {"onPress", "onPress(event?: ControlEvent): void | Promise<void>", kOverrideDoc, "onPress() { log.info(\"down\"); }"},
    {"onRelease", "onRelease(event?: ControlEvent): void | Promise<void>", kOverrideDoc,
     "onRelease() { log.info(\"up\"); }"},
    {"whileHeld", "whileHeld(event?: ControlEvent): void | Promise<void>", kOverrideDoc,
     "async whileHeld({ signal }: ControlEvent) { while (!signal.aborted) await sleep(100); }"},
    {"onEnabled", "onEnabled(event?: ControlEvent): void | Promise<void>", kOverrideDoc,
     "onEnabled() { log.info(\"on\"); }"},
    {"onDisabled", "onDisabled(event?: ControlEvent): void | Promise<void>", kOverrideDoc,
     "onDisabled() { log.info(\"off\"); }"},
    {"onTouchStart", "onTouchStart(event?: ControlEvent): void | Promise<void>", kOverrideDoc,
     "onTouchStart() { log.info(\"touch\"); }"},
    {"onTouchEnd", "onTouchEnd(event?: ControlEvent): void | Promise<void>", kOverrideDoc,
     "onTouchEnd() { log.info(\"lift\"); }"},
};

constexpr Member kHoldOverrideMembers[] = {
    {"onPressed", "onPressed(event?: ControlEvent): void | Promise<void>", kOverrideDoc,
     "onPressed() { log.info(this.name); }"},
    {"onReleased", "onReleased(event?: ControlEvent): void | Promise<void>", kOverrideDoc,
     "onReleased() { log.info(this.name); }"},
};

constexpr Member kStartEndOverrideMembers[] = {
    {"onStart", "onStart(event?: ControlEvent): void | Promise<void>", kOverrideDoc, "onStart() { log.info(this.name); }"},
    {"onEnd", "onEnd(event?: ControlEvent): void | Promise<void>", kOverrideDoc, "onEnd() { log.info(this.name); }"},
};

constexpr Member kStickOverrideMembers[] = {
    {"onMoveStart", "onMoveStart(event?: ControlEvent): void | Promise<void>", kOverrideDoc,
     "onMoveStart() { log.info(\"go\"); }"},
    {"onStop", "onStop(event?: ControlEvent): void | Promise<void>", kOverrideDoc, "onStop() { log.info(\"stop\"); }"},
    {"onDirectionChange", "onDirectionChange(event?: DirectionEvent): void | Promise<void>", kOverrideDoc,
     "onDirectionChange(event?: DirectionEvent) { log.info(event?.direction); }"},
};

constexpr Member kRightWalkOverrideMembers[] = {
    {"onWalkStart", "onWalkStart(event?: ControlEvent): void | Promise<void>", kOverrideDoc,
     "onWalkStart() { log.info(\"walk\"); }"},
    {"onStop", "onStop(event?: ControlEvent): void | Promise<void>", kOverrideDoc, "onStop() { log.info(\"arrived\"); }"},
};

constexpr Member kLookOverrideMembers[] = {
    {"onTurnStart", "onTurnStart(event?: ControlEvent): void | Promise<void>", kOverrideDoc,
     "onTurnStart() { log.info(\"turn\"); }"},
    {"onTurnEnd", "onTurnEnd(event?: ControlEvent): void | Promise<void>", kOverrideDoc,
     "onTurnEnd() { log.info(\"still\"); }"},
};

constexpr Member kCameraOverrideMembers[] = {
    {"onLock", "onLock(event?: TaskContext): void | Promise<void>", kOverrideDoc, "onLock() { hints.hide(); }"},
    {"onUnlock", "onUnlock(event?: TaskContext): void | Promise<void>", kOverrideDoc, "onUnlock() { hints.show(); }"},
};

constexpr Member kSkillOverrideMembers[] = {
    {"onAimStart", "onAimStart(event?: ControlEvent): void | Promise<void>", kOverrideDoc,
     "onAimStart() { log.info(\"aim\"); }"},
    {"onCast", "onCast(event?: ControlEvent): void | Promise<void>", kOverrideDoc, "onCast() { log.info(\"cast\"); }"},
    {"onCancel", "onCancel(event?: ControlEvent): void | Promise<void>", kOverrideDoc,
     "onCancel() { log.info(\"cancelled\"); }"},
};

constexpr Member kDialOverrideMembers[] = {
    {"onOpen", "onOpen(event?: ControlEvent): void | Promise<void>", kOverrideDoc, "onOpen() { log.info(\"open\"); }"},
    {"onChoose", "onChoose(event?: CellEvent): void | Promise<void>", kOverrideDoc,
     "onChoose(event?: CellEvent) { log.info(event?.cell); }"},
};

constexpr Member kTempCursorOverrideMembers[] = {
    {"onShow", "onShow(event?: ControlEvent): void | Promise<void>", kOverrideDoc, "onShow() { hints.hide(); }"},
    {"onHide", "onHide(event?: ControlEvent): void | Promise<void>", kOverrideDoc, "onHide() { hints.show(); }"},
};

constexpr Member kActionWheelOverrideMembers[] = {
    {"onOpen", "onOpen(event?: ControlEvent): void | Promise<void>", kOverrideDoc, "onOpen() { log.info(\"open\"); }"},
    {"onChoose", "onChoose(event?: WheelChoice): void | Promise<void>", kOverrideDoc,
     "onChoose(event?: WheelChoice) { log.info(event?.name); }"},
};

constexpr Member kTaskMembers[] = {
    {"params", "readonly params: TaskParams",
     "The settings chosen for this task on the Tasks page, or the values it declares in static params.",
     "log.info(this.params.rounds);"},
    {"progress", "progress(text: string): void",
     "Shows how far the task has come on its card and in the mirror window's task menu.",
     "this.progress(\"Round 3 of 10\");"},
    {"run", "run(context: TaskContext): Promise<void>",
     "What the task does once it starts. When it returns the task is done; an error ends it as failed; Stop cancels it.",
     "async run({ signal }) { while (!signal.aborted) await sleep(1000); }"},
};

constexpr Member kTaskClassMembers[] = {
    {"new", "new (): Task", "GameScrcpy makes the task when it starts; scripts do not create tasks themselves."},
    {"prototype", "readonly prototype: Task", "The members every task has."},
};

constexpr Member kUiPanelMembers[] = {
    {"render", "render(tree: UiNode | null): void",
     "Shows the tree in the panel in place of what it showed; null empties it. GameScrcpy draws at most 30 trees a "
     "second, so of quicker ones the last shows. Throws GS-API-075 for a tree that is not right, naming the node, and "
     "GS-API-076 when the extension does not declare the panel for this logic.",
     "stats.render({ type: \"column\", children: [{ type: \"button\", id: \"go\", title: \"Go\" }] });"},
    {"on",
     "on(type: UiEventType, handler: (event: UiEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task each time the user does something in the panel: click on a button; change of a field, number, "
     "toggle, choice or slider; submit with Enter in a field; select of a list item; open and close of the panel. "
     "Throws GS-API-017 for another event.",
     "stats.on(\"click\", ({ id }) => log.info(\"clicked\", id));"},
    {"close", "close(): void", "Closes the mirror window's side panel when it shows this panel.", "stats.close();"},
};

constexpr Member kUiStatusMembers[] = {
    {"render", "render(tree: UiStatusNode | null): void",
     "Shows the widget in place of what it showed; null takes it away. Throws GS-API-075 for a tree that is not right "
     "and GS-API-076 when the extension does not declare the widget for this logic.",
     "ui.status(\"fps\").render({ type: \"row\", children: [{ type: \"icon\", name: \"gauge\" }, { type: \"text\", "
     "text: \"60\" }] });"},
};

constexpr Member kHardwareDeviceMembers[] = {
    {"id", "readonly id: string", "The id of the device.", "log.info(port.id);"},
    {"kind", "readonly kind: HardwareKind", "The kind of device.", "log.info(port.kind);"},
    {"name", "readonly name: string", "The name of the device, as in the device list.", "log.info(port.name);"},
    {"isOpen", "readonly isOpen: boolean", "Whether it is still open.", "if (!port.isOpen) return;"},
    {"close", "close(): Promise<void>", "Closes the device, so that it can be opened again.", "await port.close();"},
    {"onClose",
     "onClose(handler: (event: DeviceCloseEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task when the device closes by itself: it was unplugged, failed or the permission was taken back.",
     "port.onClose(({ reason }) => log.warn(\"closed\", reason));"},
};

constexpr Member kSerialDeviceMembers[] = {
    {"kind", "readonly kind: \"serial\"", "Always \"serial\".", "log.info(port.kind);"},
    {"write", "write(data: HardwareBytes | string): Promise<void>",
     "Sends bytes, or text as UTF-8, up to 65536 bytes at a time; resolves once they are sent.",
     "await port.write(\"ping\\n\");"},
    {"setSignals", "setSignals(signals: SerialSignals): Promise<void>", "Turns the DTR and RTS lines on or off.",
     "await port.setSignals({ dtr: false });"},
    {"onData",
     "onData(handler: (event: DeviceDataEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task for each piece of data that comes in; by default the events queue, so none is lost.",
     "port.onData(({ data }) => log.info(data.length));"},
};

constexpr Member kHidDeviceMembers[] = {
    {"kind", "readonly kind: \"hid\"", "Always \"hid\".", "log.info(pad.kind);"},
    {"sendReport", "sendReport(report: number, data: HardwareBytes): Promise<void>",
     "Sends an output report: its id, 0 when the device numbers none, and the bytes after it.",
     "await pad.sendReport(0, [5, 255]);"},
    {"sendFeature", "sendFeature(report: number, data: HardwareBytes): Promise<void>",
     "Sends a feature report: its id and the bytes after it.", "await pad.sendFeature(3, [1]);"},
    {"getFeature", "getFeature(report: number): Promise<Uint8Array>",
     "Reads a feature report; resolves with the bytes after its id.", "const info = await pad.getFeature(1);"},
    {"onReport",
     "onReport(handler: (event: DeviceReportEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
     "Starts a task for each input report; by default the events queue, so none is lost.",
     "pad.onReport(({ report, data }) => log.info(report, data[0]));"},
};

constexpr Member kBleDeviceMembers[] = {
    {"kind", "readonly kind: \"ble\"", "Always \"ble\".", "log.info(belt.kind);"},
    {"services", "services(): Promise<readonly BleService[]>", "The services and characteristics the device offers.",
     "for (const service of await belt.services()) log.info(service.uuid);"},
    {"read", "read(service: string, characteristic: string): Promise<Uint8Array>",
     "Reads a characteristic. A UUID may be written short, such as \"180f\".",
     "const battery = await belt.read(\"180f\", \"2a19\");"},
    {"write",
     "write(service: string, characteristic: string, data: HardwareBytes, options?: BleWriteOptions): Promise<void>",
     "Writes up to 512 bytes to a characteristic.", "await belt.write(\"180d\", \"2a39\", [1]);"},
    {"subscribe",
     "subscribe(service: string, characteristic: string, handler: (event: DeviceNotifyEvent) => void | Promise<void>, "
     "options?: ListenOptions): Promise<Subscription>",
     "Turns on the notifications of a characteristic and starts a task for each new value; resolves once they are on. "
     "Disposing the last subscription of the characteristic turns them off; by default the events queue, so none is "
     "lost.",
     "await belt.subscribe(\"180d\", \"2a37\", ({ data }) => log.info(\"heart rate\", data[1]));"},
};

constexpr Member kInputSourceMembers[] = {
    {"name", "readonly name: string", "The name of the input source.", "log.info(deck.name);"},
    {"press", "press(key: string): void",
     "Presses a key of the source, named with 1 to 32 letters, digits, _ or -; it stays down until release() or "
     "until the logic stops.",
     "deck.press(\"k1\");"},
    {"release", "release(key: string): void", "Lets go of a key of the source.", "deck.release(\"k1\");"},
    {"axis", "axis(key: string, x: number, y?: number): void",
     "Moves an axis of the source to x and y, each from -1 to 1 (y is 0 when left out); bound to a stick it pushes "
     "the stick, and 0, 0 lets it go.",
     "deck.axis(\"knob\", 0.5, -0.25);"},
};

constexpr Interface kInterfaces[] = {
    {"Component", "A component of the profile in use. Every type below has these members too.", kComponentMembers},
    {"Hint", "The key hint of a component on the picture; changes last until the script stops.", kHintMembers},
    {"Tap", "A tap component: each key press taps the screen once.", kTapMembers, "Component"},
    {"Hold", "A hold component: the finger stays down while the key is held.", kHoldMembers, "Component"},
    {"LongPress", "A long-press component: each key press holds the screen for holdMs.", kLongPressMembers,
     "Component"},
    {"Turbo", "A turbo component: holding the key taps again and again.", kTurboMembers, "Component"},
    {"Swipe", "A swipe component: each key press swipes along its path.", kSwipeMembers, "Component"},
    {"Pinch", "A pinch component: two keys zoom in and out with two fingers.", kPinchMembers, "Component"},
    {"Stick", "A stick: WASD wheel or gamepad stick.", kStickMembers, "Component"},
    {"Wasd", "A WASD wheel: four keys push the stick, with walk and sprint keys.", kWasdMembers, "Stick"},
    {"PadStick", "A gamepad stick component.", {}, "Stick"},
    {"RightWalk", "A right-click walk component: a click walks the hero toward the pointer.", kRightWalkMembers,
     "Component"},
    {"Look", "A view component: the finger that turns the view.", kLookMembers, "Component"},
    {"Camera", "A camera component: the mouse turns the view while it is locked.", kCameraMembers, "Look"},
    {"PadCamera", "A gamepad camera component: a gamepad stick turns the view.", {}, "Look"},
    {"FreeLook", "A free-look component: holding the key looks around without turning.", kFreeLookMembers,
     "Component"},
    {"Skill", "A skill component: press to aim, release or click to cast.", kSkillMembers, "Component"},
    {"Dial", "A direction wheel: holding its key presses the middle of the game's wheel and the mouse picks a cell.",
     kDialMembers, "Component"},
    {"MultiPress", "A multi-press component: the key holds several places of the screen at once.", kMultiPressMembers,
     "Component"},
    {"TempCursor", "A temporary cursor: holding its key brings the cursor out while the view is locked.",
     kTempCursorMembers, "Component"},
    {"ActionWheel",
     "An action wheel: holding its key opens a wheel in the middle of the picture; letting go runs the cell the mouse "
     "points at.",
     kActionWheelMembers, "Component"},
    {"Variable", "A variable of the profile in use, such as \"on a boat\" or \"aiming\".", kVariableMembers},
    {"Key",
     "A key, mouse button or gamepad button. Listening to it in a profile's logic takes it for the script: it no "
     "longer goes to the phone as a key, and mouse buttons count only while the view is locked. A task listening to it "
     "hears it without taking it, so the profile's keys and logic still get it.",
     kKeyMembers},
    {"Globals", "Values shared by every profile and task on this phone until the program closes.", kGlobalsMembers},
    {"Gate", "A named gate from gate().", kGateMembers},
    {"Stopwatch", "A stopwatch from stopwatch().", kStopwatchMembers},
    {"Finger", "A finger the script put down with touch.down().", kFingerMembers},
    {"CapturedImage", "Part of the picture taken by vision.capture().", kCapturedImageMembers},
    {"Task",
     "A task: logic that runs on its own on a phone, started from the Tasks page or the mirror window's task menu. It "
     "keeps running when the profile changes.",
     kTaskMembers},
    {"ComponentEvent", "An event a component made in logic emits; others listen with on().", kComponentEventMembers},
    {"ComponentFinger", "The own finger of a component made in logic.", kComponentFingerMembers},
    {"ComponentClass", "The Component base class that components made in logic extend.", kComponentClassMembers},
    {"ComponentOverrides",
     "The methods a class that extends a built-in component can write; each event one runs when that event comes.",
     kComponentOverrideMembers},
    {"HoldOverrides", "The events a class that extends Hold can take on as well.", kHoldOverrideMembers,
     "ComponentOverrides"},
    {"StartEndOverrides", "The events a class that extends LongPress, Swipe or FreeLook can take on as well.",
     kStartEndOverrideMembers, "ComponentOverrides"},
    {"StickOverrides", "The events a class that extends Wasd or PadStick can take on as well.", kStickOverrideMembers,
     "ComponentOverrides"},
    {"RightWalkOverrides", "The events a class that extends RightWalk can take on as well.", kRightWalkOverrideMembers,
     "ComponentOverrides"},
    {"LookOverrides", "The events a class that extends PadCamera can take on as well.", kLookOverrideMembers,
     "ComponentOverrides"},
    {"CameraOverrides", "The events a class that extends Camera can take on as well.", kCameraOverrideMembers,
     "LookOverrides"},
    {"SkillOverrides", "The events a class that extends Skill can take on as well.", kSkillOverrideMembers,
     "ComponentOverrides"},
    {"DialOverrides", "The events a class that extends Dial can take on as well.", kDialOverrideMembers,
     "ComponentOverrides"},
    {"TempCursorOverrides", "The events a class that extends TempCursor can take on as well.",
     kTempCursorOverrideMembers, "ComponentOverrides"},
    {"ActionWheelOverrides", "The events a class that extends ActionWheel can take on as well.",
     kActionWheelOverrideMembers, "ComponentOverrides"},
    {"TaskClass", "The Task base class that a task's default export extends.", kTaskClassMembers},
    {"UiPanel", "A panel of an extension from ui.panel(), drawn with GameScrcpy's own controls.", kUiPanelMembers},
    {"UiStatus", "A status widget of an extension from ui.status().", kUiStatusMembers},
    {"Controls",
     "The components of the profile in use, by id. GameScrcpy writes the members for each profile into "
     "types/profile.d.ts; use mapping for ids that are only known at run time.",
     {}},
    {"Vars", "The variables of the profile in use, by id; the members come from types/profile.d.ts.", {}},
    {"TemplateNames",
     "The template names of the profile's res/resources.json; GameScrcpy writes the members into types/profile.d.ts.",
     {}},
    {"RegionNames", "The region names of the profile's res/resources.json; the members come from types/profile.d.ts.",
     {}},
    {"PointNames", "The point names of the profile's res/resources.json; the members come from types/profile.d.ts.", {}},
    {"PathNames", "The path names of the profile's res/resources.json; the members come from types/profile.d.ts.", {}},
    {"ColorNames", "The color names of the profile's res/resources.json; the members come from types/profile.d.ts.",
     {}},
    {"Extensions",
     "The system nodes of the installed native extensions, by extension; GameScrcpy writes the members into "
     "types/extensions.d.ts.",
     {}},
    {"HardwareDevice", "A device opened with hardware.open(). Every kind below has these members too.",
     kHardwareDeviceMembers},
    {"SerialDevice", "An open serial port.", kSerialDeviceMembers, "HardwareDevice"},
    {"HidDevice", "An open USB HID device. Keyboards, mice and keypads are never offered.", kHidDeviceMembers,
     "HardwareDevice"},
    {"BleDevice", "An open Bluetooth LE device; it stays connected while it is open.", kBleDeviceMembers,
     "HardwareDevice"},
    {"InputSource", "Keys and axes that logic sends into the phone's key mapping, made by input.source().",
     kInputSourceMembers},
};

constexpr std::string_view kBuiltInClassDoc =
    "A built-in component class. A class of yours that extends it is placed in the editor as this component, presses "
    "the screen just as it does and runs the event methods it writes as well; children are made with this.child(). "
    "Components of these classes only come from the profile: new on them throws GS-API-036.";

constexpr Value kValues[] = {
    {"Component", "ComponentClass",
     "The base class of components made in logic: extend it, declare static props and set them up in setup(); create "
     "them with new at the top level of the logic.",
     "class Toggle extends Component { static props = { key: prop.key(\"F\") }; }"},
    {"Tap", "BuiltInClass<Tap, ComponentOverrides>", kBuiltInClassDoc,
     "export default class Jump extends Tap { onPress() { log.info(\"jump\"); } }"},
    {"Hold", "BuiltInClass<Hold, HoldOverrides>", kBuiltInClassDoc,
     "export default class Scope extends Hold { onPressed() { log.info(\"scoped\"); } }"},
    {"LongPress", "BuiltInClass<LongPress, StartEndOverrides>", kBuiltInClassDoc,
     "export default class Bag extends LongPress { onEnd() { log.info(\"bag open\"); } }"},
    {"Turbo", "BuiltInClass<Turbo, ComponentOverrides>", kBuiltInClassDoc,
     "export default class Shots extends Turbo { onRelease() { log.info(\"stop\"); } }"},
    {"Swipe", "BuiltInClass<Swipe, StartEndOverrides>", kBuiltInClassDoc,
     "export default class Slide extends Swipe { onEnd() { log.info(\"slid\"); } }"},
    {"Pinch", "BuiltInClass<Pinch, ComponentOverrides>", kBuiltInClassDoc,
     "export default class Zoom extends Pinch { onTouchEnd() { log.info(\"zoomed\"); } }"},
    {"Wasd", "BuiltInClass<Wasd, StickOverrides>", kBuiltInClassDoc,
     "export default class Move extends Wasd { onStop() { log.info(\"stopped\"); } }"},
    {"RightWalk", "BuiltInClass<RightWalk, RightWalkOverrides>", kBuiltInClassDoc,
     "export default class Walk extends RightWalk { onStop() { log.info(\"arrived\"); } }"},
    {"PadStick", "BuiltInClass<PadStick, StickOverrides>", kBuiltInClassDoc,
     "export default class Stick extends PadStick { onMoveStart() { log.info(\"go\"); } }"},
    {"Camera", "BuiltInClass<Camera, CameraOverrides>", kBuiltInClassDoc,
     "export default class View extends Camera { onLock() { hints.hide(); } }"},
    {"PadCamera", "BuiltInClass<PadCamera, LookOverrides>", kBuiltInClassDoc,
     "export default class PadView extends PadCamera { onTurnStart() { log.info(\"turn\"); } }"},
    {"FreeLook", "BuiltInClass<FreeLook, StartEndOverrides>", kBuiltInClassDoc,
     "export default class Peek extends FreeLook { onStart() { log.info(\"peek\"); } }"},
    {"Skill", "BuiltInClass<Skill, SkillOverrides>", kBuiltInClassDoc,
     "export default class Blink extends Skill { onCast() { log.info(\"cast\"); } }"},
    {"Dial", "BuiltInClass<Dial, DialOverrides>", kBuiltInClassDoc,
     "export default class Weapons extends Dial { onChoose(event?: CellEvent) { log.info(event?.cell); } }"},
    {"MultiPress", "BuiltInClass<MultiPress, HoldOverrides>", kBuiltInClassDoc,
     "export default class PeekFire extends MultiPress { onPressed() { log.info(\"peek and fire\"); } }"},
    {"TempCursor", "BuiltInClass<TempCursor, TempCursorOverrides>", kBuiltInClassDoc,
     "export default class Pointer extends TempCursor { onShow() { hints.hide(); } }"},
    {"ActionWheel", "BuiltInClass<ActionWheel, ActionWheelOverrides>", kBuiltInClassDoc,
     "export default class Items extends ActionWheel { onChoose(event?: WheelChoice) { log.info(event?.name); } }"},
    {"defineInterface", "<T>(name: string) => Interface<T>",
     "Makes an interface, or gives back the one with this name, so components of different kinds can answer the same "
     "call: list it in static implements of their classes, then mapping.all(it) or it.all() gives every component "
     "that implements it. Throws GS-API-011 for a name that is not a string or is empty.",
     "const Fire = defineInterface<{ fire(): void }>(\"fire\");"},
    {"Task", "TaskClass",
     "The base class of a task. The task.ts of a task exports a class that extends it: static params lists its settings "
     "and run() does the work.",
     "export default class Daily extends Task { async run() { log.info(this.params.rounds); } }"},
    {"controls", "Controls",
     "The components of the profile in use, by id; empty without a phone. In a task they are the components of the "
     "profile the phone uses now, and are made again from the new profile when the phone switches.",
     "controls.jump.onPress(() => log.info(\"jump\"));"},
    {"vars", "Vars",
     "The variables of the profile in use, by id; empty without a phone. In a task they follow the phone's profile, "
     "as controls do.",
     "if (vars.onBoat.value) await controls.drive.hold(500);"},
    {"keys", "Keys",
     "Keys, mouse buttons and gamepad buttons by name: keys.F, keys.Space, keys.MouseLeft, keys.GamepadA.",
     "keys.F.onTap(() => controls.pick.tap());"},
    {"globals", "Globals", "Values shared by every profile and task on this phone.", "globals.set(\"afk\", true);"},
    {"machine",
     "<S extends string>(states: { readonly [K in S]: (to: MachineTo<S>) => MachineStep | void | "
     "Promise<MachineStep | void> }, options?: MachineOptions<S>) => Promise<void>",
     "Runs a state machine: each member of states is a state, an async function that receives to and returns "
     "to.<next state>(reason) to move on, or nothing to end the machine. The first member is the start unless "
     "options.start names another. The editor shows the state it is in and every transition with its reason. "
     "Throws GS-API-008 when states is not an object of functions and GS-API-002 for an unknown start.",
     "await machine({ async lobby(to) { await vision.findAndTap(\"start\"); return to.queue(\"tapped start\"); }, "
     "async queue(to) { return (await vision.waitFor(\"inGame\", { timeoutMs: 120000 })) ? to.game() : "
     "to.lobby(\"timed out\"); }, async game() {} }, { name: \"farm\" });"},
    {"ext", "Extensions",
     "The system nodes that native extensions add: ext.<extension>.<node>(...) runs a node at once and gives its "
     "result. <extension> is the extension id with each part in lowerCamelCase, so the nodes of gamescrcpy.pc-clock "
     "are ext.gamescrcpy.pcClock. Throws GS-API-060 when the extension is not on, GS-API-061 for values of the wrong "
     "type, GS-API-062 when the node fails, GS-API-063 naming a permission the extension lacks and GS-API-064 when "
     "the node takes longer than 100 ms.",
     "log.info(ext.gamescrcpy.pcClock.now());"},
};

int requiredCount(std::span<const Param> params) noexcept
{
    int count = 0;
    for (const Param& param : params) {
        if (!param.optional && !param.type.starts_with("...")) {
            ++count;
        }
    }
    return count;
}

}

std::span<const Function> globals() noexcept
{
    return kGlobals;
}

std::span<const Namespace> namespaces() noexcept
{
    return kNamespaces;
}

std::span<const Namespace> attached() noexcept
{
    return kAttached;
}

std::span<const Type> types() noexcept
{
    return kTypes;
}

std::span<const Interface> interfaces() noexcept
{
    return kInterfaces;
}

std::span<const Value> values() noexcept
{
    return kValues;
}

std::vector<std::string_view> moduleExports()
{
    std::vector<std::string_view> names;
    names.reserve(std::size(kGlobals) + std::size(kValues) + std::size(kNamespaces));
    std::ranges::transform(kGlobals, std::back_inserter(names), &Function::name);
    std::ranges::transform(kValues, std::back_inserter(names), &Value::name);
    std::ranges::transform(kNamespaces, std::back_inserter(names), &Namespace::name);
    return names;
}

std::string_view stabilityName(Stability stability) noexcept
{
    switch (stability) {
    case Stability::Stable:
        return "stable";
    case Stability::Preview:
        return "preview";
    case Stability::Experimental:
        return "experimental";
    }
    return "stable";
}

bool install(JSContext* ctx, JSValueConst global, JSValueConst action, JSValueConst library)
{
    const auto native = [ctx, action, library](std::string_view owner, const Function& function) {
        const std::string name(function.name);
        if (function.native == nullptr) {
            const std::string key = owner.empty() ? name : std::string(owner) + "." + name;
            JSValue found = JS_GetPropertyStr(ctx, library, key.c_str());
            if (!JS_IsFunction(ctx, found)) {
                JS_FreeValue(ctx, found);
                return JS_ThrowInternalError(ctx, "the script library has no %s", key.c_str());
            }
            if (JS_DefinePropertyValueStr(ctx, found, "length", JS_NewInt32(ctx, requiredCount(function.params)),
                                          JS_PROP_CONFIGURABLE) < 0 ||
                JS_DefinePropertyValueStr(ctx, found, "name", JS_NewStringLen(ctx, name.data(), name.size()),
                                          JS_PROP_CONFIGURABLE) < 0) {
                JS_FreeValue(ctx, found);
                return JS_EXCEPTION;
            }
            return found;
        }
        JSValue made = nativeFunction(ctx, function.native, name, requiredCount(function.params), function.magic);
        if (JS_IsException(made) || !function.returns.starts_with(kActionType)) {
            return made;
        }
        JSValue wrapped = JS_Call(ctx, action, JS_UNDEFINED, 1, &made);
        JS_FreeValue(ctx, made);
        return wrapped;
    };
    for (const Function& function : kGlobals) {
        if (!define(ctx, global, function.name, native({}, function), kFlags)) {
            return false;
        }
    }
    for (const Namespace& space : kNamespaces) {
        JSValue object = JS_NewObject(ctx);
        if (JS_IsException(object)) {
            return false;
        }
        for (const Function& function : space.functions) {
            if (!function.overload.empty()) {
                continue;
            }
            if (!define(ctx, object, function.name, native(space.name, function), JS_PROP_ENUMERABLE)) {
                JS_FreeValue(ctx, object);
                return false;
            }
        }
        if (JS_FreezeObject(ctx, object) < 0 || !define(ctx, global, space.name, object, kFlags)) {
            return false;
        }
    }
    return true;
}

JSValue host(JSContext* ctx)
{
    struct Entry {
        const char* name;
        JSCFunctionMagic* native;
        int magic;
        int length;
    };
    constexpr Entry kEntries[] = {
        {"controls", &hostControlsNative, 0, 0},
        {"states", &hostStatesNative, 0, 0},
        {"stateValue", &stateValueNative, 0, 1},
        {"setState", &setStateNative, 0, 2},
        {"trigger", &holdControlNative, 0, 1},
        {"hold", &holdControlNative, 1, 2},
        {"press", &pressNative, 0, 2},
        {"act", &actNative, 0, 4},
        {"tune", &tuneNative, 0, 2},
        {"status", &statusNative, 0, 1},
        {"locked", &lockedNative, 0, 0},
        {"claimKey", &claimKeyNative, 0, 1},
        {"handOver", &handOverNative, 0, 2},
        {"global", &globalNative, 0, 1},
        {"setGlobal", &setGlobalNative, 0, 2},
        {"app", &appNative, 0, 2},
        {"connected", &connectedNative, 0, 0},
        {"awaitConnection", &awaitConnectionNative, 0, 0},
        {"capture", &captureNative, 0, 2},
        {"changed", &changedNative, 1, 3},
        {"saveCapture", &saveCaptureNative, 0, 2},
        {"tap", &tapNative, 0, 3},
        {"swipe", &swipeNative, 0, 5},
        {"fingerDown", &fingerDownNative, 0, 2},
        {"fingerMove", &fingerMoveNative, 0, 4},
        {"fingerUp", &fingerUpNative, 0, 1},
        {"liftAll", &liftAllNative, 0, 0},
        {"sleep", &sleepNative, 0, 1},
        {"setEnabled", &enableControlNative, 0, 2},
        {"spawn", &spawnNative, 0, 3},
        {"cancel", &cancelNative, 0, 1},
        {"currentTask", &currentTaskNative, 0, 0},
        {"wallNow", &wallNowNative, 0, 0},
        {"random", &randomNative, 0, 0},
        {"erase", &eraseNative, 0, 1},
        {"now", &nowNative, 0, 0},
        {"nextFrame", &nextFrameNative, 0, 0},
        {"nodes", &nodesNative, 0, 0},
        {"callNode", &callNodeNative, 0, 3},
        {"call", &serviceCallNative, 0, 2},
        {"request", &serviceRequestNative, 0, 2},
    };
    JSValue object = JS_NewObject(ctx);
    if (JS_IsException(object)) {
        return object;
    }
    for (const Entry& entry : kEntries) {
        JSValue native = nativeFunction(ctx, entry.native, std::string(entry.name), entry.length, entry.magic);
        if (!define(ctx, object, entry.name, native, JS_PROP_ENUMERABLE)) {
            JS_FreeValue(ctx, object);
            return JS_EXCEPTION;
        }
    }
    return object;
}

JSValue Stores::object(JSContext* ctx)
{
    if (const auto found = m_objects.find(ctx); found != m_objects.end()) {
        return JS_DupValue(ctx, found->second);
    }
    JSValue made = JS_UNDEFINED;
    if (ScriptServices* services = Engine::of(ctx).services()) {
        if (const auto saved = services->loadStore(Engine::of(ctx).ownerOf(ctx))) {
            made = JS_ParseJSON(ctx, saved->c_str(), saved->size(), "<store>");
            if (JS_IsException(made)) {
                JS_FreeValue(ctx, JS_GetException(ctx));
                made = JS_UNDEFINED;
            }
            if (!JS_IsObject(made) || JS_IsArray(made)) {
                JS_FreeValue(ctx, made);
                made = JS_UNDEFINED;
            }
        }
    }
    if (JS_IsUndefined(made)) {
        made = JS_NewObject(ctx);
        if (JS_IsException(made)) {
            return made;
        }
    }
    m_objects.emplace(ctx, made);
    return JS_DupValue(ctx, made);
}

void Stores::forget(JSContext* ctx)
{
    if (const auto found = m_objects.find(ctx); found != m_objects.end()) {
        JS_FreeValue(ctx, found->second);
        m_objects.erase(found);
    }
}

void Stores::clear()
{
    for (const auto& [ctx, object] : m_objects) {
        JS_FreeValue(ctx, object);
    }
    m_objects.clear();
}

}

namespace gs::script {

std::string valueJson(const VarValue& value)
{
    return std::visit([](const auto& each) { return nlohmann::json(each).dump(); }, value);
}

std::optional<std::uint32_t> androidKeycode(std::string_view name) noexcept
{
    const auto& keys = detail::api::kKeys;
    const auto found = std::ranges::find(keys, name);
    if (found == keys.end()) {
        return std::nullopt;
    }
    return detail::api::kKeycodes[static_cast<std::size_t>(found - keys.begin())];
}

}
