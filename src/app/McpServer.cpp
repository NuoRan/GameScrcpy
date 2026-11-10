#include "McpServer.h"

#include "AppSettings.h"
#include "McpDrafts.h"
#include "McpGuides.h"
#include "QtConversions.h"

#include <gs/base/BuildInfo.h>

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QList>
#include <QLocalSocket>
#include <QSaveFile>

#include <windows.h>

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace gs::app {

namespace {

constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr auto kDefaultPipe = QLatin1StringView("GameScrcpy-MCP");
constexpr auto kProtocolVersions =
    std::to_array<QLatin1StringView>({QLatin1StringView("2025-06-18"), QLatin1StringView("2025-03-26"),
                                      QLatin1StringView("2024-11-05")});
constexpr auto kInstructions = QLatin1StringView(
    "GameScrcpy mirrors Android phones to this computer, maps keys to touches and runs TypeScript logic on the "
    "phones. Look things up with api_reference first: it holds the script API and the profile format. Positions are "
    "ratios of the phone picture, from (0, 0) at the top left to (1, 1) at the bottom right. Change profiles, "
    "components and tasks in a draft first (write_profile, write_component, write_task, edit_blueprint): drafts need "
    "no confirmation and change nothing the user runs. Verify a draft with check and with run_replay (draft and trace) "
    "on a recording, then call apply_draft, which the user confirms once. Try logic on a recording before running it "
    "on a phone. apply_draft, run_script and start_recording wait until the user confirms in GameScrcpy and count as "
    "refused when the user does not answer within two minutes."
    " When the user's request is vague, ask before writing code: which app, which screen, what should happen and "
    "when, and whether they already have the pictures, colors, regions and points it needs. This server's prompts "
    "walk through common jobs, and its resources hold recipes with complete code. Never guess a color, a region or a "
    "point: call request_materials with the list of pictures, colors, regions and points the logic needs, and the user "
    "frames, picks and boxes them on the phone's picture; what they give lands in this client's draft.");

QString chosenVersion(const QString& asked)
{
    for (const QLatin1StringView version : kProtocolVersions) {
        if (asked == version) {
            return asked;
        }
    }
    return QString(kProtocolVersions.front());
}

QString cleanName(const QString& given)
{
    QString name;
    for (const QChar c : given.simplified()) {
        if (c.isPrint()) {
            name += c;
        }
    }
    if (name.size() > McpServer::kMostClientChars) {
        name.truncate(McpServer::kMostClientChars);
    }
    return name.trimmed();
}

bool fromAnotherComputer(const QLocalSocket& socket)
{
    const auto handle = reinterpret_cast<HANDLE>(socket.socketDescriptor());
    std::array<wchar_t, 256> client{};
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE ||
        GetNamedPipeClientComputerNameW(handle, client.data(), static_cast<ULONG>(client.size() * sizeof(wchar_t))) ==
            FALSE) {
        return false;
    }
    std::array<wchar_t, 256> own{};
    auto length = static_cast<DWORD>(own.size());
    if (GetComputerNameExW(ComputerNameNetBIOS, own.data(), &length) == FALSE) {
        return true;
    }
    return CompareStringOrdinal(client.data(), -1, own.data(), -1, TRUE) != CSTR_EQUAL;
}

bool pipeTaken(const QString& pipe)
{
    const std::wstring path = L"\\\\.\\pipe\\" + pipe.toStdWString();
    if (WaitNamedPipeW(path.c_str(), 1) != FALSE) {
        return true;
    }
    return GetLastError() == ERROR_SEM_TIMEOUT;
}

QString textOf(const QByteArray& bytes)
{
    return QString::fromUtf8(bytes);
}

}

QString mcpPipeName()
{
    const QString overridden = qEnvironmentVariable("GS_MCP_PIPE");
    return overridden.isEmpty() ? QString(kDefaultPipe) : overridden;
}

McpServer::McpServer(AppSettings& settings, McpToolHost& tools, McpDrafts& drafts, const Clock& clock, QString pipe,
                     QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_tools(tools)
    , m_drafts(drafts)
    , m_clock(clock)
    , m_pipe(std::move(pipe))
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
    m_deadline.setSingleShot(true); // source-rule-allow R9: an unanswered question counts as refused after two minutes
    connect(&m_deadline, &QTimer::timeout, this, &McpServer::expire);
    connect(&m_server, &QLocalServer::newConnection, this, &McpServer::accept);
    connect(&m_settings, &AppSettings::valuesChanged, this, &McpServer::apply);
    connect(&m_settings, &AppSettings::mcpClientsChanged, this, &McpServer::clientsChanged);
    connect(&m_drafts, &McpDrafts::draftsChanged, this, [this](const QString&) { emit draftsChanged(); });
    apply();
}

McpServer::~McpServer()
{
    m_deadline.stop();
    std::vector<quint64> calls;
    for (const auto& entry : m_calls) {
        calls.push_back(entry.first);
    }
    m_calls.clear();
    m_pending.clear();
    for (const quint64 call : calls) {
        m_tools.cancel(call);
    }
    for (const auto& session : m_sessions) {
        if (session->socket != nullptr) {
            disconnect(session->socket, nullptr, this, nullptr);
            session->socket->abort();
        }
    }
    m_sessions.clear();
    m_server.close();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

McpServer* McpServer::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

bool McpServer::listening() const
{
    return m_server.isListening();
}

QStringList McpServer::connected() const
{
    QStringList names;
    for (const auto& session : m_sessions) {
        if (session->initialized && session->trust == Trust::Allowed && !names.contains(session->client)) {
            names.append(session->client);
        }
    }
    return names;
}

QStringList McpServer::clients() const
{
    QStringList names;
    for (const std::string& client : m_settings.mcpClients()) {
        names.append(QString::fromStdString(client));
    }
    return names;
}

QString McpServer::approval() const
{
    return m_waiting.value(0);
}

QVariantMap McpServer::confirmation() const
{
    if (m_pending.empty()) {
        return {};
    }
    const Pending& first = m_pending.front();
    QVariantMap shown = first.shown.details;
    shown.insert(QStringLiteral("id"), first.call);
    shown.insert(QStringLiteral("client"), first.client);
    shown.insert(QStringLiteral("kind"), first.shown.kind);
    return shown;
}

void McpServer::approve(const QString& client, bool allowed, bool remember)
{
    const qsizetype index = m_waiting.indexOf(client);
    if (index < 0) {
        return;
    }
    m_waiting.removeAt(index);
    for (const auto& session : m_sessions) {
        if (session->client == client && session->trust == Trust::Waiting) {
            session->trust = allowed ? Trust::Allowed : Trust::Refused;
            if (allowed) {
                send(*session, QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                           {QStringLiteral("method"), QStringLiteral("notifications/tools/list_changed")}});
            }
        }
    }
    if (allowed && remember) {
        std::vector<std::string> kept = m_settings.mcpClients();
        const std::string name = client.toStdString();
        std::erase(kept, name);
        kept.push_back(name);
        while (kept.size() > settings::Mcp::kMaxClients) {
            kept.erase(kept.begin());
        }
        m_settings.setMcpClients(kept);
    }
    qInfo("mcp %s: %s by the user%s", qUtf8Printable(client), allowed ? "allowed" : "refused",
          allowed && remember ? ", remembered" : "");
    emit approvalChanged();
    emit stateChanged();
}

void McpServer::answer(quint64 id, bool allowed, bool quiet)
{
    const auto found = std::ranges::find(m_pending, id, &Pending::call);
    if (found == m_pending.end()) {
        return;
    }
    const std::function<void(McpAnswer)> done = std::move(found->done);
    m_pending.erase(found);
    schedule();
    emit confirmationChanged();
    if (done) {
        done(!allowed ? McpAnswer::Refused : quiet ? McpAnswer::AllowedForAWhile : McpAnswer::Allowed);
    }
    if (allowed && quiet) {
        emit allowedChanged();
    }
}

void McpServer::revoke(const QString& client)
{
    std::vector<std::string> kept = m_settings.mcpClients();
    std::erase(kept, client.toStdString());
    m_settings.setMcpClients(kept);
    std::vector<quint64> dropped;
    for (const auto& session : m_sessions) {
        if (session->client == client) {
            dropped.push_back(session->number);
        }
    }
    for (const quint64 number : dropped) {
        close(number);
    }
    m_tools.forget(client);
    emit allowedChanged();
    m_drafts.discardAll(client);
    qInfo("mcp %s: no longer allowed; %zu connection(s) closed, its drafts removed", qUtf8Printable(client),
          dropped.size());
}

QVariantList McpServer::allowedNow() const
{
    return m_tools.allowedNow();
}

QVariantMap McpServer::draftProfiles() const
{
    QVariantMap shown;
    std::map<QString, QStringList> clients;
    for (const McpDraftFacts& each : m_drafts.pending()) {
        if (each.target.section(u':', 0, 0) != u"profile") {
            continue;
        }
        clients[each.target.section(u':', 1)].append(each.client);
    }
    for (const auto& [id, names] : clients) {
        shown.insert(id, names.join(QStringLiteral(", ")));
    }
    return shown;
}

void McpServer::endAllowed(const QString& client, const QString& key)
{
    m_tools.endAllowed(client, key);
    emit allowedChanged();
}

QJsonArray McpServer::localTools() const
{
    return m_tools.tools();
}

void McpServer::announceTools()
{
    for (const auto& session : m_sessions) {
        if (session->initialized && session->trust == Trust::Allowed) {
            send(*session, QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                       {QStringLiteral("method"), QStringLiteral("notifications/tools/list_changed")}});
        }
    }
}

void McpServer::runLocal(const QString& client, const QString& tool, const QJsonObject& arguments,
                         McpToolHost::Reply reply)
{
    const QJsonArray listed = m_tools.tools();
    bool known = false;
    for (qsizetype index = 0; index < listed.size() && !known; ++index) {
        known = listed.at(index).toObject().value(QStringLiteral("name")).toString() == tool;
    }
    if (!known) {
        qInfo("mcp %s: %s, GS-MCP-003", qUtf8Printable(client), qUtf8Printable(tool));
        const QString text = QStringLiteral("GS-MCP-003 There is no tool named %1; localTools lists them").arg(tool);
        if (reply) {
            reply(McpOutcome{.content = QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                               {QStringLiteral("text"), text}}},
                             .failed = true,
                             .code = QStringLiteral("GS-MCP-003")});
        }
        return;
    }
    const quint64 call = ++m_lastCall;
    m_calls.emplace(call, Running{.client = client, .tool = tool, .local = std::move(reply)});
    McpCall started{.id = call,
                    .client = client,
                    .tool = tool,
                    .arguments = arguments,
                    .confirm = [this, call](McpConfirmation shown, std::function<void(McpAnswer)> done) {
                        confirm(call, std::move(shown), std::move(done));
                    }};
    m_tools.call(started, [this, call](McpOutcome outcome) { finish(call, std::move(outcome)); });
}

QString McpServer::configuration() const
{
    QJsonObject server{
        {QStringLiteral("command"),
         QDir::toNativeSeparators(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("gs.exe")))},
        {QStringLiteral("args"), QJsonArray{QStringLiteral("mcp")}}};
    if (m_pipe != kDefaultPipe) {
        server.insert(QStringLiteral("env"), QJsonObject{{QStringLiteral("GS_MCP_PIPE"), m_pipe}});
    }
    const QJsonObject root{{QStringLiteral("mcpServers"), QJsonObject{{QStringLiteral("gamescrcpy"), server}}}};
    return textOf(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool McpServer::copyConfiguration() const
{
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) == nullptr) {
        return false;
    }
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) {
        return false;
    }
    clipboard->setText(configuration());
    return true;
}

QString McpServer::installSkill() const
{
    const QString text = mcpSkillText();
    QStringList done;
    QStringList failed;
    const QList<std::pair<QString, QString>> targets{{QStringLiteral(".cursor"), QStringLiteral("Cursor")},
                                                     {QStringLiteral(".claude"), QStringLiteral("Claude Code")}};
    for (const auto& [folder, app] : targets) {
        const QString path = QDir::home().filePath(folder + QStringLiteral("/skills/gamescrcpy/SKILL.md"));
        QSaveFile file(path);
        const bool written = QDir().mkpath(QFileInfo(path).absolutePath()) && file.open(QIODevice::WriteOnly) &&
                             file.write(text.toUtf8()) >= 0 && file.commit();
        (written ? done : failed).append(app);
    }
    if (failed.isEmpty()) {
        return tr("Installed for %1.").arg(done.join(tr(" and ", "between two app names")));
    }
    return tr("Could not write the skill for %1.").arg(failed.join(tr(" and ", "between two app names")));
}

bool McpServer::copySkill() const
{
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) == nullptr) {
        return false;
    }
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) {
        return false;
    }
    clipboard->setText(mcpSkillText());
    return true;
}

void McpServer::expire()
{
    const Microseconds now = m_clock.now();
    std::vector<std::function<void(McpAnswer)>> expired;
    for (auto at = m_pending.begin(); at != m_pending.end();) {
        if (at->deadline <= now) {
            qInfo("mcp %s: no answer within two minutes, counted as refused", qUtf8Printable(at->client));
            expired.push_back(std::move(at->done));
            at = m_pending.erase(at);
        } else {
            ++at;
        }
    }
    schedule();
    if (expired.empty()) {
        return;
    }
    emit confirmationChanged();
    for (const auto& done : expired) {
        if (done) {
            done(McpAnswer::Unanswered);
        }
    }
}

void McpServer::apply()
{
    const bool wanted = m_settings.mcpEnabled();
    if (wanted == m_on) {
        return;
    }
    m_on = wanted;
    if (wanted) {
        start();
    } else {
        stop();
    }
}

void McpServer::start()
{
    m_problem.clear();
    if (pipeTaken(m_pipe)) {
        m_problem = tr("Another program already uses the connection point %1, so AI clients cannot reach GameScrcpy")
                        .arg(m_pipe);
        qWarning("mcp: the pipe %s is open in another program; not listening", qUtf8Printable(m_pipe));
        emit stateChanged();
        return;
    }
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (m_server.listen(m_pipe)) {
        qInfo("mcp: listening for AI clients on the pipe %s", qUtf8Printable(m_pipe));
    } else {
        m_problem = tr("AI clients cannot connect: %1").arg(m_server.errorString());
        qWarning("mcp: cannot listen on the pipe %s: %s", qUtf8Printable(m_pipe),
                 qUtf8Printable(m_server.errorString()));
    }
    emit stateChanged();
}

void McpServer::stop()
{
    m_server.close();
    m_problem.clear();
    std::vector<quint64> numbers;
    for (const auto& session : m_sessions) {
        numbers.push_back(session->number);
    }
    for (const quint64 number : numbers) {
        close(number);
    }
    if (!m_waiting.isEmpty()) {
        m_waiting.clear();
        emit approvalChanged();
    }
    qInfo("mcp: stopped listening; %zu AI client connection(s) closed", numbers.size());
    emit stateChanged();
}

void McpServer::accept()
{
    while (QLocalSocket* socket = m_server.nextPendingConnection()) {
        if (fromAnotherComputer(*socket)) {
            qWarning("mcp: refused a connection from another computer");
            socket->abort();
            socket->deleteLater();
            continue;
        }
        auto opened = std::make_unique<Session>();
        opened->number = ++m_lastSession;
        opened->socket = socket;
        const quint64 number = opened->number;
        connect(socket, &QLocalSocket::readyRead, this, [this, number] { readFrom(number); });
        connect(socket, &QLocalSocket::disconnected, this, [this, number] { close(number); });
        m_sessions.push_back(std::move(opened));
        if (socket->bytesAvailable() > 0) {
            readFrom(number);
        }
    }
}

void McpServer::readFrom(quint64 number)
{
    Session* current = session(number);
    if (current == nullptr || current->socket == nullptr) {
        return;
    }
    current->buffer += current->socket->readAll();
    for (;;) {
        Session* live = session(number);
        if (live == nullptr) {
            return;
        }
        const qsizetype newline = live->buffer.indexOf('\n');
        if (newline < 0) {
            if (live->buffer.size() > kMostMessageBytes) {
                fail(*live, QJsonValue::Null, kInvalidRequest, QStringLiteral("Invalid Request: a message is longer than 32 MiB"));
                close(number);
            }
            return;
        }
        QByteArray line = live->buffer.left(newline);
        live->buffer.remove(0, newline + 1);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (line.trimmed().isEmpty()) {
            continue;
        }
        handle(*live, line);
    }
}

void McpServer::handle(Session& session, const QByteArray& line)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError) {
        fail(session, QJsonValue::Null, kParseError, QStringLiteral("Parse error: %1").arg(error.errorString()));
        return;
    }
    if (!document.isObject()) {
        fail(session, QJsonValue::Null, kInvalidRequest,
             QStringLiteral("Invalid Request: send one JSON-RPC object per line; batches are not supported"));
        return;
    }
    const QJsonObject message = document.object();
    const bool request = message.contains(QStringLiteral("id"));
    const QJsonValue id = message.value(QStringLiteral("id"));
    const QJsonValue method = message.value(QStringLiteral("method"));
    if (request && !id.isString() && !id.isDouble()) {
        fail(session, QJsonValue::Null, kInvalidRequest, QStringLiteral("Invalid Request: the id must be a string or a number"));
        return;
    }
    if (!method.isString()) {
        if (request && !message.contains(QStringLiteral("result")) && !message.contains(QStringLiteral("error"))) {
            fail(session, id, kInvalidRequest, QStringLiteral("Invalid Request: the method is missing"));
        }
        return;
    }
    if (message.value(QStringLiteral("jsonrpc")).toString() != u"2.0") {
        if (request) {
            fail(session, id, kInvalidRequest, QStringLiteral("Invalid Request: jsonrpc must be \"2.0\""));
        }
        return;
    }
    const QString name = method.toString();
    const QJsonObject params = message.value(QStringLiteral("params")).toObject();
    if (!request) {
        if (name == u"notifications/cancelled") {
            cancelRequest(session, params.value(QStringLiteral("requestId")));
        }
        return;
    }
    if (name == u"initialize") {
        initialize(session, id, params);
        return;
    }
    if (name == u"ping") {
        reply(session, id, QJsonObject{});
        return;
    }
    if (!session.initialized) {
        fail(session, id, kNotInitialized, QStringLiteral("Send initialize first"));
        return;
    }
    if (session.trust == Trust::Waiting) {
        if (name == u"tools/call") {
            qInfo("mcp %s: %s, GS-MCP-001", qUtf8Printable(session.client),
                  qUtf8Printable(params.value(QStringLiteral("name")).toString()));
        }
        fail(session, id, kRefused,
             QStringLiteral("GS-MCP-001 Waiting for the user to allow this client in GameScrcpy: ask the user to "
                            "answer the question in the GameScrcpy window, then try again"));
        return;
    }
    if (session.trust != Trust::Allowed) {
        if (name == u"tools/call") {
            qInfo("mcp %s: %s, GS-MCP-002", qUtf8Printable(session.client),
                  qUtf8Printable(params.value(QStringLiteral("name")).toString()));
        }
        fail(session, id, kRefused, QStringLiteral("GS-MCP-002 The user refused this client in GameScrcpy"));
        return;
    }
    if (name == u"tools/list") {
        reply(session, id, QJsonObject{{QStringLiteral("tools"), m_tools.tools()}});
        return;
    }
    if (name == u"tools/call") {
        callTool(session, id, params);
        return;
    }
    if (name == u"prompts/list") {
        reply(session, id, QJsonObject{{QStringLiteral("prompts"), mcpPromptList()}});
        return;
    }
    if (name == u"prompts/get") {
        const QString prompt = params.value(QStringLiteral("name")).toString();
        if (const std::optional<QJsonObject> made =
                mcpPrompt(prompt, params.value(QStringLiteral("arguments")).toObject())) {
            reply(session, id, *made);
        } else {
            fail(session, id, kInvalidParams,
                 QStringLiteral("Invalid params: there is no prompt named %1; prompts/list lists them").arg(prompt));
        }
        return;
    }
    if (name == u"resources/list") {
        reply(session, id, QJsonObject{{QStringLiteral("resources"), mcpResourceList()}});
        return;
    }
    if (name == u"resources/read") {
        const QString uri = params.value(QStringLiteral("uri")).toString();
        if (const std::optional<QJsonObject> read = mcpResource(uri)) {
            reply(session, id, *read);
        } else {
            fail(session, id, kInvalidParams,
                 QStringLiteral("Invalid params: there is no resource %1; resources/list lists them").arg(uri));
        }
        return;
    }
    fail(session, id, kMethodNotFound, QStringLiteral("Method not found: %1").arg(name));
}

void McpServer::initialize(Session& session, const QJsonValue& id, const QJsonObject& params)
{
    if (session.initialized) {
        fail(session, id, kInvalidRequest, QStringLiteral("Invalid Request: initialize was already sent on this connection"));
        return;
    }
    const QString client =
        cleanName(params.value(QStringLiteral("clientInfo")).toObject().value(QStringLiteral("name")).toString());
    if (client.isEmpty()) {
        fail(session, id, kInvalidParams, QStringLiteral("Invalid params: clientInfo.name must name the client"));
        return;
    }
    session.initialized = true;
    session.client = client;
    session.trust = remembered(client) ? Trust::Allowed : Trust::Waiting;
    const QJsonObject capabilities{
        {QStringLiteral("tools"), QJsonObject{{QStringLiteral("listChanged"), true}}},
        {QStringLiteral("prompts"), QJsonObject{{QStringLiteral("listChanged"), false}}},
        {QStringLiteral("resources"), QJsonObject{{QStringLiteral("listChanged"), false}}}};
    const QJsonObject server{{QStringLiteral("name"), QStringLiteral("GameScrcpy")},
                             {QStringLiteral("version"), toQString(gs::buildInfo().version)}};
    reply(session, id,
          QJsonObject{{QStringLiteral("protocolVersion"),
                       chosenVersion(params.value(QStringLiteral("protocolVersion")).toString())},
                      {QStringLiteral("capabilities"), capabilities},
                      {QStringLiteral("serverInfo"), server},
                      {QStringLiteral("instructions"), QString(kInstructions)}});
    if (session.trust == Trust::Allowed) {
        qInfo("mcp %s: connected", qUtf8Printable(client));
    } else {
        qInfo("mcp %s: connected, waiting for the user to allow it", qUtf8Printable(client));
        if (!m_waiting.contains(client)) {
            m_waiting.append(client);
            emit approvalChanged();
        }
        emit attentionWanted();
    }
    emit stateChanged();
}

void McpServer::callTool(Session& session, const QJsonValue& id, const QJsonObject& params)
{
    const QString tool = params.value(QStringLiteral("name")).toString();
    const QJsonValue arguments = params.value(QStringLiteral("arguments"));
    if (tool.isEmpty() || !(arguments.isUndefined() || arguments.isNull() || arguments.isObject())) {
        fail(session, id, kInvalidParams,
             QStringLiteral("Invalid params: name must be a tool name and arguments an object"));
        return;
    }
    const QJsonArray listed = m_tools.tools();
    bool known = false;
    for (qsizetype index = 0; index < listed.size() && !known; ++index) {
        known = listed.at(index).toObject().value(QStringLiteral("name")).toString() == tool;
    }
    if (!known) {
        qInfo("mcp %s: %s, GS-MCP-003", qUtf8Printable(session.client), qUtf8Printable(tool));
        fail(session, id, kInvalidParams, QStringLiteral("GS-MCP-003 There is no tool named %1; tools/list lists them").arg(tool));
        return;
    }
    const quint64 call = ++m_lastCall;
    m_calls.emplace(call, Running{.session = session.number, .request = id, .client = session.client, .tool = tool});
    McpCall started{.id = call,
                    .client = session.client,
                    .tool = tool,
                    .arguments = arguments.toObject(),
                    .confirm = [this, call](McpConfirmation shown, std::function<void(McpAnswer)> done) {
                        confirm(call, std::move(shown), std::move(done));
                    }};
    m_tools.call(started, [this, call](McpOutcome outcome) { finish(call, std::move(outcome)); });
}

void McpServer::finish(quint64 call, McpOutcome outcome)
{
    const auto found = m_calls.find(call);
    if (found == m_calls.end()) {
        return;
    }
    const Running running = found->second;
    m_calls.erase(found);
    const auto before = m_pending.size();
    std::erase_if(m_pending, [call](const Pending& pending) { return pending.call == call; });
    if (m_pending.size() != before) {
        schedule();
        emit confirmationChanged();
    }
    const QJsonObject result{{QStringLiteral("content"), outcome.content}, {QStringLiteral("isError"), outcome.failed}};
    const qsizetype size = QJsonDocument(result).toJson(QJsonDocument::Compact).size();
    if (outcome.failed) {
        qInfo("mcp %s: %s, %s", qUtf8Printable(running.client), qUtf8Printable(running.tool),
              qUtf8Printable(outcome.code.isEmpty() ? QStringLiteral("error") : outcome.code));
    } else {
        qInfo("mcp %s: %s, %lld bytes", qUtf8Printable(running.client), qUtf8Printable(running.tool),
              static_cast<long long>(size));
    }
    if (running.local) {
        running.local(std::move(outcome));
        return;
    }
    if (Session* target = session(running.session)) {
        reply(*target, running.request, result);
    }
}

void McpServer::cancelRequest(Session& session, const QJsonValue& request)
{
    const auto found = std::ranges::find_if(m_calls, [&](const auto& entry) {
        return entry.second.session == session.number && entry.second.request == request;
    });
    if (found == m_calls.end()) {
        return;
    }
    dropCall(found->first);
}

void McpServer::confirm(quint64 call, McpConfirmation shown, std::function<void(McpAnswer)> done)
{
    const auto found = m_calls.find(call);
    if (found == m_calls.end()) {
        if (done) {
            done(McpAnswer::Refused);
        }
        return;
    }
    m_pending.push_back(Pending{.call = call,
                                .client = found->second.client,
                                .shown = std::move(shown),
                                .deadline = m_clock.now() + std::chrono::duration_cast<Microseconds>(kAnswerLimit),
                                .done = std::move(done)});
    schedule();
    emit confirmationChanged();
    emit attentionWanted();
}

void McpServer::dropCall(quint64 call)
{
    const auto found = m_calls.find(call);
    if (found != m_calls.end()) {
        qInfo("mcp %s: %s, cancelled", qUtf8Printable(found->second.client), qUtf8Printable(found->second.tool));
        m_calls.erase(found);
    }
    const auto before = m_pending.size();
    std::erase_if(m_pending, [call](const Pending& pending) { return pending.call == call; });
    if (m_pending.size() != before) {
        schedule();
        emit confirmationChanged();
    }
    m_tools.cancel(call);
}

void McpServer::close(quint64 number)
{
    const auto found = std::ranges::find(m_sessions, number, [](const auto& each) { return each->number; });
    if (found == m_sessions.end()) {
        return;
    }
    const std::unique_ptr<Session> closed = std::move(*found);
    m_sessions.erase(found);
    std::vector<quint64> calls;
    for (const auto& [call, running] : m_calls) {
        if (running.session == number) {
            calls.push_back(call);
        }
    }
    for (const quint64 call : calls) {
        dropCall(call);
    }
    if (closed->socket != nullptr) {
        disconnect(closed->socket, nullptr, this, nullptr);
        closed->socket->abort();
        closed->socket->deleteLater();
    }
    if (closed->initialized) {
        qInfo("mcp %s: disconnected", qUtf8Printable(closed->client));
    }
    if (closed->trust == Trust::Waiting) {
        forgetWaiting(closed->client);
    }
    emit stateChanged();
}

void McpServer::send(Session& session, const QJsonObject& message)
{
    if (session.socket == nullptr) {
        return;
    }
    QByteArray bytes = QJsonDocument(message).toJson(QJsonDocument::Compact);
    bytes += '\n';
    session.socket->write(bytes);
}

void McpServer::reply(Session& session, const QJsonValue& id, const QJsonValue& result)
{
    send(session, QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                              {QStringLiteral("id"), id},
                              {QStringLiteral("result"), result}});
}

void McpServer::fail(Session& session, const QJsonValue& id, int code, const QString& message)
{
    send(session, QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                              {QStringLiteral("id"), id},
                              {QStringLiteral("error"),
                               QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}});
}

void McpServer::schedule()
{
    if (m_pending.empty()) {
        m_deadline.stop();
        return;
    }
    const Microseconds next =
        std::ranges::min_element(m_pending, {}, [](const Pending& pending) { return pending.deadline; })->deadline;
    const Microseconds left = std::max(next - m_clock.now(), Microseconds{0});
    m_deadline.start(std::chrono::ceil<std::chrono::milliseconds>(left));
}

void McpServer::forgetWaiting(const QString& client)
{
    const bool stillWaiting = std::ranges::any_of(m_sessions, [&client](const auto& session) {
        return session->client == client && session->trust == Trust::Waiting;
    });
    if (!stillWaiting && m_waiting.removeOne(client)) {
        emit approvalChanged();
    }
}

McpServer::Session* McpServer::session(quint64 number)
{
    const auto found = std::ranges::find(m_sessions, number, [](const auto& each) { return each->number; });
    return found != m_sessions.end() ? found->get() : nullptr;
}

bool McpServer::remembered(const QString& client) const
{
    return std::ranges::find(m_settings.mcpClients(), client.toStdString()) != m_settings.mcpClients().end();
}

}
