#include "gs/script/Replay.h"

#include "Engine.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <deque>
#include <format>
#include <iterator>
#include <memory>

namespace gs::script {

namespace {

constexpr int kMostEchoes = 1000;
constexpr ScriptId kReplayScript = 1;

std::string millisecondsText(Microseconds at)
{
    return std::format("{}", static_cast<double>(at.count()) / 1000.0);
}

}

std::string formatLine(const ReplayLine& line)
{
    return millisecondsText(line.at) + (line.output ? " " : " # ") + line.text;
}

RecordingServices::RecordingServices(const Clock& clock, DeviceInfo device, std::vector<ControlInfo> controls,
                                     std::vector<StateInfo> states)
    : m_clock(clock)
    , m_device(std::move(device))
    , m_controls(std::move(controls))
    , m_states(std::move(states))
{
}

DeviceInfo RecordingServices::device()
{
    return m_device;
}

void RecordingServices::tap(std::uint64_t gesture, Point at, double durationMs)
{
    start(gesture, std::format("tap {} {} {}", at.x, at.y, durationMs), durationMs);
}

void RecordingServices::swipe(std::uint64_t gesture, Point from, Point to, double durationMs)
{
    start(gesture, std::format("swipe {} {} {} {} {}", from.x, from.y, to.x, to.y, durationMs), durationMs);
}

void RecordingServices::lift(std::uint64_t gesture)
{
    const auto found = m_gestures.find(gesture);
    if (found == m_gestures.end()) {
        return;
    }
    std::string text = "lift " + found->second.text;
    m_gestures.erase(found);
    record(std::move(text));
}

void RecordingServices::start(std::uint64_t gesture, std::string text, double durationMs)
{
    const Microseconds now = m_clock.now();
    std::erase_if(m_gestures, [now](const auto& entry) { return entry.second.ends <= now; });
    record(text);
    m_gestures.insert_or_assign(
        gesture, Gesture{.text = std::move(text),
                         .ends = now + Microseconds{static_cast<std::int64_t>(std::ceil(durationMs * 1000.0))}});
}

void RecordingServices::key(std::string_view name, bool down)
{
    record(std::format("key {} {}", name, down ? "down" : "up"));
}

std::vector<ControlInfo> RecordingServices::controls()
{
    return m_controls;
}

bool RecordingServices::pressControl(std::string_view id, bool down)
{
    if (std::ranges::find(m_controls, id, &ControlInfo::id) == m_controls.end()) {
        return false;
    }
    record(std::format("control {} {}", id, down ? "down" : "up"));
    return true;
}

bool RecordingServices::enableControl(std::string_view id, bool enabled)
{
    const auto found = std::ranges::find(m_controls, id, &ControlInfo::id);
    if (found == m_controls.end()) {
        return false;
    }
    found->enabled = enabled;
    record(std::format("enable {} {}", id, enabled ? "on" : "off"));
    return true;
}

std::vector<StateInfo> RecordingServices::states()
{
    return m_states;
}

std::optional<VarValue> RecordingServices::stateValue(std::string_view id)
{
    const auto found = std::ranges::find(m_states, id, &StateInfo::id);
    if (found == m_states.end()) {
        return std::nullopt;
    }
    return found->value;
}

bool RecordingServices::setState(std::string_view id, const VarValue& value)
{
    const auto found = std::ranges::find(m_states, id, &StateInfo::id);
    if (found == m_states.end()) {
        return false;
    }
    const bool* on = std::get_if<bool>(&value);
    record(std::format("state {} {}", id, on != nullptr ? (*on ? "on" : "off") : valueJson(value)));
    if (found->value != value) {
        found->value = value;
        m_changes.emplace_back(std::string(id), value);
    }
    return true;
}

std::optional<std::string> RecordingServices::loadStore(std::string_view owner)
{
    const auto found = m_stores.find(owner);
    if (found == m_stores.end()) {
        return std::nullopt;
    }
    return found->second;
}

void RecordingServices::saveStore(std::string_view owner, std::string_view contents)
{
    m_stores.insert_or_assign(std::string(owner), std::string(contents));
}

void RecordingServices::note(std::string text)
{
    m_lines.push_back(ReplayLine{.at = m_clock.now(), .text = std::move(text), .output = false});
}

std::string RecordingServices::app(std::string_view request, std::string_view json)
{
    if (request == "progress" || request == "notify") {
        const nlohmann::json value = nlohmann::json::parse(json, nullptr, false);
        const auto text = [&value](const char* key) {
            const auto found = value.is_object() ? value.find(key) : value.end();
            return found != value.end() && found->is_string() ? found->get<std::string>() : std::string();
        };
        if (request == "notify") {
            record(std::format("notify {}{}{}", text("title"), text("text").empty() ? "" : ": ", text("text")));
            return "true";
        }
        const auto found = value.is_object() ? value.find("value") : value.end();
        if (found == value.end() || !found->is_number()) {
            record("progress cleared");
        } else {
            record(std::format("progress {} {}%", text("title"), std::lround(found->get<double>() * 100)));
        }
        return "true";
    }
    if (request != "machineState") {
        return "null";
    }
    const nlohmann::json value = nlohmann::json::parse(json, nullptr, false);
    if (!value.is_object()) {
        return "null";
    }
    const auto text = [&value](const char* key) {
        const auto found = value.find(key);
        return found != value.end() && found->is_string() ? found->get<std::string>() : std::string();
    };
    const auto found = value.find("line");
    ReplayMachineState state{.at = m_clock.now(),
                             .file = text("file"),
                             .line = found != value.end() && found->is_number_integer() ? found->get<int>() : 0,
                             .name = text("name"),
                             .state = text("state"),
                             .reason = text("reason")};
    const std::string label = state.name.empty() ? std::format("{}:{}", state.file, state.line) : state.name;
    note(state.state.empty()    ? std::format("machine {}: ended", label)
         : state.reason.empty() ? std::format("machine {}: {}", label, state.state)
                                : std::format("machine {}: {} ({})", label, state.state, state.reason));
    m_machineStates.push_back(std::move(state));
    return "null";
}

void RecordingServices::setServiceHook(std::function<bool(std::string_view service)> offers,
                                       std::function<ServiceAnswer(std::string_view service, std::string_view json)> call)
{
    m_hookOffers = std::move(offers);
    m_hookCall = std::move(call);
}

bool RecordingServices::offersService(std::string_view service)
{
    return service == "panel.show" || service == "ai.ask" || (m_hookOffers && m_hookCall && m_hookOffers(service));
}

ServiceAnswer RecordingServices::callService(std::string_view service, std::string_view json)
{
    if (service != "panel.show") {
        if (m_hookOffers && m_hookCall && m_hookOffers(service)) {
            return m_hookCall(service, json);
        }
        return ScriptServices::callService(service, json);
    }
    const nlohmann::json value = nlohmann::json::parse(json, nullptr, false);
    const auto found = value.is_object() ? value.find("kind") : value.end();
    record("panel " + (found != value.end() && found->is_string() ? found->get<std::string>() : std::string("?")));
    return {"true", {}, {}};
}

void RecordingServices::requestService(std::uint64_t request, std::string_view service, std::string_view json)
{
    if (service != "ai.ask") {
        m_refusals.emplace_back(request, "GS-API-070 no part of GameScrcpy offers the service " + std::string(service));
        return;
    }
    const nlohmann::json value = nlohmann::json::parse(json, nullptr, false);
    const auto question = value.is_object() ? value.find("question") : value.end();
    const auto picture = value.is_object() ? value.find("picture") : value.end();
    std::size_t characters = 0;
    if (question != value.end() && question->is_string()) {
        for (const char c : question->get_ref<const std::string&>()) {
            const auto byte = static_cast<unsigned char>(c);
            if ((byte & 0xC0U) != 0x80U) {
                characters += byte >= 0xF0 ? std::size_t{2} : std::size_t{1};
            }
        }
    }
    const bool pictured = picture != value.end() && picture->is_boolean() && picture->get<bool>();
    record(std::format("ai ask ({} character{}, {})", characters, characters == 1 ? "" : "s",
                       pictured ? "picture" : "no picture"));
    m_replies.emplace_back(request, "null");
}

void RecordingServices::toast(std::string_view text)
{
    record("toast " + std::string(text));
}

void RecordingServices::ask(std::uint64_t request, const Prompt& prompt)
{
    static constexpr std::string_view kKinds[] = {"confirm", "choose", "ask"};
    std::string line = "ask " + std::string(kKinds[static_cast<std::size_t>(prompt.kind)]) + " " + prompt.question;
    for (const std::string& choice : prompt.choices) {
        line += " | " + choice;
    }
    record(std::move(line));
    m_asked.emplace_back(request, prompt.kind);
    m_open.insert(request);
}

void RecordingServices::withdraw(std::uint64_t request)
{
    if (m_open.erase(request) > 0) {
        record("ask withdrawn");
    }
}

bool RecordingServices::answer(std::uint64_t request, std::string_view reply)
{
    if (m_open.erase(request) == 0) {
        return false;
    }
    record("answer " + std::string(reply));
    return true;
}

void RecordingServices::draw(std::uint64_t id, const Drawing& drawing)
{
    if (drawing.kind == DrawingKind::Box) {
        record(std::format("draw {} box {} {} {} {} {}{}{}", id, drawing.at.x, drawing.at.y, drawing.width,
                           drawing.height, drawing.color, drawing.text.empty() ? "" : " ", drawing.text));
    } else if (drawing.kind == DrawingKind::Mark) {
        record(std::format("draw {} mark {} {} {}{}{}", id, drawing.at.x, drawing.at.y, drawing.color,
                           drawing.text.empty() ? "" : " ", drawing.text));
    } else if (drawing.kind == DrawingKind::Image) {
        const std::string source =
            drawing.capture != 0 ? std::format("captured image {}", drawing.capture) : drawing.image;
        record(std::format("draw {} image {} {} {} {} {}{}{}", id, drawing.at.x, drawing.at.y, drawing.width,
                           drawing.height, source, drawing.text.empty() ? "" : " ", drawing.text));
    } else {
        record(std::format("draw {} text {} {} {} {}", id, drawing.at.x, drawing.at.y, drawing.color, drawing.text));
    }
}

void RecordingServices::erase(std::uint64_t id)
{
    record(std::format("erase {}", id));
}

void RecordingServices::capture(std::uint64_t request, const CaptureQuery& query)
{
    record(std::format("vision capture {} {} {} {}", query.region.x, query.region.y, query.region.width,
                       query.region.height));
    if (m_vision != nullptr) {
        reply(request, m_vision->capture(m_clock.now(), query));
        return;
    }
    m_replies.emplace_back(request, "null");
}

void RecordingServices::changed(std::uint64_t request, const ChangeQuery& query)
{
    record(std::format("vision changed {} {} {} {}", query.region.x, query.region.y, query.region.width,
                       query.region.height));
    if (m_vision != nullptr) {
        reply(request, m_vision->changed(m_clock.now(), query));
        return;
    }
    m_replies.emplace_back(request, "false");
}

void RecordingServices::readText(std::uint64_t request, const TextQuery& query)
{
    if (query.find) {
        record("vision findText " + query.text);
    } else {
        record(std::format("vision readText {} {} {} {}", query.region.x, query.region.y, query.region.width,
                           query.region.height));
    }
    if (m_vision != nullptr) {
        reply(request, m_vision->readText(m_clock.now(), query));
        return;
    }
    m_replies.emplace_back(request, "null");
}

void RecordingServices::saveCapture(std::uint64_t request, std::uint64_t capture, std::string_view name)
{
    record(std::format("vision save captured image {} as {}", capture, name));
    m_replies.emplace_back(request, "null");
}

std::optional<std::string> RecordingServices::tuneControl(std::string_view id, std::string_view change)
{
    const auto found = std::ranges::find(m_controls, id, &ControlInfo::id);
    if (found == m_controls.end()) {
        return std::nullopt;
    }
    record(std::format("tune {} {}", id, change));
    return found->props;
}

void RecordingServices::saveProfile(std::uint64_t request)
{
    record("profile save");
    m_replies.emplace_back(request, "false");
}

void RecordingServices::find(std::uint64_t request, const FindQuery& query)
{
    record(std::format("vision {} {}", query.all ? "findAll" : "find", query.image));
    if (m_vision != nullptr) {
        reply(request, m_vision->find(m_clock.now(), query));
        return;
    }
    m_replies.emplace_back(request, query.all ? "[]" : "null");
}

void RecordingServices::findColor(std::uint64_t request, const ColorQuery& query)
{
    record("vision findColor " + query.color);
    if (m_vision != nullptr) {
        reply(request, m_vision->findColor(m_clock.now(), query));
        return;
    }
    m_replies.emplace_back(request, "null");
}

void RecordingServices::readClipboard(std::uint64_t request)
{
    record("clipboard read");
    m_replies.emplace_back(request, "null");
}

void RecordingServices::writeClipboard(std::uint64_t request, std::string_view text)
{
    record("clipboard write " + std::string(text));
    m_replies.emplace_back(request, "false");
}

void RecordingServices::pickColor(std::uint64_t request, const PixelQuery& query)
{
    if (query.match) {
        record(std::format("vision matchColor {} {} {}", query.at.x, query.at.y, query.color));
    } else {
        record(std::format("vision pickColor {} {}", query.at.x, query.at.y));
    }
    if (m_vision != nullptr) {
        reply(request, m_vision->pickColor(m_clock.now(), query));
        return;
    }
    m_replies.emplace_back(request, query.match ? "false" : "null");
}

void RecordingServices::reply(std::uint64_t request, const VisionAnswer& answer)
{
    if (!answer.problem.empty()) {
        note("vision refused " + answer.problem);
        m_refusals.emplace_back(request, answer.problem);
        return;
    }
    note("vision answer " + answer.json);
    m_replies.emplace_back(request, answer.json);
}

std::vector<std::pair<std::uint64_t, std::string>> RecordingServices::takeReplies()
{
    return std::exchange(m_replies, {});
}

std::vector<std::pair<std::uint64_t, std::string>> RecordingServices::takeRefusals()
{
    return std::exchange(m_refusals, {});
}

std::vector<std::pair<std::uint64_t, PromptKind>> RecordingServices::takeAsked()
{
    return std::exchange(m_asked, {});
}

bool RecordingServices::applyState(std::string_view id, bool value)
{
    const auto found = std::ranges::find(m_states, id, &StateInfo::id);
    if (found == m_states.end()) {
        return false;
    }
    found->value = value;
    return true;
}

std::vector<std::pair<std::string, VarValue>> RecordingServices::takeStateChanges()
{
    return std::exchange(m_changes, {});
}

std::vector<ReplayLine> RecordingServices::outputs() const
{
    std::vector<ReplayLine> result;
    std::ranges::copy_if(m_lines, std::back_inserter(result), &ReplayLine::output);
    return result;
}

void RecordingServices::record(std::string text)
{
    m_lines.push_back(ReplayLine{.at = m_clock.now(), .text = std::move(text), .output = true});
}

ReplayResult replay(const ReplayOptions& options, RecordingServices& services, ManualClock& clock,
                    const ScriptHost::Sink& sink)
{
    clock.set(Microseconds{0});
    ReplayResult result;
    bool loaded = false;
    const ScriptHost::Sink watcher = [&loaded, &sink](const Report& report) {
        if (report.script == kReplayScript && report.task == 0 && report.kind == ReportKind::ScriptLoaded) {
            loaded = true;
        }
        if (sink) {
            sink(report);
        }
    };
    std::atomic<bool> stopRequested{false};
    std::atomic<TaskId> taskIds{0};
    detail::Engine engine(watcher, options.limits, stopRequested, taskIds,
                          detail::EngineOptions{.services = std::shared_ptr<ScriptServices>(std::shared_ptr<void>(), &services),
                                                .clock = &clock,
                                                .seed = options.seed,
                                                .wallEpochMs = options.wallEpochMs});
    const auto echo = [&engine, &services] {
        for (int round = 0; round < kMostEchoes; ++round) {
            const std::vector<std::pair<std::string, VarValue>> changes = services.takeStateChanges();
            if (changes.empty()) {
                return;
            }
            for (const auto& [id, value] : changes) {
                engine.event("state", id, valueJson(value));
            }
        }
    };
    const double rate = std::clamp(options.frameRate, 1.0, 240.0);
    const Microseconds frameStep{static_cast<std::int64_t>(std::llround(1'000'000.0 / rate))};
    std::vector<SessionEvent> events = options.events;
    std::ranges::stable_sort(events, {}, &SessionEvent::at);
    std::size_t next = 0;
    std::vector<std::pair<std::uint64_t, PromptKind>> pending;
    std::deque<std::string> answers;
    const auto settleAsks = [&] {
        for (int round = 0; round < kMostEchoes; ++round) {
            const std::vector<std::pair<std::uint64_t, std::string>> replies = services.takeReplies();
            for (const auto& [request, reply] : replies) {
                engine.answer(request, reply);
                echo();
            }
            const std::vector<std::pair<std::uint64_t, std::string>> refusals = services.takeRefusals();
            for (const auto& [request, message] : refusals) {
                engine.refuse(request, message);
                echo();
            }
            std::ranges::move(services.takeAsked(), std::back_inserter(pending));
            if (pending.empty()) {
                if (replies.empty() && refusals.empty()) {
                    return;
                }
                continue;
            }
            const bool later = std::any_of(events.begin() + static_cast<std::ptrdiff_t>(next), events.end(),
                                           [](const SessionEvent& event) { return event.kind == SessionEventKind::Answer; });
            std::string reply;
            if (!answers.empty()) {
                reply = std::move(answers.front());
                answers.pop_front();
            } else if (!later) {
                reply = pending.front().second == PromptKind::Confirm ? "false" : "null";
            } else {
                return;
            }
            const std::uint64_t request = pending.front().first;
            pending.erase(pending.begin());
            if (services.answer(request, reply)) {
                engine.answer(request, reply);
                echo();
            }
        }
    };
    engine.load(kReplayScript, options.source);
    echo();
    settleAsks();
    Microseconds nextFrame = frameStep;
    for (;;) {
        const Microseconds now = clock.now();
        while (next < events.size() && events[next].at <= now) {
            const SessionEvent& event = events[next++];
            switch (event.kind) {
            case SessionEventKind::Press:
                engine.controlEvent(event.id, true);
                break;
            case SessionEventKind::Release:
                engine.controlEvent(event.id, false);
                break;
            case SessionEventKind::State:
                if (services.applyState(event.id, event.value)) {
                    engine.stateEvent(event.id, event.value);
                }
                break;
            case SessionEventKind::Run:
                engine.start(taskIds.fetch_add(1, std::memory_order_relaxed) + 1, kReplayScript, event.id);
                break;
            case SessionEventKind::Answer:
                answers.push_back(event.id);
                break;
            }
            echo();
            settleAsks();
        }
        engine.fireTimers();
        echo();
        settleAsks();
        if (engine.waitingForFrame() && nextFrame <= now) {
            engine.frame();
            nextFrame = now + frameStep;
            echo();
            settleAsks();
        }
        std::optional<Microseconds> due = engine.nextTimer();
        const auto consider = [&due](Microseconds candidate) {
            if (!due || candidate < *due) {
                due = candidate;
            }
        };
        if (next < events.size()) {
            consider(events[next].at);
        }
        if (engine.waitingForFrame()) {
            consider(std::max(nextFrame, now + Microseconds{1}));
        }
        if (!due) {
            break;
        }
        if (*due > options.until) {
            clock.set(options.until);
            result.reachedLimit = true;
            break;
        }
        clock.set(*due > now ? *due : now + Microseconds{1});
    }
    result.ended = clock.now();
    engine.shutdown();
    result.loaded = loaded;
    return result;
}

}
