#include "ScriptFixture.h"

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

class HardwareServices final : public gs::script::ScriptServices
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
        return name.starts_with("hardware.") || name.starts_with("net.") || name.starts_with("speech.");
    }

    ServiceAnswer callService(std::string_view name, std::string_view json) override
    {
        const std::scoped_lock lock(m_mutex);
        m_calls.push_back(std::string(name) + " " + std::string(json));
        if (m_denied) {
            return {"null", "GS-API-080", "this extension may not use devices"};
        }
        if (name == "hardware.list") {
            return {R"([{"id":"serial:a","kind":"serial","name":"Board","present":true}])", {}, {}};
        }
        if (name == "speech.engines") {
            return {R"([{"id":"system","title":"Windows","say":true,"listen":true,"dictation":false,)"
                    R"("languages":["zh-CN","en-US"]}])",
                    {},
                    {}};
        }
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

    void dropService(std::uint64_t id) override
    {
        const std::scoped_lock lock(m_mutex);
        m_dropped.push_back(id);
    }

    std::optional<Asked> waitForAsk(std::size_t index)
    {
        std::unique_lock lock(m_mutex);
        if (!m_changed.wait_for(lock, std::chrono::seconds(30), [&] { return m_asked.size() > index; })) {
            return std::nullopt;
        }
        return m_asked[index];
    }

    std::vector<std::string> calls() const
    {
        const std::scoped_lock lock(m_mutex);
        return m_calls;
    }

    void deny()
    {
        const std::scoped_lock lock(m_mutex);
        m_denied = true;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<Asked> m_asked;
    std::vector<std::string> m_calls;
    std::vector<std::uint64_t> m_dropped;
    bool m_denied = false;
};

struct Fixture {
    ScriptFolder folder;
    Recorder recorder;
    std::shared_ptr<HardwareServices> services = std::make_shared<HardwareServices>();
    std::unique_ptr<ScriptHost> host =
        std::make_unique<ScriptHost>(recorder.sink(), gs::script::Limits{}, services);

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

}

TEST_CASE("SCR-026: hardware.request, list and open go through the service channel, and a serial port sends bytes as "
          "base64")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function pick() {
    const chosen = await hardware.request({ kind: "serial", filters: [{ vendorId: 0x1a86 }] });
    log.info("chosen", chosen.length, chosen[0].id, chosen[0].name, Object.isFrozen(chosen[0]));
    log.info("listed", JSON.stringify(hardware.list()));
    const port = await hardware.open(chosen[0].id, { baudRate: 9600 });
    log.info("opened", port.kind, port.name, port.isOpen, Object.prototype.toString.call(port));
    await port.write("Hi");
    await port.write(new Uint8Array([0, 255, 16]));
    await port.setSignals({ dtr: false });
    await port.close();
    log.info("closed", port.isOpen);
}
)js");
    const TaskId task = fixture.host->start(script, "pick");

    const Asked request = fixture.asked(0, "hardware.request");
    CHECK(request.json == R"({"kind":"serial","filters":[{"vendorId":6790}]})");
    fixture.host->answer(request.id, R"([{"id":"serial:a","kind":"serial","name":"Board"}])");
    const Asked open = fixture.asked(1, "hardware.open");
    CHECK(open.json == R"({"id":"serial:a","options":{"baudRate":9600}})");
    fixture.host->answer(open.id, R"({"handle":"d1","id":"serial:a","kind":"serial","name":"Board"})");
    const Asked text = fixture.asked(2, "hardware.write");
    CHECK(text.json == R"({"handle":"d1","data":"SGk="})");
    fixture.host->answer(text.id, "null");
    const Asked bytes = fixture.asked(3, "hardware.write");
    CHECK(bytes.json == R"({"handle":"d1","data":"AP8Q"})");
    fixture.host->answer(bytes.id, "null");
    const Asked signals = fixture.asked(4, "hardware.control");
    CHECK(signals.json == R"({"handle":"d1","op":"signals","dtr":false})");
    fixture.host->answer(signals.id, "null");
    const Asked close = fixture.asked(5, "hardware.close");
    CHECK(close.json == R"({"handle":"d1"})");
    fixture.host->answer(close.id, "null");

    const Report ended = fixture.finish(task);
    INFO(gs::script::describe(ended));
    CHECK(ended.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("chosen 1 serial:a Board true")) == 1);
    CHECK(fixture.recorder.count(
              logged(R"(listed [{"id":"serial:a","kind":"serial","name":"Board","present":true}])")) == 1);
    CHECK(fixture.recorder.count(logged("opened serial Board true [object SerialDevice]")) == 1);
    CHECK(fixture.recorder.count(logged("closed false")) == 1);
    CHECK(fixture.services->calls() == std::vector<std::string>{"hardware.list {}"});
}

TEST_CASE("SCR-026: data, reports and notifications reach the device's listeners, and a closed event ends the device")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function listen() {
    const port = await hardware.open("serial:a");
    port.onData(({ data }) => log.info("data", Array.from(data).join(",")));
    port.onClose(({ reason, message }) => log.info("gone", reason, message, port.isOpen));
    const pad = await hardware.open("hid:p");
    pad.onReport(({ report, data }) => log.info("report", report, Array.from(data).join(",")));
    const belt = await hardware.open("ble:c0ffee123456");
    const watching = await belt.subscribe("180D", "0x2A37", ({ characteristic, data }) =>
        log.info("notify", characteristic, data[1]));
    const feature = await pad.getFeature(3);
    log.info("ready", typeof watching.dispose, Array.from(feature).join(","));
    await sleep(30000);
}
)js");
    const TaskId task = fixture.host->start(script, "listen");
    fixture.host->answer(fixture.asked(0, "hardware.open").id,
                         R"({"handle":"d1","id":"serial:a","kind":"serial","name":"Board"})");
    fixture.host->answer(fixture.asked(1, "hardware.open").id,
                         R"({"handle":"d2","id":"hid:p","kind":"hid","name":"Pad"})");
    fixture.host->answer(fixture.asked(2, "hardware.open").id,
                         R"({"handle":"d3","id":"ble:c0ffee123456","kind":"ble","name":"Belt"})");
    const Asked subscribe = fixture.asked(3, "hardware.control");
    CHECK(subscribe.json == R"({"handle":"d3","op":"subscribe","service":"0000180d-0000-1000-8000-00805f9b34fb",)"
                            R"("characteristic":"00002a37-0000-1000-8000-00805f9b34fb","on":true})");
    fixture.host->answer(subscribe.id, "null");
    const Asked feature = fixture.asked(4, "hardware.control");
    CHECK(feature.json == R"({"handle":"d2","op":"getFeature","report":3})");
    fixture.host->answer(feature.id, R"({"data":"CQgH"})");
    REQUIRE(fixture.recorder.waitFor(logged("ready function 9,8,7")).has_value());

    fixture.host->event("hardware", "d1", R"({"type":"data","data":"AQID"})");
    REQUIRE(fixture.recorder.waitFor(logged("data 1,2,3")).has_value());
    fixture.host->event("hardware", "d2", R"({"type":"report","report":2,"data":"AAE="})");
    REQUIRE(fixture.recorder.waitFor(logged("report 2 0,1")).has_value());
    fixture.host->event("hardware", "d3",
                        R"({"type":"notify","service":"0000180d-0000-1000-8000-00805f9b34fb",)"
                        R"("characteristic":"00002a37-0000-1000-8000-00805f9b34fb","data":"AEg="})");
    REQUIRE(fixture.recorder.waitFor(logged("notify 00002a37-0000-1000-8000-00805f9b34fb 72")).has_value());
    fixture.host->event("hardware", "d1",
                        R"({"type":"closed","reason":"removed","message":"GS-API-082 Board is not connected"})");
    REQUIRE(fixture.recorder.waitFor(logged("gone removed GS-API-082 Board is not connected false")).has_value());
    fixture.host->event("hardware", "d1", R"({"type":"data","data":"BAU="})");

    fixture.host->cancel(task);
    CHECK(fixture.finish(task).kind == ReportKind::TaskCancelled);
    CHECK(fixture.asked(5, "hardware.close").json == R"({"handle":"d2"})");
    CHECK(fixture.asked(6, "hardware.close").json == R"({"handle":"d3"})");
    CHECK(fixture.recorder.count(logged("data 4,5")) == 0);
}

TEST_CASE("SCR-026: refusals from the program and wrong arguments carry the GS-API codes")
{
    Fixture fixture;
    fixture.services->deny();
    const ScriptId script = fixture.load(R"js(
export async function wrong() {
    try {
        await hardware.open("serial:b");
    } catch (error) {
        log.info("open", error.message);
    }
    try {
        hardware.request({ kind: "usb" });
    } catch (error) {
        log.info("kind", error.message.split(" ")[0], error.name);
    }
    try {
        hardware.list();
    } catch (error) {
        log.info("list", error.message.split(" ")[0], error.name);
    }
    try {
        input.source("Deck");
    } catch (error) {
        log.info("source", error.message.split(" ")[0]);
    }
    try {
        await hardware.open("");
    } catch (error) {
        log.info("id", error.message.split(" ")[0]);
    }
}
)js");
    const TaskId task = fixture.host->start(script, "wrong");
    fixture.host->refuse(fixture.asked(0, "hardware.open").id,
                         "GS-API-081 the device has not been allowed; ask for it with hardware.request()");
    CHECK(fixture.finish(task).kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(
              logged("open GS-API-081 the device has not been allowed; ask for it with hardware.request()")) == 1);
    CHECK(fixture.recorder.count(logged("kind GS-API-085 TypeError")) == 1);
    CHECK(fixture.recorder.count(logged("list GS-API-080 TypeError")) == 1);
    CHECK(fixture.recorder.count(logged("source GS-API-085")) == 1);
    CHECK(fixture.recorder.count(logged("id GS-API-085")) == 1);
}

TEST_CASE("SCR-026: an input source presses, releases and moves axes through hardware.input")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export function play() {
    const deck = input.source("deck");
    log.info("same", deck === input.source("deck"), deck.name, Object.prototype.toString.call(deck),
             Object.isFrozen(deck));
    deck.press("k1");
    deck.axis("knob", 0.5);
    deck.axis("knob", -1, 0.25);
    deck.release("k1");
    try {
        deck.axis("knob", 2);
    } catch (error) {
        log.info("axis", error.message.split(" ")[0], error.name);
    }
    try {
        deck.press("a.b");
    } catch (error) {
        log.info("key", error.message.split(" ")[0]);
    }
}
)js");
    const TaskId task = fixture.host->start(script, "play");
    CHECK(fixture.finish(task).kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("same true deck [object InputSource] true")) == 1);
    CHECK(fixture.recorder.count(logged("axis GS-API-085 RangeError")) == 1);
    CHECK(fixture.recorder.count(logged("key GS-API-085")) == 1);
    CHECK(fixture.services->calls() ==
          std::vector<std::string>{R"(hardware.input {"source":"deck","key":"k1","action":"press"})",
                                   R"(hardware.input {"source":"deck","key":"knob","action":"axis","x":0.5})",
                                   R"(hardware.input {"source":"deck","key":"knob","action":"axis","x":-1,"y":0.25})",
                                   R"(hardware.input {"source":"deck","key":"k1","action":"release"})"});
}

TEST_CASE("ADR-0112: hardware.claim and release go through the service channel, and onWanted lets the claim go")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function hold() {
    log.info("claimed", await hardware.claim("serial:COM9", { onWanted: (by) => { log.info("wanted", by); return true; } }));
    log.info("again", await hardware.claim("ble:AA:BB:CC:DD:EE:FF", { wait: true }));
    log.info("busy", await hardware.claim("usb:2341:0043:A1B2"));
    hardware.release("ble:AA:BB:CC:DD:EE:FF");
    try {
        await hardware.claim("COM9");
    } catch (error) {
        log.info("name", error.message.split(" ")[0], error.name);
    }
    try {
        await hardware.claim("serial:COM9", { onWanted: 1 });
    } catch (error) {
        log.info("handler", error.message.split(" ")[0]);
    }
    await sleep(30000);
}
)js");
    const TaskId task = fixture.host->start(script, "hold");
    const Asked first = fixture.asked(0, "hardware.claim");
    CHECK(first.json == R"({"resource":"serial:COM9","wait":false,"yields":true})");
    fixture.host->answer(first.id, "true");
    const Asked second = fixture.asked(1, "hardware.claim");
    CHECK(second.json == R"({"resource":"ble:AA:BB:CC:DD:EE:FF","wait":true,"yields":false})");
    fixture.host->answer(second.id, "true");
    const Asked third = fixture.asked(2, "hardware.claim");
    CHECK(third.json == R"({"resource":"usb:2341:0043:A1B2","wait":false,"yields":false})");
    fixture.host->answer(third.id, "false");
    REQUIRE(fixture.recorder.waitFor(logged("handler GS-API-008")).has_value());
    CHECK(fixture.recorder.count(logged("claimed true")) == 1);
    CHECK(fixture.recorder.count(logged("again true")) == 1);
    CHECK(fixture.recorder.count(logged("busy false")) == 1);
    CHECK(fixture.recorder.count(logged("name GS-API-085 TypeError")) == 1);

    fixture.host->event("hardware", "", R"({"type":"claimWanted","resource":"serial:COM9","by":"Box"})");
    REQUIRE(fixture.recorder.waitFor(logged("wanted Box")).has_value());
    const std::vector<std::string> released{R"(hardware.release {"resource":"ble:AA:BB:CC:DD:EE:FF"})",
                                            R"(hardware.release {"resource":"serial:COM9"})"};
    for (int round = 0; round < 3000 && fixture.services->calls() != released; ++round) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(fixture.services->calls() == released);
    fixture.host->event("hardware", "", R"({"type":"claimWanted","resource":"serial:COM9","by":"Box"})");
    fixture.host->cancel(task);
    CHECK(fixture.finish(task).kind == ReportKind::TaskCancelled);
    CHECK(fixture.recorder.count(logged("wanted Box")) == 1);
    CHECK(fixture.services->calls() == released);
}

TEST_CASE("ADR-0112: a task that stops while its wait for hardware is still open lets the hardware go")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function wait() {
    log.info("claimed", await hardware.claim("serial:COM9", { wait: true }));
}
)js");
    const TaskId task = fixture.host->start(script, "wait");
    const Asked claim = fixture.asked(0, "hardware.claim");
    CHECK(claim.json == R"({"resource":"serial:COM9","wait":true,"yields":false})");
    fixture.host->cancel(task);
    CHECK(fixture.finish(task).kind == ReportKind::TaskCancelled);
    const std::vector<std::string> released{R"(hardware.release {"resource":"serial:COM9"})"};
    for (int round = 0; round < 300 && fixture.services->calls() != released; ++round) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(fixture.services->calls() == released);
    fixture.host->answer(claim.id, "true");
    CHECK(fixture.recorder.count(logged("claimed true")) == 0);
}

TEST_CASE("ADR-0112: net.listen hands each request to its handler and sends back what the handler answers")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function serve() {
    const server = await net.listen(8765, async (request) => {
        log.info("asked", request.method, request.path, request.query.name, request.headers["x-thing"], request.text);
        if (request.path === "/json") return { status: 201, json: { ok: true } };
        if (request.path === "/text") return "hi";
        if (request.path === "/fail") throw new Error("broken");
        if (request.path === "/bad") return { status: 99 };
        return undefined;
    }, { origins: ["https://deck.example"] });
    log.info("listening", server.port, server.isOpen, Object.prototype.toString.call(server));
    try {
        await net.listen(80, () => undefined);
    } catch (error) {
        log.info("port", error.message.split(" ")[0], error.name);
    }
    await sleep(30000);
}
)js");
    const TaskId task = fixture.host->start(script, "serve");
    const Asked listen = fixture.asked(0, "net.listen");
    CHECK(listen.json == R"({"port":8765,"origins":["https://deck.example"]})");
    fixture.host->answer(listen.id, R"({"handle":"n1","port":8765})");
    REQUIRE(fixture.recorder.waitFor(logged("listening 8765 true [object NetServer]")).has_value());
    REQUIRE(fixture.recorder.waitFor(logged("port GS-API-101 RangeError")).has_value());
    for (const auto& [request, path] : std::vector<std::pair<int, std::string>>{
             {1, "/json"}, {2, "/text"}, {3, "/none"}, {4, "/fail"}, {5, "/bad"}}) {
        fixture.host->event("net", "n1",
                            R"({"type":"request","request":)" + std::to_string(request) + R"(,"method":"POST","path":")" +
                                path + R"(","query":{"name":"Ann"},"headers":{"x-thing":"a"},"body":"aGk="})");
    }
    const auto answered = [&fixture] {
        std::vector<std::string> found;
        for (const std::string& call : fixture.services->calls()) {
            if (call.starts_with("net.respond ")) {
                found.push_back(call.substr(12));
            }
        }
        return found;
    };
    for (int round = 0; round < 3000 && answered().size() < 5; ++round) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const std::vector<std::string> answers = answered();
    REQUIRE(answers.size() == 5);
    const auto has = [&answers](const std::string& wanted) {
        return std::ranges::find(answers, wanted) != answers.end();
    };
    CHECK(has(R"({"handle":"n1","request":1,"status":201,"headers":{"content-type":"application/json; charset=utf-8"},)"
              R"("body":"eyJvayI6dHJ1ZX0="})"));
    CHECK(has(R"({"handle":"n1","request":2,"status":200,"headers":{"content-type":"text/plain; charset=utf-8"},)"
              R"("body":"aGk="})"));
    CHECK(has(R"({"handle":"n1","request":3,"status":204,"headers":{},"body":""})"));
    CHECK(has(R"({"handle":"n1","request":4,"status":500,"headers":{},"body":""})"));
    CHECK(has(R"({"handle":"n1","request":5,"status":500,"headers":{},"body":""})"));
    CHECK(fixture.recorder.count(logged("asked POST /json Ann a hi")) == 1);

    fixture.host->cancel(task);
    CHECK(fixture.finish(task).kind == ReportKind::TaskCancelled);
    const auto closed = [&fixture] {
        const std::vector<std::string> calls = fixture.services->calls();
        return std::ranges::find(calls, std::string(R"(net.close {"handle":"n1"})")) != calls.end();
    };
    for (int round = 0; round < 3000 && !closed(); ++round) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(closed());
}

TEST_CASE("ADR-0112: speech.say, voices and listen go through the service channel, and a listener's handler hears "
          "each command in turn")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function talk() {
    log.info("said", await speech.say("你好", { rate: 1.5, interrupt: true }));
    const engines = speech.engines();
    log.info("engines", engines.length, engines[0].id, engines[0].languages.join(","),
             Object.isFrozen(engines) && Object.isFrozen(engines[0]) && Object.isFrozen(engines[0].languages));
    const voices = await speech.voices({ engine: "system" });
    log.info("voices", voices.length, voices[0].name, voices[0].language, Object.isFrozen(voices[0]));
    try {
        await speech.say("");
    } catch (error) {
        log.info("empty", error.message.split(" ")[0], error.name);
    }
    try {
        await speech.say("x", { rate: 3 });
    } catch (error) {
        log.info("rate", error.message.split(" ")[0], error.name);
    }
    try {
        await speech.say("x", { voice: 1 });
    } catch (error) {
        log.info("voice", error.message.split(" ")[0], error.name);
    }
    try {
        await speech.listen([], () => {});
    } catch (error) {
        log.info("phrases", error.message.split(" ")[0], error.name);
    }
    try {
        await speech.listen(["go"], 5);
    } catch (error) {
        log.info("handler", error.message.split(" ")[0], error.name);
    }
    speech.stop();
    const listener = await speech.listen(["open map", "close bag"], async ({ phrase, text, confidence }) => {
        log.info("heard", phrase, text, confidence);
        await sleep(20);
        log.info("handled", phrase);
    }, { confidence: 0.5, language: "zh-CN" });
    log.info("listening", listener.isOpen, Object.prototype.toString.call(listener));
    await sleep(30000);
}
)js");
    const TaskId task = fixture.host->start(script, "talk");
    const Asked say = fixture.asked(0, "speech.say");
    CHECK(say.json == R"({"text":"你好","rate":1.5,"interrupt":true})");
    fixture.host->answer(say.id, "true");
    const Asked voices = fixture.asked(1, "speech.voices");
    CHECK(voices.json == R"({"engine":"system"})");
    fixture.host->answer(voices.id, R"([{"id":"zh","name":"Huihui","language":"zh-CN"}])");
    const Asked listen = fixture.asked(2, "speech.listen");
    CHECK(listen.json == R"({"phrases":["open map","close bag"],"language":"zh-CN","confidence":0.5})");
    fixture.host->answer(listen.id, R"({"handle":"s1","engine":"system","language":"zh-CN"})");
    REQUIRE(fixture.recorder.waitFor(logged("listening true [object SpeechListener]")).has_value());
    CHECK(fixture.recorder.count(logged("said true")) == 1);
    CHECK(fixture.recorder.count(logged("engines 1 system zh-CN,en-US true")) == 1);
    CHECK(fixture.recorder.count(logged("voices 1 Huihui zh-CN true")) == 1);
    CHECK(fixture.recorder.count(logged("empty GS-API-111 RangeError")) == 1);
    CHECK(fixture.recorder.count(logged("rate GS-API-111 RangeError")) == 1);
    CHECK(fixture.recorder.count(logged("voice GS-API-111 TypeError")) == 1);
    CHECK(fixture.recorder.count(logged("phrases GS-API-111 TypeError")) == 1);
    CHECK(fixture.recorder.count(logged("handler GS-API-008 TypeError")) == 1);

    fixture.host->event("speech", "s1",
                        R"({"type":"heard","phrase":"open map","text":"open the map","confidence":0.9})");
    fixture.host->event("speech", "s1",
                        R"({"type":"heard","phrase":"close bag","text":"close bag","confidence":0.75})");
    const auto firstHeard = fixture.recorder.waitFor(logged("heard open map open the map 0.9"));
    const auto firstHandled = fixture.recorder.waitFor(logged("handled open map"));
    const auto secondHeard = fixture.recorder.waitFor(logged("heard close bag close bag 0.75"));
    const auto secondHandled = fixture.recorder.waitFor(logged("handled close bag"));
    REQUIRE(firstHeard.has_value());
    REQUIRE(firstHandled.has_value());
    REQUIRE(secondHeard.has_value());
    REQUIRE(secondHandled.has_value());
    CHECK(firstHeard->index < firstHandled->index);
    CHECK(firstHandled->index < secondHeard->index);
    CHECK(secondHeard->index < secondHandled->index);

    fixture.host->cancel(task);
    CHECK(fixture.finish(task).kind == ReportKind::TaskCancelled);
    const std::vector<std::string> called{"speech.engines {}", "speech.stop {}", R"(speech.close {"handle":"s1"})"};
    for (int round = 0; round < 3000 && fixture.services->calls() != called; ++round) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(fixture.services->calls() == called);
}
