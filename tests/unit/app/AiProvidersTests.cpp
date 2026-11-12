#include "AiAssistant.h"
#include "AiConversation.h"
#include "AiProviders.h"
#include "AiScripts.h"
#include "AiService.h"
#include "AppSettings.h"
#include "ExtensionHost.h"
#include "LogicServices.h"
#include "McpTestSupport.h"
#include "NativeExtensions.h"
#include "NativeJobs.h"
#include "Settings.h"

#include "unit/ext/TestFiles.h"

#include <gs/base/Clock.h>
#include <gs/ext/Manifest.h>
#include <gs/exthost/Hosted.h>
#include <gs/platform/Crypto.h>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using gs::app::AiAssistant;
using gs::app::AiConnection;
using gs::app::AiKind;
using gs::app::AiProvider;
using gs::app::AiProviderInfo;
using gs::app::AiReply;
using gs::app::AiRequest;
using gs::app::AiTurn;
using namespace Qt::StringLiterals;

namespace {

const fs::path kTestDll(GS_TEST_NATIVE_DLL);
const fs::path kProgram(GS_EXTHOST_PROGRAM);
const QString kProvider = u"gstest.native/tiny"_s;

QString uniquePrefix()
{
    std::array<std::uint8_t, 6> salt{};
    gs::platform::randomBytes(salt);
    return u"GameScrcpy/test-%1/ai/"_s.arg(QString::fromStdString(gs::platform::toHex(salt)));
}

AiRequest question(const QString& text)
{
    return AiRequest{.system = u"Be brief."_s,
                     .turns = {AiTurn{.role = AiTurn::Role::User, .text = text}},
                     .stream = true,
                     .maxTokens = 100};
}

AiConnection modelled(const QString& model, bool think = false, const QString& effort = {})
{
    return AiConnection{.kind = AiKind::OpenAi, .url = {}, .model = model, .key = {}, .think = think, .effort = effort};
}

nlohmann::json jsonOf(const QJsonObject& object)
{
    return nlohmann::json::parse(QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString());
}

std::size_t requestFiles(const fs::path& folder)
{
    std::size_t count = 0;
    std::error_code error;
    for (const fs::directory_entry& item : fs::directory_iterator(folder, error)) {
        if (item.path().extension() == L".json") {
            ++count;
        }
    }
    return count;
}

struct Heard {
    QStringList texts;
    QStringList thoughts;
    QStringList calls;
    std::optional<AiReply> reply;

    AiProvider::Handlers handlers()
    {
        return {.text = [this](const QString& delta) { texts.append(delta); },
                .done = [this](AiReply answer) { reply = std::move(answer); },
                .thinking = [this](const QString& delta) { thoughts.append(delta); },
                .calling = [this](const QString& tool) { calls.append(tool); }};
    }
};

fs::path programCopy(const gs::ext::test::Scratch& scratch)
{
    const fs::path copy = scratch / "program" / kProgram.filename();
    fs::create_directories(copy.parent_path());
    fs::copy_file(kProgram, copy, fs::copy_options::overwrite_existing);
    return copy;
}

struct Native {
    gs::test::McpApplication application;
    gs::ext::test::Scratch scratch;
    gs::app::NativeExtensions registry{
        gs::app::NativeOptions{.program = programCopy(scratch), .storeRoot = scratch / ".store", .clock = nullptr}};
    gs::app::NativeJobs jobs{registry};
    gs::app::AiProviders providers{scratch / "requests"};

    explicit Native(bool restricted = false)
    {
        const fs::path folder = scratch / "gstest.native/1.0.0";
        fs::create_directories(folder);
        fs::copy_file(kTestDll, folder / "native.dll");
        gs::app::NativeWanted wanted;
        wanted.id = "gstest.native";
        wanted.version = "1.0.0";
        wanted.name = u"Test native"_s;
        wanted.directory = folder;
        wanted.official = true;
        wanted.native.dll = "native.dll";
        wanted.native.nodes = {
            {.name = "aiModels", .params = {}, .returns = "json", .description = {}, .permissions = {}},
            {.name = "lastAiRequest", .params = {}, .returns = "json", .description = {}, .permissions = {}}};
        wanted.native.jobs = {{.name = "aiChat",
                               .title = "Chat",
                               .params = {{"request", "json"}},
                               .returns = "json",
                               .description = {},
                               .permissions = {},
                               .most = std::nullopt}};
        if (restricted) {
            wanted.native.sandbox = gs::ext::NativeSandbox::Restricted;
        }
        registry.apply({wanted});
        REQUIRE(registry.problems().empty());
        REQUIRE(gs::test::waitUntil([this] { return registry.call("gstest.native", "lastAiRequest", {}).problem.empty(); }));
        providers.use(registry, jobs);
        providers.setProviders({AiProviderInfo{.extension = u"gstest.native"_s,
                                               .name = u"tiny"_s,
                                               .title = u"Tiny AI"_s,
                                               .extensionName = u"Test native"_s,
                                               .models = u"aiModels"_s,
                                               .chat = u"aiChat"_s}});
    }

    [[nodiscard]] nlohmann::json lastRequest() const
    {
        return nlohmann::json::parse(registry.call("gstest.native", "lastAiRequest", {}).value.text, nullptr, false);
    }
};

}

TEST_CASE("ADR-0112: an extension's AI provider lists its models through its node and answers through its chat job, "
          "streaming the deltas the job hands over")
{
    Native native;
    const gs::app::AiProviderModels listed = native.providers.models(kProvider);
    CHECK(listed.problem.isEmpty());
    REQUIRE(listed.models.size() == 2);
    CHECK(listed.models[0] == gs::app::AiProviderModel{.id = u"tiny"_s,
                                                       .name = u"Tiny test model"_s,
                                                       .traits = gs::app::AiModelTraits{.think = true,
                                                                                        .alwaysThinks = false,
                                                                                        .efforts = {u"low"_s, u"high"_s},
                                                                                        .thinkingEffort = true,
                                                                                        .longContext = false}});
    CHECK(listed.models[1] == gs::app::AiProviderModel{.id = u"plain"_s, .name = u"Plain test model"_s, .traits = {}});
    CHECK_FALSE(native.providers.models(u"gstest.native/other"_s).problem.isEmpty());
    CHECK(native.providers.provider(u"gstest.native/other"_s, modelled(u"tiny"_s)) == nullptr);

    const std::unique_ptr<AiProvider> provider =
        native.providers.provider(kProvider, modelled(u"tiny"_s, true, u"high"_s));
    REQUIRE(provider != nullptr);
    Heard heard;
    provider->send(question(u"Hello"_s), heard.handlers());
    REQUIRE(gs::test::waitUntil([&heard] { return heard.reply.has_value(); }));
    CHECK(heard.thoughts == QStringList{u"Thinking."_s});
    CHECK(heard.texts == QStringList{u"Hello "_s, u"there."_s});
    CHECK(heard.calls.isEmpty());
    CHECK(heard.reply->problem.isEmpty());
    CHECK(heard.reply->text == u"Hello there.");
    CHECK(heard.reply->thinking == u"Thinking.");
    CHECK(heard.reply->stop == u"end");
    CHECK(heard.reply->usage.input == 12);
    CHECK(heard.reply->usage.output == 3);

    nlohmann::json seen = native.lastRequest();
    CHECK(seen["file"].is_null());
    nlohmann::json request = seen["args"]["request"];
    CHECK(request["model"] == "tiny");
    CHECK(request["system"] == "Be brief.");
    CHECK(request["messages"] == nlohmann::json::parse(R"([{"role":"user","text":"Hello"}])"));
    CHECK(request["tools"] == nlohmann::json::array());
    CHECK(request["stream"] == true);
    CHECK(request["maxTokens"] == 100);
    CHECK(request["think"] == true);
    CHECK(request["effort"] == "high");
    CHECK(request["longContext"] == false);

    Heard failing;
    provider->send(question(u"fail"_s), failing.handlers());
    REQUIRE(gs::test::waitUntil([&failing] { return failing.reply.has_value(); }));
    CHECK(failing.reply->problem.contains(u"Tiny AI"_s));
    CHECK(failing.reply->problem.contains(u"the test provider fails on purpose"_s));

    Heard calling;
    provider->send(question(u"call"_s), calling.handlers());
    REQUIRE(gs::test::waitUntil([&calling] { return calling.reply.has_value(); }));
    CHECK(calling.calls == QStringList{u"read_help"_s});
    CHECK(calling.reply->problem.isEmpty());
    CHECK(calling.reply->text.isEmpty());
    CHECK(calling.reply->stop == u"tool_use");
    REQUIRE(calling.reply->calls.size() == 1);
    CHECK(calling.reply->calls[0].id == u"call_7");
    CHECK(calling.reply->calls[0].name == u"read_help");
    CHECK(calling.reply->calls[0].arguments == QJsonObject{{u"topic"_s, u"connect"_s}});
    CHECK(calling.reply->calls[0].problem.isEmpty());

    Heard junk;
    provider->send(question(u"junk"_s), junk.handlers());
    REQUIRE(gs::test::waitUntil([&junk] { return junk.reply.has_value(); }));
    CHECK(junk.reply->problem == gs::app::aiUnreadable());

    Heard waiting;
    provider->send(question(u"wait"_s), waiting.handlers());
    REQUIRE(gs::test::waitUntil([&waiting] { return !waiting.texts.isEmpty(); }));
    CHECK(waiting.texts == QStringList{u"Hold on"_s});
    provider->cancel();
    REQUIRE(gs::test::waitUntil([&native] { return native.jobs.running() == 0; }));
    CHECK_FALSE(waiting.reply.has_value());
}

TEST_CASE("ADR-0112: a request too large for a job's arguments reaches the provider in a file, which is gone once the "
          "job ends")
{
    Native native;
    const std::unique_ptr<AiProvider> provider = native.providers.provider(kProvider, modelled(u"plain"_s));
    REQUIRE(provider != nullptr);
    Heard heard;
    const QString words(70'000, u'x');
    provider->send(question(words), heard.handlers());
    REQUIRE(gs::test::waitUntil([&heard] { return heard.reply.has_value(); }));
    CHECK(heard.reply->problem.isEmpty());
    CHECK(heard.reply->text == u"Hello there.");
    nlohmann::json seen = native.lastRequest();
    REQUIRE(seen["args"]["request"].is_object());
    CHECK(seen["args"]["request"].size() == 1);
    CHECK(seen["args"]["request"]["file"].is_string());
    CHECK(seen["file"]["model"] == "plain");
    CHECK(seen["file"]["messages"][0]["text"].get<std::string>().size() == 70'000);
    CHECK(requestFiles(native.scratch / "requests") == 0);
}

TEST_CASE("ADR-0112: a restricted provider reads a request too large for its job's arguments from its file as well")
{
    {
        Native native(true);
        const std::unique_ptr<AiProvider> provider = native.providers.provider(kProvider, modelled(u"plain"_s));
        REQUIRE(provider != nullptr);
        Heard heard;
        provider->send(question(QString(70'000, u'x')), heard.handlers());
        REQUIRE(gs::test::waitUntil([&heard] { return heard.reply.has_value(); }));
        CHECK(heard.reply->problem.isEmpty());
        nlohmann::json seen = native.lastRequest();
        CHECK(seen["args"]["request"]["file"].is_string());
        CHECK(seen["file"]["model"] == "plain");
        CHECK(requestFiles(native.scratch / "requests") == 0);
    }
    CHECK(gs::exthost::forgetContainer("gstest.native"));
}

TEST_CASE("ADR-0112: the assistant's conversation turns what an extension's provider streams into its answer")
{
    Native native;
    gs::app::AiConversation conversation(
        [&native](const gs::app::AiChoice&) { return native.providers.provider(kProvider, modelled(u"tiny"_s)); },
        nullptr);
    REQUIRE(conversation.send(u"Hello"_s));
    REQUIRE(gs::test::waitUntil([&conversation] { return !conversation.busy(); }));
    QStringList kinds;
    QStringList texts;
    for (int row = 0; row < conversation.count(); ++row) {
        kinds.append(conversation.data(conversation.index(row), gs::app::AiConversation::KindRole).toString());
        texts.append(conversation.data(conversation.index(row), gs::app::AiConversation::TextRole).toString());
    }
    CHECK(kinds == QStringList{u"user"_s, u"thinking"_s, u"assistant"_s});
    CHECK(texts == QStringList{u"Hello"_s, u"Thinking."_s, u"Hello there."_s});
    REQUIRE(conversation.history().size() == 2);
    CHECK(conversation.history()[1].role == AiTurn::Role::Assistant);
    CHECK(conversation.history()[1].text == u"Hello there.");
    nlohmann::json seen = native.lastRequest();
    CHECK(seen["args"]["request"]["stream"] == true);
    CHECK(seen["args"]["request"]["system"].get<std::string>().find("GameScrcpy") != std::string::npos);
}

TEST_CASE("ADR-0112: an extension's provider can be the model service: its models and their traits fill the card, "
          "the test, the assistant and ai.ask go through it, and once it is gone they are refused with GS-API-150")
{
    Native native;
    gs::app::AppSettings settings(native.scratch / "data");
    AiAssistant assistant(settings, uniquePrefix());
    assistant.setProviders(&native.providers);
    const QVariantList offered = assistant.providerList();
    REQUIRE(offered.size() == 1);
    CHECK(offered[0].toMap().value(u"id"_s).toString() == kProvider);
    CHECK(offered[0].toMap().value(u"title"_s).toString() == u"Tiny AI");
    CHECK(offered[0].toMap().value(u"extensionName"_s).toString() == u"Test native");
    CHECK(assistant.service() == u"openai");
    CHECK(assistant.providerId().isEmpty());
    CHECK_FALSE(assistant.providerMissing());
    CHECK(assistant.unavailable().isEmpty());
    CHECK_FALSE(assistant.chooseProvider(u"gstest.native/other"_s));

    REQUIRE(assistant.chooseProvider(kProvider));
    CHECK(settings.values().value(u"assistant.service"_s).toString() == u"extension");
    CHECK(settings.values().value(u"assistant.extension.provider"_s).toString() == kProvider);
    CHECK(settings.values().value(u"assistant.extension.model"_s).toString() == u"tiny");
    CHECK(assistant.service() == u"extension");
    CHECK(assistant.providerId() == kProvider);
    CHECK(assistant.providerTitle() == u"Tiny AI");
    CHECK(assistant.ready());
    CHECK(assistant.host().isEmpty());
    CHECK(assistant.defaultAddress().isEmpty());
    CHECK(assistant.models() == QStringList{u"tiny"_s, u"plain"_s});
    CHECK(assistant.modelsState() == u"done");
    CHECK(assistant.modelNames().value(u"tiny"_s).toString() == u"Tiny test model");
    CHECK(assistant.traits(u"tiny"_s).value(u"think"_s).toBool());
    CHECK(assistant.traits(u"tiny"_s).value(u"efforts"_s).toStringList() == QStringList{u"low"_s, u"high"_s});
    CHECK_FALSE(assistant.traits(u"plain"_s).value(u"think"_s).toBool());
    CHECK(assistant.defaults() ==
          gs::app::AiChoice{.model = u"tiny"_s, .service = u"extension"_s, .provider = kProvider});
    CHECK(assistant.defaultChoice().value(u"provider"_s).toString() == kProvider);

    const auto fitted = assistant.chatConnection(gs::app::AiChoice{
        .model = u"tiny"_s, .think = true, .effort = u"high"_s, .service = u"extension"_s, .provider = kProvider});
    REQUIRE(fitted.has_value());
    CHECK(fitted->model == u"tiny");
    CHECK(fitted->think);
    CHECK(fitted->effort == u"high");
    const auto plain = assistant.chatConnection(gs::app::AiChoice{.model = u"plain"_s,
                                                                  .think = true,
                                                                  .effort = u"high"_s,
                                                                  .longContext = true,
                                                                  .service = u"extension"_s,
                                                                  .provider = kProvider});
    REQUIRE(plain.has_value());
    CHECK(plain->model == u"plain");
    CHECK_FALSE(plain->think);
    CHECK(plain->effort.isEmpty());
    CHECK_FALSE(plain->longContext);
    const auto other = assistant.chatConnection(
        gs::app::AiChoice{.model = u"big"_s, .service = u"extension"_s, .provider = u"bob.ai/big"_s});
    REQUIRE(other.has_value());
    CHECK(other->model == u"tiny");

    assistant.testConnection();
    REQUIRE(gs::test::waitUntil([&assistant] { return assistant.testState() != u"running"; }));
    CHECK(assistant.testState() == u"passed");

    gs::app::ExtensionHost extensions(settings, {native.scratch / "user", {}});
    extensions.start(false);
    const gs::ManualClock clock;
    gs::app::AiScripts scripts(gs::app::aiScriptHooks(settings, assistant, extensions, {}), clock);
    std::vector<std::pair<quint64, QString>> answers;
    std::vector<std::pair<quint64, QString>> refusals;
    scripts.setReplies(gs::app::AiScriptReplies{
        .answer = [&answers](std::uint64_t, quint64 request, const QString& json) { answers.emplace_back(request, json); },
        .refuse = [&refusals](std::uint64_t, quint64 request,
                              const QString& message) { refusals.emplace_back(request, message); }});
    REQUIRE(settings.setValue(u"assistant.scripts"_s, true));
    const gs::app::LogicCaller mine{.logic = 1, .script = u"helper"_s};
    scripts.request(mine, 1, uR"({"question":"Hello"})"_s);
    REQUIRE(gs::test::waitUntil([&answers] { return !answers.empty(); }));
    CHECK(answers[0] == std::pair<quint64, QString>{1, u"\"Hello there.\""_s});
    CHECK(refusals.empty());
    nlohmann::json seen = native.lastRequest();
    CHECK(seen["args"]["request"]["stream"] == false);
    CHECK(seen["args"]["request"]["maxTokens"] == gs::app::AiScripts::kMostTokens);
    CHECK(seen["args"]["request"]["think"] == false);
    CHECK(seen["args"]["request"]["messages"][0]["text"] == "Hello");

    native.providers.setProviders({});
    CHECK(assistant.providerList().isEmpty());
    CHECK(assistant.providerMissing());
    CHECK_FALSE(assistant.ready());
    CHECK(assistant.missing().contains(kProvider));
    CHECK(assistant.providerTitle() == kProvider);
    CHECK(assistant.provider() == nullptr);
    CHECK(assistant.provider(true, {}) == nullptr);
    CHECK(assistant.modelsState() == u"failed");
    CHECK(settings.values().value(u"assistant.service"_s).toString() == u"extension");
    CHECK(settings.values().value(u"assistant.extension.provider"_s).toString() == kProvider);
    CHECK(assistant.unavailable().startsWith(u"GS-API-150 the AI provider gstest.native/tiny "_s));
    scripts.request(mine, 2, uR"({"question":"Hello"})"_s);
    REQUIRE(refusals.size() == 1);
    CHECK(refusals[0].first == 2);
    CHECK(refusals[0].second.startsWith(u"GS-API-150 "_s));
    assistant.testConnection();
    CHECK(assistant.testState() == u"failed");
    CHECK(assistant.testMessage() == assistant.missing());
}

TEST_CASE("ADR-0112: a provider gets the turns, tool calls, results and pictures in GameScrcpy's own format, and its "
          "deltas and reply are read back")
{
    const QJsonObject picture{{u"type"_s, u"image"_s}, {u"data"_s, u"AAAA"_s}, {u"mimeType"_s, u"image/jpeg"_s}};
    const QJsonArray frame{QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, u"1080 x 2400"_s}}, picture};
    AiRequest request{
        .model = u"tiny"_s,
        .system = u"Be brief."_s,
        .turns = {AiTurn{.role = AiTurn::Role::User, .text = u"Look"_s, .pictures = QJsonArray{picture}},
                  AiTurn{.role = AiTurn::Role::Assistant,
                         .text = u"Reading."_s,
                         .calls = {gs::app::AiToolCall{.id = u"call_1"_s,
                                                       .name = u"get_frame"_s,
                                                       .arguments = QJsonObject{{u"maxSide"_s, 512}}}},
                         .thinkingBlocks = QJsonArray{QJsonObject{{u"type"_s, u"thinking"_s}}}},
                  AiTurn{.role = AiTurn::Role::Tools,
                         .results = {gs::app::AiToolResult{
                             .id = u"call_1"_s, .name = u"get_frame"_s, .content = frame, .failed = false}}}},
        .tools = QJsonArray{QJsonObject{{u"name"_s, u"get_frame"_s}, {u"description"_s, u"A picture"_s}}},
        .stream = false,
        .maxTokens = 0};
    CHECK(jsonOf(gs::app::aiProviderRequestJson(request)) == nlohmann::json::parse(R"x({
        "model": "tiny", "system": "Be brief.", "stream": false, "maxTokens": 8192, "think": false, "effort": "",
        "longContext": false,
        "tools": [{"name": "get_frame", "description": "A picture", "inputSchema": {"type": "object", "properties": {}}}],
        "messages": [
            {"role": "user", "text": "Look", "pictures": [{"type": "image", "mimeType": "image/jpeg", "data": "AAAA"}]},
            {"role": "assistant", "text": "Reading.", "thinkingBlocks": [{"type": "thinking"}],
             "toolCalls": [{"id": "call_1", "name": "get_frame", "arguments": {"maxSide": 512}}]},
            {"role": "tools", "toolResults": [{"id": "call_1", "name": "get_frame", "failed": false, "content": [
                {"type": "text", "text": "1080 x 2400"}, {"type": "image", "mimeType": "image/jpeg", "data": "AAAA"}]}]}
        ]})x"));

    request.pictures = false;
    nlohmann::json without = jsonOf(gs::app::aiProviderRequestJson(request));
    const std::string leftOut = "[A picture, left out because the model does not take pictures.]";
    CHECK(without["messages"][0] == nlohmann::json{{"role", "user"}, {"text", "Look\n" + leftOut}});
    CHECK(without["messages"][2]["toolResults"][0]["content"][1] == nlohmann::json{{"type", "text"}, {"text", leftOut}});

    const gs::app::AiDelta delta = gs::app::aiProviderDelta(R"({"text":"a","thinking":"b","toolCall":"read_help"})");
    CHECK(delta.text == u"a");
    CHECK(delta.thinking == u"b");
    CHECK(delta.calling == QStringList{u"read_help"_s});
    CHECK(gs::app::aiProviderDelta("[1]").text.isEmpty());

    const AiReply reply = gs::app::aiProviderReply(
        R"x({"toolCalls": [{"id": "a", "name": "read_help", "arguments": "{\"topic\": \"x\"}"},
                           {"name": "bad", "arguments": "[1"}, {"id": "c"}],
             "problem": "Too many requests", "status": 429, "code": "rate"})x",
        u"streamed"_s, u"thought"_s);
    CHECK(reply.text == u"streamed");
    CHECK(reply.thinking == u"thought");
    REQUIRE(reply.calls.size() == 2);
    CHECK(reply.calls[0].arguments == QJsonObject{{u"topic"_s, u"x"_s}});
    CHECK(reply.calls[1].name == u"bad");
    CHECK_FALSE(reply.calls[1].problem.isEmpty());
    CHECK(reply.problem == u"Too many requests");
    CHECK(reply.status == 429);
    CHECK(reply.code == u"rate");

    const gs::app::AiModelTraits low{.think = true,
                                     .alwaysThinks = false,
                                     .efforts = {u"low"_s},
                                     .thinkingEffort = true,
                                     .longContext = false};
    CHECK(gs::app::aiFitted(low, gs::app::AiChoice{.model = u"m"_s, .effort = u"low"_s, .longContext = true}) ==
          gs::app::AiChoice{.model = u"m"_s});
    CHECK(gs::app::aiFitted(low, gs::app::AiChoice{.model = u"m"_s, .think = true, .effort = u"low"_s}).effort ==
          u"low");
    CHECK(gs::app::aiTraitsOf(QJsonObject{{u"efforts"_s, QJsonArray{u"high"_s, u"turbo"_s, u"minimal"_s}}}).efforts ==
          QStringList{u"minimal"_s, u"high"_s});
}

TEST_CASE("ADR-0112: the model service setting takes an extension's provider, and older settings files stay valid")
{
    namespace settings = gs::app::settings;
    const settings::Entry* service = settings::findSetting("assistant.service");
    REQUIRE(service != nullptr);
    CHECK(service->choices == std::vector<std::string_view>{"openai", "anthropic", "openaiResponses", "extension"});
    const settings::Entry* provider = settings::findSetting("assistant.extension.provider");
    REQUIRE(provider != nullptr);
    CHECK(provider->kind == settings::Kind::Text);
    CHECK(provider->page == settings::Page::Assistant);
    CHECK(provider->maxLength == settings::AiExtensionService::kMaxProviderLength);
    CHECK(settings::findSetting("assistant.extension.model") != nullptr);
    CHECK(settings::findSetting("assistant.extension.effort") != nullptr);
    CHECK(settings::findSetting("assistant.extension.address") == nullptr);

    const settings::Loaded<settings::Settings> old = settings::load<settings::Settings>(
        R"({"format": "gamescrcpy.settings", "version": 1,)"
        R"( "assistant": {"service": "anthropic", "anthropic": {"model": "claude-sonnet-4-5"}}})");
    CHECK(old.usable);
    CHECK(old.diagnostics.empty());
    CHECK(old.document.assistant.service == settings::AiService::Anthropic);
    CHECK(old.document.assistant.extension == settings::AiExtensionService{});

    const settings::Loaded<settings::Settings> chosen = settings::load<settings::Settings>(
        R"({"format": "gamescrcpy.settings", "version": 1, "assistant": {"service": "extension",)"
        R"( "extension": {"provider": "alice.ai/cloud", "model": "fast", "effort": "low"}}})");
    CHECK(chosen.usable);
    CHECK(chosen.diagnostics.empty());
    CHECK(chosen.document.assistant.service == settings::AiService::Extension);
    CHECK(chosen.document.assistant.extension.provider == "alice.ai/cloud");
    CHECK(chosen.document.assistant.extension.model == "fast");
    CHECK(chosen.document.assistant.extension.effort == settings::AiEffort::Low);
    CHECK(settings::load<settings::Settings>(settings::save(chosen.document)).document == chosen.document);
    CHECK(gs::app::aiChoiceOf(gs::app::aiChoiceMap(gs::app::AiChoice{
              .model = u"fast"_s, .service = u"extension"_s, .provider = u"alice.ai/cloud"_s})) ==
          gs::app::AiChoice{.model = u"fast"_s, .service = u"extension"_s, .provider = u"alice.ai/cloud"_s});
    CHECK(gs::app::aiChoiceOf(QVariantMap{{u"model"_s, u"fast"_s},
                                          {u"service"_s, u"openai"_s},
                                          {u"provider"_s, u"alice.ai/cloud"_s}})
              .provider.isEmpty());
    CHECK(gs::ext::kAiProvidePermission == std::string_view(gs::app::AiProviders::kPermission));
}
