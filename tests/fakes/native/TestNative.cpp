#include <gamescrcpy_ext.h>

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
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
};

const gs_ext_device_provider kBoxes = {sizeof(gs_ext_device_provider), "boxes", nullptr, &listBoxes, &actOnBox};

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
    auto stop = std::make_shared<std::atomic<bool>>(false);
    JobBook& book = jobBook();
    const std::scoped_lock lock(book.mutex);
    book.stops[job] = stop;
    book.threads.emplace_back([job, steps, pause, fail, stop] {
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
            Sleep(pause);
        }
        sayJob(job, !fail, fail ? std::string("asked to fail") : "{\"counted\":" + std::to_string(steps) + "}");
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

void refuseCancel(void*, uint64_t) {}

void unloadAll(void*)
{
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
}

const gs_ext_job kCount = {sizeof(gs_ext_job), "count", nullptr, &countStart, &countCancel};
const gs_ext_job kRefuse = {sizeof(gs_ext_job), "refuse", nullptr, &refuseStart, &refuseCancel};

const gs_ext_attachment kShift = {
    sizeof(gs_ext_attachment), "shift", nullptr, &shiftStart, &shiftInput, &shiftOutput, &shiftWake, &shiftReset,
    &shiftStop,
};

const uint32_t kTapSlots[] = {GS_EXT_SLOT_KEY};

const gs_ext_behavior kTap = {
    sizeof(gs_ext_behavior), "tap", kTapSlots, 1, 1, 0, nullptr, &tapStart, &tapInput, nullptr, nullptr, nullptr,
    &tapStop,
};

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
    {sizeof(gs_ext_contribution), GS_EXT_KIND_DEVICE_PROVIDER, "boxes", &kBoxes},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_ATTACHMENT, "shift", &kShift},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_BEHAVIOR, "tap", &kTap},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_JOB, "count", &kCount},
    {sizeof(gs_ext_contribution), GS_EXT_KIND_JOB, "refuse", &kRefuse},
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
