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

bool external(const settings::Assistant& chosen)
{
    return chosen.service == settings::AiService::Extension;
}

QString serviceOf(const settings::Assistant& chosen)
{
    return toQString(settings::kAiServiceNames[static_cast<std::size_t>(chosen.service)]);
}

QString extensionService()
{
    return toQString(settings::kAiServiceNames[static_cast<std::size_t>(settings::AiService::Extension)]);
}

settings::AiEndpoint endpointOf(const settings::Assistant& chosen)
{
    if (chosen.service == settings::AiService::Anthropic) {
        return chosen.anthropic;
    }
    if (chosen.service == settings::AiService::OpenAiResponses) {
        return chosen.openaiResponses;
    }
    if (external(chosen)) {
        return settings::AiEndpoint{.address = {},
                                    .model = chosen.extension.model,
                                    .thinking = chosen.extension.thinking,
                                    .effort = chosen.extension.effort,
                                    .longContext = chosen.extension.longContext};
    }
    return chosen.openai;
}

QString placeOf(const settings::Assistant& chosen)
{
    return external(chosen) ? toQString(chosen.extension.provider) : toQString(endpointOf(chosen).address);
}

QString endpointText(const settings::Assistant& chosen)
{
    return serviceOf(chosen) + u'\n' + placeOf(chosen) + u'\n' + toQString(endpointOf(chosen).model);
}

QString modelsKeyOf(const settings::Assistant& chosen)
{
    return serviceOf(chosen) + u'\n' + placeOf(chosen);
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
    return serviceOf(setup());
}

bool AiAssistant::ready() const
{
    return missing().isEmpty();
}

QString AiAssistant::missing() const
{
    const settings::Assistant current = setup();
    const settings::AiEndpoint& endpoint = endpointOf(current);
    if (external(current)) {
        const QString id = toQString(current.extension.provider);
        if (id.isEmpty()) {
            return QCoreApplication::translate("AiAssistant", "Choose the AI provider in Settings > AI.");
        }
        if (chosenProvider() == nullptr) {
            return QCoreApplication::translate("AiAssistant",
                                               "The AI provider %1 in Settings > AI is not available: its extension is "
                                               "not installed, is turned off or may not provide AI.")
                .arg(id);
        }
    }
    if (toQString(endpoint.model).trimmed().isEmpty()) {
        return QCoreApplication::translate("AiAssistant", "Choose the model in Settings > AI.");
    }
    if (external(current)) {
        return {};
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
    return external(setup()) ? QString() : aiDefaultAddress(kind());
}

QString AiAssistant::host() const
{
    const settings::Assistant current = setup();
    if (external(current)) {
        return {};
    }
    const QUrl url = aiEndpoint(kindOf(current), toQString(endpointOf(current).address));
    return url.isValid() ? url.host() : QString();
}

QVariantList AiAssistant::providerList() const
{
    QVariantList list;
    if (m_providers == nullptr) {
        return list;
    }
    for (const AiProviderInfo& info : m_providers->providers()) {
        list.append(QVariantMap{{u"id"_s, info.id()},
                                {u"title"_s, info.title},
                                {u"extension"_s, info.extension},
                                {u"extensionName"_s, info.extensionName}});
    }
    return list;
}

QString AiAssistant::providerId() const
{
    const settings::Assistant current = setup();
    return external(current) ? toQString(current.extension.provider) : QString();
}

QString AiAssistant::providerTitle() const
{
    const AiProviderInfo* found = chosenProvider();
    return found != nullptr ? found->title : providerId();
}

bool AiAssistant::providerMissing() const
{
    return external(setup()) && chosenProvider() == nullptr;
}

QString AiAssistant::unavailable() const
{
    if (!providerMissing()) {
        return {};
    }
    const QString id = providerId();
    if (id.isEmpty()) {
        return u"GS-API-150 logic may not call AI: no AI provider is chosen in Settings > AI"_s;
    }
    return u"GS-API-150 the AI provider %1 chosen in Settings > AI is not available: its extension is not installed, is "
           u"turned off or may not provide AI"_s.arg(id);
}

void AiAssistant::setProviders(AiProviders* providers)
{
    if (m_providers != nullptr) {
        disconnect(m_providers.data(), nullptr, this, nullptr);
    }
    m_providers = providers;
    if (providers != nullptr) {
        connect(providers, &AiProviders::providersChanged, this, &AiAssistant::providersUpdated);
    }
    providersUpdated();
}

bool AiAssistant::chooseProvider(const QString& id)
{
    if (m_providers == nullptr || m_providers->find(id) == nullptr) {
        return false;
    }
    if (toQString(setup().extension.provider) != id) {
        const AiProviderModels listed = m_providers->models(id);
        const QString model = listed.models.empty() ? QString() : listed.models.front().id;
        if (!m_settings.setValue(u"assistant.extension.provider"_s, id) ||
            !m_settings.setValue(u"assistant.extension.model"_s, model)) {
            return false;
        }
    }
    return m_settings.setValue(u"assistant.service"_s, extensionService());
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
    m_test = makeProvider(*target);
    if (m_test == nullptr) {
        setTest(u"failed"_s, missing());
        return;
    }
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
    if (external(current)) {
        m_modelsKey = modelsKeyOf(current);
        const AiProviderModels listed = m_providers != nullptr && chosenProvider() != nullptr
                                            ? m_providers->models(providerId())
                                            : AiProviderModels{.models = {}, .problem = missing()};
        m_models.clear();
        m_modelNames.clear();
        m_providerTraits.clear();
        for (const AiProviderModel& model : listed.models) {
            m_models.append(model.id);
            m_providerTraits.insert_or_assign(model.id, model.traits);
            if (model.name != model.id) {
                m_modelNames.insert(model.id, model.name);
            }
        }
        m_modelsProblem = listed.problem;
        if (m_modelsProblem.isEmpty() && m_models.isEmpty()) {
            m_modelsProblem = QCoreApplication::translate(
                "AiAssistant", "The extension did not list any models. Type the model's name above.");
        }
        m_modelsState = m_modelsProblem.isEmpty() ? u"done"_s : u"failed"_s;
        emit modelsChanged();
        emit setupChanged();
        return;
    }
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
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
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
    if (external(current)) {
        return AiConnection{.kind = AiKind::OpenAi,
                            .url = {},
                            .model = toQString(current.extension.model).trimmed(),
                            .key = {}};
    }
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
    const AiModelTraits found = external(setup()) ? providerTraits(model) : aiModelTraits(kind(), model);
    return {{u"think"_s, found.think},
            {u"alwaysThinks"_s, found.alwaysThinks},
            {u"efforts"_s, found.efforts},
            {u"thinkingEffort"_s, found.thinkingEffort},
            {u"longContext"_s, found.longContext}};
}

void AiAssistant::makeDefault(const QVariantMap& choice)
{
    const AiChoice wanted = aiChoiceOf(choice);
    if (wanted.model.isEmpty() || (!wanted.service.isEmpty() && wanted.service != service()) ||
        (!wanted.provider.isEmpty() && wanted.provider != providerId())) {
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
                    .service = serviceOf(current),
                    .provider = external(current) ? toQString(current.extension.provider) : QString()};
}

std::optional<AiConnection> AiAssistant::chatConnection(const AiChoice& choice) const
{
    std::optional<AiConnection> target = connection();
    if (!target) {
        return std::nullopt;
    }
    const bool own = !choice.model.isEmpty() &&
                     (choice.service.isEmpty() || (choice.service == service() && choice.provider == providerId()));
    const AiChoice picked = own ? choice : defaults();
    const AiChoice wanted =
        external(setup()) ? aiFitted(providerTraits(picked.model), picked) : aiFitted(target->kind, picked);
    target->model = wanted.model;
    target->think = wanted.think;
    target->effort = wanted.effort;
    target->longContext = wanted.longContext;
    return target;
}

std::unique_ptr<AiProvider> AiAssistant::provider(bool chat, const AiChoice& choice)
{
    const std::optional<AiConnection> target = chat ? chatConnection(choice) : connection();
    return target ? makeProvider(*target) : nullptr;
}

std::unique_ptr<AiProvider> AiAssistant::makeProvider(const AiConnection& target)
{
    if (external(setup())) {
        return m_providers != nullptr ? m_providers->provider(providerId(), target) : nullptr;
    }
    return makeAiProvider(m_network, target);
}

void AiAssistant::providersUpdated()
{
    emit providersChanged();
    if (external(setup())) {
        fetchModels();
    }
    emit setupChanged();
}

const AiProviderInfo* AiAssistant::chosenProvider() const
{
    const settings::Assistant current = setup();
    if (!external(current) || m_providers == nullptr) {
        return nullptr;
    }
    return m_providers->find(toQString(current.extension.provider));
}

AiModelTraits AiAssistant::providerTraits(const QString& model) const
{
    const auto found = m_providerTraits.find(model.trimmed());
    return found != m_providerTraits.end() ? found->second : AiModelTraits{};
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
        m_modelNames.clear();
        m_providerTraits.clear();
        m_modelsState.clear();
        m_modelsProblem.clear();
        emit modelsChanged();
        if (external(current)) {
            fetchModels();
        }
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
