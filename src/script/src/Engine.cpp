#include "Engine.h"

#include "ModulePath.h"
#include "Prelude.h"
#include "StackTrace.h"

#include <gs/tsstrip/Strip.h>

#include <malloc.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

namespace gs::script::detail {

namespace {

constexpr double kLongestSleepMs = 2147483647.0;
constexpr std::uintmax_t kLargestModule = std::uintmax_t{8} << 20;
constexpr std::size_t kUnwindHeadroom = std::size_t{1} << 20;
constexpr std::size_t kMostRejections = 256;
constexpr int kGlobalFlags = JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE;
constexpr std::string_view kComponentPrefix = "@component/";
constexpr std::string_view kMountModule = "<components>";
constexpr std::string_view kComponentsOnlyModule = "<profile>";
constexpr std::string_view kPartsModule = "<parts>";
constexpr std::size_t kMostHits = 1000;
constexpr std::size_t kMostHitValues = 4096;
constexpr Microseconds kHitsGap{50'000};
constexpr std::string_view kDebugSource = R"js((() => {
    const host = globalThis.__gsHost;
    delete globalThis.__gsHost;
    const text = (value) => {
        let shown;
        try {
            shown = typeof value === "function" ? "function" : (JSON.stringify(value) ?? String(value));
        } catch {
            shown = String(value);
        }
        return shown.length > 80 ? shown.slice(0, 79) + "\u2026" : shown;
    };
    let held = "{}";
    Object.defineProperty(globalThis, "__gsFor", {
        value: (file) => Object.freeze({
            probe(position, locals) {
                const values = {};
                for (const [name, value] of Object.entries(locals ?? {})) {
                    values[name] = text(value);
                }
                held = JSON.stringify(values);
                return host.probe(file, position, held);
            },
            pause(position) {
                const values = held;
                held = "{}";
                return host.pause(file, position, values);
            },
            enter(position) {
                return host.enter(file, position);
            },
            exit(depth) {
                host.exit(depth);
            },
        }),
    });
})();
)js";

std::string quotedJson(std::string_view text)
{
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += std::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(c)));
            } else {
                out += c;
            }
        }
    }
    out += '"';
    return out;
}

struct ComponentPart {
    std::string ref;
    std::filesystem::path folder;
    std::string path;
};

bool plainName(std::string_view name)
{
    return !name.empty() && std::ranges::none_of(name, [](char c) {
        return c == '"' || c == '\\' || static_cast<unsigned char>(c) < 0x20;
    });
}

std::optional<ComponentPart> componentPart(const std::vector<ComponentModule>& components, std::string_view name)
{
    if (!name.starts_with(kComponentPrefix)) {
        return std::nullopt;
    }
    name.remove_prefix(kComponentPrefix.size());
    const std::size_t shelf = name.find('/');
    const std::size_t folder = shelf == std::string_view::npos ? shelf : name.find('/', shelf + 1);
    if (folder == std::string_view::npos || folder + 1 >= name.size()) {
        return std::nullopt;
    }
    const std::string_view ref = name.substr(0, folder);
    const auto found = std::ranges::find(components, ref, &ComponentModule::ref);
    if (found == components.end()) {
        return std::nullopt;
    }
    return ComponentPart{std::string(ref), found->folder, std::string(name.substr(folder + 1))};
}

std::uint64_t randomSeed()
{
    std::random_device device;
    return (static_cast<std::uint64_t>(device()) << 32) ^ device();
}

std::string partsSource(const std::string& entry, bool withEntry, const std::vector<std::string>& modules)
{
    std::string source;
    if (withEntry) {
        source += std::format("import \"./{}\";\n", entry);
    }
    for (const std::string& module : modules) {
        source += std::format("import \"./{}\";\n", module);
    }
    if (withEntry) {
        source += std::format("export * from \"./{}\";\n", entry);
    }
    return source.empty() ? std::string("export {};") : source;
}

std::string problemText(const tsstrip::Result& stripped)
{
    const tsstrip::Problem& first = stripped.problems.front();
    std::string text = std::format("{} {}", tsstrip::codeName(first.code), first.message);
    if (stripped.problems.size() > 1) {
        text += std::format(" ({} more in this file)", stripped.problems.size() - 1);
    }
    return text;
}

void throwStripProblem(JSContext* ctx, std::string_view module, const tsstrip::Result& stripped)
{
    const tsstrip::Problem& first = stripped.problems.front();
    JS_ThrowSyntaxError(ctx, "%s", problemText(stripped).c_str());
    JSValue error = JS_GetException(ctx);
    const std::string stack = std::format("    at {}:{}:{}\n", module, first.line, first.column);
    JS_DefinePropertyValueStr(ctx, error, "stack", JS_NewStringLen(ctx, stack.data(), stack.size()), kGlobalFlags);
    JS_Throw(ctx, error);
}

MemoryAccount& accountOf(void* opaque) noexcept
{
    return *static_cast<MemoryAccount*>(opaque);
}

bool admit(MemoryAccount& account, std::size_t size) noexcept
{
    if (account.limit == 0) {
        return true;
    }
    const std::size_t cap = account.exceeded ? account.limit + kUnwindHeadroom : account.limit;
    if (size > cap || account.used > cap - size) {
        account.exceeded = true;
        return false;
    }
    return true;
}

void* accountMalloc(void* opaque, std::size_t size) noexcept
{
    MemoryAccount& account = accountOf(opaque);
    if (!admit(account, size)) {
        return nullptr;
    }
    void* block = std::malloc(size);
    if (block != nullptr) {
        account.used += _msize(block);
    }
    return block;
}

void* accountCalloc(void* opaque, std::size_t count, std::size_t size) noexcept
{
    MemoryAccount& account = accountOf(opaque);
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
        return nullptr;
    }
    if (!admit(account, count * size)) {
        return nullptr;
    }
    void* block = std::calloc(count, size);
    if (block != nullptr) {
        account.used += _msize(block);
    }
    return block;
}

void accountFree(void* opaque, void* block) noexcept
{
    if (block == nullptr) {
        return;
    }
    MemoryAccount& account = accountOf(opaque);
    account.used -= std::min(account.used, _msize(block));
    std::free(block);
}

void* accountRealloc(void* opaque, void* block, std::size_t size) noexcept
{
    if (block == nullptr) {
        return accountMalloc(opaque, size);
    }
    if (size == 0) {
        accountFree(opaque, block);
        return nullptr;
    }
    MemoryAccount& account = accountOf(opaque);
    const std::size_t before = _msize(block);
    if (size > before && !admit(account, size - before)) {
        return nullptr;
    }
    void* moved = std::realloc(block, size);
    if (moved == nullptr) {
        return nullptr;
    }
    account.used = account.used - std::min(account.used, before) + _msize(moved);
    return moved;
}

std::size_t accountUsableSize(const void* block) noexcept
{
    return block == nullptr ? 0 : _msize(const_cast<void*>(block));
}

const JSMallocFunctions kAccountedAllocator = {
    .js_calloc = &accountCalloc,
    .js_malloc = &accountMalloc,
    .js_free = &accountFree,
    .js_realloc = &accountRealloc,
    .js_malloc_usable_size = &accountUsableSize,
};

class JsText
{
public:
    JsText(JSContext* ctx, JSValueConst value) noexcept
        : m_context(ctx)
        , m_text(JS_ToCStringLen(ctx, &m_size, value))
    {}

    ~JsText()
    {
        if (m_text != nullptr) {
            JS_FreeCString(m_context, m_text);
        }
    }

    JsText(const JsText&) = delete;
    JsText& operator=(const JsText&) = delete;

    [[nodiscard]] bool valid() const noexcept { return m_text != nullptr; }
    [[nodiscard]] std::string_view view() const noexcept { return {m_text, m_size}; }

private:
    JSContext* m_context;
    std::size_t m_size = 0;
    const char* m_text;
};

std::string textOf(JSContext* ctx, JSValueConst value)
{
    const JsText text(ctx, value);
    if (!text.valid()) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return {};
    }
    return std::string(text.view());
}

std::string propertyText(JSContext* ctx, JSValueConst object, const char* name)
{
    JSValue value = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(value)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return {};
    }
    std::string text = JS_IsString(value) ? textOf(ctx, value) : std::string{};
    JS_FreeValue(ctx, value);
    return text;
}

char lowerAscii(char c) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

const std::string* memoryFile(const ScriptFiles& files, std::string_view name)
{
    if (const auto found = files.find(name); found != files.end()) {
        return &found->second;
    }
    for (const auto& [path, contents] : files) {
        if (path.size() == name.size() &&
            std::ranges::equal(path, name, [](char a, char b) { return lowerAscii(a) == lowerAscii(b); })) {
            return &contents;
        }
    }
    return nullptr;
}

bool readMemoryModule(const ScriptFiles& files, std::string_view name, std::string& source)
{
    const std::string* text = memoryFile(files, name);
    if (text == nullptr || text->size() > kLargestModule) {
        return false;
    }
    source = text->starts_with("\xEF\xBB\xBF") ? text->substr(3) : *text;
    return true;
}

std::optional<std::string> missingModule(const ScriptFiles* files, const std::filesystem::path& folder,
                                         std::string_view key, std::string_view path)
{
    if (files != nullptr) {
        return memoryFile(*files, key) != nullptr ? std::nullopt : std::optional<std::string>("cannot be found");
    }
    const auto file = moduleFile(folder, path);
    return file ? std::nullopt : std::optional<std::string>(file.error());
}

bool readModule(const std::filesystem::path& file, std::string& source) noexcept
{
    try {
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(file, error);
        if (error || size > kLargestModule) {
            return false;
        }
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            return false;
        }
        source.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (source.starts_with("\xEF\xBB\xBF")) {
            source.erase(0, 3);
        }
        return true;
    } catch (...) {
        return false;
    }
}

int initApiModule(JSContext* ctx, JSModuleDef* module) noexcept
{
    JSValue global = JS_GetGlobalObject(ctx);
    int result = 0;
    try {
        for (const std::string_view name : api::moduleExports()) {
            const std::string key(name);
            JSValue value = JS_GetPropertyStr(ctx, global, key.c_str());
            if (JS_IsException(value) || JS_SetModuleExport(ctx, module, key.c_str(), value) < 0) {
                result = -1;
                break;
            }
        }
    } catch (...) {
        JS_ThrowOutOfMemory(ctx);
        result = -1;
    }
    JS_FreeValue(ctx, global);
    return result;
}

JSModuleDef* apiModule(JSContext* ctx)
{
    JSModuleDef* module = JS_NewCModule(ctx, std::string(api::kModuleName).c_str(), &initApiModule);
    if (module == nullptr) {
        return nullptr;
    }
    for (const std::string_view name : api::moduleExports()) {
        if (JS_AddModuleExport(ctx, module, std::string(name).c_str()) < 0) {
            return nullptr;
        }
    }
    return module;
}

}

Engine::Engine(const ScriptHost::Sink& sink, const Limits& limits, const std::atomic<bool>& stopRequested,
               std::atomic<TaskId>& taskIds, EngineOptions options)
    : m_sink(sink)
    , m_limits(limits)
    , m_stopRequested(stopRequested)
    , m_taskIds(taskIds)
    , m_services(std::move(options.services))
    , m_time(options.clock != nullptr ? options.clock : &m_clock)
    , m_epoch(m_time->now())
    , m_wallEpochMs(options.wallEpochMs)
    , m_random(options.seed != 0 ? options.seed : randomSeed())
    , m_meter(limits)
{}

Engine::~Engine()
{
    shutdown();
}

template <typename Body>
void Engine::slice(Owner owner, Body&& body)
{
    beginSlice(owner);
    body();
    acknowledgeInterrupt();
    drainJobs();
    endSlice();
}

Microseconds Engine::now() const noexcept
{
    return m_time->now();
}

double Engine::millisecondsNow() const noexcept
{
    return static_cast<double>((m_time->now() - m_epoch).count()) / 1000.0;
}

double Engine::randomNumber()
{
    return static_cast<double>(m_random() >> 11) * 0x1.0p-53;
}

double Engine::wallMilliseconds() const
{
    if (m_wallEpochMs) {
        return std::floor(static_cast<double>(*m_wallEpochMs) + millisecondsNow());
    }
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
}

void Engine::load(ScriptId id, const ScriptSource& source)
{
    Report failure = reportFor(ReportKind::ScriptFailed, log::Level::Error, Owner{id, 0});
    failure.location.module = source.entry;
    if (auto problem = moduleNameProblem(source.entry)) {
        failure.message = std::format("The entry module '{}' {}", source.entry, *problem);
        emit(failure);
        return;
    }
    for (const std::string& module : source.modules) {
        if (auto problem = moduleNameProblem(module)) {
            failure.message = std::format("The module '{}' {}", module, *problem);
            failure.location.module = module;
            emit(failure);
            return;
        }
    }
    if (!ensureRuntime()) {
        failure.message = "The script runtime could not be created";
        emit(failure);
        return;
    }
    Script& script = m_scripts[id];
    script.id = id;
    script.folder = source.folder;
    script.entry = source.entry;
    script.modules = source.modules;
    script.components = source.components;
    script.transform = source.transform;
    script.files = source.files;
    script.permissions = source.permissions;
    script.phone = source.phone;
    slice(Owner{id, 0}, [this, &script] { evaluate(script); });
    flushDeferred();
}

void Engine::unload(ScriptId id)
{
    Script* script = findScript(id);
    if (script == nullptr) {
        return;
    }
    if (!script->unloading) {
        script->unloading = true;
        if (script->state == ScriptState::Loading) {
            script->state = ScriptState::Failed;
            Report report = reportFor(ReportKind::ScriptFailed, log::Level::Error, Owner{id, 0});
            report.message = "The script was unloaded before it finished loading";
            report.location.module = script->entry;
            emit(report);
        }
        for (const TaskId task : tasksOf(id, false)) {
            if (m_runtime == nullptr || halted()) {
                break;
            }
            slice(Owner{id, task}, [this, task] { abortTask(task); });
        }
        if (m_runtime != nullptr && !halted() && hasWaits(Owner{id, 0})) {
            slice(Owner{id, 0}, [this, id] {
                Script* current = findScript(id);
                if (current == nullptr) {
                    return;
                }
                JSValue reason =
                    JS_Call(current->context, current->cancelled, JS_UNDEFINED, 0, nullptr);
                if (JS_IsException(reason)) {
                    uncaught(current->context, JS_GetException(current->context));
                    return;
                }
                rejectWaits(Owner{id, 0}, reason);
                JS_FreeValue(current->context, reason);
            });
        }
    }
    script = findScript(id);
    if (script == nullptr) {
        return;
    }
    for (const TaskId task : tasksOf(id, true)) {
        endTask(task, ReportKind::TaskCancelled, nullptr);
    }
    script = findScript(id);
    if (script == nullptr) {
        return;
    }
    for (const PendingStart& pending : script->pending) {
        Report report =
            reportFor(ReportKind::TaskCancelled, log::Level::Info, Owner{id, pending.id});
        report.handler = pending.handler;
        emit(report);
    }
    script->pending.clear();
    releaseScript(id);
}

void Engine::start(TaskId id, ScriptId scriptId, const std::string& handler)
{
    Script* script = findScript(scriptId);
    if (script == nullptr || script->unloading || script->state == ScriptState::Failed) {
        Report report = reportFor(ReportKind::TaskFailed, log::Level::Error, Owner{scriptId, id});
        report.handler = handler;
        report.message = std::format("Script {} is not loaded", scriptId);
        emit(report);
        return;
    }
    if (script->state == ScriptState::Loading) {
        script->pending.push_back(PendingStart{.id = id, .handler = handler});
        return;
    }
    run(*script, id, handler);
    flushDeferred();
}

void Engine::cancel(TaskId id)
{
    if (Task* task = findTask(id)) {
        if (!task->aborted && m_runtime != nullptr && !halted()) {
            const Owner owner{task->script, id};
            slice(owner, [this, id] { abortTask(id); });
        }
        endTask(id, ReportKind::TaskCancelled, nullptr);
        flushDeferred();
        return;
    }
    for (auto& [scriptId, script] : m_scripts) {
        const auto found = std::ranges::find(script.pending, id, &PendingStart::id);
        if (found != script.pending.end()) {
            Report report =
                reportFor(ReportKind::TaskCancelled, log::Level::Info, Owner{scriptId, id});
            report.handler = found->handler;
            script.pending.erase(found);
            emit(report);
            return;
        }
    }
}

void Engine::frame()
{
    if (m_runtime == nullptr) {
        return;
    }
    flushHits(true);
    std::vector<std::uint64_t> ids;
    ids.swap(m_frameWaits);
    const double at = millisecondsNow();
    for (const std::uint64_t id : ids) {
        auto node = m_waits.extract(id);
        if (node.empty()) {
            continue;
        }
        Wait wait = std::move(node.mapped());
        slice(wait.owner,
              [this, &wait, at] { settle(wait, false, JS_NewFloat64(wait.context, at)); });
        if (m_runtime == nullptr) {
            break;
        }
    }
    flushDeferred();
}

void Engine::fireTimers()
{
    if (m_runtime == nullptr) {
        return;
    }
    const Microseconds now = m_time->now();
    std::vector<std::uint64_t> due;
    while (!m_timers.empty() && m_timers.top().first <= now) {
        due.push_back(m_timers.top().second);
        m_timers.pop();
    }
    for (const std::uint64_t id : due) {
        auto node = m_waits.extract(id);
        if (node.empty()) {
            continue;
        }
        Wait wait = std::move(node.mapped());
        slice(wait.owner, [this, &wait] { settle(wait, false, JS_UNDEFINED); });
        if (m_runtime == nullptr) {
            break;
        }
    }
    flushDeferred();
}

std::optional<Microseconds> Engine::nextTimer()
{
    while (!m_timers.empty() && !m_waits.contains(m_timers.top().second)) {
        m_timers.pop();
    }
    if (m_timers.empty()) {
        return std::nullopt;
    }
    return m_timers.top().first;
}

void Engine::shutdown()
{
    m_cleaningUp = true;
    std::vector<ScriptId> ids;
    ids.reserve(m_scripts.size());
    for (const auto& [id, script] : m_scripts) {
        ids.push_back(id);
    }
    std::ranges::sort(ids);
    for (const ScriptId id : ids) {
        unload(id);
    }
    flushDeferred();
    freeRuntime();
}

bool Engine::ensureRuntime()
{
    if (m_runtime != nullptr) {
        return true;
    }
    m_memory = MemoryAccount{.limit = m_limits.memoryLimitBytes};
    m_memoryWarned = false;
    m_runtime = JS_NewRuntime2(&kAccountedAllocator, &m_memory);
    if (m_runtime == nullptr) {
        return false;
    }
    JS_SetRuntimeOpaque(m_runtime, this);
    JS_SetMaxStackSize(m_runtime, m_limits.stackBytes);
    JS_SetCanBlock(m_runtime, false);
    JS_SetInterruptHandler(m_runtime, &Engine::jsInterrupt, this);
    JS_SetHostPromiseRejectionTracker(m_runtime, &Engine::jsRejection, this);
    JS_SetModuleLoaderFunc2(m_runtime, &Engine::jsNormalize, &Engine::jsLoadModule,
                            &Engine::jsCheckAttributes, this);
    return true;
}

void Engine::freeRuntime()
{
    if (m_runtime == nullptr) {
        return;
    }
    for (auto& [id, wait] : m_waits) {
        for (const std::function<void()>* callback : {&wait.cut, &wait.done}) {
            if (*callback) {
                try {
                    (*callback)();
                } catch (...) {
                }
            }
        }
        JS_FreeValueRT(m_runtime, wait.resolve);
        JS_FreeValueRT(m_runtime, wait.reject);
    }
    m_waits.clear();
    m_stores.clear();
    m_timers = {};
    m_frameWaits.clear();
    for (Rejection& rejection : m_rejections) {
        JS_FreeValueRT(m_runtime, rejection.promise);
        JS_FreeValueRT(m_runtime, rejection.reason);
    }
    m_rejections.clear();
    for (auto& [id, task] : m_tasks) {
        JS_FreeValueRT(m_runtime, task.signal);
        JS_FreeValueRT(m_runtime, task.reason);
    }
    m_tasks.clear();
    for (auto& [id, script] : m_scripts) {
        JS_FreeValueRT(m_runtime, script.exports);
        JS_FreeValueRT(m_runtime, script.createTask);
        JS_FreeValueRT(m_runtime, script.abortSignal);
        JS_FreeValueRT(m_runtime, script.cancelled);
        JS_FreeValueRT(m_runtime, script.dispatch);
        JS_FreeValueRT(m_runtime, script.ended);
        JS_FreeValueRT(m_runtime, script.drawn);
        JS_FreeValueRT(m_runtime, script.release);
        JS_FreeValueRT(m_runtime, script.startTask);
        JS_FreeValueRT(m_runtime, script.mount);
        if (script.context != nullptr) {
            JS_SetContextOpaque(script.context, nullptr);
            JS_FreeContext(script.context);
        }
    }
    m_scripts.clear();
    for (JSContext* ctx : m_retired) {
        JS_FreeContext(ctx);
    }
    m_retired.clear();
    m_deferred.clear();
    JS_FreeRuntime(m_runtime);
    m_runtime = nullptr;
    m_memory = MemoryAccount{.limit = m_limits.memoryLimitBytes};
    m_memoryWarned = false;
}

void Engine::stopRuntime()
{
    Report report = reportFor(ReportKind::MemoryLimit, log::Level::Error, m_owner);
    report.bytes = m_memory.used;
    report.byteLimit = m_memory.limit;
    emit(report);
    std::vector<TaskId> running;
    for (const auto& [id, task] : m_tasks) {
        if (!task.ended) {
            running.push_back(id);
        }
    }
    std::ranges::sort(running);
    for (const TaskId id : running) {
        Task& task = m_tasks.at(id);
        task.ended = true;
        emit(reportFor(ReportKind::TaskCancelled, log::Level::Info, Owner{task.script, id}));
    }
    std::vector<ScriptId> scripts;
    for (const auto& [id, script] : m_scripts) {
        scripts.push_back(id);
    }
    std::ranges::sort(scripts);
    for (const ScriptId id : scripts) {
        Script& script = m_scripts.at(id);
        for (const PendingStart& pending : script.pending) {
            Report cancelled =
                reportFor(ReportKind::TaskCancelled, log::Level::Info, Owner{id, pending.id});
            cancelled.handler = pending.handler;
            emit(cancelled);
        }
        if (script.state == ScriptState::Loading) {
            Report failed = reportFor(ReportKind::ScriptFailed, log::Level::Error, Owner{id, 0});
            failed.message = "The scripts were stopped at the memory limit";
            failed.location.module = script.entry;
            emit(failed);
        }
    }
    freeRuntime();
}

JSContext* Engine::newContext()
{
    JSContext* ctx = JS_NewContextRaw(m_runtime);
    if (ctx == nullptr) {
        return nullptr;
    }
    const bool ready = JS_AddIntrinsicBaseObjects(ctx) == 0 && JS_AddIntrinsicDate(ctx) == 0 &&
                       JS_AddIntrinsicEval(ctx) == 0 && JS_AddIntrinsicRegExp(ctx) == 0 &&
                       JS_AddIntrinsicJSON(ctx) == 0 && JS_AddIntrinsicProxy(ctx) == 0 &&
                       JS_AddIntrinsicMapSet(ctx) == 0 && JS_AddIntrinsicTypedArrays(ctx) == 0 &&
                       JS_AddIntrinsicPromise(ctx) == 0 && JS_AddIntrinsicWeakRef(ctx) == 0 &&
                       JS_AddIntrinsicDOMException(ctx) == 0;
    if (!ready) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        JS_FreeContext(ctx);
        return nullptr;
    }
    return ctx;
}

bool Engine::prepare(Script& script)
{
    JSContext* ctx = script.context;
    const std::string& source = prelude();
    JSValue factory = JS_Eval(ctx, source.data(), source.size(), kPreludeName.data(),
                              JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_STRICT);
    if (JS_IsException(factory)) {
        return false;
    }
    JSValue host = api::host(ctx);
    if (JS_IsException(host)) {
        JS_FreeValue(ctx, factory);
        return false;
    }
    JSValue hooks = JS_Call(ctx, factory, JS_UNDEFINED, 1, &host);
    JS_FreeValue(ctx, host);
    JS_FreeValue(ctx, factory);
    if (JS_IsException(hooks)) {
        return false;
    }
    script.createTask = JS_GetPropertyStr(ctx, hooks, "task");
    script.abortSignal = JS_GetPropertyStr(ctx, hooks, "abort");
    script.cancelled = JS_GetPropertyStr(ctx, hooks, "cancelled");
    script.dispatch = JS_GetPropertyStr(ctx, hooks, "dispatch");
    script.ended = JS_GetPropertyStr(ctx, hooks, "ended");
    script.drawn = JS_GetPropertyStr(ctx, hooks, "drawn");
    script.release = JS_GetPropertyStr(ctx, hooks, "release");
    script.startTask = JS_GetPropertyStr(ctx, hooks, "startTask");
    script.mount = JS_GetPropertyStr(ctx, hooks, "mount");
    JSValue action = JS_GetPropertyStr(ctx, hooks, "action");
    JSValue library = JS_GetPropertyStr(ctx, hooks, "library");
    JS_FreeValue(ctx, hooks);
    if (!JS_IsFunction(ctx, script.createTask) || !JS_IsFunction(ctx, script.abortSignal) ||
        !JS_IsFunction(ctx, script.cancelled) || !JS_IsFunction(ctx, script.dispatch) ||
        !JS_IsFunction(ctx, script.ended) || !JS_IsFunction(ctx, script.drawn) || !JS_IsFunction(ctx, script.release) ||
        !JS_IsFunction(ctx, script.startTask) || !JS_IsFunction(ctx, script.mount) ||
        !JS_IsFunction(ctx, action) || !JS_IsObject(library)) {
        JS_FreeValue(ctx, action);
        JS_FreeValue(ctx, library);
        return false;
    }
    JSValue global = JS_GetGlobalObject(ctx);
    const bool ready = api::install(ctx, global, action, library);
    JS_FreeValue(ctx, action);
    JS_FreeValue(ctx, library);
    JS_FreeValue(ctx, global);
    return ready && (!script.transform || prepareDebug(ctx));
}

JSValue Engine::delay(JSContext* ctx, std::optional<Microseconds> after, std::function<void()> done)
{
    const std::uint64_t before = m_lastWait;
    JSValue promise = wait(ctx, after);
    if (done) {
        if (m_lastWait != before) {
            if (const auto found = m_waits.find(m_lastWait); found != m_waits.end()) {
                found->second.done = std::move(done);
                return promise;
            }
        }
        done();
    }
    return promise;
}

JSValue Engine::gesture(JSContext* ctx, Microseconds lasts, const std::function<void(std::uint64_t)>& begin,
                        std::function<void(std::uint64_t)> cut)
{
    const std::uint64_t before = m_lastWait;
    JSValue promise = wait(ctx, lasts);
    if (m_lastWait == before || JS_IsException(promise)) {
        return promise;
    }
    const std::uint64_t id = m_lastWait;
    const auto found = m_waits.find(id);
    if (found == m_waits.end()) {
        return promise;
    }
    found->second.cut = [cut = std::move(cut), id] {
        if (cut) {
            cut(id);
        }
    };
    begin(id);
    return promise;
}

JSValue Engine::request(JSContext* ctx, const std::function<void(std::uint64_t)>& ask,
                        std::function<void(std::uint64_t)> withdrawn)
{
    const std::uint64_t before = m_lastWait;
    JSValue promise = wait(ctx, std::nullopt, true);
    if (m_lastWait == before || JS_IsException(promise)) {
        return promise;
    }
    const std::uint64_t id = m_lastWait;
    const auto found = m_waits.find(id);
    if (found == m_waits.end()) {
        return promise;
    }
    found->second.done = [withdrawn = std::move(withdrawn), id] {
        if (withdrawn) {
            withdrawn(id);
        }
    };
    ask(id);
    return promise;
}

void Engine::setBreakpoints(const std::vector<std::string>& places)
{
    m_breakpoints = std::set<std::string>(places.begin(), places.end());
}

void Engine::resume(std::uint64_t pause, StepMode mode)
{
    const auto found = m_pauses.find(pause);
    if (found == m_pauses.end()) {
        return;
    }
    const TaskId task = found->second;
    m_pauses.erase(found);
    DebugTask& state = m_debugTasks[task];
    state.mode = mode;
    state.depth = state.frames.size();
    const auto wait = m_waits.find(pause);
    Report report = reportFor(ReportKind::DebugResumed, log::Level::Info,
                              wait != m_waits.end() ? wait->second.owner : Owner{0, task});
    report.message = std::format(R"({{"pause":{}}})", pause);
    emit(report);
    answer(pause, "null");
}

bool Engine::prepareDebug(JSContext* ctx)
{
    JSValue host = JS_NewObject(ctx);
    if (JS_IsException(host)) {
        return false;
    }
    constexpr std::pair<const char*, int> kFunctions[] = {{"probe", 0}, {"pause", 1}, {"enter", 2}, {"exit", 3}};
    for (const auto& [name, which] : kFunctions) {
        JSValue function = JS_NewCFunctionMagic(ctx, &Engine::jsDebug, name, 3, JS_CFUNC_generic_magic, which);
        if (JS_IsException(function) || JS_SetPropertyStr(ctx, host, name, function) < 0) {
            JS_FreeValue(ctx, host);
            return false;
        }
    }
    JSValue global = JS_GetGlobalObject(ctx);
    const bool set = JS_SetPropertyStr(ctx, global, "__gsHost", host) >= 0;
    JS_FreeValue(ctx, global);
    if (!set) {
        return false;
    }
    JSValue done = JS_Eval(ctx, kDebugSource.data(), kDebugSource.size(), "<debug>",
                           JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_STRICT);
    if (JS_IsException(done)) {
        return false;
    }
    JS_FreeValue(ctx, done);
    return true;
}

bool Engine::debugStops(const std::string& file, const std::string& position, const std::string& values)
{
    const TaskId task = m_owner.task;
    if (m_hits.size() < kMostHits) {
        const Task* running = task != 0 ? findTask(task) : nullptr;
        m_hits.push_back(DebugHit{.task = task,
                                  .file = file,
                                  .position = position,
                                  .values = values.size() <= kMostHitValues ? values : std::string("{}"),
                                  .label = running != nullptr ? running->handler : std::string(),
                                  .at = wallMilliseconds()});
    }
    const DebugTask& state = m_debugTasks[task];
    const std::size_t depth = state.frames.size();
    switch (state.mode) {
    case StepMode::Into:
        return true;
    case StepMode::Over:
        if (depth <= state.depth) {
            return true;
        }
        break;
    case StepMode::Out:
        if (depth < state.depth) {
            return true;
        }
        break;
    case StepMode::Continue:
        break;
    }
    return m_breakpoints.contains(file + "#" + position);
}

JSValue Engine::debugPause(JSContext* ctx, const std::string& file, const std::string& position,
                           const std::string& values)
{
    const Owner owner = m_owner;
    DebugTask& state = m_debugTasks[owner.task];
    state.mode = StepMode::Continue;
    std::string stack = "[";
    for (std::size_t i = 0; i < state.frames.size(); ++i) {
        stack += (i > 0 ? "," : "") + quotedJson(state.frames[i]);
    }
    stack += "]";
    liftGestures(owner.task);
    flushHits(true);
    return request(
        ctx,
        [this, owner, file, position, values, stack](std::uint64_t id) {
            m_pauses[id] = owner.task;
            Report report = reportFor(ReportKind::DebugPaused, log::Level::Info, owner);
            report.message = std::format(R"({{"pause":{},"file":{},"position":{},"values":{},"stack":{}}})", id,
                                         quotedJson(file), quotedJson(position), values, stack);
            emit(report);
        },
        [this, owner](std::uint64_t id) {
            if (m_pauses.erase(id) > 0) {
                Report report = reportFor(ReportKind::DebugResumed, log::Level::Info, owner);
                report.message = std::format(R"({{"pause":{}}})", id);
                emit(report);
            }
        });
}

void Engine::liftGestures(TaskId task)
{
    std::vector<std::uint64_t> ids;
    for (const auto& [id, wait] : m_waits) {
        if (wait.owner.task == task && wait.cut) {
            ids.push_back(id);
        }
    }
    for (const std::uint64_t id : ids) {
        auto node = m_waits.extract(id);
        if (node.empty()) {
            continue;
        }
        Wait& wait = node.mapped();
        if (const std::function<void()> cut = std::exchange(wait.cut, nullptr)) {
            try {
                cut();
            } catch (...) {
            }
        }
        settle(wait, false, JS_UNDEFINED);
    }
}

void Engine::flushHits(bool now)
{
    if (m_hits.empty()) {
        return;
    }
    const Microseconds time = m_clock.now();
    if (!now && m_hits.size() < kMostHits && time - m_hitsFlushed < kHitsGap) {
        return;
    }
    m_hitsFlushed = time;
    std::string json = "[";
    for (std::size_t i = 0; i < m_hits.size(); ++i) {
        const DebugHit& hit = m_hits[i];
        json += std::format(R"({}{{"task":{},"label":{},"file":{},"position":{},"values":{},"at":{}}})", i > 0 ? "," : "",
                            hit.task, quotedJson(hit.label), quotedJson(hit.file), quotedJson(hit.position),
                            hit.values.empty() ? std::string("{}") : hit.values, hit.at);
    }
    json += "]";
    m_hits.clear();
    Report report = reportFor(ReportKind::DebugHits, log::Level::Info, Owner{});
    report.message = std::move(json);
    emit(report);
}

JSValue Engine::jsDebug(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int which) noexcept
{
    try {
        Engine& engine = of(ctx);
        const auto text = [ctx, argc, argv](int index) {
            std::string value;
            if (index < argc) {
                if (const char* chars = JS_ToCString(ctx, argv[index]); chars != nullptr) {
                    value = chars;
                    JS_FreeCString(ctx, chars);
                }
            }
            return value;
        };
        switch (which) {
        case 0:
            return JS_NewBool(ctx, engine.debugStops(text(0), text(1), text(2)));
        case 1:
            return engine.debugPause(ctx, text(0), text(1), text(2));
        case 2: {
            DebugTask& state = engine.m_debugTasks[engine.m_owner.task];
            state.frames.push_back(text(0) + "#" + text(1));
            return JS_NewInt64(ctx, static_cast<std::int64_t>(state.frames.size()));
        }
        default: {
            std::int64_t depth = 0;
            if (argc > 0 && JS_ToInt64(ctx, &depth, argv[0]) == 0 && depth > 0) {
                DebugTask& state = engine.m_debugTasks[engine.m_owner.task];
                if (static_cast<std::size_t>(depth) <= state.frames.size()) {
                    state.frames.resize(static_cast<std::size_t>(depth - 1));
                }
            }
            return JS_UNDEFINED;
        }
        }
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue Engine::drawn(JSContext* ctx, std::uint64_t id, double durationMs)
{
    const auto* script = static_cast<const Script*>(JS_GetContextOpaque(ctx));
    if (script == nullptr || !JS_IsFunction(ctx, script->drawn)) {
        return JS_ThrowInternalError(ctx, "The script was unloaded");
    }
    JSValue arguments[2] = {JS_NewFloat64(ctx, static_cast<double>(id)), JS_NewFloat64(ctx, durationMs)};
    return JS_Call(ctx, script->drawn, JS_UNDEFINED, 2, arguments);
}

bool Engine::releaseBinding(JSContext* ctx, const std::string& control)
{
    const auto* script = static_cast<const Script*>(JS_GetContextOpaque(ctx));
    if (script == nullptr || !JS_IsFunction(ctx, script->release)) {
        JS_ThrowInternalError(ctx, "The script was unloaded");
        return false;
    }
    JSValue id = JS_NewStringLen(ctx, control.data(), control.size());
    if (JS_IsException(id)) {
        return false;
    }
    JSValue done = JS_Call(ctx, script->release, JS_UNDEFINED, 1, &id);
    JS_FreeValue(ctx, id);
    const bool released = !JS_IsException(done);
    JS_FreeValue(ctx, done);
    return released;
}

void Engine::answer(std::uint64_t request, const std::string& json)
{
    if (m_runtime == nullptr) {
        return;
    }
    auto node = m_waits.extract(request);
    if (node.empty()) {
        return;
    }
    Wait wait = std::move(node.mapped());
    wait.done = nullptr;
    wait.cut = nullptr;
    slice(wait.owner, [this, &wait, &json] {
        JSValue value = JS_ParseJSON(wait.context, json.c_str(), json.size(), "<answer>");
        if (JS_IsException(value)) {
            JS_FreeValue(wait.context, JS_GetException(wait.context));
            value = JS_NULL;
        }
        settle(wait, false, value);
        JS_FreeValue(wait.context, value);
    });
    flushDeferred();
}

void Engine::refuse(std::uint64_t request, const std::string& message)
{
    if (m_runtime == nullptr) {
        return;
    }
    auto node = m_waits.extract(request);
    if (node.empty()) {
        return;
    }
    Wait wait = std::move(node.mapped());
    wait.done = nullptr;
    wait.cut = nullptr;
    slice(wait.owner, [this, &wait, &message] {
        JSValue error = JS_NewError(wait.context);
        if (!JS_IsException(error)) {
            JS_DefinePropertyValueStr(wait.context, error, "message",
                                      JS_NewStringLen(wait.context, message.data(), message.size()),
                                      JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
        }
        settle(wait, true, error);
        JS_FreeValue(wait.context, error);
    });
    flushDeferred();
}

std::string Engine::ownerOf(JSContext* ctx) const
{
    const auto* script = static_cast<const Script*>(JS_GetContextOpaque(ctx));
    if (script == nullptr) {
        return {};
    }
    const std::u8string text = script->folder.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

bool Engine::gated(JSContext* ctx) const
{
    const auto* script = static_cast<const Script*>(JS_GetContextOpaque(ctx));
    return script != nullptr && (script->permissions.has_value() || !script->phone);
}

std::optional<std::string> Engine::refusal(JSContext* ctx, const ServiceRule& rule) const
{
    const auto* script = static_cast<const Script*>(JS_GetContextOpaque(ctx));
    if (script == nullptr) {
        return std::nullopt;
    }
    if (rule.phone && !script->phone) {
        return phoneProblem();
    }
    if (script->permissions && !permits(*script->permissions, rule.permission)) {
        return permissionProblem(rule.permission);
    }
    return std::nullopt;
}

const LoadedResources& Engine::resources(JSContext* ctx)
{
    static const LoadedResources none{Resources{}};
    auto* script = static_cast<Script*>(JS_GetContextOpaque(ctx));
    if (script != nullptr && script->files != nullptr) {
        if (!script->memoryResources) {
            const std::string* text = memoryFile(*script->files, kResourcesFile);
            script->memoryResources = text != nullptr ? parseResources(*text) : LoadedResources(Resources{});
        }
        return *script->memoryResources;
    }
    if (script == nullptr || script->folder.empty()) {
        return none;
    }
    auto found = m_resources.find(script->folder);
    if (found == m_resources.end()) {
        found = m_resources.emplace(script->folder, ResourceFile(script->folder / "res" / "resources.json")).first;
    }
    return found->second.current();
}

void Engine::evaluate(Script& script)
{
    m_importFailure.clear();
    script.context = newContext();
    if (script.context == nullptr) {
        failLoad(script.id, ErrorInfo{.message = "The script context could not be created"});
        return;
    }
    JSContext* ctx = script.context;
    JS_SetContextOpaque(ctx, &script);
    m_slice.active = false;
    const bool ready = prepare(script);
    const Microseconds prepared = m_clock.now();
    m_slice.active = true;
    m_slice.begin = prepared;
    m_slice.budgetStart = prepared;
    if (!ready) {
        if (JS_HasException(ctx)) {
            uncaught(ctx, JS_GetException(ctx));
        }
        failLoad(script.id, ErrorInfo{.message = "The script built-ins could not be installed"});
        return;
    }
    if (!script.components.empty() && !mountComponents(script)) {
        return;
    }
    std::string source;
    std::string entryName = script.entry;
    std::optional<std::string> missing;
    bool read = false;
    if (script.files != nullptr) {
        missing = missingModule(script.files.get(), script.folder, script.entry, script.entry);
        read = !missing && readMemoryModule(*script.files, script.entry, source);
    } else if (const auto file = moduleFile(script.folder, script.entry); !file) {
        missing = file.error();
    } else {
        read = readModule(*file, source);
    }
    if (!script.modules.empty()) {
        source = partsSource(script.entry, !missing, script.modules);
        entryName = std::string(kPartsModule);
    } else if (missing && !script.components.empty()) {
        entryName = std::string(kComponentsOnlyModule);
        source = "export {};";
    } else if (missing) {
        failLoad(script.id, ErrorInfo{.message = std::format("The entry module {} {}", script.entry, *missing),
                                      .location = {.module = script.entry}});
        return;
    } else if (!read) {
        failLoad(script.id, ErrorInfo{.message = std::format(
                                          "The entry module {} cannot be read or is larger than "
                                          "8 MiB",
                                          script.entry),
                                      .location = {.module = script.entry}});
        return;
    }
    if (entryName == script.entry && isTypeScriptModule(script.entry)) {
        std::optional<std::string> debugged;
        if (script.transform) {
            const Microseconds started = m_clock.now();
            try {
                debugged = script.transform(script.entry, source);
            } catch (...) {
                debugged.reset();
            }
            excuse(m_clock.now() - started);
        }
        if (debugged) {
            source = std::move(*debugged);
        } else {
            tsstrip::Result stripped = tsstrip::strip(source);
            if (!stripped.problems.empty()) {
                const tsstrip::Problem& first = stripped.problems.front();
                failLoad(script.id, ErrorInfo{.message = "SyntaxError: " + problemText(stripped),
                                              .location = {.module = script.entry,
                                                           .line = static_cast<int>(first.line),
                                                           .column = static_cast<int>(first.column)}});
                return;
            }
            source = std::move(stripped.javascript);
        }
    }
    JSValue module = JS_Eval(ctx, source.c_str(), source.size(), entryName.c_str(),
                             JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(module)) {
        uncaught(ctx, JS_GetException(ctx));
        return;
    }
    auto* definition = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(module));
    JSValue evaluation = JS_EvalFunction(ctx, module);
    if (JS_IsException(evaluation)) {
        uncaught(ctx, JS_GetException(ctx));
        return;
    }
    JSValue exports = JS_GetModuleNamespace(ctx, definition);
    if (JS_IsException(exports)) {
        JS_FreeValue(ctx, evaluation);
        uncaught(ctx, JS_GetException(ctx));
        return;
    }
    script.exports = exports;
    watch(ctx, evaluation, &Engine::jsLoadSettled, static_cast<std::int64_t>(script.id));
    JS_FreeValue(ctx, evaluation);
}

bool Engine::mountComponents(Script& script)
{
    JSContext* ctx = script.context;
    std::string source;
    std::string list;
    for (std::size_t i = 0; i < script.components.size(); ++i) {
        const ComponentModule& component = script.components[i];
        if (!component.placed || !plainName(component.ref) || !plainName(component.entry)) {
            continue;
        }
        std::string children;
        for (const std::string& child : component.children) {
            if (plainName(child)) {
                children += std::format("{}\"{}\"", children.empty() ? "" : ", ", child);
            }
        }
        source += std::format("import C{} from \"{}{}/{}\";\n", i, kComponentPrefix, component.ref, component.entry);
        list += std::format("[\"{}\", C{}, [{}]], ", component.ref, i, children);
    }
    source += "import.meta.mount([" + list + "]);\n";
    JSValue module = JS_Eval(ctx, source.c_str(), source.size(), kMountModule.data(),
                             JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(module)) {
        uncaught(ctx, JS_GetException(ctx));
        return false;
    }
    auto* definition = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(module));
    JSValue meta = JS_GetImportMeta(ctx, definition);
    if (JS_IsException(meta) ||
        JS_DefinePropertyValueStr(ctx, meta, "mount", JS_DupValue(ctx, script.mount), JS_PROP_CONFIGURABLE) < 0) {
        JS_FreeValue(ctx, meta);
        uncaught(ctx, JS_GetException(ctx));
        return false;
    }
    JS_FreeValue(ctx, meta);
    JSValue evaluation = JS_EvalFunction(ctx, module);
    if (JS_IsException(evaluation)) {
        uncaught(ctx, JS_GetException(ctx));
        return false;
    }
    const bool rejected = JS_PromiseState(ctx, evaluation) == JS_PROMISE_REJECTED;
    watch(ctx, evaluation, &Engine::jsMountSettled, static_cast<std::int64_t>(script.id));
    JS_FreeValue(ctx, evaluation);
    return !rejected;
}

bool Engine::watch(JSContext* ctx, JSValueConst promise, JSCFunctionData* callback, std::int64_t id)
{
    JSValue data = JS_NewInt64(ctx, id);
    JSValue fulfilled = JS_NewCFunctionData(ctx, callback, 1, 0, 1, &data);
    JSValue rejected = JS_NewCFunctionData(ctx, callback, 1, 1, 1, &data);
    JS_FreeValue(ctx, data);
    if (JS_IsException(fulfilled) || JS_IsException(rejected)) {
        JS_FreeValue(ctx, fulfilled);
        JS_FreeValue(ctx, rejected);
        uncaught(ctx, JS_GetException(ctx));
        return false;
    }
    JSValue chained = JS_PromiseThen(ctx, promise, fulfilled, rejected);
    JS_FreeValue(ctx, fulfilled);
    JS_FreeValue(ctx, rejected);
    if (JS_IsException(chained)) {
        uncaught(ctx, JS_GetException(ctx));
        return false;
    }
    JS_FreeValue(ctx, chained);
    return true;
}

void Engine::beginSlice(Owner owner)
{
    const Microseconds now = m_clock.now();
    m_owner = owner;
    m_ownerHandler.clear();
    if (owner.task != 0) {
        if (const Task* task = findTask(owner.task)) {
            m_ownerHandler = task->handler;
        }
    }
    m_slice = Slice{.active = true, .begin = now, .budgetStart = now};
    m_watchdogPending = false;
}

void Engine::endSlice()
{
    acknowledgeInterrupt();
    if (m_watchdogPending && m_runtime != nullptr && !halted()) {
        m_watchdogPending = false;
        blame(ErrorInfo{.message = "InternalError: interrupted"}, true, m_watchdogRan);
        drainJobs();
    }
    m_watchdogPending = false;
    if (m_runtime != nullptr && !halted()) {
        reportRejections();
        flushHits(false);
    }
    const Microseconds end = m_clock.now();
    const Microseconds duration = end - m_slice.begin;
    const bool interrupted = m_slice.interrupted;
    m_slice.active = false;
    m_slice.tripped = false;
    if (m_runtime == nullptr) {
        return;
    }
    account(end, duration, interrupted);
    if (m_memory.exceeded) {
        stopRuntime();
        return;
    }
    checkMemory();
    collectRetired();
}

void Engine::drainJobs()
{
    while (m_runtime != nullptr && !halted()) {
        JSContext* context = nullptr;
        const int result = JS_ExecutePendingJob(m_runtime, &context);
        acknowledgeInterrupt();
        if (result == 0) {
            return;
        }
        if (result < 0 && context != nullptr) {
            uncaught(context, JS_GetException(context));
        }
    }
}

void Engine::excuse(Microseconds spent) noexcept
{
    if (!m_slice.active || spent.count() <= 0) {
        return;
    }
    m_slice.begin += spent;
    m_slice.budgetStart += spent;
}

void Engine::acknowledgeInterrupt()
{
    if (!m_slice.tripped) {
        return;
    }
    m_slice.tripped = false;
    m_slice.budgetStart = m_clock.now();
    m_watchdogPending = true;
    m_watchdogRan = m_slice.overrun;
}

bool Engine::interruptNow() noexcept
{
    if (halted() || m_slice.tripped) {
        return true;
    }
    if (!m_slice.active || m_limits.sliceBudget.count() <= 0) {
        return false;
    }
    const Microseconds ran = m_clock.now() - m_slice.budgetStart;
    if (ran < m_limits.sliceBudget) {
        return false;
    }
    m_slice.tripped = true;
    m_slice.interrupted = true;
    m_slice.overrun = ran;
    return true;
}

bool Engine::halted() const noexcept
{
    return m_memory.exceeded || (m_stopRequested.load(std::memory_order_relaxed) && !m_cleaningUp);
}

namespace {

bool taskClass(JSContext* ctx, JSValueConst function)
{
    if (!JS_IsConstructor(ctx, function)) {
        return false;
    }
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue base = JS_GetPropertyStr(ctx, global, "Task");
    JS_FreeValue(ctx, global);
    JSValue prototype = JS_GetPropertyStr(ctx, function, "prototype");
    const bool failed = JS_IsException(base) || JS_IsException(prototype);
    const int found = !failed && JS_IsFunction(ctx, base) && JS_IsObject(prototype)
                          ? JS_IsInstanceOf(ctx, prototype, base)
                          : 0;
    JS_FreeValue(ctx, prototype);
    JS_FreeValue(ctx, base);
    if (failed || found < 0) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return false;
    }
    return found == 1;
}

}

void Engine::run(Script& script, TaskId id, const std::string& handler)
{
    m_tasks.try_emplace(id, Task{.id = id, .script = script.id, .handler = handler});
    slice(Owner{script.id, id}, [this, &script, id, &handler] {
        JSContext* ctx = script.context;
        JSValue function = JS_GetPropertyStr(ctx, script.exports, handler.c_str());
        if (JS_IsException(function)) {
            uncaught(ctx, JS_GetException(ctx));
            return;
        }
        if (taskClass(ctx, function)) {
            JSValue extra = JS_NewObject(ctx);
            if (JS_IsException(extra) || JS_SetPropertyStr(ctx, extra, "taskClass", function) < 0) {
                JS_FreeValue(ctx, extra);
                uncaught(ctx, JS_GetException(ctx));
                return;
            }
            invoke(script, id, script.startTask, extra);
            JS_FreeValue(ctx, extra);
            return;
        }
        if (!JS_IsFunction(ctx, function)) {
            JS_FreeValue(ctx, function);
            const ErrorInfo error{
                .message =
                    std::format("{} does not export a function named '{}'", script.entry, handler),
                .location = {.module = script.entry},
            };
            endTask(id, ReportKind::TaskFailed, &error);
            return;
        }
        invoke(script, id, function, JS_UNDEFINED);
        JS_FreeValue(ctx, function);
    });
}

void Engine::invoke(Script& script, TaskId id, JSValueConst function, JSValueConst extra)
{
    JSContext* ctx = script.context;
    JSValue argument = JS_DupValue(ctx, extra);
    JSValue context = JS_Call(ctx, script.createTask, JS_UNDEFINED, 1, &argument);
    JS_FreeValue(ctx, argument);
    if (JS_IsException(context)) {
        uncaught(ctx, JS_GetException(ctx));
        return;
    }
    if (Task* task = findTask(id)) {
        JSValue signal = JS_GetPropertyStr(ctx, context, "signal");
        if (JS_IsException(signal)) {
            JS_FreeValue(ctx, JS_GetException(ctx));
            signal = JS_UNDEFINED;
        }
        task->signal = signal;
    }
    JSValue result = JS_Call(ctx, function, JS_UNDEFINED, 1, &context);
    JS_FreeValue(ctx, context);
    if (JS_IsException(result)) {
        uncaught(ctx, JS_GetException(ctx));
        return;
    }
    if (JS_IsPromise(result)) {
        watch(ctx, result, &Engine::jsTaskSettled, static_cast<std::int64_t>(id));
    } else {
        endTask(id, ReportKind::TaskFinished, nullptr);
    }
    JS_FreeValue(ctx, result);
}

JSValue Engine::spawn(JSContext* ctx, JSValueConst function, const std::string& label, JSValueConst extra)
{
    auto* script = static_cast<Script*>(JS_GetContextOpaque(ctx));
    if (script == nullptr || script->unloading || halted()) {
        return JS_NewInt32(ctx, 0);
    }
    const TaskId id = m_taskIds.fetch_add(1, std::memory_order_relaxed) + 1;
    m_tasks.try_emplace(id, Task{.id = id, .script = script->id, .handler = label});
    const Owner previous = m_owner;
    std::string previousHandler = std::exchange(m_ownerHandler, label);
    m_owner = Owner{script->id, id};
    invoke(*script, id, function, extra);
    m_owner = previous;
    m_ownerHandler = std::move(previousHandler);
    return JS_NewInt64(ctx, static_cast<std::int64_t>(id));
}

void Engine::cancelFromScript(JSContext* ctx, TaskId id)
{
    const auto* script = static_cast<const Script*>(JS_GetContextOpaque(ctx));
    const Task* task = findTask(id);
    if (script == nullptr || task == nullptr || task->script != script->id || task->ended) {
        return;
    }
    if (!task->aborted) {
        abortTask(id);
    }
    endTask(id, ReportKind::TaskCancelled, nullptr);
}

void Engine::controlEvent(const std::string& id, bool down)
{
    deliver("control", id, down ? "true" : "false");
}

void Engine::stateEvent(const std::string& id, bool value)
{
    deliver("state", id, value ? "true" : "false");
}

void Engine::event(const std::string& kind, const std::string& id, const std::string& json)
{
    deliver(kind, id, json);
}

void Engine::deliver(std::string_view kind, const std::string& id, const std::string& json)
{
    if (m_runtime == nullptr || halted()) {
        return;
    }
    std::vector<ScriptId> ready;
    for (const auto& [scriptId, script] : m_scripts) {
        if (script.state == ScriptState::Ready && !script.unloading && script.context != nullptr) {
            ready.push_back(scriptId);
        }
    }
    std::ranges::sort(ready);
    for (const ScriptId scriptId : ready) {
        Script* script = findScript(scriptId);
        if (script == nullptr || script->state != ScriptState::Ready || script->unloading) {
            continue;
        }
        slice(Owner{scriptId, 0}, [&kind, &id, &json, script, this] {
            JSContext* ctx = script->context;
            JSValue value = JS_ParseJSON(ctx, json.c_str(), json.size(), "<event>");
            if (JS_IsException(value)) {
                JS_FreeValue(ctx, JS_GetException(ctx));
                value = JS_NULL;
            }
            JSValue arguments[3] = {JS_NewStringLen(ctx, kind.data(), kind.size()),
                                    JS_NewStringLen(ctx, id.data(), id.size()), value};
            JSValue result = JS_Call(ctx, script->dispatch, JS_UNDEFINED, 3, arguments);
            for (JSValue& argument : arguments) {
                JS_FreeValue(ctx, argument);
            }
            if (JS_IsException(result)) {
                uncaught(ctx, JS_GetException(ctx));
            } else {
                JS_FreeValue(ctx, result);
            }
        });
        if (m_runtime == nullptr || halted()) {
            break;
        }
    }
    flushDeferred();
}

void Engine::taskEnded(ScriptId scriptId, TaskId id)
{
    Script* script = findScript(scriptId);
    if (script == nullptr || script->unloading || script->context == nullptr || m_runtime == nullptr || halted()) {
        return;
    }
    slice(Owner{scriptId, 0}, [script, id, this] {
        JSContext* ctx = script->context;
        JSValue argument = JS_NewInt64(ctx, static_cast<std::int64_t>(id));
        JSValue result = JS_Call(ctx, script->ended, JS_UNDEFINED, 1, &argument);
        JS_FreeValue(ctx, argument);
        if (JS_IsException(result)) {
            uncaught(ctx, JS_GetException(ctx));
        } else {
            JS_FreeValue(ctx, result);
        }
    });
}

void Engine::startPending(ScriptId id)
{
    Script* script = findScript(id);
    if (script == nullptr || script->state != ScriptState::Ready) {
        return;
    }
    std::vector<PendingStart> pending = std::move(script->pending);
    script->pending.clear();
    for (const PendingStart& entry : pending) {
        script = findScript(id);
        if (script == nullptr || script->state != ScriptState::Ready) {
            Report report =
                reportFor(ReportKind::TaskCancelled, log::Level::Info, Owner{id, entry.id});
            report.handler = entry.handler;
            emit(report);
            continue;
        }
        run(*script, entry.id, entry.handler);
    }
}

void Engine::abortTask(TaskId id)
{
    Task* task = findTask(id);
    if (task == nullptr || task->aborted || halted()) {
        return;
    }
    task->aborted = true;
    Script* script = findScript(task->script);
    if (script == nullptr || script->context == nullptr) {
        return;
    }
    JSContext* ctx = script->context;
    const Owner owner{task->script, id};
    JSValue reason = JS_UNDEFINED;
    if (JS_IsUndefined(task->signal)) {
        reason = JS_Call(ctx, script->cancelled, JS_UNDEFINED, 0, nullptr);
    } else {
        JSValue signal = JS_DupValue(ctx, task->signal);
        reason = JS_Call(ctx, script->abortSignal, JS_UNDEFINED, 1, &signal);
        JS_FreeValue(ctx, signal);
        if (JS_IsException(reason)) {
            uncaught(ctx, JS_GetException(ctx));
            reason = JS_Call(ctx, script->cancelled, JS_UNDEFINED, 0, nullptr);
        }
    }
    if (JS_IsException(reason)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        reason = JS_UNDEFINED;
    }
    if (Task* current = findTask(id)) {
        JS_FreeValue(ctx, current->reason);
        current->reason = JS_DupValue(ctx, reason);
    }
    rejectWaits(owner, reason);
    JS_FreeValue(ctx, reason);
}

void Engine::endTask(TaskId id, ReportKind kind, const ErrorInfo* error, Microseconds ran)
{
    Task* task = findTask(id);
    if (task == nullptr || task->ended) {
        return;
    }
    task->ended = true;
    m_debugTasks.erase(id);
    const bool failed = kind == ReportKind::TaskFailed || kind == ReportKind::TaskInterrupted;
    Report report =
        reportFor(kind, failed ? log::Level::Error : log::Level::Info, Owner{task->script, id});
    if (error != nullptr) {
        report.message = error->message;
        report.location = error->location;
        report.stack = error->stack;
    }
    if (kind == ReportKind::TaskInterrupted) {
        report.duration = ran;
        report.limit = m_limits.sliceBudget;
    }
    emit(report);
    m_deferred.push_back([this, script = report.script, id] { taskEnded(script, id); });
    if (failed && m_slice.active) {
        abortTask(id);
    }
    releaseTaskIfDone(id);
}

void Engine::releaseTaskIfDone(TaskId id)
{
    const auto found = m_tasks.find(id);
    if (found == m_tasks.end() || !found->second.ended || found->second.waits > 0) {
        return;
    }
    if (m_runtime != nullptr) {
        JS_FreeValueRT(m_runtime, found->second.signal);
        JS_FreeValueRT(m_runtime, found->second.reason);
    }
    m_tasks.erase(found);
}

void Engine::taskSettled(JSContext* ctx, TaskId id, bool rejected, JSValueConst value)
{
    Task* task = findTask(id);
    if (task == nullptr || task->ended) {
        return;
    }
    if (!rejected) {
        endTask(id, task->aborted ? ReportKind::TaskCancelled : ReportKind::TaskFinished, nullptr);
        return;
    }
    const ErrorInfo error = describeError(ctx, value);
    task = findTask(id);
    if (task == nullptr) {
        return;
    }
    if (task->aborted && error.abort) {
        endTask(id, ReportKind::TaskCancelled, nullptr);
    } else {
        endTask(id, ReportKind::TaskFailed, &error);
    }
}

void Engine::loadSettled(JSContext* ctx, ScriptId id, bool rejected, JSValueConst value)
{
    Script* script = findScript(id);
    if (script == nullptr || script->state != ScriptState::Loading) {
        return;
    }
    if (!rejected) {
        script->state = ScriptState::Ready;
        Report report = reportFor(ReportKind::ScriptLoaded, log::Level::Info, Owner{id, 0});
        report.location.module = script->entry;
        emit(report);
        if (!script->pending.empty()) {
            m_deferred.push_back([this, id] { startPending(id); });
        }
        return;
    }
    acknowledgeInterrupt();
    const bool interrupted = m_watchdogPending && m_owner == Owner{id, 0};
    if (interrupted) {
        m_watchdogPending = false;
    }
    failLoad(id, describeError(ctx, value), interrupted ? m_watchdogRan : Microseconds{0});
}

void Engine::failLoad(ScriptId id, ErrorInfo error, Microseconds ran)
{
    Script* script = findScript(id);
    if (script == nullptr || script->state != ScriptState::Loading) {
        return;
    }
    script->state = ScriptState::Failed;
    Report report = reportFor(ReportKind::ScriptFailed, log::Level::Error, Owner{id, 0});
    report.message = std::move(error.message);
    report.location = std::move(error.location);
    report.stack = std::move(error.stack);
    if (report.location.module.empty()) {
        report.location.module = m_importFailure.empty() ? script->entry : m_importFailure;
    }
    if (ran.count() > 0) {
        report.duration = ran;
        report.limit = m_limits.sliceBudget;
    }
    emit(report);
    for (const PendingStart& pending : script->pending) {
        Report failed = reportFor(ReportKind::TaskFailed, log::Level::Error, Owner{id, pending.id});
        failed.handler = pending.handler;
        failed.message = "The script failed to load";
        failed.location.module = script->entry;
        emit(failed);
    }
    script->pending.clear();
    m_deferred.push_back([this, id] { unload(id); });
}

void Engine::uncaught(JSContext* ctx, JSValue error)
{
    acknowledgeInterrupt();
    if (halted()) {
        JS_FreeValue(ctx, error);
        return;
    }
    const bool interrupted = JS_IsUncatchableError(error);
    ErrorInfo info = describeError(ctx, error);
    JS_FreeValue(ctx, error);
    if (interrupted) {
        if (!m_watchdogPending) {
            return;
        }
        m_watchdogPending = false;
        blame(std::move(info), true, m_watchdogRan);
        return;
    }
    blame(std::move(info), false, Microseconds{0});
}

void Engine::blame(ErrorInfo error, bool interrupted, Microseconds ran)
{
    if (m_owner.task != 0) {
        const ReportKind kind = interrupted ? ReportKind::TaskInterrupted : ReportKind::TaskFailed;
        const Task* task = findTask(m_owner.task);
        if (task != nullptr && !task->ended) {
            endTask(m_owner.task, kind, &error, ran);
            return;
        }
        Report report = reportFor(kind, log::Level::Error, m_owner);
        report.message = std::move(error.message);
        report.location = std::move(error.location);
        report.stack = std::move(error.stack);
        if (interrupted) {
            report.duration = ran;
            report.limit = m_limits.sliceBudget;
        }
        emit(report);
        if (task != nullptr && m_slice.active) {
            abortTask(m_owner.task);
        }
        return;
    }
    failLoad(m_owner.script, std::move(error), interrupted ? ran : Microseconds{0});
}

JSValue Engine::wait(JSContext* ctx, std::optional<Microseconds> delay, bool host)
{
    auto* script = static_cast<Script*>(JS_GetContextOpaque(ctx));
    if (script == nullptr) {
        return JS_ThrowInternalError(ctx, "The script was unloaded");
    }
    Task* task = m_owner.task != 0 ? findTask(m_owner.task) : nullptr;
    if (script->unloading || (task != nullptr && task->aborted)) {
        JSValue reason = task != nullptr && !JS_IsUndefined(task->reason)
                             ? JS_DupValue(ctx, task->reason)
                             : JS_Call(ctx, script->cancelled, JS_UNDEFINED, 0, nullptr);
        if (JS_IsException(reason)) {
            return reason;
        }
        JSValue rejected = JS_NewSettledPromise(ctx, true, reason);
        JS_FreeValue(ctx, reason);
        return rejected;
    }
    JSValue functions[2];
    JSValue promise = JS_NewPromiseCapability(ctx, functions);
    if (JS_IsException(promise)) {
        return promise;
    }
    const std::uint64_t id = ++m_lastWait;
    try {
        m_waits.emplace(id, Wait{.owner = m_owner,
                                 .context = ctx,
                                 .resolve = functions[0],
                                 .reject = functions[1]});
        try {
            if (delay) {
                m_timers.emplace(m_time->now() + *delay, id);
            } else if (!host) {
                m_frameWaits.push_back(id);
            }
        } catch (...) {
            m_waits.erase(id);
            throw;
        }
    } catch (...) {
        JS_FreeValue(ctx, functions[0]);
        JS_FreeValue(ctx, functions[1]);
        JS_FreeValue(ctx, promise);
        return JS_ThrowOutOfMemory(ctx);
    }
    if (task != nullptr) {
        ++task->waits;
    }
    return promise;
}

void Engine::settle(Wait& wait, bool rejected, JSValueConst value)
{
    const std::function<void()> cut = std::exchange(wait.cut, nullptr);
    if (rejected && cut) {
        try {
            cut();
        } catch (...) {
        }
    }
    if (wait.done) {
        const std::function<void()> done = std::move(wait.done);
        wait.done = nullptr;
        try {
            done();
        } catch (...) {
        }
    }
    JSValue argument = value;
    JSValue result =
        JS_Call(wait.context, rejected ? wait.reject : wait.resolve, JS_UNDEFINED, 1, &argument);
    JS_FreeValue(wait.context, wait.resolve);
    JS_FreeValue(wait.context, wait.reject);
    wait.resolve = JS_UNDEFINED;
    wait.reject = JS_UNDEFINED;
    if (JS_IsException(result)) {
        uncaught(wait.context, JS_GetException(wait.context));
    } else {
        JS_FreeValue(wait.context, result);
    }
    if (wait.owner.task == 0) {
        return;
    }
    Task* task = findTask(wait.owner.task);
    if (task != nullptr && task->waits > 0) {
        --task->waits;
        releaseTaskIfDone(wait.owner.task);
    }
}

void Engine::rejectWaits(Owner owner, JSValueConst reason)
{
    std::vector<std::uint64_t> ids;
    for (const auto& [id, wait] : m_waits) {
        if (wait.owner == owner) {
            ids.push_back(id);
        }
    }
    std::ranges::sort(ids);
    for (const std::uint64_t id : ids) {
        auto node = m_waits.extract(id);
        if (!node.empty()) {
            settle(node.mapped(), true, reason);
        }
    }
}

bool Engine::hasWaits(Owner owner) const
{
    return std::ranges::any_of(m_waits,
                               [owner](const auto& entry) { return entry.second.owner == owner; });
}

std::vector<TaskId> Engine::tasksOf(ScriptId script, bool runningOnly) const
{
    std::vector<TaskId> ids;
    for (const auto& [id, task] : m_tasks) {
        if (task.script == script && (!runningOnly || !task.ended)) {
            ids.push_back(id);
        }
    }
    std::ranges::sort(ids);
    return ids;
}

void Engine::releaseScript(ScriptId id)
{
    const auto found = m_scripts.find(id);
    if (found == m_scripts.end()) {
        return;
    }
    Script& script = found->second;
    if (m_runtime != nullptr) {
        for (auto entry = m_waits.begin(); entry != m_waits.end();) {
            if (entry->second.owner.script == id || entry->second.context == script.context) {
                for (const std::function<void()>* callback : {&entry->second.cut, &entry->second.done}) {
                    if (*callback) {
                        try {
                            (*callback)();
                        } catch (...) {
                        }
                    }
                }
                JS_FreeValueRT(m_runtime, entry->second.resolve);
                JS_FreeValueRT(m_runtime, entry->second.reject);
                entry = m_waits.erase(entry);
            } else {
                ++entry;
            }
        }
        std::erase_if(m_rejections, [&](Rejection& rejection) {
            if (rejection.owner.script != id && rejection.context != script.context) {
                return false;
            }
            JS_FreeValueRT(m_runtime, rejection.promise);
            JS_FreeValueRT(m_runtime, rejection.reason);
            return true;
        });
        std::erase_if(m_tasks, [&](auto& entry) {
            if (entry.second.script != id) {
                return false;
            }
            JS_FreeValueRT(m_runtime, entry.second.signal);
            JS_FreeValueRT(m_runtime, entry.second.reason);
            return true;
        });
        JS_FreeValueRT(m_runtime, script.exports);
        JS_FreeValueRT(m_runtime, script.createTask);
        JS_FreeValueRT(m_runtime, script.abortSignal);
        JS_FreeValueRT(m_runtime, script.cancelled);
        JS_FreeValueRT(m_runtime, script.dispatch);
        JS_FreeValueRT(m_runtime, script.ended);
        JS_FreeValueRT(m_runtime, script.drawn);
        JS_FreeValueRT(m_runtime, script.release);
        JS_FreeValueRT(m_runtime, script.startTask);
        JS_FreeValueRT(m_runtime, script.mount);
    }
    JSContext* ctx = script.context;
    m_scripts.erase(found);
    if (ctx != nullptr) {
        JS_SetContextOpaque(ctx, nullptr);
        retire(ctx);
    }
}

void Engine::retire(JSContext* ctx)
{
    m_retired.push_back(ctx);
    collectRetired();
}

void Engine::collectRetired()
{
    if (m_retired.empty() || m_runtime == nullptr || JS_IsJobPending(m_runtime)) {
        return;
    }
    for (JSContext* ctx : m_retired) {
        m_stores.forget(ctx);
        JS_FreeContext(ctx);
    }
    m_retired.clear();
}

void Engine::reportRejections()
{
    std::vector<Rejection> rejections;
    rejections.swap(m_rejections);
    for (Rejection& rejection : rejections) {
        const ErrorInfo error = describeError(rejection.context, rejection.reason);
        if (!error.abort) {
            Report report =
                reportFor(ReportKind::UnhandledRejection, log::Level::Error, rejection.owner);
            report.message = error.message;
            report.location = error.location;
            report.stack = error.stack;
            emit(report);
        }
        JS_FreeValue(rejection.context, rejection.promise);
        JS_FreeValue(rejection.context, rejection.reason);
    }
}

void Engine::checkMemory()
{
    const std::size_t warning = m_limits.memoryWarningBytes;
    if (warning == 0) {
        return;
    }
    if (!m_memoryWarned && m_memory.used >= warning) {
        m_memoryWarned = true;
        Report report = reportFor(ReportKind::MemoryWarning, log::Level::Warning, m_owner);
        report.bytes = m_memory.used;
        report.byteLimit = warning;
        emit(report);
    } else if (m_memoryWarned && m_memory.used < warning - warning / 4) {
        m_memoryWarned = false;
    }
}

void Engine::account(Microseconds end, Microseconds duration, bool interrupted)
{
    const LoadMeter::Verdict verdict = m_meter.record(end, duration);
    if (verdict.spike && !interrupted && claimSpike(m_owner)) {
        Report report = reportFor(ReportKind::SliceSpike, log::Level::Warning, m_owner);
        report.duration = duration;
        report.limit = m_limits.spikeThreshold;
        emit(report);
    }
    if (verdict.sustained) {
        Report report = reportFor(ReportKind::SustainedLoad, log::Level::Warning, m_owner);
        report.duration = verdict.busy;
        report.limit = m_limits.sustainedBusy;
        report.window = m_limits.sustainedWindow;
        emit(report);
    }
}

bool Engine::claimSpike(Owner owner)
{
    Script* script = findScript(owner.script);
    if (script == nullptr) {
        return true;
    }
    std::string key;
    if (owner.task != 0) {
        const Task* task = findTask(owner.task);
        key = task != nullptr ? task->handler : owner == m_owner ? m_ownerHandler : std::string{};
    }
    return script->spiked.insert(key).second;
}

Engine::ErrorInfo Engine::describeError(JSContext* ctx, JSValueConst value)
{
    ErrorInfo error;
    error.message = textOf(ctx, value);
    if (error.message.empty()) {
        error.message = "Error";
    }
    if (JS_IsObject(value)) {
        error.stack = propertyText(ctx, value, "stack");
        error.abort = propertyText(ctx, value, "name") == "AbortError";
        error.location = locationFromStack(error.stack);
    }
    return error;
}

Engine::Script* Engine::findScript(ScriptId id)
{
    const auto found = m_scripts.find(id);
    return found == m_scripts.end() ? nullptr : &found->second;
}

Engine::Task* Engine::findTask(TaskId id)
{
    const auto found = m_tasks.find(id);
    return found == m_tasks.end() ? nullptr : &found->second;
}

Report Engine::reportFor(ReportKind kind, log::Level level, Owner owner) const
{
    Report report{.kind = kind, .level = level, .script = owner.script, .task = owner.task};
    if (owner.task != 0) {
        if (const auto found = m_tasks.find(owner.task); found != m_tasks.end()) {
            report.handler = found->second.handler;
        } else if (owner == m_owner) {
            report.handler = m_ownerHandler;
        }
    }
    return report;
}

void Engine::emit(const Report& report) noexcept
{
    if (!m_sink) {
        return;
    }
    try {
        m_sink(report);
    } catch (...) {
    }
}

void Engine::flushDeferred()
{
    while (!m_deferred.empty()) {
        std::function<void()> work = std::move(m_deferred.front());
        m_deferred.pop_front();
        work();
    }
}

Engine& Engine::of(JSContext* ctx) noexcept
{
    return *static_cast<Engine*>(JS_GetRuntimeOpaque(JS_GetRuntime(ctx)));
}

int Engine::jsInterrupt(JSRuntime*, void* opaque) noexcept
{
    return static_cast<Engine*>(opaque)->interruptNow() ? 1 : 0;
}

void Engine::jsRejection(JSContext* ctx, JSValueConst promise, JSValueConst reason, bool handled,
                         void* opaque) noexcept
{
    Engine& engine = *static_cast<Engine*>(opaque);
    try {
        if (handled) {
            const auto found =
                std::ranges::find_if(engine.m_rejections, [&](const Rejection& rejection) {
                    return JS_VALUE_GET_PTR(rejection.promise) == JS_VALUE_GET_PTR(promise);
                });
            if (found != engine.m_rejections.end()) {
                JS_FreeValue(found->context, found->promise);
                JS_FreeValue(found->context, found->reason);
                engine.m_rejections.erase(found);
            }
            return;
        }
        if (engine.m_rejections.size() >= kMostRejections) {
            return;
        }
        Rejection& rejection = engine.m_rejections.emplace_back();
        rejection.owner = engine.m_owner;
        rejection.context = ctx;
        rejection.promise = JS_DupValue(ctx, promise);
        rejection.reason = JS_DupValue(ctx, reason);
    } catch (...) {
    }
}

char* Engine::jsNormalize(JSContext* ctx, const char* base, const char* name, void* opaque) noexcept
{
    Engine& engine = *static_cast<Engine*>(opaque);
    try {
        const auto* script = static_cast<const Script*>(JS_GetContextOpaque(ctx));
        if (script == nullptr) {
            JS_ThrowReferenceError(ctx, "The script was unloaded");
            return nullptr;
        }
        const std::string_view wanted(name);
        const std::string_view importer(base);
        if (wanted == api::kModuleName) {
            return js_strdup(ctx, name);
        }
        const ComponentPart* inside = nullptr;
        ComponentPart part;
        if (importer.starts_with(kComponentPrefix)) {
            if (const auto found = componentPart(script->components, importer)) {
                part = *found;
                inside = &part;
            }
        }
        if (wanted.starts_with(kComponentPrefix)) {
            const auto found = componentPart(script->components, wanted);
            if (!found || missingModule(script->files.get(), found->folder, wanted, found->path)) {
                engine.m_importFailure = base;
                JS_ThrowReferenceError(ctx, "Cannot find the component module %s", name);
                return nullptr;
            }
            return js_strdup(ctx, name);
        }
        if (inside != nullptr) {
            const std::size_t shelf = inside->ref.find('/');
            const auto resolved =
                resolveImport(inside->ref.substr(shelf + 1) + "/" + inside->path, wanted);
            if (!resolved) {
                engine.m_importFailure = base;
                JS_ThrowTypeError(ctx, "Cannot import '%s' from %s: %s", name, base, resolved.error().c_str());
                return nullptr;
            }
            const std::size_t cut = resolved->find('/');
            const std::string ref =
                cut == std::string::npos ? std::string() : inside->ref.substr(0, shelf + 1) + resolved->substr(0, cut);
            const auto sibling = std::ranges::find(script->components, ref, &ComponentModule::ref);
            if (sibling == script->components.end()) {
                engine.m_importFailure = base;
                JS_ThrowTypeError(ctx, "Cannot import '%s' from %s: the path leaves the component folder", name, base);
                return nullptr;
            }
            const std::string path = resolved->substr(cut + 1);
            const std::string module = std::string(kComponentPrefix) + ref + "/" + path;
            if (const auto missing = missingModule(script->files.get(), sibling->folder, module, path)) {
                engine.m_importFailure = base;
                JS_ThrowReferenceError(ctx, "Cannot import '%s' from %s: %s %s", name, base, path.c_str(),
                                       missing->c_str());
                return nullptr;
            }
            return js_strdup(ctx, module.c_str());
        }
        const auto resolved = resolveImport(importer, wanted);
        if (!resolved) {
            engine.m_importFailure = base;
            JS_ThrowTypeError(ctx, "Cannot import '%s' from %s: %s", name, base,
                              resolved.error().c_str());
            return nullptr;
        }
        if (const auto missing = missingModule(script->files.get(), script->folder, *resolved, *resolved)) {
            engine.m_importFailure = base;
            JS_ThrowReferenceError(ctx, "Cannot import '%s' from %s: %s %s", name, base,
                                   resolved->c_str(), missing->c_str());
            return nullptr;
        }
        return js_strdup(ctx, resolved->c_str());
    } catch (...) {
        JS_ThrowOutOfMemory(ctx);
        return nullptr;
    }
}

JSModuleDef* Engine::jsLoadModule(JSContext* ctx, const char* name, void*, JSValueConst) noexcept
{
    try {
        const auto* script = static_cast<const Script*>(JS_GetContextOpaque(ctx));
        if (script == nullptr) {
            JS_ThrowReferenceError(ctx, "The script was unloaded");
            return nullptr;
        }
        const std::string_view wanted(name);
        if (wanted == api::kModuleName) {
            return apiModule(ctx);
        }
        const auto part = wanted.starts_with(kComponentPrefix) ? componentPart(script->components, wanted) : std::nullopt;
        if (wanted.starts_with(kComponentPrefix) && !part) {
            JS_ThrowReferenceError(ctx, "Cannot load %s: there is no such component", name);
            return nullptr;
        }
        std::string source;
        if (script->files != nullptr) {
            if (memoryFile(*script->files, wanted) == nullptr) {
                JS_ThrowReferenceError(ctx, "Cannot load %s: it cannot be found", name);
                return nullptr;
            }
            if (!readMemoryModule(*script->files, wanted, source)) {
                JS_ThrowReferenceError(ctx, "Cannot load %s: it is larger than 8 MiB", name);
                return nullptr;
            }
        } else {
            const auto file = part ? moduleFile(part->folder, part->path) : moduleFile(script->folder, name);
            if (!file) {
                JS_ThrowReferenceError(ctx, "Cannot load %s: it %s", name, file.error().c_str());
                return nullptr;
            }
            if (!readModule(*file, source)) {
                JS_ThrowReferenceError(ctx, "Cannot load %s: it cannot be read or is larger than 8 MiB",
                                       name);
                return nullptr;
            }
        }
        if (isTypeScriptModule(name)) {
            std::optional<std::string> debugged;
            if (script->transform && !part) {
                Engine& engine = of(ctx);
                const Microseconds started = engine.m_clock.now();
                try {
                    debugged = script->transform(std::string(wanted), source);
                } catch (...) {
                    debugged.reset();
                }
                engine.excuse(engine.m_clock.now() - started);
            }
            if (debugged) {
                source = std::move(*debugged);
            } else {
                tsstrip::Result stripped = tsstrip::strip(source);
                if (!stripped.problems.empty()) {
                    throwStripProblem(ctx, name, stripped);
                    return nullptr;
                }
                source = std::move(stripped.javascript);
            }
        }
        JSValue compiled = JS_Eval(ctx, source.c_str(), source.size(), name,
                                   JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(compiled)) {
            return nullptr;
        }
        auto* module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(compiled));
        JS_FreeValue(ctx, compiled);
        return module;
    } catch (...) {
        JS_ThrowOutOfMemory(ctx);
        return nullptr;
    }
}

int Engine::jsCheckAttributes(JSContext* ctx, void*, JSValueConst) noexcept
{
    JS_ThrowTypeError(ctx, "Import attributes are not supported");
    return -1;
}

JSValue Engine::jsSleep(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) noexcept
{
    try {
        double ms = std::numeric_limits<double>::quiet_NaN();
        if (argc > 0 && JS_ToFloat64(ctx, &ms, argv[0]) < 0) {
            return JS_EXCEPTION;
        }
        if (!(ms >= 0.0 && ms <= kLongestSleepMs)) {
            return JS_ThrowRangeError(
                ctx, "sleep(ms) needs a number of milliseconds from 0 to 2147483647");
        }
        return of(ctx).wait(ctx, Microseconds{static_cast<std::int64_t>(std::ceil(ms * 1000.0))});
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue Engine::jsNextFrame(JSContext* ctx, JSValueConst, int, JSValueConst*) noexcept
{
    try {
        return of(ctx).wait(ctx, std::nullopt);
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue Engine::jsNow(JSContext* ctx, JSValueConst, int, JSValueConst*) noexcept
{
    return JS_NewFloat64(ctx, of(ctx).millisecondsNow());
}

JSValue Engine::jsLog(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv,
                      int level) noexcept
{
    try {
        std::string message;
        for (int i = 0; i < argc; ++i) {
            const JsText part(ctx, argv[i]);
            if (!part.valid()) {
                return JS_EXCEPTION;
            }
            if (i > 0) {
                message += ' ';
            }
            message += part.view();
        }
        Engine& engine = of(ctx);
        Report report =
            engine.reportFor(ReportKind::Log, static_cast<log::Level>(level), engine.m_owner);
        report.message = std::move(message);
        engine.emit(report);
        return JS_UNDEFINED;
    } catch (...) {
        return JS_ThrowOutOfMemory(ctx);
    }
}

JSValue Engine::jsTaskSettled(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv,
                              int rejected, JSValueConst* data) noexcept
{
    try {
        std::int64_t id = 0;
        if (JS_ToInt64(ctx, &id, data[0]) == 0) {
            of(ctx).taskSettled(ctx, static_cast<TaskId>(id), rejected != 0,
                                argc > 0 ? argv[0] : JS_UNDEFINED);
        }
    } catch (...) {
    }
    return JS_UNDEFINED;
}

JSValue Engine::jsMountSettled(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv,
                               int rejected, JSValueConst* data) noexcept
{
    try {
        std::int64_t id = 0;
        if (rejected != 0 && JS_ToInt64(ctx, &id, data[0]) == 0) {
            of(ctx).loadSettled(ctx, static_cast<ScriptId>(id), true, argc > 0 ? argv[0] : JS_UNDEFINED);
        }
    } catch (...) {
    }
    return JS_UNDEFINED;
}

JSValue Engine::jsLoadSettled(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv,
                              int rejected, JSValueConst* data) noexcept
{
    try {
        std::int64_t id = 0;
        if (JS_ToInt64(ctx, &id, data[0]) == 0) {
            of(ctx).loadSettled(ctx, static_cast<ScriptId>(id), rejected != 0,
                                argc > 0 ? argv[0] : JS_UNDEFINED);
        }
    } catch (...) {
    }
    return JS_UNDEFINED;
}

}
