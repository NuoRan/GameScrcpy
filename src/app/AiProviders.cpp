#include "AiProviders.h"

#include "AiNetwork.h"
#include "ExtensionHost.h"
#include "NativeExtensions.h"
#include "NativeJobs.h"

#include <gs/exthost/Module.h>
#include <gs/platform/AtomicFile.h>

#include <nlohmann/json.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace gs::app {

using namespace Qt::StringLiterals;

namespace {

constexpr qsizetype kMostModelChars = 256;
constexpr std::size_t kSpareArgumentBytes = 1024;

std::string utf8Of(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

std::uint64_t startedJob(const std::string& json)
{
    const nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);
    if (!parsed.is_object() || !parsed.contains("id") || !parsed["id"].is_string()) {
        return 0;
    }
    std::uint64_t id = 0;
    for (const char c : parsed["id"].get_ref<const std::string&>()) {
        if (c < '0' || c > '9') {
            return 0;
        }
        id = id * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return id;
}

QString notAvailable()
{
    return QCoreApplication::translate("AiProviders", "This AI provider is not available now.");
}

class ExtensionProvider final : public QObject, public AiProvider
{
public:
    using Share = std::function<bool(const std::filesystem::path& file, std::string& problem)>;

    ExtensionProvider(AiProviderInfo declared, AiConnection connection, NativeJobs& jobs,
                      std::filesystem::path requests, Share share)
        : m_declared(std::move(declared))
        , m_connection(std::move(connection))
        , m_jobs(&jobs)
        , m_requests(std::move(requests))
        , m_share(std::move(share))
    {
        m_idle.setSingleShot(true); // source-rule-allow R9: an answer that brings nothing for two minutes counts as lost, as with the built-in services
        m_idle.setInterval(kAiIdleLimit);
        connect(&m_idle, &QTimer::timeout, this, [this] { lapsed(); });
    }

    ~ExtensionProvider() override { stop(); }

    ExtensionProvider(const ExtensionProvider&) = delete;
    ExtensionProvider& operator=(const ExtensionProvider&) = delete;

    void send(const AiRequest& request, Handlers handlers) override
    {
        stop();
        m_handlers = std::move(handlers);
        m_text.clear();
        m_thinking.clear();
        m_timer.start();
        if (m_jobs == nullptr) {
            finishWith(AiReply{.problem = notAvailable()});
            return;
        }
        AiRequest asked = request;
        asked.model = m_connection.model;
        asked.think = asked.think || m_connection.think;
        if (asked.effort.isEmpty()) {
            asked.effort = m_connection.effort;
        }
        asked.longContext = asked.longContext || m_connection.longContext;
        const QJsonObject body = aiProviderRequestJson(asked);
        QJsonObject arguments{{u"request"_s, body}};
        std::filesystem::path spilled;
        const QByteArray direct = QJsonDocument(arguments).toJson(QJsonDocument::Compact);
        if (static_cast<std::size_t>(direct.size()) + kSpareArgumentBytes > exthost::kMostParamBytes) {
            spilled = m_requests / (QUuid::createUuid().toString(QUuid::WithoutBraces).toStdWString() + L".json");
            const QByteArray whole = QJsonDocument(body).toJson(QJsonDocument::Compact);
            std::error_code made;
            std::filesystem::create_directories(m_requests, made);
            if (const std::error_code failed = platform::writeFileAtomically(
                    spilled, std::string_view(whole.constData(), static_cast<std::size_t>(whole.size())))) {
                qWarning("ai extension %s: the request could not be written for it (%s)",
                         qUtf8Printable(m_declared.id()), failed.message().c_str());
                finishWith(AiReply{.problem = failure(QString::fromStdString(failed.message()))});
                return;
            }
            if (std::string refused; m_share && !m_share(spilled, refused)) {
                static_cast<void>(platform::removeFile(spilled));
                qWarning("ai extension %s: the request could not be shared with it (%s)",
                         qUtf8Printable(m_declared.id()), refused.c_str());
                finishWith(AiReply{.problem = failure(QString::fromStdString(refused))});
                return;
            }
            arguments = QJsonObject{
                {u"request"_s, QJsonObject{{u"file"_s, QString::fromStdWString(spilled.wstring())}}}};
        }
        const QByteArray started = QJsonDocument(QJsonObject{{u"extension"_s, m_declared.extension},
                                                             {u"job"_s, m_declared.chat},
                                                             {u"args"_s, arguments}})
                                       .toJson(QJsonDocument::Compact);
        const auto live = std::make_shared<bool>(true);
        m_live = live;
        const QPointer<ExtensionProvider> self(this);
        const script::ServiceAnswer answer = m_jobs->startForHost(
            std::string_view(started.constData(), static_cast<std::size_t>(started.size())),
            HostJobEvents{.progress = {},
                          .done =
                              [self, live, spilled](bool worked, const QString& text) {
                                  if (!spilled.empty()) {
                                      static_cast<void>(platform::removeFile(spilled));
                                  }
                                  if (self != nullptr && *live) {
                                      self->ended(worked, text);
                                  }
                              },
                          .output =
                              [self, live](const QString& json) {
                                  if (self != nullptr && *live) {
                                      self->streamed(json);
                                  }
                              }});
        if (!answer.code.empty()) {
            *live = false;
            m_live.reset();
            if (!spilled.empty()) {
                static_cast<void>(platform::removeFile(spilled));
            }
            finishWith(AiReply{.problem = failure(QString::fromStdString(answer.problem)),
                               .code = QString::fromStdString(answer.code)});
            return;
        }
        m_job = startedJob(answer.json);
        m_idle.start();
    }

    void cancel() override
    {
        stop();
        m_handlers = {};
    }

private:
    void stop()
    {
        m_idle.stop();
        if (m_live != nullptr) {
            *m_live = false;
            m_live.reset();
        }
        if (m_job != 0 && m_jobs != nullptr) {
            m_jobs->cancelForHost(m_job);
        }
        m_job = 0;
    }

    void streamed(const QString& json)
    {
        m_idle.start();
        const AiDelta delta = aiProviderDelta(json.toUtf8());
        m_text += delta.text;
        m_thinking += delta.thinking;
        if (!delta.thinking.isEmpty() && m_handlers.thinking) {
            m_handlers.thinking(delta.thinking);
        }
        for (const QString& tool : delta.calling) {
            if (m_handlers.calling) {
                m_handlers.calling(tool);
            }
        }
        if (!delta.text.isEmpty() && m_handlers.text) {
            m_handlers.text(delta.text);
        }
    }

    void ended(bool worked, const QString& text)
    {
        m_idle.stop();
        m_live.reset();
        m_job = 0;
        if (!worked) {
            finishWith(AiReply{.problem = failure(text)});
            return;
        }
        finishWith(aiProviderReply(text.toUtf8(), m_text, m_thinking));
    }

    void lapsed()
    {
        stop();
        const QString late =
            QCoreApplication::translate("AiProviders", "The AI provider %1 did not answer in time. Try again.");
        finishWith(AiReply{.problem = late.arg(m_declared.title)});
    }

    [[nodiscard]] QString failure(const QString& why) const
    {
        return QCoreApplication::translate("AiProviders", "The AI provider %1 failed: %2").arg(m_declared.title, why);
    }

    void finishWith(AiReply answer)
    {
        log(answer);
        Handlers handlers = std::exchange(m_handlers, {});
        if (handlers.done) {
            handlers.done(std::move(answer));
        }
    }

    void log(const AiReply& answer) const
    {
        const QString provider = m_declared.id();
        const auto spent = static_cast<long long>(m_timer.elapsed());
        if (answer.problem.isEmpty()) {
            qInfo("ai extension %s %s: %lld ms, %lld tokens in, %lld out", qUtf8Printable(provider),
                  qUtf8Printable(m_connection.model), spent, static_cast<long long>(answer.usage.input),
                  static_cast<long long>(answer.usage.output));
            return;
        }
        qInfo("ai extension %s %s: %lld ms, failed%s%s", qUtf8Printable(provider), qUtf8Printable(m_connection.model),
              spent, answer.code.isEmpty() ? "" : ": ", qUtf8Printable(answer.code));
    }

    AiProviderInfo m_declared;
    AiConnection m_connection;
    QPointer<NativeJobs> m_jobs;
    std::filesystem::path m_requests;
    Share m_share;
    Handlers m_handlers;
    std::shared_ptr<bool> m_live;
    std::uint64_t m_job = 0;
    QString m_text;
    QString m_thinking;
    QTimer m_idle;
    QElapsedTimer m_timer;
};

}

AiProviderModels aiProviderModelsOf(const QByteArray& json)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        return {.models = {},
                .problem = QCoreApplication::translate("AiProviders", "The extension did not give a list of models.")};
    }
    AiProviderModels listed;
    for (const QJsonValue& value : document.array()) {
        const QJsonObject entry = value.toObject();
        const QString id = entry.value(u"id"_s).toString().trimmed();
        const bool known =
            std::ranges::any_of(listed.models, [&id](const AiProviderModel& model) { return model.id == id; });
        if (id.isEmpty() || id.size() > kMostModelChars || known) {
            continue;
        }
        const QString name = entry.value(u"name"_s).toString().trimmed();
        listed.models.push_back(AiProviderModel{.id = id,
                                                .name = name.isEmpty() ? id : name,
                                                .traits = aiTraitsOf(entry.value(u"traits"_s).toObject())});
        if (listed.models.size() == AiProviders::kMostModels) {
            break;
        }
    }
    return listed;
}

AiProviders::AiProviders(std::filesystem::path requests, QObject* parent)
    : QObject(parent)
    , m_requests(std::move(requests))
{
    std::vector<std::filesystem::path> left;
    std::error_code error;
    for (const std::filesystem::directory_entry& item : std::filesystem::directory_iterator(m_requests, error)) {
        if (item.path().extension() == L".json") {
            left.push_back(item.path());
        }
    }
    for (const std::filesystem::path& file : left) {
        static_cast<void>(platform::removeFile(file));
    }
}

AiProviders::~AiProviders() = default;

void AiProviders::use(NativeExtensions& native, NativeJobs& jobs)
{
    if (m_native != nullptr) {
        disconnect(m_native.data(), nullptr, this, nullptr);
    }
    m_native = &native;
    m_jobs = &jobs;
    connect(&native, &NativeExtensions::changed, this, &AiProviders::providersChanged);
}

void AiProviders::follow(ExtensionHost& extensions)
{
    const auto update = [this, &extensions] {
        std::vector<AiProviderInfo> found;
        for (const ExtensionHost::Extension& extension : extensions.extensions()) {
            if (!extension.active || !extension.manifest || !extension.manifest->contributes.native) {
                continue;
            }
            const ext::Manifest& manifest = *extension.manifest;
            const std::vector<std::string> granted = extensions.grantedPermissions(manifest.id);
            if (std::ranges::find(granted, kPermission) == granted.end()) {
                continue;
            }
            const QString id = QString::fromStdString(manifest.id);
            const QString name = extensions.textOf(manifest, "name", manifest.name);
            for (const ext::NativeAiProvider& declared : manifest.contributes.native->aiProviders) {
                found.push_back(AiProviderInfo{
                    .extension = id,
                    .name = QString::fromStdString(declared.name),
                    .title = extensions.textOf(manifest, "aiProviders." + declared.name + ".title", declared.title),
                    .extensionName = name,
                    .models = QString::fromStdString(declared.models),
                    .chat = QString::fromStdString(declared.chat)});
            }
        }
        setProviders(std::move(found));
    };
    update();
    connect(&extensions, &ExtensionHost::extensionsChanged, this, update);
}

void AiProviders::setProviders(std::vector<AiProviderInfo> providers)
{
    if (providers == m_providers) {
        return;
    }
    m_providers = std::move(providers);
    emit providersChanged();
}

const AiProviderInfo* AiProviders::find(const QString& id) const
{
    const auto found = std::ranges::find_if(m_providers, [&id](const AiProviderInfo& info) { return info.id() == id; });
    return found != m_providers.end() ? &*found : nullptr;
}

AiProviderModels AiProviders::models(const QString& id) const
{
    const AiProviderInfo* found = find(id);
    if (found == nullptr || m_native == nullptr) {
        return {.models = {}, .problem = notAvailable()};
    }
    const script::NodeAnswer answer = m_native->call(utf8Of(found->extension), utf8Of(found->models), {});
    if (!answer.code.empty()) {
        return {.models = {}, .problem = QString::fromStdString(answer.problem)};
    }
    return aiProviderModelsOf(QByteArray::fromStdString(answer.value.text));
}

std::unique_ptr<AiProvider> AiProviders::provider(const QString& id, const AiConnection& connection) const
{
    const AiProviderInfo* found = find(id);
    if (found == nullptr || m_jobs == nullptr) {
        return nullptr;
    }
    const QPointer<NativeExtensions> native = m_native;
    auto share = [native, extension = utf8Of(found->extension)](const std::filesystem::path& file,
                                                                std::string& problem) {
        return native == nullptr || native->shareFile(extension, file, problem);
    };
    return std::make_unique<ExtensionProvider>(*found, connection, *m_jobs, m_requests, std::move(share));
}

}
