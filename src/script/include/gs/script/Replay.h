#pragma once

#include "gs/script/ScriptHost.h"
#include "gs/script/Services.h"
#include "gs/script/Session.h"

#include <gs/base/Clock.h>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::script {

struct ReplayLine {
    Microseconds at{0};
    std::string text;
    bool output = true;

    friend bool operator==(const ReplayLine&, const ReplayLine&) = default;
};

[[nodiscard]] std::string formatLine(const ReplayLine& line);

struct ReplayMachineState {
    Microseconds at{0};
    std::string file;
    int line = 0;
    std::string name;
    std::string state;
    std::string reason;

    friend bool operator==(const ReplayMachineState&, const ReplayMachineState&) = default;
};

class ReplayVision
{
public:
    virtual ~ReplayVision() = default;

    [[nodiscard]] virtual VisionAnswer find(Microseconds at, const FindQuery& query) = 0;
    [[nodiscard]] virtual VisionAnswer findColor(Microseconds at, const ColorQuery& query) = 0;
    [[nodiscard]] virtual VisionAnswer pickColor(Microseconds at, const PixelQuery& query) = 0;
    [[nodiscard]] virtual VisionAnswer capture(Microseconds at, const CaptureQuery& query) = 0;
    [[nodiscard]] virtual VisionAnswer changed(Microseconds at, const ChangeQuery& query) = 0;
    [[nodiscard]] virtual VisionAnswer readText(Microseconds at, const TextQuery& query) = 0;
};

class RecordingServices final : public ScriptServices
{
public:
    RecordingServices(const Clock& clock, DeviceInfo device, std::vector<ControlInfo> controls,
                      std::vector<StateInfo> states);

    void setVision(ReplayVision* vision) noexcept { m_vision = vision; }
    DeviceInfo device() override;
    void tap(std::uint64_t gesture, Point at, double durationMs) override;
    void swipe(std::uint64_t gesture, Point from, Point to, double durationMs) override;
    void lift(std::uint64_t gesture) override;
    void key(std::string_view name, bool down) override;
    std::vector<ControlInfo> controls() override;
    bool pressControl(std::string_view id, bool down) override;
    bool enableControl(std::string_view id, bool enabled) override;
    std::vector<StateInfo> states() override;
    std::optional<VarValue> stateValue(std::string_view id) override;
    bool setState(std::string_view id, const VarValue& value) override;
    std::optional<std::string> loadStore(std::string_view owner) override;
    void saveStore(std::string_view owner, std::string_view contents) override;
    void toast(std::string_view text) override;
    void ask(std::uint64_t request, const Prompt& prompt) override;
    void withdraw(std::uint64_t request) override;
    void draw(std::uint64_t id, const Drawing& drawing) override;
    void erase(std::uint64_t id) override;
    void find(std::uint64_t request, const FindQuery& query) override;
    void findColor(std::uint64_t request, const ColorQuery& query) override;
    void readClipboard(std::uint64_t request) override;
    void writeClipboard(std::uint64_t request, std::string_view text) override;
    void pickColor(std::uint64_t request, const PixelQuery& query) override;
    void capture(std::uint64_t request, const CaptureQuery& query) override;
    void changed(std::uint64_t request, const ChangeQuery& query) override;
    void readText(std::uint64_t request, const TextQuery& query) override;
    void saveCapture(std::uint64_t request, std::uint64_t capture, std::string_view name) override;
    [[nodiscard]] std::optional<std::string> tuneControl(std::string_view id, std::string_view change) override;
    void saveProfile(std::uint64_t request) override;
    [[nodiscard]] std::string app(std::string_view request, std::string_view json) override;
    [[nodiscard]] bool offersService(std::string_view service) override;
    [[nodiscard]] ServiceAnswer callService(std::string_view service, std::string_view json) override;
    void requestService(std::uint64_t request, std::string_view service, std::string_view json) override;

    void note(std::string text);
    bool answer(std::uint64_t request, std::string_view reply);
    bool applyState(std::string_view id, bool value);
    [[nodiscard]] std::vector<std::pair<std::string, VarValue>> takeStateChanges();
    [[nodiscard]] std::vector<std::pair<std::uint64_t, PromptKind>> takeAsked();
    [[nodiscard]] std::vector<std::pair<std::uint64_t, std::string>> takeReplies();
    [[nodiscard]] std::vector<std::pair<std::uint64_t, std::string>> takeRefusals();
    [[nodiscard]] const std::vector<ReplayLine>& lines() const noexcept { return m_lines; }
    [[nodiscard]] std::vector<ReplayLine> outputs() const;
    [[nodiscard]] const std::vector<ReplayMachineState>& machineStates() const noexcept { return m_machineStates; }

private:
    struct Gesture {
        std::string text;
        Microseconds ends{0};
    };

    void record(std::string text);
    void start(std::uint64_t gesture, std::string text, double durationMs);
    void reply(std::uint64_t request, const VisionAnswer& answer);

    const Clock& m_clock;
    DeviceInfo m_device;
    std::vector<ControlInfo> m_controls;
    std::vector<StateInfo> m_states;
    std::map<std::string, std::string, std::less<>> m_stores;
    std::vector<std::pair<std::string, VarValue>> m_changes;
    std::vector<std::pair<std::uint64_t, PromptKind>> m_asked;
    std::vector<std::pair<std::uint64_t, std::string>> m_replies;
    std::vector<std::pair<std::uint64_t, std::string>> m_refusals;
    std::set<std::uint64_t> m_open;
    std::map<std::uint64_t, Gesture> m_gestures;
    std::vector<ReplayLine> m_lines;
    std::vector<ReplayMachineState> m_machineStates;
    ReplayVision* m_vision = nullptr;
};

struct ReplayOptions {
    ScriptSource source;
    std::vector<SessionEvent> events;
    Microseconds until{60'000'000};
    std::uint64_t seed = 1;
    std::int64_t wallEpochMs = kDefaultSessionStartMs;
    double frameRate = 60.0;
    Limits limits;
};

struct ReplayResult {
    bool loaded = false;
    bool reachedLimit = false;
    Microseconds ended{0};
};

[[nodiscard]] ReplayResult replay(const ReplayOptions& options, RecordingServices& services, ManualClock& clock,
                                  const ScriptHost::Sink& sink);

}
