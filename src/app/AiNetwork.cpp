#include "AiNetwork.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QList>
#include <QMetaEnum>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <utility>

namespace gs::app {

using namespace Qt::StringLiterals;

namespace {

constexpr qsizetype kMostBodyBytes = qsizetype{8} << 20;

class SystemProxies final : public QNetworkProxyFactory
{
public:
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery& query) override { return systemProxyForQuery(query); }
};

class HttpProvider final : public QObject, public AiProvider
{
public:
    HttpProvider(QNetworkAccessManager& network, AiConnection connection)
        : m_network(&network)
        , m_connection(std::move(connection))
    {
    }

    ~HttpProvider() override { drop(); }

    HttpProvider(const HttpProvider&) = delete;
    HttpProvider& operator=(const HttpProvider&) = delete;

    void send(const AiRequest& request, Handlers handlers) override
    {
        drop();
        m_handlers = std::move(handlers);
        m_parser = AiSseParser();
        m_stream.emplace(m_connection.kind);
        m_body.clear();
        m_mode = Mode::Unknown;
        m_timer.start();
        if (m_network == nullptr) {
            AiReply missing{.problem = aiNetworkProblem(QNetworkReply::UnknownNetworkError, m_connection.url.host(),
                                                        QString())};
            finishWith(std::move(missing));
            return;
        }
        AiRequest asked = request;
        asked.model = m_connection.model;
        asked.think = asked.think || m_connection.think;
        if (asked.effort.isEmpty()) {
            asked.effort = m_connection.effort;
        }
        asked.longContext = asked.longContext || m_connection.longContext;
        QNetworkRequest http(m_connection.url);
        for (const AiHeader& header :
             aiHeaders(m_connection.kind, m_connection.key, asked.stream, asked.longContext)) {
            http.setRawHeader(header.name, header.value);
        }
        http.setTransferTimeout(kAiIdleLimit);
        http.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        http.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        http.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
        const QByteArray body =
            QJsonDocument(aiRequestJson(m_connection.kind, asked)).toJson(QJsonDocument::Compact);
        QNetworkReply* reply = m_network->post(http, body);
        m_reply = reply;
        connect(reply, &QNetworkReply::readyRead, this, [this] {
            if (m_reply != nullptr) {
                take(*m_reply, m_reply->readAll());
            }
        });
        connect(reply, &QNetworkReply::finished, this, [this] { finish(); });
    }

    void cancel() override
    {
        drop();
        m_handlers = {};
    }

private:
    enum class Mode : std::uint8_t { Unknown, Events, Whole };

    void drop()
    {
        if (m_reply == nullptr) {
            return;
        }
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }

    static int statusOf(const QNetworkReply& reply)
    {
        return reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    }

    static Mode modeOf(const QNetworkReply& reply, const QByteArray& first)
    {
        const int status = statusOf(reply);
        if (status < 200 || status >= 300) {
            return Mode::Whole;
        }
        if (reply.rawHeader("Content-Type"_ba).toLower().contains("event-stream")) {
            return Mode::Events;
        }
        const QByteArray start = first.trimmed();
        if (start.isEmpty()) {
            return Mode::Unknown;
        }
        if (start.startsWith("data:") || start.startsWith("event:") || start.startsWith(':')) {
            return Mode::Events;
        }
        return Mode::Whole;
    }

    void take(const QNetworkReply& reply, const QByteArray& bytes)
    {
        if (m_mode == Mode::Unknown) {
            m_mode = modeOf(reply, bytes);
        }
        switch (m_mode) {
        case Mode::Events:
            m_parser.feed(bytes);
            deliver();
            break;
        case Mode::Whole:
        case Mode::Unknown:
            if (m_body.size() < kMostBodyBytes) {
                m_body.append(bytes.left(kMostBodyBytes - m_body.size()));
            }
            break;
        }
    }

    void deliver()
    {
        for (const AiSseEvent& event : m_parser.take()) {
            const AiDelta delta = m_stream->feed(event);
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
    }

    void finish()
    {
        if (m_reply == nullptr) {
            return;
        }
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        take(*reply, reply->readAll());
        const int status = statusOf(*reply);
        const QNetworkReply::NetworkError error = reply->error();
        const QString detail = reply->errorString();
        reply->deleteLater();
        const QString host = m_connection.url.host();
        AiReply answer;
        if (status == 0) {
            answer.problem = aiNetworkProblem(error, host, detail);
            answer.code = QString::fromLatin1(QMetaEnum::fromType<QNetworkReply::NetworkError>().valueToKey(error));
        } else if (status < 200 || status >= 300) {
            answer = aiFailure(status, m_body);
        } else if (m_mode == Mode::Events) {
            m_parser.end();
            deliver();
            answer = m_stream->reply();
            if (error != QNetworkReply::NoError && !m_stream->complete()) {
                answer.problem = aiNetworkProblem(error, host, detail);
            }
        } else if (m_body.trimmed().isEmpty() && error != QNetworkReply::NoError) {
            answer.problem = aiNetworkProblem(error, host, detail);
        } else {
            answer = aiReplyOf(m_connection.kind, m_body);
        }
        answer.status = status;
        finishWith(std::move(answer));
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
        const QString service = aiServiceName(m_connection.kind);
        const auto spent = static_cast<long long>(m_timer.elapsed());
        if (answer.problem.isEmpty()) {
            qInfo("ai %s %s: %lld ms, %lld tokens in, %lld out", qUtf8Printable(service),
                  qUtf8Printable(m_connection.model), spent, static_cast<long long>(answer.usage.input),
                  static_cast<long long>(answer.usage.output));
            return;
        }
        QStringList reason;
        if (answer.status != 0 && (answer.status < 200 || answer.status >= 300)) {
            reason.append(u"HTTP %1"_s.arg(answer.status));
        }
        if (!answer.code.isEmpty()) {
            reason.append(answer.code);
        }
        if (reason.isEmpty()) {
            reason.append(answer.status == 0 ? u"no connection"_s : u"unreadable or cut off"_s);
        }
        qInfo("ai %s %s: %lld ms, failed: %s", qUtf8Printable(service), qUtf8Printable(m_connection.model), spent,
              qUtf8Printable(reason.join(u", "_s)));
    }

    QPointer<QNetworkAccessManager> m_network;
    AiConnection m_connection;
    QPointer<QNetworkReply> m_reply;
    Handlers m_handlers;
    AiSseParser m_parser;
    std::optional<AiStream> m_stream;
    QByteArray m_body;
    Mode m_mode = Mode::Unknown;
    QElapsedTimer m_timer;
};

}

void aiUseSystemProxy(QNetworkAccessManager& network)
{
    network.setProxyFactory(new SystemProxies);
}

std::unique_ptr<AiProvider> makeAiProvider(QNetworkAccessManager& network, AiConnection connection)
{
    return std::make_unique<HttpProvider>(network, std::move(connection));
}

}
