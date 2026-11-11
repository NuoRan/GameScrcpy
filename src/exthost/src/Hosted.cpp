#include "gs/exthost/Hosted.h"

#include "Channel.h"
#include "Wire.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <utility>

namespace gs::exthost {

namespace {

using Steady = std::chrono::steady_clock;

constexpr DWORD kPollMs = 250;
constexpr DWORD kStopWaitMs = 2'000;
constexpr std::chrono::milliseconds kClaimWantedWait{1'000};
constexpr UINT kStoppedExit = 9;
constexpr int kWarning = 1;
std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::wstring quoted(const std::filesystem::path& path)
{
    std::wstring text = path.wstring();
    while (!text.empty() && (text.back() == L'\\' || text.back() == L'/')) {
        text.pop_back();
    }
    return L"\"" + text + L"\"";
}

HANDLE sharedJob()
{
    static const HANDLE job = [] {
        const HANDLE made = CreateJobObjectW(nullptr, nullptr);
        if (made != nullptr) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags =
                JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
            SetInformationJobObject(made, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        }
        return made;
    }();
    return job;
}

struct Instance {
    detail::Channel channel;
    HANDLE process = nullptr;
};

std::optional<Instance> launch(const HostedOptions& options, std::string& problem)
{
    std::optional<detail::Channel> channel = detail::Channel::create();
    if (!channel) {
        problem = "the channel to gs-exthost.exe could not be made";
        return std::nullopt;
    }
    detail::WireWriter start;
    start.text(utf8(options.dll));
    start.text(utf8(options.store));
    start.u32(static_cast<std::uint32_t>(options.granted.size()));
    for (const std::string& permission : options.granted) {
        start.text(permission);
    }
    start.text(options.settings.size() <= kMostSettingsBytes ? std::string_view(options.settings) : std::string_view());
    if (!channel->toHost().write(detail::Kind::Start, 0, start.bytes())) {
        problem = "the start of gs-exthost.exe could not be written";
        return std::nullopt;
    }
    const std::array<HANDLE, 3> sources = channel->handles();
    std::array<HANDLE, 3> copies{};
    bool made = true;
    for (std::size_t index = 0; index < sources.size() && made; ++index) {
        made = DuplicateHandle(GetCurrentProcess(), sources[index], GetCurrentProcess(), &copies[index], 0, TRUE,
                               DUPLICATE_SAME_ACCESS) != FALSE;
    }
    PROCESS_INFORMATION started{};
    DWORD error = ERROR_SUCCESS;
    if (made) {
        std::wstring command = L"\"" + options.program.wstring() + L"\" --channel " + detail::handleText(copies) +
                               L" --parent " + std::to_wstring(GetCurrentProcessId());
        if (!options.crashes.empty()) {
            command += L" --crashes " + quoted(options.crashes) + L" --log " + quoted(options.logFile);
        }
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        std::vector<std::byte> storage(size);
        auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        made = InitializeProcThreadAttributeList(attributes, 1, 0, &size) != FALSE;
        if (made) {
            made = UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, copies.data(),
                                             sizeof(HANDLE) * copies.size(), nullptr, nullptr) != FALSE;
            if (made) {
                STARTUPINFOEXW startup{};
                startup.StartupInfo.cb = sizeof(startup);
                startup.lpAttributeList = attributes;
                const std::wstring folder = options.program.parent_path().wstring();
                made = CreateProcessW(options.program.c_str(), command.data(), nullptr, nullptr, TRUE,
                                      EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr,
                                      folder.empty() ? nullptr : folder.c_str(), &startup.StartupInfo,
                                      &started) != FALSE;
            }
            error = made ? ERROR_SUCCESS : GetLastError();
            DeleteProcThreadAttributeList(attributes);
        }
    } else {
        error = GetLastError();
    }
    for (const HANDLE copy : copies) {
        if (copy != nullptr) {
            CloseHandle(copy);
        }
    }
    if (!made) {
        problem = "gs-exthost.exe could not be started (error " + std::to_string(error) + ")";
        return std::nullopt;
    }
    if (const HANDLE job = sharedJob(); job != nullptr) {
        AssignProcessToJobObject(job, started.hProcess);
    }
    ResumeThread(started.hThread);
    CloseHandle(started.hThread);
    return Instance{std::move(*channel), started.hProcess};
}

}

struct HostedModule::Impl {
    enum class State : std::uint8_t { Starting, Ready, Restarting, Stopped };

    explicit Impl(HostedOptions given)
        : options(std::move(given))
        , wake(CreateEventW(nullptr, FALSE, FALSE, nullptr))
        , stepEvent(CreateEventW(nullptr, FALSE, FALSE, nullptr))
        , stepTimer(CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS))
    {
        if (stepTimer == nullptr) {
            stepTimer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        }
        claimLink->impl = this;
    }

    ~Impl()
    {
        {
            const std::scoped_lock lock(claimLink->mutex);
            claimLink->impl = nullptr;
        }
        {
            const std::scoped_lock lock(mutex);
            closing = true;
            if (channel && channel->toHost().write(detail::Kind::Stop, 0, {})) {
                SetEvent(channel->toHostEvent());
            }
        }
        reader.request_stop();
        if (wake != nullptr) {
            SetEvent(wake);
        }
        if (reader.joinable()) {
            reader.join();
        }
        failJobs("the extension stopped before the job ended");
        if (process != nullptr) {
            if (WaitForSingleObject(process, kStopWaitMs) != WAIT_OBJECT_0) {
                TerminateProcess(process, kStoppedExit);
            }
            CloseHandle(process);
        }
        if (wake != nullptr) {
            CloseHandle(wake);
        }
        for (const HANDLE handle : {stepEvent, stepTimer}) {
            if (handle != nullptr) {
                CloseHandle(handle);
            }
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    struct Remote {
        InstanceKind kind = InstanceKind::Attachment;
        std::uint32_t index = 0;
        std::string params;
        std::uint64_t seed = 0;
        std::uint64_t generation = 0;
        bool opened = false;
    };

    bool openRemote(std::uint64_t id, Remote& remote)
    {
        if (state != State::Ready || !channel) {
            return false;
        }
        detail::WireWriter payload;
        payload.u32(static_cast<std::uint32_t>(remote.kind));
        payload.u32(remote.index);
        payload.u64(remote.seed);
        payload.text(remote.params);
        if (!channel->toHost().write(detail::Kind::InstanceOpen, id, payload.bytes())) {
            return false;
        }
        remote.generation = generation;
        remote.opened = true;
        return true;
    }

    [[nodiscard]] const Clock& clock() const noexcept { return options.clock != nullptr ? *options.clock : steady; }

    void run(const std::stop_token& stop)
    {
        while (!stop.stop_requested()) {
            HANDLE toApp = nullptr;
            HANDLE running = nullptr;
            {
                const std::scoped_lock lock(mutex);
                if (channel && process != nullptr) {
                    toApp = channel->toAppEvent();
                    running = process;
                }
            }
            const std::array<HANDLE, 3> waits{wake, toApp, running};
            const DWORD count = toApp != nullptr ? 3 : 1;
            const DWORD waited = WaitForMultipleObjects(count, waits.data(), FALSE, count == 3 ? kPollMs : INFINITE);
            if (stop.stop_requested()) {
                return;
            }
            if (count != 3) {
                continue;
            }
            drain();
            if (waited == WAIT_OBJECT_0 + 2) {
                exited();
                continue;
            }
            checkHang();
        }
    }

    void drain()
    {
        for (;;) {
            detail::Message message;
            detail::ReadResult read = detail::ReadResult::Empty;
            {
                const std::scoped_lock lock(mutex);
                if (!channel) {
                    return;
                }
                read = channel->toApp().read(message);
                if (read == detail::ReadResult::Broken && process != nullptr) {
                    TerminateProcess(process, kStoppedExit);
                }
            }
            if (read != detail::ReadResult::Read) {
                return;
            }
            handle(message);
        }
    }

    void handle(const detail::Message& message)
    {
        detail::WireReader in(message.payload);
        switch (message.kind) {
        case detail::Kind::Ready: {
            ModuleInfo described;
            const bool readable = in.info(described) && in.done();
            bool restarted = false;
            bool late = false;
            {
                const std::scoped_lock lock(mutex);
                if (state != State::Starting && state != State::Restarting) {
                    return;
                }
                if (!readable || (everReady && described != info)) {
                    giveUpAfterExit = true;
                    problem = "the DLL offered other touch methods or nodes after it started again";
                    if (process != nullptr) {
                        TerminateProcess(process, kStoppedExit);
                    }
                } else {
                    restarted = everReady;
                    late = !everReady && started;
                    info = std::move(described);
                    everReady = true;
                    state = State::Ready;
                    ++generation;
                }
            }
            changed.notify_all();
            if (restarted && options.restarted) {
                options.restarted();
            }
            if (late && options.ready) {
                options.ready();
            }
            return;
        }
        case detail::Kind::Failed: {
            std::string why;
            if (in.text(why, 4096)) {
                const std::scoped_lock lock(mutex);
                problem = why;
            }
            return;
        }
        case detail::Kind::Answer: {
            std::uint32_t status = 0;
            CallResult result;
            if (!in.u32(status) || status > static_cast<std::uint32_t>(CallStatus::Crashed) || !in.value(result.value) ||
                !in.text(result.problem, 4096)) {
                return;
            }
            result.status = static_cast<CallStatus>(status);
            {
                const std::scoped_lock lock(mutex);
                outstanding.erase(message.id);
                if (const auto found = answers.find(message.id); found != answers.end()) {
                    found->second = std::move(result);
                }
            }
            changed.notify_all();
            return;
        }
        case detail::Kind::InstanceAnswer: {
            StepOutput result;
            if (!in.answer(result) || !in.done()) {
                result = StepOutput{.status = StepStatus::Failed};
            }
            const std::scoped_lock lock(mutex);
            outstanding.erase(message.id);
            if (stepWaiting != 0 && message.id == stepWaiting) {
                stepAnswer = std::move(result);
                SetEvent(stepEvent);
            }
            return;
        }
        case detail::Kind::TouchLost: {
            bool had = false;
            {
                const std::scoped_lock lock(mutex);
                had = sessions.erase(message.id) > 0;
            }
            if (had && options.touchLost) {
                options.touchLost(message.id);
            }
            return;
        }
        case detail::Kind::Log: {
            std::uint32_t level = 0;
            std::string text;
            if (in.u32(level) && in.text(text, 4096) && options.log) {
                options.log(static_cast<int>(std::min<std::uint32_t>(level, 2)), text);
            }
            return;
        }
        case detail::Kind::DevicesChanged: {
            std::string provider;
            if (in.text(provider, kMostNameBytes) && options.devicesChanged) {
                options.devicesChanged(provider);
            }
            return;
        }
        case detail::Kind::Event: {
            std::string topic;
            std::string json;
            if (in.text(topic, kMostTopicBytes) && in.text(json, kMostEventBytes) && validTopic(topic) && options.event) {
                options.event(topic, json);
            }
            return;
        }
        case detail::Kind::Notify: {
            std::string title;
            std::string text;
            if (in.text(title, 1024) && in.text(text, 4096) && options.notify) {
                options.notify(title, text);
            }
            return;
        }
        case detail::Kind::JobUpdate: {
            std::uint32_t type = 0;
            double fraction = 0.0;
            std::uint32_t level = 0;
            std::string text;
            if (!in.u32(type) || !in.f64(fraction) || !in.u32(level) ||
                !in.text(text, type == 2 ? kMostJobOutputBytes : kMostJobLineBytes)) {
                return;
            }
            JobEvents events;
            {
                const std::scoped_lock lock(mutex);
                const auto found = jobs.find(message.id);
                if (found == jobs.end()) {
                    return;
                }
                events = found->second;
            }
            if (type == 0 && events.progress) {
                events.progress(fraction >= 0.0 && fraction <= 1.0 ? fraction : -1.0, std::move(text));
            } else if (type == 1 && events.log) {
                events.log(static_cast<int>(std::min<std::uint32_t>(level, 2)), std::move(text));
            } else if (type == 2 && events.output) {
                events.output(std::move(text));
            }
            return;
        }
        case detail::Kind::JobDone: {
            std::uint32_t worked = 0;
            std::string text;
            const bool readable = in.u32(worked) && in.text(text, kMostJobResultBytes);
            JobEvents events;
            {
                const std::scoped_lock lock(mutex);
                const auto found = jobs.find(message.id);
                if (found == jobs.end()) {
                    return;
                }
                events = std::move(found->second);
                jobs.erase(found);
            }
            if (events.done) {
                events.done(readable && worked != 0, readable ? std::move(text) : std::string("the job's end was damaged"));
            }
            return;
        }
        case detail::Kind::Claim: {
            std::string resource;
            std::uint32_t wait = 0;
            std::uint64_t launch = 0;
            {
                const std::scoped_lock lock(mutex);
                launch = launches;
            }
            if (!in.text(resource, kMostResourceBytes) || !in.u32(wait) || !in.done() || !validResource(resource)) {
                answerClaim(message.id, resource, ClaimResult::BadArgument, launch);
                return;
            }
            if (!options.claim) {
                answerClaim(message.id, resource, ClaimResult::Unsupported, launch);
                return;
            }
            options.claim(resource, wait != 0,
                          [link = claimLink, id = message.id, resource, launch](ClaimResult result) {
                              const std::scoped_lock lock(link->mutex);
                              if (link->impl != nullptr) {
                                  link->impl->answerClaim(id, resource, result, launch);
                              }
                          });
            return;
        }
        case detail::Kind::Release: {
            std::string resource;
            if (!in.text(resource, kMostResourceBytes) || !in.done() || !validResource(resource)) {
                return;
            }
            {
                const std::scoped_lock lock(mutex);
                held.erase(resource);
            }
            if (options.release) {
                options.release(resource);
            }
            return;
        }
        default:
            return;
        }
    }

    void answerClaim(std::uint64_t id, const std::string& resource, ClaimResult result, std::uint64_t launch)
    {
        bool stale = false;
        {
            const std::scoped_lock lock(mutex);
            stale = launch != launches || !channel;
            if (!stale) {
                if (result == ClaimResult::Claimed) {
                    held.insert(resource);
                }
                detail::WireWriter out;
                out.u32(static_cast<std::uint32_t>(result));
                if (channel->toHost().write(detail::Kind::ClaimAnswer, id, out.bytes())) {
                    SetEvent(channel->toHostEvent());
                }
            }
        }
        if (stale && result == ClaimResult::Claimed && options.release) {
            options.release(resource);
        }
    }

    void failJobs(const std::string& why)
    {
        std::map<std::uint64_t, JobEvents> ended;
        {
            const std::scoped_lock lock(mutex);
            ended.swap(jobs);
        }
        for (auto& [id, events] : ended) {
            if (events.done) {
                events.done(false, why);
            }
        }
    }

    void exited()
    {
        std::vector<std::uint64_t> lost;
        bool restart = false;
        bool gaveUp = false;
        bool failedLate = false;
        std::string why;
        std::set<std::string> dropped;
        DWORD code = 0;
        {
            const std::scoped_lock lock(mutex);
            if (process != nullptr) {
                if (GetExitCodeProcess(process, &code) == FALSE) {
                    code = 0;
                }
                CloseHandle(process);
                process = nullptr;
            }
            channel.reset();
            dropped.swap(held);
            if (closing || state == State::Stopped) {
                return;
            }
            for (auto& [id, answer] : answers) {
                if (!answer) {
                    answer = CallResult{CallStatus::Crashed, {}, "the extension's process stopped while the node ran"};
                }
            }
            outstanding.clear();
            if (stepWaiting != 0) {
                SetEvent(stepEvent);
            }
            lost.assign(sessions.begin(), sessions.end());
            sessions.clear();
            if (!everReady) {
                state = State::Stopped;
                if (problem.empty()) {
                    problem = "gs-exthost.exe stopped before the DLL was ready";
                }
                failedLate = started;
                why = problem;
            } else {
                ++crashCount;
                const Microseconds now = clock().now();
                crashTimes.push_back(now);
                while (crashTimes.size() > static_cast<std::size_t>(std::max(options.crashLimit, 1))) {
                    crashTimes.pop_front();
                }
                gaveUp = giveUpAfterExit || (static_cast<int>(crashTimes.size()) >= options.crashLimit &&
                                             now - crashTimes.front() < options.crashWindow);
                if (gaveUp && problem.empty()) {
                    problem = "the DLL stopped working " + std::to_string(crashTimes.size()) + " times within a minute";
                }
                state = gaveUp ? State::Stopped : State::Restarting;
                restart = !gaveUp;
            }
        }
        changed.notify_all();
        if (options.crashed) {
            options.crashed(static_cast<std::uint32_t>(code));
        }
        if (options.release) {
            for (const std::string& resource : dropped) {
                options.release(resource);
            }
        }
        for (const std::uint64_t session : lost) {
            if (options.touchLost) {
                options.touchLost(session);
            }
        }
        failJobs("the extension's process stopped before the job ended");
        if (failedLate && options.notReady) {
            options.notReady(why);
        }
        if (gaveUp && options.stopped) {
            options.stopped();
        }
        if (restart) {
            relaunch();
        }
    }

    void relaunch()
    {
        std::string why;
        HostedOptions current;
        {
            const std::scoped_lock lock(mutex);
            current = options;
        }
        std::optional<Instance> instance = launch(current, why);
        bool failed = false;
        {
            const std::scoped_lock lock(mutex);
            if (closing) {
                if (instance) {
                    TerminateProcess(instance->process, kStoppedExit);
                    CloseHandle(instance->process);
                }
                return;
            }
            if (!instance) {
                state = State::Stopped;
                problem = why;
                failed = true;
            } else {
                channel = std::move(instance->channel);
                process = instance->process;
                launchedAt = Steady::now();
                ++launches;
            }
        }
        if (failed) {
            changed.notify_all();
            if (options.stopped) {
                options.stopped();
            }
        }
    }

    void checkHang()
    {
        bool stuck = false;
        {
            const std::scoped_lock lock(mutex);
            const Steady::time_point now = Steady::now();
            if ((state == State::Starting || state == State::Restarting) && now - launchedAt > options.startLimit) {
                stuck = true;
            }
            for (const auto& entry : outstanding) {
                stuck = stuck || now - entry.second > options.hangLimit;
            }
            if (stuck && process != nullptr) {
                TerminateProcess(process, kStoppedExit);
            }
        }
        if (stuck && options.log) {
            options.log(kWarning, "the extension's process did not answer in time and was stopped");
        }
    }

    CallResult ask(detail::Kind kind, const detail::WireWriter& payload, std::string_view what,
                   std::optional<std::chrono::milliseconds> limit = std::nullopt)
    {
        std::unique_lock lock(mutex);
        const std::chrono::milliseconds budget = limit.value_or(options.budget);
        if (state != State::Ready || !channel) {
            return {CallStatus::Unavailable, {},
                    state == State::Stopped ? "the extension stopped working and was turned off"
                                            : "the extension is starting again"};
        }
        const std::uint64_t id = ++nextId;
        if (!channel->toHost().write(kind, id, payload.bytes())) {
            return {CallStatus::Unavailable, {}, "the extension's process is too busy to take the request"};
        }
        answers.emplace(id, std::nullopt);
        outstanding.emplace(id, Steady::now());
        SetEvent(channel->toHostEvent());
        const bool answered = changed.wait_for(lock, budget, [this, id] { return answers.at(id).has_value(); });
        CallResult result = answered ? std::move(*answers.at(id))
                                     : CallResult{CallStatus::TooSlow, {},
                                                  std::string(what) + " did not answer within " +
                                                      std::to_string(budget.count()) + " ms"};
        answers.erase(id);
        return result;
    }

    HostedOptions options;
    SteadyClock steady;
    mutable std::mutex mutex;
    mutable std::condition_variable changed;
    State state = State::Starting;
    bool everReady = false;
    bool closing = false;
    bool giveUpAfterExit = false;
    bool started = false;
    std::string problem;
    std::optional<detail::Channel> channel;
    HANDLE process = nullptr;
    HANDLE wake = nullptr;
    Steady::time_point launchedAt = Steady::now();
    ModuleInfo info;
    std::uint64_t nextId = 0;
    std::map<std::uint64_t, std::optional<CallResult>> answers;
    std::map<std::uint64_t, Steady::time_point> outstanding;
    std::set<std::uint64_t> sessions;
    std::deque<Microseconds> crashTimes;
    int crashCount = 0;
    std::mutex stepMutex;
    HANDLE stepEvent = nullptr;
    HANDLE stepTimer = nullptr;
    std::uint64_t stepWaiting = 0;
    std::optional<StepOutput> stepAnswer;
    std::uint64_t generation = 0;
    std::uint64_t skipped = 0;
    std::uint64_t nextInstance = 0;
    std::map<std::uint64_t, Remote> instances;
    std::map<std::uint64_t, JobEvents> jobs;
    struct ClaimLink {
        std::mutex mutex;
        Impl* impl = nullptr;
    };
    std::shared_ptr<ClaimLink> claimLink = std::make_shared<ClaimLink>();
    std::set<std::string> held;
    std::uint64_t launches = 0;
    std::jthread reader;
};

HostedModule::HostedModule(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

HostedModule::~HostedModule() = default;

std::unique_ptr<HostedModule> HostedModule::start(HostedOptions options, std::string& problem)
{
    auto impl = std::make_unique<Impl>(std::move(options));
    std::string why;
    std::optional<Instance> instance = launch(impl->options, why);
    if (!instance) {
        problem = why;
        return nullptr;
    }
    impl->channel = std::move(instance->channel);
    impl->process = instance->process;
    impl->launchedAt = Steady::now();
    Impl* raw = impl.get();
    impl->reader = std::jthread([raw](const std::stop_token& stop) { raw->run(stop); });
    {
        std::unique_lock lock(impl->mutex);
        impl->changed.wait_for(lock, std::min(impl->options.readyWait, impl->options.startLimit),
                               [raw] { return raw->state != Impl::State::Starting; });
        if (impl->state == Impl::State::Stopped) {
            why = impl->problem.empty() ? std::string("the DLL did not get ready in time") : impl->problem;
        } else {
            impl->started = true;
        }
    }
    if (!why.empty()) {
        problem = why;
        return nullptr;
    }
    return std::unique_ptr<HostedModule>(new HostedModule(std::move(impl)));
}

const ModuleInfo& HostedModule::info() const noexcept
{
    return m_impl->info;
}

CallResult HostedModule::call(std::size_t node, std::span<const Value> arguments)
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
    detail::WireWriter payload;
    payload.u32(static_cast<std::uint32_t>(node));
    payload.u32(static_cast<std::uint32_t>(arguments.size()));
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (const std::string problem = checkValue(arguments[index], declared.params[index]); !problem.empty()) {
            return {CallStatus::BadArgument, {}, "value " + std::to_string(index + 1) + ": " + problem};
        }
        payload.value(arguments[index]);
    }
    std::unique_lock lock(d.mutex);
    if (d.state != Impl::State::Ready || !d.channel) {
        return {CallStatus::Unavailable, {},
                d.state == Impl::State::Stopped ? "the extension stopped working and was turned off"
                                                : "the extension is starting again"};
    }
    const std::uint64_t id = ++d.nextId;
    if (!d.channel->toHost().write(detail::Kind::Call, id, payload.bytes())) {
        return {CallStatus::Unavailable, {}, "the extension's process is too busy to take the call"};
    }
    d.answers.emplace(id, std::nullopt);
    d.outstanding.emplace(id, Steady::now());
    SetEvent(d.channel->toHostEvent());
    const bool answered = d.changed.wait_for(lock, d.options.budget, [&d, id] { return d.answers.at(id).has_value(); });
    CallResult result =
        answered ? std::move(*d.answers.at(id))
                 : CallResult{CallStatus::TooSlow, {},
                              "the node did not answer within " + std::to_string(d.options.budget.count()) + " ms"};
    d.answers.erase(id);
    return result;
}

std::uint64_t HostedModule::openTouch(std::size_t method, std::string_view device, std::string_view options)
{
    Impl& d = *m_impl;
    if (method >= d.info.touchMethods.size() || device.size() > kMostDeviceBytes ||
        options.size() > kMostOptionBytes) {
        return 0;
    }
    detail::WireWriter payload;
    payload.u32(static_cast<std::uint32_t>(method));
    payload.text(device);
    payload.text(options.empty() ? std::string_view("{}") : options);
    const std::scoped_lock lock(d.mutex);
    if (d.state != Impl::State::Ready || !d.channel) {
        return 0;
    }
    const std::uint64_t id = ++d.nextId;
    if (!d.channel->toHost().write(detail::Kind::TouchOpen, id, payload.bytes())) {
        return 0;
    }
    d.sessions.insert(id);
    SetEvent(d.channel->toHostEvent());
    return id;
}

bool HostedModule::touch(std::uint64_t session, const TouchFrame& frame)
{
    Impl& d = *m_impl;
    detail::WireWriter payload;
    payload.u32(frame.rotation & 3U);
    const std::size_t count = std::min(frame.contacts.size(), kMostContacts);
    payload.u32(static_cast<std::uint32_t>(count));
    for (std::size_t index = 0; index < count; ++index) {
        payload.contact(frame.contacts[index]);
    }
    const std::scoped_lock lock(d.mutex);
    if (d.state != Impl::State::Ready || !d.channel || !d.sessions.contains(session) ||
        !d.channel->toHost().write(detail::Kind::TouchFrame, session, payload.bytes())) {
        return false;
    }
    SetEvent(d.channel->toHostEvent());
    return true;
}

bool HostedModule::key(std::uint64_t session, const KeyEvent& key)
{
    Impl& d = *m_impl;
    detail::WireWriter payload;
    payload.u32(key.keycode);
    payload.u32(key.down ? 1U : 0U);
    payload.u32(key.repeat);
    payload.u32(key.meta);
    const std::scoped_lock lock(d.mutex);
    if (d.state != Impl::State::Ready || !d.channel || !d.sessions.contains(session) ||
        !d.channel->toHost().write(detail::Kind::TouchKey, session, payload.bytes())) {
        return false;
    }
    SetEvent(d.channel->toHostEvent());
    return true;
}

bool HostedModule::text(std::uint64_t session, std::string_view utf8)
{
    Impl& d = *m_impl;
    if (utf8.empty() || utf8.size() > kMostTextBytes || !validUtf8(utf8)) {
        return false;
    }
    detail::WireWriter payload;
    payload.text(utf8);
    const std::scoped_lock lock(d.mutex);
    if (d.state != Impl::State::Ready || !d.channel || !d.sessions.contains(session) ||
        !d.channel->toHost().write(detail::Kind::TouchText, session, payload.bytes())) {
        return false;
    }
    SetEvent(d.channel->toHostEvent());
    return true;
}

bool HostedModule::mouse(std::uint64_t session, const MouseEvent& mouse)
{
    Impl& d = *m_impl;
    detail::WireWriter payload;
    payload.u32(mouse.buttons);
    payload.u32(static_cast<std::uint32_t>(mouse.dx));
    payload.u32(static_cast<std::uint32_t>(mouse.dy));
    payload.u32(static_cast<std::uint32_t>(mouse.wheel));
    const std::scoped_lock lock(d.mutex);
    if (d.state != Impl::State::Ready || !d.channel || !d.sessions.contains(session) ||
        !d.channel->toHost().write(detail::Kind::TouchMouse, session, payload.bytes())) {
        return false;
    }
    SetEvent(d.channel->toHostEvent());
    return true;
}

bool HostedModule::gamepad(std::uint64_t session, const PadEvent& pad)
{
    Impl& d = *m_impl;
    detail::WireWriter payload;
    payload.u32(pad.buttons);
    payload.u32(pad.hat);
    payload.u32(static_cast<std::uint32_t>(pad.leftX));
    payload.u32(static_cast<std::uint32_t>(pad.leftY));
    payload.u32(static_cast<std::uint32_t>(pad.rightX));
    payload.u32(static_cast<std::uint32_t>(pad.rightY));
    payload.u32(pad.leftTrigger);
    payload.u32(pad.rightTrigger);
    const std::scoped_lock lock(d.mutex);
    if (d.state != Impl::State::Ready || !d.channel || !d.sessions.contains(session) ||
        !d.channel->toHost().write(detail::Kind::TouchPad, session, payload.bytes())) {
        return false;
    }
    SetEvent(d.channel->toHostEvent());
    return true;
}

void HostedModule::closeTouch(std::uint64_t session)
{
    Impl& d = *m_impl;
    const std::scoped_lock lock(d.mutex);
    if (d.sessions.erase(session) > 0 && d.channel && d.channel->toHost().write(detail::Kind::TouchClose, session, {})) {
        SetEvent(d.channel->toHostEvent());
    }
}

bool HostedModule::working() const noexcept
{
    const std::scoped_lock lock(m_impl->mutex);
    return m_impl->state != Impl::State::Stopped;
}

std::uint64_t HostedModule::openInstance(InstanceKind kind, std::size_t index, std::string_view params,
                                         std::uint64_t seed)
{
    Impl& d = *m_impl;
    const std::size_t count = kind == InstanceKind::Attachment ? d.info.attachments.size() : d.info.behaviors.size();
    if (index >= count || params.size() > kMostParamBytes) {
        return 0;
    }
    const std::scoped_lock lock(d.mutex);
    if (d.state == Impl::State::Stopped) {
        return 0;
    }
    const std::uint64_t id = ++d.nextInstance;
    Impl::Remote& remote = d.instances[id];
    remote.kind = kind;
    remote.index = static_cast<std::uint32_t>(index);
    remote.params = params.empty() ? std::string("{}") : std::string(params);
    remote.seed = seed;
    if (d.openRemote(id, remote)) {
        SetEvent(d.channel->toHostEvent());
    }
    return id;
}

StepOutput HostedModule::step(std::uint64_t instance, const StepInput& input)
{
    Impl& d = *m_impl;
    const std::scoped_lock stepping(d.stepMutex);
    bool fresh = false;
    {
        const std::scoped_lock lock(d.mutex);
        if (d.state != Impl::State::Ready || !d.channel) {
            return {.status = StepStatus::Skipped};
        }
        const auto found = d.instances.find(instance);
        if (found == d.instances.end()) {
            return {.status = StepStatus::Failed};
        }
        Impl::Remote& remote = found->second;
        if (remote.generation != d.generation) {
            fresh = remote.opened;
            if (!d.openRemote(instance, remote)) {
                return {.status = StepStatus::Skipped};
            }
        }
        detail::WireWriter payload;
        payload.u64(instance);
        payload.u32(static_cast<std::uint32_t>(input.call));
        payload.u64(input.now);
        payload.u64(input.wakeAt);
        payload.event(input.event);
        payload.command(input.command);
        const std::uint64_t id = ++d.nextId;
        if (!d.channel->toHost().write(detail::Kind::InstanceCall, id, payload.bytes())) {
            return {.status = StepStatus::Skipped};
        }
        d.stepWaiting = id;
        d.stepAnswer.reset();
        ResetEvent(d.stepEvent);
        d.outstanding.emplace(id, Steady::now());
        SetEvent(d.channel->toHostEvent());
    }
    const auto limit = static_cast<LONGLONG>(d.options.stepLimit.count());
    LARGE_INTEGER due{};
    due.QuadPart = -std::max<LONGLONG>(1, limit * 10);
    const bool armed =
        d.stepTimer != nullptr && SetWaitableTimer(d.stepTimer, &due, 0, nullptr, nullptr, FALSE) != FALSE;
    const std::array<HANDLE, 2> waits{d.stepEvent, d.stepTimer};
    const auto milliseconds = static_cast<DWORD>(std::max<LONGLONG>(1, (limit + 999) / 1000));
    static_cast<void>(
        WaitForMultipleObjects(armed ? 2 : 1, waits.data(), FALSE, armed ? milliseconds + 20 : milliseconds));
    if (armed) {
        CancelWaitableTimer(d.stepTimer);
    }
    const std::scoped_lock lock(d.mutex);
    d.stepWaiting = 0;
    if (!d.stepAnswer) {
        ++d.skipped;
        return {.status = StepStatus::Skipped};
    }
    StepOutput result = std::move(*d.stepAnswer);
    d.stepAnswer.reset();
    result.fresh = fresh;
    return result;
}

void HostedModule::resetInstance(std::uint64_t instance)
{
    Impl& d = *m_impl;
    const std::scoped_lock lock(d.mutex);
    const auto found = d.instances.find(instance);
    if (found == d.instances.end() || found->second.generation != d.generation || d.state != Impl::State::Ready ||
        !d.channel) {
        return;
    }
    if (d.channel->toHost().write(detail::Kind::InstanceReset, instance, {})) {
        SetEvent(d.channel->toHostEvent());
    }
}

void HostedModule::closeInstance(std::uint64_t instance)
{
    Impl& d = *m_impl;
    const std::scoped_lock lock(d.mutex);
    const auto found = d.instances.find(instance);
    if (found == d.instances.end()) {
        return;
    }
    const bool live = found->second.generation == d.generation && d.state == Impl::State::Ready && d.channel;
    d.instances.erase(found);
    if (live && d.channel->toHost().write(detail::Kind::InstanceClose, instance, {})) {
        SetEvent(d.channel->toHostEvent());
    }
}

std::uint64_t HostedModule::skippedSteps() const noexcept
{
    const std::scoped_lock lock(m_impl->mutex);
    return m_impl->skipped;
}

std::uint64_t HostedModule::startJob(std::size_t job, std::string_view args, JobEvents events)
{
    Impl& d = *m_impl;
    if (job >= d.info.jobs.size() || args.size() > kMostParamBytes) {
        return 0;
    }
    detail::WireWriter payload;
    payload.u32(static_cast<std::uint32_t>(job));
    payload.text(args.empty() ? std::string_view("{}") : args);
    const std::scoped_lock lock(d.mutex);
    if (d.state != Impl::State::Ready || !d.channel || d.jobs.size() >= kMostRunningJobs) {
        return 0;
    }
    const std::uint64_t id = ++d.nextId;
    if (!d.channel->toHost().write(detail::Kind::JobStart, id, payload.bytes())) {
        return 0;
    }
    d.jobs.emplace(id, std::move(events));
    SetEvent(d.channel->toHostEvent());
    return id;
}

void HostedModule::cancelJob(std::uint64_t job)
{
    Impl& d = *m_impl;
    const std::scoped_lock lock(d.mutex);
    if (d.jobs.contains(job) && d.channel && d.channel->toHost().write(detail::Kind::JobCancel, job, {})) {
        SetEvent(d.channel->toHostEvent());
    }
}

void HostedModule::setSettings(std::string_view json)
{
    if (json.size() > kMostSettingsBytes) {
        return;
    }
    Impl& d = *m_impl;
    detail::WireWriter payload;
    payload.text(json);
    const std::scoped_lock lock(d.mutex);
    d.options.settings = std::string(json);
    if (d.channel && d.channel->toHost().write(detail::Kind::Settings, 0, payload.bytes())) {
        SetEvent(d.channel->toHostEvent());
    }
}

CallResult HostedModule::listDevices(std::size_t provider)
{
    Impl& d = *m_impl;
    if (provider >= d.info.providers.size()) {
        return {CallStatus::Unavailable, {}, "the DLL has no such device provider"};
    }
    detail::WireWriter payload;
    payload.u32(static_cast<std::uint32_t>(provider));
    return d.ask(detail::Kind::DeviceList, payload, "the device provider");
}

CallResult HostedModule::actOnDevice(std::size_t provider, std::string_view device, std::string_view action)
{
    Impl& d = *m_impl;
    if (provider >= d.info.providers.size() || !d.info.providers[provider].acts) {
        return {CallStatus::Unavailable, {}, "the device provider has no actions"};
    }
    if (device.empty() || device.size() > kMostDeviceBytes || action.empty() || action.size() > kMostActionBytes) {
        return {CallStatus::BadArgument, {}, "the device or the action is empty or too long"};
    }
    detail::WireWriter payload;
    payload.u32(static_cast<std::uint32_t>(provider));
    payload.text(device);
    payload.text(action);
    return d.ask(detail::Kind::DeviceAct, payload, "the device provider");
}

bool HostedModule::claimWanted(std::string_view resource, std::string_view by)
{
    if (!validResource(resource) || by.size() > kMostClaimerBytes || !validUtf8(by)) {
        return false;
    }
    detail::WireWriter payload;
    payload.text(resource);
    payload.text(by);
    return m_impl->ask(detail::Kind::ClaimWanted, payload, "the extension", kClaimWantedWait).ok();
}

int HostedModule::crashes() const
{
    const std::scoped_lock lock(m_impl->mutex);
    return m_impl->crashCount;
}

bool HostedModule::waitReady(std::chrono::milliseconds limit) const
{
    std::unique_lock lock(m_impl->mutex);
    m_impl->changed.wait_for(lock, limit, [this] {
        return m_impl->state == Impl::State::Ready || m_impl->state == Impl::State::Stopped;
    });
    return m_impl->state == Impl::State::Ready;
}

bool HostedModule::ready() const
{
    const std::scoped_lock lock(m_impl->mutex);
    return m_impl->state == Impl::State::Ready;
}

}
