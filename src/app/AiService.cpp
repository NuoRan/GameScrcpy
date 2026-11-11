#include "AiService.h"

#include "QtConversions.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <utility>

namespace gs::app {

using namespace Qt::StringLiterals;

namespace {

constexpr qsizetype kMostToolChars = 100'000;
constexpr qsizetype kMostServiceChars = 400;
constexpr qsizetype kMostQuotedArguments = 200;

QJsonObject textBlock(const QString& text)
{
    return {{u"type"_s, u"text"_s}, {u"text"_s, text}};
}

QString bareModel(const QString& model)
{
    QString name = model.trimmed().toLower();
    if (const qsizetype slash = name.lastIndexOf(u'/'); slash >= 0) {
        name = name.mid(slash + 1);
    }
    name.replace(u'.', u'-');
    return name;
}

QString compact(const QJsonObject& object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString clipped(const QString& text, qsizetype most)
{
    if (text.size() <= most) {
        return text;
    }
    return text.left(most) + u"\n[%1 more characters left out]"_s.arg(text.size() - most);
}

QString tidy(const QString& text)
{
    const QString simple = text.simplified();
    if (simple.size() <= kMostServiceChars) {
        return simple;
    }
    return simple.left(kMostServiceChars) + u"..."_s;
}

bool isPicture(const QJsonObject& item)
{
    return item.value(u"type"_s).toString() == u"image" && !item.value(u"data"_s).toString().isEmpty();
}

QString mimeOf(const QJsonObject& item)
{
    const QString mime = item.value(u"mimeType"_s).toString();
    return mime.isEmpty() ? u"image/png"_s : mime;
}

int picturesIn(const AiRequest& request)
{
    int total = 0;
    for (const AiTurn& turn : request.turns) {
        for (const AiToolResult& result : turn.results) {
            for (const QJsonValue& item : result.content) {
                total += isPicture(item.toObject()) ? 1 : 0;
            }
        }
        for (const QJsonValue& item : turn.pictures) {
            total += isPicture(item.toObject()) ? 1 : 0;
        }
    }
    return total;
}

class PictureBudget
{
public:
    explicit PictureBudget(const AiRequest& request)
        : m_attach(request.pictures)
        , m_first(request.pictures ? std::max(0, picturesIn(request) - AiRequest::kMostPictures) : 0)
    {
    }

    bool take() { return m_attach && m_seen++ >= m_first; }

    [[nodiscard]] QString leftOut() const
    {
        return m_attach ? u"[An older picture, left out to keep the request small.]"_s
                        : u"[A picture, left out because the model does not take pictures.]"_s;
    }

private:
    bool m_attach = true;
    int m_first = 0;
    int m_seen = 0;
};

QJsonObject schemaOf(const QJsonObject& tool)
{
    QJsonObject schema = tool.value(u"inputSchema"_s).toObject();
    if (schema.isEmpty()) {
        schema = QJsonObject{{u"type"_s, u"object"_s}, {u"properties"_s, QJsonObject{}}};
    }
    return schema;
}

QJsonArray openAiTools(const QJsonArray& tools)
{
    QJsonArray listed;
    for (const QJsonValue& value : tools) {
        const QJsonObject tool = value.toObject();
        const QJsonObject function{{u"name"_s, tool.value(u"name"_s)},
                                   {u"description"_s, tool.value(u"description"_s)},
                                   {u"parameters"_s, schemaOf(tool)}};
        listed.append(QJsonObject{{u"type"_s, u"function"_s}, {u"function"_s, function}});
    }
    return listed;
}

QJsonArray anthropicTools(const QJsonArray& tools)
{
    QJsonArray listed;
    for (const QJsonValue& value : tools) {
        const QJsonObject tool = value.toObject();
        listed.append(QJsonObject{{u"name"_s, tool.value(u"name"_s)},
                                  {u"description"_s, tool.value(u"description"_s)},
                                  {u"input_schema"_s, schemaOf(tool)}});
    }
    return listed;
}

QString resultText(const AiToolResult& result, PictureBudget& pictures, QJsonArray& attached)
{
    QStringList parts;
    for (const QJsonValue& value : result.content) {
        const QJsonObject item = value.toObject();
        if (item.value(u"type"_s).toString() == u"text") {
            parts.append(item.value(u"text"_s).toString());
        } else if (isPicture(item)) {
            if (pictures.take()) {
                parts.append(u"[The picture is attached in the next message.]"_s);
                const QString url = u"data:"_s + mimeOf(item) + u";base64,"_s + item.value(u"data"_s).toString();
                attached.append(QJsonObject{{u"type"_s, u"image_url"_s},
                                            {u"image_url"_s, QJsonObject{{u"url"_s, url}}}});
            } else {
                parts.append(pictures.leftOut());
            }
        }
    }
    QString text = clipped(parts.join(u'\n'), kMostToolChars);
    if (text.trimmed().isEmpty()) {
        text = u"(no output)"_s;
    }
    if (result.failed) {
        text.prepend(u"Error: "_s);
    }
    return text;
}

QJsonValue openAiUser(const AiTurn& turn, PictureBudget& pictures)
{
    if (turn.pictures.isEmpty()) {
        return turn.text;
    }
    QJsonArray content{textBlock(turn.text)};
    for (const QJsonValue& value : turn.pictures) {
        const QJsonObject item = value.toObject();
        if (!isPicture(item)) {
            continue;
        }
        if (pictures.take()) {
            const QString url = u"data:"_s + mimeOf(item) + u";base64,"_s + item.value(u"data"_s).toString();
            content.append(QJsonObject{{u"type"_s, u"image_url"_s}, {u"image_url"_s, QJsonObject{{u"url"_s, url}}}});
        } else {
            content.append(textBlock(pictures.leftOut()));
        }
    }
    return content;
}

QJsonArray anthropicUser(const AiTurn& turn, PictureBudget& pictures)
{
    QJsonArray content;
    for (const QJsonValue& value : turn.pictures) {
        const QJsonObject item = value.toObject();
        if (!isPicture(item)) {
            continue;
        }
        if (pictures.take()) {
            const QJsonObject source{{u"type"_s, u"base64"_s},
                                     {u"media_type"_s, mimeOf(item)},
                                     {u"data"_s, item.value(u"data"_s).toString()}};
            content.append(QJsonObject{{u"type"_s, u"image"_s}, {u"source"_s, source}});
        } else {
            content.append(textBlock(pictures.leftOut()));
        }
    }
    content.append(textBlock(turn.text));
    return content;
}

QJsonObject openAiRequest(const AiRequest& request)
{
    QJsonArray messages;
    if (!request.system.isEmpty()) {
        messages.append(QJsonObject{{u"role"_s, u"system"_s}, {u"content"_s, request.system}});
    }
    PictureBudget pictures(request);
    for (const AiTurn& turn : request.turns) {
        switch (turn.role) {
        case AiTurn::Role::User:
            messages.append(QJsonObject{{u"role"_s, u"user"_s}, {u"content"_s, openAiUser(turn, pictures)}});
            break;
        case AiTurn::Role::Assistant: {
            if (turn.text.isEmpty() && turn.calls.empty()) {
                break;
            }
            const QJsonValue content = turn.text.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(turn.text);
            QJsonObject message{{u"role"_s, u"assistant"_s}, {u"content"_s, content}};
            if (!turn.calls.empty()) {
                QJsonArray calls;
                for (const AiToolCall& call : turn.calls) {
                    const QJsonObject function{{u"name"_s, call.name}, {u"arguments"_s, compact(call.arguments)}};
                    calls.append(QJsonObject{{u"id"_s, call.id}, {u"type"_s, u"function"_s}, {u"function"_s, function}});
                }
                message.insert(u"tool_calls"_s, calls);
            }
            messages.append(message);
            break;
        }
        case AiTurn::Role::Tools: {
            QJsonArray attached;
            for (const AiToolResult& result : turn.results) {
                messages.append(QJsonObject{{u"role"_s, u"tool"_s},
                                            {u"tool_call_id"_s, result.id},
                                            {u"content"_s, resultText(result, pictures, attached)}});
            }
            if (!attached.isEmpty()) {
                attached.prepend(textBlock(u"The pictures that the tools above returned, in order."_s));
                messages.append(QJsonObject{{u"role"_s, u"user"_s}, {u"content"_s, attached}});
            }
            break;
        }
        }
    }
    QJsonObject body{{u"model"_s, request.model}, {u"messages"_s, messages}, {u"stream"_s, request.stream}};
    if (request.stream) {
        body.insert(u"stream_options"_s, QJsonObject{{u"include_usage"_s, true}});
    }
    if (!request.effort.isEmpty()) {
        body.insert(u"reasoning_effort"_s, request.effort);
    }
    if (aiModelTraits(AiKind::OpenAi, request.model).think) {
        body.insert(u"enable_thinking"_s, request.think);
    }
    if (!request.tools.isEmpty()) {
        body.insert(u"tools"_s, openAiTools(request.tools));
    }
    return body;
}

QJsonArray anthropicResult(const AiToolResult& result, PictureBudget& pictures)
{
    QJsonArray inner;
    for (const QJsonValue& value : result.content) {
        const QJsonObject item = value.toObject();
        if (item.value(u"type"_s).toString() == u"text") {
            const QString text = clipped(item.value(u"text"_s).toString(), kMostToolChars);
            if (!text.trimmed().isEmpty()) {
                inner.append(textBlock(text));
            }
        } else if (isPicture(item)) {
            if (pictures.take()) {
                const QJsonObject source{{u"type"_s, u"base64"_s},
                                         {u"media_type"_s, mimeOf(item)},
                                         {u"data"_s, item.value(u"data"_s).toString()}};
                inner.append(QJsonObject{{u"type"_s, u"image"_s}, {u"source"_s, source}});
            } else {
                inner.append(textBlock(pictures.leftOut()));
            }
        }
    }
    if (inner.isEmpty()) {
        inner.append(textBlock(u"(no output)"_s));
    }
    return inner;
}

class AnthropicMessages
{
public:
    void add(const QString& role, const QJsonArray& blocks)
    {
        if (blocks.isEmpty()) {
            return;
        }
        if (role != m_role) {
            flush();
            m_role = role;
        }
        for (const QJsonValue& block : blocks) {
            m_blocks.append(block);
        }
    }

    [[nodiscard]] QJsonArray finish()
    {
        flush();
        return m_messages;
    }

private:
    void flush()
    {
        if (!m_blocks.isEmpty()) {
            m_messages.append(QJsonObject{{u"role"_s, m_role}, {u"content"_s, m_blocks}});
            m_blocks = QJsonArray();
        }
    }

    QJsonArray m_messages;
    QJsonArray m_blocks;
    QString m_role;
};

bool anthropicThinks(const AiRequest& request)
{
    if (!request.think) {
        return false;
    }
    for (auto turn = request.turns.rbegin(); turn != request.turns.rend(); ++turn) {
        if (turn->role == AiTurn::Role::User) {
            return true;
        }
        if (turn->role == AiTurn::Role::Assistant && !turn->calls.empty()) {
            return !turn->thinkingBlocks.isEmpty();
        }
    }
    return true;
}

QJsonObject anthropicRequest(const AiRequest& request)
{
    const bool thinks = anthropicThinks(request);
    AnthropicMessages messages;
    PictureBudget pictures(request);
    for (const AiTurn& turn : request.turns) {
        switch (turn.role) {
        case AiTurn::Role::User:
            messages.add(u"user"_s, anthropicUser(turn, pictures));
            break;
        case AiTurn::Role::Assistant: {
            QJsonArray content;
            if (thinks) {
                for (const QJsonValue& block : turn.thinkingBlocks) {
                    content.append(block);
                }
            }
            if (!turn.text.isEmpty()) {
                content.append(textBlock(turn.text));
            }
            for (const AiToolCall& call : turn.calls) {
                content.append(QJsonObject{{u"type"_s, u"tool_use"_s},
                                           {u"id"_s, call.id},
                                           {u"name"_s, call.name},
                                           {u"input"_s, call.arguments}});
            }
            messages.add(u"assistant"_s, content);
            break;
        }
        case AiTurn::Role::Tools: {
            QJsonArray content;
            for (const AiToolResult& result : turn.results) {
                content.append(QJsonObject{{u"type"_s, u"tool_result"_s},
                                           {u"tool_use_id"_s, result.id},
                                           {u"content"_s, anthropicResult(result, pictures)},
                                           {u"is_error"_s, result.failed}});
            }
            messages.add(u"user"_s, content);
            break;
        }
        }
    }
    const int answerTokens = request.maxTokens > 0 ? request.maxTokens : AiRequest::kAnthropicMaxTokens;
    const int budget = aiThinkingBudget(request.effort);
    QJsonObject body{{u"model"_s, request.model},
                     {u"max_tokens"_s, thinks ? answerTokens + budget : answerTokens},
                     {u"messages"_s, messages.finish()},
                     {u"stream"_s, request.stream}};
    if (thinks) {
        body.insert(u"thinking"_s, QJsonObject{{u"type"_s, u"enabled"_s}, {u"budget_tokens"_s, budget}});
    }
    if (!request.system.isEmpty()) {
        body.insert(u"system"_s, request.system);
    }
    if (!request.tools.isEmpty()) {
        body.insert(u"tools"_s, anthropicTools(request.tools));
    }
    return body;
}

QJsonValue responsesUser(const AiTurn& turn, PictureBudget& pictures)
{
    if (turn.pictures.isEmpty()) {
        return turn.text;
    }
    QJsonArray content{QJsonObject{{u"type"_s, u"input_text"_s}, {u"text"_s, turn.text}}};
    for (const QJsonValue& value : turn.pictures) {
        const QJsonObject item = value.toObject();
        if (!isPicture(item)) {
            continue;
        }
        if (pictures.take()) {
            const QString url = u"data:"_s + mimeOf(item) + u";base64,"_s + item.value(u"data"_s).toString();
            content.append(QJsonObject{{u"type"_s, u"input_image"_s}, {u"image_url"_s, url}});
        } else {
            content.append(QJsonObject{{u"type"_s, u"input_text"_s}, {u"text"_s, pictures.leftOut()}});
        }
    }
    return content;
}

QJsonObject responsesRequest(const AiRequest& request)
{
    QJsonArray input;
    PictureBudget pictures(request);
    for (const AiTurn& turn : request.turns) {
        switch (turn.role) {
        case AiTurn::Role::User:
            input.append(QJsonObject{{u"role"_s, u"user"_s}, {u"content"_s, responsesUser(turn, pictures)}});
            break;
        case AiTurn::Role::Assistant: {
            if (turn.text.isEmpty() && turn.calls.empty()) {
                break;
            }
            if (!turn.text.isEmpty()) {
                input.append(QJsonObject{{u"role"_s, u"assistant"_s}, {u"content"_s, turn.text}});
            }
            for (const AiToolCall& call : turn.calls) {
                input.append(QJsonObject{{u"type"_s, u"function_call"_s},
                                         {u"call_id"_s, call.id},
                                         {u"name"_s, call.name},
                                         {u"arguments"_s, compact(call.arguments)}});
            }
            break;
        }
        case AiTurn::Role::Tools: {
            QJsonArray attached;
            for (const AiToolResult& result : turn.results) {
                input.append(QJsonObject{{u"type"_s, u"function_call_output"_s},
                                         {u"call_id"_s, result.id},
                                         {u"output"_s, resultText(result, pictures, attached)}});
            }
            if (!attached.isEmpty()) {
                QJsonArray content{QJsonObject{{u"type"_s, u"input_text"_s},
                                              {u"text"_s,
                                               u"The pictures that the tools above returned, in order."_s}}};
                for (const QJsonValue& value : attached) {
                    const QString url =
                        value.toObject().value(u"image_url"_s).toObject().value(u"url"_s).toString();
                    content.append(QJsonObject{{u"type"_s, u"input_image"_s}, {u"image_url"_s, url}});
                }
                input.append(QJsonObject{{u"role"_s, u"user"_s}, {u"content"_s, content}});
            }
            break;
        }
        }
    }
    QJsonObject body{{u"model"_s, request.model},
                     {u"input"_s, input},
                     {u"stream"_s, request.stream},
                     {u"store"_s, false}};
    if (!request.system.isEmpty()) {
        body.insert(u"instructions"_s, request.system);
    }
    QJsonObject reasoning;
    if (!request.effort.isEmpty()) {
        reasoning.insert(u"effort"_s, request.effort);
    }
    if (request.think) {
        reasoning.insert(u"summary"_s, u"auto"_s);
    }
    if (!reasoning.isEmpty()) {
        body.insert(u"reasoning"_s, reasoning);
    }
    if (!request.tools.isEmpty()) {
        QJsonArray tools;
        for (const QJsonValue& value : request.tools) {
            const QJsonObject tool = value.toObject();
            tools.append(QJsonObject{{u"type"_s, u"function"_s},
                                     {u"name"_s, tool.value(u"name"_s)},
                                     {u"description"_s, tool.value(u"description"_s)},
                                     {u"parameters"_s, schemaOf(tool)}});
        }
        body.insert(u"tools"_s, tools);
    }
    return body;
}

void readArguments(const QJsonValue& value, AiToolCall& call)
{
    if (value.isObject()) {
        call.arguments = value.toObject();
        return;
    }
    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        call.arguments = {};
        return;
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error == QJsonParseError::NoError && document.isObject()) {
        call.arguments = document.object();
        return;
    }
    call.problem = u"The arguments of %1 are not a JSON object: %2"_s.arg(call.name, clipped(text, kMostQuotedArguments));
}

QString contentText(const QJsonValue& content)
{
    if (content.isString()) {
        return content.toString();
    }
    QString text;
    for (const QJsonValue& part : content.toArray()) {
        const QJsonObject item = part.toObject();
        if (item.value(u"type"_s).toString() == u"text") {
            text += item.value(u"text"_s).toString();
        }
    }
    return text;
}

AiUsage usageOf(const QJsonObject& usage, const QString& input, const QString& output)
{
    return {usage.value(input).toInteger(), usage.value(output).toInteger()};
}

AiReply openAiReply(const QJsonObject& root)
{
    const QJsonArray choices = root.value(u"choices"_s).toArray();
    if (choices.isEmpty()) {
        return AiReply{.problem = aiUnreadable()};
    }
    const QJsonObject choice = choices.at(0).toObject();
    const QJsonObject message = choice.value(u"message"_s).toObject();
    AiReply parsed;
    const AiThinkSplit split = aiSplitThinkTags(contentText(message.value(u"content"_s)));
    parsed.text = split.text;
    const QString reasoning = message.value(u"reasoning_content"_s).isString()
                                  ? message.value(u"reasoning_content"_s).toString()
                                  : message.value(u"reasoning"_s).toString();
    parsed.thinking = reasoning.isEmpty() ? split.thinking : reasoning;
    for (const QJsonValue& value : message.value(u"tool_calls"_s).toArray()) {
        const QJsonObject call = value.toObject();
        const QJsonObject function = call.value(u"function"_s).toObject();
        AiToolCall read{.id = call.value(u"id"_s).toString(), .name = function.value(u"name"_s).toString()};
        readArguments(function.value(u"arguments"_s), read);
        if (!read.name.isEmpty()) {
            parsed.calls.push_back(std::move(read));
        }
    }
    parsed.stop = choice.value(u"finish_reason"_s).toString();
    parsed.usage = usageOf(root.value(u"usage"_s).toObject(), u"prompt_tokens"_s, u"completion_tokens"_s);
    return parsed;
}

AiReply responsesReply(const QJsonObject& root)
{
    if (!root.value(u"output"_s).isArray()) {
        return AiReply{.problem = aiUnreadable()};
    }
    AiReply parsed;
    for (const QJsonValue& value : root.value(u"output"_s).toArray()) {
        const QJsonObject item = value.toObject();
        const QString type = item.value(u"type"_s).toString();
        if (type == u"message") {
            for (const QJsonValue& part : item.value(u"content"_s).toArray()) {
                const QJsonObject block = part.toObject();
                if (block.value(u"type"_s).toString() == u"output_text") {
                    parsed.text += block.value(u"text"_s).toString();
                }
            }
        } else if (type == u"function_call") {
            AiToolCall read{.id = item.value(u"call_id"_s).toString(), .name = item.value(u"name"_s).toString()};
            readArguments(item.value(u"arguments"_s), read);
            if (!read.name.isEmpty()) {
                parsed.calls.push_back(std::move(read));
            }
        } else if (type == u"reasoning") {
            for (const QJsonValue& part : item.value(u"summary"_s).toArray()) {
                const QString piece = part.toObject().value(u"text"_s).toString();
                if (!piece.isEmpty()) {
                    parsed.thinking += (parsed.thinking.isEmpty() ? QString() : u"\n\n"_s) + piece;
                }
            }
        }
    }
    if (root.value(u"status"_s).toString() == u"incomplete" &&
        root.value(u"incomplete_details"_s).toObject().value(u"reason"_s).toString() == u"max_output_tokens") {
        parsed.stop = u"length"_s;
    } else {
        parsed.stop = root.value(u"status"_s).toString();
    }
    parsed.usage = usageOf(root.value(u"usage"_s).toObject(), u"input_tokens"_s, u"output_tokens"_s);
    return parsed;
}

AiReply anthropicReply(const QJsonObject& root)
{
    if (!root.value(u"content"_s).isArray()) {
        return AiReply{.problem = aiUnreadable()};
    }
    AiReply parsed;
    for (const QJsonValue& value : root.value(u"content"_s).toArray()) {
        const QJsonObject block = value.toObject();
        const QString type = block.value(u"type"_s).toString();
        if (type == u"text") {
            parsed.text += block.value(u"text"_s).toString();
        } else if (type == u"tool_use") {
            AiToolCall read{.id = block.value(u"id"_s).toString(), .name = block.value(u"name"_s).toString()};
            readArguments(block.value(u"input"_s), read);
            if (!read.name.isEmpty()) {
                parsed.calls.push_back(std::move(read));
            }
        } else if (type == u"thinking") {
            parsed.thinking += block.value(u"thinking"_s).toString();
            parsed.thinkingBlocks.append(block);
        } else if (type == u"redacted_thinking") {
            parsed.thinkingBlocks.append(block);
        }
    }
    parsed.stop = root.value(u"stop_reason"_s).toString();
    parsed.usage = usageOf(root.value(u"usage"_s).toObject(), u"input_tokens"_s, u"output_tokens"_s);
    return parsed;
}

struct ServiceError {
    QString message;
    QString code;
};

QString codeOf(const QJsonObject& error)
{
    const QJsonValue code = error.value(u"code"_s);
    if (code.isString() && !code.toString().isEmpty()) {
        return code.toString();
    }
    if (code.isDouble()) {
        return QString::number(code.toInteger());
    }
    return error.value(u"type"_s).toString();
}

ServiceError serviceError(const QByteArray& body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (document.isObject()) {
        const QJsonObject root = document.object();
        const QJsonValue error = root.value(u"error"_s);
        if (error.isObject()) {
            const QJsonObject inner = error.toObject();
            return {tidy(inner.value(u"message"_s).toString()), codeOf(inner)};
        }
        if (error.isString()) {
            return {tidy(error.toString()), {}};
        }
        if (root.value(u"message"_s).isString()) {
            return {tidy(root.value(u"message"_s).toString()), codeOf(root)};
        }
        if (root.value(u"detail"_s).isString()) {
            return {tidy(root.value(u"detail"_s).toString()), {}};
        }
        return {};
    }
    const QString text = QString::fromUtf8(body).trimmed();
    if (text.isEmpty() || text.startsWith(u'<') || text.startsWith(u'{') || text.startsWith(u'[')) {
        return {};
    }
    return {tidy(text.section(u'\n', 0, 0)), {}};
}

QString statusSentence(int status)
{
    switch (status) {
    case 0:
        return QCoreApplication::translate("AiService", "The service answered with an error.");
    case 400:
    case 422:
        return QCoreApplication::translate("AiService", "The service did not accept the request (HTTP %1).").arg(status);
    case 401:
        return QCoreApplication::translate("AiService", "The service did not accept the API key (HTTP 401). Check the "
                                                        "key in Settings > AI.");
    case 402:
        return QCoreApplication::translate("AiService", "The service wants payment first (HTTP 402). Check your plan.");
    case 403:
        return QCoreApplication::translate("AiService", "The service refused the request (HTTP 403). The key may not be "
                                                        "allowed to use this model.");
    case 404:
        return QCoreApplication::translate("AiService", "The service has nothing at this address, or does not know the "
                                                        "model (HTTP 404). Check the address and the model in Settings "
                                                        "> AI.");
    case 408:
    case 504:
        return QCoreApplication::translate("AiService", "The service took too long (HTTP %1). Try again.").arg(status);
    case 413:
        return QCoreApplication::translate("AiService", "The conversation is too large for the service (HTTP 413). "
                                                        "Start a new conversation.");
    case 429:
        return QCoreApplication::translate("AiService", "The service is busy, or the quota is used up (HTTP 429). Wait "
                                                        "a moment, or check your plan.");
    default:
        break;
    }
    if (status >= 300 && status < 400) {
        return QCoreApplication::translate("AiService", "The service sent the request on to another address (HTTP %1). "
                                                        "Put the final address in Settings > AI.")
            .arg(status);
    }
    if (status >= 500) {
        return QCoreApplication::translate("AiService", "The service has a problem (HTTP %1). Try again later.").arg(status);
    }
    return QCoreApplication::translate("AiService", "The service answered with HTTP %1.").arg(status);
}

QString saidBy(const QString& sentence, const QString& message)
{
    if (message.isEmpty()) {
        return sentence;
    }
    return sentence + u' ' + QCoreApplication::translate("AiService", "The service says: %1").arg(message);
}

QString requestTail(AiKind kind)
{
    if (kind == AiKind::Anthropic) {
        return u"/messages"_s;
    }
    if (kind == AiKind::OpenAiResponses) {
        return u"/responses"_s;
    }
    return u"/chat/completions"_s;
}

QUrl endpointWith(AiKind kind, const QString& address, const QString& tail)
{
    QString given = address.trimmed();
    if (given.isEmpty()) {
        given = aiDefaultAddress(kind);
    } else if (!given.contains(u"://")) {
        const bool local = given.startsWith(u"localhost") || given.startsWith(u"127.");
        given.prepend(local ? u"http://"_s : u"https://"_s);
    }
    QUrl url(given, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty() || (url.scheme() != u"https" && url.scheme() != u"http")) {
        return {};
    }
    url.setFragment({});
    QString path = url.path();
    while (path.endsWith(u'/')) {
        path.chop(1);
    }
    if (tail == u"/models"_s) {
        for (const QString& ending : {u"/chat/completions"_s, u"/responses"_s, u"/messages"_s}) {
            if (path.endsWith(ending)) {
                path.chop(ending.size());
                break;
            }
        }
    }
    if (!path.endsWith(tail)) {
        if (kind == AiKind::Anthropic && !path.endsWith(u"/v1")) {
            path += u"/v1"_s;
        }
        path += tail;
    }
    url.setPath(path);
    return url;
}

QJsonObject providerPicture(const QJsonObject& item)
{
    return {{u"type"_s, u"image"_s}, {u"mimeType"_s, mimeOf(item)}, {u"data"_s, item.value(u"data"_s).toString()}};
}

QJsonArray providerContent(const QJsonArray& content, PictureBudget& pictures)
{
    QJsonArray items;
    for (const QJsonValue& value : content) {
        const QJsonObject item = value.toObject();
        if (item.value(u"type"_s).toString() == u"text") {
            items.append(textBlock(clipped(item.value(u"text"_s).toString(), kMostToolChars)));
        } else if (isPicture(item)) {
            items.append(pictures.take() ? providerPicture(item) : textBlock(pictures.leftOut()));
        }
    }
    return items;
}

}

QString aiServiceName(AiKind kind)
{
    return toQString(settings::kAiServiceNames[static_cast<std::size_t>(kind)]);
}

QString aiDefaultAddress(AiKind kind)
{
    return toQString(kind == AiKind::Anthropic ? settings::kAnthropicAddress : settings::kOpenAiAddress);
}

QUrl aiEndpoint(AiKind kind, const QString& address)
{
    return endpointWith(kind, address, requestTail(kind));
}

QUrl aiModelsEndpoint(AiKind kind, const QString& address)
{
    return endpointWith(kind, address, u"/models"_s);
}

QStringList aiModelsOf(const QByteArray& body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) {
        return {};
    }
    const QJsonObject root = document.object();
    const QJsonArray listed =
        root.contains(u"data"_s) ? root.value(u"data"_s).toArray() : root.value(u"models"_s).toArray();
    QStringList models;
    for (const QJsonValue& value : listed) {
        const QJsonObject entry = value.toObject();
        QString id = entry.value(u"id"_s).toString();
        if (id.isEmpty()) {
            id = entry.value(u"name"_s).toString();
        }
        if (!id.isEmpty() && !models.contains(id)) {
            models.append(id);
        }
    }
    std::ranges::sort(models);
    return models;
}

AiModelTraits aiModelTraits(AiKind kind, const QString& model)
{
    const QString name = bareModel(model);
    AiModelTraits traits;
    if (name.isEmpty()) {
        return traits;
    }
    const QStringList levels{u"low"_s, u"medium"_s, u"high"_s};
    if (kind == AiKind::Anthropic) {
        static const QRegularExpression fourth(u"^claude-(?:(?:opus|sonnet|haiku)-4|4-)"_s);
        if (name.startsWith(u"claude-3-7-sonnet") || fourth.match(name).hasMatch()) {
            traits.think = true;
            traits.efforts = levels;
            traits.thinkingEffort = true;
        }
        traits.longContext = name.startsWith(u"claude-sonnet-4");
        return traits;
    }
    const bool gpt5 = name.startsWith(u"gpt-5") && !name.contains(u"-chat");
    const bool oSeries = name.size() > 1 && name.at(0) == u'o' && name.at(1).isDigit();
    if (gpt5 || oSeries || name.contains(u"codex")) {
        traits.efforts = gpt5 ? QStringList{u"minimal"_s, u"low"_s, u"medium"_s, u"high"_s} : levels;
        traits.think = kind == AiKind::OpenAiResponses;
        return traits;
    }
    if (kind != AiKind::OpenAi) {
        return traits;
    }
    if (name.contains(u"thinking") || name.startsWith(u"qwq") || name.contains(u"reasoner") || name.contains(u"-r1")
        || name.startsWith(u"r1")) {
        traits.alwaysThinks = true;
        return traits;
    }
    if (name.contains(u"instruct") || name.contains(u"coder")) {
        return traits;
    }
    traits.think = name.startsWith(u"qwen3") || name.startsWith(u"qwen-plus") || name.startsWith(u"qwen-turbo")
                   || name.startsWith(u"qwen-flash");
    return traits;
}

AiModelTraits aiTraitsOf(const QJsonObject& traits)
{
    AiModelTraits read{.think = traits.value(u"think"_s).toBool(),
                       .alwaysThinks = traits.value(u"alwaysThinks"_s).toBool(),
                       .efforts = {},
                       .thinkingEffort = traits.value(u"thinkingEffort"_s).toBool(),
                       .longContext = traits.value(u"longContext"_s).toBool()};
    QStringList listed;
    for (const QJsonValue& value : traits.value(u"efforts"_s).toArray()) {
        listed.append(value.toString());
    }
    for (const QString& level : {u"minimal"_s, u"low"_s, u"medium"_s, u"high"_s}) {
        if (listed.contains(level)) {
            read.efforts.append(level);
        }
    }
    return read;
}

AiChoice aiFitted(AiKind kind, AiChoice choice)
{
    const AiModelTraits traits = aiModelTraits(kind, choice.model);
    return aiFitted(traits, std::move(choice));
}

AiChoice aiFitted(const AiModelTraits& traits, AiChoice choice)
{
    choice.think = choice.think && traits.think;
    if (!traits.efforts.contains(choice.effort) || (traits.thinkingEffort && !choice.think)) {
        choice.effort.clear();
    }
    choice.longContext = choice.longContext && traits.longContext;
    return choice;
}

QVariantMap aiChoiceMap(const AiChoice& choice)
{
    if (choice.model.isEmpty()) {
        return {};
    }
    QVariantMap map{{u"model"_s, choice.model},
                    {u"think"_s, choice.think},
                    {u"effort"_s, choice.effort},
                    {u"longContext"_s, choice.longContext}};
    if (!choice.service.isEmpty()) {
        map.insert(u"service"_s, choice.service);
    }
    if (!choice.provider.isEmpty()) {
        map.insert(u"provider"_s, choice.provider);
    }
    return map;
}

AiChoice aiChoiceOf(const QVariantMap& map)
{
    constexpr qsizetype kMostModelLength = 256;
    constexpr qsizetype kMostProviderLength = 160;
    const QString model = map.value(u"model"_s).toString().trimmed().left(kMostModelLength);
    if (model.isEmpty()) {
        return {};
    }
    static const QStringList efforts{u"minimal"_s, u"low"_s, u"medium"_s, u"high"_s};
    QStringList services;
    for (const std::string_view name : settings::kAiServiceNames) {
        services.append(toQString(name));
    }
    const QString effort = map.value(u"effort"_s).toString();
    const QString service = map.value(u"service"_s).toString();
    const QString provider = map.value(u"provider"_s).toString();
    const bool provided =
        service == toQString(settings::kAiServiceNames[static_cast<std::size_t>(settings::AiService::Extension)]) &&
        provider.contains(u'/') && provider.size() <= kMostProviderLength;
    return AiChoice{.model = model,
                    .think = map.value(u"think"_s).toBool(),
                    .effort = efforts.contains(effort) ? effort : QString(),
                    .longContext = map.value(u"longContext"_s).toBool(),
                    .service = services.contains(service) ? service : QString(),
                    .provider = provided ? provider : QString()};
}

int aiThinkingBudget(const QString& effort)
{
    if (effort == u"low") {
        return 2048;
    }
    if (effort == u"medium") {
        return 8192;
    }
    if (effort == u"high") {
        return 16384;
    }
    return AiRequest::kThinkingBudget;
}

std::vector<AiHeader> aiHeaders(AiKind kind, const QString& key, bool stream, bool longContext)
{
    std::vector<AiHeader> headers{
        {.name = "Content-Type"_ba, .value = "application/json"_ba},
        {.name = "Accept"_ba, .value = stream ? "text/event-stream"_ba : "application/json"_ba}};
    if (kind == AiKind::Anthropic) {
        headers.push_back({.name = "anthropic-version"_ba, .value = "2023-06-01"_ba});
        if (longContext) {
            headers.push_back({.name = "anthropic-beta"_ba, .value = "context-1m-2025-08-07"_ba});
        }
        if (!key.isEmpty()) {
            headers.push_back({.name = "x-api-key"_ba, .value = key.toUtf8()});
        }
    } else if (!key.isEmpty()) {
        headers.push_back({.name = "Authorization"_ba, .value = "Bearer "_ba + key.toUtf8()});
    }
    return headers;
}

QJsonObject aiRequestJson(AiKind kind, const AiRequest& request)
{
    if (kind == AiKind::Anthropic) {
        return anthropicRequest(request);
    }
    if (kind == AiKind::OpenAiResponses) {
        return responsesRequest(request);
    }
    return openAiRequest(request);
}

int aiPictureCount(const AiRequest& request)
{
    return request.pictures ? std::min(picturesIn(request), AiRequest::kMostPictures) : 0;
}

AiReply aiReplyOf(AiKind kind, const QByteArray& body)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return AiReply{.problem = aiUnreadable()};
    }
    const QJsonObject root = document.object();
    if (root.value(u"type"_s).toString() == u"error" ||
        (root.contains(u"error"_s) && !root.value(u"error"_s).isNull())) {
        return aiFailure(0, body);
    }
    if (kind == AiKind::Anthropic) {
        return anthropicReply(root);
    }
    if (kind == AiKind::OpenAiResponses) {
        return responsesReply(root);
    }
    return openAiReply(root);
}

AiReply aiFailure(int status, const QByteArray& body)
{
    const ServiceError said = serviceError(body);
    return AiReply{.status = status, .problem = saidBy(statusSentence(status), said.message), .code = said.code};
}

QString aiNetworkProblem(int error, const QString& host, const QString& detail)
{
    switch (static_cast<QNetworkReply::NetworkError>(error)) {
    case QNetworkReply::HostNotFoundError:
        return QCoreApplication::translate("AiService", "Cannot find %1. Check the address and the network.").arg(host);
    case QNetworkReply::ConnectionRefusedError:
        return QCoreApplication::translate("AiService", "Nothing answers at %1. Check the address, and that the service "
                                                        "is running.")
            .arg(host);
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
        return QCoreApplication::translate("AiService", "The connection to %1 was lost. Try again.").arg(host);
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
        return QCoreApplication::translate("AiService", "%1 did not answer in time. Try again.").arg(host);
    case QNetworkReply::SslHandshakeFailedError:
        return QCoreApplication::translate("AiService", "No secure connection to %1: %2").arg(host, detail);
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyConnectionClosedError:
    case QNetworkReply::ProxyNotFoundError:
    case QNetworkReply::ProxyTimeoutError:
    case QNetworkReply::ProxyAuthenticationRequiredError:
        return QCoreApplication::translate("AiService", "The proxy did not let the connection to %1 through: %2")
            .arg(host, detail);
    default:
        break;
    }
    return QCoreApplication::translate("AiService", "The connection to %1 failed: %2").arg(host, detail);
}

QString aiUnreadable()
{
    return QCoreApplication::translate("AiService", "The service sent an answer that GameScrcpy cannot read.");
}

QString aiContentText(const QJsonArray& content)
{
    QStringList parts;
    for (const QJsonValue& value : content) {
        const QJsonObject item = value.toObject();
        if (item.value(u"type"_s).toString() == u"text") {
            parts.append(item.value(u"text"_s).toString());
        }
    }
    return parts.join(u'\n');
}

QVariantList aiBlocks(const QString& text)
{
    QVariantList blocks;
    QStringList lines;
    bool code = false;
    QChar fence;
    qsizetype fenceLength = 0;
    QString language;
    const auto flush = [&] {
        QString joined = lines.join(u'\n');
        lines.clear();
        if (code) {
            while (joined.endsWith(u'\n')) {
                joined.chop(1);
            }
        } else {
            joined = joined.trimmed();
        }
        if (joined.isEmpty()) {
            return;
        }
        blocks.append(QVariantMap{{u"code"_s, code}, {u"language"_s, code ? language : QString()}, {u"text"_s, joined}});
    };
    for (QString line : text.split(u'\n')) {
        if (line.endsWith(u'\r')) {
            line.chop(1);
        }
        const QString stripped = line.trimmed();
        if (!code) {
            if (stripped.startsWith(u"```") || stripped.startsWith(u"~~~")) {
                flush();
                fence = stripped.front();
                fenceLength = 0;
                while (fenceLength < stripped.size() && stripped.at(fenceLength) == fence) {
                    ++fenceLength;
                }
                language = stripped.mid(fenceLength).trimmed().section(u' ', 0, 0);
                code = true;
                continue;
            }
        } else if (stripped.size() >= fenceLength &&
                   std::all_of(stripped.cbegin(), stripped.cend(), [fence](QChar c) { return c == fence; })) {
            flush();
            code = false;
            continue;
        }
        lines.append(line);
    }
    flush();
    return blocks;
}

void AiSseParser::feed(QByteArrayView bytes)
{
    if (m_broken) {
        return;
    }
    m_pending.append(bytes);
    const qsizetype size = m_pending.size();
    qsizetype start = 0;
    while (start < size) {
        qsizetype end = start;
        while (end < size && m_pending.at(end) != '\n' && m_pending.at(end) != '\r') {
            ++end;
        }
        if (end == size) {
            break;
        }
        const bool carriage = m_pending.at(end) == '\r';
        if (carriage && end + 1 == size) {
            break;
        }
        line(QByteArrayView(m_pending).sliced(start, end - start));
        start = end + (carriage && m_pending.at(end + 1) == '\n' ? 2 : 1);
    }
    m_pending.remove(0, start);
    if (m_pending.size() > kMostLineBytes) {
        m_broken = true;
        m_pending.clear();
    }
}

void AiSseParser::end()
{
    if (!m_broken && !m_pending.isEmpty()) {
        QByteArray rest = std::exchange(m_pending, {});
        if (rest.endsWith('\r')) {
            rest.chop(1);
        }
        line(rest);
    }
    dispatch();
}

std::vector<AiSseEvent> AiSseParser::take()
{
    return std::exchange(m_events, {});
}

void AiSseParser::line(QByteArrayView text)
{
    if (text.isEmpty()) {
        dispatch();
        return;
    }
    if (text.front() == ':') {
        return;
    }
    const qsizetype colon = text.indexOf(':');
    const QByteArrayView field = colon < 0 ? text : text.first(colon);
    QByteArrayView value = colon < 0 ? QByteArrayView() : text.sliced(colon + 1);
    if (!value.isEmpty() && value.front() == ' ') {
        value = value.sliced(1);
    }
    if (field == QByteArrayView("data")) {
        if (m_hasData) {
            m_data.append('\n');
        }
        m_data.append(value);
        m_hasData = true;
    } else if (field == QByteArrayView("event")) {
        m_name = value.toByteArray();
    }
}

void AiSseParser::dispatch()
{
    if (m_hasData && !m_data.isEmpty()) {
        m_events.push_back(AiSseEvent{.name = m_name.isEmpty() ? "message"_ba : m_name, .data = m_data});
    }
    m_name.clear();
    m_data.clear();
    m_hasData = false;
}

AiThinkSplit aiSplitThinkTags(const QString& content)
{
    static constexpr QStringView kOpen = u"<think>";
    static constexpr QStringView kClose = u"</think>";
    const QStringView rest = QStringView(content).trimmed();
    if (!rest.startsWith(kOpen)) {
        return {.thinking = {}, .text = content};
    }
    const QStringView inside = rest.sliced(kOpen.size());
    const qsizetype end = inside.indexOf(kClose);
    if (end < 0) {
        return {.thinking = inside.toString().trimmed(), .text = {}};
    }
    return {.thinking = inside.first(end).toString().trimmed(),
            .text = inside.sliced(end + kClose.size()).toString().trimmed()};
}

QJsonObject aiProviderRequestJson(const AiRequest& request)
{
    PictureBudget pictures(request);
    QJsonArray messages;
    for (const AiTurn& turn : request.turns) {
        switch (turn.role) {
        case AiTurn::Role::User: {
            QString text = turn.text;
            QJsonArray attached;
            for (const QJsonValue& value : turn.pictures) {
                const QJsonObject item = value.toObject();
                if (!isPicture(item)) {
                    continue;
                }
                if (pictures.take()) {
                    attached.append(providerPicture(item));
                } else {
                    text += u'\n' + pictures.leftOut();
                }
            }
            QJsonObject message{{u"role"_s, u"user"_s}, {u"text"_s, text}};
            if (!attached.isEmpty()) {
                message.insert(u"pictures"_s, attached);
            }
            messages.append(message);
            break;
        }
        case AiTurn::Role::Assistant: {
            if (turn.text.isEmpty() && turn.calls.empty() && turn.thinkingBlocks.isEmpty()) {
                break;
            }
            QJsonObject message{{u"role"_s, u"assistant"_s}, {u"text"_s, turn.text}};
            if (!turn.calls.empty()) {
                QJsonArray calls;
                for (const AiToolCall& call : turn.calls) {
                    calls.append(
                        QJsonObject{{u"id"_s, call.id}, {u"name"_s, call.name}, {u"arguments"_s, call.arguments}});
                }
                message.insert(u"toolCalls"_s, calls);
            }
            if (!turn.thinkingBlocks.isEmpty()) {
                message.insert(u"thinkingBlocks"_s, turn.thinkingBlocks);
            }
            messages.append(message);
            break;
        }
        case AiTurn::Role::Tools: {
            QJsonArray results;
            for (const AiToolResult& result : turn.results) {
                results.append(QJsonObject{{u"id"_s, result.id},
                                           {u"name"_s, result.name},
                                           {u"failed"_s, result.failed},
                                           {u"content"_s, providerContent(result.content, pictures)}});
            }
            messages.append(QJsonObject{{u"role"_s, u"tools"_s}, {u"toolResults"_s, results}});
            break;
        }
        }
    }
    QJsonArray tools;
    for (const QJsonValue& value : request.tools) {
        const QJsonObject tool = value.toObject();
        tools.append(QJsonObject{{u"name"_s, tool.value(u"name"_s)},
                                 {u"description"_s, tool.value(u"description"_s)},
                                 {u"inputSchema"_s, schemaOf(tool)}});
    }
    return {{u"model"_s, request.model},
            {u"system"_s, request.system},
            {u"messages"_s, messages},
            {u"tools"_s, tools},
            {u"stream"_s, request.stream},
            {u"maxTokens"_s, request.maxTokens > 0 ? request.maxTokens : AiRequest::kAnthropicMaxTokens},
            {u"think"_s, request.think},
            {u"effort"_s, request.effort},
            {u"longContext"_s, request.longContext}};
}

AiDelta aiProviderDelta(const QByteArray& json)
{
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
        return {};
    }
    const QJsonObject delta = document.object();
    AiDelta read{.text = delta.value(u"text"_s).toString(),
                 .thinking = delta.value(u"thinking"_s).toString(),
                 .calling = {}};
    const QJsonValue call = delta.value(u"toolCall"_s);
    const QString name = call.isObject() ? call.toObject().value(u"name"_s).toString() : call.toString();
    if (!name.isEmpty()) {
        read.calling.append(name);
    }
    return read;
}

AiReply aiProviderReply(const QByteArray& json, const QString& streamedText, const QString& streamedThinking)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return AiReply{.problem = aiUnreadable()};
    }
    const QJsonObject root = document.object();
    AiReply parsed;
    parsed.text = root.contains(u"text"_s) ? root.value(u"text"_s).toString() : streamedText;
    parsed.thinking = root.contains(u"thinking"_s) ? root.value(u"thinking"_s).toString() : streamedThinking;
    parsed.thinkingBlocks = root.value(u"thinkingBlocks"_s).toArray();
    for (const QJsonValue& value : root.value(u"toolCalls"_s).toArray()) {
        const QJsonObject call = value.toObject();
        AiToolCall read{.id = call.value(u"id"_s).toString(), .name = call.value(u"name"_s).toString()};
        readArguments(call.value(u"arguments"_s), read);
        if (!read.name.isEmpty()) {
            parsed.calls.push_back(std::move(read));
        }
    }
    parsed.stop = root.value(u"stop"_s).toString();
    parsed.usage = usageOf(root.value(u"usage"_s).toObject(), u"input"_s, u"output"_s);
    const QString problem = tidy(root.value(u"problem"_s).toString());
    if (!problem.isEmpty()) {
        parsed.problem = problem;
        parsed.status = root.value(u"status"_s).toInt();
        parsed.code = root.value(u"code"_s).toString();
    }
    return parsed;
}

AiDelta AiStream::feed(const AiSseEvent& event)
{
    const QByteArray data = event.data.trimmed();
    if (m_kind == AiKind::OpenAi && data == "[DONE]"_ba) {
        m_done = true;
        return {};
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }
    const QJsonObject object = document.object();
    if (m_kind == AiKind::Anthropic) {
        return anthropic(object);
    }
    if (m_kind == AiKind::OpenAiResponses) {
        return responses(object);
    }
    return openAi(object);
}

void AiStream::content(const QString& piece, AiDelta& delta)
{
    static constexpr QStringView kOpen = u"<think>";
    static constexpr QStringView kClose = u"</think>";
    QString pending = std::exchange(m_held, {}) + piece;
    while (!pending.isEmpty()) {
        if (m_tag == Tag::Unknown) {
            const QStringView lead = QStringView(pending).trimmed();
            if (lead.isEmpty() || (lead.size() < kOpen.size() && kOpen.startsWith(lead))) {
                m_held = pending;
                return;
            }
            if (!lead.startsWith(kOpen)) {
                m_tag = Tag::Outside;
                continue;
            }
            m_tag = Tag::Inside;
            pending = lead.sliced(kOpen.size()).toString();
            continue;
        }
        if (m_tag == Tag::Inside) {
            const qsizetype end = pending.indexOf(kClose);
            if (end >= 0) {
                thought(pending.left(end), delta);
                m_tag = Tag::Outside;
                pending = QStringView(pending).sliced(end + kClose.size()).trimmed().toString();
                continue;
            }
            qsizetype kept = 0;
            for (qsizetype size = std::min(pending.size(), kClose.size() - 1); size > 0; --size) {
                if (kClose.startsWith(QStringView(pending).last(size))) {
                    kept = size;
                    break;
                }
            }
            thought(pending.left(pending.size() - kept), delta);
            m_held = pending.right(kept);
            return;
        }
        m_text += pending;
        delta.text += pending;
        return;
    }
}

void AiStream::thought(const QString& piece, AiDelta& delta)
{
    if (piece.isEmpty()) {
        return;
    }
    m_thinking += piece;
    delta.thinking += piece;
}

AiReply AiStream::reply() const
{
    AiReply assembled{.text = m_tag == Tag::Inside ? m_text : m_text + m_held,
                      .thinking = m_tag == Tag::Inside ? m_thinking + m_held : m_thinking,
                      .stop = m_stop,
                      .usage = m_usage,
                      .problem = m_problem,
                      .code = m_code};
    for (const auto& entry : m_thinkingBlocks) {
        assembled.thinkingBlocks.append(entry.second);
    }
    for (const auto& entry : m_calls) {
        const Partial& partial = entry.second;
        AiToolCall call{.id = partial.id, .name = partial.name};
        if (partial.json.trimmed().isEmpty()) {
            call.arguments = partial.start;
        } else {
            readArguments(QJsonValue(partial.json), call);
        }
        if (!call.name.isEmpty()) {
            assembled.calls.push_back(std::move(call));
        }
    }
    if (assembled.problem.isEmpty() && !complete()) {
        assembled.problem =
            QCoreApplication::translate("AiService", "The answer stopped before it was complete. Try again.");
    }
    return assembled;
}

AiDelta AiStream::openAi(const QJsonObject& chunk)
{
    const QJsonValue error = chunk.value(u"error"_s);
    if (error.isObject() || error.isString()) {
        fail(error);
        return {};
    }
    const QJsonObject usage = chunk.value(u"usage"_s).toObject();
    if (!usage.isEmpty()) {
        m_usage = usageOf(usage, u"prompt_tokens"_s, u"completion_tokens"_s);
    }
    const QJsonArray choices = chunk.value(u"choices"_s).toArray();
    if (choices.isEmpty()) {
        return {};
    }
    const QJsonObject choice = choices.at(0).toObject();
    const QJsonObject delta = choice.value(u"delta"_s).toObject();
    AiDelta out;
    const QJsonValue reasoning =
        delta.value(u"reasoning_content"_s).isString() ? delta.value(u"reasoning_content"_s) : delta.value(u"reasoning"_s);
    if (reasoning.isString()) {
        thought(reasoning.toString(), out);
    }
    const QJsonValue piece = delta.value(u"content"_s);
    if (piece.isString()) {
        content(piece.toString(), out);
    }
    for (const QJsonValue& value : delta.value(u"tool_calls"_s).toArray()) {
        const QJsonObject call = value.toObject();
        const QString id = call.value(u"id"_s).toString();
        int index = -1;
        if (call.value(u"index"_s).isDouble()) {
            index = call.value(u"index"_s).toInt();
        } else {
            for (const auto& entry : m_calls) {
                if (!id.isEmpty() && entry.second.id == id) {
                    index = entry.first;
                }
            }
            if (index < 0) {
                const int last = m_calls.empty() ? -1 : m_calls.rbegin()->first;
                index = id.isEmpty() && last >= 0 ? last : last + 1;
            }
        }
        Partial& partial = m_calls[index];
        if (!id.isEmpty()) {
            partial.id = id;
        }
        const QJsonObject function = call.value(u"function"_s).toObject();
        const QString name = function.value(u"name"_s).toString();
        if (!name.isEmpty() && partial.name.isEmpty()) {
            partial.name = name;
            out.calling.append(name);
        }
        const QJsonValue arguments = function.value(u"arguments"_s);
        if (arguments.isString()) {
            partial.json += arguments.toString();
        } else if (arguments.isObject()) {
            partial.json = compact(arguments.toObject());
        }
    }
    const QString finish = choice.value(u"finish_reason"_s).toString();
    if (!finish.isEmpty()) {
        m_stop = finish;
    }
    return out;
}

AiDelta AiStream::anthropic(const QJsonObject& data)
{
    const QString type = data.value(u"type"_s).toString();
    if (type == u"message_start") {
        m_usage = usageOf(data.value(u"message"_s).toObject().value(u"usage"_s).toObject(), u"input_tokens"_s,
                          u"output_tokens"_s);
        return {};
    }
    if (type == u"content_block_start") {
        const QJsonObject block = data.value(u"content_block"_s).toObject();
        const QString kind = block.value(u"type"_s).toString();
        const int index = data.value(u"index"_s).toInt();
        AiDelta out;
        if (kind == u"tool_use") {
            m_calls[index] = Partial{.id = block.value(u"id"_s).toString(),
                                     .name = block.value(u"name"_s).toString(),
                                     .json = {},
                                     .start = block.value(u"input"_s).toObject()};
            if (!m_calls[index].name.isEmpty()) {
                out.calling.append(m_calls[index].name);
            }
        } else if (kind == u"text") {
            const QString text = block.value(u"text"_s).toString();
            m_text += text;
            out.text = text;
        } else if (kind == u"thinking") {
            m_thinkingBlocks[index] = QJsonObject{{u"type"_s, u"thinking"_s},
                                                  {u"thinking"_s, block.value(u"thinking"_s).toString()},
                                                  {u"signature"_s, block.value(u"signature"_s).toString()}};
            thought(block.value(u"thinking"_s).toString(), out);
        } else if (kind == u"redacted_thinking") {
            m_thinkingBlocks[index] = block;
        }
        return out;
    }
    if (type == u"content_block_delta") {
        const QJsonObject delta = data.value(u"delta"_s).toObject();
        const QString kind = delta.value(u"type"_s).toString();
        if (kind == u"text_delta") {
            const QString text = delta.value(u"text"_s).toString();
            m_text += text;
            return AiDelta{.text = text, .thinking = {}, .calling = {}};
        }
        if (kind == u"thinking_delta" || kind == u"signature_delta") {
            const auto found = m_thinkingBlocks.find(data.value(u"index"_s).toInt());
            if (found == m_thinkingBlocks.end()) {
                return {};
            }
            QJsonObject& block = found->second;
            if (kind == u"signature_delta") {
                block.insert(u"signature"_s, block.value(u"signature"_s).toString() + delta.value(u"signature"_s).toString());
                return {};
            }
            const QString piece = delta.value(u"thinking"_s).toString();
            block.insert(u"thinking"_s, block.value(u"thinking"_s).toString() + piece);
            AiDelta out;
            thought(piece, out);
            return out;
        }
        if (kind == u"input_json_delta") {
            const auto found = m_calls.find(data.value(u"index"_s).toInt());
            if (found != m_calls.end()) {
                found->second.json += delta.value(u"partial_json"_s).toString();
            }
        }
        return {};
    }
    if (type == u"message_delta") {
        const QString stop = data.value(u"delta"_s).toObject().value(u"stop_reason"_s).toString();
        if (!stop.isEmpty()) {
            m_stop = stop;
        }
        const QJsonObject usage = data.value(u"usage"_s).toObject();
        if (usage.contains(u"output_tokens"_s)) {
            m_usage.output = usage.value(u"output_tokens"_s).toInteger();
        }
        if (usage.value(u"input_tokens"_s).toInteger() > 0) {
            m_usage.input = usage.value(u"input_tokens"_s).toInteger();
        }
        return {};
    }
    if (type == u"message_stop") {
        m_done = true;
    } else if (type == u"error") {
        fail(data.value(u"error"_s));
    }
    return {};
}

AiDelta AiStream::responses(const QJsonObject& data)
{
    const QString type = data.value(u"type"_s).toString();
    if (type == u"response.output_text.delta") {
        const QString text = data.value(u"delta"_s).toString();
        m_text += text;
        return AiDelta{.text = text, .thinking = {}, .calling = {}};
    }
    if (type == u"response.reasoning_summary_text.delta") {
        AiDelta out;
        thought(data.value(u"delta"_s).toString(), out);
        return out;
    }
    if (type == u"response.reasoning_summary_part.added") {
        AiDelta out;
        if (!m_thinking.isEmpty()) {
            thought(u"\n\n"_s, out);
        }
        return out;
    }
    if (type == u"response.output_item.added") {
        const QJsonObject item = data.value(u"item"_s).toObject();
        AiDelta out;
        if (item.value(u"type"_s).toString() == u"function_call") {
            const QString name = item.value(u"name"_s).toString();
            m_calls[data.value(u"output_index"_s).toInt()] = Partial{.id = item.value(u"call_id"_s).toString(),
                                                                    .name = name,
                                                                    .json = item.value(u"arguments"_s).toString(),
                                                                    .start = {}};
            if (!name.isEmpty()) {
                out.calling.append(name);
            }
        }
        return out;
    }
    if (type == u"response.function_call_arguments.delta") {
        const auto found = m_calls.find(data.value(u"output_index"_s).toInt());
        if (found != m_calls.end()) {
            found->second.json += data.value(u"delta"_s).toString();
        }
        return {};
    }
    if (type == u"response.function_call_arguments.done") {
        const auto found = m_calls.find(data.value(u"output_index"_s).toInt());
        if (found != m_calls.end()) {
            found->second.json = data.value(u"arguments"_s).toString();
        }
        return {};
    }
    if (type == u"response.completed" || type == u"response.incomplete") {
        const QJsonObject response = data.value(u"response"_s).toObject();
        m_usage = usageOf(response.value(u"usage"_s).toObject(), u"input_tokens"_s, u"output_tokens"_s);
        const QString status = response.value(u"status"_s).toString();
        if (status.isEmpty()) {
            m_stop = u"incomplete"_s;
        } else if (status == u"incomplete" &&
                   response.value(u"incomplete_details"_s).toObject().value(u"reason"_s).toString() ==
                       u"max_output_tokens") {
            m_stop = u"length"_s;
        } else {
            m_stop = status;
        }
        m_done = true;
        return {};
    }
    if (type == u"response.failed") {
        fail(data.value(u"response"_s).toObject().value(u"error"_s));
        return {};
    }
    if (type == u"error") {
        fail(data);
    }
    return {};
}

void AiStream::fail(const QJsonValue& error)
{
    QString message;
    if (error.isObject()) {
        const QJsonObject object = error.toObject();
        message = tidy(object.value(u"message"_s).toString());
        m_code = codeOf(object);
    } else {
        message = tidy(error.toString());
    }
    m_problem = message.isEmpty()
                    ? QCoreApplication::translate("AiService", "The service stopped with an error.")
                    : QCoreApplication::translate("AiService", "The service stopped with an error: %1").arg(message);
}

}
