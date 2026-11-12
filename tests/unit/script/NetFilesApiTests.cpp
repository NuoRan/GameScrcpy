#include "ScriptFixture.h"

#include "gs/script/ApiFiles.h"
#include "gs/script/Permissions.h"
#include "gs/script/Services.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using gs::script::ControlInfo;
using gs::script::DeviceInfo;
using gs::script::Point;
using gs::script::Report;
using gs::script::ReportKind;
using gs::script::ScriptHost;
using gs::script::ScriptId;
using gs::script::ServiceAnswer;
using gs::script::StateInfo;
using gs::script::TaskId;
using gs::script::test::loadScript;
using gs::script::test::logged;
using gs::script::test::Recorder;
using gs::script::test::ScriptFolder;
using gs::script::test::taskEnded;

namespace {

struct Asked {
    std::uint64_t id = 0;
    std::string service;
    std::string json;
};

class NetFilesServices final : public gs::script::ScriptServices
{
public:
    DeviceInfo device() override { return {"Pixel", 15, 1080, 2400, 0}; }
    void tap(std::uint64_t, Point, double) override {}
    void swipe(std::uint64_t, Point, Point, double) override {}
    void key(std::string_view, bool) override {}
    std::vector<ControlInfo> controls() override { return {}; }
    bool pressControl(std::string_view, bool) override { return false; }
    bool enableControl(std::string_view, bool) override { return false; }
    std::vector<StateInfo> states() override { return {}; }
    std::optional<gs::script::VarValue> stateValue(std::string_view) override { return std::nullopt; }
    bool setState(std::string_view, const gs::script::VarValue&) override { return false; }
    std::optional<std::string> loadStore(std::string_view) override { return std::nullopt; }
    void saveStore(std::string_view, std::string_view) override {}

    bool offersService(std::string_view name) override
    {
        return name.starts_with("net.") || name.starts_with("files.");
    }

    ServiceAnswer callService(std::string_view name, std::string_view json) override
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_calls.push_back(std::string(name) + " " + std::string(json));
        }
        m_changed.notify_all();
        return {"null", {}, {}};
    }

    void requestService(std::uint64_t id, std::string_view name, std::string_view json) override
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_asked.push_back({id, std::string(name), std::string(json)});
        }
        m_changed.notify_all();
    }

    void dropService(std::uint64_t) override {}

    std::optional<Asked> waitForAsk(std::size_t index)
    {
        std::unique_lock lock(m_mutex);
        if (!m_changed.wait_for(lock, std::chrono::seconds(30), [&] { return m_asked.size() > index; })) {
            return std::nullopt;
        }
        return m_asked[index];
    }

    bool waitForCalls(std::size_t count)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, std::chrono::seconds(30), [&] { return m_calls.size() >= count; });
    }

    std::vector<std::string> calls() const
    {
        const std::scoped_lock lock(m_mutex);
        return m_calls;
    }

    std::vector<std::string> callsTo(std::string_view service) const
    {
        const std::scoped_lock lock(m_mutex);
        std::vector<std::string> found;
        const std::string prefix = std::string(service) + " ";
        for (const std::string& call : m_calls) {
            if (call.starts_with(prefix)) {
                found.push_back(call.substr(prefix.size()));
            }
        }
        return found;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<Asked> m_asked;
    std::vector<std::string> m_calls;
};

struct Fixture {
    ScriptFolder folder;
    Recorder recorder;
    std::shared_ptr<NetFilesServices> services = std::make_shared<NetFilesServices>();
    std::unique_ptr<ScriptHost> host = std::make_unique<ScriptHost>(recorder.sink(), gs::script::Limits{}, services);

    ScriptId load(std::string_view main)
    {
        folder.write("main.js", main);
        ScriptId id = 0;
        const auto report = loadScript(*host, recorder, folder.source(), &id);
        REQUIRE(report.has_value());
        INFO(gs::script::describe(*report));
        REQUIRE(report->kind == ReportKind::ScriptLoaded);
        return id;
    }

    Asked asked(std::size_t index, std::string_view service)
    {
        const std::optional<Asked> found = services->waitForAsk(index);
        REQUIRE(found.has_value());
        CHECK(found->service == service);
        return *found;
    }

    Report finish(TaskId task)
    {
        const auto ended = recorder.waitFor(taskEnded(task));
        REQUIRE(ended.has_value());
        return ended->report;
    }
};

std::string valueOf(const std::string& json, std::string_view key)
{
    const std::string marker = "\"" + std::string(key) + "\":\"";
    const std::size_t start = json.find(marker);
    if (start == std::string::npos) {
        return {};
    }
    const std::size_t from = start + marker.size();
    return json.substr(from, json.find('"', from) - from);
}

}

TEST_CASE("ADR-0112: net.fetch asks through the service channel and gives the answer as text, bytes and JSON")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function ask() {
    const reply = await net.fetch("https://api.example.com/scores?day=1", { method: "post",
        headers: { "X-Token": "abc", count: 3 }, body: "{\"score\":10}", timeoutMs: 5000 });
    log.info("reply", reply.status, reply.url, reply.headers["content-type"], reply.text, reply.json().ok,
        Object.isFrozen(reply));
    log.info("bytes", Array.from(reply.bytes).join(","));
    const binary = await net.fetch("https://api.example.com/logo");
    log.info("binary", binary.status, Array.from(binary.bytes).join(","), binary.text.length);
    const marked = await net.fetch("https://api.example.com/marked");
    log.info("marked", marked.status, marked.text, marked.bytes.length);
}
)js");
    const TaskId task = fixture.host->start(script, "ask");
    const Asked first = fixture.asked(0, "net.fetch");
    CHECK(first.json == R"({"url":"https://api.example.com/scores?day=1","method":"POST","headers":{"X-Token":"abc",)"
                        R"("count":"3","content-type":"text/plain; charset=utf-8"},"timeoutMs":5000,)"
                        R"("text":"{\"score\":10}"})");
    fixture.host->answer(first.id, R"({"status":200,"url":"https://api.example.com/scores?day=1",)"
                                   R"("headers":{"content-type":"application/json"},"text":"{\"ok\":true}"})");
    const Asked second = fixture.asked(1, "net.fetch");
    CHECK(second.json == R"({"url":"https://api.example.com/logo","method":"GET","headers":{},"timeoutMs":15000})");
    fixture.host->answer(second.id, R"({"status":200,"url":"https://api.example.com/logo","headers":{},"body":"AP8Q"})");
    const Asked third = fixture.asked(2, "net.fetch");
    fixture.host->answer(third.id,
                         R"({"status":404,"url":"https://api.example.com/marked","headers":{},"text":"\ufeffhi"})");

    const Report ended = fixture.finish(task);
    INFO(gs::script::describe(ended));
    CHECK(ended.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged(
              R"(reply 200 https://api.example.com/scores?day=1 application/json {"ok":true} true true)")) == 1);
    CHECK(fixture.recorder.count(logged("bytes 123,34,111,107,34,58,116,114,117,101,125")) == 1);
    CHECK(fixture.recorder.count(logged("binary 200 0,255,16 3")) == 1);
    CHECK(fixture.recorder.count(logged("marked 404 hi 5")) == 1);
}

TEST_CASE("ADR-0112: net.fetch checks its arguments, and a large body goes ahead in parts")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function send() {
    const codes = [];
    const attempt = (run) => {
        try {
            run();
            codes.push("none");
        } catch (error) {
            codes.push(error.message.split(" ")[0] + ":" + error.name);
        }
    };
    attempt(() => net.fetch(42));
    attempt(() => net.fetch("https://a.example.com", { method: "TRACE" }));
    attempt(() => net.fetch("https://a.example.com", { body: "x" }));
    attempt(() => net.fetch("https://a.example.com", { method: "POST", timeoutMs: 500 }));
    attempt(() => net.fetch("https://a.example.com", { headers: [] }));
    attempt(() => net.fetch("https://a.example.com", { method: "PUT", body: { x: 1 } }));
    attempt(() => net.fetch("https://a.example.com", { method: "PUT", body: new Uint8Array(1048577) }));
    log.info("codes", codes.join(" "));
    const big = new Uint8Array(1000000);
    big[0] = 7;
    big[999999] = 9;
    const reply = await net.fetch("https://a.example.com/up", { method: "PUT", body: big });
    log.info("sent", reply.status);
}
)js");
    const TaskId task = fixture.host->start(script, "send");
    const Asked upload = fixture.asked(0, "net.fetch");
    const std::vector<std::string> parts = fixture.services->callsTo("net.fetch.part");
    REQUIRE(parts.size() == 2);
    CHECK(parts[0].starts_with(R"({"stage":1,"data":"Bw)"));
    CHECK(valueOf(parts[0], "data").size() == 524288);
    CHECK(valueOf(parts[1], "data").size() == 524288);
    CHECK(upload.json.starts_with(R"({"url":"https://a.example.com/up","method":"PUT","headers":{},"timeoutMs":15000,)"
                                  R"("stage":1,"data":")"));
    const std::string last = valueOf(upload.json, "data");
    CHECK(last.size() == 284760);
    CHECK(last.ends_with("CQ=="));
    fixture.host->answer(upload.id, R"({"status":201,"url":"https://a.example.com/up","headers":{},"text":""})");

    CHECK(fixture.finish(task).kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("codes GS-API-156:TypeError GS-API-156:TypeError GS-API-156:TypeError "
                                        "GS-API-156:RangeError GS-API-156:TypeError GS-API-156:TypeError "
                                        "GS-API-157:RangeError")) == 1);
    CHECK(fixture.recorder.count(logged("sent 201")) == 1);
}

TEST_CASE("ADR-0112: net.socket opens through the service channel, sends text and bytes, hears messages and closes")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function talk() {
    try {
        await net.socket("wss://live.example.com/feed", { protocols: ["bad name"] });
    } catch (error) {
        log.info("protocol", error.message.split(" ")[0], error.name);
    }
    const live = await net.socket("wss://live.example.com/feed", { protocols: ["chat", "v2"] });
    log.info("open", live.url, live.protocol, live.isOpen, Object.prototype.toString.call(live), Object.isFrozen(live));
    live.onMessage(({ data, binary }) => log.info("message", binary, binary ? Array.from(data).join(",") : data));
    live.onClose(({ code, reason, clean }) => {
        log.info("closed", code, reason, clean, live.isOpen);
        try {
            live.send("late");
        } catch (error) {
            log.info("late", error.message.split(" ")[0], error.name);
        }
    });
    live.send("hi");
    live.send(new Uint8Array([1, 2, 3]));
    try {
        live.close(1001);
    } catch (error) {
        log.info("code", error.message.split(" ")[0], error.name);
    }
    const other = await net.socket("wss://live.example.com/other");
    other.close(4000, "done");
    log.info("other", other.isOpen);
    const third = await net.socket("wss://live.example.com/third");
    log.info("ready", third.isOpen);
    await sleep(30000);
}
)js");
    const TaskId task = fixture.host->start(script, "talk");
    const Asked first = fixture.asked(0, "net.socket");
    CHECK(first.json == R"({"url":"wss://live.example.com/feed","protocols":["chat","v2"]})");
    fixture.host->answer(first.id, R"({"handle":"w1","url":"wss://live.example.com/feed","protocol":"chat"})");
    const Asked second = fixture.asked(1, "net.socket");
    CHECK(second.json == R"({"url":"wss://live.example.com/other","protocols":[]})");
    fixture.host->answer(second.id, R"({"handle":"w2","url":"wss://live.example.com/other","protocol":""})");
    const Asked third = fixture.asked(2, "net.socket");
    fixture.host->answer(third.id, R"({"handle":"w3","url":"wss://live.example.com/third","protocol":""})");
    REQUIRE(fixture.recorder.waitFor(logged("ready true")).has_value());
    CHECK(fixture.recorder.count(logged("protocol GS-API-156 TypeError")) == 1);
    CHECK(fixture.recorder.count(logged("open wss://live.example.com/feed chat true [object NetSocket] true")) == 1);
    CHECK(fixture.recorder.count(logged("code GS-API-156 RangeError")) == 1);
    CHECK(fixture.recorder.count(logged("other false")) == 1);
    CHECK(fixture.services->callsTo("net.socket.send") ==
          std::vector<std::string>{R"({"handle":"w1","binary":false,"text":"hi"})",
                                   R"({"handle":"w1","binary":true,"data":"AQID"})"});
    CHECK(fixture.services->callsTo("net.socket.close") ==
          std::vector<std::string>{R"({"handle":"w2","code":4000,"reason":"done"})"});

    fixture.host->event("netSocket", "w1", R"({"type":"message","binary":false,"text":"hello"})");
    REQUIRE(fixture.recorder.waitFor(logged("message false hello")).has_value());
    fixture.host->event("netSocket", "w1", R"({"type":"message","binary":true,"data":"BAUG"})");
    REQUIRE(fixture.recorder.waitFor(logged("message true 4,5,6")).has_value());
    fixture.host->event("netSocket", "w2", R"({"type":"message","binary":false,"text":"ignored"})");
    fixture.host->event("netSocket", "w1", R"({"type":"closed","code":1000,"reason":"bye","clean":true})");
    REQUIRE(fixture.recorder.waitFor(logged("closed 1000 bye true false")).has_value());
    REQUIRE(fixture.recorder.waitFor(logged("late GS-API-159 RangeError")).has_value());
    fixture.host->event("netSocket", "w1", R"({"type":"message","binary":false,"text":"after"})");

    fixture.host->cancel(task);
    CHECK(fixture.finish(task).kind == ReportKind::TaskCancelled);
    const std::vector<std::string> closes{R"({"handle":"w2","code":4000,"reason":"done"})", R"({"handle":"w3"})"};
    for (int round = 0; round < 3000 && fixture.services->callsTo("net.socket.close") != closes; ++round) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(fixture.services->callsTo("net.socket.close") == closes);
    CHECK(fixture.recorder.count(logged("message false ignored")) == 0);
    CHECK(fixture.recorder.count(logged("message false after")) == 0);
}

TEST_CASE("ADR-0112: files.pick and files.folder give handles that read in pieces, write in parts and check paths")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function use() {
    log.info("none", await files.pick());
    const file = await files.pick({ save: true, filters: [{ name: "Notes", extensions: ["txt", "md"] }],
        suggestedName: "notes.txt" });
    log.info("file", file.name, file.isOpen, Object.prototype.toString.call(file), Object.isFrozen(file));
    log.info("bytes", Array.from(await file.read()).join(","));
    log.info("text", await file.text());
    await file.write("héllo");
    await file.write(new Uint8Array([9, 8]));
    await file.write("x".repeat(200000));
    file.close();
    log.info("closed", file.isOpen);
    try {
        file.read();
    } catch (error) {
        log.info("after", error.message.split(" ")[0], error.name);
    }
    const folder = await files.folder("exports");
    log.info("folder", folder.name, Object.prototype.toString.call(folder));
    const entries = await folder.list("saves/");
    log.info("entries", entries.length, entries[0].path, entries[0].folder, entries[0].size, Object.isFrozen(entries[0]));
    log.info("read", await folder.text("saves\\a.txt"));
    await folder.write("saves/b.txt", "b");
    log.info("removed", await folder.remove("saves/b.txt"));
    try {
        await folder.read("");
    } catch (error) {
        log.info("path", error.message.split(" ")[0], error.name);
    }
    try {
        files.folder("Exports");
    } catch (error) {
        log.info("name", error.message.split(" ")[0], error.name);
    }
    try {
        files.pick({ filters: [{ name: 1 }] });
    } catch (error) {
        log.info("filters", error.message.split(" ")[0], error.name);
    }
}
)js");
    const TaskId task = fixture.host->start(script, "use");
    const Asked none = fixture.asked(0, "files.pick");
    CHECK(none.json == R"({"save":false,"filters":[],"suggestedName":""})");
    fixture.host->answer(none.id, "null");
    const Asked pick = fixture.asked(1, "files.pick");
    CHECK(pick.json ==
          R"({"save":true,"filters":[{"name":"Notes","extensions":["txt","md"]}],"suggestedName":"notes.txt"})");
    fixture.host->answer(pick.id, R"({"handle":"f1","name":"notes.txt"})");
    const Asked start = fixture.asked(2, "files.pick.read");
    CHECK(start.json == R"({"handle":"f1","offset":0})");
    fixture.host->answer(start.id, R"({"size":5,"modified":7,"data":"AQID"})");
    const Asked rest = fixture.asked(3, "files.pick.read");
    CHECK(rest.json == R"({"handle":"f1","offset":3})");
    fixture.host->answer(rest.id, R"({"size":5,"modified":7,"data":"BAU="})");
    const Asked text = fixture.asked(4, "files.pick.read");
    CHECK(text.json == R"({"handle":"f1","offset":0,"text":true})");
    fixture.host->answer(text.id, R"({"size":9,"modified":7,"text":"\ufeffhé","next":6})");
    const Asked more = fixture.asked(5, "files.pick.read");
    CHECK(more.json == R"({"handle":"f1","offset":6,"text":true})");
    fixture.host->answer(more.id, R"({"size":9,"modified":7,"text":"llo","next":9})");
    const Asked words = fixture.asked(6, "files.pick.write");
    CHECK(words.json == R"({"handle":"f1","text":"héllo"})");
    fixture.host->answer(words.id, "null");
    const Asked bytes = fixture.asked(7, "files.pick.write");
    CHECK(bytes.json == R"({"handle":"f1","data":"CQg="})");
    fixture.host->answer(bytes.id, "null");
    const Asked staged = fixture.asked(8, "files.pick.write");
    CHECK(staged.json == R"({"handle":"f1","stage":1,"text":")" + std::string(200000 - 2 * 98304, 'x') + R"("})");
    const std::vector<std::string> parts = fixture.services->callsTo("files.part");
    REQUIRE(parts.size() == 2);
    CHECK(parts[0] == R"({"stage":1,"text":")" + std::string(98304, 'x') + R"("})");
    fixture.host->answer(staged.id, "null");
    const Asked folder = fixture.asked(9, "files.folder");
    CHECK(folder.json == R"({"name":"exports"})");
    fixture.host->answer(folder.id, R"({"handle":"d1","name":"exports"})");
    const Asked list = fixture.asked(10, "files.folder.list");
    CHECK(list.json == R"({"handle":"d1","path":"saves/"})");
    fixture.host->answer(list.id, R"([{"name":"a.txt","path":"saves/a.txt","folder":false,"size":3,"modified":5}])");
    const Asked inside = fixture.asked(11, "files.folder.read");
    CHECK(inside.json == R"({"handle":"d1","path":"saves/a.txt","offset":0,"text":true})");
    fixture.host->answer(inside.id, R"({"size":3,"modified":5,"text":"abc","next":3})");
    const Asked put = fixture.asked(12, "files.folder.write");
    CHECK(put.json == R"({"handle":"d1","path":"saves/b.txt","text":"b"})");
    fixture.host->answer(put.id, "null");
    const Asked removal = fixture.asked(13, "files.folder.remove");
    CHECK(removal.json == R"({"handle":"d1","path":"saves/b.txt"})");
    fixture.host->answer(removal.id, "true");

    const Report ended = fixture.finish(task);
    INFO(gs::script::describe(ended));
    CHECK(ended.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("none null")) == 1);
    CHECK(fixture.recorder.count(logged("file notes.txt true [object FileHandle] true")) == 1);
    CHECK(fixture.recorder.count(logged("bytes 1,2,3,4,5")) == 1);
    CHECK(fixture.recorder.count(logged("text héllo")) == 1);
    CHECK(fixture.recorder.count(logged("closed false")) == 1);
    CHECK(fixture.recorder.count(logged("after GS-API-164 RangeError")) == 1);
    CHECK(fixture.recorder.count(logged("folder exports [object FolderHandle]")) == 1);
    CHECK(fixture.recorder.count(logged("entries 1 saves/a.txt false 3 true")) == 1);
    CHECK(fixture.recorder.count(logged("read abc")) == 1);
    CHECK(fixture.recorder.count(logged("removed true")) == 1);
    CHECK(fixture.recorder.count(logged("path GS-API-161 TypeError")) == 1);
    CHECK(fixture.recorder.count(logged("name GS-API-161 TypeError")) == 1);
    CHECK(fixture.recorder.count(logged("filters GS-API-161 TypeError")) == 1);
    CHECK(fixture.services->callsTo("files.pick.close") == std::vector<std::string>{R"({"handle":"f1"})"});
}

TEST_CASE("ADR-0112: a file that changes while it is read fails, and refusals from the program keep their codes")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function changed() {
    const folder = await files.folder("exports");
    try {
        await folder.read("a.bin");
    } catch (error) {
        log.info("changed", error.message);
    }
    try {
        await net.fetch("https://elsewhere.example.com/");
    } catch (error) {
        log.info("host", error.message);
    }
}
)js");
    const TaskId task = fixture.host->start(script, "changed");
    fixture.host->answer(fixture.asked(0, "files.folder").id, R"({"handle":"d1","name":"exports"})");
    const Asked first = fixture.asked(1, "files.folder.read");
    fixture.host->answer(first.id, R"({"size":4,"modified":1,"data":"AQI="})");
    const Asked second = fixture.asked(2, "files.folder.read");
    CHECK(second.json == R"({"handle":"d1","path":"a.bin","offset":2})");
    fixture.host->answer(second.id, R"({"size":4,"modified":2,"data":"AwQ="})");
    const Asked fetch = fixture.asked(3, "net.fetch");
    fixture.host->refuse(fetch.id, "GS-API-155 this extension may not reach elsewhere.example.com: its manifest does "
                                   "not list it under the permission \"network\"");
    CHECK(fixture.finish(task).kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("changed GS-API-162 the file changed while it was read")) == 1);
    CHECK(fixture.recorder.count(logged("host GS-API-155 this extension may not reach elsewhere.example.com: its "
                                        "manifest does not list it under the permission \"network\"")) == 1);
}

TEST_CASE("ADR-0112: net.fetch and net.socket need network, the file services files.pick and files.paths, and the "
          "table declares them")
{
    CHECK(gs::script::servicePermission("net.fetch") == "network");
    CHECK(gs::script::servicePermission("net.fetch.part") == "network");
    CHECK(gs::script::servicePermission("net.socket.send") == "network");
    CHECK(gs::script::servicePermission("net.listen") == "network.listen");
    CHECK(gs::script::servicePermission("net.respond") == "network.listen");
    CHECK(gs::script::servicePermission("files.pick.read") == "files.pick");
    CHECK(gs::script::servicePermission("files.folder.write") == "files.paths");
    CHECK(gs::script::servicePermission("files.part").empty());

    const std::vector<gs::script::ApiFile> files = gs::script::apiFiles();
    const auto found = std::ranges::find(files, std::string("types/gamescrcpy.d.ts"), &gs::script::ApiFile::path);
    REQUIRE(found != files.end());
    const std::string& declarations = found->contents;
    CHECK(declarations.find("declare namespace files {") != std::string::npos);
    CHECK(declarations.find("    function fetch(url: string, options?: NetFetchOptions): Promise<NetResponse>;") !=
          std::string::npos);
    CHECK(declarations.find("    function socket(url: string, options?: NetSocketOptions): Promise<NetSocket>;") !=
          std::string::npos);
    CHECK(declarations.find("    function pick(options?: FilePickOptions): Promise<FileHandle | null>;") !=
          std::string::npos);
    CHECK(declarations.find("    function folder(name: string): Promise<FolderHandle>;") != std::string::npos);
    CHECK(declarations.find("interface FolderHandle {") != std::string::npos);
}
