#pragma once

#include "gs/exthost/Module.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace gs::exthost {

inline constexpr std::chrono::microseconds kNodeBudget{100'000};

struct InProcessOptions {
    std::filesystem::path dll;
    std::filesystem::path store;
    std::vector<std::string> granted;
    HostServices* services = nullptr;
    std::chrono::microseconds budget = kNodeBudget;
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

    struct Impl;

private:
    explicit InProcessModule(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

}
