#pragma once

#include <gs/script/LogicHost.h>
#include <gs/script/Services.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gs::scripthost {

enum class SandboxMode : std::uint8_t { None, AppContainer, Restricted };

inline constexpr std::uintmax_t kMostLogicBytes = std::uintmax_t{16} << 20;
inline constexpr std::string_view kStoppedText = "The extension's logic stopped unexpectedly";

struct RemoteOptions {
    std::filesystem::path program;
    std::string label;
    bool restrictedOnly = false;
};

[[nodiscard]] std::filesystem::path defaultProgram();
[[nodiscard]] std::string_view modeName(SandboxMode mode) noexcept;
[[nodiscard]] bool packFolder(const std::filesystem::path& folder, const std::vector<script::ComponentModule>& components,
                              script::ScriptFiles& files, std::string& problem);

class RemoteLogicHost final : public script::LogicHost
{
public:
    explicit RemoteLogicHost(Sink sink, script::Limits limits = {},
                             std::shared_ptr<script::ScriptServices> services = {}, RemoteOptions options = {});
    ~RemoteLogicHost() override;
    RemoteLogicHost(const RemoteLogicHost&) = delete;
    RemoteLogicHost& operator=(const RemoteLogicHost&) = delete;

    [[nodiscard]] bool started() const noexcept override;
    [[nodiscard]] std::string problem() const override;

    [[nodiscard]] script::ScriptId load(script::ScriptSource source) override;
    void unload(script::ScriptId script) override;
    [[nodiscard]] script::TaskId start(script::ScriptId script, std::string handler) override;
    void cancel(script::TaskId task) override;
    void frame() override;
    void setFrameRate(double hz) override;
    void controlEvent(std::string id, bool down) override;
    void stateEvent(std::string id, bool value) override;
    void event(std::string kind, std::string id, std::string json) override;
    void answer(std::uint64_t request, std::string json) override;
    void refuse(std::uint64_t request, std::string message) override;
    void setBreakpoints(std::vector<std::string> places) override;
    void resume(std::uint64_t pause, script::StepMode mode) override;

    [[nodiscard]] SandboxMode mode() const;
    [[nodiscard]] bool alive() const;
    void kill();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

struct SelfTestResult {
    SandboxMode mode = SandboxMode::None;
    bool file = false;
    bool socket = false;
    bool process = false;
    std::string text;
    std::string problem;
};

[[nodiscard]] SelfTestResult runSelfTest(const std::filesystem::path& program, const std::filesystem::path& probeFile,
                                         std::uint16_t port, bool restrictedOnly = false);

}
