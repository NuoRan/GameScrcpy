#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gs::platform {

enum class UsbDriver : std::uint8_t { Missing, Ready };

struct UsbPhone {
    std::string serial;
    std::string name;
    std::uint16_t vendorId = 0;
    std::uint16_t productId = 0;
    UsbDriver driver = UsbDriver::Missing;
    bool debugging = false;

    friend bool operator==(const UsbPhone&, const UsbPhone&) = default;
};

[[nodiscard]] std::vector<UsbPhone> listUsbPhones();
[[nodiscard]] std::string usbLibraryVersion();

enum class UsbFailure : std::uint8_t { None, NotFound, NoDriver, Busy, Gone, Refused };

class UsbDevice
{
public:
    ~UsbDevice();
    UsbDevice(const UsbDevice&) = delete;
    UsbDevice& operator=(const UsbDevice&) = delete;

    [[nodiscard]] static std::unique_ptr<UsbDevice> open(const UsbPhone& phone, UsbFailure& failure);

    [[nodiscard]] bool controlOut(std::uint8_t request, std::uint16_t value, std::uint16_t index,
                                  std::span<const std::uint8_t> data);
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> controlIn(std::uint8_t request, std::uint16_t value,
                                                                     std::uint16_t index, std::size_t length);
    [[nodiscard]] std::size_t maxPacketSize() const noexcept;
    [[nodiscard]] UsbFailure failure() const noexcept;

private:
    struct Impl;
    explicit UsbDevice(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

class UsbWatcher
{
public:
    using Changed = std::function<void()>;

    explicit UsbWatcher(Changed changed);
    ~UsbWatcher();
    UsbWatcher(const UsbWatcher&) = delete;
    UsbWatcher& operator=(const UsbWatcher&) = delete;

    [[nodiscard]] bool started() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
