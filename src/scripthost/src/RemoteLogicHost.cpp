#include "gs/scripthost/RemoteLogicHost.h"

#include "Pipe.h"
#include "Sandbox.h"
#include "Wire.h"

#include <gs/base/Log.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <system_error>
#include <thread>
#include <utility>

namespace gs::scripthost {

namespace {

using detail::Kind;
using detail::Method;
using detail::Reader;
using detail::Writer;

constexpr auto kReadyWait = std::chrono::seconds(10);
constexpr DWORD kStopWaitMs = 2'000;
constexpr DWORD kExitWaitMs = 1'000;
constexpr DWORD kSelfTestWaitMs = 20'000;
constexpr UINT kKilledExit = 9;
constexpr std::size_t kMostPackedFiles = 4000;
constexpr int kMostPackedDepth = 16;

std::wstring lowered(std::wstring text)
{
    for (wchar_t& c : text) {
        if (c >= L'A' && c <= L'Z') {
            c = static_cast<wchar_t>(c - L'A' + L'a');
        }
    }
    return text;
}

bool packedFile(const std::filesystem::path& path)
{
    const std::wstring name = lowered(path.filename().wstring());
    if (name.ends_with(L".d.ts")) {
        return false;
    }
    return name.ends_with(L".ts") || name.ends_with(L".mts") || name.ends_with(L".js") || name.ends_with(L".mjs") ||
           name.ends_with(L".json");
}

bool skippedFolder(const std::filesystem::path& path)
{
    const std::wstring name = lowered(path.filename().wstring());
    return name.starts_with(L'.') || name == L"node_modules" || name == L"types" || name == L"tests" ||
           name == L"sessions";
}

bool packInto(const std::filesystem::path& folder, const std::string& prefix, script::ScriptFiles& files,
              std::uintmax_t& total, std::string& problem)
{
    std::error_code error;
    if (folder.empty() || !std::filesystem::is_directory(folder, error)) {
        return true;
    }
    for (std::filesystem::recursive_directory_iterator
             it(folder, std::filesystem::directory_options::skip_permission_denied, error),
         end;
         !error && it != end; it.increment(error)) {
        std::error_code kind;
        if (it->is_directory(kind)) {
            if (skippedFolder(it->path()) || it.depth() >= kMostPackedDepth) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!it->is_regular_file(kind) || !packedFile(it->path())) {
            continue;
        }
        const std::uintmax_t size = it->file_size(kind);
        if (kind) {
            continue;
        }
        total += size;
        if (total > kMostLogicBytes) {
            problem = "The extension's logic is larger than 16 MiB, so it cannot run";
            return false;
        }
        if (files.size() >= kMostPackedFiles) {
            problem = "The extension's logic has more than 4000 files, so it cannot run";
            return false;
        }
        std::ifstream in(it->path(), std::ios::binary);
        std::string contents{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        const std::u8string relative = it->path().lexically_relative(folder).generic_u8string();
        files.insert_or_assign(prefix + std::string(reinterpret_cast<const char*>(relative.data()), relative.size()),
                               std::move(contents));
    }
    return true;
}

std::string randomToken()
{
    std::random_device device;
    std::string token;
    for (int part = 0; part < 4; ++part) {
        token += std::format("{:08x}", static_cast<std::uint32_t>(device()));
    }
    return token;
}

void closeHandle(HANDLE& handle) noexcept
{
    if (handle != nullptr) {
        CloseHandle(handle);
        handle = nullptr;
    }
}

}

std::filesystem::path defaultProgram()
{
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return L"gs-scripthost.exe";
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::filesystem::path(buffer).parent_path() / L"gs-scripthost.exe";
}

std::string_view modeName(SandboxMode mode) noexcept
{
    switch (mode) {
    case SandboxMode::AppContainer:
        return "an AppContainer";
    case SandboxMode::Restricted:
        return "a restricted token with low integrity";
    case SandboxMode::None:
        break;
    }
    return "no sandbox";
}

bool packFolder(const std::filesystem::path& folder, const std::vector<script::ComponentModule>& components,
                script::ScriptFiles& files, std::string& problem)
{
    std::uintmax_t total = 0;
    if (!packInto(folder, {}, files, total, problem)) {
        return false;
    }
    for (const script::ComponentModule& component : components) {
        if (!packInto(component.folder, "@component/" + component.ref + "/", files, total, problem)) {
            return false;
        }
    }
    return true;
}

struct RemoteLogicHost::Impl {
    enum class State : std::uint8_t { Idle, Wanted, Starting, Ready };

    struct Outgoing {
        std::string frame;
        std::optional<detail::LoadMessage> load;
    };

    Impl(Sink target, const script::Limits& given, std::shared_ptr<script::ScriptServices> provided,
         RemoteOptions chosen)
        : sink(std::move(target))
        , limits(given)
        , services(std::move(provided))
        , options(std::move(chosen))
    {
        if (options.program.empty()) {
            options.program = defaultProgram();
        }
        if (options.label.empty()) {
            options.label = "logic";
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void run();
    bool start();
    bool attempt(SandboxMode wanted, std::string& why);
    void deliver(Outgoing& next, const std::shared_ptr<detail::PipeWriter>& pipe);
    void post(Outgoing item);
    void post(Writer& out);
    void readFrom(std::unique_ptr<detail::PipeReader> pipe, std::uint64_t which);
    void handle(const detail::Frame& frame, std::uint64_t which);
    void serve(Reader& in, std::optional<std::uint64_t> call, Method method);
    void ended(std::uint64_t which);
    void emit(const script::Report& report);
    void fail(script::ScriptId script, const std::string& entry, std::string text);
    bool send(Writer& out);
    [[nodiscard]] bool stopping() const;
    void release() noexcept;

    Sink sink;
    const script::Limits limits;
    std::shared_ptr<script::ScriptServices> services;
    RemoteOptions options;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::deque<Outgoing> queue;
    State state = State::Wanted;
    std::shared_ptr<detail::PipeWriter> writer;
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    DWORD processId = 0;
    SandboxMode mode = SandboxMode::None;
    std::uint64_t generation = 0;
    bool ready = false;
    bool alive = false;
    bool childStarted = false;
    bool closing = false;
    bool broken = false;
    std::string problem;
    std::set<script::ScriptId> loaded;
    double frameRate = 0.0;
    std::vector<std::string> breakpoints;
    std::atomic<script::ScriptId> lastScript{0};
    std::atomic<script::TaskId> lastTask{detail::kMainTaskBase};
    std::jthread reader;
    std::jthread worker;
};

void RemoteLogicHost::Impl::run()
{
    SetThreadDescription(GetCurrentThread(), L"gs-logic-send");
    for (;;) {
        std::unique_lock lock(mutex);
        changed.wait(lock, [this] {
            return closing || state == State::Wanted || (state == State::Ready && !queue.empty());
        });
        if (closing) {
            return;
        }
        if (state == State::Wanted) {
            state = State::Starting;
            lock.unlock();
            const bool up = start();
            lock.lock();
            if (closing) {
                return;
            }
            if (!up) {
                state = State::Idle;
                std::deque<Outgoing> dropped = std::exchange(queue, {});
                const std::string why = problem;
                lock.unlock();
                for (Outgoing& item : dropped) {
                    if (item.load) {
                        fail(item.load->script, item.load->source.entry, why);
                    }
                }
            }
            continue;
        }
        Outgoing next = std::move(queue.front());
        queue.pop_front();
        const std::shared_ptr<detail::PipeWriter> pipe = writer;
        lock.unlock();
        deliver(next, pipe);
    }
}

bool RemoteLogicHost::Impl::start()
{
    if (reader.joinable()) {
        reader.join();
    }
    release();
    std::string why;
    bool up = false;
    if (!options.restrictedOnly) {
        up = attempt(SandboxMode::AppContainer, why);
        if (!up && !stopping()) {
            log::warning("logic", "{}: the logic sandbox could not start as an AppContainer ({}), trying a restricted "
                                  "token",
                         options.label, why);
        }
    }
    if (!up && !stopping()) {
        up = attempt(SandboxMode::Restricted, why);
    }
    if (up) {
        return true;
    }
    if (!stopping()) {
        log::warning("logic", "{}: the logic sandbox could not start: {}", options.label, why);
    }
    const std::scoped_lock lock(mutex);
    problem = "The logic sandbox could not start: " + why;
    broken = true;
    return false;
}

bool RemoteLogicHost::Impl::attempt(SandboxMode wanted, std::string& why)
{
    std::optional<detail::Launched> launched = detail::launch(options.program, wanted, {}, why);
    if (!launched) {
        return false;
    }
    auto pipe = std::make_unique<detail::PipeReader>(std::exchange(launched->fromChild, nullptr));
    auto out = std::make_shared<detail::PipeWriter>(std::exchange(launched->toChild, nullptr));
    std::uint64_t current = 0;
    {
        const std::scoped_lock lock(mutex);
        if (closing) {
            TerminateProcess(launched->process, kKilledExit);
            detail::closeLaunched(*launched);
            why = "the logic was stopped while its sandbox started";
            return false;
        }
        process = std::exchange(launched->process, nullptr);
        job = std::exchange(launched->job, nullptr);
        processId = launched->id;
        writer = out;
        mode = wanted;
        ready = false;
        alive = true;
        childStarted = false;
        current = ++generation;
    }
    reader = std::jthread([this, current, pipe = std::move(pipe)]() mutable { readFrom(std::move(pipe), current); });
    Writer hello(Kind::Hello);
    detail::put(hello, detail::HelloMessage{.limits = limits, .services = services != nullptr, .label = options.label});
    static_cast<void>(out->send(hello.frame()));
    std::unique_lock lock(mutex);
    changed.wait_for(lock, kReadyWait,
                     [this, current] { return closing || generation != current || ready || !alive; });
    if (closing) {
        why = "the logic was stopped while its sandbox started";
        return false;
    }
    if (generation == current && ready && alive && childStarted) {
        state = State::Ready;
        broken = false;
        problem.clear();
        const double rate = frameRate;
        const std::vector<std::string> places = breakpoints;
        const DWORD id = processId;
        lock.unlock();
        log::info("logic", "{}: logic sandbox started in {} (process {})", options.label, modeName(wanted), id);
        if (rate > 0.0) {
            Writer again(Kind::FrameRate);
            again.f64(rate);
            static_cast<void>(out->send(again.frame()));
        }
        if (!places.empty()) {
            Writer again(Kind::Breakpoints);
            detail::put(again, places);
            static_cast<void>(out->send(again.frame()));
        }
        return true;
    }
    why = !alive  ? std::string("it stopped before it was ready")
          : ready ? (problem.empty() ? std::string("its script engine did not start") : problem)
                  : std::string("it did not answer within 10 seconds");
    if (process != nullptr) {
        TerminateProcess(process, kKilledExit);
    }
    lock.unlock();
    if (reader.joinable()) {
        reader.join();
    }
    release();
    return false;
}

void RemoteLogicHost::Impl::deliver(Outgoing& next, const std::shared_ptr<detail::PipeWriter>& pipe)
{
    if (!next.load) {
        if (pipe != nullptr) {
            static_cast<void>(pipe->send(next.frame));
        }
        return;
    }
    detail::LoadMessage& message = *next.load;
    message.source.transform = nullptr;
    if (message.source.files == nullptr) {
        auto files = std::make_shared<script::ScriptFiles>();
        std::string why;
        if (!packFolder(message.source.folder, message.source.components, *files, why)) {
            fail(message.script, message.source.entry, why);
            return;
        }
        message.source.files = std::move(files);
    }
    bool gone = false;
    {
        const std::scoped_lock lock(mutex);
        gone = state != State::Ready || pipe == nullptr;
        if (!gone) {
            loaded.insert(message.script);
        }
    }
    if (gone) {
        fail(message.script, message.source.entry, std::string(kStoppedText));
        return;
    }
    Writer out(Kind::Load);
    detail::put(out, message);
    if (pipe->send(out.frame())) {
        return;
    }
    bool mine = false;
    {
        const std::scoped_lock lock(mutex);
        mine = loaded.erase(message.script) > 0;
    }
    if (mine) {
        fail(message.script, message.source.entry, std::string(kStoppedText));
    }
}

void RemoteLogicHost::Impl::post(Outgoing item)
{
    {
        const std::scoped_lock lock(mutex);
        if (closing) {
            return;
        }
        if (state == State::Idle) {
            if (!item.load) {
                return;
            }
            state = State::Wanted;
        }
        queue.push_back(std::move(item));
    }
    changed.notify_all();
}

void RemoteLogicHost::Impl::post(Writer& out)
{
    post(Outgoing{.frame = out.frame(), .load = std::nullopt});
}

void RemoteLogicHost::Impl::readFrom(std::unique_ptr<detail::PipeReader> pipe, std::uint64_t which)
{
    SetThreadDescription(GetCurrentThread(), L"gs-logic-pipe");
    while (std::optional<detail::Frame> frame = pipe->next()) {
        handle(*frame, which);
    }
    pipe.reset();
    ended(which);
}

void RemoteLogicHost::Impl::handle(const detail::Frame& frame, std::uint64_t which)
{
    Reader in(frame.payload);
    switch (frame.kind) {
    case Kind::Ready: {
        bool started = false;
        std::string why;
        if (in.flag(started) && in.text(why)) {
            {
                const std::scoped_lock lock(mutex);
                if (which == generation) {
                    ready = true;
                    childStarted = started;
                    if (!started) {
                        problem = why;
                    }
                }
            }
            changed.notify_all();
        }
        return;
    }
    case Kind::Report: {
        script::Report report;
        if (detail::get(in, report) && in.done()) {
            emit(report);
        }
        return;
    }
    case Kind::Call: {
        std::uint64_t call = 0;
        std::uint16_t method = 0;
        if (in.u64(call) && in.u16(method)) {
            serve(in, call, static_cast<Method>(method));
        }
        return;
    }
    case Kind::Post: {
        std::uint16_t method = 0;
        if (in.u16(method)) {
            serve(in, std::nullopt, static_cast<Method>(method));
        }
        return;
    }
    default:
        return;
    }
}

void RemoteLogicHost::Impl::serve(Reader& in, std::optional<std::uint64_t> call, Method method)
{
    script::ScriptServices* target = services.get();
    Writer out(Kind::Reply);
    out.u64(call.value_or(0));
    switch (method) {
    case Method::Device:
        if (in.done()) {
            detail::put(out, target != nullptr ? target->device() : script::DeviceInfo{});
        }
        break;
    case Method::Tap: {
        std::uint64_t gesture = 0;
        script::Point at;
        double ms = 0.0;
        if (in.u64(gesture) && detail::get(in, at) && in.f64(ms) && in.done() && target != nullptr) {
            target->tap(gesture, at, ms);
        }
        break;
    }
    case Method::Swipe: {
        std::uint64_t gesture = 0;
        script::Point from;
        script::Point to;
        double ms = 0.0;
        if (in.u64(gesture) && detail::get(in, from) && detail::get(in, to) && in.f64(ms) && in.done() &&
            target != nullptr) {
            target->swipe(gesture, from, to, ms);
        }
        break;
    }
    case Method::Lift: {
        std::uint64_t gesture = 0;
        if (in.u64(gesture) && in.done() && target != nullptr) {
            target->lift(gesture);
        }
        break;
    }
    case Method::Key: {
        std::string name;
        bool down = false;
        if (in.text(name) && in.flag(down) && in.done() && target != nullptr) {
            target->key(name, down);
        }
        break;
    }
    case Method::Controls:
        if (in.done()) {
            detail::put(out, target != nullptr ? target->controls() : std::vector<script::ControlInfo>{});
        }
        break;
    case Method::PressControl: {
        std::string id;
        bool down = false;
        if (in.text(id) && in.flag(down) && in.done()) {
            out.flag(target != nullptr && target->pressControl(id, down));
        }
        break;
    }
    case Method::EnableControl: {
        std::string id;
        bool enabled = false;
        if (in.text(id) && in.flag(enabled) && in.done()) {
            out.flag(target != nullptr && target->enableControl(id, enabled));
        }
        break;
    }
    case Method::States:
        if (in.done()) {
            detail::put(out, target != nullptr ? target->states() : std::vector<script::StateInfo>{});
        }
        break;
    case Method::StateValue: {
        std::string id;
        if (in.text(id) && in.done()) {
            detail::put(out, target != nullptr ? target->stateValue(id) : std::nullopt);
        }
        break;
    }
    case Method::SetState: {
        std::string id;
        script::VarValue value;
        if (in.text(id) && detail::get(in, value) && in.done()) {
            out.flag(target != nullptr && target->setState(id, value));
        }
        break;
    }
    case Method::LoadStore: {
        std::string owner;
        if (in.text(owner) && in.done()) {
            detail::put(out, target != nullptr ? target->loadStore(owner) : std::nullopt);
        }
        break;
    }
    case Method::SaveStore: {
        std::string owner;
        std::string contents;
        if (in.text(owner) && in.text(contents) && in.done() && target != nullptr) {
            target->saveStore(owner, contents);
        }
        break;
    }
    case Method::Toast: {
        std::string text;
        if (in.text(text) && in.done() && target != nullptr) {
            target->toast(text);
        }
        break;
    }
    case Method::Ask: {
        std::uint64_t request = 0;
        script::Prompt prompt;
        if (in.u64(request) && detail::get(in, prompt) && in.done() && target != nullptr) {
            target->ask(request, prompt);
        }
        break;
    }
    case Method::Withdraw: {
        std::uint64_t request = 0;
        if (in.u64(request) && in.done() && target != nullptr) {
            target->withdraw(request);
        }
        break;
    }
    case Method::Draw: {
        std::uint64_t id = 0;
        script::Drawing drawing;
        if (in.u64(id) && detail::get(in, drawing) && in.done() && target != nullptr) {
            target->draw(id, drawing);
        }
        break;
    }
    case Method::Erase: {
        std::uint64_t id = 0;
        if (in.u64(id) && in.done() && target != nullptr) {
            target->erase(id);
        }
        break;
    }
    case Method::Find: {
        std::uint64_t request = 0;
        script::FindQuery query;
        if (in.u64(request) && detail::get(in, query) && in.done() && target != nullptr) {
            target->find(request, query);
        }
        break;
    }
    case Method::FindColor: {
        std::uint64_t request = 0;
        script::ColorQuery query;
        if (in.u64(request) && detail::get(in, query) && in.done() && target != nullptr) {
            target->findColor(request, query);
        }
        break;
    }
    case Method::PickColor: {
        std::uint64_t request = 0;
        script::PixelQuery query;
        if (in.u64(request) && detail::get(in, query) && in.done() && target != nullptr) {
            target->pickColor(request, query);
        }
        break;
    }
    case Method::Capture: {
        std::uint64_t request = 0;
        script::CaptureQuery query;
        if (in.u64(request) && detail::get(in, query) && in.done() && target != nullptr) {
            target->capture(request, query);
        }
        break;
    }
    case Method::Changed: {
        std::uint64_t request = 0;
        script::ChangeQuery query;
        if (in.u64(request) && detail::get(in, query) && in.done() && target != nullptr) {
            target->changed(request, query);
        }
        break;
    }
    case Method::ReadText: {
        std::uint64_t request = 0;
        script::TextQuery query;
        if (in.u64(request) && detail::get(in, query) && in.done() && target != nullptr) {
            target->readText(request, query);
        }
        break;
    }
    case Method::SaveCapture: {
        std::uint64_t request = 0;
        std::uint64_t capture = 0;
        std::string name;
        if (in.u64(request) && in.u64(capture) && in.text(name) && in.done() && target != nullptr) {
            target->saveCapture(request, capture, name);
        }
        break;
    }
    case Method::CancelFind: {
        std::uint64_t request = 0;
        if (in.u64(request) && in.done() && target != nullptr) {
            target->cancelFind(request);
        }
        break;
    }
    case Method::Cursor:
        if (in.done()) {
            detail::put(out, target != nullptr ? target->cursor() : std::nullopt);
        }
        break;
    case Method::KeyDown: {
        std::string name;
        if (in.text(name) && in.done()) {
            out.flag(target != nullptr && target->keyDown(name));
        }
        break;
    }
    case Method::ControlAction: {
        std::string id;
        std::string action;
        script::Point at;
        if (in.text(id) && in.text(action) && detail::get(in, at) && in.done()) {
            out.flag(target != nullptr && target->controlAction(id, action, at));
        }
        break;
    }
    case Method::TuneControl: {
        std::string id;
        std::string change;
        if (in.text(id) && in.text(change) && in.done()) {
            detail::put(out, target != nullptr ? target->tuneControl(id, change) : std::nullopt);
        }
        break;
    }
    case Method::SaveProfile: {
        std::uint64_t request = 0;
        if (in.u64(request) && in.done() && target != nullptr) {
            target->saveProfile(request);
        }
        break;
    }
    case Method::ControlStatus: {
        std::string id;
        if (in.text(id) && in.done()) {
            detail::put(out, target != nullptr ? target->controlStatus(id) : std::nullopt);
        }
        break;
    }
    case Method::CameraLocked:
        if (in.done()) {
            out.flag(target != nullptr && target->cameraLocked());
        }
        break;
    case Method::ClaimKey: {
        std::string name;
        if (in.text(name) && in.done()) {
            out.flag(target == nullptr || target->claimKey(name));
        }
        break;
    }
    case Method::HandOver: {
        std::string id;
        bool on = false;
        if (in.text(id) && in.flag(on) && in.done() && target != nullptr) {
            target->handOver(id, on);
        }
        break;
    }
    case Method::Type: {
        std::string text;
        if (in.text(text) && in.done() && target != nullptr) {
            target->type(text);
        }
        break;
    }
    case Method::Hold: {
        std::uint64_t gesture = 0;
        script::Point at;
        if (in.u64(gesture) && detail::get(in, at) && in.done() && target != nullptr) {
            target->hold(gesture, at);
        }
        break;
    }
    case Method::Move: {
        std::uint64_t gesture = 0;
        script::Point to;
        double ms = 0.0;
        if (in.u64(gesture) && detail::get(in, to) && in.f64(ms) && in.done() && target != nullptr) {
            target->move(gesture, to, ms);
        }
        break;
    }
    case Method::LiftAll:
        if (in.done() && target != nullptr) {
            target->liftAll();
        }
        break;
    case Method::GlobalValue: {
        std::string name;
        if (in.text(name) && in.done()) {
            detail::put(out, target != nullptr ? target->globalValue(name) : std::nullopt);
        }
        break;
    }
    case Method::SetGlobal: {
        std::string name;
        std::string json;
        if (in.text(name) && in.text(json) && in.done() && target != nullptr) {
            target->setGlobal(name, json);
        }
        break;
    }
    case Method::App: {
        std::string request;
        std::string json;
        if (in.text(request) && in.text(json) && in.done()) {
            out.text(target != nullptr ? target->app(request, json) : std::string("null"));
        }
        break;
    }
    case Method::Connected:
        if (in.done()) {
            out.flag(target == nullptr || target->connected());
        }
        break;
    case Method::AwaitConnection: {
        std::uint64_t request = 0;
        if (in.u64(request) && in.done() && target != nullptr) {
            target->awaitConnection(request);
        }
        break;
    }
    case Method::ReadClipboard: {
        std::uint64_t request = 0;
        if (in.u64(request) && in.done() && target != nullptr) {
            target->readClipboard(request);
        }
        break;
    }
    case Method::WriteClipboard: {
        std::uint64_t request = 0;
        std::string text;
        if (in.u64(request) && in.text(text) && in.done() && target != nullptr) {
            target->writeClipboard(request, text);
        }
        break;
    }
    case Method::Nodes:
        if (in.done()) {
            detail::put(out, target != nullptr ? target->nodes() : std::vector<script::NodeInfo>{});
        }
        break;
    case Method::CallNode: {
        std::string extension;
        std::string node;
        std::vector<script::NodeValue> arguments;
        if (in.text(extension) && in.text(node) && detail::get(in, arguments) && in.done()) {
            detail::put(out, target != nullptr ? target->callNode(extension, node, arguments)
                                               : script::NodeAnswer{{}, "GS-API-060", "no extension offers this node now"});
        }
        break;
    }
    case Method::OffersService: {
        std::string name;
        if (in.text(name) && in.done()) {
            out.flag(target != nullptr && target->offersService(name));
        }
        break;
    }
    case Method::CallService: {
        std::string name;
        std::string json;
        if (in.text(name) && in.text(json) && in.done()) {
            detail::put(out, target != nullptr ? target->callService(name, json)
                                               : script::ServiceAnswer{"null", "GS-API-070",
                                                                       "no part of GameScrcpy offers this service"});
        }
        break;
    }
    case Method::RequestService: {
        std::uint64_t id = 0;
        std::string name;
        std::string json;
        if (in.u64(id) && in.text(name) && in.text(json) && in.done() && target != nullptr) {
            target->requestService(id, name, json);
        }
        break;
    }
    case Method::DropService: {
        std::uint64_t id = 0;
        if (in.u64(id) && in.done() && target != nullptr) {
            target->dropService(id);
        }
        break;
    }
    default:
        break;
    }
    if (call) {
        send(out);
    }
}

void RemoteLogicHost::Impl::ended(std::uint64_t which)
{
    HANDLE ending = nullptr;
    bool crashed = false;
    bool running = false;
    bool quitting = false;
    std::vector<script::ScriptId> lost;
    {
        const std::scoped_lock lock(mutex);
        if (which != generation) {
            return;
        }
        alive = false;
        ending = process;
        quitting = closing;
        running = state == State::Ready;
        if (running && !closing) {
            crashed = true;
            lost.assign(loaded.begin(), loaded.end());
            loaded.clear();
            const auto next = std::ranges::find_if(queue, [](const Outgoing& item) { return item.load.has_value(); });
            queue.erase(queue.begin(), next);
            state = queue.empty() ? State::Idle : State::Wanted;
        }
    }
    changed.notify_all();
    if (!running) {
        return;
    }
    DWORD code = 0;
    if (ending != nullptr) {
        WaitForSingleObject(ending, quitting ? 0 : kExitWaitMs);
        GetExitCodeProcess(ending, &code);
    }
    if (!crashed) {
        log::info("logic", "{}: logic sandbox ended (exit code {})", options.label, code);
        return;
    }
    log::warning("logic", "{}: logic sandbox stopped unexpectedly (exit code 0x{:X})", options.label, code);
    for (const script::ScriptId script : lost) {
        script::Report report{.kind = script::ReportKind::ScriptFailed, .level = log::Level::Error, .script = script};
        report.message = std::string(kStoppedText);
        emit(report);
    }
}

void RemoteLogicHost::Impl::emit(const script::Report& report)
{
    {
        const std::scoped_lock lock(mutex);
        if (closing) {
            return;
        }
        if (report.kind == script::ReportKind::MemoryLimit) {
            loaded.clear();
        } else if (report.kind == script::ReportKind::ScriptFailed) {
            loaded.erase(report.script);
        }
    }
    if (!sink) {
        return;
    }
    try {
        sink(report);
    } catch (...) {
    }
}

void RemoteLogicHost::Impl::fail(script::ScriptId script, const std::string& entry, std::string text)
{
    script::Report report{.kind = script::ReportKind::ScriptFailed, .level = log::Level::Error, .script = script};
    report.message = std::move(text);
    report.location.module = entry;
    emit(report);
}

bool RemoteLogicHost::Impl::send(Writer& out)
{
    std::shared_ptr<detail::PipeWriter> pipe;
    {
        const std::scoped_lock lock(mutex);
        pipe = writer;
    }
    return pipe != nullptr && pipe->send(out.frame());
}

bool RemoteLogicHost::Impl::stopping() const
{
    const std::scoped_lock lock(mutex);
    return closing;
}

void RemoteLogicHost::Impl::release() noexcept
{
    const std::scoped_lock lock(mutex);
    writer.reset();
    closeHandle(process);
    closeHandle(job);
    alive = false;
    ready = false;
}

RemoteLogicHost::RemoteLogicHost(Sink sink, script::Limits limits, std::shared_ptr<script::ScriptServices> services,
                                 RemoteOptions options)
    : m_impl(std::make_unique<Impl>(std::move(sink), limits, std::move(services), std::move(options)))
{
    m_impl->worker = std::jthread([impl = m_impl.get()] { impl->run(); });
}

RemoteLogicHost::~RemoteLogicHost()
{
    Impl& d = *m_impl;
    {
        const std::scoped_lock lock(d.mutex);
        d.closing = true;
        d.queue.clear();
        if (d.process != nullptr) {
            TerminateProcess(d.process, kKilledExit);
        }
    }
    d.changed.notify_all();
    if (d.worker.joinable()) {
        d.worker.join();
    }
    {
        const std::scoped_lock lock(d.mutex);
        if (d.process != nullptr) {
            TerminateProcess(d.process, kKilledExit);
        }
    }
    if (d.reader.joinable()) {
        d.reader.join();
    }
    d.release();
}

bool RemoteLogicHost::started() const noexcept
{
    const std::scoped_lock lock(m_impl->mutex);
    return !m_impl->broken;
}

std::string RemoteLogicHost::problem() const
{
    const std::scoped_lock lock(m_impl->mutex);
    return m_impl->problem;
}

script::ScriptId RemoteLogicHost::load(script::ScriptSource source)
{
    const script::ScriptId id = m_impl->lastScript.fetch_add(1, std::memory_order_relaxed) + 1;
    m_impl->post(Impl::Outgoing{.frame = {}, .load = detail::LoadMessage{.script = id, .source = std::move(source)}});
    return id;
}

void RemoteLogicHost::unload(script::ScriptId script)
{
    {
        const std::scoped_lock lock(m_impl->mutex);
        m_impl->loaded.erase(script);
    }
    Writer out(Kind::Unload);
    out.u32(script);
    m_impl->post(out);
}

script::TaskId RemoteLogicHost::start(script::ScriptId script, std::string handler)
{
    const script::TaskId id = m_impl->lastTask.fetch_add(1, std::memory_order_relaxed) + 1;
    Writer out(Kind::Start);
    out.u32(script).u64(id).text(handler);
    m_impl->post(out);
    return id;
}

void RemoteLogicHost::cancel(script::TaskId task)
{
    Writer out(Kind::Cancel);
    out.u64(task);
    m_impl->post(out);
}

void RemoteLogicHost::frame()
{
    Writer out(Kind::Frame);
    m_impl->post(out);
}

void RemoteLogicHost::setFrameRate(double hz)
{
    {
        const std::scoped_lock lock(m_impl->mutex);
        m_impl->frameRate = hz;
    }
    Writer out(Kind::FrameRate);
    out.f64(hz);
    m_impl->post(out);
}

void RemoteLogicHost::controlEvent(std::string id, bool down)
{
    Writer out(Kind::ControlEvent);
    out.text(id).flag(down);
    m_impl->post(out);
}

void RemoteLogicHost::stateEvent(std::string id, bool value)
{
    Writer out(Kind::StateEvent);
    out.text(id).flag(value);
    m_impl->post(out);
}

void RemoteLogicHost::event(std::string kind, std::string id, std::string json)
{
    Writer out(Kind::Event);
    out.text(kind).text(id).text(json);
    m_impl->post(out);
}

void RemoteLogicHost::answer(std::uint64_t request, std::string json)
{
    Writer out(Kind::Answer);
    out.u64(request).text(json);
    m_impl->post(out);
}

void RemoteLogicHost::refuse(std::uint64_t request, std::string message)
{
    Writer out(Kind::Refuse);
    out.u64(request).text(message);
    m_impl->post(out);
}

void RemoteLogicHost::setBreakpoints(std::vector<std::string> places)
{
    Writer out(Kind::Breakpoints);
    detail::put(out, places);
    {
        const std::scoped_lock lock(m_impl->mutex);
        m_impl->breakpoints = std::move(places);
    }
    m_impl->post(out);
}

void RemoteLogicHost::resume(std::uint64_t pause, script::StepMode mode)
{
    Writer out(Kind::Resume);
    out.u64(pause).u8(static_cast<std::uint8_t>(mode));
    m_impl->post(out);
}

SandboxMode RemoteLogicHost::mode() const
{
    const std::scoped_lock lock(m_impl->mutex);
    return m_impl->state == Impl::State::Ready ? m_impl->mode : SandboxMode::None;
}

bool RemoteLogicHost::alive() const
{
    const std::scoped_lock lock(m_impl->mutex);
    return m_impl->state == Impl::State::Ready;
}

void RemoteLogicHost::kill()
{
    const std::scoped_lock lock(m_impl->mutex);
    if (m_impl->process != nullptr) {
        TerminateProcess(m_impl->process, kKilledExit);
    }
}

SelfTestResult runSelfTest(const std::filesystem::path& program, const std::filesystem::path& probeFile,
                           std::uint16_t port, bool restrictedOnly)
{
    SelfTestResult result;
    const std::string token = randomToken();
    const std::wstring extra = L"--sandbox-self-test " + std::wstring(token.begin(), token.end());
    for (const SandboxMode wanted : {SandboxMode::AppContainer, SandboxMode::Restricted}) {
        if (restrictedOnly && wanted == SandboxMode::AppContainer) {
            continue;
        }
        std::string why;
        std::optional<detail::Launched> launched = detail::launch(program, wanted, extra, why);
        if (!launched) {
            result.problem = why;
            continue;
        }
        std::optional<detail::Frame> answer;
        {
            detail::PipeWriter pipeOut(std::exchange(launched->toChild, nullptr));
            detail::PipeReader pipeIn(std::exchange(launched->fromChild, nullptr));
            Writer probe(Kind::SelfTest);
            detail::put(probe, detail::SelfTestProbe{.token = token, .file = detail::utf8Of(probeFile), .port = port});
            if (pipeOut.send(probe.frame()) &&
                WaitForSingleObject(launched->process, kSelfTestWaitMs) != WAIT_OBJECT_0) {
                TerminateProcess(launched->process, kKilledExit);
            }
            answer = pipeIn.next();
        }
        WaitForSingleObject(launched->process, kStopWaitMs);
        DWORD exitCode = STILL_ACTIVE;
        GetExitCodeProcess(launched->process, &exitCode);
        detail::closeLaunched(*launched);
        std::vector<detail::SelfTestOutcome> outcomes;
        if (!answer || answer->kind != Kind::SelfTested) {
            result.problem = std::format("gs-scripthost.exe gave no self-test answer in {} (exit code 0x{:08X})",
                                         modeName(wanted), static_cast<std::uint32_t>(exitCode));
            continue;
        }
        Reader in(answer->payload);
        if (!detail::get(in, outcomes) || !in.done()) {
            result.problem = "the self-test answer is damaged";
            continue;
        }
        result = SelfTestResult{.mode = wanted};
        for (const detail::SelfTestOutcome& outcome : outcomes) {
            result.text += std::format("{}: {} (error {}){}{}\n", outcome.name, outcome.refused ? "refused" : "allowed",
                                       outcome.error, outcome.detail.empty() ? "" : ", ", outcome.detail);
            if (outcome.name == "file") {
                result.file = outcome.refused;
            } else if (outcome.name == "socket") {
                result.socket = outcome.refused;
            } else if (outcome.name == "process") {
                result.process = outcome.refused;
            }
        }
        return result;
    }
    return result;
}

}
