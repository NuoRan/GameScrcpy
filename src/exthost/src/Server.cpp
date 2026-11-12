#include "gs/exthost/Server.h"

#include "Channel.h"
#include "MediaRing.h"
#include "TapSection.h"
#include "ImageSection.h"
#include "Wire.h"

#include "gs/exthost/InProcess.h"

#include <gs/platform/CrashHandler.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace gs::exthost {

namespace {

constexpr DWORD kStartWaitMs = 10'000;
constexpr auto kClaimAnswerWait = std::chrono::seconds(2);
constexpr auto kClaimWaitedWait = std::chrono::seconds(6);
constexpr auto kServiceWait = std::chrono::seconds(12);
constexpr std::string_view kServiceThread =
    "call_service works only on a thread of the DLL's own, not inside a call from GameScrcpy";
constexpr auto kStoreWait = std::chrono::seconds(4);
constexpr auto kDeviceWait = std::chrono::seconds(22);
constexpr std::string_view kDeviceThread =
    "the device functions work only on a thread of the DLL's own, not inside a call from GameScrcpy";
thread_local bool t_hostThread = false;
constexpr std::size_t kDataWindow = 512 * 1024;
constexpr auto kRoomRetry = std::chrono::milliseconds(10);
constexpr std::size_t kMostPathBytes = 4096;
constexpr std::size_t kMostPermissionBytes = 64;
enum Exit : int { kDone = 0, kUsage = 2, kBroken = 3, kNoStart = 4, kNotLoaded = 5 };

std::filesystem::path pathOf(const std::string& text)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

class Link final : public HostServices
{
public:
    Link(detail::Channel& channel, HANDLE parent) noexcept
        : m_channel(channel)
        , m_parent(parent)
        , m_main(std::this_thread::get_id())
    {
    }

    ClaimResult claim(std::string_view resource, bool wait) override
    {
        detail::WireWriter out;
        out.text(resource);
        out.u32(wait ? 1U : 0U);
        std::uint64_t id = 0;
        {
            const std::scoped_lock lock(m_claimMutex);
            if (m_closed) {
                return ClaimResult::Busy;
            }
            id = ++m_lastClaim;
            m_answers.emplace(id, std::nullopt);
        }
        send(detail::Kind::Claim, id, out.bytes());
        const auto deadline = std::chrono::steady_clock::now() + (wait ? kClaimWaitedWait : kClaimAnswerWait);
        if (std::this_thread::get_id() == m_main) {
            pump(id, deadline);
        }
        std::unique_lock lock(m_claimMutex);
        m_answered.wait_until(lock, deadline, [this, id] { return m_closed || m_answers[id].has_value(); });
        const std::optional<ClaimResult> result = m_answers[id];
        m_answers.erase(id);
        lock.unlock();
        if (!result) {
            release(resource);
            return ClaimResult::Busy;
        }
        return *result;
    }

    void release(std::string_view resource) override
    {
        detail::WireWriter out;
        out.text(resource);
        send(detail::Kind::Release, 0, out.bytes());
    }

    ServiceResult callService(std::string_view service, std::string_view json) override
    {
        if (t_hostThread || std::this_thread::get_id() == m_main) {
            return {ServiceStatus::Unsupported, std::string(kServiceThread)};
        }
        detail::WireWriter out;
        out.text(service);
        out.text(json);
        std::uint64_t id = 0;
        {
            const std::scoped_lock lock(m_claimMutex);
            if (m_closed) {
                return {ServiceStatus::Failed, "GS-API-149 the extension is stopping"};
            }
            id = ++m_lastService;
            m_services.emplace(id, std::nullopt);
        }
        send(detail::Kind::ServiceCall, id, out.bytes());
        std::unique_lock lock(m_claimMutex);
        m_answered.wait_for(lock, kServiceWait, [this, id] { return m_closed || m_services[id].has_value(); });
        std::optional<ServiceResult> result = std::move(m_services[id]);
        m_services.erase(id);
        if (!result) {
            return {ServiceStatus::Failed, m_closed ? std::string("GS-API-149 the extension is stopping")
                                                    : std::string("GS-API-149 the service did not answer in time")};
        }
        return std::move(*result);
    }

    void serviceAnswered(const detail::Message& message)
    {
        detail::WireReader in(message.payload);
        std::uint32_t status = 0;
        std::string text;
        ServiceResult result{ServiceStatus::Failed, "GS-API-149 the answer of the service was damaged"};
        if (in.u32(status) && in.text(text, kMostServiceBytes) && in.done() &&
            status <= static_cast<std::uint32_t>(ServiceStatus::BadArgument)) {
            result = {static_cast<ServiceStatus>(status), std::move(text)};
        }
        {
            const std::scoped_lock lock(m_claimMutex);
            const auto found = m_services.find(message.id);
            if (found == m_services.end()) {
                return;
            }
            found->second = std::move(result);
        }
        m_answered.notify_all();
    }

    StoreReply storeGet(std::string_view key) override { return askStore(key, false, std::nullopt); }

    ServiceStatus storeSet(std::string_view key, std::optional<std::string_view> json) override
    {
        return askStore(key, true, json).status;
    }

    ServiceResult device(std::string_view request, std::string_view json) override
    {
        if (t_hostThread || std::this_thread::get_id() == m_main) {
            return {ServiceStatus::Unsupported, std::string(kDeviceThread)};
        }
        detail::WireWriter out;
        out.text(request);
        out.text(json);
        std::uint64_t id = 0;
        {
            const std::scoped_lock lock(m_claimMutex);
            if (m_closed) {
                return {ServiceStatus::Failed, "the extension is stopping"};
            }
            id = ++m_lastDevice;
            m_devices.emplace(id, std::nullopt);
        }
        send(detail::Kind::DeviceCall, id, out.bytes());
        std::unique_lock lock(m_claimMutex);
        m_answered.wait_for(lock, kDeviceWait, [this, id] { return m_closed || m_devices[id].has_value(); });
        std::optional<ServiceResult> result = std::move(m_devices[id]);
        m_devices.erase(id);
        if (!result) {
            return {ServiceStatus::Failed, m_closed ? std::string("the extension is stopping")
                                                    : std::string("GameScrcpy did not answer for the device in time")};
        }
        return std::move(*result);
    }

    void closeDevice(std::uint64_t device) override
    {
        detail::WireWriter out;
        out.text("hardware.close");
        out.text("{\"device\":" + std::to_string(device) + "}");
        send(detail::Kind::DeviceCall, 0, out.bytes());
    }

    void deviceAnswered(const detail::Message& message)
    {
        detail::WireReader in(message.payload);
        std::uint32_t status = 0;
        std::string text;
        ServiceResult result{ServiceStatus::Failed, "the answer for the device was damaged"};
        if (in.u32(status) && in.text(text, kMostServiceBytes) && in.done() &&
            status <= static_cast<std::uint32_t>(ServiceStatus::BadArgument)) {
            result = {static_cast<ServiceStatus>(status), std::move(text)};
        }
        {
            const std::scoped_lock lock(m_claimMutex);
            const auto found = m_devices.find(message.id);
            if (found == m_devices.end()) {
                return;
            }
            found->second = std::move(result);
        }
        m_answered.notify_all();
    }

    void storeAnswered(const detail::Message& message)
    {
        detail::WireReader in(message.payload);
        std::uint32_t status = 0;
        std::uint32_t has = 0;
        std::string text;
        StoreReply reply{ServiceStatus::Failed, std::nullopt};
        if (in.u32(status) && in.u32(has) && in.text(text, kMostServiceBytes) && in.done() &&
            status <= static_cast<std::uint32_t>(ServiceStatus::BadArgument)) {
            reply.status = static_cast<ServiceStatus>(status);
            if (has != 0) {
                reply.value = std::move(text);
            }
        }
        {
            const std::scoped_lock lock(m_claimMutex);
            const auto found = m_stores.find(message.id);
            if (found == m_stores.end()) {
                return;
            }
            found->second = std::move(reply);
        }
        m_answered.notify_all();
    }

    void answered(const detail::Message& message)
    {
        detail::WireReader in(message.payload);
        std::uint32_t status = 0;
        ClaimResult result = ClaimResult::Busy;
        if (in.u32(status) && in.done() &&
            (status == static_cast<std::uint32_t>(ClaimResult::Claimed) ||
             status == static_cast<std::uint32_t>(ClaimResult::Unsupported) ||
             status == static_cast<std::uint32_t>(ClaimResult::BadArgument))) {
            result = static_cast<ClaimResult>(status);
        }
        {
            const std::scoped_lock lock(m_claimMutex);
            const auto found = m_answers.find(message.id);
            if (found == m_answers.end()) {
                return;
            }
            found->second = result;
        }
        m_answered.notify_all();
    }

    [[nodiscard]] std::optional<detail::Message> deferred()
    {
        if (m_deferred.empty()) {
            return std::nullopt;
        }
        detail::Message message = std::move(m_deferred.front());
        m_deferred.pop_front();
        return message;
    }

    void close()
    {
        {
            const std::scoped_lock lock(m_claimMutex);
            m_closed = true;
        }
        m_answered.notify_all();
        {
            const std::scoped_lock lock(m_mutex);
            m_shut = true;
        }
        m_room.notify_all();
    }

    [[nodiscard]] bool deliver(detail::Kind kind, std::uint64_t id, std::string_view payload)
    {
        std::unique_lock lock(m_mutex);
        while (!m_shut) {
            if (m_channel.toApp().write(kind, id, payload)) {
                SetEvent(m_channel.toAppEvent());
                return true;
            }
            m_room.wait_until(lock, std::chrono::steady_clock::now() + kRoomRetry);
        }
        return false;
    }

    [[nodiscard]] bool sendData(std::uint64_t id, std::string_view bytes)
    {
        const bool onMain = std::this_thread::get_id() == m_main;
        std::unique_lock lock(m_mutex);
        while (!m_shut) {
            if (onMain || m_unacked + bytes.size() <= kDataWindow) {
                if (m_channel.toApp().write(detail::Kind::StreamData, id, bytes)) {
                    m_unacked += bytes.size();
                    SetEvent(m_channel.toAppEvent());
                    return true;
                }
                if (onMain) {
                    return false;
                }
            }
            m_room.wait_until(lock, std::chrono::steady_clock::now() + kRoomRetry);
        }
        return false;
    }

    void taken(std::size_t size)
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_unacked -= std::min(m_unacked, size);
        }
        m_room.notify_all();
    }

    void send(detail::Kind kind, std::uint64_t id, std::string_view payload)
    {
        const std::scoped_lock lock(m_mutex);
        if (m_channel.toApp().write(kind, id, payload)) {
            SetEvent(m_channel.toAppEvent());
        }
    }

    void log(int level, std::string_view text) override
    {
        detail::WireWriter out;
        out.u32(static_cast<std::uint32_t>(level));
        out.text(text);
        send(detail::Kind::Log, 0, out.bytes());
    }

    bool notify(std::string_view title, std::string_view text) override
    {
        detail::WireWriter out;
        out.text(title);
        out.text(text);
        send(detail::Kind::Notify, 0, out.bytes());
        return true;
    }

    void emitted(std::string_view topic, std::string_view json) override
    {
        detail::WireWriter out;
        out.text(topic);
        out.text(json);
        send(detail::Kind::Event, 0, out.bytes());
    }

    void devicesChanged(std::string_view provider) override
    {
        detail::WireWriter out;
        out.text(provider);
        send(detail::Kind::DevicesChanged, 0, out.bytes());
    }

    void listening(std::string_view topic, bool on) override
    {
        detail::WireWriter out;
        out.text(topic);
        out.u32(on ? 1U : 0U);
        send(detail::Kind::Subscribe, 0, out.bytes());
    }

    void inputEvent(std::string_view source, std::string_view slot, double x, double y, std::uint64_t at) override
    {
        detail::WireWriter out;
        out.text(source);
        out.text(slot);
        out.f64(x);
        out.f64(y);
        out.u64(at);
        send(detail::Kind::InputEvent, 0, out.bytes());
    }

    void transportChanged(std::string_view transport) override
    {
        detail::WireWriter out;
        out.text(transport);
        send(detail::Kind::TransportChanged, 0, out.bytes());
    }

    void sourcesChanged(std::string_view source) override
    {
        detail::WireWriter out;
        out.text(source);
        send(detail::Kind::SourcesChanged, 0, out.bytes());
    }

private:
    void pump(std::uint64_t id, std::chrono::steady_clock::time_point deadline)
    {
        for (;;) {
            {
                const std::scoped_lock lock(m_claimMutex);
                if (m_closed || m_answers[id].has_value()) {
                    return;
                }
            }
            detail::Message message;
            const detail::ReadResult read = m_channel.toHost().read(message);
            if (read == detail::ReadResult::Broken) {
                close();
                return;
            }
            if (read == detail::ReadResult::Read) {
                if (message.kind == detail::Kind::ClaimAnswer) {
                    answered(message);
                } else {
                    m_deferred.push_back(std::move(message));
                }
                continue;
            }
            const auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (left.count() <= 0) {
                return;
            }
            const HANDLE waits[] = {m_channel.toHostEvent(), m_parent};
            const DWORD waited =
                WaitForMultipleObjects(m_parent != nullptr ? 2 : 1, waits, FALSE, static_cast<DWORD>(left.count()));
            if (waited != WAIT_OBJECT_0 && waited != WAIT_TIMEOUT) {
                close();
                return;
            }
        }
    }

    StoreReply askStore(std::string_view key, bool setting, std::optional<std::string_view> json)
    {
        detail::WireWriter out;
        out.text(key);
        out.u32(setting ? 1U : 0U);
        out.u32(json ? 1U : 0U);
        out.text(json.value_or(std::string_view()));
        std::uint64_t id = 0;
        {
            const std::scoped_lock lock(m_claimMutex);
            if (m_closed) {
                return {ServiceStatus::Failed, std::nullopt};
            }
            id = ++m_lastStore;
            m_stores.emplace(id, std::nullopt);
        }
        send(detail::Kind::StoreCall, id, out.bytes());
        const auto deadline = std::chrono::steady_clock::now() + kStoreWait;
        if (std::this_thread::get_id() == m_main) {
            pumpStore(id, deadline);
        }
        std::unique_lock lock(m_claimMutex);
        m_answered.wait_until(lock, deadline, [this, id] { return m_closed || m_stores[id].has_value(); });
        std::optional<StoreReply> reply = std::move(m_stores[id]);
        m_stores.erase(id);
        return reply ? std::move(*reply) : StoreReply{ServiceStatus::Failed, std::nullopt};
    }

    void pumpStore(std::uint64_t id, std::chrono::steady_clock::time_point deadline)
    {
        for (;;) {
            {
                const std::scoped_lock lock(m_claimMutex);
                if (m_closed || m_stores[id].has_value()) {
                    return;
                }
            }
            detail::Message message;
            const detail::ReadResult read = m_channel.toHost().read(message);
            if (read == detail::ReadResult::Broken) {
                close();
                return;
            }
            if (read == detail::ReadResult::Read) {
                if (message.kind == detail::Kind::StoreAnswer) {
                    storeAnswered(message);
                } else if (message.kind == detail::Kind::ClaimAnswer) {
                    answered(message);
                } else {
                    m_deferred.push_back(std::move(message));
                }
                continue;
            }
            const auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (left.count() <= 0) {
                return;
            }
            const HANDLE waits[] = {m_channel.toHostEvent(), m_parent};
            const DWORD waited =
                WaitForMultipleObjects(m_parent != nullptr ? 2 : 1, waits, FALSE, static_cast<DWORD>(left.count()));
            if (waited != WAIT_OBJECT_0 && waited != WAIT_TIMEOUT) {
                close();
                return;
            }
        }
    }

    detail::Channel& m_channel;
    HANDLE m_parent = nullptr;
    std::thread::id m_main;
    std::mutex m_mutex;
    std::condition_variable m_room;
    std::size_t m_unacked = 0;
    bool m_shut = false;
    std::mutex m_claimMutex;
    std::condition_variable m_answered;
    std::map<std::uint64_t, std::optional<ClaimResult>> m_answers;
    std::uint64_t m_lastClaim = 0;
    bool m_closed = false;
    std::deque<detail::Message> m_deferred;
    std::map<std::uint64_t, std::optional<ServiceResult>> m_services;
    std::uint64_t m_lastService = 0;
    std::map<std::uint64_t, std::optional<StoreReply>> m_stores;
    std::uint64_t m_lastStore = 0;
    std::map<std::uint64_t, std::optional<ServiceResult>> m_devices;
    std::uint64_t m_lastDevice = 0;
};

class Worker
{
public:
    Worker(Module& module, Link& link)
        : m_module(module)
        , m_link(link)
        , m_thread([this] { run(); })
    {
    }

    ~Worker()
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_stopping = true;
        }
        m_wake.notify_all();
    }

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    void push(detail::Message message)
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_queue.push_back(std::move(message));
        }
        m_wake.notify_one();
    }

private:
    void run()
    {
        t_hostThread = true;
        for (;;) {
            detail::Message message;
            {
                std::unique_lock lock(m_mutex);
                m_wake.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
                if (m_queue.empty()) {
                    return;
                }
                message = std::move(m_queue.front());
                m_queue.pop_front();
            }
            answer(message);
        }
    }

    void answer(const detail::Message& message)
    {
        if (message.kind == detail::Kind::DeviceList || message.kind == detail::Kind::DeviceAct) {
            reply(message.id, provided(message));
            return;
        }
        if (message.kind == detail::Kind::TransportDiscover) {
            reply(message.id, discovered(message));
            return;
        }
        if (message.kind == detail::Kind::SourceList) {
            detail::WireReader in(message.payload);
            std::uint32_t kind = 0;
            std::uint32_t source = 0;
            const bool good = in.u32(kind) && kind <= static_cast<std::uint32_t>(MediaKind::Audio) && in.u32(source) &&
                              in.done();
            reply(message.id, good ? m_module.listSources(static_cast<MediaKind>(kind), source)
                                   : CallResult{CallStatus::BadArgument, {}, "the request was damaged"});
            return;
        }
        if (message.kind == detail::Kind::ClaimWanted) {
            detail::WireReader in(message.payload);
            std::string resource;
            std::string by;
            const bool yields = in.text(resource, kMostResourceBytes) && in.text(by, kMostClaimerBytes) &&
                                in.done() && m_module.claimWanted(resource, by);
            reply(message.id, yields ? CallResult{CallStatus::Ok, {}, {}}
                                     : CallResult{CallStatus::Unavailable, {}, "the extension keeps the hardware"});
            return;
        }
        detail::WireReader in(message.payload);
        std::uint32_t node = 0;
        std::uint32_t count = 0;
        std::vector<Value> values;
        bool good = in.u32(node) && in.u32(count) && count <= kMostParams;
        for (std::uint32_t index = 0; good && index < count; ++index) {
            Value value;
            good = in.value(value);
            values.push_back(std::move(value));
        }
        reply(message.id, good && in.done() ? m_module.call(node, values)
                                            : CallResult{CallStatus::BadArgument, {}, "the call was damaged"});
    }

    CallResult provided(const detail::Message& message)
    {
        detail::WireReader in(message.payload);
        std::uint32_t provider = 0;
        if (!in.u32(provider)) {
            return {CallStatus::BadArgument, {}, "the request was damaged"};
        }
        if (message.kind == detail::Kind::DeviceList) {
            return in.done() ? m_module.listDevices(provider)
                             : CallResult{CallStatus::BadArgument, {}, "the request was damaged"};
        }
        std::string device;
        std::string action;
        if (!in.text(device, kMostDeviceBytes) || !in.text(action, kMostActionBytes) || !in.done()) {
            return {CallStatus::BadArgument, {}, "the request was damaged"};
        }
        return m_module.actOnDevice(provider, device, action);
    }

    CallResult discovered(const detail::Message& message)
    {
        detail::WireReader in(message.payload);
        std::uint32_t transport = 0;
        if (!in.u32(transport) || !in.done()) {
            return {CallStatus::BadArgument, {}, "the request was damaged"};
        }
        return m_module.discover(transport);
    }

    void reply(std::uint64_t id, const CallResult& result)
    {
        detail::WireWriter out;
        out.u32(static_cast<std::uint32_t>(result.status));
        out.value(result.value);
        out.text(result.problem);
        m_link.send(detail::Kind::Answer, id, out.bytes());
    }

    Module& m_module;
    Link& m_link;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<detail::Message> m_queue;
    bool m_stopping = false;
    std::jthread m_thread;
};

struct Look {
    std::uint64_t id = 0;
    VisionKind kind = VisionKind::Ocr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::shared_ptr<detail::ImageSection> fresh;
    std::string params;
};

void replyTo(Link& link, std::uint64_t id, const CallResult& result)
{
    detail::WireWriter out;
    out.u32(static_cast<std::uint32_t>(result.status));
    out.value(result.value);
    out.text(result.problem);
    link.send(detail::Kind::Answer, id, out.bytes());
}

class Sight
{
public:
    Sight(Module& module, Link& link, std::size_t engine)
        : m_module(module)
        , m_link(link)
        , m_engine(engine)
        , m_thread([this] { run(); })
    {
    }

    ~Sight()
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_stopping = true;
        }
        m_wake.notify_all();
    }

    Sight(const Sight&) = delete;
    Sight& operator=(const Sight&) = delete;

    void push(Look look)
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_queue.push_back(std::move(look));
        }
        m_wake.notify_one();
    }

private:
    void run()
    {
        t_hostThread = true;
        for (;;) {
            Look look;
            {
                std::unique_lock lock(m_mutex);
                m_wake.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
                if (m_stopping) {
                    return;
                }
                look = std::move(m_queue.front());
                m_queue.pop_front();
            }
            replyTo(m_link, look.id, seen(look));
        }
    }

    CallResult seen(const Look& look)
    {
        if (look.fresh != nullptr) {
            m_section = look.fresh;
        }
        const std::uint64_t needed = std::uint64_t{4} * look.width * look.height;
        if (m_section == nullptr || m_section->bytes() < needed) {
            return {CallStatus::Failed, {}, "the picture did not arrive"};
        }
        const VisionImage image{.pixels = std::shared_ptr<const std::uint8_t>(m_section, m_section->data()),
                                .width = look.width,
                                .height = look.height,
                                .stride = 4 * look.width};
        return m_module.recognize(m_engine, look.kind, image, look.params);
    }

    Module& m_module;
    Link& m_link;
    std::size_t m_engine = 0;
    std::shared_ptr<detail::ImageSection> m_section;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<Look> m_queue;
    bool m_stopping = false;
    std::jthread m_thread;
};

void sighted(std::vector<std::unique_ptr<Sight>>& sights, Link& link, const detail::Message& message)
{
    detail::WireReader in(message.payload);
    std::uint32_t engine = 0;
    std::uint32_t kind = 0;
    std::uint64_t handle = 0;
    std::uint64_t bytes = 0;
    Look look{.id = message.id};
    const bool good = in.u32(engine) && in.u32(kind) && in.u32(look.width) && in.u32(look.height) && in.u64(handle) &&
                      in.u64(bytes) && in.text(look.params, kMostVisionParamBytes) && in.done();
    if (handle != 0) {
        look.fresh = detail::ImageSection::open(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(handle)), bytes);
    }
    if (!good || engine >= sights.size() || kind > static_cast<std::uint32_t>(VisionKind::Classify) ||
        (handle != 0 && look.fresh == nullptr)) {
        replyTo(link, message.id, {CallStatus::BadArgument, {}, "the picture was damaged"});
        return;
    }
    look.kind = static_cast<VisionKind>(kind);
    sights[engine]->push(std::move(look));
}

class Jobs
{
public:
    Jobs(Module& module, Link& link) noexcept
        : m_module(module)
        , m_link(link)
    {
    }

    Jobs(const Jobs&) = delete;
    Jobs& operator=(const Jobs&) = delete;

    bool handle(const detail::Message& message)
    {
        if (message.kind == detail::Kind::JobStart) {
            start(message);
            return true;
        }
        if (message.kind == detail::Kind::ExportStart) {
            exportStart(message);
            return true;
        }
        if (message.kind != detail::Kind::JobCancel) {
            return false;
        }
        std::uint64_t running = 0;
        {
            const std::scoped_lock lock(m_mutex);
            if (const auto found = m_running.find(message.id); found != m_running.end()) {
                running = found->second;
            }
        }
        if (running != 0) {
            m_module.cancelJob(running);
        }
        return true;
    }

private:
    void start(const detail::Message& message)
    {
        const std::uint64_t id = message.id;
        detail::WireReader in(message.payload);
        std::uint32_t index = 0;
        std::string args;
        const bool good = in.u32(index) && in.text(args, kMostParamBytes) && in.done();
        Link& link = m_link;
        JobEvents events{.progress =
                             [&link, id](double fraction, std::string text) {
                                 detail::WireWriter out;
                                 out.u32(0);
                                 out.f64(fraction);
                                 out.u32(0);
                                 out.text(text);
                                 link.send(detail::Kind::JobUpdate, id, out.bytes());
                             },
                         .log =
                             [&link, id](int level, std::string text) {
                                 detail::WireWriter out;
                                 out.u32(1);
                                 out.f64(0.0);
                                 out.u32(static_cast<std::uint32_t>(level));
                                 out.text(text);
                                 link.send(detail::Kind::JobUpdate, id, out.bytes());
                             },
                         .done =
                             [this, id](bool worked, std::string text) {
                                 {
                                     const std::scoped_lock lock(m_mutex);
                                     m_running.erase(id);
                                 }
                                 finish(id, worked, text);
                             },
                         .output =
                             [&link, id](std::string json) {
                                 detail::WireWriter out;
                                 out.u32(2);
                                 out.f64(0.0);
                                 out.u32(0);
                                 out.text(json);
                                 link.send(detail::Kind::JobUpdate, id, out.bytes());
                             }};
        {
            const std::scoped_lock lock(m_mutex);
            m_running[id] = 0;
        }
        const std::uint64_t running = good ? m_module.startJob(index, args, std::move(events)) : 0;
        const std::scoped_lock lock(m_mutex);
        const auto found = m_running.find(id);
        if (found == m_running.end()) {
            return;
        }
        if (running != 0) {
            found->second = running;
            return;
        }
        m_running.erase(found);
        finish(id, false,
               good ? "the job could not start: there is no such job, or 4 of the extension's jobs are running already"
                    : "the request to start the job was damaged");
    }

    void exportStart(const detail::Message& message)
    {
        const std::uint64_t id = message.id;
        detail::WireReader in(message.payload);
        std::uint32_t index = 0;
        std::string input;
        std::string output;
        const bool good = in.u32(index) && in.text(input, kMostExportPathBytes) &&
                          in.text(output, kMostExportPathBytes) && in.done();
        Link& link = m_link;
        JobEvents events{.progress =
                             [&link, id](double fraction, std::string text) {
                                 detail::WireWriter out;
                                 out.u32(0);
                                 out.f64(fraction);
                                 out.u32(0);
                                 out.text(text);
                                 link.send(detail::Kind::JobUpdate, id, out.bytes());
                             },
                         .log =
                             [&link, id](int level, std::string text) {
                                 detail::WireWriter out;
                                 out.u32(1);
                                 out.f64(0.0);
                                 out.u32(static_cast<std::uint32_t>(level));
                                 out.text(text);
                                 link.send(detail::Kind::JobUpdate, id, out.bytes());
                             },
                         .done =
                             [this, id](bool worked, std::string text) {
                                 {
                                     const std::scoped_lock lock(m_mutex);
                                     m_running.erase(id);
                                 }
                                 finish(id, worked, text);
                             }};
        {
            const std::scoped_lock lock(m_mutex);
            m_running[id] = 0;
        }
        const std::uint64_t running = good ? m_module.startExport(index, input, output, std::move(events)) : 0;
        const std::scoped_lock lock(m_mutex);
        const auto found = m_running.find(id);
        if (found == m_running.end()) {
            return;
        }
        if (running != 0) {
            found->second = running;
            return;
        }
        m_running.erase(found);
        finish(id, false,
               good ? "the export could not start: there is no such exporter, or 4 of the extension's jobs are running "
                      "already"
                    : "the request to start the export was damaged");
    }

    void finish(std::uint64_t id, bool worked, std::string_view text)
    {
        detail::WireWriter out;
        out.u32(worked ? 1U : 0U);
        out.text(text);
        m_link.send(detail::Kind::JobDone, id, out.bytes());
    }

    Module& m_module;
    Link& m_link;
    std::mutex m_mutex;
    std::map<std::uint64_t, std::uint64_t> m_running;
};

class Streams
{
public:
    Streams(Module& module, Link& link)
        : m_module(module)
        , m_link(link)
        , m_thread([this] { run(); })
    {
    }

    ~Streams()
    {
        {
            const std::scoped_lock lock(m_shared->mutex);
            m_shared->stopping = true;
        }
        m_shared->wake.notify_all();
    }

    Streams(const Streams&) = delete;
    Streams& operator=(const Streams&) = delete;

    void push(detail::Message message)
    {
        {
            const std::scoped_lock lock(m_shared->mutex);
            m_shared->queue.push_back(std::move(message));
        }
        m_shared->wake.notify_one();
    }

private:
    struct Shared {
        std::mutex mutex;
        std::condition_variable wake;
        std::deque<detail::Message> queue;
        std::vector<std::uint64_t> writable;
        bool stopping = false;
    };

    void run()
    {
        for (;;) {
            std::optional<detail::Message> message;
            std::vector<std::uint64_t> ready;
            {
                std::unique_lock lock(m_shared->mutex);
                m_shared->wake.wait(lock, [this] {
                    return m_shared->stopping || !m_shared->queue.empty() || !m_shared->writable.empty();
                });
                if (m_shared->stopping) {
                    return;
                }
                ready.swap(m_shared->writable);
                if (!m_shared->queue.empty()) {
                    message = std::move(m_shared->queue.front());
                    m_shared->queue.pop_front();
                }
            }
            for (const std::uint64_t id : ready) {
                retry(id);
            }
            if (message) {
                handle(*message);
            }
        }
    }

    void handle(const detail::Message& message)
    {
        const std::uint64_t id = message.id;
        if (message.kind == detail::Kind::StreamOpen) {
            open(message);
        } else if (message.kind == detail::Kind::StreamSend) {
            if (const auto found = m_pending.find(id); found != m_pending.end()) {
                found->second.push_back(message.payload);
            } else if (attempt(id, message.payload) == StreamSent::Busy) {
                m_pending[id].push_back(message.payload);
            }
        } else if (message.kind == detail::Kind::StreamClose) {
            m_pending.erase(id);
            m_module.closeStream(id);
        }
    }

    void open(const detail::Message& message)
    {
        const std::uint64_t id = message.id;
        detail::WireReader in(message.payload);
        std::uint32_t transport = 0;
        StreamRequest request;
        if (!in.u32(transport) || !in.text(request.phone, kMostDeviceBytes) ||
            !in.text(request.channel, kMostNameBytes) || !in.text(request.session, kMostSessionBytes) || !in.done()) {
            closed(id, "the request to open a connection was damaged");
            return;
        }
        Link& link = m_link;
        const std::shared_ptr<Shared> shared = m_shared;
        const auto later = [shared, id] {
            {
                const std::scoped_lock lock(shared->mutex);
                shared->writable.push_back(id);
            }
            shared->wake.notify_one();
        };
        StreamEvents events;
        events.opened = [&link, id] { static_cast<void>(link.deliver(detail::Kind::StreamOpened, id, {})); };
        events.data = [&link, id](std::string_view bytes) {
            while (!bytes.empty()) {
                const std::string_view chunk = bytes.substr(0, kMostStreamBytes);
                if (!link.sendData(id, chunk)) {
                    return;
                }
                bytes.remove_prefix(chunk.size());
            }
        };
        events.writable = later;
        events.closed = [&link, id, later](std::string reason) {
            detail::WireWriter out;
            out.text(reason);
            static_cast<void>(link.deliver(detail::Kind::StreamClosed, id, out.bytes()));
            later();
        };
        if (!m_module.openStream(transport, id, request, std::move(events))) {
            closed(id, "the transport could not start a connection to the phone");
        }
    }

    StreamSent attempt(std::uint64_t id, std::string_view bytes)
    {
        const StreamSent sent = m_module.sendStream(id, bytes);
        if (sent == StreamSent::Taken) {
            detail::WireWriter out;
            out.u32(static_cast<std::uint32_t>(bytes.size()));
            static_cast<void>(m_link.deliver(detail::Kind::StreamSent, id, out.bytes()));
        } else if (sent == StreamSent::Failed) {
            m_pending.erase(id);
            m_module.closeStream(id);
            closed(id, "the extension could not send to the phone");
        }
        return sent;
    }

    void retry(std::uint64_t id)
    {
        auto found = m_pending.find(id);
        while (found != m_pending.end() && !found->second.empty()) {
            const std::string chunk = found->second.front();
            if (attempt(id, chunk) != StreamSent::Taken) {
                return;
            }
            found->second.pop_front();
        }
        if (found != m_pending.end()) {
            m_pending.erase(found);
        }
    }

    void closed(std::uint64_t id, std::string_view reason)
    {
        detail::WireWriter out;
        out.text(reason);
        static_cast<void>(m_link.deliver(detail::Kind::StreamClosed, id, out.bytes()));
    }

    Module& m_module;
    Link& m_link;
    std::shared_ptr<Shared> m_shared = std::make_shared<Shared>();
    std::map<std::uint64_t, std::deque<std::string>> m_pending;
    std::jthread m_thread;
};

HANDLE handleOf(std::uint64_t value) noexcept
{
    return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value));
}

class MediaStreams
{
public:
    MediaStreams(InProcessModule& module, Link& link) noexcept
        : m_module(module)
        , m_link(link)
    {
    }

    ~MediaStreams()
    {
        for (const auto& [id, stream] : m_sources) {
            m_module.closeSource(stream);
        }
        for (const auto& [id, sink] : m_sinks) {
            m_module.closeSink(sink);
        }
    }

    MediaStreams(const MediaStreams&) = delete;
    MediaStreams& operator=(const MediaStreams&) = delete;

    bool handle(const detail::Message& message)
    {
        switch (message.kind) {
        case detail::Kind::SourceOpen:
            openSource(message);
            return true;
        case detail::Kind::SourceClose:
            if (const auto found = m_sources.find(message.id); found != m_sources.end()) {
                m_module.closeSource(found->second);
                m_sources.erase(found);
            }
            return true;
        case detail::Kind::SinkOpen:
            openSink(message);
            return true;
        case detail::Kind::SinkClose:
            if (const auto found = m_sinks.find(message.id); found != m_sinks.end()) {
                m_module.closeSink(found->second);
                m_sinks.erase(found);
            }
            return true;
        default:
            return false;
        }
    }

private:
    void ended(std::uint64_t id, std::string_view why)
    {
        detail::WireWriter out;
        out.text(why);
        m_link.send(detail::Kind::StreamEnded, id, out.bytes());
    }

    void openSource(const detail::Message& message)
    {
        const std::uint64_t id = message.id;
        detail::WireReader in(message.payload);
        std::uint32_t kind = 0;
        std::uint32_t source = 0;
        std::string name;
        std::string params;
        std::uint64_t wake = 0;
        if (!in.u32(kind) || kind > static_cast<std::uint32_t>(MediaKind::Audio) || !in.u32(source) ||
            !in.text(name, kMostSourceIdBytes) || !in.text(params, kMostParamBytes) || !in.u64(wake) || !in.done()) {
            ended(id, "the request to open the source was damaged");
            return;
        }
        Link& link = m_link;
        SourceHooks hooks{.wake = handleOf(wake),
                          .made =
                              [&link, id](MediaKind made, void* section, std::size_t capacity) {
                                  detail::WireWriter out;
                                  out.u32(static_cast<std::uint32_t>(made));
                                  out.u64(capacity);
                                  out.u64(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(section)));
                                  link.send(detail::Kind::MediaRing, id, out.bytes());
                              },
                          .ended = [this, id](const std::string& why) { ended(id, why); }};
        const std::shared_ptr<MediaStream> stream =
            m_module.openSourceWith(static_cast<MediaKind>(kind), source, name, params, std::move(hooks));
        if (stream == nullptr) {
            ended(id, "the source did not open");
            return;
        }
        m_sources.insert_or_assign(id, stream->id());
    }

    void openSink(const detail::Message& message)
    {
        const std::uint64_t id = message.id;
        detail::WireReader in(message.payload);
        std::uint32_t index = 0;
        SinkStart start;
        std::uint32_t audio = 0;
        std::uint64_t frames = 0;
        std::uint64_t sounds = 0;
        std::uint64_t wake = 0;
        if (!in.u32(index) || !in.text(start.device, kMostDeviceBytes) || !in.text(start.name, kMostDeviceBytes) ||
            !in.u32(start.width) || !in.u32(start.height) || !in.u32(start.fps) || !in.u32(audio) || !in.u64(frames) ||
            !in.u64(sounds) || !in.u64(wake) || !in.done()) {
            ended(id, "the request to start sending was damaged");
            return;
        }
        start.audio = audio != 0;
        const std::shared_ptr<detail::SinkBox> box =
            detail::SinkBox::adopt(id, start, handleOf(frames), handleOf(sounds), handleOf(wake));
        if (box == nullptr) {
            ended(id, "the picture and the sound could not be shared with the extension's process");
            return;
        }
        box->setEnded([this, id](const std::string& why) { ended(id, why); });
        const std::uint64_t sink = m_module.attachSink(index, box);
        if (sink == 0) {
            box->setEnded({});
            ended(id, "the extension did not start sending");
            return;
        }
        m_sinks.insert_or_assign(id, sink);
    }

    InProcessModule& m_module;
    Link& m_link;
    std::map<std::uint64_t, std::uint64_t> m_sources;
    std::map<std::uint64_t, std::uint64_t> m_sinks;
};

class Taps
{
public:
    Taps(Module& module, Link& link)
        : m_module(module)
        , m_link(link)
        , m_thread([this] { run(); })
    {
    }

    ~Taps()
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_stopping = true;
        }
        m_wake.notify_all();
    }

    Taps(const Taps&) = delete;
    Taps& operator=(const Taps&) = delete;

    void push(detail::Message message)
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_queue.push_back(std::move(message));
        }
        m_wake.notify_one();
    }

private:
    struct Open {
        std::uint64_t local = 0;
        std::unique_ptr<detail::TapSection> section;
    };

    void run()
    {
        for (;;) {
            detail::Message message;
            {
                std::unique_lock lock(m_mutex);
                m_wake.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
                if (m_stopping) {
                    break;
                }
                message = std::move(m_queue.front());
                m_queue.pop_front();
            }
            serve(message);
        }
        for (const auto& [id, entry] : m_open) {
            m_module.closeTap(entry.local);
        }
        m_open.clear();
    }

    void serve(const detail::Message& message)
    {
        if (message.kind == detail::Kind::TapOpen) {
            begin(message);
            return;
        }
        const auto found = m_open.find(message.id);
        if (found == m_open.end()) {
            return;
        }
        if (message.kind == detail::Kind::TapClose) {
            m_module.closeTap(found->second.local);
            m_open.erase(found);
            return;
        }
        TapFrame frame;
        if (const std::optional<std::size_t> slot = found->second.section->take(frame)) {
            static_cast<void>(m_module.offerFrame(found->second.local, frame));
            found->second.section->done(*slot);
        }
    }

    void begin(const detail::Message& message)
    {
        detail::WireReader in(message.payload);
        std::uint32_t index = 0;
        TapOpen info;
        std::uint64_t handle = 0;
        std::uint64_t bytes = 0;
        const bool good = in.u32(index) && in.text(info.device, kMostDeviceBytes) && in.u32(info.width) &&
                          in.u32(info.height) && in.u32(info.rotation) && in.u64(handle) && in.u64(bytes) && in.done();
        std::unique_ptr<detail::TapSection> section =
            good ? detail::TapSection::open(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(handle)), bytes)
                 : nullptr;
        const std::uint64_t id = message.id;
        if (section == nullptr) {
            fail(id, "the picture for the frame tap could not be shared");
            return;
        }
        Link& link = m_link;
        const std::uint64_t local =
            m_module.openTap(index, info,
                             TapEvents{.overlay =
                                           [&link, id](std::vector<Shape> shapes) {
                                               detail::WireWriter out;
                                               out.shapes(shapes);
                                               link.send(detail::Kind::TapOverlay, id, out.bytes());
                                           },
                                       .failed =
                                           [&link, id](std::string problem) {
                                               detail::WireWriter out;
                                               out.text(problem);
                                               link.send(detail::Kind::TapFailed, id, out.bytes());
                                           }});
        if (local == 0) {
            fail(id, "the frame tap did not start");
            return;
        }
        if (const auto previous = m_open.find(id); previous != m_open.end()) {
            m_module.closeTap(previous->second.local);
        }
        m_open.insert_or_assign(id, Open{.local = local, .section = std::move(section)});
    }

    void fail(std::uint64_t id, std::string_view why)
    {
        detail::WireWriter out;
        out.text(why);
        m_link.send(detail::Kind::TapFailed, id, out.bytes());
    }

    Module& m_module;
    Link& m_link;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<detail::Message> m_queue;
    bool m_stopping = false;
    std::map<std::uint64_t, Open> m_open;
    std::jthread m_thread;
};

struct Start {
    std::string dll;
    std::string store;
    std::vector<std::string> granted;
    std::string settings;
    std::vector<DeclaredInput> inputs;
};

std::optional<Start> startOf(const detail::Message& message)
{
    if (message.kind != detail::Kind::Start) {
        return std::nullopt;
    }
    detail::WireReader in(message.payload);
    Start start;
    std::uint32_t count = 0;
    if (!in.text(start.dll, kMostPathBytes) || !in.text(start.store, kMostPathBytes) || !in.u32(count) || count > 32) {
        return std::nullopt;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        std::string permission;
        if (!in.text(permission, kMostPermissionBytes)) {
            return std::nullopt;
        }
        start.granted.push_back(std::move(permission));
    }
    if (!in.text(start.settings, kMostSettingsBytes)) {
        return std::nullopt;
    }
    if (!in.inputs(start.inputs)) {
        return std::nullopt;
    }
    return in.done() ? std::optional<Start>(std::move(start)) : std::nullopt;
}

void lost(Link& link, std::uint64_t session, std::string_view why)
{
    detail::WireWriter out;
    out.text(why);
    link.send(detail::Kind::TouchLost, session, out.bytes());
}

void touched(Module& module, Link& link, std::map<std::uint64_t, std::uint64_t>& sessions,
             const detail::Message& message)
{
    detail::WireReader in(message.payload);
    switch (message.kind) {
    case detail::Kind::TouchOpen: {
        std::uint32_t method = 0;
        std::string device;
        std::string options;
        if (!in.u32(method) || !in.text(device, kMostDeviceBytes) || !in.text(options, kMostOptionBytes)) {
            lost(link, message.id, "the touch method could not be opened");
            return;
        }
        const std::uint64_t opened = module.openTouch(method, device, options);
        if (opened == 0) {
            lost(link, message.id, "the touch method did not start");
            return;
        }
        sessions.insert_or_assign(message.id, opened);
        return;
    }
    case detail::Kind::TouchFrame: {
        const auto found = sessions.find(message.id);
        if (found == sessions.end()) {
            return;
        }
        TouchFrame frame;
        std::uint32_t count = 0;
        bool good = in.u32(frame.rotation) && in.u32(count) && count <= kMostContacts;
        for (std::uint32_t index = 0; good && index < count; ++index) {
            Contact contact;
            good = in.contact(contact);
            frame.contacts.push_back(contact);
        }
        if (!good || !module.touch(found->second, frame)) {
            module.closeTouch(found->second);
            sessions.erase(found);
            lost(link, message.id, "the touch method failed");
        }
        return;
    }
    case detail::Kind::TouchKey: {
        const auto found = sessions.find(message.id);
        KeyEvent key;
        std::uint32_t down = 0;
        if (found != sessions.end() && in.u32(key.keycode) && in.u32(down) && in.u32(key.repeat) && in.u32(key.meta)) {
            key.down = down != 0;
            static_cast<void>(module.key(found->second, key));
        }
        return;
    }
    case detail::Kind::TouchText: {
        const auto found = sessions.find(message.id);
        std::string typed;
        if (found != sessions.end() && in.text(typed, kMostTextBytes)) {
            static_cast<void>(module.text(found->second, typed));
        }
        return;
    }
    case detail::Kind::TouchMouse: {
        const auto found = sessions.find(message.id);
        MouseEvent mouse;
        std::uint32_t dx = 0;
        std::uint32_t dy = 0;
        std::uint32_t wheel = 0;
        if (found != sessions.end() && in.u32(mouse.buttons) && in.u32(dx) && in.u32(dy) && in.u32(wheel)) {
            mouse.dx = static_cast<std::int32_t>(dx);
            mouse.dy = static_cast<std::int32_t>(dy);
            mouse.wheel = static_cast<std::int32_t>(wheel);
            static_cast<void>(module.mouse(found->second, mouse));
        }
        return;
    }
    case detail::Kind::TouchPad: {
        const auto found = sessions.find(message.id);
        PadEvent pad;
        std::uint32_t axes[4] = {};
        if (found != sessions.end() && in.u32(pad.buttons) && in.u32(pad.hat) && in.u32(axes[0]) && in.u32(axes[1]) &&
            in.u32(axes[2]) && in.u32(axes[3]) && in.u32(pad.leftTrigger) && in.u32(pad.rightTrigger)) {
            pad.leftX = static_cast<std::int32_t>(axes[0]);
            pad.leftY = static_cast<std::int32_t>(axes[1]);
            pad.rightX = static_cast<std::int32_t>(axes[2]);
            pad.rightY = static_cast<std::int32_t>(axes[3]);
            static_cast<void>(module.gamepad(found->second, pad));
        }
        return;
    }
    case detail::Kind::TouchClose: {
        if (const auto found = sessions.find(message.id); found != sessions.end()) {
            module.closeTouch(found->second);
            sessions.erase(found);
        }
        return;
    }
    default:
        return;
    }
}

bool instanced(Module& module, Link& link, std::map<std::uint64_t, std::uint64_t>& instances,
               const detail::Message& message)
{
    detail::WireReader in(message.payload);
    switch (message.kind) {
    case detail::Kind::InstanceOpen: {
        std::uint32_t kind = 0;
        std::uint32_t index = 0;
        std::uint64_t seed = 0;
        std::string params;
        std::uint64_t opened = 0;
        if (in.u32(kind) && kind <= static_cast<std::uint32_t>(InstanceKind::Behavior) && in.u32(index) &&
            in.u64(seed) && in.text(params, kMostParamBytes) && in.done()) {
            opened = module.openInstance(static_cast<InstanceKind>(kind), index, params, seed);
        }
        if (const auto found = instances.find(message.id); found != instances.end() && found->second != 0) {
            module.closeInstance(found->second);
        }
        instances.insert_or_assign(message.id, opened);
        return true;
    }
    case detail::Kind::InstanceCall: {
        StepInput input;
        std::uint64_t instance = 0;
        std::uint32_t call = 0;
        StepOutput answer;
        answer.status = StepStatus::Failed;
        if (in.u64(instance) && in.u32(call) && call <= static_cast<std::uint32_t>(StepCall::Wake) &&
            in.u64(input.now) && in.u64(input.wakeAt) && in.event(input.event) && in.command(input.command) &&
            in.done()) {
            input.call = static_cast<StepCall>(call);
            if (const auto found = instances.find(instance); found != instances.end() && found->second != 0) {
                answer = module.step(found->second, input);
            }
        }
        detail::WireWriter out;
        out.answer(answer);
        link.send(detail::Kind::InstanceAnswer, message.id, out.bytes());
        return true;
    }
    case detail::Kind::InstanceReset:
        if (const auto found = instances.find(message.id); found != instances.end() && found->second != 0) {
            module.resetInstance(found->second);
        }
        return true;
    case detail::Kind::InstanceClose:
        if (const auto found = instances.find(message.id); found != instances.end()) {
            if (found->second != 0) {
                module.closeInstance(found->second);
            }
            instances.erase(found);
        }
        return true;
    default:
        return false;
    }
}

}

int serve(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    std::wstring_view handles;
    DWORD parentId = 0;
    std::filesystem::path crashes;
    std::filesystem::path log;
    bool restricted = false;
    for (int index = 1; index + 1 < argc; index += 2) {
        const std::wstring_view name(argv[index]);
        if (name == L"--channel") {
            handles = argv[index + 1];
        } else if (name == L"--parent") {
            parentId = static_cast<DWORD>(std::wcstoul(argv[index + 1], nullptr, 10));
        } else if (name == L"--crashes") {
            crashes = argv[index + 1];
        } else if (name == L"--log") {
            log = argv[index + 1];
        } else if (name == L"--sandbox") {
            restricted = std::wstring_view(argv[index + 1]) == L"restricted";
        }
    }
    if (!crashes.empty()) {
        static_cast<void>(platform::installCrashHandler(crashes, log));
    }
    std::optional<detail::Channel> channel = detail::Channel::open(handles);
    if (!channel) {
        return kUsage;
    }
    const HANDLE parent = parentId != 0 ? OpenProcess(SYNCHRONIZE, FALSE, parentId) : nullptr;
    detail::Message first;
    for (;;) {
        const detail::ReadResult read = channel->toHost().read(first);
        if (read == detail::ReadResult::Read) {
            break;
        }
        if (read == detail::ReadResult::Broken ||
            WaitForSingleObject(channel->toHostEvent(), kStartWaitMs) != WAIT_OBJECT_0) {
            return read == detail::ReadResult::Broken ? kBroken : kNoStart;
        }
    }
    const std::optional<Start> start = startOf(first);
    if (!start) {
        return kBroken;
    }
    Link link(*channel, parent);
    std::string problem;
    std::unique_ptr<InProcessModule> module = InProcessModule::load({.dll = pathOf(start->dll),
                                                                     .store = pathOf(start->store),
                                                                     .granted = start->granted,
                                                                     .services = &link,
                                                                     .settings = start->settings,
                                                                     .inputs = start->inputs,
                                                                     .hostStore = restricted},
                                                                    problem);
    if (!module) {
        detail::WireWriter out;
        out.text(problem);
        link.send(detail::Kind::Failed, 0, out.bytes());
        return kNotLoaded;
    }
    {
        detail::WireWriter out;
        out.info(module->info());
        link.send(detail::Kind::Ready, 0, out.bytes());
    }
    Jobs jobs(*module, link);
    int status = kDone;
    {
        Worker worker(*module, link);
        Worker claims(*module, link);
        Streams streams(*module, link);
        MediaStreams media(*module, link);
        Taps taps(*module, link);
        std::vector<std::unique_ptr<Sight>> sights;
        for (std::size_t engine = 0; engine < module->info().visionEngines.size(); ++engine) {
            sights.push_back(std::make_unique<Sight>(*module, link, engine));
        }
        std::map<std::uint64_t, std::uint64_t> sessions;
        std::map<std::uint64_t, std::uint64_t> instances;
        bool running = true;
        while (running) {
            detail::Message message;
            if (std::optional<detail::Message> held = link.deferred()) {
                message = std::move(*held);
            } else {
                const detail::ReadResult read = channel->toHost().read(message);
                if (read == detail::ReadResult::Broken) {
                    status = kBroken;
                    break;
                }
                if (read == detail::ReadResult::Empty) {
                    const HANDLE waits[] = {channel->toHostEvent(), parent};
                    const DWORD waited = WaitForMultipleObjects(parent != nullptr ? 2 : 1, waits, FALSE, INFINITE);
                    running = waited == WAIT_OBJECT_0;
                    continue;
                }
            }
            if (message.kind == detail::Kind::ClaimAnswer) {
                link.answered(message);
            } else if (message.kind == detail::Kind::ServiceAnswer) {
                link.serviceAnswered(message);
            } else if (message.kind == detail::Kind::StoreAnswer) {
                link.storeAnswered(message);
            } else if (message.kind == detail::Kind::DeviceAnswer) {
                link.deviceAnswered(message);
            } else if (message.kind == detail::Kind::DeviceEvent) {
                detail::WireReader in(message.payload);
                std::string json;
                if (in.text(json, kMostServiceBytes) && in.done()) {
                    module->deviceEvent(message.id, json);
                }
            } else if (message.kind == detail::Kind::ClaimWanted) {
                claims.push(std::move(message));
            } else if (message.kind == detail::Kind::TapOpen || message.kind == detail::Kind::TapFrame ||
                       message.kind == detail::Kind::TapClose) {
                taps.push(std::move(message));
            } else if (message.kind == detail::Kind::Call || message.kind == detail::Kind::DeviceList ||
                       message.kind == detail::Kind::DeviceAct || message.kind == detail::Kind::TransportDiscover) {
                worker.push(std::move(message));
            } else if (message.kind == detail::Kind::StreamTaken) {
                detail::WireReader in(message.payload);
                std::uint32_t size = 0;
                if (in.u32(size) && in.done()) {
                    link.taken(size);
                }
            } else if (message.kind == detail::Kind::StreamOpen || message.kind == detail::Kind::StreamSend ||
                       message.kind == detail::Kind::StreamClose) {
                streams.push(std::move(message));
            } else if (message.kind == detail::Kind::SourceList) {
                worker.push(std::move(message));
            } else if (message.kind == detail::Kind::VisionRun) {
                sighted(sights, link, message);
            } else if (message.kind == detail::Kind::Stop) {
                running = false;
            } else if (message.kind == detail::Kind::Settings) {
                detail::WireReader in(message.payload);
                std::string json;
                if (in.text(json, kMostSettingsBytes) && in.done()) {
                    module->setSettings(json);
                }
            } else if (message.kind == detail::Kind::AppEvent) {
                detail::WireReader in(message.payload);
                std::string topic;
                std::string json;
                if (in.text(topic, kMostTopicBytes) && in.text(json, kMostEventBytes) && in.done()) {
                    module->appEvent(topic, json);
                }
            } else if (jobs.handle(message)) {
                continue;
            } else if (media.handle(message)) {
                continue;
            } else if (!instanced(*module, link, instances, message)) {
                touched(*module, link, sessions, message);
            }
        }
        link.close();
        for (const auto& [id, session] : sessions) {
            module->closeTouch(session);
        }
        for (const auto& [id, instance] : instances) {
            if (instance != 0) {
                module->closeInstance(instance);
            }
        }
    }
    module.reset();
    if (parent != nullptr) {
        CloseHandle(parent);
    }
    return status;
}

}
