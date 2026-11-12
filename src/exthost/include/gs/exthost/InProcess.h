#pragma once

#include "gs/exthost/Module.h"

#include <gs/base/Clock.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gs::exthost {

namespace detail {
class SinkBox;
}

inline constexpr std::chrono::microseconds kNodeBudget{100'000};

struct SourceHooks {
    void* wake = nullptr;
    std::function<void(MediaKind kind, void* section, std::size_t capacity)> made;
    std::function<void(const std::string& why)> ended;
};

struct InProcessOptions {
    std::filesystem::path dll;
    std::filesystem::path store;
    std::vector<std::string> granted;
    HostServices* services = nullptr;
    std::chrono::microseconds budget = kNodeBudget;
    std::string settings;
    std::vector<DeclaredInput> inputs;
    const Clock* clock = nullptr;
    std::chrono::milliseconds visionLimit = kVisionLimit;
    bool hostStore = false;
};

class InProcessModule final : public Module
{
public:
    [[nodiscard]] static std::unique_ptr<InProcessModule> load(InProcessOptions options, std::string& problem);

    ~InProcessModule() override;
    InProcessModule(const InProcessModule&) = delete;
    InProcessModule& operator=(const InProcessModule&) = delete;

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
    [[nodiscard]] std::uint64_t openInstance(InstanceKind kind, std::size_t index, std::string_view params,
                                             std::uint64_t seed) override;
    [[nodiscard]] StepOutput step(std::uint64_t instance, const StepInput& input) override;
    void resetInstance(std::uint64_t instance) override;
    void closeInstance(std::uint64_t instance) override;
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
    [[nodiscard]] CallResult listSources(MediaKind kind, std::size_t source) override;
    [[nodiscard]] std::shared_ptr<MediaStream> openSource(MediaKind kind, std::size_t source, std::string_view id,
                                                          std::string_view params) override;
    void closeSource(std::uint64_t stream) override;
    [[nodiscard]] std::shared_ptr<SinkPort> openSink(std::size_t sink, const SinkStart& start) override;
    void closeSink(std::uint64_t sink) override;
    [[nodiscard]] std::uint64_t startExport(std::size_t exporter, std::string_view input, std::string_view output,
                                            JobEvents events) override;
    [[nodiscard]] std::shared_ptr<MediaStream> openSourceWith(MediaKind kind, std::size_t source, std::string_view id,
                                                              std::string_view params, SourceHooks hooks);
    [[nodiscard]] std::uint64_t attachSink(std::size_t sink, std::shared_ptr<detail::SinkBox> box);
    [[nodiscard]] std::uint64_t openTap(std::size_t tap, const TapOpen& info, TapEvents events) override;
    [[nodiscard]] bool offerFrame(std::uint64_t tap, const TapFrame& frame) override;
    void closeTap(std::uint64_t tap) override;
    [[nodiscard]] CallResult recognize(std::size_t engine, VisionKind kind, const VisionImage& image,
                                       std::string_view params) override;
    void deviceEvent(std::uint64_t device, std::string_view json) override;

    struct Impl;

private:
    explicit InProcessModule(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

}
