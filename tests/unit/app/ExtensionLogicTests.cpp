#include "ComponentLibrary.h"
#include "ControlChannel.h"
#include "ExtensionContent.h"
#include "ExtensionLogic.h"
#include "InputHost.h"
#include "LogicServices.h"
#include "McpTestSupport.h"
#include "McpTools.h"
#include "ProfileEditor.h"
#include "ScriptRuntime.h"

#include <gs/base/Clock.h>
#include <gs/input/Profile.h>
#include <gs/scripthost/RemoteLogicHost.h>

#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>
#include <tlhelp32.h>

using namespace gs;
using namespace gs::app;
using namespace std::chrono_literals;

namespace {

class RecordingSink final : public ControlSink
{
public:
    bool send(const protocol::TouchFrame& frame) override
    {
        const std::scoped_lock lock(m_mutex);
        m_frames.push_back(frame);
        return true;
    }

    bool send(const protocol::Key&) override { return true; }
    bool send(const protocol::Scroll&) override { return true; }
    bool send(const protocol::Text&) override { return true; }

    [[nodiscard]] std::size_t scriptDowns() const
    {
        const std::scoped_lock lock(m_mutex);
        std::size_t downs = 0;
        for (const protocol::TouchFrame& frame : m_frames) {
            for (const protocol::TouchPoint& point : frame.points) {
                if (point.pointerId >= InputHost::kEnginePointers && point.action == protocol::TouchAction::Down) {
                    ++downs;
                }
            }
        }
        return downs;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<protocol::TouchFrame> m_frames;
};

struct Phone {
    explicit Phone(std::uint16_t videoSession)
    {
        channel->attach(sink);
        channel->setVideo(videoSession, 2400, 1080);
    }

    ~Phone() { channel->detach(); }

    Phone(const Phone&) = delete;
    Phone& operator=(const Phone&) = delete;

    RecordingSink sink;
    std::shared_ptr<ControlChannel> channel = std::make_shared<ControlChannel>();
};

class Rig
{
public:
    using Task = std::function<void(InputHost&, Microseconds)>;

    [[nodiscard]] Microseconds now() const
    {
        return std::chrono::duration_cast<Microseconds>(std::chrono::steady_clock::now() - m_started);
    }

    void post(Task task)
    {
        const std::scoped_lock lock(m_mutex);
        m_tasks.push_back(std::move(task));
    }

    void pump()
    {
        std::deque<Task> tasks;
        {
            const std::scoped_lock lock(m_mutex);
            tasks.swap(m_tasks);
        }
        for (Task& task : tasks) {
            task(host, now());
        }
        if (const std::optional<Microseconds> due = host.nextWakeup(); due && *due <= now()) {
            host.wake(now());
        }
    }

    template <typename Condition>
    bool until(Condition condition, std::chrono::milliseconds timeout = 30s)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!condition()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            QCoreApplication::processEvents();
            pump();
            std::this_thread::sleep_for(1ms);
        }
        return true;
    }

    void settle(std::chrono::milliseconds time)
    {
        static_cast<void>(until([] { return false; }, time));
    }

    InputHost host{{.pressedMouse = [] { return std::uint8_t{0}; }}};

private:
    std::mutex m_mutex;
    std::deque<Task> m_tasks;
    const std::chrono::steady_clock::time_point m_started = std::chrono::steady_clock::now();
};

class FakeLane final : public LaneAccess
{
public:
    FakeLane(Rig& rig, std::shared_ptr<ControlChannel> phone)
        : m_rig(rig)
        , m_phone(std::move(phone))
    {
    }

    void attach(std::shared_ptr<const input::Profile> profile, std::shared_ptr<LaneObserver> observer) override
    {
        m_rig.post([target = m_phone, profile = std::move(profile), observer = std::move(observer)](
                       InputHost& host, Microseconds now) { host.attach(target, profile, observer, now); });
    }

    void detach(std::shared_ptr<LaneObserver> observer) override
    {
        m_rig.post([target = m_phone, observer = std::move(observer)](InputHost& host, Microseconds now) {
            host.detach(target.get(), observer.get(), now);
        });
    }

    void post(std::function<void(InputHost& host, Microseconds now)> task) override { m_rig.post(std::move(task)); }

    void watch(std::shared_ptr<const input::Profile> profile, std::shared_ptr<LaneObserver> observer) override
    {
        m_rig.post([target = m_phone, profile = std::move(profile), observer = std::move(observer)](
                       InputHost& host, Microseconds now) { host.watch(target, profile, observer, now); });
    }

    void unwatch(std::shared_ptr<LaneObserver> observer) override
    {
        m_rig.post([target = m_phone, observer = std::move(observer)](InputHost& host, Microseconds now) {
            host.unwatch(target.get(), observer.get(), now);
        });
    }

    [[nodiscard]] const ControlChannel* channel() const noexcept override { return m_phone.get(); }

private:
    Rig& m_rig;
    std::shared_ptr<ControlChannel> m_phone;
};

constexpr std::string_view kProfile = R"({"format":"gamescrcpy.profile","version":3,
    "meta":{"name":"Extension test"},
    "controls":[{"id":"jump","type":"tap","anchor":"bottomRight","offset":[-0.2,-0.2],"inputs":{"press":"Space"}}],
    "vars":[]})";

constexpr std::string_view kCodeOf = R"(const codeOf = (action) => {
    try {
        action();
        return "none";
    } catch (error) {
        return error.message.split(" ")[0];
    }
};
)";

std::shared_ptr<const input::Profile> profileOf(std::string_view json)
{
    const input::LoadResult loaded = input::loadProfile(json);
    REQUIRE(loaded.ok());
    return std::make_shared<const input::Profile>(loaded.profile);
}

class Folder
{
public:
    Folder() { REQUIRE(m_dir.isValid()); }

    void write(const QString& name, std::string_view contents) const
    {
        REQUIRE(QDir(m_dir.path()).mkpath(QFileInfo(m_dir.filePath(name)).path()));
        QFile file(m_dir.filePath(name));
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(file.write(contents.data(), static_cast<qint64>(contents.size())) == static_cast<qint64>(contents.size()));
    }

    [[nodiscard]] std::filesystem::path path() const { return m_dir.path().toStdWString(); }

private:
    QTemporaryDir m_dir;
};

class Reports
{
public:
    void watch(ScriptRuntime& runtime)
    {
        QObject::connect(&runtime, &ScriptRuntime::reported,
                         [this](const LogicReport& report) { m_texts.append(report.text); });
    }

    void watch(ExtensionLogic& logic)
    {
        QObject::connect(&logic, &ExtensionLogic::reported, [this](const QString& serial, const LogicReport& report) {
            m_texts.append(serial + u'|' + report.text);
        });
    }

    [[nodiscard]] bool has(const QString& text) const { return m_texts.contains(text); }
    [[nodiscard]] qsizetype count(const QString& text) const { return m_texts.count(text); }

private:
    QStringList m_texts;
};

LogicSource extensionSource(const Folder& folder, std::shared_ptr<const input::Profile> profile,
                            std::vector<std::string> permissions)
{
    return LogicSource{.folder = folder.path(),
                       .entry = "logic.ts",
                       .profile = std::move(profile),
                       .id = QStringLiteral("helper"),
                       .name = QStringLiteral("Helper"),
                       .device = QStringLiteral("test phone"),
                       .deviceKey = QStringLiteral("test-phone"),
                       .extension = QStringLiteral("alice.helper"),
                       .permissions = std::move(permissions)};
}

struct Setup {
    Setup() : profile(profileOf(kProfile))
    {
        rig.host.setChannel(phone.channel, rig.now());
        rig.host.setProfile(profile, rig.now());
        rig.host.setActive(true, rig.now());
    }

    gs::test::McpApplication application;
    Phone phone{7};
    Rig rig;
    std::shared_ptr<const input::Profile> profile;
};

bool runningLogic(const QString& script, const QString& device)
{
    const std::vector<LogicCaller> running = LogicServices::instance().running();
    return std::ranges::any_of(running, [&](const LogicCaller& caller) {
        return caller.extension == u"alice.helper" && caller.script == script && caller.device == device;
    });
}

bool listed(const QJsonArray& tools, const QString& name)
{
    return std::ranges::any_of(tools, [&name](const QJsonValue& tool) {
        return tool.toObject().value(QStringLiteral("name")).toString() == name;
    });
}

ExtensionScript contributed(const Folder& folder, const QString& name, std::string entry, bool app,
                            std::vector<std::string> permissions)
{
    return ExtensionScript{.extensionId = QStringLiteral("alice.helper"),
                           .extensionName = QStringLiteral("Helper"),
                           .name = name,
                           .folder = folder.path(),
                           .entry = std::move(entry),
                           .app = app,
                           .permissions = std::move(permissions)};
}

ExtensionLogic::Phone sessionPhone(Setup& setup)
{
    return ExtensionLogic::Phone{
        .serial = QStringLiteral("phone-1"),
        .name = QStringLiteral("test phone"),
        .deviceKey = QStringLiteral("test-phone"),
        .channel = setup.phone.channel.get(),
        .lane = [&setup] { return std::make_shared<TaskLane>(std::make_shared<FakeLane>(setup.rig, setup.phone.channel)); },
        .snapshots = nullptr,
        .profile = setup.profile,
        .device = {.model = "Pixel test", .android = 15, .width = 1080, .height = 2400},
        .frameRate = 60.0};
}

QString stoppedText()
{
    return QString::fromUtf8(gs::scripthost::kStoppedText.data(),
                             static_cast<qsizetype>(gs::scripthost::kStoppedText.size()));
}

struct Shown {
    QString serial;
    quint64 id = 0;
    QVariantMap value;
};

class Seen
{
public:
    explicit Seen(ExtensionLogic& logic)
    {
        QObject::connect(&logic, &ExtensionLogic::prompted, &m_context,
                         [this](const QString& serial, quint64 id, const QVariantMap& value) {
                             prompts.push_back(Shown{.serial = serial, .id = id, .value = value});
                         });
        QObject::connect(&logic, &ExtensionLogic::promptWithdrawn, &m_context, [this](const QString& serial, quint64 id) {
            withdrawn.push_back(Shown{.serial = serial, .id = id, .value = {}});
        });
        QObject::connect(&logic, &ExtensionLogic::saveWanted, &m_context,
                         [this](const QString& serial, quint64 id, const QVariantMap& value) {
                             saves.push_back(Shown{.serial = serial, .id = id, .value = value});
                         });
        QObject::connect(&logic, &ExtensionLogic::drawn, &m_context,
                         [this](const QString& serial, quint64 id, const QVariantMap& value) {
                             drawings.push_back(Shown{.serial = serial, .id = id, .value = value});
                         });
        QObject::connect(&logic, &ExtensionLogic::erased, &m_context, [this](const QString& serial, quint64 id) {
            erased.push_back(Shown{.serial = serial, .id = id, .value = {}});
        });
    }

    std::vector<Shown> prompts;
    std::vector<Shown> withdrawn;
    std::vector<Shown> saves;
    std::vector<Shown> drawings;
    std::vector<Shown> erased;

private:
    QObject m_context;
};

int stopSandboxes()
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    int stopped = 0;
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        if (entry.th32ParentProcessID != GetCurrentProcessId() || lstrcmpiW(entry.szExeFile, L"gs-scripthost.exe") != 0) {
            continue;
        }
        if (const HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID); process != nullptr) {
            if (TerminateProcess(process, 1) != FALSE) {
                ++stopped;
            }
            CloseHandle(process);
        }
    }
    CloseHandle(snapshot);
    return stopped;
}

}

TEST_CASE("an extension's logic runs in the sandbox with only the permissions the user allowed, and keeps its own "
          "store (ADR-0105)")
{
    Setup setup;
    Folder store;
    Folder folder;
    folder.write(QStringLiteral("logic.ts"), std::string(kCodeOf) + R"(log.info("tap " + codeOf(() => touch.tap(0.5, 0.5)));
store.set("seen", true);
)");
    {
        Reports reports;
        ScriptRuntime denied(extensionSource(folder, setup.profile, {}), std::make_shared<FakeLane>(setup.rig, setup.phone.channel),
                             store.path());
        reports.watch(denied);
        CHECK(denied.sandboxed());
        CHECK(denied.storePath() == store.path() / L".extensions" / L"alice.helper" / L"helper" / L"test-phone.json");
        REQUIRE(setup.rig.until([&] { return reports.has(QStringLiteral("tap GS-API-072")); }));
        setup.rig.settle(200ms);
        CHECK(setup.phone.sink.scriptDowns() == 0);
        CHECK(setup.rig.until([&] {
            std::error_code error;
            return std::filesystem::exists(denied.storePath(), error);
        }));
    }
    {
        Reports reports;
        ScriptRuntime allowed(extensionSource(folder, setup.profile, {"device.input"}),
                              std::make_shared<FakeLane>(setup.rig, setup.phone.channel), store.path());
        reports.watch(allowed);
        REQUIRE(setup.rig.until([&] { return reports.has(QStringLiteral("tap none")); }));
        CHECK(setup.rig.until([&] { return setup.phone.sink.scriptDowns() == 1; }));
    }
    std::error_code error;
    CHECK_FALSE(std::filesystem::exists(store.path() / L"helper", error));
}

TEST_CASE("an extension's task and the logic of an extension's profile run in the sandbox, the user's own logic does "
          "not (ADR-0105)")
{
    Setup setup;
    Folder store;
    Folder folder;
    folder.write(QStringLiteral("task.ts"), R"(export default class Daily extends Task {
    async run() {
        log.info("task ran");
    }
}
)");
    folder.write(QStringLiteral("logic.ts"), "log.info(\"profile logic ran\");\n");

    LogicSource task{.folder = folder.path(),
                     .entry = "task.ts",
                     .profile = setup.profile,
                     .id = QStringLiteral("task-alice.helper/daily"),
                     .name = QStringLiteral("Daily"),
                     .device = QStringLiteral("test phone"),
                     .deviceKey = QStringLiteral("test-phone"),
                     .task = true,
                     .extension = QStringLiteral("alice.helper")};
    ScriptRuntime taskRun(std::move(task),
                          std::make_shared<TaskLane>(std::make_shared<FakeLane>(setup.rig, setup.phone.channel)),
                          store.path());
    QString ended;
    QObject::connect(&taskRun, &ScriptRuntime::ended, [&ended](const QString& result, const QString&) { ended = result; });
    CHECK(taskRun.sandboxed());
    CHECK(taskRun.caller().task);
    CHECK(taskRun.caller().extension == u"alice.helper");

    LogicSource profileLogic = extensionSource(folder, setup.profile, {});
    profileLogic.id = QStringLiteral("alice.helper/standard");
    ScriptRuntime contributed(std::move(profileLogic), std::make_shared<FakeLane>(setup.rig, setup.phone.channel),
                              store.path());
    int loads = 0;
    QObject::connect(&contributed, &ScriptRuntime::loaded, [&loads] { ++loads; });
    CHECK(contributed.sandboxed());

    REQUIRE(setup.rig.until([&] { return ended == u"done" && loads == 1; }));

    Phone other(9);
    LogicSource own = extensionSource(folder, setup.profile, {});
    own.extension.clear();
    own.permissions.clear();
    ScriptRuntime mine(std::move(own), std::make_shared<FakeLane>(setup.rig, other.channel), store.path());
    CHECK_FALSE(mine.sandboxed());
}

TEST_CASE("app scripts of an extension run without a phone and session scripts once per phone, and LogicServices "
          "names them for panels (ADR-0105)")
{
    Setup setup;
    Folder store;
    Folder folder;
    folder.write(QStringLiteral("app.ts"), std::string(kCodeOf) + "log.info(\"app \" + codeOf(() => device.info()));\n");
    folder.write(QStringLiteral("session.ts"), "log.info(\"session \" + device.info().model);\n");
    const std::vector<ExtensionScript> scripts{
        ExtensionScript{.extensionId = QStringLiteral("alice.helper"),
                        .extensionName = QStringLiteral("Helper"),
                        .name = QStringLiteral("tools"),
                        .folder = folder.path(),
                        .entry = "app.ts",
                        .app = true,
                        .permissions = {}},
        ExtensionScript{.extensionId = QStringLiteral("alice.helper"),
                        .extensionName = QStringLiteral("Helper"),
                        .name = QStringLiteral("watcher"),
                        .folder = folder.path(),
                        .entry = "session.ts",
                        .app = false,
                        .permissions = {"device.input"}}};
    ExtensionLogic logic(store.path());
    Reports reports;
    reports.watch(logic);
    std::vector<QString> started;
    QObject::connect(&logic, &ExtensionLogic::sessionStarted,
                     [&started](const QString& serial, ScriptRuntime*) { started.push_back(serial); });

    logic.setScripts(scripts);
    REQUIRE(logic.appRuntimes().size() == 1);
    CHECK(logic.appRuntimes().front()->sandboxed());
    REQUIRE(setup.rig.until([&] { return reports.has(QStringLiteral("|app GS-API-073")); }));
    CHECK(runningLogic(QStringLiteral("tools"), QString()));

    const auto phone = [&setup] {
        return ExtensionLogic::Phone{
            .serial = QStringLiteral("phone-1"),
            .name = QStringLiteral("test phone"),
            .deviceKey = QStringLiteral("test-phone"),
            .channel = setup.phone.channel.get(),
            .lane = [&setup] { return std::make_shared<TaskLane>(std::make_shared<FakeLane>(setup.rig, setup.phone.channel)); },
            .snapshots = nullptr,
            .profile = setup.profile,
            .device = {.model = "Pixel test", .android = 15, .width = 1080, .height = 2400},
            .frameRate = 60.0};
    };
    logic.startSession(phone());
    REQUIRE(logic.sessionRuntimes(QStringLiteral("phone-1")).size() == 1);
    CHECK(started == std::vector<QString>{QStringLiteral("phone-1")});
    CHECK(logic.runs(QStringLiteral("phone-1"), setup.phone.channel.get()));
    REQUIRE(setup.rig.until([&] { return reports.has(QStringLiteral("phone-1|session Pixel test")); }));
    CHECK(runningLogic(QStringLiteral("watcher"), QStringLiteral("phone-1")));
    logic.startSession(phone());
    CHECK(logic.sessionRuntimes(QStringLiteral("phone-1")).size() == 1);
    CHECK(started.size() == 1);

    logic.endSession(QStringLiteral("phone-1"));
    CHECK(logic.sessionRuntimes(QStringLiteral("phone-1")).empty());
    CHECK_FALSE(runningLogic(QStringLiteral("watcher"), QStringLiteral("phone-1")));
    CHECK(runningLogic(QStringLiteral("tools"), QString()));

    logic.setScripts({});
    CHECK(logic.appRuntimes().empty());
    CHECK_FALSE(runningLogic(QStringLiteral("tools"), QString()));
}

TEST_CASE("an extension's app script offers an MCP tool, a confirmed call reaches it and comes back, and the tool "
          "leaves with the logic (SCR-021)")
{
    gs::test::McpApplication application;
    gs::ManualClock clock;
    McpTools tools({}, clock);
    int changes = 0;
    QObject::connect(&tools, &McpTools::toolsChanged, [&changes] { ++changes; });
    Folder store;
    Folder folder;
    folder.write(QStringLiteral("app.ts"), R"(mcp.tool("greet", { description: "Says hello",
    input: { type: "object", properties: { name: { type: "string" } } } }, async (input) => "Hello " + input.name);
log.info("offered");
)");
    ExtensionLogic logic(store.path());
    logic.setScripts({ExtensionScript{.extensionId = QStringLiteral("alice.helper"),
                                      .extensionName = QStringLiteral("Helper"),
                                      .name = QStringLiteral("tools"),
                                      .folder = folder.path(),
                                      .entry = "app.ts",
                                      .app = true,
                                      .permissions = {}}});
    const QString name = mcpExtensionToolName(QStringLiteral("alice.helper"), QStringLiteral("greet"));
    CHECK(name == u"ext_alice_helper_greet");
    REQUIRE(gs::test::waitUntil([&tools] { return !tools.extensionTools().empty(); }, 30s));
    CHECK(gs::test::waitUntil([&changes] { return changes > 0; }));
    CHECK(listed(tools.tools(), name));
    const std::vector<McpExtensionTool> offered = tools.extensionTools();
    REQUIRE(offered.size() == 1);
    CHECK(offered.front().extension == u"alice.helper");
    CHECK(offered.front().input.value(QStringLiteral("type")).toString() == u"object");

    const auto call = [&tools, &name](quint64 id, McpAnswer answer, QString& kind, std::optional<McpOutcome>& outcome) {
        tools.call(McpCall{.id = id,
                           .client = QStringLiteral("Test client"),
                           .tool = name,
                           .arguments = QJsonObject{{QStringLiteral("name"), QStringLiteral("Ada")}},
                           .confirm =
                               [&kind, answer](McpConfirmation shown, std::function<void(McpAnswer)> done) {
                                   kind = shown.kind;
                                   done(answer);
                               }},
                   [&outcome](McpOutcome made) { outcome = std::move(made); });
    };
    QString kind;
    std::optional<McpOutcome> outcome;
    call(1, McpAnswer::Allowed, kind, outcome);
    REQUIRE(gs::test::waitUntil([&outcome] { return outcome.has_value(); }, 30s));
    CHECK(kind == u"tool");
    CHECK_FALSE(outcome->failed);
    REQUIRE(outcome->content.size() == 1);
    CHECK(outcome->content.at(0).toObject().value(QStringLiteral("text")).toString() == u"Hello Ada");

    kind.clear();
    outcome.reset();
    call(2, McpAnswer::Refused, kind, outcome);
    REQUIRE(gs::test::waitUntil([&outcome] { return outcome.has_value(); }));
    CHECK(outcome->failed);
    CHECK(outcome->code == u"GS-MCP-008");

    const int before = changes;
    logic.setScripts({});
    CHECK(tools.extensionTools().empty());
    CHECK_FALSE(listed(tools.tools(), name));
    CHECK(changes > before);
    outcome.reset();
    call(3, McpAnswer::Allowed, kind, outcome);
    REQUIRE(gs::test::waitUntil([&outcome] { return outcome.has_value(); }));
    CHECK(outcome->code == u"GS-MCP-022");
}

TEST_CASE("a component that comes with an extension cannot be placed; it is copied to the user's components first "
          "(ADR-0105)")
{
    gs::test::McpApplication application;
    QTemporaryDir mine;
    REQUIRE(mine.isValid());
    const std::filesystem::path examples = std::filesystem::path(GS_EXAMPLE_COMPONENTS_DIR);
    ComponentLibrary library(examples, mine.path().toStdWString());
    const QString ref = extensionComponentRef(QStringLiteral("alice.helper"), QStringLiteral("turns"));
    library.setExtensionComponents({ExtensionComponent{.ref = ref,
                                                       .name = QStringLiteral("turns"),
                                                       .extensionId = QStringLiteral("alice.helper"),
                                                       .extensionName = QStringLiteral("Helper"),
                                                       .folder = examples / "in-turn"}});
    const ComponentEntry* entry = library.find(ref);
    REQUIRE(entry != nullptr);
    CHECK(entry->extensionId == u"alice.helper");

    ProfileEditor editor(input::Profile{}, input::ScreenGeometry{.width = 2400, .height = 1080});
    QStringList problems;
    QObject::connect(&editor, &ProfileEditor::logicProblem, [&problems](const QString& text) { problems.append(text); });
    editor.setLogic(QString(), QString(), [](QString&) { return QStringLiteral("C:/profiles/mine"); });
    CHECK(editor.addComponent(QStringLiteral("component:") + ref, 0.5, 0.5).isEmpty());
    CHECK(problems.size() == 1);
    CHECK(editor.profile().controls.empty());

    const QString copied = library.copyToMine(ref);
    REQUIRE_FALSE(copied.isEmpty());
    CHECK(copied.startsWith(QStringLiteral("mine/")));
    REQUIRE(library.find(copied) != nullptr);
    CHECK(library.find(copied)->extensionId.isEmpty());
    CHECK_FALSE(editor.addComponent(QStringLiteral("component:") + copied, 0.5, 0.5).isEmpty());
    CHECK(editor.profile().controls.size() == 1);
}

TEST_CASE("ADR-0106: an extension's questions reach the prompt flow with its name, and an app script asks in the "
          "mirror window last in front or is answered as cancelled when there is none")
{
    Setup setup;
    Folder store;
    Folder folder;
    folder.write(QStringLiteral("app.ts"),
                 "ui.choose(\"Mode\", [\"Easy\", \"Hard\"]).then((mode) => log.info(\"mode\", mode));\n");
    folder.write(QStringLiteral("session.ts"), R"(ui.confirm("Sell the old gear?").then((sell) => {
    log.info("sell", sell);
    return ui.ask("Team name");
}).then((team) => log.info("team", team));
)");
    Reports reports;
    ExtensionLogic logic(store.path());
    reports.watch(logic);
    Seen seen(logic);
    const ExtensionScript app = contributed(folder, QStringLiteral("tools"), "app.ts", true, {});
    logic.setScripts({app});
    REQUIRE(setup.rig.until([&] { return reports.has(QStringLiteral("|mode null")); }));
    CHECK(seen.prompts.empty());

    logic.setFrontPhone([] { return QStringLiteral("phone-1"); });
    logic.setScripts({});
    logic.setScripts({app});
    REQUIRE(setup.rig.until([&] { return seen.prompts.size() == 1; }));
    const Shown mode = seen.prompts[0];
    CHECK(mode.serial == u"phone-1");
    CHECK(mode.value.value(QStringLiteral("kind")).toString() == u"choose");
    CHECK(mode.value.value(QStringLiteral("extension")).toString() == u"Helper");
    CHECK(logic.answerPrompt(mode.id, QStringLiteral("1")));
    REQUIRE(setup.rig.until([&] { return reports.has(QStringLiteral("|mode 1")); }));
    CHECK_FALSE(logic.answerPrompt(mode.id, QStringLiteral("0")));

    logic.setScripts({app, contributed(folder, QStringLiteral("watcher"), "session.ts", false, {})});
    logic.startSession(sessionPhone(setup));
    REQUIRE(setup.rig.until([&] { return seen.prompts.size() == 2; }));
    const Shown sell = seen.prompts[1];
    CHECK(sell.serial == u"phone-1");
    CHECK(sell.id != mode.id);
    CHECK(sell.value.value(QStringLiteral("kind")).toString() == u"confirm");
    CHECK(sell.value.value(QStringLiteral("question")).toString() == u"Sell the old gear?");
    CHECK(sell.value.value(QStringLiteral("extension")).toString() == u"Helper");
    CHECK(logic.answerPrompt(sell.id, QStringLiteral("true")));
    REQUIRE(setup.rig.until([&] { return reports.has(QStringLiteral("phone-1|sell true")) && seen.prompts.size() == 3; }));
    const Shown team = seen.prompts[2];
    CHECK(team.value.value(QStringLiteral("kind")).toString() == u"ask");
    CHECK(seen.withdrawn.empty());

    logic.endSession(QStringLiteral("phone-1"));
    REQUIRE(seen.withdrawn.size() == 1);
    CHECK(seen.withdrawn[0].serial == u"phone-1");
    CHECK(seen.withdrawn[0].id == team.id);
    CHECK_FALSE(logic.answerPrompt(team.id, QStringLiteral("\"Blue\"")));
}

TEST_CASE("ADR-0106: boxes and text an extension draws on the picture carry its name and go away with its logic")
{
    Setup setup;
    Folder store;
    Folder folder;
    folder.write(QStringLiteral("session.ts"), R"(const chest = ui.box(0.1, 0.2, 0.3, 0.4, { color: "good", label: "chest" });
ui.text(0.05, 0.05, "Round 2");
ui.box(0.5, 0.5, 0.2, 0.2);
chest.dispose();
log.info("drawn");
)");
    Reports reports;
    ExtensionLogic logic(store.path());
    reports.watch(logic);
    Seen seen(logic);
    logic.setScripts({contributed(folder, QStringLiteral("watcher"), "session.ts", false, {})});
    logic.startSession(sessionPhone(setup));
    REQUIRE(setup.rig.until([&] {
        return reports.has(QStringLiteral("phone-1|drawn")) && seen.drawings.size() == 3 && seen.erased.size() == 1;
    }));
    for (const Shown& drawing : seen.drawings) {
        CHECK(drawing.serial == u"phone-1");
        CHECK(drawing.value.value(QStringLiteral("extension")).toString() == u"Helper");
    }
    CHECK(seen.drawings[0].value.value(QStringLiteral("kind")).toString() == u"box");
    CHECK(seen.drawings[0].value.value(QStringLiteral("text")).toString() == u"chest");
    CHECK(seen.drawings[1].value.value(QStringLiteral("kind")).toString() == u"text");
    CHECK(seen.drawings[1].value.value(QStringLiteral("text")).toString() == u"Round 2");
    CHECK(seen.drawings[2].value.value(QStringLiteral("text")).toString().isEmpty());
    CHECK(seen.drawings[0].id != seen.drawings[1].id);
    CHECK(seen.drawings[1].id != seen.drawings[2].id);
    CHECK(seen.erased[0].id == seen.drawings[0].id);

    logic.endSession(QStringLiteral("phone-1"));
    REQUIRE(seen.erased.size() == 3);
    std::vector<quint64> gone{seen.erased[1].id, seen.erased[2].id};
    std::vector<quint64> left{seen.drawings[1].id, seen.drawings[2].id};
    std::ranges::sort(gone);
    std::ranges::sort(left);
    CHECK(gone == left);
}

TEST_CASE("ADR-0106: an extension reads and writes the phone's clipboard only with the permission device.clipboard")
{
    Setup setup;
    Folder store;
    Folder folder;
    folder.write(QStringLiteral("session.ts"), std::string(kCodeOf) + R"(log.info("clipboard",
    codeOf(() => clipboard.read().then((text) => log.info("pasted", text))),
    codeOf(() => clipboard.write("Room 4421").then((done) => log.info("copied", done))));
)");
    Reports reports;
    QStringList asked;
    ExtensionLogic logic(store.path());
    reports.watch(logic);
    QObject::connect(&logic, &ExtensionLogic::clipboardWanted,
                     [&asked](const QString& serial, ScriptRuntime* runtime, quint64 request) {
                         asked.append(serial + QStringLiteral("|read"));
                         runtime->answer(request, QStringLiteral("\"from the phone\""));
                     });
    QObject::connect(&logic, &ExtensionLogic::clipboardGiven,
                     [&asked](const QString& serial, ScriptRuntime* runtime, quint64 request, const QString& text) {
                         asked.append(serial + QStringLiteral("|write ") + text);
                         runtime->answer(request, QStringLiteral("true"));
                     });
    logic.setScripts({contributed(folder, QStringLiteral("watcher"), "session.ts", false, {})});
    logic.startSession(sessionPhone(setup));
    REQUIRE(setup.rig.until([&] { return reports.has(QStringLiteral("phone-1|clipboard GS-API-072 GS-API-072")); }));
    setup.rig.settle(200ms);
    CHECK(asked.isEmpty());

    logic.setScripts({contributed(folder, QStringLiteral("watcher"), "session.ts", false, {"device.clipboard"})});
    REQUIRE(setup.rig.until([&] {
        return reports.has(QStringLiteral("phone-1|pasted from the phone")) &&
               reports.has(QStringLiteral("phone-1|copied true"));
    }));
    CHECK(reports.has(QStringLiteral("phone-1|clipboard none none")));
    CHECK(asked == QStringList{QStringLiteral("phone-1|read"), QStringLiteral("phone-1|write Room 4421")});
}

TEST_CASE("ADR-0106: profile.save from an extension asks with its name, saves only when the card is confirmed and "
          "answers false when it is cancelled")
{
    Setup setup;
    Folder store;
    Folder folder;
    folder.write(QStringLiteral("session.ts"), R"(controls.jump.moveTo(0.5, 0.5);
profile.save().then((saved) => {
    log.info("saved", saved);
    return profile.save();
}).then((again) => log.info("again", again));
)");
    Reports reports;
    std::vector<std::pair<QString, input::Profile>> saved;
    ExtensionLogic logic(store.path());
    reports.watch(logic);
    Seen seen(logic);
    logic.setProfileSaver([&saved](const QString& serial, const input::Profile& profile) {
        saved.emplace_back(serial, profile);
        return true;
    });
    logic.setScripts({contributed(folder, QStringLiteral("watcher"), "session.ts", false, {"mapping"})});
    logic.startSession(sessionPhone(setup));
    REQUIRE(setup.rig.until([&] { return seen.saves.size() == 1; }));
    const Shown first = seen.saves[0];
    CHECK(first.serial == u"phone-1");
    CHECK(first.value.value(QStringLiteral("kind")).toString() == u"save");
    CHECK(first.value.value(QStringLiteral("extension")).toString() == u"Helper");
    const QVariantList changes = first.value.value(QStringLiteral("changes")).toList();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].toMap().value(QStringLiteral("name")).toString() == u"jump");
    CHECK(changes[0].toMap().value(QStringLiteral("prop")).toString() == u"position");
    CHECK(seen.prompts.empty());
    CHECK(saved.empty());

    CHECK(logic.answerPrompt(first.id, QStringLiteral("true")));
    REQUIRE(setup.rig.until([&] { return reports.has(QStringLiteral("phone-1|saved true")) && seen.saves.size() == 2; }));
    REQUIRE(saved.size() == 1);
    CHECK(saved[0].first == u"phone-1");
    REQUIRE(saved[0].second.controls.size() == 1);
    CHECK_FALSE(saved[0].second.controls[0] == setup.profile->controls[0]);

    CHECK(logic.cancelPrompt(seen.saves[1].id));
    REQUIRE(setup.rig.until([&] { return reports.has(QStringLiteral("phone-1|again false")); }));
    CHECK(saved.size() == 1);
}

TEST_CASE("ADR-0106: an extension's app script starts again a second after its sandbox stops, while a session script "
          "stays stopped")
{
    Setup setup;
    Folder store;
    Folder folder;
    folder.write(QStringLiteral("app.ts"), "log.info(\"app up\");\n");
    folder.write(QStringLiteral("session.ts"), "log.info(\"session up\");\n");
    Reports reports;
    ExtensionLogic logic(store.path());
    reports.watch(logic);
    logic.setScripts({contributed(folder, QStringLiteral("tools"), "app.ts", true, {}),
                      contributed(folder, QStringLiteral("watcher"), "session.ts", false, {})});
    logic.startSession(sessionPhone(setup));
    REQUIRE(setup.rig.until([&] {
        return reports.count(QStringLiteral("|app up")) == 1 && reports.count(QStringLiteral("phone-1|session up")) == 1;
    }));

    REQUIRE(stopSandboxes() >= 2);
    REQUIRE(setup.rig.until([&] {
        return reports.has(QStringLiteral("|") + stoppedText()) && reports.has(QStringLiteral("phone-1|") + stoppedText());
    }));
    REQUIRE(setup.rig.until([&] { return reports.count(QStringLiteral("|app up")) == 2; }));
    CHECK(logic.appRuntimes().size() == 1);
    CHECK(runningLogic(QStringLiteral("tools"), QString()));
    setup.rig.settle(1500ms);
    CHECK(reports.count(QStringLiteral("phone-1|session up")) == 1);
    REQUIRE(logic.sessionRuntimes(QStringLiteral("phone-1")).size() == 1);
    CHECK(logic.sessionRuntimes(QStringLiteral("phone-1")).front()->halted());
}

TEST_CASE("ADR-0106: an app script that stops three times within a minute turns its extension off, while stops "
          "further apart only start it again")
{
    Setup setup;
    Folder store;
    Folder folder;
    folder.write(QStringLiteral("app.ts"), "log.info(\"up\");\n");
    Reports reports;
    std::vector<std::pair<QString, QString>> turnedOff;
    gs::ManualClock clock;
    ExtensionLogic logic(store.path());
    logic.setClock(&clock);
    reports.watch(logic);
    QObject::connect(&logic, &ExtensionLogic::stoppedWorking,
                     [&turnedOff](const QString& id, const QString& name) { turnedOff.emplace_back(id, name); });
    const std::vector<ExtensionScript> scripts{contributed(folder, QStringLiteral("tools"), "app.ts", true, {})};
    logic.setScripts(scripts);
    const auto stopAfter = [&](qsizetype ups) {
        REQUIRE(setup.rig.until([&] { return reports.count(QStringLiteral("|up")) == ups; }));
        REQUIRE(logic.appRuntimes().size() == 1);
        emit logic.appRuntimes().front()->reported(LogicReport{
            .kind = gs::script::ReportKind::ScriptFailed, .level = gs::log::Level::Error, .text = stoppedText()});
    };
    stopAfter(1);
    clock.advance(61s);
    stopAfter(2);
    stopAfter(3);
    REQUIRE(setup.rig.until([&] { return reports.count(QStringLiteral("|up")) == 4; }));
    CHECK(turnedOff.empty());

    stopAfter(4);
    REQUIRE(setup.rig.until([&] { return !turnedOff.empty(); }));
    CHECK(turnedOff ==
          std::vector<std::pair<QString, QString>>{{QStringLiteral("alice.helper"), QStringLiteral("Helper")}});
    CHECK(logic.appRuntimes().empty());
    setup.rig.settle(1500ms);
    CHECK(logic.appRuntimes().empty());
    CHECK(reports.count(QStringLiteral("|up")) == 4);
    CHECK_FALSE(runningLogic(QStringLiteral("tools"), QString()));

    logic.setScripts({});
    logic.setScripts(scripts);
    REQUIRE(setup.rig.until([&] { return reports.count(QStringLiteral("|up")) == 5; }));
    CHECK(turnedOff.size() == 1);
}
