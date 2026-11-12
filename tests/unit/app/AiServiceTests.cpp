#include "AiService.h"

#include <doctest/doctest.h>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <vector>

using gs::app::AiHeader;
using gs::app::AiKind;
using gs::app::AiReply;
using gs::app::AiRequest;
using gs::app::AiSseEvent;
using gs::app::AiSseParser;
using gs::app::AiStream;
using gs::app::AiToolCall;
using gs::app::AiToolResult;
using gs::app::AiTurn;
using namespace Qt::StringLiterals;

namespace {

QJsonObject textItem(const QString& text)
{
    return {{u"type"_s, u"text"_s}, {u"text"_s, text}};
}

QJsonObject imageItem(const QString& data)
{
    return {{u"type"_s, u"image"_s}, {u"data"_s, data}, {u"mimeType"_s, u"image/png"_s}};
}

QJsonArray sampleTools()
{
    const QJsonObject schema{{u"type"_s, u"object"_s},
                             {u"properties"_s, QJsonObject{{u"serial"_s, QJsonObject{{u"type"_s, u"string"_s}}}}}};
    return {QJsonObject{{u"name"_s, u"get_frame"_s}, {u"description"_s, u"Returns the picture"_s},
                        {u"inputSchema"_s, schema}}};
}

AiRequest sampleRequest()
{
    AiRequest request;
    request.model = u"test-model"_s;
    request.system = u"Be brief."_s;
    request.tools = sampleTools();
    request.turns.push_back(AiTurn{.role = AiTurn::Role::User, .text = u"Where is the fire button?"_s});
    request.turns.push_back(AiTurn{.role = AiTurn::Role::Assistant,
                                   .text = u"Let me look."_s,
                                   .calls = {AiToolCall{.id = u"call_1"_s,
                                                        .name = u"get_frame"_s,
                                                        .arguments = QJsonObject{{u"serial"_s, u"R58"_s}}}}});
    request.turns.push_back(AiTurn{.role = AiTurn::Role::Tools,
                                   .results = {AiToolResult{.id = u"call_1"_s,
                                                            .name = u"get_frame"_s,
                                                            .content = QJsonArray{textItem(u"1080 x 2400"_s),
                                                                                  imageItem(u"iVBORw0KGgo="_s)}}}});
    return request;
}

QByteArray headerValue(const std::vector<AiHeader>& headers, const QByteArray& name)
{
    for (const AiHeader& header : headers) {
        if (header.name.compare(name, Qt::CaseInsensitive) == 0) {
            return header.value;
        }
    }
    return {};
}

QByteArray sse(const QByteArray& data, const QByteArray& name = {})
{
    const QJsonDocument document = QJsonDocument::fromJson(data);
    const QByteArray line = document.isNull() ? data : document.toJson(QJsonDocument::Compact);
    QByteArray text;
    if (!name.isEmpty()) {
        text += "event: " + name + "\n";
    }
    return text + "data: " + line + "\n\n";
}

std::vector<AiSseEvent> parsed(const QByteArray& bytes, const std::vector<qsizetype>& cuts)
{
    AiSseParser parser;
    qsizetype start = 0;
    for (const qsizetype cut : cuts) {
        parser.feed(QByteArrayView(bytes).sliced(start, cut - start));
        start = cut;
    }
    parser.feed(QByteArrayView(bytes).sliced(start));
    parser.end();
    return parser.take();
}

struct Pieces {
    QStringList thinking;
    QStringList calling;
};

AiReply streamed(AiKind kind, const QByteArray& bytes, QStringList* deltas = nullptr, Pieces* pieces = nullptr)
{
    AiSseParser parser;
    AiStream stream(kind);
    parser.feed(bytes);
    parser.end();
    for (const AiSseEvent& event : parser.take()) {
        const gs::app::AiDelta delta = stream.feed(event);
        if (deltas != nullptr && !delta.text.isEmpty()) {
            deltas->append(delta.text);
        }
        if (pieces != nullptr) {
            if (!delta.thinking.isEmpty()) {
                pieces->thinking.append(delta.thinking);
            }
            pieces->calling += delta.calling;
        }
    }
    return stream.reply();
}

}

TEST_CASE("the endpoint adds the path each service expects and refuses what is not a web address")
{
    CHECK(gs::app::aiEndpoint(AiKind::OpenAi, {}).toString() == u"https://api.openai.com/v1/chat/completions");
    CHECK(gs::app::aiEndpoint(AiKind::OpenAi, u" https://api.deepseek.com/ "_s).toString() ==
          u"https://api.deepseek.com/chat/completions");
    CHECK(gs::app::aiEndpoint(AiKind::OpenAi, u"http://localhost:11434/v1/"_s).toString() ==
          u"http://localhost:11434/v1/chat/completions");
    CHECK(gs::app::aiEndpoint(AiKind::OpenAi, u"localhost:11434/v1"_s).toString() ==
          u"http://localhost:11434/v1/chat/completions");
    CHECK(gs::app::aiEndpoint(AiKind::OpenAi, u"dashscope.aliyuncs.com/compatible-mode/v1"_s).toString() ==
          u"https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions");
    CHECK(gs::app::aiEndpoint(AiKind::OpenAi, u"https://example.com/v1/chat/completions"_s).toString() ==
          u"https://example.com/v1/chat/completions");
    CHECK(gs::app::aiEndpoint(AiKind::Anthropic, {}).toString() == u"https://api.anthropic.com/v1/messages");
    CHECK(gs::app::aiEndpoint(AiKind::Anthropic, u"https://api.anthropic.com/v1"_s).toString() ==
          u"https://api.anthropic.com/v1/messages");
    CHECK(gs::app::aiEndpoint(AiKind::Anthropic, u"https://api.deepseek.com/anthropic"_s).toString() ==
          u"https://api.deepseek.com/anthropic/v1/messages");
    CHECK_FALSE(gs::app::aiEndpoint(AiKind::OpenAi, u"ftp://example.com"_s).isValid());
    CHECK_FALSE(gs::app::aiEndpoint(AiKind::OpenAi, u"not a web address"_s).isValid());
    CHECK(gs::app::aiDefaultAddress(AiKind::Anthropic) == u"https://api.anthropic.com");
    CHECK(gs::app::aiServiceName(AiKind::OpenAi) == u"openai");
    CHECK(gs::app::aiServiceName(AiKind::Anthropic) == u"anthropic");
}

TEST_CASE("the Responses path and the models path come from the same address rules")
{
    CHECK(gs::app::aiEndpoint(AiKind::OpenAiResponses, {}).toString() == u"https://api.openai.com/v1/responses");
    CHECK(gs::app::aiEndpoint(AiKind::OpenAiResponses, u"https://api.openai.com/v1"_s).toString() ==
          u"https://api.openai.com/v1/responses");
    CHECK(gs::app::aiServiceName(AiKind::OpenAiResponses) == u"openaiResponses");
    CHECK(gs::app::aiModelsEndpoint(AiKind::OpenAi, u"https://api.deepseek.com/chat/completions"_s).toString() ==
          u"https://api.deepseek.com/models");
    CHECK(gs::app::aiModelsEndpoint(AiKind::Anthropic, {}).toString() == u"https://api.anthropic.com/v1/models");
}

TEST_CASE("the headers carry the key the way each service wants it")
{
    const auto openAi = gs::app::aiHeaders(AiKind::OpenAi, u"sk-test"_s, true);
    CHECK(headerValue(openAi, "Authorization") == "Bearer sk-test");
    CHECK(headerValue(openAi, "Accept") == "text/event-stream");
    CHECK(headerValue(openAi, "Content-Type") == "application/json");
    CHECK(headerValue(gs::app::aiHeaders(AiKind::OpenAi, {}, false), "Authorization").isEmpty());
    CHECK(headerValue(gs::app::aiHeaders(AiKind::OpenAi, {}, false), "Accept") == "application/json");

    const auto anthropic = gs::app::aiHeaders(AiKind::Anthropic, u"sk-ant"_s, true);
    CHECK(headerValue(anthropic, "x-api-key") == "sk-ant");
    CHECK(headerValue(anthropic, "anthropic-version") == "2023-06-01");
    CHECK(headerValue(anthropic, "Authorization").isEmpty());
}

TEST_CASE("an OpenAI request lists the system prompt, the turns, the tool calls, their results and the tools")
{
    const QJsonObject body = gs::app::aiRequestJson(AiKind::OpenAi, sampleRequest());
    CHECK(body.value(u"model"_s).toString() == u"test-model");
    CHECK(body.value(u"stream"_s).toBool());
    CHECK(body.value(u"stream_options"_s).toObject().value(u"include_usage"_s).toBool());
    CHECK_FALSE(body.contains(u"max_tokens"_s));

    const QJsonArray messages = body.value(u"messages"_s).toArray();
    REQUIRE(messages.size() == 5);
    CHECK(messages.at(0).toObject().value(u"role"_s).toString() == u"system");
    CHECK(messages.at(0).toObject().value(u"content"_s).toString() == u"Be brief.");
    CHECK(messages.at(1).toObject().value(u"role"_s).toString() == u"user");

    const QJsonObject assistant = messages.at(2).toObject();
    CHECK(assistant.value(u"role"_s).toString() == u"assistant");
    CHECK(assistant.value(u"content"_s).toString() == u"Let me look.");
    const QJsonObject call = assistant.value(u"tool_calls"_s).toArray().at(0).toObject();
    CHECK(call.value(u"id"_s).toString() == u"call_1");
    CHECK(call.value(u"type"_s).toString() == u"function");
    CHECK(call.value(u"function"_s).toObject().value(u"name"_s).toString() == u"get_frame");
    const QJsonDocument arguments =
        QJsonDocument::fromJson(call.value(u"function"_s).toObject().value(u"arguments"_s).toString().toUtf8());
    CHECK(arguments.object().value(u"serial"_s).toString() == u"R58");

    const QJsonObject tool = messages.at(3).toObject();
    CHECK(tool.value(u"role"_s).toString() == u"tool");
    CHECK(tool.value(u"tool_call_id"_s).toString() == u"call_1");
    CHECK(tool.value(u"content"_s).toString().contains(u"1080 x 2400"));
    CHECK(tool.value(u"content"_s).toString().contains(u"attached in the next message"));

    const QJsonObject pictures = messages.at(4).toObject();
    CHECK(pictures.value(u"role"_s).toString() == u"user");
    const QJsonArray parts = pictures.value(u"content"_s).toArray();
    REQUIRE(parts.size() == 2);
    CHECK(parts.at(1).toObject().value(u"type"_s).toString() == u"image_url");
    CHECK(parts.at(1).toObject().value(u"image_url"_s).toObject().value(u"url"_s).toString() ==
          u"data:image/png;base64,iVBORw0KGgo=");

    const QJsonObject function = body.value(u"tools"_s).toArray().at(0).toObject();
    CHECK(function.value(u"type"_s).toString() == u"function");
    CHECK(function.value(u"function"_s).toObject().value(u"name"_s).toString() == u"get_frame");
    CHECK(function.value(u"function"_s).toObject().value(u"parameters"_s).toObject().value(u"type"_s).toString() ==
          u"object");
}

TEST_CASE("an Anthropic request puts the tool results and their pictures into one user message")
{
    AiRequest request = sampleRequest();
    request.turns.push_back(AiTurn{.role = AiTurn::Role::User, .text = u"Thanks"_s});
    const QJsonObject body = gs::app::aiRequestJson(AiKind::Anthropic, request);
    CHECK(body.value(u"system"_s).toString() == u"Be brief.");
    CHECK(body.value(u"max_tokens"_s).toInt() == AiRequest::kAnthropicMaxTokens);
    CHECK(body.value(u"stream"_s).toBool());
    CHECK_FALSE(body.contains(u"stream_options"_s));

    const QJsonArray messages = body.value(u"messages"_s).toArray();
    REQUIRE(messages.size() == 3);
    CHECK(messages.at(0).toObject().value(u"role"_s).toString() == u"user");

    const QJsonArray said = messages.at(1).toObject().value(u"content"_s).toArray();
    CHECK(messages.at(1).toObject().value(u"role"_s).toString() == u"assistant");
    REQUIRE(said.size() == 2);
    CHECK(said.at(0).toObject().value(u"text"_s).toString() == u"Let me look.");
    CHECK(said.at(1).toObject().value(u"type"_s).toString() == u"tool_use");
    CHECK(said.at(1).toObject().value(u"input"_s).toObject().value(u"serial"_s).toString() == u"R58");

    const QJsonObject results = messages.at(2).toObject();
    CHECK(results.value(u"role"_s).toString() == u"user");
    const QJsonArray content = results.value(u"content"_s).toArray();
    REQUIRE(content.size() == 2);
    const QJsonObject result = content.at(0).toObject();
    CHECK(result.value(u"type"_s).toString() == u"tool_result");
    CHECK(result.value(u"tool_use_id"_s).toString() == u"call_1");
    CHECK_FALSE(result.value(u"is_error"_s).toBool());
    const QJsonArray inner = result.value(u"content"_s).toArray();
    REQUIRE(inner.size() == 2);
    CHECK(inner.at(0).toObject().value(u"text"_s).toString() == u"1080 x 2400");
    const QJsonObject source = inner.at(1).toObject().value(u"source"_s).toObject();
    CHECK(inner.at(1).toObject().value(u"type"_s).toString() == u"image");
    CHECK(source.value(u"type"_s).toString() == u"base64");
    CHECK(source.value(u"media_type"_s).toString() == u"image/png");
    CHECK(source.value(u"data"_s).toString() == u"iVBORw0KGgo=");
    CHECK(content.at(1).toObject().value(u"text"_s).toString() == u"Thanks");

    const QJsonObject tool = body.value(u"tools"_s).toArray().at(0).toObject();
    CHECK(tool.value(u"name"_s).toString() == u"get_frame");
    CHECK(tool.value(u"input_schema"_s).toObject().value(u"type"_s).toString() == u"object");
}

TEST_CASE("an OpenAI Responses request lists the turns, the calls, their results and the tools")
{
    AiRequest request;
    request.model = u"test-model"_s;
    request.system = u"Be brief."_s;
    request.tools = sampleTools();
    request.turns.push_back(AiTurn{.role = AiTurn::Role::User, .text = u"hi"_s});
    request.turns.push_back(AiTurn{.role = AiTurn::Role::Assistant,
                                   .calls = {AiToolCall{.id = u"call_1"_s,
                                                        .name = u"list_devices"_s,
                                                        .arguments = QJsonObject{}}}});
    request.turns.push_back(AiTurn{.role = AiTurn::Role::Tools,
                                   .results = {AiToolResult{.id = u"call_1"_s,
                                                            .name = u"list_devices"_s,
                                                            .content = QJsonArray{textItem(u"none"_s)}}}});

    const QJsonObject body = gs::app::aiRequestJson(AiKind::OpenAiResponses, request);
    CHECK(body.value(u"model"_s).toString() == u"test-model");
    CHECK(body.value(u"instructions"_s).toString() == u"Be brief.");
    CHECK(body.value(u"store"_s).isBool());
    CHECK_FALSE(body.value(u"store"_s).toBool());
    CHECK(body.value(u"stream"_s).toBool());

    const QJsonArray input = body.value(u"input"_s).toArray();
    REQUIRE(input.size() == 3);
    CHECK(input.at(0).toObject().value(u"role"_s).toString() == u"user");
    CHECK(input.at(0).toObject().value(u"content"_s).toString() == u"hi");
    CHECK(input.at(1).toObject().value(u"type"_s).toString() == u"function_call");
    CHECK(input.at(1).toObject().value(u"call_id"_s).toString() == u"call_1");
    CHECK(input.at(1).toObject().value(u"name"_s).toString() == u"list_devices");
    CHECK(input.at(1).toObject().value(u"arguments"_s).toString() == u"{}");
    CHECK(input.at(2).toObject().value(u"type"_s).toString() == u"function_call_output");
    CHECK(input.at(2).toObject().value(u"call_id"_s).toString() == u"call_1");
    CHECK(input.at(2).toObject().value(u"output"_s).toString() == u"none");

    const QJsonObject tool = body.value(u"tools"_s).toArray().at(0).toObject();
    CHECK(tool.value(u"type"_s).toString() == u"function");
    CHECK(tool.value(u"name"_s).toString() == u"get_frame");
    CHECK(tool.value(u"parameters"_s).toObject().value(u"type"_s).toString() == u"object");
}

TEST_CASE("only the newest pictures go out, and none when the model takes no pictures")
{
    AiRequest request;
    request.model = u"m"_s;
    request.turns.push_back(AiTurn{.role = AiTurn::Role::User, .text = u"Look"_s});
    AiTurn calls{.role = AiTurn::Role::Assistant};
    AiTurn results{.role = AiTurn::Role::Tools};
    for (int i = 0; i < AiRequest::kMostPictures + 2; ++i) {
        const QString id = u"call_%1"_s.arg(i);
        calls.calls.push_back(AiToolCall{.id = id, .name = u"get_frame"_s});
        results.results.push_back(
            AiToolResult{.id = id, .name = u"get_frame"_s, .content = QJsonArray{imageItem(u"AAAA"_s)}});
    }
    request.turns.push_back(calls);
    request.turns.push_back(results);
    CHECK(gs::app::aiPictureCount(request) == AiRequest::kMostPictures);

    const QJsonArray messages = gs::app::aiRequestJson(AiKind::OpenAi, request).value(u"messages"_s).toArray();
    CHECK(messages.at(2).toObject().value(u"content"_s).toString().contains(u"older picture"));
    CHECK(messages.at(3).toObject().value(u"content"_s).toString().contains(u"older picture"));
    CHECK(messages.at(4).toObject().value(u"content"_s).toString().contains(u"attached in the next message"));
    CHECK(messages.last().toObject().value(u"content"_s).toArray().size() == AiRequest::kMostPictures + 1);

    request.pictures = false;
    CHECK(gs::app::aiPictureCount(request) == 0);
    const QJsonArray without = gs::app::aiRequestJson(AiKind::Anthropic, request).value(u"messages"_s).toArray();
    const QJsonArray inner = without.at(2).toObject().value(u"content"_s).toArray().at(0).toObject()
                                 .value(u"content"_s).toArray();
    CHECK(inner.at(0).toObject().value(u"type"_s).toString() == u"text");
    CHECK(inner.at(0).toObject().value(u"text"_s).toString().contains(u"does not take pictures"));
}

TEST_CASE("a whole OpenAI reply gives the answer, the tool calls and the token counts")
{
    const AiReply plain = gs::app::aiReplyOf(AiKind::OpenAi, R"({"choices": [{"message": {"role": "assistant",
        "content": "Hello there"}, "finish_reason": "stop"}], "usage": {"prompt_tokens": 12, "completion_tokens": 3}})");
    CHECK(plain.problem.isEmpty());
    CHECK(plain.text == u"Hello there");
    CHECK(plain.calls.empty());
    CHECK(plain.stop == u"stop");
    CHECK(plain.usage.input == 12);
    CHECK(plain.usage.output == 3);

    const AiReply tools = gs::app::aiReplyOf(AiKind::OpenAi, R"({"choices": [{"message": {"role": "assistant",
        "content": null, "tool_calls": [
          {"id": "call_a", "type": "function", "function": {"name": "get_frame", "arguments": "{\"maxSide\": 512}"}},
          {"id": "call_b", "type": "function", "function": {"name": "read_logs", "arguments": ""}}]},
        "finish_reason": "tool_calls"}]})");
    CHECK(tools.problem.isEmpty());
    CHECK(tools.text.isEmpty());
    REQUIRE(tools.calls.size() == 2);
    CHECK(tools.calls[0].id == u"call_a");
    CHECK(tools.calls[0].name == u"get_frame");
    CHECK(tools.calls[0].arguments.value(u"maxSide"_s).toInt() == 512);
    CHECK(tools.calls[1].name == u"read_logs");
    CHECK(tools.calls[1].arguments.isEmpty());
    CHECK(tools.calls[1].problem.isEmpty());

    CHECK_FALSE(gs::app::aiReplyOf(AiKind::OpenAi, "not json").problem.isEmpty());
    CHECK_FALSE(gs::app::aiReplyOf(AiKind::OpenAi, R"({"choices": []})").problem.isEmpty());
}

TEST_CASE("a whole Anthropic reply gives the answer, the tool calls and the token counts")
{
    const AiReply reply = gs::app::aiReplyOf(AiKind::Anthropic, R"({"type": "message", "role": "assistant",
        "content": [{"type": "text", "text": "I will look."},
                    {"type": "tool_use", "id": "toolu_1", "name": "get_frame", "input": {"serial": "R58"}},
                    {"type": "tool_use", "id": "toolu_2", "name": "read_logs", "input": {}}],
        "stop_reason": "tool_use", "usage": {"input_tokens": 40, "output_tokens": 9}})");
    CHECK(reply.problem.isEmpty());
    CHECK(reply.text == u"I will look.");
    REQUIRE(reply.calls.size() == 2);
    CHECK(reply.calls[0].id == u"toolu_1");
    CHECK(reply.calls[0].arguments.value(u"serial"_s).toString() == u"R58");
    CHECK(reply.calls[1].name == u"read_logs");
    CHECK(reply.stop == u"tool_use");
    CHECK(reply.usage.input == 40);
    CHECK(reply.usage.output == 9);
}

TEST_CASE("a whole OpenAI Responses reply gives the answer, the tool calls and the token counts")
{
    const AiReply reply = gs::app::aiReplyOf(AiKind::OpenAiResponses, R"({"status": "completed", "output": [
        {"type": "message", "content": [{"type": "output_text", "text": "Hello"}]},
        {"type": "function_call", "call_id": "c1", "name": "tap", "arguments": "{\"x\": 1}"}],
        "usage": {"input_tokens": 5, "output_tokens": 7}})");
    CHECK(reply.problem.isEmpty());
    CHECK(reply.text == u"Hello");
    REQUIRE(reply.calls.size() == 1);
    CHECK(reply.calls[0].id == u"c1");
    CHECK(reply.calls[0].name == u"tap");
    CHECK(reply.calls[0].arguments.value(u"x"_s).toInt() == 1);
    CHECK(reply.stop == u"completed");
    CHECK(reply.usage.input == 5);
    CHECK(reply.usage.output == 7);

    CHECK_FALSE(gs::app::aiReplyOf(AiKind::OpenAiResponses, R"({"status": "completed"})").problem.isEmpty());
}

TEST_CASE("error bodies turn into a sentence with what the service says")
{
    const AiReply key = gs::app::aiFailure(401, R"({"error": {"message": "Incorrect API key provided: sk-...abcd.",
        "type": "invalid_request_error", "code": "invalid_api_key"}})");
    CHECK(key.status == 401);
    CHECK(key.problem.contains(u"HTTP 401"));
    CHECK(key.problem.contains(u"Incorrect API key provided"));
    CHECK(key.code == u"invalid_api_key");

    const AiReply busy =
        gs::app::aiFailure(529, R"({"type": "error", "error": {"type": "overloaded_error", "message": "Overloaded"}})");
    CHECK(busy.problem.contains(u"HTTP 529"));
    CHECK(busy.problem.contains(u"Overloaded"));
    CHECK(busy.code == u"overloaded_error");

    const AiReply ollama = gs::app::aiFailure(404, R"({"error": "model \"llama9\" not found, try pulling it first"})");
    CHECK(ollama.problem.contains(u"HTTP 404"));
    CHECK(ollama.problem.contains(u"llama9"));

    const AiReply page = gs::app::aiFailure(502, "<html><body>Bad gateway</body></html>");
    CHECK(page.problem.contains(u"HTTP 502"));
    CHECK_FALSE(page.problem.contains(u"<html>"));

    const AiReply plain = gs::app::aiFailure(429, "Rate limit reached\nretry later");
    CHECK(plain.problem.contains(u"HTTP 429"));
    CHECK(plain.problem.contains(u"Rate limit reached"));
    CHECK_FALSE(plain.problem.contains(u"retry later"));

    const AiReply inside = gs::app::aiReplyOf(AiKind::OpenAi, R"({"error": {"message": "Model is loading"}})");
    CHECK(inside.problem.contains(u"Model is loading"));

    CHECK(gs::app::aiNetworkProblem(QNetworkReply::HostNotFoundError, u"api.example.com"_s, {})
              .contains(u"api.example.com"));
    CHECK(gs::app::aiNetworkProblem(QNetworkReply::ConnectionRefusedError, u"localhost"_s, {}).contains(u"localhost"));
}

TEST_CASE("server-sent events split at any byte give the same events")
{
    const QByteArray stream = ": keep-alive\r\n"
                              "event: message_start\r\n"
                              "data: {\"a\": 1}\r\n"
                              "\r\n"
                              "data: first line\n"
                              "data: second line\n"
                              "\n"
                              "id: 7\n"
                              "retry: 100\n"
                              "data:{\"b\":\"\xE4\xBD\xA0\xE5\xA5\xBD\"}\r"
                              "\r"
                              "data: last";
    const std::vector<AiSseEvent> whole = parsed(stream, {});
    REQUIRE(whole.size() == 4);
    CHECK(whole[0].name == "message_start");
    CHECK(whole[0].data == "{\"a\": 1}");
    CHECK(whole[1].name == "message");
    CHECK(whole[1].data == "first line\nsecond line");
    CHECK(whole[2].data == "{\"b\":\"\xE4\xBD\xA0\xE5\xA5\xBD\"}");
    CHECK(whole[3].data == "last");

    for (qsizetype cut = 1; cut < stream.size(); ++cut) {
        const std::vector<AiSseEvent> split = parsed(stream, {cut});
        REQUIRE(split.size() == whole.size());
        for (std::size_t i = 0; i < whole.size(); ++i) {
            CHECK(split[i].name == whole[i].name);
            CHECK(split[i].data == whole[i].data);
        }
    }

    std::vector<qsizetype> each;
    for (qsizetype at = 1; at < stream.size(); ++at) {
        each.push_back(at);
    }
    const std::vector<AiSseEvent> bytes = parsed(stream, each);
    REQUIRE(bytes.size() == whole.size());
    CHECK(bytes[1].data == whole[1].data);

    QRandomGenerator random(20261106);
    for (int round = 0; round < 50; ++round) {
        std::vector<qsizetype> cuts;
        for (qsizetype at = random.bounded(1, 9); at < stream.size(); at += random.bounded(1, 9)) {
            cuts.push_back(at);
        }
        const std::vector<AiSseEvent> chunks = parsed(stream, cuts);
        REQUIRE(chunks.size() == whole.size());
        CHECK(chunks[2].data == whole[2].data);
    }
}

TEST_CASE("an OpenAI stream gives the text as it comes and puts the tool calls together from pieces")
{
    QByteArray bytes;
    bytes += sse(R"({"choices": [{"index": 0, "delta": {"role": "assistant", "content": ""}}]})");
    bytes += sse(R"({"choices": [{"index": 0, "delta": {"content": "Let me "}}]})");
    bytes += sse(R"({"choices": [{"index": 0, "delta": {"content": "look."}}]})");
    bytes += sse(R"({"choices": [{"index": 0, "delta": {"tool_calls": [{"index": 0, "id": "call_a", "type": "function",
                  "function": {"name": "get_frame", "arguments": ""}}]}}]})");
    bytes += sse(R"({"choices": [{"index": 0, "delta": {"tool_calls": [{"index": 0,
                  "function": {"arguments": "{\"max"}}]}}]})");
    bytes += sse(R"({"choices": [{"index": 0, "delta": {"tool_calls": [{"index": 1, "id": "call_b",
                  "function": {"name": "read_logs", "arguments": "{}"}}]}}]})");
    bytes += sse(R"({"choices": [{"index": 0, "delta": {"tool_calls": [{"index": 0,
                  "function": {"arguments": "Side\": 640}"}}]}}]})");
    bytes += sse(R"({"choices": [{"index": 0, "delta": {}, "finish_reason": "tool_calls"}]})");
    bytes += sse(R"({"choices": [], "usage": {"prompt_tokens": 100, "completion_tokens": 20}})");
    bytes += sse("[DONE]");

    QStringList deltas;
    const AiReply reply = streamed(AiKind::OpenAi, bytes, &deltas);
    CHECK(deltas == QStringList{u"Let me "_s, u"look."_s});
    CHECK(reply.problem.isEmpty());
    CHECK(reply.text == u"Let me look.");
    REQUIRE(reply.calls.size() == 2);
    CHECK(reply.calls[0].id == u"call_a");
    CHECK(reply.calls[0].name == u"get_frame");
    CHECK(reply.calls[0].arguments.value(u"maxSide"_s).toInt() == 640);
    CHECK(reply.calls[1].id == u"call_b");
    CHECK(reply.calls[1].arguments.isEmpty());
    CHECK(reply.stop == u"tool_calls");
    CHECK(reply.usage.input == 100);
    CHECK(reply.usage.output == 20);
}

TEST_CASE("ADR-0099 2026-11-13: a stream that goes on for longer than the most text stops instead of reading it all")
{
    const QByteArray piece(64 * 1024, 'x');
    QByteArray event = "data: {\"choices\": [{\"index\": 0, \"delta\": {\"content\": \"";
    event += piece;
    event += "\"}}]}\n\n";
    QByteArray bytes;
    for (int index = 0; index < 80; ++index) {
        bytes += event;
    }
    CHECK(bytes.size() > AiStream::kMostAnswerCharacters);

    QStringList deltas;
    const AiReply reply = streamed(AiKind::OpenAi, bytes, &deltas);
    CHECK_FALSE(reply.problem.isEmpty());
    CHECK(reply.problem.contains(u"too long"_s));
    CHECK(reply.text.size() <= AiStream::kMostAnswerCharacters);
    CHECK_FALSE(deltas.isEmpty());
    CHECK(deltas.size() < 80);
    CHECK(deltas.size() == reply.text.size() / piece.size());
}

TEST_CASE("an Anthropic stream gives the text as it comes and puts the tool input together from partial JSON")
{
    QByteArray bytes;
    bytes += sse(R"({"type": "message_start", "message": {"usage": {"input_tokens": 30, "output_tokens": 1}}})",
                 "message_start");
    bytes += sse(R"({"type": "content_block_start", "index": 0, "content_block": {"type": "text", "text": ""}})",
                 "content_block_start");
    bytes += sse(R"({"type": "ping"})", "ping");
    bytes += sse(R"({"type": "content_block_delta", "index": 0, "delta": {"type": "text_delta", "text": "Looking"}})",
                 "content_block_delta");
    bytes += sse(R"({"type": "content_block_delta", "index": 0, "delta": {"type": "text_delta", "text": " now."}})",
                 "content_block_delta");
    bytes += sse(R"({"type": "content_block_stop", "index": 0})", "content_block_stop");
    bytes += sse(R"({"type": "content_block_start", "index": 1, "content_block": {"type": "tool_use",
                  "id": "toolu_9", "name": "get_frame", "input": {}}})",
                 "content_block_start");
    bytes += sse(R"({"type": "content_block_delta", "index": 1, "delta": {"type": "input_json_delta",
                  "partial_json": "{\"serial\": \"R5"}})",
                 "content_block_delta");
    bytes += sse(R"({"type": "content_block_delta", "index": 1, "delta": {"type": "input_json_delta",
                  "partial_json": "8\"}"}})",
                 "content_block_delta");
    bytes += sse(R"({"type": "content_block_start", "index": 2, "content_block": {"type": "tool_use",
                  "id": "toolu_10", "name": "read_logs", "input": {}}})",
                 "content_block_start");
    bytes += sse(R"({"type": "message_delta", "delta": {"stop_reason": "tool_use"}, "usage": {"output_tokens": 44}})",
                 "message_delta");
    bytes += sse(R"({"type": "message_stop"})", "message_stop");

    QStringList deltas;
    const AiReply reply = streamed(AiKind::Anthropic, bytes, &deltas);
    CHECK(deltas == QStringList{u"Looking"_s, u" now."_s});
    CHECK(reply.problem.isEmpty());
    CHECK(reply.text == u"Looking now.");
    REQUIRE(reply.calls.size() == 2);
    CHECK(reply.calls[0].id == u"toolu_9");
    CHECK(reply.calls[0].arguments.value(u"serial"_s).toString() == u"R58");
    CHECK(reply.calls[1].name == u"read_logs");
    CHECK(reply.calls[1].arguments.isEmpty());
    CHECK(reply.stop == u"tool_use");
    CHECK(reply.usage.input == 30);
    CHECK(reply.usage.output == 44);
}

TEST_CASE("an OpenAI Responses stream gives the text and the call arguments as they come")
{
    QByteArray bytes;
    bytes += sse(R"({"type": "response.output_text.delta", "delta": "He"})");
    bytes += sse(R"({"type": "response.output_text.delta", "delta": "llo"})");
    bytes += sse(R"({"type": "response.output_item.added", "output_index": 1,
                  "item": {"type": "function_call", "call_id": "c1", "name": "tap", "arguments": ""}})");
    bytes += sse(R"({"type": "response.function_call_arguments.delta", "output_index": 1, "delta": "{\"x\":"})");
    bytes += sse(R"({"type": "response.function_call_arguments.delta", "output_index": 1, "delta": "1}"})");
    bytes += sse(R"({"type": "response.completed", "response": {"status": "completed",
                  "usage": {"input_tokens": 3, "output_tokens": 4}}})");

    QStringList deltas;
    const AiReply reply = streamed(AiKind::OpenAiResponses, bytes, &deltas);
    CHECK(deltas == QStringList{u"He"_s, u"llo"_s});
    CHECK(reply.problem.isEmpty());
    CHECK(reply.text == u"Hello");
    REQUIRE(reply.calls.size() == 1);
    CHECK(reply.calls[0].id == u"c1");
    CHECK(reply.calls[0].name == u"tap");
    CHECK(reply.calls[0].arguments.value(u"x"_s).toInt() == 1);
    CHECK(reply.stop == u"completed");
    CHECK(reply.usage.input == 3);
    CHECK(reply.usage.output == 4);

    QByteArray cut;
    cut += sse(R"({"type": "response.output_text.delta", "delta": "Half"})");
    cut += sse(R"({"type": "response.incomplete", "response": {"status": "incomplete",
                "incomplete_details": {"reason": "max_output_tokens"}}})");
    const AiReply short_ = streamed(AiKind::OpenAiResponses, cut);
    CHECK(short_.stop == u"length");
    CHECK(short_.text == u"Half");
}

TEST_CASE("the model list reads the shapes the services use")
{
    CHECK(gs::app::aiModelsOf(R"({"data": [{"id": "b"}, {"id": "a"}, {"id": "a"}]}") ==
          QStringList{u"a"_s, u"b"_s});
    CHECK(gs::app::aiModelsOf(R"({"models": [{"name": "llama3.1"}]}") == QStringList{u"llama3.1"_s});
    CHECK(gs::app::aiModelsOf("not json").isEmpty());
    CHECK(gs::app::aiModelsOf(R"({"data": []})").isEmpty());
}

TEST_CASE("a stream that breaks off, carries an error or bad arguments reports it")
{
    QByteArray cut;
    cut += sse(R"({"choices": [{"index": 0, "delta": {"content": "Half"}}]})");
    const AiReply early = streamed(AiKind::OpenAi, cut);
    CHECK(early.text == u"Half");
    CHECK(early.problem.contains(u"stopped before it was complete"));

    QByteArray overloaded;
    overloaded += sse(R"({"type": "message_start", "message": {"usage": {}}})", "message_start");
    overloaded += sse(R"({"type": "error", "error": {"type": "overloaded_error", "message": "Overloaded"}})", "error");
    const AiReply anthropic = streamed(AiKind::Anthropic, overloaded);
    CHECK(anthropic.problem.contains(u"Overloaded"));
    CHECK(anthropic.code == u"overloaded_error");

    const AiReply openAi = streamed(AiKind::OpenAi, sse(R"({"error": {"message": "context too long", "code": 400}})"));
    CHECK(openAi.problem.contains(u"context too long"));
    CHECK(openAi.code == u"400");

    QByteArray broken;
    broken += sse(R"({"choices": [{"index": 0, "delta": {"tool_calls": [{"index": 0, "id": "call_x",
                  "function": {"name": "get_frame", "arguments": "{\"maxSide\": "}}]}}]})");
    broken += sse(R"({"choices": [{"index": 0, "delta": {}, "finish_reason": "length"}]})");
    const AiReply bad = streamed(AiKind::OpenAi, broken);
    CHECK(bad.problem.isEmpty());
    CHECK(bad.stop == u"length");
    REQUIRE(bad.calls.size() == 1);
    CHECK(bad.calls[0].problem.contains(u"get_frame"));
}

TEST_CASE("an answer splits into prose and fenced code blocks")
{
    const QVariantList blocks =
        gs::app::aiBlocks(u"Here is the logic:\n\n```ts\nexport default function () {\n  tap(0.5, 0.5);\n}\n```\nDone."_s);
    REQUIRE(blocks.size() == 3);
    CHECK_FALSE(blocks[0].toMap().value(u"code"_s).toBool());
    CHECK(blocks[0].toMap().value(u"text"_s).toString() == u"Here is the logic:");
    CHECK(blocks[1].toMap().value(u"code"_s).toBool());
    CHECK(blocks[1].toMap().value(u"language"_s).toString() == u"ts");
    CHECK(blocks[1].toMap().value(u"text"_s).toString() == u"export default function () {\n  tap(0.5, 0.5);\n}");
    CHECK(blocks[2].toMap().value(u"text"_s).toString() == u"Done.");

    const QVariantList open = gs::app::aiBlocks(u"Writing:\n~~~json\n{\"a\": 1"_s);
    REQUIRE(open.size() == 2);
    CHECK(open[1].toMap().value(u"code"_s).toBool());
    CHECK(open[1].toMap().value(u"text"_s).toString() == u"{\"a\": 1");

    CHECK(gs::app::aiBlocks(u"   \n\n"_s).isEmpty());
    CHECK(gs::app::aiContentText(QJsonArray{textItem(u"one"_s), imageItem(u"x"_s), textItem(u"two"_s)}) ==
          u"one\ntwo");
}

TEST_CASE("ADR-0099 01:5x: reasoning from OpenAI-compatible services comes out as thinking, also from think tags")
{
    QByteArray reasoned;
    reasoned += sse(R"({"choices": [{"index": 0, "delta": {"reasoning_content": "Let me "}}]})");
    reasoned += sse(R"({"choices": [{"index": 0, "delta": {"reasoning_content": "think."}}]})");
    reasoned += sse(R"({"choices": [{"index": 0, "delta": {"content": "Hello"}}]})");
    reasoned += sse(R"({"choices": [{"index": 0, "delta": {"tool_calls": [{"index": 0, "id": "c1",
                    "function": {"name": "read_help", "arguments": ""}}]}}]})");
    reasoned += sse(R"({"choices": [{"index": 0, "delta": {"tool_calls": [{"index": 0,
                    "function": {"arguments": "{\"topic\": \"connecting\"}"}}]}}]})");
    reasoned += sse(R"({"choices": [{"index": 0, "delta": {}, "finish_reason": "tool_calls"}]})");
    reasoned += sse("[DONE]");
    QStringList text;
    Pieces pieces;
    const AiReply reply = streamed(AiKind::OpenAi, reasoned, &text, &pieces);
    CHECK(reply.problem.isEmpty());
    CHECK(reply.thinking == u"Let me think.");
    CHECK(pieces.thinking == QStringList{u"Let me "_s, u"think."_s});
    CHECK(reply.text == u"Hello");
    CHECK(text == QStringList{u"Hello"_s});
    CHECK(pieces.calling == QStringList{u"read_help"_s});
    REQUIRE(reply.calls.size() == 1);
    CHECK(reply.calls[0].arguments.value(u"topic"_s).toString() == u"connecting");

    QByteArray tagged;
    for (const char* piece : {"<thi", "nk>pondering", " more</thi", "nk>\n\nAnswer", " here"}) {
        tagged += sse(QJsonDocument(QJsonObject{{u"choices"_s,
                                                 QJsonArray{QJsonObject{{u"index"_s, 0},
                                                                        {u"delta"_s, QJsonObject{{u"content"_s,
                                                                                                  QString::fromUtf8(piece)}}}}}}})
                          .toJson(QJsonDocument::Compact));
    }
    tagged += sse(R"({"choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]})");
    QStringList visible;
    Pieces thought;
    const AiReply split = streamed(AiKind::OpenAi, tagged, &visible, &thought);
    CHECK(split.thinking == u"pondering more");
    CHECK(split.text == u"Answer here");
    CHECK(visible.join(QString()) == u"Answer here");
    CHECK(thought.thinking.join(QString()) == u"pondering more");

    QByteArray plain;
    plain += sse(R"({"choices": [{"index": 0, "delta": {"content": "<"}}]})");
    plain += sse(R"({"choices": [{"index": 0, "delta": {"content": "b>bold"}}]})");
    plain += sse(R"({"choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]})");
    const AiReply bold = streamed(AiKind::OpenAi, plain);
    CHECK(bold.text == u"<b>bold");
    CHECK(bold.thinking.isEmpty());

    const AiReply whole = gs::app::aiReplyOf(AiKind::OpenAi, R"({"choices": [{"message": {"role": "assistant",
        "content": "Sure.", "reasoning_content": "Easy one."}, "finish_reason": "stop"}]})");
    CHECK(whole.text == u"Sure.");
    CHECK(whole.thinking == u"Easy one.");
    const AiReply local = gs::app::aiReplyOf(AiKind::OpenAi, R"({"choices": [{"message": {"role": "assistant",
        "content": "<think>\nshort\n</think>\n\nDone."}, "finish_reason": "stop"}]})");
    CHECK(local.text == u"Done.");
    CHECK(local.thinking == u"short");
    CHECK(gs::app::aiSplitThinkTags(u"no tags"_s).text == u"no tags");
    CHECK(gs::app::aiSplitThinkTags(u"<think>open"_s).thinking == u"open");
}

TEST_CASE("ADR-0099 01:5x: Claude's thinking streams with its signature and goes back with the tool call")
{
    QByteArray events;
    events += sse(R"({"type": "message_start", "message": {"usage": {"input_tokens": 10, "output_tokens": 0}}})",
                  "message_start");
    events += sse(R"({"type": "content_block_start", "index": 0,
                  "content_block": {"type": "thinking", "thinking": "", "signature": ""}})",
                  "content_block_start");
    events += sse(R"({"type": "content_block_delta", "index": 0,
                  "delta": {"type": "thinking_delta", "thinking": "Plan: "}})",
                  "content_block_delta");
    events += sse(R"({"type": "content_block_delta", "index": 0,
                  "delta": {"type": "thinking_delta", "thinking": "read help."}})",
                  "content_block_delta");
    events += sse(R"({"type": "content_block_delta", "index": 0, "delta": {"type": "signature_delta", "signature": "sig"}})",
                  "content_block_delta");
    events += sse(R"({"type": "content_block_delta", "index": 0, "delta": {"type": "signature_delta", "signature": "123"}})",
                  "content_block_delta");
    events += sse(R"({"type": "content_block_start", "index": 1,
                  "content_block": {"type": "redacted_thinking", "data": "opaque"}})",
                  "content_block_start");
    events += sse(R"({"type": "content_block_start", "index": 2,
                  "content_block": {"type": "tool_use", "id": "t1", "name": "read_help", "input": {}}})",
                  "content_block_start");
    events += sse(R"({"type": "content_block_delta", "index": 2,
                  "delta": {"type": "input_json_delta", "partial_json": "{\"topic\": \"connecting\"}"}})",
                  "content_block_delta");
    events += sse(R"({"type": "message_delta", "delta": {"stop_reason": "tool_use"}, "usage": {"output_tokens": 20}})",
                  "message_delta");
    events += sse(R"({"type": "message_stop"})", "message_stop");
    Pieces pieces;
    const AiReply reply = streamed(AiKind::Anthropic, events, nullptr, &pieces);
    CHECK(reply.problem.isEmpty());
    CHECK(reply.thinking == u"Plan: read help.");
    CHECK(pieces.thinking == QStringList{u"Plan: "_s, u"read help."_s});
    CHECK(pieces.calling == QStringList{u"read_help"_s});
    REQUIRE(reply.thinkingBlocks.size() == 2);
    const QJsonObject first = reply.thinkingBlocks.at(0).toObject();
    CHECK(first.value(u"type"_s).toString() == u"thinking");
    CHECK(first.value(u"thinking"_s).toString() == u"Plan: read help.");
    CHECK(first.value(u"signature"_s).toString() == u"sig123");
    CHECK(reply.thinkingBlocks.at(1).toObject().value(u"type"_s).toString() == u"redacted_thinking");
    REQUIRE(reply.calls.size() == 1);
    CHECK(reply.calls[0].arguments.value(u"topic"_s).toString() == u"connecting");

    AiRequest request = sampleRequest();
    request.turns[1].thinkingBlocks = reply.thinkingBlocks;
    request.think = true;
    const QJsonObject thinking = gs::app::aiRequestJson(AiKind::Anthropic, request);
    CHECK(thinking.value(u"thinking"_s).toObject().value(u"type"_s).toString() == u"enabled");
    CHECK(thinking.value(u"thinking"_s).toObject().value(u"budget_tokens"_s).toInt() == AiRequest::kThinkingBudget);
    CHECK(thinking.value(u"max_tokens"_s).toInt() == AiRequest::kAnthropicMaxTokens + AiRequest::kThinkingBudget);
    const QJsonArray assistant = thinking.value(u"messages"_s).toArray().at(1).toObject().value(u"content"_s).toArray();
    REQUIRE(assistant.size() >= 3);
    CHECK(assistant.at(0).toObject().value(u"signature"_s).toString() == u"sig123");
    CHECK(assistant.at(1).toObject().value(u"type"_s).toString() == u"redacted_thinking");

    request.turns[1].thinkingBlocks = {};
    const QJsonObject withoutSignature = gs::app::aiRequestJson(AiKind::Anthropic, request);
    CHECK_FALSE(withoutSignature.contains(u"thinking"_s));
    CHECK(withoutSignature.value(u"max_tokens"_s).toInt() == AiRequest::kAnthropicMaxTokens);

    request.turns[1].thinkingBlocks = reply.thinkingBlocks;
    request.think = false;
    const QJsonObject plain = gs::app::aiRequestJson(AiKind::Anthropic, request);
    CHECK_FALSE(plain.contains(u"thinking"_s));
    const QJsonArray said = plain.value(u"messages"_s).toArray().at(1).toObject().value(u"content"_s).toArray();
    CHECK(said.at(0).toObject().value(u"type"_s).toString() == u"text");

    const AiReply whole = gs::app::aiReplyOf(AiKind::Anthropic, R"({"content": [
        {"type": "thinking", "thinking": "Hmm.", "signature": "s"}, {"type": "text", "text": "Yes."}],
        "stop_reason": "end_turn"})");
    CHECK(whole.thinking == u"Hmm.");
    CHECK(whole.text == u"Yes.");
    CHECK(whole.thinkingBlocks.size() == 1);
}

TEST_CASE("ADR-0099 01:5x: OpenAI Responses asks for a reasoning summary only with deep thinking, and streams it")
{
    AiRequest request = sampleRequest();
    CHECK_FALSE(gs::app::aiRequestJson(AiKind::OpenAiResponses, request).contains(u"reasoning"_s));
    request.think = true;
    const QJsonObject reasoning = gs::app::aiRequestJson(AiKind::OpenAiResponses, request).value(u"reasoning"_s).toObject();
    CHECK(reasoning.value(u"summary"_s).toString() == u"auto");
    CHECK_FALSE(gs::app::aiRequestJson(AiKind::OpenAi, request).contains(u"reasoning"_s));

    QByteArray events;
    events += sse(R"({"type": "response.reasoning_summary_part.added", "summary_index": 0})");
    events += sse(R"({"type": "response.reasoning_summary_text.delta", "delta": "Step one."})");
    events += sse(R"({"type": "response.reasoning_summary_part.added", "summary_index": 1})");
    events += sse(R"({"type": "response.reasoning_summary_text.delta", "delta": "Step two."})");
    events += sse(R"({"type": "response.output_item.added", "output_index": 1,
                  "item": {"type": "function_call", "call_id": "f1", "name": "read_guide", "arguments": ""}})");
    events += sse(R"({"type": "response.function_call_arguments.done", "output_index": 1,
                  "arguments": "{\"name\": \"make_keymap\"}"})");
    events += sse(R"({"type": "response.completed", "response": {"status": "completed",
                  "usage": {"input_tokens": 5, "output_tokens": 7}}})");
    Pieces pieces;
    const AiReply reply = streamed(AiKind::OpenAiResponses, events, nullptr, &pieces);
    CHECK(reply.problem.isEmpty());
    CHECK(reply.thinking == u"Step one.\n\nStep two.");
    CHECK(pieces.calling == QStringList{u"read_guide"_s});
    REQUIRE(reply.calls.size() == 1);
    CHECK(reply.calls[0].arguments.value(u"name"_s).toString() == u"make_keymap");

    const AiReply whole = gs::app::aiReplyOf(AiKind::OpenAiResponses, R"({"status": "completed", "output": [
        {"type": "reasoning", "summary": [{"type": "summary_text", "text": "A."}, {"type": "summary_text", "text": "B."}]},
        {"type": "message", "content": [{"type": "output_text", "text": "Done."}]}]})");
    CHECK(whole.thinking == u"A.\n\nB.");
    CHECK(whole.text == u"Done.");
}

TEST_CASE("ADR-0099 2026-11-10: the model's name tells which thinking options it takes")
{
    using gs::app::aiModelTraits;
    using gs::app::AiModelTraits;
    const AiModelTraits claude = aiModelTraits(AiKind::Anthropic, u"claude-sonnet-4-5-20250929"_s);
    CHECK(claude.think);
    CHECK(claude.efforts == QStringList{u"low"_s, u"medium"_s, u"high"_s});
    CHECK(claude.thinkingEffort);
    CHECK(claude.longContext);
    CHECK(aiModelTraits(AiKind::Anthropic, u"claude-opus-4-1"_s).think);
    CHECK_FALSE(aiModelTraits(AiKind::Anthropic, u"claude-opus-4-1"_s).longContext);
    CHECK(aiModelTraits(AiKind::Anthropic, u"claude-3-7-sonnet-latest"_s).think);
    CHECK(aiModelTraits(AiKind::Anthropic, u"claude-haiku-4.5"_s).think);
    CHECK(aiModelTraits(AiKind::Anthropic, u"anthropic/claude-sonnet-4"_s).longContext);
    CHECK(aiModelTraits(AiKind::Anthropic, u"claude-3-5-haiku-latest"_s) == AiModelTraits{});
    CHECK(aiModelTraits(AiKind::Anthropic, {}) == AiModelTraits{});

    const AiModelTraits gpt5 = aiModelTraits(AiKind::OpenAi, u"GPT-5-mini"_s);
    CHECK(gpt5.efforts == QStringList{u"minimal"_s, u"low"_s, u"medium"_s, u"high"_s});
    CHECK_FALSE(gpt5.think);
    CHECK_FALSE(gpt5.thinkingEffort);
    CHECK(aiModelTraits(AiKind::OpenAiResponses, u"gpt-5"_s).think);
    CHECK(aiModelTraits(AiKind::OpenAi, u"o4-mini"_s).efforts == QStringList{u"low"_s, u"medium"_s, u"high"_s});
    CHECK(aiModelTraits(AiKind::OpenAi, u"openai/o3"_s).efforts.size() == 3);
    CHECK(aiModelTraits(AiKind::OpenAi, u"gpt-5-chat-latest"_s).efforts.isEmpty());
    CHECK(aiModelTraits(AiKind::OpenAi, u"gpt-4.1"_s) == AiModelTraits{});
    CHECK(aiModelTraits(AiKind::OpenAi, u"omni-moderation-latest"_s) == AiModelTraits{});

    CHECK(aiModelTraits(AiKind::OpenAi, u"deepseek-reasoner"_s).alwaysThinks);
    CHECK(aiModelTraits(AiKind::OpenAi, u"deepseek-r1:14b"_s).alwaysThinks);
    CHECK(aiModelTraits(AiKind::OpenAi, u"qwq-plus"_s).alwaysThinks);
    CHECK(aiModelTraits(AiKind::OpenAi, u"kimi-k2-thinking"_s).alwaysThinks);
    CHECK_FALSE(aiModelTraits(AiKind::OpenAi, u"deepseek-chat"_s).alwaysThinks);
    CHECK(aiModelTraits(AiKind::OpenAi, u"qwen-plus"_s).think);
    CHECK(aiModelTraits(AiKind::OpenAi, u"qwen3-235b-a22b"_s).think);
    CHECK_FALSE(aiModelTraits(AiKind::OpenAi, u"qwen3-235b-a22b-instruct-2507"_s).think);
    CHECK_FALSE(aiModelTraits(AiKind::OpenAi, u"qwen3-coder-plus"_s).think);
    CHECK_FALSE(aiModelTraits(AiKind::Anthropic, u"qwen-plus"_s).think);
}

TEST_CASE("ADR-0099 2026-11-10: the effort, Qwen's thinking switch and the long context go out as each service wants")
{
    AiRequest request = sampleRequest();
    request.effort = u"high"_s;
    CHECK(gs::app::aiRequestJson(AiKind::OpenAi, request).value(u"reasoning_effort"_s).toString() == u"high");
    const QJsonObject responses =
        gs::app::aiRequestJson(AiKind::OpenAiResponses, request).value(u"reasoning"_s).toObject();
    CHECK(responses.value(u"effort"_s).toString() == u"high");
    CHECK_FALSE(responses.contains(u"summary"_s));

    const AiRequest asked{.model = u"claude-sonnet-4-5"_s,
                          .turns = {AiTurn{.role = AiTurn::Role::User, .text = u"Hi"_s}},
                          .think = true,
                          .effort = u"high"_s};
    const QJsonObject claude = gs::app::aiRequestJson(AiKind::Anthropic, asked);
    const int high = gs::app::aiThinkingBudget(u"high"_s);
    CHECK(claude.value(u"thinking"_s).toObject().value(u"budget_tokens"_s).toInt() == high);
    CHECK(claude.value(u"max_tokens"_s).toInt() == AiRequest::kAnthropicMaxTokens + high);
    CHECK_FALSE(claude.contains(u"reasoning_effort"_s));
    CHECK(gs::app::aiThinkingBudget(u"low"_s) < gs::app::aiThinkingBudget(u"medium"_s));
    CHECK(gs::app::aiThinkingBudget(u"medium"_s) < high);
    CHECK(AiRequest::kAnthropicMaxTokens + high <= 32000);
    CHECK(gs::app::aiThinkingBudget({}) == AiRequest::kThinkingBudget);

    request.effort.clear();
    request.think = true;
    CHECK_FALSE(gs::app::aiRequestJson(AiKind::OpenAi, request).contains(u"reasoning_effort"_s));
    CHECK_FALSE(gs::app::aiRequestJson(AiKind::OpenAi, request).contains(u"enable_thinking"_s));
    request.model = u"qwen-plus"_s;
    CHECK(gs::app::aiRequestJson(AiKind::OpenAi, request).value(u"enable_thinking"_s) == QJsonValue(true));
    request.think = false;
    CHECK(gs::app::aiRequestJson(AiKind::OpenAi, request).value(u"enable_thinking"_s) == QJsonValue(false));

    CHECK(headerValue(gs::app::aiHeaders(AiKind::Anthropic, u"k"_s, true, true), "anthropic-beta") ==
          "context-1m-2025-08-07");
    CHECK(headerValue(gs::app::aiHeaders(AiKind::Anthropic, u"k"_s, true), "anthropic-beta").isEmpty());
    CHECK(headerValue(gs::app::aiHeaders(AiKind::OpenAi, u"k"_s, true, true), "anthropic-beta").isEmpty());
}

TEST_CASE("ADR-0099 2026-11-10: options the model does not take are dropped, and a choice reads back what it wrote")
{
    using gs::app::AiChoice;
    CHECK(gs::app::aiFitted(AiKind::OpenAi, AiChoice{.model = u"gpt-4.1"_s,
                                                    .think = true,
                                                    .effort = u"high"_s,
                                                    .longContext = true}) == AiChoice{.model = u"gpt-4.1"_s});
    CHECK(gs::app::aiFitted(AiKind::OpenAi, AiChoice{.model = u"gpt-5"_s, .think = true, .effort = u"minimal"_s}) ==
          AiChoice{.model = u"gpt-5"_s, .effort = u"minimal"_s});
    CHECK(gs::app::aiFitted(AiKind::Anthropic, AiChoice{.model = u"claude-opus-4-1"_s,
                                                       .think = true,
                                                       .effort = u"minimal"_s,
                                                       .longContext = true}) ==
          AiChoice{.model = u"claude-opus-4-1"_s, .think = true});
    const AiChoice full{.model = u"claude-sonnet-4-5"_s, .think = true, .effort = u"medium"_s, .longContext = true};
    CHECK(gs::app::aiFitted(AiKind::Anthropic, full) == full);
    CHECK(gs::app::aiFitted(AiKind::Anthropic, AiChoice{.model = u"claude-sonnet-4-5"_s, .effort = u"high"_s}) ==
          AiChoice{.model = u"claude-sonnet-4-5"_s});

    CHECK(gs::app::aiChoiceOf(gs::app::aiChoiceMap(full)) == full);
    AiChoice placed = full;
    placed.service = u"anthropic"_s;
    CHECK(gs::app::aiChoiceMap(placed).value(u"service"_s).toString() == u"anthropic");
    CHECK(gs::app::aiChoiceOf(gs::app::aiChoiceMap(placed)) == placed);
    CHECK_FALSE(gs::app::aiChoiceMap(full).contains(u"service"_s));
    CHECK(gs::app::aiChoiceOf(QVariantMap{{u"model"_s, u"m"_s}, {u"service"_s, u"elsewhere"_s}}).service.isEmpty());
    CHECK(gs::app::aiChoiceMap(AiChoice{}).isEmpty());
    CHECK(gs::app::aiChoiceOf(QVariantMap{{u"model"_s, u"   "_s}, {u"think"_s, true}}) == AiChoice{});
    CHECK(gs::app::aiChoiceOf(QVariantMap{{u"model"_s, QString(300, u'x')}}).model.size() == 256);
}
