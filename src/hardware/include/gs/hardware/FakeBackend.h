#pragma once

#include "gs/hardware/Backend.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::hardware {

struct FakeValueWrite {
    std::string service;
    std::string characteristic;
    Bytes bytes;
    bool response = false;

    friend bool operator==(const FakeValueWrite&, const FakeValueWrite&) = default;
};

using FakeReport = std::pair<std::uint8_t, Bytes>;

class FakeLink
{
public:
    FakeLink(DeviceInfo device, SerialOptions serial, std::shared_ptr<Events> events);

    [[nodiscard]] const DeviceInfo& device() const noexcept { return m_device; }
    [[nodiscard]] const SerialOptions& serial() const noexcept { return m_serial; }
    [[nodiscard]] std::vector<Bytes> written() const;
    [[nodiscard]] std::vector<FakeReport> reports() const;
    [[nodiscard]] std::vector<FakeReport> featuresSent() const;
    [[nodiscard]] std::vector<FakeValueWrite> valueWrites() const;
    [[nodiscard]] std::set<std::string> subscriptions() const;
    [[nodiscard]] std::optional<bool> dtr() const;
    [[nodiscard]] std::optional<bool> rts() const;
    [[nodiscard]] bool closed() const;

    void setFeature(std::uint8_t id, Bytes bytes);
    void setValue(const std::string& service, const std::string& characteristic, Bytes bytes);
    void setServices(std::vector<BleService> services);
    void fail(std::string problem);

    void sendData(Bytes bytes);
    void sendReport(std::uint8_t id, Bytes bytes);
    void notify(const std::string& service, const std::string& characteristic, Bytes bytes);
    void drop(Closed reason, const std::string& problem);

private:
    friend class FakeDevice;

    [[nodiscard]] std::shared_ptr<Events> sink() const;
    [[nodiscard]] std::string refusal() const;

    const DeviceInfo m_device;
    const SerialOptions m_serial;
    mutable std::mutex m_mutex;
    std::shared_ptr<Events> m_events;
    std::vector<Bytes> m_written;
    std::vector<FakeReport> m_reports;
    std::vector<FakeReport> m_featuresSent;
    std::map<std::uint8_t, Bytes> m_features;
    std::vector<BleService> m_services;
    std::map<std::string, Bytes, std::less<>> m_values;
    std::vector<FakeValueWrite> m_valueWrites;
    std::set<std::string> m_subscriptions;
    std::optional<bool> m_dtr;
    std::optional<bool> m_rts;
    std::string m_failure;
    bool m_closed = false;
};

class FakeBackend final : public Backend
{
public:
    FakeBackend() = default;

    void add(DeviceInfo device);
    void remove(std::string_view id);
    void failNextOpen(std::string problem, bool missing = false);
    [[nodiscard]] std::shared_ptr<FakeLink> link(std::string_view id) const;
    [[nodiscard]] bool scanning() const;
    [[nodiscard]] int opens() const;

    [[nodiscard]] std::vector<DeviceInfo> devices() const override;
    void watch(std::function<void()> changed) override;
    void scan(bool on) override;
    void open(OpenRequest request, Opened done) override;

private:
    void changed();

    mutable std::mutex m_mutex;
    std::vector<DeviceInfo> m_devices;
    std::map<std::string, std::shared_ptr<FakeLink>, std::less<>> m_links;
    std::function<void()> m_changed;
    std::optional<std::pair<std::string, bool>> m_openFailure;
    bool m_scanning = false;
    int m_opens = 0;
};

}
