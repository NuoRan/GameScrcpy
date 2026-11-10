#pragma once

#include "Api.h"
#include "LoadMeter.h"

#include "gs/script/Permissions.h"
#include "gs/script/Resources.h"
#include "gs/script/ScriptHost.h"
#include "gs/script/Services.h"

#include <gs/base/Clock.h>

#include <quickjs.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gs::script::detail {

struct MemoryAccount {
    std::size_t used = 0;
    std::size_t limit = 0;
    bool exceeded = false;
};

struct EngineOptions {
    std::shared_ptr<ScriptServices> services;
    const Clock* clock = nullptr;
    std::uint64_t seed = 0;
    std::optional<std::int64_t> wallEpochMs;
};

class Engine
{
public:
    Engine(const ScriptHost::Sink& sink, const Limits& limits, const std::atomic<bool>& stopRequested,
           std::atomic<TaskId>& taskIds, EngineOptions options = {});
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void load(ScriptId id, const ScriptSource& source);
    void unload(ScriptId id);
    void start(TaskId id, ScriptId script, const std::string& handler);
    void cancel(TaskId id);
    void frame();
    void fireTimers();
    void controlEvent(const std::string& id, bool down);
    void stateEvent(const std::string& id, bool value);
    void event(const std::string& kind, const std::string& id, const std::string& json);
    [[nodiscard]] std::optional<Microseconds> nextTimer();
    [[nodiscard]] bool waitingForFrame() const noexcept { return !m_frameWaits.empty(); }
    [[nodiscard]] Microseconds now() const noexcept;
    void shutdown();

    [[nodiscard]] ScriptServices* services() const noexcept { return m_services.get(); }
    [[nodiscard]] JSValue delay(JSContext* ctx, std::optional<Microseconds> after,
                                std::function<void()> done = {});
    [[nodiscard]] JSValue gesture(JSContext* ctx, Microseconds lasts, const std::function<void(std::uint64_t)>& begin,
                                  std::function<void(std::uint64_t)> cut);
    [[nodiscard]] JSValue request(JSContext* ctx, const std::function<void(std::uint64_t)>& ask,
                                  std::function<void(std::uint64_t)> withdrawn);
    void answer(std::uint64_t request, const std::string& json);
    void refuse(std::uint64_t request, const std::string& message);
    void setBreakpoints(const std::vector<std::string>& places);
    void resume(std::uint64_t pause, StepMode mode);
    [[nodiscard]] std::uint64_t nextDrawing() noexcept { return ++m_lastDrawing; }
    [[nodiscard]] std::uint64_t nextFinger() noexcept { return kFingerBase + ++m_lastFinger; }
    [[nodiscard]] JSValue drawn(JSContext* ctx, std::uint64_t id, double durationMs);
    [[nodiscard]] bool releaseBinding(JSContext* ctx, const std::string& control);
    [[nodiscard]] std::string ownerOf(JSContext* ctx) const;
    [[nodiscard]] const LoadedResources& resources(JSContext* ctx);
    [[nodiscard]] bool gated(JSContext* ctx) const;
    [[nodiscard]] std::optional<std::string> refusal(JSContext* ctx, const ServiceRule& rule) const;
    [[nodiscard]] api::Stores& stores() noexcept { return m_stores; }
    [[nodiscard]] double randomNumber();
    [[nodiscard]] double wallMilliseconds() const;
    [[nodiscard]] TaskId currentTask() const noexcept { return m_owner.task; }
    [[nodiscard]] JSValue spawn(JSContext* ctx, JSValueConst function, const std::string& label, JSValueConst extra);
    void cancelFromScript(JSContext* ctx, TaskId id);

    static Engine& of(JSContext* ctx) noexcept;
    static JSValue jsSleep(JSContext* ctx, JSValueConst self, int argc,
                           JSValueConst* argv) noexcept;
    static JSValue jsNextFrame(JSContext* ctx, JSValueConst self, int argc,
                               JSValueConst* argv) noexcept;
    static JSValue jsNow(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) noexcept;
    static JSValue jsLog(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv,
                         int level) noexcept;
    static JSValue jsDebug(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv,
                           int which) noexcept;

private:
    struct Owner {
        ScriptId script = 0;
        TaskId task = 0;

        friend bool operator==(const Owner&, const Owner&) = default;
    };

    struct Wait {
        Owner owner;
        JSContext* context = nullptr;
        JSValue resolve = JS_UNDEFINED;
        JSValue reject = JS_UNDEFINED;
        std::function<void()> done;
        std::function<void()> cut;
    };

    struct Task {
        TaskId id = 0;
        ScriptId script = 0;
        std::string handler;
        JSValue signal = JS_UNDEFINED;
        JSValue reason = JS_UNDEFINED;
        std::size_t waits = 0;
        bool aborted = false;
        bool ended = false;
    };

    struct PendingStart {
        TaskId id = 0;
        std::string handler;
    };

    enum class ScriptState : std::uint8_t { Loading, Ready, Failed };

    struct Script {
        ScriptId id = 0;
        std::filesystem::path folder;
        std::string entry;
        JSContext* context = nullptr;
        JSValue exports = JS_UNDEFINED;
        JSValue createTask = JS_UNDEFINED;
        JSValue abortSignal = JS_UNDEFINED;
        JSValue cancelled = JS_UNDEFINED;
        JSValue dispatch = JS_UNDEFINED;
        JSValue ended = JS_UNDEFINED;
        JSValue drawn = JS_UNDEFINED;
        JSValue release = JS_UNDEFINED;
        JSValue startTask = JS_UNDEFINED;
        JSValue mount = JS_UNDEFINED;
        std::vector<ComponentModule> components;
        std::vector<std::string> modules;
        SourceTransform transform;
        std::shared_ptr<const ScriptFiles> files;
        std::optional<std::vector<std::string>> permissions;
        bool phone = true;
        std::optional<LoadedResources> memoryResources;
        ScriptState state = ScriptState::Loading;
        std::vector<PendingStart> pending;
        std::unordered_set<std::string> spiked;
        bool unloading = false;
    };

    struct Rejection {
        Owner owner;
        JSContext* context = nullptr;
        JSValue promise = JS_UNDEFINED;
        JSValue reason = JS_UNDEFINED;
    };

    struct ErrorInfo {
        std::string message;
        SourceLocation location;
        std::string stack;
        bool abort = false;
    };

    struct Slice {
        bool active = false;
        bool tripped = false;
        bool interrupted = false;
        Microseconds begin{0};
        Microseconds budgetStart{0};
        Microseconds overrun{0};
    };

    struct DebugTask {
        std::vector<std::string> frames;
        StepMode mode = StepMode::Continue;
        std::size_t depth = 0;
    };

    struct DebugHit {
        TaskId task = 0;
        std::string file;
        std::string position;
        std::string values;
        std::string label;
        double at = 0.0;
    };

    using Timer = std::pair<Microseconds, std::uint64_t>;

    [[nodiscard]] bool ensureRuntime();
    void freeRuntime();
    void stopRuntime();
    [[nodiscard]] JSContext* newContext();
    [[nodiscard]] bool prepare(Script& script);
    [[nodiscard]] bool prepareDebug(JSContext* ctx);
    [[nodiscard]] bool debugStops(const std::string& file, const std::string& position, const std::string& values);
    [[nodiscard]] JSValue debugPause(JSContext* ctx, const std::string& file, const std::string& position,
                                     const std::string& values);
    void liftGestures(TaskId task);
    void flushHits(bool now);
    void evaluate(Script& script);
    [[nodiscard]] bool mountComponents(Script& script);
    bool watch(JSContext* ctx, JSValueConst promise, JSCFunctionData* callback, std::int64_t id);

    template <typename Body>
    void slice(Owner owner, Body&& body);
    void beginSlice(Owner owner);
    void endSlice();
    void drainJobs();
    void excuse(Microseconds spent) noexcept;
    void acknowledgeInterrupt();
    [[nodiscard]] bool interruptNow() noexcept;
    [[nodiscard]] bool halted() const noexcept;

    void run(Script& script, TaskId id, const std::string& handler);
    void invoke(Script& script, TaskId id, JSValueConst function, JSValueConst extra);
    void deliver(std::string_view kind, const std::string& id, const std::string& json);
    void taskEnded(ScriptId script, TaskId id);
    void startPending(ScriptId id);
    void abortTask(TaskId id);
    void endTask(TaskId id, ReportKind kind, const ErrorInfo* error,
                 Microseconds ran = Microseconds{0});
    void releaseTaskIfDone(TaskId id);
    void taskSettled(JSContext* ctx, TaskId id, bool rejected, JSValueConst value);
    void loadSettled(JSContext* ctx, ScriptId id, bool rejected, JSValueConst value);
    void failLoad(ScriptId id, ErrorInfo error, Microseconds ran = Microseconds{0});
    void uncaught(JSContext* ctx, JSValue error);
    void blame(ErrorInfo error, bool interrupted, Microseconds ran);

    [[nodiscard]] JSValue wait(JSContext* ctx, std::optional<Microseconds> delay, bool host = false);
    void settle(Wait& wait, bool rejected, JSValueConst value);
    void rejectWaits(Owner owner, JSValueConst reason);
    [[nodiscard]] bool hasWaits(Owner owner) const;
    [[nodiscard]] std::vector<TaskId> tasksOf(ScriptId script, bool runningOnly) const;
    void releaseScript(ScriptId id);
    void retire(JSContext* ctx);
    void collectRetired();
    void reportRejections();
    void checkMemory();
    void account(Microseconds end, Microseconds duration, bool interrupted);
    [[nodiscard]] bool claimSpike(Owner owner);
    [[nodiscard]] ErrorInfo describeError(JSContext* ctx, JSValueConst value);
    [[nodiscard]] Script* findScript(ScriptId id);
    [[nodiscard]] Task* findTask(TaskId id);
    [[nodiscard]] Report reportFor(ReportKind kind, log::Level level, Owner owner) const;
    void emit(const Report& report) noexcept;
    void flushDeferred();
    [[nodiscard]] double millisecondsNow() const noexcept;

    static int jsInterrupt(JSRuntime* runtime, void* opaque) noexcept;
    static void jsRejection(JSContext* ctx, JSValueConst promise, JSValueConst reason, bool handled,
                            void* opaque) noexcept;
    static char* jsNormalize(JSContext* ctx, const char* base, const char* name,
                             void* opaque) noexcept;
    static JSModuleDef* jsLoadModule(JSContext* ctx, const char* name, void* opaque,
                                     JSValueConst attributes) noexcept;
    static int jsCheckAttributes(JSContext* ctx, void* opaque, JSValueConst attributes) noexcept;
    static JSValue jsTaskSettled(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv,
                                 int rejected, JSValueConst* data) noexcept;
    static JSValue jsLoadSettled(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv,
                                 int rejected, JSValueConst* data) noexcept;
    static JSValue jsMountSettled(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv,
                                  int rejected, JSValueConst* data) noexcept;

    const ScriptHost::Sink& m_sink;
    const Limits m_limits;
    const std::atomic<bool>& m_stopRequested;
    std::atomic<TaskId>& m_taskIds;
    std::shared_ptr<ScriptServices> m_services;
    api::Stores m_stores;
    std::map<std::filesystem::path, ResourceFile> m_resources;
    SteadyClock m_clock;
    const Clock* m_time;
    const Microseconds m_epoch;
    std::optional<std::int64_t> m_wallEpochMs;
    std::mt19937_64 m_random;
    LoadMeter m_meter;
    MemoryAccount m_memory;
    JSRuntime* m_runtime = nullptr;
    std::unordered_map<ScriptId, Script> m_scripts;
    std::unordered_map<TaskId, Task> m_tasks;
    std::unordered_map<std::uint64_t, Wait> m_waits;
    std::uint64_t m_lastDrawing = 0;
    std::uint64_t m_lastFinger = 0;
    static constexpr std::uint64_t kFingerBase = std::uint64_t{1} << 48;
    std::priority_queue<Timer, std::vector<Timer>, std::greater<>> m_timers;
    std::vector<std::uint64_t> m_frameWaits;
    std::vector<Rejection> m_rejections;
    std::vector<JSContext*> m_retired;
    std::deque<std::function<void()>> m_deferred;
    std::string m_importFailure;
    Owner m_owner;
    std::string m_ownerHandler;
    Slice m_slice;
    std::uint64_t m_lastWait = 0;
    std::set<std::string> m_breakpoints;
    std::unordered_map<TaskId, DebugTask> m_debugTasks;
    std::unordered_map<std::uint64_t, TaskId> m_pauses;
    std::vector<DebugHit> m_hits;
    Microseconds m_hitsFlushed{0};
    Microseconds m_watchdogRan{0};
    bool m_watchdogPending = false;
    bool m_memoryWarned = false;
    bool m_cleaningUp = false;
};

}
