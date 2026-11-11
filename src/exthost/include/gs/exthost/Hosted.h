#pragma once

#include "gs/exthost/Module.h"

#include <gs/base/Clock.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gs::exthost {

struct HostedOptions {
    std::filesystem::path program;
    std::filesystem::path dll;
    std::filesystem::path store;
    std::vector<std::string> granted;
    std::chrono::milliseconds budget{100};
    std::chrono::microseconds stepLimit{2'000};
    std::chrono::milliseconds readyWait{2'000};
    std::chrono::milliseconds startLimit{30'000};
    std::chrono::milliseconds hangLimit{2'000};
    std::chrono::microseconds crashWindow{60'000'000};
    int crashLimit = 3;
    const Clock* clock = nullptr;
    std::function<void(int level, const std::string& text)> log;
    std::function<void(const std::string& title, const std::string& text)> notify;
    std::function<void(const std::string& topic, const std::string& json)> event;
    std::function<void(const std::string& provider)> devicesChanged;
    std::function<void(const std::string& resource, bool wait, std::function<void(ClaimResult)> answer)> claim;
    std::function<void(const std::string& resource)> release;
    std::function<void(const std::string& service, const std::string& json, std::function<void(ServiceResult)> answer)>
        callService;
    std::filesystem::path crashes;
    std::filesystem::path logFile;
    std::function<void(std::uint32_t code)> crashed;
    std::string settings;
    std::function<void(std::uint64_t session)> touchLost;
    std::function<void()> restarted;
    std::function<void()> stopped;
    std::function<void()> ready;
    std::function<void(const std::string& problem)> notReady;
    std::vector<DeclaredInput> inputs;
    std::function<void(const std::string& source, const std::string& slot, double x, double y, std::uint64_t at)> input;
    std::function<void(const std::string& transport)> transportChanged;
};

class HostedModule final : public Module
{
public:
    [[nodiscard]] static std::unique_ptr<HostedModule> start(HostedOptions options, std::string& problem);

    ~HostedModule() override;
    HostedModule(const HostedModule&) = delete;
    HostedModule& operator=(const HostedModule&) = delete;

    [[nodiscard]] const ModuleInfo& info() const noexcept override;
    [[nodiscard]] CallResult call(std::size_t node, std::span<const Value> arguments) override;
    [[nodiscard]] std::uint64_t openTouch(std::size_t method, std::string_view device,
                                          std::string_view options = {}) override;
    [[nodiscard]] bool touch(std::uint64_t session, const TouchFrame& frame) override;
    [[nodiscard]] bool key(std::uint64_t session, const KeyEvent& key) override;
    [[nodiscard]] bool text(std::uint64_t session, std::string_view utf8) override;
    [[nodiscard]] bool mouse(std::uint64_t session, const MouseEvent& mouse) override;
    [[nodiscard]] bool gamepad(std::uint64_t session, const PadEvent& pad) override;
    void closeTouch(std::uint64_t session) override;
    [[nodiscard]] bool working() const noexcept override;
    [[nodiscard]] std::uint64_t openInstance(InstanceKind kind, std::size_t index, std::string_view params,
                                             std::uint64_t seed) override;
    [[nodiscard]] StepOutput step(std::uint64_t instance, const StepInput& input) override;
    void resetInstance(std::uint64_t instance) override;
    void closeInstance(std::uint64_t instance) override;
    [[nodiscard]] std::uint64_t skippedSteps() const noexcept override;
    [[nodiscard]] std::uint64_t startJob(std::size_t job, std::string_view args, JobEvents events) override;
    void cancelJob(std::uint64_t job) override;
    void setSettings(std::string_view json) override;
    [[nodiscard]] CallResult listDevices(std::size_t provider) override;
    [[nodiscard]] CallResult actOnDevice(std::size_t provider, std::string_view device,
                                         std::string_view action) override;
    [[nodiscard]] bool claimWanted(std::string_view resource, std::string_view by) override;
    [[nodiscard]] bool hears(std::string_view topic) const override;
    void appEvent(std::string_view topic, std::string_view json) override;
    [[nodiscard]] CallResult discover(std::size_t transport) override;
    [[nodiscard]] bool openStream(std::size_t transport, std::uint64_t stream, const StreamRequest& request,
                                  StreamEvents events) override;
    [[nodiscard]] StreamSent sendStream(std::uint64_t stream, std::string_view bytes) override;
    void closeStream(std::uint64_t stream) override;

    [[nodiscard]] int crashes() const;
    [[nodiscard]] bool waitReady(std::chrono::milliseconds limit) const;
    [[nodiscard]] bool ready() const;

    struct Impl;

private:
    explicit HostedModule(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

}
