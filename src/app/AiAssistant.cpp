#include "AiAssistant.h"

#include "AiNetwork.h"
#include "AppSettings.h"
#include "McpDrafts.h"
#include "McpServer.h"
#include "QtConversions.h"

#include <gs/platform/Credentials.h>

#include <QCoreApplication>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QThread>

#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace gs::app {

using namespace Qt::StringLiterals;

namespace {

QJsonArray textContent(const QString& text)
{
    return QJsonArray{QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, text}}};
}

class McpToolRunner final : public AiToolRunner
{
public:
    explicit McpToolRunner(QObject& context)
        : m_context(&context)
    {
    }

    [[nodiscard]] QJsonArray tools() const override
    {
        const McpServer* server = McpServer::instance();
        return server != nullptr ? server->localTools() : QJsonArray();
    }

    void run(const QString& tool, const QJsonObject& arguments, McpToolHost::Reply reply) override
    {
        McpServer* server = McpServer::instance();
        if (server == nullptr) {
            reply(McpOutcome{.content = textContent(u"GameScrcpy's tools are not available right now."_s),
                             .failed = true,
                             .code = {}});
            return;
        }
        const QPointer<QObject> context = m_context;
        server->runLocal(QString(kAiClient), tool, arguments,
                         [context, answer = std::move(reply)](McpOutcome outcome) {
                             if (context == nullptr) {
                                 return;
                             }
                             if (QThread::currentThread() == context->thread()) {
                                 answer(std::move(outcome));
                                 return;
                             }
                             QMetaObject::invokeMethod(
                                 context.data(),
                                 [answer, moved = std::move(outcome)]() mutable { answer(std::move(moved)); },
                                 Qt::QueuedConnection);
                         });
    }

private:
    QPointer<QObject> m_context;
};

class McpDraftStore final : public AiDraftStore
{
public:
    McpDraftStore()
    {
        McpServer* server = McpServer::instance();
        if (server == nullptr) {
            return;
        }
        McpDrafts* drafts = &server->drafts();
        m_drafts = drafts;
        connect(drafts, &McpDrafts::draftsChanged, this, [this](const QString& client) {
            if (client == kAiClient) {
                emit changed();
            }
        });
    }

    [[nodiscard]] QStringList targets() const override
    {
        return m_drafts != nullptr ? m_drafts->targets(QString(kAiClient)) : QStringList();
    }

    [[nodiscard]] std::vector<AiDraftFile> diff(const QString& target) const override
    {
        std::vector<AiDraftFile> files;
        if (m_drafts == nullptr) {
            return files;
        }
        for (const McpDraftFile& file : m_drafts->diff(QString(kAiClient), target)) {
            files.push_back(AiDraftFile{.path = file.path,
                                        .state = file.state,
                                        .added = file.added,
                                        .removed = file.removed,
                                        .conflict = file.conflict});
        }
        return files;
    }

    [[nodiscard]] QString fileDiff(const QString& target, const QString& path) const override
    {
        return m_drafts != nullptr ? m_drafts->fileDiff(QString(kAiClient), target, path) : QString();
    }

    bool apply(const QString& target, QString& problem) override
    {
        if (m_drafts == nullptr) {
            problem.clear();
            return false;
        }
        return m_drafts->apply(QString(kAiClient), target, problem);
    }

    void discard(const QString& target) override
    {
        if (m_drafts != nullptr) {
            m_drafts->discard(QString(kAiClient), target);
        }
    }

private:
    QPointer<McpDrafts> m_drafts;
};

AiKind kindOf(const settings::Assistant& chosen)
{
    if (chosen.service == settings::AiService::Anthropic) {
        return AiKind::Anthropic;
    }
    if (chosen.service == settings::AiService::OpenAiResponses) {
        return AiKind::OpenAiResponses;
    }
    return AiKind::OpenAi;
}

const settings::AiEndpoint& endpointOf(const settings::Assistant& chosen)
{
    if (chosen.service == settings::AiService::Anthropic) {
        return chosen.anthropic;
    }
    if (chosen.service == settings::AiService::OpenAiResponses) {
        return chosen.openaiResponses;
    }
    return chosen.openai;
}

QString endpointText(const settings::Assistant& chosen)
{
    const settings::AiEndpoint& endpoint = endpointOf(chosen);
    return aiServiceName(kindOf(chosen)) + u'\n' + toQString(endpoint.address) + u'\n' + toQString(endpoint.model);
}

QString modelsKeyOf(const settings::Assistant& chosen)
{
    return aiServiceName(kindOf(chosen)) + u'\n' + toQString(endpointOf(chosen).address);
}

}

AiAssistant::AiAssistant(AppSettings& appSettings, const QString& keyPrefix, QObject* parent)
    : QObject(parent)
    , m_settings(appSettings)
    , m_keyPrefix(keyPrefix)
    , m_tools(std::make_unique<McpToolRunner>(*this))
    , m_drafts(std::make_unique<McpDraftStore>())
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
    aiUseSystemProxy(m_network);
    const settings::Assistant current = setup();
    m_explain = current.explain;
    m_endpoint = endpointText(current);
    m_modelsKey = modelsKeyOf(current);
    m_keyService = service();
    readKey();
    connect(&m_settings, &AppSettings::valuesChanged, this, &AiAssistant::settingsChanged);
}

AiAssistant::~AiAssistant()
{
    m_test.reset();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

AiAssistant* AiAssistant::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

QString AiAssistant::service() const
{
    return aiServiceName(kind());
}

bool AiAssistant::ready() const
{
    return missing().isEmpty();
}

QString AiAssistant::missing() const
{
    const settings::Assistant current = setup();
    const settings::AiEndpoint& endpoint = endpointOf(current);
    if (toQString(endpoint.model).trimmed().isEmpty()) {
        return QCoreApplication::translate("AiAssistant", "Choose the model in Settings > AI.");
    }
    if (!aiEndpoint(kindOf(current), toQString(endpoint.address)).isValid()) {
        return QCoreApplication::translate("AiAssistant", "The address in Settings > AI is not a web address.");
    }
    if (kindOf(current) == AiKind::Anthropic && !m_hasKey) {
        return QCoreApplication::translate("AiAssistant", "Add the API key in Settings > AI.");
    }
    return {};
}

QString AiAssistant::keyTarget() const
{
    return m_keyPrefix + service();
}

QString AiAssistant::defaultAddress() const
{
    return aiDefaultAddress(kind());
}

QString AiAssistant::host() const
{
    const settings::Assistant current = setup();
    const QUrl url = aiEndpoint(kindOf(current), toQString(endpointOf(current).address));
    return url.isValid() ? url.host() : QString();
}

bool AiAssistant::explanationWanted() const
{
    return m_explain && !m_explained;
}

bool AiAssistant::saveKey(const QString& key)
{
    QString cleaned;
    for (const QChar c : key) {
        if (!c.isSpace() && c.isPrint()) {
            cleaned.append(c);
        }
    }
    if (cleaned.isEmpty()) {
        return false;
    }
    const std::error_code error = platform::writeCredential(keyTarget().toStdString(), cleaned.toStdString());
    if (error) {
        m_keyProblem =
            QCoreApplication::translate("AiAssistant", "Windows Credential Manager did not keep the key (error %1).")
                .arg(error.value());
        qWarning("ai: the %s API key was not saved in Windows Credential Manager (error %d)",
                 qUtf8Printable(service()), error.value());
        emit setupChanged();
        return false;
    }
    m_keyProblem.clear();
    readKey();
    m_models.clear();
    m_modelsState.clear();
    m_modelsProblem.clear();
    emit modelsChanged();
    qInfo("ai: saved the %s API key in Windows Credential Manager", qUtf8Printable(service()));
    emit setupChanged();
    return true;
}

bool AiAssistant::forgetKey()
{
    const std::error_code error = platform::deleteCredential(keyTarget().toStdString());
    if (error) {
        m_keyProblem =
            QCoreApplication::translate("AiAssistant", "Windows Credential Manager did not remove the key (error %1).")
                .arg(error.value());
        emit setupChanged();
        return false;
    }
    m_keyProblem.clear();
    readKey();
    m_models.clear();
    m_modelsState.clear();
    m_modelsProblem.clear();
    emit modelsChanged();
    qInfo("ai: removed the %s API key from Windows Credential Manager", qUtf8Printable(service()));
    emit setupChanged();
    return true;
}

void AiAssistant::testConnection()
{
    const std::optional<AiConnection> target = connection();
    if (!target) {
        setTest(u"failed"_s, missing());
        return;
    }
    m_test = makeAiProvider(m_network, *target);
    setTest(u"running"_s, QString());
    const QString model = target->model;
    const QPointer<AiAssistant> self(this);
    AiRequest request{.turns = {AiTurn{.role = AiTurn::Role::User, .text = u"Reply with the word OK."_s}},
                      .stream = false,
                      .maxTokens = 16};
    m_test->send(request, AiProvider::Handlers{.text = {}, .done = [self, model](AiReply reply) {
                                                   if (self == nullptr) {
                                                       return;
                                                   }
                                                   if (reply.problem.isEmpty()) {
                                                       self->setTest(u"passed"_s,
                                                                     QCoreApplication::translate(
                                                                         "AiAssistant", "Connected: %1 answered.")
                                                                         .arg(model));
                                                   } else {
                                                       self->setTest(u"failed"_s, reply.problem);
                                                   }
                                               }});
}

void AiAssistant::fetchModels()
{
    if (m_modelsReply != nullptr) {
        m_modelsReply->abort();
    }
    const settings::Assistant current = setup();
    const AiKind chosen = kindOf(current);
    const settings::AiEndpoint& endpoint = endpointOf(current);
    const QUrl url = aiModelsEndpoint(chosen, toQString(endpoint.address));
    if (!url.isValid()) {
        m_models.clear();
        m_modelsState = u"failed"_s;
        m_modelsProblem =
            QCoreApplication::translate("AiAssistant", "The address is not a web address. Check it above.");
        emit modelsChanged();
        return;
    }
    QString key;
    if (const auto read = platform::readCredential(keyTarget().toStdString())) {
        key = QString::fromStdString(*read);
    }
    QNetworkRequest request(url);
    for (const AiHeader& header : aiHeaders(chosen, key, false)) {
        request.setRawHeader(header.name, header.value);
    }
    request.setTransferTimeout(15000);
    QNetworkReply* reply = m_network.get(request);
    m_modelsReply = reply;
    m_modelsKey = modelsKeyOf(current);
    m_modelsState = u"running"_s;
    m_modelsProblem.clear();
    emit modelsChanged();
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        reply->deleteLater();
        if (m_modelsReply == reply) {
            m_modelsReply = nullptr;
        }
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (status < 200 || status >= 300 || reply->error() != QNetworkReply::NoError) {
            m_modelsState = u"failed"_s;
            m_modelsProblem = status != 0
                                  ? aiFailure(status, body).problem
                                  : aiNetworkProblem(static_cast<int>(reply->error()), url.host(), reply->errorString());
        } else {
            m_models = aiModelsOf(body);
            if (m_models.isEmpty()) {
                m_modelsState = u"failed"_s;
                m_modelsProblem = QCoreApplication::translate(
                    "AiAssistant", "The service did not list any models. Type the model's name above.");
            } else {
                m_modelsState = u"done"_s;
                m_modelsProblem.clear();
            }
        }
        emit modelsChanged();
    });
}

void AiAssistant::acknowledgeExplanation(bool remember)
{
    m_explained = true;
    if (remember) {
        static_cast<void>(m_settings.setValue(u"assistant.explain"_s, false));
    }
    emit explanationChanged();
}

void AiAssistant::openSettings()
{
    emit settingsWanted();
}

std::optional<AiConnection> AiAssistant::connection() const
{
    if (!ready()) {
        return std::nullopt;
    }
    const settings::Assistant current = setup();
    const AiKind chosen = kindOf(current);
    const settings::AiEndpoint& endpoint = endpointOf(current);
    QString key;
    if (const auto read = platform::readCredential(keyTarget().toStdString())) {
        key = QString::fromStdString(*read);
    }
    return AiConnection{.kind = chosen,
                        .url = aiEndpoint(chosen, toQString(endpoint.address)),
                        .model = toQString(endpoint.model).trimmed(),
                        .key = key};
}

QVariantMap AiAssistant::traits(const QString& model) const
{
    const AiModelTraits found = aiModelTraits(kind(), model);
    return {{u"think"_s, found.think},
            {u"alwaysThinks"_s, found.alwaysThinks},
            {u"efforts"_s, found.efforts},
            {u"thinkingEffort"_s, found.thinkingEffort},
            {u"longContext"_s, found.longContext}};
}

void AiAssistant::makeDefault(const QVariantMap& choice)
{
    const AiChoice wanted = aiChoiceOf(choice);
    if (wanted.model.isEmpty() || (!wanted.service.isEmpty() && wanted.service != service())) {
        return;
    }
    const QString prefix = u"assistant."_s + service() + u'.';
    static_cast<void>(m_settings.setValue(prefix + u"model"_s, wanted.model));
    static_cast<void>(m_settings.setValue(prefix + u"thinking"_s, wanted.think));
    static_cast<void>(m_settings.setValue(prefix + u"effort"_s, wanted.effort.isEmpty() ? u"auto"_s : wanted.effort));
    static_cast<void>(m_settings.setValue(prefix + u"longContext"_s, wanted.longContext));
}

AiChoice AiAssistant::defaults() const
{
    const settings::Assistant current = setup();
    const settings::AiEndpoint& endpoint = endpointOf(current);
    const QString effort =
        endpoint.effort == settings::AiEffort::Automatic
            ? QString()
            : toQString(settings::kAiEffortNames[static_cast<std::size_t>(endpoint.effort)]);
    return AiChoice{.model = toQString(endpoint.model).trimmed(),
                    .think = endpoint.thinking,
                    .effort = effort,
                    .longContext = endpoint.longContext,
                    .service = aiServiceName(kindOf(current))};
}

std::optional<AiConnection> AiAssistant::chatConnection(const AiChoice& choice) const
{
    std::optional<AiConnection> target = connection();
    if (!target) {
        return std::nullopt;
    }
    const bool own = !choice.model.isEmpty() && (choice.service.isEmpty() || choice.service == service());
    const AiChoice wanted = aiFitted(target->kind, own ? choice : defaults());
    target->model = wanted.model;
    target->think = wanted.think;
    target->effort = wanted.effort;
    target->longContext = wanted.longContext;
    return target;
}

std::unique_ptr<AiProvider> AiAssistant::provider(bool chat, const AiChoice& choice)
{
    const std::optional<AiConnection> target = chat ? chatConnection(choice) : connection();
    return target ? makeAiProvider(m_network, *target) : nullptr;
}

void AiAssistant::settingsChanged()
{
    const settings::Assistant current = setup();
    if (service() != m_keyService) {
        m_keyService = service();
        m_keyProblem.clear();
        readKey();
    }
    const QString endpoint = endpointText(current);
    if (endpoint != m_endpoint) {
        m_endpoint = endpoint;
        m_test.reset();
        if (!m_testState.isEmpty()) {
            setTest(QString(), QString());
        }
    }
    const QString modelsKey = modelsKeyOf(current);
    if (modelsKey != m_modelsKey) {
        m_modelsKey = modelsKey;
        if (m_modelsReply != nullptr) {
            m_modelsReply->abort();
        }
        m_models.clear();
        m_modelsState.clear();
        m_modelsProblem.clear();
        emit modelsChanged();
    }
    if (current.explain != m_explain) {
        m_explain = current.explain;
        if (m_explain) {
            m_explained = false;
        }
        emit explanationChanged();
    }
    emit setupChanged();
}

void AiAssistant::readKey()
{
    const auto read = platform::readCredential(keyTarget().toStdString());
    m_hasKey = read.has_value() && !read->empty();
}

void AiAssistant::setTest(const QString& state, const QString& message)
{
    m_testState = state;
    m_testMessage = message;
    emit testChanged();
}

settings::Assistant AiAssistant::setup() const
{
    return m_settings.effective({}).assistant;
}

AiKind AiAssistant::kind() const
{
    return kindOf(setup());
}

}
