#include "gs/exthost/Server.h"

#include "Channel.h"
#include "Wire.h"

#include "gs/exthost/InProcess.h"

#include <gs/platform/CrashHandler.h>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace gs::exthost {

namespace {

constexpr DWORD kStartWaitMs = 10'000;
constexpr auto kClaimAnswerWait = std::chrono::seconds(2);
constexpr auto kClaimWaitedWait = std::chrono::seconds(6);
constexpr auto kServiceWait = std::chrono::seconds(12);
constexpr std::string_view kServiceThread =
    "call_service works only on a thread of the DLL's own, not inside a call from GameScrcpy";
thread_local bool t_hostThread = false;
constexpr std::size_t kMostPathBytes = 4096;
constexpr std::size_t kMostPermissionBytes = 64;
enum Exit : int { kDone = 0, kUsage = 2, kBroken = 3, kNoStart = 4, kNotLoaded = 5 };

std::filesystem::path pathOf(const std::string& text)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

class Link final : public HostServices
{
public:
    Link(detail::Channel& channel, HANDLE parent) noexcept
        : m_channel(channel)
        , m_parent(parent)
        , m_main(std::this_thread::get_id())
    {
    }

    ClaimResult claim(std::string_view resource, bool wait) override
    {
        detail::WireWriter out;
        out.text(resource);
        out.u32(wait ? 1U : 0U);
        std::uint64_t id = 0;
        {
            const std::scoped_lock lock(m_claimMutex);
            if (m_closed) {
                return ClaimResult::Busy;
            }
            id = ++m_lastClaim;
            m_answers.emplace(id, std::nullopt);
        }
        send(detail::Kind::Claim, id, out.bytes());
        const auto deadline = std::chrono::steady_clock::now() + (wait ? kClaimWaitedWait : kClaimAnswerWait);
        if (std::this_thread::get_id() == m_main) {
            pump(id, deadline);
        }
        std::unique_lock lock(m_claimMutex);
        m_answered.wait_until(lock, deadline, [this, id] { return m_closed || m_answers[id].has_value(); });
        const std::optional<ClaimResult> result = m_answers[id];
        m_answers.erase(id);
        lock.unlock();
        if (!result) {
            release(resource);
            return ClaimResult::Busy;
        }
        return *result;
    }

    void release(std::string_view resource) override
    {
        detail::WireWriter out;
        out.text(resource);
        send(detail::Kind::Release, 0, out.bytes());
    }

    ServiceResult callService(std::string_view service, std::string_view json) override
    {
        if (t_hostThread || std::this_thread::get_id() == m_main) {
            return {ServiceStatus::Unsupported, std::string(kServiceThread)};
        }
        detail::WireWriter out;
        out.text(service);
        out.text(json);
        std::uint64_t id = 0;
        {
            const std::scoped_lock lock(m_claimMutex);
            if (m_closed) {
                return {ServiceStatus::Failed, "GS-API-149 the extension is stopping"};
            }
            id = ++m_lastService;
            m_services.emplace(id, std::nullopt);
        }
        send(detail::Kind::ServiceCall, id, out.bytes());
        std::unique_lock lock(m_claimMutex);
        m_answered.wait_for(lock, kServiceWait, [this, id] { return m_closed || m_services[id].has_value(); });
        std::optional<ServiceResult> result = std::move(m_services[id]);
        m_services.erase(id);
        if (!result) {
            return {ServiceStatus::Failed, m_closed ? std::string("GS-API-149 the extension is stopping")
                                                    : std::string("GS-API-149 the service did not answer in time")};
        }
        return std::move(*result);
    }

    void serviceAnswered(const detail::Message& message)
    {
        detail::WireReader in(message.payload);
        std::uint32_t status = 0;
        std::string text;
        ServiceResult result{ServiceStatus::Failed, "GS-API-149 the answer of the service was damaged"};
        if (in.u32(status) && in.text(text, kMostServiceBytes) && in.done() &&
            status <= static_cast<std::uint32_t>(ServiceStatus::BadArgument)) {
            result = {static_cast<ServiceStatus>(status), std::move(text)};
        }
        {
            const std::scoped_lock lock(m_claimMutex);
            const auto found = m_services.find(message.id);
            if (found == m_services.end()) {
                return;
            }
            found->second = std::move(result);
        }
        m_answered.notify_all();
    }

    void answered(const detail::Message& message)
    {
        detail::WireReader in(message.payload);
        std::uint32_t status = 0;
        ClaimResult result = ClaimResult::Busy;
        if (in.u32(status) && in.done() &&
            (status == static_cast<std::uint32_t>(ClaimResult::Claimed) ||
             status == static_cast<std::uint32_t>(ClaimResult::Unsupported) ||
             status == static_cast<std::uint32_t>(ClaimResult::BadArgument))) {
            result = static_cast<ClaimResult>(status);
        }
        {
            const std::scoped_lock lock(m_claimMutex);
            const auto found = m_answers.find(message.id);
            if (found == m_answers.end()) {
                return;
            }
            found->second = result;
        }
        m_answered.notify_all();
    }

    [[nodiscard]] std::optional<detail::Message> deferred()
    {
        if (m_deferred.empty()) {
            return std::nullopt;
        }
        detail::Message message = std::move(m_deferred.front());
        m_deferred.pop_front();
        return message;
    }

    void close()
    {
        {
            const std::scoped_lock lock(m_claimMutex);
            m_closed = true;
        }
        m_answered.notify_all();
    }

    void send(detail::Kind kind, std::uint64_t id, std::string_view payload)
    {
        const std::scoped_lock lock(m_mutex);
        if (m_channel.toApp().write(kind, id, payload)) {
            SetEvent(m_channel.toAppEvent());
        }
    }

    void log(int level, std::string_view text) override
    {
        detail::WireWriter out;
        out.u32(static_cast<std::uint32_t>(level));
        out.text(text);
        send(detail::Kind::Log, 0, out.bytes());
    }

    bool notify(std::string_view title, std::string_view text) override
    {
        detail::WireWriter out;
        out.text(title);
        out.text(text);
        send(detail::Kind::Notify, 0, out.bytes());
        return true;
    }

    void emitted(std::string_view topic, std::string_view json) override
    {
        detail::WireWriter out;
        out.text(topic);
        out.text(json);
        send(detail::Kind::Event, 0, out.bytes());
    }

    void devicesChanged(std::string_view provider) override
    {
        detail::WireWriter out;
        out.text(provider);
        send(detail::Kind::DevicesChanged, 0, out.bytes());
    }

    void listening(std::string_view topic, bool on) override
    {
        detail::WireWriter out;
        out.text(topic);
        out.u32(on ? 1U : 0U);
        send(detail::Kind::Subscribe, 0, out.bytes());
    }

    void inputEvent(std::string_view source, std::string_view slot, double x, double y, std::uint64_t at) override
    {
        detail::WireWriter out;
        out.text(source);
        out.text(slot);
        out.f64(x);
        out.f64(y);
        out.u64(at);
        send(detail::Kind::InputEvent, 0, out.bytes());
    }

private:
    void pump(std::uint64_t id, std::chrono::steady_clock::time_point deadline)
    {
        for (;;) {
            {
                const std::scoped_lock lock(m_claimMutex);
                if (m_closed || m_answers[id].has_value()) {
                    return;
                }
            }
            detail::Message message;
            const detail::ReadResult read = m_channel.toHost().read(message);
            if (read == detail::ReadResult::Broken) {
                close();
                return;
            }
            if (read == detail::ReadResult::Read) {
                if (message.kind == detail::Kind::ClaimAnswer) {
                    answered(message);
                } else {
                    m_deferred.push_back(std::move(message));
                }
                continue;
            }
            const auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (left.count() <= 0) {
                return;
            }
            const HANDLE waits[] = {m_channel.toHostEvent(), m_parent};
            const DWORD waited =
                WaitForMultipleObjects(m_parent != nullptr ? 2 : 1, waits, FALSE, static_cast<DWORD>(left.count()));
            if (waited != WAIT_OBJECT_0 && waited != WAIT_TIMEOUT) {
                close();
                return;
            }
        }
    }

    detail::Channel& m_channel;
    HANDLE m_parent = nullptr;
    std::thread::id m_main;
    std::mutex m_mutex;
    std::mutex m_claimMutex;
    std::condition_variable m_answered;
    std::map<std::uint64_t, std::optional<ClaimResult>> m_answers;
    std::uint64_t m_lastClaim = 0;
    bool m_closed = false;
    std::deque<detail::Message> m_deferred;
    std::map<std::uint64_t, std::optional<ServiceResult>> m_services;
    std::uint64_t m_lastService = 0;
};

class Worker
{
public:
    Worker(Module& module, Link& link)
        : m_module(module)
        , m_link(link)
        , m_thread([this] { run(); })
    {
    }

    ~Worker()
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_stopping = true;
        }
        m_wake.notify_all();
    }

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    void push(detail::Message message)
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_queue.push_back(std::move(message));
        }
        m_wake.notify_one();
    }

private:
    void run()
    {
        t_hostThread = true;
        for (;;) {
            detail::Message message;
            {
                std::unique_lock lock(m_mutex);
                m_wake.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
                if (m_queue.empty()) {
                    return;
                }
                message = std::move(m_queue.front());
                m_queue.pop_front();
            }
            answer(message);
        }
    }

    void answer(const detail::Message& message)
    {
        if (message.kind == detail::Kind::DeviceList || message.kind == detail::Kind::DeviceAct) {
            reply(message.id, provided(message));
            return;
        }
        if (message.kind == detail::Kind::ClaimWanted) {
            detail::WireReader in(message.payload);
            std::string resource;
            std::string by;
            const bool yields = in.text(resource, kMostResourceBytes) && in.text(by, kMostClaimerBytes) &&
                                in.done() && m_module.claimWanted(resource, by);
            reply(message.id, yields ? CallResult{CallStatus::Ok, {}, {}}
                                     : CallResult{CallStatus::Unavailable, {}, "the extension keeps the hardware"});
            return;
        }
        detail::WireReader in(message.payload);
        std::uint32_t node = 0;
        std::uint32_t count = 0;
        std::vector<Value> values;
        bool good = in.u32(node) && in.u32(count) && count <= kMostParams;
        for (std::uint32_t index = 0; good && index < count; ++index) {
            Value value;
            good = in.value(value);
            values.push_back(std::move(value));
        }
        reply(message.id, good && in.done() ? m_module.call(node, values)
                                            : CallResult{CallStatus::BadArgument, {}, "the call was damaged"});
    }

    CallResult provided(const detail::Message& message)
    {
        detail::WireReader in(message.payload);
        std::uint32_t provider = 0;
        if (!in.u32(provider)) {
            return {CallStatus::BadArgument, {}, "the request was damaged"};
        }
        if (message.kind == detail::Kind::DeviceList) {
            return in.done() ? m_module.listDevices(provider)
                             : CallResult{CallStatus::BadArgument, {}, "the request was damaged"};
        }
        std::string device;
        std::string action;
        if (!in.text(device, kMostDeviceBytes) || !in.text(action, kMostActionBytes) || !in.done()) {
            return {CallStatus::BadArgument, {}, "the request was damaged"};
        }
        return m_module.actOnDevice(provider, device, action);
    }

    void reply(std::uint64_t id, const CallResult& result)
    {
        detail::WireWriter out;
        out.u32(static_cast<std::uint32_t>(result.status));
        out.value(result.value);
        out.text(result.problem);
        m_link.send(detail::Kind::Answer, id, out.bytes());
    }

    Module& m_module;
    Link& m_link;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<detail::Message> m_queue;
    bool m_stopping = false;
    std::jthread m_thread;
};

class Jobs
{
public:
    Jobs(Module& module, Link& link) noexcept
        : m_module(module)
        , m_link(link)
    {
    }

    Jobs(const Jobs&) = delete;
    Jobs& operator=(const Jobs&) = delete;

    bool handle(const detail::Message& message)
    {
        if (message.kind == detail::Kind::JobStart) {
            start(message);
            return true;
        }
        if (message.kind != detail::Kind::JobCancel) {
            return false;
        }
        std::uint64_t running = 0;
        {
            const std::scoped_lock lock(m_mutex);
            if (const auto found = m_running.find(message.id); found != m_running.end()) {
                running = found->second;
            }
        }
        if (running != 0) {
            m_module.cancelJob(running);
        }
        return true;
    }

private:
    void start(const detail::Message& message)
    {
        const std::uint64_t id = message.id;
        detail::WireReader in(message.payload);
        std::uint32_t index = 0;
        std::string args;
        const bool good = in.u32(index) && in.text(args, kMostParamBytes) && in.done();
        Link& link = m_link;
        JobEvents events{.progress =
                             [&link, id](double fraction, std::string text) {
                                 detail::WireWriter out;
                                 out.u32(0);
                                 out.f64(fraction);
                                 out.u32(0);
                                 out.text(text);
                                 link.send(detail::Kind::JobUpdate, id, out.bytes());
                             },
                         .log =
                             [&link, id](int level, std::string text) {
                                 detail::WireWriter out;
                                 out.u32(1);
                                 out.f64(0.0);
                                 out.u32(static_cast<std::uint32_t>(level));
                                 out.text(text);
                                 link.send(detail::Kind::JobUpdate, id, out.bytes());
                             },
                         .done =
                             [this, id](bool worked, std::string text) {
                                 {
                                     const std::scoped_lock lock(m_mutex);
                                     m_running.erase(id);
                                 }
                                 finish(id, worked, text);
                             },
                         .output =
                             [&link, id](std::string json) {
                                 detail::WireWriter out;
                                 out.u32(2);
                                 out.f64(0.0);
                                 out.u32(0);
                                 out.text(json);
                                 link.send(detail::Kind::JobUpdate, id, out.bytes());
                             }};
        {
            const std::scoped_lock lock(m_mutex);
            m_running[id] = 0;
        }
        const std::uint64_t running = good ? m_module.startJob(index, args, std::move(events)) : 0;
        const std::scoped_lock lock(m_mutex);
        const auto found = m_running.find(id);
        if (found == m_running.end()) {
            return;
        }
        if (running != 0) {
            found->second = running;
            return;
        }
        m_running.erase(found);
        finish(id, false,
               good ? "the job could not start: there is no such job, or 4 of the extension's jobs are running already"
                    : "the request to start the job was damaged");
    }

    void finish(std::uint64_t id, bool worked, std::string_view text)
    {
        detail::WireWriter out;
        out.u32(worked ? 1U : 0U);
        out.text(text);
        m_link.send(detail::Kind::JobDone, id, out.bytes());
    }

    Module& m_module;
    Link& m_link;
    std::mutex m_mutex;
    std::map<std::uint64_t, std::uint64_t> m_running;
};

struct Start {
    std::string dll;
    std::string store;
    std::vector<std::string> granted;
    std::string settings;
    std::vector<DeclaredInput> inputs;
};

std::optional<Start> startOf(const detail::Message& message)
{
    if (message.kind != detail::Kind::Start) {
        return std::nullopt;
    }
    detail::WireReader in(message.payload);
    Start start;
    std::uint32_t count = 0;
    if (!in.text(start.dll, kMostPathBytes) || !in.text(start.store, kMostPathBytes) || !in.u32(count) || count > 32) {
        return std::nullopt;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        std::string permission;
        if (!in.text(permission, kMostPermissionBytes)) {
            return std::nullopt;
        }
        start.granted.push_back(std::move(permission));
    }
    if (!in.text(start.settings, kMostSettingsBytes)) {
        return std::nullopt;
    }
    if (!in.inputs(start.inputs)) {
        return std::nullopt;
    }
    return in.done() ? std::optional<Start>(std::move(start)) : std::nullopt;
}

void lost(Link& link, std::uint64_t session, std::string_view why)
{
    detail::WireWriter out;
    out.text(why);
    link.send(detail::Kind::TouchLost, session, out.bytes());
}

void touched(Module& module, Link& link, std::map<std::uint64_t, std::uint64_t>& sessions,
             const detail::Message& message)
{
    detail::WireReader in(message.payload);
    switch (message.kind) {
    case detail::Kind::TouchOpen: {
        std::uint32_t method = 0;
        std::string device;
        std::string options;
        if (!in.u32(method) || !in.text(device, kMostDeviceBytes) || !in.text(options, kMostOptionBytes)) {
            lost(link, message.id, "the touch method could not be opened");
            return;
        }
        const std::uint64_t opened = module.openTouch(method, device, options);
        if (opened == 0) {
            lost(link, message.id, "the touch method did not start");
            return;
        }
        sessions.insert_or_assign(message.id, opened);
        return;
    }
    case detail::Kind::TouchFrame: {
        const auto found = sessions.find(message.id);
        if (found == sessions.end()) {
            return;
        }
        TouchFrame frame;
        std::uint32_t count = 0;
        bool good = in.u32(frame.rotation) && in.u32(count) && count <= kMostContacts;
        for (std::uint32_t index = 0; good && index < count; ++index) {
            Contact contact;
            good = in.contact(contact);
            frame.contacts.push_back(contact);
        }
        if (!good || !module.touch(found->second, frame)) {
            module.closeTouch(found->second);
            sessions.erase(found);
            lost(link, message.id, "the touch method failed");
        }
        return;
    }
    case detail::Kind::TouchKey: {
        const auto found = sessions.find(message.id);
        KeyEvent key;
        std::uint32_t down = 0;
        if (found != sessions.end() && in.u32(key.keycode) && in.u32(down) && in.u32(key.repeat) && in.u32(key.meta)) {
            key.down = down != 0;
            static_cast<void>(module.key(found->second, key));
        }
        return;
    }
    case detail::Kind::TouchText: {
        const auto found = sessions.find(message.id);
        std::string typed;
        if (found != sessions.end() && in.text(typed, kMostTextBytes)) {
            static_cast<void>(module.text(found->second, typed));
        }
        return;
    }
    case detail::Kind::TouchMouse: {
        const auto found = sessions.find(message.id);
        MouseEvent mouse;
        std::uint32_t dx = 0;
        std::uint32_t dy = 0;
        std::uint32_t wheel = 0;
        if (found != sessions.end() && in.u32(mouse.buttons) && in.u32(dx) && in.u32(dy) && in.u32(wheel)) {
            mouse.dx = static_cast<std::int32_t>(dx);
            mouse.dy = static_cast<std::int32_t>(dy);
            mouse.wheel = static_cast<std::int32_t>(wheel);
            static_cast<void>(module.mouse(found->second, mouse));
        }
        return;
    }
    case detail::Kind::TouchPad: {
        const auto found = sessions.find(message.id);
        PadEvent pad;
        std::uint32_t axes[4] = {};
        if (found != sessions.end() && in.u32(pad.buttons) && in.u32(pad.hat) && in.u32(axes[0]) && in.u32(axes[1]) &&
            in.u32(axes[2]) && in.u32(axes[3]) && in.u32(pad.leftTrigger) && in.u32(pad.rightTrigger)) {
            pad.leftX = static_cast<std::int32_t>(axes[0]);
            pad.leftY = static_cast<std::int32_t>(axes[1]);
            pad.rightX = static_cast<std::int32_t>(axes[2]);
            pad.rightY = static_cast<std::int32_t>(axes[3]);
            static_cast<void>(module.gamepad(found->second, pad));
        }
        return;
    }
    case detail::Kind::TouchClose: {
        if (const auto found = sessions.find(message.id); found != sessions.end()) {
            module.closeTouch(found->second);
            sessions.erase(found);
        }
        return;
    }
    default:
        return;
    }
}

bool instanced(Module& module, Link& link, std::map<std::uint64_t, std::uint64_t>& instances,
               const detail::Message& message)
{
    detail::WireReader in(message.payload);
    switch (message.kind) {
    case detail::Kind::InstanceOpen: {
        std::uint32_t kind = 0;
        std::uint32_t index = 0;
        std::uint64_t seed = 0;
        std::string params;
        std::uint64_t opened = 0;
        if (in.u32(kind) && kind <= static_cast<std::uint32_t>(InstanceKind::Behavior) && in.u32(index) &&
            in.u64(seed) && in.text(params, kMostParamBytes) && in.done()) {
            opened = module.openInstance(static_cast<InstanceKind>(kind), index, params, seed);
        }
        if (const auto found = instances.find(message.id); found != instances.end() && found->second != 0) {
            module.closeInstance(found->second);
        }
        instances.insert_or_assign(message.id, opened);
        return true;
    }
    case detail::Kind::InstanceCall: {
        StepInput input;
        std::uint64_t instance = 0;
        std::uint32_t call = 0;
        StepOutput answer;
        answer.status = StepStatus::Failed;
        if (in.u64(instance) && in.u32(call) && call <= static_cast<std::uint32_t>(StepCall::Wake) &&
            in.u64(input.now) && in.u64(input.wakeAt) && in.event(input.event) && in.command(input.command) &&
            in.done()) {
            input.call = static_cast<StepCall>(call);
            if (const auto found = instances.find(instance); found != instances.end() && found->second != 0) {
                answer = module.step(found->second, input);
            }
        }
        detail::WireWriter out;
        out.answer(answer);
        link.send(detail::Kind::InstanceAnswer, message.id, out.bytes());
        return true;
    }
    case detail::Kind::InstanceReset:
        if (const auto found = instances.find(message.id); found != instances.end() && found->second != 0) {
            module.resetInstance(found->second);
        }
        return true;
    case detail::Kind::InstanceClose:
        if (const auto found = instances.find(message.id); found != instances.end()) {
            if (found->second != 0) {
                module.closeInstance(found->second);
            }
            instances.erase(found);
        }
        return true;
    default:
        return false;
    }
}

}

int serve(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    std::wstring_view handles;
    DWORD parentId = 0;
    std::filesystem::path crashes;
    std::filesystem::path log;
    for (int index = 1; index + 1 < argc; index += 2) {
        const std::wstring_view name(argv[index]);
        if (name == L"--channel") {
            handles = argv[index + 1];
        } else if (name == L"--parent") {
            parentId = static_cast<DWORD>(std::wcstoul(argv[index + 1], nullptr, 10));
        } else if (name == L"--crashes") {
            crashes = argv[index + 1];
        } else if (name == L"--log") {
            log = argv[index + 1];
        }
    }
    if (!crashes.empty()) {
        static_cast<void>(platform::installCrashHandler(crashes, log));
    }
    std::optional<detail::Channel> channel = detail::Channel::open(handles);
    if (!channel) {
        return kUsage;
    }
    const HANDLE parent = parentId != 0 ? OpenProcess(SYNCHRONIZE, FALSE, parentId) : nullptr;
    detail::Message first;
    for (;;) {
        const detail::ReadResult read = channel->toHost().read(first);
        if (read == detail::ReadResult::Read) {
            break;
        }
        if (read == detail::ReadResult::Broken ||
            WaitForSingleObject(channel->toHostEvent(), kStartWaitMs) != WAIT_OBJECT_0) {
            return read == detail::ReadResult::Broken ? kBroken : kNoStart;
        }
    }
    const std::optional<Start> start = startOf(first);
    if (!start) {
        return kBroken;
    }
    Link link(*channel, parent);
    std::string problem;
    std::unique_ptr<InProcessModule> module = InProcessModule::load({.dll = pathOf(start->dll),
                                                                     .store = pathOf(start->store),
                                                                     .granted = start->granted,
                                                                     .services = &link,
                                                                     .settings = start->settings,
                                                                     .inputs = start->inputs},
                                                                    problem);
    if (!module) {
        detail::WireWriter out;
        out.text(problem);
        link.send(detail::Kind::Failed, 0, out.bytes());
        return kNotLoaded;
    }
    {
        detail::WireWriter out;
        out.info(module->info());
        link.send(detail::Kind::Ready, 0, out.bytes());
    }
    Jobs jobs(*module, link);
    int status = kDone;
    {
        Worker worker(*module, link);
        Worker claims(*module, link);
        std::map<std::uint64_t, std::uint64_t> sessions;
        std::map<std::uint64_t, std::uint64_t> instances;
        bool running = true;
        while (running) {
            detail::Message message;
            if (std::optional<detail::Message> held = link.deferred()) {
                message = std::move(*held);
            } else {
                const detail::ReadResult read = channel->toHost().read(message);
                if (read == detail::ReadResult::Broken) {
                    status = kBroken;
                    break;
                }
                if (read == detail::ReadResult::Empty) {
                    const HANDLE waits[] = {channel->toHostEvent(), parent};
                    const DWORD waited = WaitForMultipleObjects(parent != nullptr ? 2 : 1, waits, FALSE, INFINITE);
                    running = waited == WAIT_OBJECT_0;
                    continue;
                }
            }
            if (message.kind == detail::Kind::ClaimAnswer) {
                link.answered(message);
            } else if (message.kind == detail::Kind::ServiceAnswer) {
                link.serviceAnswered(message);
            } else if (message.kind == detail::Kind::ClaimWanted) {
                claims.push(std::move(message));
            } else if (message.kind == detail::Kind::Call || message.kind == detail::Kind::DeviceList ||
                       message.kind == detail::Kind::DeviceAct) {
                worker.push(std::move(message));
            } else if (message.kind == detail::Kind::Stop) {
                running = false;
            } else if (message.kind == detail::Kind::Settings) {
                detail::WireReader in(message.payload);
                std::string json;
                if (in.text(json, kMostSettingsBytes) && in.done()) {
                    module->setSettings(json);
                }
            } else if (message.kind == detail::Kind::AppEvent) {
                detail::WireReader in(message.payload);
                std::string topic;
                std::string json;
                if (in.text(topic, kMostTopicBytes) && in.text(json, kMostEventBytes) && in.done()) {
                    module->appEvent(topic, json);
                }
            } else if (jobs.handle(message)) {
                continue;
            } else if (!instanced(*module, link, instances, message)) {
                touched(*module, link, sessions, message);
            }
        }
        link.close();
        for (const auto& [id, session] : sessions) {
            module->closeTouch(session);
        }
        for (const auto& [id, instance] : instances) {
            if (instance != 0) {
                module->closeInstance(instance);
            }
        }
    }
    module.reset();
    if (parent != nullptr) {
        CloseHandle(parent);
    }
    return status;
}

}
