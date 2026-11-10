#pragma once

#include "McpServer.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace gs::app {

enum class AiKind : std::uint8_t { OpenAi, Anthropic, OpenAiResponses };

struct AiToolCall {
    QString id;
    QString name;
    QJsonObject arguments;
    QString problem;
};

struct AiToolResult {
    QString id;
    QString name;
    QJsonArray content;
    bool failed = false;
};

struct AiTurn {
    enum class Role : std::uint8_t { User, Assistant, Tools };

    Role role = Role::User;
    QString text;
    std::vector<AiToolCall> calls;
    std::vector<AiToolResult> results;
    QJsonArray pictures;
    QJsonArray thinkingBlocks;
};

struct AiRequest {
    static constexpr int kMostPictures = 12;
    static constexpr int kAnthropicMaxTokens = 8192;
    static constexpr int kThinkingBudget = 4096;

    QString model;
    QString system;
    std::vector<AiTurn> turns;
    QJsonArray tools;
    bool stream = true;
    int maxTokens = kAnthropicMaxTokens;
    bool pictures = true;
    bool think = false;
    QString effort;
    bool longContext = false;
};

struct AiModelTraits {
    bool think = false;
    bool alwaysThinks = false;
    QStringList efforts;
    bool thinkingEffort = false;
    bool longContext = false;

    friend bool operator==(const AiModelTraits&, const AiModelTraits&) = default;
};

struct AiChoice {
    QString model;
    bool think = false;
    QString effort;
    bool longContext = false;
    QString service;

    friend bool operator==(const AiChoice&, const AiChoice&) = default;
};

struct AiUsage {
    qint64 input = 0;
    qint64 output = 0;
};

struct AiReply {
    QString text;
    QString thinking;
    QJsonArray thinkingBlocks;
    std::vector<AiToolCall> calls;
    QString stop;
    AiUsage usage;
    int status = 0;
    QString problem;
    QString code;
};

struct AiHeader {
    QByteArray name;
    QByteArray value;
};

struct AiConnection {
    AiKind kind = AiKind::OpenAi;
    QUrl url;
    QString model;
    QString key;
    bool think = false;
    QString effort;
    bool longContext = false;
};

class AiProvider
{
public:
    struct Handlers {
        std::function<void(const QString& delta)> text;
        std::function<void(AiReply reply)> done;
        std::function<void(const QString& delta)> thinking;
        std::function<void(const QString& tool)> calling;
    };

    virtual ~AiProvider() = default;

    virtual void send(const AiRequest& request, Handlers handlers) = 0;
    virtual void cancel() = 0;
};

class AiToolRunner
{
public:
    virtual ~AiToolRunner() = default;

    [[nodiscard]] virtual QJsonArray tools() const = 0;
    virtual void run(const QString& tool, const QJsonObject& arguments, McpToolHost::Reply reply) = 0;
};

[[nodiscard]] QString aiServiceName(AiKind kind);
[[nodiscard]] QString aiDefaultAddress(AiKind kind);
[[nodiscard]] QUrl aiEndpoint(AiKind kind, const QString& address);
[[nodiscard]] QUrl aiModelsEndpoint(AiKind kind, const QString& address);
[[nodiscard]] QStringList aiModelsOf(const QByteArray& body);
[[nodiscard]] AiModelTraits aiModelTraits(AiKind kind, const QString& model);
[[nodiscard]] AiChoice aiFitted(AiKind kind, AiChoice choice);
[[nodiscard]] QVariantMap aiChoiceMap(const AiChoice& choice);
[[nodiscard]] AiChoice aiChoiceOf(const QVariantMap& map);
[[nodiscard]] int aiThinkingBudget(const QString& effort);
[[nodiscard]] std::vector<AiHeader> aiHeaders(AiKind kind, const QString& key, bool stream, bool longContext = false);
[[nodiscard]] QJsonObject aiRequestJson(AiKind kind, const AiRequest& request);
[[nodiscard]] int aiPictureCount(const AiRequest& request);
[[nodiscard]] AiReply aiReplyOf(AiKind kind, const QByteArray& body);
[[nodiscard]] AiReply aiFailure(int status, const QByteArray& body);
[[nodiscard]] QString aiNetworkProblem(int error, const QString& host, const QString& detail);
[[nodiscard]] QString aiUnreadable();
[[nodiscard]] QString aiContentText(const QJsonArray& content);
[[nodiscard]] QVariantList aiBlocks(const QString& text);

struct AiSseEvent {
    QByteArray name;
    QByteArray data;
};

class AiSseParser
{
public:
    static constexpr qsizetype kMostLineBytes = qsizetype{16} << 20;

    void feed(QByteArrayView bytes);
    void end();
    [[nodiscard]] std::vector<AiSseEvent> take();
    [[nodiscard]] bool broken() const noexcept { return m_broken; }

private:
    void line(QByteArrayView text);
    void dispatch();

    QByteArray m_pending;
    QByteArray m_name;
    QByteArray m_data;
    bool m_hasData = false;
    bool m_broken = false;
    std::vector<AiSseEvent> m_events;
};

struct AiDelta {
    QString text;
    QString thinking;
    QStringList calling;
};

struct AiThinkSplit {
    QString thinking;
    QString text;
};

[[nodiscard]] AiThinkSplit aiSplitThinkTags(const QString& content);

class AiStream
{
public:
    explicit AiStream(AiKind kind)
        : m_kind(kind)
    {
    }

    AiDelta feed(const AiSseEvent& event);
    [[nodiscard]] bool complete() const noexcept { return m_done || !m_stop.isEmpty(); }
    [[nodiscard]] AiReply reply() const;

private:
    struct Partial {
        QString id;
        QString name;
        QString json;
        QJsonObject start;
    };

    enum class Tag : std::uint8_t { Unknown, Inside, Outside };

    AiDelta openAi(const QJsonObject& chunk);
    AiDelta anthropic(const QJsonObject& data);
    AiDelta responses(const QJsonObject& data);
    void content(const QString& piece, AiDelta& delta);
    void thought(const QString& piece, AiDelta& delta);
    void fail(const QJsonValue& error);

    AiKind m_kind;
    QString m_text;
    QString m_thinking;
    QString m_held;
    Tag m_tag = Tag::Unknown;
    std::map<int, QJsonObject> m_thinkingBlocks;
    std::map<int, Partial> m_calls;
    QString m_stop;
    AiUsage m_usage;
    QString m_problem;
    QString m_code;
    bool m_done = false;
};

}
