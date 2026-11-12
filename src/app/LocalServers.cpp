#include "LocalServers.h"

#include "ExtensionHost.h"

#include <QHostAddress>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>
#include <set>
#include <string>

namespace gs::app {

namespace {

constexpr std::string_view kListen = "net.listen";
constexpr std::string_view kRespond = "net.respond";
constexpr std::string_view kClose = "net.close";
constexpr int kMostResponseHeaders = 32;
constexpr qsizetype kMostResponseBytes = qsizetype{8} * 1024 * 1024;
constexpr qsizetype kMostFieldBytes = 4096;
constexpr qsizetype kMostOriginLength = 200;
constexpr int kSweepMs = 250;

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

QString noPermission()
{
    return QStringLiteral("GS-API-100 this extension may not accept connections: its manifest does not ask for the "
                          "permission \"network.listen\", or local connections are turned off for it");
}

QString invalidWith(const QString& what)
{
    return QStringLiteral("GS-API-101 ") + what;
}

QString cannotListen(const QString& why)
{
    return QStringLiteral("GS-API-102 ") + why;
}

QByteArray reasonOf(int status)
{
    switch (status) {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 202:
        return "Accepted";
    case 204:
        return "No Content";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 304:
        return "Not Modified";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 408:
        return "Request Timeout";
    case 409:
        return "Conflict";
    case 413:
        return "Content Too Large";
    case 429:
        return "Too Many Requests";
    case 431:
        return "Request Header Fields Too Large";
    case 500:
        return "Internal Server Error";
    case 501:
        return "Not Implemented";
    case 503:
        return "Service Unavailable";
    case 504:
        return "Gateway Timeout";
    default:
        return "Status";
    }
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
    return value.size() <= kMostFieldBytes && std::ranges::none_of(value, [](QChar c) {
               return c.unicode() < 0x20 && c != u'\t';
           });
}

std::optional<QString> originOf(const QString& text)
{
    const QUrl url(text, QUrl::StrictMode);
    if (text.size() > kMostOriginLength || !url.isValid() || (url.scheme() != u"http" && url.scheme() != u"https") ||
        url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() ||
        (!url.path().isEmpty() && url.path() != u"/")) {
        return std::nullopt;
    }
    QString origin = url.scheme() + QStringLiteral("://") + url.host().toLower();
    if (url.port() != -1) {
        origin += u':' + QString::number(url.port());
    }
    return origin;
}

script::ServiceAnswer refused(const QString& message)
{
    const std::string full = message.toStdString();
    const std::size_t space = full.find(' ');
    return {"null", full.substr(0, space), space == std::string::npos ? std::string() : full.substr(space + 1)};
}

}

struct LocalServers::Shared {
    mutable std::mutex mutex;
    LocalServers* owner = nullptr;
    std::set<QString> permitted;

    void post(std::function<void(LocalServers&)> work) const
    {
        const std::scoped_lock lock(mutex);
        if (owner == nullptr) {
            return;
        }
        LocalServers* target = owner;
        QMetaObject::invokeMethod(target, [target, work = std::move(work)] { work(*target); }, Qt::QueuedConnection);
    }

    [[nodiscard]] bool allowed(const QString& extension) const
    {
        const std::scoped_lock lock(mutex);
        return extension.isEmpty() || permitted.contains(extension);
    }
};

struct LocalServers::Server {
    std::uint64_t logic = 0;
    QString owner;
    quint16 port = 0;
    std::vector<QString> origins;
    std::unique_ptr<QTcpServer> listener;
    quint64 lastRequest = 0;
};

struct LocalServers::Connection {
    QString handle;
    QByteArray buffer;
    std::chrono::steady_clock::time_point deadline;
    quint64 request = 0;
    QByteArray method;
    QByteArray allowOrigin;
    bool done = false;
};

LocalServers::LocalServers(QObject* parent)
    : QObject(parent)
    , m_shared(std::make_shared<Shared>())
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
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
    connect(&m_sweep, &QTimer::timeout, this, &LocalServers::sweep);
}

LocalServers::~LocalServers()
{
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->owner = nullptr;
    }
    if (m_offered) {
        for (const std::string_view name : {kRespond, kClose, kListen}) {
            LogicServices::instance().withdraw(name);
        }
    }
    std::map<QTcpSocket*, Connection> connections;
    connections.swap(m_connections);
    for (const auto& [socket, connection] : connections) {
        socket->disconnect(this);
        socket->abort();
    }
    m_servers.clear();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

LocalServers* LocalServers::create(QQmlEngine*, QJSEngine*)
{
    if (s_instance == nullptr) {
        auto* made = new LocalServers(nullptr);
        QJSEngine::setObjectOwnership(made, QJSEngine::JavaScriptOwnership);
        return made;
    }
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void LocalServers::offerServices()
{
    LogicServices& services = LogicServices::instance();
    for (const std::string_view name : {kRespond, kClose}) {
        services.offerCall(std::string(name), [shared = m_shared, name](const LogicCaller& caller, std::string_view json) {
            return callWith(shared, caller, name, json);
        });
    }
    services.offerRequest(
        std::string(kListen),
        [this](const LogicCaller& caller, quint64 id, const QString& json) { request(caller, kListen, id, json); },
        [](const LogicCaller&, quint64) {});
    connect(&services, &LogicServices::logicEnded, this, [this](quint64 logic) { endLogic(logic); });
    m_offered = true;
}

void LocalServers::setReplies(LocalReplies replies)
{
    m_replies = std::move(replies);
}

void LocalServers::setPermitted(const QStringList& extensions)
{
    const std::set<QString> next(extensions.begin(), extensions.end());
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->permitted = next;
    }
    std::vector<QString> closing;
    for (const auto& [handle, server] : m_servers) {
        if (!server.owner.isEmpty() && !next.contains(server.owner)) {
            closing.push_back(handle);
        }
    }
    for (const QString& handle : closing) {
        drop(handle, QStringLiteral("forbidden"));
    }
}

void LocalServers::setLimits(std::chrono::milliseconds read, std::chrono::milliseconds answer)
{
    m_readLimit = read;
    m_answerLimit = answer;
}

void LocalServers::follow(ExtensionHost& extensions)
{
    const auto update = [this, &extensions] {
        QStringList permitted;
        for (const ExtensionHost::Extension& extension : extensions.extensions()) {
            if (extension.active && extension.manifest && extensions.localConnections(extension.manifest->id)) {
                permitted.append(QString::fromStdString(extension.manifest->id));
            }
        }
        setPermitted(permitted);
    };
    update();
    connect(&extensions, &ExtensionHost::extensionsChanged, this, update);
}

void LocalServers::request(const LogicCaller& caller, std::string_view service, quint64 id, const QString& json)
{
    if (service == kListen) {
        listen(caller, id, json);
        return;
    }
    if (m_replies.refuse) {
        m_replies.refuse(caller.logic, id,
                         QStringLiteral("GS-API-070 no part of GameScrcpy offers the service %1").arg(fromView(service)));
    }
}

script::ServiceAnswer LocalServers::call(const LogicCaller& caller, std::string_view service, std::string_view json)
{
    return callWith(m_shared, caller, service, json);
}

script::ServiceAnswer LocalServers::callWith(const std::shared_ptr<Shared>& shared, const LogicCaller& caller,
                                             std::string_view service, std::string_view json)
{
    if (!shared->allowed(caller.extension)) {
        return refused(noPermission());
    }
    const QJsonObject body = objectOf(QByteArray(json.data(), static_cast<qsizetype>(json.size())));
    const QString handle = body.value(u"handle").toString();
    if (service == kClose) {
        shared->post([logic = caller.logic, handle](LocalServers& self) { self.close(logic, handle); });
        return {"null", {}, {}};
    }
    if (service != kRespond) {
        return {"null", "GS-API-070", "no part of GameScrcpy answers the service " + std::string(service) + " at once"};
    }
    const double request = body.value(u"request").toDouble(-1);
    const double status = body.value(u"status").toDouble(200);
    if (!(request >= 1) || std::floor(request) != request || !(status >= 100 && status <= 599) ||
        std::floor(status) != status) {
        return refused(invalidWith(QStringLiteral("the status of an answer is a whole number from 100 to 599")));
    }
    std::vector<std::pair<QByteArray, QByteArray>> headers;
    const QJsonObject given = body.value(u"headers").toObject();
    if (given.size() > kMostResponseHeaders) {
        return refused(invalidWith(QStringLiteral("an answer has at most 32 headers")));
    }
    for (auto it = given.begin(); it != given.end(); ++it) {
        const QString name = it.key();
        const QString value = it.value().toString();
        const QString lower = name.toLower();
        if (!tokenName(name) || !it.value().isString() || !fieldValue(value)) {
            return refused(invalidWith(QStringLiteral("the header %1 has a name or a value that HTTP does not allow").arg(name)));
        }
        if (lower == u"content-length" || lower == u"connection" || lower == u"transfer-encoding" ||
            lower.startsWith(u"access-control-allow-origin")) {
            continue;
        }
        headers.emplace_back(name.toLatin1(), value.toUtf8());
    }
    const QByteArray::FromBase64Result decoded = QByteArray::fromBase64Encoding(
        body.value(u"body").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (decoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok || decoded.decoded.size() > kMostResponseBytes) {
        return refused(invalidWith(QStringLiteral("the body of an answer is at most 8 MiB")));
    }
    shared->post([logic = caller.logic, handle, request = static_cast<quint64>(request), status = static_cast<int>(status),
                  headers = std::move(headers), data = decoded.decoded](LocalServers& self) {
        self.respond(logic, handle, request, status, headers, data);
    });
    return {"null", {}, {}};
}

void LocalServers::listen(const LogicCaller& caller, quint64 id, const QString& json)
{
    const auto refuse = [this, &caller, id](const QString& message) {
        if (m_replies.refuse) {
            m_replies.refuse(caller.logic, id, message);
        }
    };
    if (!m_shared->allowed(caller.extension)) {
        refuse(noPermission());
        return;
    }
    const QJsonObject body = objectOf(json.toUtf8());
    const double port = body.value(u"port").toDouble(-1);
    if (!(port >= 1024 && port <= 65535) || std::floor(port) != port) {
        refuse(invalidWith(QStringLiteral("the port is a whole number from 1024 to 65535")));
        return;
    }
    std::vector<QString> origins;
    const QJsonArray listed = body.value(u"origins").toArray();
    if (static_cast<std::size_t>(listed.size()) > kMostOrigins) {
        refuse(invalidWith(QStringLiteral("at most 16 origins may be allowed")));
        return;
    }
    for (const QJsonValue& each : listed) {
        const std::optional<QString> origin = originOf(each.toString());
        if (!each.isString() || !origin) {
            refuse(invalidWith(QStringLiteral("an origin is written as https://host or http://host:port")));
            return;
        }
        origins.push_back(*origin);
    }
    const auto ofOwner = [&caller](const auto& entry) { return entry.second.owner == caller.extension; };
    if (static_cast<std::size_t>(std::ranges::count_if(m_servers, ofOwner)) >= kMostServers) {
        refuse(cannotListen(QStringLiteral("an extension can listen on at most 4 ports")));
        return;
    }
    auto listener = std::make_unique<QTcpServer>();
    listener->setMaxPendingConnections(static_cast<int>(kMostConnections));
    if (!listener->listen(QHostAddress::LocalHost, static_cast<quint16>(port))) {
        refuse(cannotListen(QStringLiteral("port %1 cannot be opened: %2")
                                .arg(static_cast<int>(port))
                                .arg(listener->errorString())));
        return;
    }
    const QString handle = QStringLiteral("n%1").arg(++m_lastHandle);
    connect(listener.get(), &QTcpServer::newConnection, this, [this, handle] { accept(handle); });
    m_servers.emplace(handle, Server{.logic = caller.logic,
                                     .owner = caller.extension,
                                     .port = static_cast<quint16>(port),
                                     .origins = std::move(origins),
                                     .listener = std::move(listener),
                                     .lastRequest = 0});
    if (m_replies.answer) {
        m_replies.answer(caller.logic, id,
                         compactJson(QJsonObject{{QStringLiteral("handle"), handle}, {QStringLiteral("port"), port}}));
    }
    emit portsChanged();
}

void LocalServers::accept(const QString& handle)
{
    const auto found = m_servers.find(handle);
    if (found == m_servers.end()) {
        return;
    }
    while (QTcpSocket* socket = found->second.listener->nextPendingConnection()) {
        const auto open = std::ranges::count_if(m_connections, [&handle](const auto& entry) {
            return entry.second.handle == handle;
        });
        if (static_cast<std::size_t>(open) >= kMostConnections) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        socket->setParent(this);
        m_connections.emplace(socket, Connection{.handle = handle,
                                                 .buffer = {},
                                                 .deadline = std::chrono::steady_clock::now() + m_readLimit,
                                                 .request = 0,
                                                 .method = {},
                                                 .allowOrigin = {},
                                                 .done = false});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readFrom(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { forget(socket); });
        if (!m_sweep.isActive()) {
            m_sweep.start();
        }
        if (socket->bytesAvailable() > 0) {
            readFrom(socket);
        }
    }
}

void LocalServers::readFrom(QTcpSocket* socket)
{
    const auto found = m_connections.find(socket);
    if (found == m_connections.end()) {
        return;
    }
    Connection& connection = found->second;
    if (connection.request != 0 || connection.done) {
        socket->readAll();
        return;
    }
    connection.buffer += socket->readAll();
    const qsizetype end = connection.buffer.indexOf("\r\n\r\n");
    if (end < 0 || end > kMostHeadBytes) {
        if (end > kMostHeadBytes || connection.buffer.size() > kMostHeadBytes) {
            finish(socket, 431, {}, {});
        }
        return;
    }
    const QList<QByteArray> lines = connection.buffer.left(end).split('\n');
    const QList<QByteArray> start = lines.front().trimmed().split(' ');
    if (start.size() != 3 || !start[2].startsWith("HTTP/1.") || !start[1].startsWith('/')) {
        finish(socket, 400, {}, {});
        return;
    }
    QJsonObject headers;
    for (qsizetype index = 1; index < lines.size(); ++index) {
        const QByteArray line = lines[index].trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0) {
            finish(socket, 400, {}, {});
            return;
        }
        const QString name = QString::fromLatin1(line.left(colon).trimmed()).toLower();
        const QString value = QString::fromUtf8(line.mid(colon + 1).trimmed());
        const QString before = headers.value(name).toString();
        headers.insert(name, before.isEmpty() ? value : before + QStringLiteral(", ") + value);
    }
    if (headers.contains(QStringLiteral("transfer-encoding"))) {
        finish(socket, 501, {}, {});
        return;
    }
    qsizetype length = 0;
    if (const QString given = headers.value(u"content-length").toString(); !given.isEmpty()) {
        bool number = false;
        const qlonglong parsed = given.toLongLong(&number);
        if (!number || parsed < 0) {
            finish(socket, 400, {}, {});
            return;
        }
        if (parsed > kMostBodyBytes) {
            finish(socket, 413, {}, {});
            return;
        }
        length = static_cast<qsizetype>(parsed);
    }
    if (connection.buffer.size() - (end + 4) < length) {
        return;
    }
    const auto serverAt = m_servers.find(connection.handle);
    if (serverAt == m_servers.end()) {
        finish(socket, 503, {}, {});
        return;
    }
    Server& server = serverAt->second;
    const QString port = QString::number(server.port);
    const QString host = headers.value(u"host").toString().toLower();
    if (host != QStringLiteral("127.0.0.1:") + port && host != QStringLiteral("localhost:") + port) {
        finish(socket, 403, {}, {});
        return;
    }
    const QString origin = headers.value(u"origin").toString();
    if (!origin.isEmpty()) {
        const std::optional<QString> normal = originOf(origin);
        const bool same = normal == QStringLiteral("http://127.0.0.1:") + port ||
                          normal == QStringLiteral("http://localhost:") + port;
        const bool listed = normal && std::ranges::find(server.origins, *normal) != server.origins.end();
        if (!same && !listed) {
            finish(socket, 403, {}, {});
            return;
        }
        if (listed) {
            connection.allowOrigin = origin.toUtf8();
        }
    }
    const QByteArray method = start[0];
    if (method == "OPTIONS" && !origin.isEmpty()) {
        QByteArray allow = "Access-Control-Allow-Methods: GET, POST, PUT, PATCH, DELETE\r\n"
                           "Access-Control-Max-Age: 600\r\n";
        if (const QString asked = headers.value(u"access-control-request-headers").toString();
            !asked.isEmpty() && fieldValue(asked)) {
            allow += "Access-Control-Allow-Headers: " + asked.toUtf8() + "\r\n";
        }
        finish(socket, 204, allow, {});
        return;
    }
    const QByteArray target = start[1];
    const qsizetype mark = target.indexOf('?');
    QJsonObject query;
    if (mark >= 0) {
        const QUrlQuery items(QString::fromUtf8(target.mid(mark + 1)));
        for (const auto& [key, value] : items.queryItems(QUrl::FullyDecoded)) {
            query.insert(key, value);
        }
    }
    connection.request = ++server.lastRequest;
    connection.method = method;
    connection.deadline = std::chrono::steady_clock::now() + m_answerLimit;
    const QByteArray data = connection.buffer.mid(end + 4, length);
    connection.buffer.clear();
    if (m_replies.tell) {
        m_replies.tell(server.logic, fromView(kEventKind), connection.handle,
                       compactJson(QJsonObject{{QStringLiteral("type"), QStringLiteral("request")},
                                               {QStringLiteral("request"), static_cast<double>(connection.request)},
                                               {QStringLiteral("method"), QString::fromLatin1(method)},
                                               {QStringLiteral("path"),
                                                QUrl::fromPercentEncoding(mark >= 0 ? target.left(mark) : target)},
                                               {QStringLiteral("query"), query},
                                               {QStringLiteral("headers"), headers},
                                               {QStringLiteral("body"), QString::fromLatin1(data.toBase64())}}));
    }
}

void LocalServers::respond(std::uint64_t logic, const QString& handle, quint64 request, int status,
                           const std::vector<std::pair<QByteArray, QByteArray>>& headers, const QByteArray& body)
{
    const auto server = m_servers.find(handle);
    if (server == m_servers.end() || server->second.logic != logic) {
        return;
    }
    for (auto& [socket, connection] : m_connections) {
        if (connection.handle == handle && connection.request == request && !connection.done) {
            QByteArray extra;
            for (const auto& [name, value] : headers) {
                extra += name + ": " + value + "\r\n";
            }
            finish(socket, status, extra, body);
            return;
        }
    }
}

void LocalServers::close(std::uint64_t logic, const QString& handle)
{
    const auto found = m_servers.find(handle);
    if (found != m_servers.end() && found->second.logic == logic) {
        drop(handle, {});
    }
}

void LocalServers::drop(const QString& handle, const QString& reason)
{
    const auto found = m_servers.find(handle);
    if (found == m_servers.end()) {
        return;
    }
    std::vector<QTcpSocket*> sockets;
    for (const auto& [socket, connection] : m_connections) {
        if (connection.handle == handle) {
            sockets.push_back(socket);
        }
    }
    for (QTcpSocket* socket : sockets) {
        m_connections.erase(socket);
        socket->abort();
        socket->deleteLater();
    }
    const std::uint64_t logic = found->second.logic;
    found->second.listener->close();
    m_servers.erase(found);
    if (!reason.isEmpty() && m_replies.tell) {
        m_replies.tell(logic, fromView(kEventKind), handle,
                       compactJson(QJsonObject{{QStringLiteral("type"), QStringLiteral("closed")},
                                               {QStringLiteral("reason"), reason}}));
    }
    emit portsChanged();
}

void LocalServers::finish(QTcpSocket* socket, int status, const QByteArray& headers, const QByteArray& body)
{
    const auto found = m_connections.find(socket);
    if (found == m_connections.end() || found->second.done) {
        return;
    }
    Connection& connection = found->second;
    connection.done = true;
    QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reasonOf(status) + "\r\n";
    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n";
    if (!connection.allowOrigin.isEmpty()) {
        head += "Access-Control-Allow-Origin: " + connection.allowOrigin + "\r\nVary: Origin\r\n";
    }
    head += headers + "\r\n";
    socket->write(head);
    if (connection.method != "HEAD") {
        socket->write(body);
    }
    socket->disconnectFromHost();
}

void LocalServers::forget(QTcpSocket* socket)
{
    if (m_connections.erase(socket) > 0) {
        socket->deleteLater();
    }
    if (m_connections.empty()) {
        m_sweep.stop();
    }
}

void LocalServers::sweep()
{
    const auto now = std::chrono::steady_clock::now();
    std::vector<std::pair<QTcpSocket*, int>> late;
    for (const auto& [socket, connection] : m_connections) {
        if (!connection.done && now >= connection.deadline) {
            late.emplace_back(socket, connection.request == 0 ? 408 : 504);
        }
    }
    for (const auto& [socket, status] : late) {
        finish(socket, status, {}, {});
    }
    if (m_connections.empty()) {
        m_sweep.stop();
    }
}

void LocalServers::endLogic(std::uint64_t logic)
{
    std::vector<QString> closing;
    for (const auto& [handle, server] : m_servers) {
        if (server.logic == logic) {
            closing.push_back(handle);
        }
    }
    for (const QString& handle : closing) {
        drop(handle, {});
    }
}

QVariantMap LocalServers::ports() const
{
    QVariantMap map;
    for (const auto& [handle, server] : m_servers) {
        const QString key = server.owner.isEmpty() ? QStringLiteral("mine") : server.owner;
        QVariantList list = map.value(key).toList();
        list.append(static_cast<int>(server.port));
        map.insert(key, list);
    }
    return map;
}

}
