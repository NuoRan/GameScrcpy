#include "MockHost.h"

#include <gs/base/Clock.h>
#include <gs/ext/Archive.h>
#include <gs/ext/Package.h>
#include <gs/exthost/Hosted.h>
#include <gs/exthost/InProcess.h>
#include <gs/exthost/Module.h>
#include <gs/platform/AtomicFile.h>
#include <gs/script/Replay.h>
#include <gs/script/Report.h>
#include <gs/script/Services.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <format>
#include <iterator>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <variant>

namespace gs::cli {

namespace {

namespace fs = std::filesystem;
using Json = MockJson;
using Milliseconds = std::chrono::milliseconds;

constexpr int kPassed = 0;
constexpr int kFailed = 1;
constexpr int kUnloadable = 2;
constexpr std::int64_t kScriptLoadUs = 2'000'000;
constexpr std::size_t kMostBrief = 200;
constexpr double kMostExactInteger = 9'007'199'254'740'991.0;
constexpr std::size_t kMostShownHappenings = 10;
constexpr std::size_t kMostProvidedDevices = 64;
constexpr std::size_t kMostDeviceActions = 8;
constexpr Milliseconds kHostedReady{30'000};
constexpr Milliseconds kCancelGrace{3'000};
constexpr std::array<std::string_view, 4> kDeviceStates{"ready", "busy", "problem", "offline"};
constexpr std::array<std::string_view, 4> kTouchActions{"down", "move", "up", "cancel"};
constexpr std::array<std::string_view, 4> kCommonChecks{"events", "logged", "notified", "devicesChanged"};

struct ActionShape {
    std::string_view name;
    std::vector<std::string_view> inputs;
    std::vector<std::string_view> checks;
};

const std::vector<ActionShape>& actionShapes()
{
    static const std::vector<ActionShape> shapes{
        {"call", {"node", "args"}, {"value", "error"}},
        {"job", {"job", "args", "id", "wait"}, {"result", "error", "progress", "logs", "outputs"}},
        {"job.cancel", {"id"}, {"ended"}},
        {"job.wait", {"id"}, {"result", "error", "progress", "logs", "outputs"}},
        {"touch.open", {"method", "session", "phone", "device", "touchBox", "options"}, {"opened"}},
        {"touch.frame", {"session", "contacts", "rotation"}, {"ok", "sent"}},
        {"touch.key", {"session", "key", "keycode", "down", "repeat", "meta"}, {"ok"}},
        {"touch.text", {"session", "text"}, {"ok"}},
        {"touch.mouse", {"session", "buttons", "dx", "dy", "wheel"}, {"ok"}},
        {"touch.pad",
         {"session", "buttons", "hat", "leftX", "leftY", "rightX", "rightY", "leftTrigger", "rightTrigger"},
         {"ok"}},
        {"touch.close", {"session"}, {}},
        {"devices", {"provider"}, {"devices", "count", "error", "problems"}},
        {"device.act", {"provider", "device", "action"}, {"ok", "error"}},
        {"events", {}, {}},
        {"settings", {"settings", "deviceSettings"}, {"changed"}},
        {"wait", {"ms"}, {}},
        {"holder", {"resource", "by", "yields"}, {}},
        {"holder.release", {"resource"}, {"released"}},
        {"claim.want", {"resource", "by"}, {"held", "yielded", "released"}},
        {"claims", {}, {"held", "holders"}},
        {"scripts", {}, {"loaded", "logs"}},
        {"manifest", {}, {"value"}},
    };
    return shapes;
}

std::string actionList()
{
    std::string text;
    for (const ActionShape& shape : actionShapes()) {
        text += (text.empty() ? "" : ", ") + std::string(shape.name);
    }
    return text;
}

std::string textOf(const fs::path& path)
{
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

fs::path pathFrom(std::string_view text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string cut(std::string text, std::size_t most)
{
    if (text.size() <= most) {
        return text;
    }
    text.resize(most);
    while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0U) == 0x80U) {
        text.pop_back();
    }
    if (!text.empty() && static_cast<unsigned char>(text.back()) >= 0xC0U) {
        text.pop_back();
    }
    return text + "...";
}

std::string brief(const Json& value)
{
    return cut(value.dump(-1, ' ', false, Json::error_handler_t::replace), kMostBrief);
}

Json parsedOr(std::string_view text)
{
    Json parsed = Json::parse(text, nullptr, false);
    return parsed.is_discarded() ? Json(std::string(text)) : parsed;
}

bool nestingWithin(std::string_view text, std::size_t most) noexcept
{
    std::size_t depth = 0;
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
            if (++depth > most) {
                return false;
            }
        } else if ((c == '}' || c == ']') && depth > 0) {
            --depth;
        }
    }
    return true;
}

const Json* member(const Json& object, std::string_view key)
{
    if (!object.is_object()) {
        return nullptr;
    }
    const auto found = object.find(std::string(key));
    return found != object.end() ? &*found : nullptr;
}

std::optional<Milliseconds> waitOf(const Json& value)
{
    if (!value.is_number_integer()) {
        return std::nullopt;
    }
    const std::int64_t given = value.get<std::int64_t>();
    if (given < 1 || given > kMockLongestWait.count()) {
        return std::nullopt;
    }
    return Milliseconds{given};
}

class TestClock final : public Clock
{
public:
    [[nodiscard]] Microseconds now() const noexcept override
    {
        return std::chrono::duration_cast<Microseconds>(std::chrono::steady_clock::now() - m_start) +
               Microseconds{m_ahead.load(std::memory_order_acquire)};
    }

    void advance(Microseconds by) noexcept { m_ahead.fetch_add(by.count(), std::memory_order_acq_rel); }

    [[nodiscard]] std::int64_t milliseconds() const noexcept
    {
        return std::chrono::duration_cast<Milliseconds>(now()).count();
    }

private:
    std::chrono::steady_clock::time_point m_start = std::chrono::steady_clock::now();
    std::atomic<std::int64_t> m_ahead{0};
};

struct Happening {
    std::string kind;
    Json value;
    std::int64_t at = 0;
};

struct Holder {
    std::string resource;
    std::string by;
    bool yields = false;
};

struct Shared {
    TestClock clock;
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<Happening> happened;
    std::map<std::string, Holder> holders;
    std::map<std::string, std::string> held;

    void add(std::string kind, Json value)
    {
        {
            const std::scoped_lock lock(mutex);
            happened.push_back({std::move(kind), std::move(value), clock.milliseconds()});
        }
        changed.notify_all();
    }

    void wake()
    {
        {
            const std::scoped_lock lock(mutex);
        }
        changed.notify_all();
    }
};

std::string resourceKey(std::string_view resource)
{
    constexpr std::string_view kHid = "hid:";
    constexpr std::string_view kDevicePrefix = "\\\\?\\";
    std::string key(resource);
    std::ranges::transform(key, key.begin(),
                           [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    if (key.starts_with(kHid) && std::string_view(key).substr(kHid.size()).starts_with(kDevicePrefix)) {
        key.erase(kHid.size(), kDevicePrefix.size());
    }
    return key;
}

exthost::ClaimResult claimFor(Shared& shared, std::string_view resource, bool wait)
{
    const std::string key = resourceKey(resource);
    exthost::ClaimResult result = exthost::ClaimResult::Claimed;
    Json noted{{"resource", std::string(resource)}, {"wait", wait}};
    {
        const std::scoped_lock lock(shared.mutex);
        const auto holder = shared.holders.find(key);
        if (!shared.held.contains(key) && holder != shared.holders.end()) {
            noted["by"] = holder->second.by;
            if (wait && holder->second.yields) {
                shared.holders.erase(holder);
                shared.held.emplace(key, std::string(resource));
                noted["result"] = "claimed after the holder let go";
            } else {
                result = exthost::ClaimResult::Busy;
                noted["result"] = "busy";
            }
        } else {
            shared.held.emplace(key, std::string(resource));
            noted["result"] = "claimed";
        }
        shared.happened.push_back({"claim", std::move(noted), shared.clock.milliseconds()});
    }
    shared.changed.notify_all();
    return result;
}

void releaseFor(Shared& shared, std::string_view resource)
{
    {
        const std::scoped_lock lock(shared.mutex);
        shared.held.erase(resourceKey(resource));
        shared.happened.push_back({"release", Json{{"resource", std::string(resource)}}, shared.clock.milliseconds()});
    }
    shared.changed.notify_all();
}

void noteLog(Shared& shared, int level, std::string_view text)
{
    shared.add("log", Json{{"level", level}, {"text", std::string(text)}});
}

void noteNotification(Shared& shared, std::string_view title, std::string_view text)
{
    shared.add("notification", Json{{"title", std::string(title)}, {"text", std::string(text)}});
}

void noteEvent(Shared& shared, std::string_view topic, std::string_view json)
{
    shared.add("event", Json{{"topic", std::string(topic)}, {"value", parsedOr(json)}});
}

void noteDevicesChanged(Shared& shared, std::string_view provider)
{
    shared.add("devicesChanged", Json{{"provider", std::string(provider)}});
}

class MockServices final : public exthost::HostServices
{
public:
    explicit MockServices(std::shared_ptr<Shared> shared)
        : m_shared(std::move(shared))
    {
    }

    void log(int level, std::string_view text) override { noteLog(*m_shared, level, text); }

    [[nodiscard]] bool notify(std::string_view title, std::string_view text) override
    {
        noteNotification(*m_shared, title, text);
        return true;
    }

    void emitted(std::string_view topic, std::string_view json) override { noteEvent(*m_shared, topic, json); }

    void devicesChanged(std::string_view provider) override { noteDevicesChanged(*m_shared, provider); }

    [[nodiscard]] exthost::ClaimResult claim(std::string_view resource, bool wait) override
    {
        return claimFor(*m_shared, resource, wait);
    }

    void release(std::string_view resource) override { releaseFor(*m_shared, resource); }

private:
    std::shared_ptr<Shared> m_shared;
};

struct JobRun {
    std::string name;
    std::string returns;
    std::uint64_t id = 0;
    bool cancelled = false;
    std::vector<std::pair<double, std::string>> progress;
    std::vector<std::pair<int, std::string>> logs;
    std::vector<std::string> outputs;
    std::optional<std::pair<bool, std::string>> done;
};

exthost::JobEvents jobEvents(const std::shared_ptr<Shared>& shared, const std::shared_ptr<JobRun>& run,
                             const MockCommandHooks& hooks)
{
    return {.progress =
                [shared, run, tell = hooks.progress](double fraction, std::string text) {
                    {
                        const std::scoped_lock lock(shared->mutex);
                        run->progress.emplace_back(fraction, text);
                    }
                    shared->changed.notify_all();
                    if (tell) {
                        tell(fraction, text);
                    }
                },
            .log =
                [shared, run, tell = hooks.log](int level, std::string text) {
                    {
                        const std::scoped_lock lock(shared->mutex);
                        run->logs.emplace_back(level, text);
                    }
                    shared->changed.notify_all();
                    if (tell) {
                        tell(level, text);
                    }
                },
            .done =
                [shared, run](bool worked, std::string text) {
                    {
                        const std::scoped_lock lock(shared->mutex);
                        run->done.emplace(worked, std::move(text));
                    }
                    shared->changed.notify_all();
                },
            .output =
                [shared, run](std::string json) {
                    {
                        const std::scoped_lock lock(shared->mutex);
                        run->outputs.push_back(std::move(json));
                    }
                    shared->changed.notify_all();
                }};
}

exthost::ValueType declaredType(std::string_view name)
{
    return name.empty() ? exthost::ValueType::None : exthost::valueTypeNamed(name).value_or(exthost::ValueType::None);
}

bool numberAt(const Json& given, std::string_view key, std::size_t position, double& out)
{
    const Json* found = nullptr;
    if (given.is_object()) {
        found = member(given, key);
    } else if (given.is_array() && position < given.size()) {
        found = &given[position];
    }
    if (found == nullptr || !found->is_number()) {
        return false;
    }
    out = found->get<double>();
    return true;
}

std::optional<exthost::Value> valueFrom(const Json& given, exthost::ValueType type, std::string& problem)
{
    exthost::Value made;
    made.type = type;
    switch (type) {
    case exthost::ValueType::Boolean:
        if (!given.is_boolean()) {
            problem = "true or false belongs here";
            return std::nullopt;
        }
        made.flag = given.get<bool>();
        break;
    case exthost::ValueType::Number:
    case exthost::ValueType::Duration:
        if (!given.is_number()) {
            problem = "a number belongs here";
            return std::nullopt;
        }
        made.number = given.get<double>();
        break;
    case exthost::ValueType::String:
    case exthost::ValueType::Color:
        if (!given.is_string()) {
            problem = "a text belongs here";
            return std::nullopt;
        }
        made.text = given.get<std::string>();
        break;
    case exthost::ValueType::Point:
        if ((given.is_array() && given.size() != 2) || !numberAt(given, "x", 0, made.x) ||
            !numberAt(given, "y", 1, made.y)) {
            problem = "a point is {\"x\": 0.5, \"y\": 0.5} or [0.5, 0.5]";
            return std::nullopt;
        }
        break;
    case exthost::ValueType::Region:
        if ((given.is_array() && given.size() != 4) || !numberAt(given, "x", 0, made.x) ||
            !numberAt(given, "y", 1, made.y) || !numberAt(given, "width", 2, made.width) ||
            !numberAt(given, "height", 3, made.height)) {
            problem = "a region is {\"x\", \"y\", \"width\", \"height\"} or [x, y, width, height]";
            return std::nullopt;
        }
        break;
    case exthost::ValueType::Json:
        made.text = given.dump();
        break;
    case exthost::ValueType::None:
        problem = "the manifest gives this parameter no type";
        return std::nullopt;
    }
    if (std::string checked = exthost::checkValue(made, type); !checked.empty()) {
        problem = std::move(checked);
        return std::nullopt;
    }
    return made;
}

Json jsonOf(const exthost::Value& value)
{
    switch (value.type) {
    case exthost::ValueType::None:
        return nullptr;
    case exthost::ValueType::Boolean:
        return value.flag;
    case exthost::ValueType::Number:
    case exthost::ValueType::Duration:
        if (std::trunc(value.number) == value.number && std::abs(value.number) <= kMostExactInteger) {
            return static_cast<std::int64_t>(value.number);
        }
        return value.number;
    case exthost::ValueType::String:
    case exthost::ValueType::Color:
        return value.text;
    case exthost::ValueType::Point:
        return Json{{"x", value.x}, {"y", value.y}};
    case exthost::ValueType::Region:
        return Json{{"x", value.x}, {"y", value.y}, {"width", value.width}, {"height", value.height}};
    case exthost::ValueType::Json:
        return parsedOr(value.text);
    }
    return nullptr;
}

bool matches(const Json& wanted, const Json& actual)
{
    if (wanted.is_object()) {
        if (!actual.is_object()) {
            return false;
        }
        for (const auto& [key, value] : wanted.items()) {
            const Json* found = member(actual, key);
            if (found == nullptr || !matches(value, *found)) {
                return false;
            }
        }
        return true;
    }
    if (wanted.is_array()) {
        if (!actual.is_array() || actual.size() != wanted.size()) {
            return false;
        }
        for (std::size_t index = 0; index < wanted.size(); ++index) {
            if (!matches(wanted[index], actual[index])) {
                return false;
            }
        }
        return true;
    }
    if (wanted.is_number() && actual.is_number()) {
        const double expected = wanted.get<double>();
        const double got = actual.get<double>();
        return std::abs(expected - got) <= 1e-9 * std::max({1.0, std::abs(expected), std::abs(got)});
    }
    return wanted == actual;
}

bool containsText(const Json& value, const Json& part)
{
    return value.is_string() && part.is_string() &&
           value.get_ref<const std::string&>().find(part.get_ref<const std::string&>()) != std::string::npos;
}

template <typename Match>
std::size_t matchedInOrder(const Json& wanted, const Json& actual, Match match)
{
    std::size_t matched = 0;
    for (const Json& each : actual) {
        if (matched < wanted.size() && match(wanted[matched], each)) {
            ++matched;
        }
    }
    return matched;
}

void expectValue(MockStep& step, std::string_view key, const Json& actual)
{
    const Json* wanted = member(step.expect, key);
    if (wanted != nullptr && !matches(*wanted, actual)) {
        step.problems.push_back(std::string(key) + ": expected " + brief(*wanted) + ", got " + brief(actual));
    }
}

void expectError(MockStep& step, const std::string& error)
{
    const Json* wanted = member(step.expect, "error");
    if (wanted == nullptr || (wanted->is_boolean() && !wanted->get<bool>())) {
        if (!error.empty()) {
            step.problems.push_back("expected no error, got " + error);
        }
        return;
    }
    if (wanted->is_boolean()) {
        if (error.empty()) {
            step.problems.push_back("expected an error, got none");
        }
        return;
    }
    if (!wanted->is_string()) {
        step.problems.push_back("expect.error is true, false or a part of the error's text");
        return;
    }
    const std::string& part = wanted->get_ref<const std::string&>();
    if (error.empty()) {
        step.problems.push_back("expected an error with \"" + part + "\", got none");
    } else if (error.find(part) == std::string::npos) {
        step.problems.push_back("expected an error with \"" + part + "\", got " + error);
    }
}

void expectWorked(MockStep& step, std::string_view key, bool actual)
{
    const Json* wanted = member(step.expect, key);
    if (wanted != nullptr && !wanted->is_boolean()) {
        step.problems.push_back("expect." + std::string(key) + " is true or false");
        return;
    }
    const bool expected = wanted == nullptr || wanted->get<bool>();
    if (expected != actual) {
        step.problems.push_back(std::string(key) + ": expected " + (expected ? "true" : "false") + ", got " +
                                (actual ? "true" : "false"));
    }
}

void expectInOrder(MockStep& step, std::string_view key, const Json& actual,
                   const std::function<bool(const Json& wanted, const Json& each)>& match)
{
    const Json* wanted = member(step.expect, key);
    if (wanted == nullptr) {
        return;
    }
    if (!wanted->is_array()) {
        step.problems.push_back("expect." + std::string(key) + " is a list");
        return;
    }
    const std::size_t matched = matchedInOrder(*wanted, actual, match);
    if (matched < wanted->size()) {
        step.problems.push_back(std::string(key) + ": " + brief((*wanted)[matched]) + " did not come in order; got " +
                                brief(actual));
    }
}

bool numericMember(const Json& value, std::string_view key)
{
    const Json* found = member(value, key);
    return found != nullptr && found->is_number();
}

bool fitsType(std::string_view type, const Json& value)
{
    if (type == "boolean") {
        return value.is_boolean();
    }
    if (type == "number" || type == "duration") {
        return value.is_number();
    }
    if (type == "string") {
        return value.is_string();
    }
    if (type == "color") {
        return value.is_string() && exthost::validColor(value.get_ref<const std::string&>());
    }
    if (type == "point") {
        return value.is_object() && numericMember(value, "x") && numericMember(value, "y");
    }
    if (type == "region") {
        return value.is_object() && numericMember(value, "x") && numericMember(value, "y") &&
               numericMember(value, "width") && numericMember(value, "height");
    }
    return true;
}

std::string argumentProblem(const ext::NativeJob& declared, const Json& args)
{
    if (!args.is_object()) {
        return "the arguments have to be an object with a value for each parameter";
    }
    for (const auto& [key, value] : args.items()) {
        if (std::ranges::find(declared.params, key, &ext::NativeParam::name) == declared.params.end()) {
            return "it takes no " + key;
        }
    }
    for (const ext::NativeParam& param : declared.params) {
        const Json* found = member(args, param.name);
        if (found == nullptr) {
            return "it needs " + param.name;
        }
        if (!fitsType(param.type, *found)) {
            return param.name + " has to be a " + param.type;
        }
    }
    return {};
}

std::pair<bool, std::string> checkedResult(const std::string& returns, bool worked, std::string text)
{
    if (!worked) {
        return {false, std::move(text)};
    }
    const Json value = Json::parse(text, nullptr, false);
    if (value.is_discarded()) {
        return {false, "the job gave a result that is not JSON"};
    }
    if (returns.empty()) {
        return {true, "null"};
    }
    if (!fitsType(returns, value)) {
        return {false, "the job gave a result that is not a " + returns};
    }
    return {true, value.dump()};
}

struct SettingJson {
    Json operator()(std::monostate) const { return nullptr; }
    Json operator()(bool value) const { return value; }
    Json operator()(std::int64_t value) const { return value; }
    Json operator()(double value) const { return value; }
    Json operator()(const std::string& value) const { return value; }
};

ext::SettingValue settingValueOf(const Json& value)
{
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    if (value.is_number_integer()) {
        return value.get<std::int64_t>();
    }
    if (value.is_number()) {
        return value.get<double>();
    }
    if (value.is_string()) {
        return value.get<std::string>();
    }
    return std::monostate{};
}

void collectChanged(const Json& from, const Json& to, std::set<std::string>& changed)
{
    for (const auto& [device, values] : from.items()) {
        const Json* other = member(to, device);
        for (const auto& [key, value] : values.items()) {
            const Json* same = other != nullptr ? member(*other, key) : nullptr;
            if (same == nullptr || *same != value) {
                changed.insert(key);
            }
        }
    }
}

std::optional<std::string> textIn(const Json& object, std::string_view key, std::size_t most)
{
    const Json* found = member(object, key);
    if (found == nullptr) {
        return std::string();
    }
    if (!found->is_string() || found->get_ref<const std::string&>().size() > most) {
        return std::nullopt;
    }
    return found->get<std::string>();
}

std::string deviceProblem(const ext::NativeContribution& native, const ext::NativeDeviceProvider& provider,
                          const Json& device)
{
    if (!device.is_object()) {
        return "it is not an object";
    }
    const std::optional<std::string> id = textIn(device, "id", 64);
    const std::optional<std::string> name = textIn(device, "name", 64);
    const std::optional<std::string> kind = textIn(device, "kind", 32);
    const std::optional<std::string> summary = textIn(device, "summary", 200);
    const std::optional<std::string> state = textIn(device, "state", 16);
    const std::optional<std::string> stateText = textIn(device, "stateText", 200);
    const std::optional<std::string> method = textIn(device, "touchMethod", 64);
    const std::optional<std::string> resource = textIn(device, "resource", exthost::kMostResourceBytes);
    if (!resource || (!resource->empty() && !exthost::validResource(*resource))) {
        return "its resource is not the name of hardware such as serial:COM9";
    }
    if (!id || id->empty() || !name || name->empty()) {
        return "it has no id or no name";
    }
    if (!kind || std::ranges::find(provider.kinds, *kind) == provider.kinds.end()) {
        return "its kind is not one the manifest declares";
    }
    if (!state || (!state->empty() && std::ranges::find(kDeviceStates, *state) == kDeviceStates.end()) || !summary ||
        !stateText || !method) {
        return "its state, summary, stateText or touchMethod is not valid text";
    }
    if (!method->empty() &&
        std::ranges::find(native.touchMethods, *method, &ext::NativeTouchMethod::name) == native.touchMethods.end()) {
        return "its touchMethod is not one of the extension's touch methods";
    }
    const Json* actions = member(device, "actions");
    if (actions == nullptr) {
        return {};
    }
    if (!actions->is_array() || actions->size() > kMostDeviceActions) {
        return "its actions are not a list of at most 8";
    }
    for (const Json& action : *actions) {
        const std::optional<std::string> actionId = action.is_object() ? textIn(action, "id", 64) : std::nullopt;
        const std::optional<std::string> title = action.is_object() ? textIn(action, "title", 40) : std::nullopt;
        const std::optional<std::string> panel = action.is_object() ? textIn(action, "panel", 64) : std::nullopt;
        const std::optional<std::string> job = action.is_object() ? textIn(action, "job", 64) : std::nullopt;
        if (!actionId || actionId->empty() || !title || title->empty() || !panel || !job ||
            (!job->empty() && std::ranges::find(native.jobs, *job, &ext::NativeJob::name) == native.jobs.end())) {
            return "an action has no id or title, or names a job the manifest does not declare";
        }
    }
    return {};
}

std::pair<double, double> naturalOf(double x, double y, std::uint32_t rotation)
{
    switch (rotation & 3U) {
    case 1:
        return {1.0 - y, x};
    case 2:
        return {1.0 - x, 1.0 - y};
    case 3:
        return {y, 1.0 - x};
    default:
        return {x, y};
    }
}

Json contactJson(const exthost::Contact& contact)
{
    return Json{{"id", contact.id},
                {"action", std::string(kTouchActions[static_cast<std::size_t>(contact.action)])},
                {"x", contact.x},
                {"y", contact.y},
                {"naturalX", contact.naturalX},
                {"naturalY", contact.naturalY},
                {"pressure", contact.pressure}};
}

bool happeningMatches(std::string_view kind, const Json& wanted, const Json& value)
{
    if (kind == "event") {
        if (wanted.is_string()) {
            return value.at("topic") == wanted;
        }
        const Json* topic = member(wanted, "topic");
        const Json* data = member(wanted, "value");
        return wanted.is_object() && (topic == nullptr || value.at("topic") == *topic) &&
               (data == nullptr || matches(*data, value.at("value")));
    }
    if (kind == "log") {
        if (wanted.is_string()) {
            return containsText(value.at("text"), wanted);
        }
        const Json* level = member(wanted, "level");
        const Json* text = member(wanted, "text");
        return wanted.is_object() && (level == nullptr || value.at("level") == *level) &&
               (text == nullptr || containsText(value.at("text"), *text));
    }
    if (kind == "notification") {
        if (wanted.is_string()) {
            return containsText(value.at("title"), wanted) || containsText(value.at("text"), wanted);
        }
        const Json* title = member(wanted, "title");
        const Json* text = member(wanted, "text");
        return wanted.is_object() && (title == nullptr || containsText(value.at("title"), *title)) &&
               (text == nullptr || containsText(value.at("text"), *text));
    }
    return wanted.is_string() && value.at("provider") == wanted;
}

std::string_view kindOfCheck(std::string_view check)
{
    if (check == "events") {
        return "event";
    }
    if (check == "logged") {
        return "log";
    }
    if (check == "notified") {
        return "notification";
    }
    return "devicesChanged";
}

std::string_view stateName(MockScriptState state) noexcept
{
    switch (state) {
    case MockScriptState::Loaded:
        return "loaded";
    case MockScriptState::Limited:
        return "limited";
    case MockScriptState::Failed:
        return "failed";
    }
    return "failed";
}

Json scriptJson(const MockScript& script)
{
    return Json{{"name", script.name},
                {"run", script.run},
                {"state", std::string(stateName(script.state))},
                {"problem", script.problem},
                {"logs", script.logs},
                {"calls", script.calls}};
}

MockScript loadScript(const fs::path& folder, const ext::ScriptContribution& contribution,
                      const std::vector<std::string>& granted)
{
    MockScript loaded;
    loaded.name = contribution.name;
    loaded.run = std::string(ext::kScriptRunNames[static_cast<std::size_t>(contribution.run)]);
    ManualClock clock;
    script::RecordingServices services(
        clock, script::DeviceInfo{.model = "Mock phone", .android = 14, .width = 1080, .height = 2400, .rotation = 0},
        {}, {});
    services.setServiceHook([](std::string_view service) { return service.starts_with("ui."); },
                            [&loaded](std::string_view service, std::string_view json) {
                                loaded.calls.push_back(std::string(service) + " " + std::string(json));
                                return script::ServiceAnswer{"null", {}, {}};
                            });
    std::string failure;
    const script::ScriptHost::Sink sink = [&loaded, &failure](const script::Report& report) {
        if (report.kind == script::ReportKind::Log) {
            loaded.logs.push_back(report.message);
        } else if (report.kind == script::ReportKind::ScriptFailed && failure.empty()) {
            failure = script::describe(report);
        }
    };
    script::ReplayOptions options;
    options.source.folder = folder;
    options.source.entry = contribution.entry;
    options.source.permissions = granted;
    options.source.phone = contribution.run == ext::ScriptRun::Session;
    options.until = Microseconds{kScriptLoadUs};
    const script::ReplayResult result = script::replay(options, services, clock, sink);
    if (!failure.empty()) {
        const bool offered =
            failure.find("GS-API-070") == std::string::npos && failure.find("GS-API-060") == std::string::npos;
        loaded.state = offered ? MockScriptState::Failed : MockScriptState::Limited;
        loaded.problem = failure;
    } else if (!result.loaded) {
        loaded.state = MockScriptState::Limited;
        loaded.problem = "it was still loading after 2 seconds of test time";
    }
    return loaded;
}

class Session
{
public:
    Session(const MockExtension& extension, const MockOptions& options)
        : m_extension(extension)
        , m_manifest(*extension.manifest)
        , m_options(options)
    {
        for (const ext::Permission& permission : m_manifest.permissions) {
            m_granted.push_back(permission.name);
        }
        for (const ext::SettingDeclaration& setting : m_manifest.settings) {
            m_global[setting.key] = std::visit(SettingJson{}, setting.defaultValue);
        }
    }

    ~Session()
    {
        m_module.reset();
        if (!m_container.empty()) {
            static_cast<void>(exthost::forgetContainer(m_container));
        }
    }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    [[nodiscard]] bool readSpec(const Json& spec);
    [[nodiscard]] bool loadModule(std::size_t number);
    [[nodiscard]] const std::string& specProblem() const noexcept { return m_specProblem; }
    [[nodiscard]] const std::string& moduleProblem() const noexcept { return m_moduleProblem; }
    [[nodiscard]] const std::vector<ext::Diagnostic>& mismatches() const noexcept { return m_mismatches; }
    [[nodiscard]] bool hasModule() const noexcept { return m_module != nullptr; }
    [[nodiscard]] std::string placeText() const;
    [[nodiscard]] MockStep perform(std::size_t index, const Json& given);
    [[nodiscard]] MockCommandResult runCommand(const ext::CliContribution& command, const Json& args,
                                               const MockCommandHooks& hooks);

private:
    struct Touch {
        std::uint64_t id = 0;
        exthost::TouchMethodInfo info;
        std::map<std::uint32_t, std::pair<double, double>> held;
    };

    [[nodiscard]] bool restricted() const noexcept
    {
        return m_manifest.contributes.native && m_manifest.contributes.native->sandbox == ext::NativeSandbox::Restricted;
    }
    void matchModule();
    void dispatch(MockStep& step, Milliseconds wait);
    void expectHappenings(MockStep& step, Milliseconds wait);
    [[nodiscard]] std::string snapshotText() const;
    [[nodiscard]] std::string applySettings(const Json& global, const Json& devices);
    [[nodiscard]] std::string touchOptions(const std::string& phone, const Json& device, const Json& touchBox) const;
    [[nodiscard]] std::string missingPermission(const std::vector<std::string>& needed) const;
    [[nodiscard]] std::string addHolder(const Json& given);
    [[nodiscard]] std::shared_ptr<JobRun> startJob(const std::string& name, const Json& args,
                                                   const MockCommandHooks& hooks, std::string& error);
    void finishJob(MockStep& step, const std::shared_ptr<JobRun>& run, Milliseconds wait);
    [[nodiscard]] Touch* openSession(MockStep& step);
    [[nodiscard]] const ext::NativeDeviceProvider* providerOf(MockStep& step, std::size_t& index);
    void doCall(MockStep& step);
    void doJob(MockStep& step, Milliseconds wait);
    void doCancel(MockStep& step);
    void doWaitJob(MockStep& step, Milliseconds wait);
    void doTouchOpen(MockStep& step);
    void doTouchFrame(MockStep& step);
    void doTouchKey(MockStep& step);
    void doTouchText(MockStep& step);
    void doTouchMouse(MockStep& step);
    void doTouchPad(MockStep& step);
    void doTouchClose(MockStep& step);
    void doDevices(MockStep& step);
    void doAct(MockStep& step);
    void doSettings(MockStep& step);
    void doWait(MockStep& step);
    void doHolder(MockStep& step);
    void doHolderRelease(MockStep& step);
    void doClaimWanted(MockStep& step, Milliseconds wait);
    void doClaims(MockStep& step);
    void doScripts(MockStep& step);
    void doManifest(MockStep& step);

    const MockExtension& m_extension;
    const ext::Manifest& m_manifest;
    MockOptions m_options;
    std::shared_ptr<Shared> m_shared = std::make_shared<Shared>();
    MockServices m_services{m_shared};
    std::vector<std::string> m_granted;
    Json m_global = Json::object();
    Json m_devices = Json::object();
    Milliseconds m_wait = kMockStepWait;
    std::string m_specProblem;
    std::string m_moduleProblem;
    std::vector<ext::Diagnostic> m_mismatches;
    std::vector<std::optional<std::size_t>> m_nodes;
    std::vector<std::optional<std::size_t>> m_jobs;
    std::vector<std::optional<std::size_t>> m_methods;
    std::vector<std::optional<std::size_t>> m_providers;
    std::map<std::string, std::shared_ptr<JobRun>> m_runs;
    std::map<std::string, Touch> m_touches;
    std::map<std::string, std::size_t> m_cursors;
    std::size_t m_unnamed = 0;
    std::string m_container;
    std::unique_ptr<exthost::Module> m_module;
};

std::optional<std::string> requiredText(MockStep& step, std::string_view key, std::string_view what)
{
    const Json* found = member(step.input, key);
    if (found == nullptr || !found->is_string() || found->get_ref<const std::string&>().empty()) {
        step.problems.push_back(step.action + " needs " + std::string(what) + " in \"" + std::string(key) + "\"");
        return std::nullopt;
    }
    return found->get<std::string>();
}

std::optional<std::string> optionalText(MockStep& step, std::string_view key, std::string_view fallback)
{
    const Json* found = member(step.input, key);
    if (found == nullptr) {
        return std::string(fallback);
    }
    if (!found->is_string() || found->get_ref<const std::string&>().empty()) {
        step.problems.push_back("\"" + std::string(key) + "\" is a text");
        return std::nullopt;
    }
    return found->get<std::string>();
}

std::optional<std::int64_t> optionalInteger(MockStep& step, std::string_view key, std::int64_t fallback,
                                            std::int64_t least, std::int64_t most)
{
    const Json* found = member(step.input, key);
    if (found == nullptr) {
        return fallback;
    }
    if (!found->is_number_integer() || found->get<std::int64_t>() < least || found->get<std::int64_t>() > most) {
        step.problems.push_back("\"" + std::string(key) + "\" is a whole number from " + std::to_string(least) +
                                " to " + std::to_string(most));
        return std::nullopt;
    }
    return found->get<std::int64_t>();
}

std::optional<bool> optionalFlag(MockStep& step, std::string_view key, bool fallback)
{
    const Json* found = member(step.input, key);
    if (found == nullptr) {
        return fallback;
    }
    if (!found->is_boolean()) {
        step.problems.push_back("\"" + std::string(key) + "\" is true or false");
        return std::nullopt;
    }
    return found->get<bool>();
}

bool Session::readSpec(const Json& spec)
{
    const auto fail = [this](std::string text) {
        m_specProblem = std::move(text);
        return false;
    };
    if (!spec.is_object()) {
        return fail("a test is a JSON object with a list of steps");
    }
    if (const Json* format = member(spec, "format");
        format != nullptr && (!format->is_string() || format->get_ref<const std::string&>() != kMockTestFormat)) {
        return fail("format is \"" + std::string(kMockTestFormat) + "\" or left out");
    }
    if (const Json* version = member(spec, "version");
        version != nullptr && (!version->is_number_integer() || version->get<std::int64_t>() != kMockTestVersion)) {
        return fail("this gs reads version 1 of the test format");
    }
    const Json* steps = member(spec, "steps");
    if (steps == nullptr || !steps->is_array() || steps->empty() || steps->size() > kMockMostSteps) {
        return fail("steps is a list of 1 to 1000 steps");
    }
    if (const Json* granted = member(spec, "granted"); granted != nullptr) {
        if (!granted->is_array()) {
            return fail("granted is a list of the permissions of the manifest that the test allows");
        }
        m_granted.clear();
        for (const Json& name : *granted) {
            if (!name.is_string() || std::ranges::find(m_manifest.permissions, name.get<std::string>(),
                                                       &ext::Permission::name) == m_manifest.permissions.end()) {
                return fail("granted lists " + brief(name) + ", which the manifest does not ask for");
            }
            m_granted.push_back(name.get<std::string>());
        }
    }
    if (const Json* timeout = member(spec, "timeout"); timeout != nullptr) {
        const std::optional<Milliseconds> read = waitOf(*timeout);
        if (!read) {
            return fail("timeout is a whole number of milliseconds from 1 to 600000");
        }
        m_wait = *read;
    }
    const Json* global = member(spec, "settings");
    const Json* devices = member(spec, "deviceSettings");
    if (std::string problem = applySettings(global != nullptr ? *global : Json::object(),
                                            devices != nullptr ? *devices : Json::object());
        !problem.empty()) {
        return fail(std::move(problem));
    }
    if (const Json* holders = member(spec, "holders"); holders != nullptr) {
        if (!holders->is_array()) {
            return fail("holders is a list of {\"resource\", \"by\", \"yields\"}");
        }
        for (const Json& holder : *holders) {
            if (std::string problem = addHolder(holder); !problem.empty()) {
                return fail(std::move(problem));
            }
        }
    }
    return true;
}

bool Session::loadModule(std::size_t number)
{
    if (!m_manifest.contributes.native) {
        return true;
    }
    const std::string chosen = ext::nativeDllFor(*m_manifest.contributes.native, ext::hostArchitecture());
    if (chosen.empty()) {
        m_moduleProblem = "it has no DLL for " + std::string(ext::hostArchitecture());
        return false;
    }
    std::error_code error;
    const fs::path dll = fs::absolute(m_extension.folder / pathFrom(chosen), error);
    const fs::path stores = m_extension.scratch->path() / "stores";
    fs::create_directories(stores, error);
    const fs::path store = stores / std::format("test-{}.json", number);
    std::string why;
    if (!m_options.hosted && !restricted()) {
        exthost::InProcessOptions given;
        given.dll = dll;
        given.store = store;
        given.granted = m_granted;
        given.services = &m_services;
        given.settings = snapshotText();
        given.clock = &m_shared->clock;
        m_module = exthost::InProcessModule::load(std::move(given), why);
    } else if (std::error_code missing; m_options.exthost.empty() || !fs::is_regular_file(m_options.exthost, missing)) {
        why = "gs-exthost.exe is not at " + textOf(m_options.exthost);
    } else {
        exthost::HostedOptions given;
        given.program = m_options.exthost;
        given.dll = dll;
        given.store = store;
        given.granted = m_granted;
        given.settings = snapshotText();
        const std::shared_ptr<Shared> shared = m_shared;
        given.log = [shared](int level, const std::string& text) { noteLog(*shared, level, text); };
        given.notify = [shared](const std::string& title, const std::string& text) {
            noteNotification(*shared, title, text);
        };
        given.event = [shared](const std::string& topic, const std::string& json) { noteEvent(*shared, topic, json); };
        given.devicesChanged = [shared](const std::string& provider) { noteDevicesChanged(*shared, provider); };
        given.claim = [shared](const std::string& resource, bool wait, std::function<void(exthost::ClaimResult)> answer) {
            answer(claimFor(*shared, resource, wait));
        };
        given.release = [shared](const std::string& resource) { releaseFor(*shared, resource); };
        given.crashed = [shared](std::uint32_t code) { shared->add("crash", Json{{"code", code}}); };
        given.touchLost = [shared](std::uint64_t session) { shared->add("touchLost", Json{{"session", session}}); };
        if (restricted()) {
            m_container = "gs-test:" + m_manifest.id;
            given.container = m_container;
            given.readable = m_extension.folder;
        }
        std::unique_ptr<exthost::HostedModule> hosted = exthost::HostedModule::start(std::move(given), why);
        if (hosted != nullptr && !hosted->waitReady(kHostedReady)) {
            why = "gs-exthost.exe did not get the DLL ready within 30 seconds";
            hosted.reset();
        }
        m_module = std::move(hosted);
    }
    if (m_module == nullptr) {
        m_moduleProblem = chosen + " cannot be loaded: " + why;
        return false;
    }
    matchModule();
    return true;
}

void Session::matchModule()
{
    const ext::NativeContribution& native = *m_manifest.contributes.native;
    const exthost::ModuleInfo& info = m_module->info();
    const auto mismatch = [this](std::string path, const std::string& name) {
        m_mismatches.push_back({ext::Severity::Warning, ext::Code::NativeMismatch, std::move(path), name});
    };
    for (std::size_t index = 0; index < native.nodes.size(); ++index) {
        const ext::NativeNode& node = native.nodes[index];
        const auto found = std::ranges::find(info.nodes, node.name, &exthost::NodeInfo::name);
        bool fits = found != info.nodes.end() && found->returns == declaredType(node.returns) &&
                    found->params.size() == node.params.size();
        for (std::size_t param = 0; fits && param < node.params.size(); ++param) {
            fits = found->params[param] == declaredType(node.params[param].type);
        }
        m_nodes.push_back(fits ? std::optional<std::size_t>(static_cast<std::size_t>(found - info.nodes.begin()))
                               : std::nullopt);
        if (!fits) {
            mismatch("$.contributes.native.nodes[" + std::to_string(index) + "]", node.name);
        }
    }
    for (std::size_t index = 0; index < native.touchMethods.size(); ++index) {
        const std::string& name = native.touchMethods[index].name;
        const auto found = std::ranges::find(info.touchMethods, name, &exthost::TouchMethodInfo::name);
        m_methods.push_back(found != info.touchMethods.end()
                                ? std::optional<std::size_t>(static_cast<std::size_t>(found - info.touchMethods.begin()))
                                : std::nullopt);
        if (!m_methods.back()) {
            mismatch("$.contributes.native.touchMethods[" + std::to_string(index) + "]", name);
        }
    }
    for (std::size_t index = 0; index < native.jobs.size(); ++index) {
        const std::string& name = native.jobs[index].name;
        const auto found = std::ranges::find(info.jobs, name, &exthost::JobInfo::name);
        m_jobs.push_back(found != info.jobs.end()
                             ? std::optional<std::size_t>(static_cast<std::size_t>(found - info.jobs.begin()))
                             : std::nullopt);
        if (!m_jobs.back()) {
            mismatch("$.contributes.native.jobs[" + std::to_string(index) + "]", name);
        }
    }
    for (std::size_t index = 0; index < native.deviceProviders.size(); ++index) {
        const std::string& name = native.deviceProviders[index].name;
        const auto found = std::ranges::find(info.providers, name, &exthost::ProviderInfo::name);
        m_providers.push_back(found != info.providers.end()
                                  ? std::optional<std::size_t>(static_cast<std::size_t>(found - info.providers.begin()))
                                  : std::nullopt);
        if (!m_providers.back()) {
            mismatch("$.contributes.native.deviceProviders[" + std::to_string(index) + "]", name);
        }
    }
}

std::string Session::placeText() const
{
    if (!m_manifest.contributes.native) {
        return "no native DLL";
    }
    return ext::nativeDllFor(*m_manifest.contributes.native, ext::hostArchitecture()) +
           (restricted() ? " restricted in gs-exthost.exe"
                         : (m_options.hosted ? " in gs-exthost.exe" : " in this process"));
}

std::string Session::snapshotText() const
{
    return Json{{"global", m_global}, {"devices", m_devices}}.dump();
}

std::string Session::applySettings(const Json& global, const Json& devices)
{
    if (!global.is_object() || !devices.is_object()) {
        return "settings is an object of setting keys and values, and deviceSettings an object of phones and such "
               "objects";
    }
    Json nextGlobal = m_global;
    Json nextDevices = m_devices;
    for (const auto& [key, value] : global.items()) {
        const auto setting = std::ranges::find(m_manifest.settings, key, &ext::SettingDeclaration::key);
        if (setting == m_manifest.settings.end()) {
            return "the manifest declares no setting " + key;
        }
        if (!ext::settingAccepts(*setting, settingValueOf(value))) {
            return brief(value) + " is not a value of the setting " + key;
        }
        nextGlobal[key] = value;
    }
    for (const auto& [phone, values] : devices.items()) {
        if (!values.is_object()) {
            return "the settings of the phone " + phone + " are an object of setting keys and values";
        }
        for (const auto& [key, value] : values.items()) {
            const auto setting = std::ranges::find(m_manifest.settings, key, &ext::SettingDeclaration::key);
            if (setting == m_manifest.settings.end() || setting->scope != ext::SettingScope::Device) {
                return key + " is not a setting of the manifest with scope \"device\"";
            }
            if (value.is_null()) {
                if (nextDevices.contains(phone) && nextDevices[phone].is_object()) {
                    nextDevices[phone].erase(key);
                    if (nextDevices[phone].empty()) {
                        nextDevices.erase(phone);
                    }
                }
                continue;
            }
            if (!ext::settingAccepts(*setting, settingValueOf(value))) {
                return brief(value) + " is not a value of the setting " + key;
            }
            nextDevices[phone][key] = value;
        }
    }
    m_global = std::move(nextGlobal);
    m_devices = std::move(nextDevices);
    return {};
}

std::string Session::touchOptions(const std::string& phone, const Json& device, const Json& touchBox) const
{
    const Json* own = member(m_devices, phone);
    return Json{{"settings", m_global},
                {"deviceSettings", own != nullptr ? *own : Json::object()},
                {"phone", phone},
                {"device", device},
                {"touchBox", touchBox}}
        .dump();
}

std::string Session::missingPermission(const std::vector<std::string>& needed) const
{
    for (const std::string& permission : needed) {
        if (std::ranges::find(m_granted, permission) == m_granted.end()) {
            return permission;
        }
    }
    return {};
}

std::string Session::addHolder(const Json& given)
{
    if (!given.is_object()) {
        return "a holder is {\"resource\": \"serial:COM9\", \"by\": \"another program\", \"yields\": false}";
    }
    const Json* resource = member(given, "resource");
    if (resource == nullptr || !resource->is_string() ||
        !exthost::validResource(resource->get_ref<const std::string&>())) {
        return "a holder needs a resource such as serial:COM9, ble:AA:BB:CC:DD:EE:FF, hid:<path> or "
               "usb:<vid>:<pid>:<serial>";
    }
    std::string by = "another program";
    if (const Json* who = member(given, "by"); who != nullptr) {
        if (!who->is_string() || who->get_ref<const std::string&>().empty()) {
            return "by is the name of whoever holds the resource";
        }
        by = who->get<std::string>();
    }
    bool yields = false;
    if (const Json* yielding = member(given, "yields"); yielding != nullptr) {
        if (!yielding->is_boolean()) {
            return "yields is true or false";
        }
        yields = yielding->get<bool>();
    }
    const std::string key = resourceKey(resource->get_ref<const std::string&>());
    {
        const std::scoped_lock lock(m_shared->mutex);
        if (m_shared->held.contains(key)) {
            return "the extension holds " + resource->get<std::string>() + "; a claim.want step asks it to let go";
        }
        m_shared->holders[key] = Holder{.resource = resource->get<std::string>(), .by = std::move(by), .yields = yields};
    }
    return {};
}

MockStep Session::perform(std::size_t index, const Json& given)
{
    MockStep step;
    step.index = index;
    step.at = m_shared->clock.milliseconds();
    if (!given.is_object()) {
        step.problems.push_back("a step is an object such as {\"do\": \"call\", \"node\": \"add\", \"args\": [1, 2]}");
        return step;
    }
    for (const auto& [key, value] : given.items()) {
        if (key != "do" && key != "name" && key != "expect" && key != "timeout") {
            step.input[key] = value;
        }
    }
    const Json* action = member(given, "do");
    if (action == nullptr || !action->is_string()) {
        step.problems.push_back("a step says what it does in \"do\": " + actionList());
        return step;
    }
    step.action = action->get<std::string>();
    if (const Json* label = member(given, "name"); label != nullptr) {
        if (!label->is_string()) {
            step.problems.push_back("name is a text");
            return step;
        }
        step.label = label->get<std::string>();
    }
    if (const Json* expect = member(given, "expect"); expect != nullptr) {
        if (!expect->is_object()) {
            step.problems.push_back("expect is an object");
            return step;
        }
        step.expect = *expect;
    }
    Milliseconds wait = m_wait;
    if (const Json* timeout = member(given, "timeout"); timeout != nullptr) {
        const std::optional<Milliseconds> read = waitOf(*timeout);
        if (!read) {
            step.problems.push_back("timeout is a whole number of milliseconds from 1 to 600000");
            return step;
        }
        wait = *read;
    }
    const auto shape = std::ranges::find(actionShapes(), std::string_view(step.action), &ActionShape::name);
    if (shape == actionShapes().end()) {
        step.problems.push_back("there is no step \"" + step.action + "\"; the steps are " + actionList());
        return step;
    }
    for (const auto& [key, value] : step.input.items()) {
        if (std::ranges::find(shape->inputs, std::string_view(key)) == shape->inputs.end()) {
            step.problems.push_back(step.action + " takes no \"" + key + "\"");
        }
    }
    for (const auto& [key, value] : step.expect.items()) {
        if (std::ranges::find(shape->checks, std::string_view(key)) == shape->checks.end() &&
            std::ranges::find(kCommonChecks, std::string_view(key)) == kCommonChecks.end()) {
            step.problems.push_back(step.action + " cannot check \"" + key + "\"");
        }
    }
    if (!step.problems.empty()) {
        return step;
    }
    std::size_t before = 0;
    {
        const std::scoped_lock lock(m_shared->mutex);
        before = m_shared->happened.size();
    }
    const std::size_t eventsFrom = m_cursors["event"];
    dispatch(step, wait);
    expectHappenings(step, wait);
    const std::scoped_lock lock(m_shared->mutex);
    if (step.action == "events") {
        Json events = Json::array();
        for (std::size_t position = eventsFrom; position < m_shared->happened.size(); ++position) {
            if (m_shared->happened[position].kind == "event") {
                events.push_back(m_shared->happened[position].value);
            }
        }
        step.output["events"] = std::move(events);
        m_cursors["event"] = m_shared->happened.size();
    }
    Json happened = Json::array();
    for (std::size_t position = before; position < m_shared->happened.size(); ++position) {
        const Happening& entry = m_shared->happened[position];
        happened.push_back(Json{{"at", entry.at}, {"kind", entry.kind}, {"value", entry.value}});
    }
    if (!happened.empty()) {
        step.output["happened"] = std::move(happened);
    }
    return step;
}

void Session::dispatch(MockStep& step, Milliseconds wait)
{
    const std::string& action = step.action;
    if (action == "call") {
        doCall(step);
    } else if (action == "job") {
        doJob(step, wait);
    } else if (action == "job.cancel") {
        doCancel(step);
    } else if (action == "job.wait") {
        doWaitJob(step, wait);
    } else if (action == "touch.open") {
        doTouchOpen(step);
    } else if (action == "touch.frame") {
        doTouchFrame(step);
    } else if (action == "touch.key") {
        doTouchKey(step);
    } else if (action == "touch.text") {
        doTouchText(step);
    } else if (action == "touch.mouse") {
        doTouchMouse(step);
    } else if (action == "touch.pad") {
        doTouchPad(step);
    } else if (action == "touch.close") {
        doTouchClose(step);
    } else if (action == "devices") {
        doDevices(step);
    } else if (action == "device.act") {
        doAct(step);
    } else if (action == "settings") {
        doSettings(step);
    } else if (action == "wait") {
        doWait(step);
    } else if (action == "holder") {
        doHolder(step);
    } else if (action == "holder.release") {
        doHolderRelease(step);
    } else if (action == "claim.want") {
        doClaimWanted(step, wait);
    } else if (action == "claims") {
        doClaims(step);
    } else if (action == "scripts") {
        doScripts(step);
    } else if (action == "manifest") {
        doManifest(step);
    }
}

void Session::expectHappenings(MockStep& step, Milliseconds wait)
{
    for (const std::string_view check : kCommonChecks) {
        const Json* wanted = member(step.expect, check);
        if (wanted == nullptr) {
            continue;
        }
        if (!wanted->is_array()) {
            step.problems.push_back("expect." + std::string(check) + " is a list");
            continue;
        }
        const std::string kind(kindOfCheck(check));
        std::size_t next = m_cursors[kind];
        std::size_t matched = 0;
        const auto deadline = std::chrono::steady_clock::now() + wait;
        std::unique_lock lock(m_shared->mutex);
        const auto scan = [&] {
            while (matched < wanted->size() && next < m_shared->happened.size()) {
                const Happening& entry = m_shared->happened[next++];
                if (entry.kind == kind && happeningMatches(kind, (*wanted)[matched], entry.value)) {
                    ++matched;
                }
            }
        };
        for (;;) {
            scan();
            if (matched == wanted->size()) {
                break;
            }
            if (m_shared->changed.wait_until(lock, deadline) == std::cv_status::timeout) {
                scan();
                break;
            }
        }
        m_cursors[kind] = next;
        if (matched < wanted->size()) {
            step.problems.push_back(std::string(check) + ": " + brief((*wanted)[matched]) + " did not come within " +
                                    std::to_string(wait.count()) + " ms");
        }
    }
}

void Session::doCall(MockStep& step)
{
    const std::optional<std::string> node = requiredText(step, "node", "the node's name");
    if (!node) {
        return;
    }
    const Json* given = member(step.input, "args");
    const Json args = given != nullptr ? *given : Json::array();
    if (!args.is_array()) {
        step.problems.push_back("args is the list of the node's values in the order of the manifest");
        return;
    }
    std::string error;
    Json value;
    const std::vector<ext::NativeNode> none;
    const std::vector<ext::NativeNode>& nodes = m_manifest.contributes.native ? m_manifest.contributes.native->nodes : none;
    const auto found = std::ranges::find(nodes, *node, &ext::NativeNode::name);
    const auto index = static_cast<std::size_t>(found - nodes.begin());
    if (m_module == nullptr) {
        error = "GS-API-060 " + m_manifest.name + " has no native DLL";
    } else if (found == nodes.end() || index >= m_nodes.size() || !m_nodes[index]) {
        error = "GS-API-060 " + m_manifest.name + " does not offer this node";
    } else if (const std::string missing = missingPermission(found->permissions); !missing.empty()) {
        error = "GS-API-063 it needs the permission \"" + missing + "\", which the test does not grant";
    } else if (args.size() != found->params.size()) {
        error = "GS-API-061 the node takes " + std::to_string(found->params.size()) + " values, not " +
                std::to_string(args.size());
    } else {
        std::vector<exthost::Value> values;
        for (std::size_t param = 0; param < args.size() && error.empty(); ++param) {
            std::string problem;
            const std::optional<exthost::Value> made =
                valueFrom(args[param], declaredType(found->params[param].type), problem);
            if (made) {
                values.push_back(*made);
            } else {
                error = "GS-API-061 value " + std::to_string(param + 1) + ": " + problem;
            }
        }
        if (error.empty()) {
            const exthost::CallResult result = m_module->call(*m_nodes[index], values);
            switch (result.status) {
            case exthost::CallStatus::Ok:
                value = jsonOf(result.value);
                break;
            case exthost::CallStatus::BadArgument:
                error = "GS-API-061 " + result.problem;
                break;
            case exthost::CallStatus::TooSlow:
                error = "GS-API-064 " + result.problem;
                break;
            case exthost::CallStatus::Unavailable:
                error = "GS-API-060 " + result.problem;
                break;
            case exthost::CallStatus::Failed:
            case exthost::CallStatus::Crashed:
                error = "GS-API-062 " + result.problem;
                break;
            }
        }
    }
    if (error.empty()) {
        step.output["value"] = value;
    } else {
        step.output["error"] = error;
    }
    expectError(step, error);
    if (error.empty()) {
        expectValue(step, "value", value);
    }
}

std::shared_ptr<JobRun> Session::startJob(const std::string& name, const Json& args, const MockCommandHooks& hooks,
                                          std::string& error)
{
    if (m_module == nullptr) {
        error = "GS-API-060 " + m_manifest.name + " has no native DLL";
        return nullptr;
    }
    const std::vector<ext::NativeJob>& jobs = m_manifest.contributes.native->jobs;
    const auto found = std::ranges::find(jobs, name, &ext::NativeJob::name);
    const auto index = static_cast<std::size_t>(found - jobs.begin());
    if (found == jobs.end() || index >= m_jobs.size() || !m_jobs[index]) {
        error = "GS-API-060 " + m_manifest.name + " does not offer this job";
        return nullptr;
    }
    if (const std::string missing = missingPermission(found->permissions); !missing.empty()) {
        error = "GS-API-063 it needs the permission \"" + missing + "\", which the test does not grant";
        return nullptr;
    }
    if (const std::string problem = argumentProblem(*found, args); !problem.empty()) {
        error = "GS-API-061 " + problem;
        return nullptr;
    }
    const std::int64_t most = found->most.value_or(ext::kMostRunningNativeJobs);
    std::int64_t same = 0;
    {
        const std::scoped_lock lock(m_shared->mutex);
        for (const auto& entry : m_runs) {
            if (entry.second->name == name && !entry.second->done) {
                ++same;
            }
        }
    }
    if (same >= most) {
        error = "GS-API-067 " + m_manifest.name + " runs " + std::to_string(most) + " of this job at most";
        return nullptr;
    }
    auto run = std::make_shared<JobRun>();
    run->name = name;
    run->returns = found->returns;
    const std::uint64_t id = m_module->startJob(*m_jobs[index], args.dump(), jobEvents(m_shared, run, hooks));
    if (id == 0) {
        error = "GS-API-067 " + m_manifest.name + " runs as many of its jobs as it may; wait for one to end";
        return nullptr;
    }
    {
        const std::scoped_lock lock(m_shared->mutex);
        run->id = id;
    }
    return run;
}

void Session::finishJob(MockStep& step, const std::shared_ptr<JobRun>& run, Milliseconds wait)
{
    std::unique_lock lock(m_shared->mutex);
    const bool ended = m_shared->changed.wait_for(lock, wait, [&run] { return run->done.has_value(); });
    Json progress = Json::array();
    for (const auto& [fraction, text] : run->progress) {
        progress.push_back(Json{{"fraction", fraction}, {"text", text}});
    }
    Json logs = Json::array();
    for (const auto& [level, text] : run->logs) {
        logs.push_back(Json{{"level", level}, {"text", text}});
    }
    Json outputs = Json::array();
    for (const std::string& each : run->outputs) {
        outputs.push_back(parsedOr(each));
    }
    const std::optional<std::pair<bool, std::string>> done = run->done;
    const bool cancelled = run->cancelled;
    lock.unlock();
    std::string error;
    Json result;
    if (!ended || !done) {
        step.problems.push_back("the job did not end within " + std::to_string(wait.count()) + " ms");
    } else {
        auto [fine, said] = checkedResult(run->returns, done->first, done->second);
        if (fine) {
            result = parsedOr(said);
        } else {
            error = (cancelled ? "GS-API-066 " : "GS-API-065 ") + said;
        }
    }
    if (ended) {
        if (error.empty()) {
            step.output["result"] = result;
        } else {
            step.output["error"] = error;
        }
    }
    step.output["progress"] = progress;
    step.output["logs"] = logs;
    step.output["outputs"] = outputs;
    if (ended) {
        expectError(step, error);
        if (error.empty()) {
            expectValue(step, "result", result);
        }
    }
    expectInOrder(step, "progress", progress, [](const Json& wanted, const Json& each) {
        if (wanted.is_number()) {
            return std::abs(wanted.get<double>() - each.at("fraction").get<double>()) <= 1e-6;
        }
        if (wanted.is_string()) {
            return containsText(each.at("text"), wanted);
        }
        const Json* fraction = member(wanted, "fraction");
        const Json* text = member(wanted, "text");
        return wanted.is_object() &&
               (fraction == nullptr ||
                (fraction->is_number() &&
                 std::abs(fraction->get<double>() - each.at("fraction").get<double>()) <= 1e-6)) &&
               (text == nullptr || containsText(each.at("text"), *text));
    });
    expectInOrder(step, "logs", logs, [](const Json& wanted, const Json& each) {
        if (wanted.is_string()) {
            return containsText(each.at("text"), wanted);
        }
        const Json* level = member(wanted, "level");
        const Json* text = member(wanted, "text");
        return wanted.is_object() && (level == nullptr || *level == each.at("level")) &&
               (text == nullptr || containsText(each.at("text"), *text));
    });
    expectInOrder(step, "outputs", outputs, [](const Json& wanted, const Json& each) { return matches(wanted, each); });
}

void Session::doJob(MockStep& step, Milliseconds wait)
{
    const std::optional<std::string> name = requiredText(step, "job", "the job's name");
    if (!name) {
        return;
    }
    const Json* given = member(step.input, "args");
    const Json args = given != nullptr ? *given : Json::object();
    const std::optional<bool> blocking = optionalFlag(step, "wait", true);
    const bool named = member(step.input, "id") != nullptr;
    const std::optional<std::string> chosen = optionalText(step, "id", "#" + std::to_string(m_unnamed + 1));
    if (!blocking || !chosen) {
        return;
    }
    const std::string label = *chosen;
    if (m_runs.contains(label)) {
        step.problems.push_back("a job with the id \"" + label + "\" ran already in this test");
        return;
    }
    if (!*blocking && (member(step.expect, "result") != nullptr || member(step.expect, "progress") != nullptr ||
                       member(step.expect, "logs") != nullptr || member(step.expect, "outputs") != nullptr)) {
        step.problems.push_back("a job that is not waited for has no result yet; check it in a job.wait step");
        return;
    }
    std::string error;
    const std::shared_ptr<JobRun> run = startJob(*name, args, MockCommandHooks{}, error);
    if (run == nullptr) {
        step.output["error"] = error;
        expectError(step, error);
        return;
    }
    if (!named) {
        ++m_unnamed;
    }
    m_runs[label] = run;
    step.output["id"] = label;
    if (!*blocking) {
        step.output["started"] = true;
        expectError(step, {});
        return;
    }
    finishJob(step, run, wait);
}

void Session::doCancel(MockStep& step)
{
    const std::optional<std::string> label = requiredText(step, "id", "the id of a job started with \"wait\": false");
    if (!label) {
        return;
    }
    const auto found = m_runs.find(*label);
    if (found == m_runs.end()) {
        step.problems.push_back("there is no job with the id \"" + *label + "\" in this test");
        return;
    }
    bool ended = false;
    std::uint64_t id = 0;
    {
        const std::scoped_lock lock(m_shared->mutex);
        ended = found->second->done.has_value();
        if (!ended) {
            found->second->cancelled = true;
        }
        id = found->second->id;
    }
    if (!ended && m_module != nullptr) {
        m_module->cancelJob(id);
    }
    step.output["ended"] = ended;
    expectValue(step, "ended", ended);
}

void Session::doWaitJob(MockStep& step, Milliseconds wait)
{
    const std::optional<std::string> label = requiredText(step, "id", "the id of a job started with \"wait\": false");
    if (!label) {
        return;
    }
    const auto found = m_runs.find(*label);
    if (found == m_runs.end()) {
        step.problems.push_back("there is no job with the id \"" + *label + "\" in this test");
        return;
    }
    finishJob(step, found->second, wait);
}

Session::Touch* Session::openSession(MockStep& step)
{
    const std::optional<std::string> label = optionalText(step, "session", "main");
    if (!label) {
        return nullptr;
    }
    const auto found = m_touches.find(*label);
    if (found == m_touches.end() || m_module == nullptr) {
        step.problems.push_back("there is no open touch session \"" + *label + "\"; open it with touch.open");
        return nullptr;
    }
    return &found->second;
}

void Session::doTouchOpen(MockStep& step)
{
    const std::optional<std::string> method = requiredText(step, "method", "the touch method's name");
    const std::optional<std::string> label = optionalText(step, "session", "main");
    const std::optional<std::string> phone = optionalText(step, "phone", kMockPhone);
    if (!method || !label || !phone) {
        return;
    }
    if (m_touches.contains(*label)) {
        step.problems.push_back("the touch session \"" + *label + "\" is open; close it first");
        return;
    }
    const std::vector<ext::NativeTouchMethod> none;
    const std::vector<ext::NativeTouchMethod>& methods =
        m_manifest.contributes.native ? m_manifest.contributes.native->touchMethods : none;
    const auto found = std::ranges::find(methods, *method, &ext::NativeTouchMethod::name);
    const auto index = static_cast<std::size_t>(found - methods.begin());
    if (m_module == nullptr || found == methods.end() || index >= m_methods.size() || !m_methods[index]) {
        step.problems.push_back(m_manifest.name + " has no touch method " + *method);
        return;
    }
    std::string options;
    if (const Json* given = member(step.input, "options"); given != nullptr) {
        if (!given->is_object()) {
            step.problems.push_back("options is the object the touch method gets in place of the usual one");
            return;
        }
        options = given->dump();
    } else {
        const Json* device = member(step.input, "device");
        const Json* box = member(step.input, "touchBox");
        if ((device != nullptr && !device->is_object() && !device->is_null()) ||
            (box != nullptr && !box->is_string() && !box->is_null())) {
            step.problems.push_back("device is an object { kind, path, name } and touchBox a device id");
            return;
        }
        options = touchOptions(*phone, device != nullptr ? *device : Json(nullptr), box != nullptr ? *box : Json(nullptr));
    }
    const std::uint64_t id = m_module->openTouch(*m_methods[index], *phone, options);
    step.output["opened"] = id != 0;
    step.output["options"] = parsedOr(options);
    if (id != 0) {
        m_touches[*label] = Touch{.id = id, .info = m_module->info().touchMethods[*m_methods[index]], .held = {}};
    }
    expectWorked(step, "opened", id != 0);
}

void Session::doTouchFrame(MockStep& step)
{
    Touch* touch = openSession(step);
    const std::optional<std::int64_t> rotation = optionalInteger(step, "rotation", 0, 0, 3);
    if (touch == nullptr || !rotation) {
        return;
    }
    const Json* contacts = member(step.input, "contacts");
    if (contacts == nullptr || !contacts->is_array() || contacts->size() > exthost::kMostContacts) {
        step.problems.push_back("contacts is a list of up to 32 fingers {\"id\", \"action\", \"x\", \"y\"}");
        return;
    }
    exthost::TouchFrame frame;
    frame.rotation = static_cast<std::uint32_t>(*rotation);
    Json sent = Json::array();
    for (const Json& given : *contacts) {
        const Json* finger = member(given, "id");
        const Json* action = member(given, "action");
        const Json* x = member(given, "x");
        const Json* y = member(given, "y");
        const auto named = action != nullptr && action->is_string()
                               ? std::ranges::find(kTouchActions, std::string_view(action->get_ref<const std::string&>()))
                               : kTouchActions.end();
        if (finger == nullptr || !finger->is_number_integer() || finger->get<std::int64_t>() < 0 ||
            finger->get<std::int64_t>() >= static_cast<std::int64_t>(exthost::kMostContacts) ||
            named == kTouchActions.end() || x == nullptr || !x->is_number() || y == nullptr || !y->is_number()) {
            step.problems.push_back("a contact is {\"id\": 0 to 31, \"action\": \"down\", \"move\", \"up\" or "
                                    "\"cancel\", \"x\": 0 to 1, \"y\": 0 to 1}");
            return;
        }
        exthost::Contact contact;
        contact.id = static_cast<std::uint32_t>(finger->get<std::int64_t>());
        contact.action = static_cast<exthost::TouchAction>(named - kTouchActions.begin());
        contact.x = x->get<double>();
        contact.y = y->get<double>();
        if (contact.action == exthost::TouchAction::Cancel && !touch->info.canCancel) {
            contact.action = exthost::TouchAction::Up;
            if (const auto held = touch->held.find(contact.id); held != touch->held.end()) {
                contact.x = held->second.first;
                contact.y = held->second.second;
            }
        }
        const bool lifting = contact.action == exthost::TouchAction::Up || contact.action == exthost::TouchAction::Cancel;
        const Json* pressure = member(given, "pressure");
        contact.pressure = pressure != nullptr && pressure->is_number() ? pressure->get<double>() : (lifting ? 0.0 : 1.0);
        const auto [naturalX, naturalY] = naturalOf(contact.x, contact.y, frame.rotation);
        const Json* givenX = member(given, "naturalX");
        const Json* givenY = member(given, "naturalY");
        contact.naturalX = givenX != nullptr && givenX->is_number() ? givenX->get<double>() : naturalX;
        contact.naturalY = givenY != nullptr && givenY->is_number() ? givenY->get<double>() : naturalY;
        if (lifting) {
            touch->held.erase(contact.id);
        } else {
            touch->held[contact.id] = {contact.x, contact.y};
        }
        if (frame.contacts.size() < static_cast<std::size_t>(touch->info.maxContacts)) {
            sent.push_back(contactJson(contact));
        }
        frame.contacts.push_back(contact);
    }
    const bool ok = m_module->touch(touch->id, frame);
    step.output["ok"] = ok;
    step.output["sent"] = sent;
    expectWorked(step, "ok", ok);
    expectValue(step, "sent", sent);
}

void Session::doTouchKey(MockStep& step)
{
    Touch* touch = openSession(step);
    const std::optional<bool> down = optionalFlag(step, "down", true);
    const std::optional<std::int64_t> repeat = optionalInteger(step, "repeat", 0, 0, 1'000'000);
    const std::optional<std::int64_t> meta = optionalInteger(step, "meta", 0, 0, 0x7FFFFFFF);
    if (touch == nullptr || !down || !repeat || !meta) {
        return;
    }
    std::uint32_t keycode = 0;
    if (const Json* name = member(step.input, "key"); name != nullptr) {
        const std::optional<std::uint32_t> code =
            name->is_string() ? script::androidKeycode(name->get_ref<const std::string&>()) : std::nullopt;
        if (!code) {
            step.problems.push_back("key is the name of an Android key, as in key.press(), such as \"back\"");
            return;
        }
        keycode = *code;
    } else {
        if (member(step.input, "keycode") == nullptr) {
            step.problems.push_back("touch.key needs \"key\" or \"keycode\"");
            return;
        }
        const std::optional<std::int64_t> given = optionalInteger(step, "keycode", 0, 0, 0xFFFF);
        if (!given) {
            return;
        }
        keycode = static_cast<std::uint32_t>(*given);
    }
    const bool ok = m_module->key(touch->id, exthost::KeyEvent{.keycode = keycode,
                                                               .down = *down,
                                                               .repeat = static_cast<std::uint32_t>(*repeat),
                                                               .meta = static_cast<std::uint32_t>(*meta)});
    step.output["ok"] = ok;
    expectWorked(step, "ok", ok);
}

void Session::doTouchText(MockStep& step)
{
    Touch* touch = openSession(step);
    const std::optional<std::string> text = requiredText(step, "text", "the text to type");
    if (touch == nullptr || !text) {
        return;
    }
    const bool ok = m_module->text(touch->id, *text);
    step.output["ok"] = ok;
    expectWorked(step, "ok", ok);
}

void Session::doTouchMouse(MockStep& step)
{
    Touch* touch = openSession(step);
    const std::optional<std::int64_t> buttons = optionalInteger(step, "buttons", 0, 0, 7);
    const std::optional<std::int64_t> dx = optionalInteger(step, "dx", 0, -1'000'000, 1'000'000);
    const std::optional<std::int64_t> dy = optionalInteger(step, "dy", 0, -1'000'000, 1'000'000);
    const std::optional<std::int64_t> wheel = optionalInteger(step, "wheel", 0, -1000, 1000);
    if (touch == nullptr || !buttons || !dx || !dy || !wheel) {
        return;
    }
    const bool ok = m_module->mouse(touch->id, exthost::MouseEvent{.buttons = static_cast<std::uint32_t>(*buttons),
                                                                   .dx = static_cast<std::int32_t>(*dx),
                                                                   .dy = static_cast<std::int32_t>(*dy),
                                                                   .wheel = static_cast<std::int32_t>(*wheel)});
    step.output["ok"] = ok;
    expectWorked(step, "ok", ok);
}

void Session::doTouchPad(MockStep& step)
{
    Touch* touch = openSession(step);
    const std::optional<std::int64_t> buttons = optionalInteger(step, "buttons", 0, 0, 0x1FFFF);
    const std::optional<std::int64_t> hat = optionalInteger(step, "hat", exthost::kPadHatCentred, 0, 8);
    const std::optional<std::int64_t> leftX = optionalInteger(step, "leftX", 0, -32768, 32767);
    const std::optional<std::int64_t> leftY = optionalInteger(step, "leftY", 0, -32768, 32767);
    const std::optional<std::int64_t> rightX = optionalInteger(step, "rightX", 0, -32768, 32767);
    const std::optional<std::int64_t> rightY = optionalInteger(step, "rightY", 0, -32768, 32767);
    const std::optional<std::int64_t> leftTrigger = optionalInteger(step, "leftTrigger", 0, 0, 255);
    const std::optional<std::int64_t> rightTrigger = optionalInteger(step, "rightTrigger", 0, 0, 255);
    if (touch == nullptr || !buttons || !hat || !leftX || !leftY || !rightX || !rightY || !leftTrigger ||
        !rightTrigger) {
        return;
    }
    const bool ok = m_module->gamepad(touch->id, exthost::PadEvent{.buttons = static_cast<std::uint32_t>(*buttons),
                                                                   .hat = static_cast<std::uint32_t>(*hat),
                                                                   .leftX = static_cast<std::int32_t>(*leftX),
                                                                   .leftY = static_cast<std::int32_t>(*leftY),
                                                                   .rightX = static_cast<std::int32_t>(*rightX),
                                                                   .rightY = static_cast<std::int32_t>(*rightY),
                                                                   .leftTrigger = static_cast<std::uint32_t>(*leftTrigger),
                                                                   .rightTrigger = static_cast<std::uint32_t>(*rightTrigger)});
    step.output["ok"] = ok;
    expectWorked(step, "ok", ok);
}

void Session::doTouchClose(MockStep& step)
{
    Touch* touch = openSession(step);
    if (touch == nullptr) {
        return;
    }
    m_module->closeTouch(touch->id);
    m_touches.erase(optionalText(step, "session", "main").value_or("main"));
    step.output["closed"] = true;
}

const ext::NativeDeviceProvider* Session::providerOf(MockStep& step, std::size_t& index)
{
    if (m_module == nullptr) {
        step.problems.push_back(m_manifest.name + " has no native DLL");
        return nullptr;
    }
    const std::vector<ext::NativeDeviceProvider>& providers = m_manifest.contributes.native->deviceProviders;
    std::string name;
    if (member(step.input, "provider") != nullptr) {
        const std::optional<std::string> given = requiredText(step, "provider", "the device provider's name");
        if (!given) {
            return nullptr;
        }
        name = *given;
    } else if (providers.size() == 1) {
        name = providers.front().name;
    } else {
        step.problems.push_back(step.action + " needs the device provider's name in \"provider\"");
        return nullptr;
    }
    const auto found = std::ranges::find(providers, name, &ext::NativeDeviceProvider::name);
    index = static_cast<std::size_t>(found - providers.begin());
    if (found == providers.end() || index >= m_providers.size() || !m_providers[index]) {
        step.problems.push_back(m_manifest.name + " has no device provider " + name);
        return nullptr;
    }
    return &*found;
}

void Session::doDevices(MockStep& step)
{
    std::size_t index = 0;
    const ext::NativeDeviceProvider* provider = providerOf(step, index);
    if (provider == nullptr) {
        return;
    }
    const exthost::CallResult listed = m_module->listDevices(*m_providers[index]);
    if (!listed.ok()) {
        step.output["error"] = listed.problem;
        expectError(step, listed.problem.empty() ? std::string("the device provider failed") : listed.problem);
        return;
    }
    const Json devices = parsedOr(listed.value.text);
    std::vector<std::string> problems;
    if (!devices.is_array()) {
        problems.push_back("the provider did not give a JSON array");
    } else {
        if (devices.size() > kMostProvidedDevices) {
            problems.push_back("more than 64 devices; GameScrcpy shows the first 64");
        }
        for (std::size_t position = 0; position < devices.size(); ++position) {
            if (const std::string problem = deviceProblem(*m_manifest.contributes.native, *provider, devices[position]);
                !problem.empty()) {
                problems.push_back("device " + std::to_string(position + 1) + ": " + problem);
            }
        }
    }
    step.output["devices"] = devices;
    if (!problems.empty()) {
        step.output["problems"] = problems;
    }
    expectError(step, {});
    expectValue(step, "devices", devices);
    if (const Json* count = member(step.expect, "count"); count != nullptr) {
        const std::size_t listedCount = devices.is_array() ? devices.size() : 0;
        if (!count->is_number_integer() || count->get<std::int64_t>() != static_cast<std::int64_t>(listedCount)) {
            step.problems.push_back("count: expected " + brief(*count) + ", got " + std::to_string(listedCount));
        }
    }
    if (member(step.expect, "problems") == nullptr) {
        step.problems.insert(step.problems.end(), problems.begin(), problems.end());
    } else {
        expectInOrder(step, "problems", Json(problems),
                      [](const Json& wanted, const Json& each) { return containsText(each, wanted); });
    }
}

void Session::doAct(MockStep& step)
{
    std::size_t index = 0;
    const ext::NativeDeviceProvider* provider = providerOf(step, index);
    const std::optional<std::string> device = requiredText(step, "device", "the device's id");
    const std::optional<std::string> action = requiredText(step, "action", "the action's id");
    if (provider == nullptr || !device || !action) {
        return;
    }
    const exthost::CallResult acted = m_module->actOnDevice(*m_providers[index], *device, *action);
    const std::string error = acted.ok() ? std::string() : (acted.problem.empty() ? "the action failed" : acted.problem);
    step.output["ok"] = acted.ok();
    if (!error.empty()) {
        step.output["error"] = error;
    }
    if (member(step.expect, "error") != nullptr) {
        expectError(step, error);
    } else {
        expectWorked(step, "ok", acted.ok());
    }
}

void Session::doSettings(MockStep& step)
{
    const Json before{{"global", m_global}, {"devices", m_devices}};
    const Json* global = member(step.input, "settings");
    const Json* devices = member(step.input, "deviceSettings");
    if (const std::string problem = applySettings(global != nullptr ? *global : Json::object(),
                                                  devices != nullptr ? *devices : Json::object());
        !problem.empty()) {
        step.problems.push_back(problem);
        return;
    }
    std::set<std::string> changed;
    for (const auto& [key, value] : m_global.items()) {
        const Json* old = member(before.at("global"), key);
        if (old == nullptr || *old != value) {
            changed.insert(key);
        }
    }
    collectChanged(m_devices, before.at("devices"), changed);
    collectChanged(before.at("devices"), m_devices, changed);
    if (m_module != nullptr) {
        m_module->setSettings(snapshotText());
    }
    step.output["changed"] = Json(std::vector<std::string>(changed.begin(), changed.end()));
    if (const Json* wanted = member(step.expect, "changed"); wanted != nullptr) {
        std::set<std::string> keys;
        bool listed = wanted->is_array();
        for (std::size_t position = 0; listed && position < wanted->size(); ++position) {
            const Json& key = (*wanted)[position];
            listed = key.is_string() && keys.insert(key.get<std::string>()).second;
        }
        if (!listed) {
            step.problems.push_back("expect.changed is a list of setting keys");
        } else if (keys != changed) {
            step.problems.push_back("changed: expected " + brief(*wanted) + ", got " + brief(step.output["changed"]));
        }
    }
}

void Session::doWait(MockStep& step)
{
    const Json* ms = member(step.input, "ms");
    if (ms == nullptr || !ms->is_number() || !std::isfinite(ms->get<double>()) || ms->get<double>() < 0.0 ||
        ms->get<double>() > static_cast<double>(kMockLongestJump)) {
        step.problems.push_back("wait needs \"ms\", the milliseconds to move the test clock on, from 0 to 86400000");
        return;
    }
    m_shared->clock.advance(Microseconds{static_cast<std::int64_t>(std::llround(ms->get<double>() * 1000.0))});
    step.output["now"] = m_shared->clock.milliseconds();
}

void Session::doHolder(MockStep& step)
{
    if (const std::string problem = addHolder(step.input); !problem.empty()) {
        step.problems.push_back(problem);
        return;
    }
    step.output["holding"] = step.input.at("resource");
}

void Session::doHolderRelease(MockStep& step)
{
    const std::optional<std::string> resource = requiredText(step, "resource", "the resource, such as serial:COM9");
    if (!resource) {
        return;
    }
    bool released = false;
    {
        const std::scoped_lock lock(m_shared->mutex);
        released = m_shared->holders.erase(resourceKey(*resource)) > 0;
    }
    step.output["released"] = released;
    expectValue(step, "released", released);
}

void Session::doClaimWanted(MockStep& step, Milliseconds wait)
{
    const std::optional<std::string> resource = requiredText(step, "resource", "the resource, such as serial:COM9");
    const std::optional<std::string> by = optionalText(step, "by", "another program");
    if (!resource || !by) {
        return;
    }
    if (!exthost::validResource(*resource)) {
        step.problems.push_back(*resource + " is not the name of hardware such as serial:COM9");
        return;
    }
    const std::string key = resourceKey(*resource);
    bool had = false;
    {
        const std::scoped_lock lock(m_shared->mutex);
        had = m_shared->held.contains(key);
    }
    const bool yielded = m_module != nullptr && m_module->claimWanted(*resource, *by);
    bool released = false;
    if (had && yielded) {
        std::unique_lock lock(m_shared->mutex);
        released = m_shared->changed.wait_for(lock, std::min(wait, kCancelGrace),
                                              [this, &key] { return !m_shared->held.contains(key); });
        if (released) {
            m_shared->holders[key] = Holder{.resource = *resource, .by = *by, .yields = false};
        }
    }
    step.output["held"] = had;
    step.output["yielded"] = yielded;
    step.output["released"] = released;
    expectValue(step, "held", had);
    expectValue(step, "yielded", yielded);
    expectValue(step, "released", released);
}

void Session::doClaims(MockStep& step)
{
    Json held = Json::array();
    Json holders = Json::array();
    {
        const std::scoped_lock lock(m_shared->mutex);
        for (const auto& entry : m_shared->held) {
            held.push_back(entry.second);
        }
        for (const auto& entry : m_shared->holders) {
            const Holder& holder = entry.second;
            holders.push_back(Json{{"resource", holder.resource}, {"by", holder.by}, {"yields", holder.yields}});
        }
    }
    step.output["held"] = held;
    step.output["holders"] = holders;
    expectValue(step, "held", held);
    expectValue(step, "holders", holders);
}

void Session::doScripts(MockStep& step)
{
    Json scripts = Json::array();
    Json logs = Json::array();
    for (const MockScript& script : m_extension.scripts) {
        scripts.push_back(scriptJson(script));
        for (const std::string& line : script.logs) {
            logs.push_back(line);
        }
    }
    step.output["scripts"] = scripts;
    if (const Json* loaded = member(step.expect, "loaded"); loaded != nullptr) {
        if (!loaded->is_array()) {
            step.problems.push_back("expect.loaded is a list of script names");
        } else {
            for (const Json& name : *loaded) {
                const auto found = std::ranges::find_if(m_extension.scripts, [&name](const MockScript& script) {
                    return name.is_string() && script.name == name.get_ref<const std::string&>();
                });
                if (found == m_extension.scripts.end()) {
                    step.problems.push_back("loaded: there is no script " + brief(name));
                } else if (found->state != MockScriptState::Loaded) {
                    step.problems.push_back("loaded: the script " + found->name + " is " +
                                            std::string(stateName(found->state)) + ": " + found->problem);
                }
            }
        }
    }
    expectInOrder(step, "logs", logs, [](const Json& wanted, const Json& each) { return containsText(each, wanted); });
}

void Session::doManifest(MockStep& step)
{
    const Json value = Json::parse(ext::saveManifest(m_manifest), nullptr, false);
    step.output["value"] = value;
    expectValue(step, "value", value);
}

MockCommandResult Session::runCommand(const ext::CliContribution& command, const Json& args,
                                      const MockCommandHooks& hooks)
{
    MockCommandResult result;
    result.where = placeText();
    std::string error;
    const std::shared_ptr<JobRun> run = startJob(command.job, args, hooks, error);
    if (run == nullptr) {
        result.error = error;
        return result;
    }
    if (hooks.interrupt != nullptr) {
        const std::shared_ptr<Shared> shared = m_shared;
        hooks.interrupt->attach([shared] { shared->wake(); });
    }
    std::unique_lock lock(m_shared->mutex);
    m_shared->changed.wait(lock, [&run, &hooks] {
        return run->done.has_value() || (hooks.interrupt != nullptr && hooks.interrupt->raised());
    });
    if (!run->done) {
        result.interrupted = true;
        run->cancelled = true;
        const std::uint64_t id = run->id;
        lock.unlock();
        m_module->cancelJob(id);
        lock.lock();
        m_shared->changed.wait_for(lock, kCancelGrace, [&run] { return run->done.has_value(); });
    }
    result.progress = run->progress;
    result.logs = run->logs;
    for (const std::string& each : run->outputs) {
        result.outputs.push_back(parsedOr(each));
    }
    const std::optional<std::pair<bool, std::string>> done = run->done;
    const bool cancelled = run->cancelled;
    lock.unlock();
    if (hooks.interrupt != nullptr) {
        hooks.interrupt->attach({});
    }
    if (!done) {
        result.error = "GS-API-066 the job did not stop within 3 seconds of being cancelled";
        return result;
    }
    auto [fine, said] = checkedResult(run->returns, done->first, done->second);
    if (fine) {
        result.worked = true;
        result.result = parsedOr(said);
    } else {
        result.error = (cancelled ? "GS-API-066 " : "GS-API-065 ") + said;
    }
    return result;
}

std::string subjectOf(const MockExtension& extension)
{
    return extension.manifest ? extension.manifest->id + " " + extension.manifest->version.text()
                              : textOf(extension.input);
}

void printDiagnostic(std::ostream& out, const ext::Diagnostic& diagnostic)
{
    out << ext::severityName(diagnostic.severity) << ' ' << ext::codeName(diagnostic.code) << " at " << diagnostic.path;
    if (!diagnostic.detail.empty()) {
        out << ": " << diagnostic.detail;
    }
    out << '\n';
    if (diagnostic.severity != ext::Severity::Notice) {
        out << "  " << ext::fixHint(diagnostic.code) << '\n';
    }
}

Json diagnosticsJson(const std::vector<ext::Diagnostic>& diagnostics)
{
    Json rows = Json::array();
    for (const ext::Diagnostic& diagnostic : diagnostics) {
        rows.push_back(Json{{"severity", std::string(ext::severityName(diagnostic.severity))},
                            {"code", std::string(ext::codeName(diagnostic.code))},
                            {"path", diagnostic.path},
                            {"detail", diagnostic.detail},
                            {"hint", std::string(ext::fixHint(diagnostic.code))}});
    }
    return rows;
}

Json shownOutput(const MockStep& step)
{
    Json shown = step.output;
    shown.erase("happened");
    return shown;
}

}

TempFolder::TempFolder(std::string_view prefix)
{
    std::error_code error;
    const fs::path base = fs::temp_directory_path(error);
    std::random_device device;
    const fs::path folder = base / pathFrom(std::format("{}-{:08x}{:08x}", prefix, device(), device()));
    if (error || !fs::create_directories(folder, error)) {
        throw std::runtime_error("cannot create a temporary folder in " + textOf(base));
    }
    m_path = folder;
}

TempFolder::~TempFolder()
{
    std::error_code error;
    fs::remove_all(m_path, error);
}

bool MockTest::passed() const noexcept
{
    return problem.empty() && std::ranges::all_of(steps, [](const MockStep& step) { return step.passed(); });
}

bool MockRun::passed() const noexcept
{
    return problem.empty() && std::ranges::all_of(tests, [](const MockTest& test) { return test.passed(); });
}

MockExtension openMockExtension(const std::filesystem::path& input, const MockOptions& options)
{
    MockExtension extension;
    extension.input = input;
    std::error_code error;
    if (!fs::exists(input, error)) {
        extension.problem = textOf(input) + " does not exist";
        return extension;
    }
    extension.scratch = std::make_shared<TempFolder>("gs-ext-test");
    extension.folder = input;
    if (!fs::is_directory(input, error)) {
        extension.folder = extension.scratch->path() / "package";
        extension.diagnostics = ext::extractArchive(input, extension.folder);
        if (ext::hasErrors(extension.diagnostics)) {
            extension.problem = "the package cannot be unpacked";
            return extension;
        }
    }
    ext::PackageCheck check = ext::checkDirectory(extension.folder, {.official = options.official});
    std::ranges::move(check.diagnostics, std::back_inserter(extension.diagnostics));
    extension.manifest = std::move(check.manifest);
    if (!extension.manifest || ext::hasErrors(extension.diagnostics)) {
        extension.problem = ext::hasErrors(extension.diagnostics) ? "the extension has the errors above"
                                                                  : "the manifest cannot be read";
        return extension;
    }
    std::vector<std::string> granted;
    for (const ext::Permission& permission : extension.manifest->permissions) {
        granted.push_back(permission.name);
    }
    for (const ext::ScriptContribution& contribution : extension.manifest->contributes.scripts) {
        extension.scripts.push_back(loadScript(extension.folder, contribution, granted));
        const MockScript& loaded = extension.scripts.back();
        if (loaded.state == MockScriptState::Failed && extension.problem.empty()) {
            extension.problem = "the script " + loaded.name + " cannot be loaded: " + loaded.problem;
        }
    }
    return extension;
}

std::vector<std::filesystem::path> mockTestFiles(const std::filesystem::path& folder)
{
    std::vector<fs::path> files;
    std::error_code error;
    for (auto at = fs::directory_iterator(folder / pathFrom(kMockTestsFolder), error);
         !error && at != fs::directory_iterator(); at.increment(error)) {
        std::error_code ignored;
        if (at->is_regular_file(ignored) && textOf(at->path().extension()) == ".json") {
            files.push_back(at->path());
        }
    }
    std::ranges::sort(files);
    return files;
}

MockSpec readMockSpec(const std::filesystem::path& file, const std::filesystem::path& base)
{
    MockSpec read;
    read.name = textOf(file.stem());
    const fs::path relative = base.empty() ? fs::path() : file.lexically_relative(base);
    read.file = textOf(!relative.empty() && *relative.begin() != ".." ? relative : file);
    std::string text;
    if (platform::readFile(file, kMockMostTestBytes, text)) {
        read.problem = "the test file cannot be read or is larger than 1 MiB";
        return read;
    }
    if (!nestingWithin(text, kMockMostDepth)) {
        read.problem = "the test file is nested more than 64 levels deep";
        return read;
    }
    read.spec = Json::parse(text, nullptr, false);
    if (read.spec.is_discarded()) {
        read.problem = "the test file is not JSON";
        read.spec = nullptr;
        return read;
    }
    if (const Json* name = member(read.spec, "name");
        name != nullptr && name->is_string() && !name->get_ref<const std::string&>().empty()) {
        read.name = name->get<std::string>();
    }
    return read;
}

MockRun runMockTests(const MockExtension& extension, const std::vector<MockSpec>& specs, const MockOptions& options)
{
    MockRun run;
    if (!extension.usable()) {
        run.problem = extension.problem.empty() ? std::string("the extension cannot be loaded") : extension.problem;
        return run;
    }
    for (std::size_t number = 0; number < specs.size(); ++number) {
        const MockSpec& spec = specs[number];
        MockTest test{.name = spec.name, .file = spec.file, .problem = spec.problem, .steps = {}};
        if (!test.problem.empty()) {
            run.tests.push_back(std::move(test));
            continue;
        }
        Session session(extension, options);
        if (run.where.empty()) {
            run.where = session.placeText();
        }
        if (!session.readSpec(spec.spec)) {
            test.problem = session.specProblem();
            run.tests.push_back(std::move(test));
            continue;
        }
        if (!session.loadModule(number + 1)) {
            run.problem = session.moduleProblem();
            return run;
        }
        if (run.warnings.empty() && session.hasModule()) {
            run.warnings = session.mismatches();
        }
        const Json& steps = spec.spec.at("steps");
        for (std::size_t index = 0; index < steps.size(); ++index) {
            test.steps.push_back(session.perform(index + 1, steps[index]));
        }
        run.tests.push_back(std::move(test));
    }
    if (run.where.empty()) {
        run.where = Session(extension, options).placeText();
    }
    return run;
}

int mockExitCode(const MockExtension& extension, const MockRun& run) noexcept
{
    if (!extension.usable() || !run.problem.empty()) {
        return kUnloadable;
    }
    return run.tests.empty() || !run.passed() ? kFailed : kPassed;
}

std::string mockReportText(const MockExtension& extension, const MockRun& run)
{
    std::ostringstream out;
    for (const ext::Diagnostic& diagnostic : extension.diagnostics) {
        printDiagnostic(out, diagnostic);
    }
    if (!extension.usable()) {
        out << subjectOf(extension) << ": cannot be loaded: " << extension.problem << '\n';
        return out.str();
    }
    for (const ext::Diagnostic& diagnostic : run.warnings) {
        printDiagnostic(out, diagnostic);
    }
    out << subjectOf(extension) << ": " << run.where << '\n';
    for (const MockScript& script : extension.scripts) {
        out << "script " << script.name << " (" << script.run << "): " << stateName(script.state);
        if (!script.problem.empty()) {
            out << ": " << script.problem;
        }
        out << '\n';
    }
    if (!run.problem.empty()) {
        out << subjectOf(extension) << ": cannot be loaded: " << run.problem << '\n';
        return out.str();
    }
    if (run.tests.empty()) {
        out << "no tests in " << textOf(extension.folder / pathFrom(kMockTestsFolder))
            << " (files named <name>.json); write one or pass --test <file>\n";
        return out.str();
    }
    std::size_t passed = 0;
    for (const MockTest& test : run.tests) {
        passed += test.passed() ? 1 : 0;
        out << (test.passed() ? "PASS " : "FAIL ") << test.name << " (" << test.file << ")\n";
        if (!test.problem.empty()) {
            out << "        " << test.problem << '\n';
        }
        for (const MockStep& step : test.steps) {
            out << std::format("  {}  #{:<3} {:>7} ms  {}", step.passed() ? "pass" : "FAIL", step.index, step.at,
                               step.action.empty() ? std::string("?") : step.action);
            if (!step.label.empty()) {
                out << " \"" << step.label << '"';
            }
            if (!step.input.empty()) {
                out << ' ' << brief(step.input);
            }
            const Json shown = shownOutput(step);
            if (!shown.empty()) {
                out << " -> " << brief(shown);
            }
            out << '\n';
            for (const std::string& problem : step.problems) {
                out << "        " << problem << '\n';
            }
            if (const Json* happened = member(step.output, "happened"); happened != nullptr) {
                std::size_t shownCount = 0;
                for (const Json& entry : *happened) {
                    if (shownCount++ == kMostShownHappenings) {
                        out << "        ... and " << happened->size() - kMostShownHappenings << " more\n";
                        break;
                    }
                    out << "        " << entry.at("kind").get<std::string>() << ' ' << brief(entry.at("value")) << '\n';
                }
            }
        }
    }
    out << passed << " passed, " << run.tests.size() - passed << " failed\n";
    return out.str();
}

MockJson mockReportJson(const MockExtension& extension, const MockRun& run)
{
    Json report = Json::object();
    report["ok"] = mockExitCode(extension, run) == kPassed;
    report["loaded"] = extension.usable() && run.problem.empty();
    if (extension.manifest) {
        report["id"] = extension.manifest->id;
        report["version"] = extension.manifest->version.text();
    }
    report["where"] = run.where;
    const std::string problem = !extension.problem.empty() ? extension.problem : run.problem;
    if (!problem.empty()) {
        report["problem"] = problem;
    }
    std::vector<ext::Diagnostic> diagnostics = extension.diagnostics;
    diagnostics.insert(diagnostics.end(), run.warnings.begin(), run.warnings.end());
    report["diagnostics"] = diagnosticsJson(diagnostics);
    Json scripts = Json::array();
    for (const MockScript& script : extension.scripts) {
        scripts.push_back(scriptJson(script));
    }
    report["scripts"] = std::move(scripts);
    Json tests = Json::array();
    std::size_t passed = 0;
    for (const MockTest& test : run.tests) {
        passed += test.passed() ? 1 : 0;
        Json steps = Json::array();
        for (const MockStep& step : test.steps) {
            steps.push_back(Json{{"index", step.index},
                                 {"do", step.action},
                                 {"name", step.label},
                                 {"at", step.at},
                                 {"input", step.input},
                                 {"expect", step.expect},
                                 {"output", step.output},
                                 {"ok", step.passed()},
                                 {"problems", step.problems}});
        }
        tests.push_back(Json{{"name", test.name},
                             {"file", test.file},
                             {"ok", test.passed()},
                             {"problem", test.problem},
                             {"steps", std::move(steps)}});
    }
    report["tests"] = std::move(tests);
    report["passed"] = passed;
    report["failed"] = run.tests.size() - passed;
    return report;
}

void MockInterrupt::raise()
{
    m_raised.store(true, std::memory_order_release);
    std::function<void()> wake;
    {
        const std::scoped_lock lock(m_mutex);
        wake = m_wake;
    }
    if (wake) {
        wake();
    }
}

void MockInterrupt::attach(std::function<void()> wake)
{
    const std::scoped_lock lock(m_mutex);
    m_wake = std::move(wake);
}

int MockCommandResult::exitCode() const noexcept
{
    if (!problem.empty()) {
        return kUnloadable;
    }
    return worked ? kPassed : kFailed;
}

const ext::CliContribution* findCliCommand(const ext::Manifest& manifest, std::string_view name)
{
    const auto found = std::ranges::find(manifest.contributes.cli, name, &ext::CliContribution::name);
    return found != manifest.contributes.cli.end() ? &*found : nullptr;
}

std::vector<MockCliOption> cliOptions(const ext::Manifest& manifest, const ext::CliContribution& command)
{
    std::vector<MockCliOption> options;
    if (!manifest.contributes.native) {
        return options;
    }
    const std::vector<ext::NativeJob>& jobs = manifest.contributes.native->jobs;
    const auto job = std::ranges::find(jobs, command.job, &ext::NativeJob::name);
    if (job == jobs.end()) {
        return options;
    }
    for (const ext::NativeParam& param : job->params) {
        MockCliOption option{.name = param.name, .type = param.type, .description = {}, .fallback = std::nullopt};
        const auto listed = std::ranges::find(command.params, param.name, &ext::CliParam::name);
        if (listed != command.params.end()) {
            option.description = listed->description;
            if (!listed->defaultValue.empty()) {
                const Json fallback = Json::parse(listed->defaultValue, nullptr, false);
                if (!fallback.is_discarded()) {
                    option.fallback = fallback;
                }
            }
        }
        options.push_back(std::move(option));
    }
    return options;
}

std::optional<MockJson> cliArguments(const ext::Manifest& manifest, const ext::CliContribution& command,
                                     const std::vector<std::pair<std::string, std::string>>& given,
                                     std::string& problem)
{
    const std::vector<MockCliOption> options = cliOptions(manifest, command);
    Json args = Json::object();
    for (const auto& [name, text] : given) {
        const auto option = std::ranges::find(options, name, &MockCliOption::name);
        if (option == options.end()) {
            problem = command.name + " takes no --" + name;
            return std::nullopt;
        }
        if (args.contains(name)) {
            problem = "--" + name + " is given twice";
            return std::nullopt;
        }
        Json value;
        const std::string& type = option->type;
        if (type == "boolean") {
            if (text != "true" && text != "false") {
                problem = "--" + name + " is true or false";
                return std::nullopt;
            }
            value = text == "true";
        } else if (type == "number" || type == "duration") {
            char* end = nullptr;
            const double number = std::strtod(text.c_str(), &end);
            if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(number)) {
                problem = "--" + name + " is a number";
                return std::nullopt;
            }
            value = number;
        } else if (type == "point" || type == "region") {
            std::vector<double> numbers;
            std::size_t start = 0;
            while (start <= text.size()) {
                const std::size_t comma = std::min(text.find(',', start), text.size());
                const std::string part = text.substr(start, comma - start);
                char* end = nullptr;
                const double number = std::strtod(part.c_str(), &end);
                if (part.empty() || end != part.c_str() + part.size() || !std::isfinite(number)) {
                    numbers.clear();
                    break;
                }
                numbers.push_back(number);
                start = comma + 1;
            }
            if (type == "point" && numbers.size() == 2) {
                value = Json{{"x", numbers[0]}, {"y", numbers[1]}};
            } else if (type == "region" && numbers.size() == 4) {
                value = Json{{"x", numbers[0]}, {"y", numbers[1]}, {"width", numbers[2]}, {"height", numbers[3]}};
            } else {
                problem = "--" + name + (type == "point" ? " is x,y such as 0.5,0.5" : " is x,y,width,height");
                return std::nullopt;
            }
        } else if (type == "json") {
            if (!nestingWithin(text, kMockMostDepth)) {
                problem = "--" + name + " is nested more than 64 levels deep";
                return std::nullopt;
            }
            value = parsedOr(text);
        } else {
            value = text;
        }
        if (!fitsType(type, value)) {
            problem = "--" + name + " is a " + type;
            return std::nullopt;
        }
        args[name] = std::move(value);
    }
    for (const MockCliOption& option : options) {
        if (args.contains(option.name)) {
            continue;
        }
        if (!option.fallback) {
            problem = command.name + " needs --" + option.name;
            return std::nullopt;
        }
        args[option.name] = *option.fallback;
    }
    return args;
}

MockCommandResult runMockCommand(const MockExtension& extension, const ext::CliContribution& command,
                                 const MockJson& args, const MockOptions& options, const MockCommandHooks& hooks)
{
    if (!extension.usable()) {
        MockCommandResult refused;
        refused.problem = extension.problem.empty() ? std::string("the extension cannot be loaded") : extension.problem;
        return refused;
    }
    Session session(extension, options);
    if (!session.loadModule(0)) {
        MockCommandResult refused;
        refused.where = session.placeText();
        refused.problem = session.moduleProblem();
        return refused;
    }
    return session.runCommand(command, args, hooks);
}

}
