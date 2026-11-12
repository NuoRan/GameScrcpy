#include "HardwareAccess.h"

#include "ExtensionHost.h"
#include "HardwareClaims.h"

#include <gs/base/Log.h>
#include <gs/ext/Manifest.h>
#include <gs/input/InputSource.h>
#include <gs/platform/AtomicFile.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMetaObject>
#include <QThread>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

constexpr std::string_view kGrantsFormat = "gamescrcpy.hardware-grants";
constexpr int kGrantsVersion = 1;
constexpr std::size_t kMostGrantsBytes = 1024 * 1024;
constexpr std::size_t kMostIdLength = 512;
constexpr std::size_t kMostNameLength = 256;
constexpr qsizetype kMostNamePrefix = 64;
constexpr qsizetype kMostServices = 16;
constexpr std::string_view kPermission = "hardware";
constexpr std::array<std::string_view, 3> kCalls{"hardware.list", "hardware.input", "hardware.release"};
constexpr std::array<std::string_view, 7> kRequests{"hardware.request", "hardware.open",   "hardware.write",
                                                    "hardware.control", "hardware.close",  "hardware.forget",
                                                    "hardware.claim"};

QString fromView(std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::string logicHolder(std::uint64_t logic)
{
    return "logic:" + std::to_string(logic);
}

std::string resourceOf(const hardware::DeviceInfo& device)
{
    constexpr std::string_view kPortPrefix = "\\\\.\\";
    std::string resource;
    if (device.kind == hardware::Kind::Serial) {
        const std::string_view path(device.path);
        resource = "serial:" + std::string(path.starts_with(kPortPrefix) ? path.substr(kPortPrefix.size()) : path);
    } else if (device.kind == hardware::Kind::Ble) {
        resource = "ble:" + device.path;
    } else {
        resource = device.id;
    }
    return exthost::validResource(resource) ? resource : std::string();
}

QString compactJson(const QJsonDocument& document)
{
    return QString::fromUtf8(document.toJson(QJsonDocument::Compact));
}

QJsonObject objectOf(const QByteArray& json)
{
    QJsonParseError problem;
    const QJsonDocument document = QJsonDocument::fromJson(json, &problem);
    return problem.error == QJsonParseError::NoError && document.isObject() ? document.object() : QJsonObject();
}

QString ownerOf(const LogicCaller& caller)
{
    return caller.extension.isEmpty() ? fromView(HardwareAccess::kOwnLogic) : caller.extension;
}

QString noPermission()
{
    return QStringLiteral("GS-API-080 this extension may not use devices: its manifest does not ask for the "
                          "permission \"hardware\", or you have not allowed it");
}

QString notGranted()
{
    return QStringLiteral("GS-API-081 the device has not been allowed; ask for it with hardware.request()");
}

QString notConnected(const QString& name)
{
    return QStringLiteral("GS-API-082 %1 is not connected").arg(name);
}

QString failedWith(const std::string& problem)
{
    return QStringLiteral("GS-API-083 ") + QString::fromStdString(problem);
}

QString notOpen()
{
    return QStringLiteral("GS-API-084 the device is not open");
}

QString invalidWith(const QString& what)
{
    return QStringLiteral("GS-API-085 ") + what;
}

QString badResource()
{
    return invalidWith(QStringLiteral("the resource names hardware as serial:COM9, ble:AA:BB:CC:DD:EE:FF, hid: and the "
                                      "device path, or usb:vid:pid:serial number"));
}

QString displayName(const hardware::DeviceInfo& device)
{
    if (!device.name.empty()) {
        return QString::fromStdString(device.name);
    }
    const std::string detail = hardware::detailOf(device);
    return QString::fromStdString(detail.empty() ? device.id : detail);
}

QString base64(const hardware::Bytes& bytes)
{
    return QString::fromLatin1(
        QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())).toBase64());
}

std::optional<hardware::Bytes> bytesOf(const QJsonValue& value)
{
    if (!value.isString()) {
        return std::nullopt;
    }
    const QByteArray::FromBase64Result decoded =
        QByteArray::fromBase64Encoding(value.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (decoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok) {
        return std::nullopt;
    }
    const auto* first = reinterpret_cast<const std::uint8_t*>(decoded.decoded.constData());
    return hardware::Bytes(first, first + decoded.decoded.size());
}

std::optional<long long> wholeNumber(const QJsonValue& value, long long least, long long most)
{
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number || number < static_cast<double>(least) ||
        number > static_cast<double>(most)) {
        return std::nullopt;
    }
    return static_cast<long long>(number);
}

std::optional<std::uint16_t> wordOf(const QJsonValue& value)
{
    const std::optional<long long> number = wholeNumber(value, 0, 0xFFFF);
    if (!number) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(*number);
}

std::optional<std::uint8_t> reportOf(const QJsonValue& value)
{
    const std::optional<long long> number = wholeNumber(value, 0, 0xFF);
    if (!number) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(*number);
}

std::optional<std::string> uuidOf(const QJsonValue& value)
{
    return value.isString() ? hardware::canonicalUuid(value.toString().toStdString()) : std::nullopt;
}

bool onlyKeys(const QJsonObject& object, std::initializer_list<QStringView> known)
{
    return std::ranges::all_of(object.keys(), [&known](const QString& key) {
        return std::ranges::find(known, QStringView(key)) != known.end();
    });
}

std::optional<hardware::Filter> filterOf(const QJsonValue& value)
{
    if (!value.isObject()) {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    if (!onlyKeys(object, {u"vendorId", u"productId", u"usagePage", u"usage", u"services", u"namePrefix"})) {
        return std::nullopt;
    }
    hardware::Filter filter;
    const auto word = [&object](QStringView key, std::optional<std::uint16_t>& field) {
        const QJsonValue given = object.value(key);
        if (given.isUndefined()) {
            return true;
        }
        field = wordOf(given);
        return field.has_value();
    };
    if (!word(u"vendorId", filter.vendorId) || !word(u"productId", filter.productId) ||
        !word(u"usagePage", filter.usagePage) || !word(u"usage", filter.usage)) {
        return std::nullopt;
    }
    if (const QJsonValue services = object.value(u"services"); !services.isUndefined()) {
        if (!services.isArray() || services.toArray().size() > kMostServices) {
            return std::nullopt;
        }
        for (const QJsonValue& service : services.toArray()) {
            const std::optional<std::string> uuid = uuidOf(service);
            if (!uuid) {
                return std::nullopt;
            }
            filter.services.push_back(*uuid);
        }
    }
    if (const QJsonValue prefix = object.value(u"namePrefix"); !prefix.isUndefined()) {
        if (!prefix.isString() || prefix.toString().isEmpty() || prefix.toString().size() > kMostNamePrefix) {
            return std::nullopt;
        }
        filter.namePrefix = prefix.toString().toStdString();
    }
    return filter;
}

bool serialOptionsOf(const QJsonValue& value, hardware::SerialOptions& options)
{
    if (value.isUndefined() || value.isNull()) {
        return true;
    }
    if (!value.isObject()) {
        return false;
    }
    const QJsonObject object = value.toObject();
    if (!onlyKeys(object, {u"baudRate", u"dataBits", u"parity", u"stopBits", u"flowControl", u"dtr", u"rts"})) {
        return false;
    }
    if (const QJsonValue baud = object.value(u"baudRate"); !baud.isUndefined()) {
        const std::optional<long long> number = wholeNumber(baud, hardware::kLeastBaudRate, hardware::kMostBaudRate);
        if (!number) {
            return false;
        }
        options.baudRate = static_cast<std::uint32_t>(*number);
    }
    if (const QJsonValue bits = object.value(u"dataBits"); !bits.isUndefined()) {
        const std::optional<long long> number = wholeNumber(bits, 5, 8);
        if (!number) {
            return false;
        }
        options.dataBits = static_cast<std::uint8_t>(*number);
    }
    if (const QJsonValue parity = object.value(u"parity"); !parity.isUndefined()) {
        const std::string name = parity.toString().toStdString();
        const auto found = std::ranges::find(hardware::kParityNames, std::string_view(name));
        if (!parity.isString() || found == hardware::kParityNames.end()) {
            return false;
        }
        options.parity = static_cast<hardware::Parity>(found - hardware::kParityNames.begin());
    }
    if (const QJsonValue stop = object.value(u"stopBits"); !stop.isUndefined()) {
        const double bits = stop.isDouble() ? stop.toDouble() : 0.0;
        if (bits == 1.0) {
            options.stopBits = hardware::StopBits::One;
        } else if (bits == 1.5) {
            options.stopBits = hardware::StopBits::OneAndHalf;
        } else if (bits == 2.0) {
            options.stopBits = hardware::StopBits::Two;
        } else {
            return false;
        }
    }
    if (const QJsonValue flow = object.value(u"flowControl"); !flow.isUndefined()) {
        const std::string name = flow.toString().toStdString();
        const auto found = std::ranges::find(hardware::kFlowControlNames, std::string_view(name));
        if (!flow.isString() || found == hardware::kFlowControlNames.end()) {
            return false;
        }
        options.flowControl = static_cast<hardware::FlowControl>(found - hardware::kFlowControlNames.begin());
    }
    const std::array<std::pair<QStringView, bool*>, 2> lines{{{u"dtr", &options.dtr}, {u"rts", &options.rts}}};
    for (const auto& [key, field] : lines) {
        const QJsonValue given = object.value(key);
        if (given.isUndefined()) {
            continue;
        }
        if (!given.isBool()) {
            return false;
        }
        *field = given.toBool();
    }
    return hardware::validSerialOptions(options);
}

std::optional<bool> flagOf(const QJsonValue& value, bool& valid)
{
    if (value.isUndefined() || value.isNull()) {
        return std::nullopt;
    }
    if (!value.isBool()) {
        valid = false;
        return std::nullopt;
    }
    return value.toBool();
}

QJsonObject grantJson(const HardwareGrant& grant)
{
    return {{QStringLiteral("id"), QString::fromStdString(grant.id)},
            {QStringLiteral("kind"), fromView(hardware::kindName(grant.kind))},
            {QStringLiteral("name"), QString::fromStdString(grant.name)}};
}

bool validOwner(const QString& owner)
{
    return owner == fromView(HardwareAccess::kOwnLogic) || ext::validExtensionId(owner.toStdString());
}

constexpr std::array<std::string_view, 4> kNativeRequests{"hardware.open", "hardware.write", "hardware.control",
                                                           "hardware.close"};

exthost::ServiceStatus statusOfRefusal(std::string_view message)
{
    if (message.starts_with("GS-API-080") || message.starts_with("GS-API-081")) {
        return exthost::ServiceStatus::Denied;
    }
    if (message.starts_with("GS-API-084") || message.starts_with("GS-API-085")) {
        return exthost::ServiceStatus::BadArgument;
    }
    if (message.starts_with("GS-API-070")) {
        return exthost::ServiceStatus::Unsupported;
    }
    return exthost::ServiceStatus::Failed;
}

HardwareAccess::NativeAnswer answeredOnce(HardwareAccess::NativeAnswer answer)
{
    struct Once {
        HardwareAccess::NativeAnswer answer;
        std::atomic<bool> given{false};

        Once() = default;
        Once(const Once&) = delete;
        Once& operator=(const Once&) = delete;

        ~Once()
        {
            if (answer && !given.load()) {
                answer({exthost::ServiceStatus::Failed, "GS-API-083 GameScrcpy stopped before the device answered"});
            }
        }
    };
    auto once = std::make_shared<Once>();
    once->answer = std::move(answer);
    return [once](exthost::ServiceResult result) {
        if (!once->given.exchange(true) && once->answer) {
            once->answer(std::move(result));
        }
    };
}

}

std::mutex HardwareAccess::s_nativeMutex;
std::weak_ptr<HardwareAccess::Shared> HardwareAccess::s_native;

struct HardwareAccess::Shared {
    mutable std::mutex mutex;
    HardwareAccess* owner = nullptr;
    std::map<QString, std::vector<HardwareGrant>> grants;
    std::set<QString> permitted;
    std::vector<hardware::DeviceInfo> present;
    std::set<std::pair<std::uint64_t, std::string>> claimWaits;
    std::set<std::pair<std::uint64_t, std::string>> cancelledClaimWaits;

    void post(std::function<void(HardwareAccess&)> work) const
    {
        const std::scoped_lock lock(mutex);
        if (owner == nullptr) {
            return;
        }
        HardwareAccess* target = owner;
        QMetaObject::invokeMethod(target, [target, work = std::move(work)] { work(*target); }, Qt::QueuedConnection);
    }

    [[nodiscard]] bool allowed(const QString& extension) const
    {
        const std::scoped_lock lock(mutex);
        return extension.isEmpty() || permitted.contains(extension);
    }

    void waitingForClaim(std::uint64_t logic, const std::string& resource)
    {
        const std::scoped_lock lock(mutex);
        claimWaits.emplace(logic, resource);
        cancelledClaimWaits.erase({logic, resource});
    }

    void cancelWaitingClaim(std::uint64_t logic, const std::string& resource)
    {
        const std::scoped_lock lock(mutex);
        if (claimWaits.contains({logic, resource})) {
            cancelledClaimWaits.emplace(logic, resource);
        }
    }

    [[nodiscard]] bool claimAnswered(std::uint64_t logic, const std::string& resource)
    {
        const std::scoped_lock lock(mutex);
        claimWaits.erase({logic, resource});
        return cancelledClaimWaits.erase({logic, resource}) > 0;
    }

    void forgetClaimWaits(std::uint64_t logic)
    {
        const std::scoped_lock lock(mutex);
        std::erase_if(claimWaits, [logic](const auto& entry) { return entry.first == logic; });
        std::erase_if(cancelledClaimWaits, [logic](const auto& entry) { return entry.first == logic; });
    }
};

class HardwareAccess::Sink final : public hardware::Events
{
public:
    Sink(std::shared_ptr<Shared> shared, QString handle)
        : m_shared(std::move(shared))
        , m_handle(std::move(handle))
    {
    }

    void data(hardware::Bytes bytes) override
    {
        send({{QStringLiteral("type"), QStringLiteral("data")}, {QStringLiteral("data"), base64(bytes)}});
    }

    void report(std::uint8_t id, hardware::Bytes bytes) override
    {
        send({{QStringLiteral("type"), QStringLiteral("report")},
              {QStringLiteral("report"), static_cast<int>(id)},
              {QStringLiteral("data"), base64(bytes)}});
    }

    void notified(const std::string& service, const std::string& characteristic, hardware::Bytes bytes) override
    {
        send({{QStringLiteral("type"), QStringLiteral("notify")},
              {QStringLiteral("service"), QString::fromStdString(service)},
              {QStringLiteral("characteristic"), QString::fromStdString(characteristic)},
              {QStringLiteral("data"), base64(bytes)}});
    }

    void closed(hardware::Closed reason, const std::string& problem) override
    {
        m_shared->post([handle = m_handle, reason, problem](HardwareAccess& self) {
            self.deviceClosed(handle, reason, problem);
        });
    }

private:
    void send(const QJsonObject& event) const
    {
        m_shared->post([handle = m_handle, json = compactJson(QJsonDocument(event))](HardwareAccess& self) {
            self.deviceData(handle, json);
        });
    }

    std::shared_ptr<Shared> m_shared;
    QString m_handle;
};

HardwareAccess::HardwareAccess(std::unique_ptr<hardware::Backend> backend, std::filesystem::path grantsFile,
                               QObject* parent)
    : QObject(parent)
    , m_shared(std::make_shared<Shared>())
    , m_backend(std::move(backend))
    , m_grantsFile(std::move(grantsFile))
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
    m_shared->owner = this;
    {
        const std::scoped_lock lock(s_nativeMutex);
        if (s_native.expired()) {
            s_native = m_shared;
        }
    }
    m_replies = {
        .answer = [](std::uint64_t logic, quint64 request,
                     const QString& json) { static_cast<void>(LogicServices::instance().answer(logic, request, json)); },
        .refuse = [](std::uint64_t logic, quint64 request,
                     const QString& message) { static_cast<void>(LogicServices::instance().refuse(logic, request, message)); },
        .tell = [](std::uint64_t logic, const QString& kind, const QString& id,
                   const QString& json) { static_cast<void>(LogicServices::instance().tell(logic, kind, id, json)); },
    };
    loadGrants();
    if (m_backend) {
        m_backend->watch([shared = m_shared] {
            shared->post([](HardwareAccess& self) { self.refreshDevices(); });
        });
        refreshDevices();
    }
}

HardwareAccess::~HardwareAccess()
{
    {
        const std::scoped_lock lock(s_nativeMutex);
        if (s_native.lock() == m_shared) {
            s_native.reset();
        }
    }
    m_native.clear();
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->owner = nullptr;
    }
    if (m_offered) {
        for (const std::string_view name : kCalls) {
            LogicServices::instance().withdraw(name);
        }
        for (const std::string_view name : kRequests) {
            LogicServices::instance().withdraw(name);
        }
    }
    if (m_backend && m_scanning) {
        m_backend->scan(false);
    }
    m_handles.clear();
    m_openings.clear();
    m_backend.reset();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

HardwareAccess* HardwareAccess::create(QQmlEngine*, QJSEngine*)
{
    if (s_instance == nullptr) {
        auto* made = new HardwareAccess(nullptr, {});
        QJSEngine::setObjectOwnership(made, QJSEngine::JavaScriptOwnership);
        return made;
    }
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void HardwareAccess::offerServices()
{
    LogicServices& services = LogicServices::instance();
    for (const std::string_view name : kCalls) {
        services.offerCall(std::string(name), [shared = m_shared, name](const LogicCaller& caller, std::string_view json) {
            return callWith(shared, caller, name, json);
        });
    }
    for (const std::string_view name : kRequests) {
        services.offerRequest(
            std::string(name),
            [this, name](const LogicCaller& caller, quint64 id, const QString& json) { request(caller, name, id, json); },
            [this](const LogicCaller& caller, quint64 id) { drop(caller, id); });
    }
    connect(&services, &LogicServices::logicEnded, this, [this](quint64 logic) { endLogic(logic); });
    m_offered = true;
}

void HardwareAccess::setReplies(HardwareReplies replies)
{
    m_replies = std::move(replies);
}

void HardwareAccess::setPermitted(const QStringList& extensions)
{
    const std::set<QString> next(extensions.begin(), extensions.end());
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->permitted = next;
    }
    const auto revoked = [&next](const QString& extension) {
        return !extension.isEmpty() && extension != fromView(kOwnLogic) && !next.contains(extension);
    };
    std::vector<QString> closing;
    for (const auto& [handle, open] : m_handles) {
        if (revoked(open.owner)) {
            closing.push_back(handle);
        }
    }
    for (const QString& handle : closing) {
        closeHandle(handle, QStringLiteral("forgotten"), noPermission(), true);
    }
    for (auto it = m_openings.begin(); it != m_openings.end();) {
        if (revoked(it->second.owner)) {
            refuse(it->second.logic, it->second.request, noPermission());
            it = m_openings.erase(it);
        } else {
            ++it;
        }
    }
    const bool frontRevoked = !m_picks.empty() && revoked(m_picks.front().caller.extension);
    for (auto it = m_picks.begin(); it != m_picks.end();) {
        if (revoked(it->caller.extension)) {
            refuse(it->caller.logic, it->request, noPermission());
            it = m_picks.erase(it);
        } else {
            ++it;
        }
    }
    if (frontRevoked) {
        showNextPick();
    }
}

void HardwareAccess::setExtensionNames(std::map<QString, QString> names)
{
    m_extensionNames = std::move(names);
    if (!m_picks.empty()) {
        emit pickerChanged();
    }
}

void HardwareAccess::setFeed(Feed feed, Front front)
{
    m_feed = std::move(feed);
    m_front = std::move(front);
}

void HardwareAccess::follow(ExtensionHost& extensions)
{
    const auto update = [this, &extensions] {
        QStringList permitted;
        for (const ExtensionHost::Extension& extension : extensions.extensions()) {
            if (!extension.active || !extension.manifest) {
                continue;
            }
            const ext::Manifest& manifest = *extension.manifest;
            if (std::ranges::any_of(manifest.permissions,
                                    [](const ext::Permission& permission) { return permission.name == kPermission; })) {
                permitted.append(QString::fromStdString(manifest.id));
            }
        }
        std::map<QString, QString> names;
        for (const QVariant& entry : extensions.entries()) {
            const QVariantMap fields = entry.toMap();
            names.insert_or_assign(fields.value(QStringLiteral("id")).toString(),
                                   fields.value(QStringLiteral("name")).toString());
        }
        setExtensionNames(std::move(names));
        setPermitted(permitted);
    };
    update();
    connect(&extensions, &ExtensionHost::extensionsChanged, this, update);
}

script::ServiceAnswer HardwareAccess::call(const LogicCaller& caller, std::string_view service, std::string_view json)
{
    return callWith(m_shared, caller, service, json);
}

script::ServiceAnswer HardwareAccess::callWith(const std::shared_ptr<Shared>& shared, const LogicCaller& caller,
                                               std::string_view service, std::string_view json)
{
    const auto refused = [](const QString& message) {
        const std::string full = message.toStdString();
        const std::size_t space = full.find(' ');
        return script::ServiceAnswer{"null", full.substr(0, space), space == std::string::npos ? std::string()
                                                                                                : full.substr(space + 1)};
    };
    if (!shared->allowed(caller.extension)) {
        return refused(noPermission());
    }
    const QJsonObject body = objectOf(QByteArray(json.data(), static_cast<qsizetype>(json.size())));
    const QString owner = ownerOf(caller);
    if (service == "hardware.list") {
        QJsonArray list;
        {
            const std::scoped_lock lock(shared->mutex);
            if (const auto found = shared->grants.find(owner); found != shared->grants.end()) {
                for (const HardwareGrant& grant : found->second) {
                    QJsonObject entry = grantJson(grant);
                    entry.insert(QStringLiteral("present"),
                                 std::ranges::any_of(shared->present, [&grant](const hardware::DeviceInfo& device) {
                                     return device.id == grant.id;
                                 }));
                    list.append(entry);
                }
            }
        }
        return {compactJson(QJsonDocument(list)).toStdString(), {}, {}};
    }
    if (service == "hardware.input") {
        const std::string source = body.value(u"source").toString().toStdString();
        const std::string key = body.value(u"key").toString().toStdString();
        if (!input::validInputSourceName(source) || !input::validInputKeyName(key)) {
            return refused(invalidWith(QStringLiteral("an input source is named with 1 to 32 lowercase letters, digits "
                                                  "or -, and its keys with 1 to 32 letters, digits, _ or -")));
        }
        const QString action = body.value(u"action").toString();
        input::DeviceAction act = input::DeviceAction::Down;
        input::Vec2 value;
        if (action == u"release") {
            act = input::DeviceAction::Up;
        } else if (action == u"axis") {
            act = input::DeviceAction::Axis;
            const QJsonValue x = body.value(u"x");
            const QJsonValue y = body.value(u"y");
            value.x = x.isDouble() ? x.toDouble() : std::nan("");
            value.y = y.isUndefined() ? 0.0 : y.isDouble() ? y.toDouble() : std::nan("");
            if (!(value.x >= -1.0 && value.x <= 1.0) || !(value.y >= -1.0 && value.y <= 1.0)) {
                return refused(invalidWith(QStringLiteral("an axis value is a number from -1 to 1")));
            }
        } else if (action != u"press") {
            return refused(invalidWith(QStringLiteral("the action must be press, release or axis")));
        }
        const QString name =
            QString::fromStdString(input::extensionInputName(owner.toStdString(), source, key));
        shared->post([caller, name, act, value](HardwareAccess& self) { self.deliverInput(caller, name, act, value); });
        return {"null", {}, {}};
    }
    if (service == "hardware.release") {
        const std::string resource = body.value(u"resource").toString().toStdString();
        if (!exthost::validResource(resource)) {
            return refused(badResource());
        }
        shared->cancelWaitingClaim(caller.logic, resource);
        if (HardwareClaims* claims = HardwareClaims::instance()) {
            claims->release(resource, logicHolder(caller.logic));
        }
        return {"null", {}, {}};
    }
    return {"null", "GS-API-070", "no part of GameScrcpy answers the service " + std::string(service) + " at once"};
}

void HardwareAccess::request(const LogicCaller& caller, std::string_view service, quint64 id, const QString& json)
{
    if (!m_shared->allowed(caller.extension)) {
        refuse(caller.logic, id, noPermission());
        return;
    }
    if (service == "hardware.request") {
        pickDevices(caller, id, json);
    } else if (service == "hardware.open") {
        openDevice(caller, id, json);
    } else if (service == "hardware.write") {
        writeDevice(caller, id, json);
    } else if (service == "hardware.control") {
        controlDevice(caller, id, json);
    } else if (service == "hardware.close") {
        closeDevice(caller, id, json);
    } else if (service == "hardware.forget") {
        forgetDevice(caller, id, json);
    } else if (service == "hardware.claim") {
        claimHardware(caller, id, json);
    } else {
        refuse(caller.logic, id, QStringLiteral("GS-API-070 no part of GameScrcpy offers the service %1").arg(fromView(service)));
    }
}

void HardwareAccess::drop(const LogicCaller& caller, quint64 id)
{
    for (auto it = m_picks.begin(); it != m_picks.end(); ++it) {
        if (it->caller.logic == caller.logic && it->request == id) {
            const bool front = it == m_picks.begin();
            m_picks.erase(it);
            if (front) {
                showNextPick();
            }
            return;
        }
    }
    if (std::erase_if(m_openings, [&caller, id](const auto& entry) {
            return entry.second.logic == caller.logic && entry.second.request == id;
        }) > 0) {
        return;
    }
    for (auto& [handle, open] : m_handles) {
        if (open.logic == caller.logic) {
            open.waiting.erase(id);
        }
    }
}

void HardwareAccess::endLogic(std::uint64_t logic)
{
    m_endedLogic.insert(logic);
    m_shared->forgetClaimWaits(logic);
    releaseInputs(logic);
    const bool front = !m_picks.empty() && m_picks.front().caller.logic == logic;
    std::erase_if(m_picks, [logic](const Pick& pick) { return pick.caller.logic == logic; });
    std::erase_if(m_openings, [logic](const auto& entry) { return entry.second.logic == logic; });
    std::vector<QString> closing;
    for (const auto& [handle, open] : m_handles) {
        if (open.logic == logic) {
            closing.push_back(handle);
        }
    }
    for (const QString& handle : closing) {
        closeHandle(handle, QStringLiteral("ended"), {}, false);
    }
    if (HardwareClaims* claims = HardwareClaims::instance()) {
        claims->releaseAll(holderOf(logic));
    }
    if (front) {
        showNextPick();
    }
}

QVariantMap HardwareAccess::picker() const
{
    if (m_picks.empty()) {
        return {};
    }
    const Pick& pick = m_picks.front();
    const std::vector<HardwareGrant> granted = grantsOf(ownerOf(pick.caller));
    QVariantList devices;
    for (const hardware::DeviceInfo& device : present()) {
        if (!hardware::matches(device, pick.kind, pick.filters)) {
            continue;
        }
        const bool allowed = std::ranges::any_of(granted, [&device](const HardwareGrant& grant) {
            return grant.id == device.id;
        });
        devices.append(QVariantMap{{QStringLiteral("id"), QString::fromStdString(device.id)},
                                   {QStringLiteral("name"), displayName(device)},
                                   {QStringLiteral("kind"), fromView(hardware::kindName(device.kind))},
                                   {QStringLiteral("detail"), QString::fromStdString(hardware::detailOf(device))},
                                   {QStringLiteral("granted"), allowed}});
    }
    return {{QStringLiteral("open"), true},
            {QStringLiteral("ticket"), QStringLiteral("%1:%2").arg(pick.caller.logic).arg(pick.request)},
            {QStringLiteral("requester"), requesterOf(pick.caller)},
            {QStringLiteral("own"), pick.caller.extension.isEmpty()},
            {QStringLiteral("kind"), fromView(hardware::kindName(pick.kind))},
            {QStringLiteral("devices"), devices},
            {QStringLiteral("scanning"), m_scanning}};
}

QVariantMap HardwareAccess::grantMap() const
{
    const std::vector<hardware::DeviceInfo> devices = present();
    std::map<QString, std::vector<HardwareGrant>> grants;
    {
        const std::scoped_lock lock(m_shared->mutex);
        grants = m_shared->grants;
    }
    QVariantMap owners;
    for (const auto& [owner, list] : grants) {
        QVariantList entries;
        for (const HardwareGrant& grant : list) {
            const auto found = std::ranges::find(devices, grant.id, &hardware::DeviceInfo::id);
            entries.append(QVariantMap{
                {QStringLiteral("id"), QString::fromStdString(grant.id)},
                {QStringLiteral("kind"), fromView(hardware::kindName(grant.kind))},
                {QStringLiteral("name"), QString::fromStdString(grant.name)},
                {QStringLiteral("present"), found != devices.end()},
                {QStringLiteral("detail"),
                 found != devices.end() ? QString::fromStdString(hardware::detailOf(*found)) : QString()}});
        }
        owners.insert(owner, entries);
    }
    return owners;
}

QVariantList HardwareAccess::deviceList() const
{
    QVariantList list;
    for (const hardware::DeviceInfo& device : present()) {
        list.append(QVariantMap{{QStringLiteral("id"), QString::fromStdString(device.id)},
                                {QStringLiteral("kind"), fromView(hardware::kindName(device.kind))},
                                {QStringLiteral("name"), displayName(device)},
                                {QStringLiteral("detail"), QString::fromStdString(hardware::detailOf(device))}});
    }
    return list;
}

std::optional<hardware::DeviceInfo> HardwareAccess::device(const QString& id) const
{
    const std::string wanted = id.toStdString();
    const std::scoped_lock lock(m_shared->mutex);
    const auto found = std::ranges::find(m_shared->present, wanted, &hardware::DeviceInfo::id);
    if (found == m_shared->present.end()) {
        return std::nullopt;
    }
    return *found;
}

std::vector<HardwareGrant> HardwareAccess::grantsOf(const QString& owner) const
{
    const std::scoped_lock lock(m_shared->mutex);
    const auto found = m_shared->grants.find(owner);
    return found == m_shared->grants.end() ? std::vector<HardwareGrant>{} : found->second;
}

bool HardwareAccess::allow(const QStringList& ids)
{
    if (m_picks.empty()) {
        return false;
    }
    const Pick pick = std::move(m_picks.front());
    m_picks.pop_front();
    const QString owner = ownerOf(pick.caller);
    QJsonArray chosen;
    bool changed = false;
    {
        const std::scoped_lock lock(m_shared->mutex);
        std::vector<HardwareGrant>& list = m_shared->grants[owner];
        for (const hardware::DeviceInfo& device : m_shared->present) {
            if (!hardware::matches(device, pick.kind, pick.filters) || !ids.contains(QString::fromStdString(device.id))) {
                continue;
            }
            HardwareGrant grant{device.id, device.kind, displayName(device).toStdString()};
            if (std::ranges::none_of(list, [&device](const HardwareGrant& each) { return each.id == device.id; })) {
                if (list.size() >= kMostGrants) {
                    continue;
                }
                list.push_back(grant);
                changed = true;
            }
            chosen.append(grantJson(grant));
        }
        if (list.empty()) {
            m_shared->grants.erase(owner);
        }
    }
    if (changed) {
        saveGrants();
        emit grantsChanged();
    }
    answer(pick.caller.logic, pick.request, compactJson(QJsonDocument(chosen)));
    showNextPick();
    return true;
}

void HardwareAccess::cancelPicker()
{
    if (m_picks.empty()) {
        return;
    }
    const Pick pick = std::move(m_picks.front());
    m_picks.pop_front();
    answer(pick.caller.logic, pick.request, QStringLiteral("[]"));
    showNextPick();
}

bool HardwareAccess::forget(const QString& owner, const QString& id)
{
    const std::string wanted = id.toStdString();
    bool removed = false;
    {
        const std::scoped_lock lock(m_shared->mutex);
        if (const auto found = m_shared->grants.find(owner); found != m_shared->grants.end()) {
            removed = std::erase_if(found->second, [&wanted](const HardwareGrant& grant) { return grant.id == wanted; }) > 0;
            if (found->second.empty()) {
                m_shared->grants.erase(found);
            }
        }
    }
    std::vector<QString> closing;
    for (const auto& [handle, open] : m_handles) {
        if (open.owner == owner && open.device.id == wanted) {
            closing.push_back(handle);
        }
    }
    for (const QString& handle : closing) {
        closeHandle(handle, QStringLiteral("forgotten"), notGranted(), true);
    }
    for (auto it = m_openings.begin(); it != m_openings.end();) {
        if (it->second.owner == owner && it->second.device.id == wanted) {
            refuse(it->second.logic, it->second.request, notGranted());
            it = m_openings.erase(it);
        } else {
            ++it;
        }
    }
    if (removed) {
        saveGrants();
        emit grantsChanged();
        if (!m_picks.empty()) {
            emit pickerChanged();
        }
    }
    return removed;
}

QString HardwareAccess::deviceTitle(const QString& id) const
{
    if (const std::optional<hardware::DeviceInfo> found = device(id)) {
        const std::string detail = hardware::detailOf(*found);
        return detail.empty() || found->name.empty() ? displayName(*found)
                                                     : displayName(*found) + QStringLiteral(" (") +
                                                           QString::fromStdString(detail) + u')';
    }
    const std::string wanted = id.toStdString();
    const std::scoped_lock lock(m_shared->mutex);
    for (const auto& [owner, list] : m_shared->grants) {
        const auto found = std::ranges::find(list, wanted, &HardwareGrant::id);
        if (found != list.end()) {
            return QString::fromStdString(found->name);
        }
    }
    return id;
}

void HardwareAccess::refreshDevices()
{
    std::vector<hardware::DeviceInfo> devices = m_backend ? m_backend->devices() : std::vector<hardware::DeviceInfo>{};
    std::vector<QString> gone;
    for (const auto& [handle, open] : m_handles) {
        if (open.device.kind != hardware::Kind::Ble &&
            std::ranges::find(devices, open.device.id, &hardware::DeviceInfo::id) == devices.end()) {
            gone.push_back(handle);
        }
    }
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->present = std::move(devices);
    }
    for (const QString& handle : gone) {
        const auto found = m_handles.find(handle);
        if (found != m_handles.end()) {
            closeHandle(handle, QStringLiteral("removed"), notConnected(displayName(found->second.device)), true);
        }
    }
    emit devicesChanged();
    emit grantsChanged();
    if (!m_picks.empty()) {
        emit pickerChanged();
    }
}

void HardwareAccess::opened(const QString& handle, std::shared_ptr<hardware::OpenResult> result)
{
    auto node = m_openings.extract(handle);
    if (node.empty()) {
        return;
    }
    const Opening& opening = node.mapped();
    if (!result->device) {
        refuse(opening.logic, opening.request,
               result->missing ? notConnected(displayName(opening.device)) : failedWith(result->problem));
        return;
    }
    const std::vector<HardwareGrant> grants = grantsOf(opening.owner);
    const bool stillGranted = std::ranges::any_of(grants, [&opening](const HardwareGrant& grant) {
        return grant.id == opening.device.id;
    });
    if (!stillGranted || !m_shared->allowed(opening.owner == fromView(kOwnLogic) ? QString() : opening.owner)) {
        refuse(opening.logic, opening.request, stillGranted ? noPermission() : notGranted());
        return;
    }
    std::string claimed;
    if (HardwareClaims* claims = HardwareClaims::instance()) {
        const std::string resource = resourceOf(opening.device);
        const std::string holder = holderOf(opening.logic);
        if (!resource.empty() && !claims->holds(resource, holder)) {
            if (claims->claim(resource, claimerOf(opening.logic, opening.owner), false) !=
                exthost::ClaimResult::Claimed) {
                refuse(opening.logic, opening.request,
                       failedWith(displayName(opening.device).toStdString() + " is in use by " +
                                  claims->holderName(resource).toStdString()));
                return;
            }
            claimed = resource;
        }
    }
    m_handles.insert_or_assign(handle, Handle{opening.logic, opening.owner, opening.device, std::move(result->device),
                                              {}, std::move(claimed)});
    QJsonObject made = grantJson({opening.device.id, opening.device.kind, displayName(opening.device).toStdString()});
    made.insert(QStringLiteral("handle"), handle);
    answer(opening.logic, opening.request, compactJson(QJsonDocument(made)));
}

void HardwareAccess::completed(const QString& handle, quint64 request, const std::string& problem, const QString& json)
{
    const auto found = m_handles.find(handle);
    if (found == m_handles.end() || found->second.waiting.erase(request) == 0) {
        return;
    }
    if (problem.empty()) {
        answer(found->second.logic, request, json);
    } else {
        refuse(found->second.logic, request, failedWith(problem));
    }
}

void HardwareAccess::deviceData(const QString& handle, const QString& json)
{
    if (const auto found = m_handles.find(handle); found != m_handles.end()) {
        tell(found->second.logic, handle, json);
    }
}

void HardwareAccess::deviceClosed(const QString& handle, hardware::Closed reason, const std::string& problem)
{
    const auto found = m_handles.find(handle);
    if (found == m_handles.end()) {
        return;
    }
    const bool removed = reason == hardware::Closed::Removed;
    closeHandle(handle, removed ? QStringLiteral("removed") : QStringLiteral("failed"),
                removed ? notConnected(displayName(found->second.device)) : failedWith(problem), true);
}

void HardwareAccess::deliverInput(const LogicCaller& caller, const QString& name, input::DeviceAction action,
                                  input::Vec2 value)
{
    if (m_endedLogic.contains(caller.logic) && action != input::DeviceAction::Up) {
        return;
    }
    if (action == input::DeviceAction::Down ||
        (action == input::DeviceAction::Axis && (std::abs(value.x) >= 0.5 || std::abs(value.y) >= 0.5))) {
        emit inputSeen(name);
    }
    const QString serial = !caller.device.isEmpty() ? caller.device : m_front ? m_front() : QString();
    if (serial.isEmpty() || !m_feed) {
        return;
    }
    const std::optional<input::InputSource> source = input::knownSource(name.toStdString());
    if (!source) {
        return;
    }
    std::map<QString, HeldInput>& held = m_held[caller.logic];
    if (action == input::DeviceAction::Up || (action == input::DeviceAction::Axis && value == input::Vec2{})) {
        held.erase(name);
    } else {
        held.insert_or_assign(name, HeldInput{serial, *source, action == input::DeviceAction::Axis});
    }
    if (held.empty()) {
        m_held.erase(caller.logic);
    }
    m_feed(serial, input::DeviceEvent{*source, action, value});
}

void HardwareAccess::releaseInputs(std::uint64_t logic)
{
    const auto found = m_held.find(logic);
    if (found == m_held.end()) {
        return;
    }
    const std::map<QString, HeldInput> held = std::move(found->second);
    m_held.erase(found);
    if (!m_feed) {
        return;
    }
    for (const auto& entry : held) {
        const HeldInput& pressed = entry.second;
        m_feed(pressed.serial,
               input::DeviceEvent{pressed.source, pressed.axis ? input::DeviceAction::Axis : input::DeviceAction::Up, {}});
    }
}

void HardwareAccess::provideInput(const QString& extension, const QString& name, bool button, double x, double y)
{
    input::DeviceAction action = input::DeviceAction::Axis;
    if (button) {
        action = x != 0.0 ? input::DeviceAction::Down : input::DeviceAction::Up;
    }
    const input::Vec2 value = button ? input::Vec2{} : input::Vec2{x, y};
    if (action == input::DeviceAction::Down ||
        (action == input::DeviceAction::Axis && (std::abs(value.x) >= 0.5 || std::abs(value.y) >= 0.5))) {
        emit inputSeen(name);
    }
    std::map<QString, HeldInput>& held = m_provided[extension];
    const auto found = held.find(name);
    const QString serial = found != held.end() ? found->second.serial : m_front ? m_front() : QString();
    const std::optional<input::InputSource> source = input::knownSource(name.toStdString());
    const bool fed = !serial.isEmpty() && m_feed && source.has_value();
    if (action == input::DeviceAction::Up || (action == input::DeviceAction::Axis && value == input::Vec2{})) {
        held.erase(name);
    } else if (fed) {
        held.insert_or_assign(name, HeldInput{serial, *source, action == input::DeviceAction::Axis});
    }
    if (held.empty()) {
        m_provided.erase(extension);
    }
    if (fed) {
        m_feed(serial, input::DeviceEvent{*source, action, value});
    }
}

void HardwareAccess::endProvidedInput(const QString& extension)
{
    const auto found = m_provided.find(extension);
    if (found == m_provided.end()) {
        return;
    }
    const std::map<QString, HeldInput> held = std::move(found->second);
    m_provided.erase(found);
    if (!m_feed) {
        return;
    }
    for (const auto& entry : held) {
        const HeldInput& pressed = entry.second;
        m_feed(pressed.serial,
               input::DeviceEvent{pressed.source, pressed.axis ? input::DeviceAction::Axis : input::DeviceAction::Up, {}});
    }
}

void HardwareAccess::closeHandle(const QString& handle, const QString& reason, const QString& message, bool tellLogic)
{
    auto node = m_handles.extract(handle);
    if (node.empty()) {
        return;
    }
    Handle& open = node.mapped();
    for (const quint64 request : open.waiting) {
        refuse(open.logic, request, QStringLiteral("GS-API-084 the device was closed"));
    }
    if (tellLogic) {
        QJsonObject event{{QStringLiteral("type"), QStringLiteral("closed")}, {QStringLiteral("reason"), reason}};
        if (!message.isEmpty()) {
            event.insert(QStringLiteral("message"), message);
        }
        tell(open.logic, handle, compactJson(QJsonDocument(event)));
    }
    open.link.reset();
    if (!open.claimed.empty()) {
        if (HardwareClaims* claims = HardwareClaims::instance()) {
            claims->release(open.claimed, holderOf(open.logic));
        }
    }
}

void HardwareAccess::showNextPick()
{
    updateScan();
    emit pickerChanged();
    if (!m_picks.empty()) {
        emit attentionWanted();
    }
}

void HardwareAccess::updateScan()
{
    const bool wanted = !m_picks.empty() && m_picks.front().kind == hardware::Kind::Ble;
    if (wanted == m_scanning || !m_backend) {
        return;
    }
    m_scanning = wanted;
    m_backend->scan(wanted);
}

void HardwareAccess::loadGrants()
{
    if (m_grantsFile.empty()) {
        return;
    }
    std::string contents;
    const std::error_code error = platform::readFile(m_grantsFile, kMostGrantsBytes, contents);
    if (error) {
        if (!platform::fileMissing(error)) {
            log::warning("hardware", "the allowed devices could not be read (error {})", error.value());
        }
        return;
    }
    QJsonParseError problem;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(contents), &problem);
    const QJsonObject root = document.object();
    if (problem.error != QJsonParseError::NoError || !document.isObject() ||
        root.value(u"format").toString() != fromView(kGrantsFormat) || root.value(u"version").toInt() != kGrantsVersion) {
        log::warning("hardware", "the allowed devices file is not one GameScrcpy can read; starting with none");
        return;
    }
    std::map<QString, std::vector<HardwareGrant>> loaded;
    const QJsonObject owners = root.value(u"owners").toObject();
    for (auto it = owners.begin(); it != owners.end(); ++it) {
        if (!validOwner(it.key()) || !it.value().isArray()) {
            continue;
        }
        std::vector<HardwareGrant> list;
        for (const QJsonValue& entry : it.value().toArray()) {
            const QJsonObject fields = entry.toObject();
            const std::string id = fields.value(u"id").toString().toStdString();
            const std::optional<hardware::Kind> kind =
                hardware::kindNamed(fields.value(u"kind").toString().toStdString());
            const std::string name = fields.value(u"name").toString().toStdString();
            if (id.empty() || id.size() > kMostIdLength || !kind || name.size() > kMostNameLength ||
                list.size() >= kMostGrants ||
                std::ranges::find(list, id, &HardwareGrant::id) != list.end()) {
                continue;
            }
            list.push_back({id, *kind, name});
        }
        if (!list.empty()) {
            loaded.insert_or_assign(it.key(), std::move(list));
        }
    }
    const std::scoped_lock lock(m_shared->mutex);
    m_shared->grants = std::move(loaded);
}

void HardwareAccess::saveGrants() const
{
    if (m_grantsFile.empty()) {
        return;
    }
    QJsonObject owners;
    {
        const std::scoped_lock lock(m_shared->mutex);
        for (const auto& [owner, list] : m_shared->grants) {
            QJsonArray entries;
            for (const HardwareGrant& grant : list) {
                entries.append(grantJson(grant));
            }
            owners.insert(owner, entries);
        }
    }
    const QJsonObject root{{QStringLiteral("format"), fromView(kGrantsFormat)},
                           {QStringLiteral("version"), kGrantsVersion},
                           {QStringLiteral("owners"), owners}};
    std::error_code error;
    std::filesystem::create_directories(m_grantsFile.parent_path(), error);
    error = platform::writeFileAtomically(m_grantsFile, QJsonDocument(root).toJson(QJsonDocument::Indented).toStdString());
    if (error) {
        log::warning("hardware", "the allowed devices could not be saved ({}error {})",
                     platform::fileBusy(error) ? "another program is using the file, " : "", error.value());
    }
}

void HardwareAccess::answer(std::uint64_t logic, quint64 request, const QString& json) const
{
    if (logic >= kNativeLogics) {
        answerNative(logic, request, {exthost::ServiceStatus::Ok, json.toStdString()});
        return;
    }
    if (m_replies.answer) {
        m_replies.answer(logic, request, json);
    }
}

void HardwareAccess::refuse(std::uint64_t logic, quint64 request, const QString& message) const
{
    if (logic >= kNativeLogics) {
        const std::string text = message.toStdString();
        answerNative(logic, request, {statusOfRefusal(text), text});
        return;
    }
    if (m_replies.refuse) {
        m_replies.refuse(logic, request, message);
    }
}

void HardwareAccess::tell(std::uint64_t logic, const QString& handle, const QString& json) const
{
    if (logic >= kNativeLogics) {
        tellNative(logic, handle, json);
        return;
    }
    if (m_replies.tell) {
        m_replies.tell(logic, fromView(kEventKind), handle, json);
    }
}

void HardwareAccess::pickDevices(const LogicCaller& caller, quint64 id, const QString& json)
{
    const QJsonObject body = objectOf(json.toUtf8());
    const std::optional<hardware::Kind> kind = hardware::kindNamed(body.value(u"kind").toString().toStdString());
    if (!kind) {
        refuse(caller.logic, id, invalidWith(QStringLiteral("the kind must be \"serial\", \"hid\" or \"ble\"")));
        return;
    }
    std::vector<hardware::Filter> filters;
    if (const QJsonValue given = body.value(u"filters"); !given.isUndefined() && !given.isNull()) {
        if (!given.isArray() || static_cast<std::size_t>(given.toArray().size()) > kMostFilters) {
            refuse(caller.logic, id, invalidWith(QStringLiteral("filters must be a list of at most 16 objects")));
            return;
        }
        for (const QJsonValue& each : given.toArray()) {
            std::optional<hardware::Filter> filter = filterOf(each);
            if (!filter) {
                refuse(caller.logic, id,
                       invalidWith(QStringLiteral("a filter takes vendorId, productId, usagePage and usage from 0 to "
                                              "65535, services as UUIDs and a namePrefix of at most 64 characters")));
                return;
            }
            filters.push_back(std::move(*filter));
        }
    }
    if (m_picks.size() >= kMostWaitingPicks) {
        refuse(caller.logic, id, invalidWith(QStringLiteral("too many device requests are waiting")));
        return;
    }
    m_picks.push_back(Pick{caller, id, *kind, std::move(filters)});
    if (m_picks.size() == 1) {
        showNextPick();
    }
}

void HardwareAccess::openDevice(const LogicCaller& caller, quint64 id, const QString& json)
{
    const QJsonObject body = objectOf(json.toUtf8());
    const std::string wanted = body.value(u"id").toString().toStdString();
    const QString owner = ownerOf(caller);
    std::optional<HardwareGrant> grant;
    for (const HardwareGrant& each : grantsOf(owner)) {
        if (each.id == wanted) {
            grant = each;
        }
    }
    if (!grant) {
        refuse(caller.logic, id, notGranted());
        return;
    }
    hardware::SerialOptions serial;
    if (grant->kind == hardware::Kind::Serial && !serialOptionsOf(body.value(u"options"), serial)) {
        refuse(caller.logic, id,
               invalidWith(QStringLiteral("the serial options are not possible: baudRate 50 to 4000000, dataBits 5 to 8, "
                                      "parity none, even, odd, mark or space, stopBits 1, 1.5 or 2, flowControl "
                                      "none, hardware or software, dtr and rts true or false")));
        return;
    }
    hardware::DeviceInfo info;
    if (const std::optional<hardware::DeviceInfo> found = device(QString::fromStdString(grant->id))) {
        info = *found;
    } else if (grant->kind == hardware::Kind::Ble) {
        info.id = grant->id;
        info.kind = grant->kind;
        info.name = grant->name;
        if (const std::optional<std::uint64_t> address = hardware::bleAddressOf(grant->id)) {
            info.path = hardware::addressText(*address);
        }
    } else {
        refuse(caller.logic, id, notConnected(QString::fromStdString(grant->name)));
        return;
    }
    const auto busy = [&wanted](const auto& entry) { return entry.second.device.id == wanted; };
    if (std::ranges::any_of(m_handles, busy) || std::ranges::any_of(m_openings, busy)) {
        refuse(caller.logic, id, failedWith(grant->name + " is already open"));
        return;
    }
    if (const HardwareClaims* claims = HardwareClaims::instance()) {
        const std::string resource = resourceOf(info);
        const QString holder = resource.empty() ? QString() : claims->holderName(resource);
        if (!holder.isEmpty() && !claims->holds(resource, holderOf(caller.logic))) {
            refuse(caller.logic, id, failedWith(grant->name + " is in use by " + holder.toStdString()));
            return;
        }
    }
    const auto ofLogic = [&caller](const auto& entry) { return entry.second.logic == caller.logic; };
    if (static_cast<std::size_t>(std::ranges::count_if(m_handles, ofLogic) + std::ranges::count_if(m_openings, ofLogic)) >=
        kMostOpen) {
        refuse(caller.logic, id, invalidWith(QStringLiteral("a logic can have at most 16 devices open")));
        return;
    }
    if (!m_backend) {
        refuse(caller.logic, id, notConnected(QString::fromStdString(grant->name)));
        return;
    }
    const QString handle = QStringLiteral("d%1").arg(++m_lastHandle);
    m_openings.insert_or_assign(handle, Opening{caller.logic, id, owner, info});
    m_backend->open({info, serial, std::make_shared<Sink>(m_shared, handle)},
                    [shared = m_shared, handle](hardware::OpenResult result) {
                        auto held = std::make_shared<hardware::OpenResult>(std::move(result));
                        shared->post([handle, held](HardwareAccess& self) { self.opened(handle, held); });
                    });
}

void HardwareAccess::writeDevice(const LogicCaller& caller, quint64 id, const QString& json)
{
    const QJsonObject body = objectOf(json.toUtf8());
    const QString name = body.value(u"handle").toString();
    Handle* open = handleOf(caller.logic, name);
    if (open == nullptr) {
        refuse(caller.logic, id, notOpen());
        return;
    }
    std::optional<hardware::Bytes> data = bytesOf(body.value(u"data"));
    if (!data) {
        refuse(caller.logic, id, invalidWith(QStringLiteral("the data must be bytes")));
        return;
    }
    switch (open->device.kind) {
    case hardware::Kind::Serial:
        if (data->size() > hardware::kMostWriteBytes) {
            refuse(caller.logic, id, invalidWith(QStringLiteral("at most 65536 bytes can be written at once")));
            return;
        }
        open->waiting.insert(id);
        open->link->write(std::move(*data), completion(name, id));
        return;
    case hardware::Kind::Hid: {
        const std::optional<std::uint8_t> report = reportOf(body.value(u"report"));
        if (!report || data->size() > hardware::kMostReportBytes) {
            refuse(caller.logic, id, invalidWith(QStringLiteral("a report has an id from 0 to 255 and at most 4096 bytes")));
            return;
        }
        open->waiting.insert(id);
        open->link->sendReport(*report, std::move(*data), completion(name, id));
        return;
    }
    case hardware::Kind::Ble: {
        const std::optional<std::string> service = uuidOf(body.value(u"service"));
        const std::optional<std::string> characteristic = uuidOf(body.value(u"characteristic"));
        bool valid = true;
        const std::optional<bool> response = flagOf(body.value(u"response"), valid);
        if (!service || !characteristic || !valid || data->size() > hardware::kMostBleValueBytes) {
            refuse(caller.logic, id,
                   invalidWith(QStringLiteral("a Bluetooth write names its service and characteristic by UUID and "
                                          "carries at most 512 bytes")));
            return;
        }
        open->waiting.insert(id);
        open->link->writeValue(*service, *characteristic, std::move(*data), response.value_or(true),
                               completion(name, id));
        return;
    }
    }
}

void HardwareAccess::controlDevice(const LogicCaller& caller, quint64 id, const QString& json)
{
    const QJsonObject body = objectOf(json.toUtf8());
    const QString name = body.value(u"handle").toString();
    Handle* open = handleOf(caller.logic, name);
    if (open == nullptr) {
        refuse(caller.logic, id, notOpen());
        return;
    }
    const QString op = body.value(u"op").toString();
    const hardware::Kind kind = open->device.kind;
    const auto unsupported = [this, &caller, id] {
        refuse(caller.logic, id, invalidWith(QStringLiteral("this kind of device does not do that")));
    };
    const auto withBytes = [shared = m_shared, name, id](std::string problem, hardware::Bytes bytes) {
        const QString reply = compactJson(QJsonDocument(QJsonObject{{QStringLiteral("data"), base64(bytes)}}));
        shared->post([name, id, problem = std::move(problem), reply](HardwareAccess& self) {
            self.completed(name, id, problem, reply);
        });
    };
    if (op == u"signals") {
        if (kind != hardware::Kind::Serial) {
            unsupported();
            return;
        }
        bool valid = true;
        const std::optional<bool> dtr = flagOf(body.value(u"dtr"), valid);
        const std::optional<bool> rts = flagOf(body.value(u"rts"), valid);
        if (!valid) {
            refuse(caller.logic, id, invalidWith(QStringLiteral("dtr and rts are true or false")));
            return;
        }
        open->waiting.insert(id);
        open->link->setSignals(dtr, rts, completion(name, id));
        return;
    }
    if (op == u"sendFeature" || op == u"getFeature") {
        if (kind != hardware::Kind::Hid) {
            unsupported();
            return;
        }
        const std::optional<std::uint8_t> report = reportOf(body.value(u"report"));
        if (!report) {
            refuse(caller.logic, id, invalidWith(QStringLiteral("a report id is a whole number from 0 to 255")));
            return;
        }
        if (op == u"getFeature") {
            open->waiting.insert(id);
            open->link->getFeature(*report, withBytes);
            return;
        }
        std::optional<hardware::Bytes> data = bytesOf(body.value(u"data"));
        if (!data || data->size() > hardware::kMostReportBytes) {
            refuse(caller.logic, id, invalidWith(QStringLiteral("a report carries at most 4096 bytes")));
            return;
        }
        open->waiting.insert(id);
        open->link->sendFeature(*report, std::move(*data), completion(name, id));
        return;
    }
    if (kind != hardware::Kind::Ble || (op != u"services" && op != u"read" && op != u"subscribe")) {
        unsupported();
        return;
    }
    if (op == u"services") {
        open->waiting.insert(id);
        open->link->services([shared = m_shared, name, id](std::string problem, std::vector<hardware::BleService> services) {
            QJsonArray list;
            for (const hardware::BleService& service : services) {
                QJsonArray characteristics;
                for (const hardware::BleCharacteristic& characteristic : service.characteristics) {
                    QJsonArray properties;
                    for (const std::string& property : characteristic.properties) {
                        properties.append(QString::fromStdString(property));
                    }
                    characteristics.append(QJsonObject{{QStringLiteral("uuid"), QString::fromStdString(characteristic.uuid)},
                                                       {QStringLiteral("properties"), properties}});
                }
                list.append(QJsonObject{{QStringLiteral("uuid"), QString::fromStdString(service.uuid)},
                                        {QStringLiteral("characteristics"), characteristics}});
            }
            shared->post([name, id, problem = std::move(problem), reply = compactJson(QJsonDocument(list))](
                             HardwareAccess& self) { self.completed(name, id, problem, reply); });
        });
        return;
    }
    const std::optional<std::string> service = uuidOf(body.value(u"service"));
    const std::optional<std::string> characteristic = uuidOf(body.value(u"characteristic"));
    bool valid = true;
    const std::optional<bool> on = flagOf(body.value(u"on"), valid);
    if (!service || !characteristic || !valid) {
        refuse(caller.logic, id, invalidWith(QStringLiteral("a service and a characteristic are named by UUID")));
        return;
    }
    open->waiting.insert(id);
    if (op == u"read") {
        open->link->read(*service, *characteristic, withBytes);
        return;
    }
    open->link->subscribe(*service, *characteristic, on.value_or(true), completion(name, id));
}

void HardwareAccess::closeDevice(const LogicCaller& caller, quint64 id, const QString& json)
{
    const QString name = objectOf(json.toUtf8()).value(u"handle").toString();
    if (handleOf(caller.logic, name) != nullptr) {
        closeHandle(name, QStringLiteral("closed"), {}, false);
    }
    answer(caller.logic, id, QStringLiteral("null"));
}

void HardwareAccess::forgetDevice(const LogicCaller& caller, quint64 id, const QString& json)
{
    const QString wanted = objectOf(json.toUtf8()).value(u"id").toString();
    if (wanted.isEmpty()) {
        refuse(caller.logic, id, invalidWith(QStringLiteral("the device id must be text")));
        return;
    }
    static_cast<void>(forget(ownerOf(caller), wanted));
    answer(caller.logic, id, QStringLiteral("null"));
}

HardwareAccess::Handle* HardwareAccess::handleOf(std::uint64_t logic, const QString& handle)
{
    const auto found = m_handles.find(handle);
    return found != m_handles.end() && found->second.logic == logic ? &found->second : nullptr;
}

hardware::Completion HardwareAccess::completion(const QString& handle, quint64 request) const
{
    return [shared = m_shared, handle, request](std::string problem) {
        shared->post([handle, request, problem = std::move(problem)](HardwareAccess& self) {
            self.completed(handle, request, problem, QStringLiteral("null"));
        });
    };
}

std::vector<hardware::DeviceInfo> HardwareAccess::present() const
{
    const std::scoped_lock lock(m_shared->mutex);
    return m_shared->present;
}

QString HardwareAccess::requesterOf(const LogicCaller& caller) const
{
    if (caller.extension.isEmpty()) {
        return {};
    }
    const auto found = m_extensionNames.find(caller.extension);
    return found != m_extensionNames.end() && !found->second.isEmpty() ? found->second : caller.extension;
}

QString HardwareAccess::claimerName(const QString& owner) const
{
    if (owner.isEmpty() || owner == fromView(kOwnLogic)) {
        return tr("your logic");
    }
    const auto found = m_extensionNames.find(owner);
    return found != m_extensionNames.end() && !found->second.isEmpty() ? found->second : owner;
}

void HardwareAccess::claimHardware(const LogicCaller& caller, quint64 id, const QString& json)
{
    const QJsonObject body = objectOf(json.toUtf8());
    const std::string resource = body.value(u"resource").toString().toStdString();
    if (!exthost::validResource(resource)) {
        refuse(caller.logic, id, badResource());
        return;
    }
    HardwareClaims* claims = HardwareClaims::instance();
    if (claims == nullptr) {
        answer(caller.logic, id, QStringLiteral("false"));
        return;
    }
    ClaimHolder holder{logicHolder(caller.logic), claimerName(ownerOf(caller)), {}};
    if (body.value(u"yields").toBool()) {
        holder.wanted = [shared = m_shared, logic = caller.logic](const std::string& held, const QString& by) {
            shared->post([logic, held = QString::fromStdString(held), by](HardwareAccess& self) {
                self.tell(logic, QString(),
                          compactJson(QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("claimWanted")},
                                                                {QStringLiteral("resource"), held},
                                                                {QStringLiteral("by"), by}})));
            });
            return true;
        };
    }
    if (!body.value(u"wait").toBool()) {
        const bool claimed = claims->claim(resource, holder, false) == exthost::ClaimResult::Claimed;
        answer(caller.logic, id, claimed ? QStringLiteral("true") : QStringLiteral("false"));
        return;
    }
    m_shared->waitingForClaim(caller.logic, resource);
    claims->claimLater(resource, std::move(holder),
                       [shared = m_shared, logic = caller.logic, id, resource](exthost::ClaimResult result) {
                           const bool claimed = result == exthost::ClaimResult::Claimed;
                           shared->post([logic, id, resource, claimed](HardwareAccess& self) {
                               const bool cancelled = self.m_shared->claimAnswered(logic, resource);
                               if (claimed && (cancelled || self.m_endedLogic.contains(logic))) {
                                   if (HardwareClaims* held = HardwareClaims::instance()) {
                                       held->release(resource, logicHolder(logic));
                                   }
                                   return;
                               }
                               self.answer(logic, id, claimed ? QStringLiteral("true") : QStringLiteral("false"));
                           });
                       });
}

void HardwareAccess::callFromNative(const NativeHardware& caller, std::string service, std::string json,
                                    NativeAnswer answer)
{
    NativeAnswer reply = answeredOnce(std::move(answer));
    std::shared_ptr<Shared> shared;
    {
        const std::scoped_lock lock(s_nativeMutex);
        shared = s_native.lock();
    }
    if (shared == nullptr) {
        reply({exthost::ServiceStatus::Unsupported, "GS-API-070 GameScrcpy reaches no devices for extensions now"});
        return;
    }
    if (service == "hardware.list") {
        const script::ServiceAnswer listed =
            callWith(shared, LogicCaller{.logic = kNativeLogics + caller.key, .extension = caller.extension}, service,
                     json);
        reply(listed.code.empty()
                  ? exthost::ServiceResult{exthost::ServiceStatus::Ok, listed.json}
                  : exthost::ServiceResult{statusOfRefusal(listed.code), listed.code + " " + listed.problem});
        return;
    }
    shared->post([caller, service = std::move(service), json = std::move(json), reply](HardwareAccess& self) {
        self.nativeRequest(caller, service, json, reply);
    });
}

exthost::ServiceResult HardwareAccess::callFromNativeAndWait(const NativeHardware& caller, std::string_view request,
                                                             std::string_view json, std::stop_token stop)
{
    const QCoreApplication* application = QCoreApplication::instance();
    if (application == nullptr || QThread::currentThread() == application->thread()) {
        return {exthost::ServiceStatus::Unsupported,
                "the device functions work only on a thread of the DLL's own, not on one of GameScrcpy's"};
    }
    if (stop.stop_requested()) {
        return {exthost::ServiceStatus::Failed, "GS-API-084 the extension stopped"};
    }
    struct Wait {
        std::mutex mutex;
        std::condition_variable_any answered;
        std::optional<exthost::ServiceResult> result;
    };
    const auto wait = std::make_shared<Wait>();
    callFromNative(caller, std::string(request), std::string(json), [wait](exthost::ServiceResult result) {
        {
            const std::scoped_lock lock(wait->mutex);
            wait->result = std::move(result);
        }
        wait->answered.notify_all();
    });
    std::unique_lock lock(wait->mutex);
    if (!wait->answered.wait_for(lock, stop, kNativeWait, [&wait] { return wait->result.has_value(); })) {
        return stop.stop_requested()
                   ? exthost::ServiceResult{exthost::ServiceStatus::Failed, "GS-API-084 the extension stopped"}
                   : exthost::ServiceResult{exthost::ServiceStatus::Failed,
                                            "GS-API-083 GameScrcpy did not answer for the device in time"};
    }
    return std::move(*wait->result);
}

void HardwareAccess::resetNative(std::uint64_t key)
{
    std::shared_ptr<Shared> shared;
    {
        const std::scoped_lock lock(s_nativeMutex);
        shared = s_native.lock();
    }
    if (shared != nullptr) {
        shared->post([key](HardwareAccess& self) { self.resetNativeRoute(kNativeLogics + key); });
    }
}

void HardwareAccess::endNative(std::uint64_t key)
{
    std::shared_ptr<Shared> shared;
    {
        const std::scoped_lock lock(s_nativeMutex);
        shared = s_native.lock();
    }
    if (shared != nullptr) {
        shared->post([key](HardwareAccess& self) { self.endNativeRoute(kNativeLogics + key); });
    }
}

void HardwareAccess::nativeRequest(const NativeHardware& caller, const std::string& service, const std::string& json,
                                   NativeAnswer answer)
{
    const std::uint64_t logic = kNativeLogics + caller.key;
    if (std::ranges::find(kNativeRequests, std::string_view(service)) == kNativeRequests.end()) {
        answer({exthost::ServiceStatus::Unsupported,
                "GS-API-070 no part of GameScrcpy offers the service " + service + " to a DLL"});
        return;
    }
    if (m_endedLogic.contains(logic)) {
        answer({exthost::ServiceStatus::Failed, "GS-API-084 the extension stopped"});
        return;
    }
    QJsonObject body = objectOf(QByteArray::fromStdString(json));
    if (const QJsonValue device = body.value(u"device"); device.isDouble()) {
        body.insert(QStringLiteral("handle"), QStringLiteral("d%1").arg(static_cast<qulonglong>(device.toDouble())));
    }
    body.remove(QStringLiteral("device"));
    NativeRoute& route = m_native[logic];
    route.caller = caller;
    const quint64 id = ++route.lastRequest;
    route.waiting.insert_or_assign(id, NativeWaiting{service, std::move(answer)});
    request(LogicCaller{.logic = logic, .extension = caller.extension}, service, id,
            compactJson(QJsonDocument(body)));
}

void HardwareAccess::resetNativeRoute(std::uint64_t logic)
{
    std::erase_if(m_openings, [logic](const auto& entry) { return entry.second.logic == logic; });
    std::vector<QString> closing;
    for (const auto& [handle, open] : m_handles) {
        if (open.logic == logic) {
            closing.push_back(handle);
        }
    }
    for (const QString& handle : closing) {
        closeHandle(handle, QStringLiteral("ended"), {}, false);
    }
    if (const auto found = m_native.find(logic); found != m_native.end()) {
        found->second.waiting.clear();
    }
}

void HardwareAccess::endNativeRoute(std::uint64_t logic)
{
    endLogic(logic);
    m_native.erase(logic);
}

void HardwareAccess::answerNative(std::uint64_t logic, quint64 request, exthost::ServiceResult result) const
{
    const auto route = m_native.find(logic);
    if (route == m_native.end()) {
        return;
    }
    auto node = route->second.waiting.extract(request);
    if (node.empty()) {
        return;
    }
    NativeWaiting& waiting = node.mapped();
    if (result.status == exthost::ServiceStatus::Ok && waiting.request == "hardware.open") {
        QJsonObject made = objectOf(QByteArray::fromStdString(result.text));
        const QString handle = made.value(u"handle").toString();
        made.remove(QStringLiteral("handle"));
        made.insert(QStringLiteral("device"), static_cast<qint64>(handle.mid(1).toLongLong()));
        result.text = compactJson(QJsonDocument(made)).toStdString();
    }
    if (waiting.answer) {
        waiting.answer(std::move(result));
    }
}

void HardwareAccess::tellNative(std::uint64_t logic, const QString& handle, const QString& json) const
{
    const auto route = m_native.find(logic);
    if (route == m_native.end() || !route->second.caller.event || !handle.startsWith(u'd')) {
        return;
    }
    route->second.caller.event(handle.mid(1).toULongLong(), json.toStdString());
}

std::string HardwareAccess::holderOf(std::uint64_t logic) const
{
    if (const auto found = m_native.find(logic); found != m_native.end()) {
        return found->second.caller.holder.id;
    }
    return logicHolder(logic);
}

ClaimHolder HardwareAccess::claimerOf(std::uint64_t logic, const QString& owner) const
{
    if (const auto found = m_native.find(logic); found != m_native.end()) {
        return found->second.caller.holder;
    }
    return ClaimHolder{logicHolder(logic), claimerName(owner), {}};
}

}
