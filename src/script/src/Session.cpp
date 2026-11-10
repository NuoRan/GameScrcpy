#include "gs/script/Session.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <utility>

namespace gs::script {

namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

constexpr double kLongestSessionMs = 24.0 * 3600.0 * 1000.0;
constexpr std::size_t kMostTouchPoints = 32;
constexpr std::int64_t kLargestKeyNumber = 0xFFFF'FFFFLL;
constexpr std::int64_t kLongestText = std::numeric_limits<std::int32_t>::max();

constexpr std::array kTouchActions{
    std::pair{std::string_view("down"), SessionTouchAction::Down},
    std::pair{std::string_view("up"), SessionTouchAction::Up},
    std::pair{std::string_view("move"), SessionTouchAction::Move},
    std::pair{std::string_view("cancel"), SessionTouchAction::Cancel},
};

std::optional<int> digits(std::string_view text, std::size_t at, std::size_t count)
{
    if (at + count > text.size()) {
        return std::nullopt;
    }
    int value = 0;
    const auto [end, error] = std::from_chars(text.data() + at, text.data() + at + count, value);
    if (error != std::errc{} || end != text.data() + at + count) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::int64_t> instantOf(std::string_view text)
{
    if (text.size() < 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':'
        || text[16] != ':' || text.back() != 'Z') {
        return std::nullopt;
    }
    const auto year = digits(text, 0, 4);
    const auto month = digits(text, 5, 2);
    const auto day = digits(text, 8, 2);
    const auto hour = digits(text, 11, 2);
    const auto minute = digits(text, 14, 2);
    const auto second = digits(text, 17, 2);
    if (!year || !month || !day || !hour || !minute || !second || *hour > 23 || *minute > 59 || *second > 59) {
        return std::nullopt;
    }
    int millisecond = 0;
    if (text.size() > 20) {
        if (text[19] != '.') {
            return std::nullopt;
        }
        const std::string_view fraction = text.substr(20, text.size() - 21);
        if (fraction.empty() || fraction.size() > 9
            || !std::ranges::all_of(fraction, [](char c) { return c >= '0' && c <= '9'; })) {
            return std::nullopt;
        }
        std::string padded(fraction.substr(0, 3));
        padded.resize(3, '0');
        const auto value = digits(padded, 0, 3);
        if (!value) {
            return std::nullopt;
        }
        millisecond = *value;
    }
    const std::chrono::year_month_day date{std::chrono::year{*year},
                                           std::chrono::month{static_cast<unsigned>(*month)},
                                           std::chrono::day{static_cast<unsigned>(*day)}};
    if (!date.ok()) {
        return std::nullopt;
    }
    const auto days = std::chrono::sys_days{date}.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(days).count()
         + ((*hour * 60 + *minute) * 60 + *second) * std::int64_t{1000} + millisecond;
}

std::string at(std::string_view path, std::string_view problem)
{
    return std::format("{}: {}", path, problem);
}

std::optional<std::string> readDevice(const Json& value, DeviceInfo& device)
{
    if (!value.is_object()) {
        return at("$.device", "must be an object");
    }
    if (const auto found = value.find("model"); found != value.end()) {
        if (!found->is_string()) {
            return at("$.device.model", "must be a string");
        }
        device.model = found->get<std::string>();
    }
    const auto integer = [&value](const char* name, int low, int high, int& field) -> std::optional<std::string> {
        const auto found = value.find(name);
        if (found == value.end()) {
            return std::nullopt;
        }
        if (!found->is_number_integer() || found->get<std::int64_t>() < low || found->get<std::int64_t>() > high) {
            return at(std::string("$.device.") + name, std::format("must be a whole number from {} to {}", low, high));
        }
        field = found->get<int>();
        return std::nullopt;
    };
    if (auto problem = integer("android", 0, 99, device.android)) {
        return problem;
    }
    if (auto problem = integer("width", 1, 65535, device.width)) {
        return problem;
    }
    if (auto problem = integer("height", 1, 65535, device.height)) {
        return problem;
    }
    return integer("rotation", 0, 3, device.rotation);
}

std::optional<std::string> readTime(const Json& value, const std::string& path, Microseconds& time)
{
    const auto when = value.find("at");
    if (when == value.end() || !when->is_number() || !(when->get<double>() >= 0.0)
        || when->get<double>() > kLongestSessionMs) {
        return at(path + ".at", "must be a number of milliseconds from 0 to 86400000");
    }
    time = Microseconds{static_cast<std::int64_t>(std::llround(when->get<double>() * 1000.0))};
    return std::nullopt;
}

std::optional<std::string> readEvent(const Json& value, std::size_t index, SessionEvent& event)
{
    const std::string path = std::format("$.events[{}]", index);
    if (!value.is_object()) {
        return at(path, "must be an object");
    }
    if (auto problem = readTime(value, path, event.at)) {
        return problem;
    }
    int kinds = 0;
    const auto name = [&](const char* key, SessionEventKind kind) -> std::optional<std::string> {
        const auto found = value.find(key);
        if (found == value.end()) {
            return std::nullopt;
        }
        ++kinds;
        if (!found->is_string() || found->get_ref<const std::string&>().empty()) {
            return at(path + "." + key, "must be a non-empty string");
        }
        event.kind = kind;
        event.id = found->get<std::string>();
        return std::nullopt;
    };
    for (const auto& [key, kind] : {std::pair{"press", SessionEventKind::Press}, std::pair{"release", SessionEventKind::Release},
                                    std::pair{"state", SessionEventKind::State}, std::pair{"run", SessionEventKind::Run}}) {
        if (auto problem = name(key, kind)) {
            return problem;
        }
    }
    if (const auto answer = value.find("answer"); answer != value.end()) {
        ++kinds;
        event.kind = SessionEventKind::Answer;
        event.id = answer->dump();
    }
    if (kinds != 1) {
        return at(path, "needs exactly one of press, release, state, run or answer");
    }
    if (event.kind == SessionEventKind::State) {
        const auto found = value.find("value");
        if (found == value.end() || !found->is_boolean()) {
            return at(path + ".value", "a state event needs a value of true or false");
        }
        event.value = found->get<bool>();
    }
    return std::nullopt;
}

const Json* field(const Json& object, const char* name)
{
    const auto found = object.find(name);
    return found == object.end() ? nullptr : &*found;
}

std::optional<std::int64_t> wholeNumber(const Json* value, std::int64_t low, std::int64_t high)
{
    if (value == nullptr) {
        return std::nullopt;
    }
    if (value->is_number_unsigned()) {
        const auto number = value->get<std::uint64_t>();
        if (number > static_cast<std::uint64_t>(high) || static_cast<std::int64_t>(number) < low) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(number);
    }
    if (!value->is_number_integer() || value->get<std::int64_t>() < low || value->get<std::int64_t>() > high) {
        return std::nullopt;
    }
    return value->get<std::int64_t>();
}

std::optional<double> fraction(const Json* value)
{
    if (value == nullptr || !value->is_number() || !(value->get<double>() >= 0.0) || value->get<double>() > 1.0) {
        return std::nullopt;
    }
    return value->get<double>();
}

std::optional<SessionTouchAction> touchActionOf(const Json* value)
{
    if (value == nullptr || !value->is_string()) {
        return std::nullopt;
    }
    for (const auto& [name, action] : kTouchActions) {
        if (value->get_ref<const std::string&>() == name) {
            return action;
        }
    }
    return std::nullopt;
}

std::string_view touchActionName(SessionTouchAction action)
{
    for (const auto& [name, known] : kTouchActions) {
        if (known == action) {
            return name;
        }
    }
    return "move";
}

std::optional<std::string> readTouch(const Json& value, const std::string& path, SessionTouch& touch)
{
    const Json* points = value.is_object() ? field(value, "points") : nullptr;
    if (points == nullptr || !points->is_array() || points->size() > kMostTouchPoints) {
        return at(path + ".points", "must be a list of at most 32 points");
    }
    for (std::size_t index = 0; index < points->size(); ++index) {
        const Json& item = (*points)[index];
        const std::string place = std::format("{}.points[{}]", path, index);
        if (!item.is_object()) {
            return at(place, "must be an object");
        }
        const auto pointer = wholeNumber(field(item, "id"), 0, 31);
        if (!pointer) {
            return at(place + ".id", "must be a whole number from 0 to 31");
        }
        const auto action = touchActionOf(field(item, "action"));
        if (!action) {
            return at(place + ".action", "must be \"down\", \"up\", \"move\" or \"cancel\"");
        }
        const auto x = fraction(field(item, "x"));
        if (!x) {
            return at(place + ".x", "must be a number from 0 to 1");
        }
        const auto y = fraction(field(item, "y"));
        if (!y) {
            return at(place + ".y", "must be a number from 0 to 1");
        }
        touch.points.push_back(SessionTouchPoint{.pointer = static_cast<int>(*pointer), .action = *action, .x = *x, .y = *y});
    }
    return std::nullopt;
}

std::optional<std::string> readKey(const Json& value, const std::string& path, SessionKey& key)
{
    if (!value.is_object()) {
        return at(path, "must be an object");
    }
    const Json* action = field(value, "action");
    if (action == nullptr || !action->is_string()
        || (action->get_ref<const std::string&>() != "down" && action->get_ref<const std::string&>() != "up")) {
        return at(path + ".action", "must be \"down\" or \"up\"");
    }
    key.down = action->get_ref<const std::string&>() == "down";
    const auto keycode = wholeNumber(field(value, "keycode"), 0, kLargestKeyNumber);
    if (!keycode) {
        return at(path + ".keycode", "must be a whole number from 0 to 4294967295");
    }
    key.keycode = static_cast<std::uint32_t>(*keycode);
    if (const Json* meta = field(value, "metaState")) {
        const auto state = wholeNumber(meta, 0, kLargestKeyNumber);
        if (!state) {
            return at(path + ".metaState", "must be a whole number from 0 to 4294967295");
        }
        key.metaState = static_cast<std::uint32_t>(*state);
    }
    if (const Json* repeat = field(value, "repeat")) {
        const auto count = wholeNumber(repeat, 0, 65535);
        if (!count) {
            return at(path + ".repeat", "must be a whole number from 0 to 65535");
        }
        key.repeat = static_cast<std::uint32_t>(*count);
    }
    return std::nullopt;
}

std::optional<std::string> readScroll(const Json& value, const std::string& path, SessionScroll& scroll)
{
    if (!value.is_object()) {
        return at(path, "must be an object");
    }
    const auto x = fraction(field(value, "x"));
    if (!x) {
        return at(path + ".x", "must be a number from 0 to 1");
    }
    const auto y = fraction(field(value, "y"));
    if (!y) {
        return at(path + ".y", "must be a number from 0 to 1");
    }
    const auto horizontal = wholeNumber(field(value, "h"), -32768, 32767);
    if (!horizontal) {
        return at(path + ".h", "must be a whole number from -32768 to 32767");
    }
    const auto vertical = wholeNumber(field(value, "v"), -32768, 32767);
    if (!vertical) {
        return at(path + ".v", "must be a whole number from -32768 to 32767");
    }
    scroll = SessionScroll{.x = *x, .y = *y, .horizontal = static_cast<int>(*horizontal), .vertical = static_cast<int>(*vertical)};
    return std::nullopt;
}

std::optional<std::string> readOperation(const Json& value, std::size_t index, SessionOperation& operation)
{
    const std::string path = std::format("$.operations[{}]", index);
    if (!value.is_object()) {
        return at(path, "must be an object");
    }
    if (auto problem = readTime(value, path, operation.at)) {
        return problem;
    }
    const std::array present{field(value, "touch"), field(value, "key"), field(value, "scroll"), field(value, "text")};
    if (std::ranges::count_if(present, [](const Json* item) { return item != nullptr; }) != 1) {
        return at(path, "needs exactly one of touch, key, scroll or text");
    }
    if (present[0] != nullptr) {
        SessionTouch touch;
        auto problem = readTouch(*present[0], path + ".touch", touch);
        operation.action = std::move(touch);
        return problem;
    }
    if (present[1] != nullptr) {
        SessionKey key;
        auto problem = readKey(*present[1], path + ".key", key);
        operation.action = key;
        return problem;
    }
    if (present[2] != nullptr) {
        SessionScroll scroll;
        auto problem = readScroll(*present[2], path + ".scroll", scroll);
        operation.action = scroll;
        return problem;
    }
    const auto length = present[3]->is_object() ? wholeNumber(field(*present[3], "length"), 0, kLongestText) : std::nullopt;
    if (!length) {
        return at(path + ".text.length", "must be a whole number of characters");
    }
    operation.action = SessionText{.length = static_cast<std::size_t>(*length)};
    return std::nullopt;
}

bool plainFileName(std::string_view name)
{
    return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\:") == std::string_view::npos;
}

std::optional<std::string> readFileName(const Json& root, const char* name, std::string& target)
{
    const Json* found = field(root, name);
    if (found == nullptr) {
        return std::nullopt;
    }
    if (!found->is_string() || !plainFileName(found->get_ref<const std::string&>())) {
        return at(std::string("$.") + name, "must be the name of a file next to the session");
    }
    target = found->get<std::string>();
    return std::nullopt;
}

std::optional<std::string> readRecording(const Json& root, Session& session)
{
    if (auto problem = readFileName(root, "video", session.video)) {
        return problem;
    }
    if (auto problem = readFileName(root, "audio", session.audio)) {
        return problem;
    }
    if (const Json* start = field(root, "videoStartPtsUs")) {
        const auto value = wholeNumber(start, std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max());
        if (!value) {
            return at("$.videoStartPtsUs", "must be a whole number of microseconds");
        }
        session.videoStartPtsUs = *value;
    }
    if (const Json* duration = field(root, "durationMs")) {
        const auto value = wholeNumber(duration, 0, std::numeric_limits<std::int64_t>::max() / 1000);
        if (!value) {
            return at("$.durationMs", "must be a whole number of milliseconds, 0 or more");
        }
        session.durationMs = *value;
    }
    const Json* operations = field(root, "operations");
    if (operations == nullptr) {
        return std::nullopt;
    }
    if (!operations->is_array() || operations->size() > kMostSessionOperations) {
        return at("$.operations", "must be a list of at most 1000000 operations");
    }
    session.operations.reserve(operations->size());
    for (std::size_t index = 0; index < operations->size(); ++index) {
        SessionOperation operation;
        if (auto problem = readOperation((*operations)[index], index, operation)) {
            return problem;
        }
        session.operations.push_back(std::move(operation));
    }
    std::ranges::stable_sort(session.operations, {}, &SessionOperation::at);
    return std::nullopt;
}

OrderedJson timeJson(Microseconds time)
{
    if (time.count() % 1000 == 0) {
        return time.count() / 1000;
    }
    return static_cast<double>(time.count()) / 1000.0;
}

OrderedJson numberOf(double value)
{
    if (std::isfinite(value) && std::floor(value) == value && std::abs(value) < 1e15) {
        return static_cast<std::int64_t>(value);
    }
    return value;
}

std::string instantText(std::int64_t epochMs)
{
    const std::chrono::sys_time<std::chrono::milliseconds> instant{std::chrono::milliseconds{epochMs}};
    return std::format("{:%Y-%m-%dT%H:%M:%S}Z", instant);
}

OrderedJson eventJson(const SessionEvent& event)
{
    OrderedJson item;
    item["at"] = timeJson(event.at);
    switch (event.kind) {
    case SessionEventKind::Press:
        item["press"] = event.id;
        break;
    case SessionEventKind::Release:
        item["release"] = event.id;
        break;
    case SessionEventKind::State:
        item["state"] = event.id;
        item["value"] = event.value;
        break;
    case SessionEventKind::Run:
        item["run"] = event.id;
        break;
    case SessionEventKind::Answer: {
        OrderedJson answer = OrderedJson::parse(event.id, nullptr, false);
        item["answer"] = answer.is_discarded() ? OrderedJson(event.id) : std::move(answer);
        break;
    }
    }
    return item;
}

OrderedJson operationJson(const SessionOperation& operation)
{
    OrderedJson item;
    item["at"] = timeJson(operation.at);
    if (const auto* touch = std::get_if<SessionTouch>(&operation.action)) {
        OrderedJson points = OrderedJson::array();
        for (const SessionTouchPoint& point : touch->points) {
            OrderedJson entry;
            entry["id"] = point.pointer;
            entry["action"] = std::string(touchActionName(point.action));
            entry["x"] = numberOf(point.x);
            entry["y"] = numberOf(point.y);
            points.push_back(std::move(entry));
        }
        item["touch"]["points"] = std::move(points);
    } else if (const auto* key = std::get_if<SessionKey>(&operation.action)) {
        item["key"]["action"] = key->down ? "down" : "up";
        item["key"]["keycode"] = key->keycode;
        item["key"]["metaState"] = key->metaState;
        item["key"]["repeat"] = key->repeat;
    } else if (const auto* scroll = std::get_if<SessionScroll>(&operation.action)) {
        item["scroll"]["x"] = numberOf(scroll->x);
        item["scroll"]["y"] = numberOf(scroll->y);
        item["scroll"]["h"] = scroll->horizontal;
        item["scroll"]["v"] = scroll->vertical;
    } else if (const auto* text = std::get_if<SessionText>(&operation.action)) {
        item["text"]["length"] = text->length;
    }
    return item;
}

void appendList(std::string& text, std::string_view name, const std::vector<OrderedJson>& items, bool last)
{
    text += std::format("  \"{}\": [", name);
    for (std::size_t index = 0; index < items.size(); ++index) {
        text += index == 0 ? "\n    " : ",\n    ";
        text += items[index].dump();
    }
    text += items.empty() ? "]" : "\n  ]";
    text += last ? "\n" : ",\n";
}

}

Expected<Session, std::string> parseSession(std::string_view text)
{
    const Json root = Json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return makeUnexpected(std::string("$: the session is not a JSON object"));
    }
    const auto format = root.find("format");
    if (format == root.end() || !format->is_string() || format->get_ref<const std::string&>() != kSessionFormat) {
        return makeUnexpected(at("$.format", std::format("must be \"{}\"", kSessionFormat)));
    }
    const auto version = wholeNumber(field(root, "version"), 1, kSessionVersion);
    if (!version) {
        return makeUnexpected(at("$.version", "must be 1 or 2"));
    }
    Session session;
    session.device = DeviceInfo{"Recorded phone", 15, 1080, 2400, 0};
    session.startedAtMs = kDefaultSessionStartMs;
    if (const auto device = root.find("device"); device != root.end()) {
        if (auto problem = readDevice(*device, session.device)) {
            return makeUnexpected(std::move(*problem));
        }
    }
    if (const auto started = root.find("startedAt"); started != root.end()) {
        const auto instant = started->is_string() ? instantOf(started->get_ref<const std::string&>()) : std::nullopt;
        if (!instant) {
            return makeUnexpected(at("$.startedAt", "must be a UTC time such as \"2026-11-01T12:00:00Z\""));
        }
        session.startedAtMs = *instant;
    }
    if (const auto rate = root.find("frameRate"); rate != root.end()) {
        if (!rate->is_number() || !(rate->get<double>() >= 1.0) || rate->get<double>() > 240.0) {
            return makeUnexpected(at("$.frameRate", "must be a number from 1 to 240"));
        }
        session.frameRate = rate->get<double>();
    }
    if (const auto profile = root.find("profile"); profile != root.end()) {
        if (!profile->is_string()) {
            return makeUnexpected(at("$.profile", "must be a path relative to the session file"));
        }
        session.profile = profile->get<std::string>();
    }
    if (const auto events = root.find("events"); events != root.end()) {
        if (!events->is_array() || events->size() > kMostSessionEvents) {
            return makeUnexpected(at("$.events", "must be a list of at most 100000 events"));
        }
        for (std::size_t index = 0; index < events->size(); ++index) {
            SessionEvent event;
            if (auto problem = readEvent((*events)[index], index, event)) {
                return makeUnexpected(std::move(*problem));
            }
            session.events.push_back(std::move(event));
        }
    }
    std::ranges::stable_sort(session.events, {}, &SessionEvent::at);
    if (*version >= 2) {
        if (auto problem = readRecording(root, session)) {
            return makeUnexpected(std::move(*problem));
        }
    }
    return session;
}

Expected<Session, std::string> loadSession(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return makeUnexpected(std::string("the session file cannot be read"));
    }
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return parseSession(text);
}

std::string writeSession(const Session& session)
{
    OrderedJson header;
    header["format"] = std::string(kSessionFormat);
    header["version"] = kSessionVersion;
    OrderedJson device;
    device["model"] = session.device.model;
    if (session.device.android >= 0 && session.device.android <= 99) {
        device["android"] = session.device.android;
    }
    if (session.device.width >= 1 && session.device.width <= 65535) {
        device["width"] = session.device.width;
    }
    if (session.device.height >= 1 && session.device.height <= 65535) {
        device["height"] = session.device.height;
    }
    if (session.device.rotation >= 0 && session.device.rotation <= 3) {
        device["rotation"] = session.device.rotation;
    }
    header["device"] = std::move(device);
    header["startedAt"] = instantText(session.startedAtMs);
    header["frameRate"] = numberOf(std::clamp(session.frameRate, 1.0, 240.0));
    if (!session.profile.empty()) {
        header["profile"] = session.profile;
    }
    if (plainFileName(session.video)) {
        header["video"] = session.video;
    }
    if (plainFileName(session.audio)) {
        header["audio"] = session.audio;
    }
    header["videoStartPtsUs"] = session.videoStartPtsUs;
    header["durationMs"] = std::max<std::int64_t>(session.durationMs, 0);

    std::string text = "{\n";
    for (const auto& item : header.items()) {
        text += std::format("  {}: {},\n", OrderedJson(item.key()).dump(), item.value().dump());
    }
    std::vector<OrderedJson> events;
    events.reserve(std::min(session.events.size(), kMostSessionEvents));
    for (const SessionEvent& event : session.events) {
        if (events.size() == kMostSessionEvents) {
            break;
        }
        if (event.at.count() >= 0 && event.at.count() <= kLongestSessionUs) {
            events.push_back(eventJson(event));
        }
    }
    appendList(text, "events", events, false);
    std::vector<OrderedJson> operations;
    operations.reserve(std::min(session.operations.size(), kMostSessionOperations));
    for (const SessionOperation& operation : session.operations) {
        if (operations.size() == kMostSessionOperations) {
            break;
        }
        if (operation.at.count() >= 0 && operation.at.count() <= kLongestSessionUs) {
            operations.push_back(operationJson(operation));
        }
    }
    appendList(text, "operations", operations, true);
    text += "}\n";
    return text;
}

}
