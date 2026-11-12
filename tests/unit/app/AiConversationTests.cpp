#include "AiConversation.h"
#include "McpTestSupport.h"

#include <doctest/doctest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <deque>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

using gs::app::AiConversation;
using gs::app::AiProvider;
using gs::app::AiReply;
using gs::app::AiRequest;
using gs::app::AiToolCall;
using gs::app::AiTurn;
using gs::app::McpOutcome;
using gs::app::McpToolHost;
using namespace Qt::StringLiterals;

namespace {

using Step = std::function<void(AiProvider::Handlers& handlers)>;

struct Script {
    std::vector<AiRequest> requests;
    std::deque<Step> steps;
    Step always;
    AiProvider::Handlers held;
    std::vector<gs::app::AiChoice> choices;
    int providers = 0;
    int cancels = 0;
};

class FakeProvider final : public AiProvider
{
public:
    explicit FakeProvider(std::shared_ptr<Script> script)
        : m_script(std::move(script))
    {
    }

    void send(const AiRequest& request, Handlers handlers) override
    {
        m_script->requests.push_back(request);
        if (!m_script->steps.empty()) {
            const Step step = std::move(m_script->steps.front());
            m_script->steps.pop_front();
            step(handlers);
            return;
        }
        if (m_script->always) {
            m_script->always(handlers);
            return;
        }
        m_script->held = std::move(handlers);
    }

    void cancel() override { ++m_script->cancels; }

private:
    std::shared_ptr<Script> m_script;
};

class FakeTools final : public gs::app::AiToolRunner
{
public:
    struct Call {
        QString tool;
        QJsonObject arguments;
    };

    [[nodiscard]] QJsonArray tools() const override
    {
        return QJsonArray{tool(u"read_profile"_s, {u"profile"_s, u"draft"_s}),
                          tool(u"get_frame"_s, {u"serial"_s, u"maxSide"_s}),
                          tool(u"list_profiles"_s, {}),
                          tool(u"read_logs"_s, {u"lines"_s, u"source"_s}),
                          tool(u"run_replay"_s, {u"session"_s, u"profile"_s, u"untilMs"_s}),
                          tool(u"read_draft"_s, {u"target"_s})};
    }

    void run(const QString& name, const QJsonObject& arguments, McpToolHost::Reply reply) override
    {
        calls.push_back(Call{.tool = name, .arguments = arguments});
        if (name == held) {
            waiting.push_back(std::move(reply));
            return;
        }
        if (name == u"read_profile") {
            reply(McpOutcome{.content = text(u"{\"profile.json\": \"shooter\"}"_s)});
            return;
        }
        if (name == u"get_frame") {
            QJsonArray content = text(u"1080 x 2400"_s);
            content.append(QJsonObject{{u"type"_s, u"image"_s}, {u"data"_s, u"AAAA"_s}, {u"mimeType"_s, u"image/png"_s}});
            reply(McpOutcome{.content = content});
            return;
        }
        reply(McpOutcome{.content = text(u"done: "_s + name)});
    }

    [[nodiscard]] std::vector<Call> without(const QString& name) const
    {
        std::vector<Call> kept;
        for (const Call& call : calls) {
            if (call.tool != name) {
                kept.push_back(call);
            }
        }
        return kept;
    }

    static QJsonArray text(const QString& words)
    {
        return QJsonArray{QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, words}}};
    }

    std::vector<Call> calls;
    QString held;
    std::vector<McpToolHost::Reply> waiting;

private:
    static QJsonObject tool(const QString& name, const QStringList& names)
    {
        QJsonObject properties;
        for (const QString& each : names) {
            properties.insert(each, QJsonObject{{u"type"_s, u"string"_s}});
        }
        return {{u"name"_s, name},
                {u"description"_s, u"Test tool"_s},
                {u"inputSchema"_s, QJsonObject{{u"type"_s, u"object"_s}, {u"properties"_s, properties}}}};
    }
};

AiReply answer(const QString& text)
{
    return AiReply{.text = text, .stop = u"stop"_s};
}

AiReply asking(std::vector<AiToolCall> calls, const QString& text = {})
{
    return AiReply{.text = text, .calls = std::move(calls), .stop = u"tool_calls"_s};
}

Step replyWith(AiReply reply)
{
    return [reply](AiProvider::Handlers& handlers) {
        if (!reply.text.isEmpty() && handlers.text) {
            handlers.text(reply.text);
        }
        handlers.done(reply);
    };
}

struct Setup {
    gs::test::McpApplication application;
    std::shared_ptr<Script> script = std::make_shared<Script>();
    FakeTools tools;
    AiConversation conversation{[this](const gs::app::AiChoice& choice) -> std::unique_ptr<AiProvider> {
                                    ++script->providers;
                                    script->choices.push_back(choice);
                                    return std::make_unique<FakeProvider>(script);
                                },
                                &tools};

    Setup()
    {
        conversation.setSerial(u"R58"_s);
        conversation.setPhone(u"Pixel"_s);
        conversation.setProfile(u"shooter"_s);
        conversation.setProfileName(u"Shooter"_s);
    }

    [[nodiscard]] QString kind(int row) const
    {
        return conversation.data(conversation.index(row), AiConversation::KindRole).toString();
    }

    [[nodiscard]] QVariant role(int row, int which) const
    {
        return conversation.data(conversation.index(row), which);
    }
};

}

TEST_CASE("a question with a tool call runs the tool, sends the result back and shows the answer")
{
    Setup setup;
    setup.script->steps.push_back(
        replyWith(asking({AiToolCall{.id = u"call_1"_s, .name = u"get_frame"_s}}, u"Let me look."_s)));
    setup.script->steps.push_back(replyWith(answer(u"The fire button is at the lower right."_s)));

    REQUIRE(setup.conversation.send(u"  Where is the fire button?  "_s));
    CHECK_FALSE(setup.conversation.busy());

    REQUIRE(setup.tools.calls.size() == 2);
    CHECK(setup.tools.calls[0].tool == u"read_profile");
    CHECK(setup.tools.calls[0].arguments.value(u"profile"_s).toString() == u"shooter");
    CHECK(setup.tools.calls[1].tool == u"get_frame");
    CHECK(setup.tools.calls[1].arguments.value(u"serial"_s).toString() == u"R58");
    CHECK(setup.tools.calls[1].arguments.value(u"maxSide"_s).toInt() == gs::app::kAiFrameSide);

    REQUIRE(setup.script->requests.size() == 2);
    const AiRequest& first = setup.script->requests[0];
    CHECK(first.system.contains(u"Shooter"));
    CHECK(first.system.contains(u"R58"));
    CHECK(first.system.contains(u"profile.json"));
    for (const QJsonValue& tool : first.tools) {
        CHECK(tool.toObject().value(u"name"_s).toString() != u"list_profiles");
    }
    REQUIRE(first.turns.size() == 1);
    CHECK(first.turns[0].text == u"Where is the fire button?");

    const AiRequest& second = setup.script->requests[1];
    REQUIRE(second.turns.size() == 3);
    CHECK(second.turns[1].role == AiTurn::Role::Assistant);
    REQUIRE(second.turns[1].calls.size() == 1);
    CHECK(second.turns[2].role == AiTurn::Role::Tools);
    REQUIRE(second.turns[2].results.size() == 1);
    CHECK(second.turns[2].results[0].id == u"call_1");
    CHECK_FALSE(second.turns[2].results[0].failed);
    CHECK(second.turns[2].results[0].content.size() == 2);

    REQUIRE(setup.conversation.count() == 4);
    CHECK(setup.kind(0) == u"user");
    CHECK(setup.role(0, AiConversation::TextRole).toString() == u"Where is the fire button?");
    CHECK(setup.kind(1) == u"assistant");
    CHECK(setup.role(1, AiConversation::TextRole).toString() == u"Let me look.");
    CHECK(setup.kind(2) == u"tool");
    CHECK(setup.role(2, AiConversation::TitleRole).toString() == gs::app::aiToolTitle(u"get_frame"_s, {}));
    CHECK_FALSE(setup.role(2, AiConversation::RunningRole).toBool());
    CHECK_FALSE(setup.role(2, AiConversation::FailedRole).toBool());
    CHECK(setup.role(2, AiConversation::OutputRole).toString().contains(u"1080 x 2400"));
    CHECK(setup.role(2, AiConversation::InputRole).toString().isEmpty());
    CHECK(setup.kind(3) == u"assistant");
    CHECK(setup.role(3, AiConversation::BlocksRole).toList().size() == 1);
    CHECK(setup.conversation.history().size() == 4);
}

TEST_CASE("several tool calls in one answer run one after another before the next request")
{
    Setup setup;
    setup.script->steps.push_back(replyWith(asking({AiToolCall{.id = u"a"_s, .name = u"read_logs"_s},
                                                    AiToolCall{.id = u"b"_s, .name = u"run_replay"_s,
                                                               .arguments = {{u"session"_s, u"C:/rec"_s}}}})));
    setup.script->steps.push_back(replyWith(answer(u"Both done."_s)));
    REQUIRE(setup.conversation.send(u"Check it"_s));

    const auto ran = setup.tools.without(u"read_profile"_s);
    REQUIRE(ran.size() == 2);
    CHECK(ran[0].tool == u"read_logs");
    CHECK(ran[0].arguments.value(u"source"_s).toString() == u"logic");
    CHECK(ran[1].tool == u"run_replay");
    CHECK(ran[1].arguments.value(u"profile"_s).toString() == u"shooter");
    REQUIRE(setup.script->requests.size() == 2);
    REQUIRE(setup.script->requests[1].turns.back().results.size() == 2);
    CHECK(setup.script->requests[1].turns.back().results[1].id == u"b");
}

TEST_CASE("tool calls stop after 24 rounds and the model hears why")
{
    Setup setup;
    int round = 0;
    setup.script->always = [&round](AiProvider::Handlers& handlers) {
        ++round;
        handlers.done(asking({AiToolCall{.id = u"call_%1"_s.arg(round), .name = u"read_logs"_s}}));
    };
    REQUIRE(setup.conversation.send(u"Keep going"_s));
    CHECK_FALSE(setup.conversation.busy());
    CHECK(setup.tools.without(u"read_profile"_s).size() == static_cast<std::size_t>(AiConversation::kMostRounds));
    CHECK(setup.script->requests.size() == static_cast<std::size_t>(AiConversation::kMostRounds) + 1);
    CHECK(setup.kind(setup.conversation.count() - 1) == u"notice");
    const AiTurn& last = setup.conversation.history().back();
    CHECK(last.role == AiTurn::Role::Tools);
    REQUIRE(last.results.size() == 1);
    CHECK(last.results[0].failed);
    CHECK(gs::app::aiContentText(last.results[0].content).contains(u"limit"));

    setup.script->always = {};
    setup.script->steps.push_back(replyWith(answer(u"Stopping here."_s)));
    REQUIRE(setup.conversation.send(u"Go on"_s));
    CHECK(setup.script->requests.back().turns.back().text == u"Go on");
}

TEST_CASE("stop while the model writes keeps what it wrote and ignores what comes later")
{
    Setup setup;
    REQUIRE(setup.conversation.send(u"Explain the logic"_s));
    CHECK(setup.conversation.busy());
    CHECK(setup.conversation.waiting());
    REQUIRE(setup.script->held.text);
    setup.script->held.text(u"The logic "_s);
    CHECK_FALSE(setup.conversation.waiting());
    const AiProvider::Handlers late = setup.script->held;

    setup.conversation.stop();
    CHECK_FALSE(setup.conversation.busy());
    CHECK(setup.script->cancels == 1);
    CHECK(setup.conversation.history().back().role == AiTurn::Role::Assistant);
    CHECK(setup.conversation.history().back().text == u"The logic ");
    const int rows = setup.conversation.count();
    CHECK(setup.kind(rows - 1) == u"notice");

    late.text(u"keeps going"_s);
    late.done(answer(u"The logic keeps going"_s));
    CHECK(setup.conversation.count() == rows);
    CHECK(setup.conversation.history().back().text == u"The logic ");
}

TEST_CASE("stop while a tool runs answers every call and the late result is ignored")
{
    Setup setup;
    setup.tools.held = u"run_replay"_s;
    setup.script->steps.push_back(replyWith(asking({AiToolCall{.id = u"a"_s, .name = u"run_replay"_s},
                                                    AiToolCall{.id = u"b"_s, .name = u"read_logs"_s}})));
    REQUIRE(setup.conversation.send(u"Try it"_s));
    CHECK(setup.conversation.busy());
    const int tool = setup.conversation.count() - 1;
    CHECK(setup.role(tool, AiConversation::RunningRole).toBool());

    setup.conversation.stop();
    CHECK_FALSE(setup.conversation.busy());
    CHECK_FALSE(setup.role(tool, AiConversation::RunningRole).toBool());
    CHECK(setup.role(tool, AiConversation::FailedRole).toBool());
    const AiTurn& results = setup.conversation.history().back();
    CHECK(results.role == AiTurn::Role::Tools);
    REQUIRE(results.results.size() == 2);
    CHECK(results.results[0].failed);
    CHECK(gs::app::aiContentText(results.results[0].content).contains(u"unknown"));
    CHECK(results.results[1].failed);
    CHECK(gs::app::aiContentText(results.results[1].content).contains(u"Not run"));

    REQUIRE(setup.tools.waiting.size() == 1);
    const int rows = setup.conversation.count();
    setup.tools.waiting.front()(McpOutcome{.content = FakeTools::text(u"late"_s)});
    CHECK(setup.conversation.count() == rows);
    CHECK(setup.tools.without(u"read_profile"_s).size() == 1);

    setup.script->steps.push_back(replyWith(answer(u"Fine."_s)));
    REQUIRE(setup.conversation.send(u"Never mind"_s));
    const std::vector<AiTurn>& turns = setup.script->requests.back().turns;
    REQUIRE(turns.size() >= 2);
    CHECK(turns[turns.size() - 2].role == AiTurn::Role::Tools);
    CHECK(turns.back().text == u"Never mind");
}

TEST_CASE("a call with broken arguments or a refused target is answered with the problem and the loop goes on")
{
    Setup setup;
    setup.script->steps.push_back(replyWith(
        asking({AiToolCall{.id = u"a"_s, .name = u"get_frame"_s, .problem = u"The arguments are broken"_s},
                AiToolCall{.id = u"b"_s, .name = u"get_frame"_s, .arguments = {{u"serial"_s, u"OTHER"_s}}},
                AiToolCall{.id = u"c"_s, .name = u"list_profiles"_s}})));
    setup.script->steps.push_back(replyWith(answer(u"Understood."_s)));
    REQUIRE(setup.conversation.send(u"Look"_s));
    CHECK(setup.tools.without(u"read_profile"_s).empty());
    const AiTurn& results = setup.script->requests[1].turns.back();
    REQUIRE(results.results.size() == 3);
    CHECK(gs::app::aiContentText(results.results[0].content).contains(u"broken"));
    CHECK(gs::app::aiContentText(results.results[1].content).contains(u"R58"));
    CHECK(gs::app::aiContentText(results.results[2].content).contains(u"list_profiles"));
    for (const auto& result : results.results) {
        CHECK(result.failed);
    }
    CHECK(setup.kind(setup.conversation.count() - 1) == u"assistant");
}

TEST_CASE("a refused picture leads to one more try without pictures")
{
    Setup setup;
    setup.script->steps.push_back(replyWith(asking({AiToolCall{.id = u"a"_s, .name = u"get_frame"_s}})));
    setup.script->steps.push_back(
        replyWith(AiReply{.status = 400, .problem = u"The service did not accept the request (HTTP 400)."_s}));
    setup.script->steps.push_back(replyWith(answer(u"I cannot see pictures, but here is an idea."_s)));
    REQUIRE(setup.conversation.send(u"What is on the screen?"_s));
    REQUIRE(setup.script->requests.size() == 3);
    CHECK(setup.script->requests[1].pictures);
    CHECK_FALSE(setup.script->requests[2].pictures);
    CHECK(setup.kind(setup.conversation.count() - 1) == u"assistant");
    CHECK(setup.kind(setup.conversation.count() - 2) == u"notice");
}

TEST_CASE("ADR-0099 2026-11-13: a question after one the service refused asks with pictures again")
{
    Setup setup;
    setup.script->steps.push_back(replyWith(asking({AiToolCall{.id = u"a"_s, .name = u"get_frame"_s}})));
    setup.script->steps.push_back(
        replyWith(AiReply{.status = 400, .problem = u"The service did not accept the request (HTTP 400)."_s}));
    setup.script->steps.push_back(replyWith(answer(u"I cannot see pictures, but here is an idea."_s)));
    REQUIRE(setup.conversation.send(u"What is on the screen?"_s));
    REQUIRE(setup.script->requests.size() == 3);
    CHECK(setup.script->requests[1].pictures);
    CHECK_FALSE(setup.script->requests[2].pictures);

    setup.script->steps.push_back(replyWith(answer(u"Still here."_s)));
    REQUIRE(setup.conversation.send(u"And now?"_s));
    REQUIRE(setup.script->requests.size() == 4);
    CHECK(setup.script->requests[3].pictures);
}

TEST_CASE("a service problem ends the question with a problem row")
{
    Setup setup;
    setup.script->steps.push_back(
        replyWith(AiReply{.status = 500, .problem = u"The service has a problem (HTTP 500)."_s}));
    REQUIRE(setup.conversation.send(u"Hello"_s));
    CHECK_FALSE(setup.conversation.busy());
    CHECK(setup.kind(setup.conversation.count() - 1) == u"problem");
    CHECK(setup.role(setup.conversation.count() - 1, AiConversation::TextRole).toString().contains(u"HTTP 500"));
}

TEST_CASE("sending is refused while a question runs, without words, or without a service")
{
    Setup setup;
    CHECK_FALSE(setup.conversation.send(u"   "_s));
    REQUIRE(setup.conversation.send(u"First"_s));
    CHECK_FALSE(setup.conversation.send(u"Second"_s));
    CHECK(setup.script->providers == 1);

    gs::test::McpApplication application;
    FakeTools tools;
    AiConversation lonely{[](const gs::app::AiChoice&) { return std::unique_ptr<AiProvider>(); }, &tools};
    CHECK_FALSE(lonely.send(u"Hello"_s));
    REQUIRE(lonely.count() == 1);
    CHECK(lonely.data(lonely.index(0), AiConversation::KindRole).toString() == u"problem");
}

TEST_CASE("clear empties the conversation and drops a running question")
{
    Setup setup;
    REQUIRE(setup.conversation.send(u"Explain"_s));
    REQUIRE(setup.conversation.busy());
    const AiProvider::Handlers late = setup.script->held;
    setup.conversation.clear();
    CHECK_FALSE(setup.conversation.busy());
    CHECK(setup.conversation.count() == 0);
    CHECK(setup.conversation.history().empty());
    late.done(answer(u"Too late"_s));
    CHECK(setup.conversation.count() == 0);

    setup.script->steps.push_back(replyWith(answer(u"Fresh start."_s)));
    REQUIRE(setup.conversation.send(u"Again"_s));
    REQUIRE(setup.script->requests.back().turns.size() == 1);
}

TEST_CASE("the guard keeps the tools on the phone and the profile being edited")
{
    const gs::app::AiScope scope{.serial = u"R58"_s, .profile = u"shooter"_s};
    const QJsonArray offered = gs::app::aiOfferedTools(FakeTools().tools(), scope);
    CHECK(offered.size() == FakeTools().tools().size() - 1);

    const auto frame = gs::app::aiGuard(u"get_frame"_s, {}, offered, scope);
    CHECK(frame.refusal.isEmpty());
    CHECK(frame.arguments.value(u"serial"_s).toString() == u"R58");
    CHECK(frame.arguments.value(u"maxSide"_s).toInt() == gs::app::kAiFrameSide);
    CHECK(gs::app::aiGuard(u"get_frame"_s, {{u"maxSide"_s, 300}}, offered, scope).arguments.value(u"maxSide"_s).toInt() ==
          300);
    CHECK_FALSE(gs::app::aiGuard(u"get_frame"_s, {{u"serial"_s, u"OTHER"_s}}, offered, scope).refusal.isEmpty());

    const auto read = gs::app::aiGuard(u"read_profile"_s, {}, offered, scope);
    CHECK(read.arguments.value(u"profile"_s).toString() == u"shooter");
    CHECK_FALSE(gs::app::aiGuard(u"read_profile"_s, {{u"profile"_s, u"racing"_s}}, offered, scope).refusal.isEmpty());

    CHECK(gs::app::aiGuard(u"read_logs"_s, {{u"source"_s, u"all"_s}}, offered, scope)
              .arguments.value(u"source"_s)
              .toString() == u"logic");
    CHECK_FALSE(gs::app::aiGuard(u"list_profiles"_s, {}, offered, scope).refusal.isEmpty());
    CHECK_FALSE(gs::app::aiGuard(u"nothing_like_it"_s, {}, offered, scope).refusal.isEmpty());
    CHECK(gs::app::aiGuard(u"read_draft"_s, {{u"target"_s, u"task:daily"_s}}, offered, scope).refusal.isEmpty());
    CHECK_FALSE(
        gs::app::aiGuard(u"read_draft"_s, {{u"target"_s, u"profile:racing"_s}}, offered, scope).refusal.isEmpty());
    CHECK(gs::app::aiGuard(u"read_draft"_s, {{u"target"_s, u"profile:shooter"_s}}, offered, scope).refusal.isEmpty());
    CHECK(gs::app::aiGuard(u"read_draft"_s, {{u"target"_s, u"component:mine/burst"_s}}, offered, scope)
              .refusal.isEmpty());
}

TEST_CASE("the system prompt names the conventions, the profile and the phone")
{
    const gs::app::AiContext context{.serial = u"R58"_s,
                                     .phone = u"Pixel"_s,
                                     .app = u"com.example.game"_s,
                                     .profile = u"shooter"_s,
                                     .profileName = u"Shooter"_s};
    const QString prompt = gs::app::aiSystemPrompt(context, u"{\"controls\": []}"_s, {});
    CHECK(prompt.contains(u"\"Shooter\" (id shooter)"));
    CHECK(prompt.contains(u"serial R58"));
    CHECK(prompt.contains(u"com.example.game"));
    CHECK(prompt.contains(u"(0, 0) at the top left"));
    CHECK(prompt.contains(u"draft"));
    CHECK(prompt.contains(u"run_replay"));
    CHECK(prompt.contains(u"{\"controls\": []}"));

    const QString big = gs::app::aiSystemPrompt(context, QString(70'000, u'x'), {});
    CHECK(big.contains(u"Cut here"));
    CHECK(big.size() < 70'000);
    CHECK(gs::app::aiSystemPrompt(context, {}, u"GS-MCP-006 no such profile"_s).contains(u"GS-MCP-006"));
    CHECK_FALSE(gs::app::aiSystemPrompt(gs::app::AiContext{}, {}, {}).contains(u"(id "));
}

TEST_CASE("ADR-0099 23:3x: outside a profile the assistant may name any profile or task, and a phone window keeps it on "
          "that phone")
{
    const QJsonArray tools = FakeTools().tools();
    const gs::app::AiScope anywhere;
    const QJsonArray offered = gs::app::aiOfferedTools(tools, anywhere);
    CHECK(offered.size() == tools.size());
    CHECK(gs::app::aiGuard(u"list_profiles"_s, {}, offered, anywhere).refusal.isEmpty());
    CHECK(gs::app::aiGuard(u"read_profile"_s, {{u"profile"_s, u"racing"_s}}, offered, anywhere).refusal.isEmpty());
    CHECK(gs::app::aiGuard(u"read_draft"_s, {{u"target"_s, u"task:daily"_s}}, offered, anywhere).refusal.isEmpty());
    CHECK(gs::app::aiGuard(u"read_draft"_s, {{u"target"_s, u"profile:racing"_s}}, offered, anywhere).refusal.isEmpty());

    const gs::app::AiScope phone{.serial = u"R58"_s};
    const QJsonArray mirrored = gs::app::aiOfferedTools(tools, phone);
    CHECK(gs::app::aiGuard(u"get_frame"_s, {}, mirrored, phone).arguments.value(u"serial"_s).toString() == u"R58");
    CHECK_FALSE(gs::app::aiGuard(u"get_frame"_s, {{u"serial"_s, u"OTHER"_s}}, mirrored, phone).refusal.isEmpty());
    CHECK(gs::app::aiGuard(u"read_profile"_s, {{u"profile"_s, u"racing"_s}}, mirrored, phone).refusal.isEmpty());
}

TEST_CASE("ADR-0099 23:3x: the system prompt says which window and page the user is in and what is selected there")
{
    gs::app::AiContext context;
    context.window = u"main"_s;
    context.page = u"profiles"_s;
    context.selection = u"the profile Racing"_s;
    const QString main = gs::app::aiSystemPrompt(context, {}, {});
    CHECK(main.contains(u"the main window, on the profiles page"));
    CHECK(main.contains(u"Selected there: the profile Racing."));
    CHECK(main.contains(u"list_profiles"));
    CHECK(main.contains(u"read_help"));
    CHECK(main.contains(u"read_guide"));
    CHECK(main.contains(u"open_page"));

    context.window = u"mirror"_s;
    context.page.clear();
    context.selection.clear();
    context.serial = u"R58"_s;
    context.phone = u"Pixel"_s;
    const QString mirror = gs::app::aiSystemPrompt(context, {}, {});
    CHECK(mirror.contains(u"a mirror window"));
    CHECK(mirror.contains(u"\"Pixel\" (serial R58)"));
    CHECK_FALSE(mirror.contains(u"(id "));

    context.window = u"nowhere"_s;
    CHECK_FALSE(gs::app::aiSystemPrompt(context, {}, {}).contains(u"The user is in"));
}

TEST_CASE("ADR-0099 23:3x: a saved conversation comes back with its rows and its history, without the pictures")
{
    Setup setup;
    setup.script->steps.push_back(
        replyWith(asking({AiToolCall{.id = u"call_1"_s, .name = u"get_frame"_s}}, u"Let me look."_s)));
    setup.script->steps.push_back(replyWith(answer(u"The fire button is at the lower right."_s)));
    REQUIRE(setup.conversation.send(u"Where is the fire button?"_s));
    const QJsonObject saved = setup.conversation.saved();
    CHECK_FALSE(QJsonDocument(saved).toJson().contains("AAAA"));

    Setup again;
    again.conversation.restore(saved);
    REQUIRE(again.conversation.count() == setup.conversation.count());
    for (int row = 0; row < setup.conversation.count(); ++row) {
        CHECK(again.kind(row) == setup.kind(row));
        CHECK(again.role(row, AiConversation::TextRole) == setup.role(row, AiConversation::TextRole));
    }
    REQUIRE(again.conversation.history().size() == setup.conversation.history().size());
    const AiTurn& tools = again.conversation.history()[2];
    CHECK(tools.role == AiTurn::Role::Tools);
    REQUIRE(tools.results.size() == 1);
    CHECK(gs::app::aiContentText(tools.results[0].content).contains(u"1080 x 2400"));
    CHECK(gs::app::aiContentText(tools.results[0].content).contains(u"picture"));

    again.script->steps.push_back(replyWith(answer(u"Still there."_s)));
    REQUIRE(again.conversation.send(u"And now?"_s));
    CHECK(again.script->requests.back().turns.size() == setup.conversation.history().size() + 1);

    Setup cut;
    QJsonObject halfway = saved;
    QJsonArray history = halfway.value(u"history"_s).toArray();
    history.removeLast();
    history.removeLast();
    halfway.insert(u"history"_s, history);
    cut.conversation.restore(halfway);
    REQUIRE(cut.conversation.history().size() == 3);
    CHECK(cut.conversation.history().back().role == AiTurn::Role::Tools);
    CHECK(cut.conversation.history().back().results.front().failed);
}

TEST_CASE("ADR-0099 2026-11-10: the model chosen in a conversation reaches the provider and is saved with it")
{
    Setup setup;
    CHECK(setup.conversation.choice().isEmpty());
    setup.script->always = replyWith(answer(u"Done."_s));
    REQUIRE(setup.conversation.send(u"Default model"_s));
    REQUIRE(setup.script->choices.size() == 1);
    CHECK(setup.script->choices[0] == gs::app::AiChoice{});
    CHECK_FALSE(setup.conversation.saved().contains(u"choice"_s));

    int changes = 0;
    QObject::connect(&setup.conversation, &AiConversation::choiceChanged, [&changes] { ++changes; });
    setup.conversation.setChoice(QVariantMap{{u"model"_s, u"  gpt-5  "_s},
                                             {u"think"_s, false},
                                             {u"effort"_s, u"turbo"_s},
                                             {u"longContext"_s, false}});
    CHECK(changes == 1);
    CHECK(setup.conversation.chosen() == gs::app::AiChoice{.model = u"gpt-5"_s});
    setup.conversation.setChoice(QVariantMap{{u"model"_s, u"gpt-5"_s}});
    CHECK(changes == 1);
    setup.conversation.setChoice(QVariantMap{{u"model"_s, u"gpt-5"_s}, {u"effort"_s, u"minimal"_s}});
    CHECK(changes == 2);
    REQUIRE(setup.conversation.send(u"Chosen model"_s));
    REQUIRE(setup.script->choices.size() == 2);
    CHECK(setup.script->choices[1] == gs::app::AiChoice{.model = u"gpt-5"_s, .effort = u"minimal"_s});

    const QJsonObject saved = setup.conversation.saved();
    CHECK(saved.value(u"choice"_s).toObject().value(u"model"_s).toString() == u"gpt-5");
    Setup again;
    again.conversation.restore(saved);
    CHECK(again.conversation.chosen() == setup.conversation.chosen());

    again.conversation.clear();
    CHECK(again.conversation.choice().isEmpty());
    again.conversation.setChoice(QVariantMap{{u"think"_s, true}});
    CHECK(again.conversation.choice().isEmpty());
}

TEST_CASE("tool rows get a short title that names what the tool did")
{
    CHECK(gs::app::aiToolTitle(u"run_replay"_s, {{u"untilMs"_s, 12000}}).contains(u"12"));
    CHECK(gs::app::aiToolTitle(u"write_profile"_s, {{u"files"_s, QJsonObject{{u"a.ts"_s, u""_s}, {u"b.ts"_s, u""_s}}}})
              .contains(u"2"));
    CHECK(gs::app::aiToolTitle(u"api_reference"_s, {{u"query"_s, u"vision.find"_s}}).contains(u"vision.find"));
    CHECK(gs::app::aiToolTitle(u"something_new"_s, {}).contains(u"something_new"));
    CHECK_FALSE(gs::app::aiToolTitle(u"read_profile"_s, {}).isEmpty());
}

TEST_CASE("ADR-0099 01:5x: the model's reasoning is a thinking row that folds with its seconds once the answer starts")
{
    Setup setup;
    setup.script->steps.push_back([](AiProvider::Handlers& handlers) {
        handlers.thinking(u"First "_s);
        handlers.thinking(u"idea."_s);
        handlers.text(u"Here."_s);
        handlers.done(AiReply{.text = u"Here."_s, .thinking = u"First idea."_s, .stop = u"stop"_s});
    });
    REQUIRE(setup.conversation.send(u"Help"_s));
    REQUIRE(setup.conversation.count() == 3);
    CHECK(setup.kind(1) == u"thinking");
    CHECK(setup.role(1, AiConversation::TextRole).toString() == u"First idea.");
    CHECK_FALSE(setup.role(1, AiConversation::RunningRole).toBool());
    CHECK(setup.role(1, AiConversation::SecondsRole).toInt() >= 1);
    CHECK(setup.kind(2) == u"assistant");

    REQUIRE(setup.conversation.send(u"And?"_s));
    setup.script->held.thinking(u"Hmm"_s);
    REQUIRE(setup.conversation.count() == 5);
    CHECK(setup.kind(4) == u"thinking");
    CHECK(setup.role(4, AiConversation::RunningRole).toBool());
    CHECK_FALSE(setup.conversation.waiting());
    setup.conversation.stop();
    CHECK_FALSE(setup.role(4, AiConversation::RunningRole).toBool());

    setup.script->steps.push_back([](AiProvider::Handlers& handlers) {
        handlers.done(AiReply{.text = u"Done."_s, .thinking = u"Quick."_s, .stop = u"stop"_s});
    });
    REQUIRE(setup.conversation.send(u"Once more"_s));
    const int rows = setup.conversation.count();
    CHECK(setup.kind(rows - 2) == u"thinking");
    CHECK(setup.role(rows - 2, AiConversation::TextRole).toString() == u"Quick.");
    CHECK(setup.kind(rows - 1) == u"assistant");
}

TEST_CASE("ADR-0099 01:5x: a tool row shows up when the model starts the call and turns from doing to done")
{
    Setup setup;
    setup.tools.held = u"read_logs"_s;
    setup.script->steps.push_back([](AiProvider::Handlers& handlers) {
        handlers.calling(u"read_logs"_s);
        handlers.calling(u"get_frame"_s);
        handlers.done(asking({AiToolCall{.id = u"c1"_s, .name = u"read_logs"_s}}));
    });
    setup.script->steps.push_back(replyWith(answer(u"Read."_s)));
    REQUIRE(setup.conversation.send(u"Logs?"_s));
    REQUIRE(setup.conversation.count() == 3);
    CHECK(setup.kind(1) == u"tool");
    CHECK(setup.role(1, AiConversation::TitleRole).toString() == gs::app::aiToolRunningTitle(u"read_logs"_s, {}));
    CHECK(setup.role(1, AiConversation::RunningRole).toBool());
    CHECK(setup.kind(2) == u"tool");
    CHECK(setup.role(2, AiConversation::FailedRole).toBool());
    CHECK_FALSE(setup.role(2, AiConversation::RunningRole).toBool());

    REQUIRE(setup.tools.waiting.size() == 1);
    setup.tools.waiting.front()(McpOutcome{
        .content = FakeTools::text(u"Wrote logic.ts (+3 -1), res/a.json (new, 2 lines) into this client's draft of x."_s)});
    REQUIRE(setup.conversation.count() == 4);
    CHECK(setup.role(1, AiConversation::TitleRole).toString() == gs::app::aiToolTitle(u"read_logs"_s, {}));
    CHECK_FALSE(setup.role(1, AiConversation::RunningRole).toBool());
    CHECK(setup.role(1, AiConversation::ChangesRole).toString() == u"+5 \u22121");
    CHECK(setup.kind(3) == u"assistant");

    AiConversation reopened;
    reopened.restore(setup.conversation.saved());
    REQUIRE(reopened.count() == 4);
    CHECK(reopened.data(reopened.index(1), AiConversation::ChangesRole).toString() == u"+5 \u22121");

    CHECK(gs::app::aiChangeSummary(u"Applied the draft of Racing: logic.ts (+1 -2)."_s) == u"+1 \u22122");
    CHECK(gs::app::aiChangeSummary(u"Draft of Racing: logic.ts (+1 -2)"_s).isEmpty());
    CHECK(gs::app::aiChangeSummary(u"Wrote nothing new"_s).isEmpty());
    CHECK(gs::app::aiToolRunningTitle(u"read_help"_s, {{u"query"_s, u"wifi"_s}}).contains(u"wifi"));
    CHECK(gs::app::aiToolRunningTitle(u"brand_new"_s, {}).contains(u"brand_new"));
    for (const QString& tool : {u"read_blocks"_s, u"read_settings"_s, u"list_commands"_s}) {
        CHECK(gs::app::aiToolTitle(tool, {{u"query"_s, u"bitrate"_s}}).contains(u"bitrate"));
        CHECK(gs::app::aiToolRunningTitle(tool, {{u"query"_s, u"bitrate"_s}}).contains(u"bitrate"));
        CHECK_FALSE(gs::app::aiToolTitle(tool, {}).contains(tool));
        CHECK_FALSE(gs::app::aiToolRunningTitle(tool, {}).contains(tool));
    }
    CHECK_FALSE(gs::app::aiToolTitle(u"read_connection_log"_s, {}).contains(u"read_connection_log"));
    CHECK_FALSE(gs::app::aiToolRunningTitle(u"read_connection_log"_s, {}).contains(u"read_connection_log"));
}

TEST_CASE("ADR-0099 01:5x: three or more tool calls in a row fold into one group")
{
    Setup setup;
    setup.script->steps.push_back(replyWith(asking({AiToolCall{.id = u"c1"_s, .name = u"read_logs"_s},
                                                    AiToolCall{.id = u"c2"_s, .name = u"read_draft"_s},
                                                    AiToolCall{.id = u"c3"_s, .name = u"list_profiles"_s}})));
    setup.script->steps.push_back(replyWith(answer(u"Done."_s)));
    REQUIRE(setup.conversation.send(u"Look around"_s));
    REQUIRE(setup.conversation.count() == 5);
    const QVariantMap first = setup.role(1, AiConversation::RunRole).toMap();
    CHECK(first.value(u"grouped"_s).toBool());
    CHECK(first.value(u"first"_s).toBool());
    CHECK(first.value(u"start"_s).toInt() == 1);
    CHECK(first.value(u"count"_s).toInt() == 3);
    CHECK_FALSE(first.value(u"running"_s).toBool());
    const QVariantMap last = setup.role(3, AiConversation::RunRole).toMap();
    CHECK(last.value(u"grouped"_s).toBool());
    CHECK_FALSE(last.value(u"first"_s).toBool());
    CHECK(setup.role(4, AiConversation::RunRole).toMap().isEmpty());

    Setup pair;
    pair.script->steps.push_back(replyWith(asking({AiToolCall{.id = u"c1"_s, .name = u"read_logs"_s},
                                                   AiToolCall{.id = u"c2"_s, .name = u"read_draft"_s}})));
    pair.script->steps.push_back(replyWith(answer(u"Done."_s)));
    REQUIRE(pair.conversation.send(u"Look"_s));
    CHECK_FALSE(pair.role(1, AiConversation::RunRole).toMap().value(u"grouped"_s).toBool());
}
