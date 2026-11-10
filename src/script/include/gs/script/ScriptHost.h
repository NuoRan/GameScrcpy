#pragma once

#include "gs/script/LogicHost.h"
#include "gs/script/Report.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gs::script {

class ScriptServices;

class ScriptHost final : public LogicHost
{
public:
    explicit ScriptHost(Sink sink, Limits limits = {}, std::shared_ptr<ScriptServices> services = {});
    ~ScriptHost() override;
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    [[nodiscard]] bool started() const noexcept override;
    [[nodiscard]] std::string problem() const override;

    [[nodiscard]] ScriptId load(ScriptSource source) override;
    void unload(ScriptId script) override;
    [[nodiscard]] TaskId start(ScriptId script, std::string handler) override;
    void cancel(TaskId task) override;
    void frame() override;
    void setFrameRate(double hz) override;
    void controlEvent(std::string id, bool down) override;
    void stateEvent(std::string id, bool value) override;
    void event(std::string kind, std::string id, std::string json) override;
    void answer(std::uint64_t request, std::string json) override;
    void refuse(std::uint64_t request, std::string message) override;
    void setBreakpoints(std::vector<std::string> places) override;
    void resume(std::uint64_t pause, StepMode mode) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
