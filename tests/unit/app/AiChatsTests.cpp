#include "AiChats.h"
#include "AppSettings.h"
#include "Commands.h"
#include "DeviceCommands.h"
#include "HelpLibrary.h"
#include "McpTestSupport.h"

#include <doctest/doctest.h>

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantMap>

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

using gs::app::AiChats;
using gs::app::AiChatTools;
using gs::app::AiConversation;
using gs::app::AiProvider;
using gs::app::AiReply;
using gs::app::AiRequest;
using gs::app::McpOutcome;
using gs::app::McpToolHost;
using namespace Qt::StringLiterals;

namespace {

struct Log {
    std::vector<AiRequest> requests;
};

class Answering final : public AiProvider
{
public:
    explicit Answering(std::shared_ptr<Log> log)
        : m_log(std::move(log))
    {
    }

    void send(const AiRequest& request, Handlers handlers) override
    {
        m_log->requests.push_back(request);
        handlers.done(AiReply{.text = u"Sure."_s, .stop = u"stop"_s});
    }

    void cancel() override {}

private:
    std::shared_ptr<Log> m_log;
};

class Runner final : public gs::app::AiToolRunner
{
public:
    [[nodiscard]] QJsonArray tools() const override
    {
        return QJsonArray{QJsonObject{{u"name"_s, u"read_profile"_s},
                                      {u"description"_s, u"Test tool"_s},
                                      {u"inputSchema"_s, QJsonObject{{u"type"_s, u"object"_s}}}}};
    }

    void run(const QString& name, const QJsonObject&, McpToolHost::Reply reply) override
    {
        ran.push_back(name);
        reply(McpOutcome{.content = QJsonArray{QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, u"done"_s}}}});
    }

    std::vector<QString> ran;
};

class LogRunner final : public gs::app::AiToolRunner
{
public:
    [[nodiscard]] QJsonArray tools() const override { return {}; }

    void run(const QString& name, const QJsonObject& arguments, McpToolHost::Reply reply) override
    {
        asked = name;
        given = arguments;
        reply(McpOutcome{.content = QJsonArray{QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, log}}}});
    }

    QString log;
    QString asked;
    QJsonObject given;
};

class NullHost final : public gs::app::DeviceCommandHost
{
public:
    [[nodiscard]] std::vector<gs::app::DeviceFacts> devices() const override { return {}; }
    [[nodiscard]] std::optional<gs::app::DeviceFacts> device(const QString&) const override { return std::nullopt; }
    [[nodiscard]] bool canRetryAdb() const override { return false; }

    void retryAdb() override {}
    void connectDevice(const QString&) override {}
    void disconnectDevice(const QString&) override {}
    void showWindow(const QString&) override {}
    void setTransport(const QString&, int) override {}
    void setMuted(const QString&, bool) override {}
    void setVolume(const QString&, double) override {}
    void setScreenOff(const QString&, bool) override {}
    void setOrientation(const QString&, int) override {}
    void pressKey(const QString&, int) override {}
    void setDiagnostics(const QString&, bool) override {}
    void setFullScreen(const QString&, bool) override {}
    void setMini(const QString&, bool) override {}
    void setEditing(const QString&, bool) override {}
    void editorAction(const QString&, const QString&) override {}
    void setMeasuringLatency(const QString&, bool) override {}
    void toggleToolbar(const QString&) override {}
    void setKeymap(const QString&, bool) override {}
    void setCameraLocked(const QString&, bool) override {}
    void setKeyHints(const QString&, bool) override {}
    void stepProfile(const QString&, int) override {}
    void endWireless(const QString&) override {}
};

struct Rig {
    explicit Rig(const QString& folder)
        : chats([log = log] { return std::make_unique<Answering>(log); }, &runner, nullptr, folder.toStdWString())
    {
    }

    std::shared_ptr<Log> log = std::make_shared<Log>();
    Runner runner;
    AiChats chats;
};

QString textOf(const McpOutcome& outcome)
{
    return gs::app::aiContentText(outcome.content);
}

McpOutcome ran(gs::app::AiToolRunner& tools, const QString& name, const QJsonObject& arguments)
{
    McpOutcome got;
    tools.run(name, arguments, [&got](McpOutcome outcome) { got = std::move(outcome); });
    return got;
}

int jsonFiles(const QString& folder)
{
    return static_cast<int>(QDir(folder).entryList({u"*.json"_s}, QDir::Files).size());
}

}

TEST_CASE("ADR-0099 23:3x: every finished question saves the conversation, and a new one starts empty")
{
    gs::test::McpApplication application;
    const QTemporaryDir folder;
    REQUIRE(folder.isValid());
    Rig rig(folder.path());
    CHECK(rig.chats.recent().isEmpty());

    REQUIRE(rig.chats.send(u"How do I place a key?"_s,
                           QVariantMap{{u"window"_s, u"main"_s}, {u"page"_s, u"profiles"_s},
                                       {u"selection"_s, u"the profile Racing"_s}}));
    CHECK(rig.chats.conversation()->context().window == u"main");
    CHECK(rig.chats.conversation()->context().page == u"profiles");
    REQUIRE(rig.log->requests.size() == 1);
    CHECK(rig.log->requests[0].system.contains(u"the main window, on the profiles page"));
    CHECK(rig.log->requests[0].system.contains(u"the profile Racing"));
    bool offersHelp = false;
    for (const QJsonValue& tool : rig.log->requests[0].tools) {
        offersHelp = offersHelp || tool.toObject().value(u"name"_s).toString() == u"read_help";
    }
    CHECK(offersHelp);

    REQUIRE(rig.chats.recent().size() == 1);
    const QVariantMap first = rig.chats.recent().front().toMap();
    CHECK(first.value(u"title"_s).toString() == u"How do I place a key?");
    CHECK(rig.chats.current() == first.value(u"id"_s).toString());
    CHECK(jsonFiles(folder.path()) == 1);
    const int rows = rig.chats.conversation()->count();
    CHECK(rows == 2);

    rig.chats.newConversation();
    CHECK(rig.chats.conversation()->count() == 0);
    CHECK(rig.chats.current().isEmpty());
    REQUIRE(rig.chats.send(u"And a second question that is long enough to be cut in the list of conversations"_s, {}));
    REQUIRE(rig.chats.recent().size() == 2);
    const QString title = rig.chats.recent().front().toMap().value(u"title"_s).toString();
    CHECK(title.size() == AiChats::kMostTitleChars + 1);
    CHECK(title.endsWith(u"\u2026"));

    REQUIRE(rig.chats.open(first.value(u"id"_s).toString()));
    CHECK(rig.chats.conversation()->count() == rows);
    CHECK(rig.chats.current() == first.value(u"id"_s).toString());
    CHECK_FALSE(rig.chats.open(u"../elsewhere"_s));

    Rig reopened(folder.path());
    REQUIRE(reopened.chats.recent().size() == 2);
    QStringList ids;
    for (const QVariant& entry : reopened.chats.recent()) {
        ids.append(entry.toMap().value(u"id"_s).toString());
    }
    CHECK(ids.contains(first.value(u"id"_s).toString()));
    CHECK(reopened.chats.conversation()->count() == 0);
}

TEST_CASE("ADR-0099 23:3x: only the newest 20 conversations are kept, and they can be removed one by one or all")
{
    gs::test::McpApplication application;
    const QTemporaryDir folder;
    REQUIRE(folder.isValid());
    Rig rig(folder.path());
    for (int question = 0; question < AiChats::kMostKept + 1; ++question) {
        rig.chats.newConversation();
        REQUIRE(rig.chats.send(u"Question %1"_s.arg(question), {}));
    }
    REQUIRE(rig.chats.recent().size() == AiChats::kMostKept);
    CHECK(jsonFiles(folder.path()) == AiChats::kMostKept);
    CHECK(rig.chats.recent().front().toMap().value(u"title"_s).toString() == u"Question 20");
    CHECK(rig.chats.recent().back().toMap().value(u"title"_s).toString() == u"Question 1");

    const QString newest = rig.chats.recent().front().toMap().value(u"id"_s).toString();
    rig.chats.remove(newest);
    CHECK(rig.chats.recent().size() == AiChats::kMostKept - 1);
    CHECK(rig.chats.conversation()->count() == 0);
    CHECK(rig.chats.current().isEmpty());

    rig.chats.clearAll();
    CHECK(rig.chats.recent().isEmpty());
    CHECK(jsonFiles(folder.path()) == 0);
}

TEST_CASE("ADR-0099 23:3x: the assistant reads the guides, reads the help and only shows pages, never changes them")
{
    gs::test::McpApplication application;
    const QTemporaryDir data;
    REQUIRE(data.isValid());
    const gs::app::AppSettings settings(data.path().toStdWString());
    const gs::app::HelpLibrary help(settings);
    Runner base;
    QString shownPage;
    QString shownSection;
    AiChatTools tools(&base, &help, [&](const QString& page, const QString& section) {
        shownPage = page;
        shownSection = section;
    });
    CHECK(tools.tools().size() == base.tools().size() + 7);

    const QString guides = textOf(ran(tools, u"read_guide"_s, {}));
    CHECK(guides.contains(u"make_keymap"));
    CHECK(guides.contains(u"make_extension"));
    CHECK(textOf(ran(tools, u"read_guide"_s, {{u"name"_s, u"make_keymap"_s}})).contains(u"Job:"));
    CHECK(ran(tools, u"read_guide"_s, {{u"name"_s, u"nothing"_s}}).failed);

    CHECK(textOf(ran(tools, u"read_help"_s, {})).contains(u"- connecting:"));
    const McpOutcome article = ran(tools, u"read_help"_s, {{u"topic"_s, u"connecting"_s}});
    CHECK_FALSE(article.failed);
    CHECK(textOf(article).startsWith(u"# "));
    CHECK(ran(tools, u"read_help"_s, {{u"topic"_s, u"no-such-article"_s}}).failed);
    CHECK(textOf(ran(tools, u"read_help"_s, {{u"query"_s, u"zzzz-not-in-any-article"_s}})).contains(u"No help"));

    const McpOutcome shown = ran(tools, u"open_page"_s, {{u"page"_s, u"settings"_s}, {u"section"_s, u"assistant"_s}});
    CHECK_FALSE(shown.failed);
    CHECK(shownPage == u"settings");
    CHECK(shownSection == u"assistant");
    shownPage.clear();
    CHECK(ran(tools, u"open_page"_s, {{u"page"_s, u"wherever"_s}}).failed);
    CHECK(shownPage.isEmpty());

    CHECK(textOf(ran(tools, u"read_profile"_s, {})) == u"done");
    REQUIRE(base.ran.size() == 1);
    CHECK(base.ran.front() == u"read_profile");

    AiChatTools lonely(nullptr, nullptr, {});
    CHECK(ran(lonely, u"read_help"_s, {}).failed);
    CHECK(ran(lonely, u"read_profile"_s, {}).failed);
}

TEST_CASE("ADR-0099 02:4x: the assistant looks up the blocks, the settings with how they are set and the commands "
          "with their shortcuts")
{
    gs::test::McpApplication application;
    const QTemporaryDir data;
    REQUIRE(data.isValid());
    gs::app::AppSettings settings(data.path().toStdWString());
    REQUIRE(settings.setValue(u"assistant.openai.address"_s, u"http://secret.example:8080"_s));
    NullHost host;
    const gs::app::Commands commands(host);
    AiChatTools tools(nullptr, nullptr, {});

    const QString groups = textOf(ran(tools, u"read_blocks"_s, {}));
    CHECK(groups.startsWith(u"Blocks by group"));
    CHECK(groups.contains(u"Branch"));
    CHECK(groups.contains(u"blocks of a Movement component"));
    const QString tap = textOf(ran(tools, u"read_blocks"_s, {{u"query"_s, u"tap screen"_s}}));
    CHECK(tap.contains(u"- Tap Screen"));
    CHECK(tap.contains(u"touch.tap"));
    CHECK(textOf(ran(tools, u"read_blocks"_s, {{u"query"_s, u"tap button"_s}}))
              .contains(u"blocks of a Tap button component"));
    CHECK(textOf(ran(tools, u"read_blocks"_s, {{u"query"_s, u"zzzz-no-block"_s}})).startsWith(u"No block"));

    const QString sections = textOf(ran(tools, u"read_settings"_s, {}));
    CHECK(sections.contains(u"- video: "));
    CHECK(sections.contains(u"Maximum resolution"));
    const QString size = textOf(ran(tools, u"read_settings"_s, {{u"query"_s, u"maximum resolution"_s}}));
    CHECK(size.contains(u"`video.maxSize`, section video"));
    CHECK(size.contains(u"from 0 to 8192"));
    const QString address =
        textOf(ran(tools, u"read_settings"_s, {{u"query"_s, u"address"_s}, {u"section"_s, u"ai"_s}}));
    CHECK(address.contains(u"`assistant.openai.address`, section ai): now filled in (not shown)"));
    CHECK_FALSE(address.contains(u"secret.example"));
    CHECK(textOf(ran(tools, u"read_settings"_s, {{u"section"_s, u"nowhere"_s}})).startsWith(u"No setting"));

    CHECK(textOf(ran(tools, u"list_commands"_s, {})).contains(u"shortcut F1"));
    const QString assistant = textOf(ran(tools, u"list_commands"_s, {{u"query"_s, u"app.assistant"_s}}));
    CHECK(assistant.contains(u"`app.assistant`"));
    CHECK(assistant.contains(u"shortcut Ctrl+J"));
    CHECK(textOf(ran(tools, u"list_commands"_s, {{u"query"_s, u"zzzz-no-command"_s}})).startsWith(u"No command"));
}

TEST_CASE("ADR-0099 02:4x: the connection log keeps only the lines about connecting and mirroring, newest last, "
          "without the user's folder")
{
    const QString log =
        u"2026-11-10 01:00:00.000 info     GameScrcpy 2.0.0 started\n"
        u"2026-11-10 01:00:01.000 info     mcp: listening for AI clients on the pipe GameScrcpy-MCP\n"
        u"2026-11-10 01:00:02.000 info     connecting to Pixel (R58)\n"
        u"2026-11-10 01:00:03.000 warning  [session] [R58] streaming: timeout while handshaking (no answer)\n"
        u"  the next line of that warning\n"
        u"2026-11-10 01:00:04.000 info     key mapping on\n"
        u"2026-11-10 01:00:05.000 info     [server] [R58] pushed C:/Users/Someone/AppData/server.apk (2 bytes)\n"
        u"2026-11-10 01:00:06.000 info     the picture of Pixel comes from C:\\Users\\Someone\\card\r\n"_s;
    const QString kept = gs::app::aiConnectionLog(log, u"C:/Users/Someone"_s, 10);
    CHECK(kept.startsWith(u"Recent lines about phones connecting and mirroring"));
    CHECK(kept.contains(u"GameScrcpy 2.0.0 started"));
    CHECK(kept.contains(u"connecting to Pixel (R58)"));
    CHECK(kept.contains(u"[session] [R58] streaming"));
    CHECK(kept.contains(u"\n  the next line of that warning"));
    CHECK(kept.contains(u"pushed ~/AppData/server.apk"));
    CHECK(kept.contains(u"comes from ~\\card"));
    CHECK_FALSE(kept.contains(u"mcp:"));
    CHECK_FALSE(kept.contains(u"key mapping"));
    CHECK_FALSE(kept.contains(u"Someone"));
    CHECK_FALSE(kept.contains(u'\r'));

    const QString newest = gs::app::aiConnectionLog(log, {}, 2);
    CHECK(newest.contains(u"pushed"));
    CHECK(newest.contains(u"the picture of Pixel"));
    CHECK_FALSE(newest.contains(u"connecting to Pixel"));
    CHECK(gs::app::aiConnectionLog(u"2026-11-10 01:00:04.000 info     key mapping on\n"_s, {}, 10)
              .startsWith(u"The recent log has no line"));

    gs::test::McpApplication application;
    LogRunner base;
    base.log = log;
    AiChatTools tools(&base, nullptr, {});
    const QString read = textOf(ran(tools, u"read_connection_log"_s, {{u"phone"_s, u"R58"_s}}));
    CHECK(base.asked == u"read_logs");
    CHECK(base.given.value(u"source"_s).toString() == u"program");
    CHECK(base.given.value(u"contains"_s).toString() == u"R58");
    CHECK(base.given.value(u"lines"_s).toInt() == AiChatTools::kReadLogLines);
    CHECK(read.contains(u"connecting to Pixel (R58)"));
    CHECK_FALSE(read.contains(u"key mapping"));
    ran(tools, u"read_connection_log"_s, {});
    CHECK_FALSE(base.given.contains(u"contains"));
    AiChatTools lonely(nullptr, nullptr, {});
    CHECK(ran(lonely, u"read_connection_log"_s, {}).failed);
}
