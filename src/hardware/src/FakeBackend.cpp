#include "gs/hardware/FakeBackend.h"

#include <algorithm>

namespace gs::hardware {

namespace {

constexpr std::string_view kGone = "the device is gone";

std::string valueKey(std::string_view service, std::string_view characteristic)
{
    return std::string(service) + "/" + std::string(characteristic);
}

}

class FakeDevice final : public Device
{
public:
    explicit FakeDevice(std::shared_ptr<FakeLink> link)
        : m_link(std::move(link))
    {
    }

    ~FakeDevice() override
    {
        const std::scoped_lock lock(m_link->m_mutex);
        m_link->m_closed = true;
        m_link->m_events.reset();
    }

    FakeDevice(const FakeDevice&) = delete;
    FakeDevice& operator=(const FakeDevice&) = delete;

    void write(Bytes bytes, Completion done) override
    {
        if (m_link->device().kind != Kind::Serial) {
            Device::write(std::move(bytes), std::move(done));
            return;
        }
        std::string problem = m_link->refusal();
        if (problem.empty()) {
            const std::scoped_lock lock(m_link->m_mutex);
            m_link->m_written.push_back(std::move(bytes));
        }
        done(std::move(problem));
    }

    void setSignals(std::optional<bool> dtr, std::optional<bool> rts, Completion done) override
    {
        if (m_link->device().kind != Kind::Serial) {
            Device::setSignals(dtr, rts, std::move(done));
            return;
        }
        std::string problem = m_link->refusal();
        if (problem.empty()) {
            const std::scoped_lock lock(m_link->m_mutex);
            if (dtr) {
                m_link->m_dtr = dtr;
            }
            if (rts) {
                m_link->m_rts = rts;
            }
        }
        done(std::move(problem));
    }

    void sendReport(std::uint8_t id, Bytes bytes, Completion done) override
    {
        if (m_link->device().kind != Kind::Hid) {
            Device::sendReport(id, std::move(bytes), std::move(done));
            return;
        }
        std::string problem = m_link->refusal();
        if (problem.empty()) {
            const std::scoped_lock lock(m_link->m_mutex);
            m_link->m_reports.emplace_back(id, std::move(bytes));
        }
        done(std::move(problem));
    }

    void sendFeature(std::uint8_t id, Bytes bytes, Completion done) override
    {
        if (m_link->device().kind != Kind::Hid) {
            Device::sendFeature(id, std::move(bytes), std::move(done));
            return;
        }
        std::string problem = m_link->refusal();
        if (problem.empty()) {
            const std::scoped_lock lock(m_link->m_mutex);
            m_link->m_featuresSent.emplace_back(id, bytes);
            m_link->m_features.insert_or_assign(id, std::move(bytes));
        }
        done(std::move(problem));
    }

    void getFeature(std::uint8_t id, BytesCompletion done) override
    {
        if (m_link->device().kind != Kind::Hid) {
            Device::getFeature(id, std::move(done));
            return;
        }
        std::string problem = m_link->refusal();
        Bytes bytes;
        if (problem.empty()) {
            const std::scoped_lock lock(m_link->m_mutex);
            const auto found = m_link->m_features.find(id);
            if (found == m_link->m_features.end()) {
                problem = "the device has no feature report " + std::to_string(id);
            } else {
                bytes = found->second;
            }
        }
        done(std::move(problem), std::move(bytes));
    }

    void services(ServicesCompletion done) override
    {
        if (m_link->device().kind != Kind::Ble) {
            Device::services(std::move(done));
            return;
        }
        std::string problem = m_link->refusal();
        std::vector<BleService> services;
        if (problem.empty()) {
            const std::scoped_lock lock(m_link->m_mutex);
            services = m_link->m_services;
        }
        done(std::move(problem), std::move(services));
    }

    void read(const std::string& service, const std::string& characteristic, BytesCompletion done) override
    {
        if (m_link->device().kind != Kind::Ble) {
            Device::read(service, characteristic, std::move(done));
            return;
        }
        std::string problem = m_link->refusal();
        Bytes bytes;
        if (problem.empty()) {
            const std::scoped_lock lock(m_link->m_mutex);
            const auto found = m_link->m_values.find(valueKey(service, characteristic));
            if (found == m_link->m_values.end()) {
                problem = "the device has no characteristic " + characteristic + " in service " + service;
            } else {
                bytes = found->second;
            }
        }
        done(std::move(problem), std::move(bytes));
    }

    void writeValue(const std::string& service, const std::string& characteristic, Bytes bytes, bool response,
                    Completion done) override
    {
        if (m_link->device().kind != Kind::Ble) {
            Device::writeValue(service, characteristic, std::move(bytes), response, std::move(done));
            return;
        }
        std::string problem = m_link->refusal();
        if (problem.empty()) {
            const std::scoped_lock lock(m_link->m_mutex);
            m_link->m_values.insert_or_assign(valueKey(service, characteristic), bytes);
            m_link->m_valueWrites.push_back({service, characteristic, std::move(bytes), response});
        }
        done(std::move(problem));
    }

    void subscribe(const std::string& service, const std::string& characteristic, bool on, Completion done) override
    {
        if (m_link->device().kind != Kind::Ble) {
            Device::subscribe(service, characteristic, on, std::move(done));
            return;
        }
        std::string problem = m_link->refusal();
        if (problem.empty()) {
            const std::scoped_lock lock(m_link->m_mutex);
            if (on) {
                m_link->m_subscriptions.insert(valueKey(service, characteristic));
            } else {
                m_link->m_subscriptions.erase(valueKey(service, characteristic));
            }
        }
        done(std::move(problem));
    }

private:
    std::shared_ptr<FakeLink> m_link;
};

FakeLink::FakeLink(DeviceInfo device, SerialOptions serial, std::shared_ptr<Events> events)
    : m_device(std::move(device))
    , m_serial(serial)
    , m_events(std::move(events))
{
}

std::vector<Bytes> FakeLink::written() const
{
    const std::scoped_lock lock(m_mutex);
    return m_written;
}

std::vector<FakeReport> FakeLink::reports() const
{
    const std::scoped_lock lock(m_mutex);
    return m_reports;
}

std::vector<FakeReport> FakeLink::featuresSent() const
{
    const std::scoped_lock lock(m_mutex);
    return m_featuresSent;
}

std::vector<FakeValueWrite> FakeLink::valueWrites() const
{
    const std::scoped_lock lock(m_mutex);
    return m_valueWrites;
}

std::set<std::string> FakeLink::subscriptions() const
{
    const std::scoped_lock lock(m_mutex);
    return m_subscriptions;
}

std::optional<bool> FakeLink::dtr() const
{
    const std::scoped_lock lock(m_mutex);
    return m_dtr;
}

std::optional<bool> FakeLink::rts() const
{
    const std::scoped_lock lock(m_mutex);
    return m_rts;
}

bool FakeLink::closed() const
{
    const std::scoped_lock lock(m_mutex);
    return m_closed;
}

void FakeLink::setFeature(std::uint8_t id, Bytes bytes)
{
    const std::scoped_lock lock(m_mutex);
    m_features.insert_or_assign(id, std::move(bytes));
}

void FakeLink::setValue(const std::string& service, const std::string& characteristic, Bytes bytes)
{
    const std::scoped_lock lock(m_mutex);
    m_values.insert_or_assign(valueKey(service, characteristic), std::move(bytes));
}

void FakeLink::setServices(std::vector<BleService> services)
{
    const std::scoped_lock lock(m_mutex);
    m_services = std::move(services);
}

void FakeLink::fail(std::string problem)
{
    const std::scoped_lock lock(m_mutex);
    m_failure = std::move(problem);
}

std::shared_ptr<Events> FakeLink::sink() const
{
    const std::scoped_lock lock(m_mutex);
    return m_closed ? nullptr : m_events;
}

std::string FakeLink::refusal() const
{
    const std::scoped_lock lock(m_mutex);
    if (m_closed) {
        return std::string(kGone);
    }
    return m_failure;
}

void FakeLink::sendData(Bytes bytes)
{
    if (const std::shared_ptr<Events> events = sink()) {
        events->data(std::move(bytes));
    }
}

void FakeLink::sendReport(std::uint8_t id, Bytes bytes)
{
    if (const std::shared_ptr<Events> events = sink()) {
        events->report(id, std::move(bytes));
    }
}

void FakeLink::notify(const std::string& service, const std::string& characteristic, Bytes bytes)
{
    bool subscribed = false;
    {
        const std::scoped_lock lock(m_mutex);
        subscribed = m_subscriptions.contains(valueKey(service, characteristic));
    }
    if (!subscribed) {
        return;
    }
    if (const std::shared_ptr<Events> events = sink()) {
        events->notified(service, characteristic, std::move(bytes));
    }
}

void FakeLink::drop(Closed reason, const std::string& problem)
{
    std::shared_ptr<Events> events;
    {
        const std::scoped_lock lock(m_mutex);
        if (m_closed) {
            return;
        }
        m_closed = true;
        events = std::move(m_events);
    }
    if (events != nullptr) {
        events->closed(reason, problem);
    }
}

void FakeBackend::add(DeviceInfo device)
{
    {
        const std::scoped_lock lock(m_mutex);
        std::erase_if(m_devices, [&device](const DeviceInfo& each) { return each.id == device.id; });
        m_devices.push_back(std::move(device));
    }
    changed();
}

void FakeBackend::remove(std::string_view id)
{
    std::shared_ptr<FakeLink> gone;
    {
        const std::scoped_lock lock(m_mutex);
        std::erase_if(m_devices, [id](const DeviceInfo& each) { return each.id == id; });
        if (const auto found = m_links.find(id); found != m_links.end()) {
            gone = found->second;
        }
    }
    if (gone != nullptr) {
        gone->drop(Closed::Removed, std::string(kGone));
    }
    changed();
}

void FakeBackend::failNextOpen(std::string problem, bool missing)
{
    const std::scoped_lock lock(m_mutex);
    m_openFailure = std::pair{std::move(problem), missing};
}

std::shared_ptr<FakeLink> FakeBackend::link(std::string_view id) const
{
    const std::scoped_lock lock(m_mutex);
    const auto found = m_links.find(id);
    return found == m_links.end() ? nullptr : found->second;
}

bool FakeBackend::scanning() const
{
    const std::scoped_lock lock(m_mutex);
    return m_scanning;
}

int FakeBackend::opens() const
{
    const std::scoped_lock lock(m_mutex);
    return m_opens;
}

std::vector<DeviceInfo> FakeBackend::devices() const
{
    const std::scoped_lock lock(m_mutex);
    return m_devices;
}

void FakeBackend::watch(std::function<void()> changed)
{
    const std::scoped_lock lock(m_mutex);
    m_changed = std::move(changed);
}

void FakeBackend::scan(bool on)
{
    const std::scoped_lock lock(m_mutex);
    m_scanning = on;
}

void FakeBackend::open(OpenRequest request, Opened done)
{
    OpenResult result;
    {
        const std::scoped_lock lock(m_mutex);
        ++m_opens;
        if (m_openFailure) {
            result.problem = std::move(m_openFailure->first);
            result.missing = m_openFailure->second;
            m_openFailure.reset();
        } else if (request.device.kind != Kind::Ble &&
                   std::ranges::none_of(m_devices, [&request](const DeviceInfo& each) {
                       return each.id == request.device.id;
                   })) {
            result.problem = std::string(kGone);
            result.missing = true;
        } else {
            auto link = std::make_shared<FakeLink>(request.device, request.serial, request.events);
            m_links.insert_or_assign(request.device.id, link);
            result.device = std::make_unique<FakeDevice>(std::move(link));
        }
    }
    done(std::move(result));
}

void FakeBackend::changed()
{
    std::function<void()> callback;
    {
        const std::scoped_lock lock(m_mutex);
        callback = m_changed;
    }
    if (callback) {
        callback();
    }
}

}
