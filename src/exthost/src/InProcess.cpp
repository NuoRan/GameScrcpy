#include "gs/exthost/InProcess.h"

#include "Store.h"

#include <gamescrcpy_ext.h>

#include <windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace gs::exthost {

static_assert(static_cast<int>(ValueType::Boolean) == GS_EXT_TYPE_BOOLEAN);
static_assert(static_cast<int>(ValueType::Number) == GS_EXT_TYPE_NUMBER);
static_assert(static_cast<int>(ValueType::String) == GS_EXT_TYPE_STRING);
static_assert(static_cast<int>(ValueType::Point) == GS_EXT_TYPE_POINT);
static_assert(static_cast<int>(ValueType::Region) == GS_EXT_TYPE_REGION);
static_assert(static_cast<int>(ValueType::Color) == GS_EXT_TYPE_COLOR);
static_assert(static_cast<int>(ValueType::Duration) == GS_EXT_TYPE_DURATION);
static_assert(static_cast<int>(ValueType::Json) == GS_EXT_TYPE_JSON);
static_assert(static_cast<int>(TouchAction::Down) == GS_EXT_TOUCH_DOWN);
static_assert(static_cast<int>(TouchAction::Move) == GS_EXT_TOUCH_MOVE);
static_assert(static_cast<int>(TouchAction::Up) == GS_EXT_TOUCH_UP);
static_assert(static_cast<int>(TouchAction::Cancel) == GS_EXT_TOUCH_CANCEL);
static_assert(static_cast<int>(InputKind::KeyDown) == GS_EXT_INPUT_KEY_DOWN);
static_assert(static_cast<int>(InputKind::Axis1) == GS_EXT_INPUT_AXIS1);
static_assert(static_cast<int>(InputKind::Wheel) == GS_EXT_INPUT_WHEEL);
static_assert(static_cast<int>(CommandKind::Up) == GS_EXT_COMMAND_UP);
static_assert(static_cast<int>(SlotKind::Axis2) == GS_EXT_SLOT_AXIS2);
static_assert(static_cast<int>(SlotKind::Axis1) == GS_EXT_SLOT_AXIS1);
static_assert(kMostFingers == static_cast<std::size_t>(GS_EXT_MOST_FINGERS));

namespace {

constexpr std::size_t kMostLogBytes = 4096;
constexpr std::size_t kMostErrorBytes = 1000;
constexpr std::uint32_t kMostContributions = 256;
constexpr std::chrono::seconds kJobGrace{3};
constexpr std::chrono::milliseconds kProgressGap{50};

struct ResultBox {
    std::string text;
    std::string error;
};

thread_local ResultBox t_storeBox;

std::optional<std::string> nameOf(const char* text)
{
    if (text == nullptr) {
        return std::nullopt;
    }
    const std::size_t length = strnlen(text, kMostNameBytes + 1);
    if (length == 0 || length > kMostNameBytes) {
        return std::nullopt;
    }
    std::string name(text, length);
    const bool plain = name.front() >= 'a' && name.front() <= 'z' && std::ranges::all_of(name, [](char c) {
                           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
                       });
    return plain ? std::optional<std::string>(std::move(name)) : std::nullopt;
}

gs_ext_value toNative(const Value& value)
{
    gs_ext_value made{};
    made.type = static_cast<std::uint32_t>(value.type);
    made.flag = value.flag ? 1 : 0;
    made.number = value.number;
    made.x = value.x;
    made.y = value.y;
    made.width = value.width;
    made.height = value.height;
    made.text = value.text.data();
    made.size = value.text.size();
    return made;
}

std::optional<Value> fromNative(const gs_ext_value& value, std::string& problem)
{
    if (value.type > GS_EXT_TYPE_JSON) {
        problem = "the value has the unknown type " + std::to_string(value.type);
        return std::nullopt;
    }
    Value made;
    made.type = static_cast<ValueType>(value.type);
    made.flag = value.flag != 0;
    made.number = value.number;
    made.x = value.x;
    made.y = value.y;
    made.width = value.width;
    made.height = value.height;
    const bool textual =
        value.type == GS_EXT_TYPE_STRING || value.type == GS_EXT_TYPE_COLOR || value.type == GS_EXT_TYPE_JSON;
    if (textual && value.size > 0) {
        if (value.text == nullptr || value.size > kMostTextBytes) {
            problem = value.text == nullptr ? "the text is missing" : "the text is too long";
            return std::nullopt;
        }
        made.text.assign(value.text, value.size);
    }
    return made;
}

gs_ext_input_event nativeEvent(const InputEvent& event)
{
    return {static_cast<std::uint32_t>(event.kind), event.slot, event.x, event.y, event.at};
}

gs_ext_touch_command nativeCommand(const TouchCommand& command)
{
    return {static_cast<std::uint32_t>(command.kind), command.finger, command.x, command.y, command.at};
}

StepOutput outputOf(int32_t status, const gs_ext_output& out, std::span<const gs_ext_touch_command> commands,
                    std::span<const gs_ext_input_event> events)
{
    StepOutput result;
    result.status = StepStatus::Failed;
    if (status != GS_EXT_OK || out.command_count > out.command_capacity || out.event_count > out.event_capacity ||
        out.command_count > commands.size() || out.event_count > events.size()) {
        return result;
    }
    for (std::uint32_t index = 0; index < out.command_count; ++index) {
        const gs_ext_touch_command& made = commands[index];
        if (made.kind > GS_EXT_COMMAND_UP) {
            result.commands.clear();
            return result;
        }
        result.commands.push_back(
            {.kind = static_cast<CommandKind>(made.kind), .finger = made.finger, .x = made.x, .y = made.y, .at = made.at});
    }
    for (std::uint32_t index = 0; index < out.event_count; ++index) {
        const gs_ext_input_event& made = events[index];
        if (made.kind > GS_EXT_INPUT_WHEEL) {
            result.commands.clear();
            result.events.clear();
            return result;
        }
        result.events.push_back(
            {.kind = static_cast<InputKind>(made.kind), .slot = made.slot, .x = made.x, .y = made.y, .at = made.at});
    }
    result.wakeAt = out.wake_at;
    result.status = StepStatus::Ok;
    return result;
}

}

namespace {

class EventGate
{
public:
    using Deliver = std::function<void(const std::string& topic, const std::string& json)>;

    explicit EventGate(Deliver deliver)
        : m_deliver(std::move(deliver))
    {
    }

    ~EventGate()
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_stopping = true;
        }
        m_wake.notify_all();
        if (m_flusher.joinable()) {
            m_flusher.join();
        }
    }

    EventGate(const EventGate&) = delete;
    EventGate& operator=(const EventGate&) = delete;

    [[nodiscard]] bool emit(const std::string& topic, std::string json)
    {
        const Clock::time_point now = Clock::now();
        {
            const std::scoped_lock lock(m_mutex);
            if (m_stopping) {
                return false;
            }
            auto found = m_topics.find(topic);
            if (found == m_topics.end()) {
                if (m_topics.size() >= kMostTopics) {
                    std::erase_if(m_topics, [now](const auto& entry) {
                        return !entry.second.pending && now - entry.second.window >= kWindow;
                    });
                }
                if (m_topics.size() >= kMostTopics) {
                    return false;
                }
                found = m_topics.emplace(topic, Topic{}).first;
            }
            Topic& entry = found->second;
            if (now - entry.window >= kWindow) {
                entry.window = now;
                entry.count = 0;
                entry.pending.reset();
            }
            if (entry.count >= kMostEventsPerSecond) {
                entry.pending = std::move(json);
                if (!m_flusher.joinable()) {
                    m_flusher = std::thread([this] { flush(); });
                }
                m_wake.notify_all();
                return true;
            }
            ++entry.count;
        }
        m_deliver(topic, json);
        return true;
    }

private:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::seconds kWindow{1};

    struct Topic {
        Clock::time_point window{};
        std::size_t count = 0;
        std::optional<std::string> pending;
    };

    void flush()
    {
        std::unique_lock lock(m_mutex);
        while (!m_stopping) {
            const Clock::time_point now = Clock::now();
            std::vector<std::pair<std::string, std::string>> ready;
            Clock::time_point due = Clock::time_point::max();
            for (auto& [name, entry] : m_topics) {
                if (!entry.pending) {
                    continue;
                }
                if (now - entry.window >= kWindow) {
                    entry.window = now;
                    entry.count = 1;
                    ready.emplace_back(name, std::move(*entry.pending));
                    entry.pending.reset();
                } else {
                    due = std::min(due, entry.window + kWindow);
                }
            }
            if (!ready.empty()) {
                lock.unlock();
                for (const auto& [name, json] : ready) {
                    m_deliver(name, json);
                }
                lock.lock();
            } else if (due == Clock::time_point::max()) {
                m_wake.wait(lock);
            } else {
                m_wake.wait_until(lock, due);
            }
        }
    }

    Deliver m_deliver;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::map<std::string, Topic, std::less<>> m_topics;
    bool m_stopping = false;
    std::thread m_flusher;
};

nlohmann::json settingsOf(std::string_view text)
{
    nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
    if (!parsed.is_object()) {
        parsed = nlohmann::json::object();
    }
    for (const char* part : {"global", "devices"}) {
        if (!parsed.contains(part) || !parsed[part].is_object()) {
            parsed[part] = nlohmann::json::object();
        }
    }
    return parsed;
}

void collectChanged(const nlohmann::json& from, const nlohmann::json& to, std::set<std::string>& changed)
{
    for (const auto& [device, values] : from.items()) {
        if (!values.is_object()) {
            continue;
        }
        const auto other = to.find(device);
        for (const auto& [key, value] : values.items()) {
            if (other == to.end() || !other->is_object() || !other->contains(key) || other->at(key) != value) {
                changed.insert(key);
            }
        }
    }
}

}

struct InProcessModule::Impl {
    explicit Impl(const InProcessOptions& options)
        : store(options.store)
        , granted(options.granted)
        , services(options.services)
        , budget(options.budget)
        , gate(std::make_unique<EventGate>([target = options.services](const std::string& topic,
                                                                      const std::string& json) {
            if (target != nullptr) {
                target->emitted(topic, json);
            }
        }))
        , settings(settingsOf(options.settings))
    {
    }

    ~Impl()
    {
        if (library == nullptr) {
            return;
        }
        endJobs();
        {
            const std::scoped_lock lock(touchMutex);
            for (const auto& [id, session] : sessions) {
                const gs_ext_touch_method& method = touchMethods[session.first];
                method.close(method.context, session.second);
            }
            sessions.clear();
        }
        {
            const std::scoped_lock lock(instanceMutex);
            for (const auto& [id, instance] : instances) {
                stop(instance);
            }
            instances.clear();
        }
        if (entered && described.unload != nullptr) {
            described.unload(described.context);
        }
        FreeLibrary(library);
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    struct Instance {
        InstanceKind kind = InstanceKind::Attachment;
        std::size_t index = 0;
        void* state = nullptr;
    };

    struct Job {
        std::size_t index = 0;
        JobEvents events;
        std::chrono::steady_clock::time_point told{};
        std::chrono::steady_clock::time_point outputSecond{};
        std::size_t outputs = 0;
    };

    void endJobs()
    {
        std::vector<std::pair<std::uint64_t, std::size_t>> running;
        {
            const std::scoped_lock lock(jobMutex);
            stopping = true;
            for (const auto& [id, job] : jobs) {
                running.emplace_back(id, job.index);
            }
        }
        for (const auto& [id, index] : running) {
            nativeJobs[index].cancel(nativeJobs[index].context, id);
        }
        std::unique_lock lock(jobMutex);
        jobsEnded.wait_for(lock, kJobGrace, [this] { return jobs.empty(); });
        std::vector<JobEvents> left;
        for (auto& [id, job] : jobs) {
            left.push_back(std::move(job.events));
        }
        jobs.clear();
        lock.unlock();
        for (JobEvents& events : left) {
            if (events.done) {
                events.done(false, "the extension stopped before the job ended");
            }
        }
    }

    [[nodiscard]] bool allows(std::string_view permission) const
    {
        return std::ranges::find(granted, permission) != granted.end();
    }

    void stop(const Instance& instance) const
    {
        if (instance.kind == InstanceKind::Attachment) {
            attachments[instance.index].stop(instance.state);
        } else {
            behaviors[instance.index].stop(instance.state);
        }
    }

    HMODULE library = nullptr;
    bool entered = false;
    gs_ext_host host{};
    gs_ext_plugin described{};
    std::vector<gs_ext_touch_method> touchMethods;
    std::vector<gs_ext_node> nodes;
    std::vector<gs_ext_attachment> attachments;
    std::vector<gs_ext_behavior> behaviors;
    std::vector<gs_ext_job> nativeJobs;
    std::vector<gs_ext_device_provider> providers;
    std::mutex jobMutex;
    std::condition_variable jobsEnded;
    std::map<std::uint64_t, Job> jobs;
    std::uint64_t nextJob = 0;
    bool stopping = false;
    ModuleInfo info;
    detail::ModuleStore store;
    std::vector<std::string> granted;
    HostServices* services = nullptr;
    std::chrono::microseconds budget;
    std::unique_ptr<EventGate> gate;
    nlohmann::json settings;
    std::mutex settingsMutex;
    std::mutex nodeMutex;
    std::mutex touchMutex;
    std::map<std::uint64_t, std::pair<std::size_t, void*>> sessions;
    std::uint64_t nextSession = 0;
    std::mutex instanceMutex;
    std::map<std::uint64_t, Instance> instances;
    std::uint64_t nextInstance = 0;
};

namespace {

InProcessModule::Impl& implOf(void* context)
{
    return *static_cast<InProcessModule::Impl*>(context);
}

int32_t setText(gs_ext_result* result, uint32_t type, const char* text, size_t size)
{
    if (result == nullptr || result->host == nullptr || (text == nullptr && size > 0) || size > kMostTextBytes) {
        return GS_EXT_BAD_ARGUMENT;
    }
    if (type != GS_EXT_TYPE_STRING && type != GS_EXT_TYPE_COLOR && type != GS_EXT_TYPE_JSON) {
        return GS_EXT_BAD_ARGUMENT;
    }
    auto* box = static_cast<ResultBox*>(result->host);
    if (size == 0) {
        box->text.clear();
    } else {
        box->text.assign(text, size);
    }
    result->value.type = type;
    result->value.text = box->text.data();
    result->value.size = box->text.size();
    return GS_EXT_OK;
}

int32_t setError(gs_ext_result* result, const char* text, size_t size)
{
    if (result == nullptr || result->host == nullptr || (text == nullptr && size > 0)) {
        return GS_EXT_BAD_ARGUMENT;
    }
    auto* box = static_cast<ResultBox*>(result->host);
    if (size == 0) {
        box->error.clear();
    } else {
        box->error.assign(text, std::min(size, kMostErrorBytes));
    }
    return GS_EXT_OK;
}

void hostLog(void* context, uint32_t level, const char* text, size_t size)
{
    InProcessModule::Impl& impl = implOf(context);
    if (impl.services == nullptr || text == nullptr) {
        return;
    }
    impl.services->log(static_cast<int>(std::min<uint32_t>(level, GS_EXT_LOG_ERROR)),
                       std::string_view(text, std::min(size, kMostLogBytes)));
}

uint64_t hostNow(void*)
{
    LARGE_INTEGER counter{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    const auto ticks = static_cast<std::uint64_t>(counter.QuadPart);
    const auto perSecond = static_cast<std::uint64_t>(frequency.QuadPart);
    return perSecond == 0 ? 0 : (ticks / perSecond) * 1'000'000 + (ticks % perSecond) * 1'000'000 / perSecond;
}

int32_t hostStoreGet(void* context, const char* key, size_t keySize, gs_ext_result* value)
{
    if (key == nullptr || value == nullptr) {
        return GS_EXT_BAD_ARGUMENT;
    }
    value->value = gs_ext_value{};
    value->host = &t_storeBox;
    const std::optional<std::string> found = implOf(context).store.get(std::string_view(key, keySize));
    if (!found) {
        value->value.type = GS_EXT_TYPE_NONE;
        return GS_EXT_OK;
    }
    return setText(value, GS_EXT_TYPE_JSON, found->data(), found->size());
}

int32_t hostStoreSet(void* context, const char* key, size_t keySize, const char* json, size_t jsonSize)
{
    if (key == nullptr || (json == nullptr && jsonSize > 0)) {
        return GS_EXT_BAD_ARGUMENT;
    }
    const std::optional<std::string_view> text =
        json == nullptr ? std::nullopt : std::optional<std::string_view>(std::string_view(json, jsonSize));
    switch (implOf(context).store.set(std::string_view(key, keySize), text)) {
    case detail::StoreResult::Ok:
        return GS_EXT_OK;
    case detail::StoreResult::BadArgument:
    case detail::StoreResult::Full:
        return GS_EXT_BAD_ARGUMENT;
    case detail::StoreResult::Unavailable:
        return GS_EXT_UNSUPPORTED;
    case detail::StoreResult::Failed:
        return GS_EXT_FAILED;
    }
    return GS_EXT_FAILED;
}

int32_t hostAllowed(void* context, const char* permission)
{
    if (permission == nullptr) {
        return 0;
    }
    return implOf(context).allows(std::string_view(permission, strnlen(permission, kMostNameBytes))) ? 1 : 0;
}

int32_t hostNotify(void* context, const char* title, size_t titleSize, const char* text, size_t textSize)
{
    InProcessModule::Impl& impl = implOf(context);
    if ((title == nullptr && titleSize > 0) || (text == nullptr && textSize > 0)) {
        return GS_EXT_BAD_ARGUMENT;
    }
    if (!impl.allows("notify")) {
        return GS_EXT_DENIED;
    }
    const std::string_view heading = titleSize == 0 ? std::string_view() : std::string_view(title, titleSize);
    const std::string_view body = textSize == 0 ? std::string_view() : std::string_view(text, textSize);
    if (impl.services == nullptr || !validUtf8(heading) || !validUtf8(body)) {
        return GS_EXT_FAILED;
    }
    return impl.services->notify(heading.substr(0, 200), body.substr(0, 1000)) ? GS_EXT_OK : GS_EXT_FAILED;
}

int32_t hostSupports(void*, const char* feature)
{
    if (feature == nullptr) {
        return 0;
    }
    const std::string_view name(feature, strnlen(feature, kMostNameBytes));
    return name == "jobs" || name == "events" || name == "settings" || name == "claims" || name == "job-output" ? 1 : 0;
}

int32_t hostSettingGet(void* context, const char* key, size_t keySize, const char* phone, size_t phoneSize,
                       gs_ext_result* value)
{
    InProcessModule::Impl& impl = implOf(context);
    if (key == nullptr || keySize == 0 || (phone == nullptr && phoneSize > 0) || value == nullptr) {
        return GS_EXT_BAD_ARGUMENT;
    }
    const std::string name(key, keySize);
    const std::string device = phoneSize == 0 ? std::string() : std::string(phone, phoneSize);
    std::string text;
    {
        const std::scoped_lock lock(impl.settingsMutex);
        const nlohmann::json& global = impl.settings.at("global");
        if (!global.contains(name)) {
            return GS_EXT_BAD_ARGUMENT;
        }
        const nlohmann::json* found = &global.at(name);
        const nlohmann::json& devices = impl.settings.at("devices");
        if (!device.empty() && devices.contains(device) && devices.at(device).is_object() &&
            devices.at(device).contains(name)) {
            found = &devices.at(device).at(name);
        }
        text = found->dump();
    }
    thread_local std::string held;
    held = std::move(text);
    value->value = {};
    value->value.type = GS_EXT_TYPE_JSON;
    value->value.text = held.data();
    value->value.size = held.size();
    return GS_EXT_OK;
}

int32_t hostDevicesChanged(void* context, const char* provider, size_t size)
{
    InProcessModule::Impl& impl = implOf(context);
    if (provider == nullptr || size == 0 || size > kMostNameBytes) {
        return GS_EXT_BAD_ARGUMENT;
    }
    const std::string_view name(provider, size);
    if (std::ranges::find(impl.info.providers, name, &ProviderInfo::name) == impl.info.providers.end()) {
        return GS_EXT_BAD_ARGUMENT;
    }
    if (impl.services != nullptr) {
        impl.services->devicesChanged(name);
    }
    return GS_EXT_OK;
}

int32_t hostClaim(void* context, const char* resource, size_t size, uint32_t flags)
{
    InProcessModule::Impl& impl = implOf(context);
    if (resource == nullptr || size == 0 || (flags & ~GS_EXT_CLAIM_WAIT) != 0 ||
        !validResource(std::string_view(resource, size))) {
        return GS_EXT_BAD_ARGUMENT;
    }
    if (impl.services == nullptr) {
        return GS_EXT_UNSUPPORTED;
    }
    return static_cast<int32_t>(
        impl.services->claim(std::string_view(resource, size), (flags & GS_EXT_CLAIM_WAIT) != 0));
}

int32_t hostRelease(void* context, const char* resource, size_t size)
{
    InProcessModule::Impl& impl = implOf(context);
    if (resource == nullptr || size == 0 || !validResource(std::string_view(resource, size))) {
        return GS_EXT_BAD_ARGUMENT;
    }
    if (impl.services != nullptr) {
        impl.services->release(std::string_view(resource, size));
    }
    return GS_EXT_OK;
}

int32_t hostEmitEvent(void* context, const char* topic, size_t topicSize, const char* json, size_t jsonSize)
{
    InProcessModule::Impl& impl = implOf(context);
    if ((topic == nullptr && topicSize > 0) || (json == nullptr && jsonSize > 0) || jsonSize > kMostEventBytes) {
        return GS_EXT_BAD_ARGUMENT;
    }
    const std::string_view name = topicSize == 0 ? std::string_view() : std::string_view(topic, topicSize);
    const std::string_view value = jsonSize == 0 ? std::string_view("null") : std::string_view(json, jsonSize);
    if (!validTopic(name) || !validUtf8(value) || !nlohmann::json::accept(value)) {
        return GS_EXT_BAD_ARGUMENT;
    }
    return impl.gate->emit(std::string(name), std::string(value)) ? GS_EXT_OK : GS_EXT_FAILED;
}

std::string lineOf(const char* text, size_t size, std::size_t most)
{
    if (text == nullptr || size == 0) {
        return {};
    }
    std::string_view line(text, std::min(size, most));
    while (!line.empty() && !validUtf8(line)) {
        line.remove_suffix(1);
    }
    return std::string(line);
}

int32_t hostJobProgress(void* context, uint64_t job, double fraction, const char* text, size_t size)
{
    InProcessModule::Impl& impl = implOf(context);
    if (text == nullptr && size > 0) {
        return GS_EXT_BAD_ARGUMENT;
    }
    std::function<void(double, std::string)> progress;
    {
        const std::scoped_lock lock(impl.jobMutex);
        const auto found = impl.jobs.find(job);
        if (found == impl.jobs.end()) {
            return GS_EXT_BAD_ARGUMENT;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - found->second.told < kProgressGap) {
            return GS_EXT_OK;
        }
        found->second.told = now;
        progress = found->second.events.progress;
    }
    if (progress) {
        progress(fraction >= 0.0 && fraction <= 1.0 ? fraction : -1.0, lineOf(text, size, kMostJobLineBytes));
    }
    return GS_EXT_OK;
}

int32_t hostJobLog(void* context, uint64_t job, uint32_t level, const char* text, size_t size)
{
    InProcessModule::Impl& impl = implOf(context);
    if (text == nullptr && size > 0) {
        return GS_EXT_BAD_ARGUMENT;
    }
    std::function<void(int, std::string)> log;
    {
        const std::scoped_lock lock(impl.jobMutex);
        const auto found = impl.jobs.find(job);
        if (found == impl.jobs.end()) {
            return GS_EXT_BAD_ARGUMENT;
        }
        log = found->second.events.log;
    }
    if (log) {
        log(static_cast<int>(std::min<uint32_t>(level, GS_EXT_LOG_ERROR)), lineOf(text, size, kMostJobLineBytes));
    }
    return GS_EXT_OK;
}

int32_t hostJobOutput(void* context, uint64_t job, const char* json, size_t size)
{
    InProcessModule::Impl& impl = implOf(context);
    if ((json == nullptr && size > 0) || size > kMostJobOutputBytes) {
        return GS_EXT_BAD_ARGUMENT;
    }
    const std::string_view value = size == 0 ? std::string_view("null") : std::string_view(json, size);
    if (!validUtf8(value) || !nlohmann::json::accept(value)) {
        return GS_EXT_BAD_ARGUMENT;
    }
    std::function<void(std::string)> output;
    {
        const std::scoped_lock lock(impl.jobMutex);
        const auto found = impl.jobs.find(job);
        if (found == impl.jobs.end()) {
            return GS_EXT_BAD_ARGUMENT;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - found->second.outputSecond >= std::chrono::seconds(1)) {
            found->second.outputSecond = now;
            found->second.outputs = 0;
        }
        if (found->second.outputs >= kMostJobOutputsPerSecond) {
            return GS_EXT_BUSY;
        }
        ++found->second.outputs;
        output = found->second.events.output;
    }
    if (output) {
        output(std::string(value));
    }
    return GS_EXT_OK;
}

struct EndedJob {
    JobEvents events;
    bool stopping = false;
};

std::optional<EndedJob> endedJob(InProcessModule::Impl& impl, uint64_t job)
{
    const std::scoped_lock lock(impl.jobMutex);
    const auto found = impl.jobs.find(job);
    if (found == impl.jobs.end()) {
        return std::nullopt;
    }
    EndedJob ended{.events = std::move(found->second.events), .stopping = impl.stopping};
    impl.jobs.erase(found);
    impl.jobsEnded.notify_all();
    return ended;
}

int32_t hostJobFinish(void* context, uint64_t job, const char* json, size_t size)
{
    if ((json == nullptr && size > 0) || size > kMostJobResultBytes) {
        return GS_EXT_BAD_ARGUMENT;
    }
    const std::string_view result = size == 0 ? std::string_view("null") : std::string_view(json, size);
    if (!validUtf8(result)) {
        return GS_EXT_BAD_ARGUMENT;
    }
    std::optional<EndedJob> ended = endedJob(implOf(context), job);
    if (!ended) {
        return GS_EXT_BAD_ARGUMENT;
    }
    if (ended->events.done) {
        ended->events.done(true, std::string(result));
    }
    return GS_EXT_OK;
}

int32_t hostJobFail(void* context, uint64_t job, const char* text, size_t size)
{
    if (text == nullptr && size > 0) {
        return GS_EXT_BAD_ARGUMENT;
    }
    std::optional<EndedJob> ended = endedJob(implOf(context), job);
    if (!ended) {
        return GS_EXT_BAD_ARGUMENT;
    }
    if (ended->events.done) {
        std::string problem = lineOf(text, size, kMostErrorBytes);
        if (ended->stopping) {
            problem = "the extension stopped before the job ended";
        } else if (problem.empty()) {
            problem = "the job did not work";
        }
        ended->events.done(false, std::move(problem));
    }
    return GS_EXT_OK;
}

template <typename Native>
bool copied(const void* data, Native& out)
{
    if (data == nullptr) {
        return false;
    }
    std::uint32_t declared = 0;
    std::memcpy(&declared, data, sizeof(declared));
    if (declared < sizeof(declared)) {
        return false;
    }
    out = Native{};
    std::memcpy(&out, data, std::min<std::size_t>(declared, sizeof(Native)));
    return true;
}

std::string named(const char* kind, const char* own, const gs_ext_contribution& entry,
                  std::set<std::string, std::less<>>& names, std::string& name)
{
    const std::optional<std::string> found = nameOf(own);
    const std::optional<std::string> listed = nameOf(entry.name);
    if (!found || !listed || *found != *listed) {
        return std::string("a ") + kind + " has no plain name, or not the name of its contribution";
    }
    if (!names.insert(std::string(kind) + ":" + *found).second) {
        return std::string("the ") + kind + " " + *found + " comes twice";
    }
    name = *found;
    return {};
}

std::string addTouchMethod(InProcessModule::Impl& impl, const gs_ext_contribution& entry,
                           std::set<std::string, std::less<>>& names)
{
    gs_ext_touch_method method{};
    std::string name;
    if (!copied(entry.data, method)) {
        return "a touch method has no data";
    }
    if (std::string problem = named("touch method", method.name, entry, names, name); !problem.empty()) {
        return problem;
    }
    if (method.max_contacts < 1 || method.max_contacts > kMostContacts || method.open == nullptr ||
        method.close == nullptr || method.frame == nullptr) {
        return "the touch method " + name + " lacks open, close or frame, or takes no fingers";
    }
    impl.touchMethods.push_back(method);
    impl.info.touchMethods.push_back({.name = name,
                                      .maxContacts = static_cast<int>(method.max_contacts),
                                      .canCancel = method.can_cancel != 0,
                                      .needsSession = method.needs_session != 0,
                                      .keys = method.key != nullptr,
                                      .text = method.text != nullptr,
                                      .mouse = method.mouse != nullptr,
                                      .gamepad = method.gamepad != nullptr});
    return {};
}

std::string addNode(InProcessModule::Impl& impl, const gs_ext_contribution& entry,
                    std::set<std::string, std::less<>>& names)
{
    gs_ext_node node{};
    std::string name;
    if (!copied(entry.data, node)) {
        return "a node has no data";
    }
    if (std::string problem = named("node", node.name, entry, names, name); !problem.empty()) {
        return problem;
    }
    if (node.call == nullptr || node.param_count > kMostParams || (node.param_count > 0 && node.params == nullptr) ||
        node.returns > GS_EXT_TYPE_JSON) {
        return "the node " + name + " lacks call or has parameters or a result of no known type";
    }
    NodeInfo described{.name = name, .params = {}, .returns = static_cast<ValueType>(node.returns)};
    for (std::uint32_t param = 0; param < node.param_count; ++param) {
        if (node.params[param] < GS_EXT_TYPE_BOOLEAN || node.params[param] > GS_EXT_TYPE_JSON) {
            return "parameter " + std::to_string(param + 1) + " of the node " + name + " has no known type";
        }
        described.params.push_back(static_cast<ValueType>(node.params[param]));
    }
    impl.nodes.push_back(node);
    impl.info.nodes.push_back(std::move(described));
    return {};
}

std::string addAttachment(InProcessModule::Impl& impl, const gs_ext_contribution& entry,
                          std::set<std::string, std::less<>>& names)
{
    gs_ext_attachment attachment{};
    std::string name;
    if (!copied(entry.data, attachment)) {
        return "an attachment has no data";
    }
    if (std::string problem = named("attachment", attachment.name, entry, names, name); !problem.empty()) {
        return problem;
    }
    if (attachment.start == nullptr || attachment.input == nullptr || attachment.output == nullptr ||
        attachment.stop == nullptr) {
        return "the attachment " + name + " lacks start, input, output or stop";
    }
    impl.attachments.push_back(attachment);
    impl.info.attachments.push_back({.name = name});
    return {};
}

std::string addBehavior(InProcessModule::Impl& impl, const gs_ext_contribution& entry,
                        std::set<std::string, std::less<>>& names)
{
    gs_ext_behavior behavior{};
    std::string name;
    if (!copied(entry.data, behavior)) {
        return "a behavior has no data";
    }
    if (std::string problem = named("behavior", behavior.name, entry, names, name); !problem.empty()) {
        return problem;
    }
    if (behavior.start == nullptr || behavior.input == nullptr || behavior.stop == nullptr ||
        (behavior.wants_frames != 0 && behavior.frame == nullptr)) {
        return "the behavior " + name + " lacks start, input, frame or stop";
    }
    if (behavior.slot_count > kMostBehaviorSlots || (behavior.slot_count > 0 && behavior.slot_kinds == nullptr) ||
        behavior.max_fingers < 1 || behavior.max_fingers > kMostFingers) {
        return "the behavior " + name + " has more than 8 slots, or no fingers or more than 10";
    }
    BehaviorInfo described{.name = name,
                           .slotKinds = {},
                           .fingers = static_cast<int>(behavior.max_fingers),
                           .frames = behavior.wants_frames != 0};
    for (std::uint32_t slot = 0; slot < behavior.slot_count; ++slot) {
        if (behavior.slot_kinds[slot] > GS_EXT_SLOT_AXIS1) {
            return "slot " + std::to_string(slot + 1) + " of the behavior " + name + " has no known kind";
        }
        described.slotKinds.push_back(static_cast<SlotKind>(behavior.slot_kinds[slot]));
    }
    impl.behaviors.push_back(behavior);
    impl.info.behaviors.push_back(std::move(described));
    return {};
}

std::string addJob(InProcessModule::Impl& impl, const gs_ext_contribution& entry,
                   std::set<std::string, std::less<>>& names)
{
    gs_ext_job job{};
    std::string name;
    if (!copied(entry.data, job)) {
        return "a job has no data";
    }
    if (std::string problem = named("job", job.name, entry, names, name); !problem.empty()) {
        return problem;
    }
    if (job.start == nullptr || job.cancel == nullptr) {
        return "the job " + name + " lacks start or cancel";
    }
    impl.nativeJobs.push_back(job);
    impl.info.jobs.push_back({.name = name});
    return {};
}

std::string addProvider(InProcessModule::Impl& impl, const gs_ext_contribution& entry,
                        std::set<std::string, std::less<>>& names)
{
    gs_ext_device_provider provider{};
    std::string name;
    if (!copied(entry.data, provider)) {
        return "a device provider has no data";
    }
    if (std::string problem = named("device provider", provider.name, entry, names, name); !problem.empty()) {
        return problem;
    }
    if (provider.list == nullptr) {
        return "the device provider " + name + " lacks list";
    }
    impl.providers.push_back(provider);
    impl.info.providers.push_back({.name = name, .acts = provider.act != nullptr});
    return {};
}

std::string describe(InProcessModule::Impl& impl, const gs_ext_plugin* plugin)
{
    if (!copied(plugin, impl.described)) {
        return "gs_ext_main gave no plugin";
    }
    const gs_ext_plugin& described = impl.described;
    if (described.abi_version != GS_EXT_ABI_VERSION) {
        return "the DLL was built for interface version " + std::to_string(described.abi_version) + ", not " +
               std::to_string(GS_EXT_ABI_VERSION);
    }
    if (described.contribution_count > kMostContributions ||
        (described.contribution_count > 0 && described.contributions == nullptr)) {
        return "the DLL offers too many contributions or none where it says it has some";
    }
    std::uint32_t stride = 0;
    if (described.contribution_count > 0) {
        std::memcpy(&stride, described.contributions, sizeof(stride));
        if (stride < offsetof(gs_ext_contribution, data) + sizeof(const void*)) {
            return "the contributions of the DLL do not say their size";
        }
    }
    const auto* base = reinterpret_cast<const unsigned char*>(described.contributions);
    std::set<std::string, std::less<>> names;
    for (std::uint32_t index = 0; index < described.contribution_count; ++index) {
        gs_ext_contribution entry{};
        std::memcpy(&entry, base + std::size_t{index} * stride, std::min<std::size_t>(stride, sizeof(entry)));
        const std::string kind =
            entry.kind == nullptr ? std::string() : std::string(entry.kind, strnlen(entry.kind, kMostNameBytes));
        std::string problem;
        if (kind == GS_EXT_KIND_TOUCH_METHOD) {
            problem = addTouchMethod(impl, entry, names);
        } else if (kind == GS_EXT_KIND_NODE) {
            problem = addNode(impl, entry, names);
        } else if (kind == GS_EXT_KIND_ATTACHMENT) {
            problem = addAttachment(impl, entry, names);
        } else if (kind == GS_EXT_KIND_BEHAVIOR) {
            problem = addBehavior(impl, entry, names);
        } else if (kind == GS_EXT_KIND_JOB) {
            problem = addJob(impl, entry, names);
        } else if (kind == GS_EXT_KIND_DEVICE_PROVIDER) {
            problem = addProvider(impl, entry, names);
        } else if (kind.empty()) {
            problem = "contribution " + std::to_string(index + 1) + " has no kind";
        } else if (impl.services != nullptr) {
            impl.services->log(GS_EXT_LOG_WARNING, "the DLL offers a " + kind +
                                                       ", which this GameScrcpy does not know; it is left out");
        }
        if (!problem.empty()) {
            return problem;
        }
    }
    if (impl.touchMethods.size() > kMostTouchMethods || impl.nodes.size() > kMostNodes ||
        impl.attachments.size() > kMostModuleAttachments || impl.behaviors.size() > kMostModuleBehaviors ||
        impl.nativeJobs.size() > kMostJobs || impl.providers.size() > kMostProviders) {
        return "the DLL offers more than 8 touch methods, 64 nodes, 32 attachments, 32 behaviors, 32 jobs or 8 "
               "device providers";
    }
    return {};
}

}

InProcessModule::InProcessModule(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

InProcessModule::~InProcessModule() = default;

std::unique_ptr<InProcessModule> InProcessModule::load(InProcessOptions options, std::string& problem)
{
    std::error_code error;
    const std::filesystem::path dll = std::filesystem::absolute(options.dll, error);
    if (error) {
        problem = "the DLL path cannot be resolved";
        return nullptr;
    }
    auto impl = std::make_unique<Impl>(options);
    impl->library =
        LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (impl->library == nullptr) {
        problem = "Windows could not load the DLL (error " + std::to_string(GetLastError()) + ")";
        return nullptr;
    }
    const auto entry = reinterpret_cast<gs_ext_main_function>(GetProcAddress(impl->library, "gs_ext_main"));
    if (entry == nullptr) {
        problem = GetProcAddress(impl->library, "gs_ext_entry") != nullptr
                      ? "the DLL was made for an older GameScrcpy (interface version 1); its author has to build it "
                        "again with the current SDK"
                      : "the DLL does not export gs_ext_main";
        return nullptr;
    }
    impl->host.size = sizeof(gs_ext_host);
    impl->host.abi_version = GS_EXT_ABI_VERSION;
    impl->host.context = impl.get();
    impl->host.log = &hostLog;
    impl->host.now_us = &hostNow;
    impl->host.store_get = &hostStoreGet;
    impl->host.store_set = &hostStoreSet;
    impl->host.allowed = &hostAllowed;
    impl->host.notify = &hostNotify;
    impl->host.set_text = &setText;
    impl->host.set_error = &setError;
    impl->host.supports = &hostSupports;
    impl->host.job_progress = &hostJobProgress;
    impl->host.job_log = &hostJobLog;
    impl->host.job_finish = &hostJobFinish;
    impl->host.job_fail = &hostJobFail;
    impl->host.emit_event = &hostEmitEvent;
    impl->host.setting_get = &hostSettingGet;
    impl->host.devices_changed = &hostDevicesChanged;
    impl->host.claim = &hostClaim;
    impl->host.release = &hostRelease;
    impl->host.job_output = &hostJobOutput;
    const gs_ext_plugin* plugin = nullptr;
    const int32_t status = entry(&impl->host, &plugin);
    impl->entered = status == GS_EXT_OK;
    if (!impl->entered) {
        problem = "gs_ext_main returned " + std::to_string(status);
        return nullptr;
    }
    problem = describe(*impl, plugin);
    if (!problem.empty()) {
        impl->info = {};
        impl->touchMethods.clear();
        impl->nodes.clear();
        impl->attachments.clear();
        impl->behaviors.clear();
        impl->nativeJobs.clear();
        return nullptr;
    }
    return std::unique_ptr<InProcessModule>(new InProcessModule(std::move(impl)));
}

const ModuleInfo& InProcessModule::info() const noexcept
{
    return m_impl->info;
}

CallResult InProcessModule::call(std::size_t node, std::span<const Value> arguments)
{
    Impl& d = *m_impl;
    if (node >= d.info.nodes.size()) {
        return {CallStatus::Unavailable, {}, "the DLL has no such node"};
    }
    const NodeInfo& declared = d.info.nodes[node];
    if (arguments.size() != declared.params.size()) {
        return {CallStatus::BadArgument, {},
                "the node takes " + std::to_string(declared.params.size()) + " values, not " +
                    std::to_string(arguments.size())};
    }
    std::vector<gs_ext_value> values;
    values.reserve(arguments.size());
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (const std::string problem = checkValue(arguments[index], declared.params[index]); !problem.empty()) {
            return {CallStatus::BadArgument, {}, "value " + std::to_string(index + 1) + ": " + problem};
        }
        values.push_back(toNative(arguments[index]));
    }
    ResultBox box;
    gs_ext_result result{};
    result.host = &box;
    const gs_ext_node& entry = d.nodes[node];
    int32_t status = GS_EXT_FAILED;
    std::chrono::steady_clock::duration took{};
    {
        const std::scoped_lock lock(d.nodeMutex);
        const auto started = std::chrono::steady_clock::now();
        status = entry.call(entry.context, values.empty() ? nullptr : values.data(),
                            static_cast<std::uint32_t>(values.size()), &result);
        took = std::chrono::steady_clock::now() - started;
    }
    if (status != GS_EXT_OK) {
        return {CallStatus::Failed, {},
                box.error.empty() ? "the node failed with status " + std::to_string(status) : box.error};
    }
    std::string problem;
    std::optional<Value> value = fromNative(result.value, problem);
    if (value) {
        problem = checkValue(*value, declared.returns);
    }
    if (!problem.empty()) {
        return {CallStatus::Failed, {}, "the result: " + problem};
    }
    if (took > d.budget) {
        return {CallStatus::TooSlow, {},
                "the node took " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(took).count()) +
                    " ms, more than its " +
                    std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(d.budget).count()) + " ms"};
    }
    return {CallStatus::Ok, std::move(*value), {}};
}

std::uint64_t InProcessModule::openTouch(std::size_t method, std::string_view device, std::string_view options)
{
    Impl& d = *m_impl;
    if (method >= d.info.touchMethods.size()) {
        return 0;
    }
    const std::string serial(device);
    const std::string chosen = options.empty() ? std::string("{}") : std::string(options);
    gs_ext_touch_open open{};
    open.size = sizeof(gs_ext_touch_open);
    open.device = serial.c_str();
    open.options = chosen.data();
    open.options_size = chosen.size();
    void* session = nullptr;
    const std::scoped_lock lock(d.touchMutex);
    const gs_ext_touch_method& entry = d.touchMethods[method];
    if (entry.open(entry.context, &open, &session) != GS_EXT_OK) {
        return 0;
    }
    const std::uint64_t id = ++d.nextSession;
    d.sessions.emplace(id, std::pair(method, session));
    return id;
}

bool InProcessModule::touch(std::uint64_t session, const TouchFrame& frame)
{
    Impl& d = *m_impl;
    const std::scoped_lock lock(d.touchMutex);
    const auto found = d.sessions.find(session);
    if (found == d.sessions.end()) {
        return false;
    }
    const gs_ext_touch_method& entry = d.touchMethods[found->second.first];
    const std::size_t count = std::min<std::size_t>(frame.contacts.size(), entry.max_contacts);
    std::array<gs_ext_contact, kMostContacts> contacts{};
    for (std::size_t index = 0; index < count; ++index) {
        const Contact& contact = frame.contacts[index];
        contacts[index] = gs_ext_contact{contact.id,
                                         static_cast<std::uint32_t>(contact.action),
                                         contact.x,
                                         contact.y,
                                         contact.naturalX,
                                         contact.naturalY,
                                         contact.pressure};
    }
    const gs_ext_touch_frame native{sizeof(gs_ext_touch_frame), contacts.data(), static_cast<std::uint32_t>(count),
                                    frame.rotation & 3U};
    return entry.frame(entry.context, found->second.second, &native) == GS_EXT_OK;
}

bool InProcessModule::key(std::uint64_t session, const KeyEvent& key)
{
    Impl& d = *m_impl;
    const std::scoped_lock lock(d.touchMutex);
    const auto found = d.sessions.find(session);
    if (found == d.sessions.end()) {
        return false;
    }
    const gs_ext_touch_method& entry = d.touchMethods[found->second.first];
    if (entry.key == nullptr) {
        return false;
    }
    const gs_ext_key native{key.keycode, key.down ? 1U : 0U, key.repeat, key.meta};
    return entry.key(entry.context, found->second.second, &native) == GS_EXT_OK;
}

bool InProcessModule::text(std::uint64_t session, std::string_view utf8)
{
    Impl& d = *m_impl;
    if (utf8.empty() || utf8.size() > kMostTextBytes || !validUtf8(utf8)) {
        return false;
    }
    const std::scoped_lock lock(d.touchMutex);
    const auto found = d.sessions.find(session);
    if (found == d.sessions.end()) {
        return false;
    }
    const gs_ext_touch_method& entry = d.touchMethods[found->second.first];
    return entry.text != nullptr && entry.text(entry.context, found->second.second, utf8.data(), utf8.size()) == GS_EXT_OK;
}

bool InProcessModule::mouse(std::uint64_t session, const MouseEvent& mouse)
{
    Impl& d = *m_impl;
    const std::scoped_lock lock(d.touchMutex);
    const auto found = d.sessions.find(session);
    if (found == d.sessions.end()) {
        return false;
    }
    const gs_ext_touch_method& entry = d.touchMethods[found->second.first];
    if (entry.mouse == nullptr) {
        return false;
    }
    const gs_ext_mouse native{mouse.buttons, mouse.dx, mouse.dy, mouse.wheel};
    return entry.mouse(entry.context, found->second.second, &native) == GS_EXT_OK;
}

bool InProcessModule::gamepad(std::uint64_t session, const PadEvent& pad)
{
    Impl& d = *m_impl;
    const std::scoped_lock lock(d.touchMutex);
    const auto found = d.sessions.find(session);
    if (found == d.sessions.end()) {
        return false;
    }
    const gs_ext_touch_method& entry = d.touchMethods[found->second.first];
    if (entry.gamepad == nullptr) {
        return false;
    }
    const gs_ext_gamepad native{pad.buttons,   pad.hat,    pad.leftX,       pad.leftY,
                                pad.rightX,    pad.rightY, pad.leftTrigger, pad.rightTrigger};
    return entry.gamepad(entry.context, found->second.second, &native) == GS_EXT_OK;
}

void InProcessModule::closeTouch(std::uint64_t session)
{
    Impl& d = *m_impl;
    const std::scoped_lock lock(d.touchMutex);
    const auto found = d.sessions.find(session);
    if (found == d.sessions.end()) {
        return;
    }
    const gs_ext_touch_method& entry = d.touchMethods[found->second.first];
    entry.close(entry.context, found->second.second);
    d.sessions.erase(found);
}

std::uint64_t InProcessModule::openInstance(InstanceKind kind, std::size_t index, std::string_view params,
                                            std::uint64_t seed)
{
    Impl& d = *m_impl;
    const std::size_t count = kind == InstanceKind::Attachment ? d.info.attachments.size() : d.info.behaviors.size();
    if (index >= count || params.size() > kMostParamBytes) {
        return 0;
    }
    const std::string text(params.empty() ? std::string_view("{}") : params);
    void* state = nullptr;
    const std::scoped_lock lock(d.instanceMutex);
    int32_t status = GS_EXT_FAILED;
    if (kind == InstanceKind::Attachment) {
        const gs_ext_attachment& entry = d.attachments[index];
        status = entry.start(entry.context, text.data(), text.size(), seed, &state);
    } else {
        const gs_ext_behavior& entry = d.behaviors[index];
        status = entry.start(entry.context, text.data(), text.size(), seed, &state);
    }
    if (status != GS_EXT_OK) {
        return 0;
    }
    const std::uint64_t id = ++d.nextInstance;
    d.instances.emplace(id, Impl::Instance{.kind = kind, .index = index, .state = state});
    return id;
}

StepOutput InProcessModule::step(std::uint64_t instance, const StepInput& input)
{
    Impl& d = *m_impl;
    const std::scoped_lock lock(d.instanceMutex);
    const auto found = d.instances.find(instance);
    if (found == d.instances.end()) {
        return {.status = StepStatus::Failed};
    }
    const Impl::Instance& running = found->second;
    std::array<gs_ext_touch_command, kMostStepCommands> commands{};
    std::array<gs_ext_input_event, kMostStepEvents> events{};
    std::span<const gs_ext_touch_command> commandRoom = commands;
    std::span<const gs_ext_input_event> eventRoom;
    gs_ext_output out{};
    out.size = sizeof(gs_ext_output);
    out.commands = commands.data();
    out.command_capacity = static_cast<std::uint32_t>(commands.size());
    out.wake_at = input.call == StepCall::Wake ? 0 : input.wakeAt;
    const gs_ext_input_event event = nativeEvent(input.event);
    const gs_ext_touch_command command = nativeCommand(input.command);
    int32_t status = GS_EXT_OK;
    if (running.kind == InstanceKind::Attachment) {
        const gs_ext_attachment& entry = d.attachments[running.index];
        if (input.call == StepCall::Input || input.call == StepCall::Wake) {
            out.events = events.data();
            out.event_capacity = static_cast<std::uint32_t>(events.size());
            eventRoom = events;
        }
        switch (input.call) {
        case StepCall::Input:
            events[0] = event;
            out.event_count = 1;
            out.commands = nullptr;
            out.command_capacity = 0;
            commandRoom = {};
            status = entry.input(running.state, &event, &out);
            break;
        case StepCall::Output:
            commands[0] = command;
            out.command_count = 1;
            status = entry.output(running.state, &command, &out);
            break;
        case StepCall::Wake:
            status = entry.wake != nullptr ? entry.wake(running.state, input.now, &out) : GS_EXT_OK;
            break;
        case StepCall::Frame:
            return {.status = StepStatus::Failed};
        }
    } else {
        const gs_ext_behavior& entry = d.behaviors[running.index];
        switch (input.call) {
        case StepCall::Input:
            status = entry.input(running.state, &event, &out);
            break;
        case StepCall::Frame:
            status = entry.frame != nullptr ? entry.frame(running.state, input.now, &out) : GS_EXT_OK;
            break;
        case StepCall::Wake:
            status = entry.wake != nullptr ? entry.wake(running.state, input.now, &out) : GS_EXT_OK;
            break;
        case StepCall::Output:
            return {.status = StepStatus::Failed};
        }
    }
    return outputOf(status, out, commandRoom, eventRoom);
}

void InProcessModule::resetInstance(std::uint64_t instance)
{
    Impl& d = *m_impl;
    const std::scoped_lock lock(d.instanceMutex);
    const auto found = d.instances.find(instance);
    if (found == d.instances.end()) {
        return;
    }
    const Impl::Instance& running = found->second;
    if (running.kind == InstanceKind::Attachment) {
        if (const gs_ext_attachment& entry = d.attachments[running.index]; entry.reset != nullptr) {
            entry.reset(running.state);
        }
    } else if (const gs_ext_behavior& entry = d.behaviors[running.index]; entry.reset != nullptr) {
        entry.reset(running.state);
    }
}

void InProcessModule::closeInstance(std::uint64_t instance)
{
    Impl& d = *m_impl;
    const std::scoped_lock lock(d.instanceMutex);
    const auto found = d.instances.find(instance);
    if (found == d.instances.end()) {
        return;
    }
    d.stop(found->second);
    d.instances.erase(found);
}

std::uint64_t InProcessModule::startJob(std::size_t job, std::string_view args, JobEvents events)
{
    Impl& d = *m_impl;
    if (job >= d.nativeJobs.size() || args.size() > kMostParamBytes || !validUtf8(args)) {
        return 0;
    }
    std::uint64_t id = 0;
    {
        const std::scoped_lock lock(d.jobMutex);
        if (d.jobs.size() >= kMostRunningJobs) {
            return 0;
        }
        id = ++d.nextJob;
        d.jobs.emplace(id, Impl::Job{.index = job, .events = std::move(events), .told = {}});
    }
    const gs_ext_job& entry = d.nativeJobs[job];
    const std::string text = args.empty() ? std::string("{}") : std::string(args);
    if (entry.start(entry.context, id, text.data(), text.size()) != GS_EXT_OK) {
        std::optional<EndedJob> gone = endedJob(d, id);
        if (gone && gone->events.done) {
            gone->events.done(false, "the job did not start");
        }
    }
    return id;
}

void InProcessModule::cancelJob(std::uint64_t job)
{
    Impl& d = *m_impl;
    std::size_t index = 0;
    {
        const std::scoped_lock lock(d.jobMutex);
        const auto found = d.jobs.find(job);
        if (found == d.jobs.end()) {
            return;
        }
        index = found->second.index;
    }
    d.nativeJobs[index].cancel(d.nativeJobs[index].context, job);
}

void InProcessModule::setSettings(std::string_view json)
{
    Impl& d = *m_impl;
    nlohmann::json next = settingsOf(json);
    std::set<std::string> changed;
    {
        const std::scoped_lock lock(d.settingsMutex);
        const nlohmann::json& global = d.settings.at("global");
        for (const auto& [key, value] : next.at("global").items()) {
            if (!global.contains(key) || global.at(key) != value) {
                changed.insert(key);
            }
        }
        collectChanged(next.at("devices"), d.settings.at("devices"), changed);
        collectChanged(d.settings.at("devices"), next.at("devices"), changed);
        d.settings = std::move(next);
    }
    if (!changed.empty() && d.described.settings_changed != nullptr) {
        const std::string keys = nlohmann::json(changed).dump();
        d.described.settings_changed(d.described.context, keys.data(), keys.size());
    }
}

CallResult InProcessModule::listDevices(std::size_t provider)
{
    Impl& d = *m_impl;
    if (provider >= d.providers.size()) {
        return {CallStatus::Unavailable, {}, "the DLL has no such device provider"};
    }
    ResultBox box;
    gs_ext_result result{};
    result.host = &box;
    const gs_ext_device_provider& entry = d.providers[provider];
    int32_t status = GS_EXT_FAILED;
    {
        const std::scoped_lock lock(d.nodeMutex);
        status = entry.list(entry.context, &result);
    }
    if (status != GS_EXT_OK) {
        return {CallStatus::Failed, {},
                box.error.empty() ? "the device provider failed with status " + std::to_string(status) : box.error};
    }
    std::string text = result.value.type != GS_EXT_TYPE_JSON || result.value.size == 0
                           ? std::string()
                           : std::string(result.value.text, result.value.size);
    const nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
    if (!parsed.is_array()) {
        return {CallStatus::Failed, {}, "the device provider did not give a JSON array"};
    }
    return {CallStatus::Ok, Value{.type = ValueType::Json, .text = std::move(text)}, {}};
}

CallResult InProcessModule::actOnDevice(std::size_t provider, std::string_view device, std::string_view action)
{
    Impl& d = *m_impl;
    if (provider >= d.providers.size() || d.providers[provider].act == nullptr) {
        return {CallStatus::Unavailable, {}, "the device provider has no actions"};
    }
    if (device.empty() || device.size() > kMostDeviceBytes || action.empty() || action.size() > kMostActionBytes) {
        return {CallStatus::BadArgument, {}, "the device or the action is empty or too long"};
    }
    const gs_ext_device_provider& entry = d.providers[provider];
    int32_t status = GS_EXT_FAILED;
    {
        const std::scoped_lock lock(d.nodeMutex);
        status = entry.act(entry.context, device.data(), device.size(), action.data(), action.size());
    }
    if (status != GS_EXT_OK) {
        return {CallStatus::Failed, {}, "the action failed with status " + std::to_string(status)};
    }
    return {CallStatus::Ok, {}, {}};
}

bool InProcessModule::claimWanted(std::string_view resource, std::string_view by)
{
    Impl& d = *m_impl;
    if (d.described.claim_wanted == nullptr || !validResource(resource) || by.size() > kMostClaimerBytes ||
        !validUtf8(by)) {
        return false;
    }
    {
        const std::scoped_lock lock(d.jobMutex);
        if (d.stopping) {
            return false;
        }
    }
    return d.described.claim_wanted(d.described.context, resource.data(), resource.size(), by.data(), by.size()) ==
           GS_EXT_OK;
}

}
