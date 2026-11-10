#include "gs/script/ScriptHost.h"

#include "Engine.h"

#include <windows.h>

#include <process.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <deque>
#include <iterator>
#include <mutex>
#include <optional>
#include <utility>

namespace gs::script {

namespace {

constexpr std::size_t kStackMargin = std::size_t{1} << 20;

std::string describeError(const char* what)
{
    return std::string(what) + " (error " + std::to_string(GetLastError()) + ")";
}

}

struct ScriptHost::Impl {
    using Command = std::function<void(detail::Engine&)>;

    Impl(Sink target, const Limits& configured, std::shared_ptr<ScriptServices> provided)
        : sink(std::move(target))
        , limits(configured)
        , services(std::move(provided))
        , wake(CreateEventW(nullptr, FALSE, FALSE, nullptr))
        , timer(CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                       TIMER_ALL_ACCESS))
    {
        if (timer == nullptr) {
            timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        }
    }

    ~Impl()
    {
        if (timer != nullptr) {
            CloseHandle(timer);
        }
        if (wake != nullptr) {
            CloseHandle(wake);
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    static unsigned __stdcall entry(void* opaque);
    void run();
    void post(Command command);
    void runCommands(detail::Engine& engine);
    void arm(std::optional<Microseconds> due, Microseconds now);
    void fail(std::string what);

    Sink sink;
    const Limits limits;
    std::shared_ptr<ScriptServices> services;
    HANDLE wake = nullptr;
    HANDLE timer = nullptr;
    HANDLE thread = nullptr;
    std::atomic<bool> stopRequested{false};
    std::atomic<ScriptId> lastScript{0};
    std::atomic<TaskId> lastTask{0};
    mutable std::mutex mutex;
    std::deque<Command> commands;
    std::string problem;
    std::optional<Microseconds> armedFor;
    Microseconds frameInterval{0};
    std::optional<Microseconds> nextFrame;
};

unsigned __stdcall ScriptHost::Impl::entry(void* opaque)
{
    static_cast<Impl*>(opaque)->run();
    return 0;
}

void ScriptHost::Impl::run()
{
    SetThreadDescription(GetCurrentThread(), L"gs-script");
    detail::Engine engine(sink, limits, stopRequested, lastTask, detail::EngineOptions{.services = services});
    while (!stopRequested.load(std::memory_order_acquire)) {
        runCommands(engine);
        if (stopRequested.load(std::memory_order_acquire)) {
            break;
        }
        try {
            engine.fireTimers();
            if (nextFrame && engine.now() >= *nextFrame) {
                nextFrame.reset();
                engine.frame();
            }
        } catch (...) {
            fail("a script timer failed");
        }
        std::optional<Microseconds> due = engine.nextTimer();
        if (frameInterval.count() > 0 && engine.waitingForFrame()) {
            if (!nextFrame) {
                nextFrame = engine.now() + frameInterval;
            }
            due = due ? std::min(*due, *nextFrame) : *nextFrame;
        } else {
            nextFrame.reset();
        }
        arm(due, engine.now());
        const HANDLE handles[] = {wake, timer};
        const DWORD result = WaitForMultipleObjects(static_cast<DWORD>(std::size(handles)), handles,
                                                    FALSE, INFINITE);
        if (result == WAIT_OBJECT_0 + 1) {
            armedFor.reset();
        } else if (result != WAIT_OBJECT_0) {
            fail(describeError("the script thread stopped waiting"));
            break;
        }
    }
    try {
        engine.shutdown();
    } catch (...) {
        fail("the scripts could not be stopped cleanly");
    }
}

void ScriptHost::Impl::post(Command command)
{
    {
        const std::scoped_lock lock(mutex);
        commands.push_back(std::move(command));
    }
    SetEvent(wake);
}

void ScriptHost::Impl::runCommands(detail::Engine& engine)
{
    std::deque<Command> pending;
    {
        const std::scoped_lock lock(mutex);
        pending.swap(commands);
    }
    for (Command& command : pending) {
        if (stopRequested.load(std::memory_order_acquire)) {
            return;
        }
        try {
            command(engine);
        } catch (...) {
            fail("a script command failed");
        }
    }
}

void ScriptHost::Impl::arm(std::optional<Microseconds> due, Microseconds now)
{
    if (due == armedFor) {
        return;
    }
    armedFor = due;
    if (!due) {
        CancelWaitableTimer(timer);
        return;
    }
    LARGE_INTEGER when{};
    when.QuadPart = -std::max<LONGLONG>(1, static_cast<LONGLONG>((*due - now).count()) * 10);
    if (!SetWaitableTimer(timer, &when, 0, nullptr, nullptr, FALSE)) {
        fail(describeError("could not arm the script timer"));
    }
}

void ScriptHost::Impl::fail(std::string what)
{
    const std::scoped_lock lock(mutex);
    if (problem.empty()) {
        problem = std::move(what);
    }
}

ScriptHost::ScriptHost(Sink sink, Limits limits, std::shared_ptr<ScriptServices> services)
    : m_impl(std::make_unique<Impl>(std::move(sink), limits, std::move(services)))
{
    if (m_impl->wake == nullptr || m_impl->timer == nullptr) {
        m_impl->fail(describeError("could not create the script thread's event or timer"));
        return;
    }
    const std::size_t reserve = std::min<std::size_t>(limits.stackBytes + kStackMargin, UINT_MAX);
    m_impl->thread = reinterpret_cast<HANDLE>(
        _beginthreadex(nullptr, static_cast<unsigned>(reserve), &Impl::entry, m_impl.get(),
                       STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr));
    if (m_impl->thread == nullptr) {
        m_impl->fail("could not start the script thread");
    }
}

ScriptHost::~ScriptHost()
{
    m_impl->stopRequested.store(true, std::memory_order_release);
    if (m_impl->wake != nullptr) {
        SetEvent(m_impl->wake);
    }
    if (m_impl->thread != nullptr) {
        WaitForSingleObject(m_impl->thread, INFINITE);
        CloseHandle(m_impl->thread);
        m_impl->thread = nullptr;
    }
}

bool ScriptHost::started() const noexcept
{
    return m_impl->thread != nullptr;
}

std::string ScriptHost::problem() const
{
    const std::scoped_lock lock(m_impl->mutex);
    return m_impl->problem;
}

ScriptId ScriptHost::load(ScriptSource source)
{
    const ScriptId id = m_impl->lastScript.fetch_add(1, std::memory_order_relaxed) + 1;
    m_impl->post(
        [id, source = std::move(source)](detail::Engine& engine) { engine.load(id, source); });
    return id;
}

void ScriptHost::unload(ScriptId script)
{
    m_impl->post([script](detail::Engine& engine) { engine.unload(script); });
}

TaskId ScriptHost::start(ScriptId script, std::string handler)
{
    const TaskId id = m_impl->lastTask.fetch_add(1, std::memory_order_relaxed) + 1;
    m_impl->post([id, script, handler = std::move(handler)](detail::Engine& engine) {
        engine.start(id, script, handler);
    });
    return id;
}

void ScriptHost::cancel(TaskId task)
{
    m_impl->post([task](detail::Engine& engine) { engine.cancel(task); });
}

void ScriptHost::frame()
{
    m_impl->post([](detail::Engine& engine) { engine.frame(); });
}

void ScriptHost::setFrameRate(double hz)
{
    const Microseconds interval{hz > 0.0 ? static_cast<Microseconds::rep>(1'000'000.0 / hz) : 0};
    m_impl->post([impl = m_impl.get(), interval](detail::Engine&) {
        impl->frameInterval = interval;
        impl->nextFrame.reset();
    });
}

void ScriptHost::controlEvent(std::string id, bool down)
{
    m_impl->post([id = std::move(id), down](detail::Engine& engine) { engine.controlEvent(id, down); });
}

void ScriptHost::stateEvent(std::string id, bool value)
{
    m_impl->post([id = std::move(id), value](detail::Engine& engine) { engine.stateEvent(id, value); });
}

void ScriptHost::event(std::string kind, std::string id, std::string json)
{
    m_impl->post([kind = std::move(kind), id = std::move(id), json = std::move(json)](detail::Engine& engine) {
        engine.event(kind, id, json);
    });
}

void ScriptHost::answer(std::uint64_t request, std::string json)
{
    m_impl->post([request, json = std::move(json)](detail::Engine& engine) { engine.answer(request, json); });
}

void ScriptHost::refuse(std::uint64_t request, std::string message)
{
    m_impl->post(
        [request, message = std::move(message)](detail::Engine& engine) { engine.refuse(request, message); });
}

void ScriptHost::setBreakpoints(std::vector<std::string> places)
{
    m_impl->post([places = std::move(places)](detail::Engine& engine) { engine.setBreakpoints(places); });
}

void ScriptHost::resume(std::uint64_t pause, StepMode mode)
{
    m_impl->post([pause, mode](detail::Engine& engine) { engine.resume(pause, mode); });
}

}
