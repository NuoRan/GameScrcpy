#include "NetAccess.h"

#include "AiNetwork.h"
#include "ExtensionHost.h"
#include "LogicUploads.h"
#include "WebSocketClient.h"

#include <gs/ext/Manifest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QList>
#include <QMetaObject>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QNetworkProxyQuery>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QVariant>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <utility>

namespace gs::app {

namespace {

constexpr std::string_view kFetch = "net.fetch";
constexpr std::string_view kFetchPart = "net.fetch.part";
constexpr std::string_view kSocket = "net.socket";
constexpr std::string_view kSocketPart = "net.socket.part";
constexpr std::string_view kSocketSend = "net.socket.send";
constexpr std::string_view kSocketClose = "net.socket.close";
constexpr int kMostRedirects = 5;
constexpr qsizetype kMostHeaders = 64;
constexpr qsizetype kMostHeaderValue = 4096;
constexpr qsizetype kMostUrlLength = 4096;
constexpr qsizetype kMostProtocols = 8;
constexpr qsizetype kMostReasonBytes = 123;
constexpr qsizetype kMostUploadBytes = qsizetype{16} << 20;
constexpr double kLeastTimeoutMs = 1000;
constexpr double kMostTimeoutMs = 60000;
constexpr double kDefaultTimeoutMs = 15000;
constexpr int kSweepMs = 100;
constexpr std::array<std::string_view, 7> kMethods{"GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"};
constexpr std::array<std::string_view, 9> kOwnHeaders{"host",    "content-length", "transfer-encoding",
                                                      "connection", "keep-alive", "upgrade",
                                                      "te",         "trailer",    "expect"};

QString fromView(std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString compactJson(const QJsonObject& object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QJsonObject objectOf(const QByteArray& json)
{
    QJsonParseError problem;
    const QJsonDocument document = QJsonDocument::fromJson(json, &problem);
    return problem.error == QJsonParseError::NoError && document.isObject() ? document.object() : QJsonObject();
}

script::ServiceAnswer refused(const QString& message)
{
    const std::string full = message.toStdString();
    const std::size_t space = full.find(' ');
    return {"null", full.substr(0, space), space == std::string::npos ? std::string() : full.substr(space + 1)};
}

QString hostOf(const QUrl& url)
{
    QString host = url.host(QUrl::EncodeUnicode);
    if (url.port() != -1) {
        host += u':' + QString::number(url.port());
    }
    return host;
}

QString notPermitted()
{
    return QStringLiteral("GS-API-155 this extension may not use the network: its manifest does not ask for the "
                          "permission \"network\"");
}

QString notListed(const QUrl& url)
{
    return QStringLiteral("GS-API-155 this extension may not reach %1: its manifest does not list it under the "
                          "permission \"network\"")
        .arg(hostOf(url));
}

QString invalidWith(const QString& what)
{
    return QStringLiteral("GS-API-156 ") + what;
}

QString overLimit(const QString& what)
{
    return QStringLiteral("GS-API-157 ") + what;
}

QString failedWith(const QString& what)
{
    return QStringLiteral("GS-API-158 ") + what;
}

QString notOpen()
{
    return QStringLiteral("GS-API-159 the socket is closed");
}

bool tokenName(const QString& name)
{
    constexpr std::string_view kMarks = "!#$%&'*+-.^_`|~";
    return !name.isEmpty() && name.size() <= 64 && std::ranges::all_of(name, [kMarks](QChar c) {
               const char16_t code = c.unicode();
               return (code >= u'a' && code <= u'z') || (code >= u'A' && code <= u'Z') || (code >= u'0' && code <= u'9') ||
                      (code < 128 && kMarks.find(static_cast<char>(code)) != std::string_view::npos);
           });
}

bool fieldValue(const QString& value)
{
    return value.size() <= kMostHeaderValue && std::ranges::none_of(value, [](QChar c) {
               return (c.unicode() < 0x20 && c != u'\t') || c.unicode() == 0x7F;
           });
}

}

struct NetAccess::Shared {
    struct SocketShare {
        std::uint64_t logic = 0;
        bool open = false;
        qint64 pending = 0;
        qint64 queued = 0;
    };

    mutable std::mutex mutex;
    NetAccess* owner = nullptr;
    std::map<QString, std::vector<std::string>> hosts;
    std::map<QString, SocketShare> shares;
    LogicUploads uploads{kMostUploadBytes};

    void post(std::function<void(NetAccess&)> work) const
    {
        const std::scoped_lock lock(mutex);
        if (owner == nullptr) {
            return;
        }
        NetAccess* target = owner;
        QMetaObject::invokeMethod(target, [target, work = std::move(work)] { work(*target); }, Qt::QueuedConnection);
    }

    [[nodiscard]] bool permitted(const QString& extension) const
    {
        const std::scoped_lock lock(mutex);
        return extension.isEmpty() || hosts.contains(extension);
    }
};

NetAccess::NetAccess(QObject* parent)
    : QObject(parent)
    , m_shared(std::make_shared<Shared>())
{
    m_shared->owner = this;
    m_replies = {
        .answer = [](std::uint64_t logic, quint64 request,
                     const QString& json) { static_cast<void>(LogicServices::instance().answer(logic, request, json)); },
        .refuse = [](std::uint64_t logic, quint64 request,
                     const QString& message) { static_cast<void>(LogicServices::instance().refuse(logic, request, message)); },
        .tell = [](std::uint64_t logic, const QString& kind, const QString& id,
                   const QString& json) { static_cast<void>(LogicServices::instance().tell(logic, kind, id, json)); },
    };
    m_sweep.setInterval(kSweepMs);
    connect(&m_sweep, &QTimer::timeout, this, &NetAccess::sweep);
}

NetAccess::~NetAccess()
{
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->owner = nullptr;
    }
    if (m_offered) {
        for (const std::string_view name : {kFetch, kFetchPart, kSocket, kSocketPart, kSocketSend, kSocketClose}) {
            LogicServices::instance().withdraw(name);
        }
    }
    std::vector<std::uint64_t> fetches;
    for (const auto& entry : m_fetches) {
        fetches.push_back(entry.first);
    }
    for (const std::uint64_t serial : fetches) {
        forgetFetch(serial);
    }
    for (auto& entry : m_sockets) {
        disconnect(entry.second.client.get(), nullptr, this, nullptr);
    }
    m_sockets.clear();
}

void NetAccess::offerServices()
{
    LogicServices& services = LogicServices::instance();
    for (const std::string_view name : {kFetchPart, kSocketPart, kSocketSend, kSocketClose}) {
        services.offerCall(std::string(name), [shared = m_shared, name](const LogicCaller& caller, std::string_view json) {
            return callWith(shared, caller, name, json);
        });
    }
    for (const std::string_view name : {kFetch, kSocket}) {
        services.offerRequest(
            std::string(name),
            [this, name](const LogicCaller& caller, quint64 id, const QString& json) { request(caller, name, id, json); },
            [this](const LogicCaller& caller, quint64 id) { drop(caller, id); });
    }
    connect(&services, &LogicServices::logicEnded, this, [this](quint64 logic) { endLogic(logic); });
    m_offered = true;
}

void NetAccess::setReplies(NetReplies replies)
{
    m_replies = std::move(replies);
}

void NetAccess::setHosts(std::map<QString, std::vector<std::string>> hosts)
{
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->hosts = std::move(hosts);
    }
    const auto problemOf = [this](const QString& owner, const QUrl& url) {
        if (owner.isEmpty() || reachable(owner, url)) {
            return QString();
        }
        return m_shared->permitted(owner) ? notListed(url) : notPermitted();
    };
    std::vector<std::pair<std::uint64_t, QString>> fetches;
    for (const auto& [serial, asked] : m_fetches) {
        if (QString problem = problemOf(asked.owner, asked.url); !problem.isEmpty()) {
            fetches.emplace_back(serial, std::move(problem));
        }
    }
    for (const auto& [serial, problem] : fetches) {
        failFetch(serial, problem);
    }
    std::vector<std::pair<QString, QString>> closing;
    for (const auto& [handle, socket] : m_sockets) {
        if (QString problem = problemOf(socket.owner, socket.url); !problem.isEmpty()) {
            closing.emplace_back(handle, std::move(problem));
        }
    }
    for (const auto& [handle, problem] : closing) {
        forgetSocket(handle, problem);
    }
}

void NetAccess::follow(ExtensionHost& extensions)
{
    const auto update = [this, &extensions] {
        std::map<QString, std::vector<std::string>> hosts;
        for (const ExtensionHost::Extension& extension : extensions.extensions()) {
            if (!extension.active || !extension.manifest) {
                continue;
            }
            const std::vector<ext::Permission>& permissions = extension.manifest->permissions;
            const auto network = std::ranges::find(permissions, ext::kNetworkPermission, &ext::Permission::name);
            if (network != permissions.end()) {
                hosts.insert_or_assign(QString::fromStdString(extension.manifest->id), network->hosts);
            }
        }
        setHosts(std::move(hosts));
    };
    update();
    connect(&extensions, &ExtensionHost::extensionsChanged, this, update);
}

void NetAccess::useSystemProxy()
{
    aiUseSystemProxy(m_network);
    m_systemProxy = true;
}

void NetAccess::allowPlainLocal(bool allowed)
{
    m_plainLocal = allowed;
}

void NetAccess::setHandshakeLimit(std::chrono::milliseconds limit)
{
    m_handshakeLimit = limit;
}

void NetAccess::request(const LogicCaller& caller, std::string_view service, quint64 id, const QString& json)
{
    if (service == kFetch) {
        fetch(caller, id, json);
        return;
    }
    if (service == kSocket) {
        openSocket(caller, id, json);
        return;
    }
    refuse(caller.logic, id, QStringLiteral("GS-API-070 no part of GameScrcpy offers the service %1").arg(fromView(service)));
}

script::ServiceAnswer NetAccess::call(const LogicCaller& caller, std::string_view service, std::string_view json)
{
    return callWith(m_shared, caller, service, json);
}

script::ServiceAnswer NetAccess::callWith(const std::shared_ptr<Shared>& shared, const LogicCaller& caller,
                                          std::string_view service, std::string_view json)
{
    if (!shared->permitted(caller.extension)) {
        return refused(notPermitted());
    }
    const QJsonObject body = objectOf(QByteArray(json.data(), static_cast<qsizetype>(json.size())));
    if (service == kFetchPart || service == kSocketPart) {
        const bool forBody = service == kFetchPart;
        switch (shared->uploads.add(caller.logic, body, forBody ? kMostBodyBytes : kMostMessageBytes)) {
        case LogicUploads::Result::Done:
            return {"null", {}, {}};
        case LogicUploads::Result::Invalid:
            return refused(invalidWith(QStringLiteral("what is sent must be text or bytes")));
        case LogicUploads::Result::TooLarge:
            return refused(overLimit(forBody ? QStringLiteral("the body of a request is at most 1 MiB")
                                             : QStringLiteral("a message is at most 1 MiB")));
        case LogicUploads::Result::TooMany:
            return refused(overLimit(QStringLiteral("a logic has at most 64 bodies and messages waiting in parts")));
        }
        return {"null", {}, {}};
    }
    const QString handle = body.value(u"handle").toString();
    if (service == kSocketClose) {
        int code = WebSocketClient::kNormalClose;
        if (const QJsonValue given = body.value(u"code"); !given.isUndefined()) {
            const double number = given.toDouble(-1);
            if (!given.isDouble() || std::floor(number) != number ||
                !(number == WebSocketClient::kNormalClose || (number >= 3000 && number <= 4999))) {
                return refused(invalidWith(QStringLiteral("the close code is 1000 or from 3000 to 4999")));
            }
            code = static_cast<int>(number);
        }
        const QByteArray reason = body.value(u"reason").toString().toUtf8();
        if (reason.size() > kMostReasonBytes) {
            return refused(invalidWith(QStringLiteral("the reason for closing is at most 123 bytes of UTF-8")));
        }
        {
            const std::scoped_lock lock(shared->mutex);
            const auto found = shared->shares.find(handle);
            if (found == shared->shares.end() || found->second.logic != caller.logic) {
                return {"null", {}, {}};
            }
            found->second.open = false;
        }
        shared->post([handle, code, reason](NetAccess& self) { self.closeSocket(handle, code, reason); });
        return {"null", {}, {}};
    }
    if (service != kSocketSend) {
        return {"null", "GS-API-070", "no part of GameScrcpy answers the service " + std::string(service) + " at once"};
    }
    LogicUploads::Taken payload = shared->uploads.take(caller.logic, body, kMostMessageBytes);
    if (payload.result == LogicUploads::Result::Invalid || !body.value(u"binary").isBool()) {
        return refused(invalidWith(QStringLiteral("a message must be text or bytes")));
    }
    if (payload.result == LogicUploads::Result::TooLarge) {
        return refused(overLimit(QStringLiteral("a message is at most 1 MiB")));
    }
    const bool binary = body.value(u"binary").toBool();
    const qint64 size = payload.bytes.size();
    {
        const std::scoped_lock lock(shared->mutex);
        const auto found = shared->shares.find(handle);
        if (found == shared->shares.end() || found->second.logic != caller.logic || !found->second.open) {
            return refused(notOpen());
        }
        if (found->second.pending + found->second.queued + size > kMostWaitingBytes) {
            return refused(overLimit(QStringLiteral("16 MiB are waiting to be sent on the socket; send less often")));
        }
        found->second.pending += size;
    }
    shared->post([handle, data = std::move(payload.bytes), binary](NetAccess& self) { self.sendOn(handle, data, binary); });
    return {"null", {}, {}};
}

void NetAccess::drop(const LogicCaller& caller, quint64 id)
{
    for (const auto& [serial, asked] : m_fetches) {
        if (asked.logic == caller.logic && asked.request == id) {
            forgetFetch(serial);
            return;
        }
    }
    for (const auto& [handle, socket] : m_sockets) {
        if (socket.logic == caller.logic && socket.request == id && !socket.open) {
            forgetSocket(handle, {});
            return;
        }
    }
}

void NetAccess::endLogic(std::uint64_t logic)
{
    std::vector<std::uint64_t> fetches;
    for (const auto& [serial, asked] : m_fetches) {
        if (asked.logic == logic) {
            fetches.push_back(serial);
        }
    }
    for (const std::uint64_t serial : fetches) {
        forgetFetch(serial);
    }
    std::vector<QString> closing;
    for (const auto& [handle, socket] : m_sockets) {
        if (socket.logic == logic) {
            closing.push_back(handle);
        }
    }
    for (const QString& handle : closing) {
        forgetSocket(handle, {});
    }
    m_shared->uploads.drop(logic);
}

QString NetAccess::urlProblem(const QString& owner, const QString& text, bool socket, QUrl& url) const
{
    url = QUrl(text, QUrl::StrictMode);
    const QString secure = socket ? QStringLiteral("wss") : QStringLiteral("https");
    const QString plain = socket ? QStringLiteral("ws") : QStringLiteral("http");
    const bool local = m_plainLocal && url.scheme() == plain && url.host() == u"127.0.0.1";
    if (text.size() > kMostUrlLength || !url.isValid() || url.host().isEmpty() || (url.scheme() != secure && !local)) {
        return invalidWith(socket ? QStringLiteral("the address must start with wss://, such as \"wss://example.com/live\"")
                                  : QStringLiteral("the address must start with https://, such as "
                                                   "\"https://example.com/data\""));
    }
    if (!url.userInfo().isEmpty()) {
        return invalidWith(QStringLiteral("the address must not hold a user name or a password; send them in a header"));
    }
    url.setFragment(QString());
    if (!m_shared->permitted(owner)) {
        return notPermitted();
    }
    if (!reachable(owner, url)) {
        return notListed(url);
    }
    return {};
}

bool NetAccess::reachable(const QString& owner, const QUrl& url) const
{
    if (owner.isEmpty()) {
        return true;
    }
    const bool plain = url.scheme() == u"http" || url.scheme() == u"ws";
    const std::string host = url.host(QUrl::EncodeUnicode).toStdString();
    const int port = url.port(plain ? 80 : 443);
    const std::scoped_lock lock(m_shared->mutex);
    const auto found = m_shared->hosts.find(owner);
    return found != m_shared->hosts.end() && ext::hostListed(found->second, host, port);
}

void NetAccess::fetch(const LogicCaller& caller, quint64 id, const QString& json)
{
    const QJsonObject body = objectOf(json.toUtf8());
    LogicUploads::Taken payload = m_shared->uploads.take(caller.logic, body, kMostBodyBytes);
    QUrl url;
    if (const QString problem = urlProblem(caller.extension, body.value(u"url").toString(), false, url);
        !problem.isEmpty()) {
        refuse(caller.logic, id, problem);
        return;
    }
    if (payload.result == LogicUploads::Result::Invalid) {
        refuse(caller.logic, id, invalidWith(QStringLiteral("the body must be text or bytes")));
        return;
    }
    if (payload.result == LogicUploads::Result::TooLarge) {
        refuse(caller.logic, id, overLimit(QStringLiteral("the body of a request is at most 1 MiB")));
        return;
    }
    const QByteArray method = body.value(u"method").toString(QStringLiteral("GET")).toUpper().toLatin1();
    if (std::ranges::find(kMethods, std::string_view(method.constData(), static_cast<std::size_t>(method.size()))) ==
        kMethods.end()) {
        refuse(caller.logic, id,
               invalidWith(QStringLiteral("the method is one of GET, HEAD, POST, PUT, PATCH, DELETE and OPTIONS")));
        return;
    }
    if ((method == "GET" || method == "HEAD") && !payload.bytes.isEmpty()) {
        refuse(caller.logic, id, invalidWith(QStringLiteral("a GET or HEAD request has no body")));
        return;
    }
    const QJsonValue givenHeaders = body.value(u"headers");
    const QJsonObject headers = givenHeaders.toObject();
    if ((!givenHeaders.isUndefined() && !givenHeaders.isObject()) || headers.size() > kMostHeaders) {
        refuse(caller.logic, id, invalidWith(QStringLiteral("the headers are an object of at most 64 names and texts")));
        return;
    }
    std::vector<std::pair<QByteArray, QByteArray>> raw;
    bool typed = false;
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        const QString name = it.key();
        const QString value = it.value().toString();
        if (!tokenName(name) || !it.value().isString() || !fieldValue(value)) {
            refuse(caller.logic, id,
                   invalidWith(QStringLiteral("the header %1 has a name or a value that HTTP does not allow").arg(name)));
            return;
        }
        const std::string lower = name.toLower().toStdString();
        if (std::ranges::find(kOwnHeaders, std::string_view(lower)) != kOwnHeaders.end() ||
            lower.starts_with("proxy-") || lower.starts_with("sec-")) {
            refuse(caller.logic, id, invalidWith(QStringLiteral("GameScrcpy sets the header %1 itself").arg(name)));
            return;
        }
        typed = typed || lower == "content-type";
        raw.emplace_back(name.toLatin1(), value.toUtf8());
    }
    const QJsonValue givenTimeout = body.value(u"timeoutMs");
    const double timeout = givenTimeout.isUndefined() ? kDefaultTimeoutMs : givenTimeout.toDouble(-1);
    if ((!givenTimeout.isUndefined() && !givenTimeout.isDouble()) || !(timeout >= kLeastTimeoutMs && timeout <= kMostTimeoutMs)) {
        refuse(caller.logic, id, invalidWith(QStringLiteral("timeoutMs is from 1000 to 60000 milliseconds")));
        return;
    }
    const auto ofLogic = std::ranges::count_if(m_fetches, [&caller](const auto& entry) {
        return entry.second.logic == caller.logic;
    });
    if (static_cast<std::size_t>(ofLogic) >= kMostFetches) {
        refuse(caller.logic, id, overLimit(QStringLiteral("a logic can have at most 8 requests going at once")));
        return;
    }
    QNetworkRequest http(url);
    for (const auto& [name, value] : raw) {
        http.setRawHeader(name, value);
    }
    if (!typed && !payload.bytes.isEmpty()) {
        http.setRawHeader("Content-Type", "application/octet-stream");
    }
    http.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    http.setMaximumRedirectsAllowed(kMostRedirects);
    http.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    http.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    http.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    http.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    http.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    QNetworkReply* reply = nullptr;
    if (method == "GET") {
        reply = m_network.get(http);
    } else if (method == "HEAD") {
        reply = m_network.head(http);
    } else {
        reply = m_network.sendCustomRequest(http, method, payload.bytes);
    }
    const std::uint64_t serial = ++m_lastFetch;
    const int timeoutMs = static_cast<int>(timeout);
    m_fetches.emplace(serial, Fetch{.logic = caller.logic,
                                    .request = id,
                                    .owner = caller.extension,
                                    .reply = reply,
                                    .url = url,
                                    .body = {},
                                    .deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs),
                                    .timeoutMs = timeoutMs});
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, serial] { measured(serial); });
    connect(reply, &QIODevice::readyRead, this, [this, serial] { readSome(serial); });
    connect(reply, &QNetworkReply::redirected, this, [this, serial](const QUrl& next) { redirected(serial, next); });
    connect(reply, &QNetworkReply::finished, this, [this, serial] { fetched(serial); });
    startSweep();
}

void NetAccess::readSome(std::uint64_t serial)
{
    const auto found = m_fetches.find(serial);
    if (found == m_fetches.end()) {
        return;
    }
    Fetch& asked = found->second;
    asked.body.append(asked.reply->readAll());
    if (asked.body.size() > kMostAnswerBytes) {
        failFetch(serial, overLimit(QStringLiteral("the answer is larger than 8 MiB")));
    }
}

void NetAccess::measured(std::uint64_t serial)
{
    const auto found = m_fetches.find(serial);
    if (found == m_fetches.end()) {
        return;
    }
    const QVariant length = found->second.reply->header(QNetworkRequest::ContentLengthHeader);
    if (length.isValid() && length.toLongLong() > kMostAnswerBytes) {
        failFetch(serial, overLimit(QStringLiteral("the answer is larger than 8 MiB")));
    }
}

void NetAccess::redirected(std::uint64_t serial, const QUrl& next)
{
    const auto found = m_fetches.find(serial);
    if (found == m_fetches.end()) {
        return;
    }
    Fetch& asked = found->second;
    const QUrl resolved = asked.url.resolved(next);
    QUrl target;
    if (!urlProblem(asked.owner, resolved.toString(QUrl::FullyEncoded), false, target).isEmpty()) {
        failFetch(serial, QStringLiteral("GS-API-155 the server sent the request on to %1, which this logic may not "
                                         "reach: only https addresses on the hosts the extension lists")
                              .arg(resolved.toString(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo)));
        return;
    }
    asked.url = target;
    asked.body.clear();
    emit asked.reply->redirectAllowed();
}

void NetAccess::fetched(std::uint64_t serial)
{
    auto node = m_fetches.extract(serial);
    if (node.empty()) {
        return;
    }
    Fetch& asked = node.mapped();
    QNetworkReply* reply = asked.reply;
    disconnect(reply, nullptr, this, nullptr);
    asked.body.append(reply->readAll());
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError error = reply->error();
    if (asked.body.size() > kMostAnswerBytes) {
        refuse(asked.logic, asked.request, overLimit(QStringLiteral("the answer is larger than 8 MiB")));
        return;
    }
    if (error == QNetworkReply::TooManyRedirectsError) {
        refuse(asked.logic, asked.request, failedWith(QStringLiteral("the server sent the request on more than 5 times")));
        return;
    }
    if (status == 0 || (error != QNetworkReply::NoError && static_cast<int>(error) < 200)) {
        refuse(asked.logic, asked.request,
               failedWith(QStringLiteral("%1 could not be reached: %2").arg(hostOf(asked.url), reply->errorString())));
        return;
    }
    QJsonObject headers;
    for (const auto& [name, value] : reply->rawHeaderPairs()) {
        const QString key = QString::fromLatin1(name).toLower();
        const QString text = QString::fromUtf8(value);
        const QString before = headers.value(key).toString();
        headers.insert(key, before.isEmpty() ? text : before + QStringLiteral(", ") + text);
    }
    QJsonObject result{{QStringLiteral("status"), status},
                       {QStringLiteral("url"), asked.url.toString(QUrl::FullyEncoded)},
                       {QStringLiteral("headers"), headers}};
    if (asked.body.isValidUtf8()) {
        result.insert(QStringLiteral("text"), QString::fromUtf8(asked.body));
    } else {
        result.insert(QStringLiteral("body"), QString::fromLatin1(asked.body.toBase64()));
    }
    answer(asked.logic, asked.request, compactJson(result));
}

void NetAccess::failFetch(std::uint64_t serial, const QString& message)
{
    const auto found = m_fetches.find(serial);
    if (found == m_fetches.end()) {
        return;
    }
    const std::uint64_t logic = found->second.logic;
    const quint64 id = found->second.request;
    forgetFetch(serial);
    refuse(logic, id, message);
}

void NetAccess::forgetFetch(std::uint64_t serial)
{
    auto node = m_fetches.extract(serial);
    if (node.empty()) {
        return;
    }
    QNetworkReply* reply = node.mapped().reply;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}

void NetAccess::openSocket(const LogicCaller& caller, quint64 id, const QString& json)
{
    const QJsonObject body = objectOf(json.toUtf8());
    QUrl url;
    if (const QString problem = urlProblem(caller.extension, body.value(u"url").toString(), true, url);
        !problem.isEmpty()) {
        refuse(caller.logic, id, problem);
        return;
    }
    QStringList protocols;
    if (const QJsonValue given = body.value(u"protocols"); !given.isUndefined()) {
        const QJsonArray list = given.toArray();
        if (!given.isArray() || list.size() > kMostProtocols) {
            refuse(caller.logic, id, invalidWith(QStringLiteral("protocols is a list of at most 8 names")));
            return;
        }
        for (const QJsonValue& each : list) {
            const QString name = each.toString();
            if (!each.isString() || !tokenName(name) || protocols.contains(name)) {
                refuse(caller.logic, id,
                       invalidWith(QStringLiteral("a protocol is named once, with letters, digits and !#$%&'*+-.^_`|~")));
                return;
            }
            protocols.append(name);
        }
    }
    const auto ofLogic = std::ranges::count_if(m_sockets, [&caller](const auto& entry) {
        return entry.second.logic == caller.logic && !entry.second.closing;
    });
    if (static_cast<std::size_t>(ofLogic) >= kMostSockets) {
        refuse(caller.logic, id, overLimit(QStringLiteral("a logic can have at most 4 sockets open")));
        return;
    }
    const QString handle = QStringLiteral("w%1").arg(++m_lastSocket);
    auto client = std::make_unique<WebSocketClient>(url, protocols, kMostMessageBytes);
    WebSocketClient* made = client.get();
    if (m_systemProxy) {
        QUrl probe = url;
        probe.setScheme(url.scheme() == u"ws" ? QStringLiteral("http") : QStringLiteral("https"));
        const QList<QNetworkProxy> proxies = QNetworkProxyFactory::systemProxyForQuery(QNetworkProxyQuery(probe));
        if (!proxies.isEmpty()) {
            made->setProxy(proxies.front());
        }
    }
    connect(made, &WebSocketClient::opened, this, [this, handle] { socketOpened(handle); });
    connect(made, &WebSocketClient::received, this,
            [this, handle](const QByteArray& data, bool binary) { socketMessage(handle, data, binary); });
    connect(made, &WebSocketClient::drained, this, [this, handle](qint64 queued) {
        const std::scoped_lock lock(m_shared->mutex);
        if (const auto share = m_shared->shares.find(handle); share != m_shared->shares.end()) {
            share->second.queued = queued;
        }
    });
    connect(made, &WebSocketClient::closing, this, &NetAccess::startSweep);
    connect(made, &WebSocketClient::ended, this,
            [this, handle](int code, const QString& reason, bool clean) { socketEnded(handle, code, reason, clean); });
    m_sockets.emplace(handle, Socket{.logic = caller.logic,
                                     .request = id,
                                     .owner = caller.extension,
                                     .url = url,
                                     .client = std::move(client),
                                     .open = false,
                                     .closing = false});
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->shares.insert_or_assign(handle, Shared::SocketShare{.logic = caller.logic});
    }
    startSweep();
    made->open(std::chrono::steady_clock::now() + m_handshakeLimit);
}

void NetAccess::socketOpened(const QString& handle)
{
    const auto found = m_sockets.find(handle);
    if (found == m_sockets.end()) {
        return;
    }
    Socket& socket = found->second;
    socket.open = true;
    {
        const std::scoped_lock lock(m_shared->mutex);
        if (const auto share = m_shared->shares.find(handle); share != m_shared->shares.end()) {
            share->second.open = true;
        }
    }
    answer(socket.logic, socket.request,
           compactJson(QJsonObject{{QStringLiteral("handle"), handle},
                                   {QStringLiteral("url"), socket.url.toString(QUrl::FullyEncoded)},
                                   {QStringLiteral("protocol"), socket.client->protocol()}}));
}

void NetAccess::socketMessage(const QString& handle, const QByteArray& data, bool binary)
{
    const auto found = m_sockets.find(handle);
    if (found == m_sockets.end() || !found->second.open || found->second.closing) {
        return;
    }
    QJsonObject event{{QStringLiteral("type"), QStringLiteral("message")}, {QStringLiteral("binary"), binary}};
    if (binary) {
        event.insert(QStringLiteral("data"), QString::fromLatin1(data.toBase64()));
    } else {
        event.insert(QStringLiteral("text"), QString::fromUtf8(data));
    }
    tell(found->second.logic, handle, compactJson(event));
}

void NetAccess::socketEnded(const QString& handle, int code, const QString& reason, bool clean)
{
    auto node = m_sockets.extract(handle);
    if (node.empty()) {
        return;
    }
    Socket& socket = node.mapped();
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->shares.erase(handle);
    }
    socket.client.release()->deleteLater();
    if (socket.closing) {
        return;
    }
    if (socket.open) {
        tell(socket.logic, handle,
             compactJson(QJsonObject{{QStringLiteral("type"), QStringLiteral("closed")},
                                     {QStringLiteral("code"), code},
                                     {QStringLiteral("reason"), reason},
                                     {QStringLiteral("clean"), clean}}));
        return;
    }
    refuse(socket.logic, socket.request,
           failedWith(QStringLiteral("the WebSocket connection to %1 failed: %2").arg(hostOf(socket.url), reason)));
}

void NetAccess::sendOn(const QString& handle, const QByteArray& data, bool binary)
{
    const auto found = m_sockets.find(handle);
    const bool open = found != m_sockets.end() && found->second.open && !found->second.closing &&
                      found->second.client->isOpen();
    if (open) {
        found->second.client->send(data, binary);
    }
    const std::scoped_lock lock(m_shared->mutex);
    if (const auto share = m_shared->shares.find(handle); share != m_shared->shares.end()) {
        share->second.pending = std::max<qint64>(0, share->second.pending - data.size());
        share->second.queued = open ? found->second.client->queued() : 0;
    }
}

void NetAccess::closeSocket(const QString& handle, int code, const QByteArray& reason)
{
    const auto found = m_sockets.find(handle);
    if (found == m_sockets.end() || found->second.closing) {
        return;
    }
    Socket& socket = found->second;
    socket.closing = true;
    if (!socket.open) {
        forgetSocket(handle, {});
        return;
    }
    socket.client->close(code, reason);
    startSweep();
}

void NetAccess::forgetSocket(const QString& handle, const QString& reason)
{
    auto node = m_sockets.extract(handle);
    if (node.empty()) {
        return;
    }
    Socket& socket = node.mapped();
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->shares.erase(handle);
    }
    disconnect(socket.client.get(), nullptr, this, nullptr);
    socket.client.release()->deleteLater();
    if (reason.isEmpty() || socket.closing) {
        return;
    }
    if (socket.open) {
        tell(socket.logic, handle,
             compactJson(QJsonObject{{QStringLiteral("type"), QStringLiteral("closed")},
                                     {QStringLiteral("code"), WebSocketClient::kAbnormal},
                                     {QStringLiteral("reason"), reason},
                                     {QStringLiteral("clean"), false}}));
        return;
    }
    refuse(socket.logic, socket.request, reason);
}

void NetAccess::startSweep()
{
    if (!m_sweep.isActive()) {
        m_sweep.start();
    }
}

void NetAccess::sweep()
{
    const auto now = std::chrono::steady_clock::now();
    std::vector<std::uint64_t> late;
    for (const auto& [serial, asked] : m_fetches) {
        if (now >= asked.deadline) {
            late.push_back(serial);
        }
    }
    for (const std::uint64_t serial : late) {
        const auto found = m_fetches.find(serial);
        if (found != m_fetches.end()) {
            failFetch(serial, failedWith(QStringLiteral("%1 did not answer within %2 ms")
                                             .arg(hostOf(found->second.url))
                                             .arg(found->second.timeoutMs)));
        }
    }
    std::vector<WebSocketClient*> waiting;
    for (const auto& [handle, socket] : m_sockets) {
        if (socket.client && socket.client->waiting()) {
            waiting.push_back(socket.client.get());
        }
    }
    for (WebSocketClient* client : waiting) {
        client->expire(now);
    }
    const bool more = !m_fetches.empty() || std::ranges::any_of(m_sockets, [](const auto& entry) {
                          return entry.second.client && entry.second.client->waiting();
                      });
    if (!more) {
        m_sweep.stop();
    }
}

void NetAccess::answer(std::uint64_t logic, quint64 request, const QString& json) const
{
    if (m_replies.answer) {
        m_replies.answer(logic, request, json);
    }
}

void NetAccess::refuse(std::uint64_t logic, quint64 request, const QString& message) const
{
    if (m_replies.refuse) {
        m_replies.refuse(logic, request, message);
    }
}

void NetAccess::tell(std::uint64_t logic, const QString& handle, const QString& json) const
{
    if (m_replies.tell) {
        m_replies.tell(logic, fromView(kEventKind), handle, json);
    }
}

}
