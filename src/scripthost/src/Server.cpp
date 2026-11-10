#include <winsock2.h>
#include <ws2tcpip.h>

#include "Server.h"

#include "Pipe.h"
#include "Wire.h"

#include <gs/script/ScriptHost.h>
#include <gs/script/Services.h>

#include <array>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::scripthost {

namespace {

using detail::Kind;
using detail::Method;
using detail::Reader;
using detail::Writer;

enum Exit : int { kDone = 0, kUsage = 2, kBroken = 3 };

constexpr std::size_t kMostTokenChars = 64;
constexpr std::size_t kLeastTokenChars = 32;
constexpr long kSocketWaitSeconds = 2;

class Link
{
public:
    explicit Link(detail::PipeWriter& writer) noexcept
        : m_writer(writer)
    {
    }

    bool send(Writer& out) { return m_writer.send(out.frame()); }

    template <typename Args>
    void post(Method method, Args&& args)
    {
        Writer out(Kind::Post);
        out.u16(static_cast<std::uint16_t>(method));
        args(out);
        static_cast<void>(m_writer.send(out.frame()));
    }

    template <typename Args>
    std::optional<std::string> call(Method method, Args&& args)
    {
        std::uint64_t id = 0;
        {
            const std::scoped_lock lock(m_mutex);
            if (m_closed) {
                return std::nullopt;
            }
            id = ++m_lastCall;
        }
        Writer out(Kind::Call);
        out.u64(id).u16(static_cast<std::uint16_t>(method));
        args(out);
        if (!m_writer.send(out.frame())) {
            return std::nullopt;
        }
        std::unique_lock lock(m_mutex);
        m_changed.wait(lock, [this, id] { return m_closed || m_replies.contains(id); });
        const auto found = m_replies.find(id);
        if (found == m_replies.end()) {
            return std::nullopt;
        }
        std::string payload = std::move(found->second);
        m_replies.erase(found);
        return payload;
    }

    void reply(std::uint64_t id, std::string payload)
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_replies.insert_or_assign(id, std::move(payload));
        }
        m_changed.notify_all();
    }

    void close()
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_closed = true;
            m_replies.clear();
        }
        m_changed.notify_all();
    }

private:
    detail::PipeWriter& m_writer;
    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::map<std::uint64_t, std::string> m_replies;
    std::uint64_t m_lastCall = 0;
    bool m_closed = false;
};

class ProxyServices final : public script::ScriptServices
{
public:
    explicit ProxyServices(Link& link) noexcept
        : m_link(link)
    {
    }

    script::DeviceInfo device() override
    {
        script::DeviceInfo info;
        fetch(Method::Device, [](Writer&) {}, info);
        return info;
    }

    void tap(std::uint64_t gesture, script::Point at, double durationMs) override
    {
        m_link.post(Method::Tap, [&](Writer& out) {
            out.u64(gesture);
            detail::put(out, at);
            out.f64(durationMs);
        });
    }

    void swipe(std::uint64_t gesture, script::Point from, script::Point to, double durationMs) override
    {
        m_link.post(Method::Swipe, [&](Writer& out) {
            out.u64(gesture);
            detail::put(out, from);
            detail::put(out, to);
            out.f64(durationMs);
        });
    }

    void lift(std::uint64_t gesture) override
    {
        m_link.post(Method::Lift, [&](Writer& out) { out.u64(gesture); });
    }

    void key(std::string_view name, bool down) override
    {
        m_link.post(Method::Key, [&](Writer& out) { out.text(name).flag(down); });
    }

    std::vector<script::ControlInfo> controls() override
    {
        std::vector<script::ControlInfo> list;
        fetch(Method::Controls, [](Writer&) {}, list);
        return list;
    }

    bool pressControl(std::string_view id, bool down) override
    {
        return askFlag(Method::PressControl, [&](Writer& out) { out.text(id).flag(down); }, false);
    }

    bool enableControl(std::string_view id, bool enabled) override
    {
        return askFlag(Method::EnableControl, [&](Writer& out) { out.text(id).flag(enabled); }, false);
    }

    std::vector<script::StateInfo> states() override
    {
        std::vector<script::StateInfo> list;
        fetch(Method::States, [](Writer&) {}, list);
        return list;
    }

    std::optional<script::VarValue> stateValue(std::string_view id) override
    {
        std::optional<script::VarValue> value;
        fetch(Method::StateValue, [&](Writer& out) { out.text(id); }, value);
        return value;
    }

    bool setState(std::string_view id, const script::VarValue& value) override
    {
        return askFlag(Method::SetState, [&](Writer& out) {
            out.text(id);
            detail::put(out, value);
        }, false);
    }

    std::optional<std::string> loadStore(std::string_view owner) override
    {
        std::optional<std::string> contents;
        fetch(Method::LoadStore, [&](Writer& out) { out.text(owner); }, contents);
        return contents;
    }

    void saveStore(std::string_view owner, std::string_view contents) override
    {
        m_link.post(Method::SaveStore, [&](Writer& out) { out.text(owner).text(contents); });
    }

    void toast(std::string_view text) override
    {
        m_link.post(Method::Toast, [&](Writer& out) { out.text(text); });
    }

    void ask(std::uint64_t request, const script::Prompt& prompt) override
    {
        m_link.post(Method::Ask, [&](Writer& out) {
            out.u64(request);
            detail::put(out, prompt);
        });
    }

    void withdraw(std::uint64_t request) override
    {
        m_link.post(Method::Withdraw, [&](Writer& out) { out.u64(request); });
    }

    void draw(std::uint64_t id, const script::Drawing& drawing) override
    {
        m_link.post(Method::Draw, [&](Writer& out) {
            out.u64(id);
            detail::put(out, drawing);
        });
    }

    void erase(std::uint64_t id) override
    {
        m_link.post(Method::Erase, [&](Writer& out) { out.u64(id); });
    }

    void find(std::uint64_t request, const script::FindQuery& query) override
    {
        m_link.post(Method::Find, [&](Writer& out) {
            out.u64(request);
            detail::put(out, query);
        });
    }

    void findColor(std::uint64_t request, const script::ColorQuery& query) override
    {
        m_link.post(Method::FindColor, [&](Writer& out) {
            out.u64(request);
            detail::put(out, query);
        });
    }

    void pickColor(std::uint64_t request, const script::PixelQuery& query) override
    {
        m_link.post(Method::PickColor, [&](Writer& out) {
            out.u64(request);
            detail::put(out, query);
        });
    }

    void capture(std::uint64_t request, const script::CaptureQuery& query) override
    {
        m_link.post(Method::Capture, [&](Writer& out) {
            out.u64(request);
            detail::put(out, query);
        });
    }

    void changed(std::uint64_t request, const script::ChangeQuery& query) override
    {
        m_link.post(Method::Changed, [&](Writer& out) {
            out.u64(request);
            detail::put(out, query);
        });
    }

    void readText(std::uint64_t request, const script::TextQuery& query) override
    {
        m_link.post(Method::ReadText, [&](Writer& out) {
            out.u64(request);
            detail::put(out, query);
        });
    }

    void saveCapture(std::uint64_t request, std::uint64_t capture, std::string_view name) override
    {
        m_link.post(Method::SaveCapture, [&](Writer& out) { out.u64(request).u64(capture).text(name); });
    }

    void cancelFind(std::uint64_t request) override
    {
        m_link.post(Method::CancelFind, [&](Writer& out) { out.u64(request); });
    }

    std::optional<script::Point> cursor() override
    {
        std::optional<script::Point> at;
        fetch(Method::Cursor, [](Writer&) {}, at);
        return at;
    }

    bool keyDown(std::string_view name) override
    {
        return askFlag(Method::KeyDown, [&](Writer& out) { out.text(name); }, false);
    }

    bool controlAction(std::string_view id, std::string_view action, script::Point at) override
    {
        return askFlag(Method::ControlAction, [&](Writer& out) {
            out.text(id).text(action);
            detail::put(out, at);
        }, false);
    }

    std::optional<std::string> tuneControl(std::string_view id, std::string_view change) override
    {
        std::optional<std::string> settings;
        fetch(Method::TuneControl, [&](Writer& out) { out.text(id).text(change); }, settings);
        return settings;
    }

    void saveProfile(std::uint64_t request) override
    {
        m_link.post(Method::SaveProfile, [&](Writer& out) { out.u64(request); });
    }

    std::optional<script::ControlStatus> controlStatus(std::string_view id) override
    {
        std::optional<script::ControlStatus> status;
        fetch(Method::ControlStatus, [&](Writer& out) { out.text(id); }, status);
        return status;
    }

    bool cameraLocked() override
    {
        return askFlag(Method::CameraLocked, [](Writer&) {}, false);
    }

    bool claimKey(std::string_view name) override
    {
        return askFlag(Method::ClaimKey, [&](Writer& out) { out.text(name); }, false);
    }

    void handOver(std::string_view id, bool on) override
    {
        m_link.post(Method::HandOver, [&](Writer& out) {
            out.text(id);
            out.flag(on);
        });
    }

    void type(std::string_view text) override
    {
        m_link.post(Method::Type, [&](Writer& out) { out.text(text); });
    }

    void hold(std::uint64_t gesture, script::Point at) override
    {
        m_link.post(Method::Hold, [&](Writer& out) {
            out.u64(gesture);
            detail::put(out, at);
        });
    }

    void move(std::uint64_t gesture, script::Point to, double durationMs) override
    {
        m_link.post(Method::Move, [&](Writer& out) {
            out.u64(gesture);
            detail::put(out, to);
            out.f64(durationMs);
        });
    }

    void liftAll() override
    {
        m_link.post(Method::LiftAll, [](Writer&) {});
    }

    std::optional<std::string> globalValue(std::string_view name) override
    {
        std::optional<std::string> json;
        fetch(Method::GlobalValue, [&](Writer& out) { out.text(name); }, json);
        return json;
    }

    void setGlobal(std::string_view name, std::string_view json) override
    {
        m_link.post(Method::SetGlobal, [&](Writer& out) { out.text(name).text(json); });
    }

    std::string app(std::string_view request, std::string_view json) override
    {
        std::string answer = "null";
        fetch(Method::App, [&](Writer& out) { out.text(request).text(json); }, answer);
        return answer;
    }

    bool connected() override
    {
        return askFlag(Method::Connected, [](Writer&) {}, true);
    }

    void awaitConnection(std::uint64_t request) override
    {
        m_link.post(Method::AwaitConnection, [&](Writer& out) { out.u64(request); });
    }

    void readClipboard(std::uint64_t request) override
    {
        m_link.post(Method::ReadClipboard, [&](Writer& out) { out.u64(request); });
    }

    void writeClipboard(std::uint64_t request, std::string_view text) override
    {
        m_link.post(Method::WriteClipboard, [&](Writer& out) { out.u64(request).text(text); });
    }

    std::vector<script::NodeInfo> nodes() override
    {
        std::vector<script::NodeInfo> list;
        fetch(Method::Nodes, [](Writer&) {}, list);
        return list;
    }

    script::NodeAnswer callNode(std::string_view extension, std::string_view node,
                                const std::vector<script::NodeValue>& arguments) override
    {
        script::NodeAnswer answer{{}, "GS-API-060", "no extension offers this node now"};
        fetch(Method::CallNode, [&](Writer& out) {
            out.text(extension).text(node);
            detail::put(out, arguments);
        }, answer);
        return answer;
    }

    bool offersService(std::string_view name) override
    {
        return askFlag(Method::OffersService, [&](Writer& out) { out.text(name); }, false);
    }

    script::ServiceAnswer callService(std::string_view name, std::string_view json) override
    {
        script::ServiceAnswer answer{"null", "GS-API-070", "no part of GameScrcpy offers this service"};
        fetch(Method::CallService, [&](Writer& out) { out.text(name).text(json); }, answer);
        return answer;
    }

    void requestService(std::uint64_t id, std::string_view name, std::string_view json) override
    {
        m_link.post(Method::RequestService, [&](Writer& out) { out.u64(id).text(name).text(json); });
    }

    void dropService(std::uint64_t id) override
    {
        m_link.post(Method::DropService, [&](Writer& out) { out.u64(id); });
    }

private:
    template <typename Args, typename Result>
    void fetch(Method method, Args&& args, Result& result)
    {
        const std::optional<std::string> payload = m_link.call(method, std::forward<Args>(args));
        if (!payload) {
            return;
        }
        Reader in(*payload);
        Result read{};
        if (detail::get(in, read) && in.done()) {
            result = std::move(read);
        }
    }

    template <typename Args>
    bool askFlag(Method method, Args&& args, bool fallback)
    {
        const std::optional<std::string> payload = m_link.call(method, std::forward<Args>(args));
        if (!payload) {
            return fallback;
        }
        Reader in(*payload);
        bool value = fallback;
        return in.flag(value) && in.done() ? value : fallback;
    }

    Link& m_link;
};

struct Ids {
    std::mutex mutex;
    std::map<script::ScriptId, script::ScriptId> scripts;
    std::map<script::ScriptId, script::ScriptId> mainScripts;
    std::map<script::TaskId, script::TaskId> tasks;
    std::map<script::TaskId, script::TaskId> mainTasks;
};

bool taskEnd(script::ReportKind kind) noexcept
{
    return kind == script::ReportKind::TaskFinished || kind == script::ReportKind::TaskCancelled ||
           kind == script::ReportKind::TaskFailed || kind == script::ReportKind::TaskInterrupted;
}

detail::SelfTestOutcome tryFile(const std::string& path)
{
    const std::filesystem::path file = detail::pathOf(path);
    const HANDLE opened = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (opened == INVALID_HANDLE_VALUE) {
        return {"file", true, GetLastError(), "the file under the user's profile could not be opened"};
    }
    CloseHandle(opened);
    return {"file", false, 0, "the file under the user's profile was opened"};
}

detail::SelfTestOutcome trySocket(std::uint16_t port)
{
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return {"socket", true, static_cast<std::uint32_t>(WSAGetLastError()), "the network could not be started"};
    }
    const SOCKET handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (handle == INVALID_SOCKET) {
        const int error = WSAGetLastError();
        WSACleanup();
        return {"socket", true, static_cast<std::uint32_t>(error), "no socket could be made"};
    }
    u_long nonBlocking = 1;
    ioctlsocket(handle, FIONBIO, &nonBlocking);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool connected =
        connect(handle, reinterpret_cast<const sockaddr*>(&address), static_cast<int>(sizeof(address))) == 0;
    int error = connected ? 0 : WSAGetLastError();
    if (!connected && error == WSAEWOULDBLOCK) {
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(handle, &writable);
        fd_set failed;
        FD_ZERO(&failed);
        FD_SET(handle, &failed);
        timeval wait{kSocketWaitSeconds, 0};
        const int ready = select(0, nullptr, &writable, &failed, &wait);
        if (ready > 0 && FD_ISSET(handle, &writable)) {
            connected = true;
            error = 0;
        } else if (ready > 0) {
            int code = 0;
            int size = static_cast<int>(sizeof(code));
            getsockopt(handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&code), &size);
            error = code;
        } else {
            error = ready == 0 ? WSAETIMEDOUT : WSAGetLastError();
        }
    }
    closesocket(handle);
    WSACleanup();
    return {"socket", !connected, static_cast<std::uint32_t>(error),
            connected ? "a TCP connection to this computer was made" : "no TCP connection could be made"};
}

detail::SelfTestOutcome tryProcess()
{
    std::array<wchar_t, MAX_PATH> folder{};
    const UINT length = GetSystemDirectoryW(folder.data(), static_cast<UINT>(folder.size()));
    const std::wstring program = std::wstring(folder.data(), length) + L"\\cmd.exe";
    std::wstring command = L"\"" + program + L"\" /c exit 0";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION started{};
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &startup, &started)) {
        return {"process", true, GetLastError(), "no other program could be started"};
    }
    TerminateProcess(started.hProcess, 0);
    CloseHandle(started.hThread);
    CloseHandle(started.hProcess);
    return {"process", false, 0, "another program was started"};
}

int selfTest(detail::PipeReader& in, detail::PipeWriter& out, std::wstring_view token)
{
    const std::optional<detail::Frame> frame = in.next();
    if (!frame || frame->kind != Kind::SelfTest) {
        return kBroken;
    }
    detail::SelfTestProbe probe;
    Reader reader(frame->payload);
    if (!detail::get(reader, probe) || !reader.done() || probe.token.size() < kLeastTokenChars ||
        probe.token.size() > kMostTokenChars || std::wstring(probe.token.begin(), probe.token.end()) != token) {
        return kUsage;
    }
    const std::vector<detail::SelfTestOutcome> outcomes{tryFile(probe.file), trySocket(probe.port), tryProcess()};
    Writer answer(Kind::SelfTested);
    detail::put(answer, outcomes);
    return out.send(answer.frame()) ? kDone : kBroken;
}

void dispatch(const detail::Frame& frame, script::ScriptHost& host, Ids& ids, Link& link)
{
    Reader in(frame.payload);
    switch (frame.kind) {
    case Kind::Reply: {
        std::uint64_t call = 0;
        if (in.u64(call)) {
            link.reply(call, frame.payload.substr(sizeof(call)));
        }
        return;
    }
    case Kind::Load: {
        detail::LoadMessage message;
        if (detail::get(in, message) && in.done()) {
            const std::scoped_lock lock(ids.mutex);
            const script::ScriptId local = host.load(std::move(message.source));
            ids.scripts.insert_or_assign(message.script, local);
            ids.mainScripts.insert_or_assign(local, message.script);
        }
        return;
    }
    case Kind::Unload: {
        script::ScriptId script = 0;
        if (in.u32(script) && in.done()) {
            const std::scoped_lock lock(ids.mutex);
            if (const auto found = ids.scripts.find(script); found != ids.scripts.end()) {
                host.unload(found->second);
                ids.scripts.erase(found);
            }
        }
        return;
    }
    case Kind::Start: {
        script::ScriptId script = 0;
        script::TaskId task = 0;
        std::string handler;
        if (in.u32(script) && in.u64(task) && in.text(handler) && in.done()) {
            const std::scoped_lock lock(ids.mutex);
            const auto found = ids.scripts.find(script);
            const script::TaskId local = host.start(found != ids.scripts.end() ? found->second : 0, std::move(handler));
            ids.tasks.insert_or_assign(task, local);
            ids.mainTasks.insert_or_assign(local, task);
        }
        return;
    }
    case Kind::Cancel: {
        script::TaskId task = 0;
        if (in.u64(task) && in.done()) {
            const std::scoped_lock lock(ids.mutex);
            if (const auto found = ids.tasks.find(task); found != ids.tasks.end()) {
                host.cancel(found->second);
            }
        }
        return;
    }
    case Kind::Frame:
        host.frame();
        return;
    case Kind::FrameRate: {
        double hz = 0.0;
        if (in.f64(hz) && in.done()) {
            host.setFrameRate(hz);
        }
        return;
    }
    case Kind::ControlEvent: {
        std::string id;
        bool down = false;
        if (in.text(id) && in.flag(down) && in.done()) {
            host.controlEvent(std::move(id), down);
        }
        return;
    }
    case Kind::StateEvent: {
        std::string id;
        bool value = false;
        if (in.text(id) && in.flag(value) && in.done()) {
            host.stateEvent(std::move(id), value);
        }
        return;
    }
    case Kind::Event: {
        std::string kind;
        std::string id;
        std::string json;
        if (in.text(kind) && in.text(id) && in.text(json) && in.done()) {
            host.event(std::move(kind), std::move(id), std::move(json));
        }
        return;
    }
    case Kind::Answer: {
        std::uint64_t request = 0;
        std::string json;
        if (in.u64(request) && in.text(json) && in.done()) {
            host.answer(request, std::move(json));
        }
        return;
    }
    case Kind::Refuse: {
        std::uint64_t request = 0;
        std::string message;
        if (in.u64(request) && in.text(message) && in.done()) {
            host.refuse(request, std::move(message));
        }
        return;
    }
    case Kind::Breakpoints: {
        std::vector<std::string> places;
        if (detail::get(in, places) && in.done()) {
            host.setBreakpoints(std::move(places));
        }
        return;
    }
    case Kind::Resume: {
        std::uint64_t pause = 0;
        std::uint8_t mode = 0;
        if (in.u64(pause) && in.u8(mode) && in.done() && mode <= static_cast<std::uint8_t>(script::StepMode::Out)) {
            host.resume(pause, static_cast<script::StepMode>(mode));
        }
        return;
    }
    default:
        return;
    }
}

}

int serve(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetThreadDescription(GetCurrentThread(), L"gs-scripthost");
    std::wstring_view channel;
    std::wstring_view token;
    for (int index = 1; index + 1 < argc; index += 2) {
        const std::wstring_view name(argv[index]);
        if (name == L"--channel") {
            channel = argv[index + 1];
        } else if (name == L"--sandbox-self-test") {
            token = argv[index + 1];
        }
    }
    HANDLE incoming = nullptr;
    HANDLE outgoing = nullptr;
    if (!detail::handlesOf(channel, incoming, outgoing)) {
        return kUsage;
    }
    detail::PipeReader in(incoming);
    detail::PipeWriter out(outgoing);
    if (!token.empty()) {
        return selfTest(in, out, token);
    }
    const std::optional<detail::Frame> first = in.next();
    detail::HelloMessage hello;
    if (!first || first->kind != Kind::Hello) {
        return kBroken;
    }
    Reader greeting(first->payload);
    if (!detail::get(greeting, hello) || !greeting.done()) {
        return kBroken;
    }
    Link link(out);
    Ids ids;
    const auto sink = [&ids, &link](const script::Report& report) {
        script::Report shown = report;
        {
            const std::scoped_lock lock(ids.mutex);
            if (const auto found = ids.mainScripts.find(report.script); found != ids.mainScripts.end()) {
                shown.script = found->second;
            }
            if (const auto found = ids.mainTasks.find(report.task); found != ids.mainTasks.end()) {
                shown.task = found->second;
                if (taskEnd(report.kind)) {
                    ids.tasks.erase(found->second);
                    ids.mainTasks.erase(found);
                }
            }
        }
        Writer frame(Kind::Report);
        detail::put(frame, shown);
        static_cast<void>(link.send(frame));
    };
    std::shared_ptr<ProxyServices> proxy = hello.services ? std::make_shared<ProxyServices>(link) : nullptr;
    auto host = std::make_unique<script::ScriptHost>(sink, hello.limits, proxy);
    {
        Writer ready(Kind::Ready);
        ready.flag(host->started()).text(host->problem());
        if (!link.send(ready)) {
            link.close();
            return kBroken;
        }
    }
    int status = kDone;
    for (;;) {
        const std::optional<detail::Frame> frame = in.next();
        if (!frame) {
            status = kBroken;
            break;
        }
        if (frame->kind == Kind::Stop) {
            break;
        }
        dispatch(*frame, *host, ids, link);
    }
    link.close();
    host.reset();
    return status;
}

}
