#pragma once

#include "Worker.h"

#include "gs/hardware/Backend.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gs::hardware::detail {

[[nodiscard]] std::string narrow(std::wstring_view text);
[[nodiscard]] std::wstring widen(std::string_view text);
[[nodiscard]] std::string systemMessage(std::uint32_t error);
[[nodiscard]] bool removalError(std::uint32_t error) noexcept;
[[nodiscard]] std::uint16_t hexAfter(std::string_view text, std::string_view key) noexcept;

[[nodiscard]] std::vector<DeviceInfo> listSerialPorts();
[[nodiscard]] std::vector<DeviceInfo> listHidDevices();

[[nodiscard]] OpenResult openSerial(const DeviceInfo& device, const SerialOptions& options,
                                    std::shared_ptr<Events> events);
[[nodiscard]] OpenResult openHid(const DeviceInfo& device, std::shared_ptr<Events> events);
[[nodiscard]] OpenResult openBle(const std::shared_ptr<Worker>& control, const DeviceInfo& device,
                                 std::shared_ptr<Events> events);

class BleScanner
{
public:
    using Seen = std::function<void(const DeviceInfo& device)>;

    explicit BleScanner(Seen seen);
    ~BleScanner();
    BleScanner(const BleScanner&) = delete;
    BleScanner& operator=(const BleScanner&) = delete;

    void start();
    void stop();

private:
    struct State;
    std::shared_ptr<State> m_state;
};

}
