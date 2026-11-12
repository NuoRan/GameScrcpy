#include <gamescrcpy_ext.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

const gs_ext_host* g_host = nullptr;
std::mutex g_mutex;
std::string g_touches;
std::string g_device;
std::string g_options = "null";
bool g_breakTouch = false;
std::string g_changed = "[]";
std::string g_boxes = "[]";
std::string g_acted;
bool g_yield = false;
std::string g_wanted;
std::string g_spoken = "null";
std::string g_heard;
std::string g_aiRequest = "null";

void appendNumber(std::string& out, double value)
{
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    out += buffer;
}

void appendText(std::string& out, const char* text, size_t size)
{
    out += '"';
    for (size_t index = 0; index < size; ++index) {
        const char c = text[index];
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char escaped[8] = {};
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(c));
            out += escaped;
        } else {
            out += c;
        }
    }
    out += '"';
}

void appendEntry(const std::string& entry)
{
    if (!g_touches.empty()) {
        g_touches += ',';
    }
    g_touches += entry;
}

int32_t giveText(gs_ext_result* result, uint32_t type, const std::string& text)
{
    return g_host->set_text(result, type, text.data(), text.size());
}

int32_t giveFlag(gs_ext_result* result, bool on)
{
    result->value.type = GS_EXT_TYPE_BOOLEAN;
    result->value.flag = on ? 1 : 0;
    return GS_EXT_OK;
}

int32_t add(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    result->value.type = GS_EXT_TYPE_NUMBER;
    result->value.number = args[0].number + args[1].number;
    return GS_EXT_OK;
}

int32_t echo(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    return g_host->set_text(result, GS_EXT_TYPE_STRING, args[0].text, args[0].size);
}

int32_t mirror(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    std::string out = "{\"at\":[";
    appendNumber(out, args[0].x);
    out += ',';
    appendNumber(out, args[0].y);
    out += "],\"area\":[";
    appendNumber(out, args[1].x);
    out += ',';
    appendNumber(out, args[1].y);
    out += ',';
    appendNumber(out, args[1].width);
    out += ',';
    appendNumber(out, args[1].height);
    out += "],\"tint\":";
    appendText(out, args[2].text, args[2].size);
    out += ",\"wait\":";
    appendNumber(out, args[3].number);
    out += ",\"data\":";
    out.append(args[4].text, args[4].size);
    out += ",\"on\":";
    out += args[5].flag != 0 ? "true" : "false";
    out += '}';
    return giveText(result, GS_EXT_TYPE_JSON, out);
}

int32_t failing(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    constexpr char kWhy[] = "the test node fails on purpose";
    g_host->set_error(result, kWhy, sizeof(kWhy) - 1);
    return GS_EXT_FAILED;
}

int32_t crash(void*, const gs_ext_value*, uint32_t, gs_ext_result*)
{
    RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    return GS_EXT_OK;
}

int32_t slow(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    Sleep(static_cast<DWORD>(args[0].number));
    return giveFlag(result, true);
}

int32_t remember(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    return giveFlag(result,
                    g_host->store_set(g_host->context, args[0].text, args[0].size, args[1].text, args[1].size) ==
                        GS_EXT_OK);
}

int32_t recall(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    gs_ext_result stored{};
    if (g_host->store_get(g_host->context, args[0].text, args[0].size, &stored) != GS_EXT_OK) {
        constexpr char kWhy[] = "the store could not be read";
        g_host->set_error(result, kWhy, sizeof(kWhy) - 1);
        return GS_EXT_FAILED;
    }
    if (stored.value.type == GS_EXT_TYPE_NONE) {
        return giveText(result, GS_EXT_TYPE_JSON, "null");
    }
    return g_host->set_text(result, GS_EXT_TYPE_JSON, stored.value.text, stored.value.size);
}

int32_t allowed(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    const std::string permission(args[0].text, args[0].size);
    return giveFlag(result, g_host->allowed(g_host->context, permission.c_str()) != 0);
}

int32_t notifyNode(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    result->value.type = GS_EXT_TYPE_NUMBER;
    result->value.number = g_host->notify(g_host->context, args[0].text, args[0].size, args[1].text, args[1].size);
    return GS_EXT_OK;
}

int32_t touches(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    std::string out;
    {
        const std::scoped_lock lock(g_mutex);
        out = "{\"device\":";
        appendText(out, g_device.data(), g_device.size());
        out += ",\"options\":" + g_options + ",\"seen\":[" + g_touches + "]}";
        g_touches.clear();
    }
    return giveText(result, GS_EXT_TYPE_JSON, out);
}

int32_t breakTouch(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    const std::scoped_lock lock(g_mutex);
    g_breakTouch = true;
    result->value.type = GS_EXT_TYPE_NONE;
    return GS_EXT_OK;
}

int32_t wrongType(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    result->value.type = GS_EXT_TYPE_NUMBER;
    result->value.number = 7;
    return GS_EXT_OK;
}

int32_t say(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    result->value.type = GS_EXT_TYPE_NUMBER;
    result->value.number = g_host->emit_event(g_host->context, args[0].text, args[0].size, args[1].text, args[1].size);
    return GS_EXT_OK;
}

int32_t sayMany(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    int32_t said = GS_EXT_OK;
    for (int count = 1; count <= static_cast<int>(args[1].number) && said == GS_EXT_OK; ++count) {
        const std::string json = std::to_string(count);
        said = g_host->emit_event(g_host->context, args[0].text, args[0].size, json.data(), json.size());
    }
    result->value.type = GS_EXT_TYPE_NUMBER;
    result->value.number = said;
    return GS_EXT_OK;
}

int32_t settingNode(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    gs_ext_result value{};
    const int32_t said =
        g_host->setting_get(g_host->context, args[0].text, args[0].size, args[1].text, args[1].size, &value);
    if (said != GS_EXT_OK) {
        return giveText(result, GS_EXT_TYPE_STRING, "error " + std::to_string(said));
    }
    return giveText(result, GS_EXT_TYPE_STRING, std::string(value.value.text, value.value.size));
}

int32_t changedKeys(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    std::string keys;
    {
        const std::scoped_lock lock(g_mutex);
        keys = std::exchange(g_changed, "[]");
    }
    return giveText(result, GS_EXT_TYPE_JSON, keys);
}

void settingsChanged(void*, const char* keys, size_t size)
{
    const std::scoped_lock lock(g_mutex);
    g_changed.assign(keys, size);
}

int32_t listBoxes(void*, gs_ext_result* out)
{
    std::string boxes;
    {
        const std::scoped_lock lock(g_mutex);
        boxes = g_boxes;
    }
    return g_host->set_text(out, GS_EXT_TYPE_JSON, boxes.data(), boxes.size());
}

int32_t actOnBox(void*, const char* device, size_t deviceSize, const char* action, size_t actionSize)
{
    const std::scoped_lock lock(g_mutex);
    g_acted = std::string(device, deviceSize) + ":" + std::string(action, actionSize);
    return GS_EXT_OK;
}

int32_t setBoxes(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    {
        const std::scoped_lock lock(g_mutex);
        g_boxes.assign(args[0].text, args[0].size);
    }
    constexpr char kName[] = "boxes";
    result->value.type = GS_EXT_TYPE_NUMBER;
    result->value.number = g_host->devices_changed(g_host->context, kName, sizeof(kName) - 1);
    return GS_EXT_OK;
}

int32_t lastAction(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    std::string acted;
    {
        const std::scoped_lock lock(g_mutex);
        acted = g_acted;
    }
    return giveText(result, GS_EXT_TYPE_STRING, acted);
}

int32_t claimNode(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    result->value.type = GS_EXT_TYPE_NUMBER;
    result->value.number =
        g_host->claim(g_host->context, args[0].text, args[0].size, args[1].flag != 0 ? GS_EXT_CLAIM_WAIT : 0u);
    return GS_EXT_OK;
}

int32_t releaseNode(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    result->value.type = GS_EXT_TYPE_NUMBER;
    result->value.number = g_host->release(g_host->context, args[0].text, args[0].size);
    return GS_EXT_OK;
}

int32_t supportsNode(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    const std::string name(args[0].text, args[0].size);
    return giveFlag(result, g_host->supports(g_host->context, name.c_str()) != 0);
}

int32_t yieldClaims(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    {
        const std::scoped_lock lock(g_mutex);
        g_yield = args[0].flag != 0;
    }
    return giveFlag(result, true);
}

int32_t lastWanted(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    std::string wanted;
    {
        const std::scoped_lock lock(g_mutex);
        wanted = g_wanted;
    }
    return giveText(result, GS_EXT_TYPE_STRING, wanted);
}

int32_t voicesNode(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    return giveText(result, GS_EXT_TYPE_JSON,
                    R"([{"id":"robot","name":"Test robot","language":"en-US"},)"
                    R"({"id":"bell","name":"Test bell","language":"zh-CN"}])");
}

int32_t lastSpoken(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    std::string spoken;
    {
        const std::scoped_lock lock(g_mutex);
        spoken = g_spoken;
    }
    return giveText(result, GS_EXT_TYPE_JSON, spoken);
}

int32_t aiModelsNode(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    return giveText(result, GS_EXT_TYPE_JSON,
                    R"([{"id":"tiny","name":"Tiny test model","traits":{"think":true,"efforts":["high","low","huge"],)"
                    R"("thinkingEffort":true}},{"id":"plain","name":"Plain test model"},{"name":"no id"},)"
                    R"({"id":"tiny","name":"Again"}])");
}

int32_t lastAiRequest(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    std::string request;
    {
        const std::scoped_lock lock(g_mutex);
        request = g_aiRequest;
    }
    return giveText(result, GS_EXT_TYPE_JSON, request);
}

int32_t sendInput(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    result->value.type = GS_EXT_TYPE_NUMBER;
    result->value.number = g_host->input_event(g_host->context, args[0].text, args[0].size, args[1].text, args[1].size,
                                               args[2].number, args[3].number, static_cast<uint64_t>(args[4].number));
    return GS_EXT_OK;
}

int32_t sendInputs(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    int passed = 0;
    for (int count = 0; count < static_cast<int>(args[2].number); ++count) {
        const double x = count % 2 == 0 ? 0.5 : -0.5;
        if (g_host->input_event(g_host->context, args[0].text, args[0].size, args[1].text, args[1].size, x, 0.0, 0) ==
            GS_EXT_OK) {
            ++passed;
        }
    }
    result->value.type = GS_EXT_TYPE_NUMBER;
    result->value.number = passed;
    return GS_EXT_OK;
}

struct ServiceCalls {
    std::mutex mutex;
    std::vector<std::thread> threads;
    std::string result = "null";
};

ServiceCalls& serviceCalls()
{
    static ServiceCalls calls;
    return calls;
}

int32_t serve(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    std::string out = "{\"seen\":";
    out.append(args[0].text, args[0].size);
    out += '}';
    return giveText(result, GS_EXT_TYPE_JSON, out);
}

int32_t callServiceLater(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    std::string target(args[0].text, args[0].size);
    std::string json(args[1].text, args[1].size);
    ServiceCalls& calls = serviceCalls();
    {
        const std::scoped_lock lock(calls.mutex);
        calls.result = "null";
        calls.threads.emplace_back([target = std::move(target), json = std::move(json)] {
            gs_ext_result value{};
            const int32_t status =
                g_host->call_service(g_host->context, target.data(), target.size(), json.data(), json.size(), &value);
            std::string out = "{\"status\":" + std::to_string(status) + ",\"text\":";
            appendText(out, value.value.text, value.value.size);
            out += '}';
            const std::scoped_lock done(serviceCalls().mutex);
            serviceCalls().result = out;
        });
    }
    result->value.type = GS_EXT_TYPE_NUMBER;
    result->value.number = 0;
    return GS_EXT_OK;
}

int32_t callServiceNow(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    gs_ext_result value{};
    result->value.type = GS_EXT_TYPE_NUMBER;
    result->value.number =
        g_host->call_service(g_host->context, args[0].text, args[0].size, args[1].text, args[1].size, &value);
    return GS_EXT_OK;
}

int32_t serviceResult(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    std::string out;
    {
        const std::scoped_lock lock(serviceCalls().mutex);
        out = serviceCalls().result;
    }
    return giveText(result, GS_EXT_TYPE_JSON, out);
}

void joinServiceCalls()
{
    std::vector<std::thread> threads;
    {
        const std::scoped_lock lock(serviceCalls().mutex);
        threads = std::move(serviceCalls().threads);
    }
    for (std::thread& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

struct TapBook {
    int opened = 0;
    int closed = 0;
    int frames = 0;
    bool refuse = false;
    double pauseMs = 0.0;
    std::string device;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t rotation = 0;
    uint32_t frameWidth = 0;
    uint32_t frameHeight = 0;
    uint32_t stride = 0;
    std::string first = "[]";
    std::vector<uint64_t> times;
};

TapBook g_tap;

int32_t tapStats(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    std::string out;
    {
        const std::scoped_lock lock(g_mutex);
        out = "{\"opened\":" + std::to_string(g_tap.opened) + ",\"closed\":" + std::to_string(g_tap.closed) +
              ",\"frames\":" + std::to_string(g_tap.frames) + ",\"device\":";
        appendText(out, g_tap.device.data(), g_tap.device.size());
        out += ",\"width\":" + std::to_string(g_tap.width) + ",\"height\":" + std::to_string(g_tap.height) +
               ",\"rotation\":" + std::to_string(g_tap.rotation) + ",\"frameWidth\":" + std::to_string(g_tap.frameWidth) +
               ",\"frameHeight\":" + std::to_string(g_tap.frameHeight) + ",\"stride\":" + std::to_string(g_tap.stride) +
               ",\"first\":" + g_tap.first + ",\"times\":[";
        for (size_t index = 0; index < g_tap.times.size(); ++index) {
            out += (index == 0 ? "" : ",") + std::to_string(g_tap.times[index]);
        }
        out += "]}";
    }
    return giveText(result, GS_EXT_TYPE_JSON, out);
}

int32_t tapReset(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    {
        const std::scoped_lock lock(g_mutex);
        g_tap = TapBook{};
    }
    return giveFlag(result, true);
}

int32_t tapPause(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    {
        const std::scoped_lock lock(g_mutex);
        g_tap.pauseMs = args[0].number;
    }
    return giveFlag(result, true);
}

int32_t tapRefuse(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    {
        const std::scoped_lock lock(g_mutex);
        g_tap.refuse = args[0].number != 0.0;
    }
    return giveFlag(result, true);
}

int32_t claimWanted(void*, const char* resource, size_t size, const char* by, size_t bySize)
{
    bool yields = false;
    {
        const std::scoped_lock lock(g_mutex);
        g_wanted = std::string(resource, size) + " by " + std::string(by, bySize);
        yields = g_yield;
    }
    if (!yields) {
        return GS_EXT_FAILED;
    }
    g_host->release(g_host->context, resource, size);
    return GS_EXT_OK;
}

int32_t subscribeNode(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    result->value.type = GS_EXT_TYPE_NUMBER;
    result->value.number =
        g_host->events_subscribe(g_host->context, args[0].text, args[0].size, args[1].flag != 0 ? 1u : 0u);
    return GS_EXT_OK;
}

int32_t heardNode(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    std::string heard;
    {
        const std::scoped_lock lock(g_mutex);
        heard = "[" + std::exchange(g_heard, std::string()) + "]";
    }
    return giveText(result, GS_EXT_TYPE_JSON, heard);
}

void onEvent(void*, const char* topic, size_t topicSize, const char* json, size_t jsonSize)
{
    std::string entry = "[";
    appendText(entry, topic, topicSize);
    entry += ',';
    entry.append(json, jsonSize);
    entry += ',' + std::to_string(GetCurrentThreadId()) + ']';
    const std::scoped_lock lock(g_mutex);
    if (!g_heard.empty()) {
        g_heard += ',';
    }
    g_heard += entry;
}

int32_t openTouch(void*, const gs_ext_touch_open* info, void** session)
{
    constexpr char kClaim[] = "{\"claim\":\"";
    const std::string options = info->options != nullptr && info->options_size > 0
                                    ? std::string(info->options, info->options_size)
                                    : std::string("null");
    if (options.starts_with(kClaim) && options.ends_with("\"}")) {
        const std::string resource = options.substr(sizeof(kClaim) - 1, options.size() - (sizeof(kClaim) - 1) - 2);
        if (g_host->claim(g_host->context, resource.data(), resource.size(), 0) != GS_EXT_OK) {
            return GS_EXT_BUSY;
        }
    }
    const std::scoped_lock lock(g_mutex);
    g_device = info->device != nullptr ? info->device : "";
    g_options = options;
    appendEntry("\"open\"");
    *session = &g_device;
    return GS_EXT_OK;
}

void closeTouch(void*, void*)
{
    const std::scoped_lock lock(g_mutex);
    appendEntry("\"close\"");
}

int32_t frameTouch(void*, void*, const gs_ext_touch_frame* frame)
{
    const std::scoped_lock lock(g_mutex);
    if (g_breakTouch) {
        g_breakTouch = false;
        return GS_EXT_FAILED;
    }
    for (uint32_t index = 0; index < frame->count; ++index) {
        const gs_ext_contact& contact = frame->contacts[index];
        std::string entry = "[" + std::to_string(contact.id) + "," + std::to_string(contact.action) + ",";
        appendNumber(entry, contact.x);
        entry += ',';
        appendNumber(entry, contact.y);
        entry += ',';
        appendNumber(entry, contact.natural_x);
        entry += ',';
        appendNumber(entry, contact.natural_y);
        entry += "," + std::to_string(frame->rotation) + "]";
        appendEntry(entry);
    }
    return GS_EXT_OK;
}

int32_t keyTouch(void*, void*, const gs_ext_key* key)
{
    const std::scoped_lock lock(g_mutex);
    appendEntry("[\"key\"," + std::to_string(key->keycode) + "," + std::to_string(key->down) + "]");
    return GS_EXT_OK;
}

int32_t textTouch(void*, void*, const char* text, size_t size)
{
    const std::scoped_lock lock(g_mutex);
    std::string entry = "[\"text\",";
    appendText(entry, text, size);
    appendEntry(entry + "]");
    return GS_EXT_OK;
}

int32_t mouseTouch(void*, void*, const gs_ext_mouse* mouse)
{
    const std::scoped_lock lock(g_mutex);
    appendEntry("[\"mouse\"," + std::to_string(mouse->buttons) + "," + std::to_string(mouse->dx) + "," +
                std::to_string(mouse->dy) + "," + std::to_string(mouse->wheel) + "]");
    return GS_EXT_OK;
}

int32_t padTouch(void*, void*, const gs_ext_gamepad* pad)
{
    const std::scoped_lock lock(g_mutex);
    appendEntry("[\"pad\"," + std::to_string(pad->buttons) + "," + std::to_string(pad->hat) + "," +
                std::to_string(pad->left_x) + "," + std::to_string(pad->left_y) + "," + std::to_string(pad->right_x) +
                "," + std::to_string(pad->right_y) + "," + std::to_string(pad->left_trigger) + "," +
                std::to_string(pad->right_trigger) + "]");
    return GS_EXT_OK;
}

constexpr char kLoopPhones[] = R"([{"id":"echo","name":"Echo phone","model":"Loop","detail":"answers what it gets"},)"
                               R"({"id":"pair","name":"Pair phone"},{"id":"refuse","name":"Absent phone"}])";

struct LoopBook {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::function<void()>> work;
    std::thread thread;
    bool stopping = false;
    std::map<uint64_t, std::string> phones;
    std::map<uint64_t, uint64_t> peers;
    uint64_t waitingPair = 0;
    std::vector<uint64_t> busy;
    bool hold = false;
    std::string lastOpen = "null";
    std::string closed;
    std::string listed = kLoopPhones;
};

LoopBook& loopBook()
{
    static LoopBook book;
    return book;
}

void loopLater(LoopBook& book, std::function<void()> step)
{
    book.work.push_back(std::move(step));
    if (!book.thread.joinable()) {
        book.thread = std::thread([] {
            LoopBook& running = loopBook();
            std::unique_lock lock(running.mutex);
            for (;;) {
                running.wake.wait(lock, [&running] { return running.stopping || !running.work.empty(); });
                if (running.work.empty()) {
                    return;
                }
                std::function<void()> next = std::move(running.work.front());
                running.work.pop_front();
                lock.unlock();
                next();
                lock.lock();
            }
        });
    }
    book.wake.notify_one();
}

void stopLoop()
{
    LoopBook& book = loopBook();
    std::thread thread;
    {
        const std::scoped_lock lock(book.mutex);
        book.stopping = true;
        thread = std::move(book.thread);
    }
    book.wake.notify_all();
    if (thread.joinable()) {
        thread.join();
    }
    const std::scoped_lock lock(book.mutex);
    book.stopping = false;
    book.work.clear();
    book.phones.clear();
    book.peers.clear();
    book.waitingPair = 0;
    book.busy.clear();
    book.hold = false;
    book.lastOpen = "null";
    book.closed.clear();
    book.listed = kLoopPhones;
}

void loopClosing(LoopBook& book, uint64_t stream, std::string reason)
{
    loopLater(book, [stream, reason = std::move(reason)] {
        g_host->stream_closed(g_host->context, stream, reason.data(), reason.size());
    });
}

int32_t loopDiscover(void*, gs_ext_result* out)
{
    std::string listed;
    {
        LoopBook& book = loopBook();
        const std::scoped_lock lock(book.mutex);
        listed = book.listed;
    }
    return g_host->set_text(out, GS_EXT_TYPE_JSON, listed.data(), listed.size());
}

int32_t loopOpen(void*, uint64_t stream, const gs_ext_stream_open* open)
{
    const std::string phone = open->phone != nullptr ? open->phone : "";
    const std::string channel = open->channel != nullptr ? open->channel : "";
    const std::string session = open->session != nullptr && open->session_size > 0
                                    ? std::string(open->session, open->session_size)
                                    : std::string("null");
    LoopBook& book = loopBook();
    const std::scoped_lock lock(book.mutex);
    book.lastOpen = "{\"phone\":";
    appendText(book.lastOpen, phone.data(), phone.size());
    book.lastOpen += ",\"channel\":";
    appendText(book.lastOpen, channel.data(), channel.size());
    book.lastOpen += ",\"session\":" + session + "}";
    if (phone == "refuse") {
        loopClosing(book, stream, "nobody answers");
        return GS_EXT_OK;
    }
    if (phone == "pair") {
        if (book.waitingPair == 0) {
            book.waitingPair = stream;
        } else {
            book.peers[book.waitingPair] = stream;
            book.peers[stream] = book.waitingPair;
            book.waitingPair = 0;
        }
    } else if (phone != "echo") {
        return GS_EXT_FAILED;
    }
    book.phones[stream] = phone;
    loopLater(book, [stream] { g_host->stream_opened(g_host->context, stream); });
    return GS_EXT_OK;
}

int32_t loopSend(void*, uint64_t stream, const uint8_t* data, size_t size)
{
    LoopBook& book = loopBook();
    const std::scoped_lock lock(book.mutex);
    const auto phone = book.phones.find(stream);
    if (phone == book.phones.end()) {
        return GS_EXT_FAILED;
    }
    if (book.hold) {
        if (std::ranges::find(book.busy, stream) == book.busy.end()) {
            book.busy.push_back(stream);
        }
        return GS_EXT_BUSY;
    }
    uint64_t target = stream;
    if (phone->second == "pair") {
        const auto peer = book.peers.find(stream);
        if (peer == book.peers.end()) {
            return GS_EXT_OK;
        }
        target = peer->second;
    }
    std::vector<uint8_t> bytes(data, data + size);
    loopLater(book, [target, bytes = std::move(bytes)] {
        g_host->stream_data(g_host->context, target, bytes.data(), bytes.size());
    });
    return GS_EXT_OK;
}

void loopClose(void*, uint64_t stream)
{
    LoopBook& book = loopBook();
    const std::scoped_lock lock(book.mutex);
    if (!book.closed.empty()) {
        book.closed += ',';
    }
    book.closed += std::to_string(stream);
    book.phones.erase(stream);
    std::erase(book.busy, stream);
    if (book.waitingPair == stream) {
        book.waitingPair = 0;
    }
    if (const auto peer = book.peers.find(stream); peer != book.peers.end()) {
        const uint64_t other = peer->second;
        book.peers.erase(peer);
        book.peers.erase(other);
        book.phones.erase(other);
        loopClosing(book, other, "the other end left");
    }
}

int32_t holdSends(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    LoopBook& book = loopBook();
    {
        const std::scoped_lock lock(book.mutex);
        book.hold = args[0].flag != 0;
        if (!book.hold) {
            for (const uint64_t stream : book.busy) {
                loopLater(book, [stream] { g_host->stream_writable(g_host->context, stream); });
            }
            book.busy.clear();
        }
    }
    return giveFlag(result, true);
}

int32_t dropStreams(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    const std::string reason(args[0].text, args[0].size);
    double dropped = 0;
    LoopBook& book = loopBook();
    {
        const std::scoped_lock lock(book.mutex);
        for (const auto& entry : book.phones) {
            loopClosing(book, entry.first, reason);
            ++dropped;
        }
        book.phones.clear();
        book.peers.clear();
        book.waitingPair = 0;
        book.busy.clear();
    }
    result->value.type = GS_EXT_TYPE_NUMBER;
    result->value.number = dropped;
    return GS_EXT_OK;
}

int32_t lastOpen(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    std::string opened;
    {
        LoopBook& book = loopBook();
        const std::scoped_lock lock(book.mutex);
        opened = book.lastOpen;
    }
    return giveText(result, GS_EXT_TYPE_JSON, opened);
}

int32_t closedStreams(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    std::string closed;
    {
        LoopBook& book = loopBook();
        const std::scoped_lock lock(book.mutex);
        closed = "[" + book.closed + "]";
    }
    return giveText(result, GS_EXT_TYPE_JSON, closed);
}

int32_t setPhones(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    {
        LoopBook& book = loopBook();
        const std::scoped_lock lock(book.mutex);
        book.listed.assign(args[0].text, args[0].size);
    }
    constexpr char kName[] = "loop";
    result->value.type = GS_EXT_TYPE_NUMBER;
    result->value.number = g_host->transport_changed(g_host->context, kName, sizeof(kName) - 1);
    return GS_EXT_OK;
}

const uint32_t kTwoNumbers[] = {GS_EXT_TYPE_NUMBER, GS_EXT_TYPE_NUMBER};
const uint32_t kOneString[] = {GS_EXT_TYPE_STRING};
const uint32_t kTwoStrings[] = {GS_EXT_TYPE_STRING, GS_EXT_TYPE_STRING};
const uint32_t kStringAndJson[] = {GS_EXT_TYPE_STRING, GS_EXT_TYPE_JSON};
const uint32_t kStringAndNumber[] = {GS_EXT_TYPE_STRING, GS_EXT_TYPE_NUMBER};
const uint32_t kOneJson[] = {GS_EXT_TYPE_JSON};
const uint32_t kOneDuration[] = {GS_EXT_TYPE_DURATION};
const uint32_t kMirrored[] = {GS_EXT_TYPE_POINT, GS_EXT_TYPE_REGION, GS_EXT_TYPE_COLOR,
                              GS_EXT_TYPE_DURATION, GS_EXT_TYPE_JSON, GS_EXT_TYPE_BOOLEAN};
const uint32_t kStringAndFlag[] = {GS_EXT_TYPE_STRING, GS_EXT_TYPE_BOOLEAN};
const uint32_t kOneFlag[] = {GS_EXT_TYPE_BOOLEAN};
const uint32_t kInputEvent[] = {GS_EXT_TYPE_STRING, GS_EXT_TYPE_STRING, GS_EXT_TYPE_NUMBER, GS_EXT_TYPE_NUMBER,
                                GS_EXT_TYPE_NUMBER};
const uint32_t kInputBurst[] = {GS_EXT_TYPE_STRING, GS_EXT_TYPE_STRING, GS_EXT_TYPE_NUMBER};
const uint32_t kOneNumber[] = {GS_EXT_TYPE_NUMBER};

const gs_ext_node kNodes[] = {
    {sizeof(gs_ext_node), "add", kTwoNumbers, 2, GS_EXT_TYPE_NUMBER, nullptr, &add},
    {sizeof(gs_ext_node), "echo", kOneString, 1, GS_EXT_TYPE_STRING, nullptr, &echo},
    {sizeof(gs_ext_node), "mirror", kMirrored, 6, GS_EXT_TYPE_JSON, nullptr, &mirror},
    {sizeof(gs_ext_node), "fail", nullptr, 0, GS_EXT_TYPE_STRING, nullptr, &failing},
    {sizeof(gs_ext_node), "crash", nullptr, 0, GS_EXT_TYPE_NONE, nullptr, &crash},
    {sizeof(gs_ext_node), "slow", kOneDuration, 1, GS_EXT_TYPE_BOOLEAN, nullptr, &slow},
    {sizeof(gs_ext_node), "remember", kStringAndJson, 2, GS_EXT_TYPE_BOOLEAN, nullptr, &remember},
    {sizeof(gs_ext_node), "recall", kOneString, 1, GS_EXT_TYPE_JSON, nullptr, &recall},
    {sizeof(gs_ext_node), "allowed", kOneString, 1, GS_EXT_TYPE_BOOLEAN, nullptr, &allowed},
    {sizeof(gs_ext_node), "notify", kTwoStrings, 2, GS_EXT_TYPE_NUMBER, nullptr, &notifyNode},
    {sizeof(gs_ext_node), "touches", nullptr, 0, GS_EXT_TYPE_JSON, nullptr, &touches},
    {sizeof(gs_ext_node), "breakTouch", nullptr, 0, GS_EXT_TYPE_NONE, nullptr, &breakTouch},
    {sizeof(gs_ext_node), "wrongType", nullptr, 0, GS_EXT_TYPE_STRING, nullptr, &wrongType},
    {sizeof(gs_ext_node), "say", kTwoStrings, 2, GS_EXT_TYPE_NUMBER, nullptr, &say},
    {sizeof(gs_ext_node), "sayMany", kStringAndNumber, 2, GS_EXT_TYPE_NUMBER, nullptr, &sayMany},
    {sizeof(gs_ext_node), "setting", kTwoStrings, 2, GS_EXT_TYPE_STRING, nullptr, &settingNode},
    {sizeof(gs_ext_node), "changedKeys", nullptr, 0, GS_EXT_TYPE_JSON, nullptr, &changedKeys},
    {sizeof(gs_ext_node), "setBoxes", kOneJson, 1, GS_EXT_TYPE_NUMBER, nullptr, &setBoxes},
    {sizeof(gs_ext_node), "lastAction", nullptr, 0, GS_EXT_TYPE_STRING, nullptr, &lastAction},
    {sizeof(gs_ext_node), "claim", kStringAndFlag, 2, GS_EXT_TYPE_NUMBER, nullptr, &claimNode},
    {sizeof(gs_ext_node), "release", kOneString, 1, GS_EXT_TYPE_NUMBER, nullptr, &releaseNode},
    {sizeof(gs_ext_node), "supports", kOneString, 1, GS_EXT_TYPE_BOOLEAN, nullptr, &supportsNode},
    {sizeof(gs_ext_node), "yieldClaims", kOneFlag, 1, GS_EXT_TYPE_BOOLEAN, nullptr, &yieldClaims},
    {sizeof(gs_ext_node), "lastWanted", nullptr, 0, GS_EXT_TYPE_STRING, nullptr, &lastWanted},
    {sizeof(gs_ext_node), "voices", nullptr, 0, GS_EXT_TYPE_JSON, nullptr, &voicesNode},
    {sizeof(gs_ext_node), "lastSpoken", nullptr, 0, GS_EXT_TYPE_JSON, nullptr, &lastSpoken},
    {sizeof(gs_ext_node), "subscribe", kStringAndFlag, 2, GS_EXT_TYPE_NUMBER, nullptr, &subscribeNode},
    {sizeof(gs_ext_node), "heard", nullptr, 0, GS_EXT_TYPE_JSON, nullptr, &heardNode},
    {sizeof(gs_ext_node), "sendInput", kInputEvent, 5, GS_EXT_TYPE_NUMBER, nullptr, &sendInput},
    {sizeof(gs_ext_node), "sendInputs", kInputBurst, 3, GS_EXT_TYPE_NUMBER, nullptr, &sendInputs},
    {sizeof(gs_ext_node), "serve", kOneJson, 1, GS_EXT_TYPE_JSON, nullptr, &serve},
    {sizeof(gs_ext_node), "callService", kStringAndJson, 2, GS_EXT_TYPE_NUMBER, nullptr, &callServiceLater},
    {sizeof(gs_ext_node), "callServiceNow", kStringAndJson, 2, GS_EXT_TYPE_NUMBER, nullptr, &callServiceNow},
    {sizeof(gs_ext_node), "serviceResult", nullptr, 0, GS_EXT_TYPE_JSON, nullptr, &serviceResult},
    {sizeof(gs_ext_node), "holdSends", kOneFlag, 1, GS_EXT_TYPE_BOOLEAN, nullptr, &holdSends},
    {sizeof(gs_ext_node), "dropStreams", kOneString, 1, GS_EXT_TYPE_NUMBER, nullptr, &dropStreams},
    {sizeof(gs_ext_node), "lastOpen", nullptr, 0, GS_EXT_TYPE_JSON, nullptr, &lastOpen},
    {sizeof(gs_ext_node), "closedStreams", nullptr, 0, GS_EXT_TYPE_JSON, nullptr, &closedStreams},
    {sizeof(gs_ext_node), "setPhones", kOneJson, 1, GS_EXT_TYPE_NUMBER, nullptr, &setPhones},
    {sizeof(gs_ext_node), "tapStats", nullptr, 0, GS_EXT_TYPE_JSON, nullptr, &tapStats},
    {sizeof(gs_ext_node), "tapReset", nullptr, 0, GS_EXT_TYPE_BOOLEAN, nullptr, &tapReset},
    {sizeof(gs_ext_node), "tapPause", kOneNumber, 1, GS_EXT_TYPE_BOOLEAN, nullptr, &tapPause},
    {sizeof(gs_ext_node), "tapRefuse", kOneNumber, 1, GS_EXT_TYPE_BOOLEAN, nullptr, &tapRefuse},
    {sizeof(gs_ext_node), "aiModels", nullptr, 0, GS_EXT_TYPE_JSON, nullptr, &aiModelsNode},
    {sizeof(gs_ext_node), "lastAiRequest", nullptr, 0, GS_EXT_TYPE_JSON, nullptr, &lastAiRequest},
};

const gs_ext_transport kLoop = {sizeof(gs_ext_transport), "loop",    nullptr,   &loopDiscover,
                                &loopOpen,                &loopSend, &loopClose};

const gs_ext_device_provider kBoxes = {sizeof(gs_ext_device_provider), "boxes", nullptr, &listBoxes, &actOnBox};

const gs_ext_input_source kPad = {sizeof(gs_ext_input_source), "pad"};

const gs_ext_touch_method kRecorder = {
    sizeof(gs_ext_touch_method), "recorder", 4,          1,         1,           nullptr,    &openTouch,
    &closeTouch,                 &frameTouch, &keyTouch, &textTouch, &mouseTouch, &padTouch,
};

double numberIn(const std::string& params, const char* key, double fallback)
{
    const std::string wanted = std::string("\"") + key + "\":";
    const size_t at = params.find(wanted);
    return at == std::string::npos ? fallback : std::strtod(params.c_str() + at + wanted.size(), nullptr);
}

void pointIn(const std::string& params, const char* key, double& x, double& y)
{
    const std::string wanted = std::string("\"") + key + "\":[";
    const size_t at = params.find(wanted);
    if (at == std::string::npos) {
        return;
    }
    char* end = nullptr;
    x = std::strtod(params.c_str() + at + wanted.size(), &end);
    if (end != nullptr && *end == ',') {
        y = std::strtod(end + 1, nullptr);
    }
}

struct Shift {
    double dx = 0.0;
    double sleepMs = 0.0;
    uint64_t seed = 0;
    bool holding = false;
    gs_ext_input_event held{};
};

void napIfAsked(const Shift& shift)
{
    if (shift.sleepMs > 0.0) {
        Sleep(static_cast<DWORD>(shift.sleepMs));
    }
}

int32_t shiftStart(void*, const char* params, size_t size, uint64_t seed, void** state)
{
    const std::string text(params, size);
    auto* made = new Shift;
    made->dx = numberIn(text, "dx", 0.0);
    made->sleepMs = numberIn(text, "sleepMs", 0.0);
    made->seed = seed;
    *state = made;
    return GS_EXT_OK;
}

int32_t shiftInput(void* state, const gs_ext_input_event* event, gs_ext_output* out)
{
    Shift& shift = *static_cast<Shift*>(state);
    napIfAsked(shift);
    if (event->kind == GS_EXT_INPUT_KEY_DOWN && event->slot == 1) {
        shift.holding = true;
        shift.held = *event;
        out->event_count = 0;
        out->wake_at = event->at + 2000;
        return GS_EXT_OK;
    }
    if (event->kind == GS_EXT_INPUT_AXIS2) {
        out->events[0].x = event->x * 2.0;
    }
    return GS_EXT_OK;
}

int32_t shiftOutput(void* state, const gs_ext_touch_command* command, gs_ext_output* out)
{
    const Shift& shift = *static_cast<Shift*>(state);
    napIfAsked(shift);
    out->commands[0].x = command->x + shift.dx;
    out->commands[0].y = command->y + static_cast<double>(shift.seed % 100) / 10000.0;
    if (command->kind == GS_EXT_COMMAND_DOWN && out->command_capacity >= 2) {
        out->commands[1] = out->commands[0];
        out->commands[1].kind = GS_EXT_COMMAND_MOVE;
        out->commands[1].x += shift.dx;
        out->commands[1].at = command->at + 5000;
        out->command_count = 2;
    }
    return GS_EXT_OK;
}

int32_t shiftWake(void* state, uint64_t now, gs_ext_output* out)
{
    Shift& shift = *static_cast<Shift*>(state);
    if (shift.holding && out->event_capacity >= 1) {
        out->events[0] = shift.held;
        out->events[0].at = now;
        out->event_count = 1;
        shift.holding = false;
    }
    return GS_EXT_OK;
}

void shiftReset(void* state)
{
    static_cast<Shift*>(state)->holding = false;
}

void shiftStop(void* state)
{
    delete static_cast<Shift*>(state);
}

struct Tap {
    double x = 0.5;
    double y = 0.5;
    bool fail = false;
};

int32_t tapStart(void*, const char* params, size_t size, uint64_t, void** state)
{
    const std::string text(params, size);
    auto* made = new Tap;
    pointIn(text, "@position", made->x, made->y);
    made->fail = numberIn(text, "fail", 0.0) != 0.0;
    *state = made;
    return GS_EXT_OK;
}

int32_t tapInput(void* state, const gs_ext_input_event* event, gs_ext_output* out)
{
    const Tap& tap = *static_cast<Tap*>(state);
    if (tap.fail || out->command_capacity < 1) {
        return GS_EXT_FAILED;
    }
    if (event->kind == GS_EXT_INPUT_KEY_DOWN || event->kind == GS_EXT_INPUT_KEY_UP) {
        gs_ext_touch_command& made = out->commands[out->command_count++];
        made.kind = event->kind == GS_EXT_INPUT_KEY_DOWN ? static_cast<uint32_t>(GS_EXT_COMMAND_DOWN)
                                                         : static_cast<uint32_t>(GS_EXT_COMMAND_UP);
        made.finger = 0;
        made.x = tap.x;
        made.y = tap.y;
        made.at = event->at;
    }
    return GS_EXT_OK;
}

void tapStop(void* state)
{
    delete static_cast<Tap*>(state);
}

struct JobBook {
    std::mutex mutex;
    std::vector<std::thread> threads;
    std::map<uint64_t, std::shared_ptr<std::atomic<bool>>> stops;
};

JobBook& jobBook()
{
    static JobBook book;
    return book;
}

void sayJob(uint64_t job, bool worked, const std::string& text)
{
    if (worked) {
        g_host->job_finish(g_host->context, job, text.data(), text.size());
    } else {
        g_host->job_fail(g_host->context, job, text.data(), text.size());
    }
}

int32_t countStart(void*, uint64_t job, const char* args, size_t size)
{
    const std::string text(args, size);
    const int steps = static_cast<int>(numberIn(text, "steps", 1.0));
    const auto pause = static_cast<DWORD>(numberIn(text, "pause", 0.0));
    const bool fail = numberIn(text, "fail", 0.0) != 0.0;
    const bool output = numberIn(text, "output", 0.0) != 0.0;
    auto stop = std::make_shared<std::atomic<bool>>(false);
    JobBook& book = jobBook();
    const std::scoped_lock lock(book.mutex);
    book.stops[job] = stop;
    book.threads.emplace_back([job, steps, pause, fail, output, stop] {
        int busy = 0;
        for (int step = 1; step <= steps; ++step) {
            if (stop->load()) {
                sayJob(job, false, "cancelled");
                return;
            }
            const std::string line = "step " + std::to_string(step);
            g_host->job_progress(g_host->context, job, static_cast<double>(step) / steps, line.data(), line.size());
            if (step == 1) {
                constexpr char kCounting[] = "counting";
                g_host->job_log(g_host->context, job, GS_EXT_LOG_INFO, kCounting, sizeof(kCounting) - 1);
            }
            if (output) {
                const std::string value = "{\"step\":" + std::to_string(step) + "}";
                if (g_host->job_output(g_host->context, job, value.data(), value.size()) == GS_EXT_BUSY) {
                    ++busy;
                }
            }
            Sleep(pause);
        }
        if (output) {
            constexpr char kBroken[] = "{\"step\":";
            if (g_host->job_output(g_host->context, job, kBroken, sizeof(kBroken) - 1) != GS_EXT_BAD_ARGUMENT) {
                busy = -1;
            }
        }
        sayJob(job, !fail,
               fail     ? std::string("asked to fail")
               : output ? "{\"counted\":" + std::to_string(steps) + ",\"busy\":" + std::to_string(busy) + "}"
                        : "{\"counted\":" + std::to_string(steps) + "}");
    });
    return GS_EXT_OK;
}

void countCancel(void*, uint64_t job)
{
    JobBook& book = jobBook();
    const std::scoped_lock lock(book.mutex);
    if (const auto found = book.stops.find(job); found != book.stops.end()) {
        found->second->store(true);
    }
}

int32_t refuseStart(void*, uint64_t, const char*, size_t)
{
    return GS_EXT_FAILED;
}

std::vector<std::string> phrasesIn(const std::string& args)
{
    std::vector<std::string> phrases;
    constexpr std::string_view kKey = "\"phrases\":[";
    size_t at = args.find(kKey);
    if (at == std::string::npos) {
        return phrases;
    }
    at += kKey.size();
    while (at < args.size() && args[at] == '"') {
        const size_t end = args.find('"', at + 1);
        if (end == std::string::npos) {
            break;
        }
        phrases.push_back(args.substr(at + 1, end - at - 1));
        at = end + 1;
        if (at < args.size() && args[at] == ',') {
            ++at;
        }
    }
    return phrases;
}

void untilStopped(const std::shared_ptr<std::atomic<bool>>& stop)
{
    while (!stop->load()) {
        Sleep(5);
    }
}

void bookJob(uint64_t job, std::function<void(std::shared_ptr<std::atomic<bool>>)> work)
{
    auto stop = std::make_shared<std::atomic<bool>>(false);
    JobBook& book = jobBook();
    const std::scoped_lock lock(book.mutex);
    book.stops[job] = stop;
    book.threads.emplace_back([work = std::move(work), stop] { work(stop); });
}

int32_t speakStart(void*, uint64_t job, const char* args, size_t size)
{
    const std::string text(args, size);
    {
        const std::scoped_lock lock(g_mutex);
        g_spoken = text;
    }
    const bool waits = text.find("\"text\":\"wait\"") != std::string::npos;
    bookJob(job, [job, waits](const std::shared_ptr<std::atomic<bool>>& stop) {
        if (waits) {
            untilStopped(stop);
            sayJob(job, false, "stopped");
            return;
        }
        sayJob(job, true, "null");
    });
    return GS_EXT_OK;
}

int32_t hearStart(void*, uint64_t job, const char* args, size_t size)
{
    const std::string text(args, size);
    const std::vector<std::string> phrases = phrasesIn(text);
    const bool free = text.find("\"phrases\":null") != std::string::npos;
    bookJob(job, [job, phrases, free](const std::shared_ptr<std::atomic<bool>>& stop) {
        if (free) {
            constexpr char kHeard[] = R"({"phrase":null,"text":"free words","confidence":0.8})";
            g_host->job_output(g_host->context, job, kHeard, sizeof(kHeard) - 1);
        }
        for (const std::string& phrase : phrases) {
            const std::string confidence = phrase.starts_with("quiet") ? "0.3" : "0.9";
            const std::string heard =
                "{\"phrase\":\"" + phrase + "\",\"text\":\"" + phrase + "\",\"confidence\":" + confidence + "}";
            g_host->job_output(g_host->context, job, heard.data(), heard.size());
        }
        untilStopped(stop);
        sayJob(job, false, "stopped");
    });
    return GS_EXT_OK;
}

std::string requestFileOf(const std::string& args)
{
    constexpr std::string_view kKey = "\"file\":\"";
    const size_t at = args.find(kKey);
    if (at == std::string::npos) {
        return {};
    }
    std::string path;
    for (size_t index = at + kKey.size(); index < args.size() && args[index] != '"'; ++index) {
        if (args[index] == '\\' && index + 1 < args.size()) {
            ++index;
        }
        path += args[index];
    }
    const int wide = MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), nullptr, 0);
    std::wstring name(static_cast<size_t>(wide), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), name.data(), wide);
    const HANDLE file = CreateFileW(name.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }
    std::string contents;
    char buffer[4096] = {};
    DWORD got = 0;
    while (ReadFile(file, buffer, static_cast<DWORD>(sizeof(buffer)), &got, nullptr) && got > 0) {
        contents.append(buffer, got);
    }
    CloseHandle(file);
    return contents;
}

void outputPart(uint64_t job, const char* json)
{
    g_host->job_output(g_host->context, job, json, std::strlen(json));
}

int32_t aiChatStart(void*, uint64_t job, const char* args, size_t size)
{
    const std::string text(args, size);
    const std::string spilled = requestFileOf(text);
    {
        const std::scoped_lock lock(g_mutex);
        g_aiRequest = "{\"args\":" + text + ",\"file\":" + (spilled.empty() ? std::string("null") : spilled) + "}";
    }
    const std::string request = spilled.empty() ? text : spilled;
    const auto asked = [&request](const char* words) {
        return request.find(std::string("\"text\":\"") + words + "\"") != std::string::npos;
    };
    const bool fails = asked("fail");
    const bool junk = asked("junk");
    const bool calls = asked("call");
    const bool waits = asked("wait");
    bookJob(job, [job, fails, junk, calls, waits](const std::shared_ptr<std::atomic<bool>>& stop) {
        if (fails) {
            sayJob(job, false, "the test provider fails on purpose");
            return;
        }
        if (junk) {
            sayJob(job, true, "[1,2]");
            return;
        }
        if (calls) {
            outputPart(job, R"({"toolCall":{"id":"call_7","name":"read_help"}})");
            sayJob(job, true,
                   R"({"text":"","toolCalls":[{"id":"call_7","name":"read_help",)"
                   R"("arguments":{"topic":"connect"}}],"stop":"tool_use"})");
            return;
        }
        if (waits) {
            outputPart(job, R"({"text":"Hold on"})");
            untilStopped(stop);
            sayJob(job, false, "stopped");
            return;
        }
        outputPart(job, R"({"thinking":"Thinking.","text":"Hello "})");
        outputPart(job, R"({"text":"there."})");
        sayJob(job, true,
               R"({"text":"Hello there.","thinking":"Thinking.","stop":"end","usage":{"input":12,"output":3}})");
    });
    return GS_EXT_OK;
}

void refuseCancel(void*, uint64_t) {}

struct Producer {
    std::atomic<bool> stop{false};
    std::thread thread;
};

std::mutex g_producerMutex;
std::map<uint64_t, std::unique_ptr<Producer>> g_producers;

void endStream(uint64_t stream)
{
    constexpr char kDone[] = "done";
    g_host->stream_ended(g_host->context, stream, kDone, sizeof(kDone) - 1);
}

void sendPictures(uint64_t stream, const std::string& params, const std::atomic<bool>& stop)
{
    const int frames = static_cast<int>(numberIn(params, "frames", 0.0));
    const auto width = static_cast<uint32_t>(numberIn(params, "width", 64.0));
    const auto height = static_cast<uint32_t>(numberIn(params, "height", 48.0));
    const auto pause = static_cast<DWORD>(numberIn(params, "pause", 33.0));
    const bool planar = params.find("\"nv12\"") != std::string::npos;
    std::vector<uint8_t> pixels(planar ? std::size_t{width} * height * 3 / 2 : std::size_t{width} * height * 4);
    for (int index = 0; (frames == 0 || index < frames) && !stop.load(); ++index) {
        if (planar) {
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    const uint32_t shade = x + y + static_cast<uint32_t>(index);
                    pixels[std::size_t{y} * width + x] = static_cast<uint8_t>(shade & 0xFFU);
                }
            }
            uint8_t* chroma = pixels.data() + std::size_t{width} * height;
            for (std::size_t at = 0; at + 1 < std::size_t{width} * height / 2; at += 2) {
                chroma[at] = static_cast<uint8_t>(100 + index % 100);
                chroma[at + 1] = 150;
            }
        } else {
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    uint8_t* pixel = pixels.data() + (std::size_t{y} * width + x) * 4;
                    pixel[0] = static_cast<uint8_t>(x & 0xFFU);
                    pixel[1] = static_cast<uint8_t>(y & 0xFFU);
                    pixel[2] = static_cast<uint8_t>((index * 40) & 0xFF);
                    pixel[3] = 255;
                }
            }
        }
        gs_ext_video_frame frame{};
        frame.size = sizeof(gs_ext_video_frame);
        frame.format = planar ? GS_EXT_PIXEL_NV12 : GS_EXT_PIXEL_BGRA;
        frame.width = width;
        frame.height = height;
        frame.data = pixels.data();
        frame.stride = planar ? width : width * 4;
        frame.chroma = planar ? pixels.data() + std::size_t{width} * height : nullptr;
        frame.chroma_stride = planar ? width : 0;
        frame.time_us = g_host->now_us(g_host->context);
        if (g_host->video_frame(g_host->context, stream, &frame) != GS_EXT_OK) {
            return;
        }
        Sleep(pause);
    }
    if (!stop.load()) {
        endStream(stream);
    }
}

void sendTone(uint64_t stream, const std::string& params, const std::atomic<bool>& stop)
{
    const int blocks = static_cast<int>(numberIn(params, "blocks", 0.0));
    const auto pause = static_cast<DWORD>(numberIn(params, "pause", 10.0));
    constexpr uint32_t kFrames = 480;
    std::vector<int16_t> samples(std::size_t{kFrames} * 2);
    for (int block = 0; (blocks == 0 || block < blocks) && !stop.load(); ++block) {
        for (uint32_t frame = 0; frame < kFrames; ++frame) {
            const auto value = static_cast<int16_t>((block * 7 + static_cast<int>(frame)) % 1000);
            samples[std::size_t{frame} * 2] = value;
            samples[std::size_t{frame} * 2 + 1] = static_cast<int16_t>(-value);
        }
        gs_ext_audio audio{};
        audio.size = sizeof(gs_ext_audio);
        audio.format = GS_EXT_SAMPLE_S16;
        audio.channels = 2;
        audio.rate = GS_EXT_SOUND_RATE;
        audio.data = samples.data();
        audio.frames = kFrames;
        audio.time_us = g_host->now_us(g_host->context);
        if (g_host->audio_samples(g_host->context, stream, &audio) != GS_EXT_OK) {
            return;
        }
        Sleep(pause);
    }
    if (!stop.load()) {
        endStream(stream);
    }
}

using Produce = void (*)(uint64_t stream, const std::string& params, const std::atomic<bool>& stop);

int32_t openProducer(const char* id, size_t idSize, const char* params, size_t paramsSize, uint64_t stream,
                     Produce produce)
{
    if (std::string(id, idSize) != "bars") {
        return GS_EXT_FAILED;
    }
    auto producer = std::make_unique<Producer>();
    Producer* made = producer.get();
    const std::string text(params, paramsSize);
    const std::scoped_lock lock(g_producerMutex);
    made->thread = std::thread([stream, text, made, produce] { produce(stream, text, made->stop); });
    g_producers[stream] = std::move(producer);
    return GS_EXT_OK;
}

int32_t openBars(void*, const char* id, size_t idSize, const char* params, size_t paramsSize, uint64_t stream)
{
    return openProducer(id, idSize, params, paramsSize, stream, &sendPictures);
}

int32_t openTone(void*, const char* id, size_t idSize, const char* params, size_t paramsSize, uint64_t stream)
{
    return openProducer(id, idSize, params, paramsSize, stream, &sendTone);
}

void closeProducer(void*, uint64_t stream)
{
    std::unique_ptr<Producer> producer;
    {
        const std::scoped_lock lock(g_producerMutex);
        if (const auto found = g_producers.find(stream); found != g_producers.end()) {
            producer = std::move(found->second);
            g_producers.erase(found);
        }
    }
    if (producer != nullptr) {
        producer->stop.store(true);
        if (producer->thread.joinable()) {
            producer->thread.join();
        }
    }
}

void stopProducers()
{
    std::map<uint64_t, std::unique_ptr<Producer>> left;
    {
        const std::scoped_lock lock(g_producerMutex);
        left.swap(g_producers);
    }
    for (auto& [stream, producer] : left) {
        producer->stop.store(true);
        if (producer->thread.joinable()) {
            producer->thread.join();
        }
    }
}

int32_t listBars(void*, gs_ext_result* out)
{
    const std::string bars = "[{\"id\":\"bars\",\"name\":\"Test bars\"},{\"name\":\"no id\"}]";
    return g_host->set_text(out, GS_EXT_TYPE_JSON, bars.data(), bars.size());
}

int32_t listTone(void*, gs_ext_result* out)
{
    const std::string tone = "[{\"id\":\"bars\",\"name\":\"Test tone\"}]";
    return g_host->set_text(out, GS_EXT_TYPE_JSON, tone.data(), tone.size());
}

struct Collected {
    int opened = 0;
    int closed = 0;
    int frames = 0;
    int audioFrames = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps = 0;
    uint32_t channels = 0;
    std::string device;
    std::string name;
    uint8_t corner[4] = {};
    uint8_t centre[4] = {};
    bool refuse = false;
};

Collected g_collected;

int32_t openCollector(void*, const gs_ext_sink_info* info, void** state)
{
    const std::scoped_lock lock(g_mutex);
    ++g_collected.opened;
    g_collected.device = info->device != nullptr ? info->device : "";
    g_collected.name = info->name != nullptr ? info->name : "";
    g_collected.width = info->width;
    g_collected.height = info->height;
    g_collected.fps = info->fps;
    g_collected.channels = info->channels;
    *state = &g_collected;
    return GS_EXT_OK;
}

int32_t collectFrame(void*, const gs_ext_video_frame* frame)
{
    const std::scoped_lock lock(g_mutex);
    ++g_collected.frames;
    if (frame->data != nullptr && frame->width > 1 && frame->height > 1) {
        std::memcpy(g_collected.corner, frame->data, 4);
        std::memcpy(g_collected.centre,
                    frame->data + std::size_t{frame->height / 2} * frame->stride + std::size_t{frame->width / 2} * 4, 4);
    }
    return g_collected.refuse ? GS_EXT_FAILED : GS_EXT_OK;
}

int32_t collectAudio(void*, const gs_ext_audio* audio)
{
    const std::scoped_lock lock(g_mutex);
    g_collected.audioFrames += static_cast<int>(audio->frames);
    return GS_EXT_OK;
}

void closeCollector(void*)
{
    const std::scoped_lock lock(g_mutex);
    ++g_collected.closed;
}

void appendBytes(std::string& out, const uint8_t (&bytes)[4])
{
    out += '[';
    for (int index = 0; index < 4; ++index) {
        out += (index > 0 ? "," : "") + std::to_string(bytes[index]);
    }
    out += ']';
}

int32_t sinkReport(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    std::string out;
    {
        const std::scoped_lock lock(g_mutex);
        out = "{\"opened\":" + std::to_string(g_collected.opened) + ",\"closed\":" + std::to_string(g_collected.closed) +
              ",\"frames\":" + std::to_string(g_collected.frames) +
              ",\"audioFrames\":" + std::to_string(g_collected.audioFrames) +
              ",\"width\":" + std::to_string(g_collected.width) + ",\"height\":" + std::to_string(g_collected.height) +
              ",\"fps\":" + std::to_string(g_collected.fps) + ",\"channels\":" + std::to_string(g_collected.channels) +
              ",\"device\":";
        appendText(out, g_collected.device.data(), g_collected.device.size());
        out += ",\"name\":";
        appendText(out, g_collected.name.data(), g_collected.name.size());
        out += ",\"corner\":";
        appendBytes(out, g_collected.corner);
        out += ",\"centre\":";
        appendBytes(out, g_collected.centre);
        out += '}';
    }
    return giveText(result, GS_EXT_TYPE_JSON, out);
}

int32_t refuseFrames(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    {
        const std::scoped_lock lock(g_mutex);
        g_collected.refuse = args[0].flag != 0;
    }
    return giveFlag(result, true);
}

int32_t changeSources(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    constexpr char kName[] = "bars";
    result->value.type = GS_EXT_TYPE_NUMBER;
    result->value.number = g_host->sources_changed(g_host->context, kName, sizeof(kName) - 1);
    return GS_EXT_OK;
}

std::filesystem::path pathOf(const std::string& text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

int32_t startCopy(void*, uint64_t job, const char* input, size_t inputSize, const char* output, size_t outputSize)
{
    const std::string from(input, inputSize);
    const std::string to(output, outputSize);
    JobBook& book = jobBook();
    const std::scoped_lock lock(book.mutex);
    book.threads.emplace_back([job, from, to] {
        constexpr char kCopying[] = "copying";
        g_host->job_progress(g_host->context, job, 0.5, kCopying, sizeof(kCopying) - 1);
        std::ifstream in(pathOf(from), std::ios::binary);
        if (!in) {
            sayJob(job, false, "the input cannot be read");
            return;
        }
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::ofstream out(pathOf(to), std::ios::binary);
        out << "exported:" << bytes;
        out.close();
        if (!out) {
            sayJob(job, false, "the output cannot be written");
            return;
        }
        sayJob(job, true, "{\"bytes\":" + std::to_string(bytes.size()) + "}");
    });
    return GS_EXT_OK;
}

void cancelCopy(void*, uint64_t) {}

void unloadAll(void*)
{
    stopProducers();
    std::vector<std::thread> threads;
    {
        JobBook& book = jobBook();
        const std::scoped_lock lock(book.mutex);
        for (auto& [job, stop] : book.stops) {
            stop->store(true);
        }
        threads = std::move(book.threads);
    }
    for (std::thread& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    joinServiceCalls();
    stopLoop();
}

int32_t probeRun(void*, uint32_t kind, const gs_ext_image* image, const char* params, size_t size, gs_ext_result* out)
{
    const std::string text(params, size);
    const auto pause = static_cast<DWORD>(numberIn(text, "sleepMs", 0.0));
    if (pause > 0) {
        Sleep(pause);
    }
    if (numberIn(text, "fail", 0.0) != 0.0) {
        constexpr char kWhy[] = "the test engine fails on purpose";
        g_host->set_error(out, kWhy, sizeof(kWhy) - 1);
        return GS_EXT_FAILED;
    }
    if (numberIn(text, "refuse", 0.0) != 0.0) {
        constexpr char kWhy[] = "the test engine does not take refuse";
        g_host->set_error(out, kWhy, sizeof(kWhy) - 1);
        return GS_EXT_BAD_ARGUMENT;
    }
    if (numberIn(text, "crash", 0.0) != 0.0) {
        RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    }
    if (numberIn(text, "broken", 0.0) != 0.0) {
        return giveText(out, GS_EXT_TYPE_JSON, "{\"not\":\"a list\"}");
    }
    if (kind == GS_EXT_VISION_OCR) {
        return giveText(out, GS_EXT_TYPE_JSON,
                        R"([{"text":"OK","x":0.5,"y":0.25,"width":0.5,"height":0.25,"confidence":0.75}])");
    }
    uint64_t blue = 0;
    uint64_t green = 0;
    uint64_t red = 0;
    for (uint32_t y = 0; y < image->height; ++y) {
        const uint8_t* row = image->pixels + static_cast<size_t>(y) * image->stride;
        for (uint32_t x = 0; x < image->width; ++x) {
            blue += row[4 * static_cast<size_t>(x)];
            green += row[4 * static_cast<size_t>(x) + 1];
            red += row[4 * static_cast<size_t>(x) + 2];
        }
    }
    const uint64_t count = uint64_t{image->width} * image->height;
    char color[8] = {};
    std::snprintf(color, sizeof(color), "#%02X%02X%02X", static_cast<unsigned>(red / count),
                  static_cast<unsigned>(green / count), static_cast<unsigned>(blue / count));
    std::string json = "[{\"label\":\"" + std::string(color) + "\",\"confidence\":1},{\"label\":\"" +
                       std::to_string(image->width) + "x" + std::to_string(image->height) + "/" +
                       std::to_string(image->stride) + "\",\"confidence\":0.5},{\"label\":";
    appendText(json, params, size);
    json += ",\"confidence\":0.25}]";
    return giveText(out, GS_EXT_TYPE_JSON, json);
}

const gs_ext_vision_engine kProbe = {sizeof(gs_ext_vision_engine), "probe",
                                     (1U << GS_EXT_VISION_OCR) | (1U << GS_EXT_VISION_CLASSIFY), nullptr, &probeRun};

const gs_ext_job kCount = {sizeof(gs_ext_job), "count", nullptr, &countStart, &countCancel};
const gs_ext_job kRefuse = {sizeof(gs_ext_job), "refuse", nullptr, &refuseStart, &refuseCancel};
const gs_ext_job kSpeak = {sizeof(gs_ext_job), "speak", nullptr, &speakStart, &countCancel};
const gs_ext_job kHear = {sizeof(gs_ext_job), "hear", nullptr, &hearStart, &countCancel};
const gs_ext_job kAiChat = {sizeof(gs_ext_job), "aiChat", nullptr, &aiChatStart, &countCancel};

const gs_ext_attachment kShift = {
    sizeof(gs_ext_attachment), "shift", nullptr, &shiftStart, &shiftInput, &shiftOutput, &shiftWake, &shiftReset,
    &shiftStop,
};

const uint32_t kTapSlots[] = {GS_EXT_SLOT_KEY};

const gs_ext_behavior kTap = {
    sizeof(gs_ext_behavior), "tap", kTapSlots, 1, 1, 0, nullptr, &tapStart, &tapInput, nullptr, nullptr, nullptr,
    &tapStop,
};

const gs_ext_node kMediaNodes[] = {
    {sizeof(gs_ext_node), "sinkReport", nullptr, 0, GS_EXT_TYPE_JSON, nullptr, &sinkReport},
    {sizeof(gs_ext_node), "refuseFrames", kOneFlag, 1, GS_EXT_TYPE_BOOLEAN, nullptr, &refuseFrames},
    {sizeof(gs_ext_node), "changeSources", nullptr, 0, GS_EXT_TYPE_NUMBER, nullptr, &changeSources},
};

const gs_ext_video_source kBars = {sizeof(gs_ext_video_source), "bars", nullptr, &listBars, &openBars, &closeProducer};
const gs_ext_audio_source kTone = {sizeof(gs_ext_audio_source), "tone", nullptr, &listTone, &openTone, &closeProducer};
const gs_ext_media_sink kCollector = {sizeof(gs_ext_media_sink), "collector", nullptr, &openCollector, &collectFrame,
                                      &collectAudio, &closeCollector};
const gs_ext_recording_exporter kCopy = {sizeof(gs_ext_recording_exporter), "copy", nullptr, &startCopy, &cancelCopy};

int32_t counterOpen(void*, const gs_ext_tap_open* info, void** state)
{
    const std::scoped_lock lock(g_mutex);
    if (g_tap.refuse) {
        return GS_EXT_FAILED;
    }
    ++g_tap.opened;
    g_tap.device = info->device != nullptr ? info->device : "";
    g_tap.width = info->width;
    g_tap.height = info->height;
    g_tap.rotation = info->rotation;
    *state = &g_tap;
    return GS_EXT_OK;
}

int32_t counterFrame(void*, const gs_ext_frame* frame, gs_ext_overlay* overlay)
{
    double pause = 0.0;
    {
        const std::scoped_lock lock(g_mutex);
        ++g_tap.frames;
        g_tap.frameWidth = frame->width;
        g_tap.frameHeight = frame->height;
        g_tap.stride = frame->stride;
        g_tap.times.push_back(frame->time_us);
        const auto* pixel = static_cast<const unsigned char*>(frame->pixels);
        g_tap.first = "[" + std::to_string(pixel[0]) + "," + std::to_string(pixel[1]) + "," + std::to_string(pixel[2]) +
                      "," + std::to_string(pixel[3]) + "]";
        pause = g_tap.pauseMs;
    }
    if (pause > 0.0) {
        Sleep(static_cast<DWORD>(pause));
    }
    if (overlay->capacity < 1) {
        return GS_EXT_FAILED;
    }
    gs_ext_shape& box = overlay->shapes[0];
    box.kind = GS_EXT_SHAPE_RECT;
    box.color = GS_EXT_COLOR_GOOD;
    box.id = 1;
    box.x = 0.1;
    box.y = 0.2;
    box.x2 = 0.3;
    box.y2 = 0.4;
    box.text = "box";
    box.text_size = 3;
    overlay->count = 1;
    return GS_EXT_OK;
}

void counterClose(void*)
{
    const std::scoped_lock lock(g_mutex);
    ++g_tap.closed;
}

const gs_ext_frame_tap kCounter = {sizeof(gs_ext_frame_tap), "counter", nullptr, &counterOpen, &counterFrame,
                                   &counterClose};

const gs_ext_contribution kContributions[] = {
    {sizeof(gs_ext_contribution), GS_EXT_KIND_TOUCH_METHOD, "recorder", &kRecorder},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "add", &kNodes[0]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "echo", &kNodes[1]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "mirror", &kNodes[2]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "fail", &kNodes[3]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "crash", &kNodes[4]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "slow", &kNodes[5]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "remember", &kNodes[6]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "recall", &kNodes[7]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "allowed", &kNodes[8]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "notify", &kNodes[9]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "touches", &kNodes[10]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "breakTouch", &kNodes[11]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "wrongType", &kNodes[12]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "say", &kNodes[13]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "sayMany", &kNodes[14]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "setting", &kNodes[15]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "changedKeys", &kNodes[16]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "setBoxes", &kNodes[17]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "lastAction", &kNodes[18]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "claim", &kNodes[19]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "release", &kNodes[20]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "supports", &kNodes[21]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "yieldClaims", &kNodes[22]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "lastWanted", &kNodes[23]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "subscribe", &kNodes[26]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "heard", &kNodes[27]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "serve", &kNodes[30]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "callService", &kNodes[31]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "callServiceNow", &kNodes[32]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "serviceResult", &kNodes[33]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_DEVICE_PROVIDER, "boxes", &kBoxes},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_ATTACHMENT, "shift", &kShift},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_BEHAVIOR, "tap", &kTap},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_JOB, "count", &kCount},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_JOB, "refuse", &kRefuse},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "voices", &kNodes[24]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "lastSpoken", &kNodes[25]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_JOB, "speak", &kSpeak},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_JOB, "hear", &kHear},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "sendInput", &kNodes[28]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "sendInputs", &kNodes[29]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_INPUT_SOURCE, "pad", &kPad},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "holdSends", &kNodes[34]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "dropStreams", &kNodes[35]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "lastOpen", &kNodes[36]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "closedStreams", &kNodes[37]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "setPhones", &kNodes[38]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_TRANSPORT, "loop", &kLoop},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "sinkReport", &kMediaNodes[0]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "refuseFrames", &kMediaNodes[1]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "changeSources", &kMediaNodes[2]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_VIDEO_SOURCE, "bars", &kBars},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_AUDIO_SOURCE, "tone", &kTone},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_MEDIA_SINK, "collector", &kCollector},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_RECORDING_EXPORTER, "copy", &kCopy},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "tapStats", &kNodes[39]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "tapReset", &kNodes[40]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "tapPause", &kNodes[41]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "tapRefuse", &kNodes[42]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_FRAME_TAP, "counter", &kCounter},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "aiModels", &kNodes[43]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "lastAiRequest", &kNodes[44]},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_JOB, "aiChat", &kAiChat},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_VISION_ENGINE, "probe", &kProbe},
};

const gs_ext_plugin kPlugin = {
    sizeof(gs_ext_plugin),
    GS_EXT_ABI_VERSION,
    nullptr,
    static_cast<uint32_t>(sizeof(kContributions) / sizeof(kContributions[0])),
    kContributions,
    &unloadAll,
    &settingsChanged,
    &claimWanted,
    &onEvent,
};

}

GS_EXT_EXPORT int32_t gs_ext_main(const gs_ext_host* host, const gs_ext_plugin** plugin)
{
    if (host == nullptr || plugin == nullptr || host->abi_version != GS_EXT_ABI_VERSION) {
        return GS_EXT_UNSUPPORTED;
    }
    g_host = host;
    *plugin = &kPlugin;
    return GS_EXT_OK;
}
