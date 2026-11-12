#include "gs/exthost/Hosted.h"

#include "Channel.h"
#include "MediaRing.h"
#include "TapSection.h"
#include "ImageSection.h"
#include "Container.h"
#include "Store.h"
#include "Wire.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <format>
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
constexpr std::size_t kStreamWindow = 128 * 1024;
constexpr UINT kStoppedExit = 9;
constexpr int kWarning = 1;
constexpr std::size_t kMostContainedLines = 200;
constexpr std::size_t kMostContainedNotices = 3;
constexpr std::size_t kMostContainedDeviceChanges = 4;
constexpr std::size_t kMostContainedInputs = 2 * kMostInputSources * kMostInputEventsPerSecond;
constexpr std::chrono::seconds kPaceWindow{1};
constexpr std::chrono::seconds kNoticeWindow{10};
constexpr std::array<std::wstring_view, 14> kContainedVariables{
    L"ComSpec",          L"LOCALAPPDATA",          L"NUMBER_OF_PROCESSORS", L"OS",
    L"PATHEXT",          L"PROCESSOR_ARCHITECTURE", L"PROCESSOR_IDENTIFIER", L"PROCESSOR_LEVEL",
    L"PROCESSOR_REVISION", L"SystemDrive",         L"SystemRoot",           L"TEMP",
    L"TMP",              L"windir"};

class Pace
{
public:
    enum class Verdict : std::uint8_t { Pass, FirstDropped, Dropped };

    Pace(std::chrono::milliseconds window, std::size_t most, std::string overflow)
        : m_window(window)
        , m_most(most)
        , m_overflow(std::move(overflow))
    {
    }

    [[nodiscard]] const std::string& overflow() const noexcept { return m_overflow; }

    [[nodiscard]] Verdict pass(Steady::time_point now) noexcept
    {
        if (now - m_start >= m_window) {
            m_start = now;
            m_count = 0;
        }
        if (m_count < m_most) {
            ++m_count;
            return Verdict::Pass;
        }
        if (m_count == m_most) {
            ++m_count;
            return Verdict::FirstDropped;
        }
        return Verdict::Dropped;
    }

private:
    std::chrono::milliseconds m_window;
    std::size_t m_most;
    std::string m_overflow;
    Steady::time_point m_start{};
    std::size_t m_count = 0;
};

std::wstring containedEnvironment()
{
    std::vector<std::wstring> entries;
    for (const std::wstring_view name : kContainedVariables) {
        std::array<wchar_t, 1024> value{};
        const std::wstring key(name);
        const DWORD size = GetEnvironmentVariableW(key.c_str(), value.data(), static_cast<DWORD>(value.size()));
        if (size > 0 && size < value.size()) {
            entries.push_back(key + L"=" + std::wstring(value.data(), size));
        }
    }
    if (const std::wstring system = detail::systemFolder(); !system.empty()) {
        entries.push_back(L"Path=" + system);
    }
    std::ranges::sort(entries, [](const std::wstring& left, const std::wstring& right) {
        return CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
                                    static_cast<int>(right.size()), TRUE) == CSTR_LESS_THAN;
    });
    std::wstring block;
    for (const std::wstring& entry : entries) {
        block += entry;
        block += L'\0';
    }
    block += L'\0';
    return block;
}

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

bool drawable(const Shape& shape)
{
    return std::isfinite(shape.x) && std::isfinite(shape.y) && std::isfinite(shape.x2) && std::isfinite(shape.y2) &&
           validUtf8(shape.text);
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
    HANDLE job = nullptr;
};

std::optional<Instance> launchContained(const HostedOptions& options, detail::Channel channel, std::string& problem)
{
    const PSID sid = detail::containerSid(options.container, problem);
    if (sid == nullptr || !detail::openToContainers(options.program, problem) ||
        !detail::openToContainer(options.readable.empty() ? options.dll.parent_path() : options.readable, options.dll,
                                 sid, problem)) {
        return std::nullopt;
    }
    const std::array<HANDLE, 3> sources = channel.handles();
    std::array<HANDLE, 3> copies{};
    bool made = true;
    for (std::size_t index = 0; index < sources.size() && made; ++index) {
        made = DuplicateHandle(GetCurrentProcess(), sources[index], GetCurrentProcess(), &copies[index], 0, TRUE,
                               DUPLICATE_SAME_ACCESS) != FALSE;
    }
    PROCESS_INFORMATION started{};
    DWORD error = made ? ERROR_SUCCESS : GetLastError();
    if (made) {
        std::wstring command =
            L"\"" + options.program.wstring() + L"\" --channel " + detail::handleText(copies) + L" --sandbox restricted";
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 2, 0, &size);
        std::vector<std::byte> storage(size);
        auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        SECURITY_CAPABILITIES capabilities{};
        capabilities.AppContainerSid = sid;
        made = InitializeProcThreadAttributeList(attributes, 2, 0, &size) != FALSE;
        if (made) {
            made = UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, copies.data(),
                                             sizeof(HANDLE) * copies.size(), nullptr, nullptr) != FALSE &&
                   UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &capabilities,
                                             sizeof(capabilities), nullptr, nullptr) != FALSE;
            if (made) {
                STARTUPINFOEXW startup{};
                startup.StartupInfo.cb = sizeof(startup);
                startup.lpAttributeList = attributes;
                const std::wstring folder = detail::systemFolder();
                std::wstring environment = containedEnvironment();
                made = CreateProcessW(options.program.c_str(), command.data(), nullptr, nullptr, TRUE,
                                      EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW |
                                          CREATE_UNICODE_ENVIRONMENT,
                                      environment.data(), folder.empty() ? nullptr : folder.c_str(),
                                      &startup.StartupInfo, &started) != FALSE;
            }
            error = made ? ERROR_SUCCESS : GetLastError();
            DeleteProcThreadAttributeList(attributes);
        } else {
            error = GetLastError();
        }
    }
    for (const HANDLE copy : copies) {
        if (copy != nullptr) {
            CloseHandle(copy);
        }
    }
    if (!made) {
        problem = "gs-exthost.exe could not be started in the extension's AppContainer (error " + std::to_string(error) +
                  ")";
        return std::nullopt;
    }
    HANDLE job = detail::containerJob(true);
    bool placed = job != nullptr && AssignProcessToJobObject(job, started.hProcess) != FALSE;
    DWORD placing = placed ? ERROR_SUCCESS : GetLastError();
    if (!placed && placing == ERROR_INVALID_PARAMETER) {
        if (job != nullptr) {
            CloseHandle(job);
        }
        job = detail::containerJob(false);
        placed = job != nullptr && AssignProcessToJobObject(job, started.hProcess) != FALSE;
        placing = placed ? ERROR_SUCCESS : GetLastError();
    }
    if (!placed) {
        problem = "gs-exthost.exe could not be put in the job of the extension's AppContainer (error " +
                  std::to_string(placing) + ")";
        TerminateProcess(started.hProcess, kStoppedExit);
        CloseHandle(started.hThread);
        CloseHandle(started.hProcess);
        if (job != nullptr) {
            CloseHandle(job);
        }
        return std::nullopt;
    }
    ResumeThread(started.hThread);
    CloseHandle(started.hThread);
    return Instance{std::move(channel), started.hProcess, job};
}

std::optional<Instance> launch(const HostedOptions& options, std::string& problem)
{
    std::optional<detail::Channel> channel = detail::Channel::create();
    if (!channel) {
        problem = "the channel to gs-exthost.exe could not be made";
        return std::nullopt;
    }
    detail::WireWriter start;
    start.text(utf8(options.dll));
    start.text(options.container.empty() ? utf8(options.store) : std::string());
    start.u32(static_cast<std::uint32_t>(options.granted.size()));
    for (const std::string& permission : options.granted) {
        start.text(permission);
    }
    start.text(options.settings.size() <= kMostSettingsBytes ? std::string_view(options.settings) : std::string_view());
    start.inputs(options.inputs);
    if (!channel->toHost().write(detail::Kind::Start, 0, start.bytes())) {
        problem = "the start of gs-exthost.exe could not be written";
        return std::nullopt;
    }
    if (!options.container.empty()) {
        return launchContained(options, std::move(*channel), problem);
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
        if (!options.container.empty()) {
            store.emplace(options.store);
        }
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
        failStreams("the extension stopped");
        endStreams("the extension stopped");
        if (process != nullptr) {
            if (WaitForSingleObject(process, kStopWaitMs) != WAIT_OBJECT_0) {
                TerminateProcess(process, kStoppedExit);
            }
            CloseHandle(process);
        }
        if (job != nullptr) {
            CloseHandle(job);
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

    struct TapRemote {
        std::uint32_t index = 0;
        TapOpen info;
        TapEvents events;
        std::shared_ptr<detail::TapSection> section;
        std::uint64_t generation = 0;
        bool opened = false;
        bool failed = false;
    };

    bool openTapRemote(std::uint64_t id, TapRemote& remote)
    {
        if (state != State::Ready || !channel || process == nullptr || remote.section == nullptr) {
            return false;
        }
        HANDLE copy = nullptr;
        if (DuplicateHandle(GetCurrentProcess(), remote.section->mapping(), process, &copy, 0, FALSE,
                            DUPLICATE_SAME_ACCESS) == FALSE) {
            return false;
        }
        remote.section->clear();
        detail::WireWriter payload;
        payload.u32(remote.index);
        payload.text(remote.info.device);
        payload.u32(remote.info.width);
        payload.u32(remote.info.height);
        payload.u32(remote.info.rotation);
        payload.u64(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(copy)));
        payload.u64(remote.section->bytes());
        if (!channel->toHost().write(detail::Kind::TapOpen, id, payload.bytes())) {
            DuplicateHandle(process, copy, nullptr, nullptr, 0, FALSE, DUPLICATE_CLOSE_SOURCE);
            return false;
        }
        SetEvent(channel->toHostEvent());
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

    [[nodiscard]] bool paced(Pace& pace)
    {
        if (options.container.empty()) {
            return true;
        }
        switch (pace.pass(Steady::now())) {
        case Pace::Verdict::Pass:
            return true;
        case Pace::Verdict::FirstDropped:
            if (options.log) {
                options.log(kWarning, "the DLL " + pace.overflow() + "; the ones over that are dropped");
            }
            return false;
        case Pace::Verdict::Dropped:
            return false;
        }
        return false;
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
                for (auto& entry : sights) {
                    if (entry.second.busy == message.id) {
                        entry.second.busy = 0;
                        entry.second.sent = false;
                    }
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
            if (in.u32(level) && in.text(text, 4096) && options.log && paced(linePace)) {
                options.log(static_cast<int>(std::min<std::uint32_t>(level, 2)), text);
            }
            return;
        }
        case detail::Kind::DevicesChanged: {
            std::string provider;
            if (in.text(provider, kMostNameBytes) && options.devicesChanged && paced(changePace)) {
                options.devicesChanged(provider);
            }
            return;
        }
        case detail::Kind::InputEvent: {
            std::string source;
            std::string slot;
            double x = 0.0;
            double y = 0.0;
            std::uint64_t at = 0;
            if (in.text(source, kMostInputNameBytes) && in.text(slot, kMostInputNameBytes) && in.f64(x) && in.f64(y) &&
                in.u64(at) && in.done() && options.input && paced(inputPace)) {
                options.input(source, slot, x, y, at);
            }
            return;
        }
        case detail::Kind::Event: {
            std::string topic;
            std::string json;
            if (in.text(topic, kMostTopicBytes) && in.text(json, kMostEventBytes) && validTopic(topic) &&
                !reservedTopic(topic) && options.event &&
                (options.container.empty() || (validUtf8(json) && nlohmann::json::accept(json)))) {
                options.event(topic, json);
            }
            return;
        }
        case detail::Kind::Subscribe: {
            std::string topic;
            std::uint32_t on = 0;
            if (!in.text(topic, kMostTopicBytes) || !in.u32(on) || !in.done() || !appTopic(topic)) {
                return;
            }
            const std::scoped_lock lock(mutex);
            if (on != 0) {
                heard.insert(std::move(topic));
            } else {
                heard.erase(topic);
            }
            return;
        }
        case detail::Kind::Notify: {
            std::string title;
            std::string text;
            if (in.text(title, 1024) && in.text(text, 4096) && options.notify && paced(noticePace)) {
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
        case detail::Kind::ServiceCall: {
            std::string service;
            std::string json;
            std::uint64_t launch = 0;
            {
                const std::scoped_lock lock(mutex);
                launch = launches;
            }
            if (!in.text(service, kMostServiceTargetBytes) || !in.text(json, kMostServiceBytes) || !in.done()) {
                answerService(message.id, {ServiceStatus::BadArgument, "the call to the service was damaged"}, launch);
                return;
            }
            if (!options.callService) {
                answerService(message.id, {}, launch);
                return;
            }
            options.callService(service, json, [link = claimLink, id = message.id, launch](ServiceResult result) {
                const std::scoped_lock lock(link->mutex);
                if (link->impl != nullptr) {
                    link->impl->answerService(id, result, launch);
                }
            });
            return;
        }
        case detail::Kind::TransportChanged: {
            std::string transport;
            if (in.text(transport, kMostNameBytes) && in.done() && options.transportChanged) {
                options.transportChanged(transport);
            }
            return;
        }
        case detail::Kind::StreamOpened: {
            const std::shared_ptr<const StreamEvents> events = eventsOf(message.id);
            if (events != nullptr && events->opened) {
                events->opened();
            }
            return;
        }
        case detail::Kind::StreamData: {
            const std::shared_ptr<const StreamEvents> events = eventsOf(message.id);
            if (events != nullptr && events->data) {
                events->data(message.payload);
            }
            detail::WireWriter taken;
            taken.u32(static_cast<std::uint32_t>(message.payload.size()));
            const std::scoped_lock lock(mutex);
            if (channel && channel->toHost().write(detail::Kind::StreamTaken, message.id, taken.bytes())) {
                SetEvent(channel->toHostEvent());
            }
            return;
        }
        case detail::Kind::StreamSent: {
            std::uint32_t size = 0;
            if (!in.u32(size) || !in.done()) {
                return;
            }
            std::shared_ptr<const StreamEvents> events;
            {
                const std::scoped_lock lock(mutex);
                const auto found = streams.find(message.id);
                if (found == streams.end()) {
                    return;
                }
                found->second.unsent -= std::min<std::size_t>(found->second.unsent, size);
                if (found->second.waiting) {
                    found->second.waiting = false;
                    events = found->second.events;
                }
            }
            if (events != nullptr && events->writable) {
                events->writable();
            }
            return;
        }
        case detail::Kind::StreamClosed: {
            std::string reason;
            if (!in.text(reason, kMostReasonBytes)) {
                reason.clear();
            }
            std::shared_ptr<const StreamEvents> events;
            {
                const std::scoped_lock lock(mutex);
                const auto found = streams.find(message.id);
                if (found == streams.end()) {
                    return;
                }
                events = std::move(found->second.events);
                streams.erase(found);
            }
            if (events->closed) {
                events->closed(std::move(reason));
            }
            return;
        }
        case detail::Kind::MediaRing:
            adoptRing(message.id, in);
            return;
        case detail::Kind::StreamEnded: {
            std::string why;
            if (!in.text(why, 4096)) {
                return;
            }
            std::shared_ptr<detail::StreamBox> source;
            std::shared_ptr<detail::SinkBox> sink;
            {
                const std::scoped_lock lock(mutex);
                if (const auto found = sources.find(message.id); found != sources.end()) {
                    source = found->second;
                }
                if (const auto found = sinks.find(message.id); found != sinks.end()) {
                    sink = found->second;
                }
            }
            if (source != nullptr) {
                source->end(why);
            }
            if (sink != nullptr) {
                sink->end(why);
            }
            return;
        }
        case detail::Kind::SourcesChanged: {
            std::string source;
            if (in.text(source, kMostNameBytes) && options.sourcesChanged) {
                options.sourcesChanged(source);
            }
            return;
        }
        case detail::Kind::TapOverlay: {
            std::vector<Shape> shapes;
            if (!in.shapes(shapes) || !in.done()) {
                return;
            }
            std::erase_if(shapes, [](const Shape& shape) { return !drawable(shape); });
            std::function<void(std::vector<Shape>)> overlay;
            {
                const std::scoped_lock lock(mutex);
                const auto found = taps.find(message.id);
                if (found == taps.end() || found->second.generation != generation) {
                    return;
                }
                overlay = found->second.events.overlay;
            }
            if (overlay) {
                overlay(uniqueShapes(std::move(shapes)));
            }
            return;
        }
        case detail::Kind::TapFailed: {
            std::string why;
            if (!in.text(why, 4096) || why.empty()) {
                why = "the frame tap did not start";
            }
            std::function<void(std::string)> failed;
            {
                const std::scoped_lock lock(mutex);
                const auto found = taps.find(message.id);
                if (found == taps.end()) {
                    return;
                }
                found->second.failed = true;
                failed = found->second.events.failed;
            }
            if (failed) {
                failed(std::move(why));
            }
            return;
        }
        case detail::Kind::StoreCall: {
            std::string key;
            std::uint32_t setting = 0;
            std::uint32_t has = 0;
            std::string json;
            std::uint64_t launch = 0;
            {
                const std::scoped_lock lock(mutex);
                launch = launches;
            }
            if (!in.text(key, detail::kMostStoreKeyBytes) || !in.u32(setting) || !in.u32(has) ||
                !in.text(json, kMostServiceBytes) || !in.done()) {
                answerStore(message.id, {ServiceStatus::BadArgument, std::nullopt}, launch);
                return;
            }
            if (!store) {
                answerStore(message.id, {}, launch);
                return;
            }
            if (setting == 0) {
                std::optional<std::string> found = store->get(key);
                answerStore(message.id,
                            found && found->size() > kMostServiceBytes
                                ? StoreReply{ServiceStatus::Failed, std::nullopt}
                                : StoreReply{ServiceStatus::Ok, std::move(found)},
                            launch);
                return;
            }
            const std::optional<std::string_view> value =
                has != 0 ? std::optional<std::string_view>(std::string_view(json)) : std::nullopt;
            ServiceStatus status = ServiceStatus::Failed;
            switch (store->set(key, value)) {
            case detail::StoreResult::Ok:
                status = ServiceStatus::Ok;
                break;
            case detail::StoreResult::BadArgument:
            case detail::StoreResult::Full:
                status = ServiceStatus::BadArgument;
                break;
            case detail::StoreResult::Unavailable:
                status = ServiceStatus::Unsupported;
                break;
            case detail::StoreResult::Failed:
                status = ServiceStatus::Failed;
                break;
            }
            answerStore(message.id, {status, std::nullopt}, launch);
            return;
        }
        case detail::Kind::DeviceCall: {
            std::string request;
            std::string json;
            std::uint64_t launch = 0;
            {
                const std::scoped_lock lock(mutex);
                launch = launches;
            }
            if (!in.text(request, kMostNameBytes) || !in.text(json, kMostServiceBytes) || !in.done()) {
                answerDevice(message.id, {ServiceStatus::BadArgument, "the call to the device was damaged"}, launch);
                return;
            }
            if (!options.device) {
                answerDevice(message.id, {ServiceStatus::Unsupported, "GameScrcpy does not reach devices here"},
                             launch);
                return;
            }
            options.device(request, json, [link = claimLink, id = message.id, launch](ServiceResult result) {
                const std::scoped_lock lock(link->mutex);
                if (link->impl != nullptr) {
                    link->impl->answerDevice(id, result, launch);
                }
            });
            return;
        }
        default:
            return;
        }
    }

    void answerStore(std::uint64_t id, const StoreReply& reply, std::uint64_t launch)
    {
        const std::scoped_lock lock(mutex);
        if (launch != launches || !channel) {
            return;
        }
        detail::WireWriter out;
        out.u32(static_cast<std::uint32_t>(reply.status));
        out.u32(reply.value ? 1U : 0U);
        out.text(reply.value ? std::string_view(*reply.value) : std::string_view());
        if (channel->toHost().write(detail::Kind::StoreAnswer, id, out.bytes())) {
            SetEvent(channel->toHostEvent());
        }
    }

    void answerDevice(std::uint64_t id, const ServiceResult& result, std::uint64_t launch)
    {
        const std::scoped_lock lock(mutex);
        if (id == 0 || launch != launches || !channel) {
            return;
        }
        const bool fits = result.text.size() <= kMostServiceBytes;
        detail::WireWriter out;
        out.u32(static_cast<std::uint32_t>(fits ? result.status : ServiceStatus::Failed));
        out.text(fits ? std::string_view(result.text) : std::string_view("the answer of the device is too large"));
        if (channel->toHost().write(detail::Kind::DeviceAnswer, id, out.bytes())) {
            SetEvent(channel->toHostEvent());
        }
    }

    void answerService(std::uint64_t id, const ServiceResult& result, std::uint64_t launch)
    {
        const std::scoped_lock lock(mutex);
        if (launch != launches || !channel) {
            return;
        }
        const bool fits = result.text.size() <= kMostServiceBytes;
        detail::WireWriter out;
        out.u32(static_cast<std::uint32_t>(fits ? result.status : ServiceStatus::Failed));
        out.text(fits ? std::string_view(result.text) : std::string_view("GS-API-148 the answer is larger than 252 KiB"));
        if (channel->toHost().write(detail::Kind::ServiceAnswer, id, out.bytes())) {
            SetEvent(channel->toHostEvent());
        }
    }

    std::shared_ptr<const StreamEvents> eventsOf(std::uint64_t id)
    {
        const std::scoped_lock lock(mutex);
        const auto found = streams.find(id);
        return found == streams.end() ? nullptr : found->second.events;
    }

    void failStreams(const std::string& why)
    {
        std::map<std::uint64_t, Stream> ended;
        {
            const std::scoped_lock lock(mutex);
            ended.swap(streams);
        }
        for (const auto& [id, stream] : ended) {
            if (stream.events->closed) {
                stream.events->closed(why);
            }
        }
    }

    void adoptRing(std::uint64_t id, detail::WireReader& in)
    {
        std::uint32_t kind = 0;
        std::uint64_t capacity = 0;
        std::uint64_t remote = 0;
        if (!in.u32(kind) || kind > static_cast<std::uint32_t>(MediaKind::Audio) || !in.u64(capacity) ||
            !in.u64(remote) || !in.done()) {
            return;
        }
        std::shared_ptr<detail::StreamBox> box;
        HANDLE local = nullptr;
        {
            const std::scoped_lock lock(mutex);
            const auto found = sources.find(id);
            if (found == sources.end() || process == nullptr) {
                return;
            }
            box = found->second;
            if (DuplicateHandle(process, reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(remote)),
                                GetCurrentProcess(), &local, 0, FALSE, DUPLICATE_SAME_ACCESS) == FALSE) {
                local = nullptr;
            }
        }
        const auto bytes = static_cast<std::size_t>(capacity);
        bool adopted = false;
        if (local != nullptr && kind == static_cast<std::uint32_t>(MediaKind::Video)) {
            if (std::shared_ptr<detail::FrameRing> ring = detail::FrameRing::adopt(local, bytes)) {
                box->adoptFrames(std::move(ring));
                adopted = true;
            }
        } else if (local != nullptr) {
            if (std::shared_ptr<detail::SoundRing> ring = detail::SoundRing::adopt(local, bytes)) {
                box->adoptSounds(std::move(ring));
                adopted = true;
            }
        }
        if (!adopted) {
            box->end("the extension's process could not share the picture or the sound");
        }
    }

    void endStreams(const std::string& why)
    {
        std::map<std::uint64_t, std::shared_ptr<detail::StreamBox>> open;
        std::map<std::uint64_t, std::shared_ptr<detail::SinkBox>> sending;
        {
            const std::scoped_lock lock(mutex);
            open.swap(sources);
            sending.swap(sinks);
        }
        for (const auto& [id, box] : open) {
            box->end(why);
        }
        for (const auto& [id, box] : sending) {
            box->end(why);
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
        endStreams("the extension's process stopped");
        std::vector<std::uint64_t> lost;
        bool restart = false;
        bool gaveUp = false;
        bool failedLate = false;
        std::string why;
        std::set<std::string> dropped;
        std::vector<std::function<void(std::vector<Shape>)>> cleared;
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
            if (job != nullptr) {
                CloseHandle(job);
                job = nullptr;
            }
            channel.reset();
            dropped.swap(held);
            heard.clear();
            for (auto& entry : sights) {
                if (entry.second.sent) {
                    entry.second.busy = 0;
                    entry.second.sent = false;
                }
            }
            if (closing || state == State::Stopped) {
                return;
            }
            for (auto& [id, tap] : taps) {
                tap.opened = false;
                if (tap.events.overlay) {
                    cleared.push_back(tap.events.overlay);
                }
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
        for (const auto& overlay : cleared) {
            overlay({});
        }
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
        failStreams("the extension's process stopped");
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
                    if (instance->job != nullptr) {
                        CloseHandle(instance->job);
                    }
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
                job = instance->job;
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
    HANDLE job = nullptr;
    std::optional<detail::ModuleStore> store;
    Pace linePace{kPaceWindow, kMostContainedLines,
                  std::format("logged more than {} lines in a second", kMostContainedLines)};
    Pace noticePace{kNoticeWindow, kMostContainedNotices,
                    std::format("showed more than {} notices within {} seconds", kMostContainedNotices,
                                kNoticeWindow.count())};
    Pace changePace{kPaceWindow, kMostContainedDeviceChanges,
                    std::format("said more than {} times in a second that its devices changed",
                                kMostContainedDeviceChanges)};
    Pace inputPace{kPaceWindow, kMostContainedInputs,
                   std::format("sent more than {} input events in a second", kMostContainedInputs)};
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
    struct Stream {
        std::shared_ptr<const StreamEvents> events;
        std::size_t unsent = 0;
        bool waiting = false;
    };
    std::map<std::uint64_t, Stream> streams;
    struct ClaimLink {
        std::mutex mutex;
        Impl* impl = nullptr;
    };
    std::shared_ptr<ClaimLink> claimLink = std::make_shared<ClaimLink>();
    std::set<std::string> held;
    std::uint64_t launches = 0;
    std::set<std::string, std::less<>> heard;
    std::map<std::uint64_t, std::shared_ptr<detail::StreamBox>> sources;
    std::map<std::uint64_t, std::shared_ptr<detail::SinkBox>> sinks;
    std::map<std::uint64_t, TapRemote> taps;
    std::uint64_t nextTap = 0;
    struct Sight {
        std::shared_ptr<detail::ImageSection> section;
        std::optional<std::uint64_t> sentTo;
        std::uint64_t busy = 0;
        bool sent = false;
    };
    std::map<std::size_t, Sight> sights;
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
    impl->job = instance->job;
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

std::uint64_t HostedModule::openTap(std::size_t tap, const TapOpen& info, TapEvents events)
{
    Impl& d = *m_impl;
    if (tap >= d.info.frameTaps.size() || info.device.size() > kMostDeviceBytes) {
        return 0;
    }
    std::shared_ptr<detail::TapSection> section = detail::TapSection::create(info.width, info.height);
    if (section == nullptr) {
        return 0;
    }
    const std::scoped_lock lock(d.mutex);
    if (d.state == Impl::State::Stopped) {
        return 0;
    }
    const std::uint64_t id = ++d.nextTap;
    Impl::TapRemote& remote = d.taps[id];
    remote.index = static_cast<std::uint32_t>(tap);
    remote.info = info;
    remote.events = std::move(events);
    remote.section = std::move(section);
    static_cast<void>(d.openTapRemote(id, remote));
    return id;
}

bool HostedModule::offerFrame(std::uint64_t tap, const TapFrame& frame)
{
    Impl& d = *m_impl;
    if (!validTapFrame(frame)) {
        return false;
    }
    std::shared_ptr<detail::TapSection> section;
    {
        const std::scoped_lock lock(d.mutex);
        const auto found = d.taps.find(tap);
        if (found == d.taps.end() || found->second.failed) {
            return false;
        }
        if (d.state != Impl::State::Ready || !d.channel) {
            return true;
        }
        Impl::TapRemote& remote = found->second;
        if ((!remote.opened || remote.generation != d.generation) && !d.openTapRemote(tap, remote)) {
            return true;
        }
        section = remote.section;
    }
    if (!section->put(frame)) {
        return true;
    }
    const std::scoped_lock lock(d.mutex);
    if (d.channel && d.taps.contains(tap) && d.channel->toHost().write(detail::Kind::TapFrame, tap, {})) {
        SetEvent(d.channel->toHostEvent());
    }
    return true;
}

void HostedModule::closeTap(std::uint64_t tap)
{
    Impl& d = *m_impl;
    const std::scoped_lock lock(d.mutex);
    const auto found = d.taps.find(tap);
    if (found == d.taps.end()) {
        return;
    }
    const bool live = found->second.opened && found->second.generation == d.generation &&
                      d.state == Impl::State::Ready && d.channel;
    if (live && d.channel->toHost().write(detail::Kind::TapClose, tap, {})) {
        SetEvent(d.channel->toHostEvent());
    }
    d.taps.erase(found);
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

bool HostedModule::hears(std::string_view topic) const
{
    const std::scoped_lock lock(m_impl->mutex);
    return m_impl->heard.contains(topic);
}

void HostedModule::appEvent(std::string_view topic, std::string_view json)
{
    if (topic.size() > kMostTopicBytes || json.size() > kMostEventBytes) {
        return;
    }
    Impl& d = *m_impl;
    detail::WireWriter payload;
    payload.text(topic);
    payload.text(json);
    const std::scoped_lock lock(d.mutex);
    if (d.state != Impl::State::Ready || !d.channel || !d.heard.contains(topic)) {
        return;
    }
    if (d.channel->toHost().write(detail::Kind::AppEvent, 0, payload.bytes())) {
        SetEvent(d.channel->toHostEvent());
    }
}

CallResult HostedModule::discover(std::size_t transport)
{
    Impl& d = *m_impl;
    if (transport >= d.info.transports.size()) {
        return {CallStatus::Unavailable, {}, "the DLL has no such transport"};
    }
    detail::WireWriter payload;
    payload.u32(static_cast<std::uint32_t>(transport));
    return d.ask(detail::Kind::TransportDiscover, payload, "the transport");
}

bool HostedModule::openStream(std::size_t transport, std::uint64_t stream, const StreamRequest& request,
                              StreamEvents events)
{
    Impl& d = *m_impl;
    if (transport >= d.info.transports.size() || stream == 0 || request.phone.empty() ||
        request.phone.size() > kMostDeviceBytes || request.channel.empty() || request.channel.size() > kMostNameBytes ||
        request.session.size() > kMostSessionBytes) {
        return false;
    }
    detail::WireWriter payload;
    payload.u32(static_cast<std::uint32_t>(transport));
    payload.text(request.phone);
    payload.text(request.channel);
    payload.text(request.session);
    const std::scoped_lock lock(d.mutex);
    if (d.state != Impl::State::Ready || !d.channel || d.streams.contains(stream) ||
        !d.channel->toHost().write(detail::Kind::StreamOpen, stream, payload.bytes())) {
        return false;
    }
    d.streams.emplace(stream, Impl::Stream{.events = std::make_shared<const StreamEvents>(std::move(events))});
    SetEvent(d.channel->toHostEvent());
    return true;
}

StreamSent HostedModule::sendStream(std::uint64_t stream, std::string_view bytes)
{
    Impl& d = *m_impl;
    if (bytes.size() > kMostStreamBytes) {
        return StreamSent::Failed;
    }
    const std::scoped_lock lock(d.mutex);
    const auto found = d.streams.find(stream);
    if (found == d.streams.end() || d.state != Impl::State::Ready || !d.channel) {
        return StreamSent::Failed;
    }
    if (bytes.empty()) {
        return StreamSent::Taken;
    }
    if (found->second.unsent + bytes.size() > kStreamWindow) {
        found->second.waiting = true;
        return StreamSent::Busy;
    }
    if (!d.channel->toHost().write(detail::Kind::StreamSend, stream, bytes)) {
        return StreamSent::Failed;
    }
    found->second.unsent += bytes.size();
    SetEvent(d.channel->toHostEvent());
    return StreamSent::Taken;
}

void HostedModule::closeStream(std::uint64_t stream)
{
    Impl& d = *m_impl;
    const std::scoped_lock lock(d.mutex);
    if (d.streams.erase(stream) > 0 && d.channel && d.channel->toHost().write(detail::Kind::StreamClose, stream, {})) {
        SetEvent(d.channel->toHostEvent());
    }
}

namespace {

std::uint64_t valueOf(HANDLE handle) noexcept
{
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle));
}

void closeRemote(HANDLE process, HANDLE handle) noexcept
{
    if (process != nullptr && handle != nullptr) {
        DuplicateHandle(process, handle, nullptr, nullptr, 0, FALSE, DUPLICATE_CLOSE_SOURCE);
    }
}

}

CallResult HostedModule::listSources(MediaKind kind, std::size_t source)
{
    Impl& d = *m_impl;
    const std::size_t count = kind == MediaKind::Video ? d.info.videoSources.size() : d.info.audioSources.size();
    if (source >= count) {
        return {CallStatus::Unavailable, {}, "the DLL has no such source"};
    }
    detail::WireWriter payload;
    payload.u32(static_cast<std::uint32_t>(kind));
    payload.u32(static_cast<std::uint32_t>(source));
    return d.ask(detail::Kind::SourceList, payload, "the source");
}

std::shared_ptr<MediaStream> HostedModule::openSource(MediaKind kind, std::size_t source, std::string_view id,
                                                      std::string_view params)
{
    Impl& d = *m_impl;
    const std::size_t count = kind == MediaKind::Video ? d.info.videoSources.size() : d.info.audioSources.size();
    if (source >= count || !validSourceId(id) || params.size() > kMostParamBytes) {
        return nullptr;
    }
    const std::scoped_lock lock(d.mutex);
    if (d.state != Impl::State::Ready || !d.channel || d.process == nullptr || d.sources.size() >= kMostStreams) {
        return nullptr;
    }
    const std::uint64_t stream = ++d.nextId;
    auto box = std::make_shared<detail::StreamBox>(stream, nullptr, detail::StreamBox::Hooks{});
    HANDLE remote = nullptr;
    if (box->wakeHandle() == nullptr || DuplicateHandle(GetCurrentProcess(), box->wakeHandle(), d.process, &remote, 0,
                                                        FALSE, DUPLICATE_SAME_ACCESS) == FALSE) {
        return nullptr;
    }
    detail::WireWriter payload;
    payload.u32(static_cast<std::uint32_t>(kind));
    payload.u32(static_cast<std::uint32_t>(source));
    payload.text(id);
    payload.text(params.empty() ? std::string_view("{}") : params);
    payload.u64(valueOf(remote));
    if (!d.channel->toHost().write(detail::Kind::SourceOpen, stream, payload.bytes())) {
        closeRemote(d.process, remote);
        return nullptr;
    }
    d.sources.emplace(stream, box);
    SetEvent(d.channel->toHostEvent());
    return box;
}

void HostedModule::closeSource(std::uint64_t stream)
{
    Impl& d = *m_impl;
    std::shared_ptr<detail::StreamBox> box;
    {
        const std::scoped_lock lock(d.mutex);
        const auto found = d.sources.find(stream);
        if (found == d.sources.end()) {
            return;
        }
        box = std::move(found->second);
        d.sources.erase(found);
        if (d.channel && d.channel->toHost().write(detail::Kind::SourceClose, stream, {})) {
            SetEvent(d.channel->toHostEvent());
        }
    }
    box->close();
}

std::shared_ptr<SinkPort> HostedModule::openSink(std::size_t sink, const SinkStart& start)
{
    Impl& d = *m_impl;
    if (sink >= d.info.sinks.size() || start.device.size() > kMostDeviceBytes || start.name.size() > kMostDeviceBytes) {
        return nullptr;
    }
    const std::scoped_lock lock(d.mutex);
    if (d.state != Impl::State::Ready || !d.channel || d.process == nullptr || d.sinks.size() >= kMostSinks) {
        return nullptr;
    }
    const std::uint64_t id = ++d.nextId;
    std::shared_ptr<detail::SinkBox> box = detail::SinkBox::make(id, start);
    if (box == nullptr) {
        return nullptr;
    }
    const std::array<HANDLE, 3> local{box->framesHandle(), box->soundsHandle(), box->wakeHandle()};
    std::array<HANDLE, 3> remote{};
    bool shared = true;
    for (std::size_t index = 0; index < local.size(); ++index) {
        if (local[index] != nullptr && shared) {
            shared = DuplicateHandle(GetCurrentProcess(), local[index], d.process, &remote[index], 0, FALSE,
                                     DUPLICATE_SAME_ACCESS) != FALSE;
        }
    }
    detail::WireWriter payload;
    payload.u32(static_cast<std::uint32_t>(sink));
    payload.text(start.device);
    payload.text(start.name);
    payload.u32(start.width);
    payload.u32(start.height);
    payload.u32(start.fps);
    payload.u32(start.audio ? 1U : 0U);
    payload.u64(valueOf(remote[0]));
    payload.u64(valueOf(remote[1]));
    payload.u64(valueOf(remote[2]));
    if (!shared || !d.channel->toHost().write(detail::Kind::SinkOpen, id, payload.bytes())) {
        for (const HANDLE handle : remote) {
            closeRemote(d.process, handle);
        }
        return nullptr;
    }
    d.sinks.emplace(id, box);
    SetEvent(d.channel->toHostEvent());
    return box;
}

void HostedModule::closeSink(std::uint64_t sink)
{
    Impl& d = *m_impl;
    std::shared_ptr<detail::SinkBox> box;
    {
        const std::scoped_lock lock(d.mutex);
        const auto found = d.sinks.find(sink);
        if (found == d.sinks.end()) {
            return;
        }
        box = std::move(found->second);
        d.sinks.erase(found);
        if (d.channel && d.channel->toHost().write(detail::Kind::SinkClose, sink, {})) {
            SetEvent(d.channel->toHostEvent());
        }
    }
    box->end("the sending stopped");
}

std::uint64_t HostedModule::startExport(std::size_t exporter, std::string_view input, std::string_view output,
                                        JobEvents events)
{
    Impl& d = *m_impl;
    if (exporter >= d.info.exporters.size() || input.empty() || output.empty() ||
        input.size() > kMostExportPathBytes || output.size() > kMostExportPathBytes) {
        return 0;
    }
    detail::WireWriter payload;
    payload.u32(static_cast<std::uint32_t>(exporter));
    payload.text(input);
    payload.text(output);
    const std::scoped_lock lock(d.mutex);
    if (d.state != Impl::State::Ready || !d.channel || d.jobs.size() >= kMostRunningJobs) {
        return 0;
    }
    const std::uint64_t id = ++d.nextId;
    if (!d.channel->toHost().write(detail::Kind::ExportStart, id, payload.bytes())) {
        return 0;
    }
    d.jobs.emplace(id, std::move(events));
    SetEvent(d.channel->toHostEvent());
    return id;
}

CallResult HostedModule::recognize(std::size_t engine, VisionKind kind, const VisionImage& image,
                                   std::string_view params)
{
    Impl& d = *m_impl;
    if (engine >= d.info.visionEngines.size()) {
        return {CallStatus::Unavailable, {}, "the DLL has no such vision engine"};
    }
    if ((d.info.visionEngines[engine].kinds & visionBit(kind)) == 0) {
        return {CallStatus::BadArgument, {},
                "the vision engine does not recognize " + std::string(visionKindName(kind))};
    }
    if (!validImage(image) || params.size() > kMostVisionParamBytes || !validUtf8(params)) {
        return {CallStatus::BadArgument, {}, "the picture or the params are not valid"};
    }
    const std::size_t row = std::size_t{4} * image.width;
    const std::uint64_t needed = std::uint64_t{row} * image.height;
    std::shared_ptr<detail::ImageSection> section;
    std::uint64_t id = 0;
    {
        const std::scoped_lock lock(d.mutex);
        if (d.state != Impl::State::Ready || !d.channel) {
            return {CallStatus::Unavailable, {},
                    d.state == Impl::State::Stopped ? "the extension stopped working and was turned off"
                                                    : "the extension is starting again"};
        }
        Impl::Sight& sight = d.sights[engine];
        if (sight.busy != 0) {
            return {CallStatus::TooSlow, {}, "the engine is still working on an earlier picture"};
        }
        if (sight.section == nullptr || sight.section->bytes() < needed) {
            sight.section = detail::ImageSection::create(needed);
            sight.sentTo.reset();
            if (sight.section == nullptr) {
                return {CallStatus::Failed, {}, "no shared memory could be made for the picture"};
            }
        }
        id = ++d.nextId;
        sight.busy = id;
        sight.sent = false;
        section = sight.section;
    }
    for (std::uint32_t line = 0; line < image.height; ++line) {
        std::memcpy(section->data() + std::size_t{line} * row, image.pixels.get() + std::size_t{line} * image.stride,
                    row);
    }
    std::unique_lock lock(d.mutex);
    Impl::Sight& sight = d.sights[engine];
    const auto failed = [&sight, id](CallStatus status, std::string problem) {
        if (sight.busy == id) {
            sight.busy = 0;
        }
        return CallResult{status, {}, std::move(problem)};
    };
    if (sight.busy != id) {
        return CallResult{CallStatus::TooSlow, {}, "the engine is still working on an earlier picture"};
    }
    if (d.state != Impl::State::Ready || !d.channel || d.process == nullptr || sight.section != section) {
        return failed(CallStatus::Unavailable, "the extension is starting again");
    }
    HANDLE copy = nullptr;
    if (sight.sentTo != d.launches &&
        DuplicateHandle(GetCurrentProcess(), section->mapping(), d.process, &copy, FILE_MAP_READ, FALSE, 0) == FALSE) {
        return failed(CallStatus::Failed, "the picture could not be handed to the extension's process");
    }
    detail::WireWriter payload;
    payload.u32(static_cast<std::uint32_t>(engine));
    payload.u32(static_cast<std::uint32_t>(kind));
    payload.u32(image.width);
    payload.u32(image.height);
    payload.u64(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(copy)));
    payload.u64(section->bytes());
    payload.text(params.empty() ? std::string_view("{}") : params);
    if (!d.channel->toHost().write(detail::Kind::VisionRun, id, payload.bytes())) {
        if (copy != nullptr) {
            DuplicateHandle(d.process, copy, nullptr, nullptr, 0, FALSE, DUPLICATE_CLOSE_SOURCE);
        }
        return failed(CallStatus::Unavailable, "the extension's process is too busy to take the picture");
    }
    if (copy != nullptr) {
        sight.sentTo = d.launches;
    }
    sight.sent = true;
    d.answers.emplace(id, std::nullopt);
    SetEvent(d.channel->toHostEvent());
    const std::chrono::milliseconds limit = d.options.visionLimit;
    const bool answered = d.changed.wait_for(lock, limit, [&d, id] { return d.answers.at(id).has_value(); });
    CallResult result = answered ? std::move(*d.answers.at(id))
                                 : CallResult{CallStatus::TooSlow, {},
                                              "the engine took longer than " + std::to_string(limit.count()) + " ms"};
    d.answers.erase(id);
    return result;
}

void HostedModule::deviceEvent(std::uint64_t device, std::string_view json)
{
    if (device == 0 || json.size() > kMostServiceBytes) {
        return;
    }
    Impl& d = *m_impl;
    detail::WireWriter payload;
    payload.text(json);
    const std::scoped_lock lock(d.mutex);
    if (d.state != Impl::State::Ready || !d.channel) {
        return;
    }
    if (d.channel->toHost().write(detail::Kind::DeviceEvent, device, payload.bytes())) {
        SetEvent(d.channel->toHostEvent());
    }
}

}
