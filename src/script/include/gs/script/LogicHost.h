#pragma once

#include "gs/script/Report.h"

#include <gs/base/Clock.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::script {

using SourceTransform = std::function<std::optional<std::string>(const std::string& module, const std::string& source)>;
using ScriptFiles = std::map<std::string, std::string, std::less<>>;

struct Limits {
    std::size_t memoryWarningBytes = std::size_t{48} << 20;
    std::size_t memoryLimitBytes = std::size_t{64} << 20;
    std::size_t stackBytes = std::size_t{1} << 20;
    Microseconds sliceBudget{200'000};
    Microseconds spikeThreshold{8'000};
    Microseconds sustainedWindow{1'000'000};
    Microseconds sustainedBusy{500'000};
};

struct ComponentModule {
    std::string ref;
    std::filesystem::path folder;
    std::string entry = "component.ts";
    bool placed = true;
    std::vector<std::string> children;
};

struct ScriptSource {
    std::filesystem::path folder;
    std::string entry = "main.js";
    std::vector<std::string> modules;
    std::vector<ComponentModule> components;
    SourceTransform transform;
    std::shared_ptr<const ScriptFiles> files;
    std::optional<std::vector<std::string>> permissions;
    bool phone = true;
};

[[nodiscard]] std::vector<std::string> controlModules(const std::filesystem::path& folder);

inline constexpr std::string_view kConvertHandler = "@convert";

class LogicHost
{
public:
    using Sink = std::function<void(const Report&)>;

    LogicHost() = default;
    virtual ~LogicHost() = default;
    LogicHost(const LogicHost&) = delete;
    LogicHost& operator=(const LogicHost&) = delete;

    [[nodiscard]] virtual bool started() const noexcept = 0;
    [[nodiscard]] virtual std::string problem() const = 0;

    [[nodiscard]] virtual ScriptId load(ScriptSource source) = 0;
    virtual void unload(ScriptId script) = 0;
    [[nodiscard]] virtual TaskId start(ScriptId script, std::string handler) = 0;
    virtual void cancel(TaskId task) = 0;
    virtual void frame() = 0;
    virtual void setFrameRate(double hz) = 0;
    virtual void controlEvent(std::string id, bool down) = 0;
    virtual void stateEvent(std::string id, bool value) = 0;
    virtual void event(std::string kind, std::string id, std::string json) = 0;
    virtual void answer(std::uint64_t request, std::string json) = 0;
    virtual void refuse(std::uint64_t request, std::string message) = 0;
    virtual void setBreakpoints(std::vector<std::string> places) = 0;
    virtual void resume(std::uint64_t pause, StepMode mode) = 0;
};

}
