#pragma once

#include "gs/hardware/Devices.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gs::hardware {

class Events
{
public:
    virtual ~Events() = default;

    virtual void data(Bytes bytes) = 0;
    virtual void report(std::uint8_t id, Bytes bytes) = 0;
    virtual void notified(const std::string& service, const std::string& characteristic, Bytes bytes) = 0;
    virtual void closed(Closed reason, const std::string& problem) = 0;
};

using Completion = std::function<void(std::string problem)>;
using BytesCompletion = std::function<void(std::string problem, Bytes bytes)>;
using ServicesCompletion = std::function<void(std::string problem, std::vector<BleService> services)>;

class Device
{
public:
    virtual ~Device() = default;

    virtual void write(Bytes bytes, Completion done);
    virtual void setSignals(std::optional<bool> dtr, std::optional<bool> rts, Completion done);
    virtual void sendReport(std::uint8_t id, Bytes bytes, Completion done);
    virtual void sendFeature(std::uint8_t id, Bytes bytes, Completion done);
    virtual void getFeature(std::uint8_t id, BytesCompletion done);
    virtual void services(ServicesCompletion done);
    virtual void read(const std::string& service, const std::string& characteristic, BytesCompletion done);
    virtual void writeValue(const std::string& service, const std::string& characteristic, Bytes bytes, bool response,
                            Completion done);
    virtual void subscribe(const std::string& service, const std::string& characteristic, bool on, Completion done);
};

struct OpenRequest {
    DeviceInfo device;
    SerialOptions serial;
    std::shared_ptr<Events> events;
};

struct OpenResult {
    std::unique_ptr<Device> device;
    std::string problem;
    bool missing = false;
};

using Opened = std::function<void(OpenResult result)>;

class Backend
{
public:
    virtual ~Backend() = default;

    [[nodiscard]] virtual std::vector<DeviceInfo> devices() const = 0;
    virtual void watch(std::function<void()> changed) = 0;
    virtual void scan(bool on) = 0;
    virtual void open(OpenRequest request, Opened done) = 0;
};

[[nodiscard]] std::unique_ptr<Backend> makeWindowsBackend();

}
